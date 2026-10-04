# Structured Text - IEC 61131-3 Compliance Document

<!-- DOC-STATUS: historisk -->
> ⚠️ **Historisk dokument — vedligeholdes ikke længere.** Skrevet til en ældre firmwareversion og kan være forældet (CLI-syntaks, pins, endpoints, standardværdier). Den aktuelle, vedligeholdte dokumentation er [brugermanualen](manual/00_INDEKS.md) (CLI: appendiks A, REST: appendiks B, ST: appendiks D). Markeret ved dokumentationsgennemgang 2026-09-28.


**ESP32 Modbus RTU Server - Logic Mode Implementation**

Version: 2.0.0
Date: 2025-12-24
Status: Production Release (v4.4.0)

---

## 1. Executive Summary

This document declares the **Structured Text (ST) language support** in the ESP32 Modbus RTU Server as conforming to the **IEC 61131-3:2013** international standard for programmable logic controllers (PLCs), with a clearly defined profile and constraints.

**Profile:** ST-Light (Embedded Modbus PLC Profile)
**Language:** Structured Text (ST, also known as STX in IEC 61131-3)
**Scope:** Modbus RTU control logic in Logic-mode (experimental feature)
**Target Hardware:** ESP32-WROOM-32 (240 MHz dual-core, 520 KB RAM, 4 MB Flash)

---

## 2. IEC 61131-3 Standard Overview

IEC 61131-3:2013 (fourth edition, published May 2025) defines five programming languages for PLCs:

1. **Instruction List (IL)** - Mnemonic language
2. **Ladder Diagram (LD)** - Graphical representation
3. **Structured Text (ST)** - High-level textual language ✓ *Implemented here*
4. **Function Block Diagram (FBD)** - Graphical with reusable blocks
5. **Sequential Function Chart (SFC)** - State machine language

This implementation focuses exclusively on **Structured Text (ST)** as the user-facing programming language for logic-mode configuration.

**Key IEC 61131-3 Principle:** The instruction set is *optional*. Vendors implement a subset relevant to their platform while maintaining semantic compatibility.

---

## 3. Conformance Claim: ST-Light Profile

### 3.1 Profile Definition

We declare conformance to the following **subset** of IEC 61131-3:

| Aspect | Coverage | Notes |
|--------|----------|-------|
| **Lexical Elements** (6.1) | ✓ Full | Keywords, identifiers, literals, operators, delimiters |
| **Data Types** (5) | ⚠️ Partial | BOOL, INT, DWORD, REAL only (no ARRAY, STRUCT) |
| **Variables** (6.2) | ✓ Full | VAR, VAR_INPUT, VAR_OUTPUT declarations |
| **Control Structures** (6.3.2) | ✓ Full | IF/THEN/ELSE, CASE/OF, FOR, WHILE, REPEAT |
| **Functions** (6.4) | ⚠️ Partial | Built-in standard library only (no user functions yet) |
| **Function Blocks** (6.3.3) | ✗ Not supported | Design choice for embedded constraints |
| **Recursion** | ✗ Not supported | Safety constraint (stack overflow risk) |
| **Type System** | ✓ Full | Type checking, implicit conversions per IEC rules |
| **Operators** (5.3) | ✓ Full | Arithmetic, logical, relational, bitwise |

**Compliance Statement:**
> "This implementation conforms to IEC 61131-3:2013, Part 3 (Programming Languages),
> for the Structured Text language subset as defined in this ST-Light profile.
> All implemented features follow IEC 61131-3 semantics and syntax rules."

---

## 4. Supported Language Features

### 4.1 Lexical Elements (IEC 6.1)

#### Identifiers
```
[A-Za-z_][A-Za-z0-9_]*
```
Examples: `counter`, `temp_value`, `_internal`

#### Keywords (Case-insensitive per IEC)
Control: `IF THEN ELSE ELSIF END_IF CASE OF END_CASE FOR TO BY DO END_FOR WHILE END_WHILE REPEAT UNTIL END_REPEAT EXIT`
Declaration: `VAR VAR_INPUT VAR_OUTPUT CONST PROGRAM END_PROGRAM`
Types: `BOOL INT DWORD REAL`
Operators: `AND OR NOT XOR MOD SHL SHR`

#### Literals
- **Boolean:** `TRUE`, `FALSE`
- **Integer:** Decimal (`123`), hexadecimal (`0xFF`), binary (`2#1010`)
- **Real:** IEEE 754 32-bit (`1.5`, `3.14e-2`)
- **String:** Single/double quotes (`'hello'`, `"world"`)

#### Comments
Block comments: `(* comment *)`
Nested comments supported (per IEC 61131-3 6.1.3)

#### Operators (with precedence, highest to lowest)
```
1. () [] .           Parentheses, subscript, member access
2. NOT - unary       Logical/arithmetic negation
3. * / MOD SHL SHR   Multiplicative, bitwise shift
4. + -               Additive
5. < > <= >= = <>    Relational
6. AND               Logical AND
7. OR XOR            Logical OR/XOR
```

All operators follow IEC 61131-3 semantics (Section 5.3).

### 4.2 Data Types (IEC 5)

Supported elementary data types:

| Type | IEC Name | Size | Range | Usage |
|------|----------|------|-------|-------|
| `BOOL` | BOOL | 1 byte | FALSE (0), TRUE (1) | Discrete I/O, flags |
| `INT` | INT | 4 bytes | -2,147,483,648 to 2,147,483,647 | Counters, indices |
| `DWORD` | DWORD or UINT32 | 4 bytes | 0 to 4,294,967,295 | Unsigned values, bitmasks |
| `REAL` | REAL or LREAL | 4 bytes | ±3.4×10⁻³⁸ to ±3.4×10³⁸ | Floating-point calculations |

**Unsupported types (future):**
- `LREAL` (64-bit IEEE 754) - RAM constraint
- `BYTE`, `WORD`, `DWORD` as byte/word types - use INT/DWORD instead
- `STRING`, `ARRAY`, `STRUCT` - design simplification
- `TIME`, `DATE`, `DATE_AND_TIME` - not in Modbus scope

### 4.3 Variables (IEC 6.2)

#### Variable Declarations

```structured_text
VAR
  counter : INT := 0;
  enabled : BOOL;
  limit : REAL := 100.5;
END_VAR

VAR_INPUT
  sensor_value : INT;
END_VAR

VAR_OUTPUT
  relay_active : BOOL;
END_VAR
```

**Variable Attributes:**
- Local scope only (no global VAR in PROGRAM scope yet)
- Optional initialization with constant values
- Type must be one of: BOOL, INT, DWORD, REAL

**Initialization Rules (IEC 6.2.5):**
- If not initialized, default to: BOOL=FALSE, INT=0, DWORD=0, REAL=0.0
- Initialization expressions must be constant (compile-time)

### 4.4 Control Structures (IEC 6.3.2)

#### IF Statement

```structured_text
IF condition THEN
  statements
ELSIF condition THEN
  statements
ELSE
  statements
END_IF;
```

**Semantics:** Standard if-then-else. All branches are mutually exclusive. At most one branch executes per invocation.

#### CASE Statement

```structured_text
CASE selector OF
  value1: statements;
  value2, value3: statements;
  ELSE: statements;
END_CASE;
```

**Semantics:** Multi-way branch on integer selector. First matching case executes. ELSE is optional.

#### FOR Loop (Deterministic counting loop)

```structured_text
FOR counter := start TO end [BY step] DO
  statements
END_FOR;
```

**Semantics (IEC 6.3.2.3):**
- `counter` is local loop variable, INT type
- Loop from `start` to `end` inclusive
- Step defaults to +1, can be negative
- Loop executes `(end - start) / step + 1` times (if > 0)
- After loop, counter retains final value

#### WHILE Loop (Condition-based iteration)

```structured_text
WHILE condition DO
  statements
END_WHILE;
```

**Semantics:** Evaluates condition before each iteration. Exits when false. May not execute at all.

#### REPEAT Loop (Post-test loop)

```structured_text
REPEAT
  statements
UNTIL condition
END_REPEAT;
```

**Semantics:** Always executes statements at least once. Exits when condition becomes true.

#### EXIT Statement

```structured_text
FOR i := 1 TO 100 DO
  IF error_detected THEN
    EXIT;  (* Break loop *)
  END_IF;
END_FOR;
```

**Semantics:** Immediately exits innermost loop. Only valid inside FOR/WHILE/REPEAT.

### 4.5 Operators (IEC 5.3)

#### Arithmetic Operators

| Operator | Operands | Result | Semantics |
|----------|----------|--------|-----------|
| `+` | INT, DWORD, REAL | Same type | Addition |
| `-` | INT, DWORD, REAL | Same type | Subtraction |
| `*` | INT, DWORD, REAL | Same type | Multiplication |
| `/` | INT, DWORD, REAL | REAL | Division (always returns REAL) |
| `MOD` | INT, DWORD | Same type | Modulo (remainder) |
| `-` (unary) | INT, DWORD, REAL | Same type | Negation |

**Type Promotion (IEC 5.2):**
- INT + REAL → REAL
- DWORD + INT → DWORD (unsigned)
- Implicit conversions follow standard PLC rules

#### Logical Operators

| Operator | Operands | Result | Semantics |
|----------|----------|--------|-----------|
| `AND` | BOOL, INT | Same type | Logical AND (bitwise for INT) |
| `OR` | BOOL, INT | Same type | Logical OR (bitwise for INT) |
| `NOT` | BOOL, INT | Same type | Logical NOT (bitwise for INT) |
| `XOR` | BOOL, INT | Same type | Logical XOR (future) |

#### Relational Operators

| Operator | Operands | Result | Semantics |
|----------|----------|--------|-----------|
| `=` | INT, DWORD, REAL, BOOL | BOOL | Equal |
| `<>` | INT, DWORD, REAL, BOOL | BOOL | Not equal |
| `<` | INT, DWORD, REAL | BOOL | Less than |
| `>` | INT, DWORD, REAL | BOOL | Greater than |
| `<=` | INT, DWORD, REAL | BOOL | Less than or equal |
| `>=` | INT, DWORD, REAL | BOOL | Greater than or equal |

#### Bitwise Operators (on INT/DWORD)

| Operator | Operands | Result | Semantics |
|----------|----------|--------|-----------|
| `SHL` | INT count | Same type | Shift left (x << count) |
| `SHR` | INT count | Same type | Shift right (x >> count) |

#### Assignment Operator

```
variable := expression;
```

**Semantics:** Right-hand expression evaluated, result converted to variable type, then stored. Conversion follows IEC type rules (truncation for real→int).

### 4.6 Functions

#### Built-in Standard Functions (IEC 6.4)

Implemented standard functions:

**Conversion Functions:**
```
INT_TO_REAL(i : INT) : REAL
REAL_TO_INT(r : REAL) : INT  (* Truncates *)
DWORD_TO_INT(d : DWORD) : INT
INT_TO_DWORD(i : INT) : DWORD
BOOL_TO_INT(b : BOOL) : INT
INT_TO_BOOL(i : INT) : BOOL
```

**Mathematical Functions:**
```
ABS(x : INT | DWORD | REAL) : Same type
MIN(a, b : INT | REAL) : Same type
MAX(a, b : INT | REAL) : Same type
SUM(a, b : INT | REAL) : INT | REAL
SQRT(x : REAL) : REAL
MOD(a, b : INT) : INT
```

**Rounding Functions (v4.3+):**
```
ROUND(x : REAL) : INT    (* Round to nearest integer *)
TRUNC(x : REAL) : INT    (* Truncate fractional part *)
FLOOR(x : REAL) : INT    (* Round down *)
CEIL(x : REAL) : INT     (* Round up *)
```

**Trigonometric Functions (v4.3+):**
```
SIN(x : REAL) : REAL     (* Sine, x in radians *)
COS(x : REAL) : REAL     (* Cosine, x in radians *)
TAN(x : REAL) : REAL     (* Tangent, x in radians *)
```

**Clamping & Selection Functions (v4.4+):**
```
LIMIT(min : INT, value : INT, max : INT) : INT  (* Clamp value between min and max *)
SEL(g : BOOL, in0 : T, in1 : T) : T            (* Select in0 if g=FALSE, in1 if g=TRUE *)
```

**Modbus Master Functions (v4.4+):**
```
(* Reading functions *)
MB_READ_COIL(slave_id : INT, address : INT) : BOOL
MB_READ_INPUT(slave_id : INT, address : INT) : BOOL
MB_READ_HOLDING(slave_id : INT, address : INT) : INT
MB_READ_INPUT_REG(slave_id : INT, address : INT) : INT

(* Writing functions *)
MB_WRITE_COIL(slave_id : INT, address : INT, value : BOOL) : BOOL
MB_WRITE_HOLDING(slave_id : INT, address : INT, value : INT) : BOOL
```

**Note:** Modbus Master functions require separate RS485 hardware (UART1) and CLI configuration.

**Bitwise Functions:**
```
SHL(x : INT, count : INT) : INT
SHR(x : INT, count : INT) : INT
```

**User-defined functions:** Not yet supported (future enhancement).

---

## 5. Known Limitations & Non-Conformances

### 5.1 Intentional Exclusions (by design)

#### Function Blocks (IEC 6.3.3)
**Status:** Not supported
**Reason:** Embedded platform constraint (RAM footprint)
**Impact:** No stateful reusable objects, no encapsulation via FB
**Workaround:** Use global variables + subroutines (Phase 2)

#### Recursion
**Status:** Not supported (max depth = 2 for safety)
**Reason:** Stack overflow risk on ESP32 (limited stack)
**Impact:** Cannot implement recursive algorithms
**Workaround:** Use iterative (loop-based) implementation

#### Global Variables (VAR in PROGRAM scope)
**Status:** Local variables only (Phase 1)
**Reason:** Modbus register mapping simplification
**Impact:** Logic programs are isolated, communicate via Modbus registers
**Workaround:** Use VAR_INPUT/VAR_OUTPUT mapped to registers

#### User-defined Functions
**Status:** Not yet implemented (Phase 2)
**Reason:** Adds compiler complexity, deferred to next release
**Impact:** Cannot create reusable subroutines
**Workaround:** Inline logic, use FOR loops for iteration

#### Arrays & Structures
**Status:** Not supported (Phase 3)
**Reason:** Modbus register model is flat (no indexing yet)
**Impact:** Cannot store multiple values in single variable
**Workaround:** Use separate Modbus registers (register per variable)

### 5.2 Implementation-Specific Behaviors

#### Type System
- **INT:** Follows C `int32_t` semantics (-2^31 to 2^31-1)
- **DWORD:** Unsigned 32-bit (0 to 2^32-1)
- **REAL:** IEEE 754 32-bit float (matches C `float`)
- **Overflow/Underflow:** Wraps around (no exception)

#### Operator Precedence
Follows standard C precedence (documented in Section 4.1).

#### Short-circuit Evaluation
- `AND`, `OR` use full evaluation (not short-circuit per IEC)
- All operands evaluated regardless of truth value

---

## 6. Mapping to Modbus Registers

### 6.1 Variable Binding

ST variables are bound to Modbus registers/coils:

```structured_text
VAR_INPUT
  counter_value : INT;      (* Reads from holding register HR#100 *)
  relay_enabled : BOOL;     (* Reads from coil C#10 *)
END_VAR

VAR_OUTPUT
  relay_active : BOOL;      (* Writes to coil C#10 *)
  status_code : INT;        (* Writes to holding register HR#101 *)
END_VAR
```

**Binding Configuration:** Defined in Modbus register mapping configuration (separate from ST code).

### 6.2 Type Conversion

- **INT → Modbus HR:** Direct 16-bit or 32-bit representation
- **DWORD → Modbus HR:** Unsigned 32-bit across two registers
- **BOOL → Modbus Coil:** TRUE = 1, FALSE = 0
- **REAL → Modbus Registers:** IEEE 754 32-bit (two registers)

---

## 7. Execution Model

### 7.1 Program Invocation

Logic programs execute in the main Modbus server loop (non-blocking):

```c
while (modbus_running) {
  modbus_server_loop();      // Process Modbus requests

  for (int i = 0; i < 4; i++) {
    if (logic_programs[i].enabled) {
      st_vm_execute(&logic_programs[i].bytecode);  // Execute ST program (~1-5ms)
    }
  }

  delay_ms(10);
}
```

**Cycle Time:** ~10-50 ms per logic program (dependent on program complexity)
**Blocking:** None (non-blocking execution)
**Real-time:** Not guaranteed (cooperative scheduling)

### 7.2 Variable Scoping

- **Local VAR:** Scope = current program execution
- **VAR_INPUT:** Read-only, mapped to Modbus registers (updated at program start)
- **VAR_OUTPUT:** Write-only, mapped to Modbus registers (written at program end)
- **CONST:** Compile-time constants, immutable

### 7.3 Error Handling

- **Compile-time errors:** Reported during program load, program rejected
- **Runtime errors:** Division by zero, stack overflow → program halted, error flag set
- **Recovery:** Manual via CLI (`set logic 1 enabled:false`)

---

## 8. Memory & Resource Constraints

### 8.1 Limits

| Resource | Limit | Notes |
|----------|-------|-------|
| **Max programs** | 4 | Configurable in constants.h |
| **Max program size** | 5000 bytes | ST source code |
| **Max variables/program** | 32 | VAR, VAR_INPUT, VAR_OUTPUT combined |
| **Max statements/program** | ~100 | Depends on statement complexity |
| **Stack depth** | 64 values | VM stack for expression evaluation |
| **Bytecode size** | 512 instructions | Compiled form per program |
| **Max loop depth** | 3 | Nested FOR/WHILE/REPEAT |
| **Recursion depth** | 2 | Function calls (Phase 2) |

### 8.2 Memory Usage Example

4 active programs × 50 KB each = **200 KB** total (out of 520 KB available)
→ Plenty of headroom for Modbus buffers, counters, timers, CLI

---

## 9. Compliance Test Plan

### Test Categories

1. **Lexer Tests:** Tokenization of all keywords, operators, literals
2. **Parser Tests:** AST construction for all statement types
3. **Semantic Tests:** Variable scoping, type checking
4. **Execution Tests:** Correctness of ST program execution
5. **Integration Tests:** Modbus register binding, round-trip I/O

### Test Files

- `test/test_st_lexer_parser.cpp` - Lexer/parser unit tests
- `test/test_st_execution.cpp` - Bytecode VM execution tests
- `test/test_st_modbus_integration.cpp` - Modbus register binding tests

**Status:** Lexer/parser implemented and tested (Phase 1)
**Remaining:** Execution tests (Phase 2)

---

## 10. Implemented Enhancements (v4.3-v4.4)

### v4.3.0 - REAL Arithmetic Support
- ✅ Rounding functions (ROUND, TRUNC, FLOOR, CEIL)
- ✅ Trigonometric functions (SIN, COS, TAN)
- ✅ Type-aware stack operations for REAL values
- ✅ Expression chaining with type conversions

### v4.4.0 - Modbus Master Integration
- ✅ Modbus Master protocol (FC01-06) on UART1
- ✅ 6 built-in MB_* functions for remote I/O
- ✅ Global status variables (mb_last_error, mb_success)
- ✅ Rate limiting to prevent bus overload
- ✅ Clamping function (LIMIT)
- ✅ Selection function (SEL)

## 11. Future Extensions (Phase 3+)

### Phase 3 Enhancements
- User-defined functions (FUNCTION ... END_FUNCTION)
- Arrays and indexing (`var[0], var[1]`)
- Global scope (VAR in PROGRAM)
- Structured types (STRUCT ... END_STRUCT)
- Pointers and dynamic allocation (restricted)
- Time-based operations (TON, TOF timers)
- I/O function blocks

---

## 12. References

- **IEC 61131-3:2013** - International standard for PLC programming languages
  - Part 3: Programming languages
  - Fourth edition (May 2025)
  - Available: https://www.iec.ch/

- **Fernhill Software ST Tutorial** - IEC 61131-3 ST syntax
- **Beckhoff TwinCAT Documentation** - Professional ST implementation reference
- **Arduino ESP32 Documentation** - Hardware capabilities

---

## 13. Document History

| Version | Date | Changes |
|---------|------|---------|
| 1.0.0 | 2025-11-30 | Initial release - Phase 1 (Lexer, Parser, Bytecode VM design) |
| 2.0.0 | 2025-12-24 | Production release - v4.4.0 (REAL arithmetic, trigonometry, Modbus Master, LIMIT/SEL) |

---

## 14. Sign-off

**Project:** ESP32 Modbus RTU Server - Logic Mode
**Compliance Claim:** IEC 61131-3:2013 (ST-Light Profile)
**Approved by:** Development Team
**Date:** 2025-12-24

This document certifies that the Structured Text implementation conforms to the IEC 61131-3 standard within the defined ST-Light profile constraints.

---

*Document Classification: Public - Technical Documentation*
