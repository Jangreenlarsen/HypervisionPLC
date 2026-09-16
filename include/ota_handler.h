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

#endif // OTA_HANDLER_H
