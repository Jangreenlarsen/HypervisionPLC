/**
 * @file cli_show.h
 * @brief CLI `show` command handlers (LAYER 7)
 *
 * LAYER 7: User Interface - CLI Show Commands
 * Responsibility: Implement all `show` commands for status/config display
 *
 * This file handles:
 * - show config (full system configuration)
 * - show counters (counter status table)
 * - show timers (timer status)
 * - show registers [start] [count] (holding registers)
 * - show coils (coil states)
 * - show inputs (discrete inputs)
 * - show version (firmware version)
 *
 * Each command is a standalone handler function
 * Formatting optimized for serial terminal display
 *
 * Does NOT handle:
 * - Command parsing (→ cli_parser.h)
 * - `set` commands (→ cli_commands.h)
 * - Serial I/O (→ cli_shell.h)
 */

#ifndef CLI_SHOW_H
#define CLI_SHOW_H

#include <stdint.h>
#include "types.h"

/* ============================================================================
 * PUBLIC API - Show Command Handlers
 * ============================================================================ */

/**
 * @brief Handle "show config" command (full system configuration)
 * @param section Optional section filter (NULL or "" for all, e.g. "WIFI", "MODBUS", "COUNTERS")
 */
void cli_cmd_show_config(const char *section = nullptr);

/**
 * @brief Handle "show counters" command (counter status table)
 */
void cli_cmd_show_counters(void);

/**
 * @brief Handle "show counter <id>" command (specific counter status)
 * @param id Counter ID (1-4)
 * @param verbose Show extended runtime details (default: false)
 */
void cli_cmd_show_counter(uint8_t id, bool verbose = false);

/**
 * @brief Handle "show timers" command (timer status)
 */
void cli_cmd_show_timers(void);

/**
 * @brief Handle "show timer <id>" command (specific timer status)
 * @param id Timer ID (1-4)
 * @param verbose Show extended runtime state (default: false)
 */
void cli_cmd_show_timer(uint8_t id, bool verbose = false);

/**
 * @brief Handle "show registers" command
 * @param start Starting register address (0 if all)
 * @param count Number of registers to show (0 if all)
 */
void cli_cmd_show_registers(uint16_t start, uint16_t count);

/**
 * @brief Handle "show coils" command
 */
void cli_cmd_show_coils(void);

/**
 * @brief Handle "show inputs" command (discrete inputs)
 */
void cli_cmd_show_inputs(void);

/**
 * @brief Handle "show st-stats" command (ST Logic performance stats from Modbus IR 252-293)
 */
void cli_cmd_show_st_logic_stats_modbus(void);

/**
 * @brief Handle "show version" command
 */
void cli_cmd_show_version(void);

/**
 * @brief Handle "show gpio" command (GPIO mappings)
 */
void cli_cmd_show_gpio(void);

/**
 * @brief Handle "show gpio <pin>" command (specific GPIO pin details)
 * @param pin GPIO pin number (0-39)
 */
void cli_cmd_show_gpio_pin(uint8_t pin);

/**
 * @brief Handle "show echo" command (remote echo status)
 */
void cli_cmd_show_echo(void);

/**
 * @brief Handle "show wifi" command (Wi-Fi status)
 */
void cli_cmd_show_wifi(void);

/**
 * @brief Handle "show ethernet" command (W5500 Ethernet status) (v6.1.0+)
 */
void cli_cmd_show_ethernet(void);

/**
 * @brief Handle "show http" command (HTTP REST API status) (v6.0.0+)
 */
void cli_cmd_show_http(void);

/**
 * @brief Handle "show debug" command (Debug flags status)
 */
void cli_cmd_show_debug(void);

/**
 * @brief Handle "show persist" command (Persistent register groups status)
 */
void cli_cmd_show_persist(void);

/**
 * @brief Handle "show watchdog" command (Watchdog monitor status)
 */
void cli_cmd_show_watchdog(void);

/**
 * @brief Handle "show backup" command (Backup/restore URL)
 */
void cli_cmd_show_backup(void);

/**
 * @brief Handle "read reg <id> <antal>" command
 * @param argc Argument count (must be 2)
 * @param argv Argument array (argv[0] = start addr, argv[1] = count)
 */
void cli_cmd_read_reg(uint8_t argc, char* argv[]);

/**
 * @brief Handle "read coil <id> <antal>" command
 * @param argc Argument count (must be 2)
 * @param argv Argument array (argv[0] = start addr, argv[1] = count)
 */
void cli_cmd_read_coil(uint8_t argc, char* argv[]);

/**
 * @brief Handle "read input <id> <antal>" command
 * @param argc Argument count (must be 2)
 * @param argv Argument array (argv[0] = start addr, argv[1] = count)
 */
void cli_cmd_read_input(uint8_t argc, char* argv[]);

/**
 * @brief Handle "read input-reg <start> <count>" command
 * @param argc Argument count (must be 2)
 * @param argv Argument array (argv[0] = start addr, argv[1] = count)
 */
void cli_cmd_read_input_reg(uint8_t argc, char* argv[]);

#endif // CLI_SHOW_H

