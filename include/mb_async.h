/**
 * @file mb_async.h
 * @brief Async Modbus Master — FreeRTOS background task with request cache
 *
 * Eliminates blocking in ST Logic by running Modbus UART I/O on a
 * dedicated FreeRTOS task (Core 0).  ST builtins queue requests and
 * read cached results — zero blocking, zero overruns.
 *
 * v7.7.0 (2026-03-31)
 */

#ifndef MB_ASYNC_H
#define MB_ASYNC_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "st_types.h"
#include "constants.h"

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

#define MB_CACHE_MAX_ENTRIES    32   // Max unique (slave, addr, fc) combinations
#define MB_ASYNC_QUEUE_SIZE    16   // Max pending requests in queue
#define MB_ASYNC_TASK_STACK  4096   // Background task stack (bytes)
#define MB_ASYNC_TASK_PRIO      3   // Task priority (below WiFi, above idle)
#define MB_ASYNC_TASK_CORE      0   // Run on Core 0 (main loop = Core 1)

/* ============================================================================
 * TYPES
 * ============================================================================ */

typedef enum {
  MB_REQ_READ_COIL = 1,       // FC01
  MB_REQ_READ_INPUT,          // FC02
  MB_REQ_READ_HOLDING,        // FC03
  MB_REQ_READ_INPUT_REG,      // FC04
  MB_REQ_WRITE_COIL,          // FC05
  MB_REQ_WRITE_HOLDING        // FC06
} mb_request_type_t;

typedef enum {
  MB_CACHE_EMPTY = 0,         // Never requested
  MB_CACHE_PENDING,           // Request queued, waiting for response
  MB_CACHE_VALID,             // Has valid cached value
  MB_CACHE_ERROR              // Last request failed
} mb_cache_status_t;

typedef struct {
  uint8_t  slave_id;          // 1-247
  uint16_t address;           // 0-65535
  uint8_t  req_type;          // mb_request_type_t (FC01-FC04 for reads)
} mb_cache_key_t;             // 4 bytes

typedef struct {
  mb_cache_key_t      key;
  st_value_t          value;          // Cached value (4 bytes)
  mb_cache_status_t   status;         // 1 byte
  int32_t             last_error;     // mb_error_code_t
  uint32_t            last_update_ms; // millis() at last update
} mb_cache_entry_t;                   // ~20 bytes

typedef struct {
  mb_request_type_t type;             // 1 byte
  uint8_t           slave_id;         // 1 byte
  uint16_t          address;          // 2 bytes
  st_value_t        write_value;      // 4 bytes (only for writes)
} mb_async_request_t;                 // 8 bytes

typedef struct {
  // Cache
  mb_cache_entry_t entries[MB_CACHE_MAX_ENTRIES];
  uint8_t          entry_count;

  // FreeRTOS
  QueueHandle_t    request_queue;
  TaskHandle_t     task_handle;
  volatile bool    task_running;

  // Statistics
  uint32_t cache_hits;
  uint32_t cache_misses;
  uint32_t queue_full_count;
  uint32_t total_requests;
  uint32_t total_errors;
  uint32_t total_timeouts;
} mb_async_state_t;

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

/**
 * @brief Initialize async Modbus master — create queue + start background task
 */
void mb_async_init();

/**
 * @brief Stop background task and cleanup
 */
void mb_async_deinit();

/**
 * @brief Suspend background task (for UART reconfiguration)
 */
void mb_async_suspend();

/**
 * @brief Resume background task after reconfiguration
 */
void mb_async_resume();

/**
 * @brief Find cache entry by key
 * @return Pointer to entry or NULL
 */
mb_cache_entry_t *mb_cache_find(uint8_t slave_id, uint16_t address, uint8_t req_type);

/**
 * @brief Find or create cache entry
 * @return Pointer to entry or NULL (cache full)
 */
mb_cache_entry_t *mb_cache_get_or_create(uint8_t slave_id, uint16_t address, uint8_t req_type);

/**
 * @brief Queue a read request (non-blocking, deduplicates)
 * @return true if queued or already pending
 */
bool mb_async_queue_read(mb_request_type_t type, uint8_t slave_id, uint16_t address);

/**
 * @brief Queue a write request (non-blocking, always queued)
 * @return true if queued successfully
 */
bool mb_async_queue_write(mb_request_type_t type, uint8_t slave_id, uint16_t address, st_value_t value);

/**
 * @brief Check if any requests are pending in queue
 * @return true if queue has pending items
 */
bool mb_async_is_busy();

/**
 * @brief Get async state for diagnostics (show modbus cache)
 */
const mb_async_state_t *mb_async_get_state();

/**
 * @brief Reset cache — clear all entries
 */
void mb_async_reset_cache();

/* Spinlock for thread-safe cache access between Core 0 and Core 1 */
extern portMUX_TYPE mb_cache_spinlock;

#endif // MB_ASYNC_H
