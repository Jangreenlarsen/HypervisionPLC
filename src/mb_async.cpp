/**
 * @file mb_async.cpp
 * @brief Async Modbus Master — FreeRTOS background task implementation
 *
 * Runs Modbus UART I/O on a dedicated FreeRTOS task (Core 0).
 * ST Logic builtins queue requests and read cached results.
 *
 * v7.7.0 (2026-03-31)
 */

#include "mb_async.h"
#include "modbus_master.h"
#include "st_builtin_modbus.h"

/* ============================================================================
 * GLOBALS
 * ============================================================================ */

mb_async_state_t g_mb_async = {0};
portMUX_TYPE mb_cache_spinlock = portMUX_INITIALIZER_UNLOCKED;

// Ring-buffer pool for FC16 multi-register write values
uint16_t g_mb_multi_write_pool[MB_MULTI_REG_POOL_SIZE][16] = {0};
volatile uint8_t g_mb_multi_write_next = 0;

/* ============================================================================
 * CACHE FUNCTIONS
 * ============================================================================ */

mb_cache_entry_t *mb_cache_find(uint8_t slave_id, uint16_t address, uint8_t req_type) {
  for (uint8_t i = 0; i < g_mb_async.entry_count; i++) {
    mb_cache_entry_t *e = &g_mb_async.entries[i];
    if (e->key.slave_id == slave_id &&
        e->key.address == address &&
        e->key.req_type == req_type) {
      return e;
    }
  }
  return NULL;
}

mb_cache_entry_t *mb_cache_get_or_create(uint8_t slave_id, uint16_t address, uint8_t req_type) {
  // Try find existing
  mb_cache_entry_t *e = mb_cache_find(slave_id, address, req_type);
  if (e) {
    g_mb_async.cache_hits++;
    return e;
  }

  g_mb_async.cache_misses++;

  // Create new if space available
  if (g_mb_async.entry_count < MB_CACHE_MAX_ENTRIES) {
    e = &g_mb_async.entries[g_mb_async.entry_count++];
  } else {
    // LRU eviction: find oldest entry (lowest last_update_ms)
    uint32_t oldest_ms = UINT32_MAX;
    uint8_t oldest_idx = 0;
    for (uint8_t i = 0; i < MB_CACHE_MAX_ENTRIES; i++) {
      // Skip PENDING entries (active request in flight)
      if (g_mb_async.entries[i].status == MB_CACHE_PENDING) continue;
      if (g_mb_async.entries[i].last_update_ms < oldest_ms) {
        oldest_ms = g_mb_async.entries[i].last_update_ms;
        oldest_idx = i;
      }
    }
    e = &g_mb_async.entries[oldest_idx];
  }

  memset(e, 0, sizeof(mb_cache_entry_t));
  e->key.slave_id = slave_id;
  e->key.address = address;
  e->key.req_type = req_type;
  e->last_fc = req_type;  // Default to keyed type until a real op completes
  e->status = MB_CACHE_EMPTY;
  return e;
}

/* ============================================================================
 * PRIORITY QUEUE (v7.9.7)
 *
 * Replaces FreeRTOS FIFO queue with 3-level priority:
 *   WRITE (0) > READ_FRESH (1) > READ_REFRESH (2)
 *
 * Insert: O(1) — append to end, update watermark
 * Dequeue: O(n) — scan for highest prio, oldest seq
 * Evict on full: drop newest entry with lowest priority
 * ============================================================================ */

static bool mb_pq_insert(mb_async_request_t *req) {
  if (xSemaphoreTake(g_mb_async.pq_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
    return false;
  }

  req->insert_seq = g_mb_async.pq_seq++;

  if (g_mb_async.pq_count < MB_ASYNC_QUEUE_SIZE) {
    // Space available — just append
    g_mb_async.pq_buf[g_mb_async.pq_count] = *req;
    g_mb_async.pq_count++;
  } else {
    // Queue full — evict newest entry with lowest priority
    // Find victim: highest priority value (lowest importance), highest insert_seq (newest)
    int victim = -1;
    uint8_t worst_prio = 0;
    uint16_t newest_seq = 0;
    for (uint8_t i = 0; i < g_mb_async.pq_count; i++) {
      uint8_t p = g_mb_async.pq_buf[i].priority;
      uint16_t s = g_mb_async.pq_buf[i].insert_seq;
      if (p > worst_prio || (p == worst_prio && (victim == -1 || s > newest_seq))) {
        worst_prio = p;
        newest_seq = s;
        victim = i;
      }
    }

    // Only evict if victim has lower priority (higher number) than new request
    if (victim >= 0 && worst_prio > req->priority) {
      g_mb_async.pq_buf[victim] = *req;
      g_mb_async.priority_drops++;
    } else if (victim >= 0 && worst_prio == req->priority) {
      // Same priority — drop the new request (it's newest)
      g_mb_async.queue_full_count++;
      xSemaphoreGive(g_mb_async.pq_mutex);
      return false;
    } else {
      // New request is lower priority than everything in queue — drop it
      g_mb_async.queue_full_count++;
      xSemaphoreGive(g_mb_async.pq_mutex);
      return false;
    }
  }

  // Update watermark
  if (g_mb_async.pq_count > g_mb_async.queue_high_watermark) {
    g_mb_async.queue_high_watermark = g_mb_async.pq_count;
  }

  xSemaphoreGive(g_mb_async.pq_mutex);
  xSemaphoreGive(g_mb_async.pq_semaphore);  // Signal consumer
  return true;
}

static bool mb_pq_dequeue(mb_async_request_t *out) {
  if (xSemaphoreTake(g_mb_async.pq_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
    return false;
  }

  if (g_mb_async.pq_count == 0) {
    xSemaphoreGive(g_mb_async.pq_mutex);
    return false;
  }

  // Find highest priority (lowest value), oldest (lowest insert_seq)
  int best = 0;
  for (uint8_t i = 1; i < g_mb_async.pq_count; i++) {
    uint8_t bp = g_mb_async.pq_buf[best].priority;
    uint8_t ip = g_mb_async.pq_buf[i].priority;
    if (ip < bp || (ip == bp && g_mb_async.pq_buf[i].insert_seq < g_mb_async.pq_buf[best].insert_seq)) {
      best = i;
    }
  }

  *out = g_mb_async.pq_buf[best];

  // Remove by swapping with last
  g_mb_async.pq_count--;
  if ((uint8_t)best < g_mb_async.pq_count) {
    g_mb_async.pq_buf[best] = g_mb_async.pq_buf[g_mb_async.pq_count];
  }

  xSemaphoreGive(g_mb_async.pq_mutex);
  return true;
}

/* ============================================================================
 * QUEUE FUNCTIONS
 * ============================================================================ */

bool mb_async_queue_read(mb_request_type_t type, uint8_t slave_id, uint16_t address) {
  // Check cache — if already PENDING and cache enabled, skip (deduplication)
  extern bool g_mb_cache_enabled;
  mb_cache_entry_t *entry = mb_cache_find(slave_id, address, (uint8_t)type);
  if (g_mb_cache_enabled && entry && entry->status == MB_CACHE_PENDING) {
    return true;  // Already queued
  }

  // Determine priority: fresh (no cache) vs refresh (has cached value)
  uint8_t prio = MB_PRIO_READ_FRESH;
  if (entry && entry->status == MB_CACHE_VALID) {
    prio = MB_PRIO_READ_REFRESH;
  }

  // Mark as pending
  if (!entry) {
    entry = mb_cache_get_or_create(slave_id, address, (uint8_t)type);
  }
  if (entry) {
    portENTER_CRITICAL(&mb_cache_spinlock);
    entry->status = MB_CACHE_PENDING;
    portEXIT_CRITICAL(&mb_cache_spinlock);
  }

  // Build request
  mb_async_request_t req;
  memset(&req, 0, sizeof(req));
  req.type = type;
  req.slave_id = slave_id;
  req.address = address;
  req.priority = prio;

  if (!mb_pq_insert(&req)) {
    // Revert status on queue-full
    if (entry) {
      portENTER_CRITICAL(&mb_cache_spinlock);
      entry->status = (entry->last_update_ms > 0) ? MB_CACHE_VALID : MB_CACHE_EMPTY;
      portEXIT_CRITICAL(&mb_cache_spinlock);
    }
    return false;
  }

  return true;
}

bool mb_async_queue_write(mb_request_type_t type, uint8_t slave_id, uint16_t address, st_value_t value) {
  // Write deduplication: skip if cache shows same value already written successfully
  extern bool g_mb_cache_enabled;
  if (g_mb_cache_enabled) {
    uint8_t read_type = (type == MB_REQ_WRITE_COIL) ? (uint8_t)MB_REQ_READ_COIL : (uint8_t)MB_REQ_READ_HOLDING;
    mb_cache_entry_t *cached = mb_cache_find(slave_id, address, read_type);
    if (cached && cached->status == MB_CACHE_VALID &&
        cached->value.int_val == value.int_val) {
      return true;  // Same value already confirmed written — skip
    }
  }

  mb_async_request_t req;
  memset(&req, 0, sizeof(req));
  req.type = type;
  req.slave_id = slave_id;
  req.address = address;
  req.write_value = value;
  req.priority = MB_PRIO_WRITE;

  if (!mb_pq_insert(&req)) {
    return false;
  }

  // Update cache to reflect the pending write value
  uint8_t cache_type = (type == MB_REQ_WRITE_COIL) ? (uint8_t)MB_REQ_READ_COIL : (uint8_t)MB_REQ_READ_HOLDING;
  mb_cache_entry_t *entry = mb_cache_get_or_create(slave_id, address, cache_type);
  if (entry) {
    portENTER_CRITICAL(&mb_cache_spinlock);
    entry->value = value;
    entry->status = MB_CACHE_PENDING;
    portEXIT_CRITICAL(&mb_cache_spinlock);
  }

  return true;
}

bool mb_async_queue_read_multi(uint8_t slave_id, uint16_t address, uint8_t count) {
  if (count == 0 || count > 16) return false;

  // Check if any of the addresses already have cached values → refresh priority
  uint8_t prio = MB_PRIO_READ_FRESH;
  for (uint8_t i = 0; i < count; i++) {
    mb_cache_entry_t *e = mb_cache_find(slave_id, address + i, (uint8_t)MB_REQ_READ_HOLDING);
    if (e && e->status == MB_CACHE_VALID) {
      prio = MB_PRIO_READ_REFRESH;
      break;
    }
  }

  mb_async_request_t req;
  memset(&req, 0, sizeof(req));
  req.type = MB_REQ_READ_HOLDINGS;
  req.slave_id = slave_id;
  req.address = address;
  req.count = count;
  req.priority = prio;

  // Mark all individual cache entries as pending
  for (uint8_t i = 0; i < count; i++) {
    mb_cache_entry_t *entry = mb_cache_get_or_create(slave_id, address + i, (uint8_t)MB_REQ_READ_HOLDING);
    if (entry) {
      portENTER_CRITICAL(&mb_cache_spinlock);
      entry->status = MB_CACHE_PENDING;
      portEXIT_CRITICAL(&mb_cache_spinlock);
    }
  }

  if (!mb_pq_insert(&req)) {
    g_mb_async.queue_full_count++;
    return false;
  }
  return true;
}

bool mb_async_queue_write_multi(uint8_t slave_id, uint16_t address, uint8_t count, const uint16_t *values) {
  if (count == 0 || count > 16) return false;

  // Allocate a pool slot for multi-reg values (ring-buffer, wraps at 4)
  uint8_t slot = g_mb_multi_write_next;
  g_mb_multi_write_next = (g_mb_multi_write_next + 1) % MB_MULTI_REG_POOL_SIZE;
  memcpy(g_mb_multi_write_pool[slot], values, count * sizeof(uint16_t));

  mb_async_request_t req;
  memset(&req, 0, sizeof(req));
  req.type = MB_REQ_WRITE_HOLDINGS;
  req.slave_id = slave_id;
  req.address = address;
  req.count = count;
  req.multi_pool_slot = slot;
  req.priority = MB_PRIO_WRITE;

  if (!mb_pq_insert(&req)) {
    g_mb_async.queue_full_count++;
    return false;
  }
  return true;
}

bool mb_async_is_busy() {
  return g_mb_async.pq_count > 0;
}

uint8_t mb_async_queue_depth() {
  return g_mb_async.pq_count;
}

/* ============================================================================
 * PER-SLAVE ADAPTIVE BACKOFF (v7.9.3)
 *
 * Tracks consecutive timeouts per slave_id. On timeout:
 *   backoff doubles (50→100→200→400→800→1600→2000ms cap)
 * On success:
 *   backoff decays by 100ms per success until 0
 *
 * Effect: if slave ID 250 doesn't exist, first request takes 500ms (timeout),
 * but subsequent requests wait 2s between attempts instead of flooding the bus.
 * ============================================================================ */

static uint8_t mb_backoff_find_or_create(uint8_t slave_id) {
  // Find existing slot
  for (uint8_t i = 0; i < MB_SLAVE_BACKOFF_MAX; i++) {
    if (g_mb_async.slave_backoff[i].slave_id == slave_id) return i;
  }
  // Find empty slot
  for (uint8_t i = 0; i < MB_SLAVE_BACKOFF_MAX; i++) {
    if (g_mb_async.slave_backoff[i].slave_id == 0) {
      g_mb_async.slave_backoff[i].slave_id = slave_id;
      return i;
    }
  }
  // All slots used — evict the one with lowest backoff (least problematic)
  uint8_t min_idx = 0;
  uint16_t min_bo = UINT16_MAX;
  for (uint8_t i = 0; i < MB_SLAVE_BACKOFF_MAX; i++) {
    if (g_mb_async.slave_backoff[i].backoff_ms < min_bo) {
      min_bo = g_mb_async.slave_backoff[i].backoff_ms;
      min_idx = i;
    }
  }
  memset(&g_mb_async.slave_backoff[min_idx], 0, sizeof(g_mb_async.slave_backoff[0]));
  g_mb_async.slave_backoff[min_idx].slave_id = slave_id;
  return min_idx;
}

static void mb_backoff_on_timeout(uint8_t slave_id) {
  uint8_t idx = mb_backoff_find_or_create(slave_id);
  auto &s = g_mb_async.slave_backoff[idx];
  s.timeout_count++;
  s.success_count = 0;
  if (s.backoff_ms == 0) {
    s.backoff_ms = MB_BACKOFF_INITIAL_MS;
  } else {
    s.backoff_ms = (s.backoff_ms * 2 > MB_BACKOFF_MAX_MS) ? MB_BACKOFF_MAX_MS : s.backoff_ms * 2;
  }
}

static void mb_backoff_on_success(uint8_t slave_id) {
  uint8_t idx = mb_backoff_find_or_create(slave_id);
  auto &s = g_mb_async.slave_backoff[idx];
  s.timeout_count = 0;
  s.success_count++;
  if (s.backoff_ms > 0) {
    s.backoff_ms = (s.backoff_ms > MB_BACKOFF_DECAY_MS) ? (s.backoff_ms - MB_BACKOFF_DECAY_MS) : 0;
  }
}

/* ============================================================================
 * BACKGROUND TASK
 * ============================================================================ */

static void mb_async_task_func(void *pvParameters) {
  mb_async_request_t req;

  while (g_mb_async.task_running) {
    // Block max 100ms waiting for semaphore signal (allows clean shutdown)
    if (xSemaphoreTake(g_mb_async.pq_semaphore, pdMS_TO_TICKS(100)) != pdTRUE) {
      continue;
    }
    if (!mb_pq_dequeue(&req)) {
      continue;
    }

    g_mb_async.total_requests++;

    // Per-slave backoff: SKIP request if slave is in backoff cooldown
    // Instead of blocking the entire queue with vTaskDelay, we check elapsed
    // time since last attempt and skip if not enough time has passed.
    {
      uint8_t bo_idx = 255;
      for (uint8_t i = 0; i < MB_SLAVE_BACKOFF_MAX; i++) {
        if (g_mb_async.slave_backoff[i].slave_id == req.slave_id) {
          bo_idx = i;
          break;
        }
      }
      if (bo_idx < MB_SLAVE_BACKOFF_MAX && g_mb_async.slave_backoff[bo_idx].backoff_ms > 0) {
        uint32_t elapsed = millis() - g_mb_async.slave_backoff[bo_idx].last_attempt_ms;
        if (elapsed < g_mb_async.slave_backoff[bo_idx].backoff_ms) {
          // Not enough time passed — skip this request, update cache to ERROR
          uint8_t cache_type = (uint8_t)req.type;
          if (req.type == MB_REQ_WRITE_COIL) cache_type = (uint8_t)MB_REQ_READ_COIL;
          if (req.type == MB_REQ_WRITE_HOLDING) cache_type = (uint8_t)MB_REQ_READ_HOLDING;
          mb_cache_entry_t *entry = mb_cache_find(req.slave_id, req.address, cache_type);
          if (entry) {
            portENTER_CRITICAL(&mb_cache_spinlock);
            entry->status = MB_CACHE_ERROR;
            entry->last_error = MB_TIMEOUT;
            portEXIT_CRITICAL(&mb_cache_spinlock);
          }
          g_mb_async.total_errors++;
          g_mb_async.total_timeouts++;
          continue;  // Skip to next request — no bus delay
        }
        g_mb_async.slave_backoff[bo_idx].last_attempt_ms = millis();
      }
    }

    mb_error_code_t err = MB_OK;
    st_value_t result;
    result.int_val = 0;

    switch (req.type) {
      case MB_REQ_READ_COIL: {
        bool coil_val = false;
        err = modbus_master_read_coil(req.slave_id, req.address, &coil_val);
        result.bool_val = coil_val;
        break;
      }
      case MB_REQ_READ_INPUT: {
        bool input_val = false;
        err = modbus_master_read_input(req.slave_id, req.address, &input_val);
        result.bool_val = input_val;
        break;
      }
      case MB_REQ_READ_HOLDING: {
        uint16_t reg_val = 0;
        err = modbus_master_read_holding(req.slave_id, req.address, &reg_val);
        result.int_val = (int32_t)reg_val;
        break;
      }
      case MB_REQ_READ_INPUT_REG: {
        uint16_t reg_val = 0;
        err = modbus_master_read_input_register(req.slave_id, req.address, &reg_val);
        result.int_val = (int32_t)reg_val;
        break;
      }
      case MB_REQ_WRITE_COIL: {
        err = modbus_master_write_coil(req.slave_id, req.address, req.write_value.bool_val);
        result.bool_val = (err == MB_OK);
        break;
      }
      case MB_REQ_WRITE_HOLDING: {
        err = modbus_master_write_holding(req.slave_id, req.address, (uint16_t)req.write_value.int_val);
        result.bool_val = (err == MB_OK);
        break;
      }
      case MB_REQ_READ_HOLDINGS: {
        // FC03 multi-register read — update individual cache entries
        uint8_t cnt = req.count;
        if (cnt == 0 || cnt > 16) { err = MB_INVALID_ADDRESS; break; }
        uint16_t regs[16];
        err = modbus_master_read_holdings(req.slave_id, req.address, cnt, regs);
        // Copy results to shared multi-reg buffer for MB_GET_REG()
        if (err == MB_OK) {
          memcpy(g_mb_multi_reg_buf, regs, cnt * sizeof(uint16_t));
        }
        // Update each individual cache entry
        for (uint8_t i = 0; i < cnt; i++) {
          mb_cache_entry_t *ce = mb_cache_get_or_create(req.slave_id, req.address + i, (uint8_t)MB_REQ_READ_HOLDING);
          if (ce) {
            portENTER_CRITICAL(&mb_cache_spinlock);
            if (err == MB_OK) {
              ce->value.int_val = (int32_t)regs[i];
              ce->status = MB_CACHE_VALID;
            } else {
              ce->status = MB_CACHE_ERROR;
            }
            ce->last_error = err;
            ce->last_update_ms = millis();
            ce->last_fc = (uint8_t)MB_REQ_READ_HOLDINGS;
            portEXIT_CRITICAL(&mb_cache_spinlock);
          }
        }
        result.bool_val = (err == MB_OK);
        break;
      }
      case MB_REQ_WRITE_HOLDINGS: {
        // FC16 multi-register write — read values from pool slot
        uint8_t cnt = req.count;
        if (cnt == 0 || cnt > 16) { err = MB_INVALID_ADDRESS; break; }
        uint16_t *write_vals = g_mb_multi_write_pool[req.multi_pool_slot];
        err = modbus_master_write_holdings(req.slave_id, req.address, cnt, write_vals);
        // Update cache entries with written values
        for (uint8_t i = 0; i < cnt; i++) {
          mb_cache_entry_t *ce = mb_cache_get_or_create(req.slave_id, req.address + i, (uint8_t)MB_REQ_READ_HOLDING);
          if (ce) {
            portENTER_CRITICAL(&mb_cache_spinlock);
            if (err == MB_OK) {
              ce->value.int_val = (int32_t)write_vals[i];
              ce->status = MB_CACHE_VALID;
            } else {
              ce->status = MB_CACHE_ERROR;
            }
            ce->last_error = err;
            ce->last_update_ms = millis();
            ce->last_fc = (uint8_t)MB_REQ_WRITE_HOLDINGS;
            portEXIT_CRITICAL(&mb_cache_spinlock);
          }
        }
        result.bool_val = (err == MB_OK);
        break;
      }
    }

    // Apply inter-frame delay (on background task — doesn't block ST Logic)
    // 0=auto: calculate t3.5 from baudrate per Modbus RTU spec
    {
      extern uint16_t modbus_effective_inter_frame(uint16_t, uint32_t);
      uint16_t eff_delay = modbus_effective_inter_frame(
        g_modbus_master_config.inter_frame_delay,
        g_modbus_master_config.baudrate);
      if (eff_delay > 0) {
        vTaskDelay(pdMS_TO_TICKS(eff_delay));
      }
    }

    // Multi-register ops handle their own cache updates — skip for them
    if (req.type == MB_REQ_READ_HOLDINGS || req.type == MB_REQ_WRITE_HOLDINGS) {
      goto skip_cache_update;
    }

    // Update cache (thread-safe) — single-register ops
    {
      uint8_t cache_type = (uint8_t)req.type;
      // For writes, update the corresponding read cache
      if (req.type == MB_REQ_WRITE_COIL) cache_type = (uint8_t)MB_REQ_READ_COIL;
      if (req.type == MB_REQ_WRITE_HOLDING) cache_type = (uint8_t)MB_REQ_READ_HOLDING;

      mb_cache_entry_t *entry = mb_cache_find(req.slave_id, req.address, cache_type);
      if (!entry && (req.type == MB_REQ_WRITE_COIL || req.type == MB_REQ_WRITE_HOLDING)) {
        // Write to address we've never read — create entry so UI can show it
        entry = mb_cache_get_or_create(req.slave_id, req.address, cache_type);
      }
      if (entry) {
        portENTER_CRITICAL(&mb_cache_spinlock);
        if (err == MB_OK) {
          entry->value = result;
          entry->status = MB_CACHE_VALID;
        } else {
          entry->status = MB_CACHE_ERROR;
        }
        entry->last_error = err;
        entry->last_update_ms = millis();
        entry->last_fc = (uint8_t)req.type;  // Track actual operation FC (FC01-FC06)
        portEXIT_CRITICAL(&mb_cache_spinlock);
      }
    }

    skip_cache_update:
    // Adaptive backoff: increase delay on timeout, decrease on success
    if (err == MB_TIMEOUT) {
      mb_backoff_on_timeout(req.slave_id);
    } else if (err == MB_OK) {
      mb_backoff_on_success(req.slave_id);
    }

    // Stats
    if (err != MB_OK) {
      g_mb_async.total_errors++;
      if (err == MB_TIMEOUT) g_mb_async.total_timeouts++;
    }
  }

  vTaskDelete(NULL);
}

/* ============================================================================
 * INIT / DEINIT
 * ============================================================================ */

void mb_async_init() {
  memset(&g_mb_async, 0, sizeof(g_mb_async));
  g_mb_async.stats_since_ms = millis();

  g_mb_async.pq_mutex = xSemaphoreCreateMutex();
  g_mb_async.pq_semaphore = xSemaphoreCreateCounting(MB_ASYNC_QUEUE_SIZE, 0);
  if (!g_mb_async.pq_mutex || !g_mb_async.pq_semaphore) {
    Serial.println("[MB_ASYNC] FEJL: Kunne ikke oprette queue sync primitives");
    return;
  }

  g_mb_async.task_running = true;

  BaseType_t ret = xTaskCreatePinnedToCore(
    mb_async_task_func,
    "mb_async",
    MB_ASYNC_TASK_STACK,
    NULL,
    MB_ASYNC_TASK_PRIO,
    &g_mb_async.task_handle,
    MB_ASYNC_TASK_CORE
  );

  if (ret != pdPASS) {
    Serial.println("[MB_ASYNC] FEJL: Kunne ikke starte background task");
    g_mb_async.task_running = false;
    return;
  }

  Serial.printf("[MB_ASYNC] Startet: Core %d, stack %d, prio-queue %d, cache max %d\n",
                MB_ASYNC_TASK_CORE, MB_ASYNC_TASK_STACK,
                MB_ASYNC_QUEUE_SIZE, MB_CACHE_MAX_ENTRIES);
}

void mb_async_deinit() {
  g_mb_async.task_running = false;
  if (g_mb_async.task_handle) {
    vTaskDelay(pdMS_TO_TICKS(200));  // Let task finish current operation
    g_mb_async.task_handle = NULL;
  }
  if (g_mb_async.pq_mutex) {
    vSemaphoreDelete(g_mb_async.pq_mutex);
    g_mb_async.pq_mutex = NULL;
  }
  if (g_mb_async.pq_semaphore) {
    vSemaphoreDelete(g_mb_async.pq_semaphore);
    g_mb_async.pq_semaphore = NULL;
  }
}

void mb_async_suspend() {
  if (g_mb_async.task_handle) {
    vTaskSuspend(g_mb_async.task_handle);
  }
}

void mb_async_resume() {
  if (g_mb_async.task_handle) {
    vTaskResume(g_mb_async.task_handle);
  }
}

const mb_async_state_t *mb_async_get_state() {
  return &g_mb_async;
}

void mb_async_reset_cache() {
  portENTER_CRITICAL(&mb_cache_spinlock);
  g_mb_async.entry_count = 0;
  memset(g_mb_async.entries, 0, sizeof(g_mb_async.entries));
  portEXIT_CRITICAL(&mb_cache_spinlock);
}

void mb_async_reset_stats() {
  portENTER_CRITICAL(&mb_cache_spinlock);
  g_mb_async.cache_hits = 0;
  g_mb_async.cache_misses = 0;
  g_mb_async.queue_full_count = 0;
  g_mb_async.priority_drops = 0;
  g_mb_async.queue_high_watermark = 0;
  g_mb_async.total_requests = 0;
  g_mb_async.total_errors = 0;
  g_mb_async.total_timeouts = 0;
  g_mb_async.stats_since_ms = millis();
  memset(g_mb_async.slave_backoff, 0, sizeof(g_mb_async.slave_backoff));
  portEXIT_CRITICAL(&mb_cache_spinlock);

  // Also reset modbus_master_config stats
  g_modbus_master_config.total_requests = 0;
  g_modbus_master_config.successful_requests = 0;
  g_modbus_master_config.timeout_errors = 0;
  g_modbus_master_config.crc_errors = 0;
  g_modbus_master_config.exception_errors = 0;
  g_modbus_master_config.stats_since_ms = millis();
}
