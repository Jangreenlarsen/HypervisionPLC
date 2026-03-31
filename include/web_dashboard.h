/**
 * @file web_dashboard.h
 * @brief Web-based metrics dashboard (v7.3.1)
 *
 * Serves a live dashboard at / showing Prometheus metrics
 * from /api/metrics with auto-refresh.
 */

#ifndef WEB_DASHBOARD_H
#define WEB_DASHBOARD_H

#include <esp_http_server.h>

/**
 * GET / - Serve dashboard HTML page
 */
esp_err_t web_dashboard_handler(httpd_req_t *req);

#endif // WEB_DASHBOARD_H
