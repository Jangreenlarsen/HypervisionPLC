# Modbus Master Enhancements - Design Dokument

<!-- DOC-STATUS: historisk -->
> ⚠️ **Historisk dokument — vedligeholdes ikke længere.** Skrevet til en ældre firmwareversion og kan være forældet (CLI-syntaks, pins, endpoints, standardværdier). Den aktuelle, vedligeholdte dokumentation er [brugermanualen](docs/manual/00_INDEKS.md) (CLI: appendiks A, REST: appendiks B, ST: appendiks D). Markeret ved dokumentationsgennemgang 2026-09-28.


**Dato:** 2026-01-01
**Version:** v4.5.1 → v4.6.0+
**Formål:** Design af forbedringer til Modbus Master baseret på MODBUS_MASTER_ANALYSIS.md

---

## 📋 Executive Summary

Baseret på dybdeanalysen af Modbus Master implementation er følgende forbedringer identificeret:

### Prioritet 1: KRITISK BUG FIX (v4.5.2)
- ✅ **BUG-133:** Reset `g_mb_request_count` efter hver ST execution cycle

### Prioritet 2: ROBUSTNESS (v4.6.0)
- 🔄 **ENHANCE-001:** Automatisk retry logic ved transient errors
- 📊 **ENHANCE-002:** Per-slave health monitoring og diagnostics

### Prioritet 3: PERFORMANCE (v4.7.0+)
- ⚡ **ENHANCE-003:** Non-blocking inter-frame delay
- 📦 **ENHANCE-004:** Multi-register read support (bulk operations)

---

## 🔴 BUG-133: Request Counter Reset (KRITISK)

### Problem Statement
`g_mb_request_count` bliver inkrementeret ved hver Modbus request, men bliver ALDRIG resettet efter ST execution cycle. Efter 10 requests (default `max_requests_per_cycle`) vil ALLE fremtidige requests blive blokeret permanent.

### Root Cause Analysis
```cpp
// src/st_builtin_modbus.cpp:24-30
static bool check_request_limit() {
  if (g_mb_request_count >= g_modbus_master_config.max_requests_per_cycle) {
    g_mb_last_error = MB_MAX_REQUESTS_EXCEEDED;
    g_mb_success = false;
    return false;
  }
  g_mb_request_count++;  // ← Incremented here
  return true;
}

// ❌ NOWHERE in the codebase is g_mb_request_count reset to 0!
```

### Solution Design

**Option 1: Reset i ST Logic Engine (ANBEFALET)**
```cpp
// File: src/st_logic_engine.cpp
// Location: st_logic_execute_all() function

void st_logic_execute_all(st_logic_engine_state_t *state) {
  // Reset Modbus request counter at start of each cycle
  extern uint8_t g_mb_request_count;
  g_mb_request_count = 0;  // ← FIX

  bool all_success = true;

  for (uint8_t prog_id = 0; prog_id < MAX_ST_LOGIC_PROGRAMS; prog_id++) {
    st_logic_program_config_t *prog = st_logic_get_program(state, prog_id);
    if (!prog || !prog->enabled || !prog->compiled) continue;

    bool success = st_logic_execute_program(state, prog_id);
    if (!success) {
      all_success = false;
    }
  }

  return all_success;
}
```

**Option 2: Reset i st_builtin_modbus.cpp (ALTERNATIV)**
```cpp
// File: src/st_builtin_modbus.cpp
// Add public reset function

void st_builtin_mb_reset_request_count() {
  g_mb_request_count = 0;
}

// Call from st_logic_engine.cpp:
#include "st_builtin_modbus.h"
// In st_logic_execute_all():
st_builtin_mb_reset_request_count();
```

### Implementation Steps

1. **Tilføj extern declaration i st_builtin_modbus.h:**
   ```cpp
   // In include/st_builtin_modbus.h
   extern uint8_t g_mb_request_count; // Current request count in this execution

   // Add reset function (Option 2):
   void st_builtin_mb_reset_request_count();
   ```

2. **Implementér reset i st_logic_engine.cpp:**
   ```cpp
   // In src/st_logic_engine.cpp
   #include "st_builtin_modbus.h"

   void st_logic_execute_all(st_logic_engine_state_t *state) {
     // Reset Modbus request counter
     g_mb_request_count = 0;  // Option 1 (direct access)
     // OR
     st_builtin_mb_reset_request_count();  // Option 2 (via function)

     // ... rest of function ...
   }
   ```

3. **Test:**
   ```structured-text
   (* Test: More than 10 requests in single cycle *)
   VAR i: INT; temp: INT; END_VAR

   FOR i := 1 TO 15 DO
     temp := MB_READ_HOLDING(1, i);
   END_FOR;

   (* Expected BEFORE fix: Fails at request 11 *)
   (* Expected AFTER fix: All 15 succeed *)
   ```

### Files to Modify
- `include/st_builtin_modbus.h` - Add extern declaration
- `src/st_logic_engine.cpp` - Add reset call
- (Optional) `src/st_builtin_modbus.cpp` - Add reset function

### Estimated Effort
- **Implementation:** 15 minutter
- **Testing:** 10 minutter
- **Total:** 25 minutter

### Impact
- **Severity:** KRITISK
- **Risk:** LAVI (minimal code change)
- **Benefit:** HØJI (eliminates request blocking bug)

---

## 🔄 ENHANCE-001: Automatisk Retry Logic

### Problem Statement
Ved timeout, CRC error eller Modbus exception returneres fejl direkte til ST Logic uden retry. Transient fejl (EMI noise, timing issues) kræver eksplicit håndtering i ST programmer.

### Current Behavior
```structured-text
VAR temp: INT; error: INT; END_VAR

temp := MB_READ_HOLDING(1, 100);
error := mb_last_error;

IF error <> 0 THEN
  (* Programmer skal selv retry! *)
  temp := MB_READ_HOLDING(1, 100);  (* Manual retry *)
END_IF;
```

### Solution Design

**Configurable Retry Logic:**
```cpp
// In include/constants.h
#define MODBUS_MASTER_MAX_RETRIES      2     // 0-5 retries
#define MODBUS_MASTER_RETRY_DELAY_MS   20    // Delay between retries

// In types.h - modbus_master_config_t
typedef struct {
  // ... existing fields ...
  uint8_t max_retries;           // Retry count (0 = no retries)
  uint16_t retry_delay_ms;       // Delay between retries

  // Statistics
  uint32_t retry_count;          // Total retries performed
} modbus_master_config_t;
```

**Implementation:**
```cpp
// In src/modbus_master.cpp
mb_error_code_t modbus_master_read_holding_with_retry(
  uint8_t slave_id,
  uint16_t address,
  uint16_t *result
) {
  mb_error_code_t err;

  for (uint8_t attempt = 0; attempt <= g_modbus_master_config.max_retries; attempt++) {
    err = modbus_master_read_holding(slave_id, address, result);

    if (err == MB_OK) {
      return MB_OK;  // Success - no retry needed
    }

    // Don't retry on permanent errors
    if (err == MB_EXCEPTION || err == MB_NOT_ENABLED ||
        err == MB_INVALID_SLAVE || err == MB_INVALID_ADDRESS) {
      return err;  // Permanent error - no retry
    }

    // Retry on transient errors (TIMEOUT, CRC_ERROR)
    if (attempt < g_modbus_master_config.max_retries) {
      g_modbus_master_config.retry_count++;
      delay(g_modbus_master_config.retry_delay_ms);
    }
  }

  return err;  // Return last error after all retries exhausted
}
```

**CLI Configuration:**
```bash
set modbus master max-retries 2      # 0-5 retries
set modbus master retry-delay 20     # ms between retries
show modbus master config
show modbus master stats             # Shows retry_count
```

### Retry Strategy Decision Matrix

| Error Code | Retry? | Reason |
|-----------|--------|---------|
| MB_TIMEOUT | ✅ YES | Slave might be slow/busy |
| MB_CRC_ERROR | ✅ YES | Transient noise on line |
| MB_EXCEPTION | ❌ NO | Modbus protocol error (bad address, etc.) |
| MB_NOT_ENABLED | ❌ NO | Configuration error |
| MB_INVALID_SLAVE | ❌ NO | Parameter validation error |
| MB_INVALID_ADDRESS | ❌ NO | Parameter validation error |

### Files to Modify
- `include/constants.h` - Default retry parameters
- `include/types.h` - Add retry fields to config struct
- `src/modbus_master.cpp` - Implement retry logic
- `src/cli_commands_modbus_master.cpp` - CLI commands
- `src/config_load.cpp` - Load retry settings
- `src/config_save.cpp` - Save retry settings

### Estimated Effort
- **Implementation:** 2 timer
- **Testing:** 1 time
- **Total:** 3 timer

### Impact
- **Severity:** MEDIUM
- **Risk:** LOW (backward compatible - default max_retries=0)
- **Benefit:** HIGH (automatic recovery fra transient errors)

---

## 📊 ENHANCE-002: Per-Slave Health Monitoring

### Problem Statement
Systemet tracker global statistik (success/timeout/CRC), men ikke per slave device. Svært at identificere problematiske slaves og ingen automatic blacklisting af døde devices.

### Solution Design

**Data Structures:**
```cpp
// In include/types.h
#define MODBUS_MAX_MONITORED_SLAVES 8

typedef struct {
  uint8_t slave_id;                 // Slave address (1-247, 0=unused slot)
  uint32_t requests;                // Total requests to this slave
  uint32_t successes;               // Successful responses
  uint32_t timeouts;                // Timeout count
  uint32_t crc_errors;              // CRC error count
  uint32_t exceptions;              // Modbus exception count
  uint32_t consecutive_failures;    // Consecutive failures (reset on success)
  bool auto_blacklisted;            // Auto-disabled due to persistent failures
  uint32_t last_request_time;       // millis() of last request
  uint32_t last_success_time;       // millis() of last successful response
} modbus_slave_stats_t;

// In modbus_master_config_t:
modbus_slave_stats_t slave_stats[MODBUS_MAX_MONITORED_SLAVES];
uint8_t blacklist_threshold;      // Auto-blacklist after N consecutive failures (0=disabled)
```

**Tracking Logic:**
```cpp
// In src/modbus_master.cpp
void modbus_master_update_slave_stats(uint8_t slave_id, mb_error_code_t err) {
  // Find or allocate slot
  int8_t slot = -1;
  for (uint8_t i = 0; i < MODBUS_MAX_MONITORED_SLAVES; i++) {
    if (g_modbus_master_config.slave_stats[i].slave_id == slave_id) {
      slot = i;
      break;
    }
    if (g_modbus_master_config.slave_stats[i].slave_id == 0 && slot == -1) {
      slot = i;  // First free slot
    }
  }

  if (slot == -1) return;  // No free slots

  modbus_slave_stats_t *stats = &g_modbus_master_config.slave_stats[slot];

  // Initialize if new
  if (stats->slave_id == 0) {
    stats->slave_id = slave_id;
  }

  stats->requests++;
  stats->last_request_time = millis();

  if (err == MB_OK) {
    stats->successes++;
    stats->consecutive_failures = 0;
    stats->last_success_time = millis();
    stats->auto_blacklisted = false;  // Re-enable on success
  } else {
    stats->consecutive_failures++;

    // Update error counters
    switch (err) {
      case MB_TIMEOUT: stats->timeouts++; break;
      case MB_CRC_ERROR: stats->crc_errors++; break;
      case MB_EXCEPTION: stats->exceptions++; break;
      default: break;
    }

    // Auto-blacklist check
    if (g_modbus_master_config.blacklist_threshold > 0 &&
        stats->consecutive_failures >= g_modbus_master_config.blacklist_threshold) {
      stats->auto_blacklisted = true;
    }
  }
}

// In modbus_master_send_request():
mb_error_code_t modbus_master_send_request(...) {
  // ... existing code ...
  mb_error_code_t err = /* existing logic */;

  // Update per-slave stats
  modbus_master_update_slave_stats(request[0], err);  // request[0] = slave_id

  return err;
}
```

**Blacklist Enforcement:**
```cpp
// In src/st_builtin_modbus.cpp - check_request_limit()
static bool check_request_limit(uint8_t slave_id) {
  // Existing rate limiting...

  // Check if slave is blacklisted
  for (uint8_t i = 0; i < MODBUS_MAX_MONITORED_SLAVES; i++) {
    if (g_modbus_master_config.slave_stats[i].slave_id == slave_id) {
      if (g_modbus_master_config.slave_stats[i].auto_blacklisted) {
        g_mb_last_error = MB_SLAVE_BLACKLISTED;  // New error code
        g_mb_success = false;
        return false;
      }
      break;
    }
  }

  return true;
}
```

**CLI Commands:**
```bash
# Show per-slave statistics
show modbus master stats              # Global stats (existing)
show modbus master stats slave 1      # Per-slave stats (NEW)
show modbus master stats all          # All monitored slaves (NEW)

# Manual blacklist control
set modbus master blacklist 1         # Manually blacklist slave 1
set modbus master unblacklist 1       # Manually unblacklist slave 1
set modbus master blacklist-threshold 10  # Auto-blacklist after 10 failures

# Reset statistics
reset modbus master stats             # Global stats (existing)
reset modbus master stats slave 1     # Per-slave stats (NEW)
```

**CLI Output Example:**
```
> show modbus master stats slave 1

Slave ID: 1
  Total Requests:      1234
  Successful:          1180 (95.6%)
  Timeouts:            42 (3.4%)
  CRC Errors:          10 (0.8%)
  Exceptions:          2 (0.2%)
  Consecutive Failures: 0
  Auto-Blacklisted:    NO
  Last Request:        1234ms ago
  Last Success:        1234ms ago
```

### Files to Modify
- `include/types.h` - Add slave_stats struct and fields
- `src/modbus_master.cpp` - Implement tracking logic
- `src/st_builtin_modbus.cpp` - Add blacklist check
- `src/cli_show.cpp` - Add per-slave stats display
- `src/cli_commands_modbus_master.cpp` - Add blacklist commands

### Estimated Effort
- **Implementation:** 4 timer
- **Testing:** 1 time
- **Total:** 5 timer

### Impact
- **Severity:** LOW (nice-to-have diagnostics)
- **Risk:** LOW (no breaking changes)
- **Benefit:** MEDIUM (bedre troubleshooting og automatic error recovery)

---

## ⚡ ENHANCE-003: Non-Blocking Inter-Frame Delay

### Problem Statement
`delay(g_modbus_master_config.inter_frame_delay)` i `st_builtin_modbus.cpp` er blocking (default 10ms). Ved 10 requests = 100ms ekstra latency.

### Current Implementation
```cpp
// In st_builtin_mb_read_holding()
if (g_modbus_master_config.inter_frame_delay > 0) {
  delay(g_modbus_master_config.inter_frame_delay);  // 10ms blocking
}
```

### Solution Design

**Option 1: Track Last Request Time (SIMPLE)**
```cpp
// In src/st_builtin_modbus.cpp
static uint32_t g_last_request_time = 0;

// In each MB function:
// Wait for minimum inter-frame time
uint32_t elapsed = millis() - g_last_request_time;
if (elapsed < g_modbus_master_config.inter_frame_delay) {
  delay(g_modbus_master_config.inter_frame_delay - elapsed);
}

// Call modbus function...

g_last_request_time = millis();
```

**Option 2: Yield to RTOS (ADVANCED - hvis FreeRTOS bruges)**
```cpp
while (millis() - g_last_request_time < g_modbus_master_config.inter_frame_delay) {
  vTaskDelay(1);  // Yield to other tasks instead of busy-wait
}
```

### Benefits
- **Første request:** No delay (0ms overhead)
- **Consecutive requests:** Kun delay hvis < inter_frame_delay siden sidste
- **Example:** If request takes 40ms and inter_frame = 10ms, no extra delay needed

### Files to Modify
- `src/st_builtin_modbus.cpp` - Replace blocking delay

### Estimated Effort
- **Implementation:** 30 minutter
- **Testing:** 15 minutter
- **Total:** 45 minutter

### Impact
- **Severity:** LOW (optimization)
- **Risk:** LOW
- **Benefit:** MEDIUM (reduces latency for consecutive requests)

---

## 📦 ENHANCE-004: Multi-Register Read Support

### Problem Statement
Nuværende implementation læser KUN 1 register ad gangen. Ved bulk polling (10 consecutive registers) kræves 10 separate requests i stedet for 1.

### Current Limitation
```structured-text
(* Read 10 consecutive registers - 10 requests! *)
VAR i: INT; temps: ARRAY[1..10] OF INT; END_VAR

FOR i := 1 TO 10 DO
  temps[i] := MB_READ_HOLDING(1, 100 + i - 1);  (* 10 × 40ms = 400ms *)
END_FOR;
```

### Solution Design

**New Function:**
```cpp
// In include/modbus_master.h
#define MODBUS_MAX_BULK_REGISTERS 16  // Read up to 16 registers at once

mb_error_code_t modbus_master_read_holding_multi(
  uint8_t slave_id,
  uint16_t start_address,
  uint16_t count,             // 1-16 registers
  uint16_t *results           // Array of results
);
```

**Response Buffer Update:**
```cpp
// In modbus_master.cpp
uint8_t response[3 + (MODBUS_MAX_BULK_REGISTERS * 2) + 2];  // 3 + 32 + 2 = 37 bytes
```

**Implementation:**
```cpp
mb_error_code_t modbus_master_read_holding_multi(
  uint8_t slave_id,
  uint16_t start_address,
  uint16_t count,
  uint16_t *results
) {
  if (count == 0 || count > MODBUS_MAX_BULK_REGISTERS) {
    return MB_INVALID_COUNT;  // New error code
  }

  uint8_t request[8];
  uint8_t response[3 + (MODBUS_MAX_BULK_REGISTERS * 2) + 2];
  uint8_t response_len;

  // Build request: FC03 with quantity=count
  request[0] = slave_id;
  request[1] = 0x03;
  request[2] = (start_address >> 8) & 0xFF;
  request[3] = start_address & 0xFF;
  request[4] = (count >> 8) & 0xFF;
  request[5] = count & 0xFF;
  uint16_t crc = modbus_master_calc_crc(request, 6);
  request[6] = crc & 0xFF;
  request[7] = (crc >> 8) & 0xFF;

  g_modbus_master_config.total_requests++;

  mb_error_code_t err = modbus_master_send_request(request, 8, response, &response_len, sizeof(response));
  if (err != MB_OK) {
    return err;
  }

  // Parse response: [slave_id][FC03][byte_count][data...][CRC]
  uint8_t expected_bytes = 3 + (count * 2) + 2;
  if (response_len >= expected_bytes && response[1] == 0x03) {
    for (uint16_t i = 0; i < count; i++) {
      results[i] = (response[3 + (i * 2)] << 8) | response[4 + (i * 2)];
    }
    return MB_OK;
  }

  return MB_CRC_ERROR;
}
```

**ST Logic Wrapper (FUTURE - v5.0+ når ARRAY support tilføjet):**
```structured-text
(* Future syntax when ARRAY support is added *)
VAR
  temps: ARRAY[1..10] OF INT;
END_VAR

MB_READ_HOLDING_MULTI(1, 100, 10, temps);  (* Single request - 40ms! *)
```

### Performance Improvement

**Before (10 single-register reads):**
```
10 requests × 40ms = 400ms @ 9600 baud
```

**After (1 multi-register read):**
```
1 request × 40ms = 40ms @ 9600 baud
→ 90% latency reduction!
```

### Files to Modify
- `include/modbus_master.h` - Add multi-register function
- `src/modbus_master.cpp` - Implement multi-register read
- `include/types.h` - Add MB_INVALID_COUNT error code

### Estimated Effort
- **Implementation:** 2 timer
- **Testing:** 1 time
- **Total:** 3 timer

### Impact
- **Severity:** LOW (optimization for future ARRAY support)
- **Risk:** LOW (new function, no breaking changes)
- **Benefit:** HIGH (når ARRAY support tilføjes i v5.0)

---

## 📅 Implementation Roadmap

### v4.5.2 (Immediate - Critical Bug Fix)
**Estimated: 1 time**
- ✅ **BUG-133:** Reset request counter (25 min)
- ✅ Test og verify fix (10 min)
- ✅ Update BUGS_INDEX.md (5 min)
- ✅ Update BUGS.md detailed section (15 min)
- ✅ Git commit (5 min)

### v4.6.0 (Short Term - Robustness)
**Estimated: 1 dag**
- 🔄 **ENHANCE-001:** Retry logic (3 timer)
- 📊 **ENHANCE-002:** Per-slave monitoring (5 timer)
- ✅ Integration testing (2 timer)
- ✅ Documentation update (1 time)

### v4.7.0 (Medium Term - Performance)
**Estimated: 1 dag**
- ⚡ **ENHANCE-003:** Non-blocking delay (45 min)
- ✅ Performance testing (1 time)
- ✅ Documentation update (30 min)

### v5.0.0+ (Long Term - Advanced Features)
**Estimated: 3 dage**
- 📦 **ENHANCE-004:** Multi-register read (3 timer)
- 🔤 **STRING Type Support** (2 dage) - from TODO.md
- 📐 **ARRAY Support** (3 dage) - from TODO.md

---

## 🧪 Testing Strategy

### Test 1: BUG-133 Verification
```structured-text
(* Test request counter reset *)
VAR i: INT; temp: INT; END_VAR

FOR i := 1 TO 20 DO
  temp := MB_READ_HOLDING(1, i);
  IF mb_success = FALSE THEN
    (* FAIL: Should not happen after fix *)
  END_IF;
END_FOR;
```

### Test 2: Retry Logic Verification
```structured-text
(* Test automatic retry on timeout *)
VAR temp: INT; error: INT; END_VAR

(* Pull network cable during test *)
temp := MB_READ_HOLDING(1, 100);
error := mb_last_error;

(* Expected: 3 attempts (1 + 2 retries) before returning error *)
```

### Test 3: Per-Slave Stats Verification
```bash
# Test per-slave statistics tracking
> reset modbus master stats slave 1
> # Run ST program that makes 10 requests to slave 1
> show modbus master stats slave 1

# Expected output:
# Total Requests: 10
# Successful: 10 (100%)
```

### Test 4: Multi-Register Read Verification
```cpp
// C++ test code
uint16_t results[10];
mb_error_code_t err = modbus_master_read_holding_multi(1, 100, 10, results);

// Verify all 10 registers read correctly
for (uint8_t i = 0; i < 10; i++) {
  Serial.printf("Register %d: %d\n", 100 + i, results[i]);
}
```

---

## 📚 References

- **Analysis Document:** `MODBUS_MASTER_ANALYSIS.md`
- **Bug Tracking:** `BUGS_INDEX.md` (BUG-133)
- **TODO List:** `TODO.md` (Priority 2)
- **Modbus RTU Spec:** MODBUS Application Protocol V1.1b3

---

## ✅ Anbefaling

### Umiddelbar Handling (v4.5.2)
1. **FIX BUG-133** (25 min) - KRITISK
2. Test grundigt (10 min)
3. Commit og deploy

### Næste Sprint (v4.6.0)
1. Implementér retry logic (3 timer)
2. Tilføj per-slave monitoring (5 timer)
3. Integration test (2 timer)

### Langsigtet (v4.7.0+)
1. Non-blocking delay optimization
2. Multi-register read support
3. Kombiner med ARRAY support i v5.0.0

---

**Designet af:** Claude Sonnet 4.5
**Review Status:** ✅ KOMPLET
**Ready for Implementation:** JA (BUG-133 kan implementeres med det samme)
