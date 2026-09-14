/**
 * @file web_common.cpp
 * @brief Shared CSS/JS asset handlers (flash-optimering v2)
 *
 * FLASH-OPTIMERING: HTML/CSS/JS-kilden er flyttet til web/common.css/
 * web/common.js (redigér DER, ikke i denne fil). scripts/gzip_web_assets.py
 * gzip-komprimerer dem ved hver build til nedenstående *_gz[]/_len, som
 * serveres direkte med Content-Encoding: gzip -- samme mønster som
 * web_dashboard.cpp osv.
 */

#include <esp_http_server.h>
#include <Arduino.h>
#include "web_common.h"
#include "ip_acl.h"
#include "generated_web/common_css_gz.h"
#include "generated_web/common_js_gz.h"

esp_err_t web_common_css_handler(httpd_req_t *req)
{
  if (!ip_acl_check_req(req, ACL_SVC_HTTP)) {
    return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Blocked by IP ACL");
  }
  httpd_resp_set_type(req, "text/css");
  // BUG-394-klassen: samme "no-store" som selve HTML-siderne, saa en OTA-
  // opdateret common.css/.js aldrig kan blive haengende forældet i en
  // browsers cache ved siden af en frisk-hentet HTML-side der forventer
  // den nye version.
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  return httpd_resp_send(req, (const char *)common_css_gz, common_css_gz_len);
}

esp_err_t web_common_js_handler(httpd_req_t *req)
{
  if (!ip_acl_check_req(req, ACL_SVC_HTTP)) {
    return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Blocked by IP ACL");
  }
  httpd_resp_set_type(req, "application/javascript");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  return httpd_resp_send(req, (const char *)common_js_gz, common_js_gz_len);
}
