/**
 * @file web_ota.h
 * @brief Web-based OTA firmware update page (FEAT-031)
 *
 * Serves OTA management page at /ota with firmware upload,
 * progress tracking, and rollback controls.
 */

#ifndef WEB_OTA_H
#define WEB_OTA_H

#include <esp_http_server.h>

/**
 * GET /ota - Serve OTA firmware update HTML page
 */
esp_err_t web_ota_handler(httpd_req_t *req);

#endif // WEB_OTA_H
