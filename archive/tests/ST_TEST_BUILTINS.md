# ST Logic Test - Builtin Funktioner

**Version:** v6.0.0
**Formål:** Test af ST builtin funktioner

---

## Indholdsfortegnelse

1. [Matematiske Funktioner](#1-matematiske-funktioner)
2. [Clamping/Selection Funktioner](#2-clampingselection-funktioner)
3. [Trigonometriske Funktioner](#3-trigonometriske-funktioner)
4. [Type Conversion Funktioner](#4-type-conversion-funktioner)
5. [Persistence Funktioner](#5-persistence-funktioner)

---

## Test Oversigt

| Kategori | Tests | Prioritet |
|----------|-------|-----------|
| Matematiske | 9 | HIGH |
| Clamping/Selection | 3 | MEDIUM |
| Trigonometriske | 3 | MEDIUM |
| Type Conversion | 6 | HIGH |
| Persistence | 2 | HIGH |
| **Total** | **23** | |

---

# 1. Matematiske Funktioner

## Test 1.1: ABS (Absolut Værdi)

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  a: INT;
  result: INT;
END_VAR
BEGIN
  result := ABS(a);
END_PROGRAM
END_UPLOAD

set logic 1 bind a reg:20 input
set logic 1 bind result reg:21 output
set logic 1 enabled:true
```

| Input | Forventet | Status |
|-------|-----------|--------|
| -5 | 5 | ⬜ |
| 5 | 5 | ⬜ |
| 0 | 0 | ⬜ |

---

## Test 1.2: SQRT (Kvadratrod)

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  a: REAL;
  result: REAL;
END_VAR
BEGIN
  result := SQRT(a);
END_PROGRAM
END_UPLOAD

set logic 1 bind a reg:20 input
set logic 1 bind result reg:22 output
set logic 1 enabled:true
```

| Input | Forventet | Status |
|-------|-----------|--------|
| 16.0 | 4.0 | ⬜ |
| 25.0 | 5.0 | ⬜ |
| 2.0 | 1.414... | ⬜ |

---

## Test 1.3: POW (Potens)

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  base: REAL;
  exp: REAL;
  result: REAL;
END_VAR
BEGIN
  result := POW(base, exp);
END_PROGRAM
END_UPLOAD

set logic 1 bind base reg:20 input
set logic 1 bind exp reg:22 input
set logic 1 bind result reg:24 output
set logic 1 enabled:true
```

| Base | Exp | Forventet | Status |
|------|-----|-----------|--------|
| 2.0 | 3.0 | 8.0 | ⬜ |
| 10.0 | 2.0 | 100.0 | ⬜ |
| 5.0 | 0.0 | 1.0 | ⬜ |

---

## Test 1.4: LOG (Naturlig Logaritme)

| Input | Forventet | Status |
|-------|-----------|--------|
| 2.718 | ~1.0 | ⬜ |
| 1.0 | 0.0 | ⬜ |

---

## Test 1.5: LOG10 (Base-10 Logaritme)

| Input | Forventet | Status |
|-------|-----------|--------|
| 100.0 | 2.0 | ⬜ |
| 10.0 | 1.0 | ⬜ |

---

## Test 1.6: EXP (e^x)

| Input | Forventet | Status |
|-------|-----------|--------|
| 0.0 | 1.0 | ⬜ |
| 1.0 | 2.718... | ⬜ |

---

## Test 1.7: FLOOR (Rund ned)

| Input | Forventet | Status |
|-------|-----------|--------|
| 3.7 | 3.0 | ⬜ |
| -3.7 | -4.0 | ⬜ |

---

## Test 1.8: CEIL (Rund op)

| Input | Forventet | Status |
|-------|-----------|--------|
| 3.2 | 4.0 | ⬜ |
| -3.2 | -3.0 | ⬜ |

---

## Test 1.9: ROUND (Afrund)

| Input | Forventet | Status |
|-------|-----------|--------|
| 3.4 | 3.0 | ⬜ |
| 3.5 | 4.0 | ⬜ |
| 3.6 | 4.0 | ⬜ |

---

# 2. Clamping/Selection Funktioner

## Test 2.1: MIN

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  a: INT;
  b: INT;
  result: INT;
END_VAR
BEGIN
  result := MIN(a, b);
END_PROGRAM
END_UPLOAD

set logic 1 bind a reg:20 input
set logic 1 bind b reg:21 input
set logic 1 bind result reg:22 output
set logic 1 enabled:true
```

| a | b | Forventet | Status |
|---|---|-----------|--------|
| 5 | 3 | 3 | ⬜ |
| -5 | 3 | -5 | ⬜ |
| 0 | 0 | 0 | ⬜ |

---

## Test 2.2: MAX

| a | b | Forventet | Status |
|---|---|-----------|--------|
| 5 | 3 | 5 | ⬜ |
| -5 | 3 | 3 | ⬜ |
| 0 | 0 | 0 | ⬜ |

---

## Test 2.3: LIMIT (Clamp)

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  min_val: INT := 0;
  max_val: INT := 100;
  input: INT;
  result: INT;
END_VAR
BEGIN
  result := LIMIT(min_val, input, max_val);
END_PROGRAM
END_UPLOAD

set logic 1 bind input reg:20 input
set logic 1 bind result reg:21 output
set logic 1 enabled:true
```

| Input | Min | Max | Forventet | Status |
|-------|-----|-----|-----------|--------|
| 50 | 0 | 100 | 50 | ⬜ |
| -10 | 0 | 100 | 0 | ⬜ |
| 150 | 0 | 100 | 100 | ⬜ |

---

# 3. Trigonometriske Funktioner

## Test 3.1: SIN

| Input (rad) | Forventet | Status |
|-------------|-----------|--------|
| 0.0 | 0.0 | ⬜ |
| 1.5708 (π/2) | 1.0 | ⬜ |
| 3.1416 (π) | 0.0 | ⬜ |

---

## Test 3.2: COS

| Input (rad) | Forventet | Status |
|-------------|-----------|--------|
| 0.0 | 1.0 | ⬜ |
| 1.5708 (π/2) | 0.0 | ⬜ |
| 3.1416 (π) | -1.0 | ⬜ |

---

## Test 3.3: TAN

| Input (rad) | Forventet | Status |
|-------------|-----------|--------|
| 0.0 | 0.0 | ⬜ |
| 0.7854 (π/4) | 1.0 | ⬜ |

---

# 4. Type Conversion Funktioner

## Test 4.1: INT_TO_REAL

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  a: INT;
  result: REAL;
END_VAR
BEGIN
  result := INT_TO_REAL(a);
END_PROGRAM
END_UPLOAD

set logic 1 bind a reg:20 input
set logic 1 bind result reg:22 output
set logic 1 enabled:true
```

| Input | Forventet | Status |
|-------|-----------|--------|
| 42 | 42.0 | ⬜ |
| -100 | -100.0 | ⬜ |

---

## Test 4.2: REAL_TO_INT

| Input | Forventet | Status |
|-------|-----------|--------|
| 42.7 | 42 (truncate) | ⬜ |
| -3.9 | -3 | ⬜ |

---

## Test 4.3: INT_TO_DINT

| Input | Forventet | Status |
|-------|-----------|--------|
| 32767 | 32767 | ⬜ |
| -32768 | -32768 | ⬜ |

---

## Test 4.4: DINT_TO_INT

| Input | Forventet | Status |
|-------|-----------|--------|
| 100 | 100 | ⬜ |
| 100000 | Overflow | ⬜ |

---

## Test 4.5: BOOL_TO_INT

| Input | Forventet | Status |
|-------|-----------|--------|
| TRUE | 1 | ⬜ |
| FALSE | 0 | ⬜ |

---

## Test 4.6: INT_TO_BOOL

| Input | Forventet | Status |
|-------|-----------|--------|
| 0 | FALSE | ⬜ |
| 1 | TRUE | ⬜ |
| 42 | TRUE | ⬜ |

---

# 5. Persistence Funktioner

## Test 5.1: RETAIN

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR RETAIN
  counter: INT := 0;
END_VAR
BEGIN
  counter := counter + 1;
END_PROGRAM
END_UPLOAD

set logic 1 enabled:true
```

**Test Procedure:**
1. Kør program, noter counter værdi
2. Reboot ESP32
3. Verificer counter beholder værdi

| Test | Forventet | Status |
|------|-----------|--------|
| Value preserved after reboot | Yes | ⬜ |

---

## Test 5.2: PERSISTENT

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR PERSISTENT
  setpoint: INT := 100;
END_VAR
BEGIN
  (* setpoint gemmes automatisk *)
END_PROGRAM
END_UPLOAD

set logic 1 enabled:true
```

| Test | Forventet | Status |
|------|-----------|--------|
| Value saved to NVS | Yes | ⬜ |
| Value restored after power cycle | Yes | ⬜ |

---

## Test Results Summary

| Sektion | Tests | Bestået | Fejlet |
|---------|-------|---------|--------|
| Matematiske | 9 | ⬜ | ⬜ |
| Clamping/Selection | 3 | ⬜ | ⬜ |
| Trigonometriske | 3 | ⬜ | ⬜ |
| Type Conversion | 6 | ⬜ | ⬜ |
| Persistence | 2 | ⬜ | ⬜ |
| **Total** | **23** | | |

---

**Tester:** _______________
**Dato:** _______________
