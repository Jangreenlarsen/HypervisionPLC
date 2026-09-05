/**
 * @file mb_activity_log.cpp
 * @brief Unified Modbus activity log implementation (FEAT-149)
 */

#include "mb_activity_log.h"
#include "ntp_driver.h"
#include <freertos/FreeRTOS.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <stdlib.h>

/* FEAT-153: bufferen ligger i PSRAM naar den findes (ES32D26/WROVER har
 * 4 MB). 500 poster fylder ~11 KB — ligegyldigt i PSRAM, men en maerkbar
 * bid af den interne DRAM, som er den knappe ressource. Falder tilbage til
 * almindelig heap hvis PSRAM mangler, og til sidst deaktiveres loggen helt
 * (log_capacity = 0) frem for at risikere en NULL-dereference. */
static mb_activity_entry_t *activity_log = NULL;
static uint16_t log_capacity = 0;
static uint16_t activity_log_head = 0;   // Next write position
static uint16_t activity_log_count = 0;  // Total entries (max log_capacity)

uint8_t g_mb_activity_next_source = MB_SRC_UNKNOWN;
uint8_t g_mb_activity_current_source = MB_SRC_UNKNOWN;

// Master (Core 0 async task) and Slave (Core 1 main loop) can both log
// concurrently — guard the shared ring buffer with its own spinlock.
static portMUX_TYPE activity_log_spinlock = portMUX_INITIALIZER_UNLOCKED;

void mb_activity_log_init(void) {
  // FEAT-153: allokér én gang. Kaldes ogsaa af mb_activity_log_clear(),
  // som blot skal nulstille indholdet — ikke reallokere.
  if (!activity_log) {
    size_t bytes = (size_t)MB_ACTIVITY_LOG_MAX * sizeof(mb_activity_entry_t);

    // Foerst PSRAM (fylder intet dér), ellers almindelig heap
    activity_log = (mb_activity_entry_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (!activity_log) {
      activity_log = (mb_activity_entry_t *)malloc(bytes);
    }
    log_capacity = activity_log ? MB_ACTIVITY_LOG_MAX : 0;
  }

  if (!activity_log) {
    return;  // Ingen buffer — loggen er inaktiv, men intet crasher
  }

  portENTER_CRITICAL(&activity_log_spinlock);
  memset(activity_log, 0, (size_t)log_capacity * sizeof(mb_activity_entry_t));
  activity_log_head = 0;
  activity_log_count = 0;
  portEXIT_CRITICAL(&activity_log_spinlock);
}

/* FEAT-153: start/stop. Bevidst IKKE nulstillet af mb_activity_log_init()
 * (som ogsaa er "ryd log"-knappen) — at rydde loggen mens den er sat paa
 * pause skal ikke starte den igen bag om brugeren. Kun en reboot, eller et
 * eksplicit kald, aendrer tilstanden. */
static volatile bool activity_log_enabled = true;

void mb_activity_log_add(mb_activity_role_t role, uint8_t source, uint8_t slave_id, uint8_t function_code,
                          uint16_t address, uint8_t count, int32_t value, int16_t error) {
  if (!activity_log_enabled || !activity_log) {
    return;  // FEAT-153: logning stoppet (eller ingen buffer) — bevar det opsamlede
  }

  /* FEAT-153: haent vaegur-tiden UDEN for det kritiske afsnit — ntp_driver
   * kalder ind i tidssystemet, og et spinlock skal holdes saa kort som
   * overhovedet muligt (det blokerer den anden core). */
  uint32_t epoch = 0;
  if (ntp_driver_is_synced()) {
    epoch = (uint32_t)ntp_driver_get_epoch();
  }

  portENTER_CRITICAL(&activity_log_spinlock);
  mb_activity_entry_t *e = &activity_log[activity_log_head];
  e->timestamp_ms = millis();
  e->epoch_s = epoch;
  e->role = (uint8_t)role;
  e->source = source;
  e->slave_id = slave_id;
  e->function_code = function_code;
  e->address = address;
  e->count = count;
  e->value = value;
  e->error = error;
  activity_log_head = (activity_log_head + 1) % log_capacity;
  if (activity_log_count < log_capacity) {
    activity_log_count++;
  }
  portEXIT_CRITICAL(&activity_log_spinlock);
}

void mb_activity_log_clear(void) {
  mb_activity_log_init();
}

uint16_t mb_activity_log_count(void) {
  return activity_log_count;
}

void mb_activity_log_set_enabled(bool enabled) {
  activity_log_enabled = enabled;
}

bool mb_activity_log_is_enabled(void) {
  return activity_log_enabled;
}

bool mb_activity_log_get(uint16_t idx, mb_activity_entry_t *out) {
  if (!out || !activity_log) return false;
  portENTER_CRITICAL(&activity_log_spinlock);
  if (idx >= activity_log_count) {
    portEXIT_CRITICAL(&activity_log_spinlock);
    return false;
  }
  // idx 0 = oldest entry currently held
  uint16_t real_idx = (uint16_t)((activity_log_head - activity_log_count + idx + log_capacity) % log_capacity);
  *out = activity_log[real_idx];
  portEXIT_CRITICAL(&activity_log_spinlock);
  return true;
}
