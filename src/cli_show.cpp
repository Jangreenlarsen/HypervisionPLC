/**
 * @file cli_show.cpp
 * @brief CLI `show` command handlers (LAYER 7)
 *
 * Ported from: Mega2560 v3.6.5 cli_shell.cpp (show command handlers)
 * Adapted to: ESP32 modular architecture
 *
 * Responsibility:
 * - Retrieve system state (config, status, register values)
 * - Format for terminal display (tables, lists, etc.)
 * - Print via debug_println()
 *
 * Context: Formatting optimized for 80-char serial terminals
 */

#include "cli_show.h"
#include "counter_engine.h"
#include "counter_config.h"
#include "counter_frequency.h"
#include "timer_engine.h"
#include "timer_config.h"
#include "registers.h"
#include "registers_persist.h"
#include "watchdog_monitor.h"
#include "version.h"
#include "cli_shell.h"
#include "config_struct.h"
#include "st_logic_config.h"
#include "network_manager.h"
#include "network_config.h"
#include "ethernet_driver.h"
#include "http_server.h"
#include "https_wrapper.h"
#include <WiFi.h>
#include "debug_flags.h"
#include "debug.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * SHOW CONFIG
 * ============================================================================ */

// Helper: check if section should be shown based on filter
static bool show_section_match(const char *filter, const char *section_name) {
  if (!filter || filter[0] == '\0') return true;  // No filter = show all
  // Case-insensitive comparison
  const char *f = filter;
  const char *s = section_name;
  while (*f && *s) {
    char cf = *f, cs = *s;
    if (cf >= 'a' && cf <= 'z') cf -= 32;
    if (cs >= 'a' && cs <= 'z') cs -= 32;
    if (cf != cs) return false;
    f++; s++;
  }
  return (*f == '\0');  // Filter fully matched (section_name can be longer)
}

void cli_cmd_show_config(const char *section) {
  // Section filter: NULL/"" = all, "WIFI"/"NETWORK"/"MODBUS"/"COUNTERS"/"TIMERS"/"GPIO"/"HTTP"/"MODULES"/"PERSIST"/"SYSTEM"
  bool show_all = (!section || section[0] == '\0');
  bool show_system   = show_all || show_section_match(section, "SYSTEM");
  bool show_modbus   = show_all || show_section_match(section, "MODBUS");
  bool show_counters = show_all || show_section_match(section, "COUNTER");
  bool show_timers   = show_all || show_section_match(section, "TIMER");
  bool show_gpio     = show_all || show_section_match(section, "GPIO");
  bool show_network  = show_all || show_section_match(section, "NETWORK") || show_section_match(section, "WIFI");
  bool show_ethernet = show_all || show_section_match(section, "ETHERNET") || show_section_match(section, "ETH");
  bool show_http     = show_all || show_section_match(section, "HTTP") || show_section_match(section, "API");
  bool show_modules  = show_all || show_section_match(section, "MODULE");
  bool show_persist  = show_all || show_section_match(section, "PERSIST");
  bool show_logic    = show_all || show_section_match(section, "LOGIC") || show_section_match(section, "ST");
  bool show_commands = true;  // Always show SET commands (filtered by section)

  if (show_all) {
    debug_println("\n=== CONFIGURATION ===\n");
  } else {
    debug_print("\n=== CONFIGURATION [");
    debug_print(section);
    debug_println("] ===\n");
  }

  // =========================================================================
  // SYSTEM INFO
  // =========================================================================
  if (show_system) {
  debug_println("[SYSTEM]");
  debug_print("Version: ");
  debug_print(PROJECT_VERSION);
  debug_print(" Build #");
  debug_print_uint(BUILD_NUMBER);
  debug_println("");
  debug_print("Built: ");
  debug_print(BUILD_TIMESTAMP);
  debug_print(" (");
  debug_print(GIT_BRANCH);
  debug_print("@");
  debug_print(GIT_HASH);
  debug_println(")");

  debug_print("Hostname: ");
  debug_println(g_persist_config.hostname[0] ? g_persist_config.hostname : "(NOT SET)");
  debug_println("");
  } // end show_system

  // =========================================================================
  // MODBUS
  // =========================================================================
  if (show_modbus) {
  debug_println("[MODBUS]");
  debug_print("Slave: ");
  debug_println(g_persist_config.modbus_slave.enabled ? "ENABLED" : "DISABLED");
  debug_print("  Unit-ID: ");
  debug_print_uint(g_persist_config.modbus_slave.slave_id);
  debug_println("");
  debug_print("  Baud: ");
  debug_print_uint(g_persist_config.modbus_slave.baudrate);
  debug_println("");
  debug_print("  Parity: ");
  if (g_persist_config.modbus_slave.parity == 0) debug_println("NONE");
  else if (g_persist_config.modbus_slave.parity == 1) debug_println("EVEN");
  else if (g_persist_config.modbus_slave.parity == 2) debug_println("ODD");
  else debug_println("UNKNOWN");
  debug_print("  Stop Bits: ");
  debug_print_uint(g_persist_config.modbus_slave.stop_bits);
  debug_println("");
  debug_print("  Inter-frame Delay: ");
  debug_print_uint(g_persist_config.modbus_slave.inter_frame_delay);
  debug_println(" ms");

  debug_print("Master: ");
  debug_println(g_persist_config.modbus_master.enabled ? "ENABLED" : "DISABLED");
  if (g_persist_config.modbus_master.enabled) {
    debug_print("  Baudrate: ");
    debug_print_uint(g_persist_config.modbus_master.baudrate);
    debug_println("");
    debug_print("  Parity: ");
    if (g_persist_config.modbus_master.parity == 0) debug_println("NONE");
    else if (g_persist_config.modbus_master.parity == 1) debug_println("EVEN");
    else if (g_persist_config.modbus_master.parity == 2) debug_println("ODD");
    else debug_println("UNKNOWN");
    debug_print("  Stop Bits: ");
    debug_print_uint(g_persist_config.modbus_master.stop_bits);
    debug_println("");
    debug_print("  Timeout: ");
    debug_print_uint(g_persist_config.modbus_master.timeout_ms);
    debug_println(" ms");
    debug_print("  Inter-frame Delay: ");
    debug_print_uint(g_persist_config.modbus_master.inter_frame_delay);
    debug_println(" ms");
    debug_print("  Max Requests/Cycle: ");
    debug_print_uint(g_persist_config.modbus_master.max_requests_per_cycle);
    debug_println("");
  }
  debug_println("");
  } // end show_modbus

  // =========================================================================
  // COUNTERS
  // =========================================================================
  if (show_counters) {
  debug_println("[COUNTERS]");
  bool any_counter = false;
  for (uint8_t id = 1; id <= 4; id++) {
    CounterConfig cfg;
    if (counter_config_get(id, &cfg) && cfg.enabled) {
      any_counter = true;
      debug_print("  counter ");
      debug_print_uint(id);
      debug_print(": enabled");

      // edge mode
      const char* edge_str = "rising";
      if (cfg.edge_type == COUNTER_EDGE_FALLING) edge_str = "falling";
      else if (cfg.edge_type == COUNTER_EDGE_BOTH) edge_str = "both";
      debug_print(" edge:");
      debug_print(edge_str);

      // prescaler and resolution
      debug_print(" prescaler:");
      debug_print_uint(cfg.prescaler);
      debug_print(" res:");
      debug_print_uint(cfg.bit_width);

      // direction
      const char* dir_str = (cfg.direction == COUNTER_DIR_DOWN) ? "down" : "up";
      debug_print(" dir:");
      debug_print(dir_str);

      // scale (format as float)
      debug_print(" scale:");
      debug_print_float(cfg.scale_factor);

      // Register indices
      debug_print(" input-dis:");
      debug_print_uint(cfg.input_dis);
      debug_print(" value-reg:");
      debug_print_uint(cfg.value_reg);

      if (cfg.raw_reg > 0) {
        debug_print(" raw-reg:");
        debug_print_uint(cfg.raw_reg);
      }
      if (cfg.freq_reg > 0) {
        debug_print(" freq-reg:");
        debug_print_uint(cfg.freq_reg);
      }

      debug_print(" ctrl-reg:");
      if (cfg.ctrl_reg < 1000) debug_print_uint(cfg.ctrl_reg);
      else debug_print("n/a");

      // start value (BUG-183 FIX: use 64-bit print)
      debug_print(" start:");
      debug_print_ulong(cfg.start_value);

      // debounce
      debug_print(" debounce:");
      if (cfg.debounce_enabled && cfg.debounce_ms > 0) {
        debug_print("on/");
        debug_print_uint(cfg.debounce_ms);
        debug_print("ms");
      } else {
        debug_print("off");
      }

      // hw-mode
      debug_print(" hw-mode:");
      if (cfg.hw_mode == COUNTER_HW_SW) {
        if (cfg.interrupt_pin > 0) {
          debug_print("sw-isr");
        } else {
          debug_print("sw");
        }
      } else if (cfg.hw_mode == COUNTER_HW_SW_ISR) {
        debug_print("sw-isr");
      } else if (cfg.hw_mode == COUNTER_HW_PCNT) {
        debug_print("hw");
      } else {
        debug_print("unknown");
      }

      // interrupt pin for ISR mode
      if ((cfg.hw_mode == COUNTER_HW_SW || cfg.hw_mode == COUNTER_HW_SW_ISR) && cfg.interrupt_pin > 0) {
        debug_print(" interrupt-pin:");
        debug_print_uint(cfg.interrupt_pin);
      }

      debug_println("");
    }
  }
  if (!any_counter) {
    debug_println("  (none configured)");
  }

  // Counter control block (if any enabled)
  bool show_control = false;
  for (uint8_t id = 1; id <= 4; id++) {
    CounterConfig cfg;
    if (counter_config_get(id, &cfg) && cfg.enabled) {
      show_control = true;
      break;
    }
  }

  if (show_control) {
    debug_println("counters control");
    for (uint8_t id = 1; id <= 4; id++) {
      CounterConfig cfg;
      if (counter_config_get(id, &cfg) && cfg.enabled) {
        debug_print("  counter");
        debug_print_uint(id);
        debug_print(" reset-on-read disabled auto-start disabled");
        debug_println("");
      }
    }
  }

  } // end show_counters

  // =========================================================================
  // TIMERS
  // =========================================================================
  if (show_timers) {
  debug_println("\n[TIMERS]");
  bool any_timer = false;
  for (uint8_t id = 1; id <= 4; id++) {
    TimerConfig cfg;
    if (timer_engine_get_config(id, &cfg) && cfg.enabled) {
      any_timer = true;
      debug_print("  timer ");
      debug_print_uint(id);
      debug_print(" mode:");
      debug_print_uint(cfg.mode);

      switch (cfg.mode) {
        case TIMER_MODE_1_ONESHOT:
          debug_print(" p1-dur:");
          debug_print_uint(cfg.phase1_duration_ms);
          debug_print(" p1-out:");
          debug_print_uint(cfg.phase1_output_state);
          debug_print(" p2-dur:");
          debug_print_uint(cfg.phase2_duration_ms);
          debug_print(" p2-out:");
          debug_print_uint(cfg.phase2_output_state);
          debug_print(" p3-dur:");
          debug_print_uint(cfg.phase3_duration_ms);
          debug_print(" p3-out:");
          debug_print_uint(cfg.phase3_output_state);
          break;
        case TIMER_MODE_2_MONOSTABLE:
          debug_print(" pulse-ms:");
          debug_print_uint(cfg.pulse_duration_ms);
          debug_print(" p1-out:");
          debug_print_uint(cfg.phase1_output_state);
          debug_print(" p2-out:");
          debug_print_uint(cfg.phase2_output_state);
          break;
        case TIMER_MODE_3_ASTABLE:
          debug_print(" on-ms:");
          debug_print_uint(cfg.on_duration_ms);
          debug_print(" off-ms:");
          debug_print_uint(cfg.off_duration_ms);
          debug_print(" p1-out:");
          debug_print_uint(cfg.phase1_output_state);
          debug_print(" p2-out:");
          debug_print_uint(cfg.phase2_output_state);
          break;
        case TIMER_MODE_4_INPUT_TRIGGERED:
          debug_print(" input-dis:");
          debug_print_uint(cfg.input_dis);
          debug_print(" trigger-edge:");
          debug_print_uint(cfg.trigger_edge);
          debug_print(" delay-ms:");
          debug_print_uint(cfg.delay_ms);
          debug_print(" out:");
          debug_print_uint(cfg.phase1_output_state);
          break;
        default:
          debug_print(" UNKNOWN");
      }

      debug_print(" output-coil:");
      debug_print_uint(cfg.output_coil);
      debug_print(" ctrl-reg:");
      debug_print_uint(cfg.ctrl_reg);
      debug_println("");
    }
  }
  if (!any_timer) {
    debug_println("  (none configured)");
  }
  } // end show_timers

  // =========================================================================
  // GPIO
  // =========================================================================
  if (show_gpio) {
  debug_println("\n[GPIO]");

  // Get ST Logic state (needed for ST variable name lookup in GPIO mappings)
  st_logic_engine_state_t *st_state = st_logic_get_state();

  // GPIO2 status (heartbeat control)
  debug_print("  gpio2: ");
  if (g_persist_config.gpio2_user_mode == 0) {
    debug_println("heartbeat (led blink)");
  } else {
    debug_println("user mode");
  }
  debug_println("");

  debug_println("hardware pins:");
  debug_println("  GPIO 2  - LED (heartbeat/user)");
  if (g_persist_config.modbus_slave.enabled) {
    char buf[64];
    snprintf(buf, sizeof(buf), "  GPIO %-2d - UART1 RX (Modbus Slave)", PIN_UART1_RX);
    debug_println(buf);
    snprintf(buf, sizeof(buf), "  GPIO %-2d - UART1 TX (Modbus Slave)", PIN_UART1_TX);
    debug_println(buf);
    snprintf(buf, sizeof(buf), "  GPIO %-2d - RS485 DIR (Modbus Slave)", PIN_RS485_DIR);
    debug_println(buf);
  }
  if (g_persist_config.modbus_master.enabled) {
    char buf[64];
    snprintf(buf, sizeof(buf), "  GPIO %-2d - UART TX (Modbus Master)", MODBUS_MASTER_TX_PIN);
    debug_println(buf);
    snprintf(buf, sizeof(buf), "  GPIO %-2d - UART RX (Modbus Master)", MODBUS_MASTER_RX_PIN);
    debug_println(buf);
    snprintf(buf, sizeof(buf), "  GPIO %-2d - RS485 DE/RE (Modbus Master)", MODBUS_MASTER_DE_PIN);
    debug_println(buf);
  }
  for (uint8_t i = 0; i < COUNTER_COUNT; i++) {
    const CounterConfig &cc = g_persist_config.counters[i];
    if (cc.enabled && cc.hw_mode == COUNTER_HW_PCNT && cc.hw_gpio > 0) {
      char buf[64];
      snprintf(buf, sizeof(buf), "  GPIO %-2d - Counter %d HW (PCNT)", cc.hw_gpio, i + 1);
      debug_println(buf);
    }
  }
  if (!g_persist_config.modbus_slave.enabled && !g_persist_config.modbus_master.enabled) {
    bool any_counter = false;
    for (uint8_t i = 0; i < COUNTER_COUNT; i++) {
      if (g_persist_config.counters[i].enabled && g_persist_config.counters[i].hw_mode == COUNTER_HW_PCNT && g_persist_config.counters[i].hw_gpio > 0) {
        any_counter = true; break;
      }
    }
    if (!any_counter) {
      debug_println("  (kun GPIO 2 aktiv)");
    }
  }
  debug_println("");

  // Count GPIO mappings
  uint8_t gpio_count = 0;

  for (uint8_t i = 0; i < g_persist_config.var_map_count; i++) {
    const VariableMapping* map = &g_persist_config.var_maps[i];
    if (map->source_type == MAPPING_SOURCE_GPIO) {
      gpio_count++;
    }
  }

  // Show GPIO mappings
  if (gpio_count > 0) {
    debug_print("mappings (");
    debug_print_uint(gpio_count);
    debug_println("):");
    for (uint8_t i = 0; i < g_persist_config.var_map_count; i++) {
      const VariableMapping* map = &g_persist_config.var_maps[i];
      if (map->source_type != MAPPING_SOURCE_GPIO) continue;

      debug_print("  set gpio ");
      debug_print_uint(map->gpio_pin);
      debug_print(" ");
      if (map->is_input) {
        debug_print("input ");
        debug_print_uint(map->input_reg);
      } else {
        debug_print("coil ");
        debug_print_uint(map->coil_reg);
      }

      // Check if this register is used by ST Logic variable binding
      bool found_st_var = false;
      for (uint8_t j = 0; j < g_persist_config.var_map_count; j++) {
        const VariableMapping* st_map = &g_persist_config.var_maps[j];
        if (st_map->source_type != MAPPING_SOURCE_ST_VAR) continue;

        // Check if same register
        bool same_reg = false;
        if (map->is_input && st_map->is_input) {
          // Both input - check if same input register
          same_reg = (map->input_reg == st_map->input_reg && map->input_type == st_map->input_type);
        } else if (!map->is_input && !st_map->is_input) {
          // Both output - check if same coil/reg
          same_reg = (map->coil_reg == st_map->coil_reg && map->output_type == st_map->output_type);
        }

        if (same_reg) {
          // Found ST variable using this register
          debug_print(" [ST VAR:logic");
          debug_print_uint(st_map->st_program_id + 1);
          debug_print(".");
          st_logic_program_config_t *prog = st_logic_get_program(st_state, st_map->st_program_id);
          if (prog && prog->compiled && st_map->st_var_index < prog->bytecode.var_count) {
            debug_print(prog->bytecode.var_names[st_map->st_var_index]);
          } else {
            debug_print("var");
            debug_print_uint(st_map->st_var_index);
          }
          debug_print("]");
          found_st_var = true;
          break;
        }
      }

      if (!found_st_var) {
        // Not used by ST Logic
        if (map->associated_counter != 0xff) {
          debug_print(" [Counter");
          debug_print_uint(map->associated_counter);
          debug_print("]");
        } else if (map->associated_timer != 0xff) {
          debug_print(" [Timer");
          debug_print_uint(map->associated_timer);
          debug_print("]");
        } else {
          debug_print(" [Manual]");
        }
      }

      debug_println("");
    }
  } else {
    debug_println("mappings: (none)");
  }

  // =========================================================================
  // ST LOGIC
  // =========================================================================
  debug_println("\n[ST LOGIC]");
  debug_print("  status: ");
  debug_println(st_state->enabled ? "enabled" : "disabled");

  debug_print("  debug: ");
  debug_println(st_state->debug ? "enabled" : "disabled");

  debug_print("  interval: ");
  debug_print_uint(st_state->execution_interval_ms);
  debug_println(" ms");

  debug_print("  cycles: ");
  debug_print_uint(st_state->total_cycles);
  debug_println("");

  debug_println("\nprograms:");
  // Show individual programs (all 4 slots, even if empty)
  for (uint8_t i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
    st_logic_program_config_t* prog = &st_state->programs[i];

    debug_print("  logic");
    debug_print_uint(i + 1);
    debug_print(": ");

    if (prog->source_size == 0 && !prog->compiled) {
      // Empty slot - show (none configured) and binding count if any
      debug_print("(none configured)");
      if (prog->binding_count > 0) {
        debug_print(", register bindings=");
        debug_print_uint(prog->binding_count);
      }
    } else if (!prog->compiled) {
      debug_print("not compiled");
      if (prog->binding_count > 0) {
        debug_print(", register bindings=");
        debug_print_uint(prog->binding_count);
      }
    } else if (!prog->enabled) {
      // Disabled but compiled
      debug_print("disabled");
      if (prog->binding_count > 0) {
        debug_print(", register bindings=");
        debug_print_uint(prog->binding_count);
      }
    } else {
      // Enabled and compiled
      debug_print("enabled");
      debug_print(", register bindings=");
      debug_print_uint(prog->binding_count);
      debug_print(", exec=");
      debug_print_uint(prog->execution_count);

      if (prog->error_count > 0) {
        debug_print(", errors=");
        debug_print_uint(prog->error_count);
      }
    }
    debug_println("");
  }

  // Show ST Logic variable bindings
  uint8_t st_count = 0;
  for (uint8_t i = 0; i < g_persist_config.var_map_count; i++) {
    if (g_persist_config.var_maps[i].source_type == MAPPING_SOURCE_ST_VAR) {
      st_count++;
    }
  }

  if (st_count > 0) {
    debug_println("\nvariable bindings:");

    // Group bindings by program
    for (uint8_t prog_id = 0; prog_id < 4; prog_id++) {
      bool has_bindings = false;

      // Check if this program has any bindings
      for (uint8_t i = 0; i < g_persist_config.var_map_count; i++) {
        const VariableMapping* map = &g_persist_config.var_maps[i];
        if (map->source_type == MAPPING_SOURCE_ST_VAR && map->st_program_id == prog_id) {
          has_bindings = true;
          break;
        }
      }

      if (!has_bindings) continue;

      // Print program header
      debug_print("logic");
      debug_print_uint(prog_id + 1);
      debug_println(":");

      // Print all bindings for this program
      for (uint8_t i = 0; i < g_persist_config.var_map_count; i++) {
        const VariableMapping* map = &g_persist_config.var_maps[i];
        if (map->source_type != MAPPING_SOURCE_ST_VAR || map->st_program_id != prog_id) continue;

        debug_print("  var ");

        // Lookup variable name from bytecode
        st_logic_program_config_t *prog = st_logic_get_program(st_state, map->st_program_id);
        if (prog && prog->compiled && map->st_var_index < prog->bytecode.var_count) {
          debug_print(prog->bytecode.var_names[map->st_var_index]);
        } else {
          debug_print("var");
          debug_print_uint(map->st_var_index);
        }

        // Pad variable name for alignment (assume max 15 chars)
        debug_print(" ");

        if (map->is_input) {
          // INPUT mode: check if it's a Holding Register, Discrete Input, or Coil
          debug_print("<-- ");
          if (map->input_type == 1) {
            debug_print("input:");  // Discrete Input
          } else if (map->input_type == 2) {
            debug_print("coil:");   // Coil input
          } else {
            debug_print("reg:");    // Holding Register input
          }
          debug_print_uint(map->input_reg);
          debug_print(" as input");
        } else {
          // OUTPUT mode: check if it's a Coil or Holding Register
          debug_print("--> ");
          if (map->output_type == 1) {
            debug_print("coil:");  // Coil output
          } else {
            debug_print("reg:");   // Holding Register output
          }
          debug_print_uint(map->coil_reg);
          debug_print(" as output");
        }
        debug_println("");
      }
    }
  }

  // Show EXPORT variable → IR 220-251 mapping (v5.1.0)
  debug_println("\nEXPORT variables → IR 220-251:");
  bool has_exports = false;
  for (uint8_t prog_id = 0; prog_id < 4; prog_id++) {
    st_logic_program_config_t *prog = &st_state->programs[prog_id];

    // Skip if not compiled or no IR pool allocation
    if (!prog->compiled || prog->ir_pool_offset == 65535 || prog->ir_pool_size == 0) {
      continue;
    }

    // Check if program has EXPORT variables
    bool has_export_vars = false;
    for (uint8_t i = 0; i < prog->bytecode.var_count; i++) {
      if (prog->bytecode.var_export_flags[i]) {
        has_export_vars = true;
        break;
      }
    }

    if (!has_export_vars) continue;
    has_exports = true;

    // Print program header
    debug_print("logic");
    debug_print_uint(prog_id + 1);
    debug_print(": IR pool [offset=");
    debug_print_uint(prog->ir_pool_offset);
    debug_print(", size=");
    debug_print_uint(prog->ir_pool_size);
    debug_println("]");

    // Print all EXPORT variables with their IR addresses
    uint16_t export_slot = 0;
    for (uint8_t var_idx = 0; var_idx < prog->bytecode.var_count; var_idx++) {
      if (!prog->bytecode.var_export_flags[var_idx]) {
        continue;
      }

      st_datatype_t var_type = prog->bytecode.var_types[var_idx];
      uint16_t base_reg = 220 + prog->ir_pool_offset + export_slot;

      debug_print("  ");
      debug_print(prog->bytecode.var_names[var_idx]);
      debug_print(" (");

      // Print type
      switch (var_type) {
        case ST_TYPE_BOOL: debug_print("BOOL"); break;
        case ST_TYPE_INT: debug_print("INT"); break;
        case ST_TYPE_DINT: debug_print("DINT"); break;
        case ST_TYPE_DWORD: debug_print("DWORD"); break;
        case ST_TYPE_REAL: debug_print("REAL"); break;
        default: debug_print("???"); break;
      }

      debug_print(") → IR ");
      debug_print_uint(base_reg);

      // Multi-register types show range
      if (var_type == ST_TYPE_REAL || var_type == ST_TYPE_DINT || var_type == ST_TYPE_DWORD) {
        debug_print("-");
        debug_print_uint(base_reg + 1);
        debug_print(" (2 regs)");
        export_slot += 2;
      } else {
        export_slot += 1;
      }

      debug_println("");
    }
  }

  if (!has_exports) {
    debug_println("  (No EXPORT variables configured)");
    debug_println("  Syntax: VAR temp : INT EXPORT; END_VAR");
  }

  } // end show_gpio

  // =========================================================================
  // NETWORK
  // =========================================================================
  if (show_network) {
  debug_println("\n[NETWORK]");
  debug_println("wifi:");
  debug_print("  status: ");
  debug_println(g_persist_config.network.enabled ? "enabled" : "disabled");

  debug_print("  ssid: ");
  debug_println(g_persist_config.network.ssid[0] ? g_persist_config.network.ssid : "(not set)");

  debug_print("  password: ");
  debug_println(g_persist_config.network.password[0] ? "********" : "(not set)");

  debug_print("  dhcp: ");
  debug_println(g_persist_config.network.dhcp_enabled ? "enabled" : "disabled");

  if (!g_persist_config.network.dhcp_enabled) {
    char ip_str[16];
    debug_print("  static ip: ");
    network_config_ip_to_str(g_persist_config.network.static_ip, ip_str);
    debug_println(ip_str);

    debug_print("  gateway: ");
    network_config_ip_to_str(g_persist_config.network.static_gateway, ip_str);
    debug_println(ip_str);

    debug_print("  netmask: ");
    network_config_ip_to_str(g_persist_config.network.static_netmask, ip_str);
    debug_println(ip_str);

    debug_print("  dns: ");
    network_config_ip_to_str(g_persist_config.network.static_dns, ip_str);
    debug_println(ip_str);
  }

  debug_print("  power-save: ");
  debug_println(g_persist_config.network.wifi_power_save ? "ON (low power)" : "OFF (fast response)");

  debug_println("\ntelnet:");
  debug_print("  status: ");
  debug_println(g_persist_config.network.telnet_enabled ? "enabled" : "disabled");

  debug_print("  port: ");
  debug_print_uint(g_persist_config.network.telnet_port);
  debug_println("");

  debug_print("  username: ");
  debug_println(g_persist_config.network.telnet_username[0] ? g_persist_config.network.telnet_username : "(not set)");

  debug_print("  password: ");
  debug_println(g_persist_config.network.telnet_password[0] ? "********" : "(not set)");

  } // end show_network

  // =========================================================================
  // ETHERNET (v6.1.0+)
  // =========================================================================
  if (show_ethernet) {
  debug_println("\n[ETHERNET]");
  debug_print("  status: ");
  debug_println(g_persist_config.network.ethernet.enabled ? "enabled" : "disabled");

  debug_print("  dhcp: ");
  debug_println(g_persist_config.network.ethernet.dhcp_enabled ? "enabled" : "disabled");

  if (!g_persist_config.network.ethernet.dhcp_enabled) {
    char ip_str[16];
    debug_print("  static ip: ");
    network_config_ip_to_str(g_persist_config.network.ethernet.static_ip, ip_str);
    debug_println(ip_str);

    debug_print("  gateway: ");
    network_config_ip_to_str(g_persist_config.network.ethernet.static_gateway, ip_str);
    debug_println(ip_str);

    debug_print("  netmask: ");
    network_config_ip_to_str(g_persist_config.network.ethernet.static_netmask, ip_str);
    debug_println(ip_str);

    debug_print("  dns: ");
    network_config_ip_to_str(g_persist_config.network.ethernet.static_dns, ip_str);
    debug_println(ip_str);
  }

  if (g_persist_config.network.ethernet.hostname[0]) {
    debug_print("  hostname: ");
    debug_println(g_persist_config.network.ethernet.hostname);
  }

  } // end show_ethernet

  // =========================================================================
  // API HTTP
  // =========================================================================
  if (show_http) {
  debug_println("\n[API HTTP]");
  debug_print("  server: ");
  debug_println(g_persist_config.network.http.enabled ? "enabled" : "disabled");

  debug_print("  tls: ");
  debug_println(g_persist_config.network.http.tls_enabled ? "enabled (HTTPS)" : "disabled (HTTP)");

  {
    char cert_info[48];
    if (https_wrapper_get_cert_info(cert_info, sizeof(cert_info)) == 0) {
      debug_print("  cert: ");
      debug_println(cert_info);
    }
  }

  debug_print("  port: ");
  debug_print_uint(g_persist_config.network.http.port);
  debug_println("");

  debug_print("  api: ");
  debug_println(g_persist_config.network.http.api_enabled ? "enabled" : "disabled");

  debug_print("  auth: ");
  debug_println(g_persist_config.network.http.auth_enabled ? "enabled" : "disabled");

  if (g_persist_config.network.http.auth_enabled) {
    debug_print("  username: ");
    debug_println(g_persist_config.network.http.username[0] ? g_persist_config.network.http.username : "(not set)");

    debug_print("  password: ");
    debug_println(g_persist_config.network.http.password[0] ? "********" : "(not set)");
  }

  debug_print("  priority: ");
  switch (g_persist_config.network.http.priority) {
    case 0:  debug_println("LOW"); break;
    case 2:  debug_println("HIGH"); break;
    default: debug_println("NORMAL"); break;
  }

  } // end show_http

  // =========================================================================
  // MODULES
  // =========================================================================
  if (show_modules) {
  debug_println("\n[MODULES]");
  debug_print("  counters: ");
  debug_println((g_persist_config.module_flags & MODULE_FLAG_COUNTERS_DISABLED) ? "disabled" : "enabled");
  debug_print("  timers: ");
  debug_println((g_persist_config.module_flags & MODULE_FLAG_TIMERS_DISABLED) ? "disabled" : "enabled");
  debug_print("  st-logic: ");
  debug_println((g_persist_config.module_flags & MODULE_FLAG_ST_LOGIC_DISABLED) ? "disabled" : "enabled");

  } // end show_modules

  // =========================================================================
  // PERSISTENCE
  // =========================================================================
  if (show_persist) {
  debug_println("\n[PERSISTENCE]");
  debug_print("  status: ");
  debug_println(g_persist_config.persist_regs.enabled ? "enabled" : "disabled");

  // BUG-140: Clamp group_count to max (prevent garbage display)
  uint8_t actual_group_count = g_persist_config.persist_regs.group_count;
  if (actual_group_count > PERSIST_MAX_GROUPS) {
    actual_group_count = PERSIST_MAX_GROUPS;
  }

  debug_print("  groups: ");
  debug_print_uint(actual_group_count);
  debug_print(" / ");
  debug_print_uint(PERSIST_MAX_GROUPS);
  if (g_persist_config.persist_regs.group_count > PERSIST_MAX_GROUPS) {
    debug_print(" [WARNING: stored count=");
    debug_print_uint(g_persist_config.persist_regs.group_count);
    debug_print(" exceeds max, clamped]");
  }
  debug_println("");

  if (actual_group_count > 0) {
    for (uint8_t i = 0; i < actual_group_count; i++) {
      PersistGroup* grp = &g_persist_config.persist_regs.groups[i];
      debug_print("  grp");
      debug_print_uint(i + 1);  // Group number (1-indexed for ST Logic)
      debug_print(" \"");
      debug_print(grp->name);
      debug_print("\" (");
      debug_print_uint(grp->reg_count);
      debug_print(" regs): ");

      // Show register addresses
      for (uint8_t j = 0; j < grp->reg_count; j++) {
        if (j > 0) debug_print(", ");
        debug_print_uint(grp->reg_addresses[j]);
      }
      debug_println("");
    }
  } else {
    debug_println("  (none configured)");
  }

  } // end show_persist

  // =========================================================================
  // CONFIGURATION AS SET COMMANDS (copy/paste ready)
  // =========================================================================
  if (show_commands) {
  if (show_all) {
    debug_println("\n=== CONFIGURATION AS SET COMMANDS ===");
    debug_println("# Copy/paste these commands to recreate current config");
    debug_println("# NOTE: Counter/Timer registers are AUTO-ASSIGNED (cannot be manually set)");
    debug_println("#   Counter 1 -> HR100-114, Counter 2 -> HR120-134, Counter 3 -> HR140-154, Counter 4 -> HR160-174");
    debug_println("#   Timer ctrl registers are manually specified via ctrl-reg parameter\n");
  } else {
    debug_println("\n=== SET COMMANDS ===");
  }

  if (show_modbus) {
  debug_println("# Modbus Slave");
  debug_print("set modbus-slave enabled ");
  debug_println(g_persist_config.modbus_slave.enabled ? "on" : "off");
  if (g_persist_config.modbus_slave.enabled) {
    debug_print("set modbus-slave slave-id ");
    debug_print_uint(g_persist_config.modbus_slave.slave_id);
    debug_println("");
    debug_print("set modbus-slave baudrate ");
    debug_print_uint(g_persist_config.modbus_slave.baudrate);
    debug_println("");
    debug_print("set modbus-slave parity ");
    if (g_persist_config.modbus_slave.parity == 0) debug_println("none");
    else if (g_persist_config.modbus_slave.parity == 1) debug_println("even");
    else if (g_persist_config.modbus_slave.parity == 2) debug_println("odd");
    else debug_println("none");
    debug_print("set modbus-slave stop-bits ");
    debug_print_uint(g_persist_config.modbus_slave.stop_bits);
    debug_println("");
    debug_print("set modbus-slave inter-frame-delay ");
    debug_print_uint(g_persist_config.modbus_slave.inter_frame_delay);
    debug_println("");
  }

  // Modbus Master
  debug_println("\n# Modbus Master");
  debug_print("set modbus-master enabled ");
  debug_println(g_persist_config.modbus_master.enabled ? "on" : "off");
  if (g_persist_config.modbus_master.enabled) {
    debug_print("set modbus-master baudrate ");
    debug_print_uint(g_persist_config.modbus_master.baudrate);
    debug_println("");
    debug_print("set modbus-master parity ");
    if (g_persist_config.modbus_master.parity == 0) debug_println("none");
    else if (g_persist_config.modbus_master.parity == 1) debug_println("even");
    else if (g_persist_config.modbus_master.parity == 2) debug_println("odd");
    else debug_println("none");
    debug_print("set modbus-master stop-bits ");
    debug_print_uint(g_persist_config.modbus_master.stop_bits);
    debug_println("");
    debug_print("set modbus-master timeout ");
    debug_print_uint(g_persist_config.modbus_master.timeout_ms);
    debug_println("");
    debug_print("set modbus-master inter-frame-delay ");
    debug_print_uint(g_persist_config.modbus_master.inter_frame_delay);
    debug_println("");
    debug_print("set modbus-master max-requests ");
    debug_print_uint(g_persist_config.modbus_master.max_requests_per_cycle);
    debug_println("");
  }
  } // end show_modbus

  if (show_system) {
  // Hostname
  debug_println("\n# System");
  debug_print("set hostname ");
  debug_println(g_persist_config.hostname[0] ? g_persist_config.hostname : "modbus-esp32");
  debug_print("set echo ");
  debug_println(g_persist_config.remote_echo ? "on" : "off");
  debug_print("set gpio 2 ");
  debug_println(g_persist_config.gpio2_user_mode ? "enable" : "disable");
  } // end show_system

  if (show_network) {
  // WiFi
  debug_println("\n# WiFi");
  debug_print("set wifi ");
  debug_println(g_persist_config.network.enabled ? "enable" : "disable");
  if (g_persist_config.network.enabled) {
    if (g_persist_config.network.ssid[0]) {
      debug_print("set wifi ssid ");
      debug_println(g_persist_config.network.ssid);
    }
    if (g_persist_config.network.password[0]) {
      debug_println("set wifi password ********");
    }
    debug_print("set wifi dhcp ");
    debug_println(g_persist_config.network.dhcp_enabled ? "on" : "off");
    if (!g_persist_config.network.dhcp_enabled) {
      char ip_str[16];
      network_config_ip_to_str(g_persist_config.network.static_ip, ip_str);
      debug_print("set wifi ip ");
      debug_println(ip_str);
      network_config_ip_to_str(g_persist_config.network.static_gateway, ip_str);
      debug_print("set wifi gateway ");
      debug_println(ip_str);
      network_config_ip_to_str(g_persist_config.network.static_netmask, ip_str);
      debug_print("set wifi netmask ");
      debug_println(ip_str);
      network_config_ip_to_str(g_persist_config.network.static_dns, ip_str);
      debug_print("set wifi dns ");
      debug_println(ip_str);
    }
    debug_print("set wifi telnet ");
    debug_println(g_persist_config.network.telnet_enabled ? "enable" : "disable");
    if (g_persist_config.network.telnet_enabled) {
      if (g_persist_config.network.telnet_username[0]) {
        debug_print("set wifi telnet-user ");
        debug_println(g_persist_config.network.telnet_username);
      }
      if (g_persist_config.network.telnet_password[0]) {
        debug_println("set wifi telnet-pass ********");
      }
      debug_print("set wifi telnet-port ");
      debug_print_uint(g_persist_config.network.telnet_port);
      debug_println("");
    }
    // Power save setting (v6.0.4+)
    debug_print("set wifi power-save ");
    debug_println(g_persist_config.network.wifi_power_save ? "on" : "off");
  }
  } // end show_network

  if (show_http) {
  // API HTTP
  debug_println("\n# API HTTP");
  debug_println(g_persist_config.network.http.enabled ? "set http enable" : "set http disable");
  if (g_persist_config.network.http.enabled) {
    debug_println(g_persist_config.network.http.tls_enabled ? "set http tls enable" : "set http tls disable");
    debug_print("set http port ");
    debug_print_uint(g_persist_config.network.http.port);
    debug_println("");
    debug_println(g_persist_config.network.http.api_enabled ? "set http api enable" : "set http api disable");
    debug_println(g_persist_config.network.http.auth_enabled ? "set http auth enable" : "set http auth disable");
    if (g_persist_config.network.http.auth_enabled) {
      if (g_persist_config.network.http.username[0]) {
        debug_print("set http username ");
        debug_println(g_persist_config.network.http.username);
      }
      if (g_persist_config.network.http.password[0]) {
        debug_println("set http password ********");
      }
    }
    // Priority setting (v6.0.4+)
    debug_print("set http priority ");
    switch (g_persist_config.network.http.priority) {
      case 0:  debug_println("low"); break;
      case 2:  debug_println("high"); break;
      default: debug_println("normal"); break;
    }
  }
  } // end show_http

  if (show_logic) {
  // ST Logic execution interval + enable/disable status
  debug_println("\n# ST Logic");
  debug_print("set logic interval ");
  debug_print_uint(g_persist_config.st_logic_interval_ms);
  debug_println("");

  // ST Logic enable/disable for each program
  extern st_logic_engine_state_t* st_logic_get_state(void);
  st_logic_engine_state_t* logic_state = st_logic_get_state();
  if (logic_state) {
    for (uint8_t i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
      st_logic_program_config_t* prog = &logic_state->programs[i];
      // Only show if program has source code OR is explicitly enabled/disabled
      if (prog->source_size > 0 || prog->enabled) {
        debug_print("set logic ");
        debug_print_uint(i + 1);
        debug_println(prog->enabled ? " enabled" : " disabled");
      }
    }
  }
  } // end show_logic

  if (show_persist) {
  // Persistence
  // BUG-140: Clamp group_count to prevent out-of-bounds access
  uint8_t persist_group_count = g_persist_config.persist_regs.group_count;
  if (persist_group_count > PERSIST_MAX_GROUPS) {
    persist_group_count = PERSIST_MAX_GROUPS;
  }

  if (g_persist_config.persist_regs.enabled && persist_group_count > 0) {
    debug_println("\n# Persistence");
    debug_println("set persist enable");
    for (uint8_t i = 0; i < persist_group_count; i++) {
      PersistGroup* grp = &g_persist_config.persist_regs.groups[i];
      debug_print("set persist group ");
      debug_print_uint(i + 1);
      debug_print(" name ");
      debug_print(grp->name);
      debug_print(" regs ");
      for (uint8_t j = 0; j < grp->reg_count; j++) {
        if (j > 0) debug_print(",");
        debug_print_uint(grp->reg_addresses[j]);
      }
      debug_println("");
    }
  }
  } // end show_persist

  if (show_counters) {
  // Counters (always show all 4)
  debug_println("\n# Counters");
  for (uint8_t id = 1; id <= 4; id++) {
    CounterConfig cfg;
    if (!counter_config_get(id, &cfg) || !cfg.enabled) {
      // Counter disabled - show simple disable command
      debug_print("set counter ");
      debug_print_uint(id);
      debug_println(" disable");
    } else {
      // Counter enabled - show full config
      debug_print("set counter ");
      debug_print_uint(id);
      debug_print(" mode 1 parameter");

      // HW mode
      debug_print(" hw-mode:");
      if (cfg.hw_mode == COUNTER_HW_SW) debug_print("sw");
      else if (cfg.hw_mode == COUNTER_HW_SW_ISR) debug_print("sw-isr");
      else if (cfg.hw_mode == COUNTER_HW_PCNT) debug_print("hw");

      // Edge type
      debug_print(" edge:");
      if (cfg.edge_type == COUNTER_EDGE_RISING) debug_print("rising");
      else if (cfg.edge_type == COUNTER_EDGE_FALLING) debug_print("falling");
      else if (cfg.edge_type == COUNTER_EDGE_BOTH) debug_print("both");

      // Prescaler and resolution
      debug_print(" prescaler:");
      debug_print_uint(cfg.prescaler);
      debug_print(" resolution:");
      debug_print_uint(cfg.bit_width);

      // Direction
      debug_print(" direction:");
      debug_print(cfg.direction == COUNTER_DIR_DOWN ? "down" : "up");

      // Scale factor
      debug_print(" scale:");
      debug_print_float(cfg.scale_factor);

      // Start value (BUG-183 FIX: use 64-bit print)
      debug_print(" start:");
      debug_print_ulong(cfg.start_value);

      // Debounce
      debug_print(" debounce:");
      debug_print(cfg.debounce_enabled ? "on" : "off");
      if (cfg.debounce_enabled) {
        debug_print(" debounce-time:");
        debug_print_uint(cfg.debounce_ms);
      }

      // Input/Pin based on hw_mode
      if (cfg.hw_mode == COUNTER_HW_PCNT && cfg.hw_gpio > 0) {
        debug_print(" hw-gpio:");
        debug_print_uint(cfg.hw_gpio);
      } else if (cfg.hw_mode == COUNTER_HW_SW_ISR && cfg.interrupt_pin > 0) {
        debug_print(" interrupt-pin:");
        debug_print_uint(cfg.interrupt_pin);
      } else if (cfg.hw_mode == COUNTER_HW_SW && cfg.input_dis > 0) {
        debug_print(" input-dis:");
        debug_print_uint(cfg.input_dis);
      }

      // Compare feature
      if (cfg.compare_enabled) {
        debug_print(" compare:enable");
        debug_print(" compare-mode:");
        debug_print_uint(cfg.compare_mode);
        debug_print(" compare-source:");
        debug_print_uint(cfg.compare_source);
        debug_print(" compare-value:");
        debug_print_uint((uint32_t)cfg.compare_value);
        debug_print(" reset-on-read:");
        debug_print(cfg.reset_on_read ? "on" : "off");
      }

      debug_println("");
    }
  }
  } // end show_counters

  if (show_timers) {
  // Timers (always show all 4)
  debug_println("\n# Timers");
  for (uint8_t id = 1; id <= 4; id++) {
    TimerConfig cfg;
    if (!timer_engine_get_config(id, &cfg) || !cfg.enabled) {
      // Timer disabled - show simple disable command
      debug_print("set timer ");
      debug_print_uint(id);
      debug_println(" disable");
    } else {
      // Timer enabled - show full config
      debug_print("set timer ");
      debug_print_uint(id);
      debug_print(" mode ");
      debug_print_uint(cfg.mode);
      debug_print(" parameter");

      // Mode-specific parameters
      switch (cfg.mode) {
        case TIMER_MODE_1_ONESHOT:
          debug_print(" p1-duration:");
          debug_print_uint(cfg.phase1_duration_ms);
          debug_print(" p1-output:");
          debug_print_uint(cfg.phase1_output_state);
          debug_print(" p2-duration:");
          debug_print_uint(cfg.phase2_duration_ms);
          debug_print(" p2-output:");
          debug_print_uint(cfg.phase2_output_state);
          debug_print(" p3-duration:");
          debug_print_uint(cfg.phase3_duration_ms);
          debug_print(" p3-output:");
          debug_print_uint(cfg.phase3_output_state);
          break;

        case TIMER_MODE_2_MONOSTABLE:
          debug_print(" pulse-ms:");
          debug_print_uint(cfg.pulse_duration_ms);
          debug_print(" p1-output:");
          debug_print_uint(cfg.phase1_output_state);
          debug_print(" p2-output:");
          debug_print_uint(cfg.phase2_output_state);
          break;

        case TIMER_MODE_3_ASTABLE:
          debug_print(" on-ms:");
          debug_print_uint(cfg.on_duration_ms);
          debug_print(" off-ms:");
          debug_print_uint(cfg.off_duration_ms);
          debug_print(" p1-output:");
          debug_print_uint(cfg.phase1_output_state);
          debug_print(" p2-output:");
          debug_print_uint(cfg.phase2_output_state);
          break;

        case TIMER_MODE_4_INPUT_TRIGGERED:
          debug_print(" input-dis:");
          debug_print_uint(cfg.input_dis);
          debug_print(" trigger-edge:");
          debug_print_uint(cfg.trigger_edge);
          debug_print(" delay-ms:");
          debug_print_uint(cfg.delay_ms);
          debug_print(" output:");
          debug_print_uint(cfg.phase1_output_state);
          break;
      }

      // Output coil
      debug_print(" output-coil:");
      debug_print_uint(cfg.output_coil);

      // Control register
      debug_print(" ctrl-reg:");
      debug_print_uint(cfg.ctrl_reg);

      debug_println("");
    }
  }
  } // end show_timers

  if (show_gpio) {
  // GPIO mappings (only GPIO source type, not ST Logic bindings)
  bool any_gpio_mapping = false;
  for (uint8_t i = 0; i < g_persist_config.var_map_count; i++) {
    const VariableMapping* map = &g_persist_config.var_maps[i];
    if (map->source_type != MAPPING_SOURCE_GPIO) continue;

    // Skip counter/timer associated pins (they are configured via counter/timer commands)
    if (map->associated_counter != 0xff || map->associated_timer != 0xff) continue;

    if (!any_gpio_mapping) {
      debug_println("\n# GPIO Mappings");
      any_gpio_mapping = true;
    }

    debug_print("set gpio ");
    debug_print_uint(map->gpio_pin);

    if (map->is_input) {
      debug_print(" input ");
      debug_print_uint(map->input_reg);
    } else {
      debug_print(" coil ");
      debug_print_uint(map->coil_reg);
    }

    debug_println("");
  }
  } // end show_gpio

  debug_println("\n# Note: Remember to run 'save' after making changes!");
  debug_println("");
  } // end show_commands
}

/* ============================================================================
 * SHOW COUNTERS
 * ============================================================================ */

void cli_cmd_show_counters(void) {
  // Header med forkortelser (3 linjer)
  debug_println("───────────────────────────────────────────────────────────────────---------------─────────────────────────────────────────────────────────────────────────────────────────────────────────────");
  debug_println("co = count-on, sv = startValue, res = resolution, ps = prescaler, vr = value-reg, rr = raw-reg, fr = freq-reg");
  debug_println("cr = ctrl-reg (bit3=overflow), dir = direction, sf = scaleFloat, d = debounce, dt = debounce-ms");
  debug_println("hw = HW/SW mode (SW|ISR|HW), hz = measured freq (Hz), value = scaled value, raw = prescaled counter value");
  debug_println("cmp-en = compare enabled, cmp-mode = 0:≥ 1:> 2:exact, cmp-src = 0:raw 1:prescaled 2:scaled, cmp-val = threshold, ror = reset-on-read");
  debug_println("─────────────────────────────────────────────────────────────---------------───────────────────────────────────────────────────────────────────────────────────────────────────────────────────");

  // Kolonne headers
  debug_println("counter |  en  | hw  | pin  |    co   |    sv    | res |  ps  |  vr  |  rr  |  fr  |  cr  |  dir  |   sf   | d   |  dt  |  hz   |     val     |    raw    | cmp-en | cmp-md | cmp-src | cmp-val | ror");

  // Data rækker for hver counter
  for (uint8_t id = 1; id <= 4; id++) {
    CounterConfig cfg;
    if (!counter_config_get(id, &cfg)) continue;

    char line[256];
    char* p = line;

    // Counter ID (7 chars left-aligned)
    p += snprintf(p, sizeof(line) - (p - line), " %-7d", id);
    p += snprintf(p, sizeof(line) - (p - line), "| ");

    // Mode (4 chars left-aligned)
    p += snprintf(p, sizeof(line) - (p - line), "%-4d ", cfg.enabled ? 1 : 0);
    p += snprintf(p, sizeof(line) - (p - line), "| ");

    // HW mode (3 chars left-aligned)
    const char* hw_str = "???";
    if (cfg.hw_mode == COUNTER_HW_SW) hw_str = "SW";
    else if (cfg.hw_mode == COUNTER_HW_SW_ISR) hw_str = "ISR";
    else if (cfg.hw_mode == COUNTER_HW_PCNT) hw_str = "HW";
    p += snprintf(p, sizeof(line) - (p - line), "%-3s ", hw_str);
    p += snprintf(p, sizeof(line) - (p - line), "| ");

    // Pin (4 chars left-aligned)
    // BUG FIX 1.9: Show hw_gpio from config instead of hardcoded values
    if (cfg.hw_mode == COUNTER_HW_PCNT && cfg.hw_gpio > 0) {
      p += snprintf(p, sizeof(line) - (p - line), "%-4d ", cfg.hw_gpio);
    } else if (cfg.hw_mode == COUNTER_HW_SW_ISR && cfg.interrupt_pin > 0) {
      p += snprintf(p, sizeof(line) - (p - line), "%-4d ", cfg.interrupt_pin);
    } else if (cfg.input_dis > 0) {
      p += snprintf(p, sizeof(line) - (p - line), "%-4d ", cfg.input_dis);
    } else {
      p += snprintf(p, sizeof(line) - (p - line), "%-4s ", "-");
    }
    p += snprintf(p, sizeof(line) - (p - line), "| ");

    // Count-on (7 chars left-aligned)
    const char* edge_str = "???";
    if (cfg.edge_type == COUNTER_EDGE_RISING) edge_str = "rising";
    else if (cfg.edge_type == COUNTER_EDGE_FALLING) edge_str = "falling";
    else if (cfg.edge_type == COUNTER_EDGE_BOTH) edge_str = "both";
    p += snprintf(p, sizeof(line) - (p - line), "%-7s ", edge_str);
    p += snprintf(p, sizeof(line) - (p - line), "| ");

    // Start value (8 chars right-aligned)
    p += snprintf(p, sizeof(line) - (p - line), "%8u ", (unsigned int)cfg.start_value);
    p += snprintf(p, sizeof(line) - (p - line), "| ");

    // Resolution (3 chars right-aligned)
    p += snprintf(p, sizeof(line) - (p - line), "%3d ", cfg.bit_width);
    p += snprintf(p, sizeof(line) - (p - line), "| ");

    // Prescaler (4 chars right-aligned)
    p += snprintf(p, sizeof(line) - (p - line), "%4u ", cfg.prescaler);
    p += snprintf(p, sizeof(line) - (p - line), "| ");

    // value-reg (4 chars right-aligned)
    p += snprintf(p, sizeof(line) - (p - line), "%4u ", cfg.value_reg);
    p += snprintf(p, sizeof(line) - (p - line), "| ");

    // raw-reg (4 chars right-aligned)
    p += snprintf(p, sizeof(line) - (p - line), "%4u ", cfg.raw_reg);
    p += snprintf(p, sizeof(line) - (p - line), "| ");

    // freq-reg (4 chars right-aligned)
    p += snprintf(p, sizeof(line) - (p - line), "%4u ", cfg.freq_reg);
    p += snprintf(p, sizeof(line) - (p - line), "| ");

    // ctrl-reg (4 chars right-aligned)
    if (cfg.ctrl_reg < 1000) {
      p += snprintf(p, sizeof(line) - (p - line), "%4u ", cfg.ctrl_reg);
    } else {
      p += snprintf(p, sizeof(line) - (p - line), " n/a ");
    }
    p += snprintf(p, sizeof(line) - (p - line), "| ");

    // Direction (5 chars left-aligned)
    const char* dir_str = cfg.direction == COUNTER_DIR_UP ? "up" : "down";
    p += snprintf(p, sizeof(line) - (p - line), "%-5s ", dir_str);
    p += snprintf(p, sizeof(line) - (p - line), "| ");

    // Scale factor (6 chars, format: X.XXX)
    p += snprintf(p, sizeof(line) - (p - line), "%6.3f ", cfg.scale_factor);
    p += snprintf(p, sizeof(line) - (p - line), "| ");

    // Debounce enabled (3 chars left-aligned)
    const char* deb_str = cfg.debounce_enabled ? "on" : "off";
    p += snprintf(p, sizeof(line) - (p - line), "%-3s ", deb_str);
    p += snprintf(p, sizeof(line) - (p - line), "| ");

    // Debounce time (4 chars right-aligned)
    p += snprintf(p, sizeof(line) - (p - line), "%4u ", cfg.debounce_ms);
    p += snprintf(p, sizeof(line) - (p - line), "| ");

    // Measured frequency (5 chars right-aligned)
    uint32_t freq = counter_frequency_get(id);
    p += snprintf(p, sizeof(line) - (p - line), "%5u ", freq);
    p += snprintf(p, sizeof(line) - (p - line), "| ");

    // BUG-019 FIX: Atomic counter value read to avoid race condition
    // Get counter value ONCE and calculate scaled/raw from same value
    uint64_t counter_value = counter_engine_get_value(id);

    // Calculate bit-width max value ONCE for clamping
    uint64_t max_val = 0;
    switch (cfg.bit_width) {
      case 8:  max_val = 0xFFULL; break;
      case 16: max_val = 0xFFFFULL; break;
      case 32: max_val = 0xFFFFFFFFULL; break;
      case 64:
      default: max_val = 0xFFFFFFFFFFFFFFFFULL; break;
    }

    // Clamp counter_value to bit-width FIRST to avoid overflow in scaling
    uint64_t clamped_value = counter_value & max_val;

    // Scaled value (10 chars right-aligned for 32-bit max: 4294967295)
    // CORRECT: scaled = counter × scale (not raw × scale!)
    double scaled_float = (double)clamped_value * cfg.scale_factor;

    // Clamp scaled result to bit-width range
    if (scaled_float < 0.0) scaled_float = 0.0;
    if (scaled_float > (double)max_val) scaled_float = (double)max_val;

    uint64_t scaled_value = (uint64_t)(scaled_float + 0.5);  // Round to nearest
    scaled_value &= max_val;

    p += snprintf(p, sizeof(line) - (p - line), "%10llu ", (unsigned long long)scaled_value);
    p += snprintf(p, sizeof(line) - (p - line), "| ");

    // Raw value (prescaled, 9 chars right-aligned for 32-bit max: 268435455)
    // CORRECT: raw = counter / prescaler (same clamped_value!)
    uint64_t raw_prescaled = clamped_value / cfg.prescaler;
    raw_prescaled &= max_val;

    p += snprintf(p, sizeof(line) - (p - line), "%9llu ", (unsigned long long)raw_prescaled);
    p += snprintf(p, sizeof(line) - (p - line), "| ");

    // COMPARE FEATURE COLUMNS
    // cmp-en (compare enabled, 3 chars left-aligned)
    p += snprintf(p, sizeof(line) - (p - line), "%-6s ", cfg.compare_enabled ? "yes" : "no");
    p += snprintf(p, sizeof(line) - (p - line), "| ");

    // cmp-md (compare mode: 0=≥, 1=>, 2=exact, 1 char)
    if (cfg.compare_enabled) {
      const char* cmp_mode_char = "?";
      if (cfg.compare_mode == 0) cmp_mode_char = "≥";
      else if (cfg.compare_mode == 1) cmp_mode_char = ">";
      else if (cfg.compare_mode == 2) cmp_mode_char = "=";
      p += snprintf(p, sizeof(line) - (p - line), "%-8s ", cmp_mode_char);
    } else {
      p += snprintf(p, sizeof(line) - (p - line), "%-8s ", "—");
    }
    p += snprintf(p, sizeof(line) - (p - line), "| ");

    // cmp-src (compare source: 0=raw, 1=prescaled, 2=scaled)
    if (cfg.compare_enabled) {
      const char* cmp_src_str = "?";
      if (cfg.compare_source == 0) cmp_src_str = "raw";
      else if (cfg.compare_source == 1) cmp_src_str = "presc";
      else if (cfg.compare_source == 2) cmp_src_str = "scale";
      p += snprintf(p, sizeof(line) - (p - line), "%-8s ", cmp_src_str);
    } else {
      p += snprintf(p, sizeof(line) - (p - line), "%-8s ", "—");
    }
    p += snprintf(p, sizeof(line) - (p - line), "| ");

    // cmp-val (compare value, 8 chars right-aligned)
    if (cfg.compare_enabled) {
      p += snprintf(p, sizeof(line) - (p - line), "%8llu ", (unsigned long long)cfg.compare_value);
    } else {
      p += snprintf(p, sizeof(line) - (p - line), "%-9s ", "—");
    }
    p += snprintf(p, sizeof(line) - (p - line), "| ");

    // ror (reset-on-read, 3 chars left-aligned)
    if (cfg.compare_enabled) {
      p += snprintf(p, sizeof(line) - (p - line), "%-3s", cfg.reset_on_read ? "yes" : "no");
    } else {
      p += snprintf(p, sizeof(line) - (p - line), "%-3s", "—");
    }

    // Print hele linjen
    debug_println(line);
  }

  // COUNTER CONTROL STATUS (samme stil som counter status)
  debug_println("");
  debug_println("Control register status (bit0=reset, bit1=start, bit2=stop, bit4=compare-match)");
  debug_println("─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────");
  debug_println("counter | ctrl-reg | raw-value | reset | start | stop | running | compare-match");

  for (uint8_t id = 1; id <= 4; id++) {
    CounterConfig cfg;
    if (!counter_config_get(id, &cfg) || !cfg.enabled) continue;

    char line[256];
    char* p = line;

    // Counter ID (8 chars left-aligned)
    p += snprintf(p, sizeof(line) - (p - line), " %-7d", id);
    p += snprintf(p, sizeof(line) - (p - line), "| ");

    // ctrl-reg address (10 chars)
    if (cfg.ctrl_reg < HOLDING_REGS_SIZE) {
      p += snprintf(p, sizeof(line) - (p - line), "%-9u ", cfg.ctrl_reg);
    } else {
      p += snprintf(p, sizeof(line) - (p - line), "%-10s ", "n/a");
    }
    p += snprintf(p, sizeof(line) - (p - line), "| ");

    if (cfg.ctrl_reg < HOLDING_REGS_SIZE) {
      uint16_t ctrl_value = registers_get_holding_register(cfg.ctrl_reg);

      // Raw value (hex, 10 chars)
      p += snprintf(p, sizeof(line) - (p - line), "0x%-7x ", ctrl_value);
      p += snprintf(p, sizeof(line) - (p - line), "| ");

      // Bit 0: reset (7 chars)
      uint8_t bit0 = (ctrl_value >> 0) & 1;
      p += snprintf(p, sizeof(line) - (p - line), "%-6s ", bit0 ? "yes" : "no");
      p += snprintf(p, sizeof(line) - (p - line), "| ");

      // Bit 1: start (7 chars)
      uint8_t bit1 = (ctrl_value >> 1) & 1;
      p += snprintf(p, sizeof(line) - (p - line), "%-6s ", bit1 ? "yes" : "no");
      p += snprintf(p, sizeof(line) - (p - line), "| ");

      // Bit 2: stop (6 chars)
      uint8_t bit2 = (ctrl_value >> 2) & 1;
      p += snprintf(p, sizeof(line) - (p - line), "%-5s ", bit2 ? "yes" : "no");
      p += snprintf(p, sizeof(line) - (p - line), "| ");

      // Running status (derived from start/stop, 9 chars)
      const char* running_status = "—";
      if (bit1 && !bit2) running_status = "yes";
      else if (!bit1 && bit2) running_status = "no";
      else if (bit1 && bit2) running_status = "conflict";
      p += snprintf(p, sizeof(line) - (p - line), "%-8s ", running_status);
      p += snprintf(p, sizeof(line) - (p - line), "| ");

      // Bit 4: compare-match (15 chars)
      uint8_t bit4 = (ctrl_value >> 4) & 1;
      p += snprintf(p, sizeof(line) - (p - line), "%-14s", bit4 ? "yes" : "no");
    } else {
      p += snprintf(p, sizeof(line) - (p - line), "%-10s | %-6s | %-6s | %-5s | %-8s | %-14s",
                   "—", "—", "—", "—", "—", "—");
    }

    debug_println(line);
  }
}

/* ============================================================================
 * SHOW COUNTER (SPECIFIC ID)
 * ============================================================================ */

void cli_cmd_show_counter(uint8_t id, bool verbose) {
  if (id < 1 || id > 4) {
    debug_printf("SHOW COUNTER: invalid ID %d (expected 1-4)\n", id);
    return;
  }

  CounterConfig cfg;
  if (!counter_config_get(id, &cfg)) {
    debug_printf("SHOW COUNTER: could not get config for counter %d\n", id);
    return;
  }

  debug_println("");
  debug_print("=== COUNTER ");
  debug_print_uint(id);
  if (verbose) {
    debug_print(" (VERBOSE MODE)");
  }
  debug_println(" ===\n");

  if (!cfg.enabled) {
    debug_println("Status: DISABLED\n");
    return;
  }

  debug_println("Status: ENABLED");
  debug_println("");

  // Hardware mode
  const char* hw_str = "unknown";
  if (cfg.hw_mode == COUNTER_HW_SW) hw_str = "SW (polling)";
  else if (cfg.hw_mode == COUNTER_HW_SW_ISR) hw_str = "SW-ISR (interrupt)";
  else if (cfg.hw_mode == COUNTER_HW_PCNT) hw_str = "HW (PCNT)";
  debug_print("Hardware Mode: ");
  debug_println(hw_str);

  // Edge type
  const char* edge_str = "rising";
  if (cfg.edge_type == COUNTER_EDGE_FALLING) edge_str = "falling";
  else if (cfg.edge_type == COUNTER_EDGE_BOTH) edge_str = "both";
  debug_print("Edge Type: ");
  debug_println(edge_str);

  // Prescaler
  debug_print("Prescaler: ");
  debug_print_uint(cfg.prescaler);
  debug_println("");

  // Scale factor
  debug_print("Scale Factor: ");
  debug_print_float(cfg.scale_factor);
  debug_println("");

  // Bit width
  debug_print("Bit Width: ");
  debug_print_uint(cfg.bit_width);
  debug_println("");

  // Direction
  const char* dir_str = (cfg.direction == COUNTER_DIR_DOWN) ? "down" : "up";
  debug_print("Direction: ");
  debug_println(dir_str);

  // Debounce
  debug_print("Debounce: ");
  if (cfg.debounce_enabled && cfg.debounce_ms > 0) {
    debug_print("ON (");
    debug_print_uint(cfg.debounce_ms);
    debug_println("ms)");
  } else {
    debug_println("OFF");
  }

  debug_println("");

  // Register mappings
  debug_println("Register Mappings:");
  debug_print("  Value Register (scaled value): ");
  debug_print_uint(cfg.value_reg);
  debug_println("");

  if (cfg.raw_reg > 0) {
    debug_print("  Raw Register (prescaled): ");
    debug_print_uint(cfg.raw_reg);
    debug_println("");
  }

  if (cfg.freq_reg > 0) {
    debug_print("  Frequency Register (Hz): ");
    debug_print_uint(cfg.freq_reg);
    debug_println("");
  }

  if (cfg.ctrl_reg < 1000) {
    debug_print("  Control Register (bit 3 = overflow): ");
    debug_print_uint(cfg.ctrl_reg);
    debug_println("");
  }

  debug_println("");

  // Input/Pin configuration
  debug_println("Input Configuration:");
  if (cfg.hw_mode == COUNTER_HW_SW && cfg.input_dis > 0) {
    debug_print("  Discrete Input: ");
    debug_print_uint(cfg.input_dis);
    debug_println("");
  } else if (cfg.hw_mode == COUNTER_HW_SW_ISR && cfg.interrupt_pin > 0) {
    debug_print("  Interrupt GPIO: ");
    debug_print_uint(cfg.interrupt_pin);
    debug_println("");
  } else if (cfg.hw_mode == COUNTER_HW_PCNT && cfg.hw_gpio > 0) {
    debug_print("  PCNT GPIO: ");
    debug_print_uint(cfg.hw_gpio);
    debug_println("");
  }

  debug_println("");

  // Current values
  debug_println("Current Values:");
  uint64_t raw_value = counter_engine_get_value(id);
  uint64_t scaled_value = (uint64_t)(raw_value * cfg.scale_factor);
  uint64_t raw_prescaled = raw_value / cfg.prescaler;
  uint32_t freq = counter_frequency_get(id);

  // BUG-183 FIX: Use 64-bit print for all counter values
  debug_print("  Raw Value: ");
  debug_print_ulong(raw_value);
  debug_println("");

  debug_print("  Prescaled Value: ");
  debug_print_ulong(raw_prescaled);
  debug_println("");

  debug_print("  Scaled Value: ");
  debug_print_ulong(scaled_value);
  debug_println("");

  debug_print("  Frequency: ");
  debug_print_uint(freq);
  debug_println(" Hz");

  // Compare feature
  if (cfg.compare_enabled) {
    debug_println("");
    debug_println("Compare Feature: ENABLED");
    debug_print("  Threshold: ");
    debug_print_ulong(cfg.compare_value);  // BUG-183 FIX: 64-bit print
    debug_println("");

    const char* cmp_mode = "unknown";
    if (cfg.compare_mode == 0) cmp_mode = ">= (greater or equal)";
    else if (cfg.compare_mode == 1) cmp_mode = "> (greater than)";
    else if (cfg.compare_mode == 2) cmp_mode = "= (exact match)";
    debug_print("  Mode: ");
    debug_println(cmp_mode);

    debug_print("  Reset on Read: ");
    debug_println(cfg.reset_on_read ? "ON" : "OFF");
  }

  // VERBOSE MODE: Extended runtime statistics
  if (verbose) {
    debug_println("");
    debug_println("=== EXTENDED RUNTIME DETAILS ===");
    debug_println("");

    // Control register details (if available)
    if (cfg.ctrl_reg < 1000) {
      uint16_t ctrl_val = registers_get_holding_register(cfg.ctrl_reg);
      debug_println("Control Register Status:");
      debug_print("  Running: ");
      debug_println((ctrl_val & 0x80) ? "YES (bit 7 set)" : "NO (bit 7 clear)");
      debug_print("  Auto-Start: ");
      debug_println((ctrl_val & 0x40) ? "YES (bit 6 set)" : "NO (bit 6 clear)");
      debug_print("  Compare Match: ");
      debug_println((ctrl_val & 0x10) ? "YES (bit 4 set)" : "NO (bit 4 clear)");
      debug_print("  Counter Reset on Read: ");
      debug_println((ctrl_val & 0x08) ? "YES (bit 3 set)" : "NO (bit 3 clear)");
      debug_print("  Compare Reset on Read: ");
      debug_println((ctrl_val & 0x04) ? "YES (bit 2 set)" : "NO (bit 2 clear)");
      debug_println("");
    }

    // Overflow status (read from ctrl_reg bit 3)
    if (cfg.ctrl_reg < 1000) {
      uint16_t ctrl_val = registers_get_holding_register(cfg.ctrl_reg);
      uint8_t overflow_flag = (ctrl_val & (1 << 3)) ? 1 : 0;
      debug_println("Overflow Status:");
      debug_print("  Overflow Flag (ctrl_reg bit 3): ");
      debug_println(overflow_flag ? "YES (overflow detected)" : "NO");
      debug_println("");
    }

    // Hardware-specific details
    if (cfg.hw_mode == COUNTER_HW_PCNT) {
      debug_println("Hardware Counter (PCNT) Details:");
      debug_print("  PCNT Unit: ");
      debug_print_uint(id - 1);  // Unit 0-3
      debug_println("");
      debug_print("  GPIO Pin: ");
      debug_print_uint(cfg.hw_gpio);
      debug_println("");
      debug_println("  Note: PCNT runs independently in hardware");
      debug_println("");
    }

    // Performance hints
    debug_println("Performance Notes:");
    if (cfg.hw_mode == COUNTER_HW_SW) {
      debug_println("  - SW mode: Polled every cycle (~10ms typical)");
      debug_println("  - Max frequency: ~50 Hz reliably");
    } else if (cfg.hw_mode == COUNTER_HW_SW_ISR) {
      debug_println("  - SW-ISR mode: Interrupt-driven, no polling delay");
      debug_println("  - Max frequency: ~10 kHz typical");
    } else if (cfg.hw_mode == COUNTER_HW_PCNT) {
      debug_println("  - HW mode: Hardware PCNT, zero CPU overhead");
      debug_println("  - Max frequency: 40 MHz (ESP32 spec)");
    }
    debug_println("");

    // Register mapping summary
    debug_println("Full Register Map:");
    debug_print("  HR");
    debug_print_uint(cfg.value_reg);
    debug_print("-HR");
    debug_print_uint(cfg.value_reg + 3);
    debug_println(": Scaled value (1-4 words, LSB first)");
    if (cfg.raw_reg > 0) {
      debug_print("  HR");
      debug_print_uint(cfg.raw_reg);
      debug_print("-HR");
      debug_print_uint(cfg.raw_reg + 3);
      debug_println(": Raw value (prescaled, 1-4 words)");
    }
    if (cfg.freq_reg > 0) {
      debug_print("  HR");
      debug_print_uint(cfg.freq_reg);
      debug_println(": Frequency (Hz, 1 word)");
    }
    if (cfg.ctrl_reg < 1000) {
      debug_print("  HR");
      debug_print_uint(cfg.ctrl_reg);
      debug_println(": Control bits (bit 3 = overflow, see above)");
    }
  }

  debug_println("");
}

/* ============================================================================
 * SHOW TIMERS
 * ============================================================================ */

void cli_cmd_show_timers(void) {
  debug_println("\n=== TIMER STATUS (Timer 1-4) ===\n");

  for (uint8_t id = 1; id <= 4; id++) {
    TimerConfig cfg;
    if (!timer_engine_get_config(id, &cfg)) {
      continue;
    }

    debug_print("Timer ");
    debug_print_uint(id);
    debug_print(": ");

    if (!cfg.enabled) {
      debug_println("DISABLED");
      continue;
    }

    // Show mode and status
    switch (cfg.mode) {
      case TIMER_MODE_1_ONESHOT:
        debug_println("ONE-SHOT (Mode 1)");
        debug_print("  P1: ");
        debug_print_uint(cfg.phase1_duration_ms);
        debug_print("ms → ");
        debug_print_uint(cfg.phase1_output_state);
        debug_println("");
        debug_print("  P2: ");
        debug_print_uint(cfg.phase2_duration_ms);
        debug_print("ms → ");
        debug_print_uint(cfg.phase2_output_state);
        debug_println("");
        debug_print("  P3: ");
        debug_print_uint(cfg.phase3_duration_ms);
        debug_print("ms → ");
        debug_print_uint(cfg.phase3_output_state);
        debug_println("");
        break;

      case TIMER_MODE_2_MONOSTABLE:
        debug_println("MONOSTABLE (Mode 2)");
        debug_print("  Pulse: ");
        debug_print_uint(cfg.pulse_duration_ms);
        debug_println("ms");
        debug_print("  P1 (rest): ");
        debug_print_uint(cfg.phase1_output_state);
        debug_println("");
        debug_print("  P2 (pulse): ");
        debug_print_uint(cfg.phase2_output_state);
        debug_println("");
        break;

      case TIMER_MODE_3_ASTABLE:
        debug_println("ASTABLE (Mode 3)");
        debug_print("  ON: ");
        debug_print_uint(cfg.on_duration_ms);
        debug_println("ms");
        debug_print("  OFF: ");
        debug_print_uint(cfg.off_duration_ms);
        debug_println("ms");
        debug_print("  P1 (ON): ");
        debug_print_uint(cfg.phase1_output_state);
        debug_println("");
        debug_print("  P2 (OFF): ");
        debug_print_uint(cfg.phase2_output_state);
        debug_println("");
        break;

      case TIMER_MODE_4_INPUT_TRIGGERED:
        debug_println("INPUT-TRIGGERED (Mode 4)");
        debug_print("  Discrete Input: ");
        debug_print_uint(cfg.input_dis);
        debug_println("");
        debug_print("  Trigger edge: ");
        debug_println(cfg.trigger_edge == 1 ? "RISING (0→1)" : "FALLING (1→0)");
        debug_print("  Delay: ");
        debug_print_uint(cfg.delay_ms);
        debug_println("ms");
        debug_print("  Output level: ");
        debug_print_uint(cfg.phase1_output_state);
        debug_println("");
        break;

      default:
        debug_println("UNKNOWN MODE");
        break;
    }

    debug_print("  Output COIL: ");
    debug_print_uint(cfg.output_coil);
    debug_println("");
    debug_print("  Control Register: ");
    debug_print_uint(cfg.ctrl_reg);
    debug_println("");
    debug_println("");
  }
}

/* ============================================================================
 * SHOW TIMER (SPECIFIC ID)
 * ============================================================================ */

void cli_cmd_show_timer(uint8_t id, bool verbose) {
  if (id < 1 || id > 4) {
    debug_printf("SHOW TIMER: invalid ID %d (expected 1-4)\n", id);
    return;
  }

  TimerConfig cfg;
  if (!timer_engine_get_config(id, &cfg)) {
    debug_printf("SHOW TIMER: could not get config for timer %d\n", id);
    return;
  }

  debug_println("");
  debug_print("=== TIMER ");
  debug_print_uint(id);
  if (verbose) {
    debug_print(" (VERBOSE MODE)");
  }
  debug_println(" ===\n");

  if (!cfg.enabled) {
    debug_println("Status: DISABLED\n");
    return;
  }

  debug_println("Status: ENABLED");
  debug_println("");

  // Show mode and parameters
  switch (cfg.mode) {
    case TIMER_MODE_1_ONESHOT:
      debug_println("Mode: ONE-SHOT (Mode 1 - 3-phase sequence)");
      debug_println("");
      debug_println("Phase 1:");
      debug_print("  Duration: ");
      debug_print_uint(cfg.phase1_duration_ms);
      debug_println("ms");
      debug_print("  Output State: ");
      debug_print_uint(cfg.phase1_output_state);
      debug_println("");

      debug_println("Phase 2:");
      debug_print("  Duration: ");
      debug_print_uint(cfg.phase2_duration_ms);
      debug_println("ms");
      debug_print("  Output State: ");
      debug_print_uint(cfg.phase2_output_state);
      debug_println("");

      debug_println("Phase 3:");
      debug_print("  Duration: ");
      debug_print_uint(cfg.phase3_duration_ms);
      debug_println("ms");
      debug_print("  Output State: ");
      debug_print_uint(cfg.phase3_output_state);
      debug_println("");
      break;

    case TIMER_MODE_2_MONOSTABLE:
      debug_println("Mode: MONOSTABLE (Mode 2 - Retriggerable pulse)");
      debug_println("");
      debug_print("Pulse Duration: ");
      debug_print_uint(cfg.pulse_duration_ms);
      debug_println("ms");
      debug_print("Rest Output State: ");
      debug_print_uint(cfg.phase1_output_state);
      debug_println("");
      debug_print("Pulse Output State: ");
      debug_print_uint(cfg.phase2_output_state);
      debug_println("");
      break;

    case TIMER_MODE_3_ASTABLE:
      debug_println("Mode: ASTABLE (Mode 3 - Oscillator/Blink)");
      debug_println("");
      debug_print("ON Duration: ");
      debug_print_uint(cfg.on_duration_ms);
      debug_println("ms");
      debug_print("OFF Duration: ");
      debug_print_uint(cfg.off_duration_ms);
      debug_println("ms");
      debug_print("ON Output State: ");
      debug_print_uint(cfg.phase1_output_state);
      debug_println("");
      debug_print("OFF Output State: ");
      debug_print_uint(cfg.phase2_output_state);
      debug_println("");
      break;

    case TIMER_MODE_4_INPUT_TRIGGERED:
      debug_println("Mode: INPUT-TRIGGERED (Mode 4 - Edge detection)");
      debug_println("");
      debug_print("Discrete Input: ");
      debug_print_uint(cfg.input_dis);
      debug_println("");
      debug_print("Trigger Edge: ");
      debug_println(cfg.trigger_edge == 1 ? "RISING (0→1)" : "FALLING (1→0)");
      debug_print("Delay: ");
      debug_print_uint(cfg.delay_ms);
      debug_println("ms");
      debug_print("Output Level: ");
      debug_print_uint(cfg.phase1_output_state);
      debug_println("");
      break;

    default:
      debug_println("Mode: UNKNOWN");
      break;
  }

  debug_println("");
  debug_print("Output Coil: ");
  debug_print_uint(cfg.output_coil);
  debug_println("");

  debug_print("Control Register: ");
  debug_print_uint(cfg.ctrl_reg);
  debug_println("");

  // VERBOSE MODE: Extended runtime state
  if (verbose) {
    debug_println("");
    debug_println("=== EXTENDED RUNTIME DETAILS ===");
    debug_println("");

    // Read control register and show runtime state
    uint16_t ctrl_val = registers_get_holding_register(cfg.ctrl_reg);
    debug_println("Control Register Runtime State:");
    debug_print("  Running: ");
    debug_println((ctrl_val & 0x01) ? "YES (bit 0 set)" : "NO (bit 0 clear)");
    debug_print("  Stop Requested: ");
    debug_println((ctrl_val & 0x02) ? "YES (bit 1 set)" : "NO (bit 1 clear)");
    debug_print("  Reset Requested: ");
    debug_println((ctrl_val & 0x04) ? "YES (bit 2 set)" : "NO (bit 2 clear)");
    debug_println("");

    // Read output coil current state
    uint16_t coil_state = registers_get_coil(cfg.output_coil);
    debug_println("Output Coil Current State:");
    debug_print("  Coil ");
    debug_print_uint(cfg.output_coil);
    debug_print(": ");
    debug_println(coil_state ? "ON (1)" : "OFF (0)");
    debug_println("");

    // Mode-specific hints
    debug_println("Mode-Specific Performance Notes:");
    switch (cfg.mode) {
      case TIMER_MODE_1_ONESHOT:
        debug_println("  - ONE-SHOT: Executes once per START command");
        debug_println("  - Total duration: p1 + p2 + p3 ms");
        debug_print("  - Calculated: ");
        debug_print_uint(cfg.phase1_duration_ms + cfg.phase2_duration_ms + cfg.phase3_duration_ms);
        debug_println(" ms per cycle");
        break;
      case TIMER_MODE_2_MONOSTABLE:
        debug_println("  - MONOSTABLE: Retriggerable pulse timer");
        debug_println("  - Trigger extends pulse duration (does not queue)");
        debug_print("  - Pulse width: ");
        debug_print_uint(cfg.pulse_duration_ms);
        debug_println(" ms");
        break;
      case TIMER_MODE_3_ASTABLE: {
        debug_println("  - ASTABLE: Free-running oscillator");
        debug_print("  - Period: ");
        debug_print_uint(cfg.on_duration_ms + cfg.off_duration_ms);
        debug_println(" ms");
        float freq_hz = 1000.0f / (float)(cfg.on_duration_ms + cfg.off_duration_ms);
        debug_print("  - Frequency: ");
        debug_print_float(freq_hz);
        debug_println(" Hz");
        break;
      }
      case TIMER_MODE_4_INPUT_TRIGGERED:
        debug_println("  - INPUT-TRIGGERED: Responds to discrete input changes");
        debug_print("  - Monitors DI ");
        debug_print_uint(cfg.input_dis);
        debug_println("");
        debug_println("  - Edge detection: Checked every cycle (~10ms)");
        break;
      default:
        break;
    }
    debug_println("");

    // Control commands
    debug_println("Runtime Control Commands:");
    debug_print("  write reg ");
    debug_print_uint(cfg.ctrl_reg);
    debug_println(" 1  - START timer");
    debug_print("  write reg ");
    debug_print_uint(cfg.ctrl_reg);
    debug_println(" 2  - STOP timer");
    debug_print("  write reg ");
    debug_print_uint(cfg.ctrl_reg);
    debug_println(" 4  - RESET timer");
    debug_println("");
  }

  debug_println("");
}

/* ============================================================================
 * SHOW REGISTERS
 * ============================================================================ */

void cli_cmd_show_registers(uint16_t start, uint16_t count) {
  debug_println("\n=== HOLDING REGISTERS (0..159) ===\n");

  // Show registers in groups of 8 (Mega2560 format)
  for (uint16_t base = 0; base < HOLDING_REGS_SIZE && base < 160; base += 8) {
    debug_print_uint(base);
    debug_print(": ");

    for (uint8_t i = 0; i < 8 && (base + i) < HOLDING_REGS_SIZE; i++) {
      uint16_t value = registers_get_holding_register(base + i);
      debug_print_uint(value);
      debug_print("\t");
    }
    debug_println("");
  }

  debug_println("");
}

/* ============================================================================
 * SHOW COILS
 * ============================================================================ */

void cli_cmd_show_coils(void) {
  debug_println("\n=== COILS (0..63) ===\n");

  // Show coils in groups of 16 (Mega2560 format)
  for (uint16_t base = 0; base < (COILS_SIZE * 8) && base < 64; base += 16) {
    debug_print_uint(base);
    debug_print(": ");

    for (uint8_t i = 0; i < 16 && (base + i) < (COILS_SIZE * 8); i++) {
      bool state = registers_get_coil(base + i);
      debug_print(state ? "1" : "0");
      debug_print(" ");
    }
    debug_println("");
  }

  debug_println("");
}

/* ============================================================================
 * SHOW INPUTS
 * ============================================================================ */

void cli_cmd_show_inputs(void) {
  debug_println("\n=== DISCRETE INPUTS (0..255) ===\n");

  // Show discrete inputs in groups of 16 (Mega2560 format)
  for (uint16_t base = 0; base < (DISCRETE_INPUTS_SIZE * 8) && base < 256; base += 16) {
    debug_print_uint(base);
    debug_print(": ");

    for (uint8_t i = 0; i < 16 && (base + i) < (DISCRETE_INPUTS_SIZE * 8); i++) {
      bool state = registers_get_discrete_input(base + i);
      debug_print(state ? "1" : "0");
      debug_print(" ");
    }
    debug_println("");
  }

  debug_println("");
}

/* ============================================================================
 * SHOW ST LOGIC STATS (from Modbus Input Registers 252-293)
 * ============================================================================ */

void cli_cmd_show_st_logic_stats_modbus(void) {
  debug_println("\n=== ST Logic Performance Statistics (Modbus IR 252-293) ===\n");

  uint16_t *input_regs = registers_get_input_regs();

  // Helper to read 32-bit value from 2x input registers
  auto read_32bit = [&](uint16_t addr) -> uint32_t {
    uint32_t high = input_regs[addr];
    uint32_t low = input_regs[addr + 1];
    return (high << 16) | low;
  };

  debug_println("Per-Program Min Execution Time (µs):");
  for (uint8_t i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
    uint32_t min_us = read_32bit(ST_LOGIC_MIN_EXEC_TIME_REG_BASE + (i * 2));
    debug_printf("  Logic%d: %u µs (%.3f ms)\n", i + 1, (unsigned int)min_us, min_us / 1000.0);
  }

  debug_println("\nPer-Program Max Execution Time (µs):");
  for (uint8_t i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
    uint32_t max_us = read_32bit(ST_LOGIC_MAX_EXEC_TIME_REG_BASE + (i * 2));
    debug_printf("  Logic%d: %u µs (%.3f ms)\n", i + 1, (unsigned int)max_us, max_us / 1000.0);
  }

  debug_println("\nPer-Program Avg Execution Time (µs):");
  for (uint8_t i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
    uint32_t avg_us = read_32bit(ST_LOGIC_AVG_EXEC_TIME_REG_BASE + (i * 2));
    debug_printf("  Logic%d: %u µs (%.3f ms)\n", i + 1, (unsigned int)avg_us, avg_us / 1000.0);
  }

  debug_println("\nPer-Program Overrun Count:");
  for (uint8_t i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
    uint32_t overruns = read_32bit(ST_LOGIC_OVERRUN_COUNT_REG_BASE + (i * 2));
    debug_printf("  Logic%d: %u\n", i + 1, (unsigned int)overruns);
  }

  debug_println("\nGlobal Cycle Statistics:");
  uint32_t cycle_min = read_32bit(ST_LOGIC_CYCLE_MIN_REG);
  uint32_t cycle_max = read_32bit(ST_LOGIC_CYCLE_MAX_REG);
  uint32_t cycle_overruns = read_32bit(ST_LOGIC_CYCLE_OVERRUN_REG);
  uint32_t total_cycles = read_32bit(ST_LOGIC_TOTAL_CYCLES_REG);
  uint32_t interval = read_32bit(ST_LOGIC_EXEC_INTERVAL_RO_REG);

  debug_printf("  Cycle Min: %u ms\n", (unsigned int)cycle_min);
  debug_printf("  Cycle Max: %u ms\n", (unsigned int)cycle_max);
  debug_printf("  Cycle Overruns: %u", (unsigned int)cycle_overruns);
  if (total_cycles > 0) {
    float overrun_pct = (float)cycle_overruns / total_cycles * 100.0f;
    debug_printf(" (%.1f%%)", overrun_pct);
  }
  debug_println("");
  debug_printf("  Total Cycles: %u\n", (unsigned int)total_cycles);
  debug_printf("  Execution Interval: %u ms\n\n", (unsigned int)interval);
}

/* ============================================================================
 * SHOW VERSION
 * ============================================================================ */

void cli_cmd_show_version(void) {
  debug_println("\n=== FIRMWARE VERSION ===\n");
  debug_print("Version: ");
  debug_println(PROJECT_VERSION);
  debug_print("Build:   #");
  debug_print_uint(BUILD_NUMBER);
  debug_print(" (");
  debug_print(BUILD_TIMESTAMP);
  debug_println(")");
  debug_print("Git:     ");
  debug_print(GIT_BRANCH);
  debug_print("@");
  debug_println(GIT_HASH);
  debug_println("Target:  ESP32-WROOM-32");
  debug_println("Project: Modbus RTU Server");
  debug_println("");
}

/* ============================================================================
 * SHOW GPIO
 * ============================================================================ */

void cli_cmd_show_gpio(void) {
  debug_println("\n=== GPIO MAPPING ===\n");

  // GPIO2 status
  debug_print("GPIO 2 Status: ");
  if (g_persist_config.gpio2_user_mode == 0) {
    debug_println("HEARTBEAT (LED blink aktiv)");
  } else {
    debug_println("USER MODE (frigivet til bruger)");
  }
  debug_println("  Kommandoer: 'set gpio 2 enable' / 'set gpio 2 disable'");
  debug_println("");

  // Modbus Slave pins (kun hvis enabled)
  if (g_persist_config.modbus_slave.enabled) {
    debug_println("UART1 (Modbus Slave):");
    char buf[64];
    snprintf(buf, sizeof(buf), "  GPIO %-2d - RX", PIN_UART1_RX);
    debug_println(buf);
    snprintf(buf, sizeof(buf), "  GPIO %-2d - TX", PIN_UART1_TX);
    debug_println(buf);
    snprintf(buf, sizeof(buf), "  GPIO %-2d - RS485 DIR", PIN_RS485_DIR);
    debug_println(buf);
    debug_println("");
  }

  // Modbus Master pins (kun hvis enabled)
  if (g_persist_config.modbus_master.enabled) {
    debug_println("UART (Modbus Master):");
    char buf[64];
    snprintf(buf, sizeof(buf), "  GPIO %-2d - TX", MODBUS_MASTER_TX_PIN);
    debug_println(buf);
    snprintf(buf, sizeof(buf), "  GPIO %-2d - RX", MODBUS_MASTER_RX_PIN);
    debug_println(buf);
    snprintf(buf, sizeof(buf), "  GPIO %-2d - RS485 DE/RE", MODBUS_MASTER_DE_PIN);
    debug_println(buf);
    debug_println("");
  }

  // PCNT Counter pins (kun hvis counter enabled med hw_gpio konfigureret)
  {
    bool any_counter = false;
    for (uint8_t i = 0; i < COUNTER_COUNT; i++) {
      const CounterConfig &cc = g_persist_config.counters[i];
      if (cc.enabled && cc.hw_mode == COUNTER_HW_PCNT && cc.hw_gpio > 0) {
        if (!any_counter) {
          debug_println("PCNT Counters:");
          any_counter = true;
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "  GPIO %-2d - Counter %d HW (PCNT)", cc.hw_gpio, i + 1);
        debug_println(buf);
      }
    }
    if (any_counter) debug_println("");
  }

  // Show user-configured GPIO mappings (exclude ST Logic bindings)
  uint8_t gpio_count = 0;
  for (uint8_t i = 0; i < g_persist_config.var_map_count; i++) {
    const VariableMapping* map = &g_persist_config.var_maps[i];
    if (map->source_type == MAPPING_SOURCE_GPIO) {
      gpio_count++;
    }
  }

  if (gpio_count > 0) {
    debug_print("User configured GPIO (");
    debug_print_uint(gpio_count);
    debug_println(" mappings):");
    for (uint8_t i = 0; i < g_persist_config.var_map_count; i++) {
      const VariableMapping* map = &g_persist_config.var_maps[i];
      if (map->source_type != MAPPING_SOURCE_GPIO) continue;

      debug_print("  GPIO ");
      debug_print_uint(map->gpio_pin);
      debug_print(" - ");
      if (map->is_input) {
        debug_print("INPUT:");
        debug_print_uint(map->input_reg);
      } else {
        debug_print("COIL:");
        debug_print_uint(map->coil_reg);
      }
      debug_print("  [fjern: 'no set gpio ");
      debug_print_uint(map->gpio_pin);
      debug_print("']");
      debug_println("");
    }
  } else {
    debug_println("(No user-configured GPIO mappings)");
  }
  debug_println("");
}

/* ============================================================================
 * SHOW GPIO <PIN> (specific pin details)
 * ============================================================================ */

void cli_cmd_show_gpio_pin(uint8_t pin) {
  debug_println("\n=== GPIO PIN DETAILS ===\n");

  debug_print("Pin: GPIO ");
  debug_print_uint(pin);
  debug_println("");

  // Check if this is GPIO 2 (heartbeat special case)
  if (pin == 2) {
    debug_print("Mode: ");
    if (g_persist_config.gpio2_user_mode == 0) {
      debug_println("HEARTBEAT (LED blink)");
      debug_println("  System-managed, blinks to indicate alive status");
    } else {
      debug_println("USER MODE (available for mapping)");
    }
    debug_println("Commands:");
    debug_println("  set gpio 2 enable   - Release for user control");
    debug_println("  set gpio 2 disable  - Return to heartbeat mode");
    debug_println("");
  }

  // Check if this is a hardware-reserved pin (dynamisk baseret på config)
  bool is_reserved = false;
  char reservation_buf[64];

  // Modbus Slave pins
  if (g_persist_config.modbus_slave.enabled) {
    if (pin == PIN_UART1_RX) { snprintf(reservation_buf, sizeof(reservation_buf), "UART1 RX (Modbus Slave)"); is_reserved = true; }
    else if (pin == PIN_UART1_TX) { snprintf(reservation_buf, sizeof(reservation_buf), "UART1 TX (Modbus Slave)"); is_reserved = true; }
    else if (pin == PIN_RS485_DIR) { snprintf(reservation_buf, sizeof(reservation_buf), "RS485 DIR (Modbus Slave)"); is_reserved = true; }
  }

  // Modbus Master pins
  if (!is_reserved && g_persist_config.modbus_master.enabled) {
    if (pin == MODBUS_MASTER_TX_PIN) { snprintf(reservation_buf, sizeof(reservation_buf), "UART TX (Modbus Master)"); is_reserved = true; }
    else if (pin == MODBUS_MASTER_RX_PIN) { snprintf(reservation_buf, sizeof(reservation_buf), "UART RX (Modbus Master)"); is_reserved = true; }
    else if (pin == MODBUS_MASTER_DE_PIN) { snprintf(reservation_buf, sizeof(reservation_buf), "RS485 DE/RE (Modbus Master)"); is_reserved = true; }
  }

  // Counter PCNT pins
  if (!is_reserved) {
    for (uint8_t i = 0; i < COUNTER_COUNT; i++) {
      const CounterConfig &cc = g_persist_config.counters[i];
      if (cc.enabled && cc.hw_mode == COUNTER_HW_PCNT && cc.hw_gpio > 0 && cc.hw_gpio == pin) {
        snprintf(reservation_buf, sizeof(reservation_buf), "Counter %d HW (PCNT)", i + 1);
        is_reserved = true;
        break;
      }
    }
  }

  if (is_reserved) {
    debug_print("Reservation: ");
    debug_println(reservation_buf);
    debug_println("  WARNING: This pin is reserved for hardware functionality!");
    debug_println("");
  }

  // Look for user mapping
  bool found_mapping = false;
  for (uint8_t i = 0; i < g_persist_config.var_map_count; i++) {
    const VariableMapping* map = &g_persist_config.var_maps[i];
    if (map->source_type == MAPPING_SOURCE_GPIO && map->gpio_pin == pin) {
      found_mapping = true;

      debug_println("User Mapping:");
      debug_print("  Direction: ");
      debug_println(map->is_input ? "INPUT" : "OUTPUT");

      if (map->is_input) {
        debug_print("  Modbus Discrete Input: ");
        debug_print_uint(map->input_reg);
        debug_println("");
        debug_println("  Function: Physical GPIO input -> Modbus discrete input register");
      } else {
        debug_print("  Modbus Coil: ");
        debug_print_uint(map->coil_reg);
        debug_println("");
        debug_println("  Function: Modbus coil register -> Physical GPIO output");
      }

      // Show associated counter/timer if any
      if (map->associated_counter != 0xff) {
        debug_print("  Associated Counter: ");
        debug_print_uint(map->associated_counter);
        debug_println("");
      }
      if (map->associated_timer != 0xff) {
        debug_print("  Associated Timer: ");
        debug_print_uint(map->associated_timer);
        debug_println("");
      }

      debug_println("");
      debug_println("Commands:");
      debug_print("  no set gpio ");
      debug_print_uint(pin);
      debug_println("  - Remove this mapping");
      break;
    }
  }

  if (!found_mapping && !is_reserved && pin != 2) {
    debug_println("Status: AVAILABLE (not mapped)");
    debug_println("");
    debug_println("Available Commands:");
    debug_print("  set gpio ");
    debug_print_uint(pin);
    debug_println(" input <idx>  - Map to discrete input");
    debug_print("  set gpio ");
    debug_print_uint(pin);
    debug_println(" coil <idx>   - Map to coil output");
  }

  debug_println("=====================\n");
}

/* ============================================================================
 * SHOW ECHO
 * ============================================================================ */

void cli_cmd_show_echo(void) {
  debug_println("\n=== REMOTE ECHO ===");
  if (cli_shell_get_remote_echo()) {
    debug_println("Status: ON (characters are echoed back to terminal)");
  } else {
    debug_println("Status: OFF (characters are NOT echoed back)");
  }
  debug_println("Set: set echo <on|off>");
  debug_println("=====================\n");
}

/* ============================================================================
 * SHOW WIFI
 * ============================================================================ */

void cli_cmd_show_ethernet(void) {
  debug_println("\n=== ETHERNET STATUS (W5500) ===");

#ifndef ETHERNET_W5500_ENABLED
  debug_println("Ethernet: NOT COMPILED");
  debug_println("  Build med -DETHERNET_W5500_ENABLED i platformio.ini");
  debug_println("  for at aktivere W5500 hardware support.");
  debug_println("");
  return;
#endif

  if (!g_persist_config.network.ethernet.enabled) {
    debug_println("Ethernet: DISABLED");
    debug_println("(Enable with: set ethernet enable)");
    debug_println("");
    return;
  }

  if (!ethernet_driver_is_connected()) {
    debug_println("Ethernet: NOT CONNECTED");
    debug_println("(Check W5500 wiring and cable)");
  } else {
    debug_println("Ethernet: CONNECTED");

    // Show IP
    uint32_t ip = ethernet_driver_get_local_ip();
    if (ip != 0) {
      char ip_str[16];
      network_config_ip_to_str(ip, ip_str);
      debug_print("Local IP: ");
      debug_println(ip_str);
    }

    // Gateway
    uint32_t gw = ethernet_driver_get_gateway();
    if (gw != 0) {
      char gw_str[16];
      network_config_ip_to_str(gw, gw_str);
      debug_print("Gateway: ");
      debug_println(gw_str);
    }

    // Netmask
    uint32_t nm = ethernet_driver_get_netmask();
    if (nm != 0) {
      char nm_str[16];
      network_config_ip_to_str(nm, nm_str);
      debug_print("Netmask: ");
      debug_println(nm_str);
    }

    // DNS
    uint32_t dns = ethernet_driver_get_dns();
    if (dns != 0) {
      char dns_str[16];
      network_config_ip_to_str(dns, dns_str);
      debug_print("DNS: ");
      debug_println(dns_str);
    }

    // Speed and duplex
    debug_print("Link Speed: ");
    debug_print_uint(ethernet_driver_get_speed());
    debug_print(" Mbps ");
    debug_println(ethernet_driver_is_full_duplex() ? "Full-Duplex" : "Half-Duplex");

    // MAC address
    char mac_str[18];
    ethernet_driver_get_mac_str(mac_str);
    debug_print("MAC Address: ");
    debug_println(mac_str);

    // Uptime
    uint32_t uptime = ethernet_driver_get_uptime_ms();
    debug_print("Uptime: ");
    debug_print_uint(uptime / 1000);
    debug_println(" sec");
  }

  // Show config
  debug_println("");
  debug_println("Configuration:");
  debug_print("  DHCP: ");
  debug_println(g_persist_config.network.ethernet.dhcp_enabled ? "ON" : "OFF");

  if (!g_persist_config.network.ethernet.dhcp_enabled) {
    char ip_str[16];
    network_config_ip_to_str(g_persist_config.network.ethernet.static_ip, ip_str);
    debug_print("  Static IP: ");
    debug_println(ip_str);
  }

  debug_println("");
}

void cli_cmd_show_wifi(void) {
  debug_println("\n=== WI-FI STATUS ===");

  // Get network state from network manager
  if (!network_manager_is_wifi_connected()) {
    debug_println("Wi-Fi Status: NOT CONNECTED");
    debug_println("(Connect with: connect wifi)");
  } else {
    debug_println("Wi-Fi Status: CONNECTED");

    // Show actual IP address from WiFi driver (works for both DHCP and static)
    uint32_t ip = network_manager_get_local_ip();
    if (ip != 0) {
      char ip_str[16];
      network_config_ip_to_str(ip, ip_str);
      debug_print("Local IP: ");
      debug_println(ip_str);
    } else {
      debug_println("Local IP: Acquiring from DHCP...");
    }

    // Show runtime network info (DHCP-assigned gateway/DNS if applicable)
    IPAddress gateway_ip = WiFi.gatewayIP();
    IPAddress dns_ip = WiFi.dnsIP();
    if (gateway_ip[0] != 0) {
      debug_print("Active Gateway: ");
      debug_print_uint(gateway_ip[0]); debug_print(".");
      debug_print_uint(gateway_ip[1]); debug_print(".");
      debug_print_uint(gateway_ip[2]); debug_print(".");
      debug_print_uint(gateway_ip[3]); debug_println("");
    }
    if (dns_ip[0] != 0) {
      debug_print("Active DNS: ");
      debug_print_uint(dns_ip[0]); debug_print(".");
      debug_print_uint(dns_ip[1]); debug_print(".");
      debug_print_uint(dns_ip[2]); debug_print(".");
      debug_print_uint(dns_ip[3]); debug_println("");
    }

    // Show signal strength (RSSI)
    int32_t rssi = WiFi.RSSI();
    debug_print("Signal Strength: ");
    if (rssi < 0) {
      debug_print("-");
      debug_print_uint((uint32_t)(-rssi));  // Print absolute value
    } else {
      debug_print_uint((uint32_t)rssi);
    }
    debug_print(" dBm");
    if (rssi > -50) {
      debug_println(" (Excellent)");
    } else if (rssi > -60) {
      debug_println(" (Very Good)");
    } else if (rssi > -70) {
      debug_println(" (Good)");
    } else if (rssi > -80) {
      debug_println(" (Fair)");
    } else {
      debug_println(" (Weak)");
    }

    // Show MAC address
    debug_print("MAC Address: ");
    debug_println(WiFi.macAddress().c_str());
  }

  // Show config
  debug_println("");
  debug_println("Configuration:");

  if (g_persist_config.network.enabled) {
    debug_println("  Enabled: YES");
  } else {
    debug_println("  Enabled: NO");
  }

  debug_print("  SSID: ");
  debug_println(g_persist_config.network.ssid[0] ? g_persist_config.network.ssid : "(not set)");

  if (g_persist_config.network.dhcp_enabled) {
    debug_println("  IP Mode: DHCP");
  } else {
    debug_println("  IP Mode: STATIC");
    char ip_str[16];
    network_config_ip_to_str(g_persist_config.network.static_ip, ip_str);
    debug_print("  Static IP: ");
    debug_println(ip_str);

    network_config_ip_to_str(g_persist_config.network.static_gateway, ip_str);
    debug_print("  Gateway: ");
    debug_println(ip_str);

    network_config_ip_to_str(g_persist_config.network.static_netmask, ip_str);
    debug_print("  Netmask: ");
    debug_println(ip_str);

    network_config_ip_to_str(g_persist_config.network.static_dns, ip_str);
    debug_print("  DNS: ");
    debug_println(ip_str);
  }

  // Show Telnet status
  if (g_persist_config.network.telnet_enabled) {
    debug_print("  Telnet: ENABLED (port ");
    debug_print_uint(g_persist_config.network.telnet_port);
    debug_print(", user: ");
    debug_print(g_persist_config.network.telnet_username);
    debug_print(")");
    if (!network_manager_is_wifi_connected()) {
      debug_println(" - waiting for WiFi connection...");
    } else {
      debug_println("");
    }
  } else {
    debug_println("  Telnet: DISABLED");
  }

  debug_println("\nCommands:");
  debug_println("  set wifi ssid <name>");
  debug_println("  set wifi password <pwd>");
  debug_println("  set wifi dhcp on|off");
  debug_println("  set wifi ip <ip>");
  debug_println("  set wifi gateway <ip>");
  debug_println("  set wifi netmask <ip>");
  debug_println("  set wifi dns <ip>");
  debug_println("  set wifi telnet enable|disable");
  debug_println("  set wifi telnet-user <username>");
  debug_println("  set wifi telnet-pass <password>");
  debug_println("  set wifi telnet-port <port>");
  debug_println("  connect wifi");
  debug_println("  disconnect wifi");
  debug_println("  save (to persist changes)\n");
}

/* ============================================================================
 * SHOW HTTP (v6.0.0+)
 * ============================================================================ */

void cli_cmd_show_http(void) {
  debug_println("\n=== HTTP REST API STATUS ===");

  // Server status
  debug_print("Server Status: ");
  if (http_server_is_running()) {
    debug_println("RUNNING");
  } else {
    debug_println("STOPPED");
  }

  // Configuration
  debug_println("\n--- Configuration ---");
  debug_print("Server: ");
  debug_println(g_persist_config.network.http.enabled ? "ENABLED" : "DISABLED");

  debug_print("TLS/HTTPS: ");
  if (g_persist_config.network.http.tls_enabled) {
    debug_print("ENABLED");
    if (http_server_is_tls_active()) {
      debug_println(" (active)");
    } else {
      debug_println(" (reboot required)");
    }
  } else {
    debug_println("DISABLED");
  }

  {
    char cert_info[48];
    if (https_wrapper_get_cert_info(cert_info, sizeof(cert_info)) == 0) {
      debug_print("Certificate: ");
      debug_println(cert_info);
    }
  }

  debug_print("Port: ");
  debug_print_uint(g_persist_config.network.http.port);
  debug_println("");

  debug_print("API: ");
  debug_println(g_persist_config.network.http.api_enabled ? "ENABLED" : "DISABLED");

  debug_print("Auth: ");
  debug_println(g_persist_config.network.http.auth_enabled ? "ENABLED" : "DISABLED");

  if (g_persist_config.network.http.auth_enabled) {
    debug_print("Username: ");
    debug_println(g_persist_config.network.http.username);
    debug_println("Password: ***");
  }

  // Statistics
  const HttpServerStats *stats = http_server_get_stats();
  if (stats) {
    debug_println("\n--- Statistics ---");
    debug_print("Total Requests: ");
    debug_print_uint(stats->total_requests);
    debug_println("");

    debug_print("Successful (2xx): ");
    debug_print_uint(stats->successful_requests);
    debug_println("");

    debug_print("Client Errors (4xx): ");
    debug_print_uint(stats->client_errors);
    debug_println("");

    debug_print("Server Errors (5xx): ");
    debug_print_uint(stats->server_errors);
    debug_println("");

    if (g_persist_config.network.http.auth_enabled) {
      debug_print("Auth Failures: ");
      debug_print_uint(stats->auth_failures);
      debug_println("");
    }
  }

  // API Endpoints
  debug_println("\n--- API Endpoints ---");
  debug_println("  GET  /api/                 - List all endpoints");
  debug_println("  GET  /api/status           - System info");
  debug_println("  GET  /api/counters         - All counters");
  debug_println("  GET  /api/counters/{1-4}   - Single counter");
  debug_println("  GET  /api/timers           - All timers");
  debug_println("  GET  /api/timers/{1-4}     - Single timer");
  debug_println("  GET  /api/registers/hr/{addr}  - Read holding register");
  debug_println("  POST /api/registers/hr/{addr}  - Write holding register");
  debug_println("  GET  /api/registers/ir/{addr}  - Read input register");
  debug_println("  GET  /api/registers/coils/{addr} - Read coil");
  debug_println("  POST /api/registers/coils/{addr} - Write coil");
  debug_println("  GET  /api/registers/di/{addr}  - Read discrete input");
  debug_println("  GET  /api/logic            - ST Logic programs");
  debug_println("  GET  /api/logic/{1-4}      - Single program");
  debug_println("  GET  /api/logic/{1-4}/source  - Download ST code");
  debug_println("  POST /api/logic/{1-4}/source  - Upload ST code");

  // Commands
  debug_println("\nCommands:");
  debug_println("  set http enable|disable");
  debug_println("  set http port <port>");
  debug_println("  set http api enable|disable");
  debug_println("  set http auth enable|disable");
  debug_println("  set http username <user>");
  debug_println("  set http password <pass>");
  debug_println("  save (to persist changes)\n");
}

/* ============================================================================
 * READ REG
 * ============================================================================ */

void cli_cmd_read_reg(uint8_t argc, char* argv[]) {
  // read holding-reg <id> [antal] [int|uint|real]  (antal og type er optional)
  if (argc < 1) {
    debug_println("READ HOLDING-REG: manglende parametre");
    debug_println("  Brug: read holding-reg <id> [antal] [int|uint|dint|dword|real]");
    debug_println("  Eksempel: read holding-reg90           (l\u00e6ser 1 register som uint)");
    debug_println("  Eksempel: read holding-reg90 10        (l\u00e6ser 10 registre som uint)");
    debug_println("  Eksempel: read holding-reg90 1 int     (l\u00e6ser 1 register som signed int)");
    debug_println("  Eksempel: read holding-reg100 5 uint   (l\u00e6ser 5 registre som unsigned int)");
    debug_println("  Eksempel: read holding-reg100 dint     (l\u00e6ser 2 registre som DINT/32-bit signed)");
    debug_println("  Eksempel: read holding-reg100 dword    (l\u00e6ser 2 registre som DWORD/32-bit unsigned)");
    debug_println("  Eksempel: read holding-reg100 real     (l\u00e6ser 2 registre som REAL/float)");
    return;
  }

  uint16_t start_addr = atoi(argv[0]);
  uint16_t count = 1;  // Default: 1 register
  bool display_as_signed = false;  // Default: unsigned
  bool display_as_real = false;     // BUG-108: REAL type support
  bool display_as_dint = false;     // DINT (32-bit signed) support
  bool display_as_dword = false;    // DWORD (32-bit unsigned) support

  // Parse count (argv[1])
  if (argc >= 2) {
    // Check if argv[1] is a type keyword or a number
    if (strcasecmp(argv[1], "int") == 0) {
      display_as_signed = true;
    } else if (strcasecmp(argv[1], "uint") == 0) {
      display_as_signed = false;
    } else if (strcasecmp(argv[1], "real") == 0) {
      display_as_real = true;
    } else if (strcasecmp(argv[1], "dint") == 0) {
      display_as_dint = true;
    } else if (strcasecmp(argv[1], "dword") == 0) {
      display_as_dword = true;
    } else {
      count = atoi(argv[1]);
    }
  }

  // Parse type (argv[2])
  if (argc >= 3) {
    if (strcasecmp(argv[2], "int") == 0) {
      display_as_signed = true;
      display_as_real = false;
      display_as_dint = false;
      display_as_dword = false;
    } else if (strcasecmp(argv[2], "uint") == 0) {
      display_as_signed = false;
      display_as_real = false;
      display_as_dint = false;
      display_as_dword = false;
    } else if (strcasecmp(argv[2], "real") == 0) {
      display_as_real = true;
      display_as_signed = false;
      display_as_dint = false;
      display_as_dword = false;
    } else if (strcasecmp(argv[2], "dint") == 0) {
      display_as_dint = true;
      display_as_signed = false;
      display_as_real = false;
      display_as_dword = false;
    } else if (strcasecmp(argv[2], "dword") == 0) {
      display_as_dword = true;
      display_as_signed = false;
      display_as_real = false;
      display_as_dint = false;
    }
  }

  // Validate parameters
  if (start_addr >= HOLDING_REGS_SIZE) {
    debug_print("READ HOLDING-REG: startadresse udenfor omr\u00e5de (max ");
    debug_print_uint(HOLDING_REGS_SIZE - 1);
    debug_println(")");
    return;
  }

  if (count == 0) {
    debug_println("READ HOLDING-REG: antal skal v\u00e6re st\u00f8rre end 0");
    return;
  }

  // Adjust count if it exceeds available registers
  if (start_addr + count > HOLDING_REGS_SIZE) {
    count = HOLDING_REGS_SIZE - start_addr;
    debug_print("READ HOLDING-REG: justeret antal til ");
    debug_print_uint(count);
    debug_println(" registre");
  }

  // BUG-108 FIX: REAL type requires 2 consecutive registers
  // BUG-137 FIX: Support count parameter for REAL array reading
  if (display_as_real) {
    // Validate that we have enough registers available (count REAL values = count * 2 registers)
    if (start_addr + (count * 2) > HOLDING_REGS_SIZE) {
      debug_print("READ HOLDING-REG: REAL kr\u00e6ver ");
      debug_print_uint(count * 2);
      debug_println(" registre, adresse udenfor omr\u00e5de");
      return;
    }

    debug_println("\n=== L\u00c6SNING AF HOLDING REGISTERS ===");
    debug_print("Adresse ");
    debug_print_uint(start_addr);
    debug_print("-");
    debug_print_uint(start_addr + (count * 2) - 1);
    debug_println(" (REAL/float):\n");

    // Read count REAL values (each REAL = 2 registers)
    for (uint16_t i = 0; i < count; i++) {
      uint16_t addr = start_addr + (i * 2);

      // BUG-124 FIX: Counter writes LSW first, MSW second (little-endian register order)
      uint16_t low_word = registers_get_holding_register(addr);          // LSW at base address
      uint16_t high_word = registers_get_holding_register(addr + 1);     // MSW at base+1
      uint32_t bits = ((uint32_t)high_word << 16) | low_word;
      float real_value;
      memcpy(&real_value, &bits, sizeof(float));

      debug_print("Reg[");
      debug_print_uint(addr);
      debug_print("-");
      debug_print_uint(addr + 1);
      debug_print("]: ");

      // Display float with 6 decimal precision
      char float_str[32];
      snprintf(float_str, sizeof(float_str), "%.6f", real_value);
      debug_print(float_str);
      debug_print(" (0x");
      char hex_str[16];
      snprintf(hex_str, sizeof(hex_str), "%04X%04X", high_word, low_word);
      debug_print(hex_str);
      debug_println(")");
    }
    debug_println("");

    // Call reset-on-read handler for all registers
    extern void modbus_handle_reset_on_read(uint16_t starting_address, uint16_t quantity);
    modbus_handle_reset_on_read(start_addr, count * 2);
    return;
  }

  // DINT type (32-bit signed integer, 2 consecutive registers)
  // BUG-137 FIX: Support count parameter for DINT array reading
  if (display_as_dint) {
    // Validate that we have enough registers available (count DINT values = count * 2 registers)
    if (start_addr + (count * 2) > HOLDING_REGS_SIZE) {
      debug_print("READ HOLDING-REG: DINT kr\u00e6ver ");
      debug_print_uint(count * 2);
      debug_println(" registre, adresse udenfor omr\u00e5de");
      return;
    }

    debug_println("\n=== L\u00c6SNING AF HOLDING REGISTERS ===");
    debug_print("Adresse ");
    debug_print_uint(start_addr);
    debug_print("-");
    debug_print_uint(start_addr + (count * 2) - 1);
    debug_println(" (DINT/32-bit signed):\n");

    // Read count DINT values (each DINT = 2 registers)
    for (uint16_t i = 0; i < count; i++) {
      uint16_t addr = start_addr + (i * 2);

      // BUG-124 FIX: Counter writes LSW first, MSW second (little-endian register order)
      uint16_t low_word = registers_get_holding_register(addr);          // LSW at base address
      uint16_t high_word = registers_get_holding_register(addr + 1);     // MSW at base+1
      uint32_t bits = ((uint32_t)high_word << 16) | low_word;
      int32_t dint_value = (int32_t)bits;

      debug_print("Reg[");
      debug_print_uint(addr);
      debug_print("-");
      debug_print_uint(addr + 1);
      debug_print("]: ");

      // Display DINT value
      char dint_str[16];
      snprintf(dint_str, sizeof(dint_str), "%ld", (long)dint_value);
      debug_print(dint_str);
      debug_print(" (0x");
      char hex_str[16];
      snprintf(hex_str, sizeof(hex_str), "%04X%04X", high_word, low_word);
      debug_print(hex_str);
      debug_println(")");
    }
    debug_println("");

    // Call reset-on-read handler for all registers
    extern void modbus_handle_reset_on_read(uint16_t starting_address, uint16_t quantity);
    modbus_handle_reset_on_read(start_addr, count * 2);
    return;
  }

  // DWORD type (32-bit unsigned integer, 2 consecutive registers)
  // BUG-137 FIX: Support count parameter for DWORD array reading
  if (display_as_dword) {
    // Validate that we have enough registers available (count DWORD values = count * 2 registers)
    if (start_addr + (count * 2) > HOLDING_REGS_SIZE) {
      debug_print("READ HOLDING-REG: DWORD kr\u00e6ver ");
      debug_print_uint(count * 2);
      debug_println(" registre, adresse udenfor omr\u00e5de");
      return;
    }

    debug_println("\n=== L\u00c6SNING AF HOLDING REGISTERS ===");
    debug_print("Adresse ");
    debug_print_uint(start_addr);
    debug_print("-");
    debug_print_uint(start_addr + (count * 2) - 1);
    debug_println(" (DWORD/32-bit unsigned):\n");

    // Read count DWORD values (each DWORD = 2 registers)
    for (uint16_t i = 0; i < count; i++) {
      uint16_t addr = start_addr + (i * 2);

      // BUG-124 FIX: Counter writes LSW first, MSW second (little-endian register order)
      uint16_t low_word = registers_get_holding_register(addr);          // LSW at base address
      uint16_t high_word = registers_get_holding_register(addr + 1);     // MSW at base+1
      uint32_t dword_value = ((uint32_t)high_word << 16) | low_word;

      debug_print("Reg[");
      debug_print_uint(addr);
      debug_print("-");
      debug_print_uint(addr + 1);
      debug_print("]: ");

      // Display DWORD value
      char dword_str[16];
      snprintf(dword_str, sizeof(dword_str), "%lu", (unsigned long)dword_value);
      debug_print(dword_str);
      debug_print(" (0x");
      char hex_str[16];
      snprintf(hex_str, sizeof(hex_str), "%04X%04X", high_word, low_word);
      debug_print(hex_str);
      debug_println(")");
    }
    debug_println("");

    // Call reset-on-read handler for all registers
    extern void modbus_handle_reset_on_read(uint16_t starting_address, uint16_t quantity);
    modbus_handle_reset_on_read(start_addr, count * 2);
    return;
  }

  // Read and display registers (INT/UINT mode)
  debug_println("\n=== L\u00c6SNING AF HOLDING REGISTERS ===");
  debug_print("Adresse ");
  debug_print_uint(start_addr);
  debug_print(" til ");
  debug_print_uint(start_addr + count - 1);
  debug_print(" (");
  debug_print(display_as_signed ? "signed int" : "unsigned int");
  debug_println("):\n");

  for (uint16_t i = 0; i < count; i++) {
    uint16_t addr = start_addr + i;
    uint16_t value = registers_get_holding_register(addr);

    debug_print("Reg[");
    debug_print_uint(addr);
    debug_print("]: ");

    if (display_as_signed) {
      // Display as signed 16-bit integer (-32768 to 32767)
      int16_t signed_value = (int16_t)value;
      if (signed_value < 0) {
        debug_print("-");
        debug_print_uint((uint16_t)(-signed_value));
      } else {
        debug_print_uint((uint16_t)signed_value);
      }
    } else {
      // Display as unsigned 16-bit integer (0 to 65535)
      debug_print_uint(value);
    }

    debug_println("");
  }
  debug_println("");

  // Handle reset-on-read for counter compare status (same as Modbus FC03)
  // Must be called AFTER reading registers so user sees current value
  extern void modbus_handle_reset_on_read(uint16_t starting_address, uint16_t quantity);
  modbus_handle_reset_on_read(start_addr, count);
}

/* ============================================================================
 * READ INPUT REGISTERS (from Modbus IR, including stats IR 252-293)
 * ============================================================================ */

void cli_cmd_read_input_reg(uint8_t argc, char* argv[]) {
  // read input-reg <start> [count] [int|uint|dint|dword|real] - Read Input Registers (0-1023)
  if (argc < 1) {
    debug_println("READ INPUT-REG: manglende parametre");
    debug_println("  Brug: read input-reg <id> [antal] [int|uint|dint|dword|real]");
    debug_println("  Eksempel: read input-reg 220           (l\u00e6ser 1 register som uint)");
    debug_println("  Eksempel: read input-reg 220 10        (l\u00e6ser 10 registre som uint)");
    debug_println("  Eksempel: read input-reg 220 1 int     (l\u00e6ser 1 register som signed int)");
    debug_println("  Eksempel: read input-reg 220 5 uint    (l\u00e6ser 5 registre som unsigned int)");
    debug_println("  Eksempel: read input-reg 220 int       (l\u00e6ser 1 register som signed int)");
    debug_println("  Eksempel: read input-reg 220 dint      (l\u00e6ser 2 registre som DINT/32-bit signed)");
    debug_println("  Eksempel: read input-reg 220 dword     (l\u00e6ser 2 registre som DWORD/32-bit unsigned)");
    debug_println("  Eksempel: read input-reg 220 real      (l\u00e6ser 2 registre som REAL/float)");
    return;
  }

  uint16_t start_addr = atoi(argv[0]);
  uint16_t count = 1;  // Default: 1 register
  bool display_as_signed = false;  // Default: unsigned
  bool display_as_real = false;     // REAL type support
  bool display_as_dint = false;     // DINT (32-bit signed) support
  bool display_as_dword = false;    // DWORD (32-bit unsigned) support

  // Parse count (argv[1])
  if (argc >= 2) {
    // Check if argv[1] is a type keyword or a number
    if (strcasecmp(argv[1], "int") == 0) {
      display_as_signed = true;
    } else if (strcasecmp(argv[1], "uint") == 0) {
      display_as_signed = false;
    } else if (strcasecmp(argv[1], "real") == 0) {
      display_as_real = true;
    } else if (strcasecmp(argv[1], "dint") == 0) {
      display_as_dint = true;
    } else if (strcasecmp(argv[1], "dword") == 0) {
      display_as_dword = true;
    } else {
      count = atoi(argv[1]);
    }
  }

  // Parse type (argv[2])
  if (argc >= 3) {
    if (strcasecmp(argv[2], "int") == 0) {
      display_as_signed = true;
      display_as_real = false;
      display_as_dint = false;
      display_as_dword = false;
    } else if (strcasecmp(argv[2], "uint") == 0) {
      display_as_signed = false;
      display_as_real = false;
      display_as_dint = false;
      display_as_dword = false;
    } else if (strcasecmp(argv[2], "real") == 0) {
      display_as_real = true;
      display_as_signed = false;
      display_as_dint = false;
      display_as_dword = false;
    } else if (strcasecmp(argv[2], "dint") == 0) {
      display_as_dint = true;
      display_as_signed = false;
      display_as_real = false;
      display_as_dword = false;
    } else if (strcasecmp(argv[2], "dword") == 0) {
      display_as_dword = true;
      display_as_signed = false;
      display_as_real = false;
      display_as_dint = false;
    }
  }

  uint16_t *input_regs = registers_get_input_regs();

  // Validate parameters (input registers can be larger than holding regs)
  if (start_addr >= 1024) {  // Modbus limit
    debug_println("READ INPUT-REG: startadresse udenfor område (max 1023)");
    return;
  }

  if (count == 0) {
    debug_println("READ INPUT-REG: antal skal være større end 0");
    return;
  }

  // Adjust count if it exceeds available input registers
  if (start_addr + count > 1024) {
    count = 1024 - start_addr;
    debug_print("READ INPUT-REG: justeret antal til ");
    debug_print_uint(count);
    debug_println(" registre");
  }

  // BUG-179 FIX: REAL type requires 2 consecutive registers
  if (display_as_real) {
    // Validate that we have enough registers available (count REAL values = count * 2 registers)
    if (start_addr + (count * 2) > 1024) {
      debug_print("READ INPUT-REG: REAL kræver ");
      debug_print_uint(count * 2);
      debug_println(" registre, adresse udenfor område");
      return;
    }

    debug_println("\n=== LÆSNING AF INPUT REGISTERS ===");
    debug_print("Adresse ");
    debug_print_uint(start_addr);
    debug_print("-");
    debug_print_uint(start_addr + (count * 2) - 1);
    debug_println(" (REAL/float):\n");

    // Read count REAL values (each REAL = 2 registers)
    for (uint16_t i = 0; i < count; i++) {
      uint16_t addr = start_addr + (i * 2);

      // BUG-125 FIX: ST Logic writes LSW first, MSW second (little-endian register order)
      uint16_t low_word = input_regs[addr];          // LSW at base address
      uint16_t high_word = input_regs[addr + 1];     // MSW at base+1
      uint32_t bits = ((uint32_t)high_word << 16) | low_word;
      float real_value;
      memcpy(&real_value, &bits, sizeof(float));

      debug_print("IR[");
      debug_print_uint(addr);
      debug_print("-");
      debug_print_uint(addr + 1);
      debug_print("]: ");

      // Display float with 6 decimal precision
      char float_str[32];
      snprintf(float_str, sizeof(float_str), "%.6f", real_value);
      debug_print(float_str);
      debug_print(" (0x");
      char hex_str[16];
      snprintf(hex_str, sizeof(hex_str), "%04X%04X", high_word, low_word);
      debug_print(hex_str);
      debug_println(")");
    }
    debug_println("");
    return;
  }

  // DINT type (32-bit signed integer, 2 consecutive registers)
  if (display_as_dint) {
    // Validate that we have enough registers available (count DINT values = count * 2 registers)
    if (start_addr + (count * 2) > 1024) {
      debug_print("READ INPUT-REG: DINT kræver ");
      debug_print_uint(count * 2);
      debug_println(" registre, adresse udenfor område");
      return;
    }

    debug_println("\n=== LÆSNING AF INPUT REGISTERS ===");
    debug_print("Adresse ");
    debug_print_uint(start_addr);
    debug_print("-");
    debug_print_uint(start_addr + (count * 2) - 1);
    debug_println(" (DINT/32-bit signed):\n");

    // Read count DINT values (each DINT = 2 registers)
    for (uint16_t i = 0; i < count; i++) {
      uint16_t addr = start_addr + (i * 2);

      // BUG-125 FIX: ST Logic writes LSW first, MSW second (little-endian register order)
      uint16_t low_word = input_regs[addr];          // LSW at base address
      uint16_t high_word = input_regs[addr + 1];     // MSW at base+1
      uint32_t unsigned_val = ((uint32_t)high_word << 16) | low_word;
      int32_t dint_value = (int32_t)unsigned_val;

      debug_print("IR[");
      debug_print_uint(addr);
      debug_print("-");
      debug_print_uint(addr + 1);
      debug_print("]: ");

      // Display signed 32-bit value
      char dint_str[16];
      snprintf(dint_str, sizeof(dint_str), "%ld", (long)dint_value);
      debug_print(dint_str);
      debug_print(" (0x");
      char hex_str[16];
      snprintf(hex_str, sizeof(hex_str), "%04X%04X", high_word, low_word);
      debug_print(hex_str);
      debug_println(")");
    }
    debug_println("");
    return;
  }

  // DWORD type (32-bit unsigned integer, 2 consecutive registers)
  if (display_as_dword) {
    // Validate that we have enough registers available (count DWORD values = count * 2 registers)
    if (start_addr + (count * 2) > 1024) {
      debug_print("READ INPUT-REG: DWORD kræver ");
      debug_print_uint(count * 2);
      debug_println(" registre, adresse udenfor område");
      return;
    }

    debug_println("\n=== LÆSNING AF INPUT REGISTERS ===");
    debug_print("Adresse ");
    debug_print_uint(start_addr);
    debug_print("-");
    debug_print_uint(start_addr + (count * 2) - 1);
    debug_println(" (DWORD/32-bit unsigned):\n");

    // Read count DWORD values (each DWORD = 2 registers)
    for (uint16_t i = 0; i < count; i++) {
      uint16_t addr = start_addr + (i * 2);

      // BUG-125 FIX: ST Logic writes LSW first, MSW second (little-endian register order)
      uint16_t low_word = input_regs[addr];          // LSW at base address
      uint16_t high_word = input_regs[addr + 1];     // MSW at base+1
      uint32_t dword_value = ((uint32_t)high_word << 16) | low_word;

      debug_print("IR[");
      debug_print_uint(addr);
      debug_print("-");
      debug_print_uint(addr + 1);
      debug_print("]: ");

      // Display unsigned 32-bit value
      char dword_str[16];
      snprintf(dword_str, sizeof(dword_str), "%lu", (unsigned long)dword_value);
      debug_print(dword_str);
      debug_print(" (0x");
      char hex_str[16];
      snprintf(hex_str, sizeof(hex_str), "%04X%04X", high_word, low_word);
      debug_print(hex_str);
      debug_println(")");
    }
    debug_println("");
    return;
  }

  // INT/UINT type (16-bit, default display)
  debug_println("\n=== LÆSNING AF INPUT REGISTERS ===");
  debug_print("Adresse ");
  debug_print_uint(start_addr);
  debug_print(" til ");
  debug_print_uint(start_addr + count - 1);

  if (display_as_signed) {
    debug_println(" (signed int):\n");
  } else {
    debug_println(":\n");
  }

  for (uint16_t i = 0; i < count; i++) {
    uint16_t addr = start_addr + i;
    uint16_t value = input_regs[addr];

    debug_print("IR[");
    debug_print_uint(addr);
    debug_print("]: ");

    if (display_as_signed) {
      // Display as signed 16-bit integer
      int16_t signed_value = (int16_t)value;
      char int_str[8];
      snprintf(int_str, sizeof(int_str), "%d", signed_value);
      debug_print(int_str);
    } else {
      // Display as unsigned 16-bit integer (default)
      debug_print_uint(value);
    }

    // Special info for ST Logic stats registers
    if (addr >= 252 && addr <= 293) {
      debug_print(" [ST Logic stats]");
    }

    debug_println("");
  }
  debug_println("");
}

/* ============================================================================
 * READ COIL
 * ============================================================================ */

void cli_cmd_read_coil(uint8_t argc, char* argv[]) {
  // read coil <id> [antal]
  if (argc < 1) {
    debug_println("READ COIL: manglende parametre");
    debug_println("  Brug: read coil <id> [antal]");
    debug_println("  Eksempel: read coil 0      (l\u00e6ser 1 coil)");
    debug_println("  Eksempel: read coil 0 16   (l\u00e6ser 16 coils)");
    return;
  }

  uint16_t start_addr = atoi(argv[0]);
  uint16_t count = (argc >= 2) ? atoi(argv[1]) : 1;  // Default: 1 coil

  // Validate parameters
  if (start_addr >= (COILS_SIZE * 8)) {
    debug_print("READ COIL: startadresse udenfor omr\u00e5de (max ");
    debug_print_uint((COILS_SIZE * 8) - 1);
    debug_println(")");
    return;
  }

  if (count == 0) {
    debug_println("READ COIL: antal skal v\u00e6re st\u00f8rre end 0");
    return;
  }

  // Adjust count if it exceeds available coils
  if (start_addr + count > (COILS_SIZE * 8)) {
    count = (COILS_SIZE * 8) - start_addr;
    debug_print("READ COIL: justeret antal til ");
    debug_print_uint(count);
    debug_println(" coils");
  }

  // Read and display coils
  debug_println("\n=== L\u00c6SNING AF COILS ===");
  debug_print("Adresse ");
  debug_print_uint(start_addr);
  debug_print(" til ");
  debug_print_uint(start_addr + count - 1);
  debug_println(":\n");

  for (uint16_t i = 0; i < count; i++) {
    uint16_t addr = start_addr + i;
    uint8_t value = registers_get_coil(addr);

    debug_print("Coil[");
    debug_print_uint(addr);
    debug_print("]: ");
    debug_print(value ? "1" : "0");
    debug_println("");
  }
  debug_println("");
}

/* ============================================================================
 * READ INPUT
 * ============================================================================ */

void cli_cmd_read_input(uint8_t argc, char* argv[]) {
  // read input <id> [antal]
  if (argc < 1) {
    debug_println("READ INPUT: manglende parametre");
    debug_println("  Brug: read input <id> [antal]");
    debug_println("  Eksempel: read input 0      (l\u00e6ser 1 input)");
    debug_println("  Eksempel: read input 0 16   (l\u00e6ser 16 inputs)");
    return;
  }

  uint16_t start_addr = atoi(argv[0]);
  uint16_t count = (argc >= 2) ? atoi(argv[1]) : 1;  // Default: 1 input

  // Validate parameters
  if (start_addr >= (DISCRETE_INPUTS_SIZE * 8)) {
    debug_print("READ INPUT: startadresse udenfor omr\u00e5de (max ");
    debug_print_uint((DISCRETE_INPUTS_SIZE * 8) - 1);
    debug_println(")");
    return;
  }

  if (count == 0) {
    debug_println("READ INPUT: antal skal v\u00e6re st\u00f8rre end 0");
    return;
  }

  // Adjust count if it exceeds available inputs
  if (start_addr + count > (DISCRETE_INPUTS_SIZE * 8)) {
    count = (DISCRETE_INPUTS_SIZE * 8) - start_addr;
    debug_print("READ INPUT: justeret antal til ");
    debug_print_uint(count);
    debug_println(" discrete inputs");
  }

  // Read and display discrete inputs
  debug_println("\n=== L\u00c6SNING AF DISCRETE INPUTS ===");
  debug_print("Adresse ");
  debug_print_uint(start_addr);
  debug_print(" til ");
  debug_print_uint(start_addr + count - 1);
  debug_println(":\n");

  for (uint16_t i = 0; i < count; i++) {
    uint16_t addr = start_addr + i;
    uint8_t value = registers_get_discrete_input(addr);

    debug_print("Input[");
    debug_print_uint(addr);
    debug_print("]: ");
    debug_print(value ? "1" : "0");
    debug_println("");
  }
  debug_println("");
}

/* ============================================================================
 * SHOW DEBUG FLAGS
 * ============================================================================ */

void cli_cmd_show_debug(void) {
  debug_println("");
  debug_println("=== DEBUG FLAGS ===");
  debug_println("");

  DebugFlags* dbg = debug_flags_get();

  debug_print("  config_save:       ");
  debug_println(dbg->config_save ? "ENABLED" : "DISABLED");

  debug_print("  config_load:       ");
  debug_println(dbg->config_load ? "ENABLED" : "DISABLED");

  debug_print("  wifi_connect:      ");
  debug_println(dbg->wifi_connect ? "ENABLED" : "DISABLED");

  debug_print("  network_validate:  ");
  debug_println(dbg->network_validate ? "ENABLED" : "DISABLED");

  debug_print("  http_server:       ");
  debug_println(dbg->http_server ? "ENABLED" : "DISABLED");

  debug_print("  http_api:          ");
  debug_println(dbg->http_api ? "ENABLED" : "DISABLED");

  debug_println("");
  debug_println("Use 'set debug <flag> <on|off>' to toggle debug flags");
  debug_println("");
}

/* ============================================================================
 * SHOW MODULES (v6.2.0+)
 * ============================================================================ */

void cli_cmd_show_modules(void) {
  debug_println("");
  debug_println("=== MODULE STATUS ===");
  debug_println("");

  // Counters
  debug_print("  Counters:  ");
  if (g_persist_config.module_flags & MODULE_FLAG_COUNTERS_DISABLED) {
    debug_println("DISABLED");
  } else {
    uint8_t configured = 0;
    for (uint8_t i = 0; i < COUNTER_COUNT; i++) {
      if (g_persist_config.counters[i].enabled) configured++;
    }
    debug_print("ENABLED (");
    debug_print_uint(configured);
    debug_println(" configured)");
  }

  // Timers
  debug_print("  Timers:    ");
  if (g_persist_config.module_flags & MODULE_FLAG_TIMERS_DISABLED) {
    debug_println("DISABLED");
  } else {
    uint8_t configured = 0;
    for (uint8_t i = 0; i < TIMER_COUNT; i++) {
      if (g_persist_config.timers[i].enabled) configured++;
    }
    debug_print("ENABLED (");
    debug_print_uint(configured);
    debug_println(" configured)");
  }

  // ST Logic
  debug_print("  ST Logic:  ");
  if (g_persist_config.module_flags & MODULE_FLAG_ST_LOGIC_DISABLED) {
    debug_println("DISABLED");
  } else {
    st_logic_engine_state_t* st_state = st_logic_get_state();
    uint8_t loaded = 0;
    if (st_state) {
      for (uint8_t i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
        if (st_state->programs[i].source_size > 0) loaded++;
      }
    }
    debug_print("ENABLED (");
    debug_print_uint(loaded);
    debug_println(" programs loaded)");
  }

  // Modbus (always enabled)
  debug_print("  Modbus:    ENABLED (slave ID ");
  debug_print_uint(g_persist_config.modbus_slave.slave_id);
  debug_println(")");

  // Network
  debug_print("  Network:   ");
  debug_println(g_persist_config.network.enabled ? "ENABLED" : "DISABLED");

  // HTTP API
  debug_print("  HTTP API:  ");
  debug_println(g_persist_config.network.http.enabled ? "ENABLED" : "DISABLED");

  debug_println("");
  debug_println("Use 'set module <name> enable|disable' to toggle modules");
  debug_println("Note: Reboot required for changes to take effect");
  debug_println("");
}

/* ============================================================================
 * SHOW PERSIST (v4.0+)
 * ============================================================================ */

void cli_cmd_show_persist(void) {
  // Simply call the persistence system's built-in list function
  registers_persist_list_groups();
}

void cli_cmd_show_watchdog(void) {
  WatchdogState* wdt = watchdog_get_state();

  debug_println("");
  debug_println("=== Watchdog Monitor (v4.0+) ===");
  debug_print("Status: ");
  debug_println(wdt->enabled ? "ENABLED" : "DISABLED");
  debug_print("Timeout: ");
  debug_print_uint(wdt->timeout_ms / 1000);
  debug_println(" seconds");
  debug_print("Reboot counter: ");
  debug_print_uint(wdt->reboot_counter);
  debug_println("");
  debug_print("Current uptime: ");
  debug_print_uint(millis() / 1000);
  debug_println(" seconds");

  // Last reset reason
  debug_print("Last reset reason: ");
  const char* reason_str = "Unknown";
  switch (wdt->last_reset_reason) {
    case 0:  reason_str = "Unknown"; break;
    case 1:  reason_str = "Power-on"; break;
    case 2:  reason_str = "External reset"; break;
    case 3:  reason_str = "Software reset"; break;
    case 4:  reason_str = "Panic/Exception"; break;
    case 5:  reason_str = "Interrupt watchdog"; break;
    case 6:  reason_str = "Task watchdog"; break;
    case 7:  reason_str = "Other watchdog"; break;
    case 8:  reason_str = "Deep sleep"; break;
    case 9:  reason_str = "Brownout"; break;
    case 10: reason_str = "SDIO reset"; break;
    default: reason_str = "Unknown"; break;
  }
  debug_println(reason_str);

  // Last error message
  if (strlen(wdt->last_error) > 0) {
    debug_print("Last error: ");
    debug_println(wdt->last_error);
  }

  debug_print("Last reboot uptime: ");
  debug_print_uint(wdt->last_reboot_uptime_ms / 1000);
  debug_println(" seconds");
  debug_println("");
}

/* ============================================================================
 * SHOW BACKUP
 * ============================================================================ */

void cli_cmd_show_backup(void) {
  debug_println("\n=== BACKUP / RESTORE ===");

  uint32_t ip = network_manager_get_local_ip();
  if (ip == 0) {
    debug_println("WiFi ikke forbundet - backup kraever HTTP API.");
    debug_println("Tilslut WiFi og proev igen.");
    debug_println("");
    return;
  }

  char base[64];
  const char *proto = g_persist_config.network.http.tls_enabled ? "https" : "http";
  uint16_t port = g_persist_config.network.http.port;
  snprintf(base, sizeof(base), "%s://%d.%d.%d.%d:%d",
           proto, (int)(ip & 0xFF), (int)((ip >> 8) & 0xFF),
           (int)((ip >> 16) & 0xFF), (int)((ip >> 24) & 0xFF), port);

  char line[128];

  debug_println("\n1) Download backup (browser eller curl):");
  snprintf(line, sizeof(line), "   %s/api/system/backup", base);
  debug_println(line);

  if (g_persist_config.network.http.auth_enabled) {
    snprintf(line, sizeof(line), "   curl -u %s:<password> %s/api/system/backup -o backup.json",
             g_persist_config.network.http.username, base);
  } else {
    snprintf(line, sizeof(line), "   curl %s/api/system/backup -o backup.json", base);
  }
  debug_println(line);

  debug_println("\n2) Restore fra backup fil:");
  if (g_persist_config.network.http.auth_enabled) {
    snprintf(line, sizeof(line), "   curl -u %s:<password> -X POST -H \"Content-Type: application/json\" -d @backup.json %s/api/system/restore",
             g_persist_config.network.http.username, base);
  } else {
    snprintf(line, sizeof(line), "   curl -X POST -H \"Content-Type: application/json\" -d @backup.json %s/api/system/restore", base);
  }
  debug_println(line);

  debug_println("\nIndhold: Modbus, WiFi, HTTP, counters, timers,");
  debug_println("         registers, coils, var_maps, ST Logic source.");
  debug_println("         Passwords inkluderet i backup.");
  debug_println("");
}
