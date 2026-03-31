/**
 * @file web_cli.h
 * @brief Web-based CLI console (v7.6.1)
 *
 * Serves a standalone CLI console page at /cli
 * Uses /api/cli endpoint for command execution.
 */

#ifndef WEB_CLI_H
#define WEB_CLI_H

#include <esp_http_server.h>

/**
 * GET /cli - Serve web CLI console HTML page
 */
esp_err_t web_cli_handler(httpd_req_t *req);

#endif // WEB_CLI_H
