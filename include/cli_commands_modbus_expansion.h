/**
 * @file cli_commands_modbus_expansion.h
 * @brief FEAT-409: CLI commands for Modbus Expansion Board management
 * (board CRUD, channel config, diagnostic read/write) — see
 * include/expansion_api_client.h for the underlying engine.
 */

#ifndef CLI_COMMANDS_MODBUS_EXPANSION_H
#define CLI_COMMANDS_MODBUS_EXPANSION_H

#include <stdint.h>

// "set modbus-expansion ..." subcommands
void cli_cmd_set_modbus_expansion_add(uint8_t argc, char **argv);
void cli_cmd_set_modbus_expansion_edit(uint8_t argc, char **argv);
void cli_cmd_set_modbus_expansion_remove(uint8_t argc, char **argv);
void cli_cmd_set_modbus_expansion_channel(uint8_t argc, char **argv);

// "show modbus-expansion [board]"
void cli_cmd_show_modbus_expansion(uint8_t argc, char **argv);

// "mbx <board> status | <board> <channel> read ... | <board> <channel> write ..."
void cli_cmd_mbx_status(uint8_t argc, char **argv);
void cli_cmd_mbx_read(uint8_t argc, char **argv);
void cli_cmd_mbx_write(uint8_t argc, char **argv);

#endif // CLI_COMMANDS_MODBUS_EXPANSION_H
