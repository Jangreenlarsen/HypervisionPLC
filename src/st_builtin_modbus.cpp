/**
 * @file st_builtin_modbus.cpp
 * @brief ST Logic Modbus Master Wrapper Functions (Async v8.0)
 *
 * All Modbus operations are now NON-BLOCKING.
 * Reads return cached values and queue background refresh.
 * Writes are queued for background execution.
 *
 * v7.7.0: Async rewrite — zero blocking, zero overruns.
 */

#include "st_builtin_modbus.h"
#include "modbus_master.h"
#include "mb_async.h"
#include "mb_activity_log.h"

/* ============================================================================
 * GLOBAL STATUS VARIABLES
 * ============================================================================ */

int32_t g_mb_last_error = MB_OK;
bool g_mb_success = false;
uint8_t g_mb_request_count = 0;
bool g_mb_cache_enabled = true;  // Default: cache dedup active

// Multi-register buffer for MB_SET_REG/MB_GET_REG/MB_READ_HOLDINGS/MB_WRITE_HOLDINGS
uint16_t g_mb_multi_reg_buf[MB_MULTI_REG_MAX] = {0};

/* ============================================================================
 * HELPER FUNCTION
 * ============================================================================ */

static bool check_request_limit() {
  if (g_mb_request_count >= g_modbus_master_config.max_requests_per_cycle) {
    g_mb_last_error = MB_MAX_REQUESTS_EXCEEDED;
    g_mb_success = false;
    return false;
  }
  g_mb_request_count++;
  return true;
}

/* ============================================================================
 * VALIDATION HELPER
 * ============================================================================ */

static bool validate_slave_addr(int32_t slave_id, int32_t address) {
  // BUGFIX: this used to only check whether the async subsystem was ever
  // initialized (pq_mutex != NULL), NOT whether Modbus Master is currently
  // enabled (g_modbus_master_config.enabled). On boards where mb_async_init()
  // starts based on UART role (MODBUS_SINGLE_TRANSCEIVER: mb_mode==MASTER)
  // rather than the enabled flag, the mutex exists regardless of the user
  // toggling "modbus-master enabled off". That let ST Logic keep queuing
  // MB_* requests into the async task even while the dashboard shows Master
  // as disabled — the queue dequeued them, counted them in total_requests/
  // total_errors, and only failed once modbus_master_send_request() hit its
  // own (correct) enabled check deep inside — i.e. real-looking "activity"
  // and error stats for a subsystem the user believes is off.
  if (!g_modbus_master_config.enabled || !mb_async_get_state()->pq_mutex) {
    g_mb_last_error = MB_NOT_ENABLED;
    g_mb_success = false;
    return false;
  }

  // FEAT-149: any request that gets queued from here on originated from an
  // ST Logic program — snapshotted into the queued request so the activity
  // log can attribute it correctly once actually sent (possibly later).
  g_mb_activity_next_source = MB_SRC_ST_LOGIC;
  // BUG-084: Validate slave ID (Modbus valid range: 1-247)
  if (slave_id < 1 || slave_id > 247) {
    g_mb_last_error = MB_INVALID_SLAVE;
    g_mb_success = false;
    return false;
  }
  // BUG-085: Validate address (Modbus valid range: 0-65535)
  if (address < 0 || address > 65535) {
    g_mb_last_error = MB_INVALID_ADDRESS;
    g_mb_success = false;
    return false;
  }
  return true;
}

/* ============================================================================
 * CACHE TTL HELPER
 * ============================================================================ */

static bool cache_entry_expired(const mb_cache_entry_t *entry) {
  uint16_t ttl = g_modbus_master_config.cache_ttl_ms;
  if (ttl == 0) return false;  // 0 = never expire
  if (entry->last_update_ms == 0) return true;  // Never updated
  return (millis() - entry->last_update_ms) >= ttl;
}

/* ============================================================================
 * ASYNC READ BUILTINS — return cached value, queue refresh
 * ============================================================================ */

st_value_t st_builtin_mb_read_coil(st_value_t slave_id, st_value_t address) {
  st_value_t result;
  result.bool_val = false;

  if (!check_request_limit()) return result;
  if (!validate_slave_addr(slave_id.int_val, address.int_val)) return result;

  // Cache lookup
  mb_cache_entry_t *entry = mb_cache_get_or_create(
    (uint8_t)slave_id.int_val, (uint16_t)address.int_val, (uint8_t)MB_REQ_READ_COIL);

  if (!entry) {
    g_mb_last_error = MB_MAX_REQUESTS_EXCEEDED;
    g_mb_success = false;
    return result;
  }

  // Read cached value (thread-safe)
  portENTER_CRITICAL(&mb_cache_spinlock);
  result = entry->value;
  mb_cache_status_t status = entry->status;
  g_mb_last_error = entry->last_error;
  bool expired = cache_entry_expired(entry);
  portEXIT_CRITICAL(&mb_cache_spinlock);

  // Queue background refresh: always if cache disabled/expired, otherwise only if not pending
  if (!g_mb_cache_enabled || expired || status != MB_CACHE_PENDING) {
    mb_async_queue_read(MB_REQ_READ_COIL,
                        (uint8_t)slave_id.int_val,
                        (uint16_t)address.int_val);
  }

  g_mb_success = (status == MB_CACHE_VALID && !expired);
  return result;  // Non-blocking!
}

st_value_t st_builtin_mb_read_input(st_value_t slave_id, st_value_t address) {
  st_value_t result;
  result.bool_val = false;

  if (!check_request_limit()) return result;
  if (!validate_slave_addr(slave_id.int_val, address.int_val)) return result;

  mb_cache_entry_t *entry = mb_cache_get_or_create(
    (uint8_t)slave_id.int_val, (uint16_t)address.int_val, (uint8_t)MB_REQ_READ_INPUT);

  if (!entry) {
    g_mb_last_error = MB_MAX_REQUESTS_EXCEEDED;
    g_mb_success = false;
    return result;
  }

  portENTER_CRITICAL(&mb_cache_spinlock);
  result = entry->value;
  mb_cache_status_t status = entry->status;
  g_mb_last_error = entry->last_error;
  bool expired = cache_entry_expired(entry);
  portEXIT_CRITICAL(&mb_cache_spinlock);

  if (!g_mb_cache_enabled || expired || status != MB_CACHE_PENDING) {
    mb_async_queue_read(MB_REQ_READ_INPUT,
                        (uint8_t)slave_id.int_val,
                        (uint16_t)address.int_val);
  }

  g_mb_success = (status == MB_CACHE_VALID && !expired);
  return result;
}

st_value_t st_builtin_mb_read_holding(st_value_t slave_id, st_value_t address) {
  st_value_t result;
  result.int_val = 0;

  if (!check_request_limit()) return result;
  if (!validate_slave_addr(slave_id.int_val, address.int_val)) return result;

  mb_cache_entry_t *entry = mb_cache_get_or_create(
    (uint8_t)slave_id.int_val, (uint16_t)address.int_val, (uint8_t)MB_REQ_READ_HOLDING);

  if (!entry) {
    g_mb_last_error = MB_MAX_REQUESTS_EXCEEDED;
    g_mb_success = false;
    return result;
  }

  portENTER_CRITICAL(&mb_cache_spinlock);
  result = entry->value;
  mb_cache_status_t status = entry->status;
  g_mb_last_error = entry->last_error;
  bool expired = cache_entry_expired(entry);
  portEXIT_CRITICAL(&mb_cache_spinlock);

  if (!g_mb_cache_enabled || expired || status != MB_CACHE_PENDING) {
    mb_async_queue_read(MB_REQ_READ_HOLDING,
                        (uint8_t)slave_id.int_val,
                        (uint16_t)address.int_val);
  }

  g_mb_success = (status == MB_CACHE_VALID && !expired);
  return result;
}

st_value_t st_builtin_mb_read_input_reg(st_value_t slave_id, st_value_t address) {
  st_value_t result;
  result.int_val = 0;

  if (!check_request_limit()) return result;
  if (!validate_slave_addr(slave_id.int_val, address.int_val)) return result;

  mb_cache_entry_t *entry = mb_cache_get_or_create(
    (uint8_t)slave_id.int_val, (uint16_t)address.int_val, (uint8_t)MB_REQ_READ_INPUT_REG);

  if (!entry) {
    g_mb_last_error = MB_MAX_REQUESTS_EXCEEDED;
    g_mb_success = false;
    return result;
  }

  portENTER_CRITICAL(&mb_cache_spinlock);
  result = entry->value;
  mb_cache_status_t status = entry->status;
  g_mb_last_error = entry->last_error;
  bool expired = cache_entry_expired(entry);
  portEXIT_CRITICAL(&mb_cache_spinlock);

  if (!g_mb_cache_enabled || expired || status != MB_CACHE_PENDING) {
    mb_async_queue_read(MB_REQ_READ_INPUT_REG,
                        (uint8_t)slave_id.int_val,
                        (uint16_t)address.int_val);
  }

  g_mb_success = (status == MB_CACHE_VALID && !expired);
  return result;
}

/* ============================================================================
 * ASYNC WRITE BUILTINS — queue write, return immediately
 * ============================================================================ */

st_value_t st_builtin_mb_write_coil(st_value_t slave_id, st_value_t address, st_value_t value) {
  st_value_t result;
  result.bool_val = false;

  if (!check_request_limit()) return result;
  if (!validate_slave_addr(slave_id.int_val, address.int_val)) return result;

  // Queue write in background
  bool queued = mb_async_queue_write(MB_REQ_WRITE_COIL,
    (uint8_t)slave_id.int_val, (uint16_t)address.int_val, value);

  // Optimistic cache update
  if (queued) {
    mb_cache_entry_t *entry = mb_cache_get_or_create(
      (uint8_t)slave_id.int_val, (uint16_t)address.int_val, (uint8_t)MB_REQ_READ_COIL);
    if (entry) {
      portENTER_CRITICAL(&mb_cache_spinlock);
      entry->value = value;
      entry->status = MB_CACHE_PENDING;
      entry->pending_since_ms = millis();  // BUG-333
      portEXIT_CRITICAL(&mb_cache_spinlock);
    }
  }

  g_mb_success = queued;
  g_mb_last_error = queued ? MB_OK : MB_MAX_REQUESTS_EXCEEDED;
  result.bool_val = queued;
  return result;
}

st_value_t st_builtin_mb_write_holding(st_value_t slave_id, st_value_t address, st_value_t value) {
  st_value_t result;
  result.bool_val = false;

  if (!check_request_limit()) return result;
  if (!validate_slave_addr(slave_id.int_val, address.int_val)) return result;

  // Queue write in background
  bool queued = mb_async_queue_write(MB_REQ_WRITE_HOLDING,
    (uint8_t)slave_id.int_val, (uint16_t)address.int_val, value);

  // Optimistic cache update
  if (queued) {
    mb_cache_entry_t *entry = mb_cache_get_or_create(
      (uint8_t)slave_id.int_val, (uint16_t)address.int_val, (uint8_t)MB_REQ_READ_HOLDING);
    if (entry) {
      portENTER_CRITICAL(&mb_cache_spinlock);
      entry->value = value;
      entry->status = MB_CACHE_PENDING;
      entry->pending_since_ms = millis();  // BUG-333
      portEXIT_CRITICAL(&mb_cache_spinlock);
    }
  }

  g_mb_success = queued;
  g_mb_last_error = queued ? MB_OK : MB_MAX_REQUESTS_EXCEEDED;
  result.bool_val = queued;
  return result;
}

/* ============================================================================
 * MULTI-REGISTER BUILTINS (v7.9.2)
 * ============================================================================ */

st_value_t st_builtin_mb_read_holdings(st_value_t slave_id, st_value_t address, st_value_t count) {
  st_value_t result;
  result.bool_val = false;

  if (!check_request_limit()) return result;
  if (!validate_slave_addr(slave_id.int_val, address.int_val)) return result;

  int32_t cnt = count.int_val;
  if (cnt < 1 || cnt > 16) {
    g_mb_last_error = MB_INVALID_ADDRESS;
    g_mb_success = false;
    return result;
  }

  // Validate end address doesn't exceed Modbus range
  if ((int32_t)address.int_val + cnt - 1 > 65535) {
    g_mb_last_error = MB_INVALID_ADDRESS;
    g_mb_success = false;
    return result;
  }

  bool queued = mb_async_queue_read_multi(
    (uint8_t)slave_id.int_val, (uint16_t)address.int_val, (uint8_t)cnt);

  g_mb_success = queued;
  g_mb_last_error = queued ? MB_OK : MB_MAX_REQUESTS_EXCEEDED;
  result.bool_val = queued;
  return result;
}

st_value_t st_builtin_mb_write_holdings(st_value_t slave_id, st_value_t address, st_value_t count) {
  st_value_t result;
  result.bool_val = false;

  if (!check_request_limit()) return result;
  if (!validate_slave_addr(slave_id.int_val, address.int_val)) return result;

  int32_t cnt = count.int_val;
  if (cnt < 1 || cnt > 16) {
    g_mb_last_error = MB_INVALID_ADDRESS;
    g_mb_success = false;
    return result;
  }

  if ((int32_t)address.int_val + cnt - 1 > 65535) {
    g_mb_last_error = MB_INVALID_ADDRESS;
    g_mb_success = false;
    return result;
  }

  bool queued = mb_async_queue_write_multi(
    (uint8_t)slave_id.int_val, (uint16_t)address.int_val, (uint8_t)cnt, g_mb_multi_reg_buf);

  g_mb_success = queued;
  g_mb_last_error = queued ? MB_OK : MB_MAX_REQUESTS_EXCEEDED;
  result.bool_val = queued;
  return result;
}

/* ============================================================================
 * STATUS BUILTINS (0-arg)
 * ============================================================================ */

st_value_t st_builtin_mb_success_func() {
  st_value_t r;
  r.bool_val = g_mb_success;
  return r;
}

st_value_t st_builtin_mb_busy_func() {
  st_value_t r;
  r.bool_val = mb_async_is_busy();
  return r;
}

st_value_t st_builtin_mb_error_func() {
  st_value_t r;
  r.int_val = (int16_t)g_mb_last_error;
  return r;
}

st_value_t st_builtin_mb_cache_func(st_value_t enabled) {
  st_value_t r;
  r.bool_val = g_mb_cache_enabled;  // Return previous state
  g_mb_cache_enabled = enabled.bool_val;
  return r;
}
