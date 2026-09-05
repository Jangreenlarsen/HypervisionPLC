/**
 * @file cli_config_coils.cpp
 * @brief CLI `set coil` command handlers (LAYER 7)
 *
 * Responsibility:
 * - Parse `set coil` commands (STATIC and DYNAMIC)
 * - Add/update coil mappings to PersistConfig
 * - Display current coil configuration
 */

#include "cli_config_coils.h"
#include "config_struct.h"
#include "registers.h"
#include "debug.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/**
 * set coil STATIC <address> Value <ON|OFF>
 *
 * Example:
 *   set coil STATIC 5 Value ON
 *   set coil STATIC 10 Value OFF
 */
void cli_cmd_set_coil_static(uint8_t argc, char* argv[]) {
  // set coil STATIC <address> Value <ON|OFF>

  if (argc < 3) {
    debug_println("SET COIL STATIC: missing arguments");
    debug_println("  Usage: set coil STATIC <address> Value <ON|OFF>");
    return;
  }

  // Parse address
  uint16_t address = atoi(argv[0]);
  if (address >= (COILS_SIZE * 8)) {
    debug_print("SET COIL STATIC: address out of range (max ");
    debug_print_uint((COILS_SIZE * 8) - 1);
    debug_println(")");
    return;
  }

  // argv[1] should be "Value"
  if (strcmp(argv[1], "Value") != 0) {
    debug_println("SET COIL STATIC: expected 'Value' keyword");
    debug_println("  Usage: set coil STATIC <address> Value <ON|OFF>");
    return;
  }

  // Parse value (ON/OFF)
  uint8_t value = 0;
  if (!strcmp(argv[2], "ON")) {
    value = 1;
  } else if (!strcmp(argv[2], "OFF")) {
    value = 0;
  } else {
    debug_println("SET COIL STATIC: invalid value (must be ON or OFF)");
    return;
  }

  // IMPORTANT: Write directly to coil (immediate effect)
  registers_set_coil(address, value);

  // Also store in config for persistence
  uint8_t found = 0;
  for (uint8_t i = 0; i < g_persist_config.static_coil_count; i++) {
    if (g_persist_config.static_coils[i].coil_address == address) {
      g_persist_config.static_coils[i].static_value = value;
      found = 1;
      break;
    }
  }

  if (!found) {
    if (g_persist_config.static_coil_count >= MAX_DYNAMIC_COILS) {
      debug_println("SET COIL STATIC: max STATIC coils reached");
      return;
    }

    uint8_t idx = g_persist_config.static_coil_count;
    g_persist_config.static_coils[idx].coil_address = address;
    g_persist_config.static_coils[idx].static_value = value;
    g_persist_config.static_coil_count++;
  }

  debug_print("Coil ");
  debug_print_uint(address);
  debug_print(" STATIC = ");
  debug_println(value ? "ON" : "OFF");
}

/**
 * set coil DYNAMIC <address> counter<id>:<function>
 * set coil DYNAMIC <address> timer<id>:<function>
 *
 * Counter functions: overflow (flag)
 * Timer functions: output
 *
 * Examples:
 *   set coil DYNAMIC 10 counter1:overflow
 *   set coil DYNAMIC 15 timer2:output
 */
void cli_cmd_set_coil_dynamic(uint8_t argc, char* argv[]) {
  // set coil DYNAMIC <address> counter<id>:<function> or timer<id>:<function>

  if (argc < 2) {
    debug_println("SET COIL DYNAMIC: missing arguments");
    debug_println("  Usage: set coil DYNAMIC <address> counter<id>:<function> or timer<id>:<function>");
    debug_println("  Counter functions: overflow");
    debug_println("  Timer functions: output");
    return;
  }

  // Parse address
  uint16_t address = atoi(argv[0]);
  if (address >= (COILS_SIZE * 8)) {
    debug_print("SET COIL DYNAMIC: address out of range (max ");
    debug_print_uint((COILS_SIZE * 8) - 1);
    debug_println(")");
    return;
  }

  // Parse source:function
  const char* source_str = argv[1];
  char* colon = strchr(source_str, ':');

  if (!colon) {
    debug_println("SET COIL DYNAMIC: invalid format (expected counter<id>:<func> or timer<id>:<func>)");
    return;
  }

  // Extract source type and ID
  uint8_t source_type = 0xff;
  uint8_t source_id = 0xff;
  const char* function_str = colon + 1;

  // SECURITY_INDEX #10: colon-source_str is attacker/typo-controlled length
  // with no upper bound before this — clamp it so a >31-char source token
  // can't overflow the 32-byte stack buffer.
  char source_copy[32];
  size_t source_len = (size_t)(colon - source_str);
  if (source_len >= sizeof(source_copy)) source_len = sizeof(source_copy) - 1;
  strncpy(source_copy, source_str, source_len);
  source_copy[source_len] = '\0';

  if (strncmp(source_copy, "counter", 7) == 0) {
    source_type = DYNAMIC_SOURCE_COUNTER;
    source_id = atoi(source_copy + 7);
    if (source_id < 1 || source_id > 4) {
      debug_println("SET COIL DYNAMIC: invalid counter ID (must be 1-4)");
      return;
    }
  } else if (strncmp(source_copy, "timer", 5) == 0) {
    source_type = DYNAMIC_SOURCE_TIMER;
    source_id = atoi(source_copy + 5);
    if (source_id < 1 || source_id > 4) {
      debug_println("SET COIL DYNAMIC: invalid timer ID (must be 1-4)");
      return;
    }
  } else {
    debug_println("SET COIL DYNAMIC: invalid source (must be counter<id> or timer<id>)");
    return;
  }

  // Parse function
  uint8_t source_function = 0xff;

  if (source_type == DYNAMIC_SOURCE_COUNTER) {
    if (!strcmp(function_str, "overflow")) {
      source_function = COUNTER_FUNC_OVERFLOW;
    } else {
      debug_println("SET COIL DYNAMIC: invalid counter function");
      debug_println("  Valid: overflow");
      return;
    }
  } else if (source_type == DYNAMIC_SOURCE_TIMER) {
    if (!strcmp(function_str, "output")) {
      source_function = TIMER_FUNC_OUTPUT;
    } else {
      debug_println("SET COIL DYNAMIC: invalid timer function");
      debug_println("  Valid: output");
      return;
    }
  }

  // Add or update DYNAMIC coil mapping
  uint8_t found = 0;
  for (uint8_t i = 0; i < g_persist_config.dynamic_coil_count; i++) {
    if (g_persist_config.dynamic_coils[i].coil_address == address) {
      g_persist_config.dynamic_coils[i].source_type = source_type;
      g_persist_config.dynamic_coils[i].source_id = source_id;
      g_persist_config.dynamic_coils[i].source_function = source_function;
      found = 1;
      break;
    }
  }

  if (!found) {
    if (g_persist_config.dynamic_coil_count >= MAX_DYNAMIC_COILS) {
      debug_println("SET COIL DYNAMIC: max DYNAMIC coils reached");
      return;
    }

    uint8_t idx = g_persist_config.dynamic_coil_count;
    g_persist_config.dynamic_coils[idx].coil_address = address;
    g_persist_config.dynamic_coils[idx].source_type = source_type;
    g_persist_config.dynamic_coils[idx].source_id = source_id;
    g_persist_config.dynamic_coils[idx].source_function = source_function;
    g_persist_config.dynamic_coil_count++;
  }

  debug_print("Coil ");
  debug_print_uint(address);
  debug_print(" DYNAMIC = ");
  if (source_type == DYNAMIC_SOURCE_COUNTER) {
    debug_print("counter");
    debug_print_uint(source_id);
  } else if (source_type == DYNAMIC_SOURCE_TIMER) {
    debug_print("timer");
    debug_print_uint(source_id);
  }
  debug_print(":");
  debug_println(function_str);
}
