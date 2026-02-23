/**
 * @file config_load.cpp
 * @brief Configuration load from NVS with CRC validation
 *
 * LAYER 6: Persistence
 * Responsibility: Load configuration from NVS and validate integrity
 */

#include "config_load.h"
#include "config_save.h"
#include "constants.h"
#include "debug.h"
#include "debug_flags.h"
#include "network_config.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <cstddef>
#include <cstring>

// NVS key for storing config
#define NVS_CONFIG_KEY "modbus_cfg"
#define NVS_NAMESPACE  "modbus"

/**
 * @brief Initialize configuration with factory defaults
 */
static void config_init_defaults(PersistConfig* cfg) {
  memset(cfg, 0, sizeof(PersistConfig));
  cfg->schema_version = CONFIG_SCHEMA_VERSION;  // Current schema version

  // Modbus Slave defaults (v4.4.1+)
  cfg->modbus_slave.enabled = true;
  cfg->modbus_slave.slave_id = 1;
  cfg->modbus_slave.baudrate = 9600;
  cfg->modbus_slave.parity = 0;  // None
  cfg->modbus_slave.stop_bits = 1;
  cfg->modbus_slave.inter_frame_delay = 10;

  strncpy(cfg->hostname, "modbus-esp32", 31);  // Default hostname (v3.2+)
  cfg->hostname[31] = '\0';
  cfg->remote_echo = 1;  // Default: echo ON (v3.2+)

  // Initialize persistent register system (v4.0+)
  memset(&cfg->persist_regs, 0, sizeof(PersistentRegisterData));
  cfg->persist_regs.enabled = 0;  // Disabled by default
  cfg->persist_regs.group_count = 0;

  // ST Logic configuration (v4.1+)
  cfg->st_logic_interval_ms = 10;  // Default: 10ms execution interval

  // Modbus Master configuration (v4.4+)
  cfg->modbus_master.enabled = false;  // Disabled by default
  cfg->modbus_master.baudrate = MODBUS_MASTER_DEFAULT_BAUDRATE;  // 9600
  cfg->modbus_master.parity = MODBUS_MASTER_DEFAULT_PARITY;  // 0 (none)
  cfg->modbus_master.stop_bits = MODBUS_MASTER_DEFAULT_STOP_BITS;  // 1
  cfg->modbus_master.timeout_ms = MODBUS_MASTER_DEFAULT_TIMEOUT;  // 500ms
  cfg->modbus_master.inter_frame_delay = MODBUS_MASTER_DEFAULT_INTER_FRAME;  // 10ms
  cfg->modbus_master.max_requests_per_cycle = MODBUS_MASTER_DEFAULT_MAX_REQUESTS;  // 10
  cfg->modbus_master.total_requests = 0;
  cfg->modbus_master.successful_requests = 0;
  cfg->modbus_master.timeout_errors = 0;
  cfg->modbus_master.crc_errors = 0;
  cfg->modbus_master.exception_errors = 0;

  // Initialize network config with defaults (v3.0+)
  network_config_init_defaults(&cfg->network);

  // Initialize all GPIO mappings as unused (reduced to 32 slots for NVS space)
  for (uint8_t i = 0; i < 32; i++) {
    cfg->var_maps[i].input_reg = 65535;
    cfg->var_maps[i].coil_reg = 65535;
    cfg->var_maps[i].associated_counter = 0xff;
    cfg->var_maps[i].associated_timer = 0xff;
    cfg->var_maps[i].source_type = 0xff;  // Mark as unused
    cfg->var_maps[i].input_type = 0;      // Default: Holding Register
    cfg->var_maps[i].output_type = 0;     // Default: Holding Register
  }
  cfg->var_map_count = 0;  // No mappings in default config
}

bool config_load_from_nvs(PersistConfig* out) {
  if (out == NULL) {
    debug_println("ERROR: config_load_from_nvs - NULL output");
    return false;
  }

  DebugFlags* dbg = debug_flags_get();
  if (dbg->config_load) {
    debug_println("[LOAD_START] Loading config from NVS...");
  }

  // Open NVS
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    debug_println("CONFIG LOAD: NVS namespace not found, using defaults");
    config_init_defaults(out);
    return true;
  } else if (err != ESP_OK) {
    debug_print("ERROR: NVS open failed: ");
    debug_print_uint(err);
    debug_println(", using defaults");
    config_init_defaults(out);
    return true;
  }

  // Read config blob from NVS
  size_t required_size = sizeof(PersistConfig);
  if (dbg->config_load) {
    debug_print("[LOAD_DEBUG] Reading blob, size=");
    debug_print_uint(required_size);
    debug_println("");
  }
  err = nvs_get_blob(handle, NVS_CONFIG_KEY, out, &required_size);
  if (dbg->config_load) {
    debug_print("[LOAD_DEBUG] nvs_get_blob returned err=");
    debug_print_uint(err);
    debug_print(" required_size=");
    debug_print_uint(required_size);
    debug_println("");
  }
  nvs_close(handle);

  if (err == ESP_ERR_NVS_NOT_FOUND) {
    debug_println("CONFIG LOAD: Config key not found, using defaults");
    config_init_defaults(out);
    return true;
  } else if (err != ESP_OK) {
    debug_print("ERROR: NVS get_blob failed: ");
    debug_print_uint(err);
    debug_println(", using defaults");
    config_init_defaults(out);
    return true;
  }

  if (dbg->config_load) {
    debug_print("[LOAD_DEBUG] After nvs_get_blob: var_map_count=");
    debug_print_uint(out->var_map_count);
    debug_print(" schema_version=");
    debug_print_uint(out->schema_version);
    debug_print(" crc16=");
    debug_print_uint(out->crc16);
    debug_println("");

    // DEBUG: Dump first few bytes of loaded config
    debug_println("[LOAD_DEBUG] First 20 bytes of loaded data:");
    uint8_t* data = (uint8_t*)out;
    for (int i = 0; i < 20; i++) {
      debug_print("  [");
      debug_print_uint(i);
      debug_print("]=0x");
      if (data[i] < 16) debug_print("0");
      debug_print_uint(data[i]);
      debug_print(" ");
    }
    debug_println("");
  }

  // Validate schema version (MUST be checked before CRC to prevent struct misalignment)
  if (out->schema_version != CONFIG_SCHEMA_VERSION) {
    // Schema migration support (v7 → v8 → v9)
    if (out->schema_version == 7) {
      debug_println("CONFIG LOAD: Migrating schema 7 → 8 (adding persist_regs)");

      // Initialize new persist_regs field with defaults
      memset(&out->persist_regs, 0, sizeof(PersistentRegisterData));
      out->persist_regs.enabled = 0;
      out->persist_regs.group_count = 0;

      // Update schema version
      out->schema_version = 8;

      debug_println("CONFIG LOAD: Migration 7→8 complete");
      // Note: CRC will be invalid, but we'll recalculate on next save

      // Fall through to v8→v9 migration
    }

    if (out->schema_version == 8) {
      debug_println("CONFIG LOAD: Migrating schema 8 → 9 (STATIC register multi-type support)");

      // Migrate STATIC register mappings (old format → new format)
      // OLD v8: struct { uint16_t register_address; uint16_t static_value; }
      // NEW v9: struct { uint16_t register_address; uint8_t value_type; uint8_t reserved; union { uint16_t value_16; uint32_t value_32; float value_real; }; }

      // The old struct was 4 bytes, new struct is 8 bytes (with union)
      // We need to convert in-place from the END of the array backwards to avoid overwriting

      typedef struct __attribute__((packed)) {
        uint16_t register_address;
        uint16_t static_value;
      } OldStaticRegisterMapping;

      OldStaticRegisterMapping* old_regs = (OldStaticRegisterMapping*)out->static_regs;
      uint8_t count = out->static_reg_count;

      if (count > MAX_DYNAMIC_REGS) {
        count = MAX_DYNAMIC_REGS;  // Safety clamp
      }

      // Convert from END to START (to avoid overwriting data)
      for (int i = count - 1; i >= 0; i--) {
        uint16_t addr = old_regs[i].register_address;
        uint16_t value = old_regs[i].static_value;

        // Write new format
        out->static_regs[i].register_address = addr;
        out->static_regs[i].value_type = MODBUS_TYPE_UINT;  // Default to UINT
        out->static_regs[i].reserved = 0;
        out->static_regs[i].value_16 = value;
      }

      // Update schema version
      out->schema_version = 9;

      debug_println("CONFIG LOAD: Migration 8→9 complete");
      // Note: CRC will be invalid, but we'll recalculate on next save
      // Fall through to v9→v10 migration
    }

    if (out->schema_version == 9) {
      debug_println("CONFIG LOAD: Migrating schema 9 → 10 (HTTP REST API support)");

      // Initialize HTTP config with defaults (new field in NetworkConfig)
      out->network.http.enabled = 1;                  // HTTP enabled by default
      out->network.http.port = HTTP_SERVER_PORT;      // Port 80
      out->network.http.auth_enabled = 0;             // No auth by default
      strncpy(out->network.http.username, "admin", sizeof(out->network.http.username) - 1);
      out->network.http.username[sizeof(out->network.http.username) - 1] = '\0';
      strncpy(out->network.http.password, "modbus123", sizeof(out->network.http.password) - 1);
      out->network.http.password[sizeof(out->network.http.password) - 1] = '\0';
      out->network.http.tls_enabled = 0;
      out->network.http.api_enabled = 1;              // API enabled by default
      out->network.http.priority = 1;                 // NORMAL priority
      memset(out->network.http.reserved, 0, sizeof(out->network.http.reserved));

      // Update schema version
      out->schema_version = 10;

      debug_println("CONFIG LOAD: Migration 9→10 complete");
      // Note: CRC will be invalid, but we'll recalculate on next save
      // Fall through to v10→v11 migration
    }

    if (out->schema_version == 10) {
      debug_println("CONFIG LOAD: Migrating schema 10 → 11 (W5500 Ethernet support)");

      // Initialize Ethernet config with defaults (new field in NetworkConfig)
      out->network.ethernet.enabled = 0;              // Disabled by default
      out->network.ethernet.dhcp_enabled = 1;          // DHCP by default
      out->network.ethernet.static_ip = 0;
      out->network.ethernet.static_gateway = 0;
      out->network.ethernet.static_netmask = 0;
      out->network.ethernet.static_dns = 0;
      memset(out->network.ethernet.hostname, 0, sizeof(out->network.ethernet.hostname));
      memset(out->network.ethernet.reserved, 0, sizeof(out->network.ethernet.reserved));

      // Update schema version
      out->schema_version = 11;

      debug_println("CONFIG LOAD: Migration 10→11 complete");
      // Note: CRC will be invalid, but we'll recalculate on next save
    } else if (out->schema_version != CONFIG_SCHEMA_VERSION) {
      debug_print("ERROR: Unsupported schema version (stored=");
      debug_print_uint(out->schema_version);
      debug_print(", current=");
      debug_print_uint(CONFIG_SCHEMA_VERSION);
      debug_println("), reinitializing with defaults");
      config_init_defaults(out);
      return true;
    }
  }

  if (dbg->config_load) {
    debug_println("[LOAD_DEBUG] Schema version OK, checking CRC...");
  }

  // Validate CRC
  uint16_t stored_crc = out->crc16;
  uint16_t calculated_crc = config_calculate_crc16(out);

  if (stored_crc != calculated_crc) {
    debug_print("ERROR: CRC mismatch (stored=");
    debug_print_uint(stored_crc);
    debug_print(", calculated=");
    debug_print_uint(calculated_crc);
    debug_print(") - CONFIG CORRUPTED, REJECTING");
    debug_println("");
    debug_println("SECURITY: Corrupt config detected and rejected");
    debug_println("  Reinitializing with factory defaults");
    config_init_defaults(out);
    return false;  // CRITICAL FIX: Return false to indicate load failure
  }

  // BUG-140: Sanitize count fields to prevent out-of-bounds access
  bool sanitized = false;

  if (out->var_map_count > 32) {
    debug_print("WARN: var_map_count=");
    debug_print_uint(out->var_map_count);
    debug_println(" exceeds max, clamping to 32");
    out->var_map_count = 32;
    sanitized = true;
  }

  if (out->persist_regs.group_count > PERSIST_MAX_GROUPS) {
    debug_print("WARN: persist group_count=");
    debug_print_uint(out->persist_regs.group_count);
    debug_println(" exceeds max, clamping to 8");
    out->persist_regs.group_count = PERSIST_MAX_GROUPS;
    sanitized = true;
  }

  if (out->static_reg_count > MAX_DYNAMIC_REGS) {
    debug_print("WARN: static_reg_count=");
    debug_print_uint(out->static_reg_count);
    debug_println(" exceeds max, clamping");
    out->static_reg_count = MAX_DYNAMIC_REGS;
    sanitized = true;
  }

  if (out->static_coil_count > MAX_DYNAMIC_COILS) {
    debug_print("WARN: static_coil_count=");
    debug_print_uint(out->static_coil_count);
    debug_println(" exceeds max, clamping");
    out->static_coil_count = MAX_DYNAMIC_COILS;
    sanitized = true;
  }

  // Print summary
  debug_print("CONFIG LOADED: schema=");
  debug_print_uint(out->schema_version);
  debug_print(", slave_id=");
  debug_print_uint(out->modbus_slave.slave_id);
  debug_print(", baudrate=");
  debug_print_uint(out->modbus_slave.baudrate);
  debug_print(", var_maps=");
  debug_print_uint(out->var_map_count);
  debug_print(", static_regs=");
  debug_print_uint(out->static_reg_count);
  debug_print(", static_coils=");
  debug_print_uint(out->static_coil_count);
  debug_print(", CRC=");
  debug_print_uint(calculated_crc);
  debug_println(" OK");

  if (sanitized) {
    debug_println("WARN: Config had out-of-bounds values (sanitized)");
  }

  // Debug: Print loaded GPIO mappings
  if (out->var_map_count > 0) {
    debug_println("  Loaded variable mappings:");
    for (uint8_t i = 0; i < out->var_map_count; i++) {
      const VariableMapping* map = &out->var_maps[i];
      debug_print("    [");
      debug_print_uint(i);
      debug_print("] source_type=");
      debug_print_uint(map->source_type);
      debug_print(" gpio_pin=");
      debug_print_uint(map->gpio_pin);
      debug_print(" is_input=");
      debug_print_uint(map->is_input);
      debug_print(" input_reg=");
      debug_print_uint(map->input_reg);
      debug_print(" coil_reg=");
      debug_print_uint(map->coil_reg);
      debug_println("");
    }
  }

  return true;
}
