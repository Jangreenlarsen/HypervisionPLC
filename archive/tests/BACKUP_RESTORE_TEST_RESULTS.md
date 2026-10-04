# Backup/Restore API Test Resultater

**Dato:** 2026-02-15
**Firmware:** v6.0.7 (Build #1235)
**ESP32 IP:** 10.1.1.201
**Auth:** api_user / ChangeMe123!
**Test metode:** HTTP API kald med curl + automatisk JSON sammenligning

---

## Oversigt

| Test | Resultat | Alvorlighed |
|------|----------|-------------|
| GET /api/system/backup | OK (200, 20.5KB) | - |
| POST /api/system/restore | OK (200) | - |
| Round-trip sammenligning | **FEJLET** | KRITISK |
| Network/WiFi felter | Kosmetiske problemer | LAV |
| IP-adresse format | Integer i stedet for string | MEDIUM |
| Boolean konsistens | Inkonsistent (0/1 vs true/false) | LAV |
| ST Logic source code | **KORRUPT efter restore** | KRITISK |
| var_maps (GPIO+ST bindings) | **TABT efter restore** | KRITISK |
| Modbus master API uoverensstemmelse | Bug i /api/modbus/master | MEDIUM |

---

## KRITISK BUG #1: ST Logic source code korruption i backup

### Problem
`api_handler_system_backup()` linje 3371-3373 behandler source pool pointere som null-terminerede C-strenge, men source pool entries er IKKE null-terminerede (jf. BUG-212 kommentar i st_logic_config.cpp:247).

### Kode (api_handlers.cpp:3371-3373)
```cpp
const char *src = st_logic_get_source_code(st_state, i);
if (src && p->source_size > 0) {
    pr["source"] = src;  // BUG: Behandles som C-string, laeser forbi source_size!
}
```

### Konsekvens
Source pool i hukommelsen:
```
[prog0: 646 bytes][prog1: 1619 bytes][prog2: 1394 bytes][prog3: 1499 bytes][zeros...]
```

Uden null-terminering laeser ArduinoJson forbi hvert programs graense:
- Program 0 backup: 646 + 1619 + 1394 + 1499 = **5158 bytes** (4 programmer sammenflettet)
- Program 1 backup: 1619 + 1394 + 1499 = **4512 bytes** (3 programmer)
- Program 2 backup: 1394 + 1499 = **2893 bytes** (2 programmer)
- Program 3 backup: 1499 bytes (korrekt, zeros fungerer som terminator)

### Restore konsekvens
De oppustede source strings overskrider 8KB pool (ST_LOGIC_POOL_SIZE=8000):
- Program 0: 5158 bytes upload -> pool overflow -> **source TABT (null)**
- Program 1: 4512 bytes upload -> passer (7405 total) -> **korrupt data**
- Program 2: 2893 bytes upload -> pool overflow -> **source TABT (null)**
- Program 3: 1499 bytes upload -> succeeds men **stale data laeses med**

### Verificeret med test
```
Original backup:  prog0=5158, prog1=4512, prog2=2893, prog3=1499 chars
After restore:    prog0=null,  prog1=7405, prog2=null,  prog3=2893 chars
```

### Fix (api_handlers.cpp:3371-3373)
```cpp
const char *src = st_logic_get_source_code(st_state, i);
if (src && p->source_size > 0) {
    // BUG-FIX: Opret null-termineret kopi (source pool er IKKE null-termineret)
    char *src_copy = (char *)malloc(p->source_size + 1);
    if (src_copy) {
        memcpy(src_copy, src, p->source_size);
        src_copy[p->source_size] = '\0';
        pr["source"] = src_copy;
        free(src_copy);
    }
}
```

---

## KRITISK BUG #2: var_maps tabt under restore (st_logic_delete side-effekt)

### Problem
Restore-raekkefoelgen er forkert. var_maps gendannes FOER logic_programs, men `st_logic_delete()` (kaldt under logic_programs restore) sletter var_maps entries.

### Raekkefoelge i restore (api_handlers.cpp:3648-3743)
```
1. Restore var_maps         -> var_map_count = 8 (4 ST + 4 GPIO)
2. Restore logic_programs   -> st_logic_delete(0) sletter ALLE entries med st_program_id=0
                            -> var_map_count = 0 (ALLE TABT!)
3. Save to NVS              -> gemmer tomt var_maps array
```

### Root cause (st_logic_config.cpp:411-415)
```cpp
// st_logic_delete() sletter alle var_map bindings for programmet:
extern PersistConfig g_persist_config;
uint8_t i = 0;
while (i < g_persist_config.var_map_count) {
    if (g_persist_config.var_maps[i].st_program_id == program_id) {
        // Fjerner binding fra var_maps array...
```

GPIO mappings har ogsaa `st_program_id=0`, saa de slettes ogsaa af `st_logic_delete(0)`.

### Verificeret med test
```
Original backup:  8 var_maps (4 ST Logic + 4 GPIO)
After restore:    0 var_maps (ALLE SLETTET)
```

### Fix (api_handlers.cpp)
Flyt var_maps restore-sektionen til EFTER logic_programs restore-sektionen:
```cpp
// 1. Restore logic_programs FOERST (kalder st_logic_delete som rydder var_maps)
// 2. Restore var_maps BAGEFTER (genopretter alle bindings)
```

---

## MEDIUM BUG #3: IP-adresser som raa uint32_t integers

### Problem
Network felter `static_ip`, `static_gateway`, `static_netmask`, `static_dns` gemmes som raa uint32_t vaerdier i ESP32 little-endian byte order.

### Backup JSON
```json
"network": {
    "static_ip": 1677830336,        // = 192.168.1.100
    "static_gateway": 16885952,     // = 192.168.1.1
    "static_netmask": 16777215,     // = 255.255.255.0
    "static_dns": 134744072         // = 8.8.8.8
}
```

### Sammenligning med /api/wifi endpoint
```json
"runtime": {
    "ip": "10.1.1.201",            // Laesbar string format
    "gateway": "10.1.1.1",
    "netmask": "255.255.255.0",
    "dns": "10.1.1.17"
}
```

### Konsekvens
- Backup JSON er IKKE menneske-laesbar for IP felter
- Kan ikke redigeres manuelt
- Platform-afhaengig (little-endian ESP32)
- Round-trip virker korrekt paa samme platform

### Anbefalet fix
Serialiser IP-adresser som strings i backup:
```cpp
// I stedet for:
network["static_ip"] = g_persist_config.network.static_ip;
// Brug:
char ip_str[16];
uint32_t ip = g_persist_config.network.static_ip;
snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
    ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
network["static_ip"] = ip_str;
```

Og i restore: parse string til uint32_t.

---

## LAV BUG #4: Boolean/integer inkonsistens

### Problem
Nogle felter serialiseres som integers (0/1) i stedet for booleans (true/false).

### Beroorte felter
| Felt | Backup vaerdi | Forventet | API Framework krav |
|------|--------------|-----------|-------------------|
| `wifi_power_save` | `0` | `false` | Boolean |
| `remote_echo` | `1` | `true` | Boolean |
| `gpio2_user_mode` | `0` | `false` | Boolean |

### Kode (api_handlers.cpp)
```cpp
// Korrekt (bruger ternary):
network["enabled"] = g_persist_config.network.enabled ? true : false;

// Inkorrekt (direkte uint8_t assignment):
network["wifi_power_save"] = g_persist_config.network.wifi_power_save;
doc["remote_echo"] = g_persist_config.remote_echo;
doc["gpio2_user_mode"] = g_persist_config.gpio2_user_mode;
```

### Fix
```cpp
network["wifi_power_save"] = g_persist_config.network.wifi_power_save ? true : false;
doc["remote_echo"] = g_persist_config.remote_echo ? true : false;
doc["gpio2_user_mode"] = g_persist_config.gpio2_user_mode ? true : false;
```

---

## MEDIUM BUG #5: Modbus Master API uoverensstemmelse

### Problem
`/api/modbus/master` returnerer andre vaerdier end `/api/config` og backup.

| Felt | /api/modbus/master | /api/config & backup |
|------|-------------------|---------------------|
| enabled | `false` | `true` |
| baudrate | `9600` | `115200` |
| timeout_ms | `500` | `1000` |
| max_requests_per_cycle | `10` | `5` |

### Aarsag
`/api/modbus/master` laeser sandsynligvis fra runtime modbus master module state (som har defaults), mens `/api/config` og backup laeser fra `g_persist_config` (den gemte konfiguration).

---

## INFORMATIV: Felter der fungerer korrekt

Foelgende sektioner round-tripper korrekt (ingen forskelle):
- Metadata (backup_version, firmware_version, build, schema_version, hostname)
- Modbus slave konfiguration
- Modbus master konfiguration
- Network basis felter (enabled, ssid, password, dhcp)
- Telnet konfiguration
- HTTP konfiguration
- Alle 4 counter konfigurationer
- Alle 4 timer konfigurationer
- Static registers (tom)
- Dynamic registers (tom)
- Static coils (tom)
- Dynamic coils (tom)
- Persistent register groups

---

## Test kommandoer brugt

```bash
# Backup
curl -u "api_user:ChangeMe123!" http://10.1.1.201/api/system/backup -o backup.json

# Restore
curl -u "api_user:ChangeMe123!" -X POST -H "Content-Type: application/json" \
     -d @backup.json http://10.1.1.201/api/system/restore

# Sammenligning
curl -u "api_user:ChangeMe123!" http://10.1.1.201/api/system/backup -o backup_after.json
python compare.py backup.json backup_after.json
```

---

## Prioriteret fix-raekkefoelge

1. **KRITISK** - Fix ST Logic source backup (null-terminering) - BUG i backup
2. **KRITISK** - Fix var_maps restore raekkefoelge (flyt efter logic_programs) - BUG i restore
3. **MEDIUM** - Konverter IP-adresser til string format i backup/restore
4. **LAV** - Fix boolean konsistens (wifi_power_save, remote_echo, gpio2_user_mode)
5. **MEDIUM** - Undersoog /api/modbus/master endpoint datakilder
