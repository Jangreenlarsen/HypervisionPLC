/**
 * @file mb_activity_log.h
 * @brief Unified Modbus activity log — Master + Slave, RAM-only (FEAT-149)
 *
 * A true wire-level monitor of Modbus traffic on this device: every
 * completed request is logged from the lowest common point on each side
 * (modbus_master_send_request() for Master, modbus_server_loop()'s
 * PROCESS state for Slave) so it captures ALL traffic regardless of
 * caller — ST Logic, CLI, dashboard mini-form, or an external master
 * polling this device.
 *
 * RAM-only ring buffer, not persisted to NVS/flash — resets on reboot.
 */

#ifndef MB_ACTIVITY_LOG_H
#define MB_ACTIVITY_LOG_H

#include <Arduino.h>

/* FEAT-153: haevet 40 -> 500. Bufferen ligger i PSRAM (ES32D26/WROVER har
 * 4 MB, naesten alt frit), saa de ~10 KB koster reelt ingenting og tager
 * ikke af den knappe interne DRAM. Falder tilbage til almindelig heap hvis
 * PSRAM ikke er tilgaengelig.
 *
 * To ting goer den stoerrelse ufarlig:
 *  - JSON-svaret sendes i chunks, ikke samlet i én stor buffer. (Det var
 *    netop en for lille fast buffer der gav BUG-332, hvor svaret blev
 *    afkortet midt i et objekt og dashboardet holdt op med at opdatere.)
 *  - GET understoetter ?limit=N, saa dashboardets polling hver 3. sekund
 *    kun henter de nyeste N poster. Uden det ville 500 poster (~70 KB) over
 *    WiFi 20 gange i minuttet blive et problem i sig selv.
 */
#define MB_ACTIVITY_LOG_MAX  500  // RAM-only ring buffer size (Master + Slave combined)

typedef enum {
  MB_ACTIVITY_ROLE_MASTER = 0,   // We initiated this request to another device
  MB_ACTIVITY_ROLE_SLAVE  = 1    // Another device (master) sent this request to us
} mb_activity_role_t;

/* Who originated a MASTER-role request (irrelevant/unused for SLAVE-role
 * entries, which are always MB_SRC_EXTERNAL by definition). Lets the
 * dashboard correlate a wire-level transaction with the high-level command
 * that caused it, e.g. "this READ_HOLDING to slave 1 addr 10 came from
 * an ST Logic program". */
typedef enum {
  MB_SRC_UNKNOWN   = 0,
  MB_SRC_ST_LOGIC  = 1,   // ST Logic program's MB_* builtin
  MB_SRC_CLI       = 2,   // CLI `mb read`/`mb write`/`mb scan`
  MB_SRC_DASHBOARD = 3,   // Dashboard manual Read/Write mini-form (/api/modbus/master/rw)
  MB_SRC_EXTERNAL  = 4    // SLAVE role: request came from an external master on the bus
} mb_activity_source_t;

typedef struct {
  uint32_t timestamp_ms;   // millis() at completion (altid sat — monotont, uafhaengigt af NTP)
  /* FEAT-153: rigtig vaegur-tid naar NTP er synkroniseret, ellers 0.
   * millis() bevares ved siden af, fordi den er monoton og fungerer fra
   * boot — ogsaa foer NTP naar at synkronisere, og hvis uret senere
   * justeres. Dashboardet viser epoch_s naar den er sat, ellers den
   * relative millis-tid. */
  uint32_t epoch_s;        // Unix-tid (sekunder), 0 = NTP ikke synkroniseret
  uint8_t  role;           // mb_activity_role_t
  uint8_t  source;         // mb_activity_source_t
  uint8_t  slave_id;       // MASTER: slave we talked to. SLAVE: our own slave_id addressed (0 = broadcast)
  uint8_t  function_code;  // Raw Modbus FC (1,2,3,4,5,6,15,16)
  uint16_t address;
  uint8_t  count;          // register/coil count (1 for single ops)
  int32_t  value;          // write value, or (single) read result; 0 for multi-ops
  int16_t  error;          // 0 = OK. MASTER: mb_error_code_t. SLAVE: Modbus exception code (or -1 generic fail)
} mb_activity_entry_t;      // ~17 bytes

/**
 * @brief Mark the source that will be attributed to the NEXT request queued
 * via mb_async_queue_read/write/_multi() — the actual send happens later on
 * a background task, so the source must be snapshotted into the queued
 * request itself (mb_async_request_t.source) rather than read at send time.
 * Called once by st_builtin_modbus.cpp (MB_SRC_ST_LOGIC) and once by the
 * dashboard's /api/modbus/master/rw handler (MB_SRC_DASHBOARD).
 */
extern uint8_t g_mb_activity_next_source;

/**
 * @brief The source of the transaction currently being sent on the wire.
 * Set by mb_async_task_func() (from the dequeued request's snapshotted
 * .source) or directly by CLI `mb read`/`mb write`/`mb scan` (synchronous,
 * no queue involved). Read by modbus_master_send_request() when logging.
 * Safe as a plain global: the Modbus Master UART is a single serialized
 * resource — only one transaction is ever in flight at a time.
 */
extern uint8_t g_mb_activity_current_source;

/**
 * @brief Reset the log (called once at boot)
 */
void mb_activity_log_init(void);

/**
 * @brief Append one completed request to the log (thread-safe, cross-core)
 */
void mb_activity_log_add(mb_activity_role_t role, uint8_t source, uint8_t slave_id, uint8_t function_code,
                          uint16_t address, uint8_t count, int32_t value, int16_t error);

/**
 * @brief Clear all entries (e.g. "reset log" button)
 */
void mb_activity_log_clear(void);

/**
 * @brief FEAT-153: start/stop logning uden at rydde det allerede opsamlede.
 *
 * Naar logningen er stoppet ignorerer mb_activity_log_add() nye poster —
 * eksisterende indhold bevares, saa man kan fryse billedet og naa at laese
 * (eller eksportere) det uden at det ruller videre. Ringbufferen er
 * RAM-only, saa tilstanden nulstilles til "koerer" ved reboot.
 */
void mb_activity_log_set_enabled(bool enabled);

/**
 * @brief Er logningen aktiv? (til dashboard/API-visning)
 */
bool mb_activity_log_is_enabled(void);

/**
 * @brief Number of valid entries currently held (0..MB_ACTIVITY_LOG_MAX)
 * @note uint16_t siden FEAT-153 — loggen rummer nu 500 poster, ikke 40.
 */
uint16_t mb_activity_log_count(void);

/**
 * @brief Read one entry, oldest-first (idx 0 = oldest, count()-1 = newest)
 * @return true if idx valid and *out was filled
 */
bool mb_activity_log_get(uint16_t idx, mb_activity_entry_t *out);

#endif // MB_ACTIVITY_LOG_H
