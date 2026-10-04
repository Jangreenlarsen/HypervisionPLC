# ST Logic Test - Operatorer

**Version:** v6.0.0
**Formål:** Test af alle ST operatorer

---

## Indholdsfortegnelse

1. [Aritmetiske Operatorer](#1-aritmetiske-operatorer)
2. [Logiske Operatorer](#2-logiske-operatorer)
3. [Bit-Shift Operatorer](#3-bit-shift-operatorer)
4. [Sammenlignings Operatorer](#4-sammenlignings-operatorer)

---

## Test Oversigt

| Kategori | Tests | Prioritet |
|----------|-------|-----------|
| Aritmetiske | 6 | HIGH |
| Logiske | 4 | HIGH |
| Bit-Shift | 4 | MEDIUM |
| Sammenligning | 6 | HIGH |
| **Total** | **20** | |

---

## Register Allocation

| Register | Brug |
|----------|------|
| HR 20 | Input a |
| HR 21 | Input b |
| HR 22 | Output result |
| Coil 0-2 | Boolean tests |

---

# 1. Aritmetiske Operatorer

## Test 1.1: Addition (+)

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

**Test Cases:**
```bash
# Test 1: 5 + 3 = 8
write reg 20 value int 5
write reg 21 value int 3
read reg 22 int
# Forventet: 8

# Test 2: -10 + 15 = 5
write reg 20 value int -10
write reg 21 value int 15
read reg 22 int
# Forventet: 5

# Test 3: Overflow (32767 + 1)
write reg 20 value int 32767
write reg 21 value int 1
read reg 22 int
# Forventet: -32768 (overflow)
```

| Test | Input | Forventet | Status |
|------|-------|-----------|--------|
| 5 + 3 | 5, 3 | 8 | ⬜ |
| -10 + 15 | -10, 15 | 5 | ⬜ |
| Overflow | 32767, 1 | -32768 | ⬜ |

---

## Test 1.2: Subtraktion (-)

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
  result := a - b;
END_PROGRAM
END_UPLOAD

set logic 1 bind a reg:20 input
set logic 1 bind b reg:21 input
set logic 1 bind result reg:22 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: 10 - 3 = 7
write reg 20 value int 10
write reg 21 value int 3
read reg 22 int

# Test 2: 5 - 10 = -5
write reg 20 value int 5
write reg 21 value int 10
read reg 22 int
```

| Test | Input | Forventet | Status |
|------|-------|-----------|--------|
| 10 - 3 | 10, 3 | 7 | ⬜ |
| 5 - 10 | 5, 10 | -5 | ⬜ |

---

## Test 1.3: Multiplikation (*)

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
  result := a * b;
END_PROGRAM
END_UPLOAD

set logic 1 bind a reg:20 input
set logic 1 bind b reg:21 input
set logic 1 bind result reg:22 output
set logic 1 enabled:true
```

| Test | Input | Forventet | Status |
|------|-------|-----------|--------|
| 5 * 3 | 5, 3 | 15 | ⬜ |
| -4 * 5 | -4, 5 | -20 | ⬜ |
| 0 * 100 | 0, 100 | 0 | ⬜ |

---

## Test 1.4: Division (/)

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
  result := a / b;
END_PROGRAM
END_UPLOAD

set logic 1 bind a reg:20 input
set logic 1 bind b reg:21 input
set logic 1 bind result reg:22 output
set logic 1 enabled:true
```

| Test | Input | Forventet | Status |
|------|-------|-----------|--------|
| 10 / 2 | 10, 2 | 5 | ⬜ |
| 7 / 2 | 7, 2 | 3 (int div) | ⬜ |
| 10 / 0 | 10, 0 | 0 (safe) | ⬜ |

---

## Test 1.5: Modulo (MOD)

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
  result := a MOD b;
END_PROGRAM
END_UPLOAD

set logic 1 bind a reg:20 input
set logic 1 bind b reg:21 input
set logic 1 bind result reg:22 output
set logic 1 enabled:true
```

| Test | Input | Forventet | Status |
|------|-------|-----------|--------|
| 10 MOD 3 | 10, 3 | 1 | ⬜ |
| 7 MOD 2 | 7, 2 | 1 | ⬜ |
| 8 MOD 4 | 8, 4 | 0 | ⬜ |

---

## Test 1.6: Negation (-)

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
  result := -a;
END_PROGRAM
END_UPLOAD

set logic 1 bind a reg:20 input
set logic 1 bind result reg:21 output
set logic 1 enabled:true
```

| Test | Input | Forventet | Status |
|------|-------|-----------|--------|
| -(5) | 5 | -5 | ⬜ |
| -(-10) | -10 | 10 | ⬜ |
| -(0) | 0 | 0 | ⬜ |

---

# 2. Logiske Operatorer

## Test 2.1: AND

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  a: BOOL;
  b: BOOL;
  result: BOOL;
END_VAR
BEGIN
  result := a AND b;
END_PROGRAM
END_UPLOAD

set logic 1 bind a coil:0 input
set logic 1 bind b coil:1 input
set logic 1 bind result coil:2 output
set logic 1 enabled:true
```

| a | b | Forventet | Status |
|---|---|-----------|--------|
| TRUE | TRUE | TRUE | ⬜ |
| TRUE | FALSE | FALSE | ⬜ |
| FALSE | TRUE | FALSE | ⬜ |
| FALSE | FALSE | FALSE | ⬜ |

---

## Test 2.2: OR

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  a: BOOL;
  b: BOOL;
  result: BOOL;
END_VAR
BEGIN
  result := a OR b;
END_PROGRAM
END_UPLOAD

set logic 1 bind a coil:0 input
set logic 1 bind b coil:1 input
set logic 1 bind result coil:2 output
set logic 1 enabled:true
```

| a | b | Forventet | Status |
|---|---|-----------|--------|
| TRUE | TRUE | TRUE | ⬜ |
| TRUE | FALSE | TRUE | ⬜ |
| FALSE | TRUE | TRUE | ⬜ |
| FALSE | FALSE | FALSE | ⬜ |

---

## Test 2.3: NOT

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  a: BOOL;
  result: BOOL;
END_VAR
BEGIN
  result := NOT a;
END_PROGRAM
END_UPLOAD

set logic 1 bind a coil:0 input
set logic 1 bind result coil:1 output
set logic 1 enabled:true
```

| a | Forventet | Status |
|---|-----------|--------|
| TRUE | FALSE | ⬜ |
| FALSE | TRUE | ⬜ |

---

## Test 2.4: XOR

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  a: BOOL;
  b: BOOL;
  result: BOOL;
END_VAR
BEGIN
  result := a XOR b;
END_PROGRAM
END_UPLOAD

set logic 1 bind a coil:0 input
set logic 1 bind b coil:1 input
set logic 1 bind result coil:2 output
set logic 1 enabled:true
```

| a | b | Forventet | Status |
|---|---|-----------|--------|
| TRUE | TRUE | FALSE | ⬜ |
| TRUE | FALSE | TRUE | ⬜ |
| FALSE | TRUE | TRUE | ⬜ |
| FALSE | FALSE | FALSE | ⬜ |

---

# 3. Bit-Shift Operatorer

## Test 3.1: SHL (Shift Left)

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  value: INT;
  shift: INT;
  result: INT;
END_VAR
BEGIN
  result := value SHL shift;
END_PROGRAM
END_UPLOAD

set logic 1 bind value reg:20 input
set logic 1 bind shift reg:21 input
set logic 1 bind result reg:22 output
set logic 1 enabled:true
```

| Test | Value | Shift | Forventet | Status |
|------|-------|-------|-----------|--------|
| 1 SHL 3 | 1 | 3 | 8 | ⬜ |
| 5 SHL 2 | 5 | 2 | 20 | ⬜ |
| 1 SHL 0 | 1 | 0 | 1 | ⬜ |

---

## Test 3.2: SHR (Shift Right)

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  value: INT;
  shift: INT;
  result: INT;
END_VAR
BEGIN
  result := value SHR shift;
END_PROGRAM
END_UPLOAD

set logic 1 bind value reg:20 input
set logic 1 bind shift reg:21 input
set logic 1 bind result reg:22 output
set logic 1 enabled:true
```

| Test | Value | Shift | Forventet | Status |
|------|-------|-------|-----------|--------|
| 8 SHR 3 | 8 | 3 | 1 | ⬜ |
| 20 SHR 2 | 20 | 2 | 5 | ⬜ |
| 1 SHR 0 | 1 | 0 | 1 | ⬜ |

---

## Test 3.3: ROL (Rotate Left)

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  value: INT;
  shift: INT;
  result: INT;
END_VAR
BEGIN
  result := ROL(value, shift);
END_PROGRAM
END_UPLOAD

set logic 1 bind value reg:20 input
set logic 1 bind shift reg:21 input
set logic 1 bind result reg:22 output
set logic 1 enabled:true
```

| Test | Value (hex) | Shift | Forventet | Status |
|------|-------------|-------|-----------|--------|
| ROL 4 bits | 0x1234 | 4 | 0x2341 | ⬜ |
| ROL 1 bit | 0x8000 | 1 | 0x0001 | ⬜ |
| Byte swap | 0xABCD | 8 | 0xCDAB | ⬜ |

---

## Test 3.4: ROR (Rotate Right)

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  value: INT;
  shift: INT;
  result: INT;
END_VAR
BEGIN
  result := ROR(value, shift);
END_PROGRAM
END_UPLOAD

set logic 1 bind value reg:20 input
set logic 1 bind shift reg:21 input
set logic 1 bind result reg:22 output
set logic 1 enabled:true
```

| Test | Value (hex) | Shift | Forventet | Status |
|------|-------------|-------|-----------|--------|
| ROR 4 bits | 0x1234 | 4 | 0x4123 | ⬜ |
| ROR 1 bit | 0x0001 | 1 | 0x8000 | ⬜ |
| Byte swap | 0xABCD | 8 | 0xCDAB | ⬜ |

---

# 4. Sammenlignings Operatorer

## Test 4.1: Equal (=)

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  a: INT;
  b: INT;
  result: BOOL;
END_VAR
BEGIN
  result := (a = b);
END_PROGRAM
END_UPLOAD

set logic 1 bind a reg:20 input
set logic 1 bind b reg:21 input
set logic 1 bind result coil:0 output
set logic 1 enabled:true
```

| a | b | Forventet | Status |
|---|---|-----------|--------|
| 5 | 5 | TRUE | ⬜ |
| 5 | 3 | FALSE | ⬜ |
| 0 | 0 | TRUE | ⬜ |

---

## Test 4.2: Not Equal (<>)

| a | b | Forventet | Status |
|---|---|-----------|--------|
| 5 | 3 | TRUE | ⬜ |
| 5 | 5 | FALSE | ⬜ |

---

## Test 4.3: Less Than (<)

| a | b | Forventet | Status |
|---|---|-----------|--------|
| 3 | 5 | TRUE | ⬜ |
| 5 | 3 | FALSE | ⬜ |
| 5 | 5 | FALSE | ⬜ |

---

## Test 4.4: Greater Than (>)

| a | b | Forventet | Status |
|---|---|-----------|--------|
| 5 | 3 | TRUE | ⬜ |
| 3 | 5 | FALSE | ⬜ |
| 5 | 5 | FALSE | ⬜ |

---

## Test 4.5: Less or Equal (<=)

| a | b | Forventet | Status |
|---|---|-----------|--------|
| 3 | 5 | TRUE | ⬜ |
| 5 | 5 | TRUE | ⬜ |
| 5 | 3 | FALSE | ⬜ |

---

## Test 4.6: Greater or Equal (>=)

| a | b | Forventet | Status |
|---|---|-----------|--------|
| 5 | 3 | TRUE | ⬜ |
| 5 | 5 | TRUE | ⬜ |
| 3 | 5 | FALSE | ⬜ |

---

## Test Results Summary

| Sektion | Tests | Bestået | Fejlet |
|---------|-------|---------|--------|
| Aritmetiske | 6 | ⬜ | ⬜ |
| Logiske | 4 | ⬜ | ⬜ |
| Bit-Shift | 4 | ⬜ | ⬜ |
| Sammenligning | 6 | ⬜ | ⬜ |
| **Total** | **20** | | |

---

**Tester:** _______________
**Dato:** _______________
