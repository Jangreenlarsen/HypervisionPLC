/**
 * @file api_audit_log.h
 * @brief REST API request audit log (FEAT-033)
 *
 * Ringbuffer over de seneste API-requests (metode, sti, status, IP,
 * evt. brugernavn) — til fejlfinding og sikkerhedsovervågning.
 *
 * RAM-only (PSRAM naar tilgaengelig), nulstilles ved reboot — samme
 * arkitektur som system_log.h (FEAT-086/089) og mb_activity_log.h.
 *
 * Logges fra de to centrale respons-funktioner (api_send_error/
 * api_send_json i api_handlers.cpp), IKKE fra hver enkelt handler —
 * daekker dermed alle handlers der bruger disse to (langt de fleste),
 * men IKKE handlers der streamer chunked responses direkte via
 * httpd_resp_send_chunk() (fx syslog/aktivitetslog-visning) — en
 * bevidst afvejning for at undgaa at skulle røre ~100 handler-funktioner
 * individuelt for et v1 audit-log.
 */

#ifndef API_AUDIT_LOG_H
#define API_AUDIT_LOG_H

#include <Arduino.h>
#include <esp_http_server.h>

#define API_AUDIT_LOG_MAX  100  // RAM-only ring buffer size (FEAT-033 spec: 50-100)

typedef struct {
  uint32_t timestamp_ms;   // millis() ved logning
  uint32_t epoch_s;        // Unix-tid hvis NTP synkroniseret, 0 ellers
  uint16_t status;         // HTTP-statuskode sendt til klienten
  char     method[8];      // "GET", "POST", "PUT", "DELETE", osv.
  char     path[48];       // Request-URI (afkortet)
  char     ip[16];         // Klient-IP
  char     username[24];   // RBAC-brugernavn hvis kendt, "-" ellers
} api_audit_entry_t;

void api_audit_log_init(void);

/**
 * @brief Log én afsluttet request. Kaldes fra api_send_error()/api_send_json()
 * (api_handlers.cpp) — ikke ment til direkte brug fra individuelle handlers.
 * @param ip Klient-IP. SKAL indhentes af kalderen FOER httpd_resp_sendstr() —
 *   BUG-372: naar denne funktion selv forsoegte at laese Authorization-
 *   headeren via httpd_req_get_hdr_value_str() EFTER svaret var sendt,
 *   fejlede det stille paa rigtig hardware (bekraeftet ved test), saa
 *   username blev ALTID "-". IP (raa socket-fd via getpeername) virkede
 *   godt nok stadig efter send, men begge indhentes nu foer send for at
 *   vaere robust paa samme maade.
 * @param username RBAC-brugernavn hvis allerede kendt af kalderen, NULL/"" ellers.
 */
void api_audit_log_add(httpd_req_t *req, int status, const char *ip, const char *username);

void api_audit_log_clear(void);
void api_audit_log_set_enabled(bool enabled);
bool api_audit_log_is_enabled(void);
uint16_t api_audit_log_count(void);
bool api_audit_log_get(uint16_t idx, api_audit_entry_t *out);

#endif // API_AUDIT_LOG_H
