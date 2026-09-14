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
#include "ip_acl.h"

// FLASH-OPTIMERING: HTML/CSS/JS-kilden er flyttet til web/logs.html
// (redigér DER, ikke i denne fil). scripts/gzip_web_assets.py gzip-komprimerer
// den ved hver build til nedenstående logs_html_gz[]/_len, som serveres
// direkte med Content-Encoding: gzip.
#include "generated_web/logs_html_gz.h"

esp_err_t web_logs_handler(httpd_req_t *req)
{
  // FEAT-399 (brugerkrav): en blokeret IP maa ALDRIG naa saa langt som til at
  // se selve login-skaermen — se web_io.cpp for den fulde begrundelse.
  if (!ip_acl_check_req(req, ACL_SVC_HTTP)) {
    return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Blocked by IP ACL");
  }
  httpd_resp_set_type(req, "text/html");
  // BUG-394: se web_io.cpp for begrundelse (bfcache-stale-login-tilstand paa iOS Safari)
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  return httpd_resp_send(req, (const char *)logs_html_gz, logs_html_gz_len);
}
