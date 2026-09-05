/**
 * @file system_log.cpp
 * @brief Delt haendelses-/registerændringslog implementation (FEAT-086/089)
 */

#include "system_log.h"
#include "ntp_driver.h"
#include <freertos/FreeRTOS.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <stdlib.h>

// Samme PSRAM-med-fallback-moenster som mb_activity_log.cpp (FEAT-153)
static syslog_entry_t *syslog_buf = NULL;
static uint16_t log_capacity = 0;
static uint16_t syslog_head = 0;
static uint16_t syslog_count = 0;
static volatile bool syslog_enabled = true;

static portMUX_TYPE syslog_spinlock = portMUX_INITIALIZER_UNLOCKED;

void system_log_init(void) {
  if (!syslog_buf) {
    size_t bytes = (size_t)SYSTEM_LOG_MAX * sizeof(syslog_entry_t);
    syslog_buf = (syslog_entry_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (!syslog_buf) {
      syslog_buf = (syslog_entry_t *)malloc(bytes);
    }
    log_capacity = syslog_buf ? SYSTEM_LOG_MAX : 0;
  }

  if (!syslog_buf) {
    return;  // Ingen buffer — loggen er inaktiv, men intet crasher
  }

  portENTER_CRITICAL(&syslog_spinlock);
  memset(syslog_buf, 0, (size_t)log_capacity * sizeof(syslog_entry_t));
  syslog_head = 0;
  syslog_count = 0;
  portEXIT_CRITICAL(&syslog_spinlock);
}

static void syslog_add_internal(uint8_t category, uint8_t source, const char *username,
                                 const char *ip, uint16_t reg_addr, bool is_coil,
                                 int32_t old_value, int32_t new_value, const char *message) {
  if (!syslog_enabled || !syslog_buf) return;

  // Vaegur-tid UDEN for det kritiske afsnit — se mb_activity_log.cpp's
  // tilsvarende kommentar (spinlock skal holdes saa kort som muligt)
  uint32_t epoch = ntp_driver_is_synced() ? (uint32_t)ntp_driver_get_epoch() : 0;

  portENTER_CRITICAL(&syslog_spinlock);
  syslog_entry_t *e = &syslog_buf[syslog_head];
  e->timestamp_ms = millis();
  e->epoch_s = epoch;
  e->category = category;
  e->source = source;
  strncpy(e->username, (username && username[0]) ? username : "-", sizeof(e->username) - 1);
  e->username[sizeof(e->username) - 1] = '\0';
  strncpy(e->ip, (ip && ip[0]) ? ip : "-", sizeof(e->ip) - 1);
  e->ip[sizeof(e->ip) - 1] = '\0';
  e->reg_addr = reg_addr;
  e->is_coil = is_coil;
  e->old_value = old_value;
  e->new_value = new_value;
  strncpy(e->message, message ? message : "", sizeof(e->message) - 1);
  e->message[sizeof(e->message) - 1] = '\0';
  syslog_head = (syslog_head + 1) % log_capacity;
  if (syslog_count < log_capacity) syslog_count++;
  portEXIT_CRITICAL(&syslog_spinlock);
}

void system_log_add_event(uint8_t source, const char *username, const char *ip, const char *message) {
  syslog_add_internal((uint8_t)SYSLOG_CAT_EVENT, source, username, ip, 0xFFFF, false, 0, 0, message);
}

void system_log_add_reg_change(uint8_t source, const char *username, const char *ip,
                                uint16_t reg_addr, bool is_coil,
                                int32_t old_value, int32_t new_value) {
  syslog_add_internal((uint8_t)SYSLOG_CAT_REG_CHANGE, source, username, ip,
                       reg_addr, is_coil, old_value, new_value, NULL);
}

void system_log_clear(void) {
  system_log_init();
}

void system_log_set_enabled(bool enabled) {
  syslog_enabled = enabled;
}

bool system_log_is_enabled(void) {
  return syslog_enabled;
}

uint16_t system_log_count(void) {
  return syslog_count;
}

bool system_log_get(uint16_t idx, syslog_entry_t *out) {
  if (!out || !syslog_buf) return false;
  portENTER_CRITICAL(&syslog_spinlock);
  if (idx >= syslog_count) {
    portEXIT_CRITICAL(&syslog_spinlock);
    return false;
  }
  uint16_t real_idx = (uint16_t)((syslog_head - syslog_count + idx + log_capacity) % log_capacity);
  *out = syslog_buf[real_idx];
  portEXIT_CRITICAL(&syslog_spinlock);
  return true;
}
