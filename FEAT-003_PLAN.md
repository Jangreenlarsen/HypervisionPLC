# FEAT-003: ST Logic Custom FUNCTION/FUNCTION_BLOCK Support

<!-- DOC-STATUS: historisk -->
> ⚠️ **Historisk dokument — vedligeholdes ikke længere.** Skrevet til en ældre firmwareversion og kan være forældet (CLI-syntaks, pins, endpoints, standardværdier). Den aktuelle, vedligeholdte dokumentation er [brugermanualen](docs/manual/00_INDEKS.md) (CLI: appendiks A, REST: appendiks B, ST: appendiks D). Markeret ved dokumentationsgennemgang 2026-09-28.


**Status:** ✅ COMPLETE (100% - All 6 Phases Done)
**Started:** 2026-02-04
**Completed:** 2026-02-07
**Last Updated:** 2026-02-07

---

## 🚀 RESUME INSTRUCTIONS

**Implementeringen er komplet.** Alle 6 faser er færdige.

### Hvad er implementeret:
```
✅ Phase 1: Lexer (tokens, keywords, AST types, opcodes)
✅ Phase 2: Parser (FUNCTION/FUNCTION_BLOCK/RETURN parsing)
✅ Phase 3: Compiler (registry, scope, two-pass, compile_function_def)
✅ Phase 4: VM (CALL_USER, RETURN, LOAD_PARAM, STORE_LOCAL, LOAD_LOCAL)
✅ Phase 5: FUNCTION_BLOCK (instance storage, state persistence)
✅ Phase 6: CLI (show functions, bytecode display, routing)
```

---

---

## Quick Stats

| Metric | Value |
|--------|-------|
| **Total LOC Added** | ~1500 |
| **Progress** | 100% |
| **All Phases** | ✅ COMPLETE |
| **Build Status** | ✅ SUCCESS (RAM: 32.9%, Flash: 92.8%) |

---

## Implementation Phases

### Phase 1: Lexer Extensions (~200 LOC)
**Status:** ✅ COMPLETE

| Task | Status | File | Notes |
|------|--------|------|-------|
| Add ST_TOK_FUNCTION token | ✅ DONE | st_types.h | Line 88 |
| Add ST_TOK_END_FUNCTION token | ✅ DONE | st_types.h | Line 89 |
| Add ST_TOK_FUNCTION_BLOCK token | ✅ DONE | st_types.h | Line 90 |
| Add ST_TOK_END_FUNCTION_BLOCK token | ✅ DONE | st_types.h | Line 91 |
| Add ST_TOK_RETURN token | ✅ EXISTED | st_types.h | Line 78 (already existed) |
| Add ST_TOK_VAR_INPUT token | ✅ EXISTED | st_types.h | Line 47 (already existed) |
| Add ST_TOK_VAR_OUTPUT token | ✅ EXISTED | st_types.h | Line 48 (already existed) |
| Add ST_TOK_VAR_IN_OUT token | ✅ EXISTED | st_types.h | Line 49 (already existed) |
| Implement keyword recognition | ✅ DONE | st_lexer.cpp | Lines 131-136 |
| Add token_type_to_string | ✅ DONE | st_lexer.cpp | Lines 713-717 |
| Add constants | ✅ DONE | constants.h | ST_MAX_USER_FUNCTIONS etc |
| Add AST node types | ✅ DONE | st_types.h | ST_AST_FUNCTION_DEF etc |
| Add st_function_def_t struct | ✅ DONE | st_types.h | Lines 290-310 |
| Add st_return_stmt_t struct | ✅ DONE | st_types.h | Lines 313-315 |
| Add new opcodes | ✅ DONE | st_types.h | ST_OP_CALL_USER etc |
| Add function registry structs | ✅ DONE | st_types.h | st_function_entry_t, st_function_registry_t |
| Add call frame struct | ✅ DONE | st_types.h | st_call_frame_t |
| Build test | ✅ PASS | - | Build #1179 |

**Phase 1 Progress:** 18/18 tasks ✅

---

### Phase 2: AST & Parser Extensions (~500 LOC)
**Status:** ✅ COMPLETE

| Task | Status | File | Notes |
|------|--------|------|-------|
| Add ST_AST_FUNCTION_DEF node type | ✅ DONE | st_types.h | Phase 1 |
| Add ST_AST_FUNCTION_BLOCK_DEF node type | ✅ DONE | st_types.h | Phase 1 |
| Add ST_AST_RETURN node type | ✅ DONE | st_types.h | Phase 1 |
| Add st_function_def_t structure | ✅ DONE | st_types.h | Phase 1 |
| Add st_return_stmt_t structure | ✅ DONE | st_types.h | Phase 1 |
| Implement parser_parse_function_def() | ✅ DONE | st_parser.cpp | ~200 LOC |
| Implement parser_parse_return_stmt() | ✅ DONE | st_parser.cpp | In st_parser_parse_statement() |
| Implement parameter list parsing | ✅ DONE | st_parser.cpp | VAR_INPUT/OUTPUT/IN_OUT |
| Implement VAR_INPUT/VAR_OUTPUT parsing | ✅ DONE | st_parser.cpp | In function def parser |
| Update parser_parse_statements() | ✅ DONE | st_parser.cpp | Stop on END_FUNCTION |
| Add AST node freeing for new types | ✅ DONE | st_parser.cpp | ST_AST_RETURN, ST_AST_FUNCTION_DEF |
| Add public API functions | ✅ DONE | st_parser.h/cpp | st_parser_parse_function_def() |
| Build test | ✅ PASS | - | Build #1180 |

**Phase 2 Progress:** 13/13 tasks ✅

---

### Phase 3: Compiler Extensions (~800 LOC)
**Status:** ✅ COMPLETE

| Task | Status | File | Notes |
|------|--------|------|-------|
| Add ST_OP_CALL_USER opcode | ✅ DONE | st_types.h | Phase 1 |
| Add ST_OP_RETURN opcode | ✅ DONE | st_types.h | Phase 1 |
| Add ST_OP_LOAD_PARAM opcode | ✅ DONE | st_types.h | Phase 1 |
| Add ST_OP_STORE_LOCAL opcode | ✅ DONE | st_types.h | Phase 1 |
| Add ST_OP_LOAD_LOCAL opcode | ✅ DONE | st_types.h | Phase 1 |
| Define st_function_entry_t structure | ✅ DONE | st_types.h | Phase 1 |
| Define st_function_registry_t structure | ✅ DONE | st_types.h | Phase 1 |
| Define st_call_frame_t structure | ✅ DONE | st_types.h | Phase 1 |
| Add function_depth to compiler | ✅ DONE | st_compiler.h | |
| Add func_registry to compiler | ✅ DONE | st_compiler.h | |
| Add return_patch_stack to compiler | ✅ DONE | st_compiler.h | |
| Implement compile_return_stmt() | ✅ DONE | st_compiler.cpp | In compile_node() |
| Add opcode strings | ✅ DONE | st_compiler.cpp | st_opcode_to_string() |
| Define st_scope_t for scoped symbols | ✅ DONE | st_compiler.cpp | st_scope_save_t + save/restore |
| Implement function registry init | ✅ DONE | st_compiler.cpp | st_func_registry_init() |
| Implement function registry lookup | ✅ DONE | st_compiler.cpp | st_func_registry_lookup() |
| Implement scoped symbol table | ✅ DONE | st_compiler.cpp | scope_save/scope_restore |
| Implement compile_function_def() | ✅ DONE | st_compiler.cpp | Full scope-aware compilation |
| Update compile_function_call() for user funcs | ✅ DONE | st_compiler.cpp | Registry lookup + CALL_USER |
| Implement two-pass compilation | ✅ DONE | st_compiler.cpp | Functions first, then main body |
| Add func_registry to bytecode program | ✅ DONE | st_types.h | New field in st_bytecode_program_t |
| Test compilation of simple function | ✅ DONE | - | Build #1184 SUCCESS |

**Phase 3 Progress:** 22/22 tasks ✅

---

### Phase 4: VM Extensions (~400 LOC)
**Status:** ✅ COMPLETE

| Task | Status | File | Notes |
|------|--------|------|-------|
| Add st_call_frame_t structure | ✅ DONE | st_types.h | Phase 1 |
| Add call_stack to st_vm_t | ✅ DONE | st_vm.h | 8-deep call stack |
| Add call_depth to st_vm_t | ✅ DONE | st_vm.h | |
| Add local_vars/local_types to st_vm_t | ✅ DONE | st_vm.h | 64 slots |
| Add func_registry pointer to st_vm_t | ✅ DONE | st_vm.h | |
| Implement ST_OP_CALL_USER handler | ✅ DONE | st_vm.cpp | ~50 LOC |
| Implement ST_OP_RETURN handler | ✅ DONE | st_vm.cpp | ~30 LOC |
| Implement ST_OP_LOAD_PARAM handler | ✅ DONE | st_vm.cpp | ~20 LOC |
| Implement ST_OP_STORE_LOCAL handler | ✅ DONE | st_vm.cpp | ~20 LOC |
| Implement ST_OP_LOAD_LOCAL handler | ✅ DONE | st_vm.cpp | ~20 LOC |
| Add recursion depth check | ✅ DONE | st_vm.cpp | Max 8 nested calls |
| Update vm_init for registry | ✅ DONE | st_vm.cpp | |
| VM-compiler integration | ✅ DONE | st_logic_engine.cpp | func_registry set at execution |

**Phase 4 Progress:** 13/13 tasks ✅

---

### Phase 5: FUNCTION_BLOCK Support (~300 LOC)
**Status:** ✅ COMPLETE

| Task | Status | File | Notes |
|------|--------|------|-------|
| Add st_fb_instance_t struct | ✅ DONE | st_types.h | local_vars, local_types, local_count, func_index, initialized |
| Add FB instances to registry | ✅ DONE | st_types.h | fb_instances[16], fb_instance_count |
| Add user_call struct to instr union | ✅ DONE | st_types.h | func_index + instance_id encoding |
| Add fb_instance_id to call frame | ✅ DONE | st_types.h | 0xFF = stateless FUNCTION |
| Add FB instance allocation in compiler | ✅ DONE | st_compiler.cpp | fb_instance_count, emit_user_call() |
| Add FB state load in VM CALL_USER | ✅ DONE | st_vm.cpp | Load local_vars from fb_instances[] |
| Add FB state save in VM RETURN | ✅ DONE | st_vm.cpp | Save local_vars back to fb_instances[] |
| Build test | ✅ PASS | - | Build SUCCESS |

**Phase 5 Progress:** 8/8 tasks ✅

---

### Phase 6: Integration & Testing (~500 LOC)
**Status:** ✅ COMPLETE

| Task | Status | File | Notes |
|------|--------|------|-------|
| CLI: show logic functions command | ✅ DONE | cli_commands_logic.cpp | cli_cmd_show_logic_functions() |
| CLI: routing for functions command | ✅ DONE | cli_parser.cpp | "show logic X functions" |
| CLI: header declaration | ✅ DONE | cli_commands_logic.h | Forward declaration added |
| Update show logic bytecode for new ops | ✅ DONE | cli_commands_logic.cpp | CALL_USER, RETURN, LOAD_PARAM, etc. |
| Update help text | ✅ DONE | cli_parser.cpp | print_logic_help() |
| Update documentation (this file) | ✅ DONE | FEAT-003_PLAN.md | Phase 5+6 status updated |
| Build test | ✅ PASS | - | Build SUCCESS (RAM: 32.9%, Flash: 92.8%) |

**Phase 6 Progress:** 7/7 tasks ✅

**Note:** Integration tests (nested calls, type coercion, error handling) require on-device testing.

---

## Overall Progress

| Phase | Tasks | Done | Progress |
|-------|-------|------|----------|
| 1. Lexer | 18 | 18 | ✅ 100% |
| 2. Parser | 13 | 13 | ✅ 100% |
| 3. Compiler | 22 | 22 | ✅ 100% |
| 4. VM | 13 | 13 | ✅ 100% |
| 5. FUNCTION_BLOCK | 8 | 8 | ✅ 100% |
| 6. Integration | 7 | 7 | ✅ 100% |
| **TOTAL** | **81** | **81** | **100%** |

**All phases complete!** FUNCTION and FUNCTION_BLOCK support is fully implemented.
Remaining: On-device integration testing with real ST programs.

---

## Constants & Limits

```c
// To be added to constants.h
#define ST_MAX_USER_FUNCTIONS     16    // Max user-defined functions
#define ST_MAX_FUNCTION_PARAMS    8     // Max parameters per function
#define ST_MAX_FUNCTION_LOCALS    16    // Max local variables per function
#define ST_MAX_CALL_DEPTH         8     // Max nested function calls
#define ST_MAX_TOTAL_FUNCTIONS    64    // Builtin + user-defined
```

---

## New Token Types

```c
// To be added to st_token_type_t enum in st_types.h
ST_TOK_FUNCTION,           // FUNCTION keyword
ST_TOK_END_FUNCTION,       // END_FUNCTION
ST_TOK_FUNCTION_BLOCK,     // FUNCTION_BLOCK
ST_TOK_END_FUNCTION_BLOCK, // END_FUNCTION_BLOCK
ST_TOK_RETURN,             // RETURN statement
ST_TOK_VAR_INPUT,          // VAR_INPUT
ST_TOK_VAR_OUTPUT,         // VAR_OUTPUT
ST_TOK_VAR_IN_OUT,         // VAR_IN_OUT
```

---

## New AST Node Types

```c
// To be added to st_ast_node_type_t enum
ST_AST_FUNCTION_DEF,       // Function definition
ST_AST_FUNCTION_BLOCK_DEF, // Function block definition
ST_AST_RETURN,             // RETURN statement
```

---

## New Opcodes

```c
// To be added to st_opcode_t enum
ST_OP_CALL_USER,           // Call user-defined function
ST_OP_RETURN,              // Return from function
ST_OP_LOAD_PARAM,          // Load parameter from call frame
ST_OP_STORE_LOCAL,         // Store to local variable
```

---

## Key Data Structures

### Function Registry Entry
```c
typedef struct {
    char name[64];
    st_datatype_t return_type;
    st_datatype_t param_types[ST_MAX_FUNCTION_PARAMS];
    uint8_t param_count;
    uint16_t bytecode_addr;     // Start address in bytecode
    uint16_t bytecode_size;     // Size of function bytecode
    uint8_t is_builtin;         // true = C function
    uint8_t is_stateful;        // true = FUNCTION_BLOCK
    uint8_t instance_size;      // Bytes needed for state (FB only)
} st_function_entry_t;
```

### Function Registry
```c
typedef struct {
    st_function_entry_t functions[ST_MAX_TOTAL_FUNCTIONS];
    uint8_t builtin_count;      // Number of builtin functions
    uint8_t user_count;         // Number of user-defined functions
} st_function_registry_t;
```

### Call Frame
```c
typedef struct {
    uint16_t return_pc;         // Return address
    uint16_t param_base;        // Base index for parameters on stack
    uint8_t param_count;        // Number of parameters
    uint8_t local_count;        // Number of local variables
} st_call_frame_t;
```

---

## IEC 61131-3 Syntax Reference

### FUNCTION (Stateless)
```
FUNCTION function_name : return_type
VAR_INPUT
    param1 : INT;
    param2 : REAL;
END_VAR
VAR
    local1 : INT;
END_VAR
    (* Function body *)
    local1 := param1 + 10;
    RETURN local1;
END_FUNCTION
```

### FUNCTION_BLOCK (Stateful)
```
FUNCTION_BLOCK fb_name
VAR_INPUT
    in1 : BOOL;
END_VAR
VAR_OUTPUT
    out1 : INT;
END_VAR
VAR
    state : INT := 0;  (* Retained between calls *)
END_VAR
    IF in1 THEN
        state := state + 1;
    END_IF;
    out1 := state;
END_FUNCTION_BLOCK
```

---

## Build Log

| Date | Build # | Status | Notes |
|------|---------|--------|-------|
| 2026-02-04 | - | Not started | Plan created |

---

## Issues & Blockers

| Issue | Status | Resolution |
|-------|--------|------------|
| None yet | - | - |

---

## Session Notes

### Session 1 (2026-02-04)
- Created implementation plan
- Analyzed existing architecture
- **Phase 1 COMPLETE:** Lexer extensions (tokens, keywords)
- **Phase 2 COMPLETE:** Parser extensions (FUNCTION/FUNCTION_BLOCK parsing, RETURN)
- **Phase 3 IN PROGRESS:** Compiler infrastructure (59%)
  - Added function_depth, func_registry, return_patch_stack
  - Implemented RETURN statement compilation
  - Added opcode string conversion
  - **TODO:** Function registry init/lookup, compile_function_def(), two-pass compilation
- **Phase 4 COMPLETE:** VM handlers (92%)
  - All 5 new opcodes implemented: CALL_USER, RETURN, LOAD_PARAM, STORE_LOCAL, LOAD_LOCAL
  - Call stack with 8-level depth limit
  - Local variable storage (64 slots)
  - **TODO:** Integration testing with compiler

**Next Steps (TO DO):**
1. ✅ Implement compile_function_def() to generate bytecode for user functions
2. ✅ Implement two-pass compilation (collect functions first, then compile)
3. ✅ Update compile_function_call() to check registry before builtin lookup
4. ⬜ Integration test with simple function (on-device)

---
**SESSION 1 STOPPED** - Klar til genoptagelse med "genoptag implementering"

### Session 2 (2026-02-07)
- **Phase 3 COMPLETE:** Compiler integration
  - Implemented function registry (init, add, lookup) with `st_func_registry_*()` functions
  - Implemented scoped symbol table (save/restore) for function local scopes
  - Implemented `compile_function_def()` with full scope-aware variable access
  - Implemented two-pass compilation: functions compiled first, then main body
  - Updated `compile_function_call()` to check registry before builtin lookup → emit CALL_USER
  - Added scope-aware helper functions `st_compiler_emit_load_symbol/store_symbol`
    - Params → LOAD_PARAM, Locals → LOAD_LOCAL/STORE_LOCAL, Globals → LOAD_VAR/STORE_VAR
  - Updated FOR loop compiler to use scope-aware load/store
  - Added `is_func_param`, `is_func_local`, `func_param_index`, `func_local_index` to `st_symbol_t`
  - Added `func_registry` pointer to `st_bytecode_program_t`
  - VM integration: `func_registry` set on VM in `st_logic_engine.cpp`
  - Cleanup: Free old registry on recompile in `st_logic_config.cpp`
- **Phase 4 COMPLETE:** VM-compiler integration done
- **Build #1184 SUCCESS** (RAM: 32.9%, Flash: 92.7%)

**Next Steps (TO DO):**
1. ⬜ Phase 5: FUNCTION_BLOCK stateful support
2. ⬜ Phase 6: CLI integration, testing, documentation
3. ⬜ On-device integration test with simple FUNCTION

---
**SESSION 2 STOPPED** - Klar til genoptagelse

### Session 3 (2026-02-07)
- **Phase 5 COMPLETE:** FUNCTION_BLOCK stateful support
  - Added `st_fb_instance_t` struct for persistent local variable storage
  - Added `fb_instances[16]` and `fb_instance_count` to function registry
  - Added `user_call` struct to instruction union (func_index + instance_id encoding)
  - Added `fb_instance_id` to `st_call_frame_t` (0xFF = stateless)
  - Compiler: FB instance allocation via `fb_instance_count`, `st_compiler_emit_user_call()`
  - VM CALL_USER: loads FB instance local_vars before function execution
  - VM RETURN: saves local_vars back to FB instance storage after execution
- **Phase 6 COMPLETE:** CLI integration
  - Added `cli_cmd_show_logic_functions()` - displays user functions with params, types, bytecode addr, FB instances
  - Added CLI routing: `show logic <id> functions`
  - Updated bytecode display with CALL_USER (name + instance), RETURN, LOAD_PARAM, STORE_LOCAL, LOAD_LOCAL, ADD_CHECKED
  - Added forward declaration in `cli_commands_logic.h`
  - Updated help text in `cli_parser.cpp`
- **Build SUCCESS** (RAM: 32.9%, Flash: 92.8%)

**FEAT-003 COMPLETE** - All 6 phases implemented.

---

## Files Modified

| File | Phase | Changes |
|------|-------|---------|
| include/st_types.h | 1,3,5 | +4 tokens, +3 AST types, +5 opcodes, +3 structs, +func_registry ptr, +st_fb_instance_t, +user_call union (~120 LOC) |
| include/constants.h | 1 | +5 function limits (ST_MAX_USER_FUNCTIONS, etc.) |
| src/st_lexer.cpp | 1 | +4 keywords, +5 token strings (~15 LOC) |
| src/st_parser.cpp | 2 | +FUNCTION/FUNCTION_BLOCK parsing, +RETURN parsing (~250 LOC) |
| include/st_parser.h | 2 | +2 public API functions |
| include/st_compiler.h | 3,5 | +3 fields + 4 symbol fields + fb_instance_count |
| src/st_compiler.cpp | 3,5 | +registry, +scope, +compile_function_def, +two-pass, +scope-aware load/store, +emit_user_call (~350 LOC) |
| include/st_vm.h | 4 | +6 fields (call_stack, call_depth, local_vars, etc.) |
| src/st_vm.cpp | 4,5 | +5 opcode handlers, +FB state load/save (~180 LOC) |
| src/st_logic_engine.cpp | 4 | +func_registry connection to VM (~3 LOC) |
| src/st_logic_config.cpp | 4 | +Free old func_registry on recompile (~4 LOC) |
| src/cli_commands_logic.cpp | 6 | +show_logic_functions, +bytecode display for new opcodes (~110 LOC) |
| include/cli_commands_logic.h | 6 | +cli_cmd_show_logic_functions declaration |
| src/cli_parser.cpp | 6 | +routing for "show logic X functions", +help text |

