/**
 * @file web_io.cpp
 * @brief Web-baseret I/O-konfigurationsside for Counters/Timers/GPIO (FEAT-171)
 *
 * LAYER 7: User Interface - I/O Configuration
 * RAM impact: ~0 bytes runtime (HTML stored in flash only)
 */

#include <esp_http_server.h>
#include <Arduino.h>
#include "web_io.h"
#include "debug.h"
#include "ip_acl.h"

// FLASH-OPTIMERING: HTML/CSS/JS-kilden er flyttet til web/io.html
// (redigér DER, ikke i denne fil). scripts/gzip_web_assets.py gzip-komprimerer
// den ved hver build til nedenstående io_html_gz[]/_len, som serveres
// direkte med Content-Encoding: gzip.
#include "generated_web/io_html_gz.h"

esp_err_t web_io_handler(httpd_req_t *req)
{
  // FEAT-399 (brugerkrav): en blokeret IP maa ALDRIG naa saa langt som til at
  // se selve login-skaermen — tjekkes derfor her, foer siden overhovedet
  // serveres, ikke kun ved efterfoelgende API-kald. Plain HTTP er allerede
  // daekket tidligere (httpd_config.open_fn), dette er den eneste reelle
  // haandhaevelse for HTTPS (se ip_acl.h).
  if (!ip_acl_check_req(req, ACL_SVC_HTTP)) {
    return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Blocked by IP ACL");
  }
  httpd_resp_set_type(req, "text/html");
  // BUG-394: "no-cache" uden ETag/Last-Modified kan ikke revalideres, hvilket
  // lader iOS Safaris bfcache (tilbage/frem-navigation) gendanne siden fra
  // FOER login med gammel JS-tilstand — saa badge/login-status ser forkert
  // ud selvom brugeren rent faktisk er logget ind. "no-store" forhindrer
  // baade normal caching og goer siden bfcache-ineligible i WebKit.
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  return httpd_resp_send(req, (const char *)io_html_gz, io_html_gz_len);
}
