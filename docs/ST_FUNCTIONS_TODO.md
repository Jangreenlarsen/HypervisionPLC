# ST Logic Functions - Implementation Roadmap

<!-- DOC-STATUS: historisk -->
> ⚠️ **Historisk dokument — vedligeholdes ikke længere.** Skrevet til en ældre firmwareversion og kan være forældet (CLI-syntaks, pins, endpoints, standardværdier). Den aktuelle, vedligeholdte dokumentation er [brugermanualen](manual/00_INDEKS.md) (CLI: appendiks A, REST: appendiks B, ST: appendiks D). Markeret ved dokumentationsgennemgang 2026-09-28.


**Last Updated:** 2025-12-24
**Status:** Phase 1 Partially Complete (v4.4.0)
**Goal:** Expand ST Logic engine with IEC 61131-3 standard functions

---

## ✅ Completed Functions (v4.3-v4.4)

### v4.3.0 - Trigonometric Functions
- ✅ **SIN/COS/TAN** - Fully implemented and tested
- ✅ **ROUND/TRUNC/FLOOR/CEIL** - Rounding functions
- ✅ **Type-aware stack** - REAL arithmetic support

### v4.4.0 - Selection & Modbus Master
- ✅ **LIMIT** - Clamping function (3-argument support)
- ✅ **SEL** - Selection function (3-argument support)
- ✅ **MB_READ_COIL/INPUT/HOLDING/INPUT_REG** - Modbus Master reads
- ✅ **MB_WRITE_COIL/HOLDING** - Modbus Master writes

---

## 🎯 Implementation Priorities

### 🔴 Phase 1: Critical Functions (PARTIALLY COMPLETE)

#### 1. LIMIT(min, val, max) - Clamping Function
**Priority:** 🔴 Critical
**Complexity:** 🟢 Low (1-2 hours)
**Status:** ✅ COMPLETED (v4.4.0)

**Function Signature:**
```structured-text
LIMIT(min_val: INT, value: INT, max_val: INT) : INT
```

**Implementation:**
- File: `src/st_builtins.cpp`
- Add: `ST_BUILTIN_LIMIT` enum
- Function: `st_builtin_limit(min, val, max)`
- Support: INT, DWORD, REAL types

**Use Case:**
```structured-text
VAR
  temperature: INT;
  safe_temp: INT;
END_VAR

safe_temp := LIMIT(0, temperature, 100);  (* Clamp to 0-100°C *)
```

**Testing:**
- Test: Clamp below minimum (input=-10, expect=0)
- Test: Clamp above maximum (input=150, expect=100)
- Test: Value in range (input=50, expect=50)
- Test: REAL type clamping

---

#### 2. TON/TOF - Timer Functions
**Priority:** 🔴 Critical
**Complexity:** 🔴 High (8-12 hours)
**Status:** ⏳ Not Started

**Function Signatures:**
```structured-text
TON(IN: BOOL, PT: DWORD) : BOOL     (* On-Delay Timer *)
TOF(IN: BOOL, PT: DWORD) : BOOL     (* Off-Delay Timer *)
TP(IN: BOOL, PT: DWORD) : BOOL      (* Pulse Timer *)
```

**Implementation:**
- File: `src/st_builtin_timers.cpp` (NEW)
- Header: `include/st_builtin_timers.h` (NEW)
- Stateful: Requires instance storage (timer state per variable)
- Uses: `millis()` for timing

**Timer State Structure:**
```cpp
typedef struct {
  uint8_t timer_type;    // 0=TON, 1=TOF, 2=TP
  bool last_IN;          // Previous input state
  uint32_t start_time;   // Timer start timestamp (millis)
  uint32_t PT;           // Preset time (ms)
  bool Q;                // Output state
  uint32_t ET;           // Elapsed time (ms)
  bool running;          // Timer active flag
} ST_Timer_Instance;
```

**Challenge:**
- Timers are STATEFUL (need to remember state between cycles)
- Current VM is stateless (variables cleared each cycle)
- **Solution:** Add timer instance storage to bytecode struct

**Use Case:**
```structured-text
VAR
  motor_start_delay: DWORD := 5000;  (* 5 seconds *)
  motor_running: BOOL;
END_VAR

motor_running := TON(start_button, motor_start_delay);
(* Motor starts 5 seconds after button pressed *)
```

**Testing:**
- Test: TON activates after delay
- Test: TON resets on input false
- Test: TOF delays turn-off
- Test: TP generates fixed pulse

---

#### 3. R_TRIG/F_TRIG - Edge Detection
**Priority:** 🔴 Critical
**Complexity:** 🟡 Medium (4-6 hours)
**Status:** ⏳ Not Started

**Function Signatures:**
```structured-text
R_TRIG(signal: BOOL) : BOOL    (* Rising Edge: false→true *)
F_TRIG(signal: BOOL) : BOOL    (* Falling Edge: true→false *)
```

**Implementation:**
- File: `src/st_builtin_edge.cpp` (NEW)
- Header: `include/st_builtin_edge.h` (NEW)
- Stateful: Requires last state storage

**Edge State Structure:**
```cpp
typedef struct {
  bool last_state;  // Previous signal state
} ST_Edge_Instance;
```

**Algorithm:**
```cpp
bool R_TRIG(bool current, ST_Edge_Instance* state) {
  bool result = (current && !state->last_state);  // Detect 0→1
  state->last_state = current;
  return result;
}

bool F_TRIG(bool current, ST_Edge_Instance* state) {
  bool result = (!current && state->last_state);  // Detect 1→0
  state->last_state = current;
  return result;
}
```

**Use Case:**
```structured-text
VAR
  button_count: INT := 0;
END_VAR

IF R_TRIG(button_input) THEN
  button_count := button_count + 1;  (* Count button presses *)
END_IF;
```

**Testing:**
- Test: R_TRIG on rising edge (0→1)
- Test: R_TRIG ignores high state (1→1)
- Test: F_TRIG on falling edge (1→0)
- Test: F_TRIG ignores low state (0→0)

---

#### 4. SIN/COS/TAN - Trigonometric Functions
**Priority:** 🔴 Critical (Motion Control)
**Complexity:** 🟢 Low (2-3 hours)
**Status:** ✅ COMPLETED (v4.3.0)

**Function Signatures:**
```structured-text
SIN(angle: REAL) : REAL    (* Sine, angle in radians *)
COS(angle: REAL) : REAL    (* Cosine, angle in radians *)
TAN(angle: REAL) : REAL    (* Tangent, angle in radians *)
```

**Implementation:**
- File: `src/st_builtins.cpp` (existing)
- Add: `ST_BUILTIN_SIN`, `ST_BUILTIN_COS`, `ST_BUILTIN_TAN` enums
- Use: C math library (`sinf()`, `cosf()`, `tanf()`)

**Code:**
```cpp
st_value_t st_builtin_sin(st_value_t x) {
  st_value_t result;
  result.real_val = sinf(x.real_val);
  return result;
}
```

**Use Case:**
```structured-text
VAR
  angle: REAL := 0.0;
  x_pos: REAL;
  y_pos: REAL;
  radius: REAL := 100.0;
END_VAR

(* Circular motion *)
x_pos := radius * COS(angle);
y_pos := radius * SIN(angle);
angle := angle + 0.1;  (* Increment angle *)
```

**Testing:**
- Test: SIN(0.0) = 0.0
- Test: SIN(π/2) ≈ 1.0
- Test: COS(0.0) = 1.0
- Test: TAN(π/4) ≈ 1.0

---

#### 5. SEL/MUX - Selection Functions
**Priority:** 🔴 Critical
**Complexity:** 🟢 Low (2-3 hours)
**Status:** ⚠️ PARTIAL - SEL ✅ COMPLETED (v4.4.0), MUX ⏳ Not Started

**Function Signatures:**
```structured-text
SEL(G: BOOL, IN0: ANY, IN1: ANY) : ANY    (* Select: G=false→IN0, G=true→IN1 *)
MUX(K: INT, IN0: ANY, IN1: ANY, ...) : ANY (* Multiplex: select IN[K] *)
```

**Implementation:**
- File: `src/st_builtins.cpp`
- Add: `ST_BUILTIN_SEL`, `ST_BUILTIN_MUX` enums
- SEL: 3 arguments (bool selector + 2 values)
- MUX: Variable arguments (int selector + N values)

**Code:**
```cpp
st_value_t st_builtin_sel(st_value_t G, st_value_t IN0, st_value_t IN1) {
  return (G.bool_val) ? IN1 : IN0;
}

st_value_t st_builtin_mux(st_value_t K, st_value_t* inputs, uint8_t count) {
  int index = K.int_val;
  if (index < 0 || index >= count) return inputs[0];  // Default to first
  return inputs[index];
}
```

**Use Case:**
```structured-text
VAR
  manual_mode: BOOL;
  manual_setpoint: INT := 50;
  auto_setpoint: INT := 75;
  active_setpoint: INT;
END_VAR

(* Select setpoint based on mode *)
active_setpoint := SEL(manual_mode, auto_setpoint, manual_setpoint);
```

**Testing:**
- Test: SEL(false, 10, 20) = 10
- Test: SEL(true, 10, 20) = 20
- Test: MUX(0, 10, 20, 30) = 10
- Test: MUX(2, 10, 20, 30) = 30
- Test: MUX out of bounds returns first value

---

## 🟡 Phase 2: Important Functions

### 6. EXP/LN/LOG/POW - Exponential Functions
**Priority:** 🟡 High
**Complexity:** 🟢 Low (2 hours)
**Status:** ⏳ Not Started

```structured-text
EXP(x: REAL) : REAL      (* e^x *)
LN(x: REAL) : REAL       (* Natural log *)
LOG(x: REAL) : REAL      (* Base-10 log *)
POW(x: REAL, y: REAL) : REAL  (* x^y *)
```

---

### 7. CTU/CTD - Counter Functions
**Priority:** 🟡 High
**Complexity:** 🟡 Medium (4-6 hours)
**Status:** ⏳ Not Started

```structured-text
CTU(CU: BOOL, RESET: BOOL, PV: INT) : INT  (* Count Up *)
CTD(CD: BOOL, LOAD: BOOL, PV: INT) : INT   (* Count Down *)
```

**Stateful:** Requires counter instance storage (current count, last CU/CD state)

---

### 8. ROL/ROR - Bit Rotation
**Priority:** 🟡 Medium
**Complexity:** 🟢 Low (1 hour)
**Status:** ⏳ Not Started

```structured-text
ROL(value: DWORD, n: INT) : DWORD   (* Rotate Left *)
ROR(value: DWORD, n: INT) : DWORD   (* Rotate Right *)
```

---

### 9. NOW() - Timestamp Function
**Priority:** 🟡 Medium
**Complexity:** 🟢 Low (1 hour)
**Status:** ⏳ Not Started

```structured-text
NOW() : DWORD    (* Current timestamp in milliseconds *)
```

**Implementation:** Return `millis()` as DWORD

---

## 🟢 Phase 3: Advanced Functions

### 10. Array Support
**Priority:** 🟢 Low
**Complexity:** 🔴 Very High (20-30 hours)
**Status:** ⏳ Not Started

**Requires:**
- Parser changes (ARRAY syntax)
- Compiler changes (array allocation)
- VM changes (array indexing opcodes)
- Type system changes

---

### 11. String Functions
**Priority:** 🟢 Low
**Complexity:** 🔴 High (12-16 hours)
**Status:** ⏳ Not Started

```structured-text
LEN(s: STRING) : INT
CONCAT(s1: STRING, s2: STRING) : STRING
LEFT(s: STRING, n: INT) : STRING
RIGHT(s: STRING, n: INT) : STRING
MID(s: STRING, pos: INT, n: INT) : STRING
```

**Requires:** STRING type support (currently not implemented)

---

### 12. Function Blocks (FB)
**Priority:** 🟢 Low
**Complexity:** 🔴 Very High (40+ hours)
**Status:** ⏳ Not Started

**Requires:**
- FUNCTION_BLOCK syntax parsing
- FB instance allocation
- Method call mechanism
- Separate FB compilation

---

## 📊 Implementation Status Summary

| Phase | Function | Priority | Complexity | Status | Estimate | Actual |
|-------|----------|----------|------------|--------|----------|--------|
| 1 | LIMIT | 🔴 | 🟢 | ✅ DONE | 1-2h | v4.4.0 |
| 1 | TON/TOF | 🔴 | 🔴 | ⏳ | 8-12h | - |
| 1 | R_TRIG/F_TRIG | 🔴 | 🟡 | ⏳ | 4-6h | - |
| 1 | SIN/COS/TAN | 🔴 | 🟢 | ✅ DONE | 2-3h | v4.3.0 |
| 1 | SEL/MUX | 🔴 | 🟢 | ⚠️ PARTIAL (SEL done) | 2-3h | v4.4.0 |
| 1 | MB_READ_* | 🔴 | 🟡 | ✅ DONE | - | v4.4.0 |
| 1 | MB_WRITE_* | 🔴 | 🟡 | ✅ DONE | - | v4.4.0 |
| 2 | EXP/LN/LOG/POW | 🟡 | 🟢 | ⏳ | 2h | - |
| 2 | CTU/CTD | 🟡 | 🟡 | ⏳ | 4-6h | - |
| 2 | ROL/ROR | 🟡 | 🟢 | ⏳ | 1h | - |
| 2 | NOW() | 🟡 | 🟢 | ⏳ | 1h | - |
| 3 | Arrays | 🟢 | 🔴 | ⏳ | 20-30h | - |
| 3 | Strings | 🟢 | 🔴 | ⏳ | 12-16h | - |
| 3 | Function Blocks | 🟢 | 🔴 | ⏳ | 40+h | - |

**Phase 1 Progress:** 3/5 functions complete (LIMIT, SIN/COS/TAN, SEL), plus Modbus Master (6 functions)
**Remaining Phase 1:** TON/TOF, R_TRIG/F_TRIG, MUX (12-21 hours)
**Total Estimate (Phase 2):** 8-13 hours
**Total Estimate (Phase 3):** 72-86 hours

---

## 🔧 Technical Challenges

### Challenge 1: Stateful Functions (Timers, Edge Detection, Counters)

**Problem:**
ST VM currently executes programs as pure functions. Timers/edges need to remember state between cycles.

**Solution:**
Add instance storage to bytecode:

```cpp
typedef struct {
  ST_Timer_Instance timers[16];     // Max 16 timer instances
  ST_Edge_Instance edges[16];       // Max 16 edge instances
  ST_Counter_Instance counters[16]; // Max 16 counter instances
  uint8_t timer_count;
  uint8_t edge_count;
  uint8_t counter_count;
} st_stateful_storage_t;
```

Add to `st_bytecode_t`:
```cpp
typedef struct {
  // ... existing fields ...
  st_stateful_storage_t* stateful;  // NEW: Instance storage
} st_bytecode_t;
```

**Impact:**
- Parser must detect timer/edge/counter calls
- Compiler must allocate instance IDs
- VM must pass instance storage to builtin functions

---

### Challenge 2: Variable Argument Functions (MUX)

**Problem:**
MUX can have 2-N inputs: `MUX(K, IN0, IN1, IN2, ...)`

**Solution:**
Use stack-based argument passing:

```cpp
// Compiler emits:
PUSH_INT 2        // K selector
PUSH_INT 10       // IN0
PUSH_INT 20       // IN1
PUSH_INT 30       // IN2
PUSH_INT 3        // Argument count
CALL_BUILTIN_VARARG ST_BUILTIN_MUX
```

New opcode: `ST_OP_CALL_BUILTIN_VARARG`

---

## 📝 Implementation Checklist Template

For each function, follow this checklist:

- [ ] Add enum to `st_builtins.h` (e.g., `ST_BUILTIN_LIMIT`)
- [ ] Implement function in `st_builtins.cpp`
- [ ] Add case to `st_builtin_call()` dispatcher
- [ ] Update `st_builtin_name()` for debugging
- [ ] Update `st_builtin_arg_count()` if needed
- [ ] Add parser support in `st_parser.cpp` (recognize function name)
- [ ] Add compiler support in `st_compiler.cpp` (emit CALL_BUILTIN)
- [ ] Write unit test in `tests/` (if test framework exists)
- [ ] Update `docs/README_ST_LOGIC.md` with examples
- [ ] Test in real ST program on hardware
- [ ] Commit with descriptive message

---

## 🎯 Next Steps

1. **Start with LIMIT** (easiest, immediate value)
2. **Add trigonometry** (SIN/COS/TAN - simple math lib calls)
3. **Implement SEL** (simple conditional select)
4. **Tackle edge detection** (R_TRIG/F_TRIG - introduces stateful concept)
5. **Build timer infrastructure** (TON/TOF - most complex, highest value)

---

**End of Roadmap**
