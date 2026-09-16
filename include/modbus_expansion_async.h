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
// BUG-417: modbus_expansion_async.cpp startede historisk (som mb_async.cpp)
// KUN 1 baggrundstask, der behandler forespørgsler strengt seriel — korrekt
// for mb_async.cpp (kun ÉN fysisk RS485-bus, kan fysisk ikke sende to rammer
// samtidig), men FORKERT her: hvert board har FLERE elektrisk uafhængige
// kanaler (A/B, potentielt flere), som sagtens kan afvikles samtidig. Med
// kun 1 task blokerede en enkelt time'ende kanal (500+ ms) ALLE andre
// boards/kanaler fra at komme til, uanset at de reelt er separate buse.
// Fix: en lille FAST pulje af baggrundstasks (ikke én pr. teoretisk
// kanal-slot — op til 8 boards × 8 kanaler ville være 64 tasks × 6KB stak =
// urealistisk meget RAM for et scenarie ingen reelt har) + en "in-flight"-
// tabel (se mbx_async_state_t.inflight nedenfor) der forhindrer to workers i
// at ramme SAMME (board,kanal) samtidig — det ville stadig være forkert,
// da EN enkelt fysisk bus godt kan collidere med sig selv. 4 workers dækker
// realistiske opsætninger (typisk 1-2 boards × 2 kanaler) komfortabelt.
//
// RUNDTUR (v7.9.68.11→12): dette blev sat MIDLERTIDIGT til 1 igen, fordi
// hver ekstra worker koster en ~6KB task-stak fra INTERN heap, og et bruger-
// rapporteret "Insufficient heap for AST pool" viste at ES32D26's interne
// heap var for tæt presset til det — ST-compilerens AST-node-pool
// (st_parser.cpp) krævede dengang op til ~24-32KB SAMMENHÆNGENDE intern
// heap for overhovedet at allokere, og konkurrerede direkte med disse
// worker-stakke om den samme knappe interne hukommelse. Den RIGTIGE fix
// (BUG-418) var ikke at fjerne parallelismen, men at flytte AST-poolen til
// PSRAM (~4MB, rigeligt, og compilering er en enkeltstående, ikke-ISR,
// UI-udløst handling — samme kategori som ST Logics eksisterende PSRAM-
// baserede kildekode-pool) — se ast_pool_init()/ast_pool_init_with_size()
// i st_parser.cpp. Efter den fix konkurrerer AST-poolen (den suverænt
// største enkeltstående interne allokering compileren lavede) ikke længere
// om intern RAM overhovedet, og disse 4 workers' stakke er igen trygge.
// BUG-419: 4 workers bragte HELE kø-motoren i en permanent deadlock ved live
// test — "Requests total" frøs helt, mens "queue full drops" fortsatte med
// at stige 1:1 med hver ny ST-forespørgsel (ingen worker behandlede køen
// længere, men ST blev ved med forgæves at forsøge). Overlevede Stop/Start/
// Reinit (global motor-state) — krævede fuld enheds-genstart. Rodårsagen
// blev sporet til `modbus_expansion.cpp`s `g_mbx_conn[]`-forbindelsespulje:
// skrevet under antagelsen om ÉN kaldende task, INGEN egen synkronisering —
// BUG-417's in-flight-tabel beskyttede kun KØ-laget, ikke selve
// forbindelses-/transport-laget nedenunder (find-eller-opret-slot var et
// klassisk TOCTOU-race mellem samtidige workers på FORSKELLIGE kanaler).
// **Fix** (samme BUG-419): `g_mbx_conn_mutex` gør find/claim/evict atomisk
// (`modbus_expansion.cpp`), og eviction springer nu bevidst forbindelser
// over der er in-flight lige nu (kalder denne fils nye
// `modbus_expansion_async_is_channel_inflight()`) — ingen worker kan længere
// få tæppet revet væk under sig af en anden. Sat til 2 som en FORSIGTIG
// genindførsel, brugeren bekræftede stabil drift (Requests total blev ved
// med at stige, ingen fastfrysning) — hævet til 4 igen (v7.9.68.15) på
// brugerens udtrykkelige ønske om at teste den fulde værdi. Se stadig
// `show modbus-expansion queue`s "Requests total" som lakmustest hvis
// mistanke om gentagelse opstår — den må ALDRIG fryse mens "queue full
// drops"/"cache hits" fortsætter med at stige.
#define MBX_ASYNC_WORKER_COUNT          4
#define MBX_SLAVE_BACKOFF_MAX          16   // (board,kanal,slave)-triplets, ikke kun slave — se .cpp
#define MBX_BACKOFF_INITIAL_MS         50
#define MBX_BACKOFF_MAX_MS           2000
#define MBX_BACKOFF_DECAY_MS          100
#define MBX_PENDING_STALE_FACTOR        5   // × MBX_DEFAULT_TIMEOUT_MS (modbus_expansion.cpp)
#define MBX_PENDING_STALE_MIN_MS      3000
#define MBX_PENDING_SWEEP_INTERVAL_MS 1000
#define MBX_MULTI_POOL_SIZE  4   // Ring-buffer slots for FC15/FC16 write values (mirrors MB_MULTI_REG_POOL_SIZE)
// BUG-416 (samme rodårsag som mb_async.h's MB_STARVATION_AGE_MS — se den
// kommentar for den fulde forklaring): en REFRESH-forespørgsel (klienten har
// allerede en cachet værdi) er kun FRESH-prioritet FØR dens første succes —
// en (board,kanal,slave) der ALDRIG svarer, forbliver derfor for evigt på
// FRESH og vinder enhver prioritets-uafgørelse mod enhver ANDEN kanals
// REFRESH-læsninger, uendeligt. En REFRESH, der har ventet længere end dette,
// behandles som FRESH ved valg — dens (langt ældre) insert_seq vinder så
// uafgørelser mod nyligt ankomne FRESH-forespørgsler.
#define MBX_STARVATION_AGE_MS  2000

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
  uint32_t  enqueued_ms;         // BUG-416: millis() at insert, for starvation aging
} mbx_async_request_t;

typedef struct {
  mbx_cache_entry_t entries[MBX_ASYNC_CACHE_MAX_ENTRIES];
  uint16_t          entry_count;

  mbx_async_request_t pq_buf[MBX_ASYNC_QUEUE_SIZE];
  volatile uint8_t     pq_count;
  uint16_t             pq_seq;

  SemaphoreHandle_t pq_mutex;
  SemaphoreHandle_t pq_semaphore;
  TaskHandle_t      task_handles[MBX_ASYNC_WORKER_COUNT];  // BUG-417: was a single task_handle
  volatile bool     task_running;
  volatile bool     paused;

  // BUG-417: (board,kanal)-par der lige nu behandles af en af de
  // MBX_ASYNC_WORKER_COUNT workers — forhindrer to workers i at ramme samme
  // fysiske bus samtidig. Beskyttet af pq_mutex (samme lås som selve køen,
  // da mærkning sker atomisk sammen med dequeue, se mbx_pq_dequeue()).
  struct {
    uint8_t board;    // 0 = ledig plads
    uint8_t channel;
  } inflight[MBX_ASYNC_WORKER_COUNT];

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

// BUG-419: lets modbus_expansion.cpp's connection-slot eviction check
// whether a (board,kanal) is actively being serviced by a worker right now,
// so it never evicts (and thereby corrupts) a connection that's mid-
// transaction — see modbus_expansion.cpp's mbx_get_or_evict_slot().
bool modbus_expansion_async_is_channel_inflight(uint8_t board, uint8_t channel);
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
