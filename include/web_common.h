/**
 * @file web_common.h
 * @brief Shared CSS/JS asset handlers (flash-optimering v2)
 *
 * Serves web/common.css and web/common.js -- CSS-regler og JS-funktioner
 * der var byte-identiske på tværs af >=2 af de 8 admin-siderne, udtrukket
 * til ét sted i stedet for kopieret ind i hver side. Se web/common.css og
 * web/common.js's egne header-kommentarer for hvad der bevidst IKKE blev
 * udtrukket (sider med selv en lille formuleringsforskel beholder deres
 * egen kopi -- se BUGS_INDEX.md for begrundelsen).
 */

#ifndef WEB_COMMON_H
#define WEB_COMMON_H

#include <esp_http_server.h>

/** GET /common.css - Serve shared stylesheet (gzip) */
esp_err_t web_common_css_handler(httpd_req_t *req);

/** GET /common.js - Serve shared script (gzip) */
esp_err_t web_common_js_handler(httpd_req_t *req);

#endif // WEB_COMMON_H
