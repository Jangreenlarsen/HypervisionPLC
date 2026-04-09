/**
 * @file st_logic_engine.cpp
 * @brief Structured Text Logic Engine Implementation
 *
 * Main execution loop integrating ST bytecode with Modbus I/O.
 */

#include <Arduino.h>
#include "st_logic_engine.h"
#include "st_compiler.h"
#include "st_parser.h"
#include "st_vm.h"
#include "st_stateful.h"  // BUG-153 FIX: For cycle_time_ms update
#include "st_builtin_modbus.h"  // BUG-133 FIX: For g_mb_request_count reset
#include "st_debug.h"  // FEAT-008: Debugger support
#include "config_struct.h"
#include "constants.h"
#include "debug.h"
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * BUG-038 FIX: Spinlock for ST variable access synchronization
 * Prevents race condition between execute (memcpy) and I/O (gpio_mapping)
 * ============================================================================ */
static portMUX_TYPE st_var_spinlock = portMUX_INITIALIZER_UNLOCKED;

/* ============================================================================
 * INPUT/OUTPUT OPERATIONS (MOVED TO gpio_mapping.cpp)
 *
 * NOTE: Variable I/O is now handled by the unified VariableMapping system
 * in gpio_mapping.cpp. This eliminates code duplication and provides
 * a single place to manage all variable bindings (GPIO + ST).
 *
 * The mapping engine:
 * - Reads variables from registers (INPUT mode) BEFORE program execution
 * - Writes variables to registers (OUTPUT mode) AFTER program execution
 * - Handles both GPIO pins and ST variables using the same mechanism
 * ============================================================================ */

/* ============================================================================
 * PROGRAM EXECUTION
 * ============================================================================ */

bool st_logic_execute_program(st_logic_engine_state_t *state, uint8_t program_id) {
  st_logic_program_config_t *prog = st_logic_get_program(state, program_id);
  if (!prog || !prog->compiled || !prog->enabled) return false;

  // FEAT-008: Get debug state for this program
  st_debug_state_t *debug = &state->debugger[program_id];

  // FEAT-008: If paused, skip execution entirely (wait for step/continue)
  if (debug->mode == ST_DEBUG_PAUSED) {
    return true;  // Not an error, just paused
  }

  // Create VM and initialize with bytecode
  st_vm_t vm;

  // FEAT-008: Use shared debug VM if this program owns it (preserves PC/stack between steps)
  if (debug->owns_debug_vm && g_shared_debug_vm.valid &&
      g_shared_debug_vm.vm != nullptr &&
      g_shared_debug_vm.program_id == program_id &&
      (debug->mode == ST_DEBUG_STEP || debug->mode == ST_DEBUG_RUN)) {
    // Restore VM state from shared debug VM
    memcpy(&vm, g_shared_debug_vm.vm, sizeof(st_vm_t));
    // Re-link program pointer (was cleared by memcpy or might be stale)
    vm.program = &prog->bytecode;

    // If VM was halted but user wants to continue/step, reset to start new cycle
    if (vm.halted) {
      st_vm_init(&vm, &prog->bytecode);
    }
  } else {
    // Normal initialization
    st_vm_init(&vm, &prog->bytecode);

    // If starting debug, allocate and claim the shared VM
    if (debug->mode == ST_DEBUG_STEP || debug->mode == ST_DEBUG_RUN) {
      // Allocate debug VM if needed
      if (!st_debug_alloc_vm()) {
        // Allocation failed - disable debug mode
        debug->mode = ST_DEBUG_OFF;
      } else {
        // Release any other program's claim
        if (g_shared_debug_vm.valid && g_shared_debug_vm.program_id != program_id) {
          g_shared_debug_vm.valid = false;
        }
        debug->owns_debug_vm = true;
        g_shared_debug_vm.program_id = program_id;
      }
    }
  }

  // BUG-153 FIX: Update cycle time in stateful storage before execution
  if (prog->bytecode.stateful) {
    st_stateful_storage_t *stateful = (st_stateful_storage_t*)prog->bytecode.stateful;
    stateful->cycle_time_ms = state->execution_interval_ms;
  }

  // FEAT-003: Set function registry for user-defined function calls
  if (prog->bytecode.func_registry) {
    vm.func_registry = prog->bytecode.func_registry;
  }

  // BUG-007 FIX: Add timing wrapper for execution monitoring (use micros for precision)
  uint32_t start_us = micros();

  // FEAT-008: Debug-aware execution loop
  bool success = true;
  uint32_t steps = 0;
  const uint32_t max_steps = 10000;

  while (!vm.halted && !vm.error) {
    // Max steps check (safety)
    if (steps >= max_steps) {
      snprintf(vm.error_msg, sizeof(vm.error_msg), "Max steps exceeded (%u)", max_steps);
      vm.error = 1;
      success = false;
      break;
    }

    // FEAT-008: Check for breakpoint BEFORE executing instruction
    if (debug->mode != ST_DEBUG_OFF && st_debug_check_breakpoint(debug, vm.pc)) {
      // Hit a breakpoint - pause and save snapshot
      st_debug_save_snapshot(debug, &vm, ST_DEBUG_REASON_BREAKPOINT);
      debug->hit_breakpoint_pc = vm.pc;
      debug->breakpoints_hit_count++;
      debug->mode = ST_DEBUG_PAUSED;
      break;  // Exit execution loop
    }

    // Execute one instruction
    if (!st_vm_step(&vm)) {
      break;  // Halted or error
    }

    steps++;

    // FEAT-008: Only count debugged steps when debugging is active
    if (debug->mode != ST_DEBUG_OFF) {
      debug->total_steps_debugged++;
    }

    // FEAT-008: Single-step mode - pause after one instruction
    if (debug->mode == ST_DEBUG_STEP) {
      st_debug_save_snapshot(debug, &vm, ST_DEBUG_REASON_STEP);
      debug->mode = ST_DEBUG_PAUSED;
      break;  // Exit execution loop
    }
  }

  // FEAT-008: Save snapshot on halt or error (if debugging)
  if (debug->mode != ST_DEBUG_OFF && debug->mode != ST_DEBUG_PAUSED) {
    if (vm.error) {
      st_debug_save_snapshot(debug, &vm, ST_DEBUG_REASON_ERROR);
      debug->mode = ST_DEBUG_PAUSED;
    } else if (vm.halted) {
      st_debug_save_snapshot(debug, &vm, ST_DEBUG_REASON_HALT);
      debug->mode = ST_DEBUG_PAUSED;
    }
  }

  // FEAT-008: Save VM state to shared debug VM for next step
  if (debug->mode == ST_DEBUG_PAUSED && debug->owns_debug_vm && g_shared_debug_vm.vm != nullptr) {
    memcpy(g_shared_debug_vm.vm, &vm, sizeof(st_vm_t));
    g_shared_debug_vm.valid = true;
    g_shared_debug_vm.program_id = program_id;
  } else if (debug->mode == ST_DEBUG_OFF && debug->owns_debug_vm) {
    // Release and free shared debug VM when debugging is stopped
    st_debug_free_vm();
    debug->owns_debug_vm = false;
  }

  // Check final state
  if (vm.error) {
    success = false;
  }

  uint32_t elapsed_us = micros() - start_us;
  uint32_t elapsed_ms = elapsed_us / 1000;

  // BUG-007 FIX: Log warning if execution took too long (>100ms threshold)
  if (elapsed_ms > 100) {
    debug_printf("[WARN] Logic%d execution took %ums (slow!)\n",
                 program_id + 1, (unsigned int)elapsed_ms);
  }

  // Update execution statistics
  prog->execution_count++;

  // Performance monitoring (v4.1.0): Track min/max/avg execution time
  prog->last_execution_us = elapsed_us;  // Store in microseconds for precision
  prog->total_execution_us += elapsed_us;

  if (prog->execution_count == 1) {
    // First execution
    prog->min_execution_us = elapsed_us;
    prog->max_execution_us = elapsed_us;
  } else {
    if (elapsed_us < prog->min_execution_us) prog->min_execution_us = elapsed_us;
    if (elapsed_us > prog->max_execution_us) prog->max_execution_us = elapsed_us;
  }

  // Track overruns (execution time > target interval)
  if (elapsed_ms > state->execution_interval_ms) {
    prog->overrun_count++;
  }

  // BUG-106 FIX: Check for errors BEFORE copying variables back
  if (!success || vm.error) {
    prog->error_count++;
    snprintf(prog->last_error, sizeof(prog->last_error), "%s", vm.error_msg);
    return false;
  }

  // BUG-038 FIX: Use critical section to prevent race with gpio_mapping I/O
  // BUG-106 FIX: Only copy variables back if execution was successful
  // This prevents division-by-zero or other errors from writing garbage values
  portENTER_CRITICAL(&st_var_spinlock);
  memcpy(prog->bytecode.variables, vm.variables, vm.var_count * sizeof(st_value_t));
  portEXIT_CRITICAL(&st_var_spinlock);

  // BUG-178 FIX: Write EXPORT variables to IR 220-251 after execution
  extern void ir_pool_write_exports(st_logic_program_config_t *prog);
  ir_pool_write_exports(prog);

  return true;
}

/* ============================================================================
 * MAIN LOGIC ENGINE LOOP
 *
 * EXECUTION ORDER:
 * 1. gpio_mapping_update() - Reads INPUT bindings (register→ST var)
 * 2. st_logic_engine_loop() - Executes all enabled programs
 * 3. gpio_mapping_update() - Writes OUTPUT bindings (ST var→register)
 *
 * This 3-phase approach ensures:
 * - Variables are synced BEFORE execution (fresh inputs)
 * - Variables are synced AFTER execution (outputs pushed to registers)
 * - All I/O goes through unified VariableMapping system
 * ============================================================================ */

bool st_logic_engine_loop(st_logic_engine_state_t *state,
                           uint16_t *holding_regs, uint16_t *input_regs) {
  if (!state || !state->enabled) return true;  // Logic mode disabled

  // FIXED RATE SCHEDULER: Check if enough time has elapsed since last execution
  uint32_t now = millis();
  uint32_t elapsed = now - state->last_run_time;

  if (elapsed < state->execution_interval_ms) {
    return true;  // Skip this iteration, too early (throttle execution)
  }

  // Update timestamp for next cycle
  state->last_run_time = now;

  bool all_success = true;

  // Execute each program in sequence
  // NOTE: I/O is handled by gpio_mapping_update() in main loop, not here
  uint32_t start_cycle = millis();

  for (int prog_id = 0; prog_id < ST_LOGIC_MAX_PROGRAMS; prog_id++) {
    st_logic_program_config_t *prog = &state->programs[prog_id];

    if (!prog->enabled || !prog->compiled) continue;

    // BUG-133 FIX (v2): Reset Modbus request counter PER SLOT, not per cycle.
    // Each program gets its own full quota of max_requests_per_cycle.
    // Previously reset once before the loop, causing slot 0 to exhaust the quota
    // and block slots 1-3 from issuing any Modbus requests.
    g_mb_request_count = 0;
    g_mb_cache_enabled = true;  // Reset cache mode to default per slot

    // Execute program bytecode
    bool success = st_logic_execute_program(state, prog_id);
    if (!success) {
      all_success = false;
      // Continue executing other programs despite error
    }
  }

  // Performance monitoring (v4.1.0): Track global cycle statistics
  uint32_t cycle_time = millis() - start_cycle;
  state->total_cycles++;

  if (state->total_cycles == 1) {
    // First cycle
    state->cycle_min_ms = cycle_time;
    state->cycle_max_ms = cycle_time;
  } else {
    if (cycle_time < state->cycle_min_ms) state->cycle_min_ms = cycle_time;
    if (cycle_time > state->cycle_max_ms) state->cycle_max_ms = cycle_time;
  }

  if (cycle_time > state->execution_interval_ms) {
    state->cycle_overrun_count++;
  }

  // Debug: Log total cycle time if debug enabled
  if (state->debug) {
    if (cycle_time > state->execution_interval_ms) {
      debug_printf("[ST_TIMING] WARNING: Cycle time %ums > target %ums (overrun!)\n",
                   (unsigned int)cycle_time, (unsigned int)state->execution_interval_ms);
    } else {
      debug_printf("[ST_TIMING] Cycle time: %ums / %ums (OK)\n",
                   (unsigned int)cycle_time, (unsigned int)state->execution_interval_ms);
    }
  }

  return all_success;
}

/* ============================================================================
 * DEBUGGING & STATUS
 * ============================================================================ */

void st_logic_print_status(st_logic_engine_state_t *state) {
  debug_printf("\n=== Logic Engine Status ===\n");
  debug_printf("Enabled: %s\n", state->enabled ? "YES" : "NO");
  debug_printf("Execution Interval: %ums\n", state->execution_interval_ms);
  debug_printf("\nPrograms:\n");

  for (int i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
    st_logic_program_config_t *prog = &state->programs[i];
    debug_printf("  %s:\n", prog->name);
    debug_printf("    Enabled: %s\n", prog->enabled ? "YES" : "NO");
    debug_printf("    Compiled: %s\n", prog->compiled ? "YES" : "NO");
    debug_printf("    Source: %d bytes\n", prog->source_size);
    debug_printf("    Executions: %u (errors: %u)\n", prog->execution_count, prog->error_count);
    if (prog->error_count > 0) {
      debug_printf("    Last error: %s\n", prog->last_error);
    }
  }

  debug_printf("\n");
}

void st_logic_print_program(st_logic_engine_state_t *state, uint8_t program_id, uint8_t show_source) {
  st_logic_program_config_t *prog = st_logic_get_program(state, program_id);
  if (!prog) {
    debug_printf("Invalid program ID: %d\n", program_id);
    return;
  }

  debug_printf("\n=== Logic Program: %s ===\n", prog->name);
  debug_printf("Enabled: %s\n", prog->enabled ? "YES" : "NO");
  debug_printf("Compiled: %s\n", prog->compiled ? "YES" : "NO");
  debug_printf("Source Code: %d bytes\n", prog->source_size);
  debug_printf("Execution Interval: %ums\n", (unsigned int)state->execution_interval_ms);

  // v5.1.0: Show source code only if show_source=1 or 'show logic X st' used
  if (show_source && prog->source_size > 0) {
    debug_printf("\nSource:\n");
    // Print first 500 chars of source
    const char *source = st_logic_get_source_code(state, program_id);
    if (source) {
      int chars = (prog->source_size > 500) ? 500 : prog->source_size;
      debug_printf("%.*s\n", chars, source);
      if (prog->source_size > 500) {
        debug_printf("... (%d more bytes)\n", prog->source_size - 500);
      }
    }
  } else if (!show_source && prog->source_size > 0) {
    debug_printf("\n(Source code hidden - use 'show logic %d st' to display)\n", program_id + 1);
  }

  // v5.1.0: Show EXPORT variables → IR 220-251 mapping
  debug_printf("\nEXPORT Variables → IR 220-251:\n");
  if (prog->compiled && prog->ir_pool_offset != 65535 && prog->ir_pool_size > 0) {
    debug_printf("  IR Pool: offset=%d, size=%d registers\n", prog->ir_pool_offset, prog->ir_pool_size);

    // Iterate through variables and show EXPORT ones
    uint16_t export_slot = 0;
    uint8_t export_count = 0;
    for (uint8_t var_idx = 0; var_idx < prog->bytecode.var_count; var_idx++) {
      if (!prog->bytecode.var_export_flags[var_idx]) {
        continue;
      }

      export_count++;
      st_datatype_t var_type = prog->bytecode.var_types[var_idx];
      uint16_t base_reg = 220 + prog->ir_pool_offset + export_slot;

      debug_printf("  [%d] %s (", var_idx, prog->bytecode.var_names[var_idx]);

      // Print type
      switch (var_type) {
        case ST_TYPE_BOOL: debug_printf("BOOL"); break;
        case ST_TYPE_INT: debug_printf("INT"); break;
        case ST_TYPE_DINT: debug_printf("DINT"); break;
        case ST_TYPE_DWORD: debug_printf("DWORD"); break;
        case ST_TYPE_REAL: debug_printf("REAL"); break;
        case ST_TYPE_TIME: debug_printf("TIME"); break;
        default: debug_printf("???"); break;
      }

      debug_printf(") → IR %d", base_reg);

      // Multi-register types show range
      if (var_type == ST_TYPE_REAL || var_type == ST_TYPE_DINT || var_type == ST_TYPE_DWORD) {
        debug_printf("-%d", base_reg + 1);
        export_slot += 2;
      } else {
        export_slot += 1;
      }

      debug_printf("\n");
    }

    if (export_count == 0) {
      debug_printf("  (No EXPORT variables - all variables are program-internal)\n");
    }
  } else if (prog->compiled) {
    debug_printf("  (No IR pool allocated - no EXPORT variables)\n");
  } else {
    debug_printf("  (Program not compiled)\n");
  }

  debug_printf("\nVariable Bindings (INPUT/OUTPUT modes):\n");

  // Find and display all ST bindings for this program from g_persist_config
  extern PersistConfig g_persist_config;
  uint8_t binding_count = 0;

  for (uint8_t i = 0; i < g_persist_config.var_map_count; i++) {
    VariableMapping *map = &g_persist_config.var_maps[i];

    // Check if this is an ST variable binding for this program
    if (map->source_type == MAPPING_SOURCE_ST_VAR &&
        map->st_program_id == program_id &&
        map->st_var_index < prog->bytecode.var_count) {

      binding_count++;
      const char *var_name = prog->bytecode.var_names[map->st_var_index];

      if (map->is_input) {
        // Input: Read from register/discrete input/coil
        if (map->input_type == 1) {
          // Discrete Input
          debug_printf("  [%d] %s ← Input-dis#%d (input)\n",
                 map->st_var_index, var_name, map->input_reg);
        } else if (map->input_type == 2) {
          // BUG-049 FIX: Coil input
          debug_printf("  [%d] %s ← COIL#%d (input)\n",
                 map->st_var_index, var_name, map->input_reg);
        } else {
          // Holding Register (default)
          debug_printf("  [%d] %s ← HR#%d (input)\n",
                 map->st_var_index, var_name, map->input_reg);
        }
      } else {
        // Output: Write to register or coil
        if (map->output_type == 1) {
          // Coil output
          debug_printf("  [%d] %s → Coil#%d (output)\n",
                 map->st_var_index, var_name, map->coil_reg);
        } else {
          // Holding Register output
          debug_printf("  [%d] %s → Reg#%d (output)\n",
                 map->st_var_index, var_name, map->coil_reg);
        }
      }
    }
  }

  if (binding_count == 0) {
    debug_printf("  (No bindings configured)\n");
    debug_printf("  Syntax: set logic %d bind <var_name> reg:100|coil:10|input-dis:5\n", program_id + 1);
  } else {
    debug_printf("  Total: %d binding%s\n", binding_count, binding_count != 1 ? "s" : "");
  }

  debug_printf("\nStatistics:\n");
  debug_printf("  Executions: %u\n", prog->execution_count);
  debug_printf("  Errors: %u\n", prog->error_count);

  if (prog->compiled) {
    debug_printf("\nCompiled Bytecode: %d instructions\n", prog->bytecode.instr_count);
  }

  if (prog->error_count > 0) {
    debug_printf("\nLast Error: %s\n", prog->last_error);
  }

  debug_printf("\n");
}

/* ============================================================================
 * BUG-038 FIX: Variable access lock functions for gpio_mapping synchronization
 * ============================================================================ */

void st_logic_lock_variables(void) {
  portENTER_CRITICAL(&st_var_spinlock);
}

void st_logic_unlock_variables(void) {
  portEXIT_CRITICAL(&st_var_spinlock);
}
