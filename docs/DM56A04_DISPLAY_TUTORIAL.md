# Tutorial: DM56A04 7-Segment RS485 Display via ST Logic

**Dato:** 2026-03-24
**Firmware:** v7.2.0 (Build #1540)
**Formål:** Opsætning og test af DM56A04 4×7-segment Modbus RTU display styret fra ESP32 via ST Logic og REST API.

---

## 1. OVERSIGT

### 1.1 Hvad vi opnår

Et ST Logic program der tæller fra 0 til 9999 og skriver værdien til et DM56A04 RS485 Modbus display hvert sekund. Hele konfigurationen sker via REST API.

### 1.2 Hardware

| Komponent | Beskrivelse |
|-----------|-------------|
| **ESP32** | ES32D26 board med RS485 transceiver, IP 10.1.1.30 |
| **DM56A04** | 4-digit 7-segment RS485 Modbus RTU display |
| **RS485 bus** | Shared bus, ESP32 er master, display er slave |

### 1.3 DM56A04 Modbus Register Map

| Register | Adresse | Funktion |
|----------|---------|----------|
| Digit 1 (tusinder) | 0x0000 | ASCII-kode for enkelt ciffer |
| Digit 2 (hundreder) | 0x0001 | ASCII-kode for enkelt ciffer |
| Digit 3 (tiere) | 0x0002 | ASCII-kode for enkelt ciffer |
| Digit 4 (enere) | 0x0003 | ASCII-kode for enkelt ciffer |
| Numerisk display | **0x0007** | Heltalsværdi 0–65535 (auto decimal) |
| Blink kontrol | 0x0008 | Bitmask for blinkende cifre |
| Lysstyrke | 0x0009 | 1–8 (1=dimmest, 8=max) |
| Slave adresse | 0x00FD | Ændring af Modbus adresse |
| Baud rate | 0x00FE | Ændring af kommunikationshastighed |

**Standard fabriksindstillinger:** Slave ID=1, 9600 baud, 8N1.

### 1.4 Dataflow

```
┌──────────────────────────────────────────────────────┐
│  ST Logic Program (kører hvert 10ms)                  │
│                                                       │
│  VAR                                                  │
│    counter : INT   ── intern tæller 0-9999            │
│    ok      : BOOL  ── Modbus write resultat           │
│    delay   : INT   ── forsinkelsestæller              │
│  END_VAR                                              │
│                                                       │
│  Logik: delay++ → ved 100 (=1s) → counter++ →        │
│         MB_WRITE_HOLDING(1, 7, counter) → nulstil     │
└──────────────────────┬────────────────────────────────┘
                       │
                       ▼ RS485 Modbus RTU
              ┌────────────────────┐
              │  FC06 Write Single │
              │  Slave=1, Reg=7    │
              │  Value=counter     │
              └────────┬───────────┘
                       │
                       ▼
              ┌────────────────────┐
              │   DM56A04 Display  │
              │   Viser: 0-9999   │
              └────────────────────┘
```

---

## 2. OPSÆTNING VIA REST API

Alle API-kald bruger HTTP Basic Auth: `api_user` / `ChangeMe123!`.

### 2.1 Aktiver Modbus Master

ESP32 skal konfigureres som Modbus Master for at kunne skrive til displayet.

```bash
curl -s -u api_user:'ChangeMe123!' \
  -X POST http://10.1.1.30/api/modbus/master \
  -H "Content-Type: application/json" \
  -d '{"enabled": true, "baudrate": 9600, "parity": "none", "stop_bits": 1}'
```

**Forventet svar:**
```json
{"status": "ok", "message": "Modbus master configuration updated"}
```

### 2.2 Upload ST Logic Program

ST-programmet tæller op med 1 sekunds interval og skriver til displayets register 7.

**ST Kildekode:**
```iecst
PROGRAM display_count
VAR
  counter : INT;
  ok      : BOOL;
  delay   : INT;
END_VAR

delay := delay + 1;
IF delay >= 100 THEN
  delay := 0;
  counter := counter + 1;
  IF counter > 9999 THEN
    counter := 0;
  END_IF;
  ok := MB_WRITE_HOLDING(1, 7, counter);
END_IF;
END_PROGRAM
```

**Upload via API (Python — anbefalet pga JSON escaping):**
```python
import requests, json

source = """PROGRAM display_count
VAR
  counter : INT;
  ok      : BOOL;
  delay   : INT;
END_VAR

delay := delay + 1;
IF delay >= 100 THEN
  delay := 0;
  counter := counter + 1;
  IF counter > 9999 THEN
    counter := 0;
  END_IF;
  ok := MB_WRITE_HOLDING(1, 7, counter);
END_IF;
END_PROGRAM"""

r = requests.post(
    "http://10.1.1.30/api/logic/1/source",
    auth=("api_user", "ChangeMe123!"),
    json={"source": source}
)
print(r.json())
```

**Upload via curl (alternativ):**
```bash
curl -s -u api_user:'ChangeMe123!' \
  -X POST http://10.1.1.30/api/logic/1/source \
  -H "Content-Type: application/json" \
  -d '{"source": "PROGRAM display_count\nVAR\n  counter : INT;\n  ok : BOOL;\n  delay : INT;\nEND_VAR\n\ndelay := delay + 1;\nIF delay >= 100 THEN\n  delay := 0;\n  counter := counter + 1;\n  IF counter > 9999 THEN\n    counter := 0;\n  END_IF;\n  ok := MB_WRITE_HOLDING(1, 7, counter);\nEND_IF;\nEND_PROGRAM"}'
```

**Forventet svar:**
```json
{"status": "ok", "message": "Logic1 compiled successfully", "instructions": 28, "size": 224}
```

### 2.3 Aktiver Logic Program

```bash
curl -s -u api_user:'ChangeMe123!' \
  -X POST http://10.1.1.30/api/logic/1/enable
```

**Forventet svar:**
```json
{"status": "ok", "message": "Logic1 enabled"}
```

### 2.4 Verificer Status

```bash
curl -s -u api_user:'ChangeMe123!' \
  http://10.1.1.30/api/logic/1 | python -m json.tool
```

**Forventet svar (efter et par sekunder):**
```json
{
  "id": 1,
  "enabled": true,
  "state": "running",
  "cycle_count": 523,
  "variables": {
    "counter": 5,
    "ok": 1,
    "delay": 23
  }
}
```

---

## 3. KODE FORKLARING

### 3.1 MB_WRITE_HOLDING(slave_id, register, value)

```iecst
ok := MB_WRITE_HOLDING(1, 7, counter);
```

| Parameter | Værdi | Betydning |
|-----------|-------|-----------|
| `slave_id` | 1 | DM56A04 Modbus slave adresse |
| `register` | 7 | Register 0x0007 = numerisk display |
| `value` | counter | Heltalsværdi 0–9999 |
| **Retur** | `ok` | `TRUE` = skrivning lykkedes, `FALSE` = fejl |

Funktionen sender Modbus FC06 (Write Single Holding Register) over RS485.

### 3.2 Timing

- ST Logic kører hvert **10ms** (100 cykler/sekund)
- `delay` tæller til 100 → udløser hvert **1 sekund**
- Display opdateres med ny `counter`-værdi hvert sekund

### 3.3 Wrap-around

```iecst
IF counter > 9999 THEN
  counter := 0;
END_IF;
```

DM56A04 har 4 cifre → max 9999. Tælleren nulstilles ved overflow.

---

## 4. TEST RESULTATER

**Test udført:** 2026-03-24

### 4.1 Resultat

| Test | Status | Bemærkning |
|------|--------|------------|
| Modbus Master aktivering | ✅ OK | `POST /api/modbus/master` med `enabled: true` |
| ST program kompilering | ✅ OK | 28 instructions, 224 bytes |
| ST program kørsel | ✅ OK | Stabil kørsel, ingen crashes |
| Display modtager data | ✅ OK | FC06 echo korrekt fra slave ID 1 |
| Display viser tal | ✅ OK | Tæller op 0→1→2→...→9999→0 |
| Wrap-around 9999→0 | ✅ OK | Korrekt nulstilling |
| MB_WRITE_HOLDING retur | ✅ OK | `ok=TRUE` ved succesfuld skrivning |

### 4.2 Observerede problemer under test

| Problem | Årsag | Løsning |
|---------|-------|---------|
| `MB_WRITE_HOLDING` returnerede altid `FALSE` | BUG-239: Runtime config ikke synkroniseret fra NVS ved boot | Fix: Sync i `modbus_master_init()`, workaround: `POST /api/modbus/master {"enabled":true}` |
| Større ST-programmer fejlede med "Memory allocation failed" | BUG-240: AST-noder var ~1920 bytes hver | Fix: Reduceret til ~140 bytes per node |

### 4.3 Prometheus Metrics (efter fix)

```
modbus_master_requests_total: 523
modbus_master_success_total: 523
modbus_master_timeout_total: 0
modbus_master_crc_error_total: 0
```

100% success rate efter BUG-239 fix.

---

## 5. VARIATIONER

### 5.1 Fast display-værdi (ingen tæller)

Skriv en statisk værdi til displayet:

```iecst
PROGRAM display_static
VAR ok : BOOL; END_VAR
ok := MB_WRITE_HOLDING(1, 7, 1234);
END_PROGRAM
```

### 5.2 Lysstyrke kontrol

Sæt lysstyrke til max (register 0x0009, værdi 1–8):

```iecst
ok := MB_WRITE_HOLDING(1, 9, 8);
```

### 5.3 Blink kontrol

Aktiver blink på alle cifre (register 0x0008):

```iecst
ok := MB_WRITE_HOLDING(1, 8, 15);  (* bitmask: alle 4 cifre *)
```

### 5.4 Individuelle ASCII-cifre

Skriv "H" til ciffer 1 (register 0x0000, ASCII 72):

```iecst
ok := MB_WRITE_HOLDING(1, 0, 72);  (* ASCII 'H' = 72 *)
```

---

## 6. FEJLFINDING

| Symptom | Mulig årsag | Løsning |
|---------|-------------|---------|
| `ok` er altid `FALSE` | Modbus Master ikke aktiveret | `POST /api/modbus/master {"enabled":true}` |
| Display viser intet | Forkert slave ID eller baud rate | Tjek DM56A04 DIP-switches, default: ID=1, 9600 baud |
| Display viser forkert tal | Forkert register | Brug register 7 (0x0007) for numerisk visning |
| "Memory allocation failed" ved kompilering | AST node memory bug | Opdater til firmware v7.2.0+ (BUG-240 fix) |
| Timeout fejl | RS485 kabling | Tjek A/B polaritet, termineringsmodstand |

---

## 7. REFERENCER

- [DM56A04 Modbus RTU Manual](DM56A04%20DM36B06%20MODBUS%20RTU%20Command.pdf) — Register map og kommandoer
- [ST Logic Usage Guide](ST_USAGE_GUIDE.md) — Komplet ST syntax reference
- [REST API Reference](REST_API.md) — Alle API endpoints
- [API Hello World Guide](API_HELLO_WORLD_GUIDE.md) — Grundlæggende API opsætning
- [BUG-239](../BUGS_INDEX.md) — Modbus Master config sync
- [BUG-240](../BUGS_INDEX.md) — AST node memory reduktion
