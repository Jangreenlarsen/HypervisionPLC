/**
 * @file web_cli.cpp
 * @brief Web-based CLI console (v7.6.1)
 *
 * LAYER 7: User Interface - Web CLI
 * Serves a standalone CLI console page at /cli
 * Uses /api/cli endpoint for command execution.
 *
 * RAM impact: ~0 bytes runtime (HTML stored in flash/PROGMEM only)
 */

#include <esp_http_server.h>
#include <Arduino.h>
#include "web_cli.h"
#include "debug.h"

// FLASH-OPTIMERING: HTML/CSS/JS-kilden er flyttet til web/cli.html
// (redigér DER, ikke i denne fil). scripts/gzip_web_assets.py gzip-komprimerer
// den ved hver build til nedenstående cli_html_gz[]/_len, som serveres
// direkte med Content-Encoding: gzip.
#include "generated_web/cli_html_gz.h"

/* ============================================================================
 * HTTP HANDLER
 * ============================================================================ */

esp_err_t web_cli_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  return httpd_resp_send(req, (const char *)cli_html_gz, cli_html_gz_len);
}
