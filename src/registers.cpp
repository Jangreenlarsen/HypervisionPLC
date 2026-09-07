/**
 * @file registers.cpp
 * @brief Register and coil storage implementation
 *
 * Provides arrays for holding/input registers and coils/discrete inputs
 * All Modbus read/write operations go through these functions
 *
 * Also handles DYNAMIC register/coil updates from counter/timer sources
 */

#include "registers.h"
#include "counter_engine.h"
#include "counter_config.h"
#include "timer_engine.h"
#include "config_struct.h"
#include "st_logic_config.h"
#include "debug.h"
#include "types.h"
#include "constants.h"
#include <Arduino.h>
#include <string.h>
#include <math.h>

/* ============================================================================
 * STATIC STORAGE (all registers and coils in RAM)
 * ============================================================================ */

static uint16_t holding_regs[HOLDING_REGS_SIZE] = {0};      // 16-bit registers
static uint16_t input_regs[INPUT_REGS_SIZE] = {0};          // 16-bit registers
static uint8_t coils[COILS_SIZE] = {0};                     // Packed bits (8 per byte)
static uint8_t discrete_inputs[DISCRETE_INPUTS_SIZE] = {0}; // Packed bits (8 per byte)

// FEAT-010: until now, EVERYTHING that touches these arrays ran cooperatively
// on loopTask (Core 1), so no lock was ever needed. A HIGH-priority ST Logic
// program now executes on its own dedicated Core-0 task and CAN legitimately
// touch these arrays too (e.g. via EXPORT to the IR pool, or Modbus Master
// builtins' cached-read path) — this spinlock protects the raw array
// accesses only. It deliberately does NOT extend to the control-register
// side-effect handlers below (registers_process_st_logic_*) — those can
// call into compilation/other locks, and a spinlock's critical section must
// never wrap anything that can block (same lesson as BUG-374 this session).
static portMUX_TYPE g_registers_spinlock = portMUX_INITIALIZER_UNLOCKED;

/* ============================================================================
 * FORWARD DECLARATIONS (handlers called from registers_set_holding_register)
 * ============================================================================ */

void registers_process_st_logic_control(uint16_t addr, uint16_t value);
void registers_process_st_logic_interval(uint16_t addr, uint16_t value);
void registers_process_st_logic_var_input(uint16_t addr, uint16_t value);

/* ============================================================================
 * HOLDING REGISTERS (Read/Write)
 * ============================================================================ */

uint16_t registers_get_holding_register(uint16_t addr) {
  if (addr >= HOLDING_REGS_SIZE) return 0;
  portENTER_CRITICAL(&g_registers_spinlock);
  uint16_t value = holding_regs[addr];
  portEXIT_CRITICAL(&g_registers_spinlock);
  return value;
}

void registers_set_holding_register(uint16_t addr, uint16_t value) {
  if (addr >= HOLDING_REGS_SIZE) return;
  portENTER_CRITICAL(&g_registers_spinlock);
  holding_regs[addr] = value;
  portEXIT_CRITICAL(&g_registers_spinlock);

  // Process ST Logic control registers
  if (addr >= ST_LOGIC_CONTROL_REG_BASE && addr < ST_LOGIC_CONTROL_REG_BASE + ST_LOGIC_MAX_PROGRAMS) {
    registers_process_st_logic_control(addr, value);
  }

  // Process ST Logic execution interval (v4.1.0) - HR 236-237
  if (addr == ST_LOGIC_EXEC_INTERVAL_RW_REG || addr == ST_LOGIC_EXEC_INTERVAL_RW_REG + 1) {
    registers_process_st_logic_interval(addr, value);
  }

  // Process ST Logic variable input (v4.2.0) - HR 204-235
  if (addr >= ST_LOGIC_VAR_INPUT_REG_BASE && addr < ST_LOGIC_VAR_INPUT_REG_BASE + 32) {
    registers_process_st_logic_var_input(addr, value);
  }
}

uint16_t* registers_get_holding_regs(void) {
  return holding_regs;
}

/* ============================================================================
 * INPUT REGISTERS (Read-Only from Modbus, Write from drivers)
 * ============================================================================ */

uint16_t registers_get_input_register(uint16_t addr) {
  if (addr >= INPUT_REGS_SIZE) return 0;
  portENTER_CRITICAL(&g_registers_spinlock);
  uint16_t value = input_regs[addr];
  portEXIT_CRITICAL(&g_registers_spinlock);
  return value;
}

void registers_set_input_register(uint16_t addr, uint16_t value) {
  if (addr >= INPUT_REGS_SIZE) return;
  portENTER_CRITICAL(&g_registers_spinlock);
  input_regs[addr] = value;
  portEXIT_CRITICAL(&g_registers_spinlock);
}

uint16_t* registers_get_input_regs(void) {
  return input_regs;
}

/* ============================================================================
 * COILS (Read/Write) - Packed bits
 * ============================================================================ */

uint8_t registers_get_coil(uint16_t idx) {
  if (idx >= (COILS_SIZE * 8)) return 0;
  uint16_t byte_idx = idx / 8;
  uint16_t bit_idx = idx % 8;
  portENTER_CRITICAL(&g_registers_spinlock);
  uint8_t value = (coils[byte_idx] >> bit_idx) & 1;
  portEXIT_CRITICAL(&g_registers_spinlock);
  return value;
}

void registers_set_coil(uint16_t idx, uint8_t value) {
  if (idx >= (COILS_SIZE * 8)) return;
  uint16_t byte_idx = idx / 8;
  uint16_t bit_idx = idx % 8;

  // Read-modify-write on a shared packed byte — the highest-risk spot for
  // real corruption (two concurrent sets of DIFFERENT bits in the SAME
  // byte could otherwise silently lose one write).
  portENTER_CRITICAL(&g_registers_spinlock);
  if (value) {
    coils[byte_idx] |= (1 << bit_idx);  // Set bit
  } else {
    coils[byte_idx] &= ~(1 << bit_idx); // Clear bit
  }
  portEXIT_CRITICAL(&g_registers_spinlock);
}

uint8_t* registers_get_coils(void) {
  return coils;
}

/* ============================================================================
 * DISCRETE INPUTS (Read-Only from Modbus, Write from GPIO/sensors)
 * ============================================================================ */

uint8_t registers_get_discrete_input(uint16_t idx) {
  if (idx >= (DISCRETE_INPUTS_SIZE * 8)) return 0;
  uint16_t byte_idx = idx / 8;
  uint16_t bit_idx = idx % 8;
  portENTER_CRITICAL(&g_registers_spinlock);
  uint8_t value = (discrete_inputs[byte_idx] >> bit_idx) & 1;
  portEXIT_CRITICAL(&g_registers_spinlock);
  return value;
}

void registers_set_discrete_input(uint16_t idx, uint8_t value) {
  if (idx >= (DISCRETE_INPUTS_SIZE * 8)) return;
  uint16_t byte_idx = idx / 8;
  uint16_t bit_idx = idx % 8;

  portENTER_CRITICAL(&g_registers_spinlock);
  if (value) {
    discrete_inputs[byte_idx] |= (1 << bit_idx);  // Set bit
  } else {
    discrete_inputs[byte_idx] &= ~(1 << bit_idx); // Clear bit
  }
  portEXIT_CRITICAL(&g_registers_spinlock);
}

uint8_t* registers_get_discrete_inputs(void) {
  return discrete_inputs;
}

/* ============================================================================
 * UTILITY / INITIALIZATION
 * ============================================================================ */

void registers_init(void) {
  memset(holding_regs, 0, sizeof(holding_regs));
  memset(input_regs, 0, sizeof(input_regs));
  memset(coils, 0, sizeof(coils));
  memset(discrete_inputs, 0, sizeof(discrete_inputs));
}

uint32_t registers_get_millis(void) {
  return millis();
}

/* ============================================================================
 * DYNAMIC REGISTER/COIL UPDATES
 * ============================================================================ */

/**
 * @brief Update DYNAMIC registers from counter/timer sources
 *
 * Iterates through all DYNAMIC register mappings and:
 * 1. Gets value from counter or timer
 * 2. Writes to specified holding register
 *
 * Called once per main loop iteration
 *
 * NOTE (BUG-124 FIX): Counter registers are now handled directly by counter_engine_loop()
 * which writes multi-register values correctly for 32/64-bit counters.
 * This function now ONLY handles TIMER sources to avoid overwriting with truncated values.
 */
void registers_update_dynamic_registers(void) {
  for (uint8_t i = 0; i < g_persist_config.dynamic_reg_count; i++) {
    const DynamicRegisterMapping* dyn = &g_persist_config.dynamic_regs[i];
    uint16_t reg_addr = dyn->register_address;

    // BUG-124 FIX: Skip counter sources - counter_engine handles multi-register writes
    if (dyn->source_type == DYNAMIC_SOURCE_COUNTER) {
      continue;  // Counter values are written by counter_engine_loop()
    } else if (dyn->source_type == DYNAMIC_SOURCE_TIMER) {
      uint16_t value = 0;
      uint8_t timer_id = dyn->source_id;
      TimerConfig cfg;
      memset(&cfg, 0, sizeof(cfg));

      if (!timer_engine_get_config(timer_id, &cfg) || !cfg.enabled) {
        continue;  // Timer not configured or disabled
      }

      // Get timer value based on function type
      switch (dyn->source_function) {
        case TIMER_FUNC_OUTPUT:
          // Timer output state (0 or 1)
          // Read current coil state (timer writes to output_coil via set_coil_level)
          value = registers_get_coil(cfg.output_coil) ? 1 : 0;
          break;

        default:
          continue;
      }

      // Write value to holding register
      registers_set_holding_register(reg_addr, value);
    }
  }
}

/**
 * @brief Update DYNAMIC coils from counter/timer sources
 *
 * Iterates through all DYNAMIC coil mappings and:
 * 1. Gets state from counter or timer
 * 2. Writes to specified coil
 *
 * Called once per main loop iteration
 */
void registers_update_dynamic_coils(void) {
  for (uint8_t i = 0; i < g_persist_config.dynamic_coil_count; i++) {
    const DynamicCoilMapping* dyn = &g_persist_config.dynamic_coils[i];
    uint16_t coil_addr = dyn->coil_address;
    uint8_t value = 0;

    if (dyn->source_type == DYNAMIC_SOURCE_COUNTER) {
      uint8_t counter_id = dyn->source_id;
      CounterConfig cfg;
      memset(&cfg, 0, sizeof(cfg));

      if (!counter_engine_get_config(counter_id, &cfg) || !cfg.enabled) {
        continue;  // Counter not configured or disabled
      }

      // Get counter state based on function type
      switch (dyn->source_function) {
        case COUNTER_FUNC_OVERFLOW:
          // Overflow flag
          // TODO: Get overflow state from counter state
          value = 0;  // Placeholder
          break;

        default:
          continue;
      }

      // Write value to coil
      registers_set_coil(coil_addr, value);

    } else if (dyn->source_type == DYNAMIC_SOURCE_TIMER) {
      uint8_t timer_id = dyn->source_id;
      TimerConfig cfg;
      memset(&cfg, 0, sizeof(cfg));

      if (!timer_engine_get_config(timer_id, &cfg) || !cfg.enabled) {
        continue;  // Timer not configured or disabled
      }

      // Get timer state based on function type
      uint8_t value = 0;

      switch (dyn->source_function) {
        case TIMER_FUNC_OUTPUT:
          // Timer output state (0 or 1)
          // Read current coil state (timer writes to output_coil via set_coil_level)
          value = registers_get_coil(cfg.output_coil) ? 1 : 0;
          break;

        default:
          continue;
      }

      // Write value to coil
      registers_set_coil(coil_addr, value);
    }
  }
}

/* ============================================================================
 * ST LOGIC STATUS REGISTERS (200-251)
 * ============================================================================ */

void registers_update_st_logic_status(void) {
  st_logic_engine_state_t *st_state = st_logic_get_state();

  // Update status for each of 4 logic programs
  for (uint8_t prog_id = 0; prog_id < ST_LOGIC_MAX_PROGRAMS; prog_id++) {
    st_logic_program_config_t *prog = st_logic_get_program(st_state, prog_id);

    if (!prog) continue;

    // =========================================================================
    // INPUT REGISTERS (Status - Read Only)
    // =========================================================================

    // 200-203: Status Register (Status of Logic1-4)
    uint16_t status_reg = 0;
    if (prog->enabled)    status_reg |= ST_LOGIC_STATUS_ENABLED;   // Bit 0
    if (prog->compiled)   status_reg |= ST_LOGIC_STATUS_COMPILED;  // Bit 1
    // Bit 2: Running - will be set during execution (not persistent)
    if (prog->error_count > 0) status_reg |= ST_LOGIC_STATUS_ERROR; // Bit 3
    registers_set_input_register(ST_LOGIC_STATUS_REG_BASE + prog_id, status_reg);

    // 204-207: Execution Count (BUG-006 FIX: now uint16_t, no truncation needed)
    registers_set_input_register(ST_LOGIC_EXEC_COUNT_REG_BASE + prog_id,
                                   prog->execution_count);

    // 208-211: Error Count (BUG-006 FIX: now uint16_t, no truncation needed)
    registers_set_input_register(ST_LOGIC_ERROR_COUNT_REG_BASE + prog_id,
                                   prog->error_count);

    // 212-215: Last Error Code (simple encoding of error string)
    // For now: 0 = no error, 1-255 = error present
    uint16_t error_code = (prog->last_error[0] != '\0') ? 1 : 0;
    registers_set_input_register(ST_LOGIC_ERROR_CODE_REG_BASE + prog_id, error_code);

    // 216-219: Variable Count (BUG-005 FIX: use cached binding_count)
    // Note: binding_count is updated by st_logic_update_binding_counts()
    registers_set_input_register(ST_LOGIC_VAR_COUNT_REG_BASE + prog_id, prog->binding_count);

    // =========================================================================
    // 220-251: EXPORTED Variable Values (v5.1.0 - Dynamic IR Pool)
    // =========================================================================
    // Maps EXPORT-flagged variables to IR 220-251 using dynamic pool allocation.
    // Each program gets a flexible range based on exported variable count.

    if (prog->ir_pool_offset != 65535 && prog->ir_pool_size > 0) {
      // Program has IR pool allocated - map exported variables
      uint16_t export_slot = 0;  // Current position in export array

      for (uint8_t var_idx = 0; var_idx < prog->bytecode.var_count; var_idx++) {
        // Only process EXPORT variables
        if (!prog->bytecode.var_export_flags[var_idx]) {
          continue;
        }

        st_datatype_t var_type = prog->bytecode.var_types[var_idx];
        uint16_t base_reg = ST_LOGIC_VAR_VALUES_REG_BASE + prog->ir_pool_offset + export_slot;

        // Type-aware value extraction and register writing
        if (var_type == ST_TYPE_BOOL) {
          // BOOL: 1 register
          uint16_t value = prog->bytecode.variables[var_idx].bool_val ? 1 : 0;
          registers_set_input_register(base_reg, value);
          export_slot++;

        } else if (var_type == ST_TYPE_INT) {
          // INT: 1 register (16-bit signed)
          uint16_t value = (uint16_t)prog->bytecode.variables[var_idx].int_val;
          registers_set_input_register(base_reg, value);
          export_slot++;

        } else if (var_type == ST_TYPE_REAL) {
          // REAL: 2 registers (32-bit float, LSW first per BUG-125)
          uint32_t bits;
          memcpy(&bits, &prog->bytecode.variables[var_idx].real_val, sizeof(float));
          registers_set_input_register(base_reg,     (uint16_t)(bits & 0xFFFF));        // LSW
          registers_set_input_register(base_reg + 1, (uint16_t)((bits >> 16) & 0xFFFF)); // MSW
          export_slot += 2;

        } else if (var_type == ST_TYPE_DINT) {
          // DINT: 2 registers (32-bit signed, LSW first per BUG-124)
          int32_t value = prog->bytecode.variables[var_idx].dint_val;
          registers_set_input_register(base_reg,     (uint16_t)(value & 0xFFFF));        // LSW
          registers_set_input_register(base_reg + 1, (uint16_t)((value >> 16) & 0xFFFF)); // MSW
          export_slot += 2;

        } else if (var_type == ST_TYPE_DWORD) {
          // DWORD: 2 registers (32-bit unsigned, LSW first)
          uint32_t value = prog->bytecode.variables[var_idx].dword_val;
          registers_set_input_register(base_reg,     (uint16_t)(value & 0xFFFF));        // LSW
          registers_set_input_register(base_reg + 1, (uint16_t)((value >> 16) & 0xFFFF)); // MSW
          export_slot += 2;
        }

        // Safety check: don't overflow allocated pool
        if (export_slot >= prog->ir_pool_size) {
          break;
        }
      }
    }

    // =========================================================================
    // PERFORMANCE STATISTICS (v4.1.0) - Input Registers 252-293
    // =========================================================================

    // 252-259: Min Execution Time (µs) - 32-bit, 2 registers per program
    uint16_t min_reg_offset = ST_LOGIC_MIN_EXEC_TIME_REG_BASE + (prog_id * 2);
    registers_set_input_register(min_reg_offset,     (uint16_t)(prog->min_execution_us >> 16)); // High word
    registers_set_input_register(min_reg_offset + 1, (uint16_t)(prog->min_execution_us & 0xFFFF)); // Low word

    // 260-267: Max Execution Time (µs) - 32-bit, 2 registers per program
    uint16_t max_reg_offset = ST_LOGIC_MAX_EXEC_TIME_REG_BASE + (prog_id * 2);
    registers_set_input_register(max_reg_offset,     (uint16_t)(prog->max_execution_us >> 16));
    registers_set_input_register(max_reg_offset + 1, (uint16_t)(prog->max_execution_us & 0xFFFF));

    // 268-275: Avg Execution Time (µs) - Calculated from total_execution_us / execution_count
    uint32_t avg_execution_us = 0;
    if (prog->execution_count > 0) {
      avg_execution_us = prog->total_execution_us / prog->execution_count;
    }
    uint16_t avg_reg_offset = ST_LOGIC_AVG_EXEC_TIME_REG_BASE + (prog_id * 2);
    registers_set_input_register(avg_reg_offset,     (uint16_t)(avg_execution_us >> 16));
    registers_set_input_register(avg_reg_offset + 1, (uint16_t)(avg_execution_us & 0xFFFF));

    // 276-283: Overrun Count - 32-bit, 2 registers per program
    uint16_t overrun_reg_offset = ST_LOGIC_OVERRUN_COUNT_REG_BASE + (prog_id * 2);
    registers_set_input_register(overrun_reg_offset,     (uint16_t)(prog->overrun_count >> 16));
    registers_set_input_register(overrun_reg_offset + 1, (uint16_t)(prog->overrun_count & 0xFFFF));
  }

  // =========================================================================
  // GLOBAL CYCLE STATISTICS (v4.1.0)
  // =========================================================================

  // 284-285: Global Cycle Min Time (ms)
  registers_set_input_register(ST_LOGIC_CYCLE_MIN_REG,     (uint16_t)(st_state->cycle_min_ms >> 16));
  registers_set_input_register(ST_LOGIC_CYCLE_MIN_REG + 1, (uint16_t)(st_state->cycle_min_ms & 0xFFFF));

  // 286-287: Global Cycle Max Time (ms)
  registers_set_input_register(ST_LOGIC_CYCLE_MAX_REG,     (uint16_t)(st_state->cycle_max_ms >> 16));
  registers_set_input_register(ST_LOGIC_CYCLE_MAX_REG + 1, (uint16_t)(st_state->cycle_max_ms & 0xFFFF));

  // 288-289: Global Cycle Overrun Count
  registers_set_input_register(ST_LOGIC_CYCLE_OVERRUN_REG,     (uint16_t)(st_state->cycle_overrun_count >> 16));
  registers_set_input_register(ST_LOGIC_CYCLE_OVERRUN_REG + 1, (uint16_t)(st_state->cycle_overrun_count & 0xFFFF));

  // 290-291: Total Cycles Executed
  registers_set_input_register(ST_LOGIC_TOTAL_CYCLES_REG,     (uint16_t)(st_state->total_cycles >> 16));
  registers_set_input_register(ST_LOGIC_TOTAL_CYCLES_REG + 1, (uint16_t)(st_state->total_cycles & 0xFFFF));

  // 292-293: Execution Interval (ms) - Read-only copy
  registers_set_input_register(ST_LOGIC_EXEC_INTERVAL_RO_REG,     (uint16_t)(st_state->execution_interval_ms >> 16));
  registers_set_input_register(ST_LOGIC_EXEC_INTERVAL_RO_REG + 1, (uint16_t)(st_state->execution_interval_ms & 0xFFFF));
}

/* ============================================================================
 * ST LOGIC CONTROL REGISTER HANDLER
 * ============================================================================ */

void registers_process_st_logic_control(uint16_t addr, uint16_t value) {
  // Determine which program this control register is for
  if (addr < ST_LOGIC_CONTROL_REG_BASE || addr >= ST_LOGIC_CONTROL_REG_BASE + ST_LOGIC_MAX_PROGRAMS) {
    return;  // Not a control register
  }

  uint8_t prog_id = addr - ST_LOGIC_CONTROL_REG_BASE;  // 0-3 for Logic1-4
  st_logic_engine_state_t *st_state = st_logic_get_state();
  st_logic_program_config_t *prog = st_logic_get_program(st_state, prog_id);

  if (!prog) return;

  // Bit 0: Enable/Disable program
  if (value & ST_LOGIC_CONTROL_ENABLE) {
    if (!prog->enabled) {
      st_logic_set_enabled(st_state, prog_id, 1);
      debug_print("[ST_LOGIC] Logic");
      debug_print_uint(prog_id + 1);
      debug_println(" ENABLED via Modbus");
    }
  } else {
    if (prog->enabled) {
      st_logic_set_enabled(st_state, prog_id, 0);
      debug_print("[ST_LOGIC] Logic");
      debug_print_uint(prog_id + 1);
      debug_println(" DISABLED via Modbus");
    }
  }

  // Bit 1: Start/Stop (note: this is for future use)
  // Currently, programs run continuously if enabled
  // This bit could control a "pause" state

  // Bit 2: Reset Error flag
  if (value & ST_LOGIC_CONTROL_RESET_ERROR) {
    if (prog->error_count > 0) {
      prog->error_count = 0;
      prog->last_error[0] = '\0';
      debug_print("[ST_LOGIC] Logic");
      debug_print_uint(prog_id + 1);
      debug_println(" error cleared via Modbus");
    }

    // BUG-004 FIX: Auto-clear bit 2 in control register (acknowledge command)
    uint16_t ctrl_val = registers_get_holding_register(addr);
    ctrl_val &= ~ST_LOGIC_CONTROL_RESET_ERROR;  // Clear bit 2
    registers_set_holding_register(addr, ctrl_val);
  }
}

/* ============================================================================
 * ST LOGIC EXECUTION INTERVAL HANDLER (v4.1.0)
 * ============================================================================ */

void registers_process_st_logic_interval(uint16_t addr, uint16_t value) {
  // HR 236-237: Execution Interval (32-bit, ms)
  // When EITHER register is written, reconstruct the 32-bit value and validate

  st_logic_engine_state_t *st_state = st_logic_get_state();
  if (!st_state) return;

  // Read both registers (high word and low word)
  uint16_t high_word = registers_get_holding_register(ST_LOGIC_EXEC_INTERVAL_RW_REG);
  uint16_t low_word = registers_get_holding_register(ST_LOGIC_EXEC_INTERVAL_RW_REG + 1);
  uint32_t new_interval = ((uint32_t)high_word << 16) | low_word;

  // Validate: Only allow specific intervals (10, 20, 25, 50, 75, 100 ms)
  if (new_interval != 10 && new_interval != 20 && new_interval != 25 &&
      new_interval != 50 && new_interval != 75 && new_interval != 100) {
    debug_print("[ST_LOGIC] Invalid interval via Modbus: ");
    debug_print_uint(new_interval);
    debug_println("ms (allowed: 10,20,25,50,75,100)");

    // Reset registers to current valid interval
    registers_set_holding_register(ST_LOGIC_EXEC_INTERVAL_RW_REG,     (uint16_t)(st_state->execution_interval_ms >> 16));
    registers_set_holding_register(ST_LOGIC_EXEC_INTERVAL_RW_REG + 1, (uint16_t)(st_state->execution_interval_ms & 0xFFFF));
    return;
  }

  // Apply new interval — FEAT-010: cascades to every NORMAL-priority
  // program's own interval_ms (HIGH programs are scheduled independently).
  st_logic_set_global_interval(st_state, new_interval);

  debug_print("[ST_LOGIC] Execution interval set to ");
  debug_print_uint(new_interval);
  debug_println("ms via Modbus");

  // Note: Interval is not persisted to NVS automatically
  // User must call config_save() or use CLI 'save' command to persist
}

/* ============================================================================
 * ST LOGIC VARIABLE INPUT HANDLER (v4.2.0)
 * ============================================================================
 *
 * Direct write to ST Logic variables via Modbus (HR 204-235)
 * Maps deterministically: prog_id = offset / 8, var_index = offset % 8
 *
 * HR 204-211: Logic1 variables [0-7]
 * HR 212-219: Logic2 variables [0-7]
 * HR 220-227: Logic3 variables [0-7]
 * HR 228-235: Logic4 variables [0-7]
 */

void registers_process_st_logic_var_input(uint16_t addr, uint16_t value) {
  // Validate address range
  if (addr < ST_LOGIC_VAR_INPUT_REG_BASE || addr >= ST_LOGIC_VAR_INPUT_REG_BASE + 32) {
    return;  // Not a ST Logic variable input register
  }

  // Calculate program ID (0-3) and variable index (0-7)
  uint8_t offset = addr - ST_LOGIC_VAR_INPUT_REG_BASE;
  uint8_t prog_id = offset / 8;
  uint8_t var_index = offset % 8;

  // Get program state
  st_logic_engine_state_t *st_state = st_logic_get_state();
  if (!st_state) return;

  st_logic_program_config_t *prog = st_logic_get_program(st_state, prog_id);
  if (!prog) return;

  // Bounds checking: program must be compiled and variable must exist
  if (!prog->compiled || var_index >= prog->bytecode.var_count) {
    // Silently ignore writes to non-existent variables or uncompiled programs
    return;
  }

  // Type-aware conversion (matches gpio_mapping.cpp pattern)
  st_datatype_t var_type = prog->bytecode.var_types[var_index];
  if (var_type == ST_TYPE_BOOL) {
    // BOOL: Convert value to 0 or 1
    prog->bytecode.variables[var_index].bool_val = (value != 0);
  } else if (var_type == ST_TYPE_REAL) {
    // REAL: Cast 16-bit integer to float
    prog->bytecode.variables[var_index].real_val = (float)value;
  } else {
    // INT or DWORD: Direct 16-bit signed integer
    prog->bytecode.variables[var_index].int_val = (int16_t)value;
  }

  // Optional: Debug output if enabled
  if (st_state->debug) {
    debug_printf("[ST_VAR_INPUT] Logic%d var[%d] = %d (type=%d)\n",
                 prog_id + 1, var_index, value, var_type);
  }
}
