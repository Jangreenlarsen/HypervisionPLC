# ST Logic Test - Timer Funktioner

**Version:** v6.0.0
**Formål:** Test af TON/TOF/TP timer funktioner

---

## Test Oversigt

| Funktion | Tests | Prioritet |
|----------|-------|-----------|
| TON (On-Delay) | 2 | HIGH |
| TOF (Off-Delay) | 2 | HIGH |
| TP (Pulse) | 2 | MEDIUM |
| **Total** | **6** | |

---

# 1. TON (Timer On-Delay)

## Test 1.1: TON Basic Operation

**Beskrivelse:** TON aktiverer output efter PT tid når IN er TRUE.

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  start_button: BOOL;
  motor_on: BOOL;
  timer1: TON;
END_VAR
BEGIN
  timer1(IN := start_button, PT := T#2s);
  motor_on := timer1.Q;
END_PROGRAM
END_UPLOAD

set logic 1 bind start_button coil:0 input
set logic 1 bind motor_on coil:1 output
set logic 1 enabled:true
```

**Test Procedure:**
```bash
# 1. Start med FALSE
write coil 0 value 0
read coil 1
# Forventet: 0 (FALSE)

# 2. Sæt TRUE
write coil 0 value 1

# 3. Læs straks
read coil 1
# Forventet: 0 (stadig FALSE)

# 4. Vent 2+ sekunder, læs igen
# (vent manuelt)
read coil 1
# Forventet: 1 (TRUE efter 2s)

# 5. Sæt FALSE
write coil 0 value 0
read coil 1
# Forventet: 0 (straks FALSE)
```

| Step | Action | Forventet Output | Status |
|------|--------|------------------|--------|
| 1 | IN=FALSE | Q=FALSE | ⬜ |
| 2 | IN=TRUE (t=0) | Q=FALSE | ⬜ |
| 3 | IN=TRUE (t<2s) | Q=FALSE | ⬜ |
| 4 | IN=TRUE (t>2s) | Q=TRUE | ⬜ |
| 5 | IN=FALSE | Q=FALSE (straks) | ⬜ |

---

## Test 1.2: TON Reset on Input Drop

**Beskrivelse:** Timer resetter hvis IN falder før PT.

**Test Procedure:**
1. Sæt IN=TRUE
2. Vent 1s (halvdelen af PT)
3. Sæt IN=FALSE
4. Sæt IN=TRUE igen
5. Verificer timer starter forfra

| Test | Forventet | Status |
|------|-----------|--------|
| Timer resets on IN drop | Yes | ⬜ |
| ET resets to 0 | Yes | ⬜ |

---

# 2. TOF (Timer Off-Delay)

## Test 2.1: TOF Basic Operation

**Beskrivelse:** TOF holder output TRUE i PT tid efter IN falder.

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  sensor: BOOL;
  light_on: BOOL;
  timer1: TOF;
END_VAR
BEGIN
  timer1(IN := sensor, PT := T#3s);
  light_on := timer1.Q;
END_PROGRAM
END_UPLOAD

set logic 1 bind sensor coil:0 input
set logic 1 bind light_on coil:1 output
set logic 1 enabled:true
```

**Test Procedure:**
```bash
# 1. Start med FALSE
write coil 0 value 0
read coil 1
# Forventet: 0

# 2. Sæt TRUE
write coil 0 value 1
read coil 1
# Forventet: 1 (straks TRUE)

# 3. Sæt FALSE
write coil 0 value 0
read coil 1
# Forventet: 1 (stadig TRUE)

# 4. Vent 3+ sekunder
read coil 1
# Forventet: 0 (FALSE efter 3s)
```

| Step | Action | Forventet Output | Status |
|------|--------|------------------|--------|
| 1 | IN=FALSE (initial) | Q=FALSE | ⬜ |
| 2 | IN=TRUE | Q=TRUE (straks) | ⬜ |
| 3 | IN=FALSE (t=0) | Q=TRUE | ⬜ |
| 4 | IN=FALSE (t<3s) | Q=TRUE | ⬜ |
| 5 | IN=FALSE (t>3s) | Q=FALSE | ⬜ |

---

## Test 2.2: TOF Retriggering

**Beskrivelse:** Hvis IN går TRUE igen under delay, resetter timer.

| Test | Forventet | Status |
|------|-----------|--------|
| Retrigger resets delay | Yes | ⬜ |
| Q stays TRUE | Yes | ⬜ |

---

# 3. TP (Timer Pulse)

## Test 3.1: TP Basic Operation

**Beskrivelse:** TP genererer en puls af PT varighed på rising edge af IN.

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  trigger: BOOL;
  pulse_out: BOOL;
  timer1: TP;
END_VAR
BEGIN
  timer1(IN := trigger, PT := T#1s);
  pulse_out := timer1.Q;
END_PROGRAM
END_UPLOAD

set logic 1 bind trigger coil:0 input
set logic 1 bind pulse_out coil:1 output
set logic 1 enabled:true
```

**Test Procedure:**
```bash
# 1. Trigger pulse
write coil 0 value 1
read coil 1
# Forventet: 1 (pulse starts)

# 2. Læs efter 0.5s
read coil 1
# Forventet: 1 (stadig TRUE)

# 3. Læs efter 1+ sekund
read coil 1
# Forventet: 0 (pulse ended)
```

| Step | Time | Forventet Output | Status |
|------|------|------------------|--------|
| 1 | t=0 (trigger) | Q=TRUE | ⬜ |
| 2 | t=0.5s | Q=TRUE | ⬜ |
| 3 | t>1s | Q=FALSE | ⬜ |

---

## Test 3.2: TP Non-Retriggerable

**Beskrivelse:** TP ignorerer nye triggers under aktiv puls.

**Test Procedure:**
1. Trigger puls
2. Sæt IN=FALSE og TRUE igen under puls
3. Verificer puls varighed er stadig original PT

| Test | Forventet | Status |
|------|-----------|--------|
| Ignores retrigger during pulse | Yes | ⬜ |
| Pulse duration = PT | Yes | ⬜ |

---

# 4. TIME Literals

## Test 4.1: TIME Literal Formats

**Forskellige TIME literal formater:**

```structured-text
VAR
  t1: TIME := T#1s;        (* 1 sekund *)
  t2: TIME := T#500ms;     (* 500 millisekunder *)
  t3: TIME := T#1m30s;     (* 1 minut 30 sekunder *)
  t4: TIME := T#2h;        (* 2 timer *)
  t5: TIME := T#1d;        (* 1 dag *)
END_VAR
```

| Format | Værdi | Forventet ms | Status |
|--------|-------|--------------|--------|
| T#1s | 1 sekund | 1000 | ⬜ |
| T#500ms | 500 ms | 500 | ⬜ |
| T#1m | 1 minut | 60000 | ⬜ |
| T#1h | 1 time | 3600000 | ⬜ |

---

## Test Results Summary

| Sektion | Tests | Bestået | Fejlet |
|---------|-------|---------|--------|
| TON | 2 | ⬜ | ⬜ |
| TOF | 2 | ⬜ | ⬜ |
| TP | 2 | ⬜ | ⬜ |
| **Total** | **6** | | |

---

**Tester:** _______________
**Dato:** _______________
