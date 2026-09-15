/**
 * @file modbus_expansion_async.h
 * @brief FEAT-410: Async Modbus Expansion Master — FreeRTOS background task
 * med kø/cache/adaptiv backoff, mod expansion-boardenes Modbus TCP data-plan.
 *
 * Dette er en TÆT, ORDRET PORT af mb_async.h/.cpp (se den fil for den fulde
 * designbegrundelse pr. lektion — hver af dem blev tilføjet som svar på en
 * konkret, observeret produktionsfejl, se BUGS_INDEX.md BUG-333/338/340) —
 * KUN transportlaget er udskiftet (modbus_expansion.cpp's Modbus TCP-klient
 * i stedet for modbus_master.cpp's UART), og nøglen er udvidet fra
 * (slave_id, address, fc) til (board, kanal, slave_id, address, fc), præcis
 * som anbefalet i EXPANSION_BOARD_DESIGN.md §5.1.1.
 *
 * v1-scope (FEAT-410): KUN enkelt-register-operationer (COIL/INPUT/HOLDING/
 * INPUT_REG læs, COIL/HOLDING skriv).
 *
 * v7.9.68.0: multi-register/coil WRITE tilføjet (MBX_WRITE_HOLDINGS/FC16,
 * MBX_WRITE_COILS/FC15), samme kompilator-specialcasing som MB_WRITE_HOLDINGS
 * (st_compiler.cpp's "FUNC(...) := array"-håndtering). Multi-READ er stadig
 * IKKE implementeret. Ligesom RTU-sidens MB_WRITE_HOLDINGS BYPASSER disse to
 * cache-laget helt — mbx_cache_entry_t har kun plads til én st_value_t, ikke
 * et register-RANGE, så der er ingen meningsfuld enkelt-nøgle at cache en
 * multi-write's bekræftelse under. Kun g_mbx_success afspejler resultatet.
 */

#ifndef MODBUS_EXPANSION_ASYNC_H
#define MODBUS_EXPANSION_ASYNC_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "st_types.h"
#include "constants.h"

#define MBX_ASYNC_CACHE_MAX_ENTRIES   48   // Se designnote: dimensioneret til "et par boards/kanaler", ikke det fulde 64-kanals-loft (billigt at hæve — RAM er ikke knapt)
#define MBX_ASYNC_QUEUE_SIZE          32
#define MBX_ASYNC_TASK_STACK        6144   // Lidt mere end mb_async's 4096 — Modbus TCP-transaktionen (modbus_expansion.cpp) bruger WiFiClient, som fylder lidt mere på stakken end UART-kaldene gjorde
#define MBX_ASYNC_TASK_PRIO             3
#define MBX_ASYNC_TASK_CORE             0
#define MBX_SLAVE_BACKOFF_MAX          16   // (board,kanal,slave)-triplets, ikke kun slave — se .cpp
#define MBX_BACKOFF_INITIAL_MS         50
#define MBX_BACKOFF_MAX_MS           2000
#define MBX_BACKOFF_DECAY_MS          100
#define MBX_PENDING_STALE_FACTOR        5   // × MBX_DEFAULT_TIMEOUT_MS (modbus_expansion.cpp)
#define MBX_PENDING_STALE_MIN_MS      3000
#define MBX_PENDING_SWEEP_INTERVAL_MS 1000
#define MBX_MULTI_POOL_SIZE  4   // Ring-buffer slots for FC15/FC16 write values (mirrors MB_MULTI_REG_POOL_SIZE)

typedef enum {
  MBX_REQ_READ_COIL = 1,
  MBX_REQ_READ_INPUT,
  MBX_REQ_READ_HOLDING,
  MBX_REQ_READ_INPUT_REG,
  MBX_REQ_WRITE_COIL,
  MBX_REQ_WRITE_HOLDING,
  MBX_REQ_WRITE_HOLDINGS,   // FC16 multi-register (v7.9.68.0)
  MBX_REQ_WRITE_COILS       // FC15 multi-coil (v7.9.68.0)
} mbx_request_type_t;

typedef enum {
  MBX_PRIO_WRITE        = 0,
  MBX_PRIO_READ_FRESH    = 1,
  MBX_PRIO_READ_REFRESH  = 2
} mbx_request_priority_t;

typedef enum {
  MBX_CACHE_EMPTY = 0,
  MBX_CACHE_PENDING,
  MBX_CACHE_VALID,
  MBX_CACHE_ERROR
} mbx_cache_status_t;

typedef struct {
  uint8_t  board;    // 1-8
  uint8_t  channel;  // 1-8
  uint8_t  slave_id; // 1-247
  uint16_t address;
  uint8_t  req_type; // mbx_request_type_t
} mbx_cache_key_t;

typedef struct {
  mbx_cache_key_t   key;
  st_value_t        value;
  mbx_cache_status_t status;
  int32_t           last_error;
  uint32_t          last_update_ms;
  uint32_t          pending_since_ms;
} mbx_cache_entry_t;

typedef struct {
  mbx_request_type_t type;
  uint8_t   board;
  uint8_t   channel;
  uint8_t   slave_id;
  uint16_t  address;
  st_value_t write_value;      // single-value writes only (MBX_REQ_WRITE_COIL/HOLDING)
  uint8_t   count;              // register/coil count for multi writes (v7.9.68.0)
  uint8_t   multi_pool_slot;    // index into g_mbx_multi_write_*_pool (v7.9.68.0)
  uint8_t   priority;
  uint16_t  insert_seq;
} mbx_async_request_t;

typedef struct {
  mbx_cache_entry_t entries[MBX_ASYNC_CACHE_MAX_ENTRIES];
  uint16_t          entry_count;

  mbx_async_request_t pq_buf[MBX_ASYNC_QUEUE_SIZE];
  volatile uint8_t     pq_count;
  uint16_t             pq_seq;

  SemaphoreHandle_t pq_mutex;
  SemaphoreHandle_t pq_semaphore;
  TaskHandle_t      task_handle;
  volatile bool     task_running;
  volatile bool     paused;

  struct {
    uint8_t  board;         // 0 = unused slot
    uint8_t  channel;
    uint8_t  slave_id;
    uint16_t backoff_ms;
    uint16_t timeout_count;
    uint16_t success_count;
    uint32_t last_attempt_ms;
  } slave_backoff[MBX_SLAVE_BACKOFF_MAX];

  uint32_t cache_hits;
  uint32_t cache_misses;
  uint32_t queue_full_count;
  uint32_t priority_drops;
  uint8_t  queue_high_watermark;
  uint32_t total_requests;
  uint32_t total_errors;
  uint32_t total_timeouts;
  uint32_t stats_since_ms;
  uint32_t stale_pending_recovered;
} mbx_async_state_t;

void modbus_expansion_async_init();
void modbus_expansion_async_deinit();
void modbus_expansion_async_pause();
void modbus_expansion_async_unpause();
bool modbus_expansion_async_is_paused();

mbx_cache_entry_t *mbx_cache_find(uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address, uint8_t req_type);
mbx_cache_entry_t *mbx_cache_get_or_create(uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address, uint8_t req_type);

bool modbus_expansion_async_queue_read(mbx_request_type_t type, uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address);
bool modbus_expansion_async_queue_write(mbx_request_type_t type, uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address, st_value_t value);

// v7.9.68.0: multi-register write (FC16) / multi-coil write (FC15) — bypass the
// single-address cache entirely (see file header design note above).
bool modbus_expansion_async_queue_write_multi_holdings(uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address, uint8_t count, const uint16_t *values);
bool modbus_expansion_async_queue_write_multi_coils(uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address, uint8_t count, const bool *values);

bool modbus_expansion_async_is_busy();
uint8_t modbus_expansion_async_queue_depth();
const mbx_async_state_t *modbus_expansion_async_get_state();
void modbus_expansion_async_reset_cache();
void modbus_expansion_async_reset_stats();

extern mbx_async_state_t g_mbx_async;
extern portMUX_TYPE mbx_cache_spinlock;

// Ring-buffer pools for FC16/FC15 multi write values (v7.9.68.0) — same
// ring-buffer-decoupled-from-the-VM's-transient-scratch-buffer rationale as
// mb_async.h's g_mb_multi_write_pool (a write may sit queued across ST cycles).
extern uint16_t g_mbx_multi_write_reg_pool[MBX_MULTI_POOL_SIZE][16];
extern volatile uint8_t g_mbx_multi_write_reg_next;
extern bool g_mbx_multi_write_coil_pool[MBX_MULTI_POOL_SIZE][16];
extern volatile uint8_t g_mbx_multi_write_coil_next;

#endif // MODBUS_EXPANSION_ASYNC_H
