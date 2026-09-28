# ST Logic Debugger Guide (FEAT-008)

<!-- DOC-STATUS: historisk -->
> ⚠️ **Historisk dokument — vedligeholdes ikke længere.** Skrevet til en ældre firmwareversion og kan være forældet (CLI-syntaks, pins, endpoints, standardværdier). Den aktuelle, vedligeholdte dokumentation er [brugermanualen](docs/manual/00_INDEKS.md) (CLI: appendiks A, REST: appendiks B, ST: appendiks D). Markeret ved dokumentationsgennemgang 2026-09-28.


**Version:** v5.3.0 | **Build:** #1082-1083 | **Date:** 2026-01-19

---

## Overview

The ST Logic Debugger provides interactive debugging capabilities for ST (Structured Text) programs running on the ESP32. It allows you to pause execution, single-step through instructions, set breakpoints, and inspect variable values.

### Features

- **Pause/Continue** - Stop execution at any point and resume
- **Single-Step** - Execute one bytecode instruction at a time
- **Breakpoints** - Set up to 8 breakpoints per program at specific PC addresses
- **Variable Inspection** - View all variable values when paused
- **Instruction Disassembly** - See the current instruction at PC
- **Debug Statistics** - Track total steps debugged and breakpoints hit

---

## CLI Commands Reference

### SET Commands (Control)

| Command | Description |
|---------|-------------|
| `set logic <id> debug pause` | Pause program at next instruction |
| `set logic <id> debug continue` | Continue execution until breakpoint or halt |
| `set logic <id> debug step` | Execute one instruction, then pause |
| `set logic <id> debug stop` | Stop debugging, return to normal execution |
| `set logic <id> debug break <pc>` | Add breakpoint at PC address |
| `set logic <id> debug clear` | Clear all breakpoints |
| `set logic <id> debug clear <pc>` | Clear specific breakpoint at PC |

### SHOW Commands (Inspection)

| Command | Description |
|---------|-------------|
| `show logic <id> debug` | Show debug state (mode, PC, breakpoints, stats) |
| `show logic <id> debug vars` | Show all variable values (requires pause) |
| `show logic <id> debug stack` | Show stack depth (stack inspection disabled to save RAM) |

**Note:** `<id>` is the program number 1-4.

---

## Debug Workflow

### Basic Single-Step Debugging

```
1. Upload and enable a program:
   set logic 1 upload "PROGRAM test VAR x: INT; END_VAR x := x + 1; END_PROGRAM"
   set logic 1 enabled:true

2. Pause execution:
   set logic 1 debug pause

3. View state:
   show logic 1 debug

4. Single-step through instructions:
   set logic 1 debug step
   show logic 1 debug vars    (see x value change)

5. Continue normal execution:
   set logic 1 debug stop
```

### Using Breakpoints

```
1. View bytecode to find PC addresses:
   show logic 1 bytecode

2. Set breakpoint at PC=5:
   set logic 1 debug break 5

3. Start debugging with continue:
   set logic 1 debug pause
   set logic 1 debug continue

4. Program will pause when PC reaches 5:
   show logic 1 debug         (verify stopped at PC=5)
   show logic 1 debug vars    (inspect variables)

5. Clear breakpoint and continue:
   set logic 1 debug clear 5
   set logic 1 debug continue
```

---

## Debug States

| State | Description |
|-------|-------------|
| `OFF` | Normal execution, no debugging active |
| `PAUSED` | Execution stopped, waiting for step/continue |
| `STEP` | Will execute one instruction then pause |
| `RUN` | Running until breakpoint or halt |

### State Transitions

```
OFF ──pause──> STEP ──execute──> PAUSED
                 │
PAUSED ──step──> STEP ──execute──> PAUSED
                 │
PAUSED ──continue──> RUN ──breakpoint──> PAUSED
                      │
                      └──halt/error──> PAUSED

PAUSED ──stop──> OFF
```

---

## Pause Reasons

When a program is paused, the debug state shows why:

| Reason | Description |
|--------|-------------|
| `pause command` | User requested pause via CLI |
| `single-step` | One instruction completed in step mode |
| `breakpoint` | Hit a breakpoint at current PC |
| `program halt` | Program reached HALT instruction normally |
| `runtime error` | Program encountered an error (div-by-zero, etc.) |

---

## Debug Output Examples

### `show logic 1 debug`

```
=== Logic1 Debug State ===

Mode: PAUSED
PC: 5 / 12
Stack Depth: 2 / 64
Steps Executed: 127
Pause Reason: breakpoint
Hit Breakpoint PC: 5

Breakpoints (1)

  [0] PC=5

[PC=5] LOAD_VAR 0
```

### `show logic 1 debug vars`

```
=== Logic1 Variables ===

=== Variables ===

  counter = 42 (INT)
  temperature = 23.5 (REAL)
  running = TRUE
  total = 1000000 (DINT)
```

---

## Memory Usage

The debugger uses a compact snapshot design to minimize RAM usage:

| Component | Size | Notes |
|-----------|------|-------|
| Debug state per program | ~450 bytes | Includes snapshot |
| Snapshot | ~400 bytes | 32 variables + types + error msg |
| Breakpoints | 16 bytes | 8 × uint16_t |
| Total for 4 programs | ~1.8 KB | Minimal impact on 320KB RAM |

**Design Decision:** Stack values are NOT stored in snapshot to save RAM. Use `show logic <id> debug vars` to see variable values instead.

---

## Bytecode Instructions Reference

When stepping through code, you'll see these instruction types:

| Opcode | Description | Example |
|--------|-------------|---------|
| `PUSH_BOOL` | Push boolean constant | `PUSH_BOOL TRUE` |
| `PUSH_INT` | Push integer constant | `PUSH_INT 42` |
| `PUSH_REAL` | Push float constant | `PUSH_REAL 3.14` |
| `PUSH_DWORD` | Push 32-bit constant | `PUSH_DWORD 0x0000FFFF` |
| `LOAD_VAR` | Load variable to stack | `LOAD_VAR 0` (variable index) |
| `STORE_VAR` | Store stack top to variable | `STORE_VAR 1` |
| `ADD/SUB/MUL/DIV` | Arithmetic operations | `ADD` |
| `AND/OR/XOR/NOT` | Boolean operations | `AND` |
| `EQ/NE/LT/GT/LE/GE` | Comparison operations | `LT` |
| `JMP` | Unconditional jump | `JMP 10` |
| `JMP_IF_FALSE` | Conditional jump | `JMP_IF_FALSE 15` |
| `CALL_BUILTIN` | Call built-in function | `CALL_BUILTIN func=5` |
| `HALT` | End program | `HALT` |
| `NOP` | No operation | `NOP` |

---

## Troubleshooting

### "ERROR: Program not compiled"
Upload and compile a program before debugging:
```
set logic 1 upload "PROGRAM test ... END_PROGRAM"
```

### "ERROR: No valid snapshot (program not paused)"
The program must be paused to inspect variables:
```
set logic 1 debug pause
```

### "ERROR: PC out of range"
The breakpoint PC address must be within the program's instruction count. Use `show logic 1 bytecode` to see valid addresses.

### "Max breakpoints reached"
Each program supports up to 8 breakpoints. Clear unused breakpoints:
```
set logic 1 debug clear
```

### Debug mode affects timing
When debugging is active, execution timing is affected. Use `set logic 1 debug stop` to return to normal timing-accurate execution.

---

## Implementation Details

### Files

| File | Purpose |
|------|---------|
| `include/st_debug.h` | Debug state structures and enums |
| `src/st_debug.cpp` | Debug API and display functions |
| `src/st_logic_engine.cpp` | Debug-aware execution loop |
| `src/cli_commands_logic.cpp` | CLI command handlers |
| `src/cli_parser.cpp` | Command parsing and dispatch |

### Debug State Structure

```c
typedef struct {
  st_debug_mode_t mode;              // OFF, PAUSED, STEP, RUN
  uint16_t breakpoints[8];           // PC addresses (0xFFFF = disabled)
  uint8_t breakpoint_count;
  uint8_t watch_var_index;           // Reserved for future use
  st_debug_snapshot_t snapshot;      // Compact VM state copy
  bool snapshot_valid;
  st_debug_reason_t pause_reason;    // Why we paused
  uint16_t hit_breakpoint_pc;        // Which breakpoint was hit
  uint32_t total_steps_debugged;     // Statistics
  uint32_t breakpoints_hit_count;
} st_debug_state_t;
```

### Snapshot Structure (Compact)

```c
typedef struct {
  uint16_t pc;                  // Program counter
  uint8_t sp;                   // Stack pointer (depth only)
  uint8_t halted;               // Execution halted flag
  uint8_t error;                // Error flag
  uint32_t step_count;          // Steps executed
  uint8_t var_count;            // Number of variables
  st_value_t variables[32];     // Variable values (256 bytes)
  st_datatype_t var_types[32];  // Variable types (32 bytes)
  char error_msg[64];           // Truncated error message
} st_debug_snapshot_t;
```

---

## Version History

| Version | Build | Changes |
|---------|-------|---------|
| v5.3.0 | #1082 | Initial FEAT-008 implementation |
| v5.3.0 | #1083 | BUG-190: Fixed step counter (debug mode only) |
| v5.3.0 | #1083 | BUG-191: Fixed halt/error snapshot saving |

---

## See Also

- `show logic <id> bytecode` - View compiled bytecode
- `show logic <id> st` - View ST source code
- `show logic <id>` - View program status and statistics
- [ST_COMPLETE_TEST_PLAN.md](docs/ARCHIVED/ST_COMPLETE_TEST_PLAN.md) - Test cases for debugger
