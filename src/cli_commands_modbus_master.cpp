/**
 * @file cli_commands_modbus_master.cpp
 * @brief CLI Commands for Modbus Master Configuration
 */

#include <Arduino.h>
#include "modbus_master.h"
#include "mb_async.h"
#include "config_struct.h"
#include "debug.h"

// Calculate Modbus RTU t3.5 inter-frame delay from baudrate
// Per spec: t3.5 = 3.5 * 11 bits / baudrate * 1000 ms
// For baudrates > 19200: fixed 1.75ms minimum (spec recommendation)
// We add 1ms margin for OS scheduling jitter on ESP32
uint16_t modbus_calc_t35_ms(uint32_t baudrate) {
  if (baudrate > 19200) {
    return 2;   // 1.75ms spec minimum + margin → 2ms
  }
  // t3.5 = 3.5 chars * 11 bits/char / baudrate * 1000 ms/s
  uint16_t t35 = (uint16_t)((3.5f * 11.0f / (float)baudrate) * 1000.0f + 1.5f);
  if (t35 < 2) t35 = 2;  // minimum 2ms
  return t35;
}

// Resolve effective inter-frame delay: 0=auto → calculate from baudrate
uint16_t modbus_effective_inter_frame(uint16_t configured, uint32_t baudrate) {
  return (configured == 0) ? modbus_calc_t35_ms(baudrate) : configured;
}

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
  debug_printf("[OK] Modbus Master %s (mode: %s)\n", enabled ? "ENABLED" : "DISABLED",
    g_persist_config.modbus_mode == MODBUS_MODE_SLAVE ? "slave" :
    g_persist_config.modbus_mode == MODBUS_MODE_MASTER ? "master" : "off");
  debug_println("NOTE: Use 'save' to persist to NVS");
}

void cli_cmd_set_modbus_master_baudrate(uint32_t baudrate) {
  // Validate baudrate
  if (baudrate != 2400 && baudrate != 4800 && baudrate != 9600 &&
      baudrate != 19200 && baudrate != 38400 &&
      baudrate != 57600 && baudrate != 115200) {
    debug_println("ERROR: Invalid baudrate (2400, 4800, 9600, 19200, 38400, 57600, 115200)");
    return;
  }

  g_modbus_master_config.baudrate = baudrate;
  g_persist_config.modbus_master.baudrate = baudrate;

  if (g_modbus_master_config.enabled) {
    modbus_master_reconfigure();
  }

  uint16_t eff = modbus_effective_inter_frame(g_modbus_master_config.inter_frame_delay, baudrate);
  bool is_auto = (g_modbus_master_config.inter_frame_delay == 0);
  debug_printf("[OK] Modbus Master baudrate: %u, inter-frame: %u ms (%s)\n",
               baudrate, eff, is_auto ? "auto t3.5" : "manual");
  debug_println("NOTE: Use 'save' to persist to NVS");
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
    debug_println("ERROR: Invalid parity (none, even, odd)");
    return;
  }

  g_modbus_master_config.parity = parity_val;
  g_persist_config.modbus_master.parity = parity_val;
  if (g_modbus_master_config.enabled) {
    modbus_master_reconfigure();
  }
  debug_printf("[OK] Modbus Master parity: %s (takes effect on reboot)\n", parity);
  debug_println("NOTE: Use 'save' to persist to NVS");
}

void cli_cmd_set_modbus_master_stop_bits(uint8_t bits) {
  if (bits != 1 && bits != 2) {
    debug_println("ERROR: Invalid stop bits (1 or 2)");
    return;
  }

  g_modbus_master_config.stop_bits = bits;
  g_persist_config.modbus_master.stop_bits = bits;
  if (g_modbus_master_config.enabled) {
    modbus_master_reconfigure();
  }
  debug_printf("[OK] Modbus Master stop bits: %u (takes effect on reboot)\n", bits);
  debug_println("NOTE: Use 'save' to persist to NVS");
}

void cli_cmd_set_modbus_master_timeout(uint16_t ms) {
  if (ms < 100 || ms > 5000) {
    debug_println("ERROR: Invalid timeout (100-5000 ms)");
    return;
  }

  g_modbus_master_config.timeout_ms = ms;
  g_persist_config.modbus_master.timeout_ms = ms;
  debug_printf("[OK] Modbus Master timeout: %u ms (takes effect on reboot)\n", ms);
  debug_println("NOTE: Use 'save' to persist to NVS");
}

void cli_cmd_set_modbus_master_inter_frame_delay(uint16_t ms) {
  if (ms > 1000) {
    debug_println("ERROR: Invalid inter-frame delay (0=auto, 1-1000 ms manual)");
    return;
  }

  g_modbus_master_config.inter_frame_delay = ms;
  g_persist_config.modbus_master.inter_frame_delay = ms;

  if (ms == 0) {
    uint16_t t35 = modbus_calc_t35_ms(g_modbus_master_config.baudrate);
    debug_printf("[OK] Modbus Master inter-frame: AUTO (t3.5 = %u ms @ %u baud)\n",
                 t35, g_modbus_master_config.baudrate);
  } else {
    debug_printf("[OK] Modbus Master inter-frame: %u ms (manual)\n", ms);
  }
  debug_println("NOTE: Use 'save' to persist to NVS");
}

void cli_cmd_set_modbus_master_max_requests(uint8_t count) {
  if (count == 0 || count > 100) {
    debug_println("ERROR: Invalid max requests (1-100)");
    return;
  }

  g_modbus_master_config.max_requests_per_cycle = count;
  g_persist_config.modbus_master.max_requests_per_cycle = count;
  debug_printf("[OK] Modbus Master max requests/cycle: %u\n", count);
  debug_println("     Begræns MB_READ/MB_WRITE kald per ST program per cycle");
  debug_println("NOTE: Use 'save' to persist to NVS");
}

void cli_cmd_set_modbus_master_cache_ttl(uint16_t ttl_ms) {
  g_modbus_master_config.cache_ttl_ms = ttl_ms;
  g_persist_config.modbus_master.cache_ttl_ms = ttl_ms;
  if (ttl_ms == 0) {
    debug_println("[OK] Modbus Master cache TTL: 0 (never expire)");
  } else {
    debug_printf("[OK] Modbus Master cache TTL: %u ms\n", ttl_ms);
  }
  debug_println("NOTE: Use 'save' to persist to NVS");
}

void cli_cmd_set_modbus_master_cache_size(uint8_t size) {
  if (size < 1) size = 1;
  if (size > MB_CACHE_MAX_ENTRIES) size = MB_CACHE_MAX_ENTRIES;
  g_modbus_master_config.cache_max_entries = size;
  g_persist_config.modbus_master.cache_max_entries = size;
  debug_printf("[OK] Modbus Master cache max entries: %u (compile-max: %d)\n", size, MB_CACHE_MAX_ENTRIES);
  debug_println("NOTE: Use 'save' to persist. Eksisterende entries over grænsen evictes ved næste insert.");
}

void cli_cmd_set_modbus_master_queue_size(uint8_t size) {
  if (size < 4) size = 4;
  if (size > MB_ASYNC_QUEUE_SIZE) size = MB_ASYNC_QUEUE_SIZE;
  g_modbus_master_config.queue_max_size = size;
  g_persist_config.modbus_master.queue_max_size = size;
  debug_printf("[OK] Modbus Master queue max size: %u (compile-max: %d)\n", size, MB_ASYNC_QUEUE_SIZE);
  debug_println("NOTE: Use 'save' to persist.");
}

/* ============================================================================
 * SHOW COMMAND
 * ============================================================================ */

/* ============================================================================
 * REMOTE READ/WRITE COMMANDS (mb read / mb write)
 * ============================================================================ */

static const char* mb_error_str(mb_error_code_t err) {
  switch (err) {
    case MB_OK:              return "OK";
    case MB_TIMEOUT:         return "TIMEOUT (slave svarer ikke)";
    case MB_CRC_ERROR:       return "CRC ERROR (data korrupt)";
    case MB_EXCEPTION:       return "EXCEPTION (slave afviste)";
    case MB_NOT_ENABLED:     return "NOT ENABLED (set modbus-master enabled on)";
    case MB_INVALID_SLAVE:   return "INVALID SLAVE ID (1-247)";
    case MB_INVALID_ADDRESS: return "INVALID ADDRESS";
    default:                 return "UNKNOWN ERROR";
  }
}

// Valid baudrates for temporary override
static bool mb_is_valid_baudrate(uint32_t baud) {
  return (baud == 2400 || baud == 4800 || baud == 9600 || baud == 19200 ||
          baud == 38400 || baud == 57600 || baud == 115200);
}

// Detect if last argument is a baudrate (common standard values)
// Returns baudrate if found, 0 if not
static uint32_t mb_detect_baud_arg(uint8_t argc, char **argv, uint8_t min_args) {
  if (argc <= min_args) return 0;  // No extra arg beyond required
  uint32_t val = atol(argv[argc - 1]);
  return mb_is_valid_baudrate(val) ? val : 0;
}

// RAII-style temporary baudrate switcher
struct MbTempBaud {
  uint32_t saved_baud;
  bool switched;

  MbTempBaud(uint32_t temp_baud) : saved_baud(0), switched(false) {
    if (temp_baud > 0 && temp_baud != g_modbus_master_config.baudrate) {
      saved_baud = g_modbus_master_config.baudrate;
      g_modbus_master_config.baudrate = temp_baud;
      modbus_master_reconfigure();
      switched = true;
      debug_printf("  [BAUD] Midlertidig baudrate: %u (normal: %u)\n", temp_baud, saved_baud);
    }
  }

  ~MbTempBaud() {
    if (switched) {
      g_modbus_master_config.baudrate = saved_baud;
      modbus_master_reconfigure();
      debug_printf("  [BAUD] Gendannet baudrate: %u\n", saved_baud);
    }
  }
};

void cli_cmd_mb_read(uint8_t argc, char **argv) {
  // mb read <type> <slave_id> <address> [count] [baudrate]
  if (argc < 3) {
    debug_println("Brug: mb read <type> <slave_id> <address> [count] [baudrate]");
    debug_println("  type: coil (FC01), input (FC02), holding (FC03), input-reg (FC04)");
    debug_println("  slave_id: 1-247");
    debug_println("  address: 0-65535");
    debug_println("  count: 1-16 (kun for holding, default 1)");
    debug_println("  baudrate: 2400-115200 (valgfri, midlertidig override)");
    debug_println("Eksempel: mb read holding 90 0");
    debug_println("          mb read holding 100 254 4");
    debug_println("          mb read holding 90 0 19200");
    debug_println("          mb read holding 100 254 4 9600");
    return;
  }

  if (!g_modbus_master_config.enabled) {
    debug_println("FEJL: Modbus Master er ikke aktiveret (set modbus-master enabled on)");
    return;
  }

  const char *type = argv[0];
  uint8_t slave_id = atoi(argv[1]);
  uint16_t address = atoi(argv[2]);

  // Detect optional baudrate as last arg
  // For coil/input/input-reg: 3 required args (type, slave, addr) → baud at [3]
  // For holding: 3 required + optional count → need to figure out which is count vs baud
  uint32_t temp_baud = 0;
  uint8_t count = 1;

  if (strcasecmp(type, "holding") == 0 || strcasecmp(type, "h-reg") == 0 || strcasecmp(type, "hreg") == 0) {
    // holding: mb read holding <slave> <addr> [count] [baud]
    if (argc >= 5) {
      // Two extra args: argv[3]=count, argv[4]=baud
      uint32_t last_val = atol(argv[4]);
      if (mb_is_valid_baudrate(last_val)) {
        count = atoi(argv[3]);
        temp_baud = last_val;
      } else {
        count = atoi(argv[3]);
      }
    } else if (argc >= 4) {
      // One extra arg: could be count OR baud
      uint32_t val = atol(argv[3]);
      if (mb_is_valid_baudrate(val) && val > 16) {
        temp_baud = val;  // It's a baudrate (all valid bauds are > 16)
      } else {
        count = atoi(argv[3]);
      }
    }
  } else {
    // coil/input/input-reg: mb read coil <slave> <addr> [baud]
    temp_baud = mb_detect_baud_arg(argc, argv, 3);
  }

  if (slave_id < 1 || slave_id > 247) {
    debug_println("FEJL: slave_id skal vaere 1-247");
    return;
  }

  MbTempBaud baud_guard(temp_baud);
  debug_printf("[MB READ] slave=%d addr=%d type=%s count=%d ...\n", slave_id, address, type, count);

  if (strcasecmp(type, "coil") == 0) {
    bool val = false;
    mb_error_code_t err = modbus_master_read_coil(slave_id, address, &val);
    g_modbus_master_config.total_requests++;
    if (err == MB_OK) {
      debug_printf("  Coil[%d] @ slave %d = %s\n", address, slave_id, val ? "ON (1)" : "OFF (0)");
    } else {
      debug_printf("  FEJL: %s\n", mb_error_str(err));
    }
  } else if (strcasecmp(type, "input") == 0) {
    bool val = false;
    mb_error_code_t err = modbus_master_read_input(slave_id, address, &val);
    g_modbus_master_config.total_requests++;
    if (err == MB_OK) {
      debug_printf("  Input[%d] @ slave %d = %s\n", address, slave_id, val ? "ON (1)" : "OFF (0)");
    } else {
      debug_printf("  FEJL: %s\n", mb_error_str(err));
    }
  } else if (strcasecmp(type, "holding") == 0 || strcasecmp(type, "h-reg") == 0 || strcasecmp(type, "hreg") == 0) {
    if (count > 16) { debug_println("FEJL: max 16 registre"); return; }
    if (count == 1) {
      uint16_t val = 0;
      mb_error_code_t err = modbus_master_read_holding(slave_id, address, &val);
      g_modbus_master_config.total_requests++;
      if (err == MB_OK) {
        debug_printf("  Holding[%d] @ slave %d = %u (0x%04X) (signed: %d)\n",
                     address, slave_id, val, val, (int16_t)val);
      } else {
        debug_printf("  FEJL: %s\n", mb_error_str(err));
      }
    } else {
      uint16_t vals[16];
      mb_error_code_t err = modbus_master_read_holdings(slave_id, address, count, vals);
      g_modbus_master_config.total_requests++;
      if (err == MB_OK) {
        debug_printf("  Holding[%d..%d] @ slave %d:\n", address, address + count - 1, slave_id);
        for (uint8_t i = 0; i < count; i++) {
          debug_printf("    [%d] = %u (0x%04X) (signed: %d)\n",
                       address + i, vals[i], vals[i], (int16_t)vals[i]);
        }
      } else {
        debug_printf("  FEJL: %s\n", mb_error_str(err));
      }
    }
  } else if (strcasecmp(type, "input-reg") == 0 || strcasecmp(type, "i-reg") == 0 || strcasecmp(type, "ireg") == 0) {
    uint16_t val = 0;
    mb_error_code_t err = modbus_master_read_input_register(slave_id, address, &val);
    g_modbus_master_config.total_requests++;
    if (err == MB_OK) {
      debug_printf("  InputReg[%d] @ slave %d = %u (0x%04X) (signed: %d)\n",
                   address, slave_id, val, val, (int16_t)val);
    } else {
      debug_printf("  FEJL: %s\n", mb_error_str(err));
    }
  } else {
    debug_printf("FEJL: Ukendt type '%s' (brug: coil, input, holding, input-reg)\n", type);
  }
}

void cli_cmd_mb_write(uint8_t argc, char **argv) {
  // mb write <type> <slave_id> <address> <value> [baudrate]
  if (argc < 4) {
    debug_println("Brug: mb write <type> <slave_id> <address> <value> [baudrate]");
    debug_println("  type: coil (FC05), holding (FC06)");
    debug_println("  coil value: 0/1/on/off");
    debug_println("  holding value: 0-65535");
    debug_println("  baudrate: 2400-115200 (valgfri, midlertidig override)");
    debug_println("Eksempel: mb write coil 90 0 on");
    debug_println("          mb write holding 100 254 1234");
    debug_println("          mb write holding 100 254 1234 19200");
    return;
  }

  if (!g_modbus_master_config.enabled) {
    debug_println("FEJL: Modbus Master er ikke aktiveret (set modbus-master enabled on)");
    return;
  }

  const char *type = argv[0];
  uint8_t slave_id = atoi(argv[1]);
  uint16_t address = atoi(argv[2]);
  const char *value_str = argv[3];

  // Detect optional baudrate as last arg (argv[4])
  uint32_t temp_baud = mb_detect_baud_arg(argc, argv, 4);

  if (slave_id < 1 || slave_id > 247) {
    debug_println("FEJL: slave_id skal vaere 1-247");
    return;
  }

  MbTempBaud baud_guard(temp_baud);

  if (strcasecmp(type, "coil") == 0) {
    bool val = (strcasecmp(value_str, "on") == 0 || strcasecmp(value_str, "1") == 0 ||
                strcasecmp(value_str, "true") == 0);
    debug_printf("[MB WRITE] coil slave=%d addr=%d val=%s ...\n", slave_id, address, val ? "ON" : "OFF");
    mb_error_code_t err = modbus_master_write_coil(slave_id, address, val);
    g_modbus_master_config.total_requests++;
    if (err == MB_OK) {
      debug_printf("  OK: Coil[%d] @ slave %d = %s\n", address, slave_id, val ? "ON" : "OFF");
    } else {
      debug_printf("  FEJL: %s\n", mb_error_str(err));
    }
  } else if (strcasecmp(type, "holding") == 0 || strcasecmp(type, "h-reg") == 0 || strcasecmp(type, "hreg") == 0) {
    uint16_t val = (uint16_t)atol(value_str);
    debug_printf("[MB WRITE] holding slave=%d addr=%d val=%u ...\n", slave_id, address, val);
    mb_error_code_t err = modbus_master_write_holding(slave_id, address, val);
    g_modbus_master_config.total_requests++;
    if (err == MB_OK) {
      debug_printf("  OK: Holding[%d] @ slave %d = %u (0x%04X)\n", address, slave_id, val, val);
    } else {
      debug_printf("  FEJL: %s\n", mb_error_str(err));
    }
  } else {
    debug_printf("FEJL: Ukendt type '%s' (brug: coil, holding)\n", type);
  }
}

void cli_cmd_mb_reset_backoff(uint8_t argc, char **argv) {
  if (argc >= 1) {
    uint8_t slave_id = atoi(argv[0]);
    if (slave_id > 0) {
      // Reset specific slave
      for (uint8_t i = 0; i < MB_SLAVE_BACKOFF_MAX; i++) {
        if (g_mb_async.slave_backoff[i].slave_id == slave_id) {
          g_mb_async.slave_backoff[i].backoff_ms = 0;
          g_mb_async.slave_backoff[i].timeout_count = 0;
          g_mb_async.slave_backoff[i].success_count = 0;
          debug_printf("[OK] Backoff nulstillet for slave %d\n", slave_id);
          return;
        }
      }
      debug_printf("Slave %d har ingen backoff entry\n", slave_id);
      return;
    }
  }
  // Reset all
  memset(g_mb_async.slave_backoff, 0, sizeof(g_mb_async.slave_backoff));
  debug_println("[OK] Backoff nulstillet for alle slaves");
}

void cli_cmd_mb_scan(uint8_t start_id, uint8_t end_id, uint32_t temp_baud) {
  if (start_id < 1) start_id = 1;
  if (end_id > 247) end_id = 247;
  if (start_id > end_id) {
    debug_println("FEJL: start_id skal vaere <= end_id");
    return;
  }

  if (!g_modbus_master_config.enabled) {
    debug_println("FEJL: Modbus Master er ikke aktiveret");
    return;
  }

  MbTempBaud baud_guard(temp_baud);
  debug_printf("[MB SCAN] Scanning slave %d-%d (FC03, addr 0) ...\n", start_id, end_id);
  uint8_t found = 0;

  for (uint8_t id = start_id; id <= end_id; id++) {
    uint16_t val = 0;
    mb_error_code_t err = modbus_master_read_holding(id, 0, &val);
    g_modbus_master_config.total_requests++;
    if (err == MB_OK) {
      debug_printf("  Slave %3d: FUNDET (holding[0] = %u)\n", id, val);
      found++;
    } else if (err == MB_EXCEPTION) {
      debug_printf("  Slave %3d: EXCEPTION (svarer men afviser FC03 addr 0)\n", id);
      found++;
    }
    // Timeout = ingen slave — vis ikke
    delay(10); // Kort pause mellem scans
  }

  debug_printf("[MB SCAN] Faerdigt: %d slave(s) fundet af %d testet\n", found, end_id - start_id + 1);
}

/* ============================================================================
 * SHOW COMMAND
 * ============================================================================ */

void cli_cmd_show_modbus_master() {
  debug_printf("\n=== MODBUS MASTER CONFIGURATION ===\n");
  debug_printf("Status: %s\n", g_modbus_master_config.enabled ? "ENABLED" : "DISABLED");
  debug_printf("Hardware: UART1 (TX:GPIO%d, RX:GPIO%d, DE:GPIO%d)\n",
                MODBUS_MASTER_TX_PIN, MODBUS_MASTER_RX_PIN, MODBUS_MASTER_DE_PIN);
  debug_printf("\n");

  debug_printf("Communication:\n");
  debug_printf("  Baudrate: %u\n", g_modbus_master_config.baudrate);

  const char *parity_str = "None";
  if (g_modbus_master_config.parity == 1) parity_str = "Even";
  else if (g_modbus_master_config.parity == 2) parity_str = "Odd";
  debug_printf("  Parity: %s\n", parity_str);
  debug_printf("  Stop bits: %u\n", g_modbus_master_config.stop_bits);
  debug_printf("\n");

  debug_printf("Timing:\n");
  debug_printf("  Timeout: %u ms\n", g_modbus_master_config.timeout_ms);
  if (g_modbus_master_config.inter_frame_delay == 0) {
    uint16_t t35 = modbus_calc_t35_ms(g_modbus_master_config.baudrate);
    debug_printf("  Inter-frame delay: AUTO (t3.5 = %u ms @ %u baud)\n", t35, g_modbus_master_config.baudrate);
  } else {
    debug_printf("  Inter-frame delay: %u ms (manual)\n", g_modbus_master_config.inter_frame_delay);
  }
  debug_printf("  Max requests/cycle: %u (MB_READ/MB_WRITE kald per ST program per cycle)\n", g_modbus_master_config.max_requests_per_cycle);
  if (g_modbus_master_config.cache_ttl_ms == 0) {
    debug_printf("  Cache TTL: 0 (never expire)\n");
  } else {
    debug_printf("  Cache TTL: %u ms\n", g_modbus_master_config.cache_ttl_ms);
  }
  debug_printf("  Cache size: %u / %d (max)\n",
               g_modbus_master_config.cache_max_entries, MB_CACHE_MAX_ENTRIES);
  debug_printf("  Queue size: %u / %d (max)\n",
               g_modbus_master_config.queue_max_size, MB_ASYNC_QUEUE_SIZE);
  debug_printf("\n");

  debug_printf("Statistics:\n");
  debug_printf("  Total requests: %u\n", g_modbus_master_config.total_requests);
  debug_printf("  Successful: %u (%.1f%%)\n",
                g_modbus_master_config.successful_requests,
                g_modbus_master_config.total_requests > 0
                  ? (100.0 * g_modbus_master_config.successful_requests / g_modbus_master_config.total_requests)
                  : 0.0);
  debug_printf("  Timeouts: %u (%.1f%%)\n",
                g_modbus_master_config.timeout_errors,
                g_modbus_master_config.total_requests > 0
                  ? (100.0 * g_modbus_master_config.timeout_errors / g_modbus_master_config.total_requests)
                  : 0.0);
  debug_printf("  CRC errors: %u\n", g_modbus_master_config.crc_errors);
  debug_printf("  Exceptions: %u\n", g_modbus_master_config.exception_errors);
  debug_printf("\n");

  // Async cache statistics (v7.7.0)
  const mb_async_state_t *async_state = mb_async_get_state();
  debug_printf("Async Cache (v7.7.0):\n");
  debug_printf("  Task: %s\n", async_state->task_running ? "RUNNING" : "STOPPED");
  debug_printf("  Cache entries: %u / %d\n", async_state->entry_count, MB_CACHE_MAX_ENTRIES);
  debug_printf("  Queue pending: %u / %d (hwm: %u)\n",
               async_state->pq_count, MB_ASYNC_QUEUE_SIZE,
               async_state->queue_high_watermark);
  debug_printf("  Cache hits: %u\n", async_state->cache_hits);
  debug_printf("  Cache misses: %u\n", async_state->cache_misses);
  debug_printf("  Queue full drops: %u\n", async_state->queue_full_count);
  debug_printf("  Priority drops: %u\n", async_state->priority_drops);
  debug_printf("  Async requests: %u\n", async_state->total_requests);
  debug_printf("  Async errors: %u\n", async_state->total_errors);
  debug_printf("  Async timeouts: %u\n", async_state->total_timeouts);
  debug_printf("\n");

  // Adaptive backoff per slave (v7.9.3)
  bool has_backoff = false;
  for (uint8_t i = 0; i < MB_SLAVE_BACKOFF_MAX; i++) {
    if (async_state->slave_backoff[i].slave_id != 0) {
      has_backoff = true;
      break;
    }
  }
  if (has_backoff) {
    debug_printf("Adaptive Backoff:\n");
    debug_printf("  %-6s %-10s %-9s %s\n", "Slave", "Backoff", "Timeouts", "Successes");
    for (uint8_t i = 0; i < MB_SLAVE_BACKOFF_MAX; i++) {
      if (async_state->slave_backoff[i].slave_id == 0) continue;
      debug_printf("  %-6d %-10s %-9u %u\n",
                   async_state->slave_backoff[i].slave_id,
                   (String(async_state->slave_backoff[i].backoff_ms) + "ms").c_str(),
                   async_state->slave_backoff[i].timeout_count,
                   async_state->slave_backoff[i].success_count);
    }
    debug_printf("\n");
  }

  if (async_state->entry_count > 0) {
    debug_printf("Cache Entries:\n");
    debug_printf("  %-4s %-5s %-7s %-5s %-7s %-6s %s\n",
                 "Slot", "Slave", "Addr", "FC", "Value", "Status", "Age");
    for (uint8_t i = 0; i < async_state->entry_count; i++) {
      const mb_cache_entry_t *e = &async_state->entries[i];
      const char *status_str = "EMPTY";
      if (e->status == MB_CACHE_PENDING) status_str = "PEND";
      else if (e->status == MB_CACHE_VALID) status_str = "VALID";
      else if (e->status == MB_CACHE_ERROR) status_str = "ERROR";

      const char *fc_str = "?";
      uint8_t disp_fc = (e->last_fc > 0) ? e->last_fc : e->key.req_type;
      if (disp_fc == MB_REQ_READ_COIL) fc_str = "FC01";
      else if (disp_fc == MB_REQ_READ_INPUT) fc_str = "FC02";
      else if (disp_fc == MB_REQ_READ_HOLDING) fc_str = "FC03";
      else if (disp_fc == MB_REQ_READ_INPUT_REG) fc_str = "FC04";
      else if (disp_fc == MB_REQ_WRITE_COIL) fc_str = "FC05";
      else if (disp_fc == MB_REQ_WRITE_HOLDING) fc_str = "FC06";

      uint32_t age_ms = (e->last_update_ms > 0) ? (millis() - e->last_update_ms) : 0;
      char age_buf[16];
      if (age_ms < 1000) {
        snprintf(age_buf, sizeof(age_buf), "%ums", (unsigned)age_ms);
      } else if (age_ms < 60000) {
        snprintf(age_buf, sizeof(age_buf), "%.1fs", age_ms / 1000.0f);
      } else {
        snprintf(age_buf, sizeof(age_buf), "%.1fm", age_ms / 60000.0f);
      }

      // Mark expired entries
      bool expired = (g_modbus_master_config.cache_ttl_ms > 0 &&
                      e->last_update_ms > 0 &&
                      age_ms >= g_modbus_master_config.cache_ttl_ms);
      debug_printf("  %-4d %-5d %-7d %-5s %-7d %-6s %s%s\n",
                   i, e->key.slave_id, e->key.address, fc_str,
                   e->value.int_val, status_str, age_buf,
                   expired ? " [EXP]" : "");
    }
    debug_printf("\n");
  }

  debug_printf("Configuration commands:\n");
  debug_printf("  set modbus-master enabled <on|off>\n");
  debug_printf("  set modbus-master baudrate <rate>\n");
  debug_printf("  set modbus-master parity <none|even|odd>\n");
  debug_printf("  set modbus-master stop-bits <1|2>\n");
  debug_printf("  set modbus-master timeout <ms>\n");
  debug_printf("  set modbus-master inter-frame-delay <ms>\n");
  debug_printf("  set modbus-master max-requests <count>\n");
  debug_printf("  set modbus-master cache-ttl <ms>   (0=never expire)\n");
  debug_printf("  set modbus-master cache-size <1-32> (default: 32)\n");
  debug_printf("  set modbus-master queue-size <4-32> (default: 16)\n");
  debug_printf("  Brug 'set modbus-master ?' for detaljeret hjælp\n");
  debug_printf("\n");
}
