// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file st_debug.cpp
 * @brief ST Logic Debugger Implementation
 *
 * FEAT-008: Provides debugging capabilities for ST Logic programs.
 */

#include "st_debug.h"
#include "st_logic_config.h"
#include "debug.h"
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * SHARED DEBUG VM (dynamically allocated to save RAM when not debugging)
 * ============================================================================ */

st_shared_debug_vm_t g_shared_debug_vm = {
  .vm = nullptr,
  .program_id = 0xFF,
  .valid = false
};

bool st_debug_alloc_vm(void) {
  if (g_shared_debug_vm.vm != nullptr) {
    return true;  // Already allocated
  }

  g_shared_debug_vm.vm = (st_vm_t*)malloc(sizeof(st_vm_t));
  if (g_shared_debug_vm.vm == nullptr) {
    debug_println("ERROR: Failed to allocate debug VM");
    return false;
  }

  memset(g_shared_debug_vm.vm, 0, sizeof(st_vm_t));
  return true;
}

void st_debug_free_vm(void) {
  if (g_shared_debug_vm.vm != nullptr) {
    free(g_shared_debug_vm.vm);
    g_shared_debug_vm.vm = nullptr;
  }
  g_shared_debug_vm.valid = false;
  g_shared_debug_vm.program_id = 0xFF;
}

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

void st_debug_init(st_debug_state_t *debug) {
  if (!debug) return;

  debug->mode = ST_DEBUG_OFF;
  debug->breakpoint_count = 0;
  debug->watch_var_index = 0xFF;
  debug->snapshot_valid = false;
  debug->pause_reason = ST_DEBUG_REASON_NONE;
  debug->hit_breakpoint_pc = 0;
  debug->total_steps_debugged = 0;
  debug->breakpoints_hit_count = 0;
  debug->owns_debug_vm = false;

  // Initialize all breakpoints as disabled
  for (int i = 0; i < ST_DEBUG_MAX_BREAKPOINTS; i++) {
    debug->breakpoints[i] = ST_DEBUG_BP_DISABLED;
  }
}

/* ============================================================================
 * DEBUG CONTROL
 * ============================================================================ */

void st_debug_pause(st_debug_state_t *debug) {
  if (!debug) return;

  // If already off, set to run mode first so next cycle will pause
  if (debug->mode == ST_DEBUG_OFF) {
    debug->mode = ST_DEBUG_RUN;
  }

  // Request pause at next instruction
  debug->pause_reason = ST_DEBUG_REASON_PAUSE_CMD;

  // If currently running, switch to step mode to pause after current instruction
  if (debug->mode == ST_DEBUG_RUN) {
    debug->mode = ST_DEBUG_STEP;
  }
}

void st_debug_continue(st_debug_state_t *debug) {
  if (!debug) return;

  debug->mode = ST_DEBUG_RUN;
  debug->snapshot_valid = false;  // Snapshot no longer valid
}

void st_debug_step(st_debug_state_t *debug) {
  if (!debug) return;

  debug->mode = ST_DEBUG_STEP;
  debug->snapshot_valid = false;  // Will be updated after step
}

void st_debug_stop(st_debug_state_t *debug) {
  if (!debug) return;

  // Release and free shared debug VM if we own it
  if (debug->owns_debug_vm) {
    st_debug_free_vm();
    debug->owns_debug_vm = false;
  }

  debug->mode = ST_DEBUG_OFF;
  debug->snapshot_valid = false;
  debug->pause_reason = ST_DEBUG_REASON_NONE;
}

/* ============================================================================
 * BREAKPOINT MANAGEMENT
 * ============================================================================ */

bool st_debug_add_breakpoint(st_debug_state_t *debug, uint16_t pc) {
  if (!debug) return false;

  // Check if already exists
  for (int i = 0; i < ST_DEBUG_MAX_BREAKPOINTS; i++) {
    if (debug->breakpoints[i] == pc) {
      return true;  // Already exists
    }
  }

  // Find empty slot
  for (int i = 0; i < ST_DEBUG_MAX_BREAKPOINTS; i++) {
    if (debug->breakpoints[i] == ST_DEBUG_BP_DISABLED) {
      debug->breakpoints[i] = pc;
      debug->breakpoint_count++;
      return true;
    }
  }

  return false;  // No empty slots
}

bool st_debug_remove_breakpoint(st_debug_state_t *debug, uint16_t pc) {
  if (!debug) return false;

  for (int i = 0; i < ST_DEBUG_MAX_BREAKPOINTS; i++) {
    if (debug->breakpoints[i] == pc) {
      debug->breakpoints[i] = ST_DEBUG_BP_DISABLED;
      debug->breakpoint_count--;
      return true;
    }
  }

  return false;  // Not found
}

void st_debug_clear_breakpoints(st_debug_state_t *debug) {
  if (!debug) return;

  for (int i = 0; i < ST_DEBUG_MAX_BREAKPOINTS; i++) {
    debug->breakpoints[i] = ST_DEBUG_BP_DISABLED;
  }
  debug->breakpoint_count = 0;
}

bool st_debug_check_breakpoint(st_debug_state_t *debug, uint16_t pc) {
  if (!debug || debug->breakpoint_count == 0) return false;

  for (int i = 0; i < ST_DEBUG_MAX_BREAKPOINTS; i++) {
    if (debug->breakpoints[i] == pc) {
      return true;
    }
  }

  return false;
}

/* ============================================================================
 * SNAPSHOT MANAGEMENT
 * ============================================================================ */

void st_debug_save_snapshot(st_debug_state_t *debug, const st_vm_t *vm, int reason) {
  if (!debug || !vm) return;

  // Copy essential data to compact snapshot
  debug->snapshot.pc = vm->pc;
  debug->snapshot.sp = vm->sp;
  debug->snapshot.halted = vm->halted;
  debug->snapshot.error = vm->error;
  debug->snapshot.step_count = vm->step_count;
  debug->snapshot.var_count = vm->var_count;

  // Copy variables and types
  memcpy(debug->snapshot.variables, vm->variables, sizeof(debug->snapshot.variables));

  // Copy variable types from bytecode (if available)
  if (vm->program) {
    memcpy(debug->snapshot.var_types, vm->program->var_types, sizeof(debug->snapshot.var_types));
  }

  // Truncate error message
  if (vm->error_msg[0]) {
    strncpy(debug->snapshot.error_msg, vm->error_msg, sizeof(debug->snapshot.error_msg) - 1);
    debug->snapshot.error_msg[sizeof(debug->snapshot.error_msg) - 1] = '\0';
  } else {
    debug->snapshot.error_msg[0] = '\0';
  }

  debug->snapshot_valid = true;
  debug->pause_reason = (st_debug_reason_t)reason;
}

void st_debug_clear_snapshot(st_debug_state_t *debug) {
  if (!debug) return;

  debug->snapshot_valid = false;
}

/* ============================================================================
 * DISPLAY FUNCTIONS
 * ============================================================================ */

static const char* st_debug_mode_str(st_debug_mode_t mode) {
  switch (mode) {
    case ST_DEBUG_OFF:    return "OFF";
    case ST_DEBUG_PAUSED: return "PAUSED";
    case ST_DEBUG_STEP:   return "STEP";
    case ST_DEBUG_RUN:    return "RUN";
    default:              return "UNKNOWN";
  }
}

static const char* st_debug_reason_str(int reason) {
  switch (reason) {
    case ST_DEBUG_REASON_NONE:       return "none";
    case ST_DEBUG_REASON_PAUSE_CMD:  return "pause command";
    case ST_DEBUG_REASON_STEP:       return "single-step";
    case ST_DEBUG_REASON_BREAKPOINT: return "breakpoint";
    case ST_DEBUG_REASON_HALT:       return "program halt";
    case ST_DEBUG_REASON_ERROR:      return "runtime error";
    default:                         return "unknown";
  }
}

void st_debug_print_state(st_debug_state_t *debug, void *prog_ptr) {
  st_logic_program_config_t *prog = (st_logic_program_config_t *)prog_ptr;
  if (!debug) {
    debug_println("ERROR: Debug state is NULL");
    return;
  }

  debug_println("\n=== Debug State ===\n");

  debug_print("Mode: ");
  debug_println(st_debug_mode_str(debug->mode));

  if (debug->snapshot_valid) {
    debug_print("PC: ");
    debug_print_uint(debug->snapshot.pc);
    if (prog && prog->compiled) {
      debug_print(" / ");
      debug_print_uint(prog->bytecode.instr_count);
    }
    debug_println("");

    debug_print("Stack Depth: ");
    debug_print_uint(debug->snapshot.sp);
    debug_println(" / 64");

    debug_print("Steps Executed: ");
    debug_print_uint(debug->snapshot.step_count);
    debug_println("");

    debug_print("Pause Reason: ");
    debug_println(st_debug_reason_str(debug->pause_reason));

    if (debug->pause_reason == ST_DEBUG_REASON_BREAKPOINT) {
      debug_print("Hit Breakpoint PC: ");
      debug_print_uint(debug->hit_breakpoint_pc);
      debug_println("");
    }

    if (debug->snapshot.error) {
      debug_print("Error: ");
      debug_println(debug->snapshot.error_msg);
    }
  } else {
    debug_println("(no snapshot - program not paused)");
  }

  debug_print("\nBreakpoints: ");
  debug_print_uint(debug->breakpoint_count);
  debug_println("");

  debug_print("Total Steps Debugged: ");
  debug_print_uint(debug->total_steps_debugged);
  debug_println("");

  debug_print("Breakpoints Hit: ");
  debug_print_uint(debug->breakpoints_hit_count);
  debug_println("\n");
}

void st_debug_print_variables(st_debug_state_t *debug, void *prog_ptr) {
  st_logic_program_config_t *prog = (st_logic_program_config_t *)prog_ptr;
  if (!debug || !debug->snapshot_valid) {
    debug_println("ERROR: No valid snapshot (program not paused)");
    return;
  }

  if (!prog || !prog->compiled) {
    debug_println("ERROR: Program not compiled");
    return;
  }

  st_debug_snapshot_t *snap = &debug->snapshot;

  debug_println("\n=== Variables ===\n");

  for (int i = 0; i < snap->var_count && i < ST_MAX_PROGRAM_VARS; i++) {
    // Get variable name from bytecode, type from snapshot
    const char *name = prog->bytecode.var_names[i];
    st_datatype_t type = snap->var_types[i];
    st_value_t val = snap->variables[i];

    debug_print("  ");
    if (name[0] != '\0') {
      debug_print(name);
    } else {
      debug_print("var");
      debug_print_uint(i);
    }
    debug_print(" = ");

    // Print value based on type
    switch (type) {
      case ST_TYPE_BOOL:
        debug_println(val.bool_val ? "TRUE" : "FALSE");
        break;
      case ST_TYPE_INT:
        debug_printf("%d",val.int_val);
        debug_println(" (INT)");
        break;
      case ST_TYPE_DINT:
        debug_printf("%ld",(int32_t)val.dint_val);
        debug_println(" (DINT)");
        break;
      case ST_TYPE_REAL:
        debug_print_float(val.real_val);
        debug_println(" (REAL)");
        break;
      case ST_TYPE_DWORD:
        debug_print("0x");
        debug_printf("%08lX",val.dword_val);
        debug_println(" (DWORD)");
        break;
      default:
        debug_printf("%d",val.int_val);
        debug_println(" (?)");
        break;
    }
  }

  debug_println("");
}

void st_debug_print_stack(st_debug_state_t *debug) {
  if (!debug || !debug->snapshot_valid) {
    debug_println("ERROR: No valid snapshot (program not paused)");
    return;
  }

  // Stack inspection disabled to save RAM (stack not stored in snapshot)
  debug_print("\n=== Stack (depth ");
  debug_print_uint(debug->snapshot.sp);
  debug_println(") ===\n");

  debug_println("  (stack inspection disabled to save RAM)");
  debug_println("  Use 'show logic X debug vars' for variable values.");
  debug_println("");
}

void st_debug_print_breakpoints(st_debug_state_t *debug) {
  if (!debug) {
    debug_println("ERROR: Debug state is NULL");
    return;
  }

  debug_print("\n=== Breakpoints (");
  debug_print_uint(debug->breakpoint_count);
  debug_println(") ===\n");

  if (debug->breakpoint_count == 0) {
    debug_println("  (none)");
  } else {
    for (int i = 0; i < ST_DEBUG_MAX_BREAKPOINTS; i++) {
      if (debug->breakpoints[i] != ST_DEBUG_BP_DISABLED) {
        debug_print("  [");
        debug_print_uint(i);
        debug_print("] PC=");
        debug_print_uint(debug->breakpoints[i]);
        debug_println("");
      }
    }
  }

  debug_println("");
}

void st_debug_print_instruction(st_debug_state_t *debug, void *prog_ptr) {
  st_logic_program_config_t *prog = (st_logic_program_config_t *)prog_ptr;
  if (!debug || !debug->snapshot_valid) {
    debug_println("ERROR: No valid snapshot (program not paused)");
    return;
  }

  if (!prog || !prog->compiled) {
    debug_println("ERROR: Program not compiled");
    return;
  }

  uint16_t pc = debug->snapshot.pc;

  if (pc >= prog->bytecode.instr_count) {
    debug_println("PC out of bounds (program halted)");
    return;
  }

  const st_bytecode_instr_t *instr = &prog->bytecode.instructions[pc];

  debug_print("\n[PC=");
  debug_print_uint(pc);
  debug_print("] ");

  // Print opcode name (simplified - could be expanded with full opcode names)
  switch (instr->opcode) {
    case ST_OP_PUSH_BOOL:    debug_print("PUSH_BOOL "); debug_println(instr->arg.bool_arg ? "TRUE" : "FALSE"); break;
    case ST_OP_PUSH_INT:     debug_print("PUSH_INT "); debug_printf("%d",instr->arg.int_arg); debug_println(""); break;
    case ST_OP_PUSH_REAL:    debug_print("PUSH_REAL "); debug_print_float(instr->arg.float_arg); debug_println(""); break;
    case ST_OP_PUSH_DWORD:   debug_print("PUSH_DWORD 0x"); debug_printf("%08lX",instr->arg.dword_arg); debug_println(""); break;
    case ST_OP_LOAD_VAR:     debug_print("LOAD_VAR "); debug_print_uint(instr->arg.var_index); debug_println(""); break;
    case ST_OP_STORE_VAR:    debug_print("STORE_VAR "); debug_print_uint(instr->arg.var_index); debug_println(""); break;
    case ST_OP_DUP:          debug_println("DUP"); break;
    case ST_OP_POP:          debug_println("POP"); break;
    case ST_OP_ADD:          debug_println("ADD"); break;
    case ST_OP_SUB:          debug_println("SUB"); break;
    case ST_OP_MUL:          debug_println("MUL"); break;
    case ST_OP_DIV:          debug_println("DIV"); break;
    case ST_OP_MOD:          debug_println("MOD"); break;
    case ST_OP_NEG:          debug_println("NEG"); break;
    case ST_OP_AND:          debug_println("AND"); break;
    case ST_OP_OR:           debug_println("OR"); break;
    case ST_OP_XOR:          debug_println("XOR"); break;
    case ST_OP_NOT:          debug_println("NOT"); break;
    case ST_OP_EQ:           debug_println("EQ"); break;
    case ST_OP_NE:           debug_println("NE"); break;
    case ST_OP_LT:           debug_println("LT"); break;
    case ST_OP_GT:           debug_println("GT"); break;
    case ST_OP_LE:           debug_println("LE"); break;
    case ST_OP_GE:           debug_println("GE"); break;
    case ST_OP_SHL:          debug_println("SHL"); break;
    case ST_OP_SHR:          debug_println("SHR"); break;
    case ST_OP_JMP:          debug_print("JMP "); debug_print_uint(instr->arg.int_arg); debug_println(""); break;
    case ST_OP_JMP_IF_FALSE: debug_print("JMP_IF_FALSE "); debug_print_uint(instr->arg.int_arg); debug_println(""); break;
    case ST_OP_JMP_IF_TRUE:  debug_print("JMP_IF_TRUE "); debug_print_uint(instr->arg.int_arg); debug_println(""); break;
    case ST_OP_CALL_BUILTIN: debug_print("CALL_BUILTIN func="); debug_print_uint(instr->arg.builtin_call.func_id_low); debug_println(""); break;
    case ST_OP_NOP:          debug_println("NOP"); break;
    case ST_OP_HALT:         debug_println("HALT"); break;
    default:                 debug_print("OPCODE "); debug_print_uint(instr->opcode); debug_println(""); break;
  }
}
