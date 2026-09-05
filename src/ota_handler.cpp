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
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "ota_handler.h"
#include "api_handlers.h"
#include "config_struct.h"
#include "constants.h"
#include "version.h"
#include "debug.h"
#include "system_log.h"  // FEAT-086
#include "rbac.h"  // SECURITY_INDEX #2: rbac_has_write()

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
    "\"current_version\":\"v%s.%d\",\"running_partition\":\"%s\","
    "\"boot_partition\":\"%s\",\"rollback_possible\":%s}",
    state_str,
    (unsigned long)ota_state.received,
    (unsigned long)ota_state.total,
    (unsigned)percent,
    ota_state.error_msg,
    ota_state.new_version,
    PROJECT_VERSION, BUILD_NUMBER,
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

/* ============================================================================
 * FEAT-169: GitHub Releases-baseret OTA-opdatering
 *
 * Enheden har ALDRIG foer optraadt som HTTP/HTTPS-KLIENT (kun som server +
 * Modbus RTU/TCP master/slave) — dette er en ny netvaerks-kapabilitet, ikke
 * en udvidelse af en eksisterende. Kun MANUELT trigget (ingen baggrunds-
 * polling) — enheden er en potentielt LAN-only/offline industriel
 * controller og skal ikke tale ud af sig selv uopfordret.
 *
 * TLS-tillid: rigtig CA-bundling (certs/github_ca_bundle.pem, embeddet som
 * tekstfil ligesom certs/servercert.pem allerede goer for HTTPS-serveren),
 * IKKE setInsecure() — se SECURITY_INDEX.md for den fulde afvejning. To
 * domaener/CA-kaeder er i spil: api.github.com (Sectigo) og
 * objects.githubusercontent.com (Let's Encrypt), begge rod-ankre er bundlet.
 * ============================================================================ */

#define GITHUB_OWNER_REPO "Jangreenlarsen/Modbus_server_slave_ESP32"
#define GITHUB_RELEASE_ASSET_NAME "firmware.bin"

extern const uint8_t github_ca_bundle_pem_start[] asm("_binary_certs_github_ca_bundle_pem_start");
extern const uint8_t github_ca_bundle_pem_end[]   asm("_binary_certs_github_ca_bundle_pem_end");

// embed_txtfiles giver kun _start/_end (raa byte-range, ikke nul-termineret)
// — WiFiClientSecure::setCACert() forventer en nul-termineret C-streng.
// Kopieres én gang til en lille, permanent buffer (samme levetid som
// enheden i forvejen — der findes ingen "shutdown"-fase at rydde op i).
static const char *get_github_ca_bundle(void)
{
  static char *ca_buf = NULL;
  if (!ca_buf) {
    size_t len = (size_t)(github_ca_bundle_pem_end - github_ca_bundle_pem_start);
    ca_buf = (char *)malloc(len + 1);
    if (ca_buf) {
      memcpy(ca_buf, github_ca_bundle_pem_start, len);
      ca_buf[len] = '\0';
    }
  }
  return ca_buf;
}

// Sammenligner to punktum-separerede numeriske versionsstrenge (fx
// "7.9.10.27", med eller uden foranstillet "v"). >0 hvis a>b, <0 hvis a<b.
static int compare_versions(const char *a, const char *b)
{
  if (*a == 'v' || *a == 'V') a++;
  if (*b == 'v' || *b == 'V') b++;
  while (*a || *b) {
    int na = 0, nb = 0;
    while (*a && *a != '.') { na = na * 10 + (*a - '0'); a++; }
    while (*b && *b != '.') { nb = nb * 10 + (*b - '0'); b++; }
    if (na != nb) return na - nb;
    if (*a == '.') a++;
    if (*b == '.') b++;
  }
  return 0;
}

// Cache af seneste "github-check"-resultat, saa "github-install" ikke skal
// slaa op mod GitHub API'et en ekstra gang for at faa asset-URL'en igen.
static struct {
  bool valid;
  char asset_url[384];
  uint32_t asset_size;
} g_github_release_cache = { false, {0}, 0 };

esp_err_t api_handler_ota_github_check(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_OTA(req);

  if (ota_state.in_progress) {
    return api_send_error(req, 409, "OTA already in progress");
  }

  const char *ca = get_github_ca_bundle();
  if (!ca) {
    return api_send_error(req, 500, "CA bundle unavailable");
  }

  WiFiClientSecure client;
  client.setCACert(ca);
  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(10000);

  if (!http.begin(client, "https://api.github.com/repos/" GITHUB_OWNER_REPO "/releases/latest")) {
    return api_send_error(req, 500, "Could not begin HTTPS request");
  }
  // GitHub's API afviser requests uden en User-Agent header
  http.addHeader("User-Agent", "HyberFusion-PLC-OTA");
  http.addHeader("Accept", "application/vnd.github+json");

  int httpCode = http.GET();
  if (httpCode != 200) {
    char msg[96];
    snprintf(msg, sizeof(msg), "GitHub API returned HTTP %d", httpCode);
    http.end();
    return api_send_error(req, 502, msg);
  }

  JsonDocument doc;
  DeserializationError jerr = deserializeJson(doc, http.getStream());
  http.end();
  if (jerr) {
    return api_send_error(req, 502, "Invalid JSON from GitHub");
  }

  const char *tag = doc["tag_name"] | "";
  const char *published = doc["published_at"] | "";
  if (!tag[0]) {
    return api_send_error(req, 502, "No releases found (repo has no published releases yet)");
  }

  const char *asset_url = NULL;
  uint32_t asset_size = 0;
  if (doc["assets"].is<JsonArray>()) {
    for (JsonObject a : doc["assets"].as<JsonArray>()) {
      const char *name = a["name"] | "";
      if (strcmp(name, GITHUB_RELEASE_ASSET_NAME) == 0) {
        asset_url = a["browser_download_url"] | (const char *)NULL;
        asset_size = a["size"] | 0;
        break;
      }
    }
  }

  bool newer = (asset_url != NULL) && compare_versions(tag, PROJECT_VERSION) > 0;

  g_github_release_cache.valid = false;
  if (asset_url && newer) {
    strncpy(g_github_release_cache.asset_url, asset_url, sizeof(g_github_release_cache.asset_url) - 1);
    g_github_release_cache.asset_url[sizeof(g_github_release_cache.asset_url) - 1] = '\0';
    g_github_release_cache.asset_size = asset_size;
    g_github_release_cache.valid = true;
  }

  JsonDocument resp;
  resp["available"] = newer;
  resp["current_version"] = PROJECT_VERSION;
  resp["latest_version"] = tag;
  resp["asset_size"] = asset_size;
  resp["published_at"] = published;
  if (!asset_url) {
    resp["message"] = "Ingen '" GITHUB_RELEASE_ASSET_NAME "'-asset fundet i seneste release";
  }

  char buf[512];
  serializeJson(resp, buf, sizeof(buf));
  return api_send_json(req, buf);
}

esp_err_t api_handler_ota_github_install(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_OTA(req);

  if (ota_state.in_progress) {
    return api_send_error(req, 409, "OTA already in progress");
  }
  if (!g_github_release_cache.valid || !g_github_release_cache.asset_url[0]) {
    return api_send_error(req, 400, "Kald github-check foerst (ingen nyere version fundet/cachet)");
  }

  char asset_url[sizeof(g_github_release_cache.asset_url)];
  strncpy(asset_url, g_github_release_cache.asset_url, sizeof(asset_url));
  uint32_t expected_size = g_github_release_cache.asset_size;

  const char *ca = get_github_ca_bundle();
  if (!ca) {
    return api_send_error(req, 500, "CA bundle unavailable");
  }

  ota_state.in_progress = 1;
  ota_state.state = OTA_STATE_RECEIVING;
  ota_state.received = 0;
  ota_state.total = expected_size;
  ota_state.error_msg[0] = '\0';
  ota_state.new_version[0] = '\0';

  const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
  if (!update_partition) {
    snprintf(ota_state.error_msg, sizeof(ota_state.error_msg), "No OTA partition found");
    ota_state.state = OTA_STATE_ERROR;
    ota_state.in_progress = 0;
    return api_send_error(req, 500, ota_state.error_msg);
  }

  WiFiClientSecure client;
  client.setCACert(ca);
  HTTPClient http;
  http.setConnectTimeout(15000);
  http.setTimeout(30000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // GitHub's browser_download_url redirecter til objects.githubusercontent.com

  if (!http.begin(client, asset_url)) {
    snprintf(ota_state.error_msg, sizeof(ota_state.error_msg), "Could not begin download");
    ota_state.state = OTA_STATE_ERROR;
    ota_state.in_progress = 0;
    return api_send_error(req, 500, ota_state.error_msg);
  }
  http.addHeader("User-Agent", "HyberFusion-PLC-OTA");

  int httpCode = http.GET();
  if (httpCode != 200) {
    char msg[96];
    snprintf(msg, sizeof(msg), "Download HTTP %d", httpCode);
    http.end();
    snprintf(ota_state.error_msg, sizeof(ota_state.error_msg), "%s", msg);
    ota_state.state = OTA_STATE_ERROR;
    ota_state.in_progress = 0;
    return api_send_error(req, 502, msg);
  }

  int content_len = http.getSize();
  if (content_len <= 0 || content_len > OTA_MAX_FIRMWARE_SIZE) {
    http.end();
    snprintf(ota_state.error_msg, sizeof(ota_state.error_msg), "Invalid content length: %d", content_len);
    ota_state.state = OTA_STATE_ERROR;
    ota_state.in_progress = 0;
    return api_send_error(req, 400, ota_state.error_msg);
  }
  ota_state.total = (uint32_t)content_len;

  esp_ota_handle_t ota_handle = 0;
  esp_err_t err = esp_ota_begin(update_partition, content_len, &ota_handle);
  if (err != ESP_OK) {
    http.end();
    snprintf(ota_state.error_msg, sizeof(ota_state.error_msg), "esp_ota_begin failed: 0x%x", (int)err);
    ota_state.state = OTA_STATE_ERROR;
    ota_state.in_progress = 0;
    return api_send_error(req, 500, ota_state.error_msg);
  }

  char *chunk_buf = (char *)malloc(OTA_CHUNK_SIZE);
  if (!chunk_buf) {
    esp_ota_abort(ota_handle);
    http.end();
    snprintf(ota_state.error_msg, sizeof(ota_state.error_msg), "Failed to allocate chunk buffer");
    ota_state.state = OTA_STATE_ERROR;
    ota_state.in_progress = 0;
    return api_send_error(req, 500, ota_state.error_msg);
  }

  WiFiClient *stream = http.getStreamPtr();
  uint32_t received_total = 0;
  bool first_chunk = true;
  bool ok = true;
  uint32_t last_data_ms = millis();

  while (received_total < (uint32_t)content_len) {
    size_t avail = stream->available();
    if (avail == 0) {
      if (!http.connected() || (millis() - last_data_ms > 30000)) {
        snprintf(ota_state.error_msg, sizeof(ota_state.error_msg),
                 "Connection lost at %lu/%lu bytes", (unsigned long)received_total, (unsigned long)content_len);
        ok = false;
        break;
      }
      delay(10);
      continue;
    }

    size_t to_read = avail > OTA_CHUNK_SIZE ? (size_t)OTA_CHUNK_SIZE : avail;
    size_t remaining = (uint32_t)content_len - received_total;
    if (to_read > remaining) to_read = remaining;

    int n = stream->readBytes(chunk_buf, to_read);
    if (n <= 0) continue;
    last_data_ms = millis();

    if (first_chunk) {
      first_chunk = false;
      if (n < 4 || (uint8_t)chunk_buf[0] != 0xE9) {
        snprintf(ota_state.error_msg, sizeof(ota_state.error_msg),
                 "Invalid firmware: bad magic byte (expected 0xE9, got 0x%02X)", (uint8_t)chunk_buf[0]);
        ok = false;
        break;
      }
      if (n >= (int)(sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t))) {
        const esp_app_desc_t *app_desc = (const esp_app_desc_t *)(chunk_buf + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
        if (app_desc->magic_word == ESP_APP_DESC_MAGIC_WORD) {
          strncpy(ota_state.new_version, app_desc->version, sizeof(ota_state.new_version) - 1);
          ota_state.new_version[sizeof(ota_state.new_version) - 1] = '\0';
        }
      }
    }

    err = esp_ota_write(ota_handle, chunk_buf, n);
    if (err != ESP_OK) {
      snprintf(ota_state.error_msg, sizeof(ota_state.error_msg),
               "Flash write failed at %lu bytes: 0x%x", (unsigned long)received_total, (int)err);
      ok = false;
      break;
    }

    received_total += n;
    ota_state.received = received_total;
  }

  free(chunk_buf);
  http.end();

  if (!ok || received_total != (uint32_t)content_len) {
    if (ok) {
      snprintf(ota_state.error_msg, sizeof(ota_state.error_msg),
               "Incomplete download: %lu/%lu bytes", (unsigned long)received_total, (unsigned long)content_len);
    }
    esp_ota_abort(ota_handle);
    ota_state.state = OTA_STATE_ERROR;
    ota_state.in_progress = 0;
    ESP_LOGE(TAG, "GitHub OTA failed: %s", ota_state.error_msg);
    return api_send_error(req, 500, ota_state.error_msg);
  }

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

  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK) {
    snprintf(ota_state.error_msg, sizeof(ota_state.error_msg), "Failed to set boot partition: 0x%x", (int)err);
    ota_state.state = OTA_STATE_ERROR;
    ota_state.in_progress = 0;
    ESP_LOGE(TAG, "%s", ota_state.error_msg);
    return api_send_error(req, 500, ota_state.error_msg);
  }

  ota_state.state = OTA_STATE_DONE;
  ESP_LOGI(TAG, "GitHub OTA complete: %lu bytes, version: %s. Rebooting in %dms...",
           (unsigned long)received_total,
           ota_state.new_version[0] ? ota_state.new_version : "unknown",
           OTA_REBOOT_DELAY_MS);

  char resp[256];
  int len = snprintf(resp, sizeof(resp),
    "{\"status\":\"ok\",\"message\":\"GitHub OTA complete, rebooting...\","
    "\"bytes\":%lu,\"new_version\":\"%s\",\"reboot_in_ms\":%d}",
    (unsigned long)received_total,
    ota_state.new_version[0] ? ota_state.new_version : "unknown",
    OTA_REBOOT_DELAY_MS);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, resp, len);

  xTaskCreate(ota_reboot_task, "ota_reboot", 2048, NULL, 5, NULL);
  return ESP_OK;
}
