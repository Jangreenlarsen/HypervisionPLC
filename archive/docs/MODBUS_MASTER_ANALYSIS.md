# Modbus Master Implementation - Dybdeanalyse

<!-- DOC-STATUS: historisk -->
> ⚠️ **Historisk dokument — vedligeholdes ikke længere.** Skrevet til en ældre firmwareversion og kan være forældet (CLI-syntaks, pins, endpoints, standardværdier). Den aktuelle, vedligeholdte dokumentation er [brugermanualen](docs/manual/00_INDEKS.md) (CLI: appendiks A, REST: appendiks B, ST: appendiks D). Markeret ved dokumentationsgennemgang 2026-09-28.


**Dato:** 2026-01-01
**Version:** v4.5.1 (Build #910)
**Formål:** Analyse af nuværende Modbus Master implementation til identifikation af forbedringspotentiale

---

## 📋 Executive Summary

Systemets Modbus Master implementation på UART1 er **funktionelt og production-ready**, men har flere områder med forbedringspotentiale:

### ✅ Styrker
- Korrekt Modbus RTU protocol implementation (FC01-FC06)
- Intelligent frame detection baseret på function code
- Timeout håndtering med per-byte reset
- Parameter validering (slave ID, address)
- Statistik tracking (success/timeout/CRC/exception)
- Inter-frame delay support

### ⚠️ Identificerede Issues
1. **KRITISK:** `g_mb_request_count` bliver ALDRIG resettet → rate limiting fungerer ikke korrekt
2. **MEDIUM:** Ingen retry logic → ST Logic skal håndtere alle fejl manuelt
3. **LOW:** Ingen multi-slave health monitoring
4. **LOW:** Ingen diagnostics for per-slave performance

---

## 🔄 Request/Response Flow

### High-Level Data Flow

```
┌─────────────────┐
│   ST Logic      │  VAR result: INT;
│   Program       │  result := MB_READ_HOLDING(1, 100);
└────────┬────────┘
         │ (1) Call ST builtin function
         ↓
┌─────────────────────────────┐
│ st_builtin_mb_read_holding  │  src/st_builtin_modbus.cpp
│ - Validate slave_id (1-247) │
│ - Validate address (0-65535)│
│ - Check request limit       │ ← ⚠️ BUG: count never resets!
│ - Apply inter-frame delay   │
└────────┬────────────────────┘
         │ (2) Call master function
         ↓
┌──────────────────────────────┐
│ modbus_master_read_holding   │  src/modbus_master.cpp
│ - Build Modbus RTU frame     │
│ - Calculate CRC16            │
│ - Increment stats counters   │
└────────┬─────────────────────┘
         │ (3) Send request
         ↓
┌───────────────────────────────┐
│ modbus_master_send_request    │  src/modbus_master.cpp
│ - Flush RX buffer             │
│ - Set DE/RE → TX mode         │
│ - Send frame via UART1        │
│ - Set DE/RE → RX mode         │
│ - Wait for response (timeout) │
│ - Validate CRC                │
│ - Check for exception         │
└────────┬──────────────────────┘
         │ (4) UART transmission
         ↓
┌─────────────────┐
│   UART1         │  HardwareSerial(1)
│   TX: Pin 25    │  Default: 9600 baud, 8N1
│   RX: Pin 26    │
│   DE: Pin 27    │  MAX485 direction control
└────────┬────────┘
         │ (5) RS-485 bus
         ↓
┌─────────────────┐
│  Remote Slave   │  External Modbus device
│  (Slave ID: 1)  │
└────────┬────────┘
         │ (6) Response frame
         ↓
[Same path back to ST Logic with result value]
```

### Detailed Execution Sequence

#### Phase 1: ST Logic Call
```structured-text
VAR
  temp: INT;
  slave_id: INT := 1;
  reg_addr: INT := 100;
END_VAR

temp := MB_READ_HOLDING(slave_id, reg_addr);
(* ST VM executes OP_CALL_BUILTIN with ST_BUILTIN_MB_READ_HOLDING *)
```

**Location:** `src/st_vm.cpp` (OP_CALL_BUILTIN handler)

#### Phase 2: Wrapper Function (st_builtin_modbus.cpp:118-156)

```cpp
st_value_t st_builtin_mb_read_holding(st_value_t slave_id, st_value_t address) {
  // (1) Rate limiting check
  if (g_mb_request_count >= g_modbus_master_config.max_requests_per_cycle) {
    return MB_MAX_REQUESTS_EXCEEDED;
  }
  g_mb_request_count++;  // ⚠️ NEVER RESET!

  // (2) Validate parameters
  if (slave_id.int_val < 1 || slave_id.int_val > 247)
    return MB_INVALID_SLAVE;
  if (address.int_val < 0 || address.int_val > 65535)
    return MB_INVALID_ADDRESS;

  // (3) Call master function
  uint16_t register_value = 0;
  mb_error_code_t err = modbus_master_read_holding(
    (uint8_t)slave_id.int_val,
    (uint16_t)address.int_val,
    &register_value
  );

  // (4) Update global status
  g_mb_last_error = err;
  g_mb_success = (err == MB_OK);

  // (5) Inter-frame delay
  if (g_modbus_master_config.inter_frame_delay > 0) {
    delay(g_modbus_master_config.inter_frame_delay);  // Blocking!
  }

  return (int32_t)register_value;
}
```

**Parametre:**
- `slave_id`: Modbus slave adresse (1-247)
- `address`: Register adresse (0-65535)

**Return:**
- Register værdi (16-bit unsigned → int32_t)
- `0` ved fejl (check `g_mb_last_error`)

#### Phase 3: Master Function (modbus_master.cpp:313-344)

```cpp
mb_error_code_t modbus_master_read_holding(uint8_t slave_id, uint16_t address, uint16_t *result) {
  uint8_t request[8];
  uint8_t response[9];
  uint8_t response_len;

  // Build Modbus RTU request frame:
  // [slave_id][FC03][addr_hi][addr_lo][qty_hi][qty_lo][CRC_lo][CRC_hi]
  request[0] = slave_id;
  request[1] = 0x03;  // FC03: Read Holding Registers
  request[2] = (address >> 8) & 0xFF;
  request[3] = address & 0xFF;
  request[4] = 0x00;  // Quantity high byte (read 1 register)
  request[5] = 0x01;  // Quantity low byte
  uint16_t crc = modbus_master_calc_crc(request, 6);
  request[6] = crc & 0xFF;
  request[7] = (crc >> 8) & 0xFF;

  // Update statistics
  g_modbus_master_config.total_requests++;

  // Send request and wait for response
  mb_error_code_t err = modbus_master_send_request(request, 8, response, &response_len, sizeof(response));
  if (err != MB_OK) {
    *result = 0;
    return err;
  }

  // Parse response: [slave_id][FC03][byte_count][data_hi][data_lo][CRC_lo][CRC_hi]
  if (response_len >= 7 && response[1] == 0x03) {
    *result = (response[3] << 8) | response[4];
    return MB_OK;
  }

  return MB_CRC_ERROR;
}
```

**Frame Format:**
- **Request:** 8 bytes total
- **Response:** 7 bytes (normal) eller 5 bytes (exception)

#### Phase 4: Request/Response Handler (modbus_master.cpp:128-242)

```cpp
mb_error_code_t modbus_master_send_request(
  const uint8_t *request,
  uint8_t request_len,
  uint8_t *response,
  uint8_t *response_len,
  uint8_t max_response_len
) {
  // (1) Flush RX buffer
  while (ModbusSerial.available()) {
    ModbusSerial.read();
  }

  // (2) Switch to TX mode
  digitalWrite(MODBUS_MASTER_DE_PIN, HIGH);
  delayMicroseconds(50);

  // (3) Transmit request
  ModbusSerial.write(request, request_len);
  ModbusSerial.flush();  // Wait for TX complete

  // (4) Switch to RX mode
  delayMicroseconds(50);
  digitalWrite(MODBUS_MASTER_DE_PIN, LOW);

  // (5) Wait for response with intelligent timeout
  uint32_t start_time = millis();
  uint8_t bytes_received = 0;
  bool timeout = false;

  while (bytes_received < max_response_len) {
    // Check timeout (default: 500ms)
    if (millis() - start_time > g_modbus_master_config.timeout_ms) {
      timeout = true;
      break;
    }

    // Read available bytes
    if (ModbusSerial.available()) {
      response[bytes_received++] = ModbusSerial.read();
      start_time = millis();  // ✅ Reset timeout on each byte!

      // Intelligent frame detection (lines 174-209)
      // - Exception response: 5 bytes
      // - FC01/02 (Read Coils): 3 + byte_count + 2
      // - FC03/04 (Read Registers): 3 + byte_count + 2
      // - FC05/06 (Write Single): 8 bytes
      if (frame_complete) break;
    }
  }

  *response_len = bytes_received;

  // (6) Validate response
  if (timeout || bytes_received == 0) {
    g_modbus_master_config.timeout_errors++;
    return MB_TIMEOUT;
  }

  // (7) Check CRC
  if (bytes_received >= 3) {
    uint16_t received_crc = (response[bytes_received - 1] << 8) | response[bytes_received - 2];
    uint16_t calculated_crc = modbus_master_calc_crc(response, bytes_received - 2);

    if (received_crc != calculated_crc) {
      g_modbus_master_config.crc_errors++;
      return MB_CRC_ERROR;
    }
  }

  // (8) Check for Modbus exception
  if (response[1] & 0x80) {
    g_modbus_master_config.exception_errors++;
    return MB_EXCEPTION;
  }

  g_modbus_master_config.successful_requests++;
  return MB_OK;
}
```

---

## 🔍 Identificerede Problemer

### 🔴 KRITISK: BUG-133 - Request Count Reset Mangler

**Beskrivelse:**
`g_mb_request_count` bliver inkrementeret ved hver Modbus request, men bliver ALDRIG resettet efter ST execution cycle.

**Impact:**
- Efter 10 requests (default `max_requests_per_cycle`) vil ALLE fremtidige requests blive blokeret permanent
- System vil stoppe med at kunne kommunikere med remote slaves
- Kræver reboot for at rette

**Lokation:**
- `src/st_builtin_modbus.cpp:17` - Global variable definition
- `src/st_builtin_modbus.cpp:29` - Inkrement (intet decrement/reset)

**Forventet Adfærd:**
```cpp
// In st_logic_engine.cpp - st_logic_execute_all()
void st_logic_execute_all(st_logic_engine_state_t *state) {
  // Reset Modbus request counter at start of each cycle
  g_mb_request_count = 0;  // ← MANGLER!

  for (uint8_t prog_id = 0; prog_id < MAX_ST_LOGIC_PROGRAMS; prog_id++) {
    st_logic_execute_program(state, prog_id);
  }
}
```

**Severity:** KRITISK
**Fix Required:** JA (BUG-133)

---

### 🟡 MEDIUM: Ingen Retry Logic

**Beskrivelse:**
Ved timeout, CRC error eller Modbus exception returneres fejl direkte til ST Logic uden retry.

**Impact:**
- ST programmer skal selv implementere retry logic
- Transient fejl (EMI noise, timing issues) kræver eksplicit håndtering
- Mere kompleks ST kode nødvendig

**Nuværende Adfærd:**
```structured-text
VAR
  temp: INT;
  error: INT;
END_VAR

temp := MB_READ_HOLDING(1, 100);
error := mb_last_error;

IF error <> 0 THEN
  (* Programmer skal selv retry! *)
  temp := MB_READ_HOLDING(1, 100);  (* Manual retry *)
END_IF;
```

**Alternativ Design:**
```cpp
// In modbus_master_send_request()
#define MODBUS_MASTER_MAX_RETRIES 3

for (uint8_t retry = 0; retry < MODBUS_MASTER_MAX_RETRIES; retry++) {
  err = modbus_master_send_request_internal(...);
  if (err == MB_OK) break;
  delay(50);  // Short delay before retry
}
```

**Severity:** MEDIUM
**Fix Required:** OPTIONAL (v4.6.0+)

---

### 🟢 LOW: Ingen Per-Slave Health Monitoring

**Beskrivelse:**
Systemet tracker global statistik (success/timeout/CRC), men ikke per slave device.

**Impact:**
- Svært at identificere problematiske slaves
- Ingen automatic blacklisting af døde devices
- Ingen diagnostics for `show modbus master stats slave 1`

**Ønsket Feature:**
```cpp
typedef struct {
  uint8_t slave_id;
  uint32_t requests;
  uint32_t timeouts;
  uint32_t crc_errors;
  uint32_t exceptions;
  uint32_t consecutive_failures;
  bool blacklisted;  // Auto-disable ved persistent failures
} modbus_slave_stats_t;

modbus_slave_stats_t g_slave_stats[MAX_MONITORED_SLAVES];  // Track up to 8 slaves
```

**CLI Commands:**
```bash
show modbus master stats         # Global stats (nuværende)
show modbus master stats slave 1 # Per-slave stats (ny)
set modbus master blacklist 1    # Manually blacklist slave
```

**Severity:** LOW
**Fix Required:** OPTIONAL (v4.7.0+)

---

### 🟢 LOW: Blocking Inter-Frame Delay

**Beskrivelse:**
`delay(g_modbus_master_config.inter_frame_delay)` i `st_builtin_modbus.cpp` er blocking (default 10ms).

**Impact:**
- ST execution cycle forlænges ved multiple Modbus requests
- 10 requests = 10ms × 10 = 100ms ekstra latency
- Kunne optimeres med non-blocking approach

**Nuværende:**
```cpp
// In st_builtin_mb_read_holding()
delay(g_modbus_master_config.inter_frame_delay);  // 10ms blocking
```

**Alternativ (Non-Blocking):**
```cpp
static uint32_t last_request_time = 0;

// Wait for minimum inter-frame time
while (millis() - last_request_time < g_modbus_master_config.inter_frame_delay) {
  // Could yield to other tasks (if RTOS)
}
last_request_time = millis();
```

**Severity:** LOW (10ms er acceptabelt for de fleste use cases)
**Fix Required:** OPTIONAL (v5.0.0+)

---

## 📊 Performance Karakteristik

### Timing Analysis

**Single Request (Read Holding Register):**
```
Total time = TX + Slave Processing + RX + Inter-frame Delay

TX time     = (8 bytes × 11 bits) / 9600 baud = 9.2ms
RX time     = (7 bytes × 11 bits) / 9600 baud = 8.0ms
Slave proc  = ~5-20ms (depends on slave device)
Inter-frame = 10ms (configurable)
---------------------------------------------------------
Total       = ~32-47ms per request @ 9600 baud
            = ~15-25ms per request @ 19200 baud
```

**10 Requests (max_requests_per_cycle = 10):**
```
@ 9600 baud:  10 × 40ms = 400ms
@ 19200 baud: 10 × 20ms = 200ms
```

**Impact på ST Execution Cycle:**
- Standard cycle uden Modbus: ~10-50ms
- Med 10 Modbus requests @ 9600: +400ms → Total 450ms
- **Anbefaling:** Brug højere baudrate (19200+) eller reducer antal requests

---

### Baudrate Recommendations

| Baudrate | Single Request | 10 Requests | Use Case |
|----------|---------------|-------------|----------|
| 9600     | ~40ms         | ~400ms      | Legacy devices, long cables (>200m) |
| 19200    | ~20ms         | ~200ms      | **Recommended** - Good balance |
| 38400    | ~10ms         | ~100ms      | Short cables (<50m), fast polling |
| 57600    | ~7ms          | ~70ms       | High-speed applications |
| 115200   | ~4ms          | ~40ms       | Very short cables (<10m) |

**Note:** Higher baudrates kræver bedre kabelkvalitet og kortere afstande.

---

## 🔧 Response Buffer Management

### Buffer Sizes

```cpp
// In modbus_master.cpp
uint8_t response[8];  // Read Coil/Input (max 5 bytes, but allocated 8)
uint8_t response[9];  // Read Holding/Input Register (7 bytes + margin)
```

**Frame Size Analysis:**
- **FC01/02 (Read Coils):** 5 bytes exception, 3+N+2 bytes normal
- **FC03/04 (Read Registers):** 5 bytes exception, 3+2N+2 bytes normal (N=1 → 7 bytes)
- **FC05/06 (Write Single):** 5 bytes exception, 8 bytes echo

**Current Implementation:**
- ✅ Sufficient for single-register reads (1 register = 7 bytes)
- ❌ **Ikke** skalerbart til multi-register reads (10 registers → 25 bytes)

**Future Enhancement (v5.0+):**
```cpp
#define MODBUS_MAX_REGISTERS 16  // Read up to 16 registers at once
uint8_t response[3 + (MODBUS_MAX_REGISTERS * 2) + 2];  // 3 + 32 + 2 = 37 bytes

// New function:
mb_error_code_t modbus_master_read_holding_multi(
  uint8_t slave_id,
  uint16_t start_address,
  uint16_t count,
  uint16_t *results
);
```

---

## 🚦 Error Handling & Statistics

### Error Codes (types.h:313-322)

```cpp
typedef enum {
  MB_OK = 0,                    // Success
  MB_TIMEOUT = 1,               // No response within timeout
  MB_CRC_ERROR = 2,             // CRC mismatch
  MB_EXCEPTION = 3,             // Modbus exception response
  MB_MAX_REQUESTS_EXCEEDED = 4, // Rate limit exceeded
  MB_NOT_ENABLED = 5,           // Master not enabled
  MB_INVALID_SLAVE = 6,         // Slave ID not 1-247
  MB_INVALID_ADDRESS = 7        // Address not 0-65535
} mb_error_code_t;
```

### Global Status Variables (st_builtin_modbus.cpp:15-17)

```cpp
int32_t g_mb_last_error = MB_OK;   // Last error code
bool g_mb_success = false;         // Last operation success flag
uint8_t g_mb_request_count = 0;    // ⚠️ NEVER RESET!
```

**Usage fra ST Logic:**
```structured-text
VAR
  temp: INT;
END_VAR

temp := MB_READ_HOLDING(1, 100);

IF mb_success THEN
  (* OK - use temp *)
ELSE
  IF mb_last_error = 1 THEN
    (* Timeout - slave not responding *)
  ELSIF mb_last_error = 2 THEN
    (* CRC error - noisy line *)
  ELSIF mb_last_error = 3 THEN
    (* Modbus exception - invalid address? *)
  END_IF;
END_IF;
```

### Runtime Statistics (modbus_master_config_t)

```cpp
typedef struct {
  // ... config fields ...

  uint32_t total_requests;      // Total requests sent
  uint32_t successful_requests; // Successful responses
  uint32_t timeout_errors;      // Timeout count
  uint32_t crc_errors;          // CRC error count
  uint32_t exception_errors;    // Modbus exception count
} modbus_master_config_t;
```

**CLI Command:**
```bash
show modbus master stats

Output:
  Total Requests:      1234
  Successful:          1180 (95.6%)
  Timeouts:            42 (3.4%)
  CRC Errors:          10 (0.8%)
  Exceptions:          2 (0.2%)
```

---

## 🎯 Multi-Device Polling

### Current Capability

Systemet understøtter polling af multiple slaves via forskellige slave IDs:

```structured-text
VAR
  temp_slave1: INT;
  temp_slave2: INT;
  temp_slave3: INT;
END_VAR

temp_slave1 := MB_READ_HOLDING(1, 100);  (* Slave 1 *)
temp_slave2 := MB_READ_HOLDING(2, 100);  (* Slave 2 *)
temp_slave3 := MB_READ_HOLDING(3, 100);  (* Slave 3 *)
```

**Execution Time:**
- 3 slaves × 40ms = 120ms @ 9600 baud
- 3 slaves × 20ms = 60ms @ 19200 baud

### Identified Limitations

1. **Ingen Fairness Scheduling:**
   - Hvis Slave 1 altid timeouts (500ms), bliver Slave 2/3 forsinket
   - Ingen round-robin eller priority-based scheduling

2. **Ingen Request Queue:**
   - Requests behandles synkront i rækkefølge
   - Kunne optimeres med async request queue

3. **Ingen Slave Grouping:**
   - Kunne implementere "grupper" til parallel polling (hvis multiple UARTs)

### Future Enhancement (v5.0+)

```cpp
typedef struct {
  uint8_t slave_id;
  uint16_t address;
  uint8_t function_code;
  uint16_t value;  // For writes
  bool pending;
  uint32_t timestamp;
} modbus_request_t;

#define MODBUS_REQUEST_QUEUE_SIZE 16
modbus_request_t g_request_queue[MODBUS_REQUEST_QUEUE_SIZE];

// Queue request instead of blocking
void modbus_master_queue_request(modbus_request_t *req);

// Process queue in background task
void modbus_master_process_queue();
```

---

## 📈 Recommendations

### Prioritet 1: FIX BUG-133 (KRITISK)

**Handling:** Reset `g_mb_request_count` i ST execution cycle

**Implementation:**
1. Tilføj `extern uint8_t g_mb_request_count;` til `st_builtin_modbus.h`
2. I `st_logic_engine.cpp` → `st_logic_execute_all()`:
   ```cpp
   void st_logic_execute_all(st_logic_engine_state_t *state) {
     g_mb_request_count = 0;  // Reset request counter
     // ... existing code ...
   }
   ```

**Test:**
```structured-text
(* Test: More than 10 requests should work after reset *)
VAR i: INT; temp: INT; END_VAR

FOR i := 1 TO 20 DO
  temp := MB_READ_HOLDING(1, i);
END_FOR;

(* Should complete successfully if reset works *)
```

**Estimated Effort:** 15 minutter
**Impact:** HØJI - Kritisk bug fix

---

### Prioritet 2: Add Retry Logic (OPTIONAL)

**Handling:** Automatisk retry ved transient errors

**Implementation:**
```cpp
// In constants.h
#define MODBUS_MASTER_MAX_RETRIES 2  // 0 = no retries (current)

// In modbus_master.cpp
mb_error_code_t modbus_master_read_holding_with_retry(...) {
  for (uint8_t retry = 0; retry <= MODBUS_MASTER_MAX_RETRIES; retry++) {
    err = modbus_master_read_holding(...);
    if (err == MB_OK) return MB_OK;
    if (err == MB_EXCEPTION) return err;  // Don't retry exceptions
    delay(20);  // Short delay before retry
  }
  return err;  // Return last error
}
```

**Estimated Effort:** 2 timer
**Impact:** MEDIUM - Øget robustness

---

### Prioritet 3: Per-Slave Statistics (OPTIONAL)

**Handling:** Track success/error rates per slave device

**Implementation:**
1. Tilføj `modbus_slave_stats_t` struct til `types.h`
2. Implementér tracking i `modbus_master_send_request()`
3. Tilføj CLI kommando `show modbus master stats slave <id>`

**Estimated Effort:** 4 timer
**Impact:** LOW - Bedre diagnostics

---

## 📚 Files Reference

### Core Implementation
- `src/modbus_master.cpp` - Core Modbus RTU Master (421 lines)
- `include/modbus_master.h` - Public API (149 lines)
- `src/st_builtin_modbus.cpp` - ST Logic wrapper functions (275 lines)
- `include/st_builtin_modbus.h` - ST builtin declarations (82 lines)

### Configuration & Types
- `include/types.h:313-339` - Error codes, config struct
- `include/constants.h:268-282` - Pins, defaults, limits

### Integration Points
- `src/st_builtins.cpp:298-322` - Builtin dispatcher
- `src/st_vm.cpp` - VM opcode handler (OP_CALL_BUILTIN)
- `src/cli_show.cpp` - Statistics display
- `src/cli_commands_modbus_master.cpp` - CLI configuration

---

## 🔬 Test Cases

### Test 1: Single Request
```structured-text
VAR temp: INT; END_VAR
temp := MB_READ_HOLDING(1, 100);
(* Expected: temp = register value, mb_success = TRUE *)
```

### Test 2: Multiple Slaves
```structured-text
VAR t1, t2, t3: INT; END_VAR
t1 := MB_READ_HOLDING(1, 100);
t2 := MB_READ_HOLDING(2, 100);
t3 := MB_READ_HOLDING(3, 100);
(* Expected: All succeed if slaves present *)
```

### Test 3: Non-Existent Slave (Timeout)
```structured-text
VAR temp: INT; error: INT; END_VAR
temp := MB_READ_HOLDING(99, 100);
error := mb_last_error;
(* Expected: error = 1 (MB_TIMEOUT), mb_success = FALSE *)
```

### Test 4: Rate Limiting (BUG-133 REPRODUCER)
```structured-text
VAR i: INT; temp: INT; END_VAR
FOR i := 1 TO 15 DO
  temp := MB_READ_HOLDING(1, i);
END_FOR;
(* Expected (CURRENT BUG): Fails after 10 requests *)
(* Expected (AFTER FIX): All 15 succeed *)
```

### Test 5: Invalid Parameters
```structured-text
VAR temp: INT; error: INT; END_VAR
temp := MB_READ_HOLDING(0, 100);     (* Invalid slave ID *)
error := mb_last_error;
(* Expected: error = 6 (MB_INVALID_SLAVE) *)

temp := MB_READ_HOLDING(1, 70000);   (* Invalid address *)
error := mb_last_error;
(* Expected: error = 7 (MB_INVALID_ADDRESS) *)
```

---

## 📝 Konklusion

### Samlet Vurdering

Modbus Master implementationen er **solid og production-ready** med følgende karakteristika:

**Styrker:**
- ✅ Korrekt Modbus RTU protocol (RFC compliant)
- ✅ Robust timeout håndtering med per-byte reset
- ✅ Intelligent frame detection
- ✅ Comprehensive error reporting
- ✅ Parameter validering
- ✅ Statistics tracking

**Kritiske Issues:**
- 🔴 **BUG-133:** `g_mb_request_count` reset mangler → System stopper efter 10 requests

**Forbedringspotentiale:**
- 🟡 Retry logic (OPTIONAL)
- 🟢 Per-slave health monitoring (OPTIONAL)
- 🟢 Non-blocking inter-frame delay (OPTIONAL)
- 🟢 Multi-register read support (FUTURE)

### Næste Skridt

1. **Umiddelbart:** FIX BUG-133 (15 min) → v4.5.2
2. **Kort sigt:** Implementér retry logic (2 timer) → v4.6.0
3. **Lang sigt:** Per-slave diagnostics (4 timer) → v4.7.0

---

**Analyseret af:** Claude Sonnet 4.5
**Review Status:** ✅ KOMPLET
**Anbefaling:** Fix BUG-133 før deploy til produktion
