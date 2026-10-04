# ST Logic Test - Kombinerede Tests (Fase 2)

**Version:** v6.0.0
**Formål:** Test af komplekse scenarier der kombinerer multiple features

---

## Test Oversigt

| Test | Kombinerer | Prioritet |
|------|------------|-----------|
| 2.1 | Aritmetik + Logik + Sammenligning | HIGH |
| 2.2 | Nested IF + Multiple Conditions | HIGH |
| 2.3 | Loops + Arrays | MEDIUM |
| 2.4 | CASE + Builtin Functions | MEDIUM |
| 2.5 | FOR Loop + Conditional | HIGH |
| 2.6 | WHILE + Math Functions | MEDIUM |
| 2.7 | Trigonometry + REAL | LOW |
| 2.8 | Type Conversion Chain | MEDIUM |
| 2.9 | Boolean Logic + SEL | MEDIUM |
| 2.10 | Complete System Test | HIGH |
| **Total** | **10** | |

---

# Test 2.1: Aritmetik + Logik + Sammenligning

**Beskrivelse:** Kombinerer aritmetiske, logiske og sammenlignings operatorer.

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  a: INT;
  b: INT;
  threshold: INT := 100;
  result: INT;
  in_range: BOOL;
END_VAR
BEGIN
  result := (a + b) * 2;
  in_range := (result > 0) AND (result < threshold);
END_PROGRAM
END_UPLOAD

set logic 1 bind a reg:20 input
set logic 1 bind b reg:21 input
set logic 1 bind result reg:22 output
set logic 1 bind in_range coil:0 output
set logic 1 enabled:true
```

| a | b | result | in_range | Status |
|---|---|--------|----------|--------|
| 10 | 15 | 50 | TRUE | ⬜ |
| 25 | 30 | 110 | FALSE | ⬜ |
| -5 | 5 | 0 | FALSE | ⬜ |

---

# Test 2.2: Nested IF + Multiple Conditions

**Beskrivelse:** Kompleks nested IF struktur.

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  temp: INT;
  humidity: INT;
  mode: INT;  (* 0=off, 1=heat, 2=cool, 3=dehumidify *)
END_VAR
BEGIN
  IF temp < 18 THEN
    mode := 1;  (* Heat *)
  ELSIF temp > 26 THEN
    IF humidity > 70 THEN
      mode := 3;  (* Dehumidify first *)
    ELSE
      mode := 2;  (* Cool *)
    END_IF;
  ELSE
    IF humidity > 80 THEN
      mode := 3;  (* Dehumidify *)
    ELSE
      mode := 0;  (* Off *)
    END_IF;
  END_IF;
END_PROGRAM
END_UPLOAD

set logic 1 bind temp reg:20 input
set logic 1 bind humidity reg:21 input
set logic 1 bind mode reg:22 output
set logic 1 enabled:true
```

| temp | humidity | mode | Status |
|------|----------|------|--------|
| 15 | 50 | 1 (heat) | ⬜ |
| 28 | 60 | 2 (cool) | ⬜ |
| 28 | 75 | 3 (dehumidify) | ⬜ |
| 22 | 85 | 3 (dehumidify) | ⬜ |
| 22 | 50 | 0 (off) | ⬜ |

---

# Test 2.3: Loops + Arrays (Simulation)

**Beskrivelse:** FOR loop med array-lignende beregning.

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  i: INT;
  sum: INT;
  count: INT;
  average: INT;
  v0: INT;
  v1: INT;
  v2: INT;
  v3: INT;
END_VAR
BEGIN
  (* Simuleret array summering *)
  sum := v0 + v1 + v2 + v3;
  count := 4;
  average := sum / count;
END_PROGRAM
END_UPLOAD

set logic 1 bind v0 reg:20 input
set logic 1 bind v1 reg:21 input
set logic 1 bind v2 reg:22 input
set logic 1 bind v3 reg:23 input
set logic 1 bind average reg:24 output
set logic 1 enabled:true
```

| v0 | v1 | v2 | v3 | average | Status |
|----|----|----|-----|---------|--------|
| 10 | 20 | 30 | 40 | 25 | ⬜ |
| 0 | 0 | 0 | 0 | 0 | ⬜ |
| 100 | 100 | 100 | 100 | 100 | ⬜ |

---

# Test 2.4: CASE + Builtin Functions

**Beskrivelse:** CASE statement med builtin funktioner.

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  operation: INT;
  value: INT;
  result: INT;
END_VAR
BEGIN
  CASE operation OF
    0: result := ABS(value);
    1: result := value * 2;
    2: result := value / 2;
    3: result := MIN(value, 100);
    4: result := MAX(value, 0);
  ELSE
    result := 0;
  END_CASE;
END_PROGRAM
END_UPLOAD

set logic 1 bind operation reg:20 input
set logic 1 bind value reg:21 input
set logic 1 bind result reg:22 output
set logic 1 enabled:true
```

| operation | value | result | Status |
|-----------|-------|--------|--------|
| 0 | -50 | 50 (ABS) | ⬜ |
| 1 | 25 | 50 (*2) | ⬜ |
| 2 | 100 | 50 (/2) | ⬜ |
| 3 | 150 | 100 (MIN) | ⬜ |
| 4 | -10 | 0 (MAX) | ⬜ |

---

# Test 2.5: FOR Loop + Conditional

**Beskrivelse:** FOR loop med betinget logik inde i loop.

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  i: INT;
  n: INT;
  even_sum: INT;
  odd_sum: INT;
END_VAR
BEGIN
  even_sum := 0;
  odd_sum := 0;
  FOR i := 1 TO n DO
    IF (i MOD 2) = 0 THEN
      even_sum := even_sum + i;
    ELSE
      odd_sum := odd_sum + i;
    END_IF;
  END_FOR;
END_PROGRAM
END_UPLOAD

set logic 1 bind n reg:20 input
set logic 1 bind even_sum reg:21 output
set logic 1 bind odd_sum reg:22 output
set logic 1 enabled:true
```

| n | even_sum | odd_sum | Status |
|---|----------|---------|--------|
| 10 | 30 (2+4+6+8+10) | 25 (1+3+5+7+9) | ⬜ |
| 5 | 6 (2+4) | 9 (1+3+5) | ⬜ |

---

# Test 2.6: WHILE + Math Functions

**Beskrivelse:** WHILE loop med matematiske funktioner.

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  value: INT;
  iterations: INT;
END_VAR
BEGIN
  iterations := 0;
  WHILE value > 1 DO
    value := value / 2;
    iterations := iterations + 1;
  END_WHILE;
END_PROGRAM
END_UPLOAD

set logic 1 bind value reg:20 input
set logic 1 bind iterations reg:21 output
set logic 1 enabled:true
```

| Initial value | iterations | Status |
|---------------|------------|--------|
| 16 | 4 | ⬜ |
| 100 | 6 | ⬜ |
| 1 | 0 | ⬜ |

---

# Test 2.7: Trigonometry + REAL

**Beskrivelse:** Trigonometriske funktioner med REAL typer.

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  angle: REAL;
  sin_val: REAL;
  cos_val: REAL;
  magnitude: REAL;
END_VAR
BEGIN
  sin_val := SIN(angle);
  cos_val := COS(angle);
  magnitude := SQRT(sin_val * sin_val + cos_val * cos_val);
  (* magnitude should always be ~1.0 *)
END_PROGRAM
END_UPLOAD

set logic 1 bind angle reg:20 input
set logic 1 bind magnitude reg:24 output
set logic 1 enabled:true
```

| angle (rad) | magnitude | Status |
|-------------|-----------|--------|
| 0.0 | 1.0 | ⬜ |
| 1.57 (π/2) | 1.0 | ⬜ |
| 3.14 (π) | 1.0 | ⬜ |

---

# Test 2.8: Type Conversion Chain

**Beskrivelse:** Kæde af type konverteringer.

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  input_int: INT;
  as_real: REAL;
  doubled: REAL;
  back_to_int: INT;
END_VAR
BEGIN
  as_real := INT_TO_REAL(input_int);
  doubled := as_real * 2.5;
  back_to_int := REAL_TO_INT(doubled);
END_PROGRAM
END_UPLOAD

set logic 1 bind input_int reg:20 input
set logic 1 bind back_to_int reg:21 output
set logic 1 enabled:true
```

| input_int | back_to_int | Status |
|-----------|-------------|--------|
| 10 | 25 (10*2.5) | ⬜ |
| 4 | 10 (4*2.5) | ⬜ |

---

# Test 2.9: Boolean Logic + SEL

**Beskrivelse:** Kompleks boolean logik med SEL funktion.

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  condition1: BOOL;
  condition2: BOOL;
  val_if_true: INT := 100;
  val_if_false: INT := 0;
  result: INT;
  combined: BOOL;
END_VAR
BEGIN
  combined := condition1 AND condition2;
  result := SEL(combined, val_if_false, val_if_true);
END_PROGRAM
END_UPLOAD

set logic 1 bind condition1 coil:0 input
set logic 1 bind condition2 coil:1 input
set logic 1 bind result reg:20 output
set logic 1 enabled:true
```

| condition1 | condition2 | result | Status |
|------------|------------|--------|--------|
| TRUE | TRUE | 100 | ⬜ |
| TRUE | FALSE | 0 | ⬜ |
| FALSE | TRUE | 0 | ⬜ |
| FALSE | FALSE | 0 | ⬜ |

---

# Test 2.10: Complete System Test

**Beskrivelse:** Komplet system test der simulerer en temperaturregulator.

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM temp_controller
VAR
  setpoint: INT;
  actual: INT;
  error: INT;
  output: INT;
  heater_on: BOOL;
  cooler_on: BOOL;
  deadband: INT := 2;
END_VAR
BEGIN
  error := setpoint - actual;

  IF error > deadband THEN
    heater_on := TRUE;
    cooler_on := FALSE;
    output := MIN(error * 10, 100);
  ELSIF error < -deadband THEN
    heater_on := FALSE;
    cooler_on := TRUE;
    output := MIN(ABS(error) * 10, 100);
  ELSE
    heater_on := FALSE;
    cooler_on := FALSE;
    output := 0;
  END_IF;
END_PROGRAM
END_UPLOAD

set logic 1 bind setpoint reg:70 input
set logic 1 bind actual reg:71 input
set logic 1 bind output reg:72 output
set logic 1 bind heater_on coil:10 output
set logic 1 bind cooler_on coil:11 output
set logic 1 enabled:true
```

| setpoint | actual | heater | cooler | output | Status |
|----------|--------|--------|--------|--------|--------|
| 22 | 20 | OFF | OFF | 0 | ⬜ |
| 22 | 15 | ON | OFF | 70 | ⬜ |
| 22 | 30 | OFF | ON | 80 | ⬜ |
| 22 | 22 | OFF | OFF | 0 | ⬜ |

---

## Test Results Summary

| Test | Beskrivelse | Status |
|------|-------------|--------|
| 2.1 | Aritmetik + Logik | ⬜ |
| 2.2 | Nested IF | ⬜ |
| 2.3 | Loops + Arrays | ⬜ |
| 2.4 | CASE + Builtins | ⬜ |
| 2.5 | FOR + Conditional | ⬜ |
| 2.6 | WHILE + Math | ⬜ |
| 2.7 | Trig + REAL | ⬜ |
| 2.8 | Type Conversion | ⬜ |
| 2.9 | Boolean + SEL | ⬜ |
| 2.10 | Complete System | ⬜ |

---

**Tester:** _______________
**Dato:** _______________
