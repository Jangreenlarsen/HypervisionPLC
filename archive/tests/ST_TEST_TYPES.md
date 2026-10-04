# ST Logic Test - Type System

**Version:** v6.0.0
**Formål:** Test af INT/DINT/REAL type system, EXPORT, TIME literals

---

## Test Oversigt

| Kategori | Tests | Prioritet |
|----------|-------|-----------|
| INT vs DINT | 3 | HIGH |
| EXPORT Keyword | 5 | HIGH |
| TIME Literals | 4 | MEDIUM |
| **Total** | **12** | |

---

# 1. Type System - INT vs DINT

## Test 1.1: INT Range

**Beskrivelse:** INT er 16-bit signed (-32768 til 32767).

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
  result := a + b;
END_PROGRAM
END_UPLOAD

set logic 1 bind a reg:20 input
set logic 1 bind b reg:21 input
set logic 1 bind result reg:22 output
set logic 1 enabled:true
```

| Test | a | b | Forventet | Status |
|------|---|---|-----------|--------|
| Normal | 100 | 200 | 300 | ⬜ |
| Max | 32767 | 0 | 32767 | ⬜ |
| Min | -32768 | 0 | -32768 | ⬜ |
| Overflow | 32767 | 1 | -32768 | ⬜ |

---

## Test 1.2: DINT Range

**Beskrivelse:** DINT er 32-bit signed (~±2 billion).

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  a: DINT;
  b: DINT;
  result: DINT;
END_VAR
BEGIN
  result := a + b;
END_PROGRAM
END_UPLOAD

set logic 1 bind a reg:20 input    (* 2 registers: 20-21 *)
set logic 1 bind b reg:22 input    (* 2 registers: 22-23 *)
set logic 1 bind result reg:24 output
set logic 1 enabled:true
```

| Test | a | b | Forventet | Status |
|------|---|---|-----------|--------|
| Large | 100000 | 200000 | 300000 | ⬜ |
| Beyond INT | 50000 | 50000 | 100000 | ⬜ |

---

## Test 1.3: REAL Precision

**Beskrivelse:** REAL er 32-bit float.

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  a: REAL;
  b: REAL;
  result: REAL;
END_VAR
BEGIN
  result := a + b;
END_PROGRAM
END_UPLOAD

set logic 1 bind a reg:20 input    (* 2 registers for float *)
set logic 1 bind b reg:22 input
set logic 1 bind result reg:24 output
set logic 1 enabled:true
```

| Test | a | b | Forventet | Status |
|------|---|---|-----------|--------|
| Decimal | 3.14 | 2.86 | 6.0 | ⬜ |
| Small | 0.001 | 0.002 | 0.003 | ⬜ |

---

# 2. EXPORT Keyword & IR Pool

## Test 2.1: EXPORT Basic

**Beskrivelse:** EXPORT variabler tildeles automatisk Input Registers.

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  internal: INT := 100;
END_VAR
VAR EXPORT
  sensor1: INT := 50;
  sensor2: INT := 75;
END_VAR
BEGIN
  sensor1 := sensor1 + 1;
  sensor2 := internal;
END_PROGRAM
END_UPLOAD

set logic 1 enabled:true
```

**Verificering:**
```bash
# Check IR pool allocation
show logic 1

# Læs EXPORT variabler via Modbus
# sensor1 og sensor2 bør være tilgængelige i IR
```

| Test | Forventet | Status |
|------|-----------|--------|
| EXPORT vars get IR addresses | Yes | ⬜ |
| IR values readable via Modbus | Yes | ⬜ |

---

## Test 2.2: EXPORT Multiple Types

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR EXPORT
  int_val: INT := 42;
  dint_val: DINT := 100000;
  real_val: REAL := 3.14;
  bool_val: BOOL := TRUE;
END_VAR
BEGIN
  (* Values should be accessible via IR *)
END_PROGRAM
END_UPLOAD

set logic 1 enabled:true
```

| Type | Registers Used | Status |
|------|----------------|--------|
| INT | 1 | ⬜ |
| DINT | 2 | ⬜ |
| REAL | 2 | ⬜ |
| BOOL | 1 | ⬜ |

---

## Test 2.3: IR Pool Exhaustion

**Beskrivelse:** Test at systemet håndterer for mange EXPORT variabler.

**Test Procedure:**
1. Opret program med mange EXPORT variabler
2. Verificer fejlbesked ved pool exhaustion

| Test | Forventet | Status |
|------|-----------|--------|
| Error on pool exhaustion | Yes | ⬜ |
| Clear error message | Yes | ⬜ |

---

## Test 2.4: EXPORT Cross-Program Access

**Beskrivelse:** Test at EXPORT variabler fra ét program er læsbare globalt.

**CLI Kommandoer:**
```bash
# Program 1: Producer
set logic 1 upload
PROGRAM producer
VAR EXPORT
  shared_value: INT := 0;
END_VAR
BEGIN
  shared_value := shared_value + 1;
END_PROGRAM
END_UPLOAD
set logic 1 enabled:true

# Program 2: Consumer (læser via IR)
# shared_value bør være tilgængelig i IR
```

| Test | Forventet | Status |
|------|-----------|--------|
| EXPORT accessible from other programs | Yes | ⬜ |

---

## Test 2.5: EXPORT Persistence

**Beskrivelse:** EXPORT variabler beholder værdi efter program restart.

**Test Procedure:**
1. Set EXPORT variabel til specifik værdi
2. Disable/enable program
3. Verificer værdi bevares

| Test | Forventet | Status |
|------|-----------|--------|
| Value preserved after restart | Yes | ⬜ |

---

# 3. TIME Literals (FEAT-006)

## Test 3.1: TIME Literal Parsing

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  t1: TIME := T#1s;
  t2: TIME := T#500ms;
  t3: TIME := T#1m30s;
  t4: TIME := T#2h30m;
  timer1: TON;
  output: BOOL;
END_VAR
BEGIN
  timer1(IN := TRUE, PT := t1);
  output := timer1.Q;
END_PROGRAM
END_UPLOAD

set logic 1 enabled:true
```

| Literal | Expected ms | Status |
|---------|-------------|--------|
| T#1s | 1000 | ⬜ |
| T#500ms | 500 | ⬜ |
| T#1m30s | 90000 | ⬜ |
| T#2h30m | 9000000 | ⬜ |

---

## Test 3.2: TIME Arithmetic

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  t1: TIME := T#1s;
  t2: TIME := T#500ms;
  t_sum: TIME;
  t_diff: TIME;
END_VAR
BEGIN
  t_sum := t1 + t2;    (* 1500ms *)
  t_diff := t1 - t2;   (* 500ms *)
END_PROGRAM
END_UPLOAD

set logic 1 enabled:true
```

| Operation | Forventet | Status |
|-----------|-----------|--------|
| T#1s + T#500ms | 1500ms | ⬜ |
| T#1s - T#500ms | 500ms | ⬜ |

---

## Test 3.3: TIME in Timer Functions

**Beskrivelse:** Verificer at TIME literals virker korrekt med TON/TOF/TP.

**Test Procedure:**
1. Opret timer med T#2s
2. Trigger timer
3. Verificer output efter 2s

| Timer | PT | Actual Delay | Status |
|-------|----|--------------| --------|
| TON | T#2s | ~2000ms | ⬜ |
| TOF | T#1s | ~1000ms | ⬜ |
| TP | T#500ms | ~500ms | ⬜ |

---

## Test 3.4: TIME Edge Cases

| Literal | Expected | Status |
|---------|----------|--------|
| T#0s | 0ms | ⬜ |
| T#1d | 86400000ms | ⬜ |
| T#1h1m1s1ms | 3661001ms | ⬜ |

---

## Test Results Summary

| Kategori | Tests | Bestået | Fejlet |
|----------|-------|---------|--------|
| INT vs DINT | 3 | ⬜ | ⬜ |
| EXPORT | 5 | ⬜ | ⬜ |
| TIME Literals | 4 | ⬜ | ⬜ |
| **Total** | **12** | | |

---

**Tester:** _______________
**Dato:** _______________
