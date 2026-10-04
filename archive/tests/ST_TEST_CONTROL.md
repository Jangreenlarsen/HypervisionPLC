# ST Logic Test - Kontrolstrukturer

**Version:** v6.0.0
**Formål:** Test af IF, CASE, FOR, WHILE, REPEAT

---

## Test Oversigt

| Struktur | Tests | Prioritet |
|----------|-------|-----------|
| IF/THEN/ELSE | 2 | HIGH |
| CASE | 1 | MEDIUM |
| FOR Loop | 1 | HIGH |
| WHILE Loop | 1 | MEDIUM |
| REPEAT Loop | 1 | LOW |
| **Total** | **6** | |

---

# 1. IF/THEN/ELSE

## Test 1.1: Simple IF

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  temp: INT;
  heater_on: BOOL;
END_VAR
BEGIN
  IF temp < 20 THEN
    heater_on := TRUE;
  ELSE
    heater_on := FALSE;
  END_IF;
END_PROGRAM
END_UPLOAD

set logic 1 bind temp reg:20 input
set logic 1 bind heater_on coil:0 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: temp = 15 (under threshold)
write reg 20 value int 15
read coil 0
# Forventet: 1 (heater ON)

# Test 2: temp = 25 (over threshold)
write reg 20 value int 25
read coil 0
# Forventet: 0 (heater OFF)

# Test 3: temp = 20 (exact threshold)
write reg 20 value int 20
read coil 0
# Forventet: 0 (heater OFF, >= 20)
```

| Temp | Forventet heater_on | Status |
|------|---------------------|--------|
| 15 | TRUE | ⬜ |
| 25 | FALSE | ⬜ |
| 20 | FALSE | ⬜ |

---

## Test 1.2: Nested IF with ELSIF

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  level: INT;
  alarm: INT;  (* 0=none, 1=warning, 2=critical *)
END_VAR
BEGIN
  IF level > 90 THEN
    alarm := 2;  (* Critical *)
  ELSIF level > 70 THEN
    alarm := 1;  (* Warning *)
  ELSE
    alarm := 0;  (* Normal *)
  END_IF;
END_PROGRAM
END_UPLOAD

set logic 1 bind level reg:20 input
set logic 1 bind alarm reg:21 output
set logic 1 enabled:true
```

| Level | Forventet Alarm | Status |
|-------|-----------------|--------|
| 50 | 0 (normal) | ⬜ |
| 75 | 1 (warning) | ⬜ |
| 95 | 2 (critical) | ⬜ |

---

# 2. CASE Statement

## Test 2.1: CASE with Multiple Branches

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  state: INT;
  output: INT;
END_VAR
BEGIN
  CASE state OF
    0: output := 100;
    1: output := 200;
    2: output := 300;
    3: output := 400;
  ELSE
    output := 0;
  END_CASE;
END_PROGRAM
END_UPLOAD

set logic 1 bind state reg:20 input
set logic 1 bind output reg:21 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
write reg 20 value int 0
read reg 21 int
# Forventet: 100

write reg 20 value int 2
read reg 21 int
# Forventet: 300

write reg 20 value int 99
read reg 21 int
# Forventet: 0 (ELSE branch)
```

| State | Forventet Output | Status |
|-------|------------------|--------|
| 0 | 100 | ⬜ |
| 1 | 200 | ⬜ |
| 2 | 300 | ⬜ |
| 3 | 400 | ⬜ |
| 99 | 0 (ELSE) | ⬜ |

---

# 3. FOR Loop

## Test 3.1: FOR Loop Sum

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  i: INT;
  n: INT;
  sum: INT;
END_VAR
BEGIN
  sum := 0;
  FOR i := 1 TO n DO
    sum := sum + i;
  END_FOR;
END_PROGRAM
END_UPLOAD

set logic 1 bind n reg:20 input
set logic 1 bind sum reg:21 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Sum 1+2+3+4+5 = 15
write reg 20 value int 5
read reg 21 int
# Forventet: 15

# Sum 1+2+...+10 = 55
write reg 20 value int 10
read reg 21 int
# Forventet: 55

# n=0 (no iterations)
write reg 20 value int 0
read reg 21 int
# Forventet: 0
```

| n | Forventet Sum | Status |
|---|---------------|--------|
| 5 | 15 | ⬜ |
| 10 | 55 | ⬜ |
| 0 | 0 | ⬜ |

---

# 4. WHILE Loop

## Test 4.1: WHILE Loop Counter

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  target: INT;
  count: INT;
END_VAR
BEGIN
  count := 0;
  WHILE count < target DO
    count := count + 1;
  END_WHILE;
END_PROGRAM
END_UPLOAD

set logic 1 bind target reg:20 input
set logic 1 bind count reg:21 output
set logic 1 enabled:true
```

| Target | Forventet Count | Status |
|--------|-----------------|--------|
| 5 | 5 | ⬜ |
| 10 | 10 | ⬜ |
| 0 | 0 (no iterations) | ⬜ |

---

# 5. REPEAT Loop

## Test 5.1: REPEAT UNTIL

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  target: INT;
  count: INT;
END_VAR
BEGIN
  count := 0;
  REPEAT
    count := count + 1;
  UNTIL count >= target
  END_REPEAT;
END_PROGRAM
END_UPLOAD

set logic 1 bind target reg:20 input
set logic 1 bind count reg:21 output
set logic 1 enabled:true
```

| Target | Forventet Count | Status |
|--------|-----------------|--------|
| 5 | 5 | ⬜ |
| 1 | 1 | ⬜ |
| 0 | 1 (runs once) | ⬜ |

**Note:** REPEAT kører mindst én gang (post-condition check).

---

## Test Results Summary

| Struktur | Tests | Bestået | Fejlet |
|----------|-------|---------|--------|
| IF/THEN/ELSE | 2 | ⬜ | ⬜ |
| CASE | 1 | ⬜ | ⬜ |
| FOR | 1 | ⬜ | ⬜ |
| WHILE | 1 | ⬜ | ⬜ |
| REPEAT | 1 | ⬜ | ⬜ |
| **Total** | **6** | | |

---

**Tester:** _______________
**Dato:** _______________
