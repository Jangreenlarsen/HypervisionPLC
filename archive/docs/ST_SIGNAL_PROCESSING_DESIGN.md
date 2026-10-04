# Signal Processing Function Blocks - Design Document

<!-- DOC-STATUS: historisk -->
> ⚠️ **Historisk dokument — vedligeholdes ikke længere.** Skrevet til en ældre firmwareversion og kan være forældet (CLI-syntaks, pins, endpoints, standardværdier). Den aktuelle, vedligeholdte dokumentation er [brugermanualen](manual/00_INDEKS.md) (CLI: appendiks A, REST: appendiks B, ST: appendiks D). Markeret ved dokumentationsgennemgang 2026-09-28.


**Version:** v4.8.0 (Target)
**Dato:** 2026-01-07
**Status:** 🔧 Design Phase

---

## 📋 Overview

Nye **4 signal processing function blocks** til ST Logic system:

1. **HYSTERESIS** - Schmitt trigger med hysteresis
2. **BLINK** - Blink/pulse generator
3. **FILTER** - First-order low-pass filter
4. **SCALE** - Linear scaling/mapping

**IEC 61131-3 Compliance:** Partial (SCALE er standard, andre er vendor-specific)

---

## Function Block Specifications

### 1. HYSTERESIS(IN, HIGH, LOW) → BOOL

**Purpose:** Schmitt trigger med hysteresis for noise immunity

**Syntax:**
```structured-text
output := HYSTERESIS(input_signal, upper_threshold, lower_threshold);
```

**Parameters:**
- `IN` (REAL) - Input signal
- `HIGH` (REAL) - Upper threshold (switch-on point)
- `LOW` (REAL) - Lower threshold (switch-off point)

**Returns:**
- (BOOL) - Output state

**Logic:**
```
IF IN > HIGH THEN
  Q := TRUE
ELSIF IN < LOW THEN
  Q := FALSE
ELSE
  Q := Q_prev  (* Hold previous state *)
END_IF
```

**Truth Table:**

| Previous Q | IN value | Result Q | Explanation |
|-----------|----------|----------|-------------|
| FALSE | IN < LOW | FALSE | Below lower threshold |
| FALSE | LOW ≤ IN ≤ HIGH | FALSE | In dead zone, hold FALSE |
| FALSE | IN > HIGH | TRUE | Above upper threshold → switch ON |
| TRUE | IN < LOW | FALSE | Below lower threshold → switch OFF |
| TRUE | LOW ≤ IN ≤ HIGH | TRUE | In dead zone, hold TRUE |
| TRUE | IN > HIGH | TRUE | Above upper threshold |

**Use Cases:**
- Temperature control with hysteresis (avoid relay chatter)
- Level detection with noise immunity
- Analog signal debouncing
- Threshold detection with dead zone

**Example:**
```structured-text
VAR
  temperature : REAL;
  heater_on : BOOL;
END_VAR

(* Turn heater ON at 18°C, OFF at 22°C *)
heater_on := HYSTERESIS(temperature, 22.0, 18.0);
```

**State Storage:**
- Q (BOOL) - Previous output state (1 byte)

---

### 2. BLINK(ENABLE, ON_TIME, OFF_TIME) → BOOL

**Purpose:** Periodic pulse/blink generator

**Syntax:**
```structured-text
output := BLINK(enable, on_duration_ms, off_duration_ms);
```

**Parameters:**
- `ENABLE` (BOOL) - Enable blink generator
- `ON_TIME` (INT) - ON duration in milliseconds
- `OFF_TIME` (INT) - OFF duration in milliseconds

**Returns:**
- (BOOL) - Blink output (TRUE/FALSE)

**Logic:**
```
IF NOT ENABLE THEN
  Q := FALSE
  state := IDLE
ELSIF state = IDLE THEN
  Q := TRUE
  timer := millis()
  state := ON_PHASE
ELSIF state = ON_PHASE THEN
  IF (millis() - timer) >= ON_TIME THEN
    Q := FALSE
    timer := millis()
    state := OFF_PHASE
  END_IF
ELSIF state = OFF_PHASE THEN
  IF (millis() - timer) >= OFF_TIME THEN
    Q := TRUE
    timer := millis()
    state := ON_PHASE
  END_IF
END_IF
```

**State Machine:**
```
IDLE → ON_PHASE → OFF_PHASE → ON_PHASE → ...
       (ON_TIME)   (OFF_TIME)   (repeat)
```

**Use Cases:**
- LED blinking (heartbeat, status indicators)
- Periodic alarms/warnings
- Clock signal generation
- Timed pulse sequences

**Example:**
```structured-text
VAR
  system_active : BOOL;
  led_blink : BOOL;
END_VAR

(* Blink LED: 500ms ON, 500ms OFF when system active *)
led_blink := BLINK(system_active, 500, 500);
```

**State Storage:**
- Q (BOOL) - Current output state
- state (UINT8) - State machine state (IDLE=0, ON=1, OFF=2)
- timer (UINT32) - Timestamp in milliseconds
- Total: ~6 bytes

---

### 3. FILTER(IN, TIME_CONSTANT) → REAL

**Purpose:** First-order low-pass filter for signal smoothing

**Syntax:**
```structured-text
smoothed := FILTER(noisy_signal, time_constant_ms);
```

**Parameters:**
- `IN` (REAL) - Input signal
- `TIME_CONSTANT` (INT) - Filter time constant in milliseconds (τ)

**Returns:**
- (REAL) - Filtered output

**Algorithm:**
```
α = DT / (TIME_CONSTANT + DT)
OUT = OUT_prev + α * (IN - OUT_prev)

Where:
  DT = cycle time (10ms for 100Hz execution)
  α = smoothing factor (0 to 1)
```

**Frequency Response:**
```
Cutoff frequency: fc = 1 / (2π * τ)

Examples:
  τ = 100ms → fc = 1.6 Hz
  τ = 1000ms → fc = 0.16 Hz
  τ = 10ms → fc = 16 Hz
```

**Use Cases:**
- Sensor noise filtering
- Analog input smoothing
- Vibration damping
- Signal conditioning

**Example:**
```structured-text
VAR
  raw_sensor : REAL;
  smooth_sensor : REAL;
END_VAR

(* Low-pass filter with 500ms time constant *)
smooth_sensor := FILTER(raw_sensor, 500);
```

**State Storage:**
- OUT_prev (REAL) - Previous output value (4 bytes)

---

### 4. SCALE(IN, IN_MIN, IN_MAX, OUT_MIN, OUT_MAX) → REAL

**Purpose:** Linear scaling/mapping from input range to output range

**Syntax:**
```structured-text
scaled := SCALE(input, in_low, in_high, out_low, out_high);
```

**Parameters:**
- `IN` (REAL) - Input value
- `IN_MIN` (REAL) - Input range minimum
- `IN_MAX` (REAL) - Input range maximum
- `OUT_MIN` (REAL) - Output range minimum
- `OUT_MAX` (REAL) - Output range maximum

**Returns:**
- (REAL) - Scaled output value

**Formula:**
```
OUT = (IN - IN_MIN) / (IN_MAX - IN_MIN) * (OUT_MAX - OUT_MIN) + OUT_MIN
```

**Simplified:**
```
span_in = IN_MAX - IN_MIN
span_out = OUT_MAX - OUT_MIN
OUT = (IN - IN_MIN) * span_out / span_in + OUT_MIN
```

**Clamping:**
- Input clamped to [IN_MIN, IN_MAX] before scaling
- Prevents out-of-range outputs

**Use Cases:**
- ADC to engineering units (e.g., 0-4095 → 0-100%)
- Temperature conversion (e.g., °C to °F)
- Sensor calibration
- Unit conversion

**Example:**
```structured-text
VAR
  adc_value : REAL;      (* 0-4095 raw ADC *)
  pressure_bar : REAL;   (* 0-10 bar *)
END_VAR

(* Scale 0-4095 ADC to 0-10 bar *)
pressure_bar := SCALE(adc_value, 0.0, 4095.0, 0.0, 10.0);
```

**State Storage:**
- **None** (stateless function)

---

## Memory Requirements

### Per Program (v4.8.0)

| Component | Instances | Size Each | Total |
|-----------|-----------|-----------|-------|
| **Timers** | 8 | 24 bytes | 192 bytes |
| **Edges** | 8 | 8 bytes | 64 bytes |
| **Counters** | 8 | 20 bytes | 160 bytes |
| **Latches** | 8 | 4 bytes | 32 bytes |
| **HYSTERESIS** | 8 | 1 byte | 8 bytes |
| **BLINK** | 8 | 6 bytes | 48 bytes |
| **FILTER** | 8 | 4 bytes | 32 bytes |
| **SCALE** | - | 0 bytes (stateless) | 0 bytes |
| **Overhead** | - | 4 bytes | 4 bytes |
| **TOTAL** | - | - | **540 bytes** |

### System-Wide (4 Programs)

- **Maximum**: 4 × 540 = **2.16 KB**
- **Increase from v4.7.3**: +88 bytes per program (+352 bytes total)

---

## Implementation Plan

### Phase 1: Stateless Functions (Quick Win)

**SCALE - Linear Scaling (PRIORITY 1)**
- ✅ No state storage required
- ✅ Direct formula implementation
- ✅ No compiler/VM changes needed
- ⏱️ Estimated time: **30 minutes**

**Implementation:**
```cpp
// src/st_builtin_signal.cpp

st_value_t st_builtin_scale(st_value_t in, st_value_t in_min,
                             st_value_t in_max, st_value_t out_min,
                             st_value_t out_max) {
  st_value_t result;
  result.real_val = 0.0f;

  float in_val = in.real_val;
  float in_min_val = in_min.real_val;
  float in_max_val = in_max.real_val;
  float out_min_val = out_min.real_val;
  float out_max_val = out_max.real_val;

  // Clamp input
  if (in_val < in_min_val) in_val = in_min_val;
  if (in_val > in_max_val) in_val = in_max_val;

  // Avoid divide-by-zero
  if (in_max_val == in_min_val) {
    result.real_val = out_min_val;
    return result;
  }

  // Linear scaling
  float span_in = in_max_val - in_min_val;
  float span_out = out_max_val - out_min_val;
  result.real_val = (in_val - in_min_val) * span_out / span_in + out_min_val;

  return result;
}
```

---

### Phase 2: Stateful Functions (Require Storage)

**Files to Create:**
- `include/st_builtin_signal.h` - Function declarations
- `src/st_builtin_signal.cpp` - All 4 implementations

**Files to Modify:**
- `include/st_stateful.h` - Add signal instance structs
- `src/st_stateful.cpp` - Add allocation functions
- `include/st_builtins.h` - Add enum entries
- `include/st_compiler.h` - Add instance counters
- `src/st_compiler.cpp` - Add compiler registration
- `src/st_vm.cpp` - Add VM execution handlers

**Estimated time:**
- HYSTERESIS: **1 hour**
- BLINK: **1.5 hours** (state machine)
- FILTER: **1 hour**
- Testing: **1 hour**
- Documentation: **30 minutes**
- **Total: ~5 hours**

---

## Testing Strategy

### Unit Tests

1. **HYSTERESIS:**
   - Test upper threshold crossing
   - Test lower threshold crossing
   - Test dead zone state hold
   - Test noise immunity

2. **BLINK:**
   - Test enable/disable
   - Test ON duration
   - Test OFF duration
   - Test period accuracy

3. **FILTER:**
   - Test step response
   - Test time constant
   - Test steady-state accuracy
   - Test settling time

4. **SCALE:**
   - Test linear mapping
   - Test boundary clamping
   - Test divide-by-zero protection
   - Test negative ranges

### Integration Tests

- Test multiple instances per program
- Test mixed stateful/stateless usage
- Test with other function blocks (TON, SR, etc.)

---

## Next Steps

1. ✅ Design complete
2. ⏳ Implement SCALE (stateless, quick win)
3. ⏳ Implement HYSTERESIS
4. ⏳ Implement BLINK
5. ⏳ Implement FILTER
6. ⏳ Test all functions
7. ⏳ Update documentation

---

**Ready to implement!** 🚀
