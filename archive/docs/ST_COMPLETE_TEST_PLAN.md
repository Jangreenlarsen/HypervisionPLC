# ST Logic - Komplet Testplan

**Version:** v5.1.0
**Build:** #1032+
**Formål:** Systematisk test af ALLE ST-funktioner

---

## 📋 Indholdsfortegnelse

1. [Test Oversigt](#test-oversigt)
2. [Hardware Setup](#hardware-setup)
3. [Fase 1: Individuelle Funktionstests](#fase-1-individuelle-funktionstests)
   - [1.1 Aritmetiske Operatorer](#11-aritmetiske-operatorer)
   - [1.2 Logiske Operatorer](#12-logiske-operatorer)
   - [1.3 Bit-Shift Operatorer](#13-bit-shift-operatorer)
   - [1.4 Sammenlignings Operatorer](#14-sammenlignings-operatorer)
   - [1.5 Builtin Funktioner - Matematiske](#15-builtin-funktioner---matematiske)
   - [1.6 Builtin Funktioner - Clamping/Selection](#16-builtin-funktioner---clampingselection)
   - [1.7 Builtin Funktioner - Trigonometriske](#17-builtin-funktioner---trigonometriske)
   - [1.8 Builtin Funktioner - Type Conversion](#18-builtin-funktioner---type-conversion)
   - [1.9 Builtin Funktioner - Persistence](#19-builtin-funktioner---persistence)
   - [1.10 Builtin Funktioner - Timers (TON/TOF/TP)](#110-builtin-funktioner---timers-tontoFtp)
   - [1.11 GPIO & Hardware Tests](#111-gpio--hardware-tests)
   - [1.12 Kontrolstrukturer - IF/THEN/ELSE](#112-kontrolstrukturer---ifthenelse)
   - [1.13 Kontrolstrukturer - CASE](#113-kontrolstrukturer---case)
   - [1.14 Kontrolstrukturer - FOR Loop](#114-kontrolstrukturer---for-loop)
   - [1.15 Kontrolstrukturer - WHILE Loop](#115-kontrolstrukturer---while-loop)
   - [1.16 Kontrolstrukturer - REPEAT Loop](#116-kontrolstrukturer---repeat-loop)
   - [1.17 Type System - INT vs DINT](#117-type-system---int-vs-dint)
   - [1.18 EXPORT Keyword & IR Pool Allocation](#118-export-keyword--ir-pool-allocation)
   - [1.19 TIME Literals (FEAT-006)](#119-time-literals-feat-006)
4. [Fase 2: Kombinerede Tests](#fase-2-kombinerede-tests)
5. [Test Execution Workflow](#test-execution-workflow)
6. [Fejlhåndtering](#fejlhåndtering)
7. [Udsatte Tests](#udsatte-tests)

---

## Test Oversigt

### Test Statistik
| Kategori | Antal Tests | Estimeret Tid |
|----------|-------------|---------------|
| Aritmetiske operatorer | 6 | 3 min |
| Logiske operatorer | 4 | 2 min |
| Bit-shift operatorer | 4 | 2 min |
| Sammenlignings operatorer | 6 | 3 min |
| Builtin matematiske | 9 | 5 min |
| Builtin clamping/selection | 3 | 3 min |
| Builtin trigonometriske | 3 | 2 min |
| Builtin type conversion | 6 | 3 min |
| Builtin persistence | 2 | 2 min |
| Builtin timers (TON/TOF/TP) | 6 | 8 min |
| GPIO & Hardware tests | 4 | 5 min |
| Kontrolstrukturer | 5 | 5 min |
| Type System (INT/DINT) | 3 | 5 min |
| EXPORT keyword & IR pool | 5 | 8 min |
| TIME literals (FEAT-006) | 4 | 5 min |
| **Fase 1 Total** | **70** | **61 min** |
| Kombinerede tests | 10 | 15 min |
| **Total** | **80** | **76 min** |
| **Udsat (Modbus Master)** | **6** | *Senere* |

### Test Konventioner
- ✅ = Test bestået
- ❌ = Test fejlet
- ⚠️ = Advarsel/bemærkning
- 🔄 = Test skal gentages efter fix

---

## 📍 Register Allocation for Tests

### Safe Register Ranges

Alle test cases bruger **safe register ranges** for at undgå konflikter med system funktioner:

#### Holding Registers (HR)
| Range | Brug i Tests | Undgår Konflikt Med |
|-------|--------------|---------------------|
| **HR 20-29** | Basis INT test variables (a, b, result) | Counter default range (HR 100-179) |
| **HR 40-55** | DINT/DWORD tests (multi-register, 32-bit) | Counter default range |
| **HR 60-69** | REAL tests (multi-register, float) | Counter default range |
| **HR 70-89** | Fase 2 kombinerede tests (setpoint, actual, output) | Counter default range |

#### Coils & Discrete Inputs
| Range | Brug i Tests |
|-------|--------------|
| **Coils 0-20** | Test outputs, boolean variables |
| **DI 0-20** | Test inputs, sensor simulation |

### ⚠️ Hvorfor IKKE HR 100+?

**Counter default allocation:**
- Counter 1: HR 100-119 (20 registers)
- Counter 2: HR 120-139 (20 registers)
- Counter 3: HR 140-159 (20 registers)
- Counter 4: HR 160-179 (20 registers)

**ST Logic system registers:**
- IR/HR 200-293: Status, control, statistics (SYSTEM RESERVED)

Hvis test cases bruger HR 100-179, kolliderer de med counter registers, hvilket forhindrer ST Logic i at læse/skrive korrekt.

### 💡 Best Practice

**For almindelige tests:**
```bash
# ✅ KORREKT - Safe range
set logic 1 bind a reg:20 input
set logic 1 bind b reg:21 input
set logic 1 bind result reg:22 output
```

**Undgå:**
```bash
# ❌ FORKERT - Kolliderer med Counter 1!
set logic 1 bind a reg:100 input    # Counter 1 value_reg
set logic 1 bind b reg:101 input
set logic 1 bind result reg:102 output
```

**Se også:** `MODBUS_REGISTER_MAP.md` § Register Allocation Guide for komplet dokumentation.

---

## Hardware Setup

### GPIO Konfiguration

**Output LEDs:**
| GPIO Pin | Funktion | Binding |
|----------|----------|---------|
| GPIO17 | LED 1 | Coil/Discrete Output |
| GPIO18 | LED 2 | Coil/Discrete Output |
| GPIO19 | LED 3 | Coil/Discrete Output |
| GPIO21 | LED 4 | Coil/Discrete Output |

**Input Switches:**
| GPIO Pin | Funktion | Binding |
|----------|----------|---------|
| GPIO32 | Switch 1 | Discrete Input |
| GPIO33 | Switch 2 | Discrete Input |
| GPIO34 | Switch 3 | Discrete Input |

**Test Signal:**
| GPIO Pin | Signal | Frekvens | Anvendelse |
|----------|--------|----------|------------|
| GPIO25 | Square Wave | 1 kHz | Counter/Timer Test |

### Hardware Test Setup Diagram

```
ESP32
┌─────────────────────────────────┐
│                                 │
│  GPIO17 ──────────────── LED1   │  Output Tests
│  GPIO18 ──────────────── LED2   │  (Coils)
│  GPIO19 ──────────────── LED3   │
│  GPIO21 ──────────────── LED4   │
│                                 │
│  GPIO32 ──────────────── SW1    │  Input Tests
│  GPIO33 ──────────────── SW2    │  (Discrete Inputs)
│  GPIO34 ──────────────── SW3    │
│                                 │
│  GPIO25 ◄────── 1kHz Signal     │  Counter/Timer Tests
│                                 │
└─────────────────────────────────┘
```

### GPIO Mapping i ST Logic

**Output (BOOL → GPIO):**
```bash
# LED control via ST Logic
set logic 1 bind led1 coil:17    # GPIO17
set logic 1 bind led2 coil:18    # GPIO18
set logic 1 bind led3 coil:19    # GPIO19
set logic 1 bind led4 coil:21    # GPIO21
```

**Input (GPIO → BOOL):**
```bash
# Switch read via ST Logic
set logic 1 bind sw1 discrete:32  # GPIO32
set logic 1 bind sw2 discrete:33  # GPIO33
set logic 1 bind sw3 discrete:34  # GPIO34
```

---

# Fase 1: Individuelle Funktionstests

## 1.1 Aritmetiske Operatorer

### Test 1.1.1: Addition (+)

**CLI Kommandoer (Copy/Paste):**
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
# Via Modbus eller CLI
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

# Test 3: 0 + 0 = 0
write reg 20 value int 0
write reg 21 value int 0
read reg 22 int
# Forventet: 0

# Test 4: Overflow test (INT_MAX + 1)
write reg 20 value int 32767
write reg 21 value int 1
read reg 22 int
# Forventet: -32768
```

**Forventet Resultat:**
```
✅ 5 + 3 = 8
✅ -10 + 15 = 5
✅ 0 + 0 = 0
⚠️ 32767 + 1 = -32768 (overflow behavior)
```

---

### Test 1.1.2: Subtraktion (-)

**CLI Kommandoer (Copy/Paste):**
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
# Forventet: 7

# Test 2: 5 - 10 = -5
write reg 20 value int 5
write reg 21 value int 10
read reg 22 int
# Forventet: -5

# Test 3: 0 - 0 = 0
write reg 20 value int 0
write reg 21 value int 0
read reg 22 int
# Forventet: 0
```

**Forventet Resultat:**
```
✅ 10 - 3 = 7
✅ 5 - 10 = -5
✅ 0 - 0 = 0
```

---

### Test 1.1.3: Multiplikation (*)

**CLI Kommandoer (Copy/Paste):**
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

**Test Cases:**
```bash
# Test 1: 5 * 3 = 15
write reg 20 value int 5
write reg 21 value int 3
read reg 22 int
# Forventet: 15

# Test 2: -4 * 5 = -20
write reg 20 value int -4
write reg 21 value int 5
read reg 22 int
# Forventet: -20

# Test 3: 0 * 100 = 0
write reg 20 value int 0
write reg 21 value int 100
read reg 22 int
# Forventet: 0
```

**Forventet Resultat:**
```
✅ 5 * 3 = 15
✅ -4 * 5 = -20
✅ 0 * 100 = 0
```

---

### Test 1.1.4: Division (/)

**CLI Kommandoer (Copy/Paste):**
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

**Test Cases:**
```bash
# Test 1: 10 / 2 = 5
write reg 20 value int 10
write reg 21 value int 2
read reg 22 int
# Forventet: 5

# Test 2: 7 / 2 = 3 (integer division)
write reg 20 value int 7
write reg 21 value int 2
read reg 22 int
# Forventet: 3

# Test 3: -10 / 3 = -3
write reg 20 value int -10
write reg 21 value int 3
read reg 22 int
# Forventet: -3

# Test 4: Division by zero (should handle gracefully)
write reg 20 value int 10
write reg 21 value int 0
read reg 22 int
# Forventet: 0
```

**Forventet Resultat:**
```
✅ 10 / 2 = 5
✅ 7 / 2 = 3 (integer division)
✅ -10 / 3 = -3
⚠️ 10 / 0 → handled gracefully (no crash)
```

---

### Test 1.1.5: Modulo (MOD)

**CLI Kommandoer (Copy/Paste):**
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

**Test Cases:**
```bash
# Test 1: 10 MOD 3 = 1
write reg 20 value int 10
write reg 21 value int 3
read reg 22 int
# Forventet: 1

# Test 2: 7 MOD 2 = 1
write reg 20 value int 7
write reg 21 value int 2
read reg 22 int
# Forventet: 1

# Test 3: 8 MOD 4 = 0
write reg 20 value int 8
write reg 21 value int 4
read reg 22 int
# Forventet: 0
```

**Forventet Resultat:**
```
✅ 10 MOD 3 = 1
✅ 7 MOD 2 = 1
✅ 8 MOD 4 = 0
```

---

### Test 1.1.6: Negation (unary -)

**CLI Kommandoer (Copy/Paste):**
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

**Test Cases:**
```bash
# Test 1: -5 → -5
write reg 20 value int 5
read reg 21 int
# Forventet: -5

# Test 2: -(-10) → 10
write reg 20 value int -10
read reg 21 int
# Forventet: 10

# Test 3: -0 → 0
write reg 20 value int 0
read reg 21 int
# Forventet: 0
```

**Forventet Resultat:**
```
✅ -5 = -5
✅ -(-10) = 10
✅ -0 = 0
```

---

## 1.2 Logiske Operatorer

### Test 1.2.1: Logical AND

**CLI Kommandoer (Copy/Paste):**
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

**Test Cases:**
```bash
# Test 1: TRUE AND TRUE = TRUE
write coil 0 value 1
read coil 2
# Forventet: 1

# Test 2: TRUE AND FALSE = FALSE
write coil 0 value 1
read coil 2
# Forventet: 0

# Test 3: FALSE AND TRUE = FALSE
write coil 0 value 0
read coil 2
# Forventet: 0

# Test 4: FALSE AND FALSE = FALSE
write coil 0 value 0
read coil 2
# Forventet: 0
```

**Forventet Resultat:**
```
✅ TRUE AND TRUE = TRUE
✅ TRUE AND FALSE = FALSE
✅ FALSE AND TRUE = FALSE
✅ FALSE AND FALSE = FALSE
```

---

### Test 1.2.2: Logical OR

**CLI Kommandoer (Copy/Paste):**
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

**Test Cases:**
```bash
# Test 1: TRUE OR TRUE = TRUE
write coil 0 value 1
read coil 2
# Forventet: 1

# Test 2: TRUE OR FALSE = TRUE
write coil 0 value 1
read coil 2
# Forventet: 1

# Test 3: FALSE OR TRUE = TRUE
write coil 0 value 0
read coil 2
# Forventet: 1

# Test 4: FALSE OR FALSE = FALSE
write coil 0 value 0
read coil 2
# Forventet: 0
```

**Forventet Resultat:**
```
✅ TRUE OR TRUE = TRUE
✅ TRUE OR FALSE = TRUE
✅ FALSE OR TRUE = TRUE
✅ FALSE OR FALSE = FALSE
```

---

### Test 1.2.3: Logical NOT

**CLI Kommandoer (Copy/Paste):**
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

**Test Cases:**
```bash
# Test 1: NOT TRUE = FALSE
write coil 0 value 1
read coil 1
# Forventet: 0

# Test 2: NOT FALSE = TRUE
write coil 0 value 0
read coil 1
# Forventet: 1
```

**Forventet Resultat:**
```
✅ NOT TRUE = FALSE
✅ NOT FALSE = TRUE
```

---

### Test 1.2.4: Logical XOR

**CLI Kommandoer (Copy/Paste):**
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

**Test Cases:**
```bash
# Test 1: TRUE XOR TRUE = FALSE
write coil 0 value 1
read coil 2
# Forventet: 0

# Test 2: TRUE XOR FALSE = TRUE
write coil 0 value 1
read coil 2
# Forventet: 1

# Test 3: FALSE XOR TRUE = TRUE
write coil 0 value 0
read coil 2
# Forventet: 1

# Test 4: FALSE XOR FALSE = FALSE
write coil 0 value 0
read coil 2
# Forventet: 0
```

**Forventet Resultat:**
```
✅ TRUE XOR TRUE = FALSE
✅ TRUE XOR FALSE = TRUE
✅ FALSE XOR TRUE = TRUE
✅ FALSE XOR FALSE = FALSE
```

---

## 1.3 Bit-Shift Operatorer

### Test 1.3.1: Shift Left (SHL)

**CLI Kommandoer (Copy/Paste):**
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

**Test Cases:**
```bash
# Test 1: 1 SHL 3 = 8
write reg 20 value int 1
write reg 21 value int 3
read reg 22 int
# Forventet: 8

# Test 2: 5 SHL 2 = 20
write reg 20 value int 5
write reg 21 value int 2
read reg 22 int
# Forventet: 20

# Test 3: 1 SHL 0 = 1
write reg 20 value int 1
write reg 21 value int 0
read reg 22 int
# Forventet: 1
```

**Forventet Resultat:**
```
✅ 1 SHL 3 = 8
✅ 5 SHL 2 = 20
✅ 1 SHL 0 = 1
```

---

### Test 1.3.2: Shift Right (SHR)

**CLI Kommandoer (Copy/Paste):**
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

**Test Cases:**
```bash
# Test 1: 8 SHR 3 = 1
write reg 20 value int 8
write reg 21 value int 3
read reg 22 int
# Forventet: 1

# Test 2: 20 SHR 2 = 5
write reg 20 value int 20
write reg 21 value int 2
read reg 22 int
# Forventet: 5

# Test 3: 1 SHR 0 = 1
write reg 20 value int 1
write reg 21 value int 0
read reg 22 int
# Forventet: 1
```

**Forventet Resultat:**
```
✅ 8 SHR 3 = 1
✅ 20 SHR 2 = 5
✅ 1 SHR 0 = 1
```

---

### Test 1.3.3: ROL (Rotate Left)

**CLI Kommandoer (Copy/Paste):**
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

**Test Cases:**
```bash
# Test 1: ROL(0x1234, 4) = 0x2341
write reg 20 value int 0x1234
write reg 21 value int 4
read reg 22 int
# Forventet: 0x2341 (9025 decimal)

# Test 2: ROL(0x0001, 1) = 0x0002
write reg 20 value int 1
write reg 21 value int 1
read reg 22 int
# Forventet: 2

# Test 3: ROL(0x8000, 1) = 0x0001 (MSB wraps to LSB)
write reg 20 value int 0x8000
write reg 21 value int 1
read reg 22 int
# Forventet: 1

# Test 4: ROL(0xABCD, 8) = 0xCDAB (byte swap)
write reg 20 value int 0xABCD
write reg 21 value int 8
read reg 22 int
# Forventet: 0xCDAB (52651 decimal)

# Test 5: ROL(0xFF00, 4) = 0xF00F
write reg 20 value int 0xFF00
write reg 21 value int 4
read reg 22 int
# Forventet: 0xF00F (61455 decimal)
```

**Forventet Resultat:**
```
✅ ROL(0x1234, 4) = 0x2341
✅ ROL(0x0001, 1) = 0x0002
✅ ROL(0x8000, 1) = 0x0001 (wrap)
✅ ROL(0xABCD, 8) = 0xCDAB (byte swap)
✅ ROL(0xFF00, 4) = 0xF00F
```

---

### Test 1.3.4: ROR (Rotate Right)

**CLI Kommandoer (Copy/Paste):**
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

**Test Cases:**
```bash
# Test 1: ROR(0x1234, 4) = 0x4123
write reg 20 value int 0x1234
write reg 21 value int 4
read reg 22 int
# Forventet: 0x4123 (16675 decimal)

# Test 2: ROR(0x0002, 1) = 0x0001
write reg 20 value int 2
write reg 21 value int 1
read reg 22 int
# Forventet: 1

# Test 3: ROR(0x0001, 1) = 0x8000 (LSB wraps to MSB)
write reg 20 value int 1
write reg 21 value int 1
read reg 22 int
# Forventet: 0x8000 (32768 decimal)

# Test 4: ROR(0xABCD, 8) = 0xCDAB (byte swap)
write reg 20 value int 0xABCD
write reg 21 value int 8
read reg 22 int
# Forventet: 0xCDAB (52651 decimal)

# Test 5: ROR(0xFF00, 4) = 0x0FF0
write reg 20 value int 0xFF00
write reg 21 value int 4
read reg 22 int
# Forventet: 0x0FF0 (4080 decimal)
```

**Forventet Resultat:**
```
✅ ROR(0x1234, 4) = 0x4123
✅ ROR(0x0002, 1) = 0x0001
✅ ROR(0x0001, 1) = 0x8000 (wrap)
✅ ROR(0xABCD, 8) = 0xCDAB (byte swap)
✅ ROR(0xFF00, 4) = 0x0FF0
```

---

## 1.4 Sammenlignings Operatorer

### Test 1.4.1: Equal (=)

**CLI Kommandoer (Copy/Paste):**
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

**Test Cases:**
```bash
# Test 1: 5 = 5 → TRUE
write reg 20 value int 5
write reg 21 value int 5
read coil 0
# Forventet: 1

# Test 2: 5 = 3 → FALSE
write reg 20 value int 5
write reg 21 value int 3
read coil 0
# Forventet: 0

# Test 3: 0 = 0 → TRUE
write reg 20 value int 0
write reg 21 value int 0
read coil 0
# Forventet: 1
```

**Forventet Resultat:**
```
✅ 5 = 5 → TRUE
✅ 5 = 3 → FALSE
✅ 0 = 0 → TRUE
```

---

### Test 1.4.2: Not Equal (<>)

**CLI Kommandoer (Copy/Paste):**
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
  result := (a <> b);
END_PROGRAM
END_UPLOAD
set logic 1 bind a reg:20 input
set logic 1 bind b reg:21 input
set logic 1 bind result coil:0 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: 5 <> 3 → TRUE
write reg 20 value int 5
write reg 21 value int 3
read coil 0
# Forventet: 1

# Test 2: 5 <> 5 → FALSE
write reg 20 value int 5
write reg 21 value int 5
read coil 0
# Forventet: 0
```

**Forventet Resultat:**
```
✅ 5 <> 3 → TRUE
✅ 5 <> 5 → FALSE
```

---

### Test 1.4.3: Less Than (<)

**CLI Kommandoer (Copy/Paste):**
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
  result := (a < b);
END_PROGRAM
END_UPLOAD
set logic 1 bind a reg:20 input
set logic 1 bind b reg:21 input
set logic 1 bind result coil:0 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: 3 < 5 → TRUE
write reg 20 value int 3
write reg 21 value int 5
read coil 0
# Forventet: 1

# Test 2: 5 < 3 → FALSE
write reg 20 value int 5
write reg 21 value int 3
read coil 0
# Forventet: 0

# Test 3: 5 < 5 → FALSE
write reg 20 value int 5
write reg 21 value int 5
read coil 0
# Forventet: 0
```

**Forventet Resultat:**
```
✅ 3 < 5 → TRUE
✅ 5 < 3 → FALSE
✅ 5 < 5 → FALSE
```

---

### Test 1.4.4: Greater Than (>)

**CLI Kommandoer (Copy/Paste):**
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
  result := (a > b);
END_PROGRAM
END_UPLOAD
set logic 1 bind a reg:20 input
set logic 1 bind b reg:21 input
set logic 1 bind result coil:0 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: 5 > 3 → TRUE
write reg 20 value int 5
write reg 21 value int 3
read coil 0
# Forventet: 1

# Test 2: 3 > 5 → FALSE
write reg 20 value int 3
write reg 21 value int 5
read coil 0
# Forventet: 0

# Test 3: 5 > 5 → FALSE
write reg 20 value int 5
write reg 21 value int 5
read coil 0
# Forventet: 0
```

**Forventet Resultat:**
```
✅ 5 > 3 → TRUE
✅ 3 > 5 → FALSE
✅ 5 > 5 → FALSE
```

---

### Test 1.4.5: Less or Equal (<=)

**CLI Kommandoer (Copy/Paste):**
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
  result := (a <= b);
END_PROGRAM
END_UPLOAD
set logic 1 bind a reg:20 input
set logic 1 bind b reg:21 input
set logic 1 bind result coil:0 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: 3 <= 5 → TRUE
write reg 20 value int 3
write reg 21 value int 5
read coil 0
# Forventet: 1

# Test 2: 5 <= 5 → TRUE
write reg 20 value int 5
write reg 21 value int 5
read coil 0
# Forventet: 1

# Test 3: 5 <= 3 → FALSE
write reg 20 value int 5
write reg 21 value int 3
read coil 0
# Forventet: 0
```

**Forventet Resultat:**
```
✅ 3 <= 5 → TRUE
✅ 5 <= 5 → TRUE
✅ 5 <= 3 → FALSE
```

---

### Test 1.4.6: Greater or Equal (>=)

**CLI Kommandoer (Copy/Paste):**
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
  result := (a >= b);
END_PROGRAM
END_UPLOAD
set logic 1 bind a reg:20 input
set logic 1 bind b reg:21 input
set logic 1 bind result coil:0 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: 5 >= 3 → TRUE
write reg 20 value int 5
write reg 21 value int 3
read coil 0
# Forventet: 1

# Test 2: 5 >= 5 → TRUE
write reg 20 value int 5
write reg 21 value int 5
read coil 0
# Forventet: 1

# Test 3: 3 >= 5 → FALSE
write reg 20 value int 3
write reg 21 value int 5
read coil 0
# Forventet: 0
```

**Forventet Resultat:**
```
✅ 5 >= 3 → TRUE
✅ 5 >= 5 → TRUE
✅ 3 >= 5 → FALSE
```

---

## 1.5 Builtin Funktioner - Matematiske

### Test 1.5.1: ABS (Absolut Værdi)

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  input: INT;
  output: INT;
END_VAR
BEGIN
  output := ABS(input);
END_PROGRAM
END_UPLOAD
set logic 1 bind input reg:20 input
set logic 1 bind output reg:21 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: ABS(-5) = 5
write reg 20 value int -5
read reg 21 int
# Forventet: 5

# Test 2: ABS(10) = 10
write reg 20 value int 10
read reg 21 int
# Forventet: 10

# Test 3: ABS(0) = 0
write reg 20 value int 0
read reg 21 int
# Forventet: 0
```

**Forventet Resultat:**
```
✅ ABS(-5) = 5
✅ ABS(10) = 10
✅ ABS(0) = 0
```

---

### Test 1.5.2: MIN (Minimum)

**CLI Kommandoer (Copy/Paste):**
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

**Test Cases:**
```bash
# Test 1: MIN(5, 3) = 3
write reg 20 value int 5
write reg 21 value int 3
read reg 22 int
# Forventet: 3

# Test 2: MIN(-10, 5) = -10
write reg 20 value int -10
write reg 21 value int 5
read reg 22 int
# Forventet: -10

# Test 3: MIN(7, 7) = 7
write reg 20 value int 7
write reg 21 value int 7
read reg 22 int
# Forventet: 7
```

**Forventet Resultat:**
```
✅ MIN(5, 3) = 3
✅ MIN(-10, 5) = -10
✅ MIN(7, 7) = 7
```

---

### Test 1.5.3: MAX (Maximum)

**CLI Kommandoer (Copy/Paste):**
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
  result := MAX(a, b);
END_PROGRAM
END_UPLOAD
set logic 1 bind a reg:20 input
set logic 1 bind b reg:21 input
set logic 1 bind result reg:22 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: MAX(5, 3) = 5
write reg 20 value int 5
write reg 21 value int 3
read reg 22 int
# Forventet: 5

# Test 2: MAX(-10, 5) = 5
write reg 20 value int -10
write reg 21 value int 5
read reg 22 int
# Forventet: 5

# Test 3: MAX(7, 7) = 7
write reg 20 value int 7
write reg 21 value int 7
read reg 22 int
# Forventet: 7
```

**Forventet Resultat:**
```
✅ MAX(5, 3) = 5
✅ MAX(-10, 5) = 5
✅ MAX(7, 7) = 7
```

---

### Test 1.5.4: SQRT (Kvadratrod)

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  input: REAL;
  output: REAL;
END_VAR
BEGIN
  output := SQRT(input);
END_PROGRAM
END_UPLOAD
set logic 1 bind input reg:20 input
set logic 1 bind output reg:22 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: SQRT(16.0) = 4.0
write reg 20 value real 16.0
read reg 22 real
# Forventet: 4.000000 (0x40800000)

# Test 2: SQRT(2.0) ≈ 1.414
write reg 20 value real 2.0
read reg 22 real
# Forventet: 1.414214 (0x3FB504F3)

# Test 3: SQRT(9.0) = 3.0
write reg 20 value real 9.0
read reg 22 real
# Forventet: 3.000000 (0x40400000)

# Test 4: SQRT(0.0) = 0.0
write reg 20 value real 0.0
read reg 22 real
# Forventet: 0.000000 (0x00000000)
```

**Forventet Resultat:**
```
✅ SQRT(16.0) = 4.000000
✅ SQRT(2.0) ≈ 1.414214
✅ SQRT(9.0) = 3.000000
✅ SQRT(0.0) = 0.000000
```

---

### Test 1.5.5: ROUND (Afrunding)

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  input: REAL;
  output: INT;
END_VAR
BEGIN
  output := ROUND(input);
END_PROGRAM
END_UPLOAD
set logic 1 bind input reg:20 input
set logic 1 bind output reg:22 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: ROUND(3.4) = 3
write reg 20 value real 3.4
read reg 22 int
# Forventet: 3

# Test 2: ROUND(3.6) = 4
write reg 20 value real 3.6
read reg 22 int
# Forventet: 4

# Test 3: ROUND(3.5) = 4 (banker's rounding eller 3?)
write reg 20 value real 3.5
read reg 22 int
# Forventet: 4 (or 3 depending on implementation)

# Test 4: ROUND(-2.7) = -3
write reg 20 value real -2.7
read reg 22 int
# Forventet: -3
```

**Forventet Resultat:**
```
✅ ROUND(3.4) = 3
✅ ROUND(3.6) = 4
⚠️ ROUND(3.5) = ? (verify implementation)
✅ ROUND(-2.7) = -3
```

---

### Test 1.5.6: TRUNC (Afkort)

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  input: REAL;
  output: INT;
END_VAR
BEGIN
  output := TRUNC(input);
END_PROGRAM
END_UPLOAD
set logic 1 bind input reg:20 input
set logic 1 bind output reg:22 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: TRUNC(3.9) = 3
write reg 20 value real 3.9
read reg 22 int
# Forventet: 3

# Test 2: TRUNC(-3.9) = -3
write reg 20 value real -3.9
read reg 22 int
# Forventet: -3

# Test 3: TRUNC(5.1) = 5
write reg 20 value real 5.1
read reg 22 int
# Forventet: 5
```

**Forventet Resultat:**
```
✅ TRUNC(3.9) = 3
✅ TRUNC(-3.9) = -3
✅ TRUNC(5.1) = 5
```

---

### Test 1.5.7: FLOOR (Gulv)

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  input: REAL;
  output: INT;
END_VAR
BEGIN
  output := FLOOR(input);
END_PROGRAM
END_UPLOAD
set logic 1 bind input reg:20 input
set logic 1 bind output reg:22 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: FLOOR(3.9) = 3
write reg 20 value real 3.9
read reg 22 int
# Forventet: 3

# Test 2: FLOOR(-3.1) = -4
write reg 20 value real -3.1
read reg 22 int
# Forventet: -4

# Test 3: FLOOR(5.0) = 5
write reg 20 value real 5.0
read reg 22 int
# Forventet: 5
```

**Forventet Resultat:**
```
✅ FLOOR(3.9) = 3
✅ FLOOR(-3.1) = -4
✅ FLOOR(5.0) = 5
```

---

### Test 1.5.8: CEIL (Loft)

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  input: REAL;
  output: INT;
END_VAR
BEGIN
  output := CEIL(input);
END_PROGRAM
END_UPLOAD
set logic 1 bind input reg:20 input
set logic 1 bind output reg:22 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: CEIL(3.1) = 4
write reg 20 value real 3.1
read reg 22 int
# Forventet: 4

# Test 2: CEIL(-3.9) = -3
write reg 20 value real -3.9
read reg 22 int
# Forventet: -3

# Test 3: CEIL(5.0) = 5
write reg 20 value real 5.0
read reg 22 int
# Forventet: 5
```

**Forventet Resultat:**
```
✅ CEIL(3.1) = 4
✅ CEIL(-3.9) = -3
✅ CEIL(5.0) = 5
```

---

### Test 1.5.9: SUM (Sum)

**CLI Kommandoer (Copy/Paste):**
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
  result := SUM(a, b);
END_PROGRAM
END_UPLOAD
set logic 1 bind a reg:20 input
set logic 1 bind b reg:21 input
set logic 1 bind result reg:22 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: SUM(5, 3) = 8
write reg 20 value int 5
write reg 21 value int 3
read reg 22 int
# Forventet: 8

# Test 2: SUM(-10, 15) = 5
write reg 20 value int -10
write reg 21 value int 15
read reg 22 int
# Forventet: 5
```

**Forventet Resultat:**
```
✅ SUM(5, 3) = 8
✅ SUM(-10, 15) = 5
```

---

## 1.6 Builtin Funktioner - Clamping/Selection

### Test 1.6.1: LIMIT (Begrænsning)

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  min_val: INT;
  value: INT;
  max_val: INT;
  result: INT;
END_VAR
BEGIN
  result := LIMIT(min_val, value, max_val);
END_PROGRAM
END_UPLOAD
set logic 1 bind min_val reg:20 input
set logic 1 bind value reg:21 input
set logic 1 bind max_val reg:22 input
set logic 1 bind result reg:23 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: LIMIT(0, -10, 100) = 0 (clamped to min)
write reg 20 value int 0
write reg 21 value int -10
write reg 22 value int 100
read reg 23 int
# Forventet: 0

# Test 2: LIMIT(0, 150, 100) = 100 (clamped to max)
write reg 20 value int 0
write reg 21 value int 150
write reg 22 value int 100
read reg 23 int
# Forventet: 100

# Test 3: LIMIT(0, 50, 100) = 50 (no clamping)
write reg 20 value int 0
write reg 21 value int 50
write reg 22 value int 100
read reg 23 int
# Forventet: 50
```

**Forventet Resultat:**
```
✅ LIMIT(0, -10, 100) = 0
✅ LIMIT(0, 150, 100) = 100
✅ LIMIT(0, 50, 100) = 50
```

---

### Test 1.6.2: SEL (Selector)

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  selector: BOOL;
  value_false: INT;
  value_true: INT;
  result: INT;
END_VAR
BEGIN
  result := SEL(selector, value_false, value_true);
END_PROGRAM
END_UPLOAD
set logic 1 bind selector coil:0 input
set logic 1 bind value_false reg:20 input
set logic 1 bind value_true reg:21 input
set logic 1 bind result reg:22 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
write reg 20 value int 50
write reg 21 value int 75

# Test 1: SEL(FALSE, 50, 75) = 50
write coil 0 value 0
read reg 22 int
# Forventet: 50

# Test 2: SEL(TRUE, 50, 75) = 75
write coil 0 value 1
read reg 22 int
# Forventet: 75
```

**Forventet Resultat:**
```
✅ SEL(FALSE, 50, 75) = 50
✅ SEL(TRUE, 50, 75) = 75
```

---

### Test 1.6.3: MUX (4-Way Multiplexer)

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  selector: INT;
  in0: INT;
  in1: INT;
  in2: INT;
  result: INT;
END_VAR
BEGIN
  result := MUX(selector, in0, in1, in2);
END_PROGRAM
END_UPLOAD
set logic 1 bind selector reg:20 input
set logic 1 bind in0 reg:21 input
set logic 1 bind in1 reg:22 input
set logic 1 bind in2 reg:23 input
set logic 1 bind result reg:24 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Setup input values
write reg 21 value int 0
write reg 22 value int 18
write reg 23 value int 22

# Test 1: MUX(0, 0, 18, 22) = 0 (select IN0)
write reg 20 value int 0
read reg 24 int
# Forventet: 0

# Test 2: MUX(1, 0, 18, 22) = 18 (select IN1)
write reg 20 value int 1
read reg 24 int
# Forventet: 18

# Test 3: MUX(2, 0, 18, 22) = 22 (select IN2)
write reg 20 value int 2
read reg 24 int
# Forventet: 22

# Test 4: MUX(5, 0, 18, 22) = 0 (invalid selector → default IN0)
write reg 20 value int 5
read reg 24 int
# Forventet: 0

# Test 5: MUX(-1, 0, 18, 22) = 0 (negative selector → default IN0)
write reg 20 value int -1
read reg 24 int
# Forventet: 0
```

**Forventet Resultat:**
```
✅ MUX(0, 0, 18, 22) = 0
✅ MUX(1, 0, 18, 22) = 18
✅ MUX(2, 0, 18, 22) = 22
✅ MUX(5, 0, 18, 22) = 0 (default)
✅ MUX(-1, 0, 18, 22) = 0 (default)
```

---

## 1.7 Builtin Funktioner - Trigonometriske

### Test 1.7.1: SIN (Sinus)

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  angle: REAL;
  result: REAL;
END_VAR
BEGIN
  result := SIN(angle);
END_PROGRAM
END_UPLOAD
set logic 1 bind angle reg:20 input
set logic 1 bind result reg:22 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: SIN(0) = 0.0
write reg 20 value real 0.0
read reg 22 real
# Forventet: 0.000000 (0x00000000)

# Test 2: SIN(π/2) ≈ 1.0
write reg 20 value real 1.5708
read reg 22 real
# Forventet: 1.000000 (0x3F800000)

# Test 3: SIN(π) ≈ 0.0
write reg 20 value real 3.1416
read reg 22 real
# Forventet: ~0.000000 (meget tæt på 0)

# Test 4: SIN(π/6) = 0.5
write reg 20 value real 0.5236
read reg 22 real
# Forventet: 0.500000 (0x3F000000)
```

**Forventet Resultat:**
```
✅ SIN(0) = 0.000000
✅ SIN(π/2) ≈ 1.000000
✅ SIN(π) ≈ 0.000000
✅ SIN(π/6) ≈ 0.500000
```

---

### Test 1.7.2: COS (Cosinus)

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  angle: REAL;
  result: REAL;
END_VAR
BEGIN
  result := COS(angle);
END_PROGRAM
END_UPLOAD
set logic 1 bind angle reg:20 input
set logic 1 bind result reg:22 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: COS(0) = 1.0
write reg 20 value real 0.0
read reg 22 real
# Forventet: 1.000000 (0x3F800000)

# Test 2: COS(π/2) ≈ 0.0
write reg 20 value real 1.5708
read reg 22 real
# Forventet: ~0.000000 (meget tæt på 0)

# Test 3: COS(π) ≈ -1.0
write reg 20 value real 3.1416
read reg 22 real
# Forventet: -1.000000 (0xBF800000)
```

**Forventet Resultat:**
```
✅ COS(0) = 1.000000
✅ COS(π/2) ≈ 0.000000
✅ COS(π) ≈ -1.0
```

---

### Test 1.7.3: TAN (Tangens)

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  angle: REAL;
  result: REAL;
END_VAR
BEGIN
  result := TAN(angle);
END_PROGRAM
END_UPLOAD
set logic 1 bind angle reg:20 input
set logic 1 bind result reg:22 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: TAN(0) = 0.0
write reg 20 value real 0.0
read reg 22 real
# Forventet: 0.000000 (0x00000000)

# Test 2: TAN(π/4) ≈ 1.0
write reg 20 value real 0.7854
read reg 22 real
# Forventet: 1.000000 (0x3F800000)
```

**Forventet Resultat:**
```
✅ TAN(0) = 0.000000
✅ TAN(π/4) ≈ 1.000000
```

---

## 1.8 Builtin Funktioner - Type Conversion

### Test 1.8.1: INT_TO_REAL

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  int_val: INT;
  real_val: REAL;
END_VAR
BEGIN
  real_val := INT_TO_REAL(int_val);
END_PROGRAM
END_UPLOAD
set logic 1 bind int_val reg:20 input
set logic 1 bind real_val reg:21 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: INT_TO_REAL(10) = 10.0
write reg 20 value int 10
read reg 21 real
# Forventet: 10.000000 (0x41200000)

# Test 2: INT_TO_REAL(-5) = -5.0
write reg 20 value int -5
read reg 21 real
# Forventet: -5.000000 (0xC0A00000)
```

**Forventet Resultat:**
```
✅ INT_TO_REAL(10) = 10.0
✅ INT_TO_REAL(-5) = -5.0
```

---

### Test 1.8.2: REAL_TO_INT

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  real_val: REAL;
  int_val: INT;
END_VAR
BEGIN
  int_val := REAL_TO_INT(real_val);
END_PROGRAM
END_UPLOAD
set logic 1 bind real_val reg:20 input
set logic 1 bind int_val reg:22 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: REAL_TO_INT(10.7) = 10
write reg 20 value real 10.7
read reg 22 int
# Forventet: 10

# Test 2: REAL_TO_INT(-5.3) = -5
write reg 20 value real -5.3
read reg 22 int
# Forventet: -5
```

**Forventet Resultat:**
```
✅ REAL_TO_INT(10.7) = 10
✅ REAL_TO_INT(-5.3) = -5
```

---

### Test 1.8.3: BOOL_TO_INT

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  bool_val: BOOL;
  int_val: INT;
END_VAR
BEGIN
  int_val := BOOL_TO_INT(bool_val);
END_PROGRAM
END_UPLOAD
set logic 1 bind bool_val coil:0 input
set logic 1 bind int_val reg:20 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: BOOL_TO_INT(TRUE) = 1
write coil 0 value 1
read reg 20 int
# Forventet: 1

# Test 2: BOOL_TO_INT(FALSE) = 0
write coil 0 value 0
read reg 20 int
# Forventet: 0
```

**Forventet Resultat:**
```
✅ BOOL_TO_INT(TRUE) = 1
✅ BOOL_TO_INT(FALSE) = 0
```

---

### Test 1.8.4: INT_TO_BOOL

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  int_val: INT;
  bool_val: BOOL;
END_VAR
BEGIN
  bool_val := INT_TO_BOOL(int_val);
END_PROGRAM
END_UPLOAD
set logic 1 bind int_val reg:20 input
set logic 1 bind bool_val coil:0 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: INT_TO_BOOL(1) = TRUE
write reg 20 value int 1
read coil 0
# Forventet: 1

# Test 2: INT_TO_BOOL(0) = FALSE
write reg 20 value int 0
read coil 0
# Forventet: 0

# Test 3: INT_TO_BOOL(42) = TRUE (non-zero)
write reg 20 value int 42
read coil 0
# Forventet: 1
```

**Forventet Resultat:**
```
✅ INT_TO_BOOL(1) = TRUE
✅ INT_TO_BOOL(0) = FALSE
✅ INT_TO_BOOL(42) = TRUE
```

---

### Test 1.8.5: DWORD_TO_INT

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  dword_val: DWORD;
  int_val: INT;
END_VAR
BEGIN
  int_val := DWORD_TO_INT(dword_val);
END_PROGRAM
END_UPLOAD
set logic 1 bind dword_val reg:20 output
set logic 1 bind int_val reg:24 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: DWORD_TO_INT(1000) = 1000
write HR20-103 = 1000 (DWORD format)
read reg 24 uint
# Forventet: 1000
```

**Forventet Resultat:**
```
✅ DWORD_TO_INT(1000) = 1000
```

---

### Test 1.8.6: INT_TO_DWORD

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  int_val: INT;
  dword_val: DWORD;
END_VAR
BEGIN
  dword_val := INT_TO_DWORD(int_val);
END_PROGRAM
END_UPLOAD
set logic 1 bind int_val reg:20 input
set logic 1 bind dword_val reg:21 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: INT_TO_DWORD(1000) = 1000
write reg 20 value int 1000
read HR21-104 → Forventet: 1000 (DWORD format)
```

**Forventet Resultat:**
```
✅ INT_TO_DWORD(1000) = 1000
```

---

## 1.9 Builtin Funktioner - Persistence

**VIGTIGE NOTER:**
- SAVE() og LOAD() kræver 1 argument: `group_id` (0=alle grupper, 1-8=specifik gruppe)
- Return type: INT (ikke BOOL!)
  - `0` = Success
  - `-1` = Error (gruppe ikke fundet, NVS fejl, persistence disabled)
  - `-2` = Rate limited (max 1 SAVE per 5 sekunder)
- Der skal være mindst én persist gruppe defineret
- Persistence system skal være enabled

---

### Test 1.9.1: SAVE (Gem til NVS)

**Prerequisites (kør disse først):**
```bash
# Step 1: Opret en persist gruppe (hvis ikke allerede gjort)
set persist group test_gruppe add 100-105

# Step 2: Skriv nogle testværdier til gruppen
write reg 20 value uint 1234
write reg 21 value uint 5678
write reg 22 value uint 9999

# Step 3: Verificer at gruppen er oprettet
show persist
# Forventet: test_gruppe med start=100, count=6
```

**CLI Kommandoer (Copy/Paste):**
```bash
  set logic 1 delete
  set logic 1 upload
  PROGRAM test
  VAR
    trigger: BOOL;
    trigger_prev: BOOL;
    save_result: INT;
  END_VAR
  BEGIN
    IF trigger AND NOT trigger_prev THEN
      (* Rising edge - kun kald SAVE én gang *)
      save_result := SAVE(0);
    END_IF;
    trigger_prev := trigger;
  END_PROGRAM
  END_UPLOAD
  set logic 1 bind trigger coil:0 input
  set logic 1 bind save_result reg:40 output
  set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: Første SAVE (skal succeed)
write coil 0 value 1
read reg 40 int
# Forventet: 0 (success)
# Console output: "✓ SAVE(0) completed: 1 groups saved to NVS"

# Test 2: Nulstil trigger
write coil 0 value 0
read reg 40 int
# Forventet: stadig 0 (sidste result)

# Test 3: SAVE igen indenfor 5 sekunder (rate limited)
write coil 0 value 1
read reg 40 int
# Forventet: -2 (rate limited)
# Console output: "SAVE(0) rate limited (wait 5s between saves)"

# Test 4: Vent 5+ sekunder og prøv igen
# Wait 6 seconds...
write coil 0 value 0
write coil 0 value 1
read reg 40 int
# Forventet: 0 (success igen)
```

**Forventet Console Output (success):**
```
SAVE(0): Snapshotting register groups...
SAVE(0): Writing to NVS...
✓ SAVE(0) completed: 1 groups saved to NVS
```

**Forventet Console Output (rate limited):**
```
SAVE(0) rate limited (wait 5s between saves)
```

**Verificer at data er gemt:**
```bash
# Reboot ESP32 eller kør LOAD test for at verificere
```

---

### Test 1.9.2: LOAD (Læs fra NVS)

**Prerequisites (kør disse først):**
```bash
# Step 1: Gem data først (via SAVE test ovenfor eller CLI)
set persist save-group test_gruppe

# Step 2: Modificer register værdier (så vi kan se LOAD virker)
write reg 20 value uint 0
write reg 21 value uint 0
write reg 22 value uint 0

# Step 3: Verificer at værdier er ændret
read reg 20 3
# Forventet: alle 0
```

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  trigger: BOOL;
  load_result: INT;  (* Vigtigt: INT, ikke BOOL! *)
END_VAR
BEGIN
  IF trigger THEN
    load_result := LOAD(0);  (* 0 = load alle grupper *)
  END_IF;
END_PROGRAM
END_UPLOAD
set logic 1 bind trigger coil:0 input
set logic 1 bind load_result reg:40 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: Verificer at registers er reset (fra prerequisites)
read reg 20 3
# Forventet: 0, 0, 0

# Test 2: Trigger LOAD operation
write coil 0 value 1
read reg 40 int
# Forventet: 0 (success)
# Console output: "✓ LOAD(0) completed: 1 groups restored from NVS"

# Test 3: Verificer at registers er restored
read reg 20 3
# Forventet: 1234, 5678, 9999 (oprindelige værdier fra SAVE test)

# Test 4: LOAD uden saved data (error)
set persist delete-group test_gruppe
write coil 0 value 0
write coil 0 value 1
read reg 40 int
# Forventet: -1 (error - gruppe ikke fundet)
```

**Forventet Console Output (success):**
```
LOAD(0): Reading from NVS...
LOAD(0): Restoring register values...
✓ LOAD(0) completed: 1 groups restored from NVS
```

**Forventet Console Output (error):**
```
LOAD(0): Reading from NVS...
LOAD(0): Restoring register values...
LOAD(0) failed: Could not restore group
```

---

### Test 1.9.3: SAVE/LOAD Integration (Complete Workflow)

**Complete test scenario: Gem counter værdi ved shutdown, restore ved boot**

**Step 1: Setup persist gruppe:**
```bash
# Opret gruppe til counter data
set persist group counter_backup add 200-202
```

**Step 2: Upload ST program:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM counter_backup
VAR
  counter: INT;
  save_trigger: BOOL;
  load_trigger: BOOL;
  save_status: INT;
  load_status: INT;
END_VAR
BEGIN
  (* Increment counter *)
  counter := counter + 1;

  (* Save when triggered *)
  IF save_trigger THEN
    save_status := SAVE(0);
  END_IF;

  (* Load when triggered *)
  IF load_trigger THEN
    load_status := LOAD(0);
  END_IF;
END_PROGRAM
END_UPLOAD

set logic 1 bind counter reg:60 output
set logic 1 bind save_trigger coil:10 input
set logic 1 bind load_trigger coil:11 input
set logic 1 bind save_status reg:61 output
set logic 1 bind load_status reg:62 output
set logic 1 enabled:true
```

**Step 3: Test workflow:**
```bash
# 1. Lad counter køre i 2 sekunder (~200 cycles @ 10ms interval)
# Wait 2 seconds...
read reg 60 int
# Forventet: ~200

# 2. Gem counter værdi
write coil 10 value 1
read reg 61 int
# Forventet: 0 (success)

# 3. Nulstil trigger
write coil 10 value 0

# 4. Lad counter fortsætte
# Wait 2 seconds...
read reg 60 int
# Forventet: ~400

# 5. Reset counter til 0 (simulér restart)
set logic 1 enabled:false
write reg 60 value int 0
set logic 1 enabled:true

# 6. Verificer reset
read reg 60 int
# Forventet: ~1-10 (lige startet)

# 7. Load saved værdi
write coil 11 value 1
read reg 62 int
# Forventet: 0 (success)

# 8. Verificer at counter er restored
read reg 60 int
# Forventet: ~200 (originale gemte værdi + nye cycles)
```

**Forventet Resultat:**
```
✅ Counter værdi gemt korrekt
✅ Counter værdi restored efter "reboot"
✅ SAVE/LOAD workflow fungerer end-to-end
```

---

## 1.10 Builtin Funktioner - Timers (TON/TOF/TP)

**Formål:** Test af alle tre timer funktioner med millisekund præcision

**Registers brugt:**
- Coils 10-13: Timer inputs (trigger, enable)
- Coils 14-16: Timer outputs (Q status)
- HR 70-73: Timer preset times (PT) og elapsed time (ET)

---

### Test 1.10.1: TON - On-Delay Timer (Forsinket START)

**Formål:** Verificer at output går HIGH efter preset time når input er HIGH

**ST Program:**
```st
set logic 1 upload "
VAR
  trigger : BOOL;
  timer_done : BOOL;
  delay_time : INT;
END_VAR

delay_time := 2000;  (* 2 seconds delay *)
timer_done := TON(trigger, delay_time);
"
```

**Step 2: Bind variables:**
```bash
set logic 1 bind trigger coil:10 input
set logic 1 bind timer_done coil:14 output
set logic 1 bind delay_time reg:70 input
set logic 1 enabled:true
```

**Step 3: Test workflow:**
```bash
# 1. Initial state - timer inactive
read coil 14
# Forventet: FALSE (timer_done = FALSE)

# 2. Start timer (rising edge på input)
write coil 10 value 1
read coil 14
# Forventet: FALSE (timer running, not done yet)

# 3. Wait 1 second (halfway through delay)
# Wait 1 second...
read coil 14
# Forventet: FALSE (still waiting)

# 4. Wait another 1+ second (total >2 seconds)
# Wait 1.5 seconds...
read coil 14
# Forventet: TRUE (timer done, output HIGH)

# 5. Release trigger
write coil 10 value 0
read coil 14
# Forventet: FALSE (output goes LOW immediately)

# 6. Test dynamic delay change
write reg 70 value int 500
write coil 10 value 1
# Wait 0.6 seconds...
read coil 14
# Forventet: TRUE (500ms delay expired)
```

**Forventet Resultat:**
```
✅ Timer forbliver LOW under preset time
✅ Timer går HIGH når ET >= PT
✅ Timer resetter immediately når input går LOW
✅ Dynamisk PT ændring virker
```

---

### Test 1.10.2: TOF - Off-Delay Timer (Forsinket STOP)

**Formål:** Verificer at output forbliver HIGH i preset time efter input går LOW

**ST Program:**
```st
set logic 1 upload "
VAR
  presence : BOOL;
  light_on : BOOL;
  off_delay : INT;
END_VAR

off_delay := 3000;  (* 3 seconds delay *)
light_on := TOF(presence, off_delay);
"
```

**Step 2: Bind variables:**
```bash
set logic 1 bind presence coil:11 input
set logic 1 bind light_on coil:15 output
set logic 1 bind off_delay reg:71 input
set logic 1 enabled:true
```

**Step 3: Test workflow:**
```bash
# 1. Initial state
read coil 15
# Forventet: FALSE (no presence, light OFF)

# 2. Presence detected (input HIGH)
write coil 11 value 1
read coil 15
# Forventet: TRUE (light ON immediately)

# 3. Presence lost (falling edge)
write coil 11 value 0
read coil 15
# Forventet: TRUE (light still ON, timer running)

# 4. Wait 1.5 seconds (halfway through delay)
# Wait 1.5 seconds...
read coil 15
# Forventet: TRUE (still ON, timer not expired)

# 5. Wait another 2 seconds (total >3 seconds)
# Wait 2 seconds...
read coil 15
# Forventet: FALSE (timer expired, light OFF)

# 6. Test re-trigger before timeout
write coil 11 value 1
# Wait 0.1 seconds...
write coil 11 value 0
# Wait 1 second...
write coil 11 value 1  (* Re-trigger before timeout *)
read coil 15
# Forventet: TRUE (timer reset, light stays ON)
```

**Forventet Resultat:**
```
✅ Output går HIGH immediately når input goes HIGH
✅ Output forbliver HIGH under preset time efter input goes LOW
✅ Output går LOW når timer expires
✅ Re-trigger resetter timer korrekt
```

---

### Test 1.10.3: TP - Pulse Timer (Fast puls-bredde)

**Formål:** Verificer generering af fast-width pulse ved rising edge

**ST Program:**
```st
set logic 1 upload "
VAR
  button : BOOL;
  valve_pulse : BOOL;
  pulse_width : INT;
END_VAR

pulse_width := 1000;  (* 1 second pulse *)
valve_pulse := TP(button, pulse_width);
"
```

**Step 2: Bind variables:**
```bash
set logic 1 bind button coil:12 input
set logic 1 bind valve_pulse coil:16 output
set logic 1 bind pulse_width reg:72 input
set logic 1 enabled:true
```

**Step 3: Test workflow:**
```bash
# 1. Initial state
read coil 16
# Forventet: FALSE (no pulse)

# 2. Trigger pulse (rising edge)
write coil 12 value 1
read coil 16
# Forventet: TRUE (pulse started)

# 3. Hold trigger HIGH (pulse continues)
# Wait 0.5 seconds...
read coil 16
# Forventet: TRUE (pulse still active)

# 4. Wait for pulse to complete
# Wait 0.6 seconds... (total >1 second)
read coil 16
# Forventet: FALSE (pulse completed)

# 5. Release trigger
write coil 12 value 0

# 6. Test non-retriggerable (trigger again while pulsing)
write coil 12 value 0  (* Ensure LOW *)
write coil 12 value 1  (* Rising edge 1 *)
# Wait 0.3 seconds...
write coil 12 value 0
write coil 12 value 1  (* Rising edge 2 - should be ignored *)
# Wait 0.8 seconds... (total 1.1 seconds from first trigger)
read coil 16
# Forventet: FALSE (pulse completed, not retriggered)

# 7. Test short pulse (100ms)
write reg 72 value int 100
write coil 12 value 0
# Wait 0.2 seconds...
write coil 12 value 1
# Wait 0.15 seconds...
read coil 16
# Forventet: FALSE (100ms pulse already done)
```

**Forventet Resultat:**
```
✅ Pulse starter på rising edge
✅ Pulse har fast bredde uanset input hold time
✅ Pulse er non-retriggerable
✅ Dynamisk pulse width ændring virker
```

---

### Test 1.10.4: TON Edge Cases - Negative PT & Zero PT

**Formål:** Verificer korrekt håndtering af negative og zero preset times

**ST Program:**
```st
set logic 1 upload "
VAR
  trigger : BOOL;
  output1 : BOOL;
  output2 : BOOL;
  negative_pt : INT;
  zero_pt : INT;
END_VAR

negative_pt := -1000;  (* Negative behandles som 0 *)
zero_pt := 0;          (* Zero = immediate *)

output1 := TON(trigger, negative_pt);
output2 := TON(trigger, zero_pt);
"
```

**Step 2: Bind variables:**
```bash
set logic 1 bind trigger coil:10 input
set logic 1 bind output1 coil:14 output
set logic 1 bind output2 coil:15 output
set logic 1 enabled:true
```

**Step 3: Test workflow:**
```bash
# Test negative PT (treated as 0)
write coil 10 value 1
read coil 14
read coil 15
# Forventet: BEGGE TRUE (0ms delay = immediate)

write coil 10 value 0
read coil 14
read coil 15
# Forventet: BEGGE FALSE
```

**Forventet Resultat:**
```
✅ Negative PT behandles som 0 (immediate)
✅ Zero PT giver immediate output
✅ Ingen crash eller undefined behavior
```

---

### Test 1.10.5: Timer Kombination - Sekvens med TON

**Formål:** Test multiple timers i sekvens (trafiklys-lignende)

**ST Program:**
```st
set logic 1 upload "
VAR
  start : BOOL;
  step1_done : BOOL;
  step2_done : BOOL;
  step3_done : BOOL;

  out_step1 : BOOL;
  out_step2 : BOOL;
  out_step3 : BOOL;
END_VAR

(* Step 1: 1 second *)
step1_done := TON(start, 1000);
out_step1 := start AND NOT step1_done;

(* Step 2: 0.5 seconds efter step 1 *)
step2_done := TON(step1_done, 500);
out_step2 := step1_done AND NOT step2_done;

(* Step 3: 2 seconds efter step 2 *)
step3_done := TON(step2_done, 2000);
out_step3 := step2_done AND NOT step3_done;
"
```

**Step 2: Bind variables:**
```bash
set logic 1 bind start coil:10 input
set logic 1 bind out_step1 coil:11 output
set logic 1 bind out_step2 coil:12 output
set logic 1 bind out_step3 coil:13 output
set logic 1 enabled:true
```

**Step 3: Test workflow:**
```bash
# Start sekvens
write coil 10 value 1

# Check step 1 (active 0-1 sec)
read coil 11 12 13
# Forventet: 11=TRUE, 12=FALSE, 13=FALSE

# Wait 1.2 seconds (step 1 done, step 2 active)
# Wait 1.2 seconds...
read coil 11 12 13
# Forventet: 11=FALSE, 12=TRUE, 13=FALSE

# Wait 0.6 seconds (step 2 done, step 3 active)
# Wait 0.6 seconds...
read coil 11 12 13
# Forventet: 11=FALSE, 12=FALSE, 13=TRUE

# Wait 2.2 seconds (all done)
# Wait 2.2 seconds...
read coil 11 12 13
# Forventet: 11=FALSE, 12=FALSE, 13=FALSE

# Total sekvens tid: ~3.5 sekunder
```

**Forventet Resultat:**
```
✅ Steps aktiveres i korrekt rækkefølge
✅ Timing mellem steps er korrekt
✅ Ingen overlap mellem steps
✅ Sekvens completes efter 3.5 sekunder
```

---

### Test 1.10.6: Alle Tre Timers Samtidig

**Formål:** Verificer at multiple timer instances kan køre parallelt

**ST Program:**
```st
set logic 1 upload "
VAR
  trig1 : BOOL;
  trig2 : BOOL;
  trig3 : BOOL;

  ton_out : BOOL;
  tof_out : BOOL;
  tp_out : BOOL;
END_VAR

ton_out := TON(trig1, 1000);
tof_out := TOF(trig2, 1500);
tp_out := TP(trig3, 800);
"
```

**Step 2: Bind variables:**
```bash
set logic 1 bind trig1 coil:10 input
set logic 1 bind trig2 coil:11 input
set logic 1 bind trig3 coil:12 input
set logic 1 bind ton_out coil:14 output
set logic 1 bind tof_out coil:15 output
set logic 1 bind tp_out coil:16 output
set logic 1 enabled:true
```

**Step 3: Test workflow:**
```bash
# Start alle tre timers samtidig
write coil 10 value 1  (* TON start *)
write coil 11 value 1  (* TOF immediate HIGH *)
write coil 12 value 1  (* TP pulse start *)

# Check initial state
read coil 14 15 16
# Forventet: 14=FALSE (TON waiting), 15=TRUE (TOF immediate), 16=TRUE (TP pulsing)

# Wait 0.5 seconds
# Wait 0.5 seconds...
read coil 14 15 16
# Forventet: 14=FALSE, 15=TRUE, 16=TRUE

# Release TOF trigger
write coil 11 value 0

# Wait 0.5 seconds (TON done, TP done, TOF still HIGH)
# Wait 0.5 seconds...
read coil 14 15 16
# Forventet: 14=TRUE (1 sec total), 15=TRUE (TOF delay running), 16=FALSE (800ms expired)

# Wait 1 second (TOF expires)
# Wait 1 second...
read coil 14 15 16
# Forventet: 14=TRUE, 15=FALSE (1.5 sec expired), 16=FALSE
```

**Forventet Resultat:**
```
✅ Alle tre timer typer kan køre samtidig
✅ Hver timer har independent timing
✅ Ingen interference mellem timers
✅ Max 8 timer instances per program respekteres
```

---

## 1.11 GPIO & Hardware Tests

**Hardware Required:** LEDs på GPIO17-21, switches på GPIO32-34, 1kHz signal på GPIO25

### Test 1.10.1: LED Control via GPIO Output (Coils)

**Hardware:** LED på GPIO17

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  led_state: BOOL;
  counter: INT;
END_VAR
BEGIN
  counter := counter + 1;
  IF counter >= 50 THEN led_state := NOT led_state;
  counter := 0;
  END_IF;
END_PROGRAM
END_UPLOAD
set logic 1 bind led_state coil:17 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: Verify LED blinks
# Expected: LED på GPIO17 blinker med ~1Hz (on 500ms, off 500ms)
# Visual verification: LED skal blinke synligt
```

**Forventet Resultat:**
```
✅ LED blinker med ~1Hz
✅ ST Logic kan styre GPIO output
✅ BOOL variable mapped til coil fungerer
```

---

### Test 1.10.2: Switch Input via GPIO Discrete Input

**Hardware:** Switch på GPIO32

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  switch_input: BOOL;
  led_output: BOOL;
  count: INT;
END_VAR
BEGIN
  led_output := switch_input;
  IF switch_input THEN count := count + 1;
  END_IF;
END_PROGRAM
END_UPLOAD
set logic 1 bind switch_input discrete:32
set logic 1 bind led_output coil:17 output
set logic 1 bind count reg:20 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: Press switch på GPIO32
# Expected: LED på GPIO17 tænder når switch er pressed
#           HR20 incrementerer (kan være meget hurtigt pga. 10ms loop)

# Test 2: Release switch
# Expected: LED slukker
```

**Forventet Resultat:**
```
✅ Switch input læses korrekt
✅ LED følger switch state
✅ Discrete input binding fungerer
⚠️ Count kan være høj pga. 10ms sampling (ikke debounced)
```

---

### Test 1.10.3: Multiple LED Sequence (Chaser Pattern)

**Hardware:** LEDs på GPIO17, 18, 19, 21

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  led1: BOOL;
  led2: BOOL;
  led3: BOOL;
  led4: BOOL;
  state: INT;
  counter: INT EXPORT;
END_VAR
BEGIN
  counter := counter + 1;
  IF counter >= 300 THEN 
    counter := 0;
  END_IF;
  state := state + 1;
  IF state > 3 THEN 
    state := 0;
  END_IF;
  led1 := (state = 0);
  led2 := (state = 1);
  led3 := (state = 2);
  led4 := (state = 3);
END_PROGRAM
END_UPLOAD
set logic 1 bind led1 coil:17 output
set logic 1 bind led2 coil:18 output
set logic 1 bind led3 coil:19 output
set logic 1 bind led4 coil:21 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: Visual verification
# Expected: LEDs lyser sekventielt:
#   LED1 (GPIO17) → LED2 (GPIO18) → LED3 (GPIO19) → LED4 (GPIO21) → repeat
# Hver LED er tændt i ~300ms
```

**Forventet Resultat:**
```
✅ LED chaser pattern fungerer
✅ Multiple coil outputs kan styres
✅ Sequential control logic fungerer
✅ Timing er korrekt (~300ms per step)
```

---

### Test 1.10.4: Frequency Counter med 1kHz Signal

**Hardware:** 1kHz signal på GPIO25

**Note:** Denne test kræver integration med counter engine, ikke kun ST Logic.

**Setup:**
```bash
# Configure counter 1 for GPIO25 (hardware pulse counter)
set counter 1 mode 1 hw-mode:hw edge:risingprescaler:16 start-value:30000 bit-width:16 direction:up hw-gpio:25 enable:on 
set counter 1 control running:on

# Counter 1 registers (default):
# HR20-101: Index (scaled value, 2 words for 32-bit)
# HR24-105: Raw (prescaled value)
# HR28: Frequency (Hz)
```

**Test Cases:**
```bash
# Manual test via CLI
show counter 1
# Expected: Frequency (HR28) reads ~1000 Hz

# Verify pulse counting
# HR24-105 (raw count) should increment steadily
# Rate: ~1000 counts per second
```

**Forventet Resultat:**
```
✅ Counter registrerer 1kHz signal
✅ Frequency measurement er ~1000 Hz (±5%)
✅ Raw count incrementerer korrekt
⚠️ Dette er primært en counter engine test, ikke ST Logic
```

---

## 1.11 Kontrolstrukturer - IF/THEN/ELSE

### Test 1.11.1: Simple IF/THEN

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  condition: BOOL;
  output: INT;
END_VAR
BEGIN
  IF condition THEN output := 100;
  END_IF;
END_PROGRAM
END_UPLOAD
set logic 1 bind condition coil:0 input
set logic 1 bind output reg:20 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: condition = TRUE
write coil 0 value 1
read reg 20 int
# Forventet: 100

# Test 2: condition = FALSE
write coil 0 value 0
read reg 20 int
# Forventet: 100
```

**Forventet Resultat:**
```
✅ IF TRUE THEN executes
✅ IF FALSE skips THEN block
```

---

### Test 1.11.2: IF/THEN/ELSE

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  condition: BOOL;
  output: INT;
END_VAR
BEGIN
  IF condition THEN output := 100;
  ELSE output := 200;
  END_IF;
END_PROGRAM
END_UPLOAD
set logic 1 bind condition coil:0 input
set logic 1 bind output reg:20 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: condition = TRUE
write coil 0 value 1
read reg 20 int
# Forventet: 100

# Test 2: condition = FALSE
write coil 0 value 0
read reg 20 int
# Forventet: 200
```

**Forventet Resultat:**
```
✅ IF TRUE → THEN branch
✅ IF FALSE → ELSE branch
```

---

### Test 1.11.3: IF/ELSIF/ELSE

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  value: INT;
  output: INT;
END_VAR
BEGIN
  IF value < 10 THEN output := 1;
  ELSIF value < 20 THEN output := 2;
  ELSE output := 3;
  END_IF;
END_PROGRAM
END_UPLOAD
set logic 1 bind value reg:20 input
set logic 1 bind output reg:21 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: value = 5 (< 10)
write reg 20 value int 5
read reg 21 int
# Forventet: 1

# Test 2: value = 15 (< 20 but not < 10)
write reg 20 value int 15
read reg 21 int
# Forventet: 2

# Test 3: value = 25 (>= 20)
write reg 20 value int 25
read reg 21 int
# Forventet: 3
```

**Forventet Resultat:**
```
✅ value < 10 → output = 1
✅ 10 <= value < 20 → output = 2
✅ value >= 20 → output = 3
```

---

## 1.12 Kontrolstrukturer - CASE

### Test 1.12.1: CASE Statement

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  selector: INT;
  output: INT;
END_VAR
BEGIN
  CASE selector OF 1: output := 100;
  2: output := 200;
  3: output := 300;
  ELSE output := 999;
  END_CASE;
END_PROGRAM
END_UPLOAD
set logic 1 bind selector reg:20 input
set logic 1 bind output reg:21 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: selector = 1
write reg 20 value int 1
read reg 21 int
# Forventet: 100

# Test 2: selector = 2
write reg 20 value int 2
read reg 21 int
# Forventet: 200

# Test 3: selector = 3
write reg 20 value int 3
read reg 21 int
# Forventet: 300

# Test 4: selector = 99 (no match)
write reg 20 value int 99
read reg 21 int
# Forventet: 999
```

**Forventet Resultat:**
```
✅ CASE 1 → output = 100
✅ CASE 2 → output = 200
✅ CASE 3 → output = 300
✅ CASE ELSE → output = 999
```

---

## 1.13 Kontrolstrukturer - FOR Loop

### Test 1.13.1: FOR Loop (Count to 10)

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  i: INT;
  sum: INT;
END_VAR
BEGIN
  sum := 0;
  FOR i := 1 TO 10 DO sum := sum + i;
  END_FOR;
END_PROGRAM
END_UPLOAD
set logic 1 bind sum reg:20 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: Sum 1+2+3+...+10 = 55
read reg 20 int
# Forventet: 55
```

**Forventet Resultat:**
```
✅ FOR loop executes 10 times
✅ sum = 1+2+3+...+10 = 55
```

---

### Test 1.13.2: FOR Loop with BY step

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  i: INT;
  sum: INT;
END_VAR
BEGIN
  sum := 0;
  FOR i := 0 TO 10 BY 2 DO sum := sum + i;
  END_FOR;
END_PROGRAM
END_UPLOAD
set logic 1 bind sum reg:20 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: Sum 0+2+4+6+8+10 = 30
read reg 20 int
# Forventet: 30
```

**Forventet Resultat:**
```
✅ FOR loop with BY 2 step
✅ sum = 0+2+4+6+8+10 = 30
```

---

## 1.14 Kontrolstrukturer - WHILE Loop

### Test 1.14.1: WHILE Loop (Count to 10)

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  counter: INT;
  sum: INT;
END_VAR
BEGIN
  counter := 1;
  sum := 0;
  WHILE counter <= 10 DO sum := sum + counter;
  counter := counter + 1;
  END_WHILE;
END_PROGRAM
END_UPLOAD
set logic 1 bind sum reg:20 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: Sum 1+2+3+...+10 = 55
read reg 20 int
# Forventet: 55
```

**Forventet Resultat:**
```
✅ WHILE loop executes while condition true
✅ sum = 1+2+3+...+10 = 55
```

---

## 1.15 Kontrolstrukturer - REPEAT Loop

### Test 1.15.1: REPEAT/UNTIL Loop

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  counter: INT;
  sum: INT;
END_VAR
BEGIN
  counter := 1;
  sum := 0;
  REPEAT sum := sum + counter;
  counter := counter + 1;
  UNTIL counter > 10;
END_PROGRAM
END_UPLOAD
set logic 1 bind sum reg:20 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: Sum 1+2+3+...+10 = 55
read reg 20 int
# Forventet: 55
```

**Forventet Resultat:**
```
✅ REPEAT executes at least once
✅ sum = 1+2+3+...+10 = 55
```

---

## 1.16 Type System - INT vs DINT

### Test 1.16.1: INT Overflow Behavior (16-bit)

**CLI Kommandoer (Copy/Paste):**
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
# Test 1: INT overflow (32767 + 1 = -32768)
write reg 20 value int 32767
write reg 21 value int 1
read reg 22 int
# Forventet: -32768

# Test 2: INT underflow (-32768 - 1 = 32767)
write reg 20 value int -32768
write reg 21 value int -1
read reg 22 int
# Forventet: 32767

# Test 3: Normal operation (100 + 200 = 300)
write reg 20 value int 100
write reg 21 value int 200
read reg 22 int
# Forventet: 300
```

**Forventet Resultat:**
```
✅ INT is 16-bit: 32767 + 1 = -32768 (wrapping overflow)
✅ INT underflow: -32768 - 1 = 32767
✅ Normal arithmetic within range works correctly
```

---

### Test 1.16.2: DINT Large Values (32-bit)

**CLI Kommandoer (Copy/Paste):**
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

set logic 1 bind a reg:40 input
set logic 1 bind b reg:42 input
set logic 1 bind result reg:44 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: Large DINT values (100000 + 200000 = 300000)
write reg 40 value dint 100000
write reg 42 value dint 200000
# Læs resultat (2 registers)
read reg 44 2
# Forventet: HR44=4, HR45=37856 (LSW, MSW) = 300000

# Test 2: DINT negative values (-500000 + 100 = -499900)
write reg 40 value dint -500000
write reg 42 value dint 100
read reg 44 2
# Forventet: HR44=24416, HR45=65527 (LSW, MSW) = -499900

# Test 3: Simple DINT addition (1 + 3 = 4)
write reg 40 value dint 1
write reg 42 value dint 3
read reg 44 2
# Forventet: HR44=4, HR45=0 (LSW, MSW) = 4
```

**Forventet Resultat:**
```
✅ DINT uses 2 consecutive registers (LSW first, MSW second)
✅ DINT supports values beyond INT16 range (-32768 to 32767)
✅ Large positive values (100000 + 200000 = 300000)
✅ Negative DINT values work correctly (-500000 + 100 = -499900)
✅ Simple DINT addition works (1 + 3 = 4)
✅ CLI command 'write reg <addr> value dint <value>' automatically calculates LSW/MSW
```

---

### Test 1.16.3: INT → DINT Type Promotion

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  int_val: INT;
  dint_val: DINT;
  result: DINT;
END_VAR
BEGIN
  result := int_val + dint_val;
END_PROGRAM
END_UPLOAD

set logic 1 bind int_val reg:50 input
set logic 1 bind dint_val reg:52 input
set logic 1 bind result reg:54 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: INT (1000) + DINT (200000) = DINT (201000)
write reg 50 value int 1000
write reg 52 value dint 200000
read reg 54 2
# Forventet: HR54=4392, HR55=3 (LSW, MSW) = 201000

# Test 2: INT (-100) + DINT (50000) = DINT (49900)
write reg 50 value int -100
write reg 52 value dint 50000
read reg 54 2
# Forventet: HR54=49900, HR55=0 (LSW, MSW) = 49900
```

**Forventet Resultat:**
```
✅ INT automatically promoted to DINT when mixed with DINT
✅ Result uses DINT (32-bit) arithmetic
✅ Multi-register I/O works for DINT result (LSW first, MSW second)
✅ Positive INT + large DINT (1000 + 200000 = 201000)
✅ Negative INT + positive DINT (-100 + 50000 = 49900)
✅ CLI command 'write reg <addr> value dint <value>' simplifies testing
```

---

## 1.17 EXPORT Keyword & IR Pool Allocation

**Feature:** v5.1.0 - EXPORT keyword til variable visibility control og dynamic IR 220-251 pool allocation.

**Formål:**
- Verificer EXPORT keyword funktionalitet
- Test dynamic IR pool allocation
- Validér type-aware register allocation (INT=1 reg, REAL/DINT=2 regs)
- Verificer pool persistence across reboots
- Test pool exhaustion handling

### Test 1.17.1: Basic EXPORT - Single Variable

**Formål:** Verificer at EXPORT keyword mapper variable til IR 220-251

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  temp : INT EXPORT;      (* Should be visible in IR 220-251 *)
  internal : INT;         (* Should NOT be visible in IR *)
END_VAR
BEGIN
  temp := 42;
  internal := 99;
END_PROGRAM
END_UPLOAD

set logic 1 enabled:true
```

**Test Cases:**
```bash
# Vent 1 sekund for execution
delay 1000

# Læs IR 220-251 (kun temp skal være synlig)
read input-reg 220 1
# Forventet: IR220 = 42 (temp er EXPORT)

# Verificer at internal IKKE er synlig i IR 221
read input-reg 221 1
# Forventet: IR221 = 0 eller uændret (internal har INGEN EXPORT)

# Vis Logic 1 info (skal vise ir_pool_offset og ir_pool_size)
show logic 1
# Forventet output skal inkludere:
#   IR Pool: offset=0, size=1 (1 register allocated)
```

**Forventet Resultat:**
```
✅ temp (EXPORT) synlig i IR 220 = 42
✅ internal (no EXPORT) IKKE synlig i IR 221
✅ IR pool allocation: offset=0, size=1
✅ show logic 1 viser correct pool info
```

---

### Test 1.17.2: Multi-Register Types (REAL, DINT)

**Formål:** Verificer at REAL og DINT bruger 2 registre i IR pool

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  int_var : INT EXPORT;       (* 1 register *)
  real_var : REAL EXPORT;     (* 2 registers *)
  dint_var : DINT EXPORT;     (* 2 registers *)
END_VAR
BEGIN
  int_var := 100;
  real_var := 3.14;
  dint_var := 123456;
END_PROGRAM
END_UPLOAD

set logic 1 enabled:true
```

**Test Cases:**
```bash
# Vent 1 sekund
delay 1000

# Læs IR pool (5 registers total: 1 + 2 + 2)
read input-reg 220 5

# IR 220: int_var = 100 (1 register)
# IR 221-222: real_var = 3.14 (2 registers, LSW first)
# IR 223-224: dint_var = 123456 (2 registers, LSW first)

# Forventet:
# IR220 = 100
# IR221 = LSW of 3.14 (float bits)
# IR222 = MSW of 3.14 (float bits)
# IR223 = 57920 (LSW of 123456)
# IR224 = 1 (MSW of 123456)

# Verificer pool allocation
show logic 1
# Forventet: IR Pool: offset=0, size=5
```

**Forventet Resultat:**
```
✅ int_var uses 1 register (IR 220)
✅ real_var uses 2 registers (IR 221-222, LSW first)
✅ dint_var uses 2 registers (IR 223-224, LSW first)
✅ Total pool allocation: 5 registers
✅ show logic 1 confirms size=5
```

---

### Test 1.17.3: Dynamic Pool Allocation - Multiple Programs

**Formål:** Verificer at IR pool deles fleksibelt mellem programmer

**CLI Kommandoer (Copy/Paste):**
```bash
# Logic 1: 3 EXPORT vars (3 registers)
set logic 1 delete
set logic 1 upload
PROGRAM logic1
VAR
  var1 : INT EXPORT;
  var2 : INT EXPORT;
  var3 : INT EXPORT;
END_VAR
BEGIN
  var1 := 10;
  var2 := 20;
  var3 := 30;
END_PROGRAM
END_UPLOAD
set logic 1 enabled:true

# Logic 2: 2 EXPORT vars (4 registers - en DINT)
set logic 2 delete
set logic 2 upload
PROGRAM logic2
VAR
  var1 : INT EXPORT;
  var2 : DINT EXPORT;
END_VAR
BEGIN
  var1 := 40;
  var2 := 50000;
END_PROGRAM
END_UPLOAD
set logic 2 enabled:true

# Logic 3: 1 EXPORT var (2 registers - REAL)
set logic 3 delete
set logic 3 upload
PROGRAM logic3
VAR
  temp : REAL EXPORT;
END_VAR
BEGIN
  temp := 2.5;
END_PROGRAM
END_UPLOAD
set logic 3 enabled:true
```

**Test Cases:**
```bash
# Vent 1 sekund
delay 1000

# Læs alle IR 220-251 (total 9 registers used: 3+4+2)
read input-reg 220 9

# Logic 1: IR 220-222 (offset=0, size=3)
#   IR220 = 10, IR221 = 20, IR222 = 30
# Logic 2: IR 223-226 (offset=3, size=4)
#   IR223 = 40, IR224-225 = 50000 (DINT, LSW first)
# Logic 3: IR 226-227 (offset=7, size=2)
#   IR226-227 = 2.5 (REAL, LSW first)

# Verificer pool allocations
show logic 1
# Forventet: IR Pool: offset=0, size=3

show logic 2
# Forventet: IR Pool: offset=3, size=4

show logic 3
# Forventet: IR Pool: offset=7, size=2

# Total pool usage: 3+4+2 = 9/32 registers
```

**Forventet Resultat:**
```
✅ Logic 1: 3 registers (IR 220-222)
✅ Logic 2: 4 registers (IR 223-226)
✅ Logic 3: 2 registers (IR 227-228)
✅ No overlap between programs
✅ Total pool usage: 9/32 registers
✅ Each program shows correct offset and size
```

---

### Test 1.17.4: Pool Reallocation After Delete

**Formål:** Verificer at pool compaction fungerer ved program sletning

**CLI Kommandoer (Copy/Paste):**
```bash
# Setup 3 programs
set logic 1 delete
set logic 1 upload
PROGRAM logic1
VAR
  a : INT EXPORT;
  b : INT EXPORT;
END_VAR
BEGIN
  a := 1;
  b := 2;
END_PROGRAM
END_UPLOAD
set logic 1 enabled:true

set logic 2 delete
set logic 2 upload
PROGRAM logic2
VAR
  c : INT EXPORT;
  d : INT EXPORT;
END_VAR
BEGIN
  c := 3;
  d := 4;
END_PROGRAM
END_UPLOAD
set logic 2 enabled:true

set logic 3 delete
set logic 3 upload
PROGRAM logic3
VAR
  e : INT EXPORT;
  f : INT EXPORT;
END_VAR
BEGIN
  e := 5;
  f := 6;
END_PROGRAM
END_UPLOAD
set logic 3 enabled:true

# Verificer initial allocation
show logic 1
# Forventet: IR Pool: offset=0, size=2
show logic 2
# Forventet: IR Pool: offset=2, size=2
show logic 3
# Forventet: IR Pool: offset=4, size=2

# Slet Logic 2 (midterste program)
set logic 2 delete

# Verificer at Logic 1 og Logic 3 stadig virker
show logic 1
# Forventet: IR Pool: offset=0, size=2
show logic 3
# Forventet: IR Pool: offset=4, size=2 (eller måske compacted til offset=2)

# Læs IR for at verificere
read input-reg 220 6
# IR220-221: Logic1 (1, 2)
# IR222-223: Muligvis 0 (Logic2 slettet)
# IR224-225: Logic3 (5, 6) eller compacted til IR222-223
```

**Forventet Resultat:**
```
✅ Logic 2 deleted successfully
✅ Logic 1 still functional (IR 220-221)
✅ Logic 3 still functional
✅ Pool may be compacted or fragmented (implementation choice)
✅ No crashes or corruption
```

---

### Test 1.17.5: Pool Exhaustion Handling

**Formål:** Test hvad der sker når IR pool er fuld (32 regs max)

**CLI Kommandoer (Copy/Paste):**
```bash
# Logic 1: Alloker 15 REAL variables (30 registers)
set logic 1 delete
set logic 1 upload
PROGRAM logic1
VAR
  v1 : REAL EXPORT;
  v2 : REAL EXPORT;
  v3 : REAL EXPORT;
  v4 : REAL EXPORT;
  v5 : REAL EXPORT;
  v6 : REAL EXPORT;
  v7 : REAL EXPORT;
  v8 : REAL EXPORT;
  v9 : REAL EXPORT;
  v10 : REAL EXPORT;
  v11 : REAL EXPORT;
  v12 : REAL EXPORT;
  v13 : REAL EXPORT;
  v14 : REAL EXPORT;
  v15 : REAL EXPORT;
END_VAR
BEGIN
  v1 := 1.0;
  v15 := 15.0;
END_PROGRAM
END_UPLOAD
set logic 1 enabled:true

# Verificer allocation
show logic 1
# Forventet: IR Pool: offset=0, size=30

# Forsøg at allokere Logic 2 med 2 REAL (4 registers - overstiger 32)
set logic 2 delete
set logic 2 upload
PROGRAM logic2
VAR
  extra1 : REAL EXPORT;
  extra2 : REAL EXPORT;
END_VAR
BEGIN
  extra1 := 99.0;
  extra2 := 88.0;
END_PROGRAM
END_UPLOAD
set logic 2 enabled:true

# Check om compilation fejler eller giver warning
show logic 2
# Forventet: Fejlbesked eller warning om pool exhaustion
```

**Forventet Resultat:**
```
✅ Logic 1 allocates 30 registers successfully
✅ Logic 2 compilation detects pool exhaustion
✅ Warning/error message: "IR pool exhausted" eller lignende
✅ Logic 2 kan ikke enables hvis pool er fuld
✅ System forbliver stabil (no crash)
```

**Cleanup:**
```bash
set logic 1 delete
set logic 2 delete
set logic 3 delete
```

---

## 1.19 TIME Literals (FEAT-006)

**Feature:** v5.2.0 - IEC 61131-3 TIME literal syntax for mere læsbar timer konfiguration.

**Formål:**
- Verificer TIME literal parsing i lexer
- Verificer korrekt konvertering til DINT millisekunder
- Verificer brug med timer funktioner (TON, TOF, TP)

---

### Test 1.19.1: Basis TIME Literal Parsing

**Formål:** Verificer at alle TIME units parses korrekt

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM time_test
VAR
  ms_val : DINT EXPORT;
  sec_val : DINT EXPORT;
  min_val : DINT EXPORT;
  hour_val : DINT EXPORT;
  day_val : DINT EXPORT;
END_VAR
BEGIN
  ms_val := T#100ms;
  sec_val := T#5s;
  min_val := T#2m;
  hour_val := T#1h;
  day_val := T#1d;
END_PROGRAM
END_UPLOAD

set logic 1 enabled:true
```

**Test Cases:**
```bash
# Vent 1 sekund
delay 1000

# Læs IR pool (10 registers: 5 DINT vars × 2 regs)
read input-reg 220 10 dint

# Forventede værdier (millisekunder):
# ms_val = 100
# sec_val = 5000
# min_val = 120000
# hour_val = 3600000
# day_val = 86400000
```

**Forventet Resultat:**
```
✅ T#100ms → 100
✅ T#5s → 5000
✅ T#2m → 120000
✅ T#1h → 3600000
✅ T#1d → 86400000
```

---

### Test 1.19.2: Kombinerede TIME Units

**Formål:** Verificer komplekse TIME literals med multiple units

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM time_combo
VAR
  combo1 : DINT EXPORT;
  combo2 : DINT EXPORT;
  combo3 : DINT EXPORT;
END_VAR
BEGIN
  combo1 := T#1h30m;
  combo2 := T#2m30s;
  combo3 := T#1h30m45s500ms;
END_PROGRAM
END_UPLOAD

set logic 1 enabled:true
```

**Test Cases:**
```bash
delay 1000
read input-reg 220 6 dint

# Forventede værdier:
# combo1 = 1h30m = 3600000 + 1800000 = 5400000 ms
# combo2 = 2m30s = 120000 + 30000 = 150000 ms
# combo3 = 1h30m45s500ms = 3600000 + 1800000 + 45000 + 500 = 5445500 ms
```

**Forventet Resultat:**
```
✅ T#1h30m → 5400000
✅ T#2m30s → 150000
✅ T#1h30m45s500ms → 5445500
```

---

### Test 1.19.3: TIME Literals med Timer Funktioner

**Formål:** Verificer praktisk brug af TIME literals med TON/TOF/TP

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM timer_time_test
VAR
  enable : BOOL;
  ton_out : BOOL EXPORT;
  tof_out : BOOL EXPORT;
  tp_out : BOOL EXPORT;
END_VAR
BEGIN
  enable := TRUE;
  ton_out := TON(enable, T#500ms);
  tof_out := TOF(enable, T#1s);
  tp_out := TP(enable, T#2s);
END_PROGRAM
END_UPLOAD

set logic 1 enabled:true
```

**Test Cases:**
```bash
# Timer funktionerne bruger TIME literal værdier direkte
# TON: 500ms on-delay
# TOF: 1000ms off-delay
# TP: 2000ms pulse

delay 600
read input-reg 220 3
# ton_out skulle være TRUE efter 500ms
# tp_out skulle være TRUE (inden for 2s pulse)

delay 2000
read input-reg 220 3
# tp_out skulle være FALSE (efter 2s pulse)
```

**Forventet Resultat:**
```
✅ TON(enable, T#500ms) aktiverer efter 500ms
✅ TP(enable, T#2s) giver 2 sekund pulse
✅ TIME literals fungerer som timer preset værdier
```

---

### Test 1.19.4: Case-Insensitivity og Edge Cases

**Formål:** Verificer case-insensitivity og grænsetilfælde

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM time_edge
VAR
  lower : DINT EXPORT;
  upper : DINT EXPORT;
  mixed : DINT EXPORT;
END_VAR
BEGIN
  lower := t#100ms;
  upper := T#100MS;
  mixed := T#1H30M;
END_PROGRAM
END_UPLOAD

set logic 1 enabled:true
```

**Test Cases:**
```bash
delay 1000
read input-reg 220 6 dint

# Alle tre skulle give samme resultat
# lower = upper = 100 (case insensitive)
# mixed = 5400000 (1h30m)
```

**Forventet Resultat:**
```
✅ t#100ms = T#100MS = 100 (case insensitive)
✅ T#1H30M = 5400000 (mixed case units)
```

---

**Cleanup:**
```bash
set logic 1 delete
```

---

# Fase 2: Kombinerede Tests

## Test 2.1: Aritmetik + Logik + Sammenligning

**Use Case:** PID-lignende controller (forenklet)

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  setpoint: INT;
  actual: INT;
  error: INT;
  kp: INT;
  output: INT;
  output_limited: INT;
END_VAR
BEGIN
  error := setpoint - actual;
  output := kp * error;
  output_limited := LIMIT(0, output, 100);
END_PROGRAM
END_UPLOAD
set logic 1 bind setpoint reg:20 input
set logic 1 bind actual reg:21 input
set logic 1 bind kp reg:22 input
set logic 1 bind output_limited reg:23 output
set logic 1 enabled:true
write reg 22 value int 2

```

**Test Cases:**
```bash
# Test 1: setpoint=80, actual=70 → error=10, output=20
write reg 20 value int 80
write reg 21 value int 70
read reg 23 int
# Forventet: 20

# Test 2: setpoint=50, actual=60 → error=-10, output=-20 (limited to 0)
write reg 20 value int 50
write reg 21 value int 60
read reg 23 int
# Forventet: 0

# Test 3: setpoint=100, actual=50 → error=50, output=100
write reg 20 value int 100
write reg 21 value int 50
read reg 23 int
# Forventet: 100

# Test 4: setpoint=100, actual=0 → error=100, output=200 (limited to 100)
write reg 20 value int 100
write reg 21 value int 0
read reg 23 int
# Forventet: 100
```

**Forventet Resultat:**
```
✅ Kombineret aritmetik, multiplikation, og LIMIT
✅ Alle 4 test cases bestået
```

---

## Test 2.2: Nested IF + Multiple Conditions

**Use Case:** Multi-level alarm system

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  temperature: INT;
  pressure: INT;
  alarm_level: INT;
END_VAR
BEGIN
  alarm_level := 0;
  IF temperature > 100 THEN IF pressure > 50 THEN alarm_level := 3;
  ELSE alarm_level := 2;
  END_IF;
  ELSIF temperature > 80 THEN alarm_level := 1;
  ELSE alarm_level := 0;
  END_IF;
END_PROGRAM
END_UPLOAD
set logic 1 bind temperature reg:20 input
set logic 1 bind pressure reg:21 input
set logic 1 bind alarm_level reg:22 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: temp=70, press=30 → Normal
write reg 20 value int 70
write reg 21 value int 30
read reg 22 int
# Forventet: 0

# Test 2: temp=90, press=30 → Warning
write reg 20 value int 90
write reg 21 value int 30
read reg 22 int
# Forventet: 1

# Test 3: temp=110, press=40 → High temp
write reg 20 value int 110
write reg 21 value int 40
read reg 22 int
# Forventet: 2

# Test 4: temp=110, press=60 → Critical
write reg 20 value int 110
write reg 21 value int 60
read reg 22 int
# Forventet: 3
```

**Forventet Resultat:**
```
✅ Nested IF statements work correctly
✅ Alle alarm levels fungerer
```

---

## Test 2.3: Loops + Arrays (Simulation)

**Use Case:** Average calculation over time

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  value1: INT;
  value2: INT;
  value3: INT;
  value4: INT;
  sum: INT;
  average: INT;
END_VAR
BEGIN
  sum := value1 + value2 + value3 + value4;
  average := sum / 4;
END_PROGRAM
END_UPLOAD
set logic 1 bind value1 reg:20 input
set logic 1 bind value2 reg:21 input
set logic 1 bind value3 reg:22 input
set logic 1 bind value4 reg:23 input
set logic 1 bind average reg:24 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: Average of 10, 20, 30, 40 = 25
write reg 20 value int 10
write reg 21 value int 20
write reg 22 value int 30
write reg 23 value int 40
read reg 24 int
# Forventet: 25

# Test 2: Average of 0, 0, 0, 100 = 25
write reg 20 value int 0
write reg 21 value int 0
write reg 22 value int 0
write reg 23 value int 100
read reg 24 int
# Forventet: 25
```

**Forventet Resultat:**
```
✅ Multi-variable sum calculation
✅ Integer division average
```

---

## Test 2.4: CASE + Builtin Functions

**Use Case:** Mode-based calculation

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  mode: INT;
  input: INT;
  output: INT;
END_VAR
BEGIN
  CASE mode OF 1: output := ABS(input);
  2: output := input * 2;
  3: output := input / 2;
  4: output := MIN(input, 100);
  ELSE output := input;
  END_CASE;
END_PROGRAM
END_UPLOAD
set logic 1 bind mode reg:20 input
set logic 1 bind input reg:21 input
set logic 1 bind output reg:22 output
set logic 1 enabled:true
write reg 21 value int -50
```

**Test Cases:**
```bash
# Test 1: mode=1 (ABS)
write reg 20 value int 1
read reg 22 int
# Forventet: 50

# Test 2: mode=2 (multiply by 2)
write reg 20 value int 2
read reg 22 int
# Forventet: -100

# Test 3: mode=3 (divide by 2)
write reg 20 value int 3
read reg 22 int
# Forventet: -25

# Test 4: mode=4 (MIN with 100)
write reg 20 value int 4
read reg 22 int
# Forventet: -50

# Test 5: mode=99 (ELSE)
write reg 20 value int 99
read reg 22 int
# Forventet: -50
```

**Forventet Resultat:**
```
✅ CASE kombineret med builtin functions
✅ Alle modes fungerer korrekt
```

---

## Test 2.5: FOR Loop + Conditional Logic

**Use Case:** Count values in range

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  start_count: BOOL;
  threshold: INT;
  i: INT;
  count: INT;
END_VAR
BEGIN
  IF start_count THEN count := 0;
  FOR i := 1 TO 100 DO IF i < threshold THEN count := count + 1;
  END_IF;
  END_FOR;
  END_IF;
END_PROGRAM
END_UPLOAD
set logic 1 bind start_count coil:0 input
set logic 1 bind threshold reg:20 input
set logic 1 bind count reg:21 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: threshold=50, count values 1-49 = 49
write reg 20 value int 50
write coil 0 value 1
# Wait 100ms
read reg 21 int
# Forventet: 49

# Test 2: threshold=10, count values 1-9 = 9
write reg 20 value int 10
write coil 0 value 1
# Wait 100ms
read reg 21 int
# Forventet: 9
```

**Forventet Resultat:**
```
✅ FOR loop + nested IF
✅ Conditional counting fungerer
```

---

## Test 2.6: WHILE Loop + Math Functions

**Use Case:** Binary search (simplified)

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  target: INT;
  low: INT;
  high: INT;
  mid: INT;
  iterations: INT;
END_VAR
BEGIN
  low := 0;
  high := 100;
  iterations := 0;
  WHILE (high - low) > 1 DO mid := (low + high) / 2;
  IF mid < target THEN low := mid;
  ELSE high := mid;
  END_IF;
  iterations := iterations + 1;
  END_WHILE;
END_PROGRAM
END_UPLOAD
set logic 1 bind target reg:20 input
set logic 1 bind iterations reg:21 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: target=50
write reg 20 value int 50
# Wait 100ms
read HR21 → Forventet: ~6-7 iterations
```

**Forventet Resultat:**
```
✅ WHILE loop + arithmetic
✅ Binary search completes
```

---

## Test 2.7: Trigonometry + REAL Math

**Use Case:** Vector magnitude calculation

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  x: REAL;
  y: REAL;
  magnitude: REAL;
  x_squared: REAL;
  y_squared: REAL;
  sum_squares: REAL;
END_VAR
BEGIN
  x_squared := x * x;
  y_squared := y * y;
  sum_squares := x_squared + y_squared;
  magnitude := SQRT(sum_squares);
END_PROGRAM
END_UPLOAD
set logic 1 bind x reg:20 input
set logic 1 bind y reg:22 input
set logic 1 bind magnitude reg:24 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: magnitude(3.0, 4.0) = 5.0
write reg 20 value real 3.0
write reg 22 value real 4.0
read reg 24 real
# Forventet: 5.000000 (0x40A00000)

# Test 2: magnitude(5.0, 12.0) = 13.0
write reg 20 value real 5.0
write reg 22 value real 12.0
read reg 24 real
# Forventet: 13.000000 (0x41500000)
```

**Forventet Resultat:**
```
✅ REAL arithmetic fungerer
✅ SQRT beregning korrekt
✅ Pythagorean theorem verified
```

---

## Test 2.8: Type Conversion Chain

**Use Case:** Multi-step conversion

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  int_input: INT;
  real_temp: REAL;
  scaled: REAL;
  int_output: INT;
END_VAR
BEGIN
  real_temp := INT_TO_REAL(int_input);
  scaled := real_temp * 1.5;
  int_output := REAL_TO_INT(scaled);
END_PROGRAM
END_UPLOAD
set logic 1 bind int_input reg:20 input
set logic 1 bind int_output reg:21 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Test 1: 10 * 1.5 = 15
write reg 20 value int 10
read reg 21 uint
# Forventet: 15

# Test 2: 7 * 1.5 = 10.5 → 10
write reg 20 value int 7
read reg 21 uint
# Forventet: 10

# Test 3: 100 * 1.5 = 150
write reg 20 value int 100
read reg 21 uint
# Forventet: 150
```

**Forventet Resultat:**
```
✅ INT → REAL conversion
✅ REAL arithmetic
✅ REAL → INT conversion
```

---

## Test 2.9: Boolean Logic + SEL

**Use Case:** Mode selection with validation

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  auto_mode: BOOL;
  manual_override: BOOL;
  auto_value: INT;
  manual_value: INT;
  final_output: INT;
  mode_active: BOOL;
END_VAR
BEGIN
  mode_active := auto_mode AND NOT manual_override;
  final_output := SEL(mode_active, manual_value, auto_value);
END_PROGRAM
END_UPLOAD
set logic 1 bind auto_mode coil:0 input
set logic 1 bind manual_override coil:1 input
set logic 1 bind auto_value reg:20 input
set logic 1 bind manual_value reg:21 input
set logic 1 bind final_output reg:22 output
set logic 1 enabled:true
write reg 20 value int 75
write reg 21 value int 50
```

**Test Cases:**
```bash
# Test 1: auto=TRUE, override=FALSE → mode_active=TRUE → auto_value
write coil 0 value 1
read reg 22 int
# Forventet: 75

# Test 2: auto=TRUE, override=TRUE → mode_active=FALSE → manual_value
write coil 0 value 1
read reg 22 int
# Forventet: 50

# Test 3: auto=FALSE, override=FALSE → mode_active=FALSE → manual_value
write coil 0 value 0
read reg 22 int
# Forventet: 50
```

**Forventet Resultat:**
```
✅ Boolean logic kombineret med SEL
✅ Override funktionalitet fungerer
```

---

## Test 2.10: Complete System Test - Alle Features

**Use Case:** Mini production line controller

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  start_button: BOOL;
  stop_button: BOOL;
  sensor_count: INT;
  temperature: REAL;
  motor_running: BOOL;
  alarm: BOOL;
  production_count: INT;
  state: INT;
  temp_ok: BOOL;
  count_ok: BOOL;
  temp_int: INT;
END_VAR
BEGIN
  temp_int := REAL_TO_INT(temperature);
  temp_ok := (temp_int >= 20) AND (temp_int <= 80);
  count_ok := sensor_count < 1000;
  CASE state OF 0: motor_running := FALSE;
  IF start_button AND temp_ok AND count_ok THEN state := 1;
  END_IF;
  1: motor_running := TRUE;
  production_count := production_count + 1;
  IF stop_button OR NOT temp_ok OR NOT count_ok THEN state := 2;
  END_IF;
  2: motor_running := FALSE;
  state := 0;
  ELSE state := 0;
  END_CASE;
  alarm := NOT temp_ok OR NOT count_ok;
END_PROGRAM
END_UPLOAD

set logic 1 bind start_button coil:0 input
set logic 1 bind stop_button coil:1 input
set logic 1 bind sensor_count reg:20 input
set logic 1 bind temperature reg:21 input
set logic 1 bind motor_running coil:10 output
set logic 1 bind alarm coil:11 output
set logic 1 bind production_count reg:40 output
set logic 1 enabled:true
write reg 20 value int 50
write reg 21 value real 25.0
```

**Test Cases:**
```bash
# Test 1: Start sequence
write coil 0 value 1
read coil 10
# Forventet: 1 (motor running)
read coil 11
# Forventet: 0 (no alarm)
read reg 40 int
# Forventet: > 0 (production counting)

# Test 2: Stop sequence
write coil 0 value 0
read coil 10
# Forventet: 0 (motor stopped)

# Test 3: Temperature alarm
write reg 21 value real 90.0
write coil 0 value 1
read coil 10
# Forventet: 0 (motor not starting)
read coil 11
# Forventet: 1 (alarm active)

# Test 4: Count alarm
write reg 21 value real 25.0
write reg 20 value int 1500
read coil 11
# Forventet: 1 (alarm active)
```

**Forventet Resultat:**
```
✅ State machine fungerer
✅ Type conversion (REAL_TO_INT)
✅ Boolean logic (AND, NOT)
✅ Comparison operators (>=, <=, <)
✅ CASE statement
✅ IF/THEN logic
✅ Arithmetic (increment)
✅ All outputs korrekte
```

---

# Test Execution Workflow

## Forberedelse

1. **Hardware Setup:**
   - ESP32 connected via USB
   - Serial terminal @ 115200 baud
   - (Optional) Modbus Master device på UART1 for MB_* tests

2. **Software Tools:**
   - PlatformIO CLI eller IDE
   - Serial terminal (PuTTY, screen, etc.)
   - Python 3 + pymodbus (for automated tests)

3. **Pre-Test Checklist:**
   ```bash
   # Verify build number
   show version
   # Expected: Build #787+

   # Clear all logic programs
   set logic 1 delete
   set logic 2 delete
   set logic 3 delete
   set logic 4 delete

   # Reset registers if needed
   # (Manual via Modbus or reboot device)
   ```

---

## Execution Steps (Fase 1)

### Quick Test (Single Function)

1. **Upload program** (copy/paste CLI command)
2. **Bind variables** (copy/paste bind commands)
3. **Enable program**
4. **Write test inputs** (via Modbus eller CLI)
5. **Read test outputs** (verify expected values)
6. **Mark result** (✅ eller ❌ i checklist)
7. **Cleanup** (delete program for next test)

### Example (Test 1.1.1: Addition):
```bash
# 1. Upload
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

# 2. Bind
set logic 1 bind a reg:20 output
set logic 1 bind b reg:21 output
set logic 1 bind result reg:22 output

# 3. Enable
set logic 1 enabled:true

# 4. Test (via Modbus Python script)
client.write_register(100, 5)
client.write_register(101, 3)
time.sleep(0.1)
result = client.read_holding_registers(102, 1).registers[0]
assert result == 8, f"Expected 8, got {result}"
print("✅ Test 1.1.1 passed")

# 5. Cleanup
set logic 1 delete
```

---

## Execution Steps (Fase 2)

Fase 2 tests er mere komplekse og kræver:
- Længere ST-programmer
- Flere variable bindings
- Multiple test scenarios per program

Følg samme workflow som Fase 1, men forvent:
- Længere upload tid (større programmer)
- Mere tid til verify outputs
- Brug Python scripts for automation

---

# Fejlhåndtering

## Compilation Errors

**Symptom:** `set logic upload` returnerer fejl

**Actions:**
1. Kør `show logic 1` for at se compilation error details
2. Tjek syntax (mellemrum, semikolon, END_PROGRAM)
3. Verify variable types match operations
4. Tjek at builtin function names er korrekte (case-sensitive)

**Common Fixes:**
```bash
# Missing semicolon
❌ result := a + b END_PROGRAM
✅ result := a + b; END_PROGRAM

# Wrong type
❌ result: BOOL := 5 + 3;
✅ result: INT := 5 + 3;

# Unknown function
❌ output := ABSOLUTE(input);
✅ output := ABS(input);
```

---

## Runtime Errors

**Symptom:** Program compiles but outputs forkerte

**Actions:**
1. Verify variable bindings: `show logic 1`
2. Tjek Modbus register mapping
3. Verify input values er skrevet korrekt
4. Tjek for timing issues (wait mellem write/read)

**Common Fixes:**
```bash
# Wrong binding direction
❌ set logic 1 bind output reg:20  (default: input)
✅ set logic 1 bind output reg:20  (verify it's output in program)

# Timing issue
❌ write_register(); result = read_register();
✅ write_register(); time.sleep(0.1); result = read_register();
```

---

## Divergerende Resultater

**Symptom:** Test fungerer nogle gange, andre gange fejler

**Actions:**
1. Tjek for race conditions (variable opdateres i loop)
2. Verify cycle time er sufficient (`show logic 1`)
3. Increase sleep time mellem tests
4. Tjek for ST-program side effects (persistent variables)

---

## Test Automation Script Template

**BEMÆRK (v5.0.0):** Python automation scripts er ikke længere nødvendige!

Alle test cases er nu opdateret til **CLI kommandoer** der kan kopieres direkte til telnet session.

### CLI Commands for REAL/INT/UINT Values

Brug de indbyggede CLI kommandoer i stedet for Python helpers:

```bash
# SKRIVE VÆRDIER
write reg <addr> value int <value>     # 16-bit signed INT
write reg <addr> value uint <value>    # 16-bit unsigned
write reg <addr> value real <value>    # 32-bit REAL (2 registers)

# LÆSE VÆRDIER
read reg <addr> int                    # Læs som signed INT
read reg <addr> uint                   # Læs som unsigned
read reg <addr> real                   # Læs som REAL (2 registers)

# EKSEMPLER
write reg 20 value real 3.14159
read reg 22 real
write reg 20 value int -5
read reg 20 int
```

### Legacy Python Helpers (Deprecated)

**Python helpers er nu DEPRECATED** - brug CLI kommandoer i stedet.

Hvis du stadig ønsker at bruge Python automation:

```python
from pymodbus.client import ModbusSerialClient
import time
import struct

def write_real(client, address, value):
    """DEPRECATED: Brug 'write reg X value real Y' CLI kommando"""
    bytes_val = struct.pack('!f', value)
    words = struct.unpack('!HH', bytes_val)
    client.write_registers(address, list(words))

def read_real(client, address):
    """DEPRECATED: Brug 'read reg X real' CLI kommando"""
    result = client.read_holding_registers(address, 2)
    bytes_val = struct.pack('!HH', *result.registers)
    return struct.unpack('!f', bytes_val)[0]

def test_addition():
    """Test 1.1.1: Addition"""
    print("\n" + "="*60)
    print("TEST 1.1.1: Addition (+)")
    print("="*60)

    # Test cases
    tests = [
        (5, 3, 8, "5 + 3 = 8"),
        (-10, 15, 5, "-10 + 15 = 5"),
        (0, 0, 0, "0 + 0 = 0"),
    ]

    for a, b, expected, desc in tests:
        client.write_register(100, a)
        client.write_register(101, b)
        time.sleep(0.05)
        result = client.read_holding_registers(102, 1).registers[0]

        if result == expected:
            print(f"✅ {desc} → {result}")
        else:
            print(f"❌ {desc} → {result} (expected {expected})")

# ============================
# Main Test Runner
# ============================

def main():
    # Connect to ESP32
    client = ModbusSerialClient(
        port='COM3',  # Adjust to your port
        baudrate=115200,
        timeout=1
    )

    if not client.connect():
        print("❌ Connection failed!")
        return

    print("✅ Connected to ESP32")

    # Run tests
    test_addition()
    # ... add more tests

    # Cleanup
    client.close()
    print("\n✅ All tests completed!")

if __name__ == "__main__":
    main()
```

---

# Appendix: Quick Reference

## ST Operator Precedence (High to Low)

1. `()` - Parentheses
2. `NOT` - Logical NOT
3. `*`, `/`, `MOD` - Multiplicative
4. `+`, `-` - Additive
5. `<`, `>`, `<=`, `>=`, `=`, `<>` - Comparison
6. `AND` - Logical AND
7. `XOR` - Logical XOR
8. `OR` - Logical OR

## Builtin Function Summary

| Function | Args | Return | Description |
|----------|------|--------|-------------|
| ABS | 1 | INT/REAL | Absolute value |
| MIN | 2 | INT/REAL | Minimum |
| MAX | 2 | INT/REAL | Maximum |
| SUM | 2 | INT/REAL | Sum (alias for +) |
| SQRT | 1 | REAL | Square root |
| ROUND | 1 | INT | Round to nearest |
| TRUNC | 1 | INT | Truncate |
| FLOOR | 1 | INT | Floor |
| CEIL | 1 | INT | Ceiling |
| LIMIT | 3 | INT/REAL | Clamp to range |
| SEL | 3 | ANY | Boolean selector |
| SIN | 1 | REAL | Sine (radians) |
| COS | 1 | REAL | Cosine (radians) |
| TAN | 1 | REAL | Tangent (radians) |
| INT_TO_REAL | 1 | REAL | Type conversion |
| REAL_TO_INT | 1 | INT | Type conversion |
| BOOL_TO_INT | 1 | INT | Type conversion |
| INT_TO_BOOL | 1 | BOOL | Type conversion |
| DWORD_TO_INT | 1 | INT | Type conversion |
| INT_TO_DWORD | 1 | DWORD | Type conversion |
| SAVE | 0 | BOOL | Save persistent vars |
| LOAD | 0 | BOOL | Load persistent vars |
| MB_READ_COIL | 2 | BOOL | Read Modbus coil |
| MB_READ_INPUT | 2 | BOOL | Read discrete input |
| MB_READ_HOLDING | 2 | INT | Read holding register |
| MB_READ_INPUT_REG | 2 | INT | Read input register |
| MB_WRITE_COIL | 3 | BOOL | Write Modbus coil |
| MB_WRITE_HOLDING | 3 | BOOL | Write holding register |

---

# Udsatte Tests

## Modbus Master Funktioner

**Status:** 🔄 Udsat til senere test

**Funktioner der IKKE testes i denne omgang:**
- MB_READ_COIL(slave_id, address) → BOOL
- MB_READ_INPUT(slave_id, address) → BOOL
- MB_READ_HOLDING(slave_id, address) → INT
- MB_READ_INPUT_REG(slave_id, address) → INT
- MB_WRITE_COIL(slave_id, address, value) → BOOL
- MB_WRITE_HOLDING(slave_id, address, value) → BOOL

**Begrundelse:** Disse tests kræver en Modbus Slave device forbundet til UART1.

**Når klar til test:**
1. Forbind Modbus Slave device til UART1 (TX/RX pins)
2. Konfigurer slave device med kendt ID (f.eks. ID=1)
3. Setup test registers/coils på slave
4. Kør ST Logic programmer der kalder MB_* funktioner
5. Verify at værdier læses/skrives korrekt

**Estimeret testtid:** 5-10 minutter (når hardware er klar)

**Test eksempel:**
```structured-text
PROGRAM
VAR
  slave_id: INT;
  address: INT;
  read_value: INT;
END_VAR
BEGIN
  slave_id := 1;
  address := 100;
  read_value := MB_READ_HOLDING(slave_id, address);
END_PROGRAM
```

---

## 🔄 Counter & Timer Tests (Build #1074+)

### Counter Direction-Aware Frequency Test (BUG-184)

**Test C1: DOWN counting frequency measurement**
```
# Setup: Counter 1 med DOWN counting og start_value=1000
set counter 1 mode 1 parameter hw-mode:sw edge:rising direction:down bit-width:16 start-value:1000 input-dis:45 enable:on

# Start counter
set h-reg 110 1    # Start bit (ctrl-reg)

# Simuler 100 pulser/sekund (falling edge på DI 45)
# Vent 1 sekund

# Verificer frequency
show counter 1
# FORVENTET: freq = ~100 Hz (ikke 65000+ Hz som før fix)
```

**Test C2: UP counting frequency (regression test)**
```
# Setup: Counter 2 med UP counting
set counter 2 mode 1 parameter hw-mode:sw edge:rising direction:up bit-width:16 start-value:0 input-dis:46 enable:on

# Start counter og simuler pulser
# Verificer frequency = ~100 Hz (same as before)
```

**Test C3: DOWN counting underflow wrap**
```
# Setup: Counter med start_value=100, DOWN mode
set counter 1 mode 1 parameter hw-mode:hw edge:rising direction:down bit-width:16 start-value:100 hw-gpio:23 enable:on

# Tæl fra 100 ned til 0
# Ved 0→underflow: skal wrappe til 100, IKKE 65535

# FORVENTET: value sekvens: 100→99→...→1→0→100→99...
```

### Timer ctrl_reg Default Test (BUG-187)

**Test T1: Timer ctrl_reg auto-assign**
```
# Setup: Ny timer uden manuel ctrl_reg
set timer 1 mode 1 parameter phase1-duration:1000 phase2-duration:1000 phase3-duration:1000 output-coil:10 enable:on

# Verificer ctrl_reg er auto-assigned
show config
# FORVENTET: Timer 1 ctrl-reg = HR180 (ikke HR0)
```

**Test T2: Timer register overlap check**
```
# Setup: Verificer ingen overlap mellem timers
set timer 1 mode 3 parameter on-duration:500 off-duration:500 output-coil:10 enable:on
set timer 2 mode 3 parameter on-duration:500 off-duration:500 output-coil:11 enable:on

show config
# FORVENTET: Timer 1 → HR180, Timer 2 → HR185 (ingen overlap)
```

### Edge Case Tests

**Test E1: start_value=0 med DOWN counting**
```
# Setup: DOWN med start_value=0 (degenerate case)
set counter 1 mode 1 parameter direction:down start-value:0 bit-width:16 hw-mode:sw input-dis:45 enable:on

# FORVENTET: Counter forbliver på 0 (ingen counting muligt)
# Frequency = 0 Hz
```

**Test E2: 64-bit counter overflow**
```
# Setup: 64-bit counter med høj start_value
set counter 1 mode 1 parameter bit-width:64 start-value:18446744073709551600 direction:up hw-mode:sw input-dis:45 enable:on

# Tæl op 20 counts
# FORVENTET: Wraparound håndteres korrekt ved 2^64-1
```

**Test E3: Frequency ved counter reset**
```
# Setup: Counter med aktiv frequency measurement
set counter 1 mode 1 parameter direction:up hw-mode:sw input-dis:45 enable:on

# Tæl til 1000 Hz måling
reset counter 1
# Vent 1 sekund
show counter 1

# FORVENTET: Frequency = 0 Hz efter reset (ikke negative eller garbage)
```

### PCNT Hardware Tests (HW mode)

**Test H1: PCNT 16-bit signed→unsigned conversion**
```
# Setup: HW mode counter
set counter 1 mode 1 parameter hw-mode:hw edge:rising bit-width:16 hw-gpio:23 enable:on

# Tæl til 32768 (PCNT signed boundary)
# FORVENTET: Seamless transition 32767→32768→32769 (ingen reset)
```

**Test H2: PCNT BOTH edge counting**
```
# Setup: BOTH edges
set counter 1 mode 1 parameter hw-mode:hw edge:both hw-gpio:23 enable:on

# Send square wave
# FORVENTET: 2x counts per cycle (rising + falling)
```

---

## 1.20 ST Logic Debugger (FEAT-008)

**Feature:** v5.3.0 - Interactive debugging af ST Logic programmer med pause, single-step, breakpoints og variable inspection.

**Formål:**
- Verificer pause/continue/step/stop funktionalitet
- Verificer breakpoint add/remove/clear
- Verificer variable inspection når paused
- Verificer snapshot ved halt/error

**Dokumentation:** Se [ST_DEBUG_GUIDE.md](ST_DEBUG_GUIDE.md) for komplet guide.

---

### Test 1.20.1: Basis Pause og Step

**Formål:** Verificer at pause og single-step fungerer korrekt

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM debug_test
VAR
  counter : INT EXPORT;
  running : BOOL;
END_VAR
BEGIN
  running := TRUE;
  counter := counter + 1;
  IF counter > 100 THEN
    counter := 0;
  END_IF;
END_PROGRAM
END_UPLOAD

set logic 1 enabled:true
```

**Test Cases:**
```bash
# Vent lidt så programmet kører
delay 500

# Pause programmet
set logic 1 debug pause
# FORVENTET: "[OK] Logic1 debug: PAUSE requested"

# Vis debug state
show logic 1 debug
# FORVENTET: Mode: PAUSED, PC vises, Pause Reason: pause command

# Vis variable værdier
show logic 1 debug vars
# FORVENTET: counter = <nogen værdi>, running = TRUE

# Single-step én instruktion
set logic 1 debug step
show logic 1 debug
# FORVENTET: PC incrementeret, Mode: PAUSED

# Vis nye variable værdier
show logic 1 debug vars
# FORVENTET: Variabler opdateret

# Stop debugging
set logic 1 debug stop
# FORVENTET: "[OK] Logic1 debug: STOPPED"
```

**Forventet Resultat:**
- Pause sætter mode til PAUSED
- Step eksekverer én instruktion og pauser igen
- Variable inspection viser korrekte værdier
- Stop returnerer til normal execution

---

### Test 1.20.2: Breakpoints

**Formål:** Verificer breakpoint funktionalitet

**CLI Kommandoer (Copy/Paste):**
```bash
# Vis bytecode for at finde PC adresser
show logic 1 bytecode
# Note: Find PC for interessant instruktion

# Tilføj breakpoint ved PC=5
set logic 1 debug break 5
# FORVENTET: "[OK] Logic1: Breakpoint at PC=5 added"

# Vis breakpoints
show logic 1 debug
# FORVENTET: Breakpoints (1): [0] PC=5

# Start debugging
set logic 1 debug pause
set logic 1 debug continue
# FORVENTET: Program stopper ved PC=5

# Vent til breakpoint
delay 500
show logic 1 debug
# FORVENTET: Mode: PAUSED, Pause Reason: breakpoint, Hit Breakpoint PC: 5

# Fjern breakpoint
set logic 1 debug clear 5
# FORVENTET: "[OK] Logic1: Breakpoint at PC=5 removed"

# Continue igen - skal køre frit nu
set logic 1 debug continue
delay 100
set logic 1 debug pause
show logic 1 debug
# FORVENTET: PC ikke nødvendigvis 5 længere
```

**Forventet Resultat:**
- Breakpoint tilføjes og vises korrekt
- Program stopper ved breakpoint
- Breakpoint kan fjernes
- Continue kører videre

---

### Test 1.20.3: Multiple Breakpoints

**Formål:** Verificer at multiple breakpoints fungerer (max 8)

**CLI Kommandoer (Copy/Paste):**
```bash
# Tilføj 8 breakpoints
set logic 1 debug break 0
set logic 1 debug break 2
set logic 1 debug break 4
set logic 1 debug break 6
set logic 1 debug break 8
set logic 1 debug break 10
set logic 1 debug break 12
set logic 1 debug break 14
# FORVENTET: Alle 8 tilføjet

# Forsøg at tilføje en 9. breakpoint
set logic 1 debug break 16
# FORVENTET: "ERROR: Max breakpoints reached (8)"

# Vis alle breakpoints
show logic 1 debug
# FORVENTET: Breakpoints (8): [0-7] listet

# Clear alle
set logic 1 debug clear
# FORVENTET: "[OK] Logic1: All breakpoints cleared"

show logic 1 debug
# FORVENTET: Breakpoints (0)
```

**Forventet Resultat:**
- Max 8 breakpoints per program
- Error ved 9. breakpoint
- Clear fjerner alle

---

### Test 1.20.4: Error Snapshot

**Formål:** Verificer at runtime error gemmer snapshot

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM error_test
VAR
  x : INT EXPORT;
  y : INT;
END_VAR
BEGIN
  x := x + 1;
  y := 0;
  x := x / y;  // Division by zero!
END_PROGRAM
END_UPLOAD

set logic 1 enabled:true

# Start debugging
set logic 1 debug pause
set logic 1 debug continue

# Vent på error
delay 500

# Vis debug state
show logic 1 debug
# FORVENTET: Mode: PAUSED, Pause Reason: runtime error
# FORVENTET: Error message vises

# Vis variable værdier ved error
show logic 1 debug vars
# FORVENTET: x = 1, y = 0
```

**Forventet Resultat:**
- Division by zero trigger error
- Snapshot gemmes med REASON_ERROR
- Variable values tilgængelige efter error

---

### Test 1.20.5: Halt Snapshot

**Formål:** Verificer at program halt gemmer snapshot

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM halt_test
VAR
  x : INT EXPORT;
END_VAR
BEGIN
  x := 42;
  // Program halter naturligt ved end
END_PROGRAM
END_UPLOAD

set logic 1 enabled:true

# Start debugging
set logic 1 debug pause
set logic 1 debug continue

# Vent på halt (program er kort)
delay 500

show logic 1 debug
# FORVENTET: Mode: PAUSED, Pause Reason: program halt

show logic 1 debug vars
# FORVENTET: x = 42
```

**Forventet Resultat:**
- Program halter normalt
- Snapshot gemmes med REASON_HALT
- Final variable values tilgængelige

---

### Test 1.20.6: Debug Statistics

**Formål:** Verificer debug statistik

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM stats_test
VAR
  x : INT;
END_VAR
BEGIN
  x := x + 1;
END_PROGRAM
END_UPLOAD

set logic 1 enabled:true

# Start debugging og step flere gange
set logic 1 debug pause

# Step 5 gange
set logic 1 debug step
set logic 1 debug step
set logic 1 debug step
set logic 1 debug step
set logic 1 debug step

# Tilføj breakpoint og hit det
set logic 1 debug break 0
set logic 1 debug continue
delay 500

# Vis stats
show logic 1 debug
# FORVENTET: Total Steps Debugged: > 5
# FORVENTET: Breakpoints Hit: >= 1
```

**Forventet Resultat:**
- Total Steps Debugged tælles korrekt
- Breakpoints Hit tælles korrekt
- Stats kun tælles i debug mode (BUG-190 fix)

---

### Test 1.20.7: Debug Across Programs

**Formål:** Verificer at debug state er uafhængig per program

**CLI Kommandoer (Copy/Paste):**
```bash
# Setup program 1
set logic 1 delete
set logic 1 upload
PROGRAM prog1
VAR x : INT EXPORT; END_VAR
BEGIN x := 1; END_PROGRAM
END_UPLOAD
set logic 1 enabled:true

# Setup program 2
set logic 2 delete
set logic 2 upload
PROGRAM prog2
VAR y : INT EXPORT; END_VAR
BEGIN y := 2; END_PROGRAM
END_UPLOAD
set logic 2 enabled:true

# Pause kun program 1
set logic 1 debug pause

delay 500

# Vis begge states
show logic 1 debug
# FORVENTET: Mode: PAUSED

show logic 2 debug
# FORVENTET: Mode: OFF (ikke paused)

# Tilføj breakpoint på program 2
set logic 2 debug break 0
show logic 2 debug
# FORVENTET: Breakpoints (1): PC=0

# Cleanup
set logic 1 debug stop
set logic 2 debug clear
```

**Forventet Resultat:**
- Hvert program har uafhængig debug state
- Pause på ét program påvirker ikke andre
- Breakpoints er per-program

---

### Test 1.20.8: Invalid Commands

**Formål:** Verificer error handling for invalid debug commands

**CLI Kommandoer (Copy/Paste):**
```bash
# Ugyldigt program ID
set logic 5 debug pause
# FORVENTET: "ERROR: Invalid program ID 5"

# Break uden PC
set logic 1 debug break
# FORVENTET: "SET LOGIC DEBUG BREAK: missing PC address"

# Break på ikke-compiled program
set logic 4 delete
set logic 4 debug break 0
# FORVENTET: "ERROR: Program not compiled"

# Continue uden at være i debug mode
set logic 1 debug stop
set logic 1 debug continue
# FORVENTET: "ERROR: Debug mode is OFF"

# Break ud over instruction count
set logic 1 debug break 9999
# FORVENTET: "ERROR: PC 9999 out of range"
```

**Forventet Resultat:**
- Alle fejlmeddelelser vises korrekt
- Ingen crash eller undefined behavior

---

### Debug Test Cleanup

**CLI Kommandoer (Copy/Paste):**
```bash
set logic 1 debug stop
set logic 1 debug clear
set logic 2 debug stop
set logic 2 debug clear
set logic 1 delete
set logic 2 delete
```

---

**Version:** 1.2
**Last Updated:** 2026-01-19
**Status:** ✅ Ready for Execution (59+9+8 tests = 76 tests total)

**God test! 🚀**
