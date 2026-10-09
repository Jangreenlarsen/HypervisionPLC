/**
 * @file api_ext_registers.h
 * @brief FEAT-486: REST-adgang til EKSTERNE Modbus-registre (RS485-masterens
 *        slaver og expansion boards) på samme vilkår som de interne
 *        /api/registers/... — fx til SCADA.
 *
 *   GET  /api/ext/rtu/{slave}/{hr|ir|coils|di}/{addr}[?count=&type=&wait=&max_age=]
 *   POST /api/ext/rtu/{slave}/{hr|coils}/{addr}            {"value":…[,"type":…]} | {"values":[…]}
 *   GET  /api/ext/mbx/{board}/{kanal}/{slave}/{type}/{addr}[?…]
 *   POST /api/ext/mbx/{board}/{kanal}/{slave}/{hr|coils}/{addr}
 *
 * Går gennem SAMME kø/cache som ST Logics MB_- og MBX_-funktioner (ingen direkte
 * bus-adgang fra httpd-tasken). Auth tjekkes af kalderen (api_handlers.cpp).
 */
#pragma once

#include <esp_http_server.h>

esp_err_t api_ext_registers_get(httpd_req_t *req);
esp_err_t api_ext_registers_post(httpd_req_t *req, const char *user, const char *ip);
