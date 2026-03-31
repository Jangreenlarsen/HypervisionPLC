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

/* ============================================================================
 * GLOBALS
 * ============================================================================ */

mb_async_state_t g_mb_async = {0};
portMUX_TYPE mb_cache_spinlock = portMUX_INITIALIZER_UNLOCKED;

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
  if (e) return e;

  // Create new if space available
  if (g_mb_async.entry_count >= MB_CACHE_MAX_ENTRIES) {
    return NULL;  // Cache full
  }

  e = &g_mb_async.entries[g_mb_async.entry_count++];
  memset(e, 0, sizeof(mb_cache_entry_t));
  e->key.slave_id = slave_id;
  e->key.address = address;
  e->key.req_type = req_type;
  e->status = MB_CACHE_EMPTY;
  return e;
}

/* ============================================================================
 * QUEUE FUNCTIONS
 * ============================================================================ */

bool mb_async_queue_read(mb_request_type_t type, uint8_t slave_id, uint16_t address) {
  // Check cache — if already PENDING, skip (deduplication)
  mb_cache_entry_t *entry = mb_cache_find(slave_id, address, (uint8_t)type);
  if (entry && entry->status == MB_CACHE_PENDING) {
    return true;  // Already queued
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

  // Send to queue (non-blocking)
  mb_async_request_t req;
  req.type = type;
  req.slave_id = slave_id;
  req.address = address;
  req.write_value.int_val = 0;

  if (xQueueSend(g_mb_async.request_queue, &req, 0) != pdTRUE) {
    g_mb_async.queue_full_count++;
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
  // Writes are always queued (no deduplication — each write matters)
  mb_async_request_t req;
  req.type = type;
  req.slave_id = slave_id;
  req.address = address;
  req.write_value = value;

  if (xQueueSend(g_mb_async.request_queue, &req, 0) != pdTRUE) {
    g_mb_async.queue_full_count++;
    return false;
  }

  return true;
}

bool mb_async_is_busy() {
  if (!g_mb_async.request_queue) return false;
  return uxQueueMessagesWaiting(g_mb_async.request_queue) > 0;
}

/* ============================================================================
 * BACKGROUND TASK
 * ============================================================================ */

static void mb_async_task_func(void *pvParameters) {
  mb_async_request_t req;

  while (g_mb_async.task_running) {
    // Block max 100ms waiting for request (allows clean shutdown)
    if (xQueueReceive(g_mb_async.request_queue, &req, pdMS_TO_TICKS(100)) != pdTRUE) {
      continue;
    }

    g_mb_async.total_requests++;

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
    }

    // Apply inter-frame delay (on background task — doesn't block ST Logic)
    if (g_modbus_master_config.inter_frame_delay > 0) {
      vTaskDelay(pdMS_TO_TICKS(g_modbus_master_config.inter_frame_delay));
    }

    // Update cache (thread-safe)
    uint8_t cache_type = (uint8_t)req.type;
    // For writes, update the corresponding read cache
    if (req.type == MB_REQ_WRITE_COIL) cache_type = (uint8_t)MB_REQ_READ_COIL;
    if (req.type == MB_REQ_WRITE_HOLDING) cache_type = (uint8_t)MB_REQ_READ_HOLDING;

    mb_cache_entry_t *entry = mb_cache_find(req.slave_id, req.address, cache_type);
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
      portEXIT_CRITICAL(&mb_cache_spinlock);
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

  g_mb_async.request_queue = xQueueCreate(MB_ASYNC_QUEUE_SIZE, sizeof(mb_async_request_t));
  if (!g_mb_async.request_queue) {
    Serial.println("[MB_ASYNC] FEJL: Kunne ikke oprette request queue");
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

  Serial.printf("[MB_ASYNC] Startet: Core %d, stack %d, queue %d, cache max %d\n",
                MB_ASYNC_TASK_CORE, MB_ASYNC_TASK_STACK,
                MB_ASYNC_QUEUE_SIZE, MB_CACHE_MAX_ENTRIES);
}

void mb_async_deinit() {
  g_mb_async.task_running = false;
  if (g_mb_async.task_handle) {
    vTaskDelay(pdMS_TO_TICKS(200));  // Let task finish current operation
    g_mb_async.task_handle = NULL;
  }
  if (g_mb_async.request_queue) {
    vQueueDelete(g_mb_async.request_queue);
    g_mb_async.request_queue = NULL;
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
