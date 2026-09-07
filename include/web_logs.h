/**
 * @file web_logs.h
 * @brief Web-based Request Audit Log viewer page (FEAT-033)
 *
 * Serves the logs page at /logs — viser api_audit_log-ringbufferen
 * (GET /api/system/logs) i en tabel, med opdatering/ryd-knapper.
 */

#ifndef WEB_LOGS_H
#define WEB_LOGS_H

#include <esp_http_server.h>

/**
 * GET /logs - Serve Request Audit Log HTML page
 */
esp_err_t web_logs_handler(httpd_req_t *req);

#endif // WEB_LOGS_H
