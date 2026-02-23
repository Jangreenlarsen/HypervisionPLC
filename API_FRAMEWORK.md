# API Framework & Design Standard
## ESP32 Modbus RTU Server - REST API Reference

**Version:** 1.0
**Dato:** 2026-02-02
**Formål:** Denne fil er den autoritative reference for API design. ALLE fremtidige API-ændringer SKAL overholde dette dokument.

---

## 1. PARITET: API vs CLI (GAP-ANALYSE)

### 1.1 Hvad CLI kan som API IKKE kan

| # | CLI Kommando | Beskrivelse | API Status | Prioritet |
|---|-------------|-------------|------------|-----------|
| GAP-1 | `set counter <id> mode 1 parameter ...` | Konfigurér counter | **MANGLER** | HØJ |
| GAP-2 | `set counter <id> control ...` | Counter control flags (auto-start, reset-on-read) | **MANGLER** | HØJ |
| GAP-3 | `set timer <id> mode <1-4> parameter ...` | Konfigurér timer | **MANGLER** | HØJ |
| GAP-4 | `set modbus slave ...` | Modbus slave konfiguration (slave-id, baud, parity, etc.) | **MANGLER** | MEDIUM |
| GAP-5 | `set modbus master ...` | Modbus master konfiguration | **MANGLER** | MEDIUM |
| GAP-6 | `set wifi ssid/password/dhcp/ip/...` | WiFi konfiguration | **MANGLER** | MEDIUM |
| GAP-7 | `set http enable/port/auth/tls/...` | HTTP server konfiguration | **MANGLER** | MEDIUM |
| GAP-8 | `set holding-reg STATIC <addr> Value ...` | Sæt statisk register med type | **DELVIST** (POST hr skriver værdi, men ingen type support) | HØJ |
| GAP-9 | `set holding-reg DYNAMIC <addr> counter:func` | Map register til counter/timer | **MANGLER** | LAV |
| GAP-10 | `set coil STATIC/DYNAMIC` | Konfigurér coil mappings | **DELVIST** (POST coil skriver, men ikke STATIC/DYNAMIC) | LAV |
| GAP-11 | `set gpio <pin> input/coil <idx>` | Konfigurér GPIO mappings | **MANGLER** (POST gpio kan kun skrive værdi) | MEDIUM |
| GAP-12 | `set persist group ...` | Persistence gruppe konfiguration | **MANGLER** | LAV |
| GAP-13 | `set logic <id> bind ...` | ST Logic variable binding | **MANGLER** | HØJ |
| GAP-14 | `set hostname <name>` | System hostname | **MANGLER** | LAV |
| GAP-15 | `set echo on/off` | Terminal echo | N/A (kun relevant for CLI) | - |
| GAP-16 | `delete counter <id>` | Slet counter konfiguration | **MANGLER** | HØJ |
| GAP-17 | `reset counter <id>` + `clear counters` | Reset alle counters | **DELVIST** (kun single reset via POST) | LAV |
| GAP-18 | `show modbus slave/master` | Modbus statistik (requests, CRC errors, etc.) | **MANGLER** | MEDIUM |
| GAP-19 | `show wifi` | Runtime WiFi info (RSSI, MAC, active gateway/DNS) | **DELVIST** (GET /api/status har kun IP + connected) | MEDIUM |
| GAP-20 | `show http` | HTTP server statistik (requests, 2xx/4xx/5xx) | **MANGLER** | LAV |
| GAP-21 | `connect wifi` / `disconnect wifi` | WiFi forbindelsesstyring | **MANGLER** | MEDIUM |
| GAP-22 | `defaults` | Factory reset (uden save) | **OK** - POST /api/system/defaults | ✅ |
| GAP-23 | `save` / `load` | Config persistence | **OK** - POST /api/system/save/load | ✅ |
| GAP-24 | `reboot` | System genstart | **OK** - POST /api/system/reboot | ✅ |
| GAP-25 | `read input-reg <addr>` | Læs input registers | **OK** - GET /api/registers/ir/{addr} | ✅ |
| GAP-26 | `set logic interval:<ms>` | ST Logic execution interval | **MANGLER** | MEDIUM |
| GAP-27 | `show persist` | Persistence system status | **MANGLER** | LAV |
| GAP-28 | `set module counters/timers/st_logic enable/disable` | Module enable/disable flags | **MANGLER** | MEDIUM |

### 1.2 Opsummering

| Kategori | CLI Kommandoer | API Dækket | API Mangler | Dækning |
|----------|---------------|------------|-------------|---------|
| **Counter config** | set counter, control, reset, delete | reset, start, stop | config, control, delete | 40% |
| **Timer config** | set timer | - | alt | 0% |
| **Modbus config** | set modbus slave/master (12 params) | - | alt | 0% |
| **WiFi config** | set wifi (11 params) + connect/disconnect | - | alt | 0% |
| **HTTP config** | set http (7 params) | - | alt | 0% |
| **Register config** | STATIC/DYNAMIC med typer | simpel write | type support, DYNAMIC | 25% |
| **GPIO config** | input/coil mapping, gpio2 | write value | mapping config | 20% |
| **ST Logic** | upload, enable, bind, interval, delete | upload, enable, delete | bind, interval | 60% |
| **Debug** | set debug flags | GET+POST debug | - | 100% |
| **System** | reboot, save, load, defaults | alle 4 | - | 100% |
| **Læsning (GET)** | show + read commands | alle register typer | modbus stats, http stats, wifi details | 80% |

**Samlet API paritet: ~35%** — API'et dækker primært læsning og simple handlinger, men mangler næsten al konfiguration.

---

## 2. STATUS FELT — KONSISTENT INTEGER FORMAT

### 2.1 Implementeret mønster (v6.0.5+)

`"status"` feltet er **altid en integer** (HTTP status kode) i hele API'et:

| Endpoint Type | `"status"` felt | Format |
|--------------|-----------------|--------|
| **GET endpoints** (18 stk) | **NEJ** — returnerer kun data | `{ "pin": 17, "value": 1, ... }` |
| **POST success** (15 stk) | **JA** — integer `200` | `{ "status": 200, "message": "..." }` |
| **Error responses** | **JA** — integer med fejlkode | `{ "error": "msg", "status": 400 }` |

### 2.2 Standard

**Success response (POST):**
```json
{
  "status": 200,
  "message": "Human-readable beskrivelse",
  ...data fields...
}
```

**Error response (alle):**
```json
{
  "error": "Human-readable fejlbesked",
  "status": 400
}
```

Fejl-responses returnerer også korrekt HTTP status kode i headeren (ikke 200).

---

## 3. API SYNTAX STANDARD (Framework)

### 3.1 URL Struktur

```
/api/{resource}                    → Collection (GET=list, POST=create)
/api/{resource}/{id}               → Instance (GET=read, PUT=update, DELETE=delete)
/api/{resource}/{id}/{action}      → Action (POST=execute)
/api/{resource}/{id}/{sub-resource} → Sub-resource (GET/POST)
```

**Nuværende afvigelser:**
- `/api/registers/hr/{addr}` — OK (sub-resource under registers)
- `/api/logic/{id}/source` — OK (sub-resource)
- `/api/logic/{id}/enable` — OK (action)
- `/api/system/reboot` — OK (system action)

### 3.2 HTTP Metoder

| Metode | Formål | Request Body | Response |
|--------|--------|-------------|----------|
| **GET** | Læs data | Ingen | Data object |
| **POST** | Udfør handling / skriv data | JSON body (kan være tom) | `{ "status": "ok", ... }` |
| **DELETE** | Slet resource | Ingen | `{ "status": "ok", ... }` |

> **PUT** og **PATCH** bruges IKKE i dette API (ESP32 memory constraints).

### 3.3 Request Format

**Headers (påkrævet):**
```
Content-Type: application/json
Authorization: Basic <base64(user:pass)>   (hvis auth enabled)
```

**POST body:**
```json
{
  "field1": "value1",
  "field2": 42
}
```

Tom body er tilladt for simple actions (reset, reboot, enable/disable).

### 3.4 Response Format

#### Success (GET) — Kun data
```json
{
  "field1": "value1",
  "field2": 42
}
```

#### Success (POST/DELETE) — Altid med status
```json
{
  "status": "ok",
  "message": "Counter reset to start value",
  ...eventuelle data felter...
}
```

#### Collection Response (GET list)
```json
{
  "counters": [
    { "id": 1, "enabled": true, "mode": "HW_PCNT", "value": 1234 },
    { "id": 2, "enabled": false }
  ]
}
```

#### Error Response
```json
{
  "error": "Counter 5 not found",
  "code": 404
}
```

### 3.5 Datatyper

| JSON Type | Bruges til | Eksempel |
|-----------|-----------|----------|
| `string` | Tekst, modes, navne | `"mode": "HW_PCNT"` |
| `integer` | Tal, adresser, ms-værdier | `"value": 1234` |
| `number` | Float/REAL værdier | `"scale": 0.5` |
| `boolean` | Flags, on/off states | `"enabled": true` |
| `null` | Ikke tilgængelig / ikke konfigureret | `"ip": null` |
| `array` | Lister af objekter | `"counters": [...]` |
| `object` | Nested konfiguration | `"modbus_slave": {...}` |

### 3.6 Navngivningskonvention

- **snake_case** for alle JSON felter: `slave_id`, `output_coil`, `phase1_ms`
- **lowercase** for URL paths: `/api/counters`, `/api/logic`
- **Singular** for single-resource: `/api/counters/1` (ikke `/api/counter/1`)
- **Plural** for collections: `/api/counters` (ikke `/api/counter`)
- Undgå forkortelser undtagen veletablerede: `hr`, `ir`, `di`, `ms`, `gpio`
- Boolean felter: brug positive navne (`enabled`, ikke `disabled`)

### 3.7 Pagination & Filtrering

Ikke implementeret (ESP32 har få resources: max 4 counters, 4 timers, 4 programs). Alle collections returnerer komplet data.

### 3.8 Caching

Alle responses har:
```
Cache-Control: no-store, no-cache, must-revalidate
```
ESP32 data ændres i realtid — caching er ikke relevant.

---

## 4. KOMPLET ENDPOINT OVERSIGT (Nuværende + Planlagte)

### 4.1 Eksisterende Endpoints (33 stk)

#### System
| Metode | Path | Beskrivelse | Status felt |
|--------|------|-------------|-------------|
| GET | `/api` | API discovery & endpoint liste | Nej |
| GET | `/api/status` | System status (version, uptime, heap) | Nej |
| GET | `/api/config` | Fuld konfiguration (read-only) | Nej |
| POST | `/api/system/reboot` | Genstart ESP32 | Ja |
| POST | `/api/system/save` | Gem config til NVS | Ja |
| POST | `/api/system/load` | Indlæs config fra NVS | Ja |
| POST | `/api/system/defaults` | Reset til fabriksindstillinger | Ja |

#### Counters
| Metode | Path | Beskrivelse | Status felt |
|--------|------|-------------|-------------|
| GET | `/api/counters` | Alle counters (summary) | Nej |
| GET | `/api/counters/{id}` | Enkelt counter (detaljer) | Nej |
| POST | `/api/counters/{id}/reset` | Reset counter | Ja |
| POST | `/api/counters/{id}/start` | Start counter | Ja |
| POST | `/api/counters/{id}/stop` | Stop counter | Ja |

#### Timers
| Metode | Path | Beskrivelse | Status felt |
|--------|------|-------------|-------------|
| GET | `/api/timers` | Alle timers (summary) | Nej |
| GET | `/api/timers/{id}` | Enkelt timer (detaljer) | Nej |

#### Registers
| Metode | Path | Beskrivelse | Status felt |
|--------|------|-------------|-------------|
| GET | `/api/registers/hr/{addr}` | Læs holding register | Nej |
| POST | `/api/registers/hr/{addr}` | Skriv holding register | Ja |
| GET | `/api/registers/ir/{addr}` | Læs input register | Nej |
| GET | `/api/registers/coils/{addr}` | Læs coil | Nej |
| POST | `/api/registers/coils/{addr}` | Skriv coil | Ja |
| GET | `/api/registers/di/{addr}` | Læs discrete input | Nej |

#### GPIO
| Metode | Path | Beskrivelse | Status felt |
|--------|------|-------------|-------------|
| GET | `/api/gpio` | Alle GPIO mappings | Nej |
| GET | `/api/gpio/{pin}` | Enkelt GPIO status | Nej |
| POST | `/api/gpio/{pin}` | Skriv GPIO værdi | Ja |

#### ST Logic
| Metode | Path | Beskrivelse | Status felt |
|--------|------|-------------|-------------|
| GET | `/api/logic` | Alle programmer | Nej |
| GET | `/api/logic/{id}` | Program detaljer + variabler | Nej |
| GET | `/api/logic/{id}/source` | Download ST kildekode | Nej |
| POST | `/api/logic/{id}/source` | Upload ST kildekode | Ja |
| POST | `/api/logic/{id}/enable` | Aktivér program | Ja |
| POST | `/api/logic/{id}/disable` | Deaktivér program | Ja |
| DELETE | `/api/logic/{id}` | Slet program | Ja |
| GET | `/api/logic/{id}/stats` | Performance statistik | Nej |

#### Debug
| Metode | Path | Beskrivelse | Status felt |
|--------|------|-------------|-------------|
| GET | `/api/debug` | Læs debug flags | Nej |
| POST | `/api/debug` | Sæt debug flags | Ja |

### 4.2 Manglende Endpoints (Prioriteret Roadmap)

#### Fase 1 — HØJ prioritet (Counter/Timer konfiguration)

| Metode | Path | CLI Equivalent | Beskrivelse |
|--------|------|---------------|-------------|
| POST | `/api/counters/{id}` | `set counter <id> mode 1 parameter ...` | Konfigurér counter |
| DELETE | `/api/counters/{id}` | `delete counter <id>` | Slet counter |
| POST | `/api/counters/{id}/control` | `set counter <id> control ...` | Counter control flags |
| POST | `/api/timers/{id}` | `set timer <id> mode <n> parameter ...` | Konfigurér timer |
| DELETE | `/api/timers/{id}` | (implicit: set enabled off) | Slet timer |
| POST | `/api/timers/{id}/start` | (via ctrl-reg) | Start timer |
| POST | `/api/timers/{id}/stop` | (via ctrl-reg) | Stop timer |

**POST /api/counters/{id} body eksempel:**
```json
{
  "enabled": true,
  "hw_mode": "hw",
  "edge": "rising",
  "prescaler": 1,
  "hw_gpio": 19,
  "bit_width": 32,
  "direction": "up",
  "start_value": 0,
  "debounce": true,
  "debounce_ms": 10,
  "compare": {
    "enabled": true,
    "value": 5000,
    "mode": 0,
    "source": 0
  }
}
```

**POST /api/timers/{id} body eksempel:**
```json
{
  "enabled": true,
  "mode": "ASTABLE",
  "output_coil": 100,
  "on_ms": 500,
  "off_ms": 500
}
```

#### Fase 2 — MEDIUM prioritet (Network/Modbus konfiguration)

| Metode | Path | CLI Equivalent | Beskrivelse |
|--------|------|---------------|-------------|
| GET | `/api/modbus/slave` | `show modbus slave` | Modbus slave status + stats |
| POST | `/api/modbus/slave` | `set modbus slave ...` | Konfigurér slave |
| GET | `/api/modbus/master` | `show modbus master` | Modbus master status + stats |
| POST | `/api/modbus/master` | `set modbus master ...` | Konfigurér master |
| GET | `/api/wifi` | `show wifi` | WiFi status (RSSI, MAC, etc.) |
| POST | `/api/wifi` | `set wifi ...` | WiFi konfiguration |
| POST | `/api/wifi/connect` | `connect wifi` | Forbind til WiFi |
| POST | `/api/wifi/disconnect` | `disconnect wifi` | Afbryd WiFi |
| GET | `/api/http` | `show http` | HTTP server stats |
| POST | `/api/http` | `set http ...` | HTTP konfiguration |
| POST | `/api/modules` | `set module ...` | Module enable/disable |

**POST /api/modbus/slave body eksempel:**
```json
{
  "enabled": true,
  "slave_id": 1,
  "baudrate": 115200,
  "parity": "none",
  "stop_bits": 1,
  "inter_frame_delay_ms": 5
}
```

#### Fase 3 — LAV prioritet (Avanceret konfiguration)

| Metode | Path | CLI Equivalent | Beskrivelse |
|--------|------|---------------|-------------|
| POST | `/api/logic/{id}/bind` | `set logic <id> bind ...` | Variable binding |
| POST | `/api/logic/settings` | `set logic interval:...` | Logic engine settings |
| POST | `/api/gpio/{pin}/config` | `set gpio <pin> input/coil ...` | GPIO mapping config |
| DELETE | `/api/gpio/{pin}/config` | `no set gpio <pin>` | Fjern GPIO mapping |
| GET | `/api/persist` | `show persist` | Persistence status |
| POST | `/api/persist/groups` | `set persist group ...` | Persist gruppe config |
| POST | `/api/system/hostname` | `set hostname ...` | System hostname |
| GET | `/api/registers/hr/{addr}?type=real` | `read holding-reg <addr> real` | Type-aware register read |

---

## 5. KONSISTENSREGLER FOR NYE ENDPOINTS

### Regel 1: Response Format
- GET: Kun data (intet `"status"` felt)
- POST/DELETE: Altid `"status": "ok"` eller `"status": "error"`
- Error: Altid `"error": "besked"` med HTTP status kode

### Regel 2: URL Navngivning
- Altid lowercase, ingen camelCase i URLs
- Plural for collections: `/api/counters`, `/api/timers`
- Actions som sub-path: `/api/counters/1/reset`, `/api/counters/1/start`
- Konfiguration via POST til resource: `POST /api/counters/1` = opdatér counter 1

### Regel 3: JSON Felter
- Altid snake_case: `slave_id`, `output_coil`, `inter_frame_delay_ms`
- Boolean felter: `true`/`false` (aldrig `0`/`1` eller `"on"`/`"off"`)
- Numeriske felter: brug integers eller floats (aldrig strings)
- Enum-lignende værdier: brug UPPERCASE strings: `"HW_PCNT"`, `"ASTABLE"`, `"ONESHOT"`

### Regel 4: Authentication
- Alle endpoints kræver Basic Auth (når auth er enabled)
- 401 response med `WWW-Authenticate: Basic realm="Modbus ESP32"` header

### Regel 5: Fejlhåndtering
- Altid returnér JSON (aldrig plain text)
- Brug standard HTTP status koder: 200, 400, 401, 404, 500
- Inkludér `"error"` felt med menneskelæsbar besked

### Regel 6: Idempotens
- GET: Altid sikker (ingen side effects, undtagen reset-on-read registre)
- POST med samme body: Bør give samme resultat (idempotent konfiguration)
- DELETE: Idempotent (slet noget der allerede er slettet = 200 OK)

### Regel 7: Validering
- Validér alle input parametre server-side
- Returnér 400 med specifik fejlbesked ved invalid input
- Brug samme valideringsregler som CLI

### Regel 8: Booleans i Input
- Acceptér: `true`, `false` (foretrukket)
- Acceptér også: `1`, `0` (for kompatibilitet)
- Returnér altid: `true` / `false`

---

## 6. VERSIONERING

### Nuværende: Ingen versionering
API'et bruger `/api/...` uden versionsnummer.

### Anbefaling for breaking changes
Hvis `"status"` feltet i error responses ændres fra integer til string (se §2.3), eller andre breaking changes introduceres:

1. Behold `/api/...` som current version
2. Dokumentér ændringen i release notes
3. Overvej `/api/v2/...` kun hvis breaking changes er uundgåelige

> For dette ESP32 projekt er fuld REST API versionering overkill. Brug semver i firmware versionen og dokumentér API-ændringer per build.

---

## 7. REFERENCE: ENDPOINT-TIL-CLI MAPPING

| API Endpoint | CLI Show | CLI Set |
|-------------|----------|---------|
| GET /api/status | `show version` + `show wifi` (delvist) | - |
| GET /api/config | `show config` | - |
| GET /api/counters | `show counters` | - |
| GET /api/counters/{id} | `show counter <id>` | - |
| POST /api/counters/{id}/reset | - | `reset counter <id>` |
| POST /api/counters/{id}/start | - | `set counter <id> control running:on` |
| POST /api/counters/{id}/stop | - | `set counter <id> control running:off` |
| GET /api/timers | `show timers` | - |
| GET /api/timers/{id} | `show timer <id>` | - |
| GET /api/registers/hr/{addr} | `read holding-reg <addr>` | - |
| POST /api/registers/hr/{addr} | - | `set reg <addr> <value>` |
| GET /api/registers/ir/{addr} | `read input-reg <addr>` | - |
| GET /api/registers/coils/{addr} | `read coil <addr>` | - |
| POST /api/registers/coils/{addr} | - | `set coil <idx> <0\|1>` |
| GET /api/registers/di/{addr} | `read input <addr>` | - |
| GET /api/gpio | `show gpio` | - |
| GET /api/gpio/{pin} | `show gpio <pin>` | - |
| POST /api/gpio/{pin} | - | (direkte GPIO write) |
| GET /api/logic | `show config LOGIC` (delvist) | - |
| GET /api/logic/{id} | `show config LOGIC` (delvist) | - |
| GET /api/logic/{id}/source | - | - |
| POST /api/logic/{id}/source | - | `set logic <id> upload "..."` |
| POST /api/logic/{id}/enable | - | `set logic <id> enabled:true` |
| POST /api/logic/{id}/disable | - | `set logic <id> enabled:false` |
| DELETE /api/logic/{id} | - | `set logic <id> delete` |
| GET /api/logic/{id}/stats | `show st-stats` | - |
| GET /api/debug | `show debug` | - |
| POST /api/debug | - | `set debug <flag> on/off` |
| POST /api/system/reboot | - | `reboot` |
| POST /api/system/save | - | `save` |
| POST /api/system/load | - | `load` |
| POST /api/system/defaults | - | `defaults` |

---

## 8. CHECKLIST FOR NYE ENDPOINTS

Brug denne checklist ved implementering af nye API endpoints:

- [ ] URL følger §3.1 struktur (`/api/{resource}/{id}/{action}`)
- [ ] HTTP metode følger §3.2 (GET=læs, POST=handling, DELETE=slet)
- [ ] Response format følger §3.4 (GET=kun data, POST=med status)
- [ ] JSON felter er snake_case (§3.6)
- [ ] Boolean returneres som `true`/`false` (§3.5)
- [ ] Enum værdier er UPPERCASE strings (§3.6)
- [ ] Authentication check med `CHECK_AUTH(req)` macro
- [ ] API enabled check (returnér 403 hvis disabled)
- [ ] `http_server_stat_request()` kaldes i starten
- [ ] Input validering med 400 error response
- [ ] Tilsvarende CLI kommando er dokumenteret (§7)
- [ ] Endpoint tilføjet til `/api` discovery handler
- [ ] Endpoint tilføjet til `show http` CLI output

---

**Sidst opdateret:** 2026-02-02
**Næste review:** Ved næste API endpoint tilføjelse
