/**
 * @file web_ota.cpp
 * @brief Web-based OTA firmware update page (FEAT-031)
 *
 * LAYER 7: User Interface - OTA Update
 * Serves an OTA management page at "/ota" with:
 * - Firmware .bin file upload with progress bar
 * - Real-time status polling during upload
 * - Rollback to previous firmware
 * - Current firmware version display
 *
 * RAM impact: ~0 bytes runtime (HTML stored in flash/PROGMEM only)
 */

#include <esp_http_server.h>
#include <Arduino.h>
#include "web_ota.h"
#include "debug.h"

// FLASH-OPTIMERING: HTML/CSS/JS-kilden er flyttet til web/ota.html
// (redigér DER, ikke i denne fil). scripts/gzip_web_assets.py gzip-komprimerer
// den ved hver build til nedenstående ota_html_gz[]/_len, som serveres
// direkte med Content-Encoding: gzip.
#include "generated_web/ota_html_gz.h"

/* ============================================================================
 * HTTP HANDLER
 * ============================================================================ */

esp_err_t web_ota_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  return httpd_resp_send(req, (const char *)ota_html_gz, ota_html_gz_len);
}
