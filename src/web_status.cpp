/**
 * @file web_status.cpp
 * @brief Offentlig, login-fri statusside (FEAT-407)
 *
 * LAYER 7: User Interface - Offentlig statusside
 * RAM impact: ~0 bytes runtime (HTML stored in flash only)
 */

#include <esp_http_server.h>
#include <Arduino.h>
#include "web_status.h"
#include "debug.h"
#include "ip_acl.h"

// FLASH-OPTIMERING: HTML/CSS/JS-kilden er flyttet til web/status.html
// (redigér DER, ikke i denne fil). scripts/gzip_web_assets.py gzip-komprimerer
// den ved hver build til nedenstående status_html_gz[]/_len, som serveres
// direkte med Content-Encoding: gzip.
#include "generated_web/status_html_gz.h"

esp_err_t web_status_handler(httpd_req_t *req)
{
  // FEAT-399: en blokeret IP maa ALDRIG naa saa langt som til at se selve
  // siden — se web_io.cpp for den fulde begrundelse. IP-ACL gaelder
  // UAFHAENGIGT af RBAC-login, og denne side er bevidst login-fri (FEAT-407),
  // saa dette er dens ENESTE adgangskontrol.
  if (!ip_acl_check_req(req, ACL_SVC_HTTP)) {
    return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Blocked by IP ACL");
  }
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  return httpd_resp_send(req, (const char *)status_html_gz, status_html_gz_len);
}
