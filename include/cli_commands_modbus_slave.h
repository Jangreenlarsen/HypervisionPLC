// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file cli_commands_modbus_slave.h
 * @brief CLI Commands for Modbus Slave Configuration
 */

#ifndef CLI_COMMANDS_MODBUS_SLAVE_H
#define CLI_COMMANDS_MODBUS_SLAVE_H

#include <stdint.h>

// SET commands
void cli_cmd_set_modbus_slave_enabled(bool enabled);
void cli_cmd_set_modbus_slave_slave_id(uint8_t id);
void cli_cmd_set_modbus_slave_baudrate(uint32_t baudrate);
void cli_cmd_set_modbus_slave_parity(const char *parity);
void cli_cmd_set_modbus_slave_stop_bits(uint8_t bits);
void cli_cmd_set_modbus_slave_inter_frame_delay(uint16_t ms);

// SHOW command
void cli_cmd_show_modbus_slave();

#endif // CLI_COMMANDS_MODBUS_SLAVE_H
