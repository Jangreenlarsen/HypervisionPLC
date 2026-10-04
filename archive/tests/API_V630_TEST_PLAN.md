# HTTP REST API v6.3.0 Test Plan

**Version:** v6.3.0 | **Build:** #1384 | **Dato:** 2026-03-16
**Features:** FEAT-019, FEAT-020, FEAT-021, FEAT-024, FEAT-025, FEAT-026, FEAT-027
**Formål:** Systematisk test af alle nye v6.3.0 API endpoints med live hardware
**Testresultater:** Se [API_V630_TEST_RESULTS.md](API_V630_TEST_RESULTS.md)

> **VIGTIGT — Korrekte API-formater (opdateret efter test):**
> - Bulk write URI: `/api/registers/hr/bulk` (ikke `/api/registers/hr`)
> - Bulk write body: `{"writes":[{"addr":N,"value":V},...]}`
> - Debug control: URI suffix (`/debug/pause`, `/debug/step`, etc.) — IKKE JSON body
> - Debug breakpoint: `{"pc":3}` (ikke `{"line":3}`)
> - Heartbeat URI: `/api/gpio/2/heartbeat` (ikke `/api/heartbeat`)
> - Heartbeat POST body: `{"enabled":false}` (inverted: enabled=false → user mode)

---

## Test Oversigt

### Test Statistik

| Kategori | Antal Tests | Prioritet | FEAT |
|----------|-------------|-----------|------|
| CORS Preflight | 3 | HIGH | FEAT-027 |
| Telnet API | 4 | MEDIUM | FEAT-019 |
| Hostname API | 4 | MEDIUM | FEAT-024 |
| Watchdog API | 2 | MEDIUM | FEAT-025 |
| Heartbeat API | 3 | MEDIUM | FEAT-026 |
| Bulk HR Read | 5 | HIGH | FEAT-021 |
| Bulk HR Write | 4 | HIGH | FEAT-021 |
| Bulk IR Read | 3 | HIGH | FEAT-021 |
| Bulk Coils Read | 3 | HIGH | FEAT-021 |
| Bulk Coils Write | 3 | HIGH | FEAT-021 |
| Bulk DI Read | 2 | MEDIUM | FEAT-021 |
| ST Logic Debug | 8 | HIGH | FEAT-020 |
| Discovery Update | 1 | LOW | — |
| **Total** | **45** | — | — |

---

## Forudsætninger

### Hardware
- ESP32 online og tilgængelig på netværk
- Mindst ét ST Logic program kompileret og kørende (for debug-tests)

### Test Variabler
```bash
# Device konfiguration
ESP32_IP="10.1.32.20"
BASE_URL="http://${ESP32_IP}"
AUTH="api_user:ChangeMe123!"

# Alias for nemmere curl-kald
alias apicurl='curl -s -u ${AUTH}'
```

### Verifikation af forbindelse
```bash
# Test 0: Kan vi nå enheden?
curl -s -u api_user:'ChangeMe123!' http://10.1.32.20/api/status | python -m json.tool
```

**Forventet:** JSON med version "6.3.0"

---

## FEAT-027: CORS Support

### Test CORS-001: OPTIONS Preflight på /api/status

**Prioritet:** HIGH

```bash
curl -s -i -X OPTIONS \
  http://10.1.32.20/api/status
```

**Forventet:**
```
HTTP/1.1 204 No Content
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS
Access-Control-Allow-Headers: Content-Type, Authorization
Access-Control-Max-Age: 86400
```

**Resultat:** ⬜ PASS / ⬜ FAIL

---

### Test CORS-002: OPTIONS Preflight på vilkårlig sti

**Prioritet:** HIGH

```bash
curl -s -i -X OPTIONS \
  http://10.1.32.20/api/registers/hr/100
```

**Forventet:** 204 med CORS headers (samme som CORS-001)

**Resultat:** ⬜ PASS / ⬜ FAIL

---

### Test CORS-003: CORS Header på GET response

**Prioritet:** HIGH

```bash
curl -s -i -u api_user:'ChangeMe123!' \
  http://10.1.32.20/api/status 2>&1 | grep -i "access-control"
```

**Forventet:**
```
Access-Control-Allow-Origin: *
```

**Resultat:** ⬜ PASS / ⬜ FAIL

---

## FEAT-019: Telnet API

### Test TEL-001: GET Telnet konfiguration

**Prioritet:** MEDIUM

```bash
curl -s -u api_user:'ChangeMe123!' \
  http://10.1.32.20/api/telnet | python -m json.tool
```

**Forventet:** JSON med felter:
```json
{
  "enabled": true,
  "port": 23,
  "timeout_sec": 300
}
```
(Værdier kan variere afhængig af aktuel konfiguration)

**Verificer:** Alle 3 felter er til stede, typer korrekte (bool, int, int)

**Resultat:** ⬜ PASS / ⬜ FAIL

---

### Test TEL-002: POST Ændr Telnet timeout

**Prioritet:** MEDIUM

```bash
# Læs nuværende værdi
curl -s -u api_user:'ChangeMe123!' \
  http://10.1.32.20/api/telnet | python -m json.tool

# Ændr timeout til 600
curl -s -u api_user:'ChangeMe123!' \
  -X POST -H "Content-Type: application/json" \
  -d '{"timeout_sec": 600}' \
  http://10.1.32.20/api/telnet | python -m json.tool

# Verificer ændringen
curl -s -u api_user:'ChangeMe123!' \
  http://10.1.32.20/api/telnet | python -m json.tool
```

**Forventet:** timeout_sec er nu 600

**Resultat:** ⬜ PASS / ⬜ FAIL

---

### Test TEL-003: POST Ugyldig JSON til Telnet

**Prioritet:** LOW

```bash
curl -s -u api_user:'ChangeMe123!' \
  -X POST -H "Content-Type: application/json" \
  -d 'not json' \
  http://10.1.32.20/api/telnet | python -m json.tool
```

**Forventet:** Error response (400)

**Resultat:** ⬜ PASS / ⬜ FAIL

---

### Test TEL-004: Telnet uden auth

**Prioritet:** MEDIUM

```bash
curl -s http://10.1.32.20/api/telnet | python -m json.tool
```

**Forventet:** 401 Unauthorized

**Resultat:** ⬜ PASS / ⬜ FAIL

---

## FEAT-024: Hostname API

### Test HOST-001: GET Hostname

**Prioritet:** MEDIUM

```bash
curl -s -u api_user:'ChangeMe123!' \
  http://10.1.32.20/api/hostname | python -m json.tool
```

**Forventet:**
```json
{
  "hostname": "esp32-modbus"
}
```
(Værdi kan variere)

**Resultat:** ⬜ PASS / ⬜ FAIL

---

### Test HOST-002: POST Sæt nyt hostname

**Prioritet:** MEDIUM

```bash
# Gem nuværende hostname
curl -s -u api_user:'ChangeMe123!' \
  http://10.1.32.20/api/hostname | python -m json.tool

# Sæt nyt hostname
curl -s -u api_user:'ChangeMe123!' \
  -X POST -H "Content-Type: application/json" \
  -d '{"hostname": "test-plc-01"}' \
  http://10.1.32.20/api/hostname | python -m json.tool

# Verificer
curl -s -u api_user:'ChangeMe123!' \
  http://10.1.32.20/api/hostname | python -m json.tool
```

**Forventet:** hostname er nu "test-plc-01"

**Resultat:** ⬜ PASS / ⬜ FAIL

---

### Test HOST-003: POST Gendan hostname

**Prioritet:** MEDIUM

```bash
# Gendan original hostname
curl -s -u api_user:'ChangeMe123!' \
  -X POST -H "Content-Type: application/json" \
  -d '{"hostname": "esp32-modbus"}' \
  http://10.1.32.20/api/hostname | python -m json.tool
```

**Forventet:** Status "ok"

**Resultat:** ⬜ PASS / ⬜ FAIL

---

### Test HOST-004: POST Tomt hostname

**Prioritet:** LOW

```bash
curl -s -u api_user:'ChangeMe123!' \
  -X POST -H "Content-Type: application/json" \
  -d '{"hostname": ""}' \
  http://10.1.32.20/api/hostname | python -m json.tool
```

**Forventet:** Error 400 (tomt hostname ikke tilladt) eller accepteret afhængig af implementation

**Resultat:** ⬜ PASS / ⬜ FAIL

---

## FEAT-025: Watchdog API

### Test WDG-001: GET Watchdog status

**Prioritet:** MEDIUM

```bash
curl -s -u api_user:'ChangeMe123!' \
  http://10.1.32.20/api/system/watchdog | python -m json.tool
```

**Forventet:** JSON med felter som:
```json
{
  "enabled": true,
  "timeout_sec": 30,
  "reboot_count": 0,
  "last_reboot_reason": "power_on"
}
```

**Verificer:** Alle felter til stede, reboot_count er integer >= 0

**Resultat:** ⬜ PASS / ⬜ FAIL

---

### Test WDG-002: Watchdog uden auth

**Prioritet:** LOW

```bash
curl -s http://10.1.32.20/api/system/watchdog | python -m json.tool
```

**Forventet:** 401 Unauthorized

**Resultat:** ⬜ PASS / ⬜ FAIL

---

## FEAT-026: Heartbeat API

### Test HB-001: GET Heartbeat status

**Prioritet:** MEDIUM

```bash
curl -s -u api_user:'ChangeMe123!' \
  http://10.1.32.20/api/heartbeat | python -m json.tool
```

**Forventet:**
```json
{
  "gpio2_user_mode": false,
  "led_state": true
}
```

**Resultat:** ⬜ PASS / ⬜ FAIL

---

### Test HB-002: POST Aktivér GPIO2 user mode

**Prioritet:** MEDIUM

```bash
# Aktivér user mode
curl -s -u api_user:'ChangeMe123!' \
  -X POST -H "Content-Type: application/json" \
  -d '{"gpio2_user_mode": true}' \
  http://10.1.32.20/api/heartbeat | python -m json.tool

# Verificer
curl -s -u api_user:'ChangeMe123!' \
  http://10.1.32.20/api/heartbeat | python -m json.tool
```

**Forventet:** gpio2_user_mode er nu true, LED blinker ikke længere

**Resultat:** ⬜ PASS / ⬜ FAIL

---

### Test HB-003: POST Deaktivér GPIO2 user mode (gendan)

**Prioritet:** MEDIUM

```bash
curl -s -u api_user:'ChangeMe123!' \
  -X POST -H "Content-Type: application/json" \
  -d '{"gpio2_user_mode": false}' \
  http://10.1.32.20/api/heartbeat | python -m json.tool
```

**Forventet:** gpio2_user_mode er false igen, LED blinker normalt

**Resultat:** ⬜ PASS / ⬜ FAIL

---

## FEAT-021: Bulk Register Operationer

### Holding Registers (HR)

#### Test BULK-HR-001: Læs 10 HR fra start 0

**Prioritet:** HIGH

```bash
curl -s -u api_user:'ChangeMe123!' \
  "http://10.1.32.20/api/registers/hr?start=0&count=10" | python -m json.tool
```

**Forventet:**
```json
{
  "start": 0,
  "count": 10,
  "registers": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
}
```
(Værdier afhænger af nuværende register-indhold)

**Verificer:**
- `registers` er et array med præcis 10 elementer
- `start` matcher query parameter
- `count` matcher array length

**Resultat:** ⬜ PASS / ⬜ FAIL

---

#### Test BULK-HR-002: Læs HR med default parametre

**Prioritet:** MEDIUM

```bash
curl -s -u api_user:'ChangeMe123!' \
  "http://10.1.32.20/api/registers/hr" | python -m json.tool
```

**Forventet:** start=0, count=10 (defaults)

**Resultat:** ⬜ PASS / ⬜ FAIL

---

#### Test BULK-HR-003: Læs counter registers (HR100-114)

**Prioritet:** HIGH

```bash
curl -s -u api_user:'ChangeMe123!' \
  "http://10.1.32.20/api/registers/hr?start=100&count=15" | python -m json.tool
```

**Forventet:** Array med 15 register-værdier fra counter-området

**Resultat:** ⬜ PASS / ⬜ FAIL

---

#### Test BULK-HR-004: Læs med count > max (125+)

**Prioritet:** MEDIUM

```bash
curl -s -u api_user:'ChangeMe123!' \
  "http://10.1.32.20/api/registers/hr?start=0&count=200" | python -m json.tool
```

**Forventet:** Error 400 (count overstiger max) ELLER trunceret til max tilladt

**Resultat:** ⬜ PASS / ⬜ FAIL

---

#### Test BULK-HR-005: Læs med ugyldig start-adresse

**Prioritet:** MEDIUM

```bash
curl -s -u api_user:'ChangeMe123!' \
  "http://10.1.32.20/api/registers/hr?start=9999&count=10" | python -m json.tool
```

**Forventet:** Error 400 (start + count overstiger register range)

**Resultat:** ⬜ PASS / ⬜ FAIL

---

#### Test BULK-HR-W001: Bulk skriv HR

**Prioritet:** HIGH

```bash
# Skriv 3 værdier til HR10-12
curl -s -u api_user:'ChangeMe123!' \
  -X POST -H "Content-Type: application/json" \
  -d '{"start": 10, "values": [1111, 2222, 3333]}' \
  http://10.1.32.20/api/registers/hr | python -m json.tool
```

**Forventet:**
```json
{
  "start": 10,
  "count": 3,
  "status": "ok"
}
```

**Resultat:** ⬜ PASS / ⬜ FAIL

---

#### Test BULK-HR-W002: Verificer bulk skriv

**Prioritet:** HIGH

```bash
# Læs de skrevne registers tilbage
curl -s -u api_user:'ChangeMe123!' \
  "http://10.1.32.20/api/registers/hr?start=10&count=3" | python -m json.tool
```

**Forventet:** registers = [1111, 2222, 3333]

**Resultat:** ⬜ PASS / ⬜ FAIL

---

#### Test BULK-HR-W003: Bulk skriv + enkelt read verification

**Prioritet:** HIGH

```bash
# Skriv en kendt værdi
curl -s -u api_user:'ChangeMe123!' \
  -X POST -H "Content-Type: application/json" \
  -d '{"start": 50, "values": [42]}' \
  http://10.1.32.20/api/registers/hr | python -m json.tool

# Verificer med enkelt register read
curl -s -u api_user:'ChangeMe123!' \
  http://10.1.32.20/api/registers/hr/50 | python -m json.tool
```

**Forventet:** Enkelt read viser value=42

**Resultat:** ⬜ PASS / ⬜ FAIL

---

#### Test BULK-HR-W004: Bulk skriv tom values array

**Prioritet:** LOW

```bash
curl -s -u api_user:'ChangeMe123!' \
  -X POST -H "Content-Type: application/json" \
  -d '{"start": 10, "values": []}' \
  http://10.1.32.20/api/registers/hr | python -m json.tool
```

**Forventet:** Error 400 eller count=0 ok-response

**Resultat:** ⬜ PASS / ⬜ FAIL

---

### Input Registers (IR)

#### Test BULK-IR-001: Læs 10 IR fra start 0

**Prioritet:** HIGH

```bash
curl -s -u api_user:'ChangeMe123!' \
  "http://10.1.32.20/api/registers/ir?start=0&count=10" | python -m json.tool
```

**Forventet:** JSON med start, count, registers array (10 elementer)

**Resultat:** ⬜ PASS / ⬜ FAIL

---

#### Test BULK-IR-002: Læs ST Logic status registers (IR200-219)

**Prioritet:** HIGH

```bash
curl -s -u api_user:'ChangeMe123!' \
  "http://10.1.32.20/api/registers/ir?start=200&count=20" | python -m json.tool
```

**Forventet:** Array med 20 ST Logic status-værdier

**Resultat:** ⬜ PASS / ⬜ FAIL

---

#### Test BULK-IR-003: POST til IR (skal fejle — read-only)

**Prioritet:** MEDIUM

```bash
curl -s -u api_user:'ChangeMe123!' \
  -X POST -H "Content-Type: application/json" \
  -d '{"start": 0, "values": [99]}' \
  http://10.1.32.20/api/registers/ir | python -m json.tool
```

**Forventet:** Error 405 Method Not Allowed ELLER 404 (POST ikke registreret for IR bulk)

**Resultat:** ⬜ PASS / ⬜ FAIL

---

### Coils

#### Test BULK-COIL-001: Læs 16 coils fra start 0

**Prioritet:** HIGH

```bash
curl -s -u api_user:'ChangeMe123!' \
  "http://10.1.32.20/api/registers/coils?start=0&count=16" | python -m json.tool
```

**Forventet:**
```json
{
  "start": 0,
  "count": 16,
  "coils": [false, false, ...]
}
```

**Verificer:** `coils` array med 16 boolean værdier

**Resultat:** ⬜ PASS / ⬜ FAIL

---

#### Test BULK-COIL-002: Bulk skriv coils

**Prioritet:** HIGH

```bash
# Skriv coil 0-3
curl -s -u api_user:'ChangeMe123!' \
  -X POST -H "Content-Type: application/json" \
  -d '{"start": 0, "values": [true, false, true, false]}' \
  http://10.1.32.20/api/registers/coils | python -m json.tool

# Verificer
curl -s -u api_user:'ChangeMe123!' \
  "http://10.1.32.20/api/registers/coils?start=0&count=4" | python -m json.tool
```

**Forventet:** coils = [true, false, true, false]

**Resultat:** ⬜ PASS / ⬜ FAIL

---

#### Test BULK-COIL-003: Nulstil coils efter test

**Prioritet:** MEDIUM

```bash
curl -s -u api_user:'ChangeMe123!' \
  -X POST -H "Content-Type: application/json" \
  -d '{"start": 0, "values": [false, false, false, false]}' \
  http://10.1.32.20/api/registers/coils | python -m json.tool
```

**Forventet:** Status ok, coils nulstillet

**Resultat:** ⬜ PASS / ⬜ FAIL

---

### Discrete Inputs (DI)

#### Test BULK-DI-001: Læs 16 discrete inputs

**Prioritet:** MEDIUM

```bash
curl -s -u api_user:'ChangeMe123!' \
  "http://10.1.32.20/api/registers/di?start=0&count=16" | python -m json.tool
```

**Forventet:** JSON med start, count, og boolean array

**Resultat:** ⬜ PASS / ⬜ FAIL

---

#### Test BULK-DI-002: DI er read-only

**Prioritet:** MEDIUM

```bash
curl -s -u api_user:'ChangeMe123!' \
  -X POST -H "Content-Type: application/json" \
  -d '{"start": 0, "values": [true]}' \
  http://10.1.32.20/api/registers/di | python -m json.tool
```

**Forventet:** Error (POST ikke tilgængelig for DI)

**Resultat:** ⬜ PASS / ⬜ FAIL

---

## FEAT-020: ST Logic Debug API

> **VIGTIGT:** Disse tests kræver at mindst ét ST Logic program er kompileret og kørende.
> Verificer med: `curl -s -u api_user:'ChangeMe123!' http://10.1.32.20/api/logic | python -m json.tool`

### Test DBG-001: GET Debug state

**Prioritet:** HIGH

```bash
curl -s -u api_user:'ChangeMe123!' \
  http://10.1.32.20/api/logic/1/debug/state | python -m json.tool
```

**Forventet:**
```json
{
  "program_id": 1,
  "paused": false,
  "current_line": 0,
  "breakpoints": [],
  "snapshot": { ... }
}
```

**Verificer:** program_id=1, paused=false (normalt kørende)

**Resultat:** ⬜ PASS / ⬜ FAIL

---

### Test DBG-002: POST Pause program

**Prioritet:** HIGH

```bash
curl -s -u api_user:'ChangeMe123!' \
  -X POST -H "Content-Type: application/json" \
  -d '{"action": "pause"}' \
  http://10.1.32.20/api/logic/1/debug/control | python -m json.tool
```

**Forventet:** Success response, program paused

**Verificer med:**
```bash
curl -s -u api_user:'ChangeMe123!' \
  http://10.1.32.20/api/logic/1/debug/state | python -m json.tool
```

**Forventet:** paused=true

**Resultat:** ⬜ PASS / ⬜ FAIL

---

### Test DBG-003: POST Step (mens paused)

**Prioritet:** HIGH

```bash
# Program skal være paused (fra DBG-002)
curl -s -u api_user:'ChangeMe123!' \
  -X POST -H "Content-Type: application/json" \
  -d '{"action": "step"}' \
  http://10.1.32.20/api/logic/1/debug/control | python -m json.tool
```

**Forventet:** Success, current_line opdateret

**Resultat:** ⬜ PASS / ⬜ FAIL

---

### Test DBG-004: POST Sæt breakpoint

**Prioritet:** HIGH

```bash
curl -s -u api_user:'ChangeMe123!' \
  -X POST -H "Content-Type: application/json" \
  -d '{"line": 3}' \
  http://10.1.32.20/api/logic/1/debug/breakpoint | python -m json.tool
```

**Forventet:** Success, breakpoint sat på linje 3

**Verificer:**
```bash
curl -s -u api_user:'ChangeMe123!' \
  http://10.1.32.20/api/logic/1/debug/state | python -m json.tool
```

**Forventet:** breakpoints array indeholder 3

**Resultat:** ⬜ PASS / ⬜ FAIL

---

### Test DBG-005: DELETE Fjern breakpoint

**Prioritet:** HIGH

```bash
curl -s -u api_user:'ChangeMe123!' \
  -X DELETE -H "Content-Type: application/json" \
  -d '{"line": 3}' \
  http://10.1.32.20/api/logic/1/debug/breakpoint | python -m json.tool
```

**Forventet:** Success, breakpoint fjernet

**Verificer:** breakpoints array er tomt igen

**Resultat:** ⬜ PASS / ⬜ FAIL

---

### Test DBG-006: POST Continue (genoptag)

**Prioritet:** HIGH

```bash
curl -s -u api_user:'ChangeMe123!' \
  -X POST -H "Content-Type: application/json" \
  -d '{"action": "continue"}' \
  http://10.1.32.20/api/logic/1/debug/control | python -m json.tool
```

**Forventet:** Success, program kører igen

**Verificer:** paused=false i debug state

**Resultat:** ⬜ PASS / ⬜ FAIL

---

### Test DBG-007: POST Stop program

**Prioritet:** MEDIUM

```bash
curl -s -u api_user:'ChangeMe123!' \
  -X POST -H "Content-Type: application/json" \
  -d '{"action": "stop"}' \
  http://10.1.32.20/api/logic/1/debug/control | python -m json.tool
```

**Forventet:** Success, program stoppet

**Resultat:** ⬜ PASS / ⬜ FAIL

---

### Test DBG-008: Debug på ikke-kompileret program

**Prioritet:** MEDIUM

```bash
# Brug et program der ikke er kompileret (typisk 3 eller 4)
curl -s -u api_user:'ChangeMe123!' \
  http://10.1.32.20/api/logic/4/debug/state | python -m json.tool
```

**Forventet:** Error eller tom state (intet at debugge)

**Resultat:** ⬜ PASS / ⬜ FAIL

---

## Discovery Endpoint

### Test DISC-001: Verificer nye endpoints i discovery

**Prioritet:** LOW

```bash
curl -s -u api_user:'ChangeMe123!' \
  http://10.1.32.20/api/discover | python -m json.tool | grep -E "telnet|hostname|watchdog|heartbeat|bulk|debug|cors"
```

**Forventet:** Alle nye v6.3.0 endpoints synlige i discovery

**Resultat:** ⬜ PASS / ⬜ FAIL

---

## Komplet Sekvens-Test

Denne test kører alle endpoints sekventielt for at teste system-stabilitet.

```bash
#!/bin/bash
# v6.3.0 API Sequential Test Script
# Kør: bash api_test_v630.sh

ESP32="10.1.32.20"
AUTH="api_user:ChangeMe123!"
PASS=0
FAIL=0

check() {
  local name="$1"
  local code="$2"
  local expected="$3"
  if [ "$code" = "$expected" ]; then
    echo "  PASS: $name (HTTP $code)"
    ((PASS++))
  else
    echo "  FAIL: $name (HTTP $code, expected $expected)"
    ((FAIL++))
  fi
}

echo "=== v6.3.0 API Test Suite ==="
echo "Target: $ESP32"
echo ""

# CORS
echo "--- CORS ---"
CODE=$(curl -s -o /dev/null -w "%{http_code}" -X OPTIONS http://$ESP32/api/status)
check "OPTIONS /api/status" "$CODE" "204"

# Telnet
echo "--- Telnet ---"
CODE=$(curl -s -o /dev/null -w "%{http_code}" -u $AUTH http://$ESP32/api/telnet)
check "GET /api/telnet" "$CODE" "200"

CODE=$(curl -s -o /dev/null -w "%{http_code}" http://$ESP32/api/telnet)
check "GET /api/telnet (no auth)" "$CODE" "401"

# Hostname
echo "--- Hostname ---"
CODE=$(curl -s -o /dev/null -w "%{http_code}" -u $AUTH http://$ESP32/api/hostname)
check "GET /api/hostname" "$CODE" "200"

# Watchdog
echo "--- Watchdog ---"
CODE=$(curl -s -o /dev/null -w "%{http_code}" -u $AUTH http://$ESP32/api/system/watchdog)
check "GET /api/system/watchdog" "$CODE" "200"

# Heartbeat
echo "--- Heartbeat ---"
CODE=$(curl -s -o /dev/null -w "%{http_code}" -u $AUTH http://$ESP32/api/heartbeat)
check "GET /api/heartbeat" "$CODE" "200"

# Bulk HR
echo "--- Bulk HR ---"
CODE=$(curl -s -o /dev/null -w "%{http_code}" -u $AUTH "http://$ESP32/api/registers/hr?start=0&count=10")
check "GET /api/registers/hr (bulk)" "$CODE" "200"

CODE=$(curl -s -o /dev/null -w "%{http_code}" -u $AUTH \
  -X POST -H "Content-Type: application/json" \
  -d '{"start":10,"values":[1111,2222,3333]}' \
  http://$ESP32/api/registers/hr)
check "POST /api/registers/hr (bulk write)" "$CODE" "200"

# Verificer write
VALS=$(curl -s -u $AUTH "http://$ESP32/api/registers/hr?start=10&count=3")
echo "  Verify bulk write: $VALS"

# Bulk IR
echo "--- Bulk IR ---"
CODE=$(curl -s -o /dev/null -w "%{http_code}" -u $AUTH "http://$ESP32/api/registers/ir?start=0&count=10")
check "GET /api/registers/ir (bulk)" "$CODE" "200"

# Bulk Coils
echo "--- Bulk Coils ---"
CODE=$(curl -s -o /dev/null -w "%{http_code}" -u $AUTH "http://$ESP32/api/registers/coils?start=0&count=16")
check "GET /api/registers/coils (bulk)" "$CODE" "200"

CODE=$(curl -s -o /dev/null -w "%{http_code}" -u $AUTH \
  -X POST -H "Content-Type: application/json" \
  -d '{"start":0,"values":[true,false,true,false]}' \
  http://$ESP32/api/registers/coils)
check "POST /api/registers/coils (bulk write)" "$CODE" "200"

# Nulstil coils
curl -s -u $AUTH -X POST -H "Content-Type: application/json" \
  -d '{"start":0,"values":[false,false,false,false]}' \
  http://$ESP32/api/registers/coils > /dev/null

# Bulk DI
echo "--- Bulk DI ---"
CODE=$(curl -s -o /dev/null -w "%{http_code}" -u $AUTH "http://$ESP32/api/registers/di?start=0&count=16")
check "GET /api/registers/di (bulk)" "$CODE" "200"

# Debug
echo "--- ST Logic Debug ---"
CODE=$(curl -s -o /dev/null -w "%{http_code}" -u $AUTH http://$ESP32/api/logic/1/debug/state)
check "GET /api/logic/1/debug/state" "$CODE" "200"

CODE=$(curl -s -o /dev/null -w "%{http_code}" -u $AUTH \
  -X POST -H "Content-Type: application/json" \
  -d '{"action":"pause"}' \
  http://$ESP32/api/logic/1/debug/control)
check "POST debug pause" "$CODE" "200"

CODE=$(curl -s -o /dev/null -w "%{http_code}" -u $AUTH \
  -X POST -H "Content-Type: application/json" \
  -d '{"action":"step"}' \
  http://$ESP32/api/logic/1/debug/control)
check "POST debug step" "$CODE" "200"

CODE=$(curl -s -o /dev/null -w "%{http_code}" -u $AUTH \
  -X POST -H "Content-Type: application/json" \
  -d '{"action":"continue"}' \
  http://$ESP32/api/logic/1/debug/control)
check "POST debug continue" "$CODE" "200"

# Discovery
echo "--- Discovery ---"
CODE=$(curl -s -o /dev/null -w "%{http_code}" -u $AUTH http://$ESP32/api/discover)
check "GET /api/discover" "$CODE" "200"

# Cleanup: nulstil HR 10-12
curl -s -u $AUTH -X POST -H "Content-Type: application/json" \
  -d '{"start":10,"values":[0,0,0]}' \
  http://$ESP32/api/registers/hr > /dev/null

echo ""
echo "=== RESULTAT ==="
echo "PASS: $PASS"
echo "FAIL: $FAIL"
echo "TOTAL: $((PASS + FAIL))"
```

---

## Cleanup Checklist

Efter test-kørsel, verificer at enheden er i normal tilstand:

```bash
# 1. Alle ST Logic programmer kører?
curl -s -u api_user:'ChangeMe123!' http://10.1.32.20/api/logic | python -m json.tool

# 2. Heartbeat normal?
curl -s -u api_user:'ChangeMe123!' http://10.1.32.20/api/heartbeat | python -m json.tool

# 3. Hostname korrekt?
curl -s -u api_user:'ChangeMe123!' http://10.1.32.20/api/hostname | python -m json.tool

# 4. Test-registers nulstillet?
curl -s -u api_user:'ChangeMe123!' "http://10.1.32.20/api/registers/hr?start=10&count=3" | python -m json.tool

# 5. System status OK?
curl -s -u api_user:'ChangeMe123!' http://10.1.32.20/api/status | python -m json.tool
```

---

## Test Resultat Opsummering

| Kategori | Tests | Pass | Fail | Status |
|----------|-------|------|------|--------|
| CORS (FEAT-027) | 3 | | | ⬜ |
| Telnet (FEAT-019) | 4 | | | ⬜ |
| Hostname (FEAT-024) | 4 | | | ⬜ |
| Watchdog (FEAT-025) | 2 | | | ⬜ |
| Heartbeat (FEAT-026) | 3 | | | ⬜ |
| Bulk HR Read (FEAT-021) | 5 | | | ⬜ |
| Bulk HR Write (FEAT-021) | 4 | | | ⬜ |
| Bulk IR Read (FEAT-021) | 3 | | | ⬜ |
| Bulk Coils (FEAT-021) | 3 | | | ⬜ |
| Bulk Coils Write (FEAT-021) | 3 | | | ⬜ |
| Bulk DI (FEAT-021) | 2 | | | ⬜ |
| ST Logic Debug (FEAT-020) | 8 | | | ⬜ |
| Discovery | 1 | | | ⬜ |
| **Total** | **45** | | | ⬜ |

**Testet af:** _______________
**Dato:** _______________
**Firmware:** v6.3.0 Build #1380
**Device IP:** 10.1.32.20

---

**Document Version:** 1.0
**Last Updated:** 2026-03-16
**Compatible With:** Firmware v6.3.0+
