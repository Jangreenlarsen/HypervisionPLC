# ST Logic Test - GPIO & Hardware

**Version:** v6.0.0
**Formål:** Test af GPIO interaktion via ST Logic

---

## Hardware Setup

### GPIO Pin Configuration

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
| GPIO Pin | Signal | Frekvens |
|----------|--------|----------|
| GPIO25 | Square Wave | 1 kHz |

### Hardware Diagram

```
ESP32
┌─────────────────────────────────┐
│                                 │
│  GPIO17 ──────────────── LED1   │  Output Tests
│  GPIO18 ──────────────── LED2   │
│  GPIO19 ──────────────── LED3   │
│  GPIO21 ──────────────── LED4   │
│                                 │
│  GPIO32 ──────────────── SW1    │  Input Tests
│  GPIO33 ──────────────── SW2    │
│  GPIO34 ──────────────── SW3    │
│                                 │
│  GPIO25 ◄────── 1kHz Signal     │  Counter Tests
│                                 │
└─────────────────────────────────┘
```

---

## Test Oversigt

| Test | Beskrivelse | Prioritet |
|------|-------------|-----------|
| GPIO-001 | LED Output Control | HIGH |
| GPIO-002 | Switch Input Read | HIGH |
| GPIO-003 | Input→Output Logic | HIGH |
| GPIO-004 | GPIO Binding Update | MEDIUM |
| **Total** | **4** | |

---

# 1. GPIO Output Tests

## Test GPIO-001: LED Output Control

**Beskrivelse:** Kontrollér LEDs via ST Logic coil outputs.

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  led1: BOOL;
  led2: BOOL;
  led3: BOOL;
  led4: BOOL;
END_VAR
BEGIN
  (* LEDs kontrolleres via bindings *)
END_PROGRAM
END_UPLOAD

set logic 1 bind led1 coil:17 output
set logic 1 bind led2 coil:18 output
set logic 1 bind led3 coil:19 output
set logic 1 bind led4 coil:21 output
set logic 1 enabled:true
```

**Test Procedure:**
```bash
# Tænd LED1
write coil 17 value 1
# Verificer: LED1 lyser

# Tænd alle LEDs
write coil 17 value 1
write coil 18 value 1
write coil 19 value 1
write coil 21 value 1
# Verificer: Alle LEDs lyser

# Sluk alle LEDs
write coil 17 value 0
write coil 18 value 0
write coil 19 value 0
write coil 21 value 0
# Verificer: Alle LEDs slukket
```

| Test | Action | Forventet | Status |
|------|--------|-----------|--------|
| LED1 ON | write coil 17 = 1 | LED1 lyser | ⬜ |
| LED1 OFF | write coil 17 = 0 | LED1 slukket | ⬜ |
| All ON | alle coils = 1 | Alle lyser | ⬜ |
| All OFF | alle coils = 0 | Alle slukket | ⬜ |

---

# 2. GPIO Input Tests

## Test GPIO-002: Switch Input Read

**Beskrivelse:** Læs switch inputs via ST Logic discrete inputs.

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  sw1: BOOL;
  sw2: BOOL;
  sw3: BOOL;
  any_pressed: BOOL;
END_VAR
BEGIN
  any_pressed := sw1 OR sw2 OR sw3;
END_PROGRAM
END_UPLOAD

set logic 1 bind sw1 discrete:32 input
set logic 1 bind sw2 discrete:33 input
set logic 1 bind sw3 discrete:34 input
set logic 1 bind any_pressed coil:0 output
set logic 1 enabled:true
```

**Test Procedure:**
```bash
# 1. Ingen switches trykket
read coil 0
# Forventet: 0

# 2. Tryk SW1
read coil 0
# Forventet: 1

# 3. Slip SW1, tryk SW2
read coil 0
# Forventet: 1

# 4. Slip alle
read coil 0
# Forventet: 0
```

| Switch State | Forventet any_pressed | Status |
|--------------|----------------------|--------|
| None pressed | FALSE | ⬜ |
| SW1 pressed | TRUE | ⬜ |
| SW2 pressed | TRUE | ⬜ |
| SW3 pressed | TRUE | ⬜ |
| All pressed | TRUE | ⬜ |

---

# 3. Input→Output Logic Tests

## Test GPIO-003: Switch Controls LED

**Beskrivelse:** Switch input kontrollerer LED output direkte.

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  sw1: BOOL;
  sw2: BOOL;
  led1: BOOL;
  led2: BOOL;
END_VAR
BEGIN
  led1 := sw1;
  led2 := NOT sw2;  (* Inverteret *)
END_PROGRAM
END_UPLOAD

set logic 1 bind sw1 discrete:32 input
set logic 1 bind sw2 discrete:33 input
set logic 1 bind led1 coil:17 output
set logic 1 bind led2 coil:18 output
set logic 1 enabled:true
```

**Test Procedure:**
1. Tryk SW1 → LED1 tændes
2. Slip SW1 → LED1 slukkes
3. Tryk SW2 → LED2 slukkes (inverteret)
4. Slip SW2 → LED2 tændes

| SW1 | SW2 | LED1 | LED2 | Status |
|-----|-----|------|------|--------|
| OFF | OFF | OFF | ON | ⬜ |
| ON | OFF | ON | ON | ⬜ |
| OFF | ON | OFF | OFF | ⬜ |
| ON | ON | ON | OFF | ⬜ |

---

# 4. Binding Update Tests

## Test GPIO-004: Dynamic Binding Update

**Beskrivelse:** Test at bindings kan ændres runtime.

**CLI Kommandoer:**
```bash
# Start med LED1
set logic 1 bind output coil:17 output

# Verificer LED1 kontrolleres
write coil 17 value 1
# LED1 lyser

# Ændr binding til LED2
set logic 1 unbind output
set logic 1 bind output coil:18 output

# Verificer LED2 kontrolleres
write coil 18 value 1
# LED2 lyser
```

| Test | Forventet | Status |
|------|-----------|--------|
| Initial binding works | Yes | ⬜ |
| Unbind clears binding | Yes | ⬜ |
| New binding works | Yes | ⬜ |

---

## Test Results Summary

| Test ID | Beskrivelse | Status |
|---------|-------------|--------|
| GPIO-001 | LED Output Control | ⬜ |
| GPIO-002 | Switch Input Read | ⬜ |
| GPIO-003 | Input→Output Logic | ⬜ |
| GPIO-004 | Dynamic Binding | ⬜ |

---

**Tester:** _______________
**Dato:** _______________
