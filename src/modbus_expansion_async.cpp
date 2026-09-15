/**
 * @file modbus_expansion_async.cpp
 * @brief FEAT-410: Async Modbus Expansion Master — background task.
 *
 * Tæt, ordret port af mb_async.cpp — se modbus_expansion_async.h's
 * designnote og EXPANSION_BOARD_DESIGN.md §5.1.1 for hvorfor hvert element
 * her er en bevidst genbrugt, produktionshærdet lektion og ikke genopfundet.
 */

#include "modbus_expansion_async.h"
#include "modbus_expansion.h"

mbx_async_state_t g_mbx_async = {0};
portMUX_TYPE mbx_cache_spinlock = portMUX_INITIALIZER_UNLOCKED;

uint16_t g_mbx_multi_write_reg_pool[MBX_MULTI_POOL_SIZE][16] = {0};
volatile uint8_t g_mbx_multi_write_reg_next = 0;
bool g_mbx_multi_write_coil_pool[MBX_MULTI_POOL_SIZE][16] = {false};
volatile uint8_t g_mbx_multi_write_coil_next = 0;

/* ============================================================================
 * CACHE FUNCTIONS
 * ============================================================================ */

mbx_cache_entry_t *mbx_cache_find(uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address, uint8_t req_type) {
  for (uint16_t i = 0; i < g_mbx_async.entry_count; i++) {
    mbx_cache_entry_t *e = &g_mbx_async.entries[i];
    if (e->key.board == board && e->key.channel == channel &&
        e->key.slave_id == slave_id && e->key.address == address &&
        e->key.req_type == req_type) {
      return e;
    }
  }
  return NULL;
}

mbx_cache_entry_t *mbx_cache_get_or_create(uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address, uint8_t req_type) {
  mbx_cache_entry_t *e = mbx_cache_find(board, channel, slave_id, address, req_type);
  if (e) {
    g_mbx_async.cache_hits++;
    return e;
  }

  g_mbx_async.cache_misses++;

  if (g_mbx_async.entry_count < MBX_ASYNC_CACHE_MAX_ENTRIES) {
    e = &g_mbx_async.entries[g_mbx_async.entry_count++];
  } else {
    uint32_t oldest_ms = UINT32_MAX;
    uint16_t oldest_idx = 0;
    for (uint16_t i = 0; i < MBX_ASYNC_CACHE_MAX_ENTRIES; i++) {
      if (g_mbx_async.entries[i].status == MBX_CACHE_PENDING) continue;
      if (g_mbx_async.entries[i].last_update_ms < oldest_ms) {
        oldest_ms = g_mbx_async.entries[i].last_update_ms;
        oldest_idx = i;
      }
    }
    e = &g_mbx_async.entries[oldest_idx];
  }

  memset(e, 0, sizeof(mbx_cache_entry_t));
  e->key.board = board;
  e->key.channel = channel;
  e->key.slave_id = slave_id;
  e->key.address = address;
  e->key.req_type = req_type;
  e->status = MBX_CACHE_EMPTY;
  return e;
}

/* ============================================================================
 * PENDING lifecycle (BUG-333-lektionen, se mb_async.cpp)
 * ============================================================================ */

static inline void mbx_cache_mark_pending_locked(mbx_cache_entry_t *entry) {
  entry->status = MBX_CACHE_PENDING;
  entry->pending_since_ms = millis();
}

static void mbx_cache_clear_pending(mbx_cache_entry_t *entry) {
  if (!entry) return;
  portENTER_CRITICAL(&mbx_cache_spinlock);
  if (entry->status == MBX_CACHE_PENDING) {
    entry->status = (entry->last_update_ms > 0) ? MBX_CACHE_VALID : MBX_CACHE_EMPTY;
  }
  portEXIT_CRITICAL(&mbx_cache_spinlock);
}

static void mbx_cache_clear_pending_for_request(const mbx_async_request_t *req) {
  if (!req) return;
  uint8_t cache_type;
  switch (req->type) {
    case MBX_REQ_WRITE_COIL:    cache_type = (uint8_t)MBX_REQ_READ_COIL;    break;
    case MBX_REQ_WRITE_HOLDING: cache_type = (uint8_t)MBX_REQ_READ_HOLDING; break;
    default:                    cache_type = (uint8_t)req->type;           break;
  }
  mbx_cache_clear_pending(mbx_cache_find(req->board, req->channel, req->slave_id, req->address, cache_type));
}

static void mbx_cache_sweep_stale_pending(void) {
  uint32_t limit = (uint32_t)MODBUS_EXPANSION_TRANSACTION_TIMEOUT_MS * MBX_PENDING_STALE_FACTOR;
  if (limit < MBX_PENDING_STALE_MIN_MS) limit = MBX_PENDING_STALE_MIN_MS;

  uint32_t now = millis();
  for (uint16_t i = 0; i < g_mbx_async.entry_count; i++) {
    mbx_cache_entry_t *e = &g_mbx_async.entries[i];
    if (e->status != MBX_CACHE_PENDING) continue;
    if ((now - e->pending_since_ms) < limit) continue;

    portENTER_CRITICAL(&mbx_cache_spinlock);
    if (e->status == MBX_CACHE_PENDING) {
      e->status = (e->last_update_ms > 0) ? MBX_CACHE_VALID : MBX_CACHE_EMPTY;
      e->last_error = MB_TIMEOUT;
      g_mbx_async.stale_pending_recovered++;
    }
    portEXIT_CRITICAL(&mbx_cache_spinlock);
  }
}

/* ============================================================================
 * PRIORITY QUEUE (identisk algoritme til mb_async.cpp's, se den fil for
 * den fulde forklaring af evictions-strategien)
 * ============================================================================ */

static bool mbx_pq_insert(mbx_async_request_t *req) {
  mbx_async_request_t evicted;
  bool had_eviction = false;

  if (xSemaphoreTake(g_mbx_async.pq_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
    return false;
  }

  req->insert_seq = g_mbx_async.pq_seq++;

  if (g_mbx_async.pq_count < MBX_ASYNC_QUEUE_SIZE) {
    g_mbx_async.pq_buf[g_mbx_async.pq_count] = *req;
    g_mbx_async.pq_count++;
  } else {
    int victim = -1;
    uint8_t worst_prio = 0;
    uint16_t newest_seq = 0;
    for (uint8_t i = 0; i < g_mbx_async.pq_count; i++) {
      uint8_t p = g_mbx_async.pq_buf[i].priority;
      uint16_t s = g_mbx_async.pq_buf[i].insert_seq;
      if (p > worst_prio || (p == worst_prio && (victim == -1 || s > newest_seq))) {
        worst_prio = p;
        newest_seq = s;
        victim = i;
      }
    }

    if (victim >= 0 && worst_prio > req->priority) {
      evicted = g_mbx_async.pq_buf[victim];
      had_eviction = true;
      g_mbx_async.pq_buf[victim] = *req;
      g_mbx_async.priority_drops++;
    } else {
      g_mbx_async.queue_full_count++;
      xSemaphoreGive(g_mbx_async.pq_mutex);
      return false;
    }
  }

  if (g_mbx_async.pq_count > g_mbx_async.queue_high_watermark) {
    g_mbx_async.queue_high_watermark = g_mbx_async.pq_count;
  }

  xSemaphoreGive(g_mbx_async.pq_mutex);

  if (had_eviction) {
    mbx_cache_clear_pending_for_request(&evicted);
  }

  xSemaphoreGive(g_mbx_async.pq_semaphore);
  return true;
}

static bool mbx_pq_dequeue(mbx_async_request_t *out) {
  if (xSemaphoreTake(g_mbx_async.pq_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
    return false;
  }
  if (g_mbx_async.pq_count == 0) {
    xSemaphoreGive(g_mbx_async.pq_mutex);
    return false;
  }

  int best = 0;
  for (uint8_t i = 1; i < g_mbx_async.pq_count; i++) {
    uint8_t bp = g_mbx_async.pq_buf[best].priority;
    uint8_t ip = g_mbx_async.pq_buf[i].priority;
    if (ip < bp || (ip == bp && g_mbx_async.pq_buf[i].insert_seq < g_mbx_async.pq_buf[best].insert_seq)) {
      best = i;
    }
  }

  *out = g_mbx_async.pq_buf[best];
  g_mbx_async.pq_count--;
  if ((uint8_t)best < g_mbx_async.pq_count) {
    g_mbx_async.pq_buf[best] = g_mbx_async.pq_buf[g_mbx_async.pq_count];
  }

  xSemaphoreGive(g_mbx_async.pq_mutex);
  return true;
}

/* ============================================================================
 * QUEUE FUNCTIONS
 * ============================================================================ */

bool modbus_expansion_async_queue_read(mbx_request_type_t type, uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address) {
  mbx_cache_entry_t *entry = mbx_cache_find(board, channel, slave_id, address, (uint8_t)type);
  if (entry && entry->status == MBX_CACHE_PENDING) {
    return true;  // Allerede i kø (deduplikering)
  }

  uint8_t prio = MBX_PRIO_READ_FRESH;
  if (entry && entry->status == MBX_CACHE_VALID) {
    prio = MBX_PRIO_READ_REFRESH;
  }

  if (!entry) {
    entry = mbx_cache_get_or_create(board, channel, slave_id, address, (uint8_t)type);
  }
  if (entry) {
    portENTER_CRITICAL(&mbx_cache_spinlock);
    mbx_cache_mark_pending_locked(entry);
    portEXIT_CRITICAL(&mbx_cache_spinlock);
  }

  mbx_async_request_t req;
  memset(&req, 0, sizeof(req));
  req.type = type;
  req.board = board;
  req.channel = channel;
  req.slave_id = slave_id;
  req.address = address;
  req.priority = prio;

  if (!mbx_pq_insert(&req)) {
    if (entry) {
      portENTER_CRITICAL(&mbx_cache_spinlock);
      entry->status = (entry->last_update_ms > 0) ? MBX_CACHE_VALID : MBX_CACHE_EMPTY;
      portEXIT_CRITICAL(&mbx_cache_spinlock);
    }
    return false;
  }
  return true;
}

bool modbus_expansion_async_queue_write(mbx_request_type_t type, uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address, st_value_t value) {
  uint8_t read_type = (type == MBX_REQ_WRITE_COIL) ? (uint8_t)MBX_REQ_READ_COIL : (uint8_t)MBX_REQ_READ_HOLDING;
  mbx_cache_entry_t *cached = mbx_cache_find(board, channel, slave_id, address, read_type);
  if (cached && cached->status == MBX_CACHE_VALID && cached->value.int_val == value.int_val) {
    return true;  // Samme vaerdi allerede bekraeftet skrevet — skip (samme dedup som mb_async)
  }

  mbx_async_request_t req;
  memset(&req, 0, sizeof(req));
  req.type = type;
  req.board = board;
  req.channel = channel;
  req.slave_id = slave_id;
  req.address = address;
  req.write_value = value;
  req.priority = MBX_PRIO_WRITE;

  if (!mbx_pq_insert(&req)) {
    return false;
  }

  mbx_cache_entry_t *entry = mbx_cache_get_or_create(board, channel, slave_id, address, read_type);
  if (entry) {
    portENTER_CRITICAL(&mbx_cache_spinlock);
    entry->value = value;
    mbx_cache_mark_pending_locked(entry);
    portEXIT_CRITICAL(&mbx_cache_spinlock);
  }
  return true;
}

// v7.9.68.0: FC16 multi-register write — no per-address cache entry (see file
// header design note), so unlike modbus_expansion_async_queue_write() above,
// there is no dedup-against-cached-value check and no cache entry created here.
bool modbus_expansion_async_queue_write_multi_holdings(uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address, uint8_t count, const uint16_t *values) {
  if (count == 0 || count > 16) return false;

  uint8_t slot = g_mbx_multi_write_reg_next;
  g_mbx_multi_write_reg_next = (g_mbx_multi_write_reg_next + 1) % MBX_MULTI_POOL_SIZE;
  memcpy(g_mbx_multi_write_reg_pool[slot], values, count * sizeof(uint16_t));

  mbx_async_request_t req;
  memset(&req, 0, sizeof(req));
  req.type = MBX_REQ_WRITE_HOLDINGS;
  req.board = board;
  req.channel = channel;
  req.slave_id = slave_id;
  req.address = address;
  req.count = count;
  req.multi_pool_slot = slot;
  req.priority = MBX_PRIO_WRITE;

  return mbx_pq_insert(&req);
}

// v7.9.68.0: FC15 multi-coil write — mirrors modbus_expansion_async_queue_write_multi_holdings() above.
bool modbus_expansion_async_queue_write_multi_coils(uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address, uint8_t count, const bool *values) {
  if (count == 0 || count > 16) return false;

  uint8_t slot = g_mbx_multi_write_coil_next;
  g_mbx_multi_write_coil_next = (g_mbx_multi_write_coil_next + 1) % MBX_MULTI_POOL_SIZE;
  memcpy(g_mbx_multi_write_coil_pool[slot], values, count * sizeof(bool));

  mbx_async_request_t req;
  memset(&req, 0, sizeof(req));
  req.type = MBX_REQ_WRITE_COILS;
  req.board = board;
  req.channel = channel;
  req.slave_id = slave_id;
  req.address = address;
  req.count = count;
  req.multi_pool_slot = slot;
  req.priority = MBX_PRIO_WRITE;

  return mbx_pq_insert(&req);
}

bool modbus_expansion_async_is_busy() { return g_mbx_async.pq_count > 0; }
uint8_t modbus_expansion_async_queue_depth() { return g_mbx_async.pq_count; }

/* ============================================================================
 * PER-(board,kanal,slave) ADAPTIV BACKOFF
 * ============================================================================ */

static uint8_t mbx_backoff_find_or_create(uint8_t board, uint8_t channel, uint8_t slave_id) {
  for (uint8_t i = 0; i < MBX_SLAVE_BACKOFF_MAX; i++) {
    if (g_mbx_async.slave_backoff[i].board == board && g_mbx_async.slave_backoff[i].channel == channel &&
        g_mbx_async.slave_backoff[i].slave_id == slave_id) {
      return i;
    }
  }
  for (uint8_t i = 0; i < MBX_SLAVE_BACKOFF_MAX; i++) {
    if (g_mbx_async.slave_backoff[i].board == 0) {
      g_mbx_async.slave_backoff[i].board = board;
      g_mbx_async.slave_backoff[i].channel = channel;
      g_mbx_async.slave_backoff[i].slave_id = slave_id;
      return i;
    }
  }
  uint8_t min_idx = 0;
  uint16_t min_bo = UINT16_MAX;
  for (uint8_t i = 0; i < MBX_SLAVE_BACKOFF_MAX; i++) {
    if (g_mbx_async.slave_backoff[i].backoff_ms < min_bo) {
      min_bo = g_mbx_async.slave_backoff[i].backoff_ms;
      min_idx = i;
    }
  }
  memset(&g_mbx_async.slave_backoff[min_idx], 0, sizeof(g_mbx_async.slave_backoff[0]));
  g_mbx_async.slave_backoff[min_idx].board = board;
  g_mbx_async.slave_backoff[min_idx].channel = channel;
  g_mbx_async.slave_backoff[min_idx].slave_id = slave_id;
  return min_idx;
}

static void mbx_backoff_on_timeout(uint8_t board, uint8_t channel, uint8_t slave_id) {
  uint8_t idx = mbx_backoff_find_or_create(board, channel, slave_id);
  auto &s = g_mbx_async.slave_backoff[idx];
  s.timeout_count++;
  s.success_count = 0;
  if (s.backoff_ms == 0) {
    s.backoff_ms = MBX_BACKOFF_INITIAL_MS;
  } else {
    s.backoff_ms = (s.backoff_ms * 2 > MBX_BACKOFF_MAX_MS) ? MBX_BACKOFF_MAX_MS : s.backoff_ms * 2;
  }
}

static void mbx_backoff_on_success(uint8_t board, uint8_t channel, uint8_t slave_id) {
  uint8_t idx = mbx_backoff_find_or_create(board, channel, slave_id);
  auto &s = g_mbx_async.slave_backoff[idx];
  s.timeout_count = 0;
  s.success_count++;
  if (s.backoff_ms > 0) {
    s.backoff_ms = (s.backoff_ms > MBX_BACKOFF_DECAY_MS) ? (s.backoff_ms - MBX_BACKOFF_DECAY_MS) : 0;
  }
}

/* ============================================================================
 * BACKGROUND TASK
 * ============================================================================ */

static void modbus_expansion_async_task_func(void *pvParameters) {
  mbx_async_request_t req;
  uint32_t last_sweep_ms = 0;

  while (g_mbx_async.task_running) {
    if (g_mbx_async.paused) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    uint32_t now_ms = millis();
    if ((now_ms - last_sweep_ms) >= MBX_PENDING_SWEEP_INTERVAL_MS) {
      last_sweep_ms = now_ms;
      mbx_cache_sweep_stale_pending();
    }

    if (xSemaphoreTake(g_mbx_async.pq_semaphore, pdMS_TO_TICKS(100)) != pdTRUE) {
      continue;
    }
    if (!mbx_pq_dequeue(&req)) {
      continue;
    }

    g_mbx_async.total_requests++;

    // Per-(board,kanal,slave) backoff — skip uden at blokere hele koeen
    {
      uint8_t bo_idx = 255;
      for (uint8_t i = 0; i < MBX_SLAVE_BACKOFF_MAX; i++) {
        if (g_mbx_async.slave_backoff[i].board == req.board && g_mbx_async.slave_backoff[i].channel == req.channel &&
            g_mbx_async.slave_backoff[i].slave_id == req.slave_id) {
          bo_idx = i;
          break;
        }
      }
      if (bo_idx < MBX_SLAVE_BACKOFF_MAX && g_mbx_async.slave_backoff[bo_idx].backoff_ms > 0) {
        uint32_t elapsed = millis() - g_mbx_async.slave_backoff[bo_idx].last_attempt_ms;
        if (elapsed < g_mbx_async.slave_backoff[bo_idx].backoff_ms) {
          uint8_t cache_type = (uint8_t)req.type;
          if (req.type == MBX_REQ_WRITE_COIL) cache_type = (uint8_t)MBX_REQ_READ_COIL;
          if (req.type == MBX_REQ_WRITE_HOLDING) cache_type = (uint8_t)MBX_REQ_READ_HOLDING;
          mbx_cache_entry_t *entry = mbx_cache_find(req.board, req.channel, req.slave_id, req.address, cache_type);
          if (entry) {
            portENTER_CRITICAL(&mbx_cache_spinlock);
            entry->status = MBX_CACHE_ERROR;
            entry->last_error = MB_TIMEOUT;
            portEXIT_CRITICAL(&mbx_cache_spinlock);
          }
          g_mbx_async.total_errors++;
          g_mbx_async.total_timeouts++;
          continue;
        }
        g_mbx_async.slave_backoff[bo_idx].last_attempt_ms = millis();
      }
    }

    mb_error_code_t err = MB_OK;
    st_value_t result;
    result.int_val = 0;

    switch (req.type) {
      case MBX_REQ_READ_COIL: {
        bool v = false;
        err = modbus_expansion_read_coil(req.board, req.channel, req.slave_id, req.address, &v);
        result.bool_val = v;
        break;
      }
      case MBX_REQ_READ_INPUT: {
        bool v = false;
        err = modbus_expansion_read_input(req.board, req.channel, req.slave_id, req.address, &v);
        result.bool_val = v;
        break;
      }
      case MBX_REQ_READ_HOLDING: {
        uint16_t v = 0;
        err = modbus_expansion_read_holding(req.board, req.channel, req.slave_id, req.address, &v);
        result.int_val = (int32_t)v;
        break;
      }
      case MBX_REQ_READ_INPUT_REG: {
        uint16_t v = 0;
        err = modbus_expansion_read_input_register(req.board, req.channel, req.slave_id, req.address, &v);
        result.int_val = (int32_t)v;
        break;
      }
      case MBX_REQ_WRITE_COIL: {
        err = modbus_expansion_write_coil(req.board, req.channel, req.slave_id, req.address, req.write_value.bool_val);
        result.bool_val = (err == MB_OK);
        break;
      }
      case MBX_REQ_WRITE_HOLDING: {
        err = modbus_expansion_write_holding(req.board, req.channel, req.slave_id, req.address, (uint16_t)req.write_value.int_val);
        result.bool_val = (err == MB_OK);
        break;
      }
      case MBX_REQ_WRITE_HOLDINGS: {
        // v7.9.68.0: FC16 multi-register write — read values from pool slot
        uint8_t cnt = req.count;
        if (cnt == 0 || cnt > 16) { err = MB_INVALID_ADDRESS; break; }
        err = modbus_expansion_write_holdings(req.board, req.channel, req.slave_id, req.address, cnt,
                                               g_mbx_multi_write_reg_pool[req.multi_pool_slot]);
        result.bool_val = (err == MB_OK);
        break;
      }
      case MBX_REQ_WRITE_COILS: {
        // v7.9.68.0: FC15 multi-coil write — read values from pool slot
        uint8_t cnt = req.count;
        if (cnt == 0 || cnt > 16) { err = MB_INVALID_ADDRESS; break; }
        err = modbus_expansion_write_coils(req.board, req.channel, req.slave_id, req.address, cnt,
                                            g_mbx_multi_write_coil_pool[req.multi_pool_slot]);
        result.bool_val = (err == MB_OK);
        break;
      }
    }

    // v7.9.68.0: multi writes bypass the single-address cache entirely (see
    // file header design note) — only g_mbx_success (set by the caller from
    // this function's return value) reflects them, no cache entry to update.
    if (req.type != MBX_REQ_WRITE_HOLDINGS && req.type != MBX_REQ_WRITE_COILS) {
      uint8_t cache_type = (uint8_t)req.type;
      if (req.type == MBX_REQ_WRITE_COIL) cache_type = (uint8_t)MBX_REQ_READ_COIL;
      if (req.type == MBX_REQ_WRITE_HOLDING) cache_type = (uint8_t)MBX_REQ_READ_HOLDING;

      mbx_cache_entry_t *entry = mbx_cache_find(req.board, req.channel, req.slave_id, req.address, cache_type);
      if (!entry && (req.type == MBX_REQ_WRITE_COIL || req.type == MBX_REQ_WRITE_HOLDING)) {
        entry = mbx_cache_get_or_create(req.board, req.channel, req.slave_id, req.address, cache_type);
      }
      if (entry) {
        portENTER_CRITICAL(&mbx_cache_spinlock);
        if (err == MB_OK) {
          entry->value = result;
          entry->status = MBX_CACHE_VALID;
        } else {
          entry->status = MBX_CACHE_ERROR;
        }
        entry->last_error = err;
        entry->last_update_ms = millis();
        portEXIT_CRITICAL(&mbx_cache_spinlock);
      }
    }

    if (err == MB_TIMEOUT) {
      mbx_backoff_on_timeout(req.board, req.channel, req.slave_id);
    } else if (err == MB_OK) {
      mbx_backoff_on_success(req.board, req.channel, req.slave_id);
    }

    if (err != MB_OK) {
      g_mbx_async.total_errors++;
      if (err == MB_TIMEOUT) g_mbx_async.total_timeouts++;
    }
  }

  vTaskDelete(NULL);
}

/* ============================================================================
 * INIT / DEINIT
 * ============================================================================ */

void modbus_expansion_async_init() {
  memset(&g_mbx_async, 0, sizeof(g_mbx_async));
  g_mbx_async.stats_since_ms = millis();

  g_mbx_async.pq_mutex = xSemaphoreCreateMutex();
  g_mbx_async.pq_semaphore = xSemaphoreCreateCounting(MBX_ASYNC_QUEUE_SIZE, 0);
  if (!g_mbx_async.pq_mutex || !g_mbx_async.pq_semaphore) {
    Serial.println("[MBX_ASYNC] FEJL: Kunne ikke oprette queue sync primitives");
    return;
  }

  g_mbx_async.task_running = true;

  BaseType_t ret = xTaskCreatePinnedToCore(
    modbus_expansion_async_task_func,
    "mbx_async",
    MBX_ASYNC_TASK_STACK,
    NULL,
    MBX_ASYNC_TASK_PRIO,
    &g_mbx_async.task_handle,
    MBX_ASYNC_TASK_CORE
  );

  if (ret != pdPASS) {
    Serial.println("[MBX_ASYNC] FEJL: Kunne ikke starte background task");
    g_mbx_async.task_running = false;
    return;
  }

  Serial.printf("[MBX_ASYNC] Startet: Core %d, stack %d, prio-queue %d, cache max %d\n",
                MBX_ASYNC_TASK_CORE, MBX_ASYNC_TASK_STACK, MBX_ASYNC_QUEUE_SIZE, MBX_ASYNC_CACHE_MAX_ENTRIES);
}

void modbus_expansion_async_deinit() {
  g_mbx_async.task_running = false;
  if (g_mbx_async.task_handle) {
    vTaskDelay(pdMS_TO_TICKS(200));
    g_mbx_async.task_handle = NULL;
  }
  if (g_mbx_async.pq_mutex) { vSemaphoreDelete(g_mbx_async.pq_mutex); g_mbx_async.pq_mutex = NULL; }
  if (g_mbx_async.pq_semaphore) { vSemaphoreDelete(g_mbx_async.pq_semaphore); g_mbx_async.pq_semaphore = NULL; }
}

void modbus_expansion_async_pause() { g_mbx_async.paused = true; }
void modbus_expansion_async_unpause() { g_mbx_async.paused = false; }
bool modbus_expansion_async_is_paused() { return g_mbx_async.paused; }

const mbx_async_state_t *modbus_expansion_async_get_state() { return &g_mbx_async; }

void modbus_expansion_async_reset_cache() {
  portENTER_CRITICAL(&mbx_cache_spinlock);
  g_mbx_async.entry_count = 0;
  memset(g_mbx_async.entries, 0, sizeof(g_mbx_async.entries));
  portEXIT_CRITICAL(&mbx_cache_spinlock);
}

void modbus_expansion_async_reset_stats() {
  portENTER_CRITICAL(&mbx_cache_spinlock);
  g_mbx_async.cache_hits = 0;
  g_mbx_async.cache_misses = 0;
  g_mbx_async.queue_full_count = 0;
  g_mbx_async.priority_drops = 0;
  g_mbx_async.queue_high_watermark = 0;
  g_mbx_async.total_requests = 0;
  g_mbx_async.total_errors = 0;
  g_mbx_async.total_timeouts = 0;
  g_mbx_async.stats_since_ms = millis();
  memset(g_mbx_async.slave_backoff, 0, sizeof(g_mbx_async.slave_backoff));
  portEXIT_CRITICAL(&mbx_cache_spinlock);
}
