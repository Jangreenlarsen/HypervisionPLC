/**
 * @file cli_commands.h
 * @brief CLI `set` command handlers (LAYER 7)
 *
 * LAYER 7: User Interface - CLI Commands
 * Responsibility: Implement all `set` commands
 *
 * This file handles:
 * - set counter ... (configuration)
 * - set timer ... (configuration)
 * - set hostname <name>
 * - set baud <rate>
 * - set id <slave_id>
 * - set reg <addr> <value>
 * - set coil <idx> <0|1>
 *
 * Each command is a standalone handler function
 * No cross-command dependencies (modular)
 *
 * Does NOT handle:
 * - Command parsing (→ cli_parser.h)
 * - `show` commands (→ cli_show.h)
 * - Serial I/O (→ cli_shell.h)
 */

#ifndef CLI_COMMANDS_H
#define CLI_COMMANDS_H

#include <stdint.h>
#include "types.h"

/* ============================================================================
 * PUBLIC API - Command Handlers
 * ============================================================================ */

/**
 * @brief Handle "set counter" command
 * @param argc Argument count
 * @param argv Argument array
 */
void cli_cmd_set_counter(uint8_t argc, char* argv[]);

/**
 * @brief Handle "set counter <id> control" command
 * @param argc Argument count
 * @param argv Argument array
 */
void cli_cmd_set_counter_control(uint8_t argc, char* argv[]);

/**
 * @brief Handle "set timer" command
 * @param argc Argument count
 * @param argv Argument array
 */
void cli_cmd_set_timer(uint8_t argc, char* argv[]);

/**
 * @brief Handle "reset counter" command
 * @param argc Argument count
 * @param argv Argument array
 */
void cli_cmd_reset_counter(uint8_t argc, char* argv[]);

/**
 * @brief Handle "delete counter" command (disable and reset)
 * @param argc Argument count
 * @param argv Argument array
 */
void cli_cmd_delete_counter(uint8_t argc, char* argv[]);

/**
 * @brief Handle "clear counters" command
 */
void cli_cmd_clear_counters(void);

/**
 * @brief Handle "set hostname" command
 * @param hostname New hostname (max 32 chars)
 */
void cli_cmd_set_hostname(const char* hostname);

/**
 * @brief Handle "set baud" command
 * @param baud Baud rate
 */
void cli_cmd_set_baud(uint32_t baud);

/**
 * @brief Handle "set id" command
 * @param id Slave ID (0-247)
 */
void cli_cmd_set_id(uint8_t id);

/**
 * @brief Handle "set reg" command (write holding register)
 * @param addr Register address
 * @param value Register value
 */
void cli_cmd_set_reg(uint16_t addr, uint16_t value);

/**
 * @brief Handle "set coil" command
 * @param idx Coil index
 * @param value 0 or 1
 */
void cli_cmd_set_coil(uint16_t idx, uint8_t value);

/**
 * @brief Handle "set gpio" command (map GPIO pin to coil/input)
 * @param argc Argument count
 * @param argv Argument array
 */
void cli_cmd_set_gpio(uint8_t argc, char* argv[]);

/**
 * @brief Handle "no set gpio" command (remove GPIO mapping)
 * @param argc Argument count
 * @param argv Argument array
 */
void cli_cmd_no_set_gpio(uint8_t argc, char* argv[]);

/**
 * @brief Handle "save" command (persist config)
 */
void cli_cmd_save(void);

/**
 * @brief Handle "load" command (load config from storage)
 */
void cli_cmd_load(void);

/**
 * @brief Handle "set persist group" command (create/modify persistence group)
 * @param argc Argument count
 * @param argv Argument array
 *
 * Syntax:
 *   set persist group <name> add <reg1> [reg2] [reg3] ...
 *   set persist group <name> remove <reg>
 *   set persist group <name> delete
 */
void cli_cmd_set_persist_group(uint8_t argc, char* argv[]);

/**
 * @brief Handle "save registers" command (save persistent register groups)
 * @param argc Argument count
 * @param argv Argument array
 *
 * Syntax:
 *   save registers all
 *   save registers group <name>
 */
void cli_cmd_save_registers(uint8_t argc, char* argv[]);

/**
 * @brief Handle "load registers all" or "load registers group <name>" command
 *
 * Loads persistent register groups from NVS.
 * Syntax:
 *   load registers all              - Load all groups
 *   load registers group <name>     - Load specific group
 */
void cli_cmd_load_registers(uint8_t argc, char* argv[]);

/**
 * @brief Handle "set persist enable" command
 * @param enabled true to enable, false to disable
 */
void cli_cmd_set_persist_enable(bool enabled);

/**
 * @brief Handle "set persist reset" command (clear all corrupted groups)
 *
 * BUG-140 FIX: Clears all persistence groups and resets to clean state.
 * Use this to recover from corrupted NVS data.
 */
void cli_cmd_set_persist_reset(void);

/**
 * @brief Handle "set persist auto-load" command (configure auto-load on boot)
 * @param argc Argument count
 * @param argv Argument array
 *
 * Syntax:
 *   set persist auto-load enable
 *   set persist auto-load disable
 *   set persist auto-load add <group_id>
 *   set persist auto-load remove <group_id>
 */
void cli_cmd_set_persist_auto_load(uint8_t argc, char* argv[]);

/**
 * @brief Handle "defaults" command (reset to factory defaults)
 */
void cli_cmd_defaults(void);

/**
 * @brief Handle "reboot" command
 */
void cli_cmd_reboot(void);

/**
 * @brief Handle "exit" command (close Telnet connection)
 */
void cli_cmd_exit(void);

/**
 * @brief Handle "set echo" command (enable/disable remote echo)
 * @param argc Argument count
 * @param argv Argument array (argv[0] = "on" or "off")
 */
void cli_cmd_set_echo(uint8_t argc, char* argv[]);

/**
 * @brief Handle "write reg" command (write single holding register)
 * @param argc Argument count
 * @param argv Argument array (argv[0] = addr, argv[1] = "value", argv[2] = value)
 */
void cli_cmd_write_reg(uint8_t argc, char* argv[]);

/**
 * @brief Handle "write coil" command (write single coil)
 * @param argc Argument count
 * @param argv Argument array (argv[0] = addr, argv[1] = "value", argv[2] = on|off)
 */
void cli_cmd_write_coil(uint8_t argc, char* argv[]);

/**
 * @brief Handle "set gpio 2 enable/disable" command (GPIO2 heartbeat control)
 * @param argc Argument count
 * @param argv Argument array (argv[0] = "2", argv[1] = "enable" or "disable")
 */
void cli_cmd_set_gpio2(uint8_t argc, char* argv[]);

/**
 * @brief Handle "set debug" command (runtime debug flag control)
 * @param argc Argument count
 * @param argv Argument array (argv[0] = flag, argv[1] = "on" or "off")
 */
void cli_cmd_set_debug(uint8_t argc, char* argv[]);

/* ============================================================================
 * NETWORK/WI-FI COMMANDS (v3.0+)
 * ============================================================================ */

/**
 * @brief Handle "set wifi" command (SSID, password, DHCP, static IP)
 * @param argc Argument count
 * @param argv Argument array (argv[0] = option name, argv[1:] = values)
 *
 * Examples:
 *   set wifi ssid MyNetwork
 *   set wifi password MyPassword123
 *   set wifi dhcp on
 *   set wifi dhcp off
 *   set wifi ip 192.168.1.100
 *   set wifi gateway 192.168.1.1
 *   set wifi netmask 255.255.255.0
 *   set wifi dns 8.8.8.8
 */
void cli_cmd_set_wifi(uint8_t argc, char* argv[]);

/**
 * @brief Handle "set telnet" command for Telnet configuration (standalone)
 * @param argc Argument count
 * @param argv Argument values
 *
 * Usage:
 *   set telnet enable|disable
 *   set telnet user <username>
 *   set telnet pass <password>
 *   set telnet port <port>
 */
void cli_cmd_set_telnet(uint8_t argc, char* argv[]);

/**
 * @brief Handle "set ethernet" command for W5500 Ethernet configuration (v6.1.0+)
 * @param argc Argument count
 * @param argv Argument values
 *
 * Usage:
 *   set ethernet enable|disable
 *   set ethernet dhcp on|off
 *   set ethernet ip 192.168.1.101
 *   set ethernet gateway 192.168.1.1
 *   set ethernet netmask 255.255.255.0
 *   set ethernet dns 8.8.8.8
 */
void cli_cmd_set_ethernet(uint8_t argc, char* argv[]);

/**
 * @brief Handle "set http" command for HTTP REST API configuration (v6.0.0+)
 * @param argc Argument count
 * @param argv Argument values (e.g., {"enabled", "on"})
 *
 * Usage:
 *   set http enabled on|off
 *   set http port 8080
 *   set http auth on|off
 *   set http username admin
 *   set http password secret
 */
void cli_cmd_set_http(uint8_t argc, char* argv[]);

/**
 * @brief Handle "connect wifi" command (start Wi-Fi connection)
 */
void cli_cmd_connect_wifi(void);

/**
 * @brief Handle "disconnect wifi" command (stop Wi-Fi connection)
 */
void cli_cmd_disconnect_wifi(void);

#endif // CLI_COMMANDS_H

