/**
 * @file cli_commands_modbus_slave.cpp
 * @brief CLI Commands for Modbus Slave Configuration
 */

#include <Arduino.h>
#include "cli_commands_modbus_slave.h"
#include "config_struct.h"
#include "constants.h"
#include "debug.h"

// Defined in cli_commands_modbus_master.cpp
extern uint16_t modbus_calc_t35_ms(uint32_t baudrate);
extern uint16_t modbus_effective_inter_frame(uint16_t configured, uint32_t baudrate);

/* ============================================================================
 * SET COMMANDS
 * ============================================================================ */

void cli_cmd_set_modbus_slave_enabled(bool enabled) {
  g_persist_config.modbus_slave.enabled = enabled;
  // Sync modbus_mode to stay consistent
  if (enabled) {
    g_persist_config.modbus_mode = MODBUS_MODE_SLAVE;
    g_persist_config.modbus_master.enabled = false;
  } else if (!g_persist_config.modbus_master.enabled) {
    g_persist_config.modbus_mode = MODBUS_MODE_OFF;
  }
  debug_printf("[OK] Modbus Slave %s (mode: %s)\n", enabled ? "ENABLED" : "DISABLED",
    g_persist_config.modbus_mode == MODBUS_MODE_SLAVE ? "slave" :
    g_persist_config.modbus_mode == MODBUS_MODE_MASTER ? "master" : "off");
  debug_println("NOTE: Use 'save' to persist to NVS");
}

void cli_cmd_set_modbus_slave_slave_id(uint8_t id) {
  if (id == 0 || id > 247) {
    debug_println("ERROR: Invalid slave ID (must be 1-247)");
    return;
  }

  g_persist_config.modbus_slave.slave_id = id;
  debug_printf("[OK] Modbus Slave ID: %u (takes effect on reboot)\n", id);
  debug_println("NOTE: Use 'save' to persist to NVS");
}

void cli_cmd_set_modbus_slave_baudrate(uint32_t baudrate) {
  // Validate baudrate
  if (baudrate != 9600 && baudrate != 19200 && baudrate != 38400 &&
      baudrate != 57600 && baudrate != 115200) {
    debug_println("ERROR: Invalid baudrate (9600, 19200, 38400, 57600, 115200)");
    return;
  }

  g_persist_config.modbus_slave.baudrate = baudrate;

  uint16_t eff = modbus_effective_inter_frame(g_persist_config.modbus_slave.inter_frame_delay, baudrate);
  bool is_auto = (g_persist_config.modbus_slave.inter_frame_delay == 0);
  debug_printf("[OK] Modbus Slave baudrate: %u, inter-frame: %u ms (%s)\n",
               baudrate, eff, is_auto ? "auto t3.5" : "manual");
  debug_println("NOTE: Use 'save' to persist to NVS");
}

void cli_cmd_set_modbus_slave_parity(const char *parity) {
  uint8_t parity_val;

  if (strcasecmp(parity, "none") == 0 || strcasecmp(parity, "n") == 0) {
    parity_val = 0;
  } else if (strcasecmp(parity, "even") == 0 || strcasecmp(parity, "e") == 0) {
    parity_val = 1;
  } else if (strcasecmp(parity, "odd") == 0 || strcasecmp(parity, "o") == 0) {
    parity_val = 2;
  } else {
    debug_println("ERROR: Invalid parity (none, even, odd)");
    return;
  }

  g_persist_config.modbus_slave.parity = parity_val;
  debug_printf("[OK] Modbus Slave parity: %s (takes effect on reboot)\n", parity);
  debug_println("NOTE: Use 'save' to persist to NVS");
}

void cli_cmd_set_modbus_slave_stop_bits(uint8_t bits) {
  if (bits != 1 && bits != 2) {
    debug_println("ERROR: Invalid stop bits (1 or 2)");
    return;
  }

  g_persist_config.modbus_slave.stop_bits = bits;
  debug_printf("[OK] Modbus Slave stop bits: %u (takes effect on reboot)\n", bits);
  debug_println("NOTE: Use 'save' to persist to NVS");
}

void cli_cmd_set_modbus_slave_inter_frame_delay(uint16_t ms) {
  if (ms > 1000) {
    debug_println("ERROR: Invalid inter-frame delay (0=auto, 1-1000 ms manual)");
    return;
  }

  g_persist_config.modbus_slave.inter_frame_delay = ms;

  if (ms == 0) {
    uint16_t t35 = modbus_calc_t35_ms(g_persist_config.modbus_slave.baudrate);
    debug_printf("[OK] Modbus Slave inter-frame: AUTO (t3.5 = %u ms @ %u baud)\n",
                 t35, g_persist_config.modbus_slave.baudrate);
  } else {
    debug_printf("[OK] Modbus Slave inter-frame: %u ms (manual)\n", ms);
  }
  debug_println("NOTE: Use 'save' to persist to NVS");
}

/* ============================================================================
 * SHOW COMMAND
 * ============================================================================ */

void cli_cmd_show_modbus_slave() {
  debug_printf("\n=== MODBUS SLAVE CONFIGURATION ===\n");
  debug_printf("Status: %s\n", g_persist_config.modbus_slave.enabled ? "ENABLED" : "DISABLED");
  debug_printf("Hardware: UART0 (Serial)\n");
  debug_printf("\n");

  debug_printf("Communication:\n");
  debug_printf("  Slave ID: %u\n", g_persist_config.modbus_slave.slave_id);
  debug_printf("  Baudrate: %u\n", g_persist_config.modbus_slave.baudrate);

  const char *parity_str = "None";
  if (g_persist_config.modbus_slave.parity == 1) parity_str = "Even";
  else if (g_persist_config.modbus_slave.parity == 2) parity_str = "Odd";
  debug_printf("  Parity: %s\n", parity_str);
  debug_printf("  Stop bits: %u\n", g_persist_config.modbus_slave.stop_bits);
  debug_printf("\n");

  debug_printf("Timing:\n");
  if (g_persist_config.modbus_slave.inter_frame_delay == 0) {
    uint16_t t35 = modbus_calc_t35_ms(g_persist_config.modbus_slave.baudrate);
    debug_printf("  Inter-frame delay: AUTO (t3.5 = %u ms @ %u baud)\n", t35, g_persist_config.modbus_slave.baudrate);
  } else {
    debug_printf("  Inter-frame delay: %u ms (manual)\n", g_persist_config.modbus_slave.inter_frame_delay);
  }
  debug_printf("\n");

  debug_printf("Statistics:\n");
  debug_printf("  Total requests: %u\n", g_persist_config.modbus_slave.total_requests);
  debug_printf("  Successful: %u (%.1f%%)\n",
                g_persist_config.modbus_slave.successful_requests,
                g_persist_config.modbus_slave.total_requests > 0
                  ? (100.0 * g_persist_config.modbus_slave.successful_requests / g_persist_config.modbus_slave.total_requests)
                  : 0.0);
  debug_printf("  CRC errors: %u\n", g_persist_config.modbus_slave.crc_errors);
  debug_printf("  Exceptions: %u\n", g_persist_config.modbus_slave.exception_errors);
  debug_printf("\n");
}
