/**
 * @file config_apply.cpp
 * @brief Configuration apply - activate config in system
 *
 * LAYER 6: Persistence
 * Responsibility: Apply loaded configuration to running system
 */

#include "config_apply.h"
#include "counter_engine.h"
#include "timer_engine.h"
#include "gpio_driver.h"
#include "config_struct.h"
#include "registers.h"
#include "registers_persist.h"
#include "heartbeat.h"
#include "cli_shell.h"
#include "st_logic_config.h"
#include "debug.h"
#include <esp_wifi.h>
#include <cstddef>
#include <cstring>
#include <cstdio>

bool config_apply(const PersistConfig* cfg) {
  if (cfg == NULL) return false;

  debug_println("CONFIG APPLY: Applying configuration to system");

  // Display Modbus slave ID (NOTE: already set in main.cpp via modbus_server_init())
  debug_print("  Slave ID: ");
  debug_print_uint(cfg->modbus_slave.slave_id);
  debug_println("");

  // Apply baudrate (NOTE: requires reboot for UART reinit)
  debug_print("  Baudrate: ");
  debug_print_uint(cfg->modbus_slave.baudrate);
  debug_println(" (requires reboot)");

  // Apply GPIO2 heartbeat configuration
  debug_print("  GPIO2 user mode: ");
  debug_print_uint(cfg->gpio2_user_mode);
  debug_println("");
  if (cfg->gpio2_user_mode) {
    heartbeat_disable();
    debug_println("    Heartbeat disabled (GPIO2 available for user)");
  } else {
    heartbeat_enable();
    debug_println("    Heartbeat enabled (GPIO2 LED blink)");
  }

  // Apply remote echo configuration
  debug_print("  Remote echo: ");
  debug_println(cfg->remote_echo ? "ON" : "OFF");
  cli_shell_set_remote_echo(cfg->remote_echo);

  // Apply variable mappings (initialize GPIO pins)
  debug_print("  Variable mappings (GPIO + ST): ");
  debug_print_uint(cfg->var_map_count);
  debug_println("");

  for (uint8_t i = 0; i < cfg->var_map_count; i++) {
    const VariableMapping* map = &cfg->var_maps[i];

    // Handle GPIO mappings
    if (map->source_type == MAPPING_SOURCE_GPIO) {
      // Skip mappings associated with counters/timers (they will be initialized by their engines)
      if (map->associated_counter != 0xff || map->associated_timer != 0xff) {
        continue;
      }

      // Initialize GPIO pin direction for STATIC mappings
      if (map->is_input) {
        gpio_set_direction(map->gpio_pin, GPIO_INPUT);
        debug_print("    GPIO");
        debug_print_uint(map->gpio_pin);
        debug_print(" - INPUT:");
        debug_print_uint(map->input_reg);
        debug_println("");
      } else {
        gpio_set_direction(map->gpio_pin, GPIO_OUTPUT);
        debug_print("    GPIO");
        debug_print_uint(map->gpio_pin);
        debug_print(" - COIL:");
        debug_print_uint(map->coil_reg);
        debug_println("");
      }
    }
    // Handle ST variable mappings (no initialization needed, just logging)
    else if (map->source_type == MAPPING_SOURCE_ST_VAR) {
      // BUG-142 FIX: Validate ST program exists before logging binding
      extern st_logic_engine_state_t* st_logic_get_state(void);
      st_logic_engine_state_t* logic_state = st_logic_get_state();
      bool program_exists = false;
      if (logic_state && map->st_program_id < ST_LOGIC_MAX_PROGRAMS) {
        st_logic_program_config_t* prog = &logic_state->programs[map->st_program_id];
        program_exists = (prog->source_size > 0);  // Program has code uploaded
      }

      debug_print("    Logic");
      debug_print_uint(map->st_program_id + 1);
      debug_print(": var[");
      debug_print_uint(map->st_var_index);
      debug_print("] ");
      debug_print(map->is_input ? "<- HR#" : "-> HR#");
      debug_print_uint(map->is_input ? map->input_reg : map->coil_reg);

      if (!program_exists) {
        debug_print(" [WARNING: program not loaded/deleted]");
      }
      debug_println("");
    }
  }

  // Apply STATIC register mappings (initialize holding register values)
  debug_print("  STATIC registers: ");
  debug_print_uint(cfg->static_reg_count);
  debug_println("");
  for (uint8_t i = 0; i < cfg->static_reg_count; i++) {
    const StaticRegisterMapping* map = &cfg->static_regs[i];
    uint16_t addr = map->register_address;

    switch (map->value_type) {
      case MODBUS_TYPE_UINT:
      case MODBUS_TYPE_INT:
        // 16-bit value (1 register)
        registers_set_holding_register(addr, map->value_16);
        debug_print("    Reg[");
        debug_print_uint(addr);
        debug_print("] = ");
        debug_print_uint(map->value_16);
        debug_print(" (");
        debug_print(map->value_type == MODBUS_TYPE_UINT ? "uint" : "int");
        debug_println(")");
        break;

      case MODBUS_TYPE_DINT:
      case MODBUS_TYPE_DWORD: {
        // 32-bit value (2 registers)
        uint16_t low_word = (uint16_t)(map->value_32 & 0xFFFF);
        uint16_t high_word = (uint16_t)((map->value_32 >> 16) & 0xFFFF);
        registers_set_holding_register(addr, low_word);
        registers_set_holding_register(addr + 1, high_word);
        debug_print("    Reg[");
        debug_print_uint(addr);
        debug_print("-");
        debug_print_uint(addr + 1);
        debug_print("] = ");
        debug_print_uint(map->value_32);
        debug_print(" (");
        debug_print(map->value_type == MODBUS_TYPE_DINT ? "dint" : "dword");
        debug_println(")");
        break;
      }

      case MODBUS_TYPE_REAL: {
        // 32-bit float (2 registers)
        uint32_t bits;
        memcpy(&bits, &map->value_real, sizeof(float));
        uint16_t low_word = (uint16_t)(bits & 0xFFFF);
        uint16_t high_word = (uint16_t)((bits >> 16) & 0xFFFF);
        registers_set_holding_register(addr, low_word);
        registers_set_holding_register(addr + 1, high_word);
        debug_print("    Reg[");
        debug_print_uint(addr);
        debug_print("-");
        debug_print_uint(addr + 1);
        debug_print("] = ");

        // Print float with 2 decimal places
        char float_str[16];
        snprintf(float_str, sizeof(float_str), "%.2f", map->value_real);
        debug_print(float_str);
        debug_println(" (real)");
        break;
      }

      default:
        debug_print("    Reg[");
        debug_print_uint(addr);
        debug_println("] = UNKNOWN TYPE!");
        break;
    }
  }

  // Apply STATIC coil mappings (initialize coil values)
  debug_print("  STATIC coils: ");
  debug_print_uint(cfg->static_coil_count);
  debug_println("");
  for (uint8_t i = 0; i < cfg->static_coil_count; i++) {
    registers_set_coil(
      cfg->static_coils[i].coil_address,
      cfg->static_coils[i].static_value
    );
    debug_print("    Coil[");
    debug_print_uint(cfg->static_coils[i].coil_address);
    debug_print("] = ");
    debug_print_uint(cfg->static_coils[i].static_value);
    debug_println("");
  }

  // Apply DYNAMIC register mappings (info only, actual updates happen in registers.cpp loop)
  debug_print("  DYNAMIC registers: ");
  debug_print_uint(cfg->dynamic_reg_count);
  debug_println("");

  // Apply DYNAMIC coil mappings (info only, actual updates happen in registers.cpp loop)
  debug_print("  DYNAMIC coils: ");
  debug_print_uint(cfg->dynamic_coil_count);
  debug_println("");

  // Apply counter configs
  debug_println("  Counters:");
  for (uint8_t i = 0; i < COUNTER_COUNT; i++) {
    if (cfg->counters[i].enabled) {
      debug_print("    Counter ");
      debug_print_uint(i + 1);
      debug_println(" enabled - configuring...");
      counter_engine_configure(i + 1, &cfg->counters[i]);

      // BUG-017 FIX: Check auto-start flag and trigger start if enabled
      if (cfg->counters[i].ctrl_reg < HOLDING_REGS_SIZE) {
        uint16_t ctrl_val = registers_get_holding_register(cfg->counters[i].ctrl_reg);
        if (ctrl_val & 0x0002) {  // Bit 1 = auto-start
          debug_print("    Counter ");
          debug_print_uint(i + 1);
          debug_println(" auto-start enabled - starting...");
          // Trigger start command (will be processed in next loop iteration)
          registers_set_holding_register(cfg->counters[i].ctrl_reg, ctrl_val | 0x0002);
        }
      }
    }
  }

  // Apply timer configs
  debug_println("  Timers:");
  for (uint8_t i = 0; i < TIMER_COUNT; i++) {
    if (cfg->timers[i].enabled) {
      debug_print("    Timer ");
      debug_print_uint(i + 1);
      debug_println(" enabled - configured");
      timer_engine_configure(i + 1, &cfg->timers[i]);
    }
  }

  // Apply ST Logic configuration (v4.1+)
  // BUG-014 FIX: Load execution interval from persistent config
  debug_print("  ST Logic execution interval: ");
  debug_print_uint(cfg->st_logic_interval_ms);
  debug_println("ms");
  if (cfg->st_logic_interval_ms >= 2 && cfg->st_logic_interval_ms <= 100) {
    st_logic_engine_state_t *st_state = st_logic_get_state();
    if (st_state) {
      st_state->execution_interval_ms = cfg->st_logic_interval_ms;
    }
  }

  // Apply persistent register groups (v4.0+)
  if (cfg->persist_regs.enabled && cfg->persist_regs.group_count > 0) {
    debug_print("  Persistent registers: ");
    debug_print_uint(cfg->persist_regs.group_count);
    debug_println(" groups");

    // Restore all group register values
    registers_persist_restore_all_groups();
  }

  // Apply Wi-Fi power save setting (v6.0.4+)
  wifi_ps_type_t ps_mode = cfg->network.wifi_power_save ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE;
  esp_wifi_set_ps(ps_mode);
  debug_print("  Wi-Fi power save: ");
  debug_println(cfg->network.wifi_power_save ? "ON (min modem)" : "OFF (low latency)");

  // Apply Ethernet configuration (v6.1.0+)
  debug_print("  Ethernet: ");
  debug_println(cfg->network.ethernet.enabled ? "ENABLED" : "DISABLED");

  debug_println("CONFIG APPLY: Done");
  return true;
}
