/**
 * @file st_logic_config.h
 * @brief Structured Text Logic Mode Configuration
 *
 * Configuration for logic programs and Modbus register bindings.
 * Supports 4 independent logic programs with register I/O.
 */

#ifndef ST_LOGIC_CONFIG_H
#define ST_LOGIC_CONFIG_H

#include <stdint.h>
#include <stddef.h>  // FEAT-010: size_t (st_logic_set_program_priority's error_out_size)
#include "st_types.h"
#include "constants.h"
#include "config_struct.h"
#include "st_debug.h"  // FEAT-008: Debugger support

/* ============================================================================
 * LOGIC PROGRAM CONFIGURATION
 *
 * NOTE: Variable bindings are now handled by unified VariableMapping system
 * in gpio_mapping.cpp. No longer duplicated here.
 *
 * DYNAMIC POOL ALLOCATION (v4.7.1, expanded v7.9.7.6):
 * Source code is stored in a global pool shared between all 4 programs.
 * Each program stores offset + size instead of fixed array.
 * This allows flexible allocation (1×pool, 2×half, 4×quarter, or any mix).
 *
 * Pool size:
 *   - WROVER (BOARD_HAS_PSRAM): 64 KB allokeret i PSRAM ved boot
 *   - WROOM: 8 KB statisk i DRAM (legacy/fallback)
 * Pool pegepind (source_pool) er dynamisk allokeret i st_logic_init() —
 * heap_caps_malloc(MALLOC_CAP_SPIRAM) hvis PSRAM tilgængelig, ellers heap.
 * ============================================================================ */

#ifdef BOARD_HAS_PSRAM
  #define ST_LOGIC_POOL_SIZE 65536  // 64 KB i PSRAM (WROVER) — 8× WROOM
#else
  #define ST_LOGIC_POOL_SIZE 8000   // 8 KB i DRAM (WROOM fallback)
#endif

typedef struct {
  // Program identification
  char name[32];              // "Logic1", "Logic2", etc.
  uint8_t enabled;            // Is this program enabled?

  // Source code storage (dynamic pool allocation)
  uint32_t source_offset;     // Offset in global pool (0xFFFFFFFF if not allocated)
  uint32_t source_size;       // Actual source code size

  // Compiled bytecode
  st_bytecode_program_t bytecode; // Compiled and ready to execute
  uint8_t compiled;           // Is bytecode valid?

  // Execution statistics
  // BUG-006 FIX: Changed to uint16_t to match register size (65535 max, saves 8 bytes RAM)
  uint16_t execution_count;   // Number of times executed (wraps at 65535)
  uint16_t error_count;       // Number of execution errors (wraps at 65535)
  uint32_t last_execution_us; // Last execution time (microseconds)
  char last_error[128];       // Last error message (127 chars max) — BUG-382: was 64, too small once
                              // double-wrapped ("Parse error: " + "Parse error at line N: " + message)
                              // left only ~20-25 usable chars; not NVS-persisted (runtime-only field)

  // BUG-005 FIX: Cache variable binding count (performance optimization)
  uint8_t binding_count;      // Number of variable bindings for this program

  // Performance monitoring (v4.1.0)
  uint32_t min_execution_us;  // Minimum execution time (microseconds)
  uint32_t max_execution_us;  // Maximum execution time (microseconds)
  uint32_t total_execution_us;// Total execution time for average calculation (microseconds)
  uint32_t overrun_count;     // Number of times execution > target interval

  // IR Pool allocation (v5.1.0 - dynamic export to IR 220-251)
  uint16_t ir_pool_offset;    // Start offset in IR 220-251 (65535 if not allocated)
  uint8_t ir_pool_size;       // Number of registers allocated (0-32)

  // FEAT-010: Per-program priority + execution interval (v7.9.14.0).
  // Replaces the single shared execution_interval_ms as the effective
  // interval for NORMAL programs; HIGH programs are scheduled by a
  // separate dedicated Core-0 task (see st_logic_config.cpp). Persisted
  // in the program's own SPIFFS .dat file header, NOT in PersistConfig —
  // see ST_LOGIC_DAT_MAGIC's doc comment for why.
  uint8_t priority;           // ST_LOGIC_PRIORITY_NORMAL or ST_LOGIC_PRIORITY_HIGH
  uint16_t interval_ms;       // 2-60000ms
  uint32_t last_run_time;     // millis() of this program's last execution (per-program due-time scheduling)

} st_logic_program_config_t;

/* ============================================================================
 * FEAT-007: GLOBAL_VAR — inter-program shared variable (v7.9.12.0)
 *
 * Scalar-only (BOOL/INT/DINT/DWORD/REAL/TIME) — no STRING/ARRAY. See
 * constants.h's ST_MAX_GLOBAL_VARS comment for the full design rationale.
 * ============================================================================ */

typedef struct {
  char name[32];
  st_datatype_t type;
  st_value_t value;
} st_global_var_t;

/* ============================================================================
 * GLOBAL LOGIC ENGINE STATE
 * ============================================================================ */

typedef struct {
  // 4 independent logic programs
  st_logic_program_config_t programs[ST_LOGIC_MAX_PROGRAMS];

  // FEAT-007: variables shared between Logic1-4, declared once in a
  // dedicated "GLOBAL_VAR ... END_VAR" source block (separate from any
  // single program's own source). Values reset to zero on every
  // (re)compile of this block; only the declaration source text persists
  // across reboot (st_logic_save_to_nvs/_load_from_nvs).
  st_global_var_t globals[ST_MAX_GLOBAL_VARS];
  uint8_t global_count;
  char global_source[ST_GLOBAL_SOURCE_MAX];  // Null-terminated; fixed buffer, no pool needed (small)
  uint32_t global_source_size;
  char global_last_error[64];

  // Global source code pool (v7.9.7.6: dynamisk PSRAM/heap allokering).
  // Peger på buffer allokeret i st_logic_init() — PSRAM foretrækkes.
  // NULL indtil init kører. Alle pool-operationer skal tjekke source_pool != NULL.
  char *source_pool;

  // Global settings
  uint8_t enabled;            // Logic mode enabled/disabled globally
  uint8_t debug;              // Debug output enabled (bytecode, execution trace, etc.)
  uint32_t execution_interval_ms; // How often to run programs (10ms default)
  uint32_t last_run_time;     // Timestamp of last execution

  // Global cycle statistics (v4.1.0)
  uint32_t cycle_min_ms;      // Minimum total cycle time (all programs)
  uint32_t cycle_max_ms;      // Maximum total cycle time
  uint32_t cycle_overrun_count; // Number of cycles where time > interval
  uint32_t total_cycles;      // Total number of cycles executed

  // FEAT-008: Per-program debugger state
  st_debug_state_t debugger[ST_LOGIC_MAX_PROGRAMS];  // Debugger state for each program

} st_logic_engine_state_t;

/* ============================================================================
 * FUNCTIONS
 * ============================================================================ */

/**
 * @brief Initialize logic engine state
 * @param state Logic engine state
 */
void st_logic_init(st_logic_engine_state_t *state);

/**
 * @brief Upload ST source code for a program (dynamic pool allocation)
 * @param state Logic engine state
 * @param program_id Program ID (0-3)
 * @param source ST source code
 * @param source_size Size of source code
 * @return true if successful (false if pool full)
 */
bool st_logic_upload(st_logic_engine_state_t *state, uint8_t program_id,
                      const char *source, uint32_t source_size);

/**
 * @brief Get pointer to source code from pool
 * @param state Logic engine state
 * @param program_id Program ID (0-3)
 * @return Pointer to source code (NULL if not allocated)
 */
const char* st_logic_get_source_code(st_logic_engine_state_t *state, uint8_t program_id);

/**
 * @brief Get pool usage statistics
 * @param state Logic engine state
 * @param used_bytes Output: bytes used in pool
 * @param free_bytes Output: bytes free in pool
 * @param largest_free Output: largest contiguous free block
 */
void st_logic_get_pool_stats(st_logic_engine_state_t *state,
                              uint32_t *used_bytes, uint32_t *free_bytes, uint32_t *largest_free);

/**
 * @brief Compile and prepare logic program for execution
 * @param state Logic engine state
 * @param program_id Program ID (0-3)
 * @return true if successful
 */
bool st_logic_compile(st_logic_engine_state_t *state, uint8_t program_id);

/**
 * @brief Set variable binding (ST variable ↔ Modbus register)
 *
 * DEPRECATED: Use the unified VariableMapping system in gpio_mapping.cpp instead.
 * To bind a variable:
 *   1. Create a VariableMapping entry in g_persist_config.var_maps
 *   2. Set source_type = MAPPING_SOURCE_ST_VAR
 *   3. Set st_program_id and st_var_index
 *   4. Set is_input/output_reg or is_output fields
 *   5. Call config_save() to persist
 *
 * The mapping engine will handle all I/O automatically.
 */
// FUNCTION REMOVED - use VariableMapping system instead

/**
 * @brief Compile using chunked multi-pass pipeline (reduced peak heap)
 *
 * Uses small AST pool (~4.5 KB) per chunk instead of full pool (23-82 KB).
 * Falls back to st_logic_compile() if no user functions in source.
 *
 * @param state Logic engine state
 * @param program_id Program ID (0-3)
 * @return true if successful
 */
bool st_logic_compile_chunked(st_logic_engine_state_t *state, uint8_t program_id);

/**
 * @brief Enable/disable a logic program
 * @param state Logic engine state
 * @param program_id Program ID (0-3)
 * @param enabled true to enable, false to disable
 * @return true if successful
 */
bool st_logic_set_enabled(st_logic_engine_state_t *state, uint8_t program_id, uint8_t enabled);

/**
 * @brief Cold restart: reset variables to compiled initial values
 * @param state Logic engine state
 * @param program_id Program ID (0-3)
 * @return true if successful (false if not compiled)
 */
bool st_logic_reinit(st_logic_engine_state_t *state, uint8_t program_id);

/**
 * @brief Delete/clear a logic program
 * @param state Logic engine state
 * @param program_id Program ID (0-3)
 * @return true if successful
 */
bool st_logic_delete(st_logic_engine_state_t *state, uint8_t program_id);

/**
 * @brief Get program info
 * @param state Logic engine state
 * @param program_id Program ID (0-3)
 * @return Program configuration (NULL if invalid ID)
 */
st_logic_program_config_t *st_logic_get_program(st_logic_engine_state_t *state, uint8_t program_id);

/**
 * @brief Get pointer to global logic engine state
 * @return Pointer to the global ST logic engine state
 */
st_logic_engine_state_t *st_logic_get_state(void);

/**
 * @brief Update binding_count cache for all programs (BUG-005 fix)
 *
 * Counts variable bindings from g_persist_config.var_maps and updates
 * each program's cached binding_count field for performance.
 * Call this after bind/unbind operations.
 *
 * @param state Logic engine state
 */
void st_logic_update_binding_counts(st_logic_engine_state_t *state);

/**
 * @brief Reset performance statistics for a program (v4.1.0)
 * @param state Logic engine state
 * @param program_id Program ID (0-3), or 0xFF for all programs
 */
void st_logic_reset_stats(st_logic_engine_state_t *state, uint8_t program_id);

/**
 * @brief Reset global cycle statistics (v4.1.0)
 * @param state Logic engine state
 */
void st_logic_reset_cycle_stats(st_logic_engine_state_t *state);

/* ============================================================================
 * FEAT-007: GLOBAL_VAR MANAGEMENT
 * ============================================================================ */

/**
 * @brief Upload GLOBAL_VAR declaration source (does NOT compile/parse it)
 * @param state Logic engine state
 * @param source Source text (just the "GLOBAL_VAR ... END_VAR" block)
 * @param source_size Size of source code
 * @return true if successful (false if too large for ST_GLOBAL_SOURCE_MAX)
 */
bool st_logic_globals_upload(st_logic_engine_state_t *state, const char *source, uint32_t source_size);

/**
 * @brief Parse the uploaded GLOBAL_VAR source and (re)build state->globals[]
 *
 * Resets all global values to zero — any program relying on a global's
 * current value across this call will see it reset. Safe to call at boot
 * (before any of Logic1-4 compile) or after an explicit re-upload.
 *
 * @param state Logic engine state
 * @return true if successful (false on parse error, see state->global_last_error)
 */
bool st_logic_globals_compile(st_logic_engine_state_t *state);

/**
 * @brief Look up a global variable by name
 * @param state Logic engine state
 * @param name Variable name (case-sensitive, matches local variable rules)
 * @return Index into state->globals[], or 0xFF if not found
 */
uint8_t st_logic_globals_lookup(st_logic_engine_state_t *state, const char *name);

/* ============================================================================
 * FEAT-010: PROGRAM PRIORITY / SCHEDULING
 * ============================================================================ */

/**
 * @brief Set the shared "default NORMAL interval" AND cascade it to every
 * currently-NORMAL-priority program's own interval_ms (HIGH programs are
 * untouched — they're scheduled independently). This is what the old,
 * single global execution_interval_ms setter now means in practice.
 * @param state Logic engine state
 * @param interval_ms New interval (ST_LOGIC_INTERVAL_MIN_MS..MAX_MS)
 */
void st_logic_set_global_interval(st_logic_engine_state_t *state, uint32_t interval_ms);

/**
 * @brief Set one program's own execution interval (independent of the others)
 * @param state Logic engine state
 * @param program_id Program ID (0-3)
 * @param interval_ms New interval (ST_LOGIC_INTERVAL_MIN_MS..MAX_MS)
 * @return true if successful (false: invalid ID/range)
 */
bool st_logic_set_program_interval(st_logic_engine_state_t *state, uint8_t program_id, uint32_t interval_ms);

/**
 * @brief Set one program's priority (NORMAL/HIGH)
 *
 * Rejected if the program currently has any active Modbus/GPIO variable
 * binding (HIGH programs cannot use bindings in this v1 — see BUGS_INDEX.md
 * FEAT-010). Starts/stops the shared HIGH task+timer as needed.
 * @param state Logic engine state
 * @param program_id Program ID (0-3)
 * @param priority ST_LOGIC_PRIORITY_NORMAL or ST_LOGIC_PRIORITY_HIGH
 * @param error_out Optional: filled with a reason string if rejected (may be NULL)
 * @param error_out_size Size of error_out buffer
 * @return true if successful
 */
bool st_logic_set_program_priority(st_logic_engine_state_t *state, uint8_t program_id, uint8_t priority,
                                    char *error_out, size_t error_out_size);

/**
 * @brief Initialize the shared HIGH-priority task + esp_timer (called once at boot).
 * The task starts idle (no timer running) until at least one program is both
 * enabled and HIGH-priority.
 */
void st_logic_high_task_init(void);

/**
 * @brief Recompute the HIGH-priority esp_timer's period from the fastest
 * currently enabled+HIGH program's interval_ms, starting/stopping the timer
 * as needed. Call after any enable/disable/priority/interval change.
 * @param state Logic engine state
 */
void st_logic_high_reschedule(st_logic_engine_state_t *state);

/**
 * @brief Save ST Logic programs to PersistConfig (before config_save_to_nvs)
 * @param config Persistent config to save programs into
 * @return true if successful
 */
bool st_logic_save_to_persist_config(PersistConfig *config);

/**
 * @brief Load ST Logic programs from PersistConfig (after config_load_from_nvs)
 * @param config Persistent config containing programs to load
 * @return true if successful
 */
bool st_logic_load_from_persist_config(const PersistConfig *config);

#endif // ST_LOGIC_CONFIG_H
