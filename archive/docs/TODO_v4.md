# ESP32 Modbus RTU Server - TODO Liste

**Version:** v4.5.0 → v4.6.0+
**Opdateret:** 2025-12-31
**Status:** Production Ready → Feature Enhancement

---

## 🔥 PRIORITY 1: KRITISKE FEATURES (v4.5.1)

### ✅ DONE - Build #910
- [x] BUG-131: CLI `set id` kommando fix
- [x] BUG-132: CLI `set baud` kommando fix
- [x] System feature dybdeanalyse (135 features verificeret)

### 🚧 IN PROGRESS

**Ingen aktive critical tasks** - Systemet er production ready.

---

## 🎯 PRIORITY 2: MODBUS MASTER HÅNDTERING (v4.5.1)

**Mål:** Forbedret håndtering af remote Modbus kald fra Master interface (UART1)

### Task 1: Analyse af Nuværende Implementation
**Status:** ⏳ TODO
**Estimated:** 1 time
**Description:**
- Læs `src/modbus_master.cpp` - forstå nuværende request/response håndtering
- Læs `src/st_builtins.cpp` - MB_READ_*/MB_WRITE_* funktioner
- Dokumentér current flow: ST Logic → MB function → UART1 → Response → ST variable
- Identificér potentielle problemer:
  - Timeout håndtering
  - Error recovery
  - Request rate limiting
  - Response buffer management
  - Multi-device polling

**Files:**
- `src/modbus_master.cpp` - Core master implementation
- `src/st_builtins.cpp` - MB_* builtin functions
- `include/modbus_master.h` - Interface definitions

**Output:** Dokumentation i `MODBUS_MASTER_ANALYSIS.md`

### Task 2: Identificér Issues og Manglende Features
**Status:** ⏳ TODO
**Estimated:** 30 min
**Description:**
- Er timeout håndtering robust nok?
- Hvad sker der ved consecutive failures?
- Kan vi håndtere multiple slaves effektivt?
- Er request queue implementation optimal?
- Mangler vi retry logic?
- Error propagation til ST Logic korrekt?

**Success Criteria:**
- Liste af identificerede issues
- Prioriteret liste af forbedringer

### Task 3: Design Forbedringer
**Status:** ⏳ TODO
**Estimated:** 1 time
**Description:**
Baseret på Task 2 analyse, design forbedringer:
- **Option 1:** Request Queue System
  - FIFO queue for MB requests
  - Async response handling
  - Retry logic med exponential backoff

- **Option 2:** Round-Robin Multi-Slave Polling
  - Fair scheduling mellem devices
  - Health monitoring per slave

- **Option 3:** Enhanced Error Recovery
  - Auto-reconnect ved persistent failures
  - Slave blacklisting (temporary disable)
  - Error statistics per slave

**Output:** Design doc i `MODBUS_MASTER_ENHANCEMENTS.md`

### Task 4: Implementér Valgte Forbedringer
**Status:** ⏳ TODO
**Estimated:** 2-4 timer (afhængig af scope)
**Description:**
- Implementér design fra Task 3
- Opdater ST builtins med nye fejlkoder hvis nødvendigt
- Tilføj CLI kommandoer til diagnostics
- Test med multiple slaves
- Update documentation

**Files to Modify:**
- `src/modbus_master.cpp`
- `src/st_builtins.cpp` (hvis MB_* API ændres)
- `include/modbus_master.h`
- `src/cli_show.cpp` (diagnostics)

**Test Cases:**
```bash
# Test 1: Multiple slave polling
MB_READ_HOLDING(1, 100)  # Slave 1
MB_READ_HOLDING(2, 100)  # Slave 2
MB_READ_HOLDING(3, 100)  # Slave 3

# Test 2: Timeout recovery
MB_READ_HOLDING(99, 100)  # Non-existent slave

# Test 3: Request rate limiting
FOR i := 1 TO 100 DO
  temp := MB_READ_HOLDING(1, i);
END_FOR
```

---

## 📦 PRIORITY 3: FEATURE ENHANCEMENTS (v4.6.0)

### FEAT-001: `set reg STATIC` Multi-Register Type Support
**Status:** ✅ DONE
**Priority:** 🟠 MEDIUM
**Completed:** Build #966
**Target:** v4.7.1

**Description:**
Udvid `set reg STATIC` til at understøtte DINT/DWORD/REAL typer (ikke kun uint16_t).

**Current:**
```bash
set reg STATIC 100 Value 1234  # Kun uint16_t
```

**New:**
```bash
set reg STATIC 100 Value uint 1234
set reg STATIC 100 Value int -500
set reg STATIC 100 Value dint 100000    # 2 registers
set reg STATIC 100 Value dword 500000   # 2 registers
set reg STATIC 100 Value real 3.14159   # 2 registers
```

**Implementation Checklist:**
- [ ] Udvid `StaticRegisterConfig` struct med type field (`include/types.h`)
- [ ] Opdater `cli_cmd_set_reg_static()` parser (`src/cli_config_regs.cpp`)
- [ ] Opdater `config_apply.cpp` - multi-register restore ved boot
- [ ] Opdater `register_allocator.cpp` - multi-register range tracking
- [ ] Schema version bump: v8 → v9 (`include/constants.h`)
- [ ] Migration logic i `config_load.cpp` (v8 → v9)
- [ ] Test backward compatibility
- [ ] Test DINT persistence (100000)
- [ ] Test REAL persistence (3.14159)
- [ ] Test register conflict detection

**Files:**
1. `include/types.h` - StaticRegisterConfig struct
2. `src/cli_config_regs.cpp` - Parser
3. `src/config_apply.cpp` - Boot restore
4. `src/register_allocator.cpp` - Multi-register tracking
5. `include/constants.h` - Schema version
6. `src/config_load.cpp` - Migration

**Documentation:**
- Update `BUGS.md` - Mark FEAT-001 as IMPLEMENTED
- Update `README.md` - New syntax examples
- Update `CHANGELOG.md` - v4.6.0 entry

---

## 🔧 PRIORITY 4: ST LOGIC ENHANCEMENTS (v4.7.0)

### Task 1: ROL/ROR Bitwise Operators
**Status:** ⏳ TODO
**Priority:** 🟠 MEDIUM
**Estimated:** 2 timer
**Target:** v4.7.0

**Description:**
Tilføj rotate left (ROL) og rotate right (ROR) bitwise operators til ST Logic.

**Syntax:**
```structured-text
result := ROL(value, shift_amount);  (* Rotate left *)
result := ROR(value, shift_amount);  (* Rotate right *)
```

**Implementation:**
- [ ] Add ROL/ROR tokens til lexer (`st_lexer.cpp`)
- [ ] Add parser support (`st_parser.cpp`)
- [ ] Add VM opcodes OP_ROL, OP_ROR (`st_vm.h`)
- [ ] Implement VM execution (`st_vm.cpp`)
- [ ] Add shift amount validation (0-31)
- [ ] Test cases: ROL(0x1234, 4), ROR(0x8000, 1)

**Files:**
- `src/st_lexer.cpp` - Tokens
- `src/st_parser.cpp` - Parser
- `include/st_vm.h` - Opcodes
- `src/st_vm.cpp` - Implementation

**Test:**
```structured-text
VAR
  x: DWORD := 16#12345678;
END_VAR

x := ROL(x, 8);   (* Expected: 16#34567812 *)
x := ROR(x, 4);   (* Expected: 16#23456781 *)
```

---

## 🚀 PRIORITY 5: ADVANCED FEATURES (v5.0.0+)

### Future Enhancements (Post v4.x)

#### 1. IEC 61131-3 STRING Type
**Status:** 📋 PLANNED
**Priority:** 🟡 HIGH (for v5.0.0)
**Estimated:** 1-2 dage

**Description:**
Add native STRING type support til ST Logic.

**Features:**
- `VAR str: STRING[80]; END_VAR` - Fixed-length strings
- String literals: `str := 'Hello World';`
- Concatenation: `result := str1 + str2;`
- Length: `len := LEN(str);`
- Substring: `sub := MID(str, start, count);`
- Comparison: `IF str = 'test' THEN`

**Challenges:**
- Memory management (stack allocation?)
- Register mapping (multi-register? eller separate string memory?)
- Type safety (prevent buffer overflows)

**Files to Create/Modify:**
- `st_types.h` - STRING type definition
- `st_vm.cpp` - String operations
- `st_builtins.cpp` - LEN, MID, LEFT, RIGHT, CONCAT

#### 2. ARRAY Support
**Status:** 📋 PLANNED
**Priority:** 🟡 HIGH (for v5.0.0)
**Estimated:** 2-3 dage

**Description:**
Static array support for grouped data.

**Syntax:**
```structured-text
VAR
  temperatures: ARRAY[1..10] OF REAL;
  flags: ARRAY[0..15] OF BOOL;
END_VAR

temperatures[1] := 23.5;
temperatures[2] := 24.1;

FOR i := 1 TO 10 DO
  avg := avg + temperatures[i];
END_FOR;
```

**Features:**
- 1D arrays (multidimensional senere)
- Compile-time bounds checking
- Runtime index validation
- Multi-register bindings (array → consecutive Modbus registers)

**Challenges:**
- Memory allocation (max array size?)
- Index bounds checking overhead
- Register allocator complexity

#### 3. User-Defined FUNCTION Blocks
**Status:** 📋 PLANNED
**Priority:** 🟠 MEDIUM (for v5.1.0)
**Estimated:** 3-5 dage

**Description:**
Tillad brugere at definere genbrugelige funktioner.

**Syntax:**
```structured-text
FUNCTION avg_two : REAL
  VAR_INPUT
    a: REAL;
    b: REAL;
  END_VAR

  avg_two := (a + b) / 2.0;
END_FUNCTION

(* Usage: *)
VAR
  result: REAL;
END_VAR

result := avg_two(10.5, 20.3);
```

**Features:**
- FUNCTION declaration syntax
- Local variables (VAR_LOCAL)
- Return value via function name assignment
- Function call parsing og compilation

**Challenges:**
- Symbol table scoping
- Stack frame management
- Recursive call prevention (eller depth limit?)

#### 4. STRUCT Types
**Status:** 📋 PLANNED
**Priority:** 🟠 MEDIUM (for v5.1.0)
**Estimated:** 4-6 dage

**Description:**
Structured data types for grouped variables.

**Syntax:**
```structured-text
TYPE
  TankData: STRUCT
    level: REAL;
    temp: REAL;
    pressure: INT;
    alarm: BOOL;
  END_STRUCT
END_TYPE

VAR
  tank1: TankData;
  tank2: TankData;
END_VAR

tank1.level := 75.5;
tank1.temp := 22.3;

IF tank1.level > 90.0 THEN
  tank1.alarm := TRUE;
END_IF;
```

**Features:**
- TYPE declaration section
- Struct member access (dot notation)
- Nested structs
- Struct assignment: `tank2 := tank1;`

**Challenges:**
- Memory layout (padding, alignment)
- Register mapping (struct → Modbus registers)
- Symbol table complexity

---

## 📝 EXCLUDED FEATURES

### ❌ Wi-Fi AP Mode
**Status:** ❌ NOT IMPLEMENTING
**Reason:** Systemet kører kun i client mode (STA mode)
**Workaround:** Brug eksisterende Wi-Fi STA mode med router

---

## 🔄 WORKFLOW

### Development Cycle:
1. **Analyse** - Forstå problem/feature request
2. **Design** - Planlæg implementation (opdater dette dokument)
3. **Implement** - Skriv kode
4. **Test** - Verificer funktionalitet
5. **Document** - Opdater README, CHANGELOG, BUGS
6. **Commit** - Git commit med clear message
7. **Build** - Increment build number

### Before ANY Code Change:
1. ✅ Check `BUGS_INDEX.md` for related bugs
2. ✅ Read `CLAUDE_WORKFLOW.md` for guidelines
3. ✅ Update this TODO.md with progress

### Commit Message Format:
```
FEAT: Description (Build #XXX)

Implementation details...

Files:
- file1.cpp - changes
- file2.h - changes

Tests:
- Test case 1 ✅
- Test case 2 ✅

🤖 Generated with Claude Code
Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>
```

---

## 📊 Progress Tracking

### v4.5.1 (Current Sprint)
- [x] BUG-131 fix (Build #910)
- [x] BUG-132 fix (Build #910)
- [x] System feature analysis (SYSTEM_FEATURE_ANALYSIS.md)
- [ ] Modbus Master håndtering analyse
- [ ] Modbus Master enhancements (if needed)

### v4.6.0 (Next Release)
- [ ] FEAT-001: set reg STATIC multi-register support
- [ ] ROL/ROR bitwise operators
- [ ] Enhanced Modbus Master diagnostics

### v5.0.0 (Major Release)
- [ ] STRING type support
- [ ] ARRAY support

### v5.1.0 (Advanced Features)
- [ ] User-defined FUNCTIONs
- [ ] STRUCT types

---

## 🎯 Current Focus

**RIGHT NOW:** Analysér Modbus Master håndtering

**NEXT UP:** FEAT-001 implementation

**LONG TERM:** v5.0.0 type system enhancements

---

**Last Updated:** 2025-12-31
**Maintained By:** Claude Code (Sonnet 4.5)
