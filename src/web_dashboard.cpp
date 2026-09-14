/**
 * @file web_dashboard.cpp
 * @brief Web-based metrics dashboard (v7.4.0)
 *
 * LAYER 7: User Interface - Web Dashboard
 * Serves a single-page dashboard at "/" showing live Prometheus metrics.
 * Fetches /api/metrics every 3 seconds and parses Prometheus text format.
 *
 * Features: Live metrics, history graphs, alarms, register viewer, register map
 * RAM impact: ~0 bytes runtime (HTML stored in flash/PROGMEM only)
 */

#include <esp_http_server.h>
#include <Arduino.h>
#include "web_dashboard.h"
#include "debug.h"
#include "ip_acl.h"
// FLASH-OPTIMERING: HTML/CSS/JS-kilden er flyttet til web/dashboard.html
// (redigér DER, ikke i denne fil). scripts/gzip_web_assets.py gzip-komprimerer
// den ved hver build til nedenstående dashboard_html_gz[]/_len, som serveres
// direkte med Content-Encoding: gzip — ~120 KB rå HTML bliver til ~29 KB i
// flash. Se web_dashboard_handler() nederst i filen.
#include "generated_web/dashboard_html_gz.h"

/* ============================================================================
 * HTTP HANDLER
 * ============================================================================ */

esp_err_t web_dashboard_handler(httpd_req_t *req)
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
  return httpd_resp_send(req, (const char *)dashboard_html_gz, dashboard_html_gz_len);
}
