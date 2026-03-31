/**
 * @file web_system.h
 * @brief Web-based system administration page (v7.4.0)
 *
 * Serves system management page at /system with backup/restore,
 * save/load, reboot, factory defaults, and persist group management.
 */

#ifndef WEB_SYSTEM_H
#define WEB_SYSTEM_H

#include <esp_http_server.h>

/**
 * GET /system - Serve system administration HTML page
 */
esp_err_t web_system_handler(httpd_req_t *req);

#endif // WEB_SYSTEM_H
