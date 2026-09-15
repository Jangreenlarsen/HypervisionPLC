/**
 * @file st_builtin_modbus_expansion.cpp
 * @brief FEAT-410: ST Logic Modbus Expansion Board Wrapper Functions.
 *
 * Samme non-blocking design som st_builtin_modbus.cpp: læs returnerer en
 * cachet værdi øjeblikkeligt og queuer en baggrunds-opdatering; skriv queues
 * for baggrunds-udførelse. Se modbus_expansion_async.h's designnote — den
 * ufravigelige regel om ALDRIG at blokere synkront (EXPANSION_BOARD_DESIGN.md
 * §5.1.1) er netop derfor overholdt her: ingen kald i denne fil venter på
 * noget netværkssvar, de læser/skriver udelukkende cachen.
 */

#include "st_builtin_modbus_expansion.h"
#include "modbus_expansion_async.h"
#include "expansion_api_client.h"
#include "config_struct.h"

int32_t g_mbx_last_error = MB_OK;
bool    g_mbx_success = false;
uint8_t g_mbx_request_count = 0;
bool    g_mbx_cache_enabled = true;

// v7.9.68.0: multi-register/coil scratch buffers for MBX_WRITE_HOLDINGS/MBX_WRITE_COILS
uint16_t g_mbx_multi_reg_buf[MBX_MULTI_MAX] = {0};
bool     g_mbx_multi_coil_buf[MBX_MULTI_MAX] = {false};

#define MBX_MAX_REQUESTS_PER_CYCLE 20

static bool mbx_check_request_limit() {
  if (g_mbx_request_count >= MBX_MAX_REQUESTS_PER_CYCLE) {
    g_mbx_last_error = MB_MAX_REQUESTS_EXCEEDED;
    g_mbx_success = false;
    return false;
  }
  g_mbx_request_count++;
  return true;
}

// Validerer board (1-8, skal være konfigureret), kanal (1-8), slave (1-247)
// og adresse (0-65535) — samme disciplin som st_builtin_modbus.cpp's
// validate_slave_addr() (BUG-084/085), udvidet med board/kanal-tjek (uden
// hvilket et forkert board-nr ville forsøge en cache/kø-operation mod et
// ikke-eksisterende slot — ikke en direkte hukommelsesfejl her, da al
// board/kanal-brug er ren nøgle-sammenligning, men stadig meningsløst at lade
// gennem uden en klar fejlkode).
static bool mbx_validate(int32_t board, int32_t channel, int32_t slave_id, int32_t address) {
  if (board < 1 || board > EXPANSION_BOARD_MAX || !g_persist_config.expansion_boards[board - 1].configured) {
    g_mbx_last_error = MB_INVALID_ADDRESS;
    g_mbx_success = false;
    return false;
  }
  if (channel < 1 || channel > 8) {
    g_mbx_last_error = MB_INVALID_ADDRESS;
    g_mbx_success = false;
    return false;
  }
  if (slave_id < 1 || slave_id > 247) {
    g_mbx_last_error = MB_INVALID_SLAVE;
    g_mbx_success = false;
    return false;
  }
  if (address < 0 || address > 65535) {
    g_mbx_last_error = MB_INVALID_ADDRESS;
    g_mbx_success = false;
    return false;
  }
  return true;
}

static bool mbx_cache_entry_expired(const mbx_cache_entry_t *entry) {
  // v1: ingen konfigurerbar TTL endnu (se modbus_expansion.h's designnote om
  // manglende PLC-side kanal-config-cache) — en fast, konservativ TTL.
  const uint32_t ttl_ms = 2000;
  if (entry->last_update_ms == 0) return true;
  return (millis() - entry->last_update_ms) >= ttl_ms;
}

/* ============================================================================
 * ASYNC READ BUILTINS
 * ============================================================================ */

st_value_t st_builtin_mbx_read_coil(st_value_t board, st_value_t channel, st_value_t slave_id, st_value_t address) {
  st_value_t result; result.bool_val = false;
  if (!mbx_check_request_limit()) return result;
  if (!mbx_validate(board.int_val, channel.int_val, slave_id.int_val, address.int_val)) return result;

  uint8_t b = (uint8_t)board.int_val, c = (uint8_t)channel.int_val, s = (uint8_t)slave_id.int_val;
  uint16_t a = (uint16_t)address.int_val;

  mbx_cache_entry_t *entry = mbx_cache_get_or_create(b, c, s, a, (uint8_t)MBX_REQ_READ_COIL);
  if (!entry) { g_mbx_last_error = MB_MAX_REQUESTS_EXCEEDED; g_mbx_success = false; return result; }

  portENTER_CRITICAL(&mbx_cache_spinlock);
  result = entry->value;
  mbx_cache_status_t status = entry->status;
  g_mbx_last_error = entry->last_error;
  bool expired = mbx_cache_entry_expired(entry);
  portEXIT_CRITICAL(&mbx_cache_spinlock);

  if (!g_mbx_cache_enabled || expired || status != MBX_CACHE_PENDING) {
    modbus_expansion_async_queue_read(MBX_REQ_READ_COIL, b, c, s, a);
  }
  g_mbx_success = (status == MBX_CACHE_VALID && !expired);
  return result;
}

st_value_t st_builtin_mbx_read_input(st_value_t board, st_value_t channel, st_value_t slave_id, st_value_t address) {
  st_value_t result; result.bool_val = false;
  if (!mbx_check_request_limit()) return result;
  if (!mbx_validate(board.int_val, channel.int_val, slave_id.int_val, address.int_val)) return result;

  uint8_t b = (uint8_t)board.int_val, c = (uint8_t)channel.int_val, s = (uint8_t)slave_id.int_val;
  uint16_t a = (uint16_t)address.int_val;

  mbx_cache_entry_t *entry = mbx_cache_get_or_create(b, c, s, a, (uint8_t)MBX_REQ_READ_INPUT);
  if (!entry) { g_mbx_last_error = MB_MAX_REQUESTS_EXCEEDED; g_mbx_success = false; return result; }

  portENTER_CRITICAL(&mbx_cache_spinlock);
  result = entry->value;
  mbx_cache_status_t status = entry->status;
  g_mbx_last_error = entry->last_error;
  bool expired = mbx_cache_entry_expired(entry);
  portEXIT_CRITICAL(&mbx_cache_spinlock);

  if (!g_mbx_cache_enabled || expired || status != MBX_CACHE_PENDING) {
    modbus_expansion_async_queue_read(MBX_REQ_READ_INPUT, b, c, s, a);
  }
  g_mbx_success = (status == MBX_CACHE_VALID && !expired);
  return result;
}

st_value_t st_builtin_mbx_read_holding(st_value_t board, st_value_t channel, st_value_t slave_id, st_value_t address) {
  st_value_t result; result.int_val = 0;
  if (!mbx_check_request_limit()) return result;
  if (!mbx_validate(board.int_val, channel.int_val, slave_id.int_val, address.int_val)) return result;

  uint8_t b = (uint8_t)board.int_val, c = (uint8_t)channel.int_val, s = (uint8_t)slave_id.int_val;
  uint16_t a = (uint16_t)address.int_val;

  mbx_cache_entry_t *entry = mbx_cache_get_or_create(b, c, s, a, (uint8_t)MBX_REQ_READ_HOLDING);
  if (!entry) { g_mbx_last_error = MB_MAX_REQUESTS_EXCEEDED; g_mbx_success = false; return result; }

  portENTER_CRITICAL(&mbx_cache_spinlock);
  result = entry->value;
  mbx_cache_status_t status = entry->status;
  g_mbx_last_error = entry->last_error;
  bool expired = mbx_cache_entry_expired(entry);
  portEXIT_CRITICAL(&mbx_cache_spinlock);

  if (!g_mbx_cache_enabled || expired || status != MBX_CACHE_PENDING) {
    modbus_expansion_async_queue_read(MBX_REQ_READ_HOLDING, b, c, s, a);
  }
  g_mbx_success = (status == MBX_CACHE_VALID && !expired);
  return result;
}

st_value_t st_builtin_mbx_read_input_reg(st_value_t board, st_value_t channel, st_value_t slave_id, st_value_t address) {
  st_value_t result; result.int_val = 0;
  if (!mbx_check_request_limit()) return result;
  if (!mbx_validate(board.int_val, channel.int_val, slave_id.int_val, address.int_val)) return result;

  uint8_t b = (uint8_t)board.int_val, c = (uint8_t)channel.int_val, s = (uint8_t)slave_id.int_val;
  uint16_t a = (uint16_t)address.int_val;

  mbx_cache_entry_t *entry = mbx_cache_get_or_create(b, c, s, a, (uint8_t)MBX_REQ_READ_INPUT_REG);
  if (!entry) { g_mbx_last_error = MB_MAX_REQUESTS_EXCEEDED; g_mbx_success = false; return result; }

  portENTER_CRITICAL(&mbx_cache_spinlock);
  result = entry->value;
  mbx_cache_status_t status = entry->status;
  g_mbx_last_error = entry->last_error;
  bool expired = mbx_cache_entry_expired(entry);
  portEXIT_CRITICAL(&mbx_cache_spinlock);

  if (!g_mbx_cache_enabled || expired || status != MBX_CACHE_PENDING) {
    modbus_expansion_async_queue_read(MBX_REQ_READ_INPUT_REG, b, c, s, a);
  }
  g_mbx_success = (status == MBX_CACHE_VALID && !expired);
  return result;
}

/* ============================================================================
 * ASYNC WRITE BUILTINS
 * ============================================================================ */

st_value_t st_builtin_mbx_write_coil(st_value_t board, st_value_t channel, st_value_t slave_id, st_value_t address, st_value_t value) {
  st_value_t result; result.bool_val = false;
  if (!mbx_check_request_limit()) return result;
  if (!mbx_validate(board.int_val, channel.int_val, slave_id.int_val, address.int_val)) return result;

  bool queued = modbus_expansion_async_queue_write(MBX_REQ_WRITE_COIL,
    (uint8_t)board.int_val, (uint8_t)channel.int_val, (uint8_t)slave_id.int_val, (uint16_t)address.int_val, value);
  g_mbx_success = queued;
  result.bool_val = queued;
  return result;
}

st_value_t st_builtin_mbx_write_holding(st_value_t board, st_value_t channel, st_value_t slave_id, st_value_t address, st_value_t value) {
  st_value_t result; result.bool_val = false;
  if (!mbx_check_request_limit()) return result;
  if (!mbx_validate(board.int_val, channel.int_val, slave_id.int_val, address.int_val)) return result;

  bool queued = modbus_expansion_async_queue_write(MBX_REQ_WRITE_HOLDING,
    (uint8_t)board.int_val, (uint8_t)channel.int_val, (uint8_t)slave_id.int_val, (uint16_t)address.int_val, value);
  g_mbx_success = queued;
  result.bool_val = queued;
  return result;
}

// v7.9.68.0: FC16 multi-register write — no single-address cache entry (see
// modbus_expansion_async.h's design note), so unlike the single-value writes
// above, only g_mbx_success is set, not a cache-backed confirmation.
st_value_t st_builtin_mbx_write_holdings(st_value_t board, st_value_t channel, st_value_t slave_id, st_value_t address, st_value_t count) {
  st_value_t result; result.bool_val = false;
  if (!mbx_check_request_limit()) return result;
  if (!mbx_validate(board.int_val, channel.int_val, slave_id.int_val, address.int_val)) return result;

  int32_t cnt = count.int_val;
  if (cnt < 1 || cnt > MBX_MULTI_MAX) {
    g_mbx_last_error = MB_INVALID_ADDRESS;
    g_mbx_success = false;
    return result;
  }
  if (address.int_val + cnt - 1 > 65535) {
    g_mbx_last_error = MB_INVALID_ADDRESS;
    g_mbx_success = false;
    return result;
  }

  bool queued = modbus_expansion_async_queue_write_multi_holdings(
    (uint8_t)board.int_val, (uint8_t)channel.int_val, (uint8_t)slave_id.int_val,
    (uint16_t)address.int_val, (uint8_t)cnt, g_mbx_multi_reg_buf);
  g_mbx_success = queued;
  g_mbx_last_error = queued ? MB_OK : MB_MAX_REQUESTS_EXCEEDED;
  result.bool_val = queued;
  return result;
}

// v7.9.68.0: FC15 multi-coil write — mirrors st_builtin_mbx_write_holdings() above.
st_value_t st_builtin_mbx_write_coils(st_value_t board, st_value_t channel, st_value_t slave_id, st_value_t address, st_value_t count) {
  st_value_t result; result.bool_val = false;
  if (!mbx_check_request_limit()) return result;
  if (!mbx_validate(board.int_val, channel.int_val, slave_id.int_val, address.int_val)) return result;

  int32_t cnt = count.int_val;
  if (cnt < 1 || cnt > MBX_MULTI_MAX) {
    g_mbx_last_error = MB_INVALID_ADDRESS;
    g_mbx_success = false;
    return result;
  }
  if (address.int_val + cnt - 1 > 65535) {
    g_mbx_last_error = MB_INVALID_ADDRESS;
    g_mbx_success = false;
    return result;
  }

  bool queued = modbus_expansion_async_queue_write_multi_coils(
    (uint8_t)board.int_val, (uint8_t)channel.int_val, (uint8_t)slave_id.int_val,
    (uint16_t)address.int_val, (uint8_t)cnt, g_mbx_multi_coil_buf);
  g_mbx_success = queued;
  g_mbx_last_error = queued ? MB_OK : MB_MAX_REQUESTS_EXCEEDED;
  result.bool_val = queued;
  return result;
}

/* ============================================================================
 * STATUS BUILTINS
 * ============================================================================ */

st_value_t st_builtin_mbx_success_func() {
  st_value_t result; result.bool_val = g_mbx_success; return result;
}
st_value_t st_builtin_mbx_busy_func() {
  st_value_t result; result.bool_val = modbus_expansion_async_is_busy(); return result;
}
st_value_t st_builtin_mbx_error_func() {
  st_value_t result; result.int_val = g_mbx_last_error; return result;
}
