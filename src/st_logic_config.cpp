/**
 * @file st_logic_config.cpp
 * @brief Structured Text Logic Configuration Implementation
 */

#include "st_logic_config.h"
#include "st_parser.h"
#include "st_compiler.h"
#include "st_debug.h"  // FEAT-008: Reset debug state on delete/compile
#include "register_allocator.h"
#include "ir_pool_manager.h"  // v5.1.0 - IR pool management
#include "debug.h"
#include "debug_flags.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <FS.h>
#include <SPIFFS.h>

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================ */

// Global logic engine state - single instance for all 4 programs
static st_logic_engine_state_t g_logic_state;

// Global parser and compiler to avoid stack overflow
// (These are large structures, don't allocate on stack)
// Dynamic allocation: parser + compiler are only needed during compile (~12 KB saved)
static st_parser_t *g_parser = NULL;
static st_compiler_t *g_compiler = NULL;

/**
 * @brief Get pointer to global logic engine state
 */
st_logic_engine_state_t *st_logic_get_state(void) {
  return &g_logic_state;
}

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

void st_logic_init(st_logic_engine_state_t *state) {
  memset(state, 0, sizeof(*state));
  state->enabled = 1;
  state->execution_interval_ms = 10;  // Run every 10ms by default

  // Initialize each program
  for (int i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
    st_logic_program_config_t *prog = &state->programs[i];
    memset(prog, 0, sizeof(*prog));
    snprintf(prog->name, sizeof(prog->name), "Logic%d", i + 1);
    prog->enabled = 0;  // Disabled by default
    prog->compiled = 0;
    prog->source_offset = 0xFFFFFFFF;  // Not allocated in pool
    prog->source_size = 0;
    prog->ir_pool_offset = 65535;  // v5.1.0 - IR pool not allocated
    prog->ir_pool_size = 0;
  }

  // v5.1.0 - Initialize IR pool manager
  ir_pool_init(state);

  // FEAT-008: Initialize debug states for each program
  for (int i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
    st_debug_init(&state->debugger[i]);
  }
}

/* ============================================================================
 * POOL MANAGEMENT (Dynamic Allocation, v4.7.1)
 * ============================================================================ */

/**
 * @brief Get pointer to source code from pool
 */
const char* st_logic_get_source_code(st_logic_engine_state_t *state, uint8_t program_id) {
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) return NULL;

  st_logic_program_config_t *prog = &state->programs[program_id];
  if (prog->source_offset == 0xFFFFFFFF || prog->source_size == 0) {
    return NULL;  // Not allocated
  }

  return &state->source_pool[prog->source_offset];
}

/**
 * @brief Calculate pool usage statistics
 */
void st_logic_get_pool_stats(st_logic_engine_state_t *state,
                              uint32_t *used_bytes, uint32_t *free_bytes, uint32_t *largest_free) {
  uint32_t total_used = 0;

  // Calculate used bytes
  for (uint8_t i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
    st_logic_program_config_t *prog = &state->programs[i];
    if (prog->source_offset != 0xFFFFFFFF) {
      total_used += prog->source_size;
    }
  }

  if (used_bytes) *used_bytes = total_used;
  if (free_bytes) *free_bytes = ST_LOGIC_POOL_SIZE - total_used;
  if (largest_free) *largest_free = ST_LOGIC_POOL_SIZE - total_used; // Simplified: assumes contiguous free space
}

/**
 * @brief Free program's pool allocation
 */
static void st_logic_pool_free(st_logic_engine_state_t *state, uint8_t program_id) {
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) return;

  st_logic_program_config_t *prog = &state->programs[program_id];
  if (prog->source_offset == 0xFFFFFFFF) return;  // Not allocated

  // Compact pool: move all programs after this one down
  uint32_t free_offset = prog->source_offset;
  uint32_t free_size = prog->source_size;

  // Move data down
  for (uint8_t i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
    st_logic_program_config_t *other = &state->programs[i];
    if (other->source_offset > free_offset && other->source_offset != 0xFFFFFFFF) {
      // Move this program's source code down
      memmove(&state->source_pool[other->source_offset - free_size],
              &state->source_pool[other->source_offset],
              other->source_size);
      other->source_offset -= free_size;
    }
  }

  // Mark as freed
  prog->source_offset = 0xFFFFFFFF;
  prog->source_size = 0;
}

/**
 * @brief Allocate space in pool for program
 * @return true if successful, false if pool full
 */
static bool st_logic_pool_allocate(st_logic_engine_state_t *state, uint8_t program_id, uint32_t size) {
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) return false;

  // Free existing allocation if any
  st_logic_pool_free(state, program_id);

  // Calculate used space
  uint32_t pool_used = 0;
  for (uint8_t i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
    st_logic_program_config_t *prog = &state->programs[i];
    if (prog->source_offset != 0xFFFFFFFF) {
      uint32_t end = prog->source_offset + prog->source_size;
      if (end > pool_used) {
        pool_used = end;
      }
    }
  }

  // Check if we have space
  if (pool_used + size > ST_LOGIC_POOL_SIZE) {
    return false;  // Pool full
  }

  // Allocate at end of used space
  st_logic_program_config_t *prog = &state->programs[program_id];
  prog->source_offset = pool_used;
  prog->source_size = size;

  return true;
}

/* ============================================================================
 * PROGRAM UPLOAD & COMPILATION
 * ============================================================================ */

bool st_logic_upload(st_logic_engine_state_t *state, uint8_t program_id,
                      const char *source, uint32_t source_size) {
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) return false;

  st_logic_program_config_t *prog = &state->programs[program_id];

  // Validate source code
  if (!source) {
    snprintf(prog->last_error, sizeof(prog->last_error), "Source code pointer is NULL");
    return false;
  }

  if (source_size == 0) {
    snprintf(prog->last_error, sizeof(prog->last_error), "Source code is empty (0 bytes)");
    return false;
  }

  if (source_size > ST_LOGIC_POOL_SIZE) {
    snprintf(prog->last_error, sizeof(prog->last_error),
             "Source code too large: %u bytes (max %u bytes pool). Remove comments to reduce size.",
             (unsigned int)source_size, ST_LOGIC_POOL_SIZE);
    return false;
  }

  // Try to allocate space in pool
  if (!st_logic_pool_allocate(state, program_id, source_size)) {
    // Calculate available space
    uint32_t used, free, largest;
    st_logic_get_pool_stats(state, &used, &free, &largest);

    snprintf(prog->last_error, sizeof(prog->last_error),
             "Pool full: need %u bytes, only %u free (used: %u/%u)",
             (unsigned int)source_size, (unsigned int)free, (unsigned int)used, ST_LOGIC_POOL_SIZE);
    return false;
  }

  // Copy source code to pool
  memcpy(&state->source_pool[prog->source_offset], source, source_size);
  prog->compiled = 0;  // Mark as needing compilation

  return true;
}

bool st_logic_compile(st_logic_engine_state_t *state, uint8_t program_id) {
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) return false;

  // FEAT-008: Reset debug state before recompiling (old snapshot is now invalid)
  st_debug_state_t *debug = &state->debugger[program_id];
  st_debug_stop(debug);
  st_debug_init(debug);

  st_logic_program_config_t *prog = &state->programs[program_id];

  // FEAT-003: Free old function registry if recompiling
  if (prog->bytecode.func_registry) {
    free(prog->bytecode.func_registry);
    prog->bytecode.func_registry = NULL;
  }

  // Get source code from pool
  const char *source_raw = st_logic_get_source_code(state, program_id);
  if (!source_raw || prog->source_size == 0) {
    snprintf(prog->last_error, sizeof(prog->last_error), "No source code uploaded");
    return false;
  }

  // BUG-212: Create null-terminated copy of source code
  // Source pool entries are NOT null-terminated (BUG-202 variant).
  // The lexer requires '\0' to detect EOF. Without it, the parser reads
  // past the source into adjacent pool data, causing parse corruption.
  char *source_code = (char *)malloc(prog->source_size + 1);
  if (!source_code) {
    snprintf(prog->last_error, sizeof(prog->last_error), "Insufficient heap for source copy");
    return false;
  }
  memcpy(source_code, source_raw, prog->source_size);
  source_code[prog->source_size] = '\0';

  // Dynamically allocate parser and compiler (~12 KB, only needed during compile)
  g_parser = (st_parser_t *)malloc(sizeof(st_parser_t));
  if (!g_parser) {
    snprintf(prog->last_error, sizeof(prog->last_error), "Insufficient heap for parser");
    free(source_code);
    return false;
  }

  st_parser_init(g_parser, source_code);
  st_program_t *program = st_parser_parse_program(g_parser);

  if (!program) {
    snprintf(prog->last_error, sizeof(prog->last_error), "Parse error: %s", g_parser->error_msg);
    free(g_parser);
    g_parser = NULL;
    free(source_code);
    return false;
  }

  g_compiler = (st_compiler_t *)malloc(sizeof(st_compiler_t));
  if (!g_compiler) {
    snprintf(prog->last_error, sizeof(prog->last_error), "Insufficient heap for compiler");
    st_program_free(program);
    free(g_parser);
    g_parser = NULL;
    free(source_code);
    return false;
  }

  st_compiler_init(g_compiler);
  st_bytecode_program_t *bytecode = st_compiler_compile(g_compiler, program);

  if (!bytecode) {
    snprintf(prog->last_error, sizeof(prog->last_error), "Compile error: %s", g_compiler->error_msg);
    st_program_free(program);
    free(g_compiler);
    g_compiler = NULL;
    free(g_parser);
    g_parser = NULL;
    free(source_code);
    return false;
  }

  // Store compiled bytecode in config
  memcpy(&prog->bytecode, bytecode, sizeof(*bytecode));
  prog->compiled = 1;
  prog->execution_count = 0;
  prog->error_count = 0;

  // FEAT-008: Set program_id in line map for source-level breakpoints
  g_line_map.program_id = program_id;

  // v5.1.0 - Allocate IR pool for EXPORT variables
  // Free old allocation if recompiling
  if (prog->ir_pool_offset != 65535) {
    ir_pool_free(state, program_id);
  }

  // Calculate required IR pool size
  uint8_t ir_size_needed = ir_pool_calculate_size(&prog->bytecode);
  if (ir_size_needed > 0) {
    uint8_t ir_offset = ir_pool_allocate(state, program_id, ir_size_needed);
    if (ir_offset == 255) {
      // Pool exhausted - compilation succeeded but IR export disabled
      snprintf(prog->last_error, sizeof(prog->last_error),
               "Warning: IR pool exhausted (%d regs needed, %d free). EXPORT disabled.",
               ir_size_needed, ir_pool_get_free_space(state));
      debug_printf("[WARN] Logic%d: IR pool exhausted, EXPORT disabled\n", program_id + 1);
      prog->ir_pool_offset = 65535;  // No allocation
      prog->ir_pool_size = 0;
    }
  } else {
    // No exported variables
    prog->ir_pool_offset = 65535;
    prog->ir_pool_size = 0;
  }

  st_program_free(program);
  free(bytecode);

  // Free dynamically allocated parser and compiler
  free(g_compiler);
  g_compiler = NULL;
  free(g_parser);
  g_parser = NULL;
  free(source_code);  // BUG-212: Free null-terminated copy

  return true;
}

/* ============================================================================
 * VARIABLE BINDING (DEPRECATED)
 *
 * NOTE: Variable binding is now handled by the unified VariableMapping system.
 * Instead of using st_logic_bind_variable(), create a VariableMapping entry:
 *
 * Example:
 *   VariableMapping map = {
 *     .source_type = MAPPING_SOURCE_ST_VAR,
 *     .st_program_id = 0,
 *     .st_var_index = 2,
 *     .is_input = 0,  // OUTPUT mode
 *     .coil_reg = 100  // Write to HR#100
 *   };
 *   g_persist_config.var_maps[g_persist_config.var_map_count++] = map;
 *   config_save();
 *
 * This eliminates code duplication and provides a single place to manage
 * all variable bindings (GPIO + ST).
 * ============================================================================ */

/* ============================================================================
 * PROGRAM MANAGEMENT
 * ============================================================================ */

bool st_logic_set_enabled(st_logic_engine_state_t *state, uint8_t program_id, uint8_t enabled) {
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) return false;

  st_logic_program_config_t *prog = &state->programs[program_id];
  prog->enabled = (enabled != 0);

  return true;
}

bool st_logic_delete(st_logic_engine_state_t *state, uint8_t program_id) {
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) return false;

  // FEAT-008: Reset debug state before deleting program
  st_debug_state_t *debug = &state->debugger[program_id];
  st_debug_stop(debug);
  st_debug_init(debug);

  // Free pool allocations
  st_logic_pool_free(state, program_id);
  ir_pool_free(state, program_id);  // v5.1.0 - Free IR pool

  // FEAT-003: Free function registry before clearing program
  st_logic_program_config_t *prog = &state->programs[program_id];
  if (prog->bytecode.func_registry) {
    free(prog->bytecode.func_registry);
    prog->bytecode.func_registry = NULL;
  }

  // Clear the program itself
  memset(prog, 0, sizeof(*prog));
  snprintf(prog->name, sizeof(prog->name), "Logic%d", program_id + 1);
  prog->enabled = 0;
  prog->compiled = 0;
  prog->source_offset = 0xFFFFFFFF;  // Not allocated
  prog->source_size = 0;
  prog->ir_pool_offset = 65535;  // v5.1.0 - IR pool not allocated
  prog->ir_pool_size = 0;

  // Clear all variable bindings for this program
  extern PersistConfig g_persist_config;
  uint8_t i = 0;
  while (i < g_persist_config.var_map_count) {
    if (g_persist_config.var_maps[i].st_program_id == program_id) {
      // BUG-047 FIX: Free allocated registers before removing binding
      VariableMapping *map = &g_persist_config.var_maps[i];

      // BUG-109 FIX: Free multi-register bindings (DINT/REAL use word_count=2)
      uint8_t word_count = (map->word_count > 0) ? map->word_count : 1;

      // Free input register(s) if allocated (HR type)
      if (map->is_input && map->input_type == 0 && map->input_reg < ALLOCATOR_SIZE) {
        for (uint8_t w = 0; w < word_count; w++) {
          if (map->input_reg + w < ALLOCATOR_SIZE) {
            register_allocator_free(map->input_reg + w);
          }
        }
      }

      // Free output register/coil if allocated
      if (!map->is_input) {
        if (map->output_type == 0 && map->coil_reg < ALLOCATOR_SIZE) {
          // Holding register - free all words
          for (uint8_t w = 0; w < word_count; w++) {
            if (map->coil_reg + w < ALLOCATOR_SIZE) {
              register_allocator_free(map->coil_reg + w);
            }
          }
        }
        // Note: Coils don't use register allocator (only HR 0-299 tracked)
      }

      // Remove this binding by shifting all subsequent bindings down
      for (uint8_t j = i; j < g_persist_config.var_map_count - 1; j++) {
        g_persist_config.var_maps[j] = g_persist_config.var_maps[j + 1];
      }
      g_persist_config.var_map_count--;
      // Don't increment i - check this position again
    } else {
      i++;
    }
  }

  return true;
}

st_logic_program_config_t *st_logic_get_program(st_logic_engine_state_t *state, uint8_t program_id) {
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) return NULL;
  return &state->programs[program_id];
}

/**
 * @brief Update binding_count cache for all programs (BUG-005 fix)
 *
 * Counts variable bindings from g_persist_config.var_maps and updates
 * each program's cached binding_count field for performance optimization.
 * This avoids O(n*m) nested loop in registers_update_st_logic_status().
 */
void st_logic_update_binding_counts(st_logic_engine_state_t *state) {
  extern PersistConfig g_persist_config;

  // Reset all binding counts
  for (uint8_t prog_id = 0; prog_id < ST_LOGIC_MAX_PROGRAMS; prog_id++) {
    state->programs[prog_id].binding_count = 0;
  }

  // Count bindings for each program
  for (uint8_t i = 0; i < g_persist_config.var_map_count; i++) {
    const VariableMapping *map = &g_persist_config.var_maps[i];
    if (map->source_type == MAPPING_SOURCE_ST_VAR && map->st_program_id < ST_LOGIC_MAX_PROGRAMS) {
      state->programs[map->st_program_id].binding_count++;
    }
  }
}

/**
 * @brief Reset performance statistics for a program (v4.1.0)
 * @param state Logic engine state
 * @param program_id Program ID (0-3), or 0xFF for all programs
 */
void st_logic_reset_stats(st_logic_engine_state_t *state, uint8_t program_id) {
  if (program_id == 0xFF) {
    // Reset all programs
    for (uint8_t i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
      st_logic_program_config_t *prog = &state->programs[i];
      prog->min_execution_us = 0;
      prog->max_execution_us = 0;
      prog->total_execution_us = 0;
      prog->overrun_count = 0;
      prog->execution_count = 0;
      prog->error_count = 0;
    }
  } else if (program_id < ST_LOGIC_MAX_PROGRAMS) {
    // Reset single program
    st_logic_program_config_t *prog = &state->programs[program_id];
    prog->min_execution_us = 0;
    prog->max_execution_us = 0;
    prog->total_execution_us = 0;
    prog->overrun_count = 0;
    prog->execution_count = 0;
    prog->error_count = 0;
  }
}

/**
 * @brief Reset global cycle statistics (v4.1.0)
 * @param state Logic engine state
 */
void st_logic_reset_cycle_stats(st_logic_engine_state_t *state) {
  state->cycle_min_ms = 0;
  state->cycle_max_ms = 0;
  state->cycle_overrun_count = 0;
  state->total_cycles = 0;
}

/* ============================================================================
 * PERSISTENCE (SPIFFS STORAGE)
 * ============================================================================ */

/**
 * @brief Save ST Logic programs to SPIFFS (unlimited size)
 * @return true if successful
 */
bool st_logic_save_to_nvs(void) {
  st_logic_engine_state_t *state = st_logic_get_state();
  DebugFlags* dbg = debug_flags_get();

  // Mount SPIFFS if not already mounted
  if (!SPIFFS.begin(true)) {
    if (dbg->config_save) {
      debug_println("ST_LOGIC SAVE: SPIFFS mount failed");
    }
    return false;
  }

  if (dbg->config_save) {
    debug_println("ST_LOGIC SAVE: Saving programs to SPIFFS");
  }

  // Save each program
  uint8_t saved_count = 0;
  for (uint8_t i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
    st_logic_program_config_t *prog = &state->programs[i];

    // Delete file if program is empty
    char filename[32];
    snprintf(filename, sizeof(filename), "/logic_%d.dat", i);

    if (prog->source_size == 0) {
      if (SPIFFS.exists(filename)) {
        SPIFFS.remove(filename);
        if (dbg->config_save) {
          debug_print("  Program ");
          debug_print_uint(i);
          debug_println(": deleted (empty)");
        }
      }
      continue;
    }

    // Open file for writing
    File file = SPIFFS.open(filename, FILE_WRITE);
    if (!file) {
      if (dbg->config_save) {
        debug_print("  Program ");
        debug_print_uint(i);
        debug_println(": FAILED to open file");
      }
      continue;
    }

    // Write: enabled flag (1 byte) + source size (4 bytes) + source code
    file.write(prog->enabled);
    file.write((uint8_t*)&prog->source_size, sizeof(uint32_t));

    // Get source code from pool and write
    const char *source_code = st_logic_get_source_code(state, i);
    if (source_code && prog->source_size > 0 && prog->source_size <= ST_LOGIC_POOL_SIZE) {
      file.write((uint8_t*)source_code, prog->source_size);
    }
    file.close();

    if (dbg->config_save) {
      debug_print("  Program ");
      debug_print_uint(i);
      debug_print(": saved ");
      debug_print_uint(prog->source_size);
      debug_print(" bytes (enabled=");
      debug_print_uint(prog->enabled);
      debug_println(")");
    }
    saved_count++;
  }

  if (dbg->config_save) {
    debug_print("ST_LOGIC SAVE: Saved ");
    debug_print_uint(saved_count);
    debug_println(" programs to SPIFFS");
  }

  return true;
}

/**
 * @brief Load ST Logic programs from SPIFFS
 * @return true if successful
 */
bool st_logic_load_from_nvs(void) {
  st_logic_engine_state_t *state = st_logic_get_state();
  DebugFlags* dbg = debug_flags_get();

  // Mount SPIFFS if not already mounted
  if (!SPIFFS.begin(true)) {
    if (dbg->config_load) {
      debug_println("ST_LOGIC LOAD: SPIFFS mount failed");
    }
    return false;
  }

  if (dbg->config_load) {
    debug_println("ST_LOGIC LOAD: Loading programs from SPIFFS");
  }

  // Load each program
  uint8_t loaded_count = 0;
  for (uint8_t i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
    st_logic_program_config_t *prog = &state->programs[i];

    char filename[32];
    snprintf(filename, sizeof(filename), "/logic_%d.dat", i);

    // Check if file exists
    if (!SPIFFS.exists(filename)) {
      // No file for this program slot - OK (empty slot)
      continue;
    }

    // Open file for reading
    File file = SPIFFS.open(filename, FILE_READ);
    if (!file) {
      if (dbg->config_load) {
        debug_print("  Program ");
        debug_print_uint(i);
        debug_println(": FAILED to open file");
      }
      continue;
    }

    // Read: enabled flag (1 byte) + source size (4 bytes) + source code
    if (file.available() < 5) {
      if (dbg->config_load) {
        debug_print("  Program ");
        debug_print_uint(i);
        debug_println(": file too small");
      }
      file.close();
      continue;
    }

    prog->enabled = file.read();
    file.read((uint8_t*)&prog->source_size, sizeof(uint32_t));

    if (prog->source_size > 0 && prog->source_size <= ST_LOGIC_POOL_SIZE) {
      // Allocate space in pool
      if (!st_logic_pool_allocate(state, i, prog->source_size)) {
        if (dbg->config_load) {
          debug_print("  Program ");
          debug_print_uint(i);
          debug_println(": FAILED to allocate pool space");
        }
        file.close();
        continue;
      }

      // Read source code from file into pool
      file.read((uint8_t*)&state->source_pool[prog->source_offset], prog->source_size);
      prog->compiled = 0;  // Mark as needing recompilation
      file.close();

      if (dbg->config_load) {
        debug_print("  Program ");
        debug_print_uint(i);
        debug_print(": loaded ");
        debug_print_uint(prog->source_size);
        debug_print(" bytes, enabled=");
        debug_print_uint(prog->enabled);
        debug_print(", compiling...");
      }

      // Immediately compile the program
      if (st_logic_compile(state, i)) {
        if (dbg->config_load) {
          debug_println(" OK");
        }
        loaded_count++;
      } else {
        if (dbg->config_load) {
          debug_print(" FAILED: ");
          debug_println(prog->last_error);
        }
      }
    } else {
      if (dbg->config_load) {
        debug_print("  Program ");
        debug_print_uint(i);
        debug_print(": invalid size ");
        debug_print_uint(prog->source_size);
        debug_println("");
      }
      file.close();
    }
  }

  if (dbg->config_load) {
    debug_print("ST_LOGIC LOAD: Loaded ");
    debug_print_uint(loaded_count);
    debug_println(" programs from SPIFFS");
  }

  // BUG-005 FIX: Update binding count cache after loading programs
  st_logic_update_binding_counts(state);

  return true;
}

// Legacy compatibility functions - redirect to SPIFFS storage
bool st_logic_save_to_persist_config(PersistConfig *config) {
  (void)config;  // Unused - ST Logic now saves to SPIFFS filesystem
  return st_logic_save_to_nvs();  // Function name kept for compatibility
}

bool st_logic_load_from_persist_config(const PersistConfig *config) {
  (void)config;  // Unused - ST Logic now loads from SPIFFS filesystem
  return st_logic_load_from_nvs();  // Function name kept for compatibility
}
