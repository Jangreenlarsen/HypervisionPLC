# ST Logic Test - User-Defined Functions (FEAT-003)

**Version:** v6.1.0
**Formål:** Test af FUNCTION og FUNCTION_BLOCK (IEC 61131-3 custom funktioner)

---

## Test Oversigt

| Kategori | Tests | Prioritet |
|----------|-------|-----------|
| FUNCTION (stateless) | 5 | HIGH |
| FUNCTION_BLOCK (stateful) | 4 | HIGH |
| Fejlhåndtering | 3 | MEDIUM |
| **Total** | **12** | |

---

## Forudsætninger

- FEAT-003 implementeret (Phase 1-6)
- Build kompilerer uden fejl
- ESP32 forbundet via CLI (Serial eller Telnet)

### Register Allocation for Tests

| Range | Brug |
|-------|------|
| HR 20-29 | Test input/output variabler |
| HR 30-39 | Ekstra test variabler |

---

# 1. FUNCTION (Stateless)

## Test 1.1: Simpel Funktion - DOUBLE

En funktion der fordobler input-værdien.

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  x: INT;
  result: INT;
END_VAR

FUNCTION DOUBLE : INT
VAR_INPUT
  val: INT;
END_VAR
BEGIN
  DOUBLE := val * 2;
END_FUNCTION

BEGIN
  result := DOUBLE(x);
END_PROGRAM
END_UPLOAD

set logic 1 bind x reg:20 input
set logic 1 bind result reg:21 output
set logic 1 enabled:true
```

**Verifikation:**
```bash
show logic 1 functions
# Forventet: Viser FUNCTION DOUBLE : INT, 1 param (INT)

show logic 1 bytecode
# Forventet: Indeholder CALL_USER med DOUBLE, LOAD_PARAM, RETURN
```

**Test Cases:**
```bash
write reg 20 value int 5
read reg 21 int
# Forventet: 10

write reg 20 value int 0
read reg 21 int
# Forventet: 0

write reg 20 value int -7
read reg 21 int
# Forventet: -14
```

| Input x | Forventet result | Status |
|---------|------------------|--------|
| 5 | 10 | ⬜ |
| 0 | 0 | ⬜ |
| -7 | -14 | ⬜ |

---

## Test 1.2: Funktion med Flere Parametre - ADD3

En funktion der adderer tre tal.

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  a: INT;
  b: INT;
  c: INT;
  sum: INT;
END_VAR

FUNCTION ADD3 : INT
VAR_INPUT
  p1: INT;
  p2: INT;
  p3: INT;
END_VAR
BEGIN
  ADD3 := p1 + p2 + p3;
END_FUNCTION

BEGIN
  sum := ADD3(a, b, c);
END_PROGRAM
END_UPLOAD

set logic 1 bind a reg:20 input
set logic 1 bind b reg:21 input
set logic 1 bind c reg:22 input
set logic 1 bind sum reg:23 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
write reg 20 value int 10
write reg 21 value int 20
write reg 22 value int 30
read reg 23 int
# Forventet: 60

write reg 20 value int -5
write reg 21 value int 5
write reg 22 value int 0
read reg 23 int
# Forventet: 0
```

| a | b | c | Forventet sum | Status |
|---|---|---|---------------|--------|
| 10 | 20 | 30 | 60 | ⬜ |
| -5 | 5 | 0 | 0 | ⬜ |
| 1 | 2 | 3 | 6 | ⬜ |

---

## Test 1.3: Funktion med Lokal Variabel

En funktion der bruger en lokal variabel til beregning.

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  input_val: INT;
  output_val: INT;
END_VAR

FUNCTION CLAMP_POSITIVE : INT
VAR_INPUT
  val: INT;
END_VAR
VAR
  temp: INT;
END_VAR
BEGIN
  temp := val;
  IF temp < 0 THEN
    temp := 0;
  END_IF;
  CLAMP_POSITIVE := temp;
END_FUNCTION

BEGIN
  output_val := CLAMP_POSITIVE(input_val);
END_PROGRAM
END_UPLOAD

set logic 1 bind input_val reg:20 input
set logic 1 bind output_val reg:21 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
write reg 20 value int 42
read reg 21 int
# Forventet: 42

write reg 20 value int -10
read reg 21 int
# Forventet: 0

write reg 20 value int 0
read reg 21 int
# Forventet: 0
```

| Input | Forventet Output | Status |
|-------|------------------|--------|
| 42 | 42 | ⬜ |
| -10 | 0 | ⬜ |
| 0 | 0 | ⬜ |

---

## Test 1.4: Flere Funktionskald i Samme Program

Test at flere funktioner kan defineres og kaldes i samme program.

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  x: INT;
  doubled: INT;
  tripled: INT;
END_VAR

FUNCTION DOUBLE : INT
VAR_INPUT
  val: INT;
END_VAR
BEGIN
  DOUBLE := val * 2;
END_FUNCTION

FUNCTION TRIPLE : INT
VAR_INPUT
  val: INT;
END_VAR
BEGIN
  TRIPLE := val * 3;
END_FUNCTION

BEGIN
  doubled := DOUBLE(x);
  tripled := TRIPLE(x);
END_PROGRAM
END_UPLOAD

set logic 1 bind x reg:20 input
set logic 1 bind doubled reg:21 output
set logic 1 bind tripled reg:22 output
set logic 1 enabled:true
```

**Verifikation:**
```bash
show logic 1 functions
# Forventet: 2 user functions (DOUBLE, TRIPLE)
```

**Test Cases:**
```bash
write reg 20 value int 7
read reg 21 int
# Forventet: 14 (7*2)
read reg 22 int
# Forventet: 21 (7*3)
```

| x | Forventet doubled | Forventet tripled | Status |
|---|-------------------|-------------------|--------|
| 7 | 14 | 21 | ⬜ |
| 0 | 0 | 0 | ⬜ |
| 10 | 20 | 30 | ⬜ |

---

## Test 1.5: Funktion der Kalder Funktion (Nested Calls)

Test af nestede funktionskald.

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  x: INT;
  result: INT;
END_VAR

FUNCTION DOUBLE : INT
VAR_INPUT
  val: INT;
END_VAR
BEGIN
  DOUBLE := val * 2;
END_FUNCTION

FUNCTION QUADRUPLE : INT
VAR_INPUT
  val: INT;
END_VAR
BEGIN
  QUADRUPLE := DOUBLE(DOUBLE(val));
END_FUNCTION

BEGIN
  result := QUADRUPLE(x);
END_PROGRAM
END_UPLOAD

set logic 1 bind x reg:20 input
set logic 1 bind result reg:21 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
write reg 20 value int 3
read reg 21 int
# Forventet: 12 (3*2*2)

write reg 20 value int 5
read reg 21 int
# Forventet: 20 (5*2*2)
```

| x | Forventet result | Status |
|---|------------------|--------|
| 3 | 12 | ⬜ |
| 5 | 20 | ⬜ |
| -2 | -8 | ⬜ |

---

# 2. FUNCTION_BLOCK (Stateful)

FUNCTION_BLOCK bevarer tilstand mellem kald. Hver instans i kildekoden
får sin egen state storage - ligesom builtin TON/CTU funktioner.

## Test 2.1: Simpel Tæller FB

En FUNCTION_BLOCK der tæller op for hvert kald.

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  enable: BOOL;
  count_out: INT;
END_VAR

FUNCTION_BLOCK MY_COUNTER
VAR_INPUT
  en: BOOL;
END_VAR
VAR_OUTPUT
  count: INT;
END_VAR
VAR
  internal_count: INT;
END_VAR
BEGIN
  IF en THEN
    internal_count := internal_count + 1;
  END_IF;
  count := internal_count;
END_FUNCTION_BLOCK

BEGIN
  MY_COUNTER(en := enable);
  count_out := MY_COUNTER.count;
END_PROGRAM
END_UPLOAD

set logic 1 bind enable coil:0 input
set logic 1 bind count_out reg:20 output
set logic 1 enabled:true
```

**Verifikation:**
```bash
show logic 1 functions
# Forventet: FUNCTION_BLOCK MY_COUNTER med inst=0

show logic 1 bytecode
# Forventet: CALL_USER med instance_id
```

**Test Cases:**
```bash
# Aktivér tæller
write coil 0 value 1

# Vent 200ms (2 execution cycles ved 10ms interval)
# Læs tæller - skal stige for hvert kald
read reg 20 int
# Forventet: > 0 (stiger for hver execution cycle)

# Deaktivér tæller
write coil 0 value 0

# Vent 200ms
# Læs tæller igen - skal være stoppet
read reg 20 int
# Forventet: Samme som før (stopper ved deaktivering)

# Aktivér igen - skal fortsætte fra sidste værdi
write coil 0 value 1
# Vent 200ms
read reg 20 int
# Forventet: Højere end sidste aflæsning (state bevaret)
```

| Handling | Forventet Adfærd | Status |
|----------|-----------------|--------|
| enable=TRUE, vent | count stiger | ⬜ |
| enable=FALSE, vent | count stopper | ⬜ |
| enable=TRUE igen | count fortsætter fra sidste | ⬜ |

**Vigtigst:** Testen verificerer at `internal_count` bevares mellem execution cycles (state persistence).

---

## Test 2.2: FB med Input og Output Parametre

En FUNCTION_BLOCK med begge typer parametre.

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  set_val: INT;
  limit_val: INT;
  result: INT;
END_VAR

FUNCTION_BLOCK LIMITER
VAR_INPUT
  value: INT;
  max_limit: INT;
END_VAR
VAR_OUTPUT
  limited: INT;
END_VAR
BEGIN
  IF value > max_limit THEN
    limited := max_limit;
  ELSE
    limited := value;
  END_IF;
END_FUNCTION_BLOCK

BEGIN
  LIMITER(value := set_val, max_limit := limit_val);
  result := LIMITER.limited;
END_PROGRAM
END_UPLOAD

set logic 1 bind set_val reg:20 input
set logic 1 bind limit_val reg:21 input
set logic 1 bind result reg:22 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Under limit
write reg 20 value int 50
write reg 21 value int 100
read reg 22 int
# Forventet: 50

# Over limit
write reg 20 value int 150
write reg 21 value int 100
read reg 22 int
# Forventet: 100

# Exact limit
write reg 20 value int 100
write reg 21 value int 100
read reg 22 int
# Forventet: 100
```

| value | max_limit | Forventet limited | Status |
|-------|-----------|-------------------|--------|
| 50 | 100 | 50 | ⬜ |
| 150 | 100 | 100 | ⬜ |
| 100 | 100 | 100 | ⬜ |

---

## Test 2.3: Flere Instanser af Samme FB

Test at to instanser af samme FUNCTION_BLOCK har uafhængig state.

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  en1: BOOL;
  en2: BOOL;
  count1: INT;
  count2: INT;
END_VAR

FUNCTION_BLOCK UP_COUNTER
VAR_INPUT
  en: BOOL;
END_VAR
VAR_OUTPUT
  cv: INT;
END_VAR
VAR
  cnt: INT;
END_VAR
BEGIN
  IF en THEN
    cnt := cnt + 1;
  END_IF;
  cv := cnt;
END_FUNCTION_BLOCK

BEGIN
  (* To separate instanser - hver med egen state *)
  UP_COUNTER(en := en1);
  count1 := UP_COUNTER.cv;
  UP_COUNTER(en := en2);
  count2 := UP_COUNTER.cv;
END_PROGRAM
END_UPLOAD

set logic 1 bind en1 coil:0 input
set logic 1 bind en2 coil:1 input
set logic 1 bind count1 reg:20 output
set logic 1 bind count2 reg:21 output
set logic 1 enabled:true
```

**Verifikation:**
```bash
show logic 1 functions
# Forventet: FUNCTION_BLOCK UP_COUNTER
#   inst[0]: ... (for first call)
#   inst[1]: ... (for second call)
```

**Test Cases:**
```bash
# Aktivér kun counter 1
write coil 0 value 1
write coil 1 value 0

# Vent 200ms
read reg 20 int
# Forventet: > 0 (tæller op)
read reg 21 int
# Forventet: 0 (ikke aktiveret)

# Aktivér kun counter 2
write coil 0 value 0
write coil 1 value 1

# Vent 200ms
read reg 21 int
# Forventet: > 0 (tæller op)

# Counter 1 stadig på sin gamle værdi
read reg 20 int
# Forventet: Samme som før (stoppet)
```

| Handling | count1 | count2 | Status |
|----------|--------|--------|--------|
| en1=TRUE, en2=FALSE, vent | stiger | 0 | ⬜ |
| en1=FALSE, en2=TRUE, vent | stoppet | stiger | ⬜ |
| Begge TRUE, vent | stiger | stiger | ⬜ |

**Vigtigst:** Verificerer at to kald til UP_COUNTER bruger separate instanser med uafhængig `cnt` state.

---

## Test 2.4: FB State Persistens over Reboot-Cycle

Test at FB state nulstilles korrekt ved program restart.

**CLI Kommandoer:**
```bash
# Først: upload og kør programmet
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  enable: BOOL;
  count_out: INT;
END_VAR

FUNCTION_BLOCK PERSISTENT_CTR
VAR_INPUT
  en: BOOL;
END_VAR
VAR_OUTPUT
  val: INT;
END_VAR
VAR
  state: INT;
END_VAR
BEGIN
  IF en THEN
    state := state + 1;
  END_IF;
  val := state;
END_FUNCTION_BLOCK

BEGIN
  PERSISTENT_CTR(en := enable);
  count_out := PERSISTENT_CTR.val;
END_PROGRAM
END_UPLOAD

set logic 1 bind enable coil:0 input
set logic 1 bind count_out reg:20 output
set logic 1 enabled:true
```

**Test Procedure:**
```bash
# Step 1: Kør tæller op
write coil 0 value 1
# Vent 500ms
read reg 20 int
# Forventet: > 0

# Step 2: Disable og re-enable program
set logic 1 enabled:false
set logic 1 enabled:true

# Step 3: Kør tæller igen
write coil 0 value 1
# Vent 200ms
read reg 20 int
# Forventet: Fortsætter tælling (FB instanser bevares ved enable/disable)

# Step 4: Rekompilér program (state nulstilles)
set logic 1 upload
PROGRAM test
VAR enable: BOOL; count_out: INT; END_VAR
FUNCTION_BLOCK PERSISTENT_CTR
VAR_INPUT en: BOOL; END_VAR
VAR_OUTPUT val: INT; END_VAR
VAR state: INT; END_VAR
BEGIN IF en THEN state := state + 1; END_IF; val := state; END_FUNCTION_BLOCK
BEGIN PERSISTENT_CTR(en := enable); count_out := PERSISTENT_CTR.val; END_PROGRAM
END_UPLOAD
set logic 1 enabled:true

# Step 5: Verificér state er nulstillet
write coil 0 value 1
# Vent 200ms
read reg 20 int
# Forventet: Starter fra 0 igen (rekompilering nulstiller FB instanser)
```

| Handling | Forventet | Status |
|----------|-----------|--------|
| enable/disable: state bevaret | count fortsætter | ⬜ |
| Rekompilér: state nulstillet | count starter fra 0 | ⬜ |

---

# 3. Fejlhåndtering

## Test 3.1: Forkert Antal Parametre

Test at compiler giver fejl ved forkert antal argumenter.

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  result: INT;
END_VAR

FUNCTION ADD2 : INT
VAR_INPUT
  a: INT;
  b: INT;
END_VAR
BEGIN
  ADD2 := a + b;
END_FUNCTION

BEGIN
  result := ADD2(10);
END_PROGRAM
END_UPLOAD
```

| Forventet Resultat | Status |
|--------------------|--------|
| Kompileringsfejl (forkert antal parametre) | ⬜ |

---

## Test 3.2: Max Rekursionsdybde

Test at VM stopper ved for dyb rekursion (max 8 niveauer).

**CLI Kommandoer:**
```bash
set logic 1 delete
set logic 1 upload
PROGRAM test
VAR
  x: INT;
  result: INT;
END_VAR

FUNCTION RECURSE : INT
VAR_INPUT
  n: INT;
END_VAR
BEGIN
  IF n <= 0 THEN
    RECURSE := 0;
  ELSE
    RECURSE := n + RECURSE(n - 1);
  END_IF;
END_FUNCTION

BEGIN
  result := RECURSE(x);
END_PROGRAM
END_UPLOAD

set logic 1 bind x reg:20 input
set logic 1 bind result reg:21 output
set logic 1 enabled:true
```

**Test Cases:**
```bash
# Safe recursion depth (3 levels)
write reg 20 value int 3
read reg 21 int
# Forventet: 6 (3+2+1+0)

# Deep recursion (should hit limit at 8)
write reg 20 value int 20
# Vent 200ms
show logic 1
# Forventet: Runtime error (max call depth exceeded)
```

| Input n | Forventet | Status |
|---------|-----------|--------|
| 3 | result=6 | ⬜ |
| 7 | result=28 | ⬜ |
| 20 | Runtime fejl (max depth) | ⬜ |

---

## Test 3.3: Max Antal Funktioner

Test at compiler giver fejl ved for mange funktionsdefinitioner.

**Note:** ST_MAX_USER_FUNCTIONS = 16. Denne test kræver et program med 17+ funktioner.

| Forventet Resultat | Status |
|--------------------|--------|
| Kompileringsfejl ved funktion #17 | ⬜ |

---

# 4. CLI Verifikation

## Test 4.1: show logic functions

Verificér at `show logic <id> functions` viser korrekt output.

**Procedure:**
1. Upload Test 1.4 (DOUBLE + TRIPLE funktioner)
2. Kør `show logic 1 functions`

**Forventet Output:**
```
======== Logic1 User Functions ========

User Functions: 2

  [0] FUNCTION DOUBLE : INT
      Params: 1 (INT)
      Bytecode: addr=X, size=Y instructions

  [1] FUNCTION TRIPLE : INT
      Params: 1 (INT)
      Bytecode: addr=X, size=Y instructions

========================================
```

| Verificering | Status |
|--------------|--------|
| Funktionsnavne vises | ⬜ |
| Return type vises | ⬜ |
| Parametertyper vises | ⬜ |
| Bytecode adresse vises | ⬜ |

## Test 4.2: show logic bytecode med funktioner

Verificér at `show logic <id> bytecode` viser nye opcodes korrekt.

**Procedure:**
1. Upload Test 1.1 (DOUBLE funktion)
2. Kør `show logic 1 bytecode`

**Forventet:** Bytecode indeholder:
- `JMP` over funktionens body
- `LOAD_PARAM [0]` inde i funktionen
- `RETURN` i slutningen af funktionen
- `CALL_USER func[X] ; DOUBLE` i hoveddelen

| Verificering | Status |
|--------------|--------|
| JMP over funktion body | ⬜ |
| LOAD_PARAM vises | ⬜ |
| RETURN vises | ⬜ |
| CALL_USER med funktionsnavn | ⬜ |

---

## Test Results Summary

| Kategori | Tests | Bestået | Fejlet |
|----------|-------|---------|--------|
| FUNCTION (stateless) | 5 | ⬜ | ⬜ |
| FUNCTION_BLOCK (stateful) | 4 | ⬜ | ⬜ |
| Fejlhåndtering | 3 | ⬜ | ⬜ |
| CLI Verifikation | 2 | ⬜ | ⬜ |
| **Total** | **14** | | |

---

**Tester:** _______________
**Dato:** _______________
**Build:** _______________
