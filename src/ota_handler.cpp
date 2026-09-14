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
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include "wifi_driver.h"
#include "ethernet_driver.h"

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

// BUG-366: WiFiClientSecure/mbedTLS-haandtrykket kraever ét stort SAMMENHAENGENDE
// stykke INTERN hukommelse (ikke PSRAM — TLS-buffere allokeres via standard
// heap, som paa denne PSRAM-udgave (BOARD_HAS_PSRAM) ellers automatisk
// omdirigerer store allokeringer til ekstern SPI-RAM). Er interne heap for
// fragmenteret (sandsynligt efter lang oppetid med web/Modbus/ST Logic-drift),
// kan mbedTLS's allokering fejle paa en maade der IKKE fanges paent af
// HTTPClient, men i stedet crasher hele enheden. Minimumsgraense er sat
// konservativt hoejt (48KB) i forhold til ESP-IDF's default TLS in+out
// buffer-stoerrelser (typisk 16KB+16KB) plus mbedTLS-kontekst-overhead.
#define GITHUB_OTA_MIN_INTERNAL_BLOCK (48 * 1024)

static bool github_ota_internal_heap_ok(const char *ctx_label)
{
  size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  ESP_LOGI(TAG, "%s: stoerste sammenhaengende INTERNE bloek=%u byte (fri heap total=%u)",
           ctx_label, (unsigned)largest_internal, (unsigned)ESP.getFreeHeap());
  return largest_internal >= GITHUB_OTA_MIN_INTERNAL_BLOCK;
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

// BUG-367: ALVORLIG regression opdaget efter BUG-366 — brugeren bekraeftede
// at HELE web-serveren gik i sort (ping virkede stadig, men INGEN browser-
// forbindelse kunne oprettes til hverken dashboard eller noget andet) efter
// blot at have klikket "Tjek for opdatering". Root cause: esp_http_server er
// SINGLETRAADET her (én "httpd"-task betjener ALLE HTTP-requests i denne
// enhed) — http.setConnectTimeout()/setTimeout() begraenser kun TCP-connect
// og databaseoverfoersel, IKKE selve DNS-opslaget (lwIP's
// gethostbyname/getaddrinfo), som kan blokere langt laengere (i praksis
// ubegraenset ved forkert/uopnaaeligt DNS-server-IP, fx efter et
// netvaerksskift) end nogen af de eksplicitte timeouts. Blokerer DNS-kaldet,
// blokerer dermed HELE web-serveren for evigt — ikke kun GitHub-kaldet —
// hvilket praecis matcher det brugeren observerede.
//
// Fix: selve HTTPS-arbejdet (DNS+TLS+GET+parse/download) koeres nu i en
// DEDIKERET FreeRTOS-baggrundstask, adskilt fra httpd's egen task. Den
// oprindelige httpd-handler venter kun paa den med en HAARD, oevre
// tidsgraense (semaphore-wait) — timer den ud, faar brugeren straks en
// fejlbesked, og httpd-tasken (og dermed resten af web-serveren) er FRI
// igen, uanset om baggrundstasken stadig haenger i DNS-opslaget. En evt.
// "haengt" baggrundstask forbliver isoleret (egen stak, egen ressource) og
// blokerer ikke laengere andre requests — i vaerste fald et enkelt, begraenset
// ressourceforbrug per fejlslagent forsoeg, i stedet for at laase HELE
// grebslebrugerfladen.

// MIDLERTIDIG DIAGNOSTIK (fjernes igen naar krasset er fundet): enheden har
// ingen serial-konsol tilgaengelig lige nu til at faa et rigtigt backtrace
// naar github-check panic'er (ESP_RST_PANIC, reproduceret 2x via ren API-kald
// uden GUI involveret). RTC_NOINIT_ATTR-hukommelse overlever en panic-reboot
// (ryddes KUN ved power-on-reset), saa vi kan laegge et "sidst naaede trin"-
// breadcrumb ind lige foer hvert risikofyldt undertrin og laese det tilbage
// EFTER genstarten via /api/system/ota/github-debug for at indsnaevre præcis
// hvor det dør, uden fysisk adgang til enheden.
RTC_NOINIT_ATTR static uint32_t g_gh_debug_stage;
RTC_NOINIT_ATTR static uint32_t g_gh_debug_magic;
RTC_NOINIT_ATTR static int32_t  g_gh_debug_http_code;
#define GH_DEBUG_MAGIC 0x67684442u  // "ghDB"
#define GH_STAGE(n) do { g_gh_debug_magic = GH_DEBUG_MAGIC; g_gh_debug_stage = (n); } while (0)

static SemaphoreHandle_t g_github_check_sem = NULL;

struct GithubCheckResult {
  int http_code;      // 0 = aldrig naaet frem til GET
  bool json_ok;
  char tag[64];
  char published[32];
  char asset_url[384];
  uint32_t asset_size;
  char err[96];        // ikke-tom => hard fejl foer/uden http_code
};
static GithubCheckResult g_github_check_result;

// BUG-368: vTaskDelete(NULL) er en FreeRTOS-primitiv, IKKE et normalt C++
// return — den frigoer tasksens stak som en raa hukommelsesblok uden at
// koere C++-destruktorer for objekter der stadig er i scope paa den stak.
// Kaldes vTaskDelete() direkte fra en funktion der stadig har en levende
// `WiFiClientSecure client`/`HTTPClient http` lokalt, bliver mbedTLS's
// TLS-kontekst/buffere (typisk 20-40KB) ALDRIG frigivet — en permanent
// hukommelseslaekage pr. forsoeg. Efter blot et par klik paa "Tjek for
// opdatering" var intern heap saa opbrugt at enhedens EGEN HTTPS-server
// ikke laengere kunne oprette TLS-sessions til browser-klienter
// ("mbedtls_ssl_setup returned -0x7F00" / ESP_ERR_MBEDTLS_SSL_SETUP_FAILED
// i CLI-loggen — bekraeftet af brugeren efter et par forsoeg med
// v7.9.10.33). Fix: selve arbejdet ligger nu i en separat funktion
// (github_check_do_work) der returnerer NORMALT — alle lokale C++-objekter
// destrueres dermed korrekt FOER github_check_worker (den egentlige
// FreeRTOS-task-entry) kalder vTaskDelete().
static void github_check_do_work(void)
{
  GH_STAGE(1);  // funktion startet
  GithubCheckResult *res = &g_github_check_result;
  memset(res, 0, sizeof(*res));

  const char *ca = get_github_ca_bundle();
  GH_STAGE(2);  // CA-bundle hentet/kopieret
  if (!ca) {
    snprintf(res->err, sizeof(res->err), "CA bundle unavailable");
    return;
  }

  ESP_LOGI(TAG, "GitHub check (baggrundstask): starter HTTPS GET mod api.github.com, fri heap=%u", (unsigned)ESP.getFreeHeap());

  WiFiClientSecure client;
  GH_STAGE(3);  // WiFiClientSecure konstrueret
  client.setCACert(ca);
  GH_STAGE(4);  // setCACert() returneret (PEM parset)
  HTTPClient http;
  http.setConnectTimeout(6000);
  http.setTimeout(6000);

  if (!http.begin(client, "https://api.github.com/repos/" GITHUB_OWNER_REPO "/releases/latest")) {
    ESP_LOGE(TAG, "GitHub check: http.begin() fejlede");
    snprintf(res->err, sizeof(res->err), "Could not begin HTTPS request");
    return;
  }
  GH_STAGE(5);  // http.begin() returneret
  // GitHub's API afviser requests uden en User-Agent header
  http.addHeader("User-Agent", "HyberFusion-PLC-OTA");
  http.addHeader("Accept", "application/vnd.github+json");

  ESP_LOGI(TAG, "GitHub check: kalder http.GET()...");
  GH_STAGE(6);  // lige foer selve TLS-handshake+GET (mistænkt hovedkandidat)
  int httpCode = http.GET();
  GH_STAGE(7);  // http.GET() returneret (TLS-handshake overlevet)
  res->http_code = httpCode;
  g_gh_debug_http_code = httpCode;
  ESP_LOGI(TAG, "GitHub check: http.GET() returnerede %d, fri heap=%u", httpCode, (unsigned)ESP.getFreeHeap());
  if (httpCode != 200) {
    http.end();
    GH_STAGE(8);  // http.end() efter fejl-statuskode
    return;
  }

  JsonDocument doc;
  GH_STAGE(9);  // lige foer JSON-parsing (anden mistænkt kandidat)
  DeserializationError jerr = deserializeJson(doc, http.getStream());
  GH_STAGE(10);  // JSON-parsing returneret
  http.end();
  ESP_LOGI(TAG, "GitHub check: JSON parse %s, fri heap=%u", jerr ? "FEJLEDE" : "OK", (unsigned)ESP.getFreeHeap());
  if (jerr) {
    snprintf(res->err, sizeof(res->err), "Invalid JSON from GitHub");
    return;
  }
  GH_STAGE(11);  // JSON parset OK, gaar videre til felt-udtraekning

  const char *tag = doc["tag_name"] | "";
  const char *published = doc["published_at"] | "";
  strncpy(res->tag, tag, sizeof(res->tag) - 1);
  strncpy(res->published, published, sizeof(res->published) - 1);

  if (doc["assets"].is<JsonArray>()) {
    for (JsonObject a : doc["assets"].as<JsonArray>()) {
      const char *name = a["name"] | "";
      if (strcmp(name, GITHUB_RELEASE_ASSET_NAME) == 0) {
        const char *url = a["browser_download_url"] | (const char *)NULL;
        if (url) strncpy(res->asset_url, url, sizeof(res->asset_url) - 1);
        res->asset_size = a["size"] | 0;
        break;
      }
    }
  }
  res->json_ok = true;
}

// Brugeren rapporterede at enheden crasher (ESP_RST_PANIC) ved "Tjek for
// opdatering" — reproduceret direkte via API (curl mod
// /api/system/ota/github-check), altsaa IKKE et GUI-specifikt/samtidigheds-
// problem (heap var 114KB fri, langt over BUG-366's 48KB-graense, saa det er
// ikke samme klasse fejl som BUG-366/369). Ingen serial-konsol tilgaengelig
// til at faa et praecist backtrace. Stakken var 16384 byte — samme
// stoerrelsesorden som BUG-364 allerede viste er UTILSTRAEKKELIG for BLOT et
// TLS-haandtryk alene (https_wrapper.c bruger 10240 til det ENE formaal);
// denne task har OGSAA en fuld HTTPClient+JsonDocument-parsing oveni samme
// stak. Doblet til 32768 som den mest sandsynlige, evidensbaserede fix —
// samme "bump med reel margin"-princip som BUG-364/FEAT-010s HIGH-task-
// stack-fix (RAM har rigelig plads, 251KB+ fri).
// BUG-377: efter at have udelukket stack-stoerrelse, heap, core-affinitet/
// prioritet OG socket-headroom som aarsag (alle testet live, krascher
// identisk hver gang) staar tilbage: krasjet sker KONSEKVENT eengang EFTER
// vores egen kode er 100% faerdig (RTC-breadcrumbs viser konsekvent det
// allersidste trin naaet, en triviel `return`), mens klienten kun modtager
// HTTP-headerne, aldrig selve JSON-kroppen. Faelles for alle test-forsoeg:
// httpd-HANDLEREN holdt forbindelsen AABEN OG BLOKERET i flere sekunder
// (semaphore-ventetid paa baggrundstasken) foer den overhovedet begyndte at
// sende noget svar. I stedet for at blive ved med at gaette paa PRAeCIS
// hvilken ESP-IDF/lwIP-intern mekanisme der reagerer daarligt paa dette,
// fjernes selve moensteret: handleren blokerer nu ALDRIG paa forbindelsen.
// `POST` starter tjekket og svarer STRAKS ("started"), imens klienten
// POLLER resultatet via `GET` paa samme URI — nøjagtig samme
// start+poll-moenster som allerede bruges for firmware-download/-flash
// (ota_state + GET .../ota/status). Semaphoren beholdes (harmløs) i
// tilfaelde af fremtidig brug, men INGEN handler venter laengere paa den.
enum { GH_CHECK_IDLE = 0, GH_CHECK_RUNNING = 1, GH_CHECK_DONE = 2 };
static volatile int g_gh_check_state = GH_CHECK_IDLE;

static void github_check_worker(void *pv)
{
  (void)pv;
  github_check_do_work();  // alle lokale C++-objekter (client/http/doc) destrueres normalt her
  GH_STAGE(12);  // do_work() returneret til worker-tasken
  g_gh_check_state = GH_CHECK_DONE;
  xSemaphoreGive(g_github_check_sem);
  GH_STAGE(13);  // semaphore givet, lige foer vTaskDelete
  vTaskDelete(NULL);
}

// MIDLERTIDIG DIAGNOSTIK — se kommentar ved GH_STAGE-makroen ovenfor. Læses
// EFTER en evt. panic-genstart for at se hvilket trin github-check naaede.
esp_err_t api_handler_ota_github_debug(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_OTA(req);
  char buf[160];
  if (g_gh_debug_magic == GH_DEBUG_MAGIC) {
    snprintf(buf, sizeof(buf), "{\"valid\":true,\"last_stage\":%u,\"last_http_code\":%d}",
             (unsigned)g_gh_debug_stage, (int)g_gh_debug_http_code);
  } else {
    snprintf(buf, sizeof(buf), "{\"valid\":false,\"note\":\"Ingen breadcrumb siden sidste power-on-reset\"}");
  }
  return api_send_json(req, buf);
}

// POST /api/system/ota/github-check — starter tjekket, svarer STRAKS uden
// at vente paa noget. Resultatet hentes efterfoelgende via GET (poll).
esp_err_t api_handler_ota_github_check_start(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_OTA(req);

  if (ota_state.in_progress) {
    return api_send_error(req, 409, "OTA already in progress");
  }
  if (g_gh_check_state == GH_CHECK_RUNNING) {
    return api_send_error(req, 409, "GitHub-check allerede i gang");
  }

  // BUG-366: undgaa et TLS-haandtryk der risikerer at crashe enheden hvis
  // intern heap er for fragmenteret — fejl paent i stedet.
  if (!github_ota_internal_heap_ok("GitHub check")) {
    return api_send_error(req, 503, "Ikke nok sammenhaengende intern hukommelse til TLS lige nu - proev igen senere");
  }

  if (!g_github_check_sem) {
    g_github_check_sem = xSemaphoreCreateBinary();
  }
  xSemaphoreTake(g_github_check_sem, 0);  // dræn evt. gammelt signal fra et timeout'et forsøg

  g_gh_check_state = GH_CHECK_RUNNING;
  BaseType_t created = xTaskCreatePinnedToCore(github_check_worker, "gh_check", 32768, NULL, 1, NULL, tskNO_AFFINITY);
  if (created != pdPASS) {
    g_gh_check_state = GH_CHECK_IDLE;
    return api_send_error(req, 500, "Kunne ikke starte GitHub-check baggrundstask");
  }

  return api_send_json(req, "{\"status\":\"started\"}");
}

// GET /api/system/ota/github-check — poller resultatet af det tjek der blev
// startet via POST ovenfor. Ingen blokerende ventetid overhovedet — svarer
// altid straks med den aktuelle tilstand.
esp_err_t api_handler_ota_github_check_poll(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_OTA(req);

  if (g_gh_check_state == GH_CHECK_IDLE) {
    return api_send_json(req, "{\"state\":\"idle\"}");
  }
  if (g_gh_check_state == GH_CHECK_RUNNING) {
    return api_send_json(req, "{\"state\":\"running\"}");
  }

  GH_STAGE(14);  // klient har afhentet et FAERDIGT resultat (poll-vejen)
  GithubCheckResult *res = &g_github_check_result;
  g_gh_check_state = GH_CHECK_IDLE;  // resultatet er nu "afhentet" — naeste POST starter et helt nyt tjek
  if (res->err[0]) {
    return api_send_error(req, 502, res->err);
  }
  GH_STAGE(15);  // intet hard err, tjekker http_code
  if (res->http_code != 200) {
    char msg[96];
    snprintf(msg, sizeof(msg), "GitHub API returned HTTP %d", res->http_code);
    GH_STAGE(16);  // msg bygget, lige foer api_send_error() for non-200
    esp_err_t r = api_send_error(req, 502, msg);
    GH_STAGE(17);  // api_send_error() returneret normalt
    return r;
  }
  if (!res->tag[0]) {
    return api_send_error(req, 502, "No releases found (repo has no published releases yet)");
  }

  bool has_asset = res->asset_url[0] != '\0';
  bool newer = has_asset && compare_versions(res->tag, PROJECT_VERSION) > 0;

  g_github_release_cache.valid = false;
  if (has_asset && newer) {
    strncpy(g_github_release_cache.asset_url, res->asset_url, sizeof(g_github_release_cache.asset_url) - 1);
    g_github_release_cache.asset_url[sizeof(g_github_release_cache.asset_url) - 1] = '\0';
    g_github_release_cache.asset_size = res->asset_size;
    g_github_release_cache.valid = true;
  }

  JsonDocument resp;
  resp["state"] = "done";
  resp["available"] = newer;
  resp["current_version"] = PROJECT_VERSION;
  resp["latest_version"] = res->tag;
  resp["asset_size"] = res->asset_size;
  resp["published_at"] = res->published;
  if (!has_asset) {
    resp["message"] = "Ingen '" GITHUB_RELEASE_ASSET_NAME "'-asset fundet i seneste release";
  }

  char buf[512];
  serializeJson(resp, buf, sizeof(buf));
  return api_send_json(req, buf);
}

// BUG-367: samme baggrundstask-princip som github_check_worker ovenfor.
// Download+flash-loopet havde allerede sit eget 30s-stalde-tjek (uaendret
// nedenfor), men det DAEKKEDE IKKE opstartsfasen (DNS+TLS-connect FOER
// download-loopet naas) — samme hul som i check-varianten. ota_state
// (received/total/state/error_msg/new_version) bruges i forvejen til at
// rapportere fremdrift (samme felter som manuel upload allerede skriver
// til), saa baggrundstasken skriver blot ind i den, ganske som foer — kun
// AT den koerer i sin egen task, samt at httpd-handleren nu venter med en
// oevre graense i stedet for ubegraenset, er nyt.
static SemaphoreHandle_t g_github_install_sem = NULL;

struct GithubInstallCtx {
  char asset_url[384];
};
static GithubInstallCtx g_github_install_ctx;

// BUG-368: samme vTaskDelete()-skipper-C++-destruktorer-fejl som i
// github_check_worker (se kommentar der) — arbejdet ligger derfor ogsaa her
// i en separat funktion der returnerer NORMALT, saa `client`/`http` naar at
// blive destrueret korrekt foer github_install_worker kalder vTaskDelete().
static bool github_install_do_work(void)
{
  const char *ca = get_github_ca_bundle();
  if (!ca) {
    snprintf(ota_state.error_msg, sizeof(ota_state.error_msg), "CA bundle unavailable");
    ota_state.state = OTA_STATE_ERROR;
    ota_state.in_progress = 0;
    return false;
  }

  const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
  if (!update_partition) {
    snprintf(ota_state.error_msg, sizeof(ota_state.error_msg), "No OTA partition found");
    ota_state.state = OTA_STATE_ERROR;
    ota_state.in_progress = 0;
    return false;
  }

  ESP_LOGI(TAG, "GitHub install (baggrundstask): starter download, fri heap=%u", (unsigned)ESP.getFreeHeap());

  WiFiClientSecure client;
  client.setCACert(ca);
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // GitHub's browser_download_url redirecter til objects.githubusercontent.com

  if (!http.begin(client, g_github_install_ctx.asset_url)) {
    ESP_LOGE(TAG, "GitHub install: http.begin() fejlede");
    snprintf(ota_state.error_msg, sizeof(ota_state.error_msg), "Could not begin download");
    ota_state.state = OTA_STATE_ERROR;
    ota_state.in_progress = 0;
    return false;
  }
  http.addHeader("User-Agent", "HyberFusion-PLC-OTA");

  ESP_LOGI(TAG, "GitHub install: kalder http.GET()...");
  int httpCode = http.GET();
  ESP_LOGI(TAG, "GitHub install: http.GET() returnerede %d, fri heap=%u", httpCode, (unsigned)ESP.getFreeHeap());
  if (httpCode != 200) {
    char msg[96];
    snprintf(msg, sizeof(msg), "Download HTTP %d", httpCode);
    http.end();
    snprintf(ota_state.error_msg, sizeof(ota_state.error_msg), "%s", msg);
    ota_state.state = OTA_STATE_ERROR;
    ota_state.in_progress = 0;
    return false;
  }

  int content_len = http.getSize();
  if (content_len <= 0 || content_len > OTA_MAX_FIRMWARE_SIZE) {
    http.end();
    snprintf(ota_state.error_msg, sizeof(ota_state.error_msg), "Invalid content length: %d", content_len);
    ota_state.state = OTA_STATE_ERROR;
    ota_state.in_progress = 0;
    return false;
  }
  ota_state.total = (uint32_t)content_len;

  esp_ota_handle_t ota_handle = 0;
  esp_err_t err = esp_ota_begin(update_partition, content_len, &ota_handle);
  if (err != ESP_OK) {
    http.end();
    snprintf(ota_state.error_msg, sizeof(ota_state.error_msg), "esp_ota_begin failed: 0x%x", (int)err);
    ota_state.state = OTA_STATE_ERROR;
    ota_state.in_progress = 0;
    return false;
  }

  char *chunk_buf = (char *)malloc(OTA_CHUNK_SIZE);
  if (!chunk_buf) {
    esp_ota_abort(ota_handle);
    http.end();
    snprintf(ota_state.error_msg, sizeof(ota_state.error_msg), "Failed to allocate chunk buffer");
    ota_state.state = OTA_STATE_ERROR;
    ota_state.in_progress = 0;
    return false;
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
    return false;
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
    return false;
  }

  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK) {
    snprintf(ota_state.error_msg, sizeof(ota_state.error_msg), "Failed to set boot partition: 0x%x", (int)err);
    ota_state.state = OTA_STATE_ERROR;
    ota_state.in_progress = 0;
    ESP_LOGE(TAG, "%s", ota_state.error_msg);
    return false;
  }

  ota_state.state = OTA_STATE_DONE;
  ESP_LOGI(TAG, "GitHub OTA complete: %lu bytes, version: %s. Rebooting in %dms...",
           (unsigned long)received_total,
           ota_state.new_version[0] ? ota_state.new_version : "unknown",
           OTA_REBOOT_DELAY_MS);
  return true;
}

static void github_install_worker(void *pv)
{
  (void)pv;
  bool success = github_install_do_work();  // client/http destrueres normalt her, uanset udfald
  xSemaphoreGive(g_github_install_sem);
  if (success) {
    xTaskCreate(ota_reboot_task, "ota_reboot", 2048, NULL, 5, NULL);
  }
  vTaskDelete(NULL);
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
  // BUG-366: samme hurtige netvaerks-forhaandstjek som github-check
  if (!wifi_driver_is_connected() && !ethernet_driver_is_connected()) {
    return api_send_error(req, 502, "Ingen WiFi/Ethernet-forbindelse - kan ikke naa GitHub");
  }
  // BUG-366: samme heap-forhaandstjek som github-check
  if (!github_ota_internal_heap_ok("GitHub install")) {
    return api_send_error(req, 503, "Ikke nok sammenhaengende intern hukommelse til TLS lige nu - proev igen senere");
  }

  strncpy(g_github_install_ctx.asset_url, g_github_release_cache.asset_url, sizeof(g_github_install_ctx.asset_url) - 1);
  g_github_install_ctx.asset_url[sizeof(g_github_install_ctx.asset_url) - 1] = '\0';

  ota_state.in_progress = 1;
  ota_state.state = OTA_STATE_RECEIVING;
  ota_state.received = 0;
  ota_state.total = g_github_release_cache.asset_size;
  ota_state.error_msg[0] = '\0';
  ota_state.new_version[0] = '\0';

  if (!g_github_install_sem) {
    g_github_install_sem = xSemaphoreCreateBinary();
  }
  xSemaphoreTake(g_github_install_sem, 0);  // dræn evt. gammelt signal

  // BUG-377: samme aendring som github-check — handleren blokerer IKKE
  // laengere paa forbindelsen imens baggrundstasken downloader+flasher
  // (tidligere op til 90s!). Svarer STRAKS "started"; fremdrift/resultat
  // afhentes udelukkende via den ALLEREDE eksisterende
  // `GET /api/system/ota/status` (ota_state — samme felter som manuel
  // .bin-upload allerede rapporterer progress igennem, ingen ny endpoint
  // noedvendig). Ved succes reboot'er enheden selv (github_install_worker
  // starter ota_reboot_task); klienten ser det ved at status skifter til
  // OTA_STATE_DONE og/eller forbindelsen til sidst dropper ved reboot.
  BaseType_t created = xTaskCreatePinnedToCore(github_install_worker, "gh_install", 32768, NULL, 1, NULL, tskNO_AFFINITY);
  if (created != pdPASS) {
    ota_state.state = OTA_STATE_ERROR;
    ota_state.in_progress = 0;
    snprintf(ota_state.error_msg, sizeof(ota_state.error_msg), "Kunne ikke starte baggrundstask");
    return api_send_error(req, 500, ota_state.error_msg);
  }

  return api_send_json(req, "{\"status\":\"started\"}");
}
