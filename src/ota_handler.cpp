// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file ota_handler.cpp
 * @brief OTA firmware update via HTTP API (FEAT-031)
 *
 * LAYER 7: User Interface - OTA Update
 * Implements chunked firmware upload using ESP-IDF OTA APIs.
 * Streams 4KB chunks directly to flash — no full firmware buffering in RAM.
 *
 * Heap impact:
 *   - Peak: ~8.7KB during upload (4KB chunk + ESP-IDF internals)
 *   - Permanent: ~252 bytes (ota_state struct + URI registrations)
 *
 * Endpoints:
 *   POST /api/system/ota          - Upload firmware .bin
 *   GET  /api/system/ota/status   - Poll progress
 *   POST /api/system/ota/rollback - Rollback to previous firmware
 */

#include <string.h>
#include <Arduino.h>
#include <esp_http_server.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_app_format.h>
#include <esp_system.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <lwip/sockets.h>

#include "ota_handler.h"
#include "api_handlers.h"
#include "config_struct.h"
#include "constants.h"
#include "version.h"
#include "debug.h"
#include "system_log.h"  // FEAT-086
#include "rbac.h"  // SECURITY_INDEX #2: rbac_has_write()
#include "ip_acl.h"  // FEAT-399

// External functions from http_server.cpp / api_handlers.cpp
extern void http_server_stat_request(void);
extern bool http_server_check_auth(httpd_req_t *req);
extern int http_server_auth_user(httpd_req_t *req);
bool http_rate_limit_check(httpd_req_t *req);

// SECURITY_INDEX #2 + #7: this used to only require a VALID SESSION
// (http_server_check_auth — any authenticated user, incl. read-only) to
// flash arbitrary firmware, unlike every other write endpoint which goes
// through CHECK_AUTH_WRITE + rbac_has_write(). Now mirrors that macro
// exactly (api_handlers.cpp), including the same #7 fix (rate-limit
// checked before the auth decision, not after).
#define CHECK_AUTH_OTA(req) \
  do { \
    if (!ip_acl_check_req(req, ACL_SVC_HTTP)) { \
      return api_send_error(req, 403, "Blocked by IP ACL"); \
    } \
    if (!g_persist_config.network.http.api_enabled) { \
      return api_send_error(req, 403, "API disabled"); \
    } \
    if (!http_rate_limit_check(req)) { \
      return api_send_error(req, 429, "Too many requests"); \
    } \
    int _ota_uid = http_server_auth_user(req); \
    if (_ota_uid < 0) { \
      return api_send_error(req, 401, "Authentication required"); \
    } \
    if (!rbac_has_write(_ota_uid)) { \
      return api_send_error(req, 403, "Write privilege required"); \
    } \
  } while(0)

static const char *TAG = "OTA";

/* ============================================================================
 * OTA STATE
 * ============================================================================ */

static struct {
  volatile uint8_t  state;          // OTA_STATE_*
  volatile uint32_t received;       // bytes received so far
  volatile uint32_t total;          // total expected bytes
  volatile uint8_t  in_progress;    // atomic lock to prevent concurrent uploads
  char error_msg[64];               // last error message
  char new_version[32];             // version extracted from uploaded firmware
} ota_state = {
  .state = OTA_STATE_IDLE,
  .received = 0,
  .total = 0,
  .in_progress = 0,
  .error_msg = {0},
  .new_version = {0}
};

/* ============================================================================
 * BUG-448: ROLLBACK-MAAL — findes der en gyldig firmware i den anden partition?
 *
 * Tidligere viste status "rollback_possible" = (running != boot), hvilket kun
 * er sandt i sekunderne mellem en OTA og genstarten — efter en normal opstart
 * stod der altid "Nej", selv om den forrige firmware laa urort i den anden
 * partition. Nu: den anden partition (= den OTA ville skrive til) har et
 * gyldigt app-image, hvis esp_ota_get_partition_description() kan laese dets
 * app-header (ren esp_partition_read — IKKE esp_ota_get_state_partition(),
 * se BUG-423-kommentaren i main.cpp). esp_ota_set_boot_partition() verificerer
 * hele imaget foer der skiftes.
 *
 * Versionen: esp_app_desc_t.version er den samme i alle builds (kommer fra
 * Arduino-frameworkets forkompilerede bibliotek), saa firmwaren indlejrer sin
 * egen markoer, som soeges frem i den anden partition. Firmware fra foer
 * v7.9.68.54 har ingen markoer → version "ukendt". Resultatet caches (den
 * anden partition aendres kun af en OTA, som nulstiller cachen).
 * ============================================================================ */

#define BUG448_STR2(x) #x
#define BUG448_STR(x) BUG448_STR2(x)
#define FW_MARKER_PREFIX "HVPLC_FWVER:"
extern "C" __attribute__((used)) const char g_fw_version_marker[] =
  FW_MARKER_PREFIX PROJECT_VERSION "." BUG448_STR(BUILD_NUMBER);

static struct {
  bool valid;          // cache udfyldt
  bool possible;       // gyldigt image i den anden partition
  char label[17];      // partitionens navn
  char version[32];    // fundet version, "" = ukendt
} rb_cache = {false, false, {0}, {0}};

static void rollback_target_refresh(void)
{
  rb_cache.possible = false;
  rb_cache.label[0] = '\0';
  rb_cache.version[0] = '\0';

  const esp_partition_t *other = esp_ota_get_next_update_partition(NULL);
  if (other) {
    strncpy(rb_cache.label, other->label, sizeof(rb_cache.label) - 1);
    rb_cache.label[sizeof(rb_cache.label) - 1] = '\0';
    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(other, &desc) == ESP_OK) {
      rb_cache.possible = true;
      // Soeg markoeren frem i bidder med overlap (markoeren kan krydse en graense)
      static const char prefix[] = FW_MARKER_PREFIX;
      const size_t plen = sizeof(prefix) - 1;
      const size_t CHUNK = 2048, OVL = 64;
      uint8_t *buf = (uint8_t *)malloc(CHUNK + OVL);
      if (buf) {
        for (size_t off = 0; off < other->size && !rb_cache.version[0]; off += CHUNK) {
          size_t n = CHUNK + OVL;
          if (off + n > other->size) n = other->size - off;
          if (esp_partition_read(other, off, buf, n) != ESP_OK) break;
          // kun fund der STARTER foer overlappet — et fund i overlappet tages
          // af naeste bid med hele versionen (ellers kunne den afkortes)
          for (size_t i = 0; i < CHUNK && i + plen + 1 < n; i++) {
            if (buf[i] != 'H' || memcmp(buf + i, prefix, plen) != 0) continue;
            const uint8_t *v = buf + i + plen;
            if (*v < '0' || *v > '9') continue;  // soege-literalen selv (efterfulgt af NUL)
            size_t k = 0;
            while (k < sizeof(rb_cache.version) - 1 && i + plen + k < n &&
                   ((v[k] >= '0' && v[k] <= '9') || v[k] == '.')) {
              rb_cache.version[k] = (char)v[k];
              k++;
            }
            rb_cache.version[k] = '\0';
            break;
          }
        }
        free(buf);
      }
    }
  }
  rb_cache.valid = true;
  ESP_LOGI(TAG, "Rollback-maal: %s, gyldigt=%d, version=%s",
           rb_cache.label, rb_cache.possible, rb_cache.version[0] ? rb_cache.version : "ukendt");
}

/* ============================================================================
 * REBOOT TASK
 * ============================================================================ */

static void ota_reboot_task(void *arg)
{
  // FEAT-086: log foer forsinkelsen, saa den naar at blive skrevet
  system_log_add_event((uint8_t)SYSLOG_SRC_SYSTEM, NULL, NULL, "Reboot udloest af OTA-firmwareopdatering");
  vTaskDelay(pdMS_TO_TICKS(OTA_REBOOT_DELAY_MS));
  ESP_LOGI(TAG, "Rebooting into new firmware...");
  esp_restart();
}

/* ============================================================================
 * POST /api/system/ota - Upload firmware
 * ============================================================================ */

esp_err_t api_handler_ota_upload(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_OTA(req);

  // Prevent concurrent uploads
  if (ota_state.in_progress) {
    return api_send_error(req, 409, "OTA already in progress");
  }

  // Validate content length
  size_t content_len = req->content_len;
  if (content_len == 0) {
    return api_send_error(req, 400, "Empty request body");
  }
  if (content_len > OTA_MAX_FIRMWARE_SIZE) {
    return api_send_error(req, 400, "Firmware too large (max 1.8125MB)");
  }

  // Set OTA state
  ota_state.in_progress = 1;
  ota_state.state = OTA_STATE_RECEIVING;
  ota_state.received = 0;
  ota_state.total = content_len;
  ota_state.error_msg[0] = '\0';
  ota_state.new_version[0] = '\0';
  rb_cache.valid = false;  // BUG-448: den anden partition overskrives nu

  // Find next OTA partition
  const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
  if (!update_partition) {
    snprintf(ota_state.error_msg, sizeof(ota_state.error_msg), "No OTA partition found");
    ota_state.state = OTA_STATE_ERROR;
    ota_state.in_progress = 0;
    return api_send_error(req, 500, ota_state.error_msg);
  }

  ESP_LOGI(TAG, "OTA target partition: %s (offset 0x%lx, size 0x%lx)",
           update_partition->label,
           (unsigned long)update_partition->address,
           (unsigned long)update_partition->size);

  // Begin OTA
  esp_ota_handle_t ota_handle = 0;
  esp_err_t err = esp_ota_begin(update_partition, content_len, &ota_handle);
  if (err != ESP_OK) {
    snprintf(ota_state.error_msg, sizeof(ota_state.error_msg),
             "esp_ota_begin failed: 0x%x", (int)err);
    ota_state.state = OTA_STATE_ERROR;
    ota_state.in_progress = 0;
    ESP_LOGE(TAG, "%s", ota_state.error_msg);
    return api_send_error(req, 500, ota_state.error_msg);
  }

  // Allocate chunk buffer on heap (not stack — 8KB stack is tight)
  char *chunk_buf = (char *)malloc(OTA_CHUNK_SIZE);
  if (!chunk_buf) {
    esp_ota_abort(ota_handle);
    snprintf(ota_state.error_msg, sizeof(ota_state.error_msg), "Failed to allocate chunk buffer");
    ota_state.state = OTA_STATE_ERROR;
    ota_state.in_progress = 0;
    return api_send_error(req, 500, ota_state.error_msg);
  }

  // Set longer receive timeout for large uploads (60 seconds)
  struct timeval recv_timeout = { .tv_sec = 60, .tv_usec = 0 };
  setsockopt(httpd_req_to_sockfd(req), SOL_SOCKET, SO_RCVTIMEO,
             &recv_timeout, sizeof(recv_timeout));

  // Chunked receive + flash write loop
  uint32_t received_total = 0;
  bool first_chunk = true;
  bool upload_ok = true;

  while (received_total < content_len) {
    size_t to_read = content_len - received_total;
    if (to_read > OTA_CHUNK_SIZE) to_read = OTA_CHUNK_SIZE;

    int received = httpd_req_recv(req, chunk_buf, to_read);
    if (received <= 0) {
      if (received == HTTPD_SOCK_ERR_TIMEOUT) {
        // Timeout — retry
        continue;
      }
      snprintf(ota_state.error_msg, sizeof(ota_state.error_msg),
               "Receive error at %lu/%lu bytes", (unsigned long)received_total, (unsigned long)content_len);
      upload_ok = false;
      break;
    }

    // Validate first chunk: ESP32 firmware magic byte
    if (first_chunk) {
      first_chunk = false;
      if (received < 4 || (uint8_t)chunk_buf[0] != 0xE9) {
        snprintf(ota_state.error_msg, sizeof(ota_state.error_msg),
                 "Invalid firmware: bad magic byte (expected 0xE9, got 0x%02X)",
                 (uint8_t)chunk_buf[0]);
        upload_ok = false;
        break;
      }

      // Extract version from esp_app_desc_t if image is large enough
      // esp_image_header_t (24B) + esp_image_segment_header_t (8B) + esp_app_desc_t
      if (received >= (int)(sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t))) {
        const esp_app_desc_t *app_desc = (const esp_app_desc_t *)(chunk_buf + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
        if (app_desc->magic_word == ESP_APP_DESC_MAGIC_WORD) {
          strncpy(ota_state.new_version, app_desc->version, sizeof(ota_state.new_version) - 1);
          ota_state.new_version[sizeof(ota_state.new_version) - 1] = '\0';
          ESP_LOGI(TAG, "New firmware version: %s", ota_state.new_version);
        }
      }
    }

    // Write chunk to flash
    err = esp_ota_write(ota_handle, chunk_buf, received);
    if (err != ESP_OK) {
      snprintf(ota_state.error_msg, sizeof(ota_state.error_msg),
               "Flash write failed at %lu bytes: 0x%x",
               (unsigned long)received_total, (int)err);
      upload_ok = false;
      break;
    }

    received_total += received;
    ota_state.received = received_total;
  }

  free(chunk_buf);

  if (!upload_ok) {
    esp_ota_abort(ota_handle);
    ota_state.state = OTA_STATE_ERROR;
    ota_state.in_progress = 0;
    ESP_LOGE(TAG, "OTA failed: %s", ota_state.error_msg);
    return api_send_error(req, 500, ota_state.error_msg);
  }

  // Verify and finalize
  ota_state.state = OTA_STATE_VERIFYING;
  err = esp_ota_end(ota_handle);
  if (err != ESP_OK) {
    if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
      snprintf(ota_state.error_msg, sizeof(ota_state.error_msg), "Firmware validation failed (bad checksum)");
    } else {
      snprintf(ota_state.error_msg, sizeof(ota_state.error_msg), "esp_ota_end failed: 0x%x", (int)err);
    }
    ota_state.state = OTA_STATE_ERROR;
    ota_state.in_progress = 0;
    ESP_LOGE(TAG, "%s", ota_state.error_msg);
    return api_send_error(req, 500, ota_state.error_msg);
  }

  // Set boot partition
  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK) {
    snprintf(ota_state.error_msg, sizeof(ota_state.error_msg),
             "Failed to set boot partition: 0x%x", (int)err);
    ota_state.state = OTA_STATE_ERROR;
    ota_state.in_progress = 0;
    ESP_LOGE(TAG, "%s", ota_state.error_msg);
    return api_send_error(req, 500, ota_state.error_msg);
  }

  ota_state.state = OTA_STATE_DONE;
  ESP_LOGI(TAG, "OTA complete: %lu bytes, version: %s. Rebooting in %dms...",
           (unsigned long)received_total,
           ota_state.new_version[0] ? ota_state.new_version : "unknown",
           OTA_REBOOT_DELAY_MS);

  // Send success response before reboot
  char resp[256];
  int len = snprintf(resp, sizeof(resp),
    "{\"status\":\"ok\",\"message\":\"OTA complete, rebooting...\","
    "\"bytes\":%lu,\"new_version\":\"%s\",\"reboot_in_ms\":%d}",
    (unsigned long)received_total,
    ota_state.new_version[0] ? ota_state.new_version : "unknown",
    OTA_REBOOT_DELAY_MS);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, resp, len);

  // Schedule reboot (let HTTP response complete first)
  xTaskCreate(ota_reboot_task, "ota_reboot", 2048, NULL, 5, NULL);

  // Note: in_progress stays set — device is about to reboot
  return ESP_OK;
}

/* ============================================================================
 * GET /api/system/ota/status - Poll progress
 * ============================================================================ */

esp_err_t api_handler_ota_status(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_OTA(req);

  uint8_t percent = 0;
  if (ota_state.total > 0) {
    percent = (uint8_t)((uint64_t)ota_state.received * 100 / ota_state.total);
  }

  // Current running firmware info
  const esp_app_desc_t *running = esp_ota_get_app_description();
  const esp_partition_t *running_part = esp_ota_get_running_partition();
  const esp_partition_t *boot_part = esp_ota_get_boot_partition();

  static const char *state_names[] = {"idle", "receiving", "verifying", "done", "error"};
  const char *state_str = (ota_state.state <= OTA_STATE_ERROR) ? state_names[ota_state.state] : "unknown";

  // BUG-448: rollback-maal fra den anden partition (kun naar ingen OTA koerer)
  bool idle = (ota_state.state == OTA_STATE_IDLE || ota_state.state == OTA_STATE_ERROR);
  if (idle && !rb_cache.valid) rollback_target_refresh();
  bool rb_possible = idle && rb_cache.valid && rb_cache.possible;

  char resp[640];
  int len = snprintf(resp, sizeof(resp),
    "{\"state\":\"%s\",\"received\":%lu,\"total\":%lu,\"percent\":%u,"
    "\"error\":\"%s\",\"new_version\":\"%s\","
    "\"current_version\":\"v%s.%d\",\"running_partition\":\"%s\","
    "\"boot_partition\":\"%s\",\"rollback_possible\":%s,"
    "\"rollback_partition\":\"%s\",\"rollback_version\":\"%s\"}",
    state_str,
    (unsigned long)ota_state.received,
    (unsigned long)ota_state.total,
    (unsigned)percent,
    ota_state.error_msg,
    ota_state.new_version,
    PROJECT_VERSION, BUILD_NUMBER,
    running_part ? running_part->label : "unknown",
    boot_part ? boot_part->label : "unknown",
    rb_possible ? "true" : "false",
    rb_possible ? rb_cache.label : "",
    (rb_possible && rb_cache.version[0]) ? rb_cache.version : "");

  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, resp, len);
}

/* ============================================================================
 * POST /api/system/ota/rollback - Rollback to previous firmware
 * ============================================================================ */

esp_err_t api_handler_ota_rollback(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_OTA(req);

  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *boot = esp_ota_get_boot_partition();

  // Check if rollback is possible
  if (!running || !boot) {
    return api_send_error(req, 500, "Cannot determine partition info");
  }

  // Find the other OTA partition to rollback to
  const esp_partition_t *other = esp_ota_get_next_update_partition(NULL);
  if (!other) {
    return api_send_error(req, 400, "No previous firmware partition found");
  }

  // BUG-448: gyldigt app-image i den anden partition? (header-laesning, ikke
  // esp_ota_get_state_partition() — se BUG-423-kommentaren i main.cpp).
  // esp_ota_set_boot_partition() nedenfor verificerer hele imaget.
  if (ota_state.state == OTA_STATE_RECEIVING || ota_state.state == OTA_STATE_VERIFYING) {
    return api_send_error(req, 409, "OTA upload in progress");
  }
  esp_app_desc_t other_desc;
  esp_err_t err = esp_ota_get_partition_description(other, &other_desc);
  if (err != ESP_OK) {
    return api_send_error(req, 400, "Previous partition has no valid firmware");
  }

  // Set boot partition to the other one
  err = esp_ota_set_boot_partition(other);
  if (err != ESP_OK) {
    char msg[64];
    snprintf(msg, sizeof(msg), "Rollback failed: 0x%x", (int)err);
    return api_send_error(req, 500, msg);
  }

  ESP_LOGI(TAG, "Rollback: switching boot from %s to %s, rebooting...",
           running->label, other->label);

  char resp[128];
  int len = snprintf(resp, sizeof(resp),
    "{\"status\":\"ok\",\"message\":\"Rolling back to %s, rebooting...\","
    "\"reboot_in_ms\":%d}",
    other->label, OTA_REBOOT_DELAY_MS);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, resp, len);

  // Schedule reboot
  xTaskCreate(ota_reboot_task, "ota_reboot", 2048, NULL, 5, NULL);
  return ESP_OK;
}

