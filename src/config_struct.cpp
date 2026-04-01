/**
 * @file config_struct.cpp
 * @brief Configuration struct utilities implementation
 *
 * Contains the global PersistConfig structure used throughout the system
 */

#include "config_struct.h"
#include "constants.h"
#include <string.h>

// Global persistent configuration (accessible to all modules)
PersistConfig g_persist_config = {0};

PersistConfig* config_struct_create_default(void) {
  memset(&g_persist_config, 0, sizeof(PersistConfig));
  g_persist_config.schema_version = CONFIG_SCHEMA_VERSION;  // Current schema version

  // Modbus Slave defaults (v4.4.1+)
  g_persist_config.modbus_slave.enabled = true;
  g_persist_config.modbus_slave.slave_id = 1;
  g_persist_config.modbus_slave.baudrate = 115200;
  g_persist_config.modbus_slave.parity = 0;  // None
  g_persist_config.modbus_slave.stop_bits = 1;
  g_persist_config.modbus_slave.inter_frame_delay = 0;  // 0=auto (t3.5 from baudrate)

  strncpy(g_persist_config.hostname, "modbus-esp32", 31);
  g_persist_config.hostname[31] = '\0';
  g_persist_config.remote_echo = 1;  // Default: echo ON

  // HTTP defaults (v6.0.4+)
  g_persist_config.network.http.api_enabled = 1;  // API enabled by default
  g_persist_config.network.http.priority = 1;     // NORMAL priority

  // ST Logic configuration (v4.1+)
  g_persist_config.st_logic_interval_ms = 10;  // Default: 10ms execution interval

  // Initialize all var_maps as unused (important for CRC stability)
  for (uint8_t i = 0; i < 32; i++) {
    g_persist_config.var_maps[i].input_reg = 65535;
    g_persist_config.var_maps[i].coil_reg = 65535;
    g_persist_config.var_maps[i].associated_counter = 0xff;
    g_persist_config.var_maps[i].associated_timer = 0xff;
    g_persist_config.var_maps[i].source_type = 0xff;
    g_persist_config.var_maps[i].input_type = 0;      // Default: Holding Register
    g_persist_config.var_maps[i].output_type = 0;     // Default: Holding Register
  }
  g_persist_config.var_map_count = 0;

  return &g_persist_config;
}
