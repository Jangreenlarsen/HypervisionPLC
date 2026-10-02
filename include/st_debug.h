/**
 * @file st_debug.h
 * @brief ST Logic Debugger - Single-step, breakpoints, and variable inspection
 *
 * FEAT-008: Provides debugging capabilities for ST Logic programs.
 *
 * Features:
 *   - Pause/continue program execution
 *   - Single-step through instructions
 *   - Breakpoints at PC addresses
 *   - Variable inspection when paused
 *   - Stack inspection when paused
 *
 * Usage:
 *   set logic 1 debug pause      - Pause program
 *   set logic 1 debug step       - Execute one instruction
 *   set logic 1 debug continue   - Continue to next breakpoint
 *   set logic 1 debug break 10   - Set breakpoint at PC=10
 *   show logic 1 debug           - Show debug state
 *   show logic 1 debug vars      - Show variable values
 */

#ifndef ST_DEBUG_H
#define ST_DEBUG_H

#include <stdint.h>
#include <stdbool.h>
#include "st_vm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum number of breakpoints per program */
#define ST_DEBUG_MAX_BREAKPOINTS 8

/* Disabled breakpoint marker */
#define ST_DEBUG_BP_DISABLED 0xFFFF

/* ============================================================================
 * DEBUG MODE ENUM
 * ============================================================================ */

typedef enum {
  ST_DEBUG_OFF = 0,      // Normal execution (no debugging)
  ST_DEBUG_PAUSED,       // Paused - waiting for step/continue command
  ST_DEBUG_STEP,         // Execute one instruction, then pause
  ST_DEBUG_RUN           // Run until breakpoint or halt
} st_debug_mode_t;

/* ============================================================================
 * PAUSE REASON ENUM
 * ============================================================================ */

typedef enum {
  ST_DEBUG_REASON_NONE = 0,
  ST_DEBUG_REASON_PAUSE_CMD,   // User requested pause
  ST_DEBUG_REASON_STEP,        // Single-step completed
  ST_DEBUG_REASON_BREAKPOINT,  // Hit a breakpoint
  ST_DEBUG_REASON_HALT,        // Program halted normally
  ST_DEBUG_REASON_ERROR        // Runtime error
} st_debug_reason_t;

/* ============================================================================
 * COMPACT DEBUG SNAPSHOT (to save RAM)
 * Only stores essential data for inspection, not full VM state
 * ============================================================================ */

typedef struct {
  uint16_t pc;                   // Program counter
  uint8_t sp;                    // Stack pointer
  uint8_t halted;                // Execution halted
  uint8_t error;                 // Error flag
  uint32_t step_count;           // Steps executed
  uint8_t var_count;             // Number of variables
  st_value_t variables[ST_MAX_PROGRAM_VARS];      // Variable values (FEAT-426: 64)
  st_datatype_t var_types[ST_MAX_PROGRAM_VARS];   // Variable types
  char error_msg[64];            // Truncated error message
} st_debug_snapshot_t;

/* ============================================================================
 * DEBUG STATE STRUCTURE
 * ============================================================================ */

typedef struct {
  // Current debug mode
  st_debug_mode_t mode;

  // Breakpoints (PC addresses, 0xFFFF = disabled)
  uint16_t breakpoints[ST_DEBUG_MAX_BREAKPOINTS];
  uint8_t breakpoint_count;

  // Watch variable (0xFF = none)
  uint8_t watch_var_index;

  // Compact snapshot of VM state when paused (for inspection)
  st_debug_snapshot_t snapshot;
  bool snapshot_valid;

  // Last pause reason
  st_debug_reason_t pause_reason;

  // Breakpoint that was hit (if pause_reason == BREAKPOINT)
  uint16_t hit_breakpoint_pc;

  // Statistics
  uint32_t total_steps_debugged;
  uint32_t breakpoints_hit_count;

  // Flag: Does this program own the shared debug VM?
  bool owns_debug_vm;

} st_debug_state_t;

/* ============================================================================
 * SHARED DEBUG VM (dynamically allocated to save RAM when not debugging)
 * ============================================================================ */

typedef struct {
  st_vm_t *vm;             // Pointer to dynamically allocated VM (NULL when not debugging)
  uint8_t program_id;      // Which program owns it (0-3, or 0xFF = none)
  bool valid;              // Is VM state valid?
} st_shared_debug_vm_t;

// Global shared debug VM (declared in st_debug.cpp)
extern st_shared_debug_vm_t g_shared_debug_vm;

/**
 * @brief Allocate shared debug VM (call when starting debug)
 * @return true if allocated successfully
 */
bool st_debug_alloc_vm(void);

/**
 * @brief Free shared debug VM (call when stopping debug)
 */
void st_debug_free_vm(void);

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

/**
 * @brief Initialize debug state for a program
 * @param debug Debug state to initialize
 */
void st_debug_init(st_debug_state_t *debug);

/* ============================================================================
 * DEBUG CONTROL
 * ============================================================================ */

/**
 * @brief Pause program at next instruction
 * @param debug Debug state
 */
void st_debug_pause(st_debug_state_t *debug);

/**
 * @brief Continue execution until breakpoint or halt
 * @param debug Debug state
 */
void st_debug_continue(st_debug_state_t *debug);

/**
 * @brief Execute one instruction then pause
 * @param debug Debug state
 */
void st_debug_step(st_debug_state_t *debug);

/**
 * @brief Stop debugging and return to normal execution
 * @param debug Debug state
 */
void st_debug_stop(st_debug_state_t *debug);

/* ============================================================================
 * BREAKPOINT MANAGEMENT
 * ============================================================================ */

/**
 * @brief Add breakpoint at PC address
 * @param debug Debug state
 * @param pc PC address for breakpoint
 * @return true if added, false if max breakpoints reached or already exists
 */
bool st_debug_add_breakpoint(st_debug_state_t *debug, uint16_t pc);

/**
 * @brief Remove breakpoint at PC address
 * @param debug Debug state
 * @param pc PC address to remove
 * @return true if removed, false if not found
 */
bool st_debug_remove_breakpoint(st_debug_state_t *debug, uint16_t pc);

/**
 * @brief Clear all breakpoints
 * @param debug Debug state
 */
void st_debug_clear_breakpoints(st_debug_state_t *debug);

/**
 * @brief Check if PC is at a breakpoint
 * @param debug Debug state
 * @param pc Current PC
 * @return true if breakpoint exists at this PC
 */
bool st_debug_check_breakpoint(st_debug_state_t *debug, uint16_t pc);

/* ============================================================================
 * SNAPSHOT MANAGEMENT
 * ============================================================================ */

/**
 * @brief Save VM state snapshot for inspection
 * @param debug Debug state
 * @param vm Current VM state to snapshot
 * @param reason Why we're pausing
 */
void st_debug_save_snapshot(st_debug_state_t *debug, const st_vm_t *vm, int reason);

/**
 * @brief Invalidate snapshot (after continue/step)
 * @param debug Debug state
 */
void st_debug_clear_snapshot(st_debug_state_t *debug);

/* ============================================================================
 * DISPLAY FUNCTIONS
 * ============================================================================ */

/**
 * @brief Print debug state summary
 * @param debug Debug state
 * @param prog Program config (for variable names) - pass as void* to avoid circular include
 */
void st_debug_print_state(st_debug_state_t *debug, void *prog);

/**
 * @brief Print all variables with current values
 * @param debug Debug state
 * @param prog Program config (for variable names and types) - pass as void* to avoid circular include
 */
void st_debug_print_variables(st_debug_state_t *debug, void *prog);

/**
 * @brief Print execution stack
 * @param debug Debug state
 */
void st_debug_print_stack(st_debug_state_t *debug);

/**
 * @brief Print breakpoints list
 * @param debug Debug state
 */
void st_debug_print_breakpoints(st_debug_state_t *debug);

/**
 * @brief Print current instruction at PC
 * @param debug Debug state
 * @param prog Program config (for bytecode) - pass as void* to avoid circular include
 */
void st_debug_print_instruction(st_debug_state_t *debug, void *prog);

#ifdef __cplusplus
}
#endif

#endif // ST_DEBUG_H
