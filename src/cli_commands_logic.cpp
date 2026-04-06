/**
 * @file cli_commands_logic.cpp
 * @brief CLI Commands for Structured Text Logic Mode (Fase 4)
 *
 * Commands for managing logic programs:
 *   set logic <id> upload <code>
 *   set logic <id> enabled:true|false
 *   set logic <id> delete
 *   set logic <id> bind <var_index> <register> [input|output|both]
 *   show logic <id>
 *   show logic all
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "debug.h"
#include "debug_flags.h"

/* ST Logic Engine includes */
#include "st_logic_config.h"
#include "st_logic_engine.h"
#include "st_compiler.h"
#include "st_debug.h"  // FEAT-008: Debugger support

/* Config & Mapping includes */
#include "config_struct.h"
#include "constants.h"
#include "register_allocator.h"  // BUG-025: Register overlap checking
#include "counter_config.h"      // For counter_config_get/set (persistent cleanup)

/* Forward declarations - from existing CLI infrastructure */
extern void debug_println(const char *msg);

/* Forward declarations - config persistence */
extern bool config_save_to_nvs(const PersistConfig* cfg);

/* Forward declaration for bind function */
int cli_cmd_set_logic_bind(st_logic_engine_state_t *logic_state, uint8_t program_id,
                           uint8_t var_index, uint16_t modbus_reg, const char *direction, uint8_t input_type, uint8_t output_type);

/* ============================================================================
 * COMMAND HANDLERS
 * ============================================================================ */

/**
 * @brief set logic <id> upload "<st_code>"
 *
 * Upload ST source code for a logic program
 *
 * Example:
 *   set logic 1 upload "VAR x: INT; END_VAR IF x > 10 THEN x := 1; END_IF;"
 */
int cli_cmd_set_logic_upload(st_logic_engine_state_t *logic_state, uint8_t program_id,
                             const char *source_code) {
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) {
    debug_printf("ERROR: Invalid program ID (0-%d)\n", ST_LOGIC_MAX_PROGRAMS - 1);
    return -1;
  }

  if (!source_code || strlen(source_code) == 0) {
    debug_println("ERROR: Source code cannot be empty");
    return -1;
  }

  // Upload source
  uint32_t source_len = strlen(source_code);
  if (!st_logic_upload(logic_state, program_id, source_code, source_len)) {
    st_logic_program_config_t *prog = st_logic_get_program(logic_state, program_id);

    // Show detailed error message with pool stats
    uint32_t pool_used, pool_free, pool_largest;
    st_logic_get_pool_stats(logic_state, &pool_used, &pool_free, &pool_largest);

    debug_println("");
    debug_println("╔════════════════════════════════════════════════════════╗");
    debug_println("║              UPLOAD ERROR - Logic Program             ║");
    debug_println("╚════════════════════════════════════════════════════════╝");
    debug_printf("Program ID: Logic%d\n", program_id + 1);
    debug_printf("Source size: %d bytes\n", (int)source_len);
    debug_printf("Pool: %d/%d bytes used (%d bytes free)\n",
                 (int)pool_used, ST_LOGIC_POOL_SIZE, (int)pool_free);
    debug_printf("Error: %s\n", prog->last_error);
    debug_println("");
    return -1;
  }

  // Try to compile immediately
  st_logic_program_config_t *prog_before = st_logic_get_program(logic_state, program_id);
  uint8_t compiled_before = prog_before ? prog_before->compiled : 0;

  if (!st_logic_compile(logic_state, program_id)) {
    st_logic_program_config_t *prog = st_logic_get_program(logic_state, program_id);

    // Better error output
    debug_println("");
    debug_println("╔════════════════════════════════════════════════════════╗");
    debug_println("║            COMPILATION ERROR - Logic Program          ║");
    debug_println("╚════════════════════════════════════════════════════════╝");
    debug_printf("Program ID: Logic%d\n", program_id + 1);
    debug_printf("Error: %s\n", prog->last_error);
    debug_printf("Source size: %d bytes\n", prog->source_size);
    debug_println("");

    return -1;
  }

  st_logic_program_config_t *prog = st_logic_get_program(logic_state, program_id);
  uint8_t compiled_after = prog ? prog->compiled : 0;

  // Success output with pool statistics
  uint32_t pool_used, pool_free, pool_largest;
  st_logic_get_pool_stats(logic_state, &pool_used, &pool_free, &pool_largest);
  uint32_t pool_usage_pct = (pool_used * 100) / ST_LOGIC_POOL_SIZE;

  debug_println("");
  debug_println("✓ COMPILATION SUCCESSFUL");
  debug_printf("  Program: Logic%d\n", program_id + 1);
  debug_printf("  Source: %d bytes\n", (int)source_len);
  debug_printf("  Bytecode: %d instructions\n", prog->bytecode.instr_count);
  debug_printf("  Variables: %d\n", prog->bytecode.var_count);
  debug_printf("  Pool: %d/%d bytes used (%d%% full, %d bytes free)\n",
               (int)pool_used, ST_LOGIC_POOL_SIZE, (int)pool_usage_pct, (int)pool_free);
  debug_printf("  [DEBUG] compiled: %d → %d\n", compiled_before, compiled_after);
  debug_println("");

  // Debug: Print bytecode instructions (if debug mode enabled)
  if (logic_state && logic_state->debug) {
    st_bytecode_print(&prog->bytecode);
  }

  return 0;
}

/**
 * @brief set logic <id> enabled:true|false
 *
 * Enable or disable a logic program
 *
 * Example:
 *   set logic 1 enabled:true
 */
int cli_cmd_set_logic_enabled(st_logic_engine_state_t *logic_state, uint8_t program_id,
                              bool enabled) {
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) {
    debug_printf("ERROR: Invalid program ID (0-%d)\n", ST_LOGIC_MAX_PROGRAMS - 1);
    return -1;
  }

  st_logic_program_config_t *prog = st_logic_get_program(logic_state, program_id);
  if (logic_state && logic_state->debug) {
    debug_printf("[ENABLED_DEBUG] program_id=%d, prog=%p, compiled=%d\n",
                 program_id, prog, prog ? prog->compiled : -1);
  }

  if (!prog->compiled) {
    debug_println("ERROR: Program not compiled. Upload source code first.");
    return -1;
  }

  if (!st_logic_set_enabled(logic_state, program_id, enabled)) {
    debug_println("ERROR: Failed to set enabled state");
    return -1;
  }

  debug_printf("[OK] Logic%d %s\n", program_id + 1, enabled ? "ENABLED" : "DISABLED");
  return 0;
}

/**
 * @brief set logic <id> reinit
 *
 * Cold restart: Reset all variables to compiled initial values (VAR declarations).
 * Also resets stateful builtins (TON/TOF, R_TRIG/F_TRIG, CTU/CTD) and FB instances.
 */
int cli_cmd_set_logic_reinit(st_logic_engine_state_t *logic_state, uint8_t program_id) {
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) {
    debug_printf("ERROR: Invalid program ID (0-%d)\n", ST_LOGIC_MAX_PROGRAMS - 1);
    return -1;
  }

  st_logic_program_config_t *prog = st_logic_get_program(logic_state, program_id);
  if (!prog || !prog->compiled) {
    debug_println("ERROR: Program not compiled. Nothing to reinitialize.");
    return -1;
  }

  if (!st_logic_reinit(logic_state, program_id)) {
    debug_println("ERROR: Failed to reinitialize program");
    return -1;
  }

  debug_printf("[OK] Logic%d cold restart (variables reset to initial values)\n", program_id + 1);
  return 0;
}

/**
 * @brief set logic debug:true|false
 *
 * Enable/disable debug output for ST Logic (bytecode printing, execution trace, etc.)
 *
 * Example:
 *   set logic debug:true
 *   set logic debug:false
 */
int cli_cmd_set_logic_debug(st_logic_engine_state_t *logic_state, bool debug) {
  if (!logic_state) {
    debug_println("ERROR: Logic state not initialized");
    return -1;
  }

  logic_state->debug = debug ? 1 : 0;
  debug_printf("[OK] ST Logic debug %s\n", debug ? "ENABLED" : "DISABLED");
  return 0;
}

/**
 * @brief set logic interval:X
 *
 * Set global execution interval for all ST Logic programs
 *
 * Allowed values: 2, 5, 10, 20, 25, 50, 75, 100 ms
 *
 * Example:
 *   set logic interval:2     # 2ms (very fast, may cause performance issues)
 *   set logic interval:5     # 5ms (fast)
 *   set logic interval:10    # 10ms (default)
 *   set logic interval:50    # 50ms (slow)
 *
 * @param logic_state Logic engine state
 * @param interval_ms Execution interval in milliseconds
 * @return 0 on success, -1 on error
 */
int cli_cmd_set_logic_interval(st_logic_engine_state_t *logic_state, uint32_t interval_ms) {
  if (!logic_state) {
    debug_println("ERROR: Logic state not initialized");
    return -1;
  }

  // Validate interval (only allow specific values)
  if (interval_ms != 2 && interval_ms != 5 && interval_ms != 10 && interval_ms != 20 &&
      interval_ms != 25 && interval_ms != 50 && interval_ms != 75 && interval_ms != 100) {
    debug_printf("ERROR: Invalid interval %ums (allowed: 2, 5, 10, 20, 25, 50, 75, 100)\n",
                 (unsigned int)interval_ms);
    return -1;
  }

  // Warn if very fast interval (may impact other system functions)
  if (interval_ms < 10) {
    debug_printf("⚠️  WARNING: Very fast interval (%ums) may impact other system operations\n",
                 (unsigned int)interval_ms);
  }

  logic_state->execution_interval_ms = interval_ms;

  // BUG-014 FIX: Also update persistent config so interval survives reboot
  extern PersistConfig g_persist_config;
  g_persist_config.st_logic_interval_ms = interval_ms;

  debug_printf("[OK] ST Logic execution interval set to %ums\n", (unsigned int)interval_ms);
  debug_println("Note: Use 'save' command to persist to NVS");
  return 0;
}

/**
 * @brief set logic <id> delete
 *
 * Delete a logic program
 *
 * Example:
 *   set logic 1 delete
 */
int cli_cmd_set_logic_delete(st_logic_engine_state_t *logic_state, uint8_t program_id) {
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) {
    debug_printf("ERROR: Invalid program ID (0-%d)\n", ST_LOGIC_MAX_PROGRAMS - 1);
    return -1;
  }

  if (!st_logic_delete(logic_state, program_id)) {
    debug_println("ERROR: Failed to delete program");
    return -1;
  }

  debug_printf("[OK] Logic%d deleted\n", program_id + 1);
  return 0;
}

/**
 * @brief Parse new bind syntax: set logic <id> bind <var_name> reg:100|coil:10|input-dis:5 [input|output|both]
 *
 * Finds variable by name and calls cli_cmd_set_logic_bind_by_index
 *
 * Example:
 *   set logic 1 bind sensor reg:100 input       # sensor reads from HR#100
 *   set logic 1 bind led reg:101 output         # led writes to HR#101
 *   set logic 1 bind angle reg:102 both         # angle reads and writes HR#102
 *   set logic 1 bind switch input-dis:5         # switch reads from discrete input (auto-input)
 */
int cli_cmd_set_logic_bind_by_name(st_logic_engine_state_t *logic_state, uint8_t program_id,
                                    const char *var_name, const char *binding_spec, const char *direction_override) {
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) {
    debug_printf("ERROR: Invalid program ID (0-%d)\n", ST_LOGIC_MAX_PROGRAMS - 1);
    return -1;
  }

  st_logic_program_config_t *prog = st_logic_get_program(logic_state, program_id);
  if (logic_state && logic_state->debug) {
    debug_printf("[BIND_DEBUG] program_id=%d, var=%s, prog=%p, compiled=%d\n",
                 program_id, var_name, prog, prog ? prog->compiled : -1);
  }

  if (!prog || !prog->compiled) {
    debug_println("ERROR: Program not compiled. Upload source code first.");
    return -1;
  }

  // Find variable by name
  uint8_t var_index = 0xff;
  for (uint8_t i = 0; i < prog->bytecode.var_count; i++) {
    if (strcmp(prog->bytecode.var_names[i], var_name) == 0) {
      var_index = i;
      break;
    }
  }

  if (var_index == 0xff) {
    debug_printf("ERROR: Variable '%s' not found in Logic%d\n", var_name, program_id + 1);
    debug_printf("Available variables: ");
    for (uint8_t i = 0; i < prog->bytecode.var_count; i++) {
      if (i > 0) debug_printf(", ");
      debug_printf(prog->bytecode.var_names[i]);
    }
    debug_println("");
    return -1;
  }

  // Debug output if debug mode enabled
  if (logic_state && logic_state->debug) {
    debug_printf("[BIND_DEBUG] Found variable '%s' at index %d\n", var_name, var_index);
  }

  // Parse binding spec: reg:100, coil:10, or input-dis:5
  uint16_t register_addr = 0;
  const char *direction = "output";  // default: output mode for ST variables (typically write to Modbus)
  uint8_t input_type = 0;  // 0 = Holding Register (HR), 1 = Discrete Input (DI), 2 = Coil (BUG-049)
  uint8_t output_type = 0;  // 0 = Holding Register (HR), 1 = Coil

  if (strncmp(binding_spec, "reg:", 4) == 0) {
    // Holding register (16-bit integer) - default OUTPUT, but can be overridden
    register_addr = atoi(binding_spec + 4);
    direction = "output";  // Default for reg:
    input_type = 0;  // HR
    output_type = 0;  // HR
  } else if (strncmp(binding_spec, "coil:", 5) == 0) {
    // Coil (BOOL read/write) - default OUTPUT, but can be overridden
    register_addr = atoi(binding_spec + 5);
    direction = "output";  // Default for coil:
    input_type = 2;  // Coil (BUG-049 FIX: was 1=DI, now 2=Coil for input mode)
    output_type = 1;  // Coil
  } else if (strncmp(binding_spec, "input-dis:", 10) == 0 || strncmp(binding_spec, "input:", 6) == 0) {
    // Discrete input (BOOL read) - always INPUT mode (cannot override)
    register_addr = (strncmp(binding_spec, "input-dis:", 10) == 0) ?
                    atoi(binding_spec + 10) : atoi(binding_spec + 6);
    direction = "input";
    input_type = 1;  // DI
    output_type = 0;  // N/A for input
  } else {
    debug_printf("ERROR: Invalid binding spec '%s' (use reg:, coil:, or input:)\n", binding_spec);
    return -1;
  }

  // BUG-048 FIX: Override direction if explicitly specified
  if (direction_override && strlen(direction_override) > 0) {
    if (strcmp(direction_override, "input") == 0 ||
        strcmp(direction_override, "output") == 0 ||
        strcmp(direction_override, "both") == 0) {
      direction = direction_override;
    } else {
      debug_printf("ERROR: Invalid direction '%s' (use input, output, or both)\n", direction_override);
      return -1;
    }
  }

  // Validate register address
  if (register_addr >= 160) {  // HOLDING_REGS_SIZE
    debug_printf("ERROR: Invalid Modbus register %d (0-159)\n", register_addr);
    return -1;
  }

  // Debug output if debug mode enabled
  if (logic_state && logic_state->debug) {
    debug_printf("[BIND_DEBUG] Parsed binding: reg=%d, dir=%s, input_type=%d, output_type=%d\n",
                 register_addr, direction, input_type, output_type);
  }

  // Call original bind function with input_type and output_type
  return cli_cmd_set_logic_bind(logic_state, program_id, var_index, register_addr, direction, input_type, output_type);
}

/* ============================================================================
 * HELPER: Persistent Counter Cleanup on ST Logic Binding Change (BUG-026 FIX)
 * ============================================================================ */

/**
 * @brief Disable and persist removal of any counters using registers in range
 *
 * When ST Logic binding changes to use a register (e.g., HR100), we must:
 * 1. Free the register from RAM allocator (done in main bind function)
 * 2. Disable persistent counters that use same register (this function)
 *
 * Without step 2: On next boot, register_allocator_init() will pre-allocate
 * the register to the counter from persistent config, causing conflicts again.
 *
 * @param old_reg The old register used by ST Logic binding (being freed)
 */
static void cleanup_counters_using_register(uint16_t old_reg) {
  // Only need to cleanup if it's in the tracked range
  if (old_reg >= ALLOCATOR_SIZE) {
    return;
  }

  // Check all counters (including disabled ones!)
  for (uint8_t counter_id = 1; counter_id <= 4; counter_id++) {
    CounterConfig cfg;
    if (!counter_config_get(counter_id, &cfg)) {
      continue;  // Counter not configured
    }

    // BUG-026 EXTENSION FIX: Check BOTH enabled AND disabled counters
    // Disabled counters may still have register allocations in persistent config
    // that will conflict on next boot

    // Check if this counter uses the register we're freeing
    // Each counter uses 4 registers: value_reg, raw_reg, freq_reg, ctrl_reg (overflow in ctrl_reg bit 3)
    bool uses_register = (
      cfg.value_reg == old_reg ||
      cfg.raw_reg == old_reg ||
      cfg.freq_reg == old_reg ||
      cfg.ctrl_reg == old_reg
    );

    if (uses_register) {
      DebugFlags* dbg = debug_flags_get();
      if (dbg->config_save) {
        debug_printf("[CLEANUP] Counter %d uses HR%d - resetting to defaults to avoid boot conflict\n",
                     counter_id, old_reg);
      }

      // Reset counter to defaults (clears register allocations)
      // This ensures persistent config doesn't contain stale register values
      CounterConfig default_cfg = counter_config_defaults(counter_id);
      default_cfg.enabled = false;  // Keep disabled
      counter_config_set(counter_id, &default_cfg);

      // Also update persistent config directly (will be saved at end of bind function)
      g_persist_config.counters[counter_id - 1] = default_cfg;

      // Note: The counter will be saved to persistent config by config_save_to_nvs()
      // called at the end of the bind function
    }
  }
}

/**
 * @brief set logic <id> bind <var_idx> <register> [input|output|both]
 *
 * Bind ST variable to Modbus register (unified VariableMapping system)
 * LEGACY: Use cli_cmd_set_logic_bind_by_name for new syntax
 *
 * Example:
 *   set logic 1 bind 0 100 input   # ST var[0] reads from HR#100
 *   set logic 1 bind 1 101 output  # ST var[1] writes to HR#101
 */
int cli_cmd_set_logic_bind(st_logic_engine_state_t *logic_state, uint8_t program_id,
                           uint8_t var_index, uint16_t modbus_reg, const char *direction, uint8_t input_type, uint8_t output_type) {
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) {
    debug_printf("ERROR: Invalid program ID (0-%d)\n", ST_LOGIC_MAX_PROGRAMS - 1);
    return -1;
  }

  st_logic_program_config_t *prog = st_logic_get_program(logic_state, program_id);
  if (!prog || !prog->compiled) {
    debug_println("ERROR: Program not compiled. Upload source code first.");
    return -1;
  }

  if (var_index >= prog->bytecode.var_count) {
    debug_printf("ERROR: Invalid variable index (0-%d)\n", prog->bytecode.var_count - 1);
    return -1;
  }

  if (modbus_reg >= HOLDING_REGS_SIZE) {
    debug_printf("ERROR: Invalid Modbus register (0-%d)\n", HOLDING_REGS_SIZE - 1);
    return -1;
  }

  uint8_t is_input = 0, is_output = 0;

  if (strcmp(direction, "input") == 0) {
    is_input = 1;
  } else if (strcmp(direction, "output") == 0) {
    is_output = 1;
  } else if (strcmp(direction, "both") == 0 || strcmp(direction, "inout") == 0) {
    is_input = 1;
    is_output = 1;
  } else {
    debug_println("ERROR: Direction must be 'input', 'output', or 'both'");
    return -1;
  }

  // IMPORTANT: VariableMapping.is_input is a MODE flag (1=INPUT, 0=OUTPUT), NOT a capability flag!
  // This means we CANNOT have a single mapping that does both INPUT and OUTPUT.
  // For "both" mode, we need to create TWO separate mappings:
  //   - Mapping 1: INPUT mode (Modbus → ST var)
  //   - Mapping 2: OUTPUT mode (ST var → Modbus)

  // Step 1: Delete ALL existing bindings for this ST variable (we'll recreate them)
  for (uint8_t i = 0; i < g_persist_config.var_map_count; i++) {
    VariableMapping *map = &g_persist_config.var_maps[i];
    if (map->source_type == MAPPING_SOURCE_ST_VAR &&
        map->st_program_id == program_id &&
        map->st_var_index == var_index) {
      // BUG-EXTENSION & BUG-105: Free old registers from allocation map (multi-register aware)
      uint8_t old_word_count = (map->word_count > 0) ? map->word_count : 1;

      if (map->is_input && map->input_type == 0 && map->input_reg < ALLOCATOR_SIZE) {
        for (uint8_t w = 0; w < old_word_count; w++) {
          if (map->input_reg + w < ALLOCATOR_SIZE) {
            register_allocator_free(map->input_reg + w);
            // BUG-026 FIX: Also cleanup any counters using same register (persistent config)
            cleanup_counters_using_register(map->input_reg + w);
          }
        }
      }
      if (!map->is_input && map->output_type == 0 && map->coil_reg < ALLOCATOR_SIZE) {
        for (uint8_t w = 0; w < old_word_count; w++) {
          if (map->coil_reg + w < ALLOCATOR_SIZE) {
            register_allocator_free(map->coil_reg + w);
            // BUG-026 FIX: Also cleanup any counters using same register (persistent config)
            cleanup_counters_using_register(map->coil_reg + w);
          }
        }
      }

      // Delete this mapping by shifting all subsequent mappings down
      for (uint8_t j = i; j < g_persist_config.var_map_count - 1; j++) {
        g_persist_config.var_maps[j] = g_persist_config.var_maps[j + 1];
      }
      g_persist_config.var_map_count--;
      i--;  // Re-check this index (we just shifted)
    }
  }

  // Step 2: Check if we have space for new mapping(s)
  uint8_t mappings_needed = (is_input && is_output) ? 2 : 1;
  if (g_persist_config.var_map_count + mappings_needed > 64) {
    debug_printf("ERROR: Maximum variable mappings (64) would be exceeded (need %d more)\n", mappings_needed);
    return -1;
  }

  // BUG-025 FIX: Check register overlap before creating binding
  // Only check if this is a holding register binding (input_type=0 or output_type=0)
  if ((is_input && input_type == 0) || (is_output && output_type == 0)) {
    // Check if register is in protected ST Logic range (200-293)
    if (modbus_reg >= 200 && modbus_reg <= 293) {
      debug_printf("ERROR: Register HR%d already allocated!\n", modbus_reg);
      debug_printf("  Owner: ST Logic Fixed (status/control)\n");
      debug_printf("  Suggestion: Try HR0-99 or HR105-159\n");
      return -1;
    }

    // Check if register is allocated by other subsystems
    if (modbus_reg < ALLOCATOR_SIZE) {
      RegisterOwner owner;
      if (!register_allocator_check(modbus_reg, &owner)) {
        // Register is already allocated
        debug_printf("ERROR: Register HR%d already allocated!\n", modbus_reg);
        debug_printf("  Owner: %s\n", owner.description);

        // Suggest free registers in range
        uint16_t suggested = register_allocator_find_free(0, 99);
        if (suggested != 0xFFFF) {
          debug_printf("  Suggestion: Try HR%d or higher\n", suggested);
        }
        return -1;
      }
    }
  }

  // BUG-105: Auto-detect word_count based on ST variable type
  st_datatype_t var_type = prog->bytecode.var_types[var_index];
  uint8_t word_count = 1;  // Default: BOOL, INT (16-bit)
  if (var_type == ST_TYPE_DINT || var_type == ST_TYPE_DWORD || var_type == ST_TYPE_REAL) {
    word_count = 2;  // 32-bit types require 2 consecutive registers
  }

  // Step 3: Create new mapping(s)
  if (is_input && is_output) {
    // "both" mode: Create TWO mappings (INPUT + OUTPUT)

    // Mapping 1: INPUT (Modbus → ST var)
    VariableMapping *map_in = &g_persist_config.var_maps[g_persist_config.var_map_count++];
    memset(map_in, 0xff, sizeof(*map_in));
    map_in->source_type = MAPPING_SOURCE_ST_VAR;
    map_in->st_program_id = program_id;
    map_in->st_var_index = var_index;
    map_in->is_input = 1;
    map_in->input_type = input_type;  // Use parameter (0=HR, 1=DI/Coil)
    map_in->input_reg = modbus_reg;
    map_in->word_count = word_count;  // BUG-105

    // Mapping 2: OUTPUT (ST var → Modbus)
    VariableMapping *map_out = &g_persist_config.var_maps[g_persist_config.var_map_count++];
    memset(map_out, 0xff, sizeof(*map_out));
    map_out->source_type = MAPPING_SOURCE_ST_VAR;
    map_out->st_program_id = program_id;
    map_out->st_var_index = var_index;
    map_out->is_input = 0;
    map_out->output_type = output_type;  // Use parameter (0=HR, 1=Coil)
    map_out->coil_reg = modbus_reg;
    map_out->word_count = word_count;  // BUG-105

    debug_printf("[OK] Logic%d: var[%d] (%s) <-> Modbus HR#%d (2 mappings created)\n",
                 program_id + 1, var_index, prog->bytecode.var_names[var_index], modbus_reg);

    // BUG-EXTENSION & BUG-105: Allocate register(s) in global allocator (multi-register aware)
    if (input_type == 0 && modbus_reg < ALLOCATOR_SIZE) {
      for (uint8_t w = 0; w < word_count; w++) {
        if (modbus_reg + w < ALLOCATOR_SIZE) {
          register_allocator_allocate(modbus_reg + w, REG_OWNER_ST_VAR, program_id + 1, "i/o");
        }
      }
    }
  } else {
    // "input" or "output" mode: Create ONE mapping
    VariableMapping *map = &g_persist_config.var_maps[g_persist_config.var_map_count++];
    memset(map, 0xff, sizeof(*map));
    map->source_type = MAPPING_SOURCE_ST_VAR;
    map->st_program_id = program_id;
    map->st_var_index = var_index;
    map->word_count = word_count;  // BUG-105

    if (is_input) {
      map->is_input = 1;
      map->input_type = input_type;
      map->input_reg = modbus_reg;
    } else {
      map->is_input = 0;
      map->output_type = output_type;
      map->coil_reg = modbus_reg;
    }

    // Print confirmation
    debug_printf("[OK] Logic%d: var[%d] (%s) ", program_id + 1, var_index, prog->bytecode.var_names[var_index]);

    // BUG-107 FIX: Display correct type for all input/output combinations
    if (is_input && input_type == 1) {
      debug_printf("<- Modbus INPUT#%d\n", modbus_reg);
    } else if (is_input && input_type == 2) {
      debug_printf("<- Modbus COIL#%d\n", modbus_reg);
    } else if (is_input) {
      debug_printf("<- Modbus HR#%d\n", modbus_reg);
    } else if (output_type == 1) {
      debug_printf("-> Modbus COIL#%d\n", modbus_reg);
    } else {
      debug_printf("-> Modbus HR#%d\n", modbus_reg);
    }

    // BUG-EXTENSION & BUG-105: Allocate register(s) in global allocator (multi-register aware)
    if (((is_input && input_type == 0) || (!is_input && output_type == 0)) && modbus_reg < ALLOCATOR_SIZE) {
      for (uint8_t w = 0; w < word_count; w++) {
        if (modbus_reg + w < ALLOCATOR_SIZE) {
          register_allocator_allocate(modbus_reg + w, REG_OWNER_ST_VAR, program_id + 1, is_input ? "in" : "out");
        }
      }
    }
  }

  // BUG-005 FIX: Update binding count cache after modifying bindings
  st_logic_update_binding_counts(logic_state);

  // Save config to NVS to make binding persistent
  // First, copy ST Logic programs to persistent config
  st_logic_save_to_persist_config(&g_persist_config);
  if (!config_save_to_nvs(&g_persist_config)) {
    debug_println("WARNING: Failed to save config to NVS (binding still active in runtime)");
  }

  return 0;
}

/* ============================================================================
 * SHOW COMMANDS
 * ============================================================================ */

/**
 * @brief show logic <id> [st]
 *
 * Show detailed info about a logic program
 * @param show_source 1=show ST source code, 0=hide (v5.1.0)
 */
int cli_cmd_show_logic_program(st_logic_engine_state_t *logic_state, uint8_t program_id, uint8_t show_source) {
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) {
    debug_printf("ERROR: Invalid program ID (0-%d)\n", ST_LOGIC_MAX_PROGRAMS - 1);
    return -1;
  }

  st_logic_print_program(logic_state, program_id, show_source);
  return 0;
}

/**
 * @brief show logic all
 *
 * Show status of all logic programs
 */
int cli_cmd_show_logic_all(st_logic_engine_state_t *logic_state) {
  st_logic_print_status(logic_state);
  return 0;
}


/**
 * @brief show logic program
 *
 * Show overview of all programs with status summary
 */
int cli_cmd_show_logic_programs(st_logic_engine_state_t *logic_state) {
  debug_printf("\n=== All Logic Programs ===\n\n");

  for (int i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
    st_logic_program_config_t *prog = &logic_state->programs[i];

    // Program header with status indicator
    const char *status = "";
    if (!prog->source_size) {
      status = "⚪ EMPTY";
    } else if (!prog->compiled) {
      status = "🔴 FAILED";
    } else if (!prog->enabled) {
      status = "🟡 DISABLED";
    } else {
      status = "🟢 ACTIVE";
    }

    debug_printf("  [%d] %s %s\n", i + 1, prog->name, status);

    if (prog->source_size > 0) {
      debug_printf("      Source: %d bytes | Variables: %d\n", prog->source_size, prog->bytecode.var_count);
      debug_printf("      Executions: %u | Errors: %u\n", prog->execution_count, prog->error_count);

      if (prog->error_count > 0 && prog->last_error[0] != '\0') {
        debug_printf("      Last error: %s\n", prog->last_error);
      }
    }
  }

  debug_printf("\n");
  return 0;
}

/**
 * @brief show logic errors
 *
 * Show only programs with compilation or runtime errors
 */
int cli_cmd_show_logic_errors(st_logic_engine_state_t *logic_state) {
  debug_printf("\n=== Logic Program Errors ===\n\n");

  int error_count = 0;

  for (int i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
    st_logic_program_config_t *prog = &logic_state->programs[i];

    // Show if: has compilation error OR has runtime errors
    bool has_compilation_error = (prog->source_size > 0 && !prog->compiled);
    bool has_runtime_errors = (prog->error_count > 0);

    if (has_compilation_error || has_runtime_errors) {
      error_count++;

      debug_printf("  [Logic%d] %s\n", i + 1, prog->name);
      debug_printf("    Status: %s\n", prog->compiled ? "COMPILED" : "NOT COMPILED");

      if (has_compilation_error || (has_runtime_errors && prog->last_error[0] != '\0')) {
        debug_printf("    Error: %s\n", prog->last_error);
      }

      if (has_runtime_errors) {
        debug_printf("    Runtime Errors: %u\n", prog->error_count);
        float error_rate = prog->execution_count > 0 ?
          (float)prog->error_count / prog->execution_count * 100.0f : 0.0f;
        debug_printf("    Error Rate: %.2f%% (%u/%u executions)\n",
               error_rate, prog->error_count, prog->execution_count);
      }

      debug_printf("\n");
    }
  }

  if (error_count == 0) {
    debug_printf("  ✓ No errors found!\n\n");
  } else {
    debug_printf("  Total programs with errors: %d/%d\n\n", error_count, ST_LOGIC_MAX_PROGRAMS);
  }

  return 0;
}

/**
 * @brief show logic <id> code
 *
 * Show source code for a specific logic program with line breaks
 */
int cli_cmd_show_logic_code(st_logic_engine_state_t *logic_state, uint8_t program_id) {
  st_logic_program_config_t *prog = st_logic_get_program(logic_state, program_id);
  if (!prog) {
    debug_printf("[ERROR] Invalid program ID: %d\n", program_id);
    return 1;
  }

  debug_printf("\n=== Logic Program: %s - Source Code ===\n\n", prog->name);
  debug_printf("Status: %s | Compiled: %s | Size: %d bytes\n\n",
         prog->enabled ? "ENABLED" : "DISABLED",
         prog->compiled ? "YES" : "NO",
         prog->source_size);

  if (prog->source_size == 0) {
    debug_printf("(empty - no program uploaded)\n\n");
    return 0;
  }

  // Print source code with proper line breaks
  debug_printf("--- SOURCE CODE ---\n");
  const char *source = st_logic_get_source_code(logic_state, program_id);

  // Print source code - use debug_printf with %.*s to respect Telnet routing
  if (source) {
    debug_printf("%.*s\n", prog->source_size, source);
  }

  debug_printf("--- END SOURCE CODE ---\n\n");
  return 0;
}

/**
 * @brief show logic all code
 *
 * Show source code for all logic programs
 */
int cli_cmd_show_logic_code_all(st_logic_engine_state_t *logic_state) {
  debug_printf("\n========================================\n");
  debug_printf("  All Logic Programs - Source Code\n");
  debug_printf("========================================\n\n");

  for (int i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
    st_logic_program_config_t *prog = &logic_state->programs[i];

    debug_printf("--- [%d] %s ---\n", i + 1, prog->name);
    debug_printf("Status: %s | Compiled: %s | Size: %d bytes\n",
           prog->enabled ? "ENABLED" : "DISABLED",
           prog->compiled ? "YES" : "NO",
           prog->source_size);

    if (prog->source_size == 0) {
      debug_printf("(empty - no program uploaded)\n");
    } else {
      debug_printf("\nSource:\n");
      const char *source = st_logic_get_source_code(logic_state, i);
      // Print source code - use debug_printf with %.*s to respect Telnet routing
      if (source) {
        debug_printf("%.*s\n", prog->source_size, source);
      }
    }

    debug_printf("\n");
  }

  debug_printf("========================================\n\n");
  return 0;
}

/**
 * @brief show logic <id> bytecode - Display compiled bytecode for a program
 *
 * Shows the compiled bytecode instructions with opcodes and arguments
 *
 * @param logic_state ST Logic engine state
 * @param program_id Program ID (0-3)
 * @return 0 on success, -1 on error
 */
int cli_cmd_show_logic_bytecode(st_logic_engine_state_t *logic_state, uint8_t program_id) {
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) {
    debug_printf("ERROR: Invalid program ID (1-%d)\n", ST_LOGIC_MAX_PROGRAMS);
    return -1;
  }

  st_logic_program_config_t *prog = &logic_state->programs[program_id];

  debug_printf("\n======== Logic%d Bytecode Dump ========\n\n", program_id + 1);

  if (!prog->compiled || prog->bytecode.instr_count == 0) {
    debug_printf("Program not compiled or empty.\n\n");
    return 0;
  }

  debug_printf("Program: %s\n", prog->name);
  debug_printf("Status:  %s\n", prog->enabled ? "ENABLED" : "DISABLED");
  debug_printf("Instructions: %u\n", prog->bytecode.instr_count);
  debug_printf("Variables: %u\n\n", prog->bytecode.var_count);

  // Show variable table
  if (prog->bytecode.var_count > 0) {
    debug_printf("--- Variable Table ---\n");
    for (uint8_t i = 0; i < prog->bytecode.var_count; i++) {
      debug_printf("  [%d] %s (type=%d)\n", i, prog->bytecode.var_names[i], prog->bytecode.var_types[i]);
    }
    debug_printf("\n");
  }

  // Show bytecode instructions
  debug_printf("--- Bytecode Instructions ---\n");
  for (uint16_t i = 0; i < prog->bytecode.instr_count; i++) {
    const st_bytecode_instr_t *instr = &prog->bytecode.instructions[i];
    debug_printf("%04d: ", i);

    switch (instr->opcode) {
      case ST_OP_PUSH_BOOL:
        debug_printf("PUSH_BOOL %d", instr->arg.bool_arg);
        break;
      case ST_OP_PUSH_INT:
        debug_printf("PUSH_INT %d", instr->arg.int_arg);
        break;
      case ST_OP_PUSH_DWORD:
        debug_printf("PUSH_DWORD %u", (unsigned int)instr->arg.dword_arg);
        break;
      case ST_OP_PUSH_REAL:
        debug_printf("PUSH_REAL %f", instr->arg.float_arg);
        break;
      case ST_OP_PUSH_VAR:
        debug_printf("PUSH_VAR [%d]", instr->arg.var_index);
        if (instr->arg.var_index < prog->bytecode.var_count) {
          debug_printf(" ; %s", prog->bytecode.var_names[instr->arg.var_index]);
        }
        break;
      case ST_OP_DUP:
        debug_printf("DUP");
        break;
      case ST_OP_POP:
        debug_printf("POP");
        break;
      case ST_OP_ADD:
        debug_printf("ADD");
        break;
      case ST_OP_SUB:
        debug_printf("SUB");
        break;
      case ST_OP_MUL:
        debug_printf("MUL");
        break;
      case ST_OP_DIV:
        debug_printf("DIV");
        break;
      case ST_OP_MOD:
        debug_printf("MOD");
        break;
      case ST_OP_NEG:
        debug_printf("NEG");
        break;
      case ST_OP_AND:
        debug_printf("AND");
        break;
      case ST_OP_OR:
        debug_printf("OR");
        break;
      case ST_OP_NOT:
        debug_printf("NOT");
        break;
      case ST_OP_XOR:
        debug_printf("XOR");
        break;
      case ST_OP_SHL:
        debug_printf("SHL");
        break;
      case ST_OP_SHR:
        debug_printf("SHR");
        break;
      case ST_OP_EQ:
        debug_printf("EQ");
        break;
      case ST_OP_NE:
        debug_printf("NE");
        break;
      case ST_OP_LT:
        debug_printf("LT");
        break;
      case ST_OP_GT:
        debug_printf("GT");
        break;
      case ST_OP_LE:
        debug_printf("LE");
        break;
      case ST_OP_GE:
        debug_printf("GE");
        break;
      case ST_OP_JMP:
        debug_printf("JMP %d", instr->arg.int_arg);
        break;
      case ST_OP_JMP_IF_FALSE:
        debug_printf("JMP_IF_FALSE %d", instr->arg.int_arg);
        break;
      case ST_OP_JMP_IF_TRUE:
        debug_printf("JMP_IF_TRUE %d", instr->arg.int_arg);
        break;
      case ST_OP_STORE_VAR:
        debug_printf("STORE_VAR [%d]", instr->arg.var_index);
        if (instr->arg.var_index < prog->bytecode.var_count) {
          debug_printf(" ; %s", prog->bytecode.var_names[instr->arg.var_index]);
        }
        break;
      case ST_OP_LOAD_VAR:
        debug_printf("LOAD_VAR [%d]", instr->arg.var_index);
        if (instr->arg.var_index < prog->bytecode.var_count) {
          debug_printf(" ; %s", prog->bytecode.var_names[instr->arg.var_index]);
        }
        break;
      case ST_OP_LOOP_INIT:
        debug_printf("LOOP_INIT");
        break;
      case ST_OP_LOOP_TEST:
        debug_printf("LOOP_TEST");
        break;
      case ST_OP_LOOP_NEXT:
        debug_printf("LOOP_NEXT");
        break;
      case ST_OP_CALL_BUILTIN:
        debug_printf("CALL_BUILTIN %d", instr->arg.int_arg);
        break;
      // FEAT-003: User-defined function opcodes
      case ST_OP_CALL_USER: {
        uint8_t fi = instr->arg.user_call.func_index;
        uint8_t ii = instr->arg.user_call.instance_id;
        debug_printf("CALL_USER func[%d]", fi);
        if (prog->bytecode.func_registry && fi < prog->bytecode.func_registry->builtin_count + prog->bytecode.func_registry->user_count) {
          debug_printf(" ; %s", prog->bytecode.func_registry->functions[fi].name);
        }
        if (ii != 0xFF) {
          debug_printf(" inst=%d", ii);
        }
        break;
      }
      case ST_OP_RETURN:
        debug_printf("RETURN");
        break;
      case ST_OP_LOAD_PARAM:
        debug_printf("LOAD_PARAM [%d]", instr->arg.var_index);
        break;
      case ST_OP_STORE_LOCAL:
        debug_printf("STORE_LOCAL [%d]", instr->arg.var_index);
        break;
      case ST_OP_LOAD_LOCAL:
        debug_printf("LOAD_LOCAL [%d]", instr->arg.var_index);
        break;
      case ST_OP_ADD_CHECKED:
        debug_printf("ADD_CHECKED");
        break;
      case ST_OP_NOP:
        debug_printf("NOP");
        break;
      case ST_OP_HALT:
        debug_printf("HALT");
        break;
      default:
        debug_printf("UNKNOWN_OP(%d)", instr->opcode);
        break;
    }
    debug_printf("\n");
  }

  debug_printf("\n========================================\n\n");
  return 0;
}

/**
 * @brief show logic stats - Display performance statistics for all programs (v4.1.0)
 */
int cli_cmd_show_logic_stats(st_logic_engine_state_t *logic_state) {
  if (!logic_state) return -1;

  debug_printf("\n======== ST Logic Performance Statistics ========\n\n");

  // Global cycle statistics
  debug_printf("Global Cycle Stats:\n");
  debug_printf("  Total cycles:    %u\n", (unsigned int)logic_state->total_cycles);
  debug_printf("  Cycle min:       %ums\n", (unsigned int)logic_state->cycle_min_ms);
  debug_printf("  Cycle max:       %ums\n", (unsigned int)logic_state->cycle_max_ms);

  if (logic_state->total_cycles > 0) {
    debug_printf("  Cycle target:    %ums\n", (unsigned int)logic_state->execution_interval_ms);
    debug_printf("  Overruns:        %u (%.1f%%)\n",
                 (unsigned int)logic_state->cycle_overrun_count,
                 (float)logic_state->cycle_overrun_count * 100.0 / logic_state->total_cycles);
  }

  debug_printf("\n");

  // Pool memory statistics
  uint32_t pool_used, pool_free, pool_largest;
  st_logic_get_pool_stats(logic_state, &pool_used, &pool_free, &pool_largest);

  debug_printf("Memory Pool Stats:\n");
  debug_printf("  Pool size:       %d bytes (8KB shared)\n", ST_LOGIC_POOL_SIZE);
  debug_printf("  Used:            %u bytes (%d%%)\n",
               (unsigned int)pool_used,
               (int)((pool_used * 100) / ST_LOGIC_POOL_SIZE));
  debug_printf("  Free:            %u bytes (%d%%)\n",
               (unsigned int)pool_free,
               (int)((pool_free * 100) / ST_LOGIC_POOL_SIZE));
  debug_printf("  Largest free:    %u bytes\n", (unsigned int)pool_largest);

  debug_printf("\n");

  // Per-program statistics
  for (uint8_t i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
    st_logic_program_config_t *prog = &logic_state->programs[i];

    // Skip only if program is empty AND has never run
    if (prog->source_size == 0 && prog->execution_count == 0) {
      continue;  // Skip empty programs with no history
    }

    debug_printf("Logic%d (%s):\n", i + 1, prog->name);
    debug_printf("  Status:        %s%s%s\n",
                 prog->enabled ? "ENABLED" : "disabled",
                 prog->compiled ? ", compiled" : "",
                 prog->error_count > 0 ? ", HAS ERRORS" : "");

    debug_printf("  Source size:   %u bytes", (unsigned int)prog->source_size);
    if (prog->source_size > 0) {
      debug_printf(" (%.1f%% of pool)", (float)prog->source_size * 100.0 / ST_LOGIC_POOL_SIZE);
    }
    debug_printf("\n");

    debug_printf("  Executions:    %u\n", (unsigned int)prog->execution_count);

    if (prog->execution_count > 0) {
      uint32_t avg_us = prog->total_execution_us / prog->execution_count;

      debug_printf("  Min time:      %u.%03ums\n",
                   (unsigned int)(prog->min_execution_us / 1000),
                   (unsigned int)(prog->min_execution_us % 1000));
      debug_printf("  Max time:      %u.%03ums\n",
                   (unsigned int)(prog->max_execution_us / 1000),
                   (unsigned int)(prog->max_execution_us % 1000));
      debug_printf("  Avg time:      %u.%03ums\n",
                   (unsigned int)(avg_us / 1000),
                   (unsigned int)(avg_us % 1000));
      debug_printf("  Last time:     %u.%03ums\n",
                   (unsigned int)(prog->last_execution_us / 1000),
                   (unsigned int)(prog->last_execution_us % 1000));

      if (prog->overrun_count > 0) {
        debug_printf("  Overruns:      %u (%.1f%%) ⚠️\n",
                     (unsigned int)prog->overrun_count,
                     (float)prog->overrun_count * 100.0 / prog->execution_count);
      }

      if (prog->error_count > 0) {
        debug_printf("  Errors:        %u (%.1f%%) ❌\n",
                     (unsigned int)prog->error_count,
                     (float)prog->error_count * 100.0 / prog->execution_count);
      }
    }

    debug_printf("\n");
  }

  debug_printf("Use 'show logic X timing' for detailed timing analysis\n");
  debug_printf("Use 'set logic stats reset' to clear statistics\n");
  debug_printf("=================================================\n\n");

  return 0;
}

/**
 * @brief show logic X timing - Display detailed timing analysis for specific program (v4.1.0)
 */
int cli_cmd_show_logic_timing(st_logic_engine_state_t *logic_state, uint8_t program_id) {
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) {
    debug_printf("ERROR: Invalid program ID (1-%d)\n", ST_LOGIC_MAX_PROGRAMS);
    return -1;
  }

  st_logic_program_config_t *prog = &logic_state->programs[program_id];

  debug_printf("\n======== Logic%d Timing Analysis ========\n\n", program_id + 1);

  if (!prog->enabled && prog->execution_count == 0) {
    debug_printf("Program has never been executed.\n\n");
    return 0;
  }

  debug_printf("Program: %s\n", prog->name);
  debug_printf("Status:  %s%s\n",
               prog->enabled ? "ENABLED" : "DISABLED",
               prog->compiled ? " (compiled)" : " (not compiled)");
  debug_printf("\n");

  // Execution statistics
  debug_printf("Execution Statistics:\n");
  debug_printf("  Total executions:  %u\n", (unsigned int)prog->execution_count);
  debug_printf("  Successful:        %u\n", (unsigned int)(prog->execution_count - prog->error_count));
  debug_printf("  Failed:            %u\n", (unsigned int)prog->error_count);

  if (prog->error_count > 0) {
    debug_printf("  Success rate:      %.1f%%\n",
                 (float)(prog->execution_count - prog->error_count) * 100.0 / prog->execution_count);
    debug_printf("  Last error:        %s\n", prog->last_error);
  }
  debug_printf("\n");

  // Timing statistics
  if (prog->execution_count > 0) {
    uint32_t avg_us = prog->total_execution_us / prog->execution_count;

    debug_printf("Timing Performance:\n");
    debug_printf("  Min:               %u.%03ums (%uµs)\n",
                 (unsigned int)(prog->min_execution_us / 1000),
                 (unsigned int)(prog->min_execution_us % 1000),
                 (unsigned int)prog->min_execution_us);
    debug_printf("  Max:               %u.%03ums (%uµs)\n",
                 (unsigned int)(prog->max_execution_us / 1000),
                 (unsigned int)(prog->max_execution_us % 1000),
                 (unsigned int)prog->max_execution_us);
    debug_printf("  Avg:               %u.%03ums (%uµs)\n",
                 (unsigned int)(avg_us / 1000),
                 (unsigned int)(avg_us % 1000),
                 (unsigned int)avg_us);
    debug_printf("  Last:              %u.%03ums (%uµs)\n",
                 (unsigned int)(prog->last_execution_us / 1000),
                 (unsigned int)(prog->last_execution_us % 1000),
                 (unsigned int)prog->last_execution_us);
    debug_printf("\n");

    // Performance analysis
    debug_printf("Performance Analysis:\n");
    debug_printf("  Target interval:   %ums\n", (unsigned int)logic_state->execution_interval_ms);

    uint32_t avg_ms = avg_us / 1000;
    if (avg_ms < logic_state->execution_interval_ms / 4) {
      debug_printf("  Rating:            ✓ EXCELLENT (< 25%% of target)\n");
    } else if (avg_ms < logic_state->execution_interval_ms / 2) {
      debug_printf("  Rating:            ✓ GOOD (< 50%% of target)\n");
    } else if (avg_ms < logic_state->execution_interval_ms) {
      debug_printf("  Rating:            ⚠️  ACCEPTABLE (< 100%% of target)\n");
    } else {
      debug_printf("  Rating:            ❌ POOR (> 100%% of target) - REFACTOR NEEDED!\n");
    }

    if (prog->overrun_count > 0) {
      debug_printf("  Overruns:          %u (%.1f%%) ⚠️\n",
                   (unsigned int)prog->overrun_count,
                   (float)prog->overrun_count * 100.0 / prog->execution_count);
      debug_printf("                     Program exceeded target interval!\n");
    } else {
      debug_printf("  Overruns:          0 (0.0%%) ✓\n");
    }
    debug_printf("\n");

    // Recommendations
    if (avg_ms > logic_state->execution_interval_ms) {
      debug_printf("⚠️  RECOMMENDATIONS:\n");
      debug_printf("  - Simplify program logic (reduce loop iterations)\n");
      debug_printf("  - Increase execution interval (set logic interval:20)\n");
      debug_printf("  - Split program into smaller sub-programs\n");
      debug_printf("  - Consider moving heavy computation outside ST Logic\n");
    } else if (prog->overrun_count > 0) {
      debug_printf("⚠️  RECOMMENDATIONS:\n");
      debug_printf("  - Occasional overruns detected\n");
      debug_printf("  - Check for conditional paths that take longer\n");
      debug_printf("  - Monitor with 'set logic debug:true'\n");
    }
  }

  debug_printf("==========================================\n\n");
  return 0;
}

/**
 * @brief set logic stats reset [X] - Reset statistics (v4.1.0)
 */
int cli_cmd_reset_logic_stats(st_logic_engine_state_t *logic_state, const char *target) {
  if (!logic_state) return -1;

  if (strcmp(target, "all") == 0) {
    // Reset all program stats
    st_logic_reset_stats(logic_state, 0xFF);
    st_logic_reset_cycle_stats(logic_state);
    debug_printf("All ST Logic statistics reset.\n");
  } else if (strcmp(target, "cycle") == 0) {
    // Reset only global cycle stats
    st_logic_reset_cycle_stats(logic_state);
    debug_printf("Global cycle statistics reset.\n");
  } else {
    // Parse program ID (1-4)
    int prog_num = atoi(target);
    if (prog_num < 1 || prog_num > ST_LOGIC_MAX_PROGRAMS) {
      debug_printf("ERROR: Invalid target. Use: all, cycle, or 1-%d\n", ST_LOGIC_MAX_PROGRAMS);
      return -1;
    }

    st_logic_reset_stats(logic_state, prog_num - 1);
    debug_printf("Logic%d statistics reset.\n", prog_num);
  }

  return 0;
}

/* ============================================================================
 * FEAT-008: DEBUGGER COMMANDS
 * ============================================================================ */

int cli_cmd_set_logic_debug_pause(st_logic_engine_state_t *logic_state, uint8_t program_id) {
  if (!logic_state) return -1;
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) {
    debug_printf("ERROR: Invalid program ID (0-%d)\n", ST_LOGIC_MAX_PROGRAMS - 1);
    return -1;
  }

  st_logic_program_config_t *prog = st_logic_get_program(logic_state, program_id);
  if (!prog || !prog->compiled) {
    debug_println("ERROR: Program not compiled. Upload source code first.");
    return -1;
  }

  if (!prog->enabled) {
    debug_println("ERROR: Program not enabled. Use 'set logic X enabled:true' first.");
    return -1;
  }

  st_debug_state_t *debug = &logic_state->debugger[program_id];
  st_debug_pause(debug);

  // Execute synchronously - pause will trigger after one instruction
  st_logic_execute_program(logic_state, program_id);

  debug_printf("[OK] Logic%d debug: PAUSED\n", program_id + 1);

  if (debug->snapshot_valid) {
    debug_printf("     PC: %u / %u\n", debug->snapshot.pc, prog->bytecode.instr_count);
  }
  debug_println("     Use 'show logic X debug' to inspect state.");
  debug_println("     Use 'set logic X debug step' to single-step.");
  return 0;
}

int cli_cmd_set_logic_debug_continue(st_logic_engine_state_t *logic_state, uint8_t program_id) {
  if (!logic_state) return -1;
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) {
    debug_printf("ERROR: Invalid program ID (0-%d)\n", ST_LOGIC_MAX_PROGRAMS - 1);
    return -1;
  }

  st_logic_program_config_t *prog = st_logic_get_program(logic_state, program_id);
  if (!prog || !prog->compiled) {
    debug_println("ERROR: Program not compiled.");
    return -1;
  }

  st_debug_state_t *debug = &logic_state->debugger[program_id];

  if (debug->mode == ST_DEBUG_OFF) {
    debug_println("ERROR: Debug mode is OFF. Use 'debug pause' first.");
    return -1;
  }

  // If program was halted, invalidate debug VM so it restarts from PC=0
  if (debug->snapshot_valid && debug->snapshot.halted) {
    g_shared_debug_vm.valid = false;  // Force restart from beginning
  }

  st_debug_continue(debug);

  // Execute synchronously - run until breakpoint or halt
  st_logic_execute_program(logic_state, program_id);

  debug_printf("[OK] Logic%d debug: ", program_id + 1);

  if (debug->snapshot_valid) {
    if (debug->pause_reason == ST_DEBUG_REASON_BREAKPOINT) {
      debug_printf("BREAKPOINT at PC=%u\n", debug->snapshot.pc);
    } else if (debug->snapshot.halted) {
      debug_println("HALTED");
    } else if (debug->snapshot.error) {
      debug_printf("ERROR: %s\n", debug->snapshot.error_msg);
    } else {
      debug_printf("PAUSED at PC=%u\n", debug->snapshot.pc);
    }
  } else {
    debug_println("CONTINUE");
  }

  return 0;
}

int cli_cmd_set_logic_debug_step(st_logic_engine_state_t *logic_state, uint8_t program_id) {
  if (!logic_state) return -1;
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) {
    debug_printf("ERROR: Invalid program ID (0-%d)\n", ST_LOGIC_MAX_PROGRAMS - 1);
    return -1;
  }

  st_logic_program_config_t *prog = st_logic_get_program(logic_state, program_id);
  if (!prog || !prog->compiled) {
    debug_println("ERROR: Program not compiled. Upload source code first.");
    return -1;
  }

  if (!prog->enabled) {
    debug_println("ERROR: Program not enabled. Use 'set logic X enabled:true' first.");
    return -1;
  }

  st_debug_state_t *debug = &logic_state->debugger[program_id];

  // Set step mode
  st_debug_step(debug);

  // Execute synchronously - run ONE instruction immediately instead of waiting for scheduler
  st_logic_execute_program(logic_state, program_id);

  // Show result immediately
  debug_printf("[OK] Logic%d debug: STEP executed\n", program_id + 1);

  if (debug->snapshot_valid) {
    debug_printf("     PC: %u / %u\n", debug->snapshot.pc, prog->bytecode.instr_count);
    if (debug->snapshot.halted) {
      debug_println("     Program HALTED.");
    } else if (debug->snapshot.error) {
      debug_printf("     ERROR: %s\n", debug->snapshot.error_msg);
    }
  }

  return 0;
}

int cli_cmd_set_logic_debug_breakpoint(st_logic_engine_state_t *logic_state, uint8_t program_id, uint16_t pc) {
  if (!logic_state) return -1;
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) {
    debug_printf("ERROR: Invalid program ID (0-%d)\n", ST_LOGIC_MAX_PROGRAMS - 1);
    return -1;
  }

  st_logic_program_config_t *prog = st_logic_get_program(logic_state, program_id);
  if (!prog || !prog->compiled) {
    debug_println("ERROR: Program not compiled. Upload source code first.");
    return -1;
  }

  if (pc >= prog->bytecode.instr_count) {
    debug_printf("ERROR: PC %u out of range (max %u)\n", pc, prog->bytecode.instr_count - 1);
    return -1;
  }

  st_debug_state_t *debug = &logic_state->debugger[program_id];

  if (!st_debug_add_breakpoint(debug, pc)) {
    debug_println("ERROR: Max breakpoints reached (8) or already exists");
    return -1;
  }

  debug_printf("[OK] Logic%d: Breakpoint added at PC=%u\n", program_id + 1, pc);
  return 0;
}

int cli_cmd_set_logic_debug_breakpoint_line(st_logic_engine_state_t *logic_state, uint8_t program_id, uint16_t line) {
  if (!logic_state) return -1;
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) {
    debug_printf("ERROR: Invalid program ID (0-%d)\n", ST_LOGIC_MAX_PROGRAMS - 1);
    return -1;
  }

  st_logic_program_config_t *prog = st_logic_get_program(logic_state, program_id);
  if (!prog || !prog->compiled) {
    debug_println("ERROR: Program not compiled. Upload source code first.");
    return -1;
  }

  // Check if line map is valid and matches this program
  if (!g_line_map.valid) {
    debug_println("ERROR: Line map not available. Re-compile the program first.");
    return -1;
  }

  if (g_line_map.program_id != program_id) {
    debug_printf("ERROR: Line map is for Logic%d, not Logic%d. Re-compile this program.\n",
                 g_line_map.program_id + 1, program_id + 1);
    return -1;
  }

  // Get PC for this line
  uint16_t pc = st_line_map_get_pc(line);
  if (pc == 0xFFFF) {
    debug_printf("ERROR: No code at line %u (valid range: 1-%u)\n", line, g_line_map.max_line);
    return -1;
  }

  if (pc >= prog->bytecode.instr_count) {
    debug_printf("ERROR: Line %u maps to invalid PC=%u\n", line, pc);
    return -1;
  }

  st_debug_state_t *debug = &logic_state->debugger[program_id];

  if (!st_debug_add_breakpoint(debug, pc)) {
    debug_println("ERROR: Max breakpoints reached (8) or already exists");
    return -1;
  }

  debug_printf("[OK] Logic%d: Breakpoint added at line %u (PC=%u)\n", program_id + 1, line, pc);
  return 0;
}

int cli_cmd_set_logic_debug_clear(st_logic_engine_state_t *logic_state, uint8_t program_id, int pc) {
  if (!logic_state) return -1;
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) {
    debug_printf("ERROR: Invalid program ID (0-%d)\n", ST_LOGIC_MAX_PROGRAMS - 1);
    return -1;
  }

  st_debug_state_t *debug = &logic_state->debugger[program_id];

  if (pc < 0) {
    // Clear all breakpoints
    st_debug_clear_breakpoints(debug);
    debug_printf("[OK] Logic%d: All breakpoints cleared\n", program_id + 1);
  } else {
    // Clear specific breakpoint
    if (!st_debug_remove_breakpoint(debug, (uint16_t)pc)) {
      debug_printf("ERROR: No breakpoint at PC=%d\n", pc);
      return -1;
    }
    debug_printf("[OK] Logic%d: Breakpoint at PC=%d removed\n", program_id + 1, pc);
  }

  return 0;
}

int cli_cmd_set_logic_debug_stop(st_logic_engine_state_t *logic_state, uint8_t program_id) {
  if (!logic_state) return -1;
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) {
    debug_printf("ERROR: Invalid program ID (0-%d)\n", ST_LOGIC_MAX_PROGRAMS - 1);
    return -1;
  }

  st_debug_state_t *debug = &logic_state->debugger[program_id];
  st_debug_stop(debug);

  debug_printf("[OK] Logic%d debug: STOPPED\n", program_id + 1);
  debug_println("     Normal execution resumed.");
  return 0;
}

int cli_cmd_show_logic_debug(st_logic_engine_state_t *logic_state, uint8_t program_id) {
  if (!logic_state) return -1;
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) {
    debug_printf("ERROR: Invalid program ID (0-%d)\n", ST_LOGIC_MAX_PROGRAMS - 1);
    return -1;
  }

  st_logic_program_config_t *prog = st_logic_get_program(logic_state, program_id);
  st_debug_state_t *debug = &logic_state->debugger[program_id];

  debug_printf("\n=== Logic%d Debug State ===\n", program_id + 1);
  st_debug_print_state(debug, prog);
  st_debug_print_breakpoints(debug);

  if (debug->snapshot_valid) {
    st_debug_print_instruction(debug, prog);
  }

  return 0;
}

int cli_cmd_show_logic_debug_vars(st_logic_engine_state_t *logic_state, uint8_t program_id) {
  if (!logic_state) return -1;
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) {
    debug_printf("ERROR: Invalid program ID (0-%d)\n", ST_LOGIC_MAX_PROGRAMS - 1);
    return -1;
  }

  st_logic_program_config_t *prog = st_logic_get_program(logic_state, program_id);
  st_debug_state_t *debug = &logic_state->debugger[program_id];

  debug_printf("\n=== Logic%d Variables ===", program_id + 1);
  st_debug_print_variables(debug, prog);

  return 0;
}

int cli_cmd_show_logic_debug_stack(st_logic_engine_state_t *logic_state, uint8_t program_id) {
  if (!logic_state) return -1;
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) {
    debug_printf("ERROR: Invalid program ID (0-%d)\n", ST_LOGIC_MAX_PROGRAMS - 1);
    return -1;
  }

  st_debug_state_t *debug = &logic_state->debugger[program_id];

  debug_printf("\n=== Logic%d Stack ===", program_id + 1);
  st_debug_print_stack(debug);

  return 0;
}

/* ============================================================================
 * FEAT-003: USER FUNCTION COMMANDS
 * ============================================================================ */

/**
 * @brief show logic <id> functions
 *
 * Display registered user-defined functions for a program.
 * Shows function name, parameters, return type, bytecode address,
 * and FUNCTION_BLOCK instance info.
 */
int cli_cmd_show_logic_functions(st_logic_engine_state_t *logic_state, uint8_t program_id) {
  if (!logic_state) return -1;
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) {
    debug_printf("ERROR: Invalid program ID (0-%d)\n", ST_LOGIC_MAX_PROGRAMS - 1);
    return -1;
  }

  st_logic_program_config_t *prog = &logic_state->programs[program_id];

  debug_printf("\n======== Logic%d User Functions ========\n\n", program_id + 1);

  if (!prog->compiled || prog->bytecode.instr_count == 0) {
    debug_printf("Program not compiled or empty.\n\n");
    return 0;
  }

  st_function_registry_t *reg = prog->bytecode.func_registry;
  if (!reg || reg->user_count == 0) {
    debug_printf("No user-defined functions.\n\n");
    return 0;
  }

  // Type name helper
  static const char *type_names[] = {"BOOL", "INT", "DINT", "WORD", "DWORD", "REAL", "TIME", "STRING", "???"};

  debug_printf("User Functions: %d\n", reg->user_count);
  if (reg->fb_instance_count > 0) {
    debug_printf("FB Instances:   %d / %d\n", reg->fb_instance_count, ST_MAX_FB_INSTANCES);
  }
  debug_printf("\n");

  // List user functions (start after builtins)
  for (uint8_t i = reg->builtin_count; i < reg->builtin_count + reg->user_count; i++) {
    st_function_entry_t *func = &reg->functions[i];

    // Function type indicator
    const char *func_type = func->is_function_block ? "FUNCTION_BLOCK" : "FUNCTION";

    // Return type
    uint8_t rt_idx = (uint8_t)func->return_type;
    if (rt_idx > 7) rt_idx = 8;
    const char *ret_type = type_names[rt_idx];

    debug_printf("  [%d] %s %s", i, func_type, func->name);
    if (!func->is_function_block) {
      debug_printf(" : %s", ret_type);
    }
    debug_printf("\n");

    // Parameters
    if (func->param_count > 0) {
      debug_printf("      Params: %d (", func->param_count);
      for (uint8_t p = 0; p < func->param_count; p++) {
        if (p > 0) debug_printf(", ");
        uint8_t pt_idx = (uint8_t)func->param_types[p];
        if (pt_idx > 7) pt_idx = 8;
        debug_printf("%s", type_names[pt_idx]);
      }
      debug_printf(")\n");
    }

    // Bytecode info
    debug_printf("      Bytecode: addr=%u, size=%u instructions\n",
                 func->bytecode_addr, func->bytecode_size);

    // FB instance info
    if (func->is_function_block && func->instance_size > 0) {
      debug_printf("      Instance size: %d locals\n", func->instance_size);

      // Show allocated instances for this function
      for (uint8_t inst = 0; inst < reg->fb_instance_count; inst++) {
        if (reg->fb_instances[inst].func_index == i) {
          debug_printf("        inst[%d]: %s, %d vars\n",
                       inst,
                       reg->fb_instances[inst].initialized ? "initialized" : "not initialized",
                       reg->fb_instances[inst].local_count);
        }
      }
    }

    debug_printf("\n");
  }

  debug_printf("========================================\n\n");
  return 0;
}
