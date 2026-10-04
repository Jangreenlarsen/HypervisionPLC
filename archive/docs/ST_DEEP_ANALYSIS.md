# Dyb Analyse af ST Logic Subsystemet

<!-- DOC-STATUS: historisk -->
> ⚠️ **Historisk dokument — vedligeholdes ikke længere.** Skrevet til en ældre firmwareversion og kan være forældet (CLI-syntaks, pins, endpoints, standardværdier). Den aktuelle, vedligeholdte dokumentation er [brugermanualen](docs/manual/00_INDEKS.md) (CLI: appendiks A, REST: appendiks B, ST: appendiks D). Markeret ved dokumentationsgennemgang 2026-09-28.


**Dato:** 2026-01-17
**Projekt:** ESP32 Modbus RTU Server - ST Logic Engine
**Version:** v5.1.7 (Build #1074+)

---

## 1. Arkitektur Oversigt

ST Logic subsystemet implementerer en IEC 61131-3 kompatibel Structured Text interpreter med følgende komponenter:

```
┌─────────────────────────────────────────────────────────────────────┐
│                         ST LOGIC PIPELINE                          │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌──────────┐   ┌──────────┐   ┌───────────┐   ┌────────────────┐  │
│  │  LEXER   │──▶│  PARSER  │──▶│ COMPILER  │──▶│ BYTECODE PROG  │  │
│  │st_lexer  │   │st_parser │   │st_compiler│   │st_bytecode_    │  │
│  │  .cpp    │   │  .cpp    │   │   .cpp    │   │ program_t      │  │
│  └──────────┘   └──────────┘   └───────────┘   └────────────────┘  │
│       ▲              │                                  │          │
│       │              ▼                                  ▼          │
│  ┌──────────┐   ┌──────────┐                    ┌────────────────┐ │
│  │  SOURCE  │   │   AST    │                    │    ST VM       │ │
│  │  CODE    │   │  Nodes   │                    │   st_vm.cpp    │ │
│  │ (5KB max)│   │(temporary)                    │ (stack-based)  │ │
│  └──────────┘   └──────────┘                    └────────────────┘ │
│                                                         │          │
│                                                         ▼          │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │                    BUILT-IN FUNCTIONS                        │  │
│  ├──────────┬──────────┬──────────┬──────────┬─────────────────┤  │
│  │  EDGE    │  TIMER   │ COUNTER  │  LATCH   │    SIGNAL       │  │
│  │R_TRIG    │TON/TOF/TP│CTU/CTD/  │SR/RS     │SCALE/HYSTERESIS │  │
│  │F_TRIG    │          │CTUD      │          │FILTER/BLINK     │  │
│  └──────────┴──────────┴──────────┴──────────┴─────────────────┘  │
│                                                         │          │
│                                                         ▼          │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │                  STATEFUL STORAGE                            │  │
│  │  st_stateful_storage_t (persistent state mellem cycles)      │  │
│  └──────────────────────────────────────────────────────────────┘  │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

### 1.1 Komponent Statistik

| Komponent | Fil | Linjer | Kompleksitet |
|-----------|-----|--------|--------------|
| Lexer | st_lexer.cpp | ~450 | Medium |
| Parser | st_parser.cpp | ~900 | Høj (rekursiv descent) |
| Compiler | st_compiler.cpp | ~950 | Høj (bytecode generering) |
| VM | st_vm.cpp | ~1650 | Høj (type-aware execution) |
| Engine | st_logic_engine.cpp | ~300 | Medium |
| Types | st_types.h | ~450 | Reference |
| Signal Builtins | st_builtin_signal.cpp | ~250 | Medium |
| Timer Builtins | st_builtin_timers.cpp | ~200 | Medium |
| Counter Builtins | st_builtin_counters.cpp | ~150 | Medium |
| Edge Builtins | st_builtin_edge.cpp | ~100 | Lav |
| Latch Builtins | st_builtin_latch.cpp | ~80 | Lav |
| Modbus Builtins | st_builtin_modbus.cpp | ~300 | Medium |

**Total:** ~5500+ linjer ST-specifik kode

---

## 2. Type System Analyse

### 2.1 IEC 61131-3 Type Support

| Type | Størrelse | Registers | Status |
|------|-----------|-----------|--------|
| BOOL | 1 bit | 1 | ✅ Fuld support |
| INT | 16-bit signed | 1 | ✅ Fuld support |
| DINT | 32-bit signed | 2 | ✅ Fuld support |
| DWORD | 32-bit unsigned | 2 | ✅ Fuld support |
| REAL | 32-bit IEEE 754 | 2 | ✅ Fuld support |

### 2.2 Type Promotion Regler

VM'en implementerer korrekt type promotion:
```
INT + INT = INT (16-bit overflow wrap)
INT + DINT = DINT (promotion)
INT + REAL = REAL (promotion)
DINT + REAL = REAL (promotion)
```

### 2.3 Type Stack Tracking

VM'en vedligeholder en parallel type stack (`type_stack[]`) for at tracke runtime typer:
- `st_vm_push_typed()` - Push med type info
- `st_vm_pop_typed()` - Pop med type info
- **BUG-151** fixede legacy `st_vm_pop()` der korrupterede type stack

---

## 3. Bug Kategorisering og Mønstre

### 3.1 Historiske Bug Kategorier (Alle Fixet)

| Kategori | Antal | Kritiske | Eksempler |
|----------|-------|----------|-----------|
| Buffer Overflow | 12 | 8 | BUG-032, BUG-056, BUG-067, BUG-155 |
| Type Tracking | 8 | 5 | BUG-050, BUG-052, BUG-059, BUG-151 |
| Missing Validation | 10 | 4 | BUG-003, BUG-154, BUG-156, BUG-158 |
| Memory Safety | 6 | 3 | BUG-066, BUG-081, BUG-146 |
| Logic Errors | 8 | 4 | BUG-054, BUG-106, BUG-110, BUG-134 |
| IEC Compliance | 4 | 2 | BUG-105, BUG-123 |
| Parser Issues | 5 | 2 | BUG-046, BUG-053, BUG-157 |

### 3.2 Nuværende Åbne ST-relaterede Bugs

| Bug ID | Beskrivelse | Prioritet | Status |
|--------|-------------|-----------|--------|
| BUG-011 | Variabelnavn `coil_reg` bruges til HR | LOW | OPEN (cosmetic) |

**Status:** ST Logic subsystemet er i meget stabil tilstand med kun 1 kosmetisk åben bug.

---

## 4. Potentielle Forbedringer Identificeret

### 4.1 Performance Optimizations (Ikke Bugs)

| ID | Område | Beskrivelse | Impact |
|----|--------|-------------|--------|
| OPT-001 | Symbol lookup | O(n) linear search | Acceptabel for max 32 vars |
| OPT-002 | AST memory | ~600 bytes per node | Acceptabel (temporary) |
| OPT-003 | Instruction fetch | Ingen caching | Minimal impact |

### 4.2 Dokumenterede Design Choices

| Valg | Begrundelse | Reference |
|------|-------------|-----------|
| Wrapping arithmetic | Performance over safety | BUG-172 DOCUMENTED |
| C remainder semantics | Standard C behavior | BUG-173 DOCUMENTED |
| Single-threaded | Arduino loop() model | BUG-166 NOT A BUG |

---

## 5. Security Analyse

### 5.1 Input Validation Status

| Område | Validering | Status |
|--------|------------|--------|
| Source code size | Max 5KB | ✅ Validated |
| Variable count | Max 32 | ✅ Validated |
| Instruction count | Max 1024 | ✅ Validated |
| Stack depth | Max 64 | ✅ Validated |
| Parser recursion | Max 50 depth | ✅ BUG-157 FIXED |
| Function arguments | Max 5 | ✅ BUG-156 FIXED |
| Case branches | Max 16 | ✅ BUG-168 FIXED |
| String literals | Max 255 chars | ✅ BUG-155 FIXED |
| Identifiers | Max 63 chars | ✅ BUG-155 FIXED |

### 5.2 Runtime Protection

| Mekanisme | Implementering | Status |
|-----------|----------------|--------|
| Max steps limit | 10000 per execution | ✅ Active |
| Execution timeout | Warning at 100ms | ✅ BUG-007 FIXED |
| Division by zero | Error + variable rollback | ✅ BUG-106 FIXED |
| Jump validation | Target bounds check | ✅ BUG-154 FIXED |
| NULL checks | Stateful storage | ✅ BUG-158 FIXED |
| NaN/INF detection | REAL operations | ✅ BUG-160 FIXED |

---

## 6. Bug Fix Plan (Prioriteret)

### 6.1 Ingen Kritiske Bugs Tilbage

ST Logic subsystemet har **ingen åbne kritiske bugs**. Alle 67+ identificerede bugs er enten:
- ✅ FIXED
- ✔️ DOCUMENTED (design choice)
- LOW priority cosmetic

### 6.2 Anbefalet Cosmetic Fix

**BUG-011: Variabelnavn `coil_reg` bruges til HR også**

**Beskrivelse:** I `types.h` struct `VariableMapping` hedder feltet `coil_reg`, men det bruges til både coils OG holding registers. Dette er forvirrende for vedligeholdelse.

**Foreslået Fix:**
```cpp
// types.h - VariableMapping struct
typedef struct {
    // ...
    uint16_t target_reg;  // Rename from coil_reg
    // ...
} VariableMapping;
```

**Impact:** Cosmetic/documentation only. Ingen funktionel ændring.

**Filer at opdatere:**
- `include/types.h`
- `src/gpio_mapping.cpp`
- `src/registers.cpp`
- `src/cli_commands.cpp`

---

## 7. Test Coverage Anbefaling

### 7.1 Kritiske Test Cases

| Kategori | Test | Verificeret |
|----------|------|-------------|
| Type promotion | INT+REAL, DINT+REAL | ✅ |
| Overflow | INT wraparound | ✅ |
| Boundary | 32 variables | ✅ |
| Control flow | Nested IF/FOR/WHILE | ✅ |
| Builtins | R_TRIG edge detection | ✅ |
| Timers | TON 100ms accuracy | ✅ |
| Counters | CTU/CTD/CTUD | ✅ |
| Modbus | MB_READ/WRITE | ✅ |

### 7.2 Regression Test Suite

Anbefalet minimal test suite:

```st
(* TEST 1: Type promotion *)
VAR x: INT; y: DINT; z: REAL; END_VAR;
x := 100;
y := 100000;
z := x + y;  (* Skal give 100100.0 *)

(* TEST 2: Boundary *)
VAR i: INT; END_VAR;
FOR i := 1 TO 10 DO
  (* Loop body *)
END_FOR;

(* TEST 3: Timer *)
VAR enable: BOOL; q: BOOL; END_VAR;
enable := TRUE;
q := TON(enable, 100);

(* TEST 4: Counter *)
VAR pulse: BOOL; count: INT; END_VAR;
count := CTU(pulse, FALSE, 10);
```

---

## 8. Konklusion

### 8.1 Helhedsvurdering

ST Logic subsystemet er **produktionsklart** med:
- ✅ Fuld IEC 61131-3 type system support
- ✅ Robust input validation
- ✅ Comprehensive error handling
- ✅ Alle kritiske bugs fixet
- ✅ Dokumenterede design choices

### 8.2 Risiko Vurdering

| Risiko | Niveau | Mitigering |
|--------|--------|------------|
| Runtime crash | LAV | Max steps, NULL checks, bounds validation |
| Memory corruption | LAV | strncpy, buffer size checks |
| Infinite loops | LAV | Max steps limit (10000) |
| Type confusion | LAV | Full type tracking in VM |

### 8.3 Næste Skridt

1. **Cosmetic:** Fix BUG-011 (variabelnavn klarhed)
2. **Documentation:** Ingen yderligere dokumentation nødvendig
3. **Testing:** Køre regression test suite efter større ændringer

---

## Appendix A: ST Logic Bug Timeline

```
v4.0.2: BUG-001 til BUG-007 (foundation bugs)
v4.1.0: BUG-008 til BUG-012 (latency, type fixes)
v4.2.x: BUG-015 til BUG-044 (counter, parser, CLI)
v4.3.x: BUG-046 til BUG-054 (lexer, VM, FOR loop)
v4.4.x: BUG-055 til BUG-089 (buffer overflow, comparison, overflow)
v5.0.0: BUG-105 til BUG-125 (IEC compliance, byte order)
v4.5.x-4.8.x: BUG-130 til BUG-168 (NVS, type validation, security)
v5.1.x: BUG-178 til BUG-189 (EXPORT, counter, timer)
```

**Total bugs tracked:** 189
**ST-relaterede bugs:** ~80 (42%)
**Åbne ST bugs:** 1 (cosmetic)

---

## Appendix B: VM Opcode Reference

| Opcode | Args | Stack | Beskrivelse |
|--------|------|-------|-------------|
| PUSH_BOOL | bool | +1 | Push boolean literal |
| PUSH_INT | int16 | +1 | Push INT literal |
| PUSH_DWORD | uint32 | +1 | Push DWORD literal |
| PUSH_REAL | float | +1 | Push REAL literal |
| LOAD_VAR | idx | +1 | Load variable value |
| STORE_VAR | idx | -1 | Store to variable |
| DUP | - | +1 | Duplicate top |
| POP | - | -1 | Discard top |
| ADD | - | -1 | a + b (type-aware) |
| SUB | - | -1 | a - b (type-aware) |
| MUL | - | -1 | a * b (type-aware) |
| DIV | - | -1 | a / b (type-aware) |
| MOD | - | -1 | a MOD b |
| NEG | - | 0 | Negate top |
| AND | - | -1 | Logical AND |
| OR | - | -1 | Logical OR |
| XOR | - | -1 | Logical XOR |
| NOT | - | 0 | Logical NOT |
| EQ | - | -1 | a = b |
| NE | - | -1 | a <> b |
| LT | - | -1 | a < b |
| GT | - | -1 | a > b |
| LE | - | -1 | a <= b |
| GE | - | -1 | a >= b |
| SHL | - | -1 | Shift left |
| SHR | - | -1 | Shift right |
| JMP | addr | 0 | Unconditional jump |
| JMP_IF_FALSE | addr | -1 | Jump if false |
| JMP_IF_TRUE | addr | -1 | Jump if true |
| CALL_BUILTIN | func,inst | varies | Call function |
| NOP | - | 0 | No operation |
| HALT | - | 0 | Stop execution |

---

## Appendix C: Builtin Function Reference

### Standard Functions
| Function | Args | Return | Beskrivelse |
|----------|------|--------|-------------|
| ABS | 1 | same | Absolute value (type-polymorphic) |
| MIN | 2 | same | Minimum value (type-polymorphic) |
| MAX | 2 | same | Maximum value (type-polymorphic) |
| LIMIT | 3 | same | Clamp value (min, val, max) |
| SEL | 3 | same | Select (cond, val0, val1) |
| MUX | 4 | same | Multiplex (k, in0, in1, in2) |
| SUM | 2 | same | Addition (type-polymorphic) |
| SQRT | 1 | REAL | Square root |
| SIN | 1 | REAL | Sine (radians) |
| COS | 1 | REAL | Cosine (radians) |
| TAN | 1 | REAL | Tangent (radians) |
| LN | 1 | REAL | Natural log |
| LOG | 1 | REAL | Log base 10 |
| EXP | 1 | REAL | e^x |
| ROL | 2 | same | Rotate left |
| ROR | 2 | same | Rotate right |

### Edge Detection (Stateful)
| Function | Args | Return | Beskrivelse |
|----------|------|--------|-------------|
| R_TRIG | 1 | BOOL | Rising edge detection |
| F_TRIG | 1 | BOOL | Falling edge detection |

### Timers (Stateful)
| Function | Args | Return | Beskrivelse |
|----------|------|--------|-------------|
| TON | 2 | BOOL | On-delay timer |
| TOF | 2 | BOOL | Off-delay timer |
| TP | 2 | BOOL | Pulse timer |

### Counters (Stateful)
| Function | Args | Return | BOOL | Beskrivelse |
|----------|------|--------|------|-------------|
| CTU | 3 | BOOL | Count up |
| CTD | 3 | BOOL | Count down |
| CTUD | 5 | BOOL | Count up/down |

### Latches (Stateful)
| Function | Args | Return | Beskrivelse |
|----------|------|--------|-------------|
| SR | 2 | BOOL | Set-dominant latch |
| RS | 2 | BOOL | Reset-dominant latch |

### Signal Processing (Stateful)
| Function | Args | Return | Beskrivelse |
|----------|------|--------|-------------|
| SCALE | 5 | REAL | Linear scaling |
| HYSTERESIS | 3 | BOOL | Hysteresis comparator |
| FILTER | 2 | REAL | Low-pass filter |
| BLINK | 3 | BOOL | Oscillator |

### Modbus Master
| Function | Args | Return | Beskrivelse |
|----------|------|--------|-------------|
| MB_READ_COIL | 2 | BOOL | Read remote coil |
| MB_READ_DI | 2 | BOOL | Read remote DI |
| MB_READ_IR | 2 | INT | Read remote IR |
| MB_READ_HR | 2 | INT | Read remote HR |
| MB_WRITE_COIL | 3 | BOOL | Write remote coil |
| MB_WRITE_HOLDING | 3 | BOOL | Write remote HR |

---

*Rapport genereret: 2026-01-17*
*Analyseret af: Claude Code*
