/**
 * @file api_audit_log.cpp
 * @brief REST API request audit log implementation (FEAT-033)
 */

#include "api_audit_log.h"
#include "ntp_driver.h"
#include <freertos/FreeRTOS.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <stdlib.h>

// Samme PSRAM-med-fallback-moenster som system_log.cpp/mb_activity_log.cpp
static api_audit_entry_t *audit_buf = NULL;
static uint16_t log_capacity = 0;
static uint16_t audit_head = 0;
static uint16_t audit_count = 0;
static volatile bool audit_enabled = true;

static portMUX_TYPE audit_spinlock = portMUX_INITIALIZER_UNLOCKED;

void api_audit_log_init(void) {
  if (!audit_buf) {
    size_t bytes = (size_t)API_AUDIT_LOG_MAX * sizeof(api_audit_entry_t);
    audit_buf = (api_audit_entry_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (!audit_buf) {
      audit_buf = (api_audit_entry_t *)malloc(bytes);
    }
    log_capacity = audit_buf ? API_AUDIT_LOG_MAX : 0;
  }

  if (!audit_buf) {
    return;  // Ingen buffer — loggen er inaktiv, men intet crasher
  }

  portENTER_CRITICAL(&audit_spinlock);
  memset(audit_buf, 0, (size_t)log_capacity * sizeof(api_audit_entry_t));
  audit_head = 0;
  audit_count = 0;
  portEXIT_CRITICAL(&audit_spinlock);
}

static const char *method_to_str(int method) {
  switch (method) {
    case HTTP_GET:    return "GET";
    case HTTP_POST:   return "POST";
    case HTTP_PUT:    return "PUT";
    case HTTP_DELETE: return "DELETE";
    case HTTP_PATCH:  return "PATCH";
    case HTTP_HEAD:   return "HEAD";
    case HTTP_OPTIONS:return "OPTIONS";
    default:          return "?";
  }
}

void api_audit_log_add(httpd_req_t *req, int status, const char *ip, const char *username) {
  if (!audit_enabled || !audit_buf || !req) return;

  // Vaegur-tid UDEN for det kritiske afsnit (samme begrundelse som system_log.cpp)
  uint32_t epoch = ntp_driver_is_synced() ? (uint32_t)ntp_driver_get_epoch() : 0;

  portENTER_CRITICAL(&audit_spinlock);
  api_audit_entry_t *e = &audit_buf[audit_head];
  e->timestamp_ms = millis();
  e->epoch_s = epoch;
  e->status = (uint16_t)status;
  strncpy(e->method, method_to_str(req->method), sizeof(e->method) - 1);
  e->method[sizeof(e->method) - 1] = '\0';
  strncpy(e->path, req->uri, sizeof(e->path) - 1);
  e->path[sizeof(e->path) - 1] = '\0';
  strncpy(e->ip, (ip && ip[0]) ? ip : "-", sizeof(e->ip) - 1);
  e->ip[sizeof(e->ip) - 1] = '\0';
  strncpy(e->username, (username && username[0]) ? username : "-", sizeof(e->username) - 1);
  e->username[sizeof(e->username) - 1] = '\0';
  audit_head = (audit_head + 1) % log_capacity;
  if (audit_count < log_capacity) audit_count++;
  portEXIT_CRITICAL(&audit_spinlock);
}

void api_audit_log_clear(void) {
  api_audit_log_init();
}

void api_audit_log_set_enabled(bool enabled) {
  audit_enabled = enabled;
}

bool api_audit_log_is_enabled(void) {
  return audit_enabled;
}

uint16_t api_audit_log_count(void) {
  return audit_count;
}

bool api_audit_log_get(uint16_t idx, api_audit_entry_t *out) {
  if (!out || !audit_buf) return false;
  portENTER_CRITICAL(&audit_spinlock);
  if (idx >= audit_count) {
    portEXIT_CRITICAL(&audit_spinlock);
    return false;
  }
  uint16_t real_idx = (uint16_t)((audit_head - audit_count + idx + log_capacity) % log_capacity);
  *out = audit_buf[real_idx];
  portEXIT_CRITICAL(&audit_spinlock);
  return true;
}
