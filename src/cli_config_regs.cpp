/**
 * @file cli_config_regs.cpp
 * @brief CLI `set reg` command handlers (LAYER 7)
 *
 * Responsibility:
 * - Parse `set reg` commands (STATIC and DYNAMIC)
 * - Add/update register mappings to PersistConfig
 * - Display current register configuration
 */

#include "cli_config_regs.h"
#include "config_struct.h"
#include "registers.h"
#include "debug.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>  // BUG-148 FIX: For PRId32 portable int32_t printf format

/**
 * set holding-reg STATIC <address> Value [type] <value>
 *
 * Examples:
 *   set holding-reg STATIC 100 Value 42              # Legacy: uint16
 *   set holding-reg STATIC 100 Value uint 42         # Explicit uint16
 *   set holding-reg STATIC 100 Value int -500        # Signed int16
 *   set holding-reg STATIC 100 Value dint 100000     # Signed int32 (2 regs)
 *   set holding-reg STATIC 100 Value dword 500000    # Unsigned int32 (2 regs)
 *   set holding-reg STATIC 100 Value real 3.14159    # IEEE-754 float (2 regs)
 */
void cli_cmd_set_reg_static(uint8_t argc, char* argv[]) {
  // Syntax: set holding-reg STATIC <address> Value [type] <value>
  // type is optional (defaults to uint for backward compatibility)

  if (argc < 3) {
    debug_println("SET HOLDING-REG STATIC: missing arguments");
    debug_println("  Usage: set holding-reg STATIC <address> Value [type] <value>");
    debug_println("  Types: uint (default), int, dint, dword, real");
    debug_println("  Examples:");
    debug_println("    set holding-reg STATIC 100 Value 42");
    debug_println("    set holding-reg STATIC 100 Value int -500");
    debug_println("    set holding-reg STATIC 100 Value dint 100000");
    debug_println("    set holding-reg STATIC 100 Value real 3.14");
    return;
  }

  // Parse address
  uint16_t address = atoi(argv[0]);
  if (address >= HOLDING_REGS_SIZE) {
    debug_print("SET HOLDING-REG STATIC: address out of range (max ");
    debug_print_uint(HOLDING_REGS_SIZE - 1);
    debug_println(")");
    return;
  }

  // BUG-142 FIX: Validate address against actual ST Logic reserved range (HR200-237)
  // ST Logic uses: HR200-203 (control), HR204-235 (var inputs), HR236-237 (exec interval)
  // HR238-299 are available for STATIC registers
  if (address >= 200 && address < 238) {
    debug_println("SET HOLDING-REG STATIC: ERROR - Address 200-237 reserved for ST Logic system");
    debug_println("  HR200-203: Logic control registers");
    debug_println("  HR204-235: Logic variable inputs");
    debug_println("  HR236-237: Execution interval");
    debug_println("  Use addresses 0-199 or 238+ for STATIC registers");
    return;
  }

  // argv[1] should be "Value"
  if (strcmp(argv[1], "Value") != 0) {
    debug_println("SET HOLDING-REG STATIC: expected 'Value' keyword");
    return;
  }

  // Determine if type is specified (argc == 4) or not (argc == 3)
  ModbusValueType value_type = MODBUS_TYPE_UINT;  // Default
  const char* value_str = NULL;

  if (argc == 3) {
    // Legacy syntax: set reg STATIC <addr> Value <value>
    value_type = MODBUS_TYPE_UINT;
    value_str = argv[2];
  } else if (argc >= 4) {
    // New syntax: set reg STATIC <addr> Value <type> <value>
    const char* type_str = argv[2];
    value_str = argv[3];

    if (!strcmp(type_str, "uint")) {
      value_type = MODBUS_TYPE_UINT;
    } else if (!strcmp(type_str, "int")) {
      value_type = MODBUS_TYPE_INT;
    } else if (!strcmp(type_str, "dint")) {
      value_type = MODBUS_TYPE_DINT;
    } else if (!strcmp(type_str, "dword")) {
      value_type = MODBUS_TYPE_DWORD;
    } else if (!strcmp(type_str, "real")) {
      value_type = MODBUS_TYPE_REAL;
    } else {
      debug_println("SET HOLDING-REG STATIC: invalid type");
      debug_println("  Valid types: uint, int, dint, dword, real");
      return;
    }
  }

  // Validate address range for multi-register types
  if ((value_type == MODBUS_TYPE_DINT || value_type == MODBUS_TYPE_DWORD || value_type == MODBUS_TYPE_REAL)) {
    if (address + 1 >= HOLDING_REGS_SIZE) {
      debug_print("SET HOLDING-REG STATIC: type ");
      if (value_type == MODBUS_TYPE_DINT) debug_print("dint");
      else if (value_type == MODBUS_TYPE_DWORD) debug_print("dword");
      else debug_print("real");
      debug_print(" requires 2 registers, address ");
      debug_print_uint(address);
      debug_print(" out of range (max ");
      debug_print_uint(HOLDING_REGS_SIZE - 2);
      debug_println(")");
      return;
    }

    // BUG-142 FIX: Check if second register crosses into ST Logic reserved range (HR200-237)
    if ((address < 200 && address + 1 >= 200) || (address >= 200 && address < 238)) {
      debug_println("SET HOLDING-REG STATIC: ERROR - Multi-register type crosses into ST Logic reserved range (200-237)");
      debug_println("  Use addresses 0-198 or 238+ for 2-register types (DINT/DWORD/REAL)");
      return;
    }
  }

  // Parse and write value based on type
  StaticRegisterMapping mapping;
  mapping.register_address = address;
  mapping.value_type = value_type;
  mapping.reserved = 0;

  switch (value_type) {
    case MODBUS_TYPE_UINT: {
      int32_t temp = atoi(value_str);
      if (temp < 0 || temp > 65535) {
        debug_println("SET HOLDING-REG STATIC: uint value must be 0-65535");
        return;
      }
      mapping.value_16 = (uint16_t)temp;
      registers_set_holding_register(address, mapping.value_16);
      break;
    }

    case MODBUS_TYPE_INT: {
      int32_t temp = atoi(value_str);
      if (temp < -32768 || temp > 32767) {
        debug_println("SET HOLDING-REG STATIC: int value must be -32768 to 32767");
        return;
      }
      int16_t signed_val = (int16_t)temp;
      mapping.value_16 = (uint16_t)signed_val;  // Two's complement
      registers_set_holding_register(address, mapping.value_16);
      break;
    }

    case MODBUS_TYPE_DINT: {
      int32_t dint_val = atol(value_str);
      mapping.value_32 = (uint32_t)dint_val;
      uint16_t low_word = (uint16_t)(mapping.value_32 & 0xFFFF);
      uint16_t high_word = (uint16_t)((mapping.value_32 >> 16) & 0xFFFF);
      registers_set_holding_register(address, low_word);
      registers_set_holding_register(address + 1, high_word);
      break;
    }

    case MODBUS_TYPE_DWORD: {
      uint32_t dword_val = strtoul(value_str, NULL, 10);
      mapping.value_32 = dword_val;
      uint16_t low_word = (uint16_t)(mapping.value_32 & 0xFFFF);
      uint16_t high_word = (uint16_t)((mapping.value_32 >> 16) & 0xFFFF);
      registers_set_holding_register(address, low_word);
      registers_set_holding_register(address + 1, high_word);
      break;
    }

    case MODBUS_TYPE_REAL: {
      float real_val = atof(value_str);
      mapping.value_real = real_val;
      uint32_t bits;
      memcpy(&bits, &real_val, sizeof(float));
      uint16_t low_word = (uint16_t)(bits & 0xFFFF);
      uint16_t high_word = (uint16_t)((bits >> 16) & 0xFFFF);
      registers_set_holding_register(address, low_word);
      registers_set_holding_register(address + 1, high_word);
      break;
    }

    default:
      debug_println("SET HOLDING-REG STATIC: unknown type");
      return;
  }

  // Store in config for persistence
  uint8_t found = 0;
  for (uint8_t i = 0; i < g_persist_config.static_reg_count; i++) {
    if (g_persist_config.static_regs[i].register_address == address) {
      g_persist_config.static_regs[i] = mapping;
      found = 1;
      break;
    }
  }

  if (!found) {
    if (g_persist_config.static_reg_count >= MAX_DYNAMIC_REGS) {
      debug_println("SET HOLDING-REG STATIC: max STATIC registers reached");
      return;
    }

    g_persist_config.static_regs[g_persist_config.static_reg_count] = mapping;
    g_persist_config.static_reg_count++;
  }

  // Display confirmation
  debug_print("Register ");
  debug_print_uint(address);
  if (value_type == MODBUS_TYPE_DINT || value_type == MODBUS_TYPE_DWORD || value_type == MODBUS_TYPE_REAL) {
    debug_print("-");
    debug_print_uint(address + 1);
  }
  debug_print(" STATIC = ");
  debug_print(value_str);
  debug_print(" (");
  if (value_type == MODBUS_TYPE_UINT) debug_print("uint");
  else if (value_type == MODBUS_TYPE_INT) debug_print("int");
  else if (value_type == MODBUS_TYPE_DINT) debug_print("dint");
  else if (value_type == MODBUS_TYPE_DWORD) debug_print("dword");
  else if (value_type == MODBUS_TYPE_REAL) debug_print("real");
  debug_println(")");
}

/**
 * set holding-reg DYNAMIC <address> counter<id>:<function>
 * set holding-reg DYNAMIC <address> timer<id>:<function>
 *
 * Counter functions: index, raw, freq, overflow, ctrl
 * Timer functions: output
 *
 * Examples:
 *   set holding-reg DYNAMIC 100 counter1:index
 *   set holding-reg DYNAMIC 101 counter1:raw
 *   set holding-reg DYNAMIC 102 counter1:freq
 *   set holding-reg DYNAMIC 103 counter1:overflow
 *   set holding-reg DYNAMIC 150 timer2:output
 */
void cli_cmd_set_reg_dynamic(uint8_t argc, char* argv[]) {
  // set holding-reg DYNAMIC <address> counter<id>:<function> or timer<id>:<function>

  if (argc < 2) {
    debug_println("SET HOLDING-REG DYNAMIC: missing arguments");
    debug_println("  Usage: set holding-reg DYNAMIC <address> counter<id>:<function> or timer<id>:<function>");
    debug_println("  Counter functions: index, raw, freq, overflow, ctrl");
    debug_println("  Timer functions: output");
    return;
  }

  // Parse address
  uint16_t address = atoi(argv[0]);
  if (address >= HOLDING_REGS_SIZE) {
    debug_print("SET HOLDING-REG DYNAMIC: address out of range (max ");
    debug_print_uint(HOLDING_REGS_SIZE - 1);
    debug_println(")");
    return;
  }

  // Parse source:function
  const char* source_str = argv[1];
  char* colon = strchr(source_str, ':');

  if (!colon) {
    debug_println("SET HOLDING-REG DYNAMIC: invalid format (expected counter<id>:<func> or timer<id>:<func>)");
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
      debug_println("SET HOLDING-REG DYNAMIC: invalid counter ID (must be 1-4)");
      return;
    }
  } else if (strncmp(source_copy, "timer", 5) == 0) {
    source_type = DYNAMIC_SOURCE_TIMER;
    source_id = atoi(source_copy + 5);
    if (source_id < 1 || source_id > 4) {
      debug_println("SET HOLDING-REG DYNAMIC: invalid timer ID (must be 1-4)");
      return;
    }
  } else {
    debug_println("SET HOLDING-REG DYNAMIC: invalid source (must be counter<id> or timer<id>)");
    return;
  }

  // Parse function
  uint8_t source_function = 0xff;

  if (source_type == DYNAMIC_SOURCE_COUNTER) {
    if (!strcmp(function_str, "index")) {
      source_function = COUNTER_FUNC_INDEX;
    } else if (!strcmp(function_str, "raw")) {
      source_function = COUNTER_FUNC_RAW;
    } else if (!strcmp(function_str, "freq")) {
      source_function = COUNTER_FUNC_FREQ;
    } else if (!strcmp(function_str, "overflow")) {
      source_function = COUNTER_FUNC_OVERFLOW;
    } else if (!strcmp(function_str, "ctrl")) {
      source_function = COUNTER_FUNC_CTRL;
    } else {
      debug_println("SET HOLDING-REG DYNAMIC: invalid counter function");
      debug_println("  Valid: index, raw, freq, overflow, ctrl");
      return;
    }
  } else if (source_type == DYNAMIC_SOURCE_TIMER) {
    if (!strcmp(function_str, "output")) {
      source_function = TIMER_FUNC_OUTPUT;
    } else {
      debug_println("SET HOLDING-REG DYNAMIC: invalid timer function");
      debug_println("  Valid: output");
      return;
    }
  }

  // Add or update DYNAMIC register mapping
  uint8_t found = 0;
  for (uint8_t i = 0; i < g_persist_config.dynamic_reg_count; i++) {
    if (g_persist_config.dynamic_regs[i].register_address == address) {
      g_persist_config.dynamic_regs[i].source_type = source_type;
      g_persist_config.dynamic_regs[i].source_id = source_id;
      g_persist_config.dynamic_regs[i].source_function = source_function;
      found = 1;
      break;
    }
  }

  if (!found) {
    if (g_persist_config.dynamic_reg_count >= MAX_DYNAMIC_REGS) {
      debug_println("SET HOLDING-REG DYNAMIC: max DYNAMIC registers reached");
      return;
    }

    uint8_t idx = g_persist_config.dynamic_reg_count;
    g_persist_config.dynamic_regs[idx].register_address = address;
    g_persist_config.dynamic_regs[idx].source_type = source_type;
    g_persist_config.dynamic_regs[idx].source_id = source_id;
    g_persist_config.dynamic_regs[idx].source_function = source_function;
    g_persist_config.dynamic_reg_count++;
  }

  debug_print("Register ");
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

/**
 * show reg - Display all STATIC and DYNAMIC register mappings
 */
void cli_cmd_show_regs(void) {
  debug_println("[regs]");

  // Show STATIC registers
  if (g_persist_config.static_reg_count > 0) {
    debug_println("# STATIC registers");
    for (uint8_t i = 0; i < g_persist_config.static_reg_count; i++) {
      const StaticRegisterMapping* map = &g_persist_config.static_regs[i];
      debug_print("set holding-reg STATIC ");
      debug_print_uint(map->register_address);
      debug_print(" Value ");

      // Print type
      if (map->value_type == MODBUS_TYPE_UINT) {
        debug_print("uint ");
        debug_print_uint(map->value_16);
      } else if (map->value_type == MODBUS_TYPE_INT) {
        debug_print("int ");
        int16_t signed_val = (int16_t)map->value_16;
        char str[16];
        snprintf(str, sizeof(str), "%d", signed_val);
        debug_print(str);
      } else if (map->value_type == MODBUS_TYPE_DINT) {
        debug_print("dint ");
        int32_t dint_val = (int32_t)map->value_32;
        char str[16];
        snprintf(str, sizeof(str), "%" PRId32, dint_val);  // BUG-148 FIX: Use PRId32 for portable int32_t formatting
        debug_print(str);
      } else if (map->value_type == MODBUS_TYPE_DWORD) {
        debug_print("dword ");
        debug_print_uint(map->value_32);
      } else if (map->value_type == MODBUS_TYPE_REAL) {
        debug_print("real ");
        char str[16];
        snprintf(str, sizeof(str), "%.2f", map->value_real);
        debug_print(str);
      }

      debug_println("");
    }
  }

  // Show DYNAMIC registers
  if (g_persist_config.dynamic_reg_count > 0) {
    debug_println("# DYNAMIC registers");
    for (uint8_t i = 0; i < g_persist_config.dynamic_reg_count; i++) {
      const DynamicRegisterMapping* dyn = &g_persist_config.dynamic_regs[i];
      debug_print("reg DYNAMIC ");
      debug_print_uint(dyn->register_address);
      debug_print(" ");

      if (dyn->source_type == DYNAMIC_SOURCE_COUNTER) {
        debug_print("counter");
        debug_print_uint(dyn->source_id);
        debug_print(":");

        switch (dyn->source_function) {
          case COUNTER_FUNC_INDEX:
            debug_print("index");
            break;
          case COUNTER_FUNC_RAW:
            debug_print("raw");
            break;
          case COUNTER_FUNC_FREQ:
            debug_print("freq");
            break;
          case COUNTER_FUNC_OVERFLOW:
            debug_print("overflow");
            break;
          case COUNTER_FUNC_CTRL:
            debug_print("ctrl");
            break;
          default:
            debug_print("?");
        }
      } else if (dyn->source_type == DYNAMIC_SOURCE_TIMER) {
        debug_print("timer");
        debug_print_uint(dyn->source_id);
        debug_print(":");

        switch (dyn->source_function) {
          case TIMER_FUNC_OUTPUT:
            debug_print("output");
            break;
          default:
            debug_print("?");
        }
      }

      debug_println("");
    }
  }

  if (g_persist_config.static_reg_count == 0 && g_persist_config.dynamic_reg_count == 0) {
    debug_println("# No registers configured");
  }
}
