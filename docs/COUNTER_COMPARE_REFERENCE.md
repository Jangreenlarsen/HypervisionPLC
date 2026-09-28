# Counter Compare Feature - Technical Reference Guide

<!-- DOC-STATUS: historisk -->
> ⚠️ **Historisk dokument — vedligeholdes ikke længere.** Skrevet til en ældre firmwareversion og kan være forældet (CLI-syntaks, pins, endpoints, standardværdier). Den aktuelle, vedligeholdte dokumentation er [brugermanualen](manual/00_INDEKS.md) (CLI: appendiks A, REST: appendiks B, ST: appendiks D). Markeret ved dokumentationsgennemgang 2026-09-28.


**Version:** 2.3+
**Implementation Status:** Phase 1-3 Complete (Build #329)
**Release Date:** 2025-12-03

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Configuration Structure](#configuration-structure)
3. [Operational Modes](#operational-modes)
4. [CLI Command Reference](#cli-command-reference)
5. [Modbus Integration](#modbus-integration)
6. [Persistence & Storage](#persistence--storage)
7. [State Machine & Timing](#state-machine--timing)
8. [Edge Cases & Limitations](#edge-cases--limitations)
9. [Implementation Details](#implementation-details)

---

## Architecture Overview

### Three-Layer Implementation

**Layer 5a: Compare Detection (counter_engine.cpp)**
```
counter_engine_loop()
  ├─ counter_engine_store_value_to_registers()
  │   └─ counter_engine_check_compare()  ← Comparison logic
  │       ├─ Mode 0: value >= compare_value
  │       ├─ Mode 1: value > compare_value
  │       └─ Mode 2: value >= (transition from <)
  └─ Sets status bit in input register if match
```

**Layer 2: Reset-on-Read (modbus_fc_read.cpp)**
```
modbus_fc04_read_input_registers()
  ├─ Read registers from storage
  ├─ modbus_fc04_handle_reset_on_read()  ← Auto-clear logic
  │   └─ For each counter with reset_on_read:
  │       ├─ Check if status_reg in read range
  │       └─ Clear bit: status &= ~(1 << compare_bit)
  └─ Send response to master
```

**Layer 6: Configuration & Persistence (counter_config.cpp, config_load.cpp, config_save.cpp)**
```
counter_config_set()
  ├─ Validate compare fields
  ├─ Sanitize compare values
  └─ Store in runtime array

config_save.cpp (on "save config")
  └─ Persist to NVS with schema version 3

config_load.cpp (on boot)
  └─ Restore from NVS
```

### Block Diagram

```
Hardware (GPIO/PCNT)
    ↓
Counter Mode (SW/ISR/HW) → Raw counter value
    ↓
counter_engine_loop()
    ├─ Get current value
    ├─ Check compare condition
    ├─ SET status bit if match
    └─ Write to registers
    ↓
Modbus Master reads FC04
    ├─ Gets status bit = 1
    ├─ Modbus response sent
    └─ FC04_handle_reset_on_read()
        └─ CLEAR status bit
    ↓
Next loop cycle detects when value hits threshold again
```

---

## Configuration Structure

### CounterConfig Struct Extension (types.h)

```c
typedef struct {
  // ... existing fields ...

  // COMPARE FEATURE (v2.3+) - 6 fields added
  uint8_t  compare_enabled;        // Boolean: enable/disable
  uint8_t  compare_mode;           // 0=≥, 1=>, 2===
  uint64_t compare_value;          // 64-bit threshold
  uint16_t compare_status_reg;     // Input register 0-255 (65535=unused)
  uint8_t  compare_bit;            // Bit position 0-15
  uint8_t  reset_on_read;          // Boolean: auto-clear on FC04 read
} CounterConfig;
```

### Runtime State (counter_engine.cpp)

```c
typedef struct {
  uint8_t  compare_triggered;      // Was triggered this iteration?
  uint32_t compare_time_ms;        // When triggered (for logging)
  uint64_t last_value;             // Previous value (for exact match mode)
} CounterCompareRuntime;

static CounterCompareRuntime counter_compare_state[COUNTER_COUNT];  // 4 counters
```

### Defaults (counter_config.cpp)

```c
cfg.compare_enabled = 0;           // Disabled by default
cfg.compare_mode = 0;              // ≥ (greater-or-equal)
cfg.compare_value = 0;             // No threshold set
cfg.compare_status_reg = 65535;    // Marker: not configured
cfg.compare_bit = 0;               // Bit 0 (LSB)
cfg.reset_on_read = 1;             // Auto-clear enabled by default
```

---

## Operational Modes

### Mode 0: Greater-or-Equal (≥)

**Condition:** `counter_value >= compare_value`

**Behavior:**
- Status bit SET when condition becomes true
- Remains SET until reset-on-read clears it
- SET again immediately when value goes above threshold in next cycle

**Use Cases:**
- Production counts (e.g., "reached 1000 units")
- Level alerts (e.g., "tank level >= danger zone")
- Threshold detection (e.g., "frequency >= 5000 Hz")

**Example:**
```
Cycle 1: counter=999 → compare=1000 → bit stays 0 (999 < 1000)
Cycle 2: counter=1005 → compare=1000 → bit set to 1 (1005 >= 1000) ✓
Cycle 3: Master reads FC04 → gets bit=1, then bit cleared to 0
Cycle 4: counter=1005 (still >= 1000) → bit set to 1 again ✓
```

### Mode 1: Greater-Than (>)

**Condition:** `counter_value > compare_value`

**Behavior:**
- Like Mode 0 but strictly greater-than
- Never matches if counter equals compare value

**Use Cases:**
- Strict threshold detection
- Hysteresis-like behavior needed

**Example:**
```
Cycle 1: counter=1000 → compare=1000 → bit stays 0 (1000 is not > 1000)
Cycle 2: counter=1001 → compare=1000 → bit set to 1 (1001 > 1000) ✓
```

### Mode 2: Exact Match on Transition (===)

**Condition:** `last_value < compare_value AND counter_value >= compare_value`

**Behavior:**
- Only SET when counter **crosses** the threshold from below
- Detects exactly when threshold is _entered_
- Ignores if counter jumps over or stays above

**Use Cases:**
- One-shot alerts (trigger exactly once per increment)
- Rising edge detection
- Precise event timing

**Example:**
```
Cycle 1: counter=998, last=0 → crossing? (0 < 1000 AND 998 >= 1000)? NO
Cycle 2: counter=1000, last=998 → crossing? (998 < 1000 AND 1000 >= 1000)? YES ✓ bit=1
Cycle 3: counter=1001, last=1000 → crossing? (1000 < 1000 AND 1001 >= 1000)? NO
Cycle 4: counter=1005, last=1001 → crossing? (1001 < 1000 AND 1005 >= 1000)? NO
Cycle 5: Master reads FC04 → gets bit=1, then bit cleared
Cycle 6: counter=1005, last=1005 → (1005 < 1000)? NO → bit stays 0
```

---

## CLI Command Reference

### Set Compare Parameters

```bash
set counter <ID> compare:<on|off> [options...]
```

#### Full Option Reference

| Option | Type | Range | Default | Description |
|--------|------|-------|---------|-------------|
| `compare` | bool | `on` / `off` | `off` | Enable/disable compare feature |
| `compare-value` | u64 | 0 - 2^64-1 | 0 | Threshold value to compare against |
| `compare-mode` | u8 | 0, 1, 2 | 0 | Mode: ≥ / > / === |
| `compare-status-reg` | u16 | 0-255 / 65535 | 65535 | Input register holding status bit |
| `compare-bit` | u8 | 0-15 | 0 | Bit position in status register (0=LSB) |
| `reset-on-read` | bool | `on` / `off` | `on` | Auto-clear bit after FC04 read? |

#### Example Commands

**Minimal (enable with defaults):**
```bash
set counter 1 compare:on
# Uses: mode=0, value=0, status-reg=65535 (disabled!)
# Result: Feature enabled but not functional (need to set status-reg)
```

**Typical:**
```bash
set counter 1 \
  compare:on \
  compare-value:1000 \
  compare-status-reg:100 \
  compare-bit:0 \
  reset-on-read:on
```

**Complete (all options):**
```bash
set counter 1 \
  enabled:1 \
  mode:1 \
  hw-mode:pcnt \
  hw-gpio:19 \
  index-reg:0 \
  raw-reg:1 \
  freq-reg:2 \
  overload-reg:3 \
  ctrl-reg:4 \
  prescaler:1 \
  bit-width:32 \
  scale-factor:1.0 \
  compare:on \
  compare-value:5000 \
  compare-mode:0 \
  compare-status-reg:100 \
  compare-bit:5 \
  reset-on-read:on
```

### Show Counter Status

```bash
show counter <ID>
```

**Output includes Compare section:**
```
=== COUNTER COMPARE FEATURE ===
Counter 1:
  Mode: ≥ (greater-or-equal)
  Compare Value: 1000
  Status Register: input_reg[100], Bit 5
  Reset-on-Read: ENABLED
  Current Status Bit: SET (1)
==============================
```

---

## Modbus Integration

### FC04: Read Input Registers (with Reset-on-Read)

**Request Frame:**
```
Byte 0:   Slave ID (0x01)
Byte 1:   Function Code (0x04)
Byte 2-3: Starting Address (register 100 = 0x0064)
Byte 4-5: Quantity (1 register = 0x0001)
Byte 6-7: CRC16
```

**Response Frame:**
```
Byte 0:   Slave ID (0x01)
Byte 1:   Function Code (0x04)
Byte 2:   Byte Count (0x02)
Byte 3-4: Register Value (0xFFDF if bit 5 = 0)
Byte 5-6: CRC16
```

**Timeline (with reset-on-read):**

```
T0: Modbus master sends FC04 request for register 100
    ↓
T1: ESP32 receives request
    ├─ Reads register 100 from input_regs[]
    ├─ Gets value with bit 5 = 1 (status bit SET)
    ├─ Sends response with bit 5 = 1
    └─ Returns immediately to main loop
    ↓
T2: modbus_fc04_handle_reset_on_read() executes in same FC04 call
    ├─ Iterates all 4 counters
    ├─ Finds counter 1: compare_status_reg=100, compare_bit=5, reset_on_read=1
    ├─ Checks: status_reg (100) in range [100..101)? YES
    ├─ Clears bit: input_regs[100] &= ~(1 << 5)
    └─ Logs: "FC04 reset-on-read: Counter 1 status bit 5 cleared (reg 100)"
    ↓
T3: Master receives response (with bit 5 = 1)
    ├─ Processes alert/notification
    └─ Waits for next FC04 read
    ↓
T4: Next loop cycle (counter_engine_loop())
    ├─ Counter value >= 1000 again
    └─ Sets bit 5 = 1 again (ready for next read)
```

### FC03: Read Holding Registers

**Note:** Reset-on-read only applies to **input registers** (FC04), not holding registers (FC03).

If you read holding registers containing counter values, no reset-on-read occurs. This is correct because holding registers are typically for configuration, not status.

---

## Persistence & Storage

### Schema Version

**Current:** Version 3 (CONFIG_SCHEMA_VERSION in constants.h)

**Changes from v2 → v3:**
- Added 6 compare fields to CounterConfig struct
- ST Logic moved to separate NVS namespace
- Added schema_version field for migration

### NVS Storage (Non-Volatile Storage)

**Namespace:** `nvs_modbus` (shared with other config)

**Key-Value pairs (per counter):**
```
counter_1_enabled           = 1 (u8)
counter_1_hw_mode           = 2 (u8)
counter_1_compare_enabled   = 1 (u8)      ← NEW in v3
counter_1_compare_value     = 1000 (u64)  ← NEW in v3
counter_1_compare_mode      = 0 (u8)      ← NEW in v3
counter_1_compare_status_reg = 100 (u16)  ← NEW in v3
counter_1_compare_bit       = 5 (u8)      ← NEW in v3
counter_1_reset_on_read     = 1 (u8)      ← NEW in v3
... (and 3 more counters)
```

### Persistence Workflow

**On "set counter" command:**
```
cli_cmd_set_counter()
  → counter_config_set()
  → counter_config_sanitize()
  → counter_configs[] array updated
  → NOT yet saved to NVS
```

**On "save config" command:**
```
config_save()
  → Iterate all counters
  → Save each field to NVS
  → Calculate CRC16
  → Increment CONFIG_VERSION if changed
  → Log "Config saved"
```

**On boot:**
```
config_load()
  → Check NVS schema version
  → If v2 → migrate to v3 (set defaults for compare fields)
  → Load all fields from NVS
  → Verify CRC16
  → If invalid → use defaults
  → Call config_apply()
```

### Manual Reset to Defaults

```bash
reset config
# All counters and compare settings return to defaults
# Default: compare_enabled=0 (disabled)
```

---

## State Machine & Timing

### Compare Check Cycle (Main Loop)

```
counter_engine_loop()
├─ For each counter (ID 1-4):
│  ├─ Get config
│  ├─ If not enabled → skip
│  ├─ Call mode-specific handler (SW/ISR/HW)
│  ├─ Update frequency measurement
│  ├─ counter_engine_store_value_to_registers()
│  │  ├─ Apply prescaler division
│  │  ├─ Apply scale factor
│  │  ├─ Write to index/raw/freq registers
│  │  └─ counter_engine_check_compare()  ← HERE
│  │      ├─ Get current value
│  │      ├─ Check compare condition
│  │      ├─ If match:
│  │      │   ├─ Set status bit
│  │      │   ├─ Update runtime state
│  │      │   └─ Log event (debug)
│  │      └─ Store last_value for next cycle
│  └─ Return to next counter
└─ Return to main loop
```

### Timing Constants

| Parameter | Value | Purpose |
|-----------|-------|---------|
| Loop frequency | ~100 Hz (10 ms) | Main loop iteration |
| Compare check | Every loop | Immediate detection |
| Reset-on-read | FC04 response | After register sent |
| Frequency window | 1000 ms | Measurement period |
| Debug logging | Per event | When compare triggered |

### Latency Analysis

**Best case (compare detected):**
```
T0: Counter reaches threshold
T1: Next loop (< 10ms): Compare detected, bit SET
T2: Next Modbus request: Master reads bit (< 100ms typical)
Total latency: < 100ms
```

**Worst case:**
```
T0: Counter reaches threshold
T1: If loop blocked: up to 10ms
T2: If Modbus quiet: up to 1000ms until next read
Total latency: up to 1010ms (but very unlikely)
```

---

## Edge Cases & Limitations

### Limitation 1: 4 Counters Maximum

- Only 4 counters can be configured simultaneously
- Each can have own compare settings independently
- Multiple counters can use same status register (different bits)

**Workaround:** Use different bit positions:
```bash
set counter 1 compare:on compare-status-reg:100 compare-bit:0
set counter 2 compare:on compare-status-reg:100 compare-bit:1
set counter 3 compare:on compare-status-reg:100 compare-bit:2
set counter 4 compare:on compare-status-reg:100 compare-bit:3
# All 4 share register 100, use bits 0-3
```

### Limitation 2: 16-Bit Input Register

Input registers are 16 bits (0-65535), so status register holds max 16 bits.

**Workaround:** Distribute across multiple registers:
```bash
set counter 1 compare-status-reg:100 compare-bit:0
set counter 2 compare-status-reg:101 compare-bit:0
# Use different registers for different status bits
```

### Limitation 3: Compare Value Limited to 64-Bit

Counter value is internally 64-bit, but practical counters rarely use full range.

**Typical:** 32-bit counts (0-4 billion) sufficient for most applications

### Limitation 4: Reset-on-Read Triggers on ANY Read

If you read input register 100 via FC04, ALL counters with `compare_status_reg=100` and `reset_on_read=on` will have their bits cleared.

**Design:** This is intentional - master is expected to read and process all status bits together.

**Workaround:** Use separate registers for independent status bits:
```bash
set counter 1 compare-status-reg:100  # Counter 1 alone
set counter 2 compare-status-reg:101  # Counter 2 alone
```

### Edge Case 1: Counter Overflow

**Scenario:** Counter reaches 64-bit maximum (2^64-1), then wraps to 0.

**Mode 0/1 behavior:**
```
Before overflow: counter=18446744073709551614, compare=1000
After overflow:  counter=0, compare=1000
Result: Bit clears (0 < 1000)
```

**Mode 2 behavior:**
```
Before overflow: last_value=18446744073709551614 (>= 1000)
After overflow:  counter=0 (< 1000)
Result: NOT a transition (because last_value is not < 1000)
Bit stays 0 until counter reaches 1000 again
```

**Impact:** Low - 64-bit wrap happens ~never with typical sensors

### Edge Case 2: Prescaler Interaction

**Scenario:** Counter has `prescaler=100`, `compare-value=1000`

**Behavior:** Compare happens on **raw counter value**, not prescaled value.

```
Raw counter=100000 → Scaled in index-reg = 1000 (100000/100)
Compare checks: raw (100000) >= 1000? YES → Bit SET
```

**Implication:** Set compare-value based on RAW counter, not scaled display value.

### Edge Case 3: Reset-on-Read During Configuration

**Scenario:** You change `reset-on-read` while counter is running.

```bash
set counter 1 reset-on-read:off  # Disable auto-clear
# Bit now persists until manual clear
show counter 1  # Bit still shown as SET
set counter 1 reset-on-read:on   # Re-enable auto-clear
# Bit will clear on next FC04 read
```

**No issue** - transition is atomic and safe.

---

## Implementation Details

### Function: counter_engine_check_compare()

**Location:** `src/counter_engine.cpp:394-441`

**Pseudo-code:**
```c
static void counter_engine_check_compare(uint8_t id, uint64_t counter_value) {
  // 1. Validate parameters
  if (id < 1 || id > 4) return;

  CounterConfig cfg;
  if (!counter_config_get(id, &cfg) || !cfg.compare_enabled) return;
  if (cfg.compare_status_reg >= INPUT_REGS_SIZE) return;

  // 2. Get runtime state for exact match mode
  CounterCompareRuntime *runtime = &counter_compare_state[id - 1];
  uint8_t compare_hit = 0;

  // 3. Evaluate condition based on mode
  switch (cfg.compare_mode) {
    case 0:  // ≥
      compare_hit = (counter_value >= cfg.compare_value) ? 1 : 0;
      break;
    case 1:  // >
      compare_hit = (counter_value > cfg.compare_value) ? 1 : 0;
      break;
    case 2:  // === transition
      compare_hit = (runtime->last_value < cfg.compare_value &&
                     counter_value >= cfg.compare_value) ? 1 : 0;
      break;
  }

  // 4. Store current value for next cycle
  runtime->last_value = counter_value;

  // 5. If condition met, set the status bit
  if (compare_hit) {
    uint16_t status = registers_get_input_register(cfg.compare_status_reg);
    status |= (1 << cfg.compare_bit);  // Set specific bit
    registers_set_input_register(cfg.compare_status_reg, status);

    // 6. Update runtime state for logging
    runtime->compare_triggered = 1;
    runtime->compare_time_ms = millis();
  }
}
```

### Function: modbus_fc04_handle_reset_on_read()

**Location:** `src/modbus_fc_read.cpp:30-57`

**Pseudo-code:**
```c
static void modbus_fc04_handle_reset_on_read(uint16_t starting_address, uint16_t quantity) {
  // 1. Iterate all counters
  for (uint8_t id = 1; id <= 4; id++) {
    CounterConfig cfg;
    if (!counter_config_get(id, &cfg)) continue;

    // 2. Skip if not configured for reset-on-read
    if (!cfg.compare_enabled || !cfg.reset_on_read) continue;

    // 3. Check if this counter's status register was in read range
    uint16_t status_reg = cfg.compare_status_reg;
    if (status_reg >= INPUT_REGS_SIZE) continue;
    if (status_reg < starting_address || status_reg >= starting_address + quantity) {
      continue;  // Not in range
    }

    // 4. Clear the status bit
    uint16_t status = registers_get_input_register(status_reg);
    status &= ~(1 << cfg.compare_bit);  // Clear specific bit
    registers_set_input_register(status_reg, status);

    // 5. Log the action (debug output)
    debug_print("FC04 reset-on-read: Counter ");
    debug_print_uint(id);
    debug_print(" status bit ");
    debug_print_uint(cfg.compare_bit);
    debug_print(" cleared (reg ");
    debug_print_uint(status_reg);
    debug_println(")");
  }
}
```

### Initialization: counter_engine_init()

**Location:** `src/counter_engine.cpp:58-74`

```c
void counter_engine_init(void) {
  // ... other init code ...

  // Initialize runtime state for compare feature
  memset(counter_compare_state, 0, sizeof(counter_compare_state));

  // Now all 4 counters have:
  // - compare_triggered = 0
  // - compare_time_ms = 0
  // - last_value = 0
}
```

---

## Debugging & Troubleshooting

### Enable Debug Output

In `include/debug.h`, define:
```c
#define DEBUG_ENABLED 1
```

Then recompile. Counter compare events will log:
```
FC04 reset-on-read: Counter 1 status bit 5 cleared (reg 100)
```

### Verify Configuration in NVS

**Via CLI:**
```bash
show counter 1
show counter 2
show counter 3
show counter 4
```

**Check persistence:**
```bash
save config
# Reboot device
show counter 1  # Should still have same settings
```

### Monitor Register Changes

Use external Modbus master to poll register:
```
Read FC04 register 100 every 100ms
Watch bit 5 toggle 0→1→0→1...
```

### Check Prescaler Interaction

```bash
show counter 1
# Note: raw_value = counter_value / prescaler
# But: compare-value is compared against counter_value (raw, not prescaled)
```

---

## Memory Usage

### Static Data (ROM)

- CounterCompareRuntime struct: 4 × 15 bytes = 60 bytes
- Helper function code: ~300 bytes
- Debug strings: ~200 bytes
- **Total ROM:** ~560 bytes (negligible)

### Dynamic Data (RAM)

- counter_compare_state array: 4 × 15 bytes = 60 bytes
- Additional code path: minimal
- **Total RAM:** ~60 bytes (negligible)

### No Memory Regression

Build #329 memory usage:
- **RAM:** 23.5% (76,916 / 327,680 bytes) - compare feature adds ~60 bytes
- **Flash:** 28.5% (373,601 / 1,310,720 bytes) - compare feature adds ~500 bytes

---

## Future Extensions

### Possible Features (v2.4+)

1. **Hysteresis Mode:** Compare with upper/lower bounds
2. **Pulse Output:** Generate pulse on status register when triggered
3. **Filter Window:** Ignore brief excursions (debounce-like behavior)
4. **Configurable Timeout:** Auto-clear bit after X milliseconds
5. **Condition Combinations:** AND/OR multiple counter conditions

### API Stability

Current API (v2.3) is **stable and locked** for v2.x releases. New fields will use new version (v3.x).

---

## Support & Documentation

- **Quick Start:** [COUNTER_COMPARE_QUICK_START.md](COUNTER_COMPARE_QUICK_START.md)
- **Feature Guide:** [FEATURE_GUIDE.md](FEATURE_GUIDE.md)
- **CLI Commands:** Run `help` in CLI for command syntax
- **Architecture:** [ESP32_Module_Architecture.md](ESP32_Module_Architecture.md)

---

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 2.3 | 2025-12-03 | Counter compare feature v1.0 (Phase 1-3) |
| 2.2 | 2025-11-XX | Previous release (no compare) |

---

## License & Credits

Counter Compare Feature
- **Phase 1 (Detection):** Core compare logic in counter_engine.cpp
- **Phase 2 (Configuration):** CLI integration in cli_commands.cpp / cli_show.cpp
- **Phase 3 (Integration):** Modbus reset-on-read in modbus_fc_read.cpp
- **Implementation:** Build #329, schema version 3, CONFIG_SCHEMA_VERSION
- **Testing:** Manual validation via CLI and Modbus FC04 reads
