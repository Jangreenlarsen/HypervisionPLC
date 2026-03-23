/**
 * @file cli_commands_modbus_slave.cpp
 * @brief CLI Commands for Modbus Slave Configuration
 */

#include <Arduino.h>
#include "cli_commands_modbus_slave.h"
#include "config_struct.h"
#include "constants.h"

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
  Serial.printf("[OK] Modbus Slave %s (mode: %s)\n", enabled ? "ENABLED" : "DISABLED",
    g_persist_config.modbus_mode == MODBUS_MODE_SLAVE ? "slave" :
    g_persist_config.modbus_mode == MODBUS_MODE_MASTER ? "master" : "off");
  Serial.println("NOTE: Use 'save' to persist to NVS");
}

void cli_cmd_set_modbus_slave_slave_id(uint8_t id) {
  if (id == 0 || id > 247) {
    Serial.println("ERROR: Invalid slave ID (must be 1-247)");
    return;
  }

  g_persist_config.modbus_slave.slave_id = id;
  Serial.printf("[OK] Modbus Slave ID: %u (takes effect on reboot)\n", id);
  Serial.println("NOTE: Use 'save' to persist to NVS");
}

void cli_cmd_set_modbus_slave_baudrate(uint32_t baudrate) {
  // Validate baudrate
  if (baudrate != 9600 && baudrate != 19200 && baudrate != 38400 &&
      baudrate != 57600 && baudrate != 115200) {
    Serial.println("ERROR: Invalid baudrate (9600, 19200, 38400, 57600, 115200)");
    return;
  }

  g_persist_config.modbus_slave.baudrate = baudrate;
  Serial.printf("[OK] Modbus Slave baudrate: %u (takes effect on reboot)\n", baudrate);
  Serial.println("NOTE: Use 'save' to persist to NVS");
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
    Serial.println("ERROR: Invalid parity (none, even, odd)");
    return;
  }

  g_persist_config.modbus_slave.parity = parity_val;
  Serial.printf("[OK] Modbus Slave parity: %s (takes effect on reboot)\n", parity);
  Serial.println("NOTE: Use 'save' to persist to NVS");
}

void cli_cmd_set_modbus_slave_stop_bits(uint8_t bits) {
  if (bits != 1 && bits != 2) {
    Serial.println("ERROR: Invalid stop bits (1 or 2)");
    return;
  }

  g_persist_config.modbus_slave.stop_bits = bits;
  Serial.printf("[OK] Modbus Slave stop bits: %u (takes effect on reboot)\n", bits);
  Serial.println("NOTE: Use 'save' to persist to NVS");
}

void cli_cmd_set_modbus_slave_inter_frame_delay(uint16_t ms) {
  if (ms > 1000) {
    Serial.println("ERROR: Invalid inter-frame delay (0-1000 ms)");
    return;
  }

  g_persist_config.modbus_slave.inter_frame_delay = ms;
  Serial.printf("[OK] Modbus Slave inter-frame delay: %u ms (takes effect on reboot)\n", ms);
  Serial.println("NOTE: Use 'save' to persist to NVS");
}

/* ============================================================================
 * SHOW COMMAND
 * ============================================================================ */

void cli_cmd_show_modbus_slave() {
  Serial.println("\n=== MODBUS SLAVE CONFIGURATION ===");
  Serial.printf("Status: %s\n", g_persist_config.modbus_slave.enabled ? "ENABLED" : "DISABLED");
  Serial.println("Hardware: UART0 (Serial)");
  Serial.println();

  Serial.println("Communication:");
  Serial.printf("  Slave ID: %u\n", g_persist_config.modbus_slave.slave_id);
  Serial.printf("  Baudrate: %u\n", g_persist_config.modbus_slave.baudrate);

  const char *parity_str = "None";
  if (g_persist_config.modbus_slave.parity == 1) parity_str = "Even";
  else if (g_persist_config.modbus_slave.parity == 2) parity_str = "Odd";
  Serial.printf("  Parity: %s\n", parity_str);
  Serial.printf("  Stop bits: %u\n", g_persist_config.modbus_slave.stop_bits);
  Serial.println();

  Serial.println("Timing:");
  Serial.printf("  Inter-frame delay: %u ms\n", g_persist_config.modbus_slave.inter_frame_delay);
  Serial.println();

  Serial.println("Statistics:");
  Serial.printf("  Total requests: %u\n", g_persist_config.modbus_slave.total_requests);
  Serial.printf("  Successful: %u (%.1f%%)\n",
                g_persist_config.modbus_slave.successful_requests,
                g_persist_config.modbus_slave.total_requests > 0
                  ? (100.0 * g_persist_config.modbus_slave.successful_requests / g_persist_config.modbus_slave.total_requests)
                  : 0.0);
  Serial.printf("  CRC errors: %u\n", g_persist_config.modbus_slave.crc_errors);
  Serial.printf("  Exceptions: %u\n", g_persist_config.modbus_slave.exception_errors);
  Serial.println();
}
