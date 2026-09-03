/**
 * @file mb_activity_log.cpp
 * @brief Unified Modbus activity log implementation (FEAT-149)
 */

#include "mb_activity_log.h"
#include <freertos/FreeRTOS.h>
#include <string.h>

static mb_activity_entry_t activity_log[MB_ACTIVITY_LOG_MAX];
static uint8_t activity_log_head = 0;   // Next write position
static uint8_t activity_log_count = 0;  // Total entries (max MB_ACTIVITY_LOG_MAX)

uint8_t g_mb_activity_next_source = MB_SRC_UNKNOWN;
uint8_t g_mb_activity_current_source = MB_SRC_UNKNOWN;

// Master (Core 0 async task) and Slave (Core 1 main loop) can both log
// concurrently — guard the shared ring buffer with its own spinlock.
static portMUX_TYPE activity_log_spinlock = portMUX_INITIALIZER_UNLOCKED;

void mb_activity_log_init(void) {
  portENTER_CRITICAL(&activity_log_spinlock);
  memset(activity_log, 0, sizeof(activity_log));
  activity_log_head = 0;
  activity_log_count = 0;
  portEXIT_CRITICAL(&activity_log_spinlock);
}

void mb_activity_log_add(mb_activity_role_t role, uint8_t source, uint8_t slave_id, uint8_t function_code,
                          uint16_t address, uint8_t count, int32_t value, int16_t error) {
  portENTER_CRITICAL(&activity_log_spinlock);
  mb_activity_entry_t *e = &activity_log[activity_log_head];
  e->timestamp_ms = millis();
  e->role = (uint8_t)role;
  e->source = source;
  e->slave_id = slave_id;
  e->function_code = function_code;
  e->address = address;
  e->count = count;
  e->value = value;
  e->error = error;
  activity_log_head = (activity_log_head + 1) % MB_ACTIVITY_LOG_MAX;
  if (activity_log_count < MB_ACTIVITY_LOG_MAX) {
    activity_log_count++;
  }
  portEXIT_CRITICAL(&activity_log_spinlock);
}

void mb_activity_log_clear(void) {
  mb_activity_log_init();
}

uint8_t mb_activity_log_count(void) {
  return activity_log_count;
}

bool mb_activity_log_get(uint8_t idx, mb_activity_entry_t *out) {
  if (!out) return false;
  portENTER_CRITICAL(&activity_log_spinlock);
  if (idx >= activity_log_count) {
    portEXIT_CRITICAL(&activity_log_spinlock);
    return false;
  }
  // idx 0 = oldest entry currently held
  uint8_t real_idx = (activity_log_head - activity_log_count + idx + MB_ACTIVITY_LOG_MAX) % MB_ACTIVITY_LOG_MAX;
  *out = activity_log[real_idx];
  portEXIT_CRITICAL(&activity_log_spinlock);
  return true;
}
