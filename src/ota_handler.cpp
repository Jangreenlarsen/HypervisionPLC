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
#include <lwip/sockets.h>

#include "ota_handler.h"
#include "api_handlers.h"
#include "config_struct.h"
#include "constants.h"
#include "debug.h"

// External functions from http_server.cpp / api_handlers.cpp
extern void http_server_stat_request(void);
extern bool http_server_check_auth(httpd_req_t *req);
bool http_rate_limit_check(httpd_req_t *req);

#define CHECK_AUTH_OTA(req) \
  do { \
    if (!g_persist_config.network.http.api_enabled) { \
      return api_send_error(req, 403, "API disabled"); \
    } \
    if (!http_server_check_auth(req)) { \
      return api_send_error(req, 401, "Authentication required"); \
    } \
    if (!http_rate_limit_check(req)) { \
      return api_send_error(req, 429, "Too many requests"); \
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
 * REBOOT TASK
 * ============================================================================ */

static void ota_reboot_task(void *arg)
{
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
    return api_send_error(req, 400, "Firmware too large (max 1.625MB)");
  }

  // Set OTA state
  ota_state.in_progress = 1;
  ota_state.state = OTA_STATE_RECEIVING;
  ota_state.received = 0;
  ota_state.total = content_len;
  ota_state.error_msg[0] = '\0';
  ota_state.new_version[0] = '\0';

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

  char resp[512];
  int len = snprintf(resp, sizeof(resp),
    "{\"state\":\"%s\",\"received\":%lu,\"total\":%lu,\"percent\":%u,"
    "\"error\":\"%s\",\"new_version\":\"%s\","
    "\"current_version\":\"%s\",\"running_partition\":\"%s\","
    "\"boot_partition\":\"%s\",\"rollback_possible\":%s}",
    state_str,
    (unsigned long)ota_state.received,
    (unsigned long)ota_state.total,
    (unsigned)percent,
    ota_state.error_msg,
    ota_state.new_version,
    running ? running->version : "unknown",
    running_part ? running_part->label : "unknown",
    boot_part ? boot_part->label : "unknown",
    (running_part && boot_part && running_part != boot_part) ? "true" : "false");

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

  // Verify the other partition has valid firmware
  esp_ota_img_states_t other_state;
  esp_err_t err = esp_ota_get_state_partition(other, &other_state);
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
