/**
 * @file cli_commands.cpp
 * @brief CLI `set` command handlers (LAYER 7)
 *
 * Ported from: Mega2560 v3.6.5 cli_shell.cpp (command handlers)
 * Adapted to: ESP32 modular architecture
 *
 * Responsibility:
 * - Parse command parameters (key:value pairs, etc.)
 * - Call appropriate subsystem APIs
 * - Error handling and user feedback
 *
 * Context: Each handler is independent, can be tested separately
 */

#include "cli_commands.h"
#include "counter_engine.h"
#include "counter_config.h"
#include "timer_engine.h"
#include "timer_config.h"
#include "registers.h"
#include "registers_persist.h"
#include "cli_shell.h"
#include "config_struct.h"
#include "config_save.h"
#include "config_load.h"
#include "st_logic_config.h"
#include "config_apply.h"
#include "gpio_driver.h"
#include "heartbeat.h"
#include "debug.h"
#include "debug_flags.h"
#include "network_manager.h"
#include "network_config.h"
#include "register_allocator.h"  // BUG-025: Register overlap checking
#include <Arduino.h>
#include <esp_system.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ============================================================================
 * COUNTER COMMANDS
 * ============================================================================ */

void cli_cmd_set_counter(uint8_t argc, char* argv[]) {
  // set counter <id> mode 1 parameter hw-mode:... edge:... prescaler:... ...
  if (argc < 3) {
    debug_println("SET COUNTER: missing parameters");
    return;
  }

  uint8_t id = atoi(argv[0]);
  if (id < 1 || id > 4) {
    debug_println("SET COUNTER: invalid counter ID (must be 1-4)");
    return;
  }

  // Skip past "mode 1" (argv[1] = "mode", argv[2] = "1")
  // argv[3] onwards = parameters

  if (argc < 4) {
    debug_println("SET COUNTER: missing 'parameter' keyword");
    return;
  }

  // Parse key:value parameters (TODO: implement full parser)
  CounterConfig cfg = counter_config_defaults(id);

  for (uint8_t i = 3; i < argc; i++) {
    char* arg = argv[i];
    char* colon = strchr(arg, ':');

    if (!colon) {
      debug_print("SET COUNTER: invalid parameter format: ");
      debug_println(arg);
      continue;
    }

    *colon = '\0';
    const char* key = arg;
    const char* value = colon + 1;

    // Parse known keys
    if (!strcmp(key, "hw-mode")) {
      if (!strcmp(value, "sw")) cfg.hw_mode = COUNTER_HW_SW;
      else if (!strcmp(value, "sw-isr")) cfg.hw_mode = COUNTER_HW_SW_ISR;
      else if (!strcmp(value, "hw")) cfg.hw_mode = COUNTER_HW_PCNT;
    } else if (!strcmp(key, "edge")) {
      if (!strcmp(value, "rising")) cfg.edge_type = COUNTER_EDGE_RISING;
      else if (!strcmp(value, "falling")) cfg.edge_type = COUNTER_EDGE_FALLING;
      else if (!strcmp(value, "both")) cfg.edge_type = COUNTER_EDGE_BOTH;
    } else if (!strcmp(key, "prescaler")) {
      cfg.prescaler = atoi(value);
    // BUG-020 FIX: Disable manual register configuration (v4.2.0)
    // Registers are now ALWAYS auto-assigned by smart defaults
    // Counter 1 → 100-104, Counter 2 → 110-114, etc.
    } else if (!strcmp(key, "index-reg") || !strcmp(key, "reg")) {
      debug_println("ERROR: Manual register configuration is disabled!");
      debug_println("  Registers are AUTO-ASSIGNED by smart defaults:");
      debug_println("  Counter 1 → HR100-104");
      debug_println("  Counter 2 → HR110-114");
      debug_println("  Counter 3 → HR120-124");
      debug_println("  Counter 4 → HR130-134");
      debug_println("  Cannot override register addresses.");
      continue;
    } else if (!strcmp(key, "raw-reg")) {
      debug_println("ERROR: Manual register configuration is disabled!");
      debug_println("  Use smart defaults instead (no manual raw-reg allowed)");
      continue;
    } else if (!strcmp(key, "freq-reg")) {
      debug_println("ERROR: Manual register configuration is disabled!");
      debug_println("  Use smart defaults instead (no manual freq-reg allowed)");
      continue;
    } else if (!strcmp(key, "ctrl-reg")) {
      debug_println("ERROR: Manual register configuration is disabled!");
      debug_println("  Use smart defaults instead (no manual ctrl-reg allowed)");
      continue;
    } else if (!strcmp(key, "overload-reg")) {
      debug_println("ERROR: Manual register configuration is disabled!");
      debug_println("  Use smart defaults instead (no manual overload-reg allowed)");
      continue;
    } else if (!strcmp(key, "start-value")) {
      // BUG-183 FIX: Use strtoull (unsigned 64-bit) for full 64-bit counter support
      // strtoul() max = 4294967295 (unsigned 32-bit)
      // strtoull() max = 18446744073709551615 (unsigned 64-bit)
      cfg.start_value = strtoull(value, NULL, 10);
    } else if (!strcmp(key, "scale")) {
      cfg.scale_factor = atof(value);
    } else if (!strcmp(key, "bit-width")) {
      cfg.bit_width = atoi(value);
    } else if (!strcmp(key, "direction")) {
      if (!strcmp(value, "down")) cfg.direction = COUNTER_DIR_DOWN;
      else cfg.direction = COUNTER_DIR_UP;
    } else if (!strcmp(key, "debounce")) {
      cfg.debounce_enabled = (!strcmp(value, "on")) ? 1 : 0;
    } else if (!strcmp(key, "debounce-ms")) {
      cfg.debounce_ms = atoi(value);
    } else if (!strcmp(key, "input-dis")) {
      cfg.input_dis = atoi(value);
    } else if (!strcmp(key, "interrupt-pin")) {
      cfg.interrupt_pin = atoi(value);
      debug_print("  DEBUG: interrupt_pin = ");
      debug_print_uint(cfg.interrupt_pin);
      debug_println("");
    } else if (!strcmp(key, "hw-gpio")) {
      uint8_t pin = atoi(value);

      // BUG-CLI-2 FIX: Validate GPIO pin range
      if (pin == 0 || pin > 39) {
        debug_println("ERROR: Invalid GPIO pin (must be 1-39 for ESP32-WROOM-32)");
        continue;
      }

      // Warn if strapping pins (can affect boot)
      if (pin == 2 || pin == 15) {
        debug_print("WARNING: GPIO ");
        debug_print_uint(pin);
        debug_println(" is a strapping pin - may affect boot behavior!");
      }

      cfg.hw_gpio = pin;
      debug_print("  hw_gpio = ");
      debug_print_uint(cfg.hw_gpio);
      debug_println("");
    }
    // COMPARE FEATURE (v2.3+) - Status stored in ctrl_reg bit 4
    else if (!strcmp(key, "compare") || !strcmp(key, "compare-enabled")) {
      cfg.compare_enabled = (!strcmp(value, "on") || !strcmp(value, "1")) ? 1 : 0;
    } else if (!strcmp(key, "compare-value")) {
      // BUG-182 FIX: Use strtoull (unsigned) instead of atoll (signed)
      // atoll() max = 9223372036854775807 (signed 64-bit)
      // strtoull() max = 18446744073709551615 (unsigned 64-bit)
      cfg.compare_value = strtoull(value, NULL, 10);  // 64-bit unsigned value
    } else if (!strcmp(key, "compare-mode")) {
      cfg.compare_mode = atoi(value);  // 0=≥, 1=>, 2===
    } else if (!strcmp(key, "compare-source")) {
      cfg.compare_source = atoi(value);  // BUG-040: 0=raw, 1=prescaled, 2=scaled
    } else if (!strcmp(key, "enable")) {
      cfg.enabled = (!strcmp(value, "on") || !strcmp(value, "1")) ? 1 : 0;
    } else if (!strcmp(key, "disable")) {
      cfg.enabled = (!strcmp(value, "on") || !strcmp(value, "1")) ? 0 : 1;
    }
  }

  // BUG-021 FIX: Allow explicit disable via "enable:off" or "disable:on" (v4.2.0)
  // Default: if user didn't explicitly set enable/disable, assume enabled=1
  // But if they DID set it, respect their choice
  if (cfg.enabled == 0) {
    // User explicitly disabled
  } else {
    cfg.enabled = 1;  // Default: enable when configuring
  }

  // DEBUG: Print final config before applying
  debug_print("Counter ");
  debug_print_uint(id);
  debug_print(" config: enabled=");
  debug_print_uint(cfg.enabled);
  debug_print(" hw_mode=");
  debug_print_uint(cfg.hw_mode);
  debug_print(" hw_gpio=");
  debug_print_uint(cfg.hw_gpio);
  debug_println("");

  // Validate register usage - check for overlaps with other counters
  for (uint8_t other_id = 1; other_id <= COUNTER_COUNT; other_id++) {
    if (other_id == id) continue;  // Skip self

    CounterConfig other_cfg;
    if (!counter_config_get(other_id, &other_cfg) || !other_cfg.enabled) continue;

    // Check if any registers overlap
    bool overlap = false;
    uint16_t overlap_reg = 0;
    const char* overlap_type = "";

    if (cfg.value_reg > 0 && cfg.value_reg == other_cfg.value_reg) {
      overlap = true; overlap_reg = cfg.value_reg; overlap_type = "value-reg";
    }
    else if (cfg.raw_reg > 0 && cfg.raw_reg == other_cfg.raw_reg) {
      overlap = true; overlap_reg = cfg.raw_reg; overlap_type = "raw-reg";
    }
    else if (cfg.freq_reg > 0 && cfg.freq_reg == other_cfg.freq_reg) {
      overlap = true; overlap_reg = cfg.freq_reg; overlap_type = "freq-reg";
    }
    else if (cfg.ctrl_reg > 0 && cfg.ctrl_reg < 1000 && cfg.ctrl_reg == other_cfg.ctrl_reg) {
      overlap = true; overlap_reg = cfg.ctrl_reg; overlap_type = "ctrl-reg";
    }

    if (overlap) {
      debug_print("WARNING: Counter ");
      debug_print_uint(id);
      debug_print(" ");
      debug_print(overlap_type);
      debug_print("=");
      debug_print_uint(overlap_reg);
      debug_print(" overlaps with Counter ");
      debug_print_uint(other_id);
      debug_println("!");
      debug_println("  This will cause register conflicts!");
      debug_println("  Please use different register addresses for each counter.");
    }
  }

  // BUG-025 FIX: Check register overlap with other subsystems (Timer, ST Logic)
  // Check all 4 counter registers: value, raw, freq, ctrl (overflow in ctrl_reg bit 3)
  uint16_t counter_regs[4] = {cfg.value_reg, cfg.raw_reg, cfg.freq_reg, cfg.ctrl_reg};
  const char* counter_reg_names[4] = {"value-reg", "raw-reg", "freq-reg", "ctrl-reg"};

  for (int i = 0; i < 4; i++) {
    if (counter_regs[i] > 0 && counter_regs[i] < HOLDING_REGS_SIZE) {
      RegisterOwner owner;
      if (!register_allocator_check(counter_regs[i], &owner)) {
        // This register is allocated to something else
        debug_print("ERROR: Counter ");
        debug_print_uint(id);
        debug_print(" ");
        debug_print(counter_reg_names[i]);
        debug_print("=HR");
        debug_print_uint(counter_regs[i]);
        debug_println(" already allocated!");

        // Print owner type and description (with fallback if description is empty)
        debug_print("  Owner: ");
        if (owner.type == REG_OWNER_COUNTER) {
          debug_print("Counter ");
          debug_print_uint(owner.subsystem_id);
        } else if (owner.type == REG_OWNER_TIMER) {
          debug_print("Timer ");
          debug_print_uint(owner.subsystem_id);
        } else if (owner.type == REG_OWNER_ST_VAR) {
          debug_print("ST Logic var");
        } else if (owner.type == REG_OWNER_ST_FIXED) {
          debug_print("ST Logic (fixed)");
        } else if (owner.type == REG_OWNER_USER) {
          debug_print("User (manual)");
        } else {
          debug_print("Unknown (type=");
          debug_print_uint(owner.type);
          debug_print(", subsys=");
          debug_print_uint(owner.subsystem_id);
          debug_print(")");
        }

        // Add description if available
        if (owner.description[0] != '\0') {
          debug_print(" (");
          debug_print(owner.description);
          debug_print(")");
        }
        debug_println("");
        return;  // Abort configuration
      }
    }
  }

  // Store in persistent config
  if (id >= 1 && id <= COUNTER_COUNT) {
    g_persist_config.counters[id - 1] = cfg;
  }

  // Apply configuration to engine
  if (counter_engine_configure(id, &cfg)) {
    debug_print("Counter ");
    debug_print_uint(id);
    debug_println(" configured");

    // BUG FIX: Auto-save config to NVS after successful configure
    // First, copy ST Logic programs to persistent config
    st_logic_save_to_persist_config(&g_persist_config);
    if (config_save_to_nvs(&g_persist_config)) {
      debug_println("  (Config auto-saved to NVS)");
    } else {
      debug_println("  WARNING: Failed to save config to NVS!");
    }

    // Warn if input-dis or interrupt-pin not set for SW modes
    if (cfg.hw_mode == COUNTER_HW_SW && cfg.input_dis == 0) {
      debug_println("  HINT: SW mode requires input-dis:<pin>");
      debug_println("        Example: input-dis:45");
    }
    if (cfg.hw_mode == COUNTER_HW_SW_ISR && cfg.interrupt_pin == 0) {
      debug_println("  HINT: SW-ISR mode requires interrupt-pin:<gpio>");
      debug_println("        Example: interrupt-pin:23");
    }

    debug_println("NOTE: Use 'save' to persist to NVS");
  } else {
    debug_println("Failed to configure counter");
  }
}

void cli_cmd_reset_counter(uint8_t argc, char* argv[]) {
  if (argc < 1) {
    debug_println("RESET COUNTER: missing counter ID");
    return;
  }

  uint8_t id = atoi(argv[0]);
  if (id < 1 || id > 4) {
    debug_println("RESET COUNTER: invalid counter ID");
    return;
  }

  counter_engine_reset(id);
  debug_print("Counter ");
  debug_print_uint(id);
  debug_println(" reset");
}

// BUG-021 FIX: Add delete counter command (v4.2.0)
void cli_cmd_delete_counter(uint8_t argc, char* argv[]) {
  if (argc < 1) {
    debug_println("DELETE COUNTER: missing counter ID");
    return;
  }

  uint8_t id = atoi(argv[0]);
  if (id < 1 || id > 4) {
    debug_println("DELETE COUNTER: invalid counter ID");
    return;
  }

  // Disable counter by setting enabled=0
  CounterConfig cfg = counter_config_defaults(id);
  cfg.enabled = 0;  // DISABLE counter
  counter_engine_configure(id, &cfg);

  debug_print("Counter ");
  debug_print_uint(id);
  debug_println(" deleted (disabled)");
}

void cli_cmd_clear_counters(void) {
  counter_engine_reset_all();
  debug_println("All counters cleared");
}

void cli_cmd_set_counter_control(uint8_t argc, char* argv[]) {
  // set counter <id> control counter-reg-reset-on-read:on|off compare-reg-reset-on-read:on|off auto-start:on|off running:on|off
  if (argc < 2) {
    debug_println("SET COUNTER CONTROL: missing parameters");
    debug_println("  Usage: set counter <id> control <parameter>:<on|off> [...]");
    debug_println("");
    debug_println("  Parameters:");
    debug_println("    counter-reg-reset-on-read:<on|off>  - Reset counter when value registers read via Modbus");
    debug_println("    compare-reg-reset-on-read:<on|off>  - Auto-clear compare bit 4 when ctrl-reg read");
    debug_println("    auto-start:<on|off>                 - Start counter automatically at boot");
    debug_println("    running:<on|off>                    - Start/stop counter (persistent state)");
    debug_println("");
    debug_println("  Example: set counter 1 control auto-start:on running:on");
    debug_println("           set counter 1 control counter-reg-reset-on-read:on");
    debug_println("");
    debug_println("  Ctrl-reg bits:");
    debug_println("    Bit 0: counter-reg-reset-on-read flag (persistent)");
    debug_println("    Bit 1: auto-start flag (persistent)");
    debug_println("    Bit 4: compare-match status (read-only, set by compare engine)");
    debug_println("    Bit 7: running state (persistent)");
    debug_println("");
    debug_println("  Note: You can also use Modbus write to ctrl-reg for one-shot commands:");
    debug_println("    Write bit 0=1 -> Reset counter immediately (auto-clears)");
    debug_println("    Write bit 1=1 -> Start counter (auto-clears)");
    debug_println("    Write bit 2=1 -> Stop counter (auto-clears)");
    return;
  }

  uint8_t id = atoi(argv[0]);
  if (id < 1 || id > 4) {
    debug_println("SET COUNTER CONTROL: invalid counter ID (must be 1-4)");
    return;
  }

  // Get counter config to find ctrl-reg address
  CounterConfig cfg;
  if (!counter_config_get(id, &cfg)) {
    debug_println("SET COUNTER CONTROL: counter not found");
    return;
  }

  if (cfg.ctrl_reg >= HOLDING_REGS_SIZE) {
    debug_println("SET COUNTER CONTROL: ctrl-reg not configured");
    debug_println("  Hint: First configure counter with ctrl-reg:<address>");
    return;
  }

  // Read current ctrl-reg value
  uint16_t ctrl_value = registers_get_holding_register(cfg.ctrl_reg);

  // Parse control parameters (argv[1] onwards, skip "control" keyword)
  for (uint8_t i = 1; i < argc; i++) {
    char* arg = argv[i];
    char* colon = strchr(arg, ':');

    if (!colon) {
      debug_print("SET COUNTER CONTROL: invalid parameter format: ");
      debug_println(arg);
      continue;
    }

    *colon = '\0';
    const char* key = arg;
    const char* value = colon + 1;

    // Parse control flags
    if (!strcmp(key, "counter-reg-reset-on-read")) {
      // BUG-041: counter-reg-reset-on-read sets bit 0 (reset counter when value regs read)
      if (!strcmp(value, "on") || !strcmp(value, "ON")) {
        ctrl_value |= 0x01;  // Set bit 0
      } else if (!strcmp(value, "off") || !strcmp(value, "OFF")) {
        ctrl_value &= ~0x01; // Clear bit 0
      } else {
        debug_print("SET COUNTER CONTROL: invalid value for counter-reg-reset-on-read: ");
        debug_println(value);
      }
    } else if (!strcmp(key, "compare-reg-reset-on-read")) {
      // BUG-041: compare-reg-reset-on-read sets cfg.reset_on_read (clear bit 4 when ctrl-reg read)
      // Need to update config, not just ctrl register
      if (!strcmp(value, "on") || !strcmp(value, "ON")) {
        cfg.reset_on_read = 1;
        if (!counter_config_set(id, &cfg)) {
          debug_println("SET COUNTER CONTROL: failed to update compare-reg-reset-on-read");
        }
      } else if (!strcmp(value, "off") || !strcmp(value, "OFF")) {
        cfg.reset_on_read = 0;
        if (!counter_config_set(id, &cfg)) {
          debug_println("SET COUNTER CONTROL: failed to update compare-reg-reset-on-read");
        }
      } else {
        debug_print("SET COUNTER CONTROL: invalid value for compare-reg-reset-on-read: ");
        debug_println(value);
      }
    } else if (!strcmp(key, "auto-start")) {
      if (!strcmp(value, "on") || !strcmp(value, "ON")) {
        ctrl_value |= 0x02;  // Set bit 1
      } else if (!strcmp(value, "off") || !strcmp(value, "OFF")) {
        ctrl_value &= ~0x02; // Clear bit 1
      } else {
        debug_print("SET COUNTER CONTROL: invalid value for auto-start: ");
        debug_println(value);
      }
    } else if (!strcmp(key, "running")) {
      if (!strcmp(value, "on") || !strcmp(value, "ON")) {
        // BUG-015 FIX: Validate HW mode has GPIO configured
        if (cfg.hw_mode == COUNTER_HW_PCNT && cfg.hw_gpio == 0) {
          debug_println("ERROR: Cannot start HW counter - GPIO pin not configured!");
          debug_println("  Use: set counter <id> mode 1 hw-mode:hw hw-gpio:<pin> first");
          continue;  // Skip setting running bit
        }
        ctrl_value |= 0x80;  // Set bit 7
      } else if (!strcmp(value, "off") || !strcmp(value, "OFF")) {
        ctrl_value &= ~0x80; // Clear bit 7
      } else {
        debug_print("SET COUNTER CONTROL: invalid value for running: ");
        debug_println(value);
      }
    } else {
      debug_print("SET COUNTER CONTROL: unknown control flag: ");
      debug_println(key);
    }
  }

  // BUG-022 FIX: Auto-enable counter if running:on is set
  // When user sets running:on, they expect counter to actually run
  // So if it was deleted (en=0), re-enable it automatically
  bool running = (ctrl_value & 0x80) != 0;
  if (running && cfg.enabled == 0) {
    cfg.enabled = 1;  // Auto-enable if running is requested
    counter_engine_configure(id, &cfg);
    debug_println("  NOTE: Counter auto-enabled (was disabled)");
  }

  // Write updated ctrl-reg value
  registers_set_holding_register(cfg.ctrl_reg, ctrl_value);

  debug_print("Counter ");
  debug_print_uint(id);
  debug_print(" control updated: ctrl-reg[");
  debug_print_uint(cfg.ctrl_reg);
  debug_print("] = ");
  debug_print_uint(ctrl_value);
  debug_println("");

  // Show status
  bool counter_reset_on_read = (ctrl_value & 0x01) != 0;
  bool auto_start = (ctrl_value & 0x02) != 0;
  running = (ctrl_value & 0x80) != 0;
  bool compare_reset_on_read = cfg.reset_on_read;

  debug_print("  counter-reg-reset-on-read: ");
  debug_println(counter_reset_on_read ? "ENABLED" : "DISABLED");
  debug_print("  compare-reg-reset-on-read: ");
  debug_println(compare_reset_on_read ? "ENABLED" : "DISABLED");
  debug_print("  auto-start: ");
  debug_println(auto_start ? "ENABLED" : "DISABLED");
  debug_print("  running: ");
  debug_println(running ? "YES" : "NO");
}

/* ============================================================================
 * TIMER COMMANDS
 * ============================================================================ */

void cli_cmd_set_timer(uint8_t argc, char* argv[]) {
  // set timer <id> mode <1|2|3|4> parameter key:value ...
  if (argc < 3) {
    debug_println("SET TIMER: missing parameters");
    return;
  }

  uint8_t id = atoi(argv[0]);
  if (id < 1 || id > TIMER_COUNT) {
    debug_println("SET TIMER: invalid timer ID (must be 1-4)");
    return;
  }

  // argv[1] = "mode", argv[2] = mode number
  if (strcmp(argv[1], "mode") != 0) {
    debug_println("SET TIMER: expected 'mode' keyword");
    return;
  }

  uint8_t mode = atoi(argv[2]);
  if (mode < 1 || mode > 4) {
    debug_println("SET TIMER: invalid mode (must be 1-4)");
    return;
  }

  if (argc < 4) {
    debug_println("SET TIMER: missing 'parameter' keyword or parameters");
    return;
  }

  // Create default config and update with provided parameters
  TimerConfig cfg = {0};
  cfg.enabled = 1;
  cfg.mode = (TimerMode)mode;
  cfg.output_coil = 65535;  // No output by default

  // Set mode-specific defaults
  if (mode == TIMER_MODE_3_ASTABLE) {
    // Astable mode: toggle between HIGH (1) and LOW (0)
    cfg.phase1_output_state = 1;  // Phase 1 = HIGH
    cfg.phase2_output_state = 0;  // Phase 2 = LOW
  } else if (mode == TIMER_MODE_4_INPUT_TRIGGERED) {
    // Input-triggered mode: output HIGH when triggered
    cfg.phase1_output_state = 1;  // Output = HIGH when triggered
    cfg.phase2_output_state = 0;  // Unused, set to LOW
  }

  // Parse key:value parameters
  for (uint8_t i = 3; i < argc; i++) {
    char* arg = argv[i];
    char* colon = strchr(arg, ':');

    if (!colon) {
      debug_print("SET TIMER: invalid parameter format: ");
      debug_println(arg);
      continue;
    }

    *colon = '\0';
    const char* key = arg;
    const char* value = colon + 1;

    // Parse mode 1 parameters (one-shot)
    if (!strcmp(key, "p1-duration")) {
      cfg.phase1_duration_ms = atol(value);
    } else if (!strcmp(key, "p1-output")) {
      cfg.phase1_output_state = atoi(value);
    } else if (!strcmp(key, "p2-duration")) {
      cfg.phase2_duration_ms = atol(value);
    } else if (!strcmp(key, "p2-output")) {
      cfg.phase2_output_state = atoi(value);
    } else if (!strcmp(key, "p3-duration")) {
      cfg.phase3_duration_ms = atol(value);
    } else if (!strcmp(key, "p3-output")) {
      cfg.phase3_output_state = atoi(value);
    }
    // Mode 2 parameters (monostable)
    else if (!strcmp(key, "pulse-ms")) {
      cfg.pulse_duration_ms = atol(value);
    } else if (!strcmp(key, "trigger-level")) {
      cfg.trigger_level = atoi(value);
    }
    // Mode 3 parameters (astable)
    else if (!strcmp(key, "on-ms") || !strcmp(key, "on")) {
      cfg.on_duration_ms = atol(value);
    } else if (!strcmp(key, "off-ms") || !strcmp(key, "off")) {
      cfg.off_duration_ms = atol(value);
    }
    // Mode 4 parameters (input-triggered)
    else if (!strcmp(key, "input-dis")) {
      cfg.input_dis = atoi(value);
    } else if (!strcmp(key, "delay-ms")) {
      cfg.delay_ms = atol(value);
    } else if (!strcmp(key, "trigger-edge")) {
      cfg.trigger_edge = atoi(value);
    }
    // Common parameters
    else if (!strcmp(key, "output-coil")) {
      cfg.output_coil = atoi(value);
    } else if (!strcmp(key, "ctrl-reg")) {
      cfg.ctrl_reg = atoi(value);
    } else if (!strcmp(key, "enabled")) {
      cfg.enabled = (!strcmp(value, "on") || !strcmp(value, "1")) ? 1 : 0;
    }
  }

  // BUG-025 FIX: Check register overlap if ctrl-reg is set
  if (cfg.ctrl_reg > 0 && cfg.ctrl_reg < HOLDING_REGS_SIZE) {
    RegisterOwner owner;
    if (!register_allocator_check(cfg.ctrl_reg, &owner)) {
      // This register is allocated to something else
      debug_print("ERROR: Timer ");
      debug_print_uint(id);
      debug_print(" ctrl-reg=HR");
      debug_print_uint(cfg.ctrl_reg);
      debug_println(" already allocated!");
      debug_print("  Owner: ");
      debug_println(owner.description);
      return;  // Abort configuration
    }
  }

  // Store in persistent config
  if (id >= 1 && id <= TIMER_COUNT) {
    g_persist_config.timers[id - 1] = cfg;

    // Activate timer in timer_engine immediately
    timer_engine_configure(id, &cfg);
  }

  debug_print("Timer ");
  debug_print_uint(id);
  debug_print(" configured (mode ");
  debug_print_uint(mode);
  debug_println(")");
  debug_println("NOTE: Use 'save' to persist to NVS");
}

/* ============================================================================
 * SYSTEM COMMANDS
 * ============================================================================ */

void cli_cmd_set_hostname(const char* hostname) {
  if (!hostname || strlen(hostname) == 0) {
    debug_println("SET HOSTNAME: empty hostname");
    return;
  }

  // Validate hostname length (max 31 chars + null terminator)
  if (strlen(hostname) > 31) {
    debug_println("SET HOSTNAME: hostname too long (max 31 characters)");
    return;
  }

  // Update configuration
  strncpy(g_persist_config.hostname, hostname, 31);
  g_persist_config.hostname[31] = '\0';

  debug_print("Hostname set to: ");
  debug_println(hostname);
  debug_println("NOTE: Use 'save' to persist to NVS");
}

void cli_cmd_set_baud(uint32_t baud) {
  // Validate baudrate
  if (baud < 300 || baud > 115200) {
    debug_println("SET BAUD: invalid baud rate (must be 300-115200)");
    return;
  }

  // Update configuration
  g_persist_config.modbus_slave.baudrate = baud;

  debug_print("Baud rate set to: ");
  debug_print_uint(baud);
  debug_println(" (will apply on next boot)");
  debug_println("NOTE: Use 'save' to persist to NVS");
}

void cli_cmd_set_id(uint8_t id) {
  if (id > 247) {
    debug_println("SET ID: invalid slave ID (must be 0-247)");
    return;
  }

  // Update configuration
  g_persist_config.modbus_slave.slave_id = id;

  debug_print("Slave ID set to: ");
  debug_print_uint(id);
  debug_println(" (will apply on next boot)");
  debug_println("NOTE: Use 'save' to persist to NVS");
}

void cli_cmd_set_reg(uint16_t addr, uint16_t value) {
  if (addr >= HOLDING_REGS_SIZE) {
    debug_print("SET REG: address out of range (max ");
    debug_print_uint(HOLDING_REGS_SIZE - 1);
    debug_println(")");
    return;
  }

  // Write to holding register
  registers_set_holding_register(addr, value);
  debug_print("Register ");
  debug_print_uint(addr);
  debug_print(" = ");
  debug_print_uint(value);
  debug_println("");
}

void cli_cmd_set_coil(uint16_t idx, uint8_t value) {
  if (idx >= (COILS_SIZE * 8)) {
    debug_print("SET COIL: index out of range (max ");
    debug_print_uint((COILS_SIZE * 8) - 1);
    debug_println(")");
    return;
  }

  // Write to coil
  registers_set_coil(idx, value ? 1 : 0);
  debug_print("Coil ");
  debug_print_uint(idx);
  debug_print(" = ");
  debug_print_uint(value ? 1 : 0);
  debug_println("");
}

/**
 * no set gpio <pin>   (Remove GPIO mapping)
 */
void cli_cmd_no_set_gpio(uint8_t argc, char* argv[]) {
  if (argc < 1) {
    debug_println("NO SET GPIO: missing pin");
    debug_println("  Usage: no set gpio <pin>");
    debug_println("  Example: no set gpio 23");
    return;
  }

  uint16_t gpio_pin = atoi(argv[0]);

  // Validate pin: ESP32 has pins 0-39, or virtual GPIO 100+ (reads from COIL)
  if ((gpio_pin >= 40 && gpio_pin < 100) || gpio_pin >= 256) {
    debug_print("NO SET GPIO: invalid pin ");
    debug_print_uint(gpio_pin);
    debug_println(" (0-39 or 100-255 for virtual GPIO)");
    return;
  }

  // Find GPIO mapping
  uint8_t found_idx = 0xff;
  for (uint8_t i = 0; i < g_persist_config.var_map_count; i++) {
    if (g_persist_config.var_maps[i].gpio_pin == gpio_pin) {
      found_idx = i;
      break;
    }
  }

  if (found_idx == 0xff) {
    debug_print("NO SET GPIO: pin ");
    debug_print_uint(gpio_pin);
    debug_println(" is not mapped");
    return;
  }

  // Remove mapping by shifting all following entries one position down
  for (uint8_t i = found_idx; i < g_persist_config.var_map_count - 1; i++) {
    g_persist_config.var_maps[i] = g_persist_config.var_maps[i + 1];
  }
  g_persist_config.var_map_count--;

  debug_print("GPIO ");
  debug_print_uint(gpio_pin);
  debug_println(" mapping removed");

  // Auto-save to NVS (GPIO mappings persist like counters/timers)
  bool success = config_save_to_nvs(&g_persist_config);
  if (success) {
    debug_println("[OK] GPIO mapping removal saved to NVS");
  } else {
    debug_println("WARNING: Failed to save GPIO mapping removal to NVS (still active in runtime)");
  }
}

/**
 * set gpio <pin> input <idx>   (INPUT mode: GPIO pin → discrete input)
 * set gpio <pin> coil <idx>    (OUTPUT mode: coil → GPIO pin)
 */
void cli_cmd_set_gpio(uint8_t argc, char* argv[]) {
  if (argc < 3) {
    debug_println("SET GPIO: missing arguments");
    debug_println("  Usage: set gpio <pin> input <idx>");
    debug_println("         set gpio <pin> coil <idx>");
    debug_println("  Examples:");
    debug_println("    set gpio 23 input 45   (GPIO23 input → discrete input 45)");
    debug_println("    set gpio 12 coil 112   (Coil 112 → GPIO12 output)");
    return;
  }

  uint16_t gpio_pin = atoi(argv[0]);

  // Validate pin: ESP32 has pins 0-39, or virtual GPIO 100+ (reads from COIL)
  if ((gpio_pin >= 40 && gpio_pin < 100) || gpio_pin >= 256) {
    debug_print("SET GPIO: invalid pin ");
    debug_print_uint(gpio_pin);
    debug_println(" (0-39 or 100-255 for virtual GPIO)");
    return;
  }

  // New simplified syntax: set gpio <pin> input <idx> OR set gpio <pin> coil <idx>
  const char* direction = argv[1];
  uint16_t index = atoi(argv[2]);

  uint16_t input_index = 65535;
  uint16_t coil_index = 65535;
  uint8_t is_input = 0;

  if (!strcmp(direction, "input")) {
    // INPUT mode: GPIO pin → discrete input
    if (index >= (DISCRETE_INPUTS_SIZE * 8)) {
      debug_print("SET GPIO: input index out of range (max ");
      debug_print_uint((DISCRETE_INPUTS_SIZE * 8) - 1);
      debug_println(")");
      return;
    }
    input_index = index;
    is_input = 1;
  } else if (!strcmp(direction, "coil")) {
    // OUTPUT mode: coil → GPIO pin
    if (index >= (COILS_SIZE * 8)) {
      debug_print("SET GPIO: coil index out of range (max ");
      debug_print_uint((COILS_SIZE * 8) - 1);
      debug_println(")");
      return;
    }
    coil_index = index;
    is_input = 0;
  } else {
    debug_print("SET GPIO: unknown direction '");
    debug_print(direction);
    debug_println("' (expected 'input' or 'coil')");
    return;
  }

  // Find existing GPIO mapping or create new
  uint8_t found_idx = 0xff;
  for (uint8_t i = 0; i < g_persist_config.var_map_count; i++) {
    if (g_persist_config.var_maps[i].gpio_pin == gpio_pin) {
      found_idx = i;
      break;
    }
  }

  if (found_idx == 0xff) {
    // Create new mapping
    if (g_persist_config.var_map_count >= 32) {
      debug_println("SET GPIO: max GPIO mappings reached (32)");
      return;
    }
    found_idx = g_persist_config.var_map_count;
    g_persist_config.var_map_count++;
  }

  // Store STATIC mapping
  g_persist_config.var_maps[found_idx].source_type = MAPPING_SOURCE_GPIO;  // Mark as GPIO source
  g_persist_config.var_maps[found_idx].gpio_pin = gpio_pin;
  g_persist_config.var_maps[found_idx].is_input = is_input;
  g_persist_config.var_maps[found_idx].associated_counter = 0xff;  // No counter in STATIC mode
  g_persist_config.var_maps[found_idx].associated_timer = 0xff;    // No timer in STATIC mode
  g_persist_config.var_maps[found_idx].input_reg = input_index;
  g_persist_config.var_maps[found_idx].coil_reg = coil_index;

  // Initialize GPIO pin direction
  if (is_input) {
    gpio_set_direction(gpio_pin, GPIO_INPUT);
  } else {
    gpio_set_direction(gpio_pin, GPIO_OUTPUT);
  }

  // Print confirmation
  debug_print("GPIO ");
  debug_print_uint(gpio_pin);
  if (is_input) {
    debug_print(" - INPUT:");
    debug_print_uint(input_index);
  } else {
    debug_print(" - COIL:");
    debug_print_uint(coil_index);
  }
  debug_println("");

  // Auto-save to NVS (GPIO mappings persist like counters/timers)
  bool success = config_save_to_nvs(&g_persist_config);
  if (success) {
    debug_println("[OK] GPIO mapping saved to NVS");
  } else {
    debug_println("WARNING: Failed to save GPIO mapping to NVS (still active in runtime)");
  }
}

void cli_cmd_save(void) {
  debug_println("Saving configuration to NVS...");

  // Copy ST Logic programs to persistent config before saving
  st_logic_save_to_persist_config(&g_persist_config);

  // Calculate CRC before saving
  g_persist_config.crc16 = config_calculate_crc16(&g_persist_config);

  // Save to NVS
  bool success = config_save_to_nvs(&g_persist_config);

  if (success) {
    debug_println("SAVE: Configuration saved successfully");

    // Count enabled counters and timers
    uint8_t enabled_counters = 0, enabled_timers = 0;
    for (uint8_t i = 0; i < COUNTER_COUNT; i++) {
      if (g_persist_config.counters[i].enabled) enabled_counters++;
    }
    for (uint8_t i = 0; i < TIMER_COUNT; i++) {
      if (g_persist_config.timers[i].enabled) enabled_timers++;
    }

    // Count active GPIO/ST variable mappings (source_type != 0xff means active)
    uint8_t active_mappings = 0;
    uint8_t safe_var_map_count = g_persist_config.var_map_count;
    if (safe_var_map_count > 32) safe_var_map_count = 32;  // BUG-140 style clamp
    for (uint8_t i = 0; i < safe_var_map_count; i++) {
      if (g_persist_config.var_maps[i].source_type != 0xff) {
        active_mappings++;
      }
    }

    debug_print("  - ");
    debug_print_uint(enabled_counters);
    debug_println(" counters");
    debug_print("  - ");
    debug_print_uint(enabled_timers);
    debug_println(" timers");
    debug_print("  - ");
    debug_print_uint(g_persist_config.static_reg_count);
    debug_println(" static registers");
    debug_print("  - ");
    debug_print_uint(g_persist_config.dynamic_reg_count);
    debug_println(" dynamic registers");
    debug_print("  - ");
    debug_print_uint(g_persist_config.static_coil_count);
    debug_println(" static coils");
    debug_print("  - ");
    debug_print_uint(g_persist_config.dynamic_coil_count);
    debug_println(" dynamic coils");
    debug_print("  - ");
    debug_print_uint(active_mappings);
    debug_println(" variable mappings");
  } else {
    debug_println("SAVE: Failed to save configuration");
  }
}

void cli_cmd_load(void) {
  debug_println("Loading configuration from NVS...");

  bool success = config_load_from_nvs(&g_persist_config);

  if (success) {
    debug_println("LOAD: Configuration loaded successfully");

    // Count enabled counters and timers
    uint8_t enabled_counters = 0, enabled_timers = 0;
    for (uint8_t i = 0; i < COUNTER_COUNT; i++) {
      if (g_persist_config.counters[i].enabled) enabled_counters++;
    }
    for (uint8_t i = 0; i < TIMER_COUNT; i++) {
      if (g_persist_config.timers[i].enabled) enabled_timers++;
    }

    // Count active GPIO/ST variable mappings (source_type != 0xff means active)
    uint8_t active_mappings = 0;
    uint8_t safe_var_map_count = g_persist_config.var_map_count;
    if (safe_var_map_count > 32) safe_var_map_count = 32;  // BUG-140 style clamp
    for (uint8_t i = 0; i < safe_var_map_count; i++) {
      if (g_persist_config.var_maps[i].source_type != 0xff) {
        active_mappings++;
      }
    }

    debug_print("  - ");
    debug_print_uint(enabled_counters);
    debug_println(" counters");
    debug_print("  - ");
    debug_print_uint(enabled_timers);
    debug_println(" timers");
    debug_print("  - ");
    debug_print_uint(g_persist_config.static_reg_count);
    debug_println(" static registers");
    debug_print("  - ");
    debug_print_uint(g_persist_config.dynamic_reg_count);
    debug_println(" dynamic registers");
    debug_print("  - ");
    debug_print_uint(g_persist_config.static_coil_count);
    debug_println(" static coils");
    debug_print("  - ");
    debug_print_uint(g_persist_config.dynamic_coil_count);
    debug_println(" dynamic coils");
    debug_print("  - ");
    debug_print_uint(active_mappings);
    debug_println(" variable mappings");

    // Apply configuration to running system
    config_apply(&g_persist_config);
    debug_println("NOTE: Configuration applied. Some changes may require reboot.");
  } else {
    debug_println("LOAD: Failed to load configuration (using defaults)");
  }
}

/* ============================================================================
 * PERSISTENCE GROUP MANAGEMENT (v4.0+)
 * ============================================================================ */

/**
 * @brief Parse register range notation (supports ranges and comma-separated values)
 *
 * Examples:
 *   "100-105"           -> [100, 101, 102, 103, 104, 105]
 *   "110,112"           -> [110, 112]
 *   "100-105,110,120-122" -> [100, 101, 102, 103, 104, 105, 110, 120, 121, 122]
 *
 * @param input String containing register ranges/values
 * @param output Array to store parsed register addresses
 * @param max_output Maximum size of output array
 * @return Number of registers parsed, or -1 on error
 */
static int parse_register_range(const char* input, uint16_t* output, uint16_t max_output) {
  char buffer[256];
  strncpy(buffer, input, sizeof(buffer) - 1);
  buffer[sizeof(buffer) - 1] = '\0';

  uint16_t count = 0;
  char* token = strtok(buffer, ",");

  while (token != NULL && count < max_output) {
    // Trim leading/trailing whitespace
    while (*token == ' ') token++;
    char* end = token + strlen(token) - 1;
    while (end > token && *end == ' ') *end-- = '\0';

    // Check if it's a range (contains '-')
    char* dash = strchr(token, '-');

    if (dash != NULL) {
      // Parse range (e.g., "100-105")
      *dash = '\0';  // Split at dash
      uint16_t start = atoi(token);
      uint16_t end_val = atoi(dash + 1);

      // Validate range
      if (start > end_val) {
        debug_print("ERROR: Invalid range '");
        debug_print(token);
        debug_print("-");
        debug_print(dash + 1);
        debug_println("' (start > end)");
        return -1;
      }

      if (end_val >= HOLDING_REGS_SIZE) {
        debug_print("ERROR: Register address ");
        debug_print_uint(end_val);
        debug_print(" out of range (max ");
        debug_print_uint(HOLDING_REGS_SIZE - 1);
        debug_println(")");
        return -1;
      }

      // Add all registers in range
      for (uint16_t addr = start; addr <= end_val && count < max_output; addr++) {
        output[count++] = addr;
      }

    } else {
      // Parse single register
      uint16_t addr = atoi(token);

      if (addr >= HOLDING_REGS_SIZE) {
        debug_print("ERROR: Register address ");
        debug_print_uint(addr);
        debug_print(" out of range (max ");
        debug_print_uint(HOLDING_REGS_SIZE - 1);
        debug_println(")");
        return -1;
      }

      output[count++] = addr;
    }

    token = strtok(NULL, ",");
  }

  if (count == 0) {
    debug_println("ERROR: No valid registers found in input");
    return -1;
  }

  if (count >= max_output) {
    debug_println("WARNING: Register list truncated (max 256 registers per command)");
  }

  return count;
}

void cli_cmd_set_persist_group(uint8_t argc, char* argv[]) {
  // Syntax:
  //   set persist group <name> add <reg1> [reg2] [reg3] ...
  //   set persist group <name> remove <reg>
  //   set persist group <name> delete

  if (argc < 2) {
    debug_println("ERROR: Missing group name");
    debug_println("Usage: set persist group <name> add|remove|delete [registers...]");
    return;
  }

  const char* group_name = argv[0];
  const char* action = argv[1];

  if (strcmp(action, "add") == 0) {
    // Create group if it doesn't exist
    PersistGroup* grp = registers_persist_group_find(group_name);
    if (grp == NULL) {
      if (!registers_persist_group_create(group_name)) {
        return;  // Error already printed
      }
    }

    // Add registers (supports ranges: 100-105,110,112,120-122)
    if (argc < 3) {
      debug_println("ERROR: No registers specified");
      debug_println("Usage: set persist group <name> add <reg_spec>");
      debug_println("Examples:");
      debug_println("  set persist group test add 100-105");
      debug_println("  set persist group test add 110,112,115");
      debug_println("  set persist group test add 100-105,110,120-122");
      debug_println("  set persist group test add 100 101 102  (old syntax also works)");
      return;
    }

    // Concatenate all arguments from argv[2] onwards (allows both old and new syntax)
    char range_spec[256] = "";
    for (uint8_t i = 2; i < argc; i++) {
      if (i > 2) strcat(range_spec, ",");
      strncat(range_spec, argv[i], sizeof(range_spec) - strlen(range_spec) - 1);
    }

    // Parse register range/list
    static uint16_t reg_list[256];
    int reg_count = parse_register_range(range_spec, reg_list, 256);
    if (reg_count < 0) {
      return;  // Error already printed
    }

    // Add all registers to group
    uint8_t added = 0;
    for (int i = 0; i < reg_count; i++) {
      if (registers_persist_group_add_reg(group_name, reg_list[i])) {
        added++;
      }
    }

    debug_print("✓ Added ");
    debug_print_uint(added);
    debug_print(" registers to group '");
    debug_print(group_name);
    debug_println("'");

    // Enable persistence system if not already enabled
    if (!registers_persist_is_enabled()) {
      registers_persist_set_enabled(true);
    }

  } else if (strcmp(action, "remove") == 0) {
    if (argc < 3) {
      debug_println("ERROR: No register specified");
      debug_println("Usage: set persist group <name> remove <reg>");
      return;
    }

    uint16_t reg_addr = atoi(argv[2]);
    if (registers_persist_group_remove_reg(group_name, reg_addr)) {
      debug_print("✓ Removed register ");
      debug_print_uint(reg_addr);
      debug_print(" from group '");
      debug_print(group_name);
      debug_println("'");
    }

  } else if (strcmp(action, "delete") == 0) {
    if (registers_persist_group_delete(group_name)) {
      debug_print("✓ Deleted group '");
      debug_print(group_name);
      debug_println("'");
    }

  } else {
    debug_print("ERROR: Unknown action '");
    debug_print(action);
    debug_println("'");
    debug_println("Valid actions: add, remove, delete");
  }
}

void cli_cmd_save_registers(uint8_t argc, char* argv[]) {
  // Syntax:
  //   save registers all
  //   save registers group <name>

  if (argc == 0) {
    debug_println("ERROR: Missing argument");
    debug_println("Usage: save registers all | save registers group <name>");
    return;
  }

  if (strcmp(argv[0], "all") == 0) {
    // Save all groups
    debug_println("Saving all persistent register groups...");

    if (!registers_persist_is_enabled()) {
      debug_println("ERROR: Persistence system disabled");
      debug_println("Hint: Use 'set persist enable on' first");
      return;
    }

    // Snapshot all groups
    registers_persist_save_all_groups();

    // Save entire config to NVS
    g_persist_config.crc16 = config_calculate_crc16(&g_persist_config);
    bool success = config_save_to_nvs(&g_persist_config);

    if (success) {
      debug_print("✓ Saved ");
      debug_print_uint(g_persist_config.persist_regs.group_count);
      debug_println(" groups to NVS");
    } else {
      debug_println("ERROR: Failed to save to NVS");
    }

  } else if (strcmp(argv[0], "group") == 0) {
    if (argc < 2) {
      debug_println("ERROR: Missing group name");
      debug_println("Usage: save registers group <name>");
      return;
    }

    const char* group_name = argv[1];

    // Snapshot specific group
    if (!registers_persist_group_save(group_name)) {
      return;  // Error already printed
    }

    // Save entire config to NVS
    g_persist_config.crc16 = config_calculate_crc16(&g_persist_config);
    bool success = config_save_to_nvs(&g_persist_config);

    if (success) {
      debug_print("✓ Saved group '");
      debug_print(group_name);
      debug_println("' to NVS");
    } else {
      debug_println("ERROR: Failed to save to NVS");
    }

  } else {
    debug_print("ERROR: Unknown argument '");
    debug_print(argv[0]);
    debug_println("'");
    debug_println("Usage: save registers all | save registers group <name>");
  }
}

void cli_cmd_load_registers(uint8_t argc, char* argv[]) {
  // Syntax:
  //   load registers all
  //   load registers group <name>

  if (argc == 0) {
    debug_println("ERROR: Missing argument");
    debug_println("Usage: load registers all | load registers group <name>");
    return;
  }

  if (strcmp(argv[0], "all") == 0) {
    // Load all groups from NVS
    debug_println("Loading all persistent register groups from NVS...");

    if (!registers_persist_is_enabled()) {
      debug_println("ERROR: Persistence system disabled");
      debug_println("Hint: Use 'set persist enable on' first");
      return;
    }

    // Load config from NVS
    PersistConfig temp_config;
    bool success = config_load_from_nvs(&temp_config);
    if (!success) {
      debug_println("ERROR: Failed to load from NVS");
      return;
    }

    // Copy persist_regs to global config
    g_persist_config.persist_regs = temp_config.persist_regs;

    // Restore all groups
    success = registers_persist_restore_all_groups();

    if (success) {
      debug_print("✓ Loaded ");
      debug_print_uint(g_persist_config.persist_regs.group_count);
      debug_println(" groups from NVS");
    } else {
      debug_println("ERROR: Failed to restore groups");
    }

  } else if (strcmp(argv[0], "group") == 0) {
    if (argc < 2) {
      debug_println("ERROR: Missing group name");
      debug_println("Usage: load registers group <name>");
      return;
    }

    const char* group_name = argv[1];

    // Load config from NVS
    PersistConfig temp_config;
    bool success = config_load_from_nvs(&temp_config);
    if (!success) {
      debug_println("ERROR: Failed to load from NVS");
      return;
    }

    // Copy persist_regs to global config
    g_persist_config.persist_regs = temp_config.persist_regs;

    // Restore specific group
    if (!registers_persist_group_restore(group_name)) {
      return;  // Error already printed
    }

    debug_print("✓ Loaded group '");
    debug_print(group_name);
    debug_println("' from NVS");

  } else {
    debug_print("ERROR: Unknown argument '");
    debug_print(argv[0]);
    debug_println("'");
    debug_println("Usage: load registers all | load registers group <name>");
  }
}

void cli_cmd_set_persist_enable(bool enabled) {
  registers_persist_set_enabled(enabled);

  if (enabled) {
    debug_println("You can now create groups with: set persist group <name> add <reg1> ...");
  }
}

void cli_cmd_set_persist_reset(void) {
  // BUG-140 FIX: Clear all corrupted persistence groups
  debug_println("WARN: This will DELETE ALL persistence groups!");
  debug_println("Clearing all groups...");

  // Clear all group data
  memset(&g_persist_config.persist_regs.groups, 0, sizeof(g_persist_config.persist_regs.groups));
  g_persist_config.persist_regs.group_count = 0;
  g_persist_config.persist_regs.enabled = 0;

  // Clear auto-load config
  g_persist_config.persist_regs.auto_load_enabled = 0;
  memset(&g_persist_config.persist_regs.auto_load_group_ids, 0, sizeof(g_persist_config.persist_regs.auto_load_group_ids));

  debug_println("✓ All persistence groups cleared");
  debug_println("  Run 'config save' to persist this change to NVS");
}

void cli_cmd_set_persist_auto_load(uint8_t argc, char* argv[]) {
  // Syntax:
  //   set persist auto-load enable
  //   set persist auto-load disable
  //   set persist auto-load add <group_id>
  //   set persist auto-load remove <group_id>

  if (argc < 1) {
    debug_println("ERROR: Missing action");
    debug_println("Usage: set persist auto-load enable|disable|add|remove [group_id]");
    return;
  }

  const char* action = argv[0];

  // Normalize action to uppercase for comparison
  if (strcasecmp(action, "enable") == 0) {
    registers_persist_set_auto_load_enabled(true);
  } else if (strcasecmp(action, "disable") == 0) {
    registers_persist_set_auto_load_enabled(false);
  } else if (strcasecmp(action, "add") == 0) {
    if (argc < 2) {
      debug_println("ERROR: Missing group ID");
      debug_println("Usage: set persist auto-load add <group_id>");
      return;
    }
    uint8_t group_id = atoi(argv[1]);
    registers_persist_auto_load_add_group(group_id);
  } else if (strcasecmp(action, "remove") == 0) {
    if (argc < 2) {
      debug_println("ERROR: Missing group ID");
      debug_println("Usage: set persist auto-load remove <group_id>");
      return;
    }
    uint8_t group_id = atoi(argv[1]);
    registers_persist_auto_load_remove_group(group_id);
  } else {
    debug_print("ERROR: Unknown action '");
    debug_print(action);
    debug_println("'");
    debug_println("Valid actions: enable, disable, add, remove");
  }
}

void cli_cmd_defaults(void) {
  debug_println("Resetting to factory defaults...");

  // Reset config to defaults (zero out everything)
  memset(&g_persist_config, 0, sizeof(PersistConfig));
  g_persist_config.schema_version = 1;

  // Modbus Slave defaults
  g_persist_config.modbus_slave.enabled = true;
  g_persist_config.modbus_slave.slave_id = 1;
  g_persist_config.modbus_slave.baudrate = 9600;
  g_persist_config.modbus_slave.parity = 0;  // None
  g_persist_config.modbus_slave.stop_bits = 1;
  g_persist_config.modbus_slave.inter_frame_delay = 10;

  // Initialize all GPIO mappings as unused
  for (uint8_t i = 0; i < 8; i++) {
    g_persist_config.var_maps[i].input_reg = 65535;
    g_persist_config.var_maps[i].coil_reg = 65535;
    g_persist_config.var_maps[i].associated_counter = 0xff;
    g_persist_config.var_maps[i].associated_timer = 0xff;
    g_persist_config.var_maps[i].input_type = 0;      // Default: Holding Register
    g_persist_config.var_maps[i].output_type = 0;     // Default: Holding Register
  }

  debug_println("DEFAULTS: Configuration reset to factory defaults");
  debug_println("NOTE: Use 'save' to persist, or 'reboot' to discard");
}

void cli_cmd_reboot(void) {
  debug_println("Rebooting in 2 seconds...");
  delay(2000);
  esp_restart();
}

void cli_cmd_exit(void) {
  // Get current console
  Console *console = cli_shell_get_debug_console();

  if (!console) {
    debug_println("ERROR: No active console");
    return;
  }

  // Check if this is a network console (Telnet)
  // Serial console should not close (it's the debug port)
  // We detect Telnet by checking if context is not NULL and if it's not the global serial console
  extern Console *g_serial_console;  // Defined in main.cpp

  if (console == g_serial_console) {
    debug_println("ERROR: Cannot exit serial console (use Ctrl+C in terminal)");
    return;
  }

  // Request connection close (Telnet will handle it)
  console->close_requested = 1;
  debug_println("Goodbye!");
}

void cli_cmd_set_echo(uint8_t argc, char* argv[]) {
  // set echo <on|off>
  if (argc < 1) {
    debug_println("SET ECHO: missing parameter (on|off)");
    return;
  }

  const char* state = argv[0];
  if (!strcmp(state, "on")) {
    cli_shell_set_remote_echo(1);
    g_persist_config.remote_echo = 1;
    debug_println("Remote echo: ON");
  } else if (!strcmp(state, "off")) {
    cli_shell_set_remote_echo(0);
    g_persist_config.remote_echo = 0;
    debug_println("Remote echo: OFF");
  } else {
    debug_print("SET ECHO: invalid state '");
    debug_print(state);
    debug_println("' (use: on|off)");
    return;
  }

  debug_println("NOTE: Use 'save' to persist to NVS");
}

/* ============================================================================
 * WRITE REG
 * ============================================================================ */

void cli_cmd_write_reg(uint8_t argc, char* argv[]) {
  // write reg <id> value uint|int|real <v\u00e6rdi>
  // Formats:
  //   write reg <addr> value uint <0-65535>    - unsigned (0 til 65535)
  //   write reg <addr> value int <-32768-32767> - signed (konverteres til two's complement)
  //   write reg <addr> value real <float>       - REAL (32-bit float, bruger 2 registers)
  if (argc < 4) {
    debug_println("WRITE REG: manglende parametre");
    debug_println("  Brug: write reg <addr> value uint <v\u00e6rdi>");
    debug_println("        write reg <addr> value int <v\u00e6rdi>");
    debug_println("        write reg <addr> value dint <v\u00e6rdi>");
    debug_println("        write reg <addr> value dword <v\u00e6rdi>");
    debug_println("        write reg <addr> value real <v\u00e6rdi>");
    debug_println("  Eksempel: write reg 100 value uint 223");
    debug_println("            write reg 112 value int -10");
    debug_println("            write reg 100 value dint 100000");
    debug_println("            write reg 100 value dword 3000000000");
    debug_println("            write reg 100 value real 3.14");
    return;
  }

  uint16_t addr = atoi(argv[0]);

  // argv[1] should be "value"
  if (strcmp(argv[1], "value")) {
    debug_println("WRITE REG: forventet 'value' som 2. parameter");
    debug_println("  Brug: write reg <addr> value uint|int|real <v\u00e6rdi>");
    return;
  }

  // argv[2] should be "uint" or "int"
  const char* type = argv[2];
  const char* value_str = argv[3];

  uint16_t value = 0;
  bool is_signed = false;

  if (!strcmp(type, "uint")) {
    // Unsigned value (0-65535)
    int32_t temp = atoi(value_str);
    if (temp < 0) {
      debug_println("WRITE REG: uint v\u00e6rdi skal v\u00e6re >= 0");
      debug_print("  Modtaget: ");
      debug_print(value_str);
      debug_println("");
      return;
    }
    if (temp > 65535) {
      debug_println("WRITE REG: uint v\u00e6rdi skal v\u00e6re <= 65535");
      return;
    }
    value = (uint16_t)temp;
    is_signed = false;
  } else if (!strcmp(type, "int")) {
    // Signed value (-32768 to 32767)
    int32_t temp = atoi(value_str);
    if (temp < -32768 || temp > 32767) {
      debug_println("WRITE REG: int v\u00e6rdi skal v\u00e6re mellem -32768 og 32767");
      return;
    }
    int16_t signed_val = (int16_t)temp;
    // Convert to two's complement representation
    value = (uint16_t)signed_val;
    is_signed = true;
  } else if (!strcmp(type, "real")) {
    // BUG-108 FIX: REAL value (32-bit float, uses 2 consecutive registers)
    float real_val = atof(value_str);

    // Validate address (need 2 consecutive registers)
    if (addr + 1 >= HOLDING_REGS_SIZE) {
      debug_print("WRITE REG: REAL kr\u00e6ver 2 registers, adresse ");
      debug_print_uint(addr);
      debug_print(" udenfor omr\u00e5de (max ");
      debug_print_uint(HOLDING_REGS_SIZE - 2);
      debug_println(")");
      return;
    }

    // Convert float to IEEE 754 bit representation
    uint32_t bits;
    memcpy(&bits, &real_val, sizeof(float));

    uint16_t low_word = (uint16_t)(bits & 0xFFFF);
    uint16_t high_word = (uint16_t)((bits >> 16) & 0xFFFF);

    // Write to 2 consecutive registers (LSW first, MSW second - matches counter_engine)
    // BUG-124 FIX: Little-endian register order
    registers_set_holding_register(addr, low_word);      // LSW at base address
    registers_set_holding_register(addr + 1, high_word); // MSW at base+1

    debug_print("Register ");
    debug_print_uint(addr);
    debug_print("-");
    debug_print_uint(addr + 1);
    debug_print(" = ");
    debug_print(value_str);
    debug_print(" (real, lagret som 0x");
    char hex_str[16];
    snprintf(hex_str, sizeof(hex_str), "%04X%04X", high_word, low_word);
    debug_print(hex_str);
    debug_println(")");
    return;
  } else if (!strcmp(type, "dint")) {
    // DINT value (32-bit signed integer, uses 2 consecutive registers)
    int32_t dint_val = atol(value_str);

    // Validate address (need 2 consecutive registers)
    if (addr + 1 >= HOLDING_REGS_SIZE) {
      debug_print("WRITE REG: DINT kr\u00e6ver 2 registers, adresse ");
      debug_print_uint(addr);
      debug_print(" udenfor omr\u00e5de (max ");
      debug_print_uint(HOLDING_REGS_SIZE - 2);
      debug_println(")");
      return;
    }

    // Convert DINT to 2 words (high word, low word)
    uint32_t bits = (uint32_t)dint_val;
    uint16_t low_word = (uint16_t)(bits & 0xFFFF);
    uint16_t high_word = (uint16_t)((bits >> 16) & 0xFFFF);

    // Write to 2 consecutive registers (LSW first, MSW second - matches counter_engine)
    // BUG-124 FIX: Little-endian register order
    registers_set_holding_register(addr, low_word);      // LSW at base address
    registers_set_holding_register(addr + 1, high_word); // MSW at base+1

    debug_print("Register ");
    debug_print_uint(addr);
    debug_print("-");
    debug_print_uint(addr + 1);
    debug_print(" = ");
    debug_print(value_str);
    debug_print(" (dint, lagret som 0x");
    char hex_str2[16];
    snprintf(hex_str2, sizeof(hex_str2), "%04X%04X", high_word, low_word);
    debug_print(hex_str2);
    debug_println(")");
    return;
  } else if (!strcmp(type, "dword")) {
    // DWORD value (32-bit unsigned integer, uses 2 consecutive registers)
    uint32_t dword_val = strtoul(value_str, NULL, 10);

    // Validate address (need 2 consecutive registers)
    if (addr + 1 >= HOLDING_REGS_SIZE) {
      debug_print("WRITE REG: DWORD kr\u00e6ver 2 registers, adresse ");
      debug_print_uint(addr);
      debug_print(" udenfor omr\u00e5de (max ");
      debug_print_uint(HOLDING_REGS_SIZE - 2);
      debug_println(")");
      return;
    }

    // Convert DWORD to 2 words (high word, low word)
    uint16_t low_word = (uint16_t)(dword_val & 0xFFFF);
    uint16_t high_word = (uint16_t)((dword_val >> 16) & 0xFFFF);

    // Write to 2 consecutive registers (LSW first, MSW second - matches counter_engine)
    // BUG-124 FIX: Little-endian register order
    registers_set_holding_register(addr, low_word);      // LSW at base address
    registers_set_holding_register(addr + 1, high_word); // MSW at base+1

    debug_print("Register ");
    debug_print_uint(addr);
    debug_print("-");
    debug_print_uint(addr + 1);
    debug_print(" = ");
    debug_print(value_str);
    debug_print(" (dword, lagret som 0x");
    char hex_str3[16];
    snprintf(hex_str3, sizeof(hex_str3), "%04X%04X", high_word, low_word);
    debug_print(hex_str3);
    debug_println(")");
    return;
  } else {
    debug_print("WRITE REG: ukendt type '");
    debug_print(type);
    debug_println("' (brug: uint, int, dint, dword, eller real)");
    return;
  }

  // Validate address
  if (addr >= HOLDING_REGS_SIZE) {
    debug_print("WRITE REG: adresse udenfor omr\u00e5de (max ");
    debug_print_uint(HOLDING_REGS_SIZE - 1);
    debug_println(")");
    return;
  }

  // Write to holding register
  registers_set_holding_register(addr, value);

  debug_print("Register ");
  debug_print_uint(addr);
  debug_print(" = ");
  if (is_signed) {
    // Display as signed value
    int16_t signed_val = (int16_t)value;
    if (signed_val < 0) {
      debug_print("-");
      debug_print_uint((uint16_t)(-signed_val));
    } else {
      debug_print_uint((uint16_t)signed_val);
    }
    debug_print(" (int, lagret som ");
    debug_print_uint(value);
    debug_println(")");
  } else {
    debug_print_uint(value);
    debug_println(" (uint)");
  }
}

/* ============================================================================
 * WRITE COIL
 * ============================================================================ */

void cli_cmd_write_coil(uint8_t argc, char* argv[]) {
  // write coil <id> value <on|off>
  if (argc < 3) {
    debug_println("WRITE COIL: manglende parametre");
    debug_println("  Brug: write coil <id> value <on|off>");
    debug_println("  Eksempel: write coil 5 value on");
    return;
  }

  uint16_t addr = atoi(argv[0]);

  // Check if second parameter is "value" (optional keyword)
  uint8_t value_idx = 1;
  if (argc >= 3 && !strcmp(argv[1], "value")) {
    value_idx = 2;
  }

  const char* value_str = argv[value_idx];
  uint8_t value = 0;

  // Parse on/off or 1/0
  if (!strcmp(value_str, "on") || !strcmp(value_str, "ON") || !strcmp(value_str, "1")) {
    value = 1;
  } else if (!strcmp(value_str, "off") || !strcmp(value_str, "OFF") || !strcmp(value_str, "0")) {
    value = 0;
  } else {
    debug_print("WRITE COIL: ugyldig v\u00e6rdi '");
    debug_print(value_str);
    debug_println("' (brug: on|off eller 1|0)");
    return;
  }

  // Validate address
  if (addr >= (COILS_SIZE * 8)) {
    debug_print("WRITE COIL: adresse udenfor omr\u00e5de (max ");
    debug_print_uint((COILS_SIZE * 8) - 1);
    debug_println(")");
    return;
  }

  // Write to coil
  registers_set_coil(addr, value);

  debug_print("Coil ");
  debug_print_uint(addr);
  debug_print(" = ");
  debug_print(value ? "1 (ON)" : "0 (OFF)");
  debug_println(" (skrevet)");
}

/* ============================================================================
 * GPIO2 ENABLE/DISABLE (HEARTBEAT CONTROL)
 * ============================================================================ */

void cli_cmd_set_gpio2(uint8_t argc, char* argv[]) {
  // set gpio 2 enable/disable
  if (argc < 2) {
    debug_println("SET GPIO 2: manglende parametre");
    debug_println("  Brug: set gpio 2 enable   (frigiv GPIO2 til bruger)");
    debug_println("        set gpio 2 disable  (aktiver heartbeat igen)");
    return;
  }

  // argv[0] = "2", argv[1] = "enable" or "disable"
  const char* action = argv[1];

  if (!strcmp(action, "enable") || !strcmp(action, "ENABLE")) {
    // Enable user mode (disable heartbeat)
    g_persist_config.gpio2_user_mode = 1;
    heartbeat_disable();

    debug_println("GPIO2 frigivet til bruger (heartbeat deaktiveret)");
    debug_println("OBS: GPIO2 kan nu bruges til andre formål");
    debug_println("TIP: Brug 'set gpio 2 input <idx>' eller 'set gpio 2 coil <idx>'");
    debug_println("NOTE: Husk 'save' for at gemme indstillingen");

  } else if (!strcmp(action, "disable") || !strcmp(action, "DISABLE")) {
    // Disable user mode (enable heartbeat)
    g_persist_config.gpio2_user_mode = 0;
    heartbeat_enable();

    debug_println("GPIO2 reserveret til heartbeat (LED blink aktiv)");
    debug_println("NOTE: Husk 'save' for at gemme indstillingen");

  } else {
    debug_print("SET GPIO 2: ugyldig handling '");
    debug_print(action);
    debug_println("' (brug: enable|disable)");
  }
}

void cli_cmd_set_debug(uint8_t argc, char* argv[]) {
  if (argc < 2) {
    debug_println("SET DEBUG: missing parameter");
    debug_println("  Usage: set debug <flag> on|off");
    debug_println("  Flags:");
    debug_println("    config-save  - Debug config save to NVS");
    debug_println("    config-load  - Debug config load from NVS");
    debug_println("    all          - All debug flags");
    return;
  }

  const char* flag = argv[0];
  const char* state = argv[1];
  uint8_t enabled = (strcmp(state, "on") == 0) ? 1 : 0;

  if (strcmp(flag, "config-save") == 0) {
    debug_flags_set_config_save(enabled);
    debug_print("Config save debug: ");
    debug_println(enabled ? "ON" : "OFF");
  } else if (strcmp(flag, "config-load") == 0) {
    debug_flags_set_config_load(enabled);
    debug_print("Config load debug: ");
    debug_println(enabled ? "ON" : "OFF");
  } else if (strcmp(flag, "all") == 0) {
    debug_flags_set_all(enabled);
    debug_print("All debug flags: ");
    debug_println(enabled ? "ON" : "OFF");
  } else {
    debug_print("SET DEBUG: unknown flag '");
    debug_print(flag);
    debug_println("'");
  }
}

/* ============================================================================
 * NETWORK/WI-FI COMMANDS (v3.0+)
 * ============================================================================ */

void cli_cmd_set_wifi(uint8_t argc, char* argv[]) {
  if (argc < 2) {
    debug_println("SET WIFI: missing parameters");
    debug_println("  Usage: set wifi <option> <value>");
    debug_println("");
    debug_println("  Options:");
    debug_println("    ssid <name>         - Set Wi-Fi network name");
    debug_println("    password <pwd>      - Set Wi-Fi password (WPA2, 8-63 chars)");
    debug_println("    dhcp on|off         - Enable/disable DHCP (default: on)");
    debug_println("    ip <address>        - Static IP (e.g., 192.168.1.100)");
    debug_println("    gateway <address>   - Gateway IP (e.g., 192.168.1.1)");
    debug_println("    netmask <address>   - Netmask (e.g., 255.255.255.0)");
    debug_println("    dns <address>       - DNS server (e.g., 8.8.8.8)");
    debug_println("    telnet-port <port>  - Telnet port (default: 23)");
    debug_println("    enable              - Enable Wi-Fi");
    debug_println("    disable             - Disable Wi-Fi");
    debug_println("");
    debug_println("  Note: Use 'save' to persist settings to NVS");
    return;
  }

  const char* option = argv[0];
  const char* value = argv[1];

  if (!strcmp(option, "ssid")) {
    if (strlen(value) > WIFI_SSID_MAX_LEN - 1) {
      debug_println("SET WIFI: SSID too long (max 32 chars)");
      return;
    }
    strncpy(g_persist_config.network.ssid, value, WIFI_SSID_MAX_LEN - 1);
    g_persist_config.network.ssid[WIFI_SSID_MAX_LEN - 1] = '\0';
    debug_print("Wi-Fi SSID set to: ");
    debug_println(g_persist_config.network.ssid);

  } else if (!strcmp(option, "password")) {
    if (strlen(value) < 8 || strlen(value) > WIFI_PASSWORD_MAX_LEN - 1) {
      debug_println("SET WIFI: Password must be 8-63 characters");
      return;
    }
    strncpy(g_persist_config.network.password, value, WIFI_PASSWORD_MAX_LEN - 1);
    g_persist_config.network.password[WIFI_PASSWORD_MAX_LEN - 1] = '\0';
    debug_println("Wi-Fi password set (not shown for security)");

  } else if (!strcmp(option, "dhcp")) {
    if (!strcmp(value, "on") || !strcmp(value, "ON")) {
      g_persist_config.network.dhcp_enabled = 1;
      debug_println("DHCP enabled (automatic IP assignment)");
    } else if (!strcmp(value, "off") || !strcmp(value, "OFF")) {
      g_persist_config.network.dhcp_enabled = 0;
      debug_println("DHCP disabled (use static IP settings)");
    } else {
      debug_println("SET WIFI DHCP: invalid value (use: on|off)");
    }

  } else if (!strcmp(option, "ip")) {
    uint32_t ip;
    if (!network_config_str_to_ip(value, &ip)) {
      debug_println("SET WIFI IP: invalid IP address format");
      return;
    }
    g_persist_config.network.static_ip = ip;
    debug_print("Static IP set to: ");
    char ip_str[16];
    debug_println(network_config_ip_to_str(ip, ip_str));

  } else if (!strcmp(option, "gateway")) {
    uint32_t gw;
    if (!network_config_str_to_ip(value, &gw)) {
      debug_println("SET WIFI GATEWAY: invalid IP address format");
      return;
    }
    g_persist_config.network.static_gateway = gw;
    debug_print("Gateway set to: ");
    char gw_str[16];
    debug_println(network_config_ip_to_str(gw, gw_str));

  } else if (!strcmp(option, "netmask")) {
    uint32_t nm;
    if (!network_config_str_to_ip(value, &nm)) {
      debug_println("SET WIFI NETMASK: invalid IP address format");
      return;
    }
    if (!network_config_is_valid_netmask(nm)) {
      debug_println("SET WIFI NETMASK: invalid netmask (must be contiguous bits)");
      return;
    }
    g_persist_config.network.static_netmask = nm;
    debug_print("Netmask set to: ");
    char nm_str[16];
    debug_println(network_config_ip_to_str(nm, nm_str));

  } else if (!strcmp(option, "dns")) {
    uint32_t dns;
    if (!network_config_str_to_ip(value, &dns)) {
      debug_println("SET WIFI DNS: invalid IP address format");
      return;
    }
    g_persist_config.network.static_dns = dns;
    debug_print("DNS set to: ");
    char dns_str[16];
    debug_println(network_config_ip_to_str(dns, dns_str));

  } else if (!strcmp(option, "telnet-user")) {
    // Set Telnet username
    if (!value || value[0] == '\0') {
      debug_println("SET WIFI TELNET-USER: missing username");
      return;
    }
    strncpy(g_persist_config.network.telnet_username, value, sizeof(g_persist_config.network.telnet_username) - 1);
    g_persist_config.network.telnet_username[sizeof(g_persist_config.network.telnet_username) - 1] = '\0';
    debug_print("Telnet username set to: ");
    debug_println(g_persist_config.network.telnet_username);

  } else if (!strcmp(option, "telnet-pass")) {
    // Set Telnet password
    if (!value || value[0] == '\0') {
      debug_println("SET WIFI TELNET-PASS: missing password");
      return;
    }
    strncpy(g_persist_config.network.telnet_password, value, sizeof(g_persist_config.network.telnet_password) - 1);
    g_persist_config.network.telnet_password[sizeof(g_persist_config.network.telnet_password) - 1] = '\0';
    debug_println("Telnet password set (hidden for security)");

  } else if (!strcmp(option, "telnet-port")) {
    uint16_t port = atoi(value);
    if (port < 1 || port > 65535) {
      debug_println("SET WIFI TELNET-PORT: invalid port (1-65535)");
      return;
    }
    g_persist_config.network.telnet_port = port;
    debug_print("Telnet port set to: ");
    debug_print_uint(port);
    debug_println("");

  } else if (!strcmp(option, "telnet")) {
    // Enable/disable Telnet server
    if (!value || (!strcmp(value, "") && argc < 3)) {
      debug_println("SET WIFI TELNET: missing enable|disable parameter");
      return;
    }

    // Get the value - could be in option or as separate parameter
    const char* telnet_option = value;
    if (!telnet_option || telnet_option[0] == '\0') {
      // Try to get from next parameter
      if (argc >= 3) {
        telnet_option = argv[2];
      } else {
        debug_println("SET WIFI TELNET: missing enable|disable parameter");
        return;
      }
    }

    if (!strcmp(telnet_option, "enable")) {
      g_persist_config.network.telnet_enabled = 1;
      debug_println("Telnet enabled");
    } else if (!strcmp(telnet_option, "disable")) {
      g_persist_config.network.telnet_enabled = 0;
      debug_println("Telnet disabled");
    } else if (!strcmp(telnet_option, "on")) {
      g_persist_config.network.telnet_enabled = 1;
      debug_println("Telnet enabled");
    } else if (!strcmp(telnet_option, "off")) {
      g_persist_config.network.telnet_enabled = 0;
      debug_println("Telnet disabled");
    } else {
      debug_println("SET WIFI TELNET: unknown option (use: enable, disable, on, off)");
      return;
    }

  } else if (!strcmp(option, "enable")) {
    g_persist_config.network.enabled = 1;
    debug_println("Wi-Fi enabled");

  } else if (!strcmp(option, "disable")) {
    g_persist_config.network.enabled = 0;
    debug_println("Wi-Fi disabled");

  } else {
    debug_print("SET WIFI: unknown option '");
    debug_print(option);
    debug_println("' (use: ssid, password, dhcp, ip, gateway, netmask, dns, telnet, telnet-user, telnet-pass, telnet-port, enable, disable)");
  }

  debug_println("Hint: Use 'save' to persist configuration to NVS");
}

/* ============================================================================
 * ETHERNET COMMANDS (v6.1.0+ W5500)
 * ============================================================================ */

void cli_cmd_set_ethernet(uint8_t argc, char* argv[]) {
  if (argc < 1) {
    debug_println("SET ETHERNET: missing parameters");
    debug_println("  Usage: set ethernet <option> [value]");
    debug_println("");
    debug_println("  Options:");
    debug_println("    enable              - Enable Ethernet (W5500)");
    debug_println("    disable             - Disable Ethernet");
    debug_println("    dhcp on|off         - Enable/disable DHCP (default: on)");
    debug_println("    ip <address>        - Static IP (e.g., 192.168.1.101)");
    debug_println("    gateway <address>   - Gateway IP (e.g., 192.168.1.1)");
    debug_println("    netmask <address>   - Netmask (e.g., 255.255.255.0)");
    debug_println("    dns <address>       - DNS server (e.g., 8.8.8.8)");
    debug_println("");
    debug_println("  Note: Use 'save' to persist settings to NVS");
    return;
  }

  const char* option = argv[0];
  const char* value = (argc >= 2) ? argv[1] : "";

  if (!strcmp(option, "enable")) {
    g_persist_config.network.ethernet.enabled = 1;
    debug_println("Ethernet enabled (W5500)");

  } else if (!strcmp(option, "disable")) {
    g_persist_config.network.ethernet.enabled = 0;
    debug_println("Ethernet disabled");

  } else if (!strcmp(option, "dhcp")) {
    if (!strcmp(value, "on") || !strcmp(value, "ON")) {
      g_persist_config.network.ethernet.dhcp_enabled = 1;
      debug_println("Ethernet DHCP enabled");
    } else if (!strcmp(value, "off") || !strcmp(value, "OFF")) {
      g_persist_config.network.ethernet.dhcp_enabled = 0;
      debug_println("Ethernet DHCP disabled (use static IP settings)");
    } else {
      debug_println("SET ETHERNET DHCP: invalid value (use: on|off)");
    }

  } else if (!strcmp(option, "ip")) {
    uint32_t ip;
    if (!network_config_str_to_ip(value, &ip)) {
      debug_println("SET ETHERNET IP: invalid IP address format");
      return;
    }
    g_persist_config.network.ethernet.static_ip = ip;
    debug_print("Ethernet static IP set to: ");
    char ip_str[16];
    debug_println(network_config_ip_to_str(ip, ip_str));

  } else if (!strcmp(option, "gateway")) {
    uint32_t gw;
    if (!network_config_str_to_ip(value, &gw)) {
      debug_println("SET ETHERNET GATEWAY: invalid IP address format");
      return;
    }
    g_persist_config.network.ethernet.static_gateway = gw;
    debug_print("Ethernet gateway set to: ");
    char gw_str[16];
    debug_println(network_config_ip_to_str(gw, gw_str));

  } else if (!strcmp(option, "netmask")) {
    uint32_t nm;
    if (!network_config_str_to_ip(value, &nm)) {
      debug_println("SET ETHERNET NETMASK: invalid IP address format");
      return;
    }
    if (!network_config_is_valid_netmask(nm)) {
      debug_println("SET ETHERNET NETMASK: invalid netmask (must be contiguous bits)");
      return;
    }
    g_persist_config.network.ethernet.static_netmask = nm;
    debug_print("Ethernet netmask set to: ");
    char nm_str[16];
    debug_println(network_config_ip_to_str(nm, nm_str));

  } else if (!strcmp(option, "dns")) {
    uint32_t dns;
    if (!network_config_str_to_ip(value, &dns)) {
      debug_println("SET ETHERNET DNS: invalid IP address format");
      return;
    }
    g_persist_config.network.ethernet.static_dns = dns;
    debug_print("Ethernet DNS set to: ");
    char dns_str[16];
    debug_println(network_config_ip_to_str(dns, dns_str));

  } else {
    debug_print("SET ETHERNET: unknown option '");
    debug_print(option);
    debug_println("' (use: enable, disable, dhcp, ip, gateway, netmask, dns)");
  }

  debug_println("Hint: Use 'save' to persist configuration to NVS");
}

/* ============================================================================
 * HTTP REST API COMMANDS (v6.0.0+)
 * ============================================================================ */

void cli_cmd_set_http(uint8_t argc, char* argv[]) {
  if (argc < 2) {
    debug_println("SET HTTP: missing parameters");
    debug_println("  Usage: set http <option> <value>");
    debug_println("");
    debug_println("  Options:");
    debug_println("    enabled <on|off>    - Enable/disable HTTP server");
    debug_println("    port <port>         - Set HTTP port (default: 80, HTTPS: 443)");
    debug_println("    auth <on|off>       - Enable/disable Basic Auth");
    debug_println("    username <user>     - Set HTTP username");
    debug_println("    password <pass>     - Set HTTP password");
    debug_println("    api <on|off>        - Enable/disable API endpoints");
    debug_println("    tls <on|off>        - Enable/disable HTTPS/TLS (requires reboot)");
    debug_println("");
    debug_println("  Note: Use 'save' to persist settings to NVS");
    return;
  }

  const char* option = argv[0];
  const char* value = argv[1];

  if (!strcmp(option, "enabled") || !strcmp(option, "enable")) {
    if (!strcmp(value, "on") || !strcmp(value, "ON") || !strcmp(value, "1")) {
      g_persist_config.network.http.enabled = 1;
      debug_println("HTTP REST API enabled");
    } else if (!strcmp(value, "off") || !strcmp(value, "OFF") || !strcmp(value, "0")) {
      g_persist_config.network.http.enabled = 0;
      debug_println("HTTP REST API disabled");
    } else {
      debug_println("SET HTTP ENABLED: invalid value (use: on|off)");
    }

  } else if (!strcmp(option, "port")) {
    uint16_t port = atoi(value);
    if (port < 1 || port > 65535) {
      debug_println("SET HTTP PORT: invalid port (1-65535)");
      return;
    }
    g_persist_config.network.http.port = port;
    debug_print("HTTP port set to: ");
    debug_print_uint(port);
    debug_println("");

  } else if (!strcmp(option, "auth")) {
    if (!strcmp(value, "on") || !strcmp(value, "ON") || !strcmp(value, "1")) {
      g_persist_config.network.http.auth_enabled = 1;
      debug_println("HTTP Basic Auth enabled");
    } else if (!strcmp(value, "off") || !strcmp(value, "OFF") || !strcmp(value, "0")) {
      g_persist_config.network.http.auth_enabled = 0;
      debug_println("HTTP Basic Auth disabled");
    } else {
      debug_println("SET HTTP AUTH: invalid value (use: on|off)");
    }

  } else if (!strcmp(option, "username") || !strcmp(option, "user")) {
    if (!value || value[0] == '\0') {
      debug_println("SET HTTP USERNAME: missing username");
      return;
    }
    if (strlen(value) > HTTP_AUTH_USERNAME_MAX_LEN - 1) {
      debug_println("SET HTTP USERNAME: username too long (max 31 chars)");
      return;
    }
    strncpy(g_persist_config.network.http.username, value, HTTP_AUTH_USERNAME_MAX_LEN - 1);
    g_persist_config.network.http.username[HTTP_AUTH_USERNAME_MAX_LEN - 1] = '\0';
    debug_print("HTTP username set to: ");
    debug_println(g_persist_config.network.http.username);

  } else if (!strcmp(option, "password") || !strcmp(option, "pass")) {
    if (!value || value[0] == '\0') {
      debug_println("SET HTTP PASSWORD: missing password");
      return;
    }
    if (strlen(value) > HTTP_AUTH_PASSWORD_MAX_LEN - 1) {
      debug_println("SET HTTP PASSWORD: password too long (max 63 chars)");
      return;
    }
    strncpy(g_persist_config.network.http.password, value, HTTP_AUTH_PASSWORD_MAX_LEN - 1);
    g_persist_config.network.http.password[HTTP_AUTH_PASSWORD_MAX_LEN - 1] = '\0';
    debug_println("HTTP password set (hidden for security)");

  } else if (!strcmp(option, "api")) {
    if (!strcmp(value, "on") || !strcmp(value, "ON") || !strcmp(value, "1") || !strcmp(value, "enable")) {
      g_persist_config.network.http.api_enabled = 1;
      debug_println("HTTP API endpoints enabled");
    } else if (!strcmp(value, "off") || !strcmp(value, "OFF") || !strcmp(value, "0") || !strcmp(value, "disable")) {
      g_persist_config.network.http.api_enabled = 0;
      debug_println("HTTP API endpoints disabled");
    } else {
      debug_println("SET HTTP API: invalid value (use: on|off)");
    }

  } else if (!strcmp(option, "tls")) {
    if (!strcmp(value, "on") || !strcmp(value, "ON") || !strcmp(value, "1") || !strcmp(value, "enable")) {
      g_persist_config.network.http.tls_enabled = 1;
      debug_println("HTTPS/TLS enabled (requires reboot)");
    } else if (!strcmp(value, "off") || !strcmp(value, "OFF") || !strcmp(value, "0") || !strcmp(value, "disable")) {
      g_persist_config.network.http.tls_enabled = 0;
      debug_println("HTTPS/TLS disabled (plain HTTP, requires reboot)");
    } else {
      debug_println("SET HTTP TLS: invalid value (use: on|off)");
    }

  } else {
    debug_print("SET HTTP: unknown option '");
    debug_print(option);
    debug_println("' (use: enabled, port, auth, username, password, api, tls)");
  }

  debug_println("Hint: Use 'save' to persist configuration to NVS");
}

void cli_cmd_connect_wifi(void) {
  if (!g_persist_config.network.enabled) {
    debug_println("CONNECT WIFI: Wi-Fi is disabled in config");
    debug_println("Hint: Use 'set wifi enable' first");
    return;
  }

  // Check SSID - must not be empty
  if (strlen(g_persist_config.network.ssid) == 0) {
    debug_println("CONNECT WIFI: SSID not set (empty)");
    debug_println("Hint: Use 'set wifi ssid <network_name>' first");
    return;
  }

  if (!network_config_is_valid_ssid(g_persist_config.network.ssid)) {
    debug_println("CONNECT WIFI: SSID contains invalid characters");
    debug_println("Hint: SSID must be 1-32 printable ASCII characters");
    return;
  }

  // Check password
  int pwd_len = strlen(g_persist_config.network.password);
  if (pwd_len > 0 && pwd_len < 8) {
    debug_println("CONNECT WIFI: password too short");
    debug_println("Hint: Password must be empty (open network) or 8-63 characters");
    return;
  }

  debug_print("Connecting to Wi-Fi: ");
  debug_println(g_persist_config.network.ssid);

  if (network_manager_connect(&g_persist_config.network) == 0) {
    debug_println("Wi-Fi connection started (async)");
    debug_println("Use 'show wifi' to check connection status");
  } else {
    debug_println("CONNECT WIFI: connection failed - check configuration");
    debug_println("Hint: Use 'show config' to review network settings");
  }
}

void cli_cmd_disconnect_wifi(void) {
  debug_println("Disconnecting from Wi-Fi...");
  if (network_manager_stop() == 0) {
    debug_println("Wi-Fi disconnected");
  } else {
    debug_println("DISCONNECT WIFI: failed");
  }
}
