/**
 * @file cli_commands_logic.h
 * @brief CLI Commands for Structured Text Logic Mode
 */

#ifndef CLI_COMMANDS_LOGIC_H
#define CLI_COMMANDS_LOGIC_H

#include <stdint.h>
#include "st_logic_config.h"
#include "st_logic_engine.h"

/* ============================================================================
 * CLI COMMAND HANDLERS FOR LOGIC MODE
 * ============================================================================ */

/**
 * @brief set logic <id> upload "<st_code>"
 * Upload ST source code for a logic program
 */
int cli_cmd_set_logic_upload(st_logic_engine_state_t *logic_state, uint8_t program_id,
                             const char *source_code);

/**
 * @brief set logic <id> enabled:true|false
 * Enable or disable a logic program
 */
int cli_cmd_set_logic_enabled(st_logic_engine_state_t *logic_state, uint8_t program_id,
                              bool enabled);

/**
 * @brief set logic <id> reinit
 * Cold restart: reset variables to compiled initial values
 */
int cli_cmd_set_logic_reinit(st_logic_engine_state_t *logic_state, uint8_t program_id);

/**
 * @brief set logic debug:true|false
 * Enable/disable debug output for ST Logic
 */
int cli_cmd_set_logic_debug(st_logic_engine_state_t *logic_state, bool debug);

/**
 * @brief set logic interval:X (v4.1.0)
 * Set global execution interval for all ST Logic programs
 * Allowed values: 10, 20, 25, 50, 75, 100 ms
 */
int cli_cmd_set_logic_interval(st_logic_engine_state_t *logic_state, uint32_t interval_ms);

/**
 * @brief set logic <id> delete
 * Delete a logic program
 */
int cli_cmd_set_logic_delete(st_logic_engine_state_t *logic_state, uint8_t program_id);

/**
 * @brief set logic <id> bind <var_name> reg:100|coil:10|input-dis:5
 * Bind ST variable to Modbus register using variable name and binding spec
 */
int cli_cmd_set_logic_bind_by_name(st_logic_engine_state_t *logic_state, uint8_t program_id,
                                    const char *var_name, const char *binding_spec, const char *direction_override);

/**
 * @brief set logic <id> bind <var_index> <register> [input|output|both]
 * Bind ST variable to Modbus register (LEGACY - use bind_by_name for new syntax)
 */
int cli_cmd_set_logic_bind(st_logic_engine_state_t *logic_state, uint8_t program_id,
                           uint8_t var_idx, uint16_t register_address,
                           const char *direction, uint8_t input_type, uint8_t output_type);

/**
 * @brief show logic <id> [st]
 * Show details for a specific logic program
 * @param show_source 1=show ST source, 0=hide (v5.1.0)
 */
int cli_cmd_show_logic_program(st_logic_engine_state_t *logic_state, uint8_t program_id, uint8_t show_source);

/**
 * @brief show logic all
 * Show all logic programs
 */
int cli_cmd_show_logic_all(st_logic_engine_state_t *logic_state);

/**
 * @brief show logic stats
 * Show execution statistics for logic programs
 */
int cli_cmd_show_logic_stats(st_logic_engine_state_t *logic_state);

/**
 * @brief show logic programs
 * Show overview of all programs with status summary
 */
int cli_cmd_show_logic_programs(st_logic_engine_state_t *logic_state);

/**
 * @brief show logic errors
 * Show only programs with compilation or runtime errors
 */
int cli_cmd_show_logic_errors(st_logic_engine_state_t *logic_state);

/**
 * @brief show logic <id> code
 * Show source code for a specific logic program with line breaks
 */
int cli_cmd_show_logic_code(st_logic_engine_state_t *logic_state, uint8_t program_id);

/**
 * @brief show logic all code
 * Show source code for all logic programs
 */
int cli_cmd_show_logic_code_all(st_logic_engine_state_t *logic_state);

/**
 * @brief show logic <id> timing
 * Show timing information (execution times, jitter) for a specific logic program
 */
int cli_cmd_show_logic_timing(st_logic_engine_state_t *logic_state, uint8_t program_id);

/**
 * @brief show logic <id> bytecode
 * Show compiled bytecode instructions for a specific logic program
 */
int cli_cmd_show_logic_bytecode(st_logic_engine_state_t *logic_state, uint8_t program_id);

/**
 * @brief show logic <id> functions (FEAT-003)
 * Show user-defined functions for a specific logic program
 */
int cli_cmd_show_logic_functions(st_logic_engine_state_t *logic_state, uint8_t program_id);

/**
 * @brief reset logic stats [all|<id>]
 * Reset execution statistics for one or all logic programs
 * @param target "all" or program ID as string
 */
int cli_cmd_reset_logic_stats(st_logic_engine_state_t *logic_state, const char *target);

/* ============================================================================
 * FEAT-008: DEBUGGER COMMANDS
 * ============================================================================ */

/**
 * @brief set logic <id> debug pause
 * Pause program at next instruction
 */
int cli_cmd_set_logic_debug_pause(st_logic_engine_state_t *logic_state, uint8_t program_id);

/**
 * @brief set logic <id> debug continue
 * Continue execution until breakpoint or halt
 */
int cli_cmd_set_logic_debug_continue(st_logic_engine_state_t *logic_state, uint8_t program_id);

/**
 * @brief set logic <id> debug step
 * Execute one instruction then pause
 */
int cli_cmd_set_logic_debug_step(st_logic_engine_state_t *logic_state, uint8_t program_id);

/**
 * @brief set logic <id> debug break <pc>
 * Add breakpoint at PC address
 */
int cli_cmd_set_logic_debug_breakpoint(st_logic_engine_state_t *logic_state, uint8_t program_id, uint16_t pc);

/**
 * @brief set logic <id> debug break line <line>
 * Add breakpoint at source line number
 */
int cli_cmd_set_logic_debug_breakpoint_line(st_logic_engine_state_t *logic_state, uint8_t program_id, uint16_t line);

/**
 * @brief set logic <id> debug clear [<pc>]
 * Clear breakpoint at PC, or all breakpoints if no PC given
 */
int cli_cmd_set_logic_debug_clear(st_logic_engine_state_t *logic_state, uint8_t program_id, int pc);

/**
 * @brief set logic <id> debug stop
 * Stop debugging and return to normal execution
 */
int cli_cmd_set_logic_debug_stop(st_logic_engine_state_t *logic_state, uint8_t program_id);

/**
 * @brief show logic <id> debug
 * Show debug state (mode, PC, breakpoints)
 */
int cli_cmd_show_logic_debug(st_logic_engine_state_t *logic_state, uint8_t program_id);

/**
 * @brief show logic <id> debug vars
 * Show all variables with current values
 */
int cli_cmd_show_logic_debug_vars(st_logic_engine_state_t *logic_state, uint8_t program_id);

/**
 * @brief show logic <id> debug stack
 * Show execution stack
 */
int cli_cmd_show_logic_debug_stack(st_logic_engine_state_t *logic_state, uint8_t program_id);

#endif /* CLI_COMMANDS_LOGIC_H */
