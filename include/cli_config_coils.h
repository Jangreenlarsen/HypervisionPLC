/**
 * @file cli_config_coils.h
 * @brief CLI coil configuration commands
 */

#ifndef CLI_CONFIG_COILS_H
#define CLI_CONFIG_COILS_H

#include <stdint.h>
#include "types.h"
#include "constants.h"

/**
 * set coil STATIC <address> Value <ON|OFF>
 */
void cli_cmd_set_coil_static(uint8_t argc, char* argv[]);

/**
 * set coil DYNAMIC <address> counter<id>:<function> or timer<id>:<function>
 */
void cli_cmd_set_coil_dynamic(uint8_t argc, char* argv[]);
void cli_cmd_no_set_coil(uint8_t argc, char* argv[]);  // BUG-440

#endif // CLI_CONFIG_COILS_H
