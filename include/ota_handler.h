/**
 * @file ota_handler.h
 * @brief OTA firmware update via HTTP API (FEAT-031)
 *
 * Provides chunked firmware upload, progress tracking, and rollback.
 * Uses ESP-IDF OTA APIs with 4KB streaming writes (no full buffering).
 */

#ifndef OTA_HANDLER_H
#define OTA_HANDLER_H

#include <esp_http_server.h>

/* OTA state constants */
#define OTA_STATE_IDLE       0
#define OTA_STATE_RECEIVING  1
#define OTA_STATE_VERIFYING  2
#define OTA_STATE_DONE       3
#define OTA_STATE_ERROR      4

/**
 * POST /api/system/ota - Upload firmware binary (chunked)
 * Expects raw .bin file in request body (Content-Type: application/octet-stream)
 */
esp_err_t api_handler_ota_upload(httpd_req_t *req);

/**
 * GET /api/system/ota/status - Poll OTA progress
 * Returns: { state, received, total, percent, error, new_version }
 */
esp_err_t api_handler_ota_status(httpd_req_t *req);

/**
 * POST /api/system/ota/rollback - Rollback to previous firmware
 * Only works before current firmware is validated
 */
esp_err_t api_handler_ota_rollback(httpd_req_t *req);

/**
 * POST /api/system/ota/github-check - Start a GitHub Releases check for a
 * newer firmware version (FEAT-169). Manual-only — never called
 * automatically — so the device never talks out to the internet without an
 * explicit, logged-in user action. BUG-377: returns IMMEDIATELY with
 * {"status":"started"} — does NOT block the connection while the background
 * task talks to GitHub (a long-held blocking connection was the suspected
 * trigger for a still-unresolved device panic). Poll the result via GET on
 * the same URI.
 */
esp_err_t api_handler_ota_github_check_start(httpd_req_t *req);

/**
 * GET /api/system/ota/github-check - Poll the result of the check started
 * via POST above. Never blocks. Returns {"state":"idle"|"running"} while
 * pending, or the full result (available/current_version/latest_version/
 * asset_size/published_at) once done — same shape the old blocking GET used
 * to return directly.
 */
esp_err_t api_handler_ota_github_check_poll(httpd_req_t *req);

/**
 * GET /api/system/ota/github-debug - MIDLERTIDIG diagnostik (fjernes igen).
 * Returnerer sidste "stage"-breadcrumb fra github_check_do_work(), gemt i
 * RTC_NOINIT_ATTR-hukommelse der overlever en panic-genstart, til at
 * indsnaevre hvor et krasch under GitHub-check sker uden serial-adgang.
 */
esp_err_t api_handler_ota_github_debug(httpd_req_t *req);

/**
 * POST /api/system/ota/github-install - Download the latest GitHub Release
 * asset (firmware.bin) and flash it via the same esp_ota_* flow as
 * api_handler_ota_upload() (FEAT-169). BUG-377: returns IMMEDIATELY with
 * {"status":"started"} — does NOT block the connection for the whole
 * download+flash (previously up to 90s). Progress/result is reported
 * exclusively via the already-existing GET /api/system/ota/status (same
 * ota_state fields the manual-upload endpoint already reports through).
 */
esp_err_t api_handler_ota_github_install(httpd_req_t *req);

#endif // OTA_HANDLER_H
