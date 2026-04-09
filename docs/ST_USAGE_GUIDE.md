# Structured Text Logic Mode - Usage Guide

**ESP32 Modbus RTU Server v2.1.0+** (with unified VariableMapping system)

---

## 📚 Table of Contents

1. [Quick Start](#quick-start)
2. [CLI Commands](#cli-commands)
3. [ST Language Examples](#st-language-examples)
4. [Variable Bindings (Modbus I/O)](#variable-bindings-modbus-io)
5. [Built-in Functions](#built-in-functions)
6. [Troubleshooting](#troubleshooting)

---

## Quick Start

### 1. Upload a Simple Logic Program

```bash
# ST program: If counter > 100, set relay ON
set logic 1 upload "VAR counter: INT; relay: BOOL; END_VAR IF counter > 100 THEN relay := TRUE; ELSE relay := FALSE; END_IF;"
```

✓ Output: `Logic1 compiled successfully (xxx bytes, xx instructions)`

### 2. Bind Variables to Modbus Registers

```bash
# NEW SYNTAX: Bind by variable name
set logic 1 bind counter reg:100      # counter reads from holding register #100
set logic 1 bind relay coil:101       # relay writes to coil #101
```

✓ Output:
```
✓ Logic1: var[0] (counter) ← Modbus HR#100
✓ Logic1: var[1] (relay) → Modbus Coil#101
```

**Alternative (legacy) syntax:**
```bash
set logic 1 bind 0 100 input          # Variable index 0, register 100, input mode
set logic 1 bind 1 101 output         # Variable index 1, register 101, output mode
```

### 3. Enable the Program

```bash
set logic 1 enabled:true
```

✓ Output: `✓ Logic1 ENABLED`

### 4. Verify Configuration

```bash
show logic 1
```

Output:
```
=== Logic Program: Logic1 ===
Enabled: YES
Compiled: YES
Source Code: xxx bytes

Source:
VAR counter: INT; relay: BOOL; END_VAR IF counter > 100...

Variable Bindings: 2
  [0] ST var[0] ← Modbus HR#100
  [1] ST var[1] → Modbus HR#101

Executions: 42
Errors: 0
```

---

## CLI Commands

### Upload & Manage Programs

```bash
# Upload ST source code
set logic <id> upload "<st_code>"

# Enable/disable program
set logic <id> enabled:true
set logic <id> enabled:false

# Delete program
set logic <id> delete

# Bind variable to Modbus register (NEW SYNTAX - recommended)
set logic <id> bind <var_name> reg:<register>      # Holding register (INT/DWORD)
set logic <id> bind <var_name> coil:<register>     # Coil output (BOOL write)
set logic <id> bind <var_name> input-dis:<input>   # Discrete input (BOOL read)

# Legacy syntax (still supported for backward compatibility)
set logic <id> bind <var_idx> <register> [input|output|both]
```

### View Status

```bash
# Show detailed info about one program
show logic <id>

# Show all programs
show logic all

# Show execution statistics
show logic stats
```

---

## ST Language Examples

### Example 1: Simple IF/ELSE (Threshold Logic)

```structured-text
VAR
  temperature: INT;
  heating: BOOL;
  cooling: BOOL;
END_VAR

IF temperature < 18 THEN
  heating := TRUE;
  cooling := FALSE;
ELSIF temperature > 25 THEN
  heating := FALSE;
  cooling := TRUE;
ELSE
  heating := FALSE;
  cooling := FALSE;
END_IF;
```

**CLI:**
```bash
set logic 1 upload "VAR temperature: INT; heating: BOOL; cooling: BOOL; END_VAR IF temperature < 18 THEN heating := TRUE; cooling := FALSE; ELSIF temperature > 25 THEN heating := FALSE; cooling := TRUE; ELSE heating := FALSE; cooling := FALSE; END_IF;"
set logic 1 bind temperature reg:10  # temperature reads from HR#10
set logic 1 bind heating coil:11     # heating writes to coil #11
set logic 1 bind cooling coil:12     # cooling writes to coil #12
set logic 1 enabled:true
```

---

### Example 2: Counting Loop

```structured-text
VAR
  count: INT;
  total: INT;
END_VAR

count := 0;
total := 0;

FOR count := 1 TO 10 DO
  total := total + count;
END_FOR;
```

**CLI:**
```bash
set logic 1 upload "VAR count: INT; total: INT; END_VAR count := 0; total := 0; FOR count := 1 TO 10 DO total := total + count; END_FOR;"
set logic 1 bind count reg:20   # count writes to HR#20
set logic 1 bind total reg:21   # total writes to HR#21
set logic 1 enabled:true
```

Result after execution: `total = 55` (1+2+3+...+10)

---

### Example 3: Using WHILE Loop with Condition

```structured-text
VAR
  sensor: INT;
  pulses: INT;
END_VAR

sensor := 0;
pulses := 0;

WHILE sensor < 100 DO
  sensor := sensor + 10;
  pulses := pulses + 1;
END_WHILE;
```

---

### Example 4: Using Built-in Functions

```structured-text
VAR
  value1: INT;
  value2: INT;
  min_value: INT;
  max_value: INT;
  abs_value: INT;
END_VAR

min_value := MIN(value1, value2);
max_value := MAX(value1, value2);
abs_value := ABS(value1);
```

**CLI:**
```bash
set logic 1 upload "VAR value1: INT; value2: INT; min_value: INT; max_value: INT; abs_value: INT; END_VAR min_value := MIN(value1, value2); max_value := MAX(value1, value2); abs_value := ABS(value1);"
set logic 1 bind value1 reg:30      # value1 reads from HR#30
set logic 1 bind value2 reg:31      # value2 reads from HR#31
set logic 1 bind min_value reg:32   # min_value writes to HR#32
set logic 1 bind max_value reg:33   # max_value writes to HR#33
set logic 1 bind abs_value reg:34   # abs_value writes to HR#34
set logic 1 enabled:true
```

---

### Example 5: Nested IF Statements

```structured-text
VAR
  x: INT;
  y: INT;
  result: INT;
END_VAR

IF x > 10 THEN
  IF y > 20 THEN
    result := 3;
  ELSE
    result := 2;
  END_IF;
ELSE
  result := 1;
END_IF;
```

---

## Variable Bindings (Modbus I/O)

**[V2.1.0+]** Variable bindings use the unified **VariableMapping system** which also handles GPIO pins. Both GPIO and ST variables use the same I/O engine.

**3-Phase Execution Model (every 10ms):**
```
Phase 1: Read all INPUTs (GPIO + ST variables from Modbus)
Phase 2: Execute enabled ST programs
Phase 3: Write all OUTPUTs (GPIO + ST variables to Modbus)
```

This means ST variables and GPIO pins are treated identically - they both sync with Modbus through the same unified mapping system.

### Input Variables (Read from Registers)

Variables read from Modbus holding registers **before** program execution:

```bash
# NEW SYNTAX: Use reg: for holding registers (automatic input/output based on variable)
set logic 1 bind counter reg:100   # counter ← HR#100 before execution
```

When the program runs, `counter` will contain the value from `HR#100`.

### Output Variables (Write to Registers)

Variables write to Modbus registers **after** program execution:

```bash
# Write to coil (BOOL output)
set logic 1 bind relay coil:10     # relay → Coil#10 after execution

# Write to holding register (INT output)
set logic 1 bind result reg:101    # result → HR#101 after execution
```

When the program completes, the value is written to the register.

### Input-Only BOOL Variables

For discrete inputs (read-only boolean):

```bash
set logic 1 bind button input-dis:5   # button ← Discrete Input#5 before execution
```

**Legend:**
- `reg:` → Holding Register (INT/DWORD, can be input or output)
- `coil:` → Coil (BOOL, write-only)
- `input-dis:` → Discrete Input (BOOL, read-only)

---

## Built-in Functions

### Mathematical Functions

```structured-text
result := ABS(-42);              (* → 42 *)
min_val := MIN(10, 5);           (* → 5 *)
max_val := MAX(10, 5);           (* → 10 *)
sum_val := SUM(10, 5);           (* → 15 *)
sqrt_val := SQRT(16.0);          (* → 4.0 *)
rounded := ROUND(3.7);           (* → 4 *)
truncated := TRUNC(3.9);         (* → 3 *)
floored := FLOOR(3.7);           (* → 3 *)
ceiled := CEIL(3.2);             (* → 4 *)
```

### Type Conversion Functions

```structured-text
real_val := INT_TO_REAL(42);     (* → 42.0 *)
int_val := REAL_TO_INT(3.7);     (* → 3 *)
int_from_bool := BOOL_TO_INT(TRUE);  (* → 1 *)
bool_from_int := INT_TO_BOOL(42);    (* → TRUE *)
int_from_dword := DWORD_TO_INT(1000);
dword_from_int := INT_TO_DWORD(42);
```

### Clamping & Selection Functions (v4.4+)

```structured-text
(* LIMIT: Clamp value between min and max *)
safe_val := LIMIT(0, sensor, 100);    (* Keeps sensor in range 0-100 *)
temp := LIMIT(-10, temperature, 50);  (* Clamps temperature to -10...50 *)

(* SEL: Select one of two inputs based on condition *)
output := SEL(selector, value_0, value_1);
(* If selector=FALSE → returns value_0 *)
(* If selector=TRUE  → returns value_1 *)

(* Example: Choose heating or cooling setpoint *)
setpoint := SEL(mode, 18, 24);  (* mode=FALSE → 18°C, mode=TRUE → 24°C *)

(* MUX: 4-way multiplexer - select one of three inputs by index *)
output := MUX(K, IN0, IN1, IN2);
(* K=0 → returns IN0 (default) *)
(* K=1 → returns IN1 *)
(* K=2 → returns IN2 *)
(* K<0 or K>2 → returns IN0 (default) *)

(* Example 1: Select temperature setpoint based on mode *)
VAR
  mode: INT;          (* 0=OFF, 1=ECO, 2=COMFORT *)
  setpoint: INT;
END_VAR

setpoint := MUX(mode, 0, 18, 22);
(* mode=0 → 0°C (OFF) *)
(* mode=1 → 18°C (ECO) *)
(* mode=2 → 22°C (COMFORT) *)

(* Example 2: Select pump speed based on sensor level *)
VAR
  level: INT;         (* Water level sensor 0-100 *)
  level_zone: INT;    (* 0=LOW, 1=MEDIUM, 2=HIGH *)
  pump_speed: INT;
END_VAR

IF level < 30 THEN
  level_zone := 0;
ELSIF level < 70 THEN
  level_zone := 1;
ELSE
  level_zone := 2;
END_IF;

pump_speed := MUX(level_zone, 0, 50, 100);
(* Low level → 0% (pump off) *)
(* Medium level → 50% (half speed) *)
(* High level → 100% (full speed) *)

(* Example 3: Multi-source sensor selection *)
VAR
  sensor_select: INT;  (* 0=internal, 1=external_A, 2=external_B *)
  temp_internal: INT;
  temp_ext_A: INT;
  temp_ext_B: INT;
  temperature: INT;
END_VAR

temperature := MUX(sensor_select, temp_internal, temp_ext_A, temp_ext_B);
```

### Trigonometric Functions (v4.4+)

**Note:** All trigonometric functions work with REAL values. Angles are in **radians**.

```structured-text
(* Convert degrees to radians: radians = degrees * PI / 180.0 *)
VAR
  angle_deg: INT;
  angle_rad: REAL;
  sine: REAL;
  cosine: REAL;
  tangent: REAL;
  PI: REAL := 3.14159265;
END_VAR

angle_deg := 90;  (* 90 degrees *)
angle_rad := INT_TO_REAL(angle_deg) * PI / 180.0;

sine := SIN(angle_rad);      (* → 1.0 (sin 90° = 1) *)
cosine := COS(angle_rad);    (* → 0.0 (cos 90° = 0) *)
tangent := TAN(angle_rad);   (* → undefined (tan 90° → infinity) *)

(* Common values: *)
sine_0 := SIN(0.0);          (* → 0.0 *)
cosine_0 := COS(0.0);        (* → 1.0 *)
sine_30 := SIN(PI / 6.0);    (* → 0.5 (30 degrees) *)
cosine_60 := COS(PI / 3.0);  (* → 0.5 (60 degrees) *)
```

### TIME Datatype (v7.9.4.0, FEAT-121)

IEC 61131-3 TIME datatype til tidsbaseret logik. Repræsenteres internt som 32-bit millisekunder (op til ~24.8 dage).

#### TIME Literal Syntax

```structured-text
VAR
  short_delay : TIME := T#100ms;       (* 100 millisekunder *)
  five_sec    : TIME := T#5s;          (* 5000 ms *)
  one_min     : TIME := T#1m;          (* 60000 ms *)
  complex     : TIME := T#1h30m5s;     (* 5405000 ms *)
  max_time    : TIME := T#24d;         (* ~24 dage *)
END_VAR
```

**Understøttede enheder:**
| Enhed | Eksempel | Millisekunder |
|-------|----------|---------------|
| `ms`  | `T#100ms` | 100 |
| `s`   | `T#5s` | 5000 |
| `m`   | `T#2m` | 120000 |
| `h`   | `T#1h` | 3600000 |
| `d`   | `T#1d` | 86400000 |

Enheder kan kombineres: `T#1h30m15s100ms` = 5415100 ms.

**Aritmetik:** TIME-værdier understøtter standard aritmetik og sammenligninger:
```structured-text
total := delay1 + delay2;       (* TIME + TIME *)
remaining := timeout - elapsed;  (* TIME - TIME *)
IF elapsed > T#10s THEN ...     (* TIME sammenligning *)
```

---

### Timer Function Blocks (v4.7+, named params v7.9.4.0)

IEC 61131-3 standard timer function blocks med millisekund-præcision. Alle timere bevarer tilstand mellem programcyklusser (stateful).

**Max 8 timer-instanser per program.**

Understøtter to syntaxer:
1. **IEC 61131-3 named parameters** (v7.9.4.0) — anbefalet
2. **Positionel syntax** — backward-kompatibel

---

#### TON - On-Delay Timer (FEAT-122)

Aktiverer output Q efter input IN har vaeret TRUE i PT tid. Bruges til debouncing, forsinkede aktiveringer og timeout-overvagning.

**Timing-diagram:**
```
IN:  __|‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾|_______
Q:   _________|‾‾‾‾‾‾‾‾‾|_______
ET:  0...PT   PT  PT  PT  0
          ^-- Q goes TRUE after PT elapsed
```

**IEC 61131-3 syntax (anbefalet):**
```structured-text
VAR
  start_btn : BOOL;
  motor_on  : BOOL;
  elapsed   : TIME;
END_VAR

(* Motor starter 5 sekunder efter knap trykkes *)
TON(IN := start_btn, PT := T#5s, Q => motor_on, ET => elapsed);
```

**Positionel syntax (backward-kompatibel):**
```structured-text
motor_on := TON(start_btn, 5000);   (* 5000 ms = 5 sekunder *)
```

**Input parametre:**
| Param | Type | Beskrivelse |
|-------|------|-------------|
| `IN`  | BOOL | Trigger input — timer korer mens TRUE |
| `PT`  | TIME/INT | Preset tid i millisekunder |

**Output parametre:**
| Param | Type | Beskrivelse |
|-------|------|-------------|
| `Q`   | BOOL | Output — TRUE naar IN har vaeret TRUE i >= PT tid |
| `ET`  | TIME | Elapsed time — tid siden IN gik TRUE (0 naar IN=FALSE) |

**Opforsel:**
- Rising edge pa IN: Start timer, ET=0, Q=FALSE
- IN=TRUE og ET < PT: Timer korer, Q=FALSE, ET stiger
- IN=TRUE og ET >= PT: Timer faerdig, Q=TRUE, ET=PT
- IN=FALSE: Nulstil alt, Q=FALSE, ET=0

---

#### TOF - Off-Delay Timer (FEAT-123)

Holder output Q aktiv i PT tid efter input IN gar FALSE. Bruges til efterkoring, hold-tider og forsinket slukning.

**Timing-diagram:**
```
IN:  __|‾‾‾‾‾|_________________
Q:   __|‾‾‾‾‾‾‾‾‾‾‾‾‾|________
ET:  0  0  0  0...PT   0
                   ^-- Q goes FALSE after PT elapsed
```

**IEC 61131-3 syntax:**
```structured-text
VAR
  motion     : BOOL;
  light_on   : BOOL;
  remaining  : TIME;
END_VAR

(* Lys forbliver taendt 30 sekunder efter bevaegelse stopper *)
TOF(IN := motion, PT := T#30s, Q => light_on, ET => remaining);
```

**Positionel syntax:**
```structured-text
light_on := TOF(motion, 30000);
```

**Input/Output parametre:** Samme som TON.

**Opforsel:**
- Rising edge pa IN: Q=TRUE straks, ET=0
- IN=TRUE: Q forbliver TRUE, ET=0
- Falling edge pa IN: Start timer, ET=0
- IN=FALSE og ET < PT: Q forbliver TRUE, ET stiger
- IN=FALSE og ET >= PT: Q=FALSE

---

#### TP - Pulse Timer

Genererer en puls af fast laengde PT pa rising edge af IN. Bruges til faste pulsbredder og one-shot signaler.

**Timing-diagram:**
```
IN:  __|‾‾‾‾‾‾‾‾‾‾‾‾‾‾|_______
Q:   __|‾‾‾‾‾‾‾|______________
ET:  0...PT     PT  0  0  0
          ^-- Q goes FALSE after PT, ignoring IN
```

**Positionel syntax:**
```structured-text
VAR
  trigger : BOOL;
  pulse   : BOOL;
END_VAR

pulse := TP(trigger, 500);   (* 500ms puls *)
```

---

#### Eksempel: Start-forsinket motor med timeout

```structured-text
VAR
  start_btn    : BOOL;
  stop_btn     : BOOL;
  motor_enable : BOOL;
  motor_run    : BOOL;
  start_delay  : TIME;
END_VAR

(* 3 sekunders startforsinkelse *)
TON(IN := start_btn AND NOT stop_btn,
    PT := T#3s,
    Q  => motor_enable,
    ET => start_delay);

(* Motor korer kun naar delay er overstaaet og stop ikke er aktiv *)
motor_run := motor_enable AND NOT stop_btn;
```

#### Eksempel: Bevaegelsessensor med efterkoring

```structured-text
VAR
  pir_sensor   : BOOL;     (* PIR bevaegelsessensor *)
  light        : BOOL;     (* Lys output *)
  hold_time    : TIME;
END_VAR

(* Lys slukker 2 minutter efter sidste bevaegelse *)
TOF(IN := pir_sensor, PT := T#2m, Q => light, ET => hold_time);
```

#### Eksempel: Debounce med TON

```structured-text
VAR
  raw_input    : BOOL;     (* Raa kontakt-signal med bounce *)
  debounced    : BOOL;     (* Stabilt signal *)
END_VAR

(* Input skal vaere stabil i 50ms for at tael *)
debounced := TON(raw_input, 50);
```

---

### Counter Function Blocks (v4.8.1+, named params v7.9.4.0)

IEC 61131-3 standard counter function blocks til taelling af haendelser. Alle tael edge-triggered og bevarer tilstand mellem cyklusser (stateful).

**Max 8 counter-instanser per program.**

Understotter to syntaxer:
1. **IEC 61131-3 named parameters** (v7.9.4.0) — anbefalet
2. **Positionel syntax** — backward-kompatibel

---

#### CTU - Count Up Counter (FEAT-124)

Inkrementerer taeller pa rising edge af CU. Output Q gar TRUE naar count naar preset PV.

**IEC 61131-3 syntax (anbefalet):**
```structured-text
VAR
  pulse      : BOOL;
  reset      : BOOL;
  batch_done : BOOL;
  count      : INT;
END_VAR

(* Tael til 100 — batch faerdig *)
CTU(CU := pulse, RESET := reset, PV := 100,
    Q => batch_done, CV => count);

(* batch_done = TRUE naar count >= 100 *)
(* count indeholder aktuel taellervaerdi *)
```

**Positionel syntax:**
```structured-text
batch_done := CTU(pulse, reset, 100);
```

**Input parametre:**
| Param | Type | Beskrivelse |
|-------|------|-------------|
| `CU`    | BOOL | Count-up input (rising edge trigger) |
| `RESET` | BOOL | Reset input (TRUE nulstiller CV til 0) |
| `PV`    | INT  | Preset vaerdi (maaltael) |

**Output parametre:**
| Param | Type | Beskrivelse |
|-------|------|-------------|
| `Q`   | BOOL | TRUE naar CV >= PV |
| `CV`  | INT  | Aktuel taellervaerdi |

**Opforsel:**
- Rising edge pa CU: CV++ (clamp ved INT32_MAX)
- RESET=TRUE: CV=0, Q=FALSE (hojeste prioritet)
- CV >= PV: Q=TRUE

---

#### CTD - Count Down Counter (FEAT-125)

Dekrementerer taeller pa rising edge af CD. Output Q gar TRUE naar count naar nul.

**IEC 61131-3 syntax:**
```structured-text
VAR
  pulse      : BOOL;
  load       : BOOL;
  empty      : BOOL;
  remaining  : INT;
END_VAR

(* Nedtael fra 50 *)
CTD(CD := pulse, LOAD := load, PV := 50,
    Q => empty, CV => remaining);

(* empty = TRUE naar remaining <= 0 *)
```

**Positionel syntax:**
```structured-text
empty := CTD(pulse, load, 50);
```

**Input parametre:**
| Param | Type | Beskrivelse |
|-------|------|-------------|
| `CD`   | BOOL | Count-down input (rising edge trigger) |
| `LOAD` | BOOL | Load input (rising edge loader PV ind i CV) |
| `PV`   | INT  | Preset vaerdi (startvaerdi) |

**Output parametre:**
| Param | Type | Beskrivelse |
|-------|------|-------------|
| `Q`   | BOOL | TRUE naar CV <= 0 |
| `CV`  | INT  | Aktuel taellervaerdi |

---

#### CTUD - Count Up/Down Counter (FEAT-126)

Bidirektional taeller — inkrementerer pa CU, dekrementerer pa CD. Tre outputs: QU (ovre graense), QD (nul) og CV (vaerdi).

**IEC 61131-3 syntax:**
```structured-text
VAR
  inc_pulse  : BOOL;
  dec_pulse  : BOOL;
  rst        : BOOL;
  ld         : BOOL;
  at_max     : BOOL;
  at_zero    : BOOL;
  count      : INT;
END_VAR

CTUD(CU := inc_pulse, CD := dec_pulse,
     RESET := rst, LOAD := ld, PV := 100,
     QU => at_max, QD => at_zero, CV => count);
```

**Positionel syntax:**
```structured-text
at_max := CTUD(inc_pulse, dec_pulse, rst, ld, 100);
```

**Input parametre:**
| Param | Type | Beskrivelse |
|-------|------|-------------|
| `CU`    | BOOL | Count-up input (rising edge) |
| `CD`    | BOOL | Count-down input (rising edge) |
| `RESET` | BOOL | Reset (CV=0) |
| `LOAD`  | BOOL | Load (CV=PV) |
| `PV`    | INT  | Preset vaerdi (ovre graense) |

**Output parametre:**
| Param | Type | Beskrivelse |
|-------|------|-------------|
| `QU`  | BOOL | TRUE naar CV >= PV |
| `QD`  | BOOL | TRUE naar CV <= 0 |
| `CV`  | INT  | Aktuel taellervaerdi |

**Prioritet (hojest til lavest):**
1. RESET (vinder altid — CV=0)
2. LOAD (CV=PV, kun hvis RESET=FALSE)
3. CU/CD (taelling, kun hvis hverken RESET eller LOAD)

---

#### Eksempel: Produktions batch-taeller

```structured-text
VAR
  sensor_trigger : BOOL;
  reset_button   : BOOL;
  batch_complete : BOOL;
  product_count  : INT;
END_VAR

(* Tael 100 produkter per batch *)
CTU(CU := sensor_trigger, RESET := reset_button, PV := 100,
    Q => batch_complete, CV => product_count);

IF batch_complete THEN
  (* Trigger alarm, stop transportbaand, etc. *)
END_IF;
```

#### Eksempel: Parkerings-taeller med CTUD

```structured-text
VAR
  entry_sensor  : BOOL;
  exit_sensor   : BOOL;
  reset         : BOOL;
  load          : BOOL;
  lot_full      : BOOL;
  lot_empty     : BOOL;
  car_count     : INT;
END_VAR

(* Tael biler ind/ud af parkeringsplads *)
CTUD(CU := entry_sensor, CD := exit_sensor,
     RESET := reset, LOAD := load, PV := 50,
     QU => lot_full, QD => lot_empty, CV => car_count);

(* lot_full = TRUE naar 50 biler, lot_empty = TRUE naar 0 biler *)
(* car_count viser altid det aktuelle antal *)
```

#### Eksempel: Timer + Counter kombination

```structured-text
VAR
  sensor       : BOOL;
  debounced    : BOOL;
  done         : BOOL;
  count        : INT;
  elapsed      : TIME;
END_VAR

(* Debounce sensor-input med 50ms TON *)
debounced := TON(sensor, 50);

(* Tael stabile pulser op til 1000 *)
CTU(CU := debounced, RESET := FALSE, PV := 1000,
    Q => done, CV => count);
```

---

### Edge Detection Function Blocks (v4.7+)

```structured-text
(* R_TRIG: Rising edge detection — TRUE for one cycle on 0→1 *)
rising := R_TRIG(input_signal);

(* F_TRIG: Falling edge detection — TRUE for one cycle on 1→0 *)
falling := F_TRIG(input_signal);
```

---

### Latch Function Blocks (v4.7.3+)

```structured-text
(* SR: Set-dominant bistable — S1 has priority over R *)
output := SR(set_input, reset_input);

(* RS: Reset-dominant bistable — R1 has priority over S *)
output := RS(set_input, reset_input);
```

---

### Signal Processing Function Blocks (v4.8+)

```structured-text
(* SCALE: Linear scaling between ranges *)
scaled := SCALE(raw_input, in_min, in_max, out_min, out_max);

(* HYSTERESIS: Schmitt trigger with high/low thresholds *)
output := HYSTERESIS(sensor_value, high_threshold, low_threshold);

(* BLINK: Pulse generator with configurable on/off times *)
blink_out := BLINK(enable, on_time_ms, off_time_ms);

(* FILTER: First-order low-pass filter *)
filtered := FILTER(noisy_input, time_constant_ms);
```

---

### Oversigt: Alle Function Block Instanser

| Type | Function Blocks | Max instanser | Stateful |
|------|----------------|---------------|----------|
| Timer | TON, TOF, TP | 8 per program | Ja |
| Counter | CTU, CTD, CTUD | 8 per program | Ja |
| Edge | R_TRIG, F_TRIG | 8 per program | Ja |
| Latch | SR, RS | 8 per program | Ja |
| Signal | HYSTERESIS, BLINK, FILTER | 8 per program | Ja |

**Samlet stateful storage:** ~540 bytes per program (4 programmer = ~2.2 KB)

---

### Modbus Master Functions (v4.4+, async v7.7.0)

**Prerequisites:**
1. Enable Modbus Master: `set modbus-master enabled on`
2. Configure serial port: `set modbus-master baudrate 9600`
3. Set timeout: `set modbus-master timeout 500` (ms)

**Hardware:** Uses separate RS485 port on UART1 (TX:GPIO25, RX:GPIO26, DE:GPIO27)

#### Asynkron arkitektur (v7.7.0)

Fra v7.7.0 er alle Modbus Master funktioner **non-blocking**. En dedikeret FreeRTOS
baggrundstask (Core 0) håndterer UART kommunikation, mens ST Logic kører uhindret
på Core 1.

```
  ST Logic (Core 1)              Modbus Task (Core 0)
  ┌──────────────────┐           ┌──────────────────┐
  │ MB_READ_HOLDING() │──queue──>│ UART send/receive │
  │ → cached value    │          │ → opdatér cache   │
  │ (non-blocking)    │<─cache──│ (blocking OK her) │
  └──────────────────┘           └──────────────────┘
```

**Sådan virker det:**
- **Reads** returnerer den **cached værdi** og køer en refresh-request i baggrunden
- **Writes** køes i baggrunden og returnerer straks (optimistic update)
- Første kald til en ny adresse returnerer 0 (cache tom) — næste cyklus har den rigtige værdi
- Identiske read-requests deduplikeres automatisk (kun én queue-entry per adresse)

**Status Variables (globale — delt mellem alle program-slots):**
- `mb_last_error` (INT) - Error code fra sidste cache-opslag (0 = success)
- `mb_success` (BOOL) - TRUE hvis cached data er gyldig
- Tjek altid `MB_SUCCESS()` umiddelbart efter et MB_READ kald

**Error Codes:**
- 0 = MB_OK
- 1 = MB_TIMEOUT
- 2 = MB_CRC_ERROR
- 3 = MB_EXCEPTION
- 4 = MB_MAX_REQUESTS_EXCEEDED (queue fuld eller cache fuld)
- 5 = MB_NOT_ENABLED

---

#### Funktionsoversigt

| Funktion | FC | Args | Retur | Beskrivelse |
|----------|-----|------|-------|-------------|
| `MB_READ_COIL` | FC01 | 2 | BOOL | Læs enkelt coil |
| `MB_READ_INPUT` | FC02 | 2 | BOOL | Læs enkelt discrete input |
| `MB_READ_HOLDING` | FC03 | 2 | INT | Læs enkelt holding register |
| `MB_READ_INPUT_REG` | FC04 | 2 | INT | Læs enkelt input register |
| `MB_WRITE_COIL` | FC05 | 3 | BOOL | Skriv enkelt coil |
| `MB_WRITE_HOLDING` | FC06 | 3 | BOOL | Skriv enkelt holding register |
| `MB_READ_HOLDINGS` | FC03 | 3+arr | BOOL | Læs flere holding registre til ARRAY (v7.9.2) |
| `MB_WRITE_HOLDINGS` | FC16 | 3+arr | BOOL | Skriv flere holding registre fra ARRAY (v7.9.2) |
| `MB_SUCCESS` | — | 0 | BOOL | Sidste cache-opslag var gyldigt |
| `MB_BUSY` | — | 0 | BOOL | Async kø har ventende requests |
| `MB_ERROR` | — | 0 | INT | Sidste fejlkode (0=OK) |
| `MB_CACHE` | — | 1 | BOOL | Aktiver/deaktiver cache dedup |

---

#### Single-Register Read (FC01–FC04)

Alle read-funktioner returnerer **cached værdi** og køer en background refresh.

```structured-text
(* FC01: Læs coil fra remote slave *)
coil_value := MB_READ_COIL(slave_id, address);
(* Returns: BOOL — cached coil state *)

(* FC02: Læs discrete input fra remote slave *)
input_value := MB_READ_INPUT(slave_id, address);
(* Returns: BOOL — cached input state *)

(* FC03: Læs holding register fra remote slave *)
register_value := MB_READ_HOLDING(slave_id, address);
(* Returns: INT — cached register value *)

(* FC04: Læs input register fra remote slave *)
input_reg := MB_READ_INPUT_REG(slave_id, address);
(* Returns: INT — cached register value *)
```

#### Single-Register Write (FC05–FC06)

Writes køes i baggrunden og returnerer straks.
To syntaxer understøttes — begge er ækvivalente:

```structured-text
(* Funktions-syntax *)
result := MB_WRITE_COIL(slave_id, address, value);
result := MB_WRITE_HOLDING(slave_id, address, value);

(* Assignment-syntax (anbefalet — mere læsbar) *)
MB_WRITE_COIL(slave_id, address) := value;
MB_WRITE_HOLDING(slave_id, address) := value;
```

#### Multi-Register Read/Write med ARRAY (v7.9.2, FC03/FC16)

Læs/skriv op til 16 consecutive holding registre i én Modbus-transaktion.
Bruger native ARRAY OF INT — data kopieres direkte til/fra array-variable slots.

```structured-text
(* FC03 multi-read: Læs count registre ind i array *)
array_var := MB_READ_HOLDINGS(slave_id, start_address, count);

(* FC16 multi-write: Skriv count registre fra array *)
MB_WRITE_HOLDINGS(slave_id, start_address, count) := array_var;
```

**Krav:** `array_var` skal være deklareret som `ARRAY[0..N] OF INT` med mindst `count` elementer.

#### Status Functions (v7.7.0)

Tjek altid `MB_SUCCESS()` umiddelbart efter et MB_READ kald:

```structured-text
ok   := MB_SUCCESS();   (* TRUE hvis cached data er gyldig *)
busy := MB_BUSY();      (* TRUE hvis requests venter i kø *)
err  := MB_ERROR();     (* Fejlkode: 0=OK, 1=TIMEOUT, 2=CRC, 3=EXCEPTION *)
```

---

### Programmeringseksempler — Modbus Master

#### Eksempel 1: Læs coil og discrete input (FC01/FC02)

```structured-text
VAR
  remote_run    : BOOL;   (* Coil: motor kører *)
  door_closed   : BOOL;   (* Discrete input: dør-kontakt *)
  safe_to_run   : BOOL;
END_VAR

(* Læs motor-status coil fra slave 2, adresse 0 *)
remote_run := MB_READ_COIL(2, 0);

(* Læs dør-kontakt input fra slave 2, adresse 10 *)
door_closed := MB_READ_INPUT(2, 10);

(* Sikkerhedslogik: motor kun tilladt når dør er lukket *)
IF MB_SUCCESS() THEN
  safe_to_run := remote_run AND door_closed;
END_IF;
```

#### Eksempel 2: Skriv coil — fjernstyr relæ (FC05)

```structured-text
VAR
  temperature     : INT;
  heating_enabled : BOOL;
END_VAR

(* Temperatur-regulering *)
IF temperature < 18 THEN
  heating_enabled := TRUE;
ELSIF temperature > 22 THEN
  heating_enabled := FALSE;
END_IF;

(* Skriv til remote relæ-coil #20 på slave 3 *)
MB_WRITE_COIL(3, 20) := heating_enabled;

IF MB_ERROR() <> 0 THEN
  (* Kommunikationsfejl — log eller alarm *)
END_IF;
```

#### Eksempel 3: Læs/skriv enkelt holding register (FC03/FC06)

```structured-text
VAR
  remote_sensor : INT;
  local_output  : INT;
  setpoint      : INT;
  data_valid    : BOOL;
END_VAR

(* Læs sensor-værdi fra slave 5, holding register #100 *)
remote_sensor := MB_READ_HOLDING(5, 100);

(* Tjek om cached data er gyldig *)
IF MB_SUCCESS() THEN
  local_output := remote_sensor;
  data_valid := TRUE;
ELSE
  data_valid := FALSE;
END_IF;

(* Skriv setpoint til slave 5, holding register #200 *)
setpoint := 250;
MB_WRITE_HOLDING(5, 200) := setpoint;
```

#### Eksempel 4: Multi-device monitoring (FC03 enkelt)

```structured-text
VAR
  dev1_temp : INT;
  dev2_temp : INT;
  dev3_temp : INT;
  max_temp  : INT;
  alarm     : BOOL;
END_VAR

(* Læs temperatur fra 3 remote slaves *)
(* Alle returnerer straks med cached værdier *)
dev1_temp := MB_READ_HOLDING(1, 100);
dev2_temp := MB_READ_HOLDING(2, 100);
dev3_temp := MB_READ_HOLDING(3, 100);

(* Find max temperatur *)
max_temp := MAX(MAX(dev1_temp, dev2_temp), dev3_temp);

(* Alarm hvis over 50 grader *)
alarm := (max_temp > 50);
```

#### Eksempel 5: Multi-register read med ARRAY (FC03, v7.9.2)

```structured-text
VAR
  sensor_data : ARRAY[0..3] OF INT;   (* 4 consecutive registre *)
  temp        : INT;
  humidity    : INT;
  pressure    : INT;
  wind_speed  : INT;
END_VAR

(* Læs 4 registre fra slave 10, start-adresse 100 *)
(* Én FC03-pakke: mere effektivt end 4 separate reads *)
sensor_data := MB_READ_HOLDINGS(10, 100, 4);

IF MB_SUCCESS() THEN
  temp       := sensor_data[0];   (* Register 100 *)
  humidity   := sensor_data[1];   (* Register 101 *)
  pressure   := sensor_data[2];   (* Register 102 *)
  wind_speed := sensor_data[3];   (* Register 103 *)
END_IF;
```

#### Eksempel 6: Multi-register write med ARRAY (FC16, v7.9.2)

```structured-text
VAR
  setpoints : ARRAY[0..2] OF INT;   (* 3 setpoints *)
END_VAR

(* Forbered setpoint-værdier *)
setpoints[0] := 250;    (* Temperatur setpoint *)
setpoints[1] := 60;     (* Humidity setpoint *)
setpoints[2] := 1013;   (* Pressure setpoint *)

(* Skriv alle 3 til slave 10, start-adresse 200 *)
(* Én FC16-pakke i stedet for 3 separate FC06 *)
MB_WRITE_HOLDINGS(10, 200, 3) := setpoints;
```

#### Eksempel 7: Komplet SCADA gateway (read + write, single + multi)

```structured-text
VAR
  (* Input: læs fra remote enhed *)
  inputs   : ARRAY[0..7] OF INT;    (* 8 sensor-registre *)

  (* Output: skriv til remote enhed *)
  outputs  : ARRAY[0..3] OF INT;    (* 4 styring-registre *)

  (* Lokal logik *)
  avg_temp : INT;
  alarm    : BOOL;
  pump_on  : BOOL;
END_VAR

(* --- READS --- *)
(* Læs 8 sensor-registre fra slave 1, adresse 0-7 *)
inputs := MB_READ_HOLDINGS(1, 0, 8);

IF MB_SUCCESS() THEN
  (* Beregn gennemsnit af 4 temperatur-sensorer *)
  avg_temp := (inputs[0] + inputs[1] + inputs[2] + inputs[3]) / 4;

  (* Alarm hvis gennemsnit over 80 *)
  alarm := (avg_temp > 80);

  (* Pumpe-logik baseret på niveau-sensor *)
  pump_on := (inputs[4] < 200);   (* Niveau under 200 → pump ON *)
END_IF;

(* --- WRITES --- *)
outputs[0] := avg_temp;            (* Feedback: gennemsnitstemperatur *)
outputs[1] := BOOL_TO_INT(alarm);  (* Alarm-status *)
outputs[2] := BOOL_TO_INT(pump_on);(* Pumpe-kommando *)
outputs[3] := 1;                   (* Heartbeat *)

(* Skriv 4 styrings-registre til slave 1, adresse 100-103 *)
MB_WRITE_HOLDINGS(1, 100, 4) := outputs;
```

#### Eksempel 8: Vent på completion med MB_BUSY

```structured-text
VAR
  sensor_val : INT;
  ready      : BOOL;
END_VAR

(* Kø en read-request *)
sensor_val := MB_READ_HOLDING(1, 100);

(* Tjek om alle pending requests er behandlet *)
IF NOT MB_BUSY() THEN
  ready := TRUE;
  sensor_val := MB_READ_HOLDING(1, 100);  (* Nu med frisk cached værdi *)
END_IF;
```

#### Rate Limiting & Cache

**Request Limit:** Max 10 requests per ST program per cycle (configurable).
- Hver af de 4 program-slots får sin egen uafhængige quota
- Med default 10 kan 4 aktive programmer lave op til 40 Modbus requests per cycle
- Configure: `set modbus-master max-requests 20`
- If exceeded, function returns error MB_MAX_REQUESTS_EXCEEDED (code 4)

**Cache:**
- 32 entries max (unique slave+address+function combinations) — delt mellem alle slots
- Read deduplication: identical reads queued only once
- Cache auto-refresh: hvert kald til MB_READ_* køer automatisk en refresh
- Async queue: 16 entries — delt mellem alle slots
- Diagnostik: `show modbus-master` viser cache entries og statistik

**Multi-Program Arkitektur (v7.9.0.2+):**

Alle 4 ST program-slots kan bruge Modbus operationer samtidig:

```
Cycle N:
├─ Logic1: g_mb_request_count = 0 → op til 10 requests
├─ Logic2: g_mb_request_count = 0 → op til 10 requests
├─ Logic3: g_mb_request_count = 0 → op til 10 requests
└─ Logic4: g_mb_request_count = 0 → op til 10 requests
```

**Variabel-isolation:** Variabler er fuldt isoleret per program-slot. To programmer
kan have variabler med samme navn (f.eks. `counter`) uden konflikter — de kompileres
til separate indices i hvert programs eget `variables[32]` array.

**Delte ressourcer:**
- Cache (32 entries) og async queue (16 entries) er globale
- Hvis alle slots fylder cachen, returnerer nye cache-opslag fejl
- Prioritér at fordele Modbus-adresser så cache-bruget er effektivt

**Best Practice:**
- Eksisterende ST-programmer virker uændret (backward-kompatibel)
- Data er typisk 1 cyklus (10ms) gammelt — acceptabelt for PLC-applikationer
- Brug `MB_SUCCESS()` til at tjekke om cached data er gyldig
- Brug `MB_BUSY()` til at vente på at alle requests er behandlet
- Brug `MB_ERROR()` til fejldiagnostik
- Samme variabel-navne i forskellige slots er OK — ingen konflikter

---

### Bit Rotation Functions (v4.8.4+)

Rotate bits left or right within INT (16-bit), DINT (32-bit), or DWORD (32-bit) values.

**Note:** Return type matches input type (type-polymorphic).

```structured-text
(* ROL: Rotate bits left by N positions *)
result := ROL(value, N);

(* ROR: Rotate bits right by N positions *)
result := ROR(value, N);

(* Example 1: 16-bit rotation (INT) *)
VAR
  value: INT := 16#1234;     (* 0001 0010 0011 0100 binary *)
  rotated_left: INT;
  rotated_right: INT;
END_VAR

rotated_left := ROL(value, 4);   (* → 0x2341 = 0010 0011 0100 0001 *)
rotated_right := ROR(value, 4);  (* → 0x4123 = 0100 0001 0010 0011 *)

(* Example 2: 32-bit rotation (DINT/DWORD) *)
VAR
  flags: DWORD := 16#12345678;
  shifted: DWORD;
END_VAR

shifted := ROL(flags, 8);   (* → 0x34567812 *)
shifted := ROR(flags, 8);   (* → 0x78123456 *)

(* Example 3: Circular buffer pointer with ROL *)
VAR
  buffer_ptr: INT := 1;      (* Pointer bit position *)
  step: INT;
END_VAR

FOR step := 1 TO 8 DO
  buffer_ptr := ROL(buffer_ptr, 1);  (* Rotate bit left *)
  (* buffer_ptr cycles: 0x0001 → 0x0002 → 0x0004 → ... → 0x8000 → 0x0001 *)
END_FOR;

(* Example 4: Shift register simulation *)
VAR
  shift_reg: INT := 0;
  input_bit: BOOL;
  output_bit: BOOL;
END_VAR

(* Shift left and insert new bit *)
IF input_bit THEN
  shift_reg := ROL(shift_reg, 1) OR 1;  (* Insert 1 at LSB *)
ELSE
  shift_reg := ROL(shift_reg, 1) AND 16#FFFE;  (* Insert 0 at LSB *)
END_IF;

(* Extract MSB before rotation *)
output_bit := (shift_reg AND 16#8000) <> 0;

(* Negative shift values are normalized *)
VAR
  val: INT := 16#ABCD;
END_VAR

result := ROL(val, -4);  (* Same as ROL(val, 12) for 16-bit *)
result := ROR(val, -4);  (* Same as ROR(val, 12) for 16-bit *)
```

**Supported Types:**
- INT (16-bit): Rotation within 0-15 bit positions
- DINT (32-bit signed): Rotation within 0-31 bit positions
- DWORD (32-bit unsigned): Rotation within 0-31 bit positions

**Behavior:**
- Negative N values are normalized (N = N % bit_width + bit_width)
- N values > bit_width are wrapped (N = N % bit_width)
- No bits are lost - they wrap around from one end to the other

---

## Execution Flow (Unified Mapping Model)

```
┌──────────────────────────────────────────────────────────┐
│ Modbus Server Main Loop (every 10ms)                     │
├──────────────────────────────────────────────────────────┤
│                                                          │
│  PHASE 1: Sync INPUTs                                   │
│  ┌─────────────────────────────────────┐                │
│  │ gpio_mapping_update()                │                │
│  │  - Read GPIO pins → Discrete inputs  │                │
│  │  - Read HR# → ST variables (input)   │                │
│  └─────────────────────────────────────┘                │
│                                                          │
│  PHASE 2: Execute Programs                              │
│  ┌─────────────────────────────────────┐                │
│  │ st_logic_engine_loop()                │                │
│  │  - For each enabled Logic program:    │                │
│  │    * Reset Modbus request counter     │                │
│  │    * Execute compiled bytecode        │                │
│  │    * Each slot gets own MB quota      │                │
│  │    * Programs run independently       │                │
│  │    * Non-blocking (< 1ms)             │                │
│  └─────────────────────────────────────┘                │
│                                                          │
│  PHASE 3: Sync OUTPUTs                                  │
│  ┌─────────────────────────────────────┐                │
│  │ gpio_mapping_update()                │                │
│  │  - Read Coils → GPIO pins            │                │
│  │  - Read ST variables (output) → HR# │                │
│  └─────────────────────────────────────┘                │
│                                                          │
│  Continue Modbus RTU service...                          │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

**Key Points:**
- Each program executes **non-blocking** and **independently**
- GPIO mapping and ST variables use the same **unified VariableMapping** engine
- **Both** read inputs before program execution
- **Both** write outputs after program execution
- ST variables and GPIO pins are treated identically
- Programs don't interfere with each other or the Modbus RTU service
- Variabler med samme navn i forskellige slots er **fuldt isoleret** (separate arrays)
- Hvert program-slot får sin **egen Modbus request quota** (default 10 per slot)

---

## Troubleshooting

### Program Won't Compile

**Error:** `Compile error: Parse error...`

**Solution:** Check ST syntax. Common issues:
- Missing `END_VAR` after variable declarations
- Missing `;` at end of statements
- Misspelled keywords (keywords are case-insensitive)

**Example (WRONG):**
```
VAR x: INT END_VAR IF x > 10 THEN x := 1 END_IF
                  ↑ Missing semicolon
```

**Example (CORRECT):**
```
VAR x: INT; END_VAR IF x > 10 THEN x := 1; END_IF;
```

---

### Program Compiles But No Output

**Cause:** Program not enabled or bindings not configured

**Solution:**
```bash
show logic 1   # Check if compiled and enabled
set logic 1 enabled:true
set logic 1 bind myvar reg:100   # Configure bindings first! (replace myvar with actual variable name)
```

---

### Variables Not Updating in Modbus

**Cause:** Output variables not bound to registers

**Solution:**
```bash
set logic 1 bind myoutput reg:101   # Bind output variable (replace myoutput with actual variable name)
```

Then check Modbus register #101 after executing the program.

---

### Program Runs But Wrong Results

**Cause:** Logic error or type mismatch

**Solution:**
1. Verify variable types (INT, BOOL, REAL, DWORD)
2. Check Modbus register ranges (0-159)
3. Verify operator precedence (use parentheses when unsure)
4. Test with simpler program first

---

## Performance Notes

- **Execution Time:** ~1-5ms per program (depends on complexity)
- **Memory:** ~50KB for 4 programs with source + bytecode
- **Cycle Time:** Programs run every ~10ms (non-blocking)
- **Register Limit:** 160 holding registers (0-159) for all I/O

---

## Security & Safety

⚠️ **Important:**

- Logic programs run in **sandbox** (limited to bytecode execution)
- No arbitrary code execution or memory access
- Programs can only read/write assigned registers
- Runtime errors stop execution of that program (others continue)
- Maximum execution steps: 10,000 (prevents infinite loops)

---

## Next Steps

1. Upload first logic program: `set logic 1 upload "..."`
2. Bind variables to registers: `set logic 1 bind ...`
3. Enable program: `set logic 1 enabled:true`
4. Use Modbus master to read/write registers
5. Monitor with: `show logic stats`

**Happy Programming!** 🚀

---

## Version Information

- **Feature:** Structured Text Logic Mode
- **IEC Standard:** 61131-3 (ST-Light Profile)
- **First Release:** v2.0.0 (2025-11-30)
- **Current Version:** v7.9.3.2 (2026-04-08)
- **Status:** Production Ready ✅

### Feature History

- **v2.0.0** - Initial ST Logic implementation
- **v4.3.0** - Added REAL arithmetic support (SIN, COS, TAN)
- **v4.4.0** - Added Modbus Master functions (MB_READ_*, MB_WRITE_*)
- **v4.4.0** - Added LIMIT and SEL functions
- **v7.7.0** - Async Modbus Master: non-blocking reads/writes, MB_SUCCESS/MB_BUSY/MB_ERROR builtins
- **v7.9.0.2** - Per-slot Modbus request quota: alle 4 slots kan køre Modbus uafhængigt
- **v7.9.2.0** - Multi-register Modbus: MB_READ_HOLDINGS/MB_WRITE_HOLDINGS med native ARRAY support (FC03/FC16)
- **v7.9.3.0** - Compiler heap optimering: AST node 156->84 bytes, dynamisk bytecode buffer, max 2048 instruktioner
- **v7.9.3.1** - Realtime compiler ressource-info i web editor (heap-bar, AST nodes, auto-poll)
- **v7.9.3.2** - Modbus Master: FC16 timeout fix, request struct 41→10 bytes, LRU cache eviction, write dedup, adaptive per-slave backoff (50ms→2s), stats reset med tidspunkt (API + web)
