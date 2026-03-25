/**
 * @file web_editor.h
 * @brief Web-based ST Logic program editor (v7.2.3+)
 *
 * Serves a single-page HTML/CSS/JS editor at /editor
 * Uses existing /api/logic/* endpoints for all operations.
 */

#ifndef WEB_EDITOR_H
#define WEB_EDITOR_H

#include <esp_http_server.h>

/**
 * GET /editor - Serve ST Logic web editor HTML page
 */
esp_err_t web_editor_handler(httpd_req_t *req);

#endif // WEB_EDITOR_H
