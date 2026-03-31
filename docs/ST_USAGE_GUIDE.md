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

### Counter Functions (v4.8.1+)

IEC 61131-3 standard counter function blocks for counting events.

**Note:** All counters maintain internal state and require stateful storage.

---

#### CTU - Count Up Counter

Increments counter on rising edge of CU input. Output Q goes TRUE when count reaches preset value PV.

```structured-text
VAR
  pulse: BOOL;
  reset: BOOL;
  batch_done: BOOL;
END_VAR

(* Count to 100 - batch complete *)
batch_done := CTU(pulse, reset, 100);

(* When batch_done = TRUE, counter reached 100 *)
```

**Parameters:**
- `CU` (BOOL) - Count-up input (rising edge triggers increment)
- `RESET` (BOOL) - Reset input (TRUE resets CV to 0)
- `PV` (INT) - Preset value (count limit)

**Returns:** Q (BOOL) - TRUE when CV >= PV

---

#### CTD - Count Down Counter

Decrements counter on rising edge of CD input. Output Q goes TRUE when count reaches zero.

```structured-text
VAR
  pulse: BOOL;
  load: BOOL;
  empty: BOOL;
END_VAR

(* Count down from 50 *)
empty := CTD(pulse, load, 50);

(* When empty = TRUE, counter reached 0 *)
```

**Parameters:**
- `CD` (BOOL) - Count-down input (rising edge triggers decrement)
- `LOAD` (BOOL) - Load input (TRUE loads PV into CV)
- `PV` (INT) - Preset value (starting count)

**Returns:** Q (BOOL) - TRUE when CV <= 0

---

#### CTUD - Count Up/Down Counter

Bidirectional counter - increments on CU, decrements on CD. Two outputs: QU (reached upper limit) and QD (reached zero).

```structured-text
VAR
  inc_pulse: BOOL;
  dec_pulse: BOOL;
  reset: BOOL;
  load: BOOL;
  up_done: BOOL;
  down_done: BOOL;
END_VAR

(* Count up/down with dual limits *)
up_done := CTUD(inc_pulse, dec_pulse, reset, load, 100);

(* up_done = TRUE when CV >= 100 (QU output) *)
(* Access QD via instance storage for down limit *)
```

**Parameters:**
- `CU` (BOOL) - Count-up input (rising edge increments)
- `CD` (BOOL) - Count-down input (rising edge decrements)
- `RESET` (BOOL) - Reset input (TRUE resets CV to 0)
- `LOAD` (BOOL) - Load input (TRUE loads PV into CV)
- `PV` (INT) - Preset value (upper limit)

**Returns:** QU (BOOL) - TRUE when CV >= PV

**Note:** QD (down limit) is stored in instance but not directly returned. Use CTU/CTD for single-direction counting if only one limit is needed.

**Priority (highest to lowest):**
1. RESET (always wins)
2. LOAD (loads PV if RESET is FALSE)
3. CU/CD (count if neither RESET nor LOAD is TRUE)

---

#### Example: Product Batch Counter

```structured-text
VAR
  sensor_trigger: BOOL;
  reset_button: BOOL;
  batch_complete: BOOL;
  product_count: INT;
END_VAR

(* Count 100 products per batch *)
batch_complete := CTU(sensor_trigger, reset_button, 100);

IF batch_complete THEN
  (* Trigger alarm, stop conveyor, etc. *)
  product_count := 100;
END_IF;
```

---

#### Example: Parking Space Counter

```structured-text
VAR
  entry_sensor: BOOL;
  exit_sensor: BOOL;
  reset: BOOL;
  load: BOOL;
  spaces_full: BOOL;
  spaces_empty: BOOL;
  max_spaces: INT := 50;
END_VAR

(* Count cars entering/leaving parking lot *)
spaces_full := CTUD(entry_sensor, exit_sensor, reset, load, max_spaces);

(* spaces_full = TRUE when lot is full (50 cars) *)
(* spaces_empty would be TRUE when lot is empty (CV = 0) *)
```

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

**Global Status Variables:**
- `mb_last_error` (INT) - Error code fra sidste cache-opslag (0 = success)
- `mb_success` (BOOL) - TRUE hvis cached data er gyldig

**Error Codes:**
- 0 = MB_OK
- 1 = MB_TIMEOUT
- 2 = MB_CRC_ERROR
- 3 = MB_EXCEPTION
- 4 = MB_MAX_REQUESTS_EXCEEDED (queue fuld eller cache fuld)
- 5 = MB_NOT_ENABLED

---

#### Reading Functions (2 arguments, async cached)

```structured-text
(* MB_READ_COIL: Read single coil from remote device *)
coil_value := MB_READ_COIL(slave_id, address);
(* Returns: BOOL — cached coil state, queues refresh *)

(* MB_READ_INPUT: Read single discrete input from remote device *)
input_value := MB_READ_INPUT(slave_id, address);
(* Returns: BOOL — cached input state, queues refresh *)

(* MB_READ_HOLDING: Read single holding register from remote device *)
register_value := MB_READ_HOLDING(slave_id, address);
(* Returns: INT — cached register value, queues refresh *)

(* MB_READ_INPUT_REG: Read single input register from remote device *)
input_reg := MB_READ_INPUT_REG(slave_id, address);
(* Returns: INT — cached register value, queues refresh *)
```

#### Writing Functions (3 arguments, async queued)

```structured-text
(* MB_WRITE_COIL: Write single coil to remote device *)
result := MB_WRITE_COIL(slave_id, address, value);
(* Returns: BOOL — TRUE if queued successfully *)

(* MB_WRITE_HOLDING: Write single holding register to remote device *)
result := MB_WRITE_HOLDING(slave_id, address, value);
(* Returns: BOOL — TRUE if queued successfully *)
```

#### Status Functions (0 arguments, v7.7.0)

```structured-text
(* MB_SUCCESS: Check if last cached read had valid data *)
ok := MB_SUCCESS();
(* Returns: BOOL — TRUE if cache entry was VALID *)

(* MB_BUSY: Check if async queue has pending requests *)
busy := MB_BUSY();
(* Returns: BOOL — TRUE if requests are still in queue *)

(* MB_ERROR: Get last Modbus error code *)
err := MB_ERROR();
(* Returns: INT — error code (0=OK, 1=TIMEOUT, etc.) *)
```

---

#### Example: Read Remote Sensor (async)

```structured-text
VAR
  remote_sensor: INT;
  local_output: INT;
  data_valid: BOOL;
END_VAR

(* Read holding register #100 from slave ID 5 *)
(* Returnerer cached værdi — baggrundstask refresher automatisk *)
remote_sensor := MB_READ_HOLDING(5, 100);

(* Tjek om cached data er gyldig *)
IF MB_SUCCESS() THEN
  local_output := remote_sensor;
  data_valid := TRUE;
ELSE
  (* Første kald eller kommunikationsfejl — brug sidst kendte værdi *)
  data_valid := FALSE;
END_IF;
```

#### Example: Control Remote Relay (async)

```structured-text
VAR
  temperature: INT;
  heating_enabled: BOOL;
  write_queued: BOOL;
END_VAR

(* Read local temperature from HR#10 *)
temperature := 20;  (* Assume bound to reg:10 *)

(* Control remote relay based on temperature *)
IF temperature < 18 THEN
  heating_enabled := TRUE;
ELSE
  heating_enabled := FALSE;
END_IF;

(* Write to remote coil #20 on slave ID 3 *)
(* Returnerer straks — skrivningen sker i baggrunden *)
write_queued := MB_WRITE_COIL(3, 20, heating_enabled);

IF NOT write_queued THEN
  (* Queue fuld — prøv igen næste cyklus *)
END_IF;
```

#### Example: Multi-Device Monitoring (async)

```structured-text
VAR
  device1_temp: INT;
  device2_temp: INT;
  device3_temp: INT;
  max_temp: INT;
  alarm: BOOL;
END_VAR

(* Read temperature from 3 remote devices *)
(* Alle 3 returnerer straks med cached værdier *)
(* Baggrundstask refresher dem asynkront *)
device1_temp := MB_READ_HOLDING(1, 100);  (* Slave 1, HR#100 *)
device2_temp := MB_READ_HOLDING(2, 100);  (* Slave 2, HR#100 *)
device3_temp := MB_READ_HOLDING(3, 100);  (* Slave 3, HR#100 *)

(* Find maximum temperature *)
max_temp := MAX(device1_temp, device2_temp);
max_temp := MAX(max_temp, device3_temp);

(* Trigger alarm if any device exceeds 50 *)
IF max_temp > 50 THEN
  alarm := TRUE;
ELSE
  alarm := FALSE;
END_IF;
```

#### Example: Wait for Completion (async)

```structured-text
VAR
  sensor_val: INT;
  ready: BOOL;
END_VAR

(* Kø en read-request *)
sensor_val := MB_READ_HOLDING(1, 100);

(* Vent til alle pending requests er behandlet *)
IF NOT MB_BUSY() THEN
  (* Alle requests er færdige — data er opdateret *)
  ready := TRUE;
  sensor_val := MB_READ_HOLDING(1, 100);  (* Nu med frisk cached værdi *)
END_IF;
```

#### Rate Limiting & Cache

**Request Limit:** Max 10 requests per ST execution cycle (configurable).
- Configure: `set modbus-master max-requests 20`
- If exceeded, function returns error MB_MAX_REQUESTS_EXCEEDED (code 4)

**Cache:**
- 32 entries max (unique slave+address+function combinations)
- Read deduplication: identical reads queued only once
- Cache auto-refresh: hvert kald til MB_READ_* køer automatisk en refresh
- Diagnostik: `show modbus-master` viser cache entries og statistik

**Best Practice:**
- Eksisterende ST-programmer virker uændret (backward-kompatibel)
- Data er typisk 1 cyklus (10ms) gammelt — acceptabelt for PLC-applikationer
- Brug `MB_SUCCESS()` til at tjekke om cached data er gyldig
- Brug `MB_BUSY()` til at vente på at alle requests er behandlet
- Brug `MB_ERROR()` til fejldiagnostik

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
│  │    * Execute compiled bytecode        │                │
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
- **Current Version:** v4.4.0 (2025-12-24)
- **Status:** Production Ready ✅

### Feature History

- **v2.0.0** - Initial ST Logic implementation
- **v4.3.0** - Added REAL arithmetic support (SIN, COS, TAN)
- **v4.4.0** - Added Modbus Master functions (MB_READ_*, MB_WRITE_*)
- **v4.4.0** - Added LIMIT and SEL functions
- **v7.7.0** - Async Modbus Master: non-blocking reads/writes, MB_SUCCESS/MB_BUSY/MB_ERROR builtins
