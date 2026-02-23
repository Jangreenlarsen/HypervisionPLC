# API Hello World Guide — ST Logic + GPIO via REST API

**Dato:** 2026-02-23
**Firmware:** v6.0.7 (Build #1246)
**Formål:** Komplet gennemgang af hvordan man opsætter et ST Logic program med GPIO-styring udelukkende via HTTP REST API.

---

## 1. OVERSIGT

### 1.1 Hvad vi opnår

Et ST Logic program der blinker en LED på GPIO 19 med ~1 sekund periode (500ms on, 500ms off). Hele konfigurationen sker via REST API — ingen CLI, ingen serial, ingen fysisk adgang til enheden udover netværk.

### 1.2 Dataflow

```
┌──────────────────────────────────────────────────────────────┐
│  ST Logic Program 1 (kører hvert 10ms)                       │
│                                                              │
│  VAR                                                         │
│    led   : BOOL  ──► bundet til Coil 10 (output)            │
│    ticks : INT       intern tæller (ingen binding)           │
│  END_VAR                                                     │
│                                                              │
│  Logik: ticks++ → ved 50 → toggle led → nulstil ticks       │
└──────────────────────┬───────────────────────────────────────┘
                       │
                       ▼
              ┌────────────────┐
              │    Coil 10     │
              │  (Modbus coil) │
              └───────┬────────┘
                      │
                      ▼
              ┌────────────────┐
              │   GPIO 19      │
              │  (output mode) │
              │  Fysisk pin    │
              └────────────────┘
                      │
                      ▼
                 ┌─────────┐
                 │   LED   │
                 └─────────┘
```

### 1.3 Forudsætninger

| Krav | Værdi |
|------|-------|
| ESP32 firmware | v6.0.7+ med HTTP API aktiveret |
| WiFi | Forbundet, IP tilgængelig |
| HTTP Auth | Aktiveret med brugernavn/password |
| Værktøj | `curl` (eller lignende HTTP-klient) |
| GPIO 19 | Ledig (ikke brugt af Modbus master/slave UART) |

---

## 2. AUTHENTICATION

### 2.1 Credentials

API'et bruger HTTP Basic Authentication:

| Parameter | Værdi |
|-----------|-------|
| Brugernavn | `api_user` |
| Password | `ChangeMe123!` |
| Base64 encoded | `YXBpX3VzZXI6ITIzUGFzc3dvcmQ=` |

### 2.2 Base64 Encoding

```bash
echo -n 'api_user:ChangeMe123!' | base64
# Output: YXBpX3VzZXI6ITIzUGFzc3dvcmQ=
```

### 2.3 Brug i curl

**Metode 1: Eksplicit header (anbefalet)**
```bash
curl -H "Authorization: Basic YXBpX3VzZXI6ITIzUGFzc3dvcmQ="
```

**Metode 2: curl -u flag**
```bash
curl -u 'api_user:ChangeMe123!'
```

> **BEMÆRK:** I dette projekt viste Metode 1 sig at virke korrekt, mens Metode 2 (`-u`) returnerede 401. Det skyldes sandsynligvis specialtegnet `!` i passwordet, som bash shell-expander. Brug altid eksplicit Base64 header.

---

## 3. TRIN-FOR-TRIN IMPLEMENTERING

### Trin 1: Upload ST Logic kildekode

**Formål:** Opretter et ST Logic program (ID 1) med blink-logik. Programmet kompileres automatisk ved upload.

**API Endpoint:** `POST /api/logic/{id}/source`

**Request:**
```bash
curl -s \
  -H "Authorization: Basic YXBpX3VzZXI6ITIzUGFzc3dvcmQ=" \
  -H "Content-Type: application/json" \
  -X POST http://10.1.1.126/api/logic/1/source \
  -d '{
    "source": "VAR\n  led : BOOL := FALSE;\n  ticks : INT := 0;\nEND_VAR\nBEGIN\n  ticks := ticks + 1;\n  IF ticks >= 50 THEN\n    led := NOT led;\n    ticks := 0;\n  END_IF;\nEND"
  }'
```

**ST Kildekode (formateret):**
```iecst
VAR
  led   : BOOL := FALSE;
  ticks : INT  := 0;
END_VAR
BEGIN
  ticks := ticks + 1;
  IF ticks >= 50 THEN
    led := NOT led;
    ticks := 0;
  END_IF;
END
```

**Forklaring af ST-koden:**
- `led` — BOOL variabel der styrer GPIO output (TRUE = HIGH, FALSE = LOW)
- `ticks` — INT tæller der holder styr på antal executions
- Programmet kører hvert 10ms (default interval)
- Ved 50 ticks (50 × 10ms = 500ms) toggles `led` og `ticks` nulstilles
- Resultatet er: 500ms ON, 500ms OFF = 1 sekunds blinkperiode

**Response:**
```json
{
  "status": 200,
  "id": 1,
  "name": "Logic1",
  "compiled": true,
  "source_size": 154
}
```

**Vigtige detaljer:**
- `compiled: true` bekræfter at kildekoden er syntaktisk korrekt
- Hvis `compiled: false` returneres, indeholder response et `error` felt med fejlbesked
- Max kildekode størrelse: 8192 bytes
- Programmet er IKKE aktivt endnu — kun uploadet og kompileret
- Program ID skal være 1-4 (max 4 samtidige programmer)

---

### Trin 2: Bind variabel til Modbus coil

**Formål:** Forbinder ST-variablen `led` til Modbus Coil 10 som output. Når ST-programmet ændrer `led`, skrives værdien automatisk til Coil 10.

**API Endpoint:** `POST /api/logic/{id}/bind`

**Request:**
```bash
curl -s \
  -H "Authorization: Basic YXBpX3VzZXI6ITIzUGFzc3dvcmQ=" \
  -H "Content-Type: application/json" \
  -X POST http://10.1.1.126/api/logic/1/bind \
  -d '{
    "variable": "led",
    "binding": "coil:10",
    "direction": "output"
  }'
```

**Response:**
```json
{
  "status": 200,
  "program": 1,
  "variable": "led",
  "binding": "coil:10",
  "direction": "output",
  "mappings_created": 1,
  "message": "Variable binding created"
}
```

**Forklaring af parametre:**

| Parameter | Værdi | Beskrivelse |
|-----------|-------|-------------|
| `variable` | `"led"` | Navnet på variablen i ST-koden (case-sensitive) |
| `binding` | `"coil:10"` | Mål: Modbus Coil adresse 10 |
| `direction` | `"output"` | ST skriver → Coil (ved slutning af hvert cycle) |

**Mulige binding-typer:**

| Binding syntaks | Beskrivelse | Typisk direction |
|----------------|-------------|------------------|
| `coil:N` | Modbus coil N (BOOL) | `output` eller `input` |
| `reg:N` | Holding Register N (INT/DINT/REAL) | `output` eller `input` |
| `input:N` | Discrete Input N (BOOL, read-only) | `input` |

**Mulige directions:**

| Direction | Betydning |
|-----------|-----------|
| `input` | Modbus → ST variabel (læses ved start af cycle) |
| `output` | ST variabel → Modbus (skrives ved slut af cycle) |
| `both` | Begge retninger (typisk for holding registers) |

**Vigtige detaljer:**
- Programmet skal være kompileret FØR binding (variabelnavne skal eksistere i bytecode)
- Variabelnavnet skal matche præcist (case-sensitive)
- Tidligere bindings for samme variabel overskrives automatisk
- Max 32 variable mappings på tværs af alle programmer
- BOOL variabler binder naturligt til coils, INT/DINT/REAL til holding registers

---

### Trin 3: Konfigurér GPIO 19 som output

**Formål:** Mapper den fysiske GPIO pin 19 til Coil 10. Når Coil 10 er TRUE, sættes GPIO 19 HIGH (3.3V). Når FALSE, sættes GPIO 19 LOW (0V).

**API Endpoint:** `POST /api/gpio/{pin}/config`

**Request:**
```bash
curl -s \
  -H "Authorization: Basic YXBpX3VzZXI6ITIzUGFzc3dvcmQ=" \
  -H "Content-Type: application/json" \
  -X POST http://10.1.1.126/api/gpio/19/config \
  -d '{
    "direction": "output",
    "coil": 10
  }'
```

**Response:**
```json
{
  "status": 200,
  "pin": 19,
  "direction": "output",
  "coil": 10,
  "message": "GPIO mapping configured"
}
```

**Forklaring af parametre:**

| Parameter | Værdi | Beskrivelse |
|-----------|-------|-------------|
| `direction` | `"output"` | GPIO pin konfigureres som digital output |
| `coil` | `10` | GPIO følger værdien af Modbus Coil 10 |

**GPIO som input (alternativt eksempel):**
```json
{
  "direction": "input",
  "register": 5
}
```
> Her ville GPIO-pinnen læses og værdien lagres i Discrete Input register 5.

**Vigtige detaljer:**
- ESP32 GPIO range: 0-39 (ikke alle pins er tilgængelige som output)
- GPIO 19 er en gyldig output pin på ESP32-WROOM-32
- Mapping opdateres i realtid — GPIO-pinnen begynder straks at følge coilen
- Pas på konflikter: GPIO 19 må ikke være i brug af Modbus UART (typisk GPIO 16/17 for master, GPIO 1/3 for slave serial)

---

### Trin 4: Aktivér programmet

**Formål:** Starter execution af ST Logic Program 1. Programmet kører herefter med fast interval (default 10ms).

**API Endpoint:** `POST /api/logic/{id}/enable`

**Request:**
```bash
curl -s \
  -H "Authorization: Basic YXBpX3VzZXI6ITIzUGFzc3dvcmQ=" \
  -H "Content-Type: application/json" \
  -X POST http://10.1.1.126/api/logic/1/enable
```

**Response:**
```json
{
  "status": 200,
  "program": 1,
  "enabled": true,
  "message": "Program enabled"
}
```

**Vigtige detaljer:**
- Programmet skal være kompileret (`compiled: true`) — ellers returneres 400 error
- Execution starter øjeblikkeligt
- Default interval: 10ms (100 executions per sekund)
- Programmet kører i 3 faser per cycle:
  1. **Læs inputs** — Alle `input`-bindings kopieres fra Modbus → ST variabler
  2. **Kør bytecode** — ST-koden eksekveres
  3. **Skriv outputs** — Alle `output`-bindings kopieres fra ST variabler → Modbus

---

### Trin 5: Gem konfiguration

**Formål:** Persisterer hele konfigurationen (inkl. ST-kildekode, bindings, GPIO mappings) til ESP32's Non-Volatile Storage (NVS). Konfigurationen overlever genstart og strømudfald.

**API Endpoint:** `POST /api/system/save`

**Request:**
```bash
curl -s \
  -H "Authorization: Basic YXBpX3VzZXI6ITIzUGFzc3dvcmQ=" \
  -H "Content-Type: application/json" \
  -X POST http://10.1.1.126/api/system/save
```

**Response:**
```json
{
  "status": 200,
  "message": "Configuration saved to NVS"
}
```

**Vigtige detaljer:**
- Uden `save` går konfigurationen tabt ved genstart
- NVS bruger CRC-verificering for dataintegritet
- Alle 5 trin (source, bind, gpio, enable, save) gemmes atomisk
- Ved boot indlæses config automatisk, programmet genstartes

---

## 4. VERIFIKATION

### 4.1 Tjek programstatus

```bash
curl -s \
  -H "Authorization: Basic YXBpX3VzZXI6ITIzUGFzc3dvcmQ=" \
  http://10.1.1.126/api/logic/1
```

**Response:**
```json
{
  "id": 1,
  "name": "Logic1",
  "enabled": true,
  "compiled": true,
  "execution_count": 15733,
  "error_count": 0,
  "last_execution_us": 85,
  "min_execution_us": 64,
  "max_execution_us": 419,
  "overrun_count": 0,
  "variables": [
    { "index": 0, "name": "led",   "type": "BOOL", "value": false },
    { "index": 1, "name": "ticks", "type": "INT",  "value": 33 }
  ]
}
```

**Læsning af response:**

| Felt | Værdi | Betydning |
|------|-------|-----------|
| `enabled` | `true` | Programmet kører aktivt |
| `compiled` | `true` | Kildekoden er korrekt kompileret |
| `execution_count` | `15733` | Antal gange programmet er kørt (stiger ~100/sek) |
| `error_count` | `0` | Ingen runtime-fejl |
| `last_execution_us` | `85` | Sidste cycle tog 85 mikrosekunder |
| `min_execution_us` | `64` | Hurtigste cycle: 64µs |
| `max_execution_us` | `419` | Langsomste cycle: 419µs |
| `overrun_count` | `0` | Programmet har aldrig overskredet sit interval |
| `variables[0]` | `led: false` | LED-variablen toggler (snapshot) |
| `variables[1]` | `ticks: 33` | Tæller er ved 33 af 50 (snapshot) |

### 4.2 Tjek coil-værdi

```bash
curl -s \
  -H "Authorization: Basic YXBpX3VzZXI6ITIzUGFzc3dvcmQ=" \
  http://10.1.1.126/api/registers/coils/10
```

**Response:**
```json
{ "address": 10, "value": false }
```

> Værdien skifter mellem `true` og `false` hvert 500ms.

### 4.3 Tjek GPIO status

```bash
curl -s \
  -H "Authorization: Basic YXBpX3VzZXI6ITIzUGFzc3dvcmQ=" \
  http://10.1.1.126/api/gpio/19
```

**Response:**
```json
{
  "pin": 19,
  "value": 0,
  "configured": true,
  "direction": "output",
  "coil": 10
}
```

| Felt | Betydning |
|------|-----------|
| `configured: true` | GPIO 19 er aktivt konfigureret |
| `direction: "output"` | Pin er sat som digital output |
| `coil: 10` | Følger Modbus Coil 10 |
| `value: 0` | Nuværende pin-tilstand (0=LOW, 1=HIGH) |

### 4.4 Download ST kildekode

```bash
curl -s \
  -H "Authorization: Basic YXBpX3VzZXI6ITIzUGFzc3dvcmQ=" \
  http://10.1.1.126/api/logic/1/source
```

**Response:**
```json
{
  "id": 1,
  "name": "Logic1",
  "source": "VAR\n  led : BOOL := FALSE;\n  ticks : INT := 0;\nEND_VAR\nBEGIN\n  ticks := ticks + 1;\n  IF ticks >= 50 THEN\n    led := NOT led;\n    ticks := 0;\n  END_IF;\nEND",
  "size": 154
}
```

---

## 5. KOMPLET RÆKKEFØLGE (COPY-PASTE READY)

```bash
# ============================================================
# ESP32 ST Logic Hello World — GPIO 19 Blink via REST API
# ============================================================
# Target: 10.1.1.126
# Auth:   api_user / ChangeMe123!
# ============================================================

AUTH="Authorization: Basic YXBpX3VzZXI6ITIzUGFzc3dvcmQ="
HOST="http://10.1.1.126"

# --- Trin 1: Upload ST kildekode ---
echo "=== Trin 1: Upload ST Logic ==="
curl -s -H "$AUTH" -H "Content-Type: application/json" \
  -X POST "$HOST/api/logic/1/source" \
  -d '{"source": "VAR\n  led : BOOL := FALSE;\n  ticks : INT := 0;\nEND_VAR\nBEGIN\n  ticks := ticks + 1;\n  IF ticks >= 50 THEN\n    led := NOT led;\n    ticks := 0;\n  END_IF;\nEND"}'
echo ""

# --- Trin 2: Bind led → Coil 10 ---
echo "=== Trin 2: Bind variabel ==="
curl -s -H "$AUTH" -H "Content-Type: application/json" \
  -X POST "$HOST/api/logic/1/bind" \
  -d '{"variable": "led", "binding": "coil:10", "direction": "output"}'
echo ""

# --- Trin 3: GPIO 19 → Coil 10 output ---
echo "=== Trin 3: Konfigurér GPIO ==="
curl -s -H "$AUTH" -H "Content-Type: application/json" \
  -X POST "$HOST/api/gpio/19/config" \
  -d '{"direction": "output", "coil": 10}'
echo ""

# --- Trin 4: Aktivér program ---
echo "=== Trin 4: Aktivér ==="
curl -s -H "$AUTH" -H "Content-Type: application/json" \
  -X POST "$HOST/api/logic/1/enable"
echo ""

# --- Trin 5: Gem til NVS ---
echo "=== Trin 5: Gem config ==="
curl -s -H "$AUTH" -H "Content-Type: application/json" \
  -X POST "$HOST/api/system/save"
echo ""

# --- Verifikation ---
echo "=== Verifikation ==="
echo "Program status:"
curl -s -H "$AUTH" "$HOST/api/logic/1"
echo ""
echo "GPIO 19:"
curl -s -H "$AUTH" "$HOST/api/gpio/19"
echo ""
echo "Coil 10:"
curl -s -H "$AUTH" "$HOST/api/registers/coils/10"
echo ""
```

---

## 6. OPRYDNING / FJERN PROGRAMMET

Hvis du vil fjerne alt igen:

```bash
AUTH="Authorization: Basic YXBpX3VzZXI6ITIzUGFzc3dvcmQ="
HOST="http://10.1.1.126"

# Deaktivér program
curl -s -H "$AUTH" -X POST "$HOST/api/logic/1/disable"

# Slet program (fjerner source, bindings)
curl -s -H "$AUTH" -X DELETE "$HOST/api/logic/1"

# Gem (persistér sletningen)
curl -s -H "$AUTH" -X POST "$HOST/api/system/save"
```

---

## 7. FEJLFINDING

### 7.1 Authentication fejler (401)

| Symptom | Årsag | Løsning |
|---------|-------|---------|
| `{"error":"Authentication required","status":401}` | Forkert credentials eller shell-expansion af `!` | Brug eksplicit Base64 header i stedet for `-u` flag |

**Specifikt problem med `curl -u`:**
```bash
# FEJLER — bash ekspanderer ! i passwordet
curl -u 'api_user:ChangeMe123!' ...

# VIRKER — eksplicit Base64 header
curl -H "Authorization: Basic YXBpX3VzZXI6ITIzUGFzc3dvcmQ=" ...
```

### 7.2 Compilation fejler

| Symptom | Årsag | Løsning |
|---------|-------|---------|
| `compiled: false` | Syntaksfejl i ST-kode | Tjek `error` felt i response, ret kildekoden |
| Missing semicolon | Manglende `;` efter statement | Alle statements i ST kræver `;` |
| Unknown variable | Variabelnavn stavet forkert i bind | Variabelnavne er case-sensitive |

### 7.3 Bind fejler

| Symptom | Årsag | Løsning |
|---------|-------|---------|
| 400: "Variable not found" | Variablen eksisterer ikke i programmet | Upload og kompilér først, tjek variabelnavn |
| 400: "Program not compiled" | Bind kaldt før source upload | Upload source først |

### 7.4 GPIO virker ikke

| Symptom | Årsag | Løsning |
|---------|-------|---------|
| GPIO ændrer sig ikke | Program ikke enabled | `POST /api/logic/1/enable` |
| GPIO altid LOW | Coil-nummer mismatch | Tjek at bind og gpio config bruger samme coil |
| Pin i brug | GPIO 19 brugt af anden funktion | Tjek med `GET /api/gpio` |

---

## 8. UDVIDELSER

### 8.1 Hurtigere blink (200ms)

Ændr `50` til `10` i ST-koden:
```iecst
IF ticks >= 10 THEN    (* 10 × 10ms = 100ms per fase = 200ms periode *)
```

### 8.2 Tilføj input (knap på GPIO 23)

```bash
# 1. GPIO 23 som input → Discrete Input 5
curl -s -H "$AUTH" -H "Content-Type: application/json" \
  -X POST "$HOST/api/gpio/23/config" \
  -d '{"direction": "input", "register": 5}'

# 2. Opdatér ST-kode med knap-variabel
curl -s -H "$AUTH" -H "Content-Type: application/json" \
  -X POST "$HOST/api/logic/1/source" \
  -d '{"source": "VAR\n  led : BOOL := FALSE;\n  button : BOOL := FALSE;\n  ticks : INT := 0;\nEND_VAR\nBEGIN\n  IF button THEN\n    ticks := ticks + 1;\n    IF ticks >= 50 THEN\n      led := NOT led;\n      ticks := 0;\n    END_IF;\n  ELSE\n    led := FALSE;\n    ticks := 0;\n  END_IF;\nEND"}'

# 3. Bind button som input fra discrete input 5
curl -s -H "$AUTH" -H "Content-Type: application/json" \
  -X POST "$HOST/api/logic/1/bind" \
  -d '{"variable": "button", "binding": "input:5", "direction": "input"}'

# 4. Re-bind led (bindings slettes ved ny source upload)
curl -s -H "$AUTH" -H "Content-Type: application/json" \
  -X POST "$HOST/api/logic/1/bind" \
  -d '{"variable": "led", "binding": "coil:10", "direction": "output"}'

# 5. Enable + save
curl -s -H "$AUTH" -X POST "$HOST/api/logic/1/enable"
curl -s -H "$AUTH" -X POST "$HOST/api/system/save"
```

### 8.3 Eksportér tæller til Modbus holding register

```bash
# Bind ticks til Holding Register 100 (læsbar via Modbus)
curl -s -H "$AUTH" -H "Content-Type: application/json" \
  -X POST "$HOST/api/logic/1/bind" \
  -d '{"variable": "ticks", "binding": "reg:100", "direction": "output"}'
```

Nu kan en Modbus master (SCADA) læse HR100 for at se den aktuelle ticks-værdi.

---

## 9. API ENDPOINT REFERENCE (brugt i denne guide)

| # | Metode | Endpoint | Formål |
|---|--------|----------|--------|
| 1 | POST | `/api/logic/{id}/source` | Upload og kompilér ST kildekode |
| 2 | POST | `/api/logic/{id}/bind` | Bind ST variabel til Modbus register/coil |
| 3 | POST | `/api/gpio/{pin}/config` | Konfigurér GPIO pin som input/output |
| 4 | POST | `/api/logic/{id}/enable` | Start program execution |
| 5 | POST | `/api/logic/{id}/disable` | Stop program execution |
| 6 | DELETE | `/api/logic/{id}` | Slet program helt |
| 7 | POST | `/api/system/save` | Gem konfiguration til NVS |
| 8 | GET | `/api/logic/{id}` | Læs programstatus og variabler |
| 9 | GET | `/api/logic/{id}/source` | Download ST kildekode |
| 10 | GET | `/api/gpio/{pin}` | Læs GPIO pin status |
| 11 | GET | `/api/registers/coils/{addr}` | Læs coil-værdi |
| 12 | GET | `/api/status` | System status (version, uptime, heap) |

---

## 10. KONCEPTDIAGRAM

```
┌─────────────────────────────────────────────────────────────────┐
│                        REST API Layer                           │
│                                                                 │
│  POST /api/logic/1/source   ──► Kompilér ST kildekode          │
│  POST /api/logic/1/bind     ──► Forbind variabel ↔ register    │
│  POST /api/gpio/19/config   ──► Map fysisk pin ↔ coil          │
│  POST /api/logic/1/enable   ──► Start execution                │
│  POST /api/system/save      ──► Persistér til flash            │
└─────────────────────────────────┬───────────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────┐
│                     ST Logic Engine                              │
│                                                                 │
│  Scheduler: hvert 10ms                                          │
│  ┌──────────┐    ┌──────────┐    ┌──────────┐                  │
│  │ Fase 1:  │    │ Fase 2:  │    │ Fase 3:  │                  │
│  │ Læs      │──►│ Kør      │──►│ Skriv    │                  │
│  │ inputs   │    │ bytecode │    │ outputs  │                  │
│  └──────────┘    └──────────┘    └──────────┘                  │
│       ▲                                │                        │
│       │          led := NOT led        │                        │
│       │          ticks := ticks+1      ▼                        │
│  (ingen inputs   (ST VM kører     led → Coil 10                │
│   i dette        kompileret       (output binding)              │
│   eksempel)      bytecode)                                      │
└─────────────────────────────────┬───────────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────┐
│                     GPIO Mapping Layer                           │
│                                                                 │
│  Coil 10 ──► GPIO 19 (output)                                  │
│                                                                 │
│  Coil TRUE  → GPIO HIGH (3.3V) → LED ON                        │
│  Coil FALSE → GPIO LOW  (0V)   → LED OFF                       │
└─────────────────────────────────────────────────────────────────┘
```

---

**Sidst opdateret:** 2026-02-23
**Testet med:** Firmware v6.0.7, Build #1246, ESP32 på 10.1.1.126
