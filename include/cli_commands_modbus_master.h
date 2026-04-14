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
void cli_cmd_set_modbus_master_cache_size(uint8_t size);
void cli_cmd_set_modbus_master_queue_size(uint8_t size);

// SHOW command
void cli_cmd_show_modbus_master();

// REMOTE READ/WRITE commands (mb read / mb write)
void cli_cmd_mb_read(uint8_t argc, char **argv);
void cli_cmd_mb_write(uint8_t argc, char **argv);
void cli_cmd_mb_reset_backoff(uint8_t argc, char **argv);
void cli_cmd_mb_scan(uint8_t start_id, uint8_t end_id, uint32_t temp_baud = 0);

#endif // CLI_COMMANDS_MODBUS_MASTER_H
