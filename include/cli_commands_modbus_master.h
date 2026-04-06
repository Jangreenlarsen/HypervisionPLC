/**
 * @file cli_commands_modbus_master.h
 * @brief CLI Commands for Modbus Master Configuration
 */

#ifndef CLI_COMMANDS_MODBUS_MASTER_H
#define CLI_COMMANDS_MODBUS_MASTER_H

#include <stdint.h>

// SET commands
void cli_cmd_set_modbus_master_enabled(bool enabled);
void cli_cmd_set_modbus_master_baudrate(uint32_t baudrate);
void cli_cmd_set_modbus_master_parity(const char *parity);
void cli_cmd_set_modbus_master_stop_bits(uint8_t bits);
void cli_cmd_set_modbus_master_timeout(uint16_t ms);
void cli_cmd_set_modbus_master_inter_frame_delay(uint16_t ms);
void cli_cmd_set_modbus_master_max_requests(uint8_t count);
void cli_cmd_set_modbus_master_cache_ttl(uint16_t ttl_ms);

// SHOW command
void cli_cmd_show_modbus_master();

#endif // CLI_COMMANDS_MODBUS_MASTER_H
