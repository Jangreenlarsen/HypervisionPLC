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

#define MB_CACHE_MAX_ENTRIES_DEFAULT  32   // Default max unique (slave, addr, fc) combinations
#define MB_ASYNC_QUEUE_SIZE_DEFAULT  16   // Default max pending requests in queue
#define MB_CACHE_MAX_ENTRIES    32   // Compile-time max (array size)
#define MB_ASYNC_QUEUE_SIZE    32   // Compile-time max (array size for priority queue)
#define MB_ASYNC_TASK_STACK  4096   // Background task stack (bytes)
#define MB_ASYNC_TASK_PRIO      3   // Task priority (below WiFi, above idle)
#define MB_ASYNC_TASK_CORE      0   // Run on Core 0 (main loop = Core 1)
#define MB_MULTI_REG_POOL_SIZE  4   // Ring-buffer slots for FC16 write values
#define MB_SLAVE_BACKOFF_MAX    8   // Max tracked slaves for adaptive backoff
#define MB_BACKOFF_INITIAL_MS  50   // Initial extra delay after first timeout
#define MB_BACKOFF_MAX_MS    2000   // Max backoff delay (2 seconds)
#define MB_BACKOFF_DECAY_MS   100   // Reduce backoff by this much on each success

/* ============================================================================
 * TYPES
 * ============================================================================ */

typedef enum {
  MB_REQ_READ_COIL = 1,       // FC01
  MB_REQ_READ_INPUT,          // FC02
  MB_REQ_READ_HOLDING,        // FC03
  MB_REQ_READ_INPUT_REG,      // FC04
  MB_REQ_WRITE_COIL,          // FC05
  MB_REQ_WRITE_HOLDING,       // FC06
  MB_REQ_READ_HOLDINGS,       // FC03 multi-register (v7.9.2)
  MB_REQ_WRITE_HOLDINGS       // FC16 multi-register (v7.9.2)
} mb_request_type_t;

/* Priority levels for queue ordering (lower value = higher priority) */
typedef enum {
  MB_PRIO_WRITE        = 0,   // Writes always first (FC05/FC06/FC16)
  MB_PRIO_READ_FRESH   = 1,   // First read — no cached value yet
  MB_PRIO_READ_REFRESH = 2    // Cache refresh — client already has a value
} mb_request_priority_t;

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
  uint8_t             last_fc;        // Actual FC of last completed op (1-6)
} mb_cache_entry_t;                   // ~21 bytes

typedef struct {
  mb_request_type_t type;             // 1 byte
  uint8_t           slave_id;         // 1 byte
  uint16_t          address;          // 2 bytes
  st_value_t        write_value;      // 4 bytes (only for single writes)
  uint8_t           count;            // register count for multi-register ops (v7.9.2)
  uint8_t           multi_pool_slot;  // index into g_mb_multi_write_pool (v7.9.3: was multi_regs[16])
  uint8_t           priority;         // mb_request_priority_t (v7.9.7: priority queue)
  uint16_t          insert_seq;       // insertion order for FIFO within same priority
} mb_async_request_t;                 // 13 bytes

typedef struct {
  // Cache
  mb_cache_entry_t entries[MB_CACHE_MAX_ENTRIES];
  uint8_t          entry_count;

  // Priority queue (replaces FreeRTOS FIFO queue — v7.9.7)
  mb_async_request_t pq_buf[MB_ASYNC_QUEUE_SIZE]; // Ring buffer for requests
  volatile uint8_t   pq_count;                     // Current items in queue
  uint16_t           pq_seq;                        // Monotonic insert counter

  // FreeRTOS synchronization
  SemaphoreHandle_t  pq_mutex;       // Mutex for queue access
  SemaphoreHandle_t  pq_semaphore;   // Counting semaphore (signals new items)
  TaskHandle_t       task_handle;
  volatile bool      task_running;

  // Per-slave adaptive backoff (v7.9.5: non-blocking skip instead of vTaskDelay)
  struct {
    uint8_t  slave_id;           // 0 = unused slot
    uint16_t backoff_ms;         // Current cooldown for this slave
    uint16_t timeout_count;      // Consecutive timeouts
    uint16_t success_count;      // Consecutive successes (for decay)
    uint32_t last_attempt_ms;    // millis() of last actual bus attempt
  } slave_backoff[MB_SLAVE_BACKOFF_MAX];

  // Statistics
  uint32_t cache_hits;
  uint32_t cache_misses;
  uint32_t queue_full_count;
  uint32_t priority_drops;        // Requests dropped by priority eviction (v7.9.7)
  uint8_t  queue_high_watermark;  // Max queue depth seen (v7.9.7)
  uint32_t total_requests;
  uint32_t total_errors;
  uint32_t total_timeouts;
  uint32_t stats_since_ms;        // millis() at last stats reset (v7.9.3.2)
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
 * @brief Queue a multi-register read (FC03 with count > 1)
 * Updates individual cache entries for each address in range.
 * @return true if queued successfully
 */
bool mb_async_queue_read_multi(uint8_t slave_id, uint16_t address, uint8_t count);

/**
 * @brief Queue a multi-register write (FC16)
 * Writes values from provided buffer via FC16.
 * @param values Array of uint16_t values to write (count entries)
 * @return true if queued successfully
 */
bool mb_async_queue_write_multi(uint8_t slave_id, uint16_t address, uint8_t count, const uint16_t *values);

/**
 * @brief Check if any requests are pending in queue
 * @return true if queue has pending items
 */
bool mb_async_is_busy();

/**
 * @brief Get current queue depth
 */
uint8_t mb_async_queue_depth();

/**
 * @brief Get async state for diagnostics (show modbus cache)
 */
const mb_async_state_t *mb_async_get_state();

/**
 * @brief Reset cache — clear all entries
 */
void mb_async_reset_cache();

/**
 * @brief Reset statistics counters (cache hits/misses, requests, errors, backoff)
 */
void mb_async_reset_stats();

/* Global async state */
extern mb_async_state_t g_mb_async;

/* Spinlock for thread-safe cache access between Core 0 and Core 1 */
extern portMUX_TYPE mb_cache_spinlock;

/* Ring-buffer pool for FC16 multi-register write values (v7.9.3)
 * 4 slots × 16 regs × 2 bytes = 128 bytes (was 32 bytes × 16 queue items = 512 bytes inline) */
extern uint16_t g_mb_multi_write_pool[MB_MULTI_REG_POOL_SIZE][16];
extern volatile uint8_t g_mb_multi_write_next;  // Next free slot (0-3, wraps)

#endif // MB_ASYNC_H
