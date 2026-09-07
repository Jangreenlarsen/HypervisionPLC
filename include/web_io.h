/**
 * @file web_io.h
 * @brief Web-baseret I/O-konfigurationsside for Counters/Timers/GPIO (FEAT-171)
 *
 * Serverer siden på /io — GUI for de tre subsystemer der hidtil kun havde
 * CLI/REST-adgang (Fase B, opfølgning på FEAT-170's system.html-oprydning).
 */

#ifndef WEB_IO_H
#define WEB_IO_H

#include <esp_http_server.h>

/**
 * GET /io - Serve I/O Configuration HTML page
 */
esp_err_t web_io_handler(httpd_req_t *req);

#endif // WEB_IO_H
