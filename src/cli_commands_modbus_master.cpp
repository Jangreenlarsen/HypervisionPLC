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
  if (baudrate != 9600 && baudrate != 19200 && baudrate != 38400 &&
      baudrate != 57600 && baudrate != 115200) {
    debug_println("ERROR: Invalid baudrate (9600, 19200, 38400, 57600, 115200)");
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
  debug_println("     Begræns MB_READ/MB_WRITE kald per ST execution cycle (alle 4 programmer deler kvoten)");
  debug_println("NOTE: Use 'save' to persist to NVS");
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
  debug_printf("  Max requests/cycle: %u (MB_READ/MB_WRITE kald per ST cycle, alle programmer)\n", g_modbus_master_config.max_requests_per_cycle);
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
  debug_printf("  Queue pending: %u / %d\n",
               async_state->request_queue ? (unsigned)uxQueueMessagesWaiting(async_state->request_queue) : 0,
               MB_ASYNC_QUEUE_SIZE);
  debug_printf("  Cache hits: %u\n", async_state->cache_hits);
  debug_printf("  Cache misses: %u\n", async_state->cache_misses);
  debug_printf("  Queue full: %u\n", async_state->queue_full_count);
  debug_printf("  Async requests: %u\n", async_state->total_requests);
  debug_printf("  Async errors: %u\n", async_state->total_errors);
  debug_printf("  Async timeouts: %u\n", async_state->total_timeouts);
  debug_printf("\n");

  if (async_state->entry_count > 0) {
    debug_printf("Cache Entries:\n");
    debug_printf("  %-4s %-5s %-7s %-5s %-7s %-6s %s\n",
                 "Slot", "Slave", "Addr", "FC", "Value", "Status", "Age(ms)");
    for (uint8_t i = 0; i < async_state->entry_count; i++) {
      const mb_cache_entry_t *e = &async_state->entries[i];
      const char *status_str = "EMPTY";
      if (e->status == MB_CACHE_PENDING) status_str = "PEND";
      else if (e->status == MB_CACHE_VALID) status_str = "VALID";
      else if (e->status == MB_CACHE_ERROR) status_str = "ERROR";

      const char *fc_str = "?";
      if (e->key.req_type == MB_REQ_READ_COIL) fc_str = "FC01";
      else if (e->key.req_type == MB_REQ_READ_INPUT) fc_str = "FC02";
      else if (e->key.req_type == MB_REQ_READ_HOLDING) fc_str = "FC03";
      else if (e->key.req_type == MB_REQ_READ_INPUT_REG) fc_str = "FC04";

      uint32_t age = (e->last_update_ms > 0) ? (millis() - e->last_update_ms) : 0;

      debug_printf("  %-4d %-5d %-7d %-5s %-7d %-6s %u\n",
                   i, e->key.slave_id, e->key.address, fc_str,
                   e->value.int_val, status_str, age);
    }
    debug_printf("\n");
  }
}
