/**
 * @file cli_commands_modbus_master.cpp
 * @brief CLI Commands for Modbus Master Configuration
 */

#include <Arduino.h>
#include "modbus_master.h"
#include "config_struct.h"

/* ============================================================================
 * SET COMMANDS
 * ============================================================================ */

void cli_cmd_set_modbus_master_enabled(bool enabled) {
  modbus_master_set_enabled(enabled);
  g_persist_config.modbus_master.enabled = enabled;
  // Sync modbus_mode to stay consistent
  if (enabled) {
    g_persist_config.modbus_mode = MODBUS_MODE_MASTER;
    g_persist_config.modbus_slave.enabled = false;
  } else if (!g_persist_config.modbus_slave.enabled) {
    g_persist_config.modbus_mode = MODBUS_MODE_OFF;
  }
  Serial.printf("[OK] Modbus Master %s (mode: %s)\n", enabled ? "ENABLED" : "DISABLED",
    g_persist_config.modbus_mode == MODBUS_MODE_SLAVE ? "slave" :
    g_persist_config.modbus_mode == MODBUS_MODE_MASTER ? "master" : "off");
  Serial.println("NOTE: Use 'save' to persist to NVS");
}

void cli_cmd_set_modbus_master_baudrate(uint32_t baudrate) {
  // Validate baudrate
  if (baudrate != 9600 && baudrate != 19200 && baudrate != 38400 &&
      baudrate != 57600 && baudrate != 115200) {
    Serial.println("ERROR: Invalid baudrate (9600, 19200, 38400, 57600, 115200)");
    return;
  }

  g_modbus_master_config.baudrate = baudrate;
  g_persist_config.modbus_master.baudrate = baudrate;  // Update persist config for show config
  if (g_modbus_master_config.enabled) {
    modbus_master_reconfigure();
  }
  Serial.printf("[OK] Modbus Master baudrate: %u (takes effect on reboot)\n", baudrate);
  Serial.println("NOTE: Use 'save' to persist to NVS");
}

void cli_cmd_set_modbus_master_parity(const char *parity) {
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

  g_modbus_master_config.parity = parity_val;
  g_persist_config.modbus_master.parity = parity_val;  // Update persist config for show config
  if (g_modbus_master_config.enabled) {
    modbus_master_reconfigure();
  }
  Serial.printf("[OK] Modbus Master parity: %s (takes effect on reboot)\n", parity);
  Serial.println("NOTE: Use 'save' to persist to NVS");
}

void cli_cmd_set_modbus_master_stop_bits(uint8_t bits) {
  if (bits != 1 && bits != 2) {
    Serial.println("ERROR: Invalid stop bits (1 or 2)");
    return;
  }

  g_modbus_master_config.stop_bits = bits;
  g_persist_config.modbus_master.stop_bits = bits;  // Update persist config for show config
  if (g_modbus_master_config.enabled) {
    modbus_master_reconfigure();
  }
  Serial.printf("[OK] Modbus Master stop bits: %u (takes effect on reboot)\n", bits);
  Serial.println("NOTE: Use 'save' to persist to NVS");
}

void cli_cmd_set_modbus_master_timeout(uint16_t ms) {
  if (ms < 100 || ms > 5000) {
    Serial.println("ERROR: Invalid timeout (100-5000 ms)");
    return;
  }

  g_modbus_master_config.timeout_ms = ms;
  g_persist_config.modbus_master.timeout_ms = ms;  // Update persist config for show config
  Serial.printf("[OK] Modbus Master timeout: %u ms (takes effect on reboot)\n", ms);
  Serial.println("NOTE: Use 'save' to persist to NVS");
}

void cli_cmd_set_modbus_master_inter_frame_delay(uint16_t ms) {
  if (ms > 1000) {
    Serial.println("ERROR: Invalid inter-frame delay (0-1000 ms)");
    return;
  }

  g_modbus_master_config.inter_frame_delay = ms;
  g_persist_config.modbus_master.inter_frame_delay = ms;  // Update persist config for show config
  Serial.printf("[OK] Modbus Master inter-frame delay: %u ms (takes effect on reboot)\n", ms);
  Serial.println("NOTE: Use 'save' to persist to NVS");
}

void cli_cmd_set_modbus_master_max_requests(uint8_t count) {
  if (count == 0 || count > 100) {
    Serial.println("ERROR: Invalid max requests (1-100)");
    return;
  }

  g_modbus_master_config.max_requests_per_cycle = count;
  g_persist_config.modbus_master.max_requests_per_cycle = count;  // Update persist config for show config
  Serial.printf("[OK] Modbus Master max requests/cycle: %u (takes effect on reboot)\n", count);
  Serial.println("NOTE: Use 'save' to persist to NVS");
}

/* ============================================================================
 * SHOW COMMAND
 * ============================================================================ */

void cli_cmd_show_modbus_master() {
  Serial.println("\n=== MODBUS MASTER CONFIGURATION ===");
  Serial.printf("Status: %s\n", g_modbus_master_config.enabled ? "ENABLED" : "DISABLED");
  Serial.printf("Hardware: UART1 (TX:GPIO%d, RX:GPIO%d, DE:GPIO%d)\n",
                MODBUS_MASTER_TX_PIN, MODBUS_MASTER_RX_PIN, MODBUS_MASTER_DE_PIN);
  Serial.println();

  Serial.println("Communication:");
  Serial.printf("  Baudrate: %u\n", g_modbus_master_config.baudrate);

  const char *parity_str = "None";
  if (g_modbus_master_config.parity == 1) parity_str = "Even";
  else if (g_modbus_master_config.parity == 2) parity_str = "Odd";
  Serial.printf("  Parity: %s\n", parity_str);
  Serial.printf("  Stop bits: %u\n", g_modbus_master_config.stop_bits);
  Serial.println();

  Serial.println("Timing:");
  Serial.printf("  Timeout: %u ms\n", g_modbus_master_config.timeout_ms);
  Serial.printf("  Inter-frame delay: %u ms\n", g_modbus_master_config.inter_frame_delay);
  Serial.printf("  Max requests/cycle: %u\n", g_modbus_master_config.max_requests_per_cycle);
  Serial.println();

  Serial.println("Statistics:");
  Serial.printf("  Total requests: %u\n", g_modbus_master_config.total_requests);
  Serial.printf("  Successful: %u (%.1f%%)\n",
                g_modbus_master_config.successful_requests,
                g_modbus_master_config.total_requests > 0
                  ? (100.0 * g_modbus_master_config.successful_requests / g_modbus_master_config.total_requests)
                  : 0.0);
  Serial.printf("  Timeouts: %u (%.1f%%)\n",
                g_modbus_master_config.timeout_errors,
                g_modbus_master_config.total_requests > 0
                  ? (100.0 * g_modbus_master_config.timeout_errors / g_modbus_master_config.total_requests)
                  : 0.0);
  Serial.printf("  CRC errors: %u\n", g_modbus_master_config.crc_errors);
  Serial.printf("  Exceptions: %u\n", g_modbus_master_config.exception_errors);
  Serial.println();
}
