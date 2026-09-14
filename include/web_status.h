/**
 * @file web_status.h
 * @brief Offentlig, login-fri statusside (FEAT-407)
 *
 * Serves den offentlige statusside paa "/" (roden — overtaget fra det
 * tidligere Dashboard, som nu bor paa /dashboard og kraever login, se
 * BUG-406). Viser et admin-udvalgt subset af dashboardets kort, drevet af
 * GET /api/metrics/public (samme metrics minus register-dumpet) og
 * GET /api/public-dashboard/cards (hvilke kort er valgt).
 */

#ifndef WEB_STATUS_H
#define WEB_STATUS_H

#include <esp_http_server.h>

/**
 * GET / - Serve den offentlige statusside (ingen RBAC-login kraevet)
 */
esp_err_t web_status_handler(httpd_req_t *req);

#endif // WEB_STATUS_H
