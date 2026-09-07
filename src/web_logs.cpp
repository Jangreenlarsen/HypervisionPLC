/**
 * @file web_logs.cpp
 * @brief Web-based Request Audit Log viewer page (FEAT-033)
 *
 * LAYER 7: User Interface - Request Audit Log
 * RAM impact: ~0 bytes runtime (HTML stored in flash only)
 */

#include <esp_http_server.h>
#include <Arduino.h>
#include "web_logs.h"
#include "debug.h"

// FLASH-OPTIMERING: HTML/CSS/JS-kilden er flyttet til web/logs.html
// (redigér DER, ikke i denne fil). scripts/gzip_web_assets.py gzip-komprimerer
// den ved hver build til nedenstående logs_html_gz[]/_len, som serveres
// direkte med Content-Encoding: gzip.
#include "generated_web/logs_html_gz.h"

esp_err_t web_logs_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  return httpd_resp_send(req, (const char *)logs_html_gz, logs_html_gz_len);
}
