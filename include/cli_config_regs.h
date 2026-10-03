/**
 * @file cli_config_regs.h
 * @brief CLI register configuration commands
 */

#ifndef CLI_CONFIG_REGS_H
#define CLI_CONFIG_REGS_H

#include <stdint.h>
#include "types.h"
#include "constants.h"

/**
 * set reg STATIC <address> Value <value>
 */
void cli_cmd_set_reg_static(uint8_t argc, char* argv[]);

/**
 * set reg DYNAMIC <address> counter<id>:<function> or timer<id>:<function>
 */
void cli_cmd_set_reg_dynamic(uint8_t argc, char* argv[]);
void cli_cmd_no_set_reg(uint8_t argc, char* argv[]);   // BUG-440

/**
 * show reg - Display all register mappings
 */
void cli_cmd_show_regs(void);

#endif // CLI_CONFIG_REGS_H
