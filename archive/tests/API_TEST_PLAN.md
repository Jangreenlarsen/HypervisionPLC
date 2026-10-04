# HTTP REST API Test Plan

**Version:** v6.0.0
**Build:** #1108+
**Feature:** FEAT-011
**Formål:** Systematisk test af HTTP REST API endpoints

---

## Indholdsfortegnelse

1. [Test Oversigt](#test-oversigt)
2. [Forudsætninger](#forudsætninger)
3. [Konfiguration Tests](#konfiguration-tests)
4. [System Status Tests](#system-status-tests)
5. [Counter API Tests](#counter-api-tests)
6. [Timer API Tests](#timer-api-tests)
7. [Register API Tests](#register-api-tests)
8. [ST Logic API Tests](#st-logic-api-tests)
9. [Authentication Tests](#authentication-tests)
10. [Error Handling Tests](#error-handling-tests)
11. [Performance Tests](#performance-tests)

---

## Test Oversigt

### Test Statistik

| Kategori | Antal Tests | Prioritet |
|----------|-------------|-----------|
| Konfiguration | 4 | HIGH |
| System Status | 3 | HIGH |
| Counter API | 5 | HIGH |
| Timer API | 4 | MEDIUM |
| Register API | 8 | HIGH |
| ST Logic API | 4 | MEDIUM |
| Authentication | 4 | HIGH |
| Error Handling | 6 | MEDIUM |
| Performance | 3 | LOW |
| **Total** | **41** | - |

---

## Forudsætninger

### Hardware
- ESP32 forbundet til WiFi
- PC på samme netværk

### Software
- curl (eller Postman/Insomnia)
- Python 3.x med `requests` (optional)

### ESP32 Konfiguration
```bash
# Verificer WiFi forbindelse
show wifi

# Aktivér HTTP server
set http enabled on
set http port 80
set http auth off

# Gem og verificer
save
show http
```

### Test Variabler
```bash
# Erstat med din ESP32's IP
ESP32_IP="192.168.1.100"
BASE_URL="http://${ESP32_IP}"
```

---

## Konfiguration Tests

### Test CFG-001: HTTP Server Enable/Disable

**Prioritet:** HIGH

**Test Steps:**
```bash
# 1. Deaktivér HTTP server
set http enabled off
show http
# Forventet: Server Status: STOPPED

# 2. Prøv at tilgå API
curl -s http://${ESP32_IP}/api/status
# Forventet: Connection refused / timeout

# 3. Aktivér HTTP server
set http enabled on
show http
# Forventet: Server Status: RUNNING

# 4. Prøv at tilgå API igen
curl -s http://${ESP32_IP}/api/status
# Forventet: JSON response med system info
```

**Forventet Resultat:**
```
✅ Server stopper når disabled
✅ Server starter når enabled
✅ API svarer efter enable
```

---

### Test CFG-002: Port Configuration

**Prioritet:** MEDIUM

**Test Steps:**
```bash
# 1. Skift port til 8080
set http port 8080
set http enabled off
set http enabled on

# 2. Test på ny port
curl -s http://${ESP32_IP}:8080/api/status
# Forventet: JSON response

# 3. Test på gammel port
curl -s http://${ESP32_IP}:80/api/status
# Forventet: Connection refused

# 4. Gendan default port
set http port 80
set http enabled off
set http enabled on
```

**Forventet Resultat:**
```
✅ Port ændring virker
✅ Gammel port svarer ikke
✅ Ny port svarer korrekt
```

---

### Test CFG-003: Configuration Persistence

**Prioritet:** HIGH

**Test Steps:**
```bash
# 1. Konfigurer HTTP
set http port 8080
set http auth on
set http username testuser
set http password testpass
save

# 2. Reboot ESP32
reboot

# 3. Efter reboot, verificer
show http
# Forventet: Port: 8080, Auth: ENABLED, Username: testuser

# 4. Gendan defaults
set http port 80
set http auth off
save
```

**Forventet Resultat:**
```
✅ Konfiguration gemmes i NVS
✅ Konfiguration gendannes efter reboot
```

---

### Test CFG-004: Show HTTP Statistics

**Prioritet:** LOW

**Test Steps:**
```bash
# 1. Kør nogle API requests
curl -s http://${ESP32_IP}/api/status
curl -s http://${ESP32_IP}/api/counters
curl -s http://${ESP32_IP}/api/invalid  # 404

# 2. Check statistics
show http
```

**Forventet Output:**
```
=== HTTP REST API STATUS ===
Server Status: RUNNING

--- Configuration ---
Enabled: YES
Port: 80
Auth: DISABLED

--- Statistics ---
Total Requests: 3
Successful (2xx): 2
Client Errors (4xx): 1
Server Errors (5xx): 0
```

**Forventet Resultat:**
```
✅ Request counter incrementerer
✅ Success/error kategorisering korrekt
```

---

## System Status Tests

### Test SYS-001: GET /api/status

**Prioritet:** HIGH

**Test Command:**
```bash
curl -s http://${ESP32_IP}/api/status | jq .
```

**Forventet Response:**
```json
{
  "version": "6.0.0",
  "build": 1108,
  "uptime_ms": 123456789,
  "heap_free": 180000,
  "wifi_connected": true,
  "ip": "192.168.1.100",
  "modbus_slave_id": 1
}
```

**Validering:**
```bash
# Python validation
python3 << 'EOF'
import requests
r = requests.get("http://${ESP32_IP}/api/status")
data = r.json()
assert "version" in data, "Missing version"
assert "build" in data, "Missing build"
assert "uptime_ms" in data, "Missing uptime"
assert data["heap_free"] > 0, "Invalid heap_free"
assert data["wifi_connected"] == True, "WiFi not connected"
print("✅ SYS-001 PASSED")
EOF
```

**Forventet Resultat:**
```
✅ Status code 200
✅ Content-Type: application/json
✅ Alle felter tilstede
✅ Værdier er valide
```

---

### Test SYS-002: Status Uptime Increment

**Prioritet:** MEDIUM

**Test Steps:**
```bash
# 1. Hent uptime
UPTIME1=$(curl -s http://${ESP32_IP}/api/status | jq .uptime_ms)

# 2. Vent 5 sekunder
sleep 5

# 3. Hent uptime igen
UPTIME2=$(curl -s http://${ESP32_IP}/api/status | jq .uptime_ms)

# 4. Verificer increment
echo "Uptime 1: $UPTIME1"
echo "Uptime 2: $UPTIME2"
# Forventet: UPTIME2 > UPTIME1 + 4000
```

**Forventet Resultat:**
```
✅ Uptime incrementerer korrekt
✅ Difference ca. 5000ms
```

---

### Test SYS-003: Heap Memory Stability

**Prioritet:** MEDIUM

**Test Steps:**
```bash
# 1. Hent initial heap
HEAP1=$(curl -s http://${ESP32_IP}/api/status | jq .heap_free)

# 2. Kør 100 requests
for i in $(seq 1 100); do
  curl -s http://${ESP32_IP}/api/status > /dev/null
done

# 3. Hent final heap
HEAP2=$(curl -s http://${ESP32_IP}/api/status | jq .heap_free)

# 4. Sammenlign
echo "Heap before: $HEAP1"
echo "Heap after: $HEAP2"
# Forventet: Difference < 5000 bytes
```

**Forventet Resultat:**
```
✅ Ingen memory leak
✅ Heap stabil (±5KB acceptable)
```

---

## Counter API Tests

### Test CNT-001: GET /api/counters (All)

**Prioritet:** HIGH

**Test Command:**
```bash
curl -s http://${ESP32_IP}/api/counters | jq .
```

**Forventet Response:**
```json
{
  "counters": [
    {"id": 1, "enabled": true, "mode": "HW_PCNT", "value": 12345},
    {"id": 2, "enabled": false, "mode": "DISABLED", "value": 0},
    {"id": 3, "enabled": false, "mode": "DISABLED", "value": 0},
    {"id": 4, "enabled": false, "mode": "DISABLED", "value": 0}
  ]
}
```

**Forventet Resultat:**
```
✅ Status code 200
✅ Array med 4 counters
✅ Hvert element har id, enabled, mode, value
```

---

### Test CNT-002: GET /api/counters/{id} (Single)

**Prioritet:** HIGH

**Test Steps:**
```bash
# Counter 1
curl -s http://${ESP32_IP}/api/counters/1 | jq .

# Counter 4
curl -s http://${ESP32_IP}/api/counters/4 | jq .
```

**Forventet Response (Counter 1):**
```json
{
  "id": 1,
  "enabled": true,
  "mode": "HW_PCNT",
  "value": 12345,
  "raw": 123,
  "frequency": 100,
  "running": true,
  "overflow": false,
  "compare_triggered": false
}
```

**Forventet Resultat:**
```
✅ Status code 200
✅ Detaljeret info inkluderet
✅ Alle felter har valide værdier
```

---

### Test CNT-003: Invalid Counter ID

**Prioritet:** MEDIUM

**Test Steps:**
```bash
# ID 0 (under range)
curl -s http://${ESP32_IP}/api/counters/0 | jq .

# ID 5 (over range)
curl -s http://${ESP32_IP}/api/counters/5 | jq .

# ID 99 (langt over)
curl -s http://${ESP32_IP}/api/counters/99 | jq .
```

**Forventet Response:**
```json
{
  "error": "Invalid counter ID (must be 1-4)",
  "status": 400
}
```

**Forventet Resultat:**
```
✅ Status code 400
✅ Beskrivende error message
✅ Ingen crash
```

---

### Test CNT-004: Counter Value Matches CLI

**Prioritet:** HIGH

**Test Steps:**
```bash
# 1. Læs counter via CLI
# I ESP32 terminal:
show counters
# Noter value for Counter 1

# 2. Læs via API
curl -s http://${ESP32_IP}/api/counters/1 | jq .value

# 3. Sammenlign værdier
# Forventet: Samme værdi (±1 for timing)
```

**Forventet Resultat:**
```
✅ API værdi matcher CLI værdi
```

---

### Test CNT-005: Counter Mode Representation

**Prioritet:** MEDIUM

**Forudsætning:** Konfigurer counter i forskellige modes

**Test Steps:**
```bash
# 1. Konfigurer Counter 2 som SW mode
set counter 2 mode sw
set counter 2 enabled on

# 2. Læs via API
curl -s http://${ESP32_IP}/api/counters/2 | jq .mode
# Forventet: "SW"

# 3. Skift til HW_PCNT
set counter 2 mode pcnt
curl -s http://${ESP32_IP}/api/counters/2 | jq .mode
# Forventet: "HW_PCNT"

# 4. Deaktivér
set counter 2 enabled off
curl -s http://${ESP32_IP}/api/counters/2 | jq .mode
# Forventet: "DISABLED"
```

**Forventet Resultat:**
```
✅ Mode strenge matcher dokumentation
✅ Dynamisk opdatering virker
```

---

## Timer API Tests

### Test TMR-001: GET /api/timers (All)

**Prioritet:** HIGH

**Test Command:**
```bash
curl -s http://${ESP32_IP}/api/timers | jq .
```

**Forventet Response:**
```json
{
  "timers": [
    {"id": 1, "enabled": true, "mode": "ASTABLE", "output": true},
    {"id": 2, "enabled": false, "mode": "DISABLED", "output": false},
    {"id": 3, "enabled": false, "mode": "DISABLED", "output": false},
    {"id": 4, "enabled": false, "mode": "DISABLED", "output": false}
  ]
}
```

**Forventet Resultat:**
```
✅ Status code 200
✅ Array med 4 timers
✅ Korrekte mode strenge
```

---

### Test TMR-002: GET /api/timers/{id} (Single)

**Prioritet:** MEDIUM

**Test Command:**
```bash
curl -s http://${ESP32_IP}/api/timers/1 | jq .
```

**Forventet Response (Astable mode):**
```json
{
  "id": 1,
  "enabled": true,
  "mode": "ASTABLE",
  "output_coil": 10,
  "output": true,
  "on_duration_ms": 1000,
  "off_duration_ms": 500
}
```

**Forventet Resultat:**
```
✅ Status code 200
✅ Mode-specifikke felter inkluderet
```

---

### Test TMR-003: Timer Output State Changes

**Prioritet:** MEDIUM

**Forudsætning:** Timer 1 konfigureret som ASTABLE med kort interval

**Test Steps:**
```bash
# Konfigurer timer
set timer 1 mode 3
set timer 1 on_time 500
set timer 1 off_time 500
set timer 1 enabled on

# Poll output state
for i in $(seq 1 10); do
  curl -s http://${ESP32_IP}/api/timers/1 | jq .output
  sleep 0.3
done
```

**Forventet Resultat:**
```
✅ Output skifter mellem true/false
✅ API reflekterer real-time state
```

---

### Test TMR-004: Invalid Timer ID

**Prioritet:** LOW

**Test Command:**
```bash
curl -s http://${ESP32_IP}/api/timers/5 | jq .
```

**Forventet Response:**
```json
{
  "error": "Invalid timer ID (must be 1-4)",
  "status": 400
}
```

---

## Register API Tests

### Test REG-001: GET Holding Register

**Prioritet:** HIGH

**Test Steps:**
```bash
# 1. Sæt værdi via CLI
write reg 20 value int 12345

# 2. Læs via API
curl -s http://${ESP32_IP}/api/registers/hr/20 | jq .
```

**Forventet Response:**
```json
{
  "address": 20,
  "value": 12345
}
```

---

### Test REG-002: POST Holding Register

**Prioritet:** HIGH

**Test Steps:**
```bash
# 1. Skriv via API
curl -s -X POST \
     -H "Content-Type: application/json" \
     -d '{"value": 54321}' \
     http://${ESP32_IP}/api/registers/hr/20 | jq .

# 2. Verificer via CLI
read reg 20 int
# Forventet: 54321

# 3. Verificer via API
curl -s http://${ESP32_IP}/api/registers/hr/20 | jq .value
# Forventet: 54321
```

**Forventet Response (POST):**
```json
{
  "address": 20,
  "value": 54321,
  "status": "ok"
}
```

---

### Test REG-003: GET Input Register

**Prioritet:** MEDIUM

**Test Command:**
```bash
curl -s http://${ESP32_IP}/api/registers/ir/50 | jq .
```

**Forventet Response:**
```json
{
  "address": 50,
  "value": 1234
}
```

---

### Test REG-004: GET/POST Coil

**Prioritet:** HIGH

**Test Steps:**
```bash
# 1. Skriv TRUE til coil 10
curl -s -X POST \
     -H "Content-Type: application/json" \
     -d '{"value": true}' \
     http://${ESP32_IP}/api/registers/coils/10 | jq .

# 2. Læs tilbage
curl -s http://${ESP32_IP}/api/registers/coils/10 | jq .

# 3. Skriv FALSE
curl -s -X POST \
     -H "Content-Type: application/json" \
     -d '{"value": false}' \
     http://${ESP32_IP}/api/registers/coils/10 | jq .

# 4. Læs tilbage
curl -s http://${ESP32_IP}/api/registers/coils/10 | jq .value
# Forventet: false
```

---

### Test REG-005: GET Discrete Input

**Prioritet:** MEDIUM

**Test Command:**
```bash
curl -s http://${ESP32_IP}/api/registers/di/5 | jq .
```

**Forventet Response:**
```json
{
  "address": 5,
  "value": false
}
```

---

### Test REG-006: Invalid Register Address

**Prioritet:** MEDIUM

**Test Steps:**
```bash
# Over max address
curl -s http://${ESP32_IP}/api/registers/hr/999 | jq .

# Negative address
curl -s http://${ESP32_IP}/api/registers/hr/-1 | jq .
```

**Forventet Response:**
```json
{
  "error": "Invalid register address",
  "status": 400
}
```

---

### Test REG-007: Missing Value Field

**Prioritet:** MEDIUM

**Test Command:**
```bash
# POST uden value
curl -s -X POST \
     -H "Content-Type: application/json" \
     -d '{}' \
     http://${ESP32_IP}/api/registers/hr/20 | jq .
```

**Forventet Response:**
```json
{
  "error": "Missing 'value' field",
  "status": 400
}
```

---

### Test REG-008: Invalid JSON

**Prioritet:** MEDIUM

**Test Command:**
```bash
curl -s -X POST \
     -H "Content-Type: application/json" \
     -d 'not valid json' \
     http://${ESP32_IP}/api/registers/hr/20 | jq .
```

**Forventet Response:**
```json
{
  "error": "Invalid JSON",
  "status": 400
}
```

---

## ST Logic API Tests

### Test LOG-001: GET /api/logic (All Programs)

**Prioritet:** HIGH

**Test Command:**
```bash
curl -s http://${ESP32_IP}/api/logic | jq .
```

**Forventet Response:**
```json
{
  "enabled": true,
  "execution_interval_ms": 10,
  "total_cycles": 123456,
  "programs": [
    {
      "id": 1,
      "name": "Logic1",
      "enabled": true,
      "compiled": true,
      "execution_count": 12345,
      "error_count": 0
    },
    ...
  ]
}
```

---

### Test LOG-002: GET /api/logic/{id} (With Variables)

**Prioritet:** HIGH

**Forudsætning:** ST program uploadet med variabler

**Test Command:**
```bash
curl -s http://${ESP32_IP}/api/logic/1 | jq .
```

**Forventet Response:**
```json
{
  "id": 1,
  "name": "Logic1",
  "enabled": true,
  "compiled": true,
  "execution_count": 12345,
  "error_count": 0,
  "last_execution_us": 125,
  "min_execution_us": 98,
  "max_execution_us": 450,
  "overrun_count": 0,
  "variables": [
    {"index": 0, "name": "counter", "type": "INT", "value": 42},
    {"index": 1, "name": "threshold", "type": "INT", "value": 100},
    {"index": 2, "name": "motor_on", "type": "BOOL", "value": true},
    {"index": 3, "name": "temperature", "type": "REAL", "value": 23.5}
  ]
}
```

---

### Test LOG-003: Variable Types Correct

**Prioritet:** MEDIUM

**Forudsætning:** Upload program med alle typer

```bash
# Upload test program
set logic 1 upload
PROGRAM test
VAR
  bool_var: BOOL := TRUE;
  int_var: INT := 100;
  dint_var: DINT := 100000;
  real_var: REAL := 3.14;
END_VAR
BEGIN
  (* Do nothing *)
END_PROGRAM
END_UPLOAD
set logic 1 enabled:true
```

**Test Command:**
```bash
curl -s http://${ESP32_IP}/api/logic/1 | jq '.variables'
```

**Validering:**
```
✅ BOOL vises som boolean (true/false)
✅ INT vises som heltal
✅ DINT vises som heltal
✅ REAL vises som decimal
```

---

### Test LOG-004: Invalid Logic ID

**Prioritet:** LOW

**Test Command:**
```bash
curl -s http://${ESP32_IP}/api/logic/5 | jq .
```

**Forventet Response:**
```json
{
  "error": "Invalid logic program ID (must be 1-4)",
  "status": 400
}
```

---

## Authentication Tests

### Test AUTH-001: Access Without Auth (Auth Disabled)

**Prioritet:** HIGH

**Forudsætning:**
```bash
set http auth off
```

**Test Command:**
```bash
curl -s http://${ESP32_IP}/api/status | jq .
```

**Forventet Resultat:**
```
✅ Status code 200
✅ Normal response
```

---

### Test AUTH-002: Access Without Credentials (Auth Enabled)

**Prioritet:** HIGH

**Forudsætning:**
```bash
set http auth on
set http username admin
set http password secret123
```

**Test Command:**
```bash
curl -s http://${ESP32_IP}/api/status | jq .
```

**Forventet Response:**
```json
{
  "error": "Authentication required",
  "status": 401
}
```

---

### Test AUTH-003: Access With Correct Credentials

**Prioritet:** HIGH

**Test Command:**
```bash
curl -s -u admin:secret123 http://${ESP32_IP}/api/status | jq .
```

**Forventet Resultat:**
```
✅ Status code 200
✅ Normal response med data
```

---

### Test AUTH-004: Access With Wrong Credentials

**Prioritet:** HIGH

**Test Command:**
```bash
curl -s -u admin:wrongpassword http://${ESP32_IP}/api/status | jq .
```

**Forventet Response:**
```json
{
  "error": "Authentication required",
  "status": 401
}
```

---

## Error Handling Tests

### Test ERR-001: 404 Not Found

**Prioritet:** MEDIUM

**Test Command:**
```bash
curl -s http://${ESP32_IP}/api/nonexistent | jq .
```

**Forventet Response:**
```json
{
  "error": "Not found",
  "status": 404
}
```

---

### Test ERR-002: Method Not Allowed

**Prioritet:** LOW

**Test Command:**
```bash
# DELETE på read-only endpoint
curl -s -X DELETE http://${ESP32_IP}/api/status | jq .
```

**Forventet Response:**
```json
{
  "error": "Method not allowed",
  "status": 405
}
```

---

### Test ERR-003: Malformed Request

**Prioritet:** MEDIUM

**Test Command:**
```bash
# POST uden Content-Type
curl -s -X POST \
     -d '{"value": 123}' \
     http://${ESP32_IP}/api/registers/hr/20 | jq .
```

**Forventet Resultat:**
```
✅ Graceful error handling
✅ Ingen crash
```

---

### Test ERR-004: Large Payload

**Prioritet:** LOW

**Test Command:**
```bash
# Send meget stor JSON
curl -s -X POST \
     -H "Content-Type: application/json" \
     -d '{"value": 123, "padding": "'"$(python3 -c "print('x' * 10000)")"'"}' \
     http://${ESP32_IP}/api/registers/hr/20 | jq .
```

**Forventet Resultat:**
```
✅ Request afvist gracefully
✅ Ingen memory exhaustion
✅ Ingen crash
```

---

### Test ERR-005: Concurrent Requests

**Prioritet:** MEDIUM

**Test Command:**
```bash
# 10 parallelle requests
for i in $(seq 1 10); do
  curl -s http://${ESP32_IP}/api/status &
done
wait
```

**Forventet Resultat:**
```
✅ Alle requests håndteres
✅ Ingen crash
✅ Korrekte responses
```

---

### Test ERR-006: Stress Test

**Prioritet:** LOW

**Test Command:**
```bash
# 100 requests i rækkefølge
for i in $(seq 1 100); do
  curl -s http://${ESP32_IP}/api/status > /dev/null
  echo -n "."
done
echo ""
echo "Done - check ESP32 for crashes"
```

**Forventet Resultat:**
```
✅ Alle 100 requests OK
✅ Ingen memory leak
✅ Ingen crash
```

---

## Performance Tests

### Test PERF-001: Response Time

**Prioritet:** LOW

**Test Command:**
```bash
# Mål response time
for endpoint in status counters timers logic; do
  echo -n "$endpoint: "
  curl -s -o /dev/null -w "%{time_total}s\n" http://${ESP32_IP}/api/$endpoint
done
```

**Forventet Resultat:**
```
status: <0.050s
counters: <0.050s
timers: <0.050s
logic: <0.100s (pga. variabler)
```

---

### Test PERF-002: Throughput

**Prioritet:** LOW

**Test Command:**
```bash
# Mål requests per second
START=$(date +%s)
for i in $(seq 1 100); do
  curl -s http://${ESP32_IP}/api/status > /dev/null
done
END=$(date +%s)
echo "100 requests in $((END-START)) seconds"
echo "~$((100/(END-START))) req/s"
```

**Forventet Resultat:**
```
✅ Mindst 10 req/s
```

---

### Test PERF-003: Memory Under Load

**Prioritet:** MEDIUM

**Test Steps:**
```bash
# 1. Check initial heap
curl -s http://${ESP32_IP}/api/status | jq .heap_free

# 2. Kør 500 requests
for i in $(seq 1 500); do
  curl -s http://${ESP32_IP}/api/counters > /dev/null
done

# 3. Check final heap
curl -s http://${ESP32_IP}/api/status | jq .heap_free

# 4. Sammenlign - difference skal være < 5KB
```

**Forventet Resultat:**
```
✅ Heap stabil efter 500 requests
✅ Ingen memory leak detected
```

---

## Test Results Summary

### Konfiguration Tests
| Test ID | Beskrivelse | Status | Noter |
|---------|-------------|--------|-------|
| CFG-001 | HTTP Enable/Disable | ⬜ | |
| CFG-002 | Port Configuration | ⬜ | |
| CFG-003 | Configuration Persistence | ⬜ | |
| CFG-004 | Statistics | ⬜ | |

### System Status Tests
| Test ID | Beskrivelse | Status | Noter |
|---------|-------------|--------|-------|
| SYS-001 | GET /api/status | ⬜ | |
| SYS-002 | Uptime Increment | ⬜ | |
| SYS-003 | Heap Stability | ⬜ | |

### Counter API Tests
| Test ID | Beskrivelse | Status | Noter |
|---------|-------------|--------|-------|
| CNT-001 | GET All Counters | ⬜ | |
| CNT-002 | GET Single Counter | ⬜ | |
| CNT-003 | Invalid Counter ID | ⬜ | |
| CNT-004 | Value Matches CLI | ⬜ | |
| CNT-005 | Mode Representation | ⬜ | |

### Timer API Tests
| Test ID | Beskrivelse | Status | Noter |
|---------|-------------|--------|-------|
| TMR-001 | GET All Timers | ⬜ | |
| TMR-002 | GET Single Timer | ⬜ | |
| TMR-003 | Output State Changes | ⬜ | |
| TMR-004 | Invalid Timer ID | ⬜ | |

### Register API Tests
| Test ID | Beskrivelse | Status | Noter |
|---------|-------------|--------|-------|
| REG-001 | GET Holding Register | ⬜ | |
| REG-002 | POST Holding Register | ⬜ | |
| REG-003 | GET Input Register | ⬜ | |
| REG-004 | GET/POST Coil | ⬜ | |
| REG-005 | GET Discrete Input | ⬜ | |
| REG-006 | Invalid Address | ⬜ | |
| REG-007 | Missing Value | ⬜ | |
| REG-008 | Invalid JSON | ⬜ | |

### ST Logic API Tests
| Test ID | Beskrivelse | Status | Noter |
|---------|-------------|--------|-------|
| LOG-001 | GET All Programs | ⬜ | |
| LOG-002 | GET Program Variables | ⬜ | |
| LOG-003 | Variable Types | ⬜ | |
| LOG-004 | Invalid Logic ID | ⬜ | |

### Authentication Tests
| Test ID | Beskrivelse | Status | Noter |
|---------|-------------|--------|-------|
| AUTH-001 | Access Without Auth | ⬜ | |
| AUTH-002 | Missing Credentials | ⬜ | |
| AUTH-003 | Correct Credentials | ⬜ | |
| AUTH-004 | Wrong Credentials | ⬜ | |

### Error Handling Tests
| Test ID | Beskrivelse | Status | Noter |
|---------|-------------|--------|-------|
| ERR-001 | 404 Not Found | ⬜ | |
| ERR-002 | Method Not Allowed | ⬜ | |
| ERR-003 | Malformed Request | ⬜ | |
| ERR-004 | Large Payload | ⬜ | |
| ERR-005 | Concurrent Requests | ⬜ | |
| ERR-006 | Stress Test | ⬜ | |

### Performance Tests
| Test ID | Beskrivelse | Status | Noter |
|---------|-------------|--------|-------|
| PERF-001 | Response Time | ⬜ | |
| PERF-002 | Throughput | ⬜ | |
| PERF-003 | Memory Under Load | ⬜ | |

---

## Sign-Off

**Test Plan Created:** 2026-01-25
**Version:** v6.0.0 Build #1108
**Status:** Ready for Execution

**Tester:** _______________
**Date:** _______________
**Overall Result:** ⬜ PASS / ⬜ FAIL

---

**End of API Test Plan**
