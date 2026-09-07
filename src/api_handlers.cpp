/**
 * @file api_handlers.cpp
 * @brief HTTP REST API endpoint handlers implementation (v6.0.0+)
 *
 * LAYER 1.5: Protocol (same level as Telnet server)
 * Responsibility: JSON response building and HTTP request handling
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>
#include <mbedtls/base64.h>

#include "api_handlers.h"
#include "http_server.h"
#include "constants.h"
#include <math.h>  // FEAT-034/035/036: lroundf() for analog setpoint
#include <SPIFFS.h>  // FEAT-082: SPIFFS.usedBytes()/totalBytes()
#include <nvs.h>     // FEAT-081: nvs_get_stats()
#include "system_log.h"  // FEAT-086/089
#include "api_audit_log.h"  // FEAT-033
#include "types.h"
#include "config_struct.h"
#include "registers.h"
#include "counter_engine.h"
#include "counter_config.h"
#include "counter_frequency.h"
#include "timer_engine.h"
#include "timer_config.h"
#include "st_logic_config.h"
#include "st_logic_engine.h"  // FEAT-007: st_logic_lock_variables()/_unlock_variables()
#include "wifi_driver.h"
#include "ethernet_driver.h"
#include "build_version.h"
#include "debug_flags.h"
#include "debug.h"
#include "config_save.h"
#include "config_load.h"
#include "config_apply.h"
#include "gpio_driver.h"
#include "network_manager.h"
#include "modbus_master.h"
#include "st_debug.h"
#include "watchdog_monitor.h"
#include "heartbeat.h"
#include "registers_persist.h"
#include "sse_events.h"
#include "cli_parser.h"
#include "cli_shell.h"
#include "rbac.h"
#include "mb_async.h"
#include "mb_activity_log.h"
#include "trend_recorder.h"
#include "ntp_driver.h"
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "API_HDLR";

// External functions from http_server.cpp for stats
extern void http_server_stat_request(void);
extern void http_server_stat_success(void);
extern void http_server_stat_client_error(void);
extern void http_server_stat_server_error(void);
extern void http_server_stat_auth_failure(void);
extern bool http_server_check_auth(httpd_req_t *req);
extern int http_server_auth_user(httpd_req_t *req);

// FEAT-028: Rate limiting (implemented later in this file)
bool http_rate_limit_check(httpd_req_t *req);

// Forward declarations for handlers used in suffix routing (defined later in this file)
esp_err_t api_handler_logic_source_get(httpd_req_t *req);
esp_err_t api_handler_logic_source_post(httpd_req_t *req);
esp_err_t api_handler_logic_enable(httpd_req_t *req);
esp_err_t api_handler_logic_disable(httpd_req_t *req);
esp_err_t api_handler_logic_reinit(httpd_req_t *req);
esp_err_t api_handler_logic_stats(httpd_req_t *req);
esp_err_t api_handler_logic_globals(httpd_req_t *req);  // FEAT-007
esp_err_t api_handler_logic_priority_post(httpd_req_t *req);  // FEAT-010
esp_err_t api_handler_logic_program_interval_post(httpd_req_t *req);  // FEAT-010
esp_err_t api_handler_counter_reset(httpd_req_t *req);
esp_err_t api_handler_counter_start(httpd_req_t *req);
esp_err_t api_handler_counter_stop(httpd_req_t *req);
static esp_err_t api_handler_counter_config_post(httpd_req_t *req);
static esp_err_t api_handler_counter_control_post(httpd_req_t *req);

/* ============================================================================
 * FEAT-085: ALARM HISTORY RINGBUFFER (v7.8.0)
 * ============================================================================ */

#define ALARM_LOG_MAX 32
#define ALARM_MSG_MAX 80

typedef struct {
  uint32_t timestamp_ms;  // millis() when alarm triggered
  char     message[ALARM_MSG_MAX];
  uint8_t  severity;      // 0=info, 1=warning, 2=critical
  bool     acknowledged;
  time_t   epoch;         // Real time if NTP synced, 0 otherwise
  char     source_ip[16]; // Client IP (if applicable, e.g. auth failures)
  char     username[32];  // Username attempted (if applicable)
} alarm_entry_t;

/* FEAT-154: alarmloggen flyttet fra intern DRAM til PSRAM (~4,4 KB frigjort).
 *
 * Baggrund: Arduino-frameworket har CONFIG_SPIRAM_USE_MALLOC=y med
 * ALWAYSINTERNAL=4096, saa alt der malloc'es over 4 KB havner AUTOMATISK i
 * PSRAM. Statiske/globale arrays som dette gaar derimod altid i intern DRAM
 * uanset den indstilling, og EXT_RAM_BSS_ATTR er ikke tilgaengelig her
 * (CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY er slaaet fra). Derfor
 * eksplicit heap_caps_malloc.
 *
 * Sikkert netop for DENNE buffer: den skrives kun ved alarmhaendelser og
 * laeses af dashboardets polling hvert 5. sekund — ingen ISR-adgang, ingen
 * DMA, ingen kritisk timing. (Til sammenligning blev g_logic_state og
 * g_persist_config bevidst IKKE flyttet: de laeses i hver loop()-iteration,
 * og PSRAM er SPI-tilgaaet og dermed langsommere.)
 *
 * Adgangssyntaksen alarm_log[i] er uaendret — kun typen skifter fra array
 * til peger. Alle brugssteder tjekker via alarm_log_ready(). */
static alarm_entry_t *alarm_log = NULL;
static uint8_t alarm_log_head = 0;   // Next write position
static uint8_t alarm_log_count = 0;  // Total entries (max ALARM_LOG_MAX)

/* Allokerer ved foerste brug. Returnerer false hvis der ikke kunne skaffes
 * hukommelse — saa springes logningen over i stedet for at dereferere NULL. */
static bool alarm_log_ready(void) {
  if (alarm_log) return true;
  size_t bytes = (size_t)ALARM_LOG_MAX * sizeof(alarm_entry_t);
  alarm_log = (alarm_entry_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  if (!alarm_log) {
    alarm_log = (alarm_entry_t *)malloc(bytes);  // fallback: intern heap
  }
  if (alarm_log) {
    memset(alarm_log, 0, bytes);
    return true;
  }
  return false;
}
static uint32_t alarm_check_prev_ms = 0;
static uint32_t alarm_prev_slave_crc = 0;
static uint32_t alarm_prev_master_timeout = 0;
static uint32_t alarm_prev_auth_fail = 0;
static uint32_t alarm_prev_write_denied = 0;
static bool alarm_sse_full_active = false;

static void alarm_log_add(uint8_t severity, const char *msg) {
  if (!alarm_log_ready()) return;  // FEAT-154
  alarm_entry_t *e = &alarm_log[alarm_log_head];
  e->timestamp_ms = millis();
  strncpy(e->message, msg, ALARM_MSG_MAX - 1);
  e->message[ALARM_MSG_MAX - 1] = '\0';
  e->severity = severity;
  e->acknowledged = false;
  e->epoch = ntp_driver_is_synced() ? ntp_driver_get_epoch() : 0;
  e->source_ip[0] = '\0';
  e->username[0] = '\0';
  alarm_log_head = (alarm_log_head + 1) % ALARM_LOG_MAX;
  if (alarm_log_count < ALARM_LOG_MAX) alarm_log_count++;
}

// Extended version with source IP and username (for auth failures etc.)
static void alarm_log_add_detail(uint8_t severity, const char *msg,
                                  const char *ip, const char *user) {
  if (!alarm_log_ready()) return;  // FEAT-154
  alarm_entry_t *e = &alarm_log[alarm_log_head];
  e->timestamp_ms = millis();
  strncpy(e->message, msg, ALARM_MSG_MAX - 1);
  e->message[ALARM_MSG_MAX - 1] = '\0';
  e->severity = severity;
  e->acknowledged = false;
  e->epoch = ntp_driver_is_synced() ? ntp_driver_get_epoch() : 0;
  if (ip && ip[0]) {
    strncpy(e->source_ip, ip, sizeof(e->source_ip) - 1);
    e->source_ip[sizeof(e->source_ip) - 1] = '\0';
  } else {
    e->source_ip[0] = '\0';
  }
  if (user && user[0]) {
    strncpy(e->username, user, sizeof(e->username) - 1);
    e->username[sizeof(e->username) - 1] = '\0';
  } else {
    e->username[0] = '\0';
  }
  alarm_log_head = (alarm_log_head + 1) % ALARM_LOG_MAX;
  if (alarm_log_count < ALARM_LOG_MAX) alarm_log_count++;
}

/* FEAT-086/089: faelles hjaelper til at hente klient-IP + RBAC-brugernavn for
 * en ALLEREDE AUTENTIFICERET request (til forskel fra
 * alarm_record_auth_failure_info's manuelle header-dekodning nedenfor, som
 * specifikt daekker 401/403-fejl-stien hvor der IKKE er en gyldig session).
 * Bruges ved alle nye system_log-kaldesteder der logger en vellykket
 * bruger-handling (reboot, registerskrivning, o.lign.). */
static void http_get_client_info(httpd_req_t *req, char *ip_out, size_t ip_len,
                                  char *user_out, size_t user_len) {
  if (ip_out && ip_len) ip_out[0] = '\0';
  if (user_out && user_len) user_out[0] = '\0';

  if (ip_out && ip_len) {
    int sockfd = httpd_req_to_sockfd(req);
    struct sockaddr_in6 addr6;
    socklen_t addr_len = sizeof(addr6);
    if (sockfd >= 0 && getpeername(sockfd, (struct sockaddr *)&addr6, &addr_len) == 0) {
      if (addr6.sin6_family == AF_INET) {
        struct sockaddr_in *addr4 = (struct sockaddr_in *)&addr6;
        inet_ntoa_r(addr4->sin_addr, ip_out, ip_len);
      } else if (addr6.sin6_family == AF_INET6) {
        struct in_addr mapped;
        memcpy(&mapped, &addr6.sin6_addr.un.u32_addr[3], 4);
        inet_ntoa_r(mapped, ip_out, ip_len);
      }
    }
  }

  if (user_out && user_len) {
    extern int http_server_auth_user(httpd_req_t *req);
    int uid = http_server_auth_user(req);
    const RbacUser *u = rbac_get_user(uid);
    if (u) {
      strncpy(user_out, u->username, user_len - 1);
      user_out[user_len - 1] = '\0';
    }
  }
}

// Track last auth failure details for alarm context
static char s_last_auth_fail_ip[16] = {0};
static char s_last_auth_fail_user[32] = {0};
static uint32_t s_write_denied_count = 0;
static char s_last_write_denied_ip[16] = {0};
static char s_last_write_denied_user[32] = {0};

// Called from api_send_error when 401 is sent
void alarm_record_auth_failure_info(const char *ip, const char *user) {
  if (ip && ip[0]) {
    strncpy(s_last_auth_fail_ip, ip, sizeof(s_last_auth_fail_ip) - 1);
    s_last_auth_fail_ip[sizeof(s_last_auth_fail_ip) - 1] = '\0';
  }
  if (user && user[0]) {
    strncpy(s_last_auth_fail_user, user, sizeof(s_last_auth_fail_user) - 1);
    s_last_auth_fail_user[sizeof(s_last_auth_fail_user) - 1] = '\0';
  }
}

// Called from api_send_error when 403 is sent (write privilege denied)
void alarm_record_write_denied_info(const char *ip, const char *user) {
  s_write_denied_count++;
  if (ip && ip[0]) {
    strncpy(s_last_write_denied_ip, ip, sizeof(s_last_write_denied_ip) - 1);
    s_last_write_denied_ip[sizeof(s_last_write_denied_ip) - 1] = '\0';
  }
  if (user && user[0]) {
    strncpy(s_last_write_denied_user, user, sizeof(s_last_write_denied_user) - 1);
    s_last_write_denied_user[sizeof(s_last_write_denied_user) - 1] = '\0';
  }
}

// Called periodically from metrics fetch to check for new alarms
void alarm_check_thresholds() {
  uint32_t now = millis();
  if (now - alarm_check_prev_ms < 3000) return;  // Check every 3s
  alarm_check_prev_ms = now;

  // Heap critical
  uint32_t heap = ESP.getFreeHeap();
  if (heap < 20000) {
    alarm_log_add(2, "Heap kritisk lav");
  } else if (heap < 30000) {
    alarm_log_add(1, "Heap advarsel");
  }

  // Modbus Slave CRC errors (rising)
  uint32_t slave_crc = g_persist_config.modbus_slave.crc_errors;
  if (slave_crc > alarm_prev_slave_crc && alarm_prev_slave_crc > 0) {
    uint32_t delta = slave_crc - alarm_prev_slave_crc;
    if (delta >= 5) {
      char buf[ALARM_MSG_MAX];
      snprintf(buf, sizeof(buf), "Modbus Slave CRC fejl +%lu RX <- ID:%u",
               (unsigned long)delta,
               g_persist_config.modbus_slave.slave_id);
      alarm_log_add(1, buf);
    }
  }
  alarm_prev_slave_crc = slave_crc;

  // Modbus Master timeouts (rising)
  uint32_t master_to = g_modbus_master_config.timeout_errors;
  if (master_to > alarm_prev_master_timeout && alarm_prev_master_timeout > 0) {
    uint32_t delta = master_to - alarm_prev_master_timeout;
    if (delta >= 3) {
      char buf[ALARM_MSG_MAX];
      snprintf(buf, sizeof(buf), "Modbus Master timeout +%lu TX -> ID:%u @%u",
               (unsigned long)delta,
               g_modbus_master_config.last_error_slave_id,
               g_modbus_master_config.last_error_address);
      alarm_log_add(1, buf);
    }
  }
  alarm_prev_master_timeout = master_to;

  // Auth failures (rising)
  const HttpServerStats *stats = http_server_get_stats();
  if (stats) {
    if (stats->auth_failures > alarm_prev_auth_fail && alarm_prev_auth_fail > 0) {
      uint32_t delta = stats->auth_failures - alarm_prev_auth_fail;
      if (delta >= 3) {
        char buf[ALARM_MSG_MAX];
        snprintf(buf, sizeof(buf), "HTTP auth failures +%lu", (unsigned long)delta);
        alarm_log_add_detail(2, buf, s_last_auth_fail_ip, s_last_auth_fail_user);
      }
    }
    alarm_prev_auth_fail = stats->auth_failures;
  }

  // Write privilege denied (403)
  if (s_write_denied_count > alarm_prev_write_denied && alarm_prev_write_denied > 0) {
    uint32_t delta = s_write_denied_count - alarm_prev_write_denied;
    if (delta >= 1) {
      char buf[ALARM_MSG_MAX];
      snprintf(buf, sizeof(buf), "Write privilege denied +%lu", (unsigned long)delta);
      alarm_log_add_detail(1, buf, s_last_write_denied_ip, s_last_write_denied_user);
    }
  }
  alarm_prev_write_denied = s_write_denied_count;

  // SSE max clients reached
  {
    int sse_active = sse_get_client_count();
    uint8_t sse_max = g_persist_config.network.http.sse_max_clients;
    if (sse_max > 0 && sse_active >= (int)sse_max) {
      if (!alarm_sse_full_active) {
        char buf[ALARM_MSG_MAX];
        snprintf(buf, sizeof(buf), "SSE server mættet %d/%d klienter", sse_active, (int)sse_max);
        alarm_log_add(1, buf);
        alarm_sse_full_active = true;
      }
    } else {
      alarm_sse_full_active = false;
    }
  }

  // ST Logic overruns
  st_logic_engine_state_t *ls = st_logic_get_state();
  if (ls && ls->total_cycles > 100) {
    float pct = (float)ls->cycle_overrun_count / ls->total_cycles * 100.0f;
    if (pct > 5.0f) {
      alarm_log_add(1, "ST Logic overrun rate > 5%");
    }
  }
}

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

int api_extract_id_from_uri(httpd_req_t *req, const char *prefix)
{
  const char *uri = req->uri;
  size_t prefix_len = strlen(prefix);

  // Check if URI starts with prefix
  if (strncmp(uri, prefix, prefix_len) != 0) {
    return -1;
  }

  // Extract ID after prefix
  const char *id_str = uri + prefix_len;
  if (*id_str == '\0') {
    return -1;
  }

  // Find end of ID (before any query params)
  char id_buf[8];
  int i = 0;
  while (*id_str && *id_str != '?' && *id_str != '/' && i < 7) {
    id_buf[i++] = *id_str++;
  }
  id_buf[i] = '\0';

  return atoi(id_buf);
}

esp_err_t api_send_error(httpd_req_t *req, int status, const char *error_msg)
{
  DebugFlags* dbg = debug_flags_get();
  if (dbg->http_api) {
    debug_printf("[API] %s -> %d %s\n", req->uri, status, error_msg);
  }

  char buf[256];
  snprintf(buf, sizeof(buf), "{\"error\":\"%s\",\"status\":%d}", error_msg, status);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
  httpd_resp_set_hdr(req, "Connection", "keep-alive");
  httpd_resp_set_hdr(req, "Keep-Alive", "timeout=15, max=100");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  // BUG-376: denne mapning daekkede kun 400/401/403/404/500 — enhver anden
  // kode (409, 429, 502, 503, 504, som bruges bredt i api_handlers.cpp/
  // ota_handler.cpp) faldt igennem til "400 Bad Request" paa selve
  // status-linjen, selvom JSON-brødteksten (bygget separat ovenfor via
  // `status`-parameteren) korrekt viste den TILTAENKTE kode. Klienten saa
  // dermed altid HTTP 400 paa ledningen for disse, uanset hvad JSON'en sagde.
  // static: httpd_resp_set_status() gemmer kun POINTEREN (kopierer ikke
  // strengen) og forventer den er gyldig frem til selve send-kaldet
  // (httpd_resp_sendstr() nedenfor) — en stack-lokal buffer ville risikere at
  // pege paa ugyldig hukommelse paa det tidspunkt. `static` er sikkert her
  // fordi denne httpd-instans koerer som ÉN enkelt-traadet worker-task
  // (jf. BUG-367s analyse) — ingen samtidig genindtraeden er mulig.
  static char status_line[40];
  const char *status_text =
    status == 400 ? "Bad Request" :
    status == 401 ? "Unauthorized" :
    status == 403 ? "Forbidden" :
    status == 404 ? "Not Found" :
    status == 409 ? "Conflict" :
    status == 429 ? "Too Many Requests" :
    status == 500 ? "Internal Server Error" :
    status == 502 ? "Bad Gateway" :
    status == 503 ? "Service Unavailable" :
    status == 504 ? "Gateway Timeout" : "Error";
  snprintf(status_line, sizeof(status_line), "%d %s", status, status_text);
  httpd_resp_set_status(req, status_line);

  // For 401/403, capture client IP and username BEFORE sending response (socket may close after send)
  char fail_ip[16] = {0};
  char fail_user[32] = {0};
  if (status == 401 || status == 403) {
    if (status == 401) {
      httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Modbus ESP32\"");
    }
    // Get client IP while socket is still valid
    int sockfd = httpd_req_to_sockfd(req);
    struct sockaddr_in6 addr6;
    socklen_t addr_len = sizeof(addr6);
    if (getpeername(sockfd, (struct sockaddr *)&addr6, &addr_len) == 0) {
      if (addr6.sin6_family == AF_INET) {
        struct sockaddr_in *addr4 = (struct sockaddr_in *)&addr6;
        inet_ntoa_r(addr4->sin_addr, fail_ip, sizeof(fail_ip));
      } else if (addr6.sin6_family == AF_INET6) {
        // Check for IPv4-mapped IPv6 (::ffff:x.x.x.x)
        struct in_addr mapped;
        memcpy(&mapped, &addr6.sin6_addr.un.u32_addr[3], 4);
        inet_ntoa_r(mapped, fail_ip, sizeof(fail_ip));
      }
    }
    // Extract username from Authorization header
    char auth_hdr[256] = {0};
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_hdr, sizeof(auth_hdr)) == ESP_OK) {
      const char *b64 = strstr(auth_hdr, "Basic ");
      if (b64) {
        b64 += 6;
        while (*b64 == ' ') b64++;
        unsigned char decoded[128];
        size_t decoded_len = 0;
        if (mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &decoded_len,
            (const unsigned char *)b64, strlen(b64)) == 0) {
          decoded[decoded_len] = '\0';
          char *colon = (char *)strchr((const char *)decoded, ':');
          if (colon) {
            *colon = '\0';
            strncpy(fail_user, (const char *)decoded, sizeof(fail_user) - 1);
          }
        }
      }
    }
  }

  // FEAT-033/BUG-372: audit-log-hook — IP+brugernavn skal indhentes FOER
  // httpd_resp_sendstr() (samme regel som 401/403-stien to sektioner
  // ovenfor allerede overholder, jf. kommentaren "capture ... BEFORE sending
  // response"). Bekraeftet ved test mod rigtig hardware: efter send()
  // returnerer httpd_req_get_hdr_value_str("Authorization") stille en fejl
  // (headeren kan ikke laengere laeses), saa et username-opslag EFTER send
  // gav altid "-" uanset autentificering — kun IP (raa socket-fd, forbliver
  // gyldig) kom korrekt igennem.
  char audit_ip[16] = {0};
  char audit_user[24] = {0};
  if (status == 401 || status == 403) {
    strncpy(audit_ip, fail_ip, sizeof(audit_ip) - 1);
    strncpy(audit_user, fail_user, sizeof(audit_user) - 1);
  } else {
    http_get_client_info(req, audit_ip, sizeof(audit_ip), audit_user, sizeof(audit_user));
  }

  httpd_resp_sendstr(req, buf);

  if (status == 401) {
    http_server_stat_auth_failure();
    alarm_record_auth_failure_info(fail_ip, fail_user);
    // FEAT-086: login-FEJL er et meningsfuldt haendelse (sikkerhedsrelevant,
    // sjaelden). Login-SUCCESS logges bevidst IKKE — API'et er stateless
    // Basic Auth, saa "success" ville betyde HVER ENESTE autentificerede
    // request (dashboard-polling m.m.), hvilket ville oversvoemme loggen
    // uden reel vaerdi.
    system_log_add_event((uint8_t)SYSLOG_SRC_REST, fail_user, fail_ip, "Login fejlede (401)");
  } else if (status == 403) {
    http_server_stat_client_error();
    alarm_record_write_denied_info(fail_ip, fail_user);
    system_log_add_event((uint8_t)SYSLOG_SRC_REST, fail_user, fail_ip, "Skriveadgang naegtet (403)");
  } else if (status >= 500) {
    http_server_stat_server_error();
  } else {
    http_server_stat_client_error();
  }

  // FEAT-033: audit-log-hook — dette er ét af de to centrale respons-punkter
  // (se api_audit_log.h for hvorfor her og ikke i hver enkelt handler).
  api_audit_log_add(req, status, audit_ip, audit_user);

  return ESP_OK;
}

esp_err_t api_send_json(httpd_req_t *req, const char *json_str)
{
  DebugFlags* dbg = debug_flags_get();
  if (dbg->http_api) {
    debug_printf("[API] %s -> 200 OK (%u bytes)\n", req->uri, (unsigned)strlen(json_str));
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
  httpd_resp_set_hdr(req, "Connection", "keep-alive");
  httpd_resp_set_hdr(req, "Keep-Alive", "timeout=15, max=100");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  // FEAT-033/BUG-372: audit-log-hook — SKAL indhentes FOER send (se
  // api_audit_log.h's dokumentation af hvorfor: Authorization-headeren kan
  // ikke laeses efter httpd_resp_sendstr()).
  char audit_ip[16], audit_user[24];
  http_get_client_info(req, audit_ip, sizeof(audit_ip), audit_user, sizeof(audit_user));

  httpd_resp_sendstr(req, json_str);
  http_server_stat_success();
  api_audit_log_add(req, 200, audit_ip, audit_user);

  return ESP_OK;
}

/* ============================================================================
 * AUTHENTICATION CHECK MACRO
 * ============================================================================ */

#define CHECK_API_ENABLED(req) \
  do { \
    if (!g_persist_config.network.http.api_enabled) { \
      return api_send_error(req, 403, "API disabled"); \
    } \
  } while(0)

// SECURITY_INDEX #7: rate-limit is now checked BEFORE the auth decision in
// all four CHECK_AUTH* macros (this one + WRITE/ROLE below + CHECK_AUTH_OTA
// in ota_handler.cpp). It used to run AFTER — a client that fails auth
// always hit 401 before ever reaching the rate-limiter, so Basic Auth
// guessing was effectively unthrottled. No behavior change for a client
// within its normal rate budget.
#define CHECK_AUTH(req) \
  do { \
    CHECK_API_ENABLED(req); \
    if (!http_rate_limit_check(req)) { \
      return api_send_error(req, 429, "Too many requests"); \
    } \
    if (!http_server_check_auth(req)) { \
      return api_send_error(req, 401, "Authentication required"); \
    } \
  } while(0)

#define CHECK_AUTH_WRITE(req) \
  do { \
    CHECK_API_ENABLED(req); \
    if (!http_rate_limit_check(req)) { \
      return api_send_error(req, 429, "Too many requests"); \
    } \
    int _uid = http_server_auth_user(req); \
    if (_uid < 0) { \
      return api_send_error(req, 401, "Authentication required"); \
    } \
    if (!rbac_has_write(_uid)) { \
      return api_send_error(req, 403, "Write privilege required"); \
    } \
  } while(0)

#define CHECK_AUTH_ROLE(req, role) \
  do { \
    CHECK_API_ENABLED(req); \
    if (!http_rate_limit_check(req)) { \
      return api_send_error(req, 429, "Too many requests"); \
    } \
    int _uid = http_server_auth_user(req); \
    if (_uid < 0) { \
      return api_send_error(req, 401, "Authentication required"); \
    } \
    if (!rbac_has_role(_uid, role)) { \
      return api_send_error(req, 403, "Insufficient role"); \
    } \
  } while(0)

/* ============================================================================
 * GET /api/ - API Discovery (list all endpoints)
 * GET /api/schema - OpenAPI 3.0 schema (FEAT-029)
 *
 * FEAT-029: begge endpoints deler nu ÉN kilde (API_ROUTES nedenfor) i stedet
 * for hver sin haandskrevne liste. Forud for dette var /api/'s liste (82
 * entries) allerede ude af sync med de faktisk registrerede routes (105+ i
 * http_server.cpp) — en delt tabel forhindrer at /api/schema arver samme
 * drift. Listen er stadig haandholdt (ingen runtime-introspektion af
 * ESP-IDF's httpd-registry findes), saa en ny route her SKAL ogsaa
 * tilfoejes til API_ROUTES for at dukke op i begge endpoints.
 * ============================================================================ */

typedef struct {
  const char *method;
  const char *path;
  const char *desc;
} api_route_info_t;

static const api_route_info_t API_ROUTES[] = {
  {"GET",    "/api/",                              "List endpoints"},
  {"GET",    "/api/schema",                        "OpenAPI 3.0 schema (FEAT-029)"},
  {"GET",    "/api/status",                        "System status"},
  {"GET",    "/api/config",                        "Full configuration"},
  {"GET",    "/api/counters",                      "All counters"},
  {"GET",    "/api/counters/{1-4}",                 "Single counter"},
  {"POST",   "/api/counters/{1-4}",                 "Configure counter"},
  {"POST",   "/api/counters/{1-4}/reset",            "Reset counter"},
  {"POST",   "/api/counters/{1-4}/start",            "Start counter"},
  {"POST",   "/api/counters/{1-4}/stop",             "Stop counter"},
  {"POST",   "/api/counters/{1-4}/control",          "Counter control"},
  {"DELETE", "/api/counters/{1-4}",                 "Delete counter"},
  {"GET",    "/api/timers",                        "All timers"},
  {"GET",    "/api/timers/{1-4}",                    "Single timer"},
  {"POST",   "/api/timers/{1-4}",                    "Configure timer"},
  {"POST",   "/api/timers/{1-4}/control",             "Timer control"},
  {"DELETE", "/api/timers/{1-4}",                    "Delete timer"},
  {"GET",    "/api/registers/hr/{addr}",            "Read HR"},
  {"POST",   "/api/registers/hr/{addr}",            "Write HR"},
  {"GET",    "/api/registers/ir/{addr}",            "Read IR"},
  {"GET",    "/api/registers/coils/{addr}",         "Read coil"},
  {"POST",   "/api/registers/coils/{addr}",         "Write coil"},
  {"GET",    "/api/registers/di/{addr}",            "Read DI"},
  {"GET",    "/api/gpio",                          "All GPIO mappings"},
  {"GET",    "/api/gpio/{pin}",                     "Single GPIO"},
  {"POST",   "/api/gpio/{pin}",                     "Write GPIO"},
  {"DELETE", "/api/gpio/{pin}",                     "Remove GPIO mapping"},
  {"POST",   "/api/gpio/2/heartbeat",               "Heartbeat control"},
  {"GET",    "/api/logic",                         "ST Logic programs"},
  {"GET",    "/api/logic/{1-4}",                     "Single program"},
  {"GET",    "/api/logic/{1-4}/source",              "Download ST code"},
  {"POST",   "/api/logic/{1-4}/source",              "Upload ST code"},
  {"POST",   "/api/logic/{1-4}/enable",              "Enable program"},
  {"POST",   "/api/logic/{1-4}/disable",             "Disable program"},
  {"POST",   "/api/logic/{1-4}/reinit",              "Cold restart (reset variables)"},
  {"DELETE", "/api/logic/{1-4}",                     "Delete program"},
  {"GET",    "/api/logic/{1-4}/stats",               "Program stats"},
  {"POST",   "/api/logic/{1-4}/debug/pause",         "Pause program"},
  {"POST",   "/api/logic/{1-4}/debug/continue",      "Continue program"},
  {"POST",   "/api/logic/{1-4}/debug/step",          "Step instruction"},
  {"POST",   "/api/logic/{1-4}/debug/breakpoint",    "Set breakpoint"},
  {"DELETE", "/api/logic/{1-4}/debug/breakpoint",    "Remove breakpoint"},
  {"POST",   "/api/logic/{1-4}/debug/stop",          "Stop debug"},
  {"GET",    "/api/logic/{1-4}/debug/state",         "Debug snapshot"},
  {"POST",   "/api/logic/settings",                "Logic engine settings"},
  {"GET",    "/api/logic/globals",                   "List GLOBAL_VAR values"},
  {"GET",    "/api/logic/globals/source",            "Download GLOBAL_VAR source"},
  {"POST",   "/api/logic/globals/source",            "Upload GLOBAL_VAR source"},
  {"GET",    "/api/bindings",                      "ST var<->register bindings"},
  {"POST",   "/api/bindings/{id}",                  "Configure binding"},
  {"GET",    "/api/modbus/slave",                  "Slave config+stats"},
  {"POST",   "/api/modbus/slave",                  "Configure slave"},
  {"GET",    "/api/modbus/master",                 "Master config+stats"},
  {"POST",   "/api/modbus/master",                 "Configure master"},
  {"GET",    "/api/wifi",                          "WiFi config+status"},
  {"POST",   "/api/wifi",                          "Configure WiFi"},
  {"POST",   "/api/wifi/connect",                  "Connect WiFi"},
  {"POST",   "/api/wifi/disconnect",               "Disconnect WiFi"},
  {"GET",    "/api/ethernet",                      "Ethernet (W5500) config+status"},
  {"POST",   "/api/ethernet",                      "Configure Ethernet"},
  {"POST",   "/api/http",                          "Configure HTTP server"},
  {"GET",    "/api/ntp",                           "NTP config+status"},
  {"POST",   "/api/ntp",                           "Configure NTP"},
  {"GET",    "/api/analog",                        "Analog I/O values (ES32D26)"},
  {"POST",   "/api/analog",                        "Configure/write analog I/O"},
  {"GET",    "/api/modules",                       "Module flags"},
  {"POST",   "/api/modules",                       "Set module flags"},
  {"GET",    "/api/debug",                         "Debug flags"},
  {"POST",   "/api/debug",                         "Set debug flags"},
  {"GET",    "/api/rbac",                          "RBAC status + user list"},
  {"POST",   "/api/rbac",                          "Enable/disable RBAC"},
  {"POST",   "/api/rbac/users",                    "Create/update RBAC user"},
  {"DELETE", "/api/rbac/users/{username}",         "Delete RBAC user"},
  {"GET",    "/api/user/me",                       "Current session info"},
  {"POST",   "/api/login",                         "Authenticate, issue session token"},
  {"POST",   "/api/logout",                        "Invalidate session token"},
  {"POST",   "/api/system/reboot",                 "Reboot ESP32"},
  {"POST",   "/api/system/save",                   "Save config to NVS"},
  {"POST",   "/api/system/load",                   "Load config from NVS"},
  {"POST",   "/api/system/defaults",               "Reset to defaults"},
  {"GET",    "/api/system/backup",                 "Download config backup"},
  {"POST",   "/api/system/restore",                "Restore config from backup"},
  {"GET",    "/api/system/watchdog",                "Watchdog status"},
  {"GET",    "/api/system/logs",                   "Request audit log (FEAT-033)"},
  {"POST",   "/api/system/logs/clear",             "Clear request audit log"},
  {"GET",    "/api/system/rate-limit",             "Rate-limit status (not persisted)"},
  {"POST",   "/api/system/rate-limit",             "Enable/disable rate-limit (not persisted)"},
  {"GET",    "/api/syslog",                        "System event/reg-change log"},
  {"POST",   "/api/syslog/clear",                  "Clear system log"},
  {"POST",   "/api/syslog/start",                  "Resume system log"},
  {"POST",   "/api/syslog/stop",                   "Pause system log"},
  {"GET",    "/api/trend/config",                  "Trend recorder config/status"},
  {"POST",   "/api/trend/config",                  "Configure trend recorder watch list"},
  {"POST",   "/api/trend/start",                   "Start trend recording"},
  {"POST",   "/api/trend/stop",                    "Stop trend recording"},
  {"POST",   "/api/trend/clear",                   "Clear trend samples"},
  {"GET",    "/api/trend/data",                    "Trend recorder sample dump"},
  {"GET",    "/api/telnet",                        "Telnet config+status"},
  {"POST",   "/api/telnet",                        "Configure Telnet"},
  {"GET",    "/api/hostname",                      "Get hostname"},
  {"POST",   "/api/hostname",                      "Set hostname"},
  {"GET",    "/api/registers/hr",                  "Bulk read HRs (start,count)"},
  {"POST",   "/api/registers/hr/bulk",             "Bulk write HRs"},
  {"GET",    "/api/registers/ir",                  "Bulk read IRs (start,count)"},
  {"GET",    "/api/registers/coils",               "Bulk read coils (start,count)"},
  {"POST",   "/api/registers/coils/bulk",          "Bulk write coils"},
  {"GET",    "/api/registers/di",                  "Bulk read DIs (start,count)"},
  {"GET",    "/api/events",                        "SSE real-time event stream (FEAT-023)"},
  {"GET",    "/api/events/status",                 "SSE subsystem info"},
  {"GET",    "/api/events/clients",                "SSE connected clients"},
  {"POST",   "/api/events/disconnect",             "Disconnect an SSE client"},
  {"GET",    "/api/version",                       "API version info (FEAT-030)"},
  {"GET",    "/api/v1/*",                          "API v1 versioned endpoint (FEAT-030)"},
  {"GET",    "/api/metrics",                       "Prometheus metrics (FEAT-032)"},
  {"GET",    "/api/alarms",                        "Alarm log"},
  {"POST",   "/api/alarms/ack",                    "Acknowledge alarm"},
  {"GET",    "/api/persist/groups",                "List persistence groups"},
  {"GET",    "/api/persist/groups/{id}",           "Single persistence group"},
  {"POST",   "/api/persist/groups/{id}",           "Create/modify persistence group"},
  {"DELETE", "/api/persist/groups/{id}",           "Delete persistence group"},
  {"POST",   "/api/persist/save",                  "Save persistence group(s)"},
  {"POST",   "/api/persist/restore",               "Restore persistence group(s)"},
  {"POST",   "/api/persist/config",                "Set persistence enabled/auto_load_enabled"},
  {"GET",    "/api/dashboard/layout",               "Dashboard layout settings"},
  {"POST",   "/api/dashboard/layout",               "Save dashboard layout settings"},
  {"POST",   "/api/system/ota",                    "Upload firmware (OTA, FEAT-031)"},
  {"GET",    "/api/system/ota/status",              "OTA progress status (FEAT-031)"},
  {"POST",   "/api/system/ota/rollback",           "Rollback firmware (FEAT-031)"},
  {"GET",    "/api/system/ota/github-check",        "Check GitHub Releases for newer firmware (FEAT-169)"},
  {"POST",   "/api/system/ota/github-install",      "Download+install latest GitHub release (FEAT-169)"},
};
#define API_ROUTES_COUNT (sizeof(API_ROUTES) / sizeof(API_ROUTES[0]))

esp_err_t api_handler_endpoints(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char head[128];
  snprintf(head, sizeof(head), "{\"name\":\"Modbus ESP32 REST API\",\"version\":\"%s\",\"build\":%d,\"endpoints\":[",
           PROJECT_VERSION, BUILD_NUMBER);
  httpd_resp_send_chunk(req, head, HTTPD_RESP_USE_STRLEN);

  char item[192];
  for (size_t i = 0; i < API_ROUTES_COUNT; i++) {
    snprintf(item, sizeof(item), "%s{\"method\":\"%s\",\"path\":\"%s\",\"desc\":\"%s\"}",
             (i == 0) ? "" : ",", API_ROUTES[i].method, API_ROUTES[i].path, API_ROUTES[i].desc);
    httpd_resp_send_chunk(req, item, HTTPD_RESP_USE_STRLEN);
  }

  httpd_resp_send_chunk(req, "]}", HTTPD_RESP_USE_STRLEN);
  httpd_resp_send_chunk(req, NULL, 0);
  http_server_stat_success();
  return ESP_OK;
}

// Konverterer et {method}-templeret sti-mønster ("/api/counters/{1-4}") til
// en OpenAPI-lovlig "{param}"-form ("/api/counters/{id}") — OpenAPI tillader
// ikke bogstaveligt "{1-4}" som et path-parameternavn.
static void openapi_normalize_path(const char *src, char *out, size_t out_len) {
  size_t j = 0;
  bool in_brace = false;
  for (size_t i = 0; src[i] != '\0' && j + 1 < out_len; i++) {
    char c = src[i];
    if (c == '{') { in_brace = true; out[j++] = '{'; continue; }
    if (c == '}') { in_brace = false; if (j > 0 && out[j - 1] != '{') { /* already wrote a name */ } out[j++] = '}'; continue; }
    if (in_brace) {
      // Skriv kun ÉT normaliseret "param" pr. brace-par, ignorér resten (1-4, addr, pin, id, username)
      if (j == 0 || out[j - 1] == '{') {
        const char *name = "param";
        for (size_t k = 0; name[k] != '\0' && j + 1 < out_len; k++) out[j++] = name[k];
      }
      continue;
    }
    out[j++] = c;
  }
  out[j] = '\0';
}

// GET /api/schema — OpenAPI 3.0 schema (FEAT-029). Genererer et gyldigt,
// om end minimalt, OpenAPI-dokument fra API_ROUTES ovenfor — hvert path faar
// et generisk request/response-skema (projektet sporer ikke i dag
// parameter-/svar-typer struktureret pr. endpoint, kun fritekst-beskrivelser),
// tilstraekkeligt til automatisk klient-kodegenerering af selve
// rute-/metode-overfladen.
esp_err_t api_handler_schema(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char head[256];
  snprintf(head, sizeof(head),
    "{\"openapi\":\"3.0.3\",\"info\":{\"title\":\"Modbus ESP32 REST API\",\"version\":\"%s\",\"description\":\"Auto-generated fra enhedens interne route-tabel (FEAT-029)\"},\"paths\":{",
    PROJECT_VERSION);
  httpd_resp_send_chunk(req, head, HTTPD_RESP_USE_STRLEN);

  // Grupperer eksplicit efter normaliseret sti FREMFOR at antage at samme
  // sti's forskellige metoder staar ved siden af hinanden i API_ROUTES —
  // det gør de IKKE altid (fx har /api/counters/{1-4}'s DELETE flere andre
  // sub-action-paths imellem sig og sit GET/POST) — en ren nabo-sammenligning
  // ville ellers have emitteret samme "path"-nøgle to gange (ugyldigt/tabt
  // data i et JSON-objekt).
  // BUG-nyeste: seen_paths (~103*64 = 6.6KB) paa STACKEN ville risikere
  // overflow af den 8KB httpd-worker-stak (samme klasse fejl som denne
  // enhed lige har brugt en hel session paa at fikse for GitHub-OTA,
  // BUG-364/369) — allokeres derfor eksplicit paa heap i stedet.
  char item[256];
  char norm_path[64];
  char (*seen_paths)[64] = (char (*)[64])malloc(API_ROUTES_COUNT * 64);
  if (!seen_paths) {
    return api_send_error(req, 500, "Out of memory");
  }
  size_t seen_count = 0;
  bool first_path = true;

  for (size_t i = 0; i < API_ROUTES_COUNT; i++) {
    if (strstr(API_ROUTES[i].path, "*") != NULL) continue;  // wildcard-ruter, se ovenfor
    openapi_normalize_path(API_ROUTES[i].path, norm_path, sizeof(norm_path));

    bool already_emitted = false;
    for (size_t s = 0; s < seen_count; s++) {
      if (strcmp(seen_paths[s], norm_path) == 0) { already_emitted = true; break; }
    }
    if (already_emitted) continue;
    strncpy(seen_paths[seen_count], norm_path, sizeof(seen_paths[0]) - 1);
    seen_paths[seen_count][sizeof(seen_paths[0]) - 1] = '\0';
    seen_count++;

    if (!first_path) httpd_resp_send_chunk(req, "},", HTTPD_RESP_USE_STRLEN);
    snprintf(item, sizeof(item), "\"%s\":{", norm_path);
    httpd_resp_send_chunk(req, item, HTTPD_RESP_USE_STRLEN);
    first_path = false;

    bool first_method = true;
    for (size_t j = i; j < API_ROUTES_COUNT; j++) {
      if (strstr(API_ROUTES[j].path, "*") != NULL) continue;
      char cmp_path[64];
      openapi_normalize_path(API_ROUTES[j].path, cmp_path, sizeof(cmp_path));
      if (strcmp(cmp_path, norm_path) != 0) continue;

      snprintf(item, sizeof(item),
        "%s\"%s\":{\"summary\":\"%s\",\"responses\":{\"200\":{\"description\":\"OK\"},"
        "\"401\":{\"description\":\"Authentication required\"},\"403\":{\"description\":\"Forbidden\"}}}",
        first_method ? "" : ",",
        (strcmp(API_ROUTES[j].method, "GET") == 0) ? "get" :
        (strcmp(API_ROUTES[j].method, "POST") == 0) ? "post" :
        (strcmp(API_ROUTES[j].method, "DELETE") == 0) ? "delete" : "get",
        API_ROUTES[j].desc);
      httpd_resp_send_chunk(req, item, HTTPD_RESP_USE_STRLEN);
      first_method = false;
    }
  }
  if (!first_path) httpd_resp_send_chunk(req, "}", HTTPD_RESP_USE_STRLEN);
  free(seen_paths);

  httpd_resp_send_chunk(req, "}}", HTTPD_RESP_USE_STRLEN);
  httpd_resp_send_chunk(req, NULL, 0);
  http_server_stat_success();
  return ESP_OK;
}

/* ============================================================================
 * GET /api/status
 * ============================================================================ */

esp_err_t api_handler_status(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  JsonDocument doc;

  doc["version"] = PROJECT_VERSION;
  doc["build"] = BUILD_NUMBER;
  char fw_str[32];
  snprintf(fw_str, sizeof(fw_str), "v%s.%d", PROJECT_VERSION, BUILD_NUMBER);
  doc["firmware"] = fw_str;
  doc["uptime_ms"] = millis();
  doc["heap_free"] = ESP.getFreeHeap();
  doc["wifi_connected"] = wifi_driver_is_connected() ? true : false;

  // IP address
  if (wifi_driver_is_connected()) {
    uint32_t ip = wifi_driver_get_local_ip();
    struct in_addr addr;
    addr.s_addr = ip;
    doc["ip"] = inet_ntoa(addr);
  } else {
    doc["ip"] = nullptr;
  }

  doc["modbus_slave_id"] = g_persist_config.modbus_slave.slave_id;
  doc["https"] = http_server_is_tls_active() ? true : false;

  char buf[HTTP_JSON_DOC_SIZE];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

/* ============================================================================
 * GET /api/counters
 * ============================================================================ */

esp_err_t api_handler_counters(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  JsonDocument doc;
  JsonArray counters = doc["counters"].to<JsonArray>();

  for (int i = 0; i < COUNTER_COUNT; i++) {
    CounterConfig cfg;
    if (counter_engine_get_config(i + 1, &cfg)) {
      JsonObject counter = counters.add<JsonObject>();
      counter["id"] = i + 1;
      counter["enabled"] = cfg.enabled ? true : false;

      const char *mode_str = "DISABLED";
      switch (cfg.hw_mode) {
        case COUNTER_HW_SW:     mode_str = "SW"; break;
        case COUNTER_HW_SW_ISR: mode_str = "SW_ISR"; break;
        case COUNTER_HW_PCNT:   mode_str = "HW_PCNT"; break;
      }
      counter["mode"] = mode_str;
      counter["value"] = counter_engine_get_value(i + 1);
    }
  }

  char buf[HTTP_JSON_DOC_SIZE];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

/* ============================================================================
 * GET /api/counters/{id}
 * ============================================================================ */

esp_err_t api_handler_counter_single(httpd_req_t *req)
{
  // Check for action suffixes (ESP-IDF wildcard only matches at end of URI,
  // so /api/counters/*/reset etc. never match - we must route internally)
  const char *uri = req->uri;
  size_t uri_len = strlen(uri);

  if (req->method == HTTP_POST) {
    if (uri_len >= 6 && strcmp(uri + uri_len - 6, "/reset") == 0) {
      return api_handler_counter_reset(req);
    }
    if (uri_len >= 6 && strcmp(uri + uri_len - 6, "/start") == 0) {
      return api_handler_counter_start(req);
    }
    if (uri_len >= 5 && strcmp(uri + uri_len - 5, "/stop") == 0) {
      return api_handler_counter_stop(req);
    }
    if (uri_len >= 8 && strcmp(uri + uri_len - 8, "/control") == 0) {
      return api_handler_counter_control_post(req);
    }
    // POST /api/counters/{id} (no suffix) = counter config
    // Check: no '/' after the id digit
    const char *after_prefix = uri + strlen("/api/counters/");
    const char *slash = strchr(after_prefix, '/');
    if (slash == NULL) {
      return api_handler_counter_config_post(req);
    }
  }

  http_server_stat_request();
  CHECK_AUTH(req);

  int id = api_extract_id_from_uri(req, "/api/counters/");
  if (id < 1 || id > COUNTER_COUNT) {
    return api_send_error(req, 400, "Invalid counter ID (must be 1-4)");
  }

  CounterConfig cfg;
  if (!counter_engine_get_config(id, &cfg)) {
    return api_send_error(req, 404, "Counter not found");
  }

  JsonDocument doc;

  doc["id"] = id;
  doc["enabled"] = cfg.enabled ? true : false;

  const char *mode_str = "DISABLED";
  switch (cfg.hw_mode) {
    case COUNTER_HW_SW:     mode_str = "SW"; break;
    case COUNTER_HW_SW_ISR: mode_str = "SW_ISR"; break;
    case COUNTER_HW_PCNT:   mode_str = "HW_PCNT"; break;
  }
  doc["mode"] = mode_str;

  // FEAT-171: denne handler returnerede hidtil KUN status/live-værdier
  // (id/enabled/mode/value/raw/frequency/running/overflow/compare_triggered)
  // — ingen af de faktiske konfigurationsfelter, saa en GUI kunne aldrig
  // indlaese/vise den gemte konfiguration (kun skrive den blindt via POST).
  doc["edge_type"] = cfg.edge_type;
  doc["direction"] = cfg.direction;
  doc["prescaler"] = cfg.prescaler;
  doc["bit_width"] = cfg.bit_width;
  doc["scale_factor"] = cfg.scale_factor;
  doc["debounce_enabled"] = cfg.debounce_enabled ? true : false;
  doc["debounce_ms"] = cfg.debounce_ms;
  doc["input_dis"] = cfg.input_dis;
  doc["interrupt_pin"] = cfg.interrupt_pin;
  doc["hw_gpio"] = cfg.hw_gpio;
  doc["compare_enabled"] = cfg.compare_enabled ? true : false;
  doc["compare_mode"] = cfg.compare_mode;
  doc["compare_value"] = cfg.compare_value;
  doc["compare_source"] = cfg.compare_source;
  doc["reset_on_read"] = cfg.reset_on_read ? true : false;
  if (cfg.value_reg != 0xFFFF) doc["value_reg"] = cfg.value_reg;
  if (cfg.raw_reg != 0xFFFF) doc["raw_reg"] = cfg.raw_reg;
  if (cfg.freq_reg != 0xFFFF) doc["freq_reg"] = cfg.freq_reg;
  if (cfg.ctrl_reg != 0xFFFF) doc["ctrl_reg"] = cfg.ctrl_reg;

  uint64_t value = counter_engine_get_value(id);
  doc["value"] = value;

  // Read raw value from raw register
  if (cfg.raw_reg != 0xFFFF) {
    uint16_t raw = registers_get_holding_register(cfg.raw_reg);
    doc["raw"] = raw;
  }

  // Frequency
  if (cfg.freq_reg != 0xFFFF) {
    uint16_t freq = registers_get_holding_register(cfg.freq_reg);
    doc["frequency"] = freq;
  }

  // Control register flags
  if (cfg.ctrl_reg != 0xFFFF) {
    uint16_t ctrl = registers_get_holding_register(cfg.ctrl_reg);
    // FEAT-171: bit2 (0x04) er STOP-KOMMANDOEN (transient, selv-clearer
    // samme loop-tick, jf. counter_engine.cpp:310-326) — læste derfor
    // praktisk talt altid false uanset reel tilstand. Den persistente
    // running-status står i bit7 (0x80, counter_engine.cpp:328-365).
    doc["running"] = (ctrl & 0x80) ? true : false;
    doc["overflow"] = (ctrl & 0x08) ? true : false;
    doc["compare_triggered"] = (ctrl & 0x10) ? true : false;
  }

  char buf[HTTP_JSON_DOC_SIZE];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

/* ============================================================================
 * GET /api/timers
 * ============================================================================ */

esp_err_t api_handler_timers(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  JsonDocument doc;
  JsonArray timers = doc["timers"].to<JsonArray>();

  for (int i = 0; i < TIMER_COUNT; i++) {
    TimerConfig cfg;
    if (timer_engine_get_config(i + 1, &cfg)) {
      JsonObject timer = timers.add<JsonObject>();
      timer["id"] = i + 1;
      timer["enabled"] = cfg.enabled ? true : false;

      const char *mode_str = "DISABLED";
      switch (cfg.mode) {
        case TIMER_MODE_DISABLED: mode_str = "DISABLED"; break;
        case TIMER_MODE_1_ONESHOT: mode_str = "ONESHOT"; break;
        case TIMER_MODE_2_MONOSTABLE: mode_str = "MONOSTABLE"; break;
        case TIMER_MODE_3_ASTABLE: mode_str = "ASTABLE"; break;
        case TIMER_MODE_4_INPUT_TRIGGERED: mode_str = "INPUT_TRIGGERED"; break;
      }
      timer["mode"] = mode_str;

      // Read output coil state
      if (cfg.output_coil != 0xFFFF) {
        timer["output"] = registers_get_coil(cfg.output_coil) ? true : false;
      }
    }
  }

  char buf[HTTP_JSON_DOC_SIZE];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

/* ============================================================================
 * GET /api/timers/{id}
 * ============================================================================ */

esp_err_t api_handler_timer_single(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  int id = api_extract_id_from_uri(req, "/api/timers/");
  if (id < 1 || id > TIMER_COUNT) {
    return api_send_error(req, 400, "Invalid timer ID (must be 1-4)");
  }

  TimerConfig cfg;
  if (!timer_engine_get_config(id, &cfg)) {
    return api_send_error(req, 404, "Timer not found");
  }

  JsonDocument doc;

  doc["id"] = id;
  doc["enabled"] = cfg.enabled ? true : false;

  const char *mode_str = "DISABLED";
  switch (cfg.mode) {
    case TIMER_MODE_DISABLED: mode_str = "DISABLED"; break;
    case TIMER_MODE_1_ONESHOT: mode_str = "ONESHOT"; break;
    case TIMER_MODE_2_MONOSTABLE: mode_str = "MONOSTABLE"; break;
    case TIMER_MODE_3_ASTABLE: mode_str = "ASTABLE"; break;
    case TIMER_MODE_4_INPUT_TRIGGERED: mode_str = "INPUT_TRIGGERED"; break;
  }
  doc["mode"] = mode_str;

  // Output coil
  if (cfg.output_coil != 0xFFFF) {
    doc["output_coil"] = cfg.output_coil;
    doc["output"] = registers_get_coil(cfg.output_coil) ? true : false;
  }
  if (cfg.ctrl_reg != 0xFFFF) doc["ctrl_reg"] = cfg.ctrl_reg;

  // Mode-specific parameters
  // FEAT-171: phase*_output_state og trigger_edge manglede her — GUI'en kan
  // ikke indlæse eksisterende polaritet/triggerkant-konfiguration uden dem.
  switch (cfg.mode) {
    case TIMER_MODE_1_ONESHOT:
      doc["phase1_duration_ms"] = cfg.phase1_duration_ms;
      doc["phase2_duration_ms"] = cfg.phase2_duration_ms;
      doc["phase3_duration_ms"] = cfg.phase3_duration_ms;
      doc["phase1_output_state"] = cfg.phase1_output_state ? true : false;
      doc["phase2_output_state"] = cfg.phase2_output_state ? true : false;
      doc["phase3_output_state"] = cfg.phase3_output_state ? true : false;
      break;
    case TIMER_MODE_2_MONOSTABLE:
      doc["pulse_duration_ms"] = cfg.pulse_duration_ms;
      break;
    case TIMER_MODE_3_ASTABLE:
      doc["on_duration_ms"] = cfg.on_duration_ms;
      doc["off_duration_ms"] = cfg.off_duration_ms;
      doc["phase1_output_state"] = cfg.phase1_output_state ? true : false;
      doc["phase2_output_state"] = cfg.phase2_output_state ? true : false;
      break;
    case TIMER_MODE_4_INPUT_TRIGGERED:
      doc["input_dis"] = cfg.input_dis;
      doc["delay_ms"] = cfg.delay_ms;
      doc["trigger_edge"] = cfg.trigger_edge ? true : false;
      doc["phase1_output_state"] = cfg.phase1_output_state ? true : false;
      break;
    default:
      break;
  }

  // FEAT-171: runtime-tilstand (is_active/current_phase) fandtes hidtil kun
  // via Prometheus-metrics, ikke via denne JSON-GET — noedvendig for et
  // "koerer nu"-statusfelt i GUI'en.
  uint8_t rt_phase = 0, rt_active = 0;
  if (timer_engine_get_runtime(id, &rt_phase, &rt_active)) {
    doc["running"] = rt_active ? true : false;
    doc["current_phase"] = rt_phase;
  }

  char buf[HTTP_JSON_DOC_SIZE];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

/* ============================================================================
 * GET /api/registers/hr/{addr}
 * ============================================================================ */

esp_err_t api_handler_hr_read(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  int addr = api_extract_id_from_uri(req, "/api/registers/hr/");
  if (addr < 0 || addr >= HOLDING_REGS_SIZE) {
    return api_send_error(req, 400, "Invalid register address");
  }

  uint16_t value = registers_get_holding_register(addr);

  JsonDocument doc;
  doc["address"] = addr;
  doc["value"] = value;

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

/* ============================================================================
 * POST /api/registers/hr/{addr}
 * Type-aware write: uint (default), int, dint, dword, real (GAP-8)
 * ============================================================================ */

esp_err_t api_handler_hr_write(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  int addr = api_extract_id_from_uri(req, "/api/registers/hr/");
  if (addr < 0 || addr >= HOLDING_REGS_SIZE) {
    return api_send_error(req, 400, "Invalid register address");
  }

  // FEAT-089: gammel vaerdi FOER skrivningen. Bemaerk: for dint/dword/real
  // (2 registre) fanger dette kun det FOERSTE register (addr) — pragmatisk
  // forenkling, da alle typer altid skriver til addr, saa en aendring der
  // registret fanges uanset type, blot uden det fulde 32-bit billede for
  // to-register-typer.
  uint16_t syslog_old_val = registers_get_holding_register(addr);

  // Read request body
  char content[256];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    return api_send_error(req, 400, "Failed to read request body");
  }
  content[ret] = '\0';

  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, content);
  if (error) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  if (!doc.containsKey("value")) {
    return api_send_error(req, 400, "Missing 'value' field");
  }

  // Get type (default: uint)
  const char *type_str = "uint";
  if (doc.containsKey("type")) {
    type_str = doc["type"].as<const char*>();
    if (!type_str) type_str = "uint";
  }

  // Response
  JsonDocument resp;
  resp["address"] = addr;
  resp["status"] = 200;

  if (strcmp(type_str, "uint") == 0) {
    // 16-bit unsigned (default)
    uint16_t value = doc["value"].as<uint16_t>();
    registers_set_holding_register(addr, value);
    resp["value"] = value;
    resp["type"] = "uint";
  }
  else if (strcmp(type_str, "int") == 0) {
    // 16-bit signed
    int16_t value = doc["value"].as<int16_t>();
    registers_set_holding_register(addr, (uint16_t)value);
    resp["value"] = value;
    resp["type"] = "int";
  }
  else if (strcmp(type_str, "dint") == 0) {
    // 32-bit signed (2 registers)
    if (addr + 1 >= HOLDING_REGS_SIZE) {
      return api_send_error(req, 400, "DINT requires 2 registers, address out of range");
    }
    int32_t value = doc["value"].as<int32_t>();
    uint32_t uval = (uint32_t)value;
    registers_set_holding_register(addr, (uint16_t)(uval >> 16));      // High word
    registers_set_holding_register(addr + 1, (uint16_t)(uval & 0xFFFF)); // Low word
    resp["value"] = value;
    resp["type"] = "dint";
    resp["registers"] = 2;
  }
  else if (strcmp(type_str, "dword") == 0) {
    // 32-bit unsigned (2 registers)
    if (addr + 1 >= HOLDING_REGS_SIZE) {
      return api_send_error(req, 400, "DWORD requires 2 registers, address out of range");
    }
    uint32_t value = doc["value"].as<uint32_t>();
    registers_set_holding_register(addr, (uint16_t)(value >> 16));      // High word
    registers_set_holding_register(addr + 1, (uint16_t)(value & 0xFFFF)); // Low word
    resp["value"] = value;
    resp["type"] = "dword";
    resp["registers"] = 2;
  }
  else if (strcmp(type_str, "real") == 0) {
    // 32-bit float (2 registers, IEEE 754)
    if (addr + 1 >= HOLDING_REGS_SIZE) {
      return api_send_error(req, 400, "REAL requires 2 registers, address out of range");
    }
    float value = doc["value"].as<float>();
    uint32_t uval;
    memcpy(&uval, &value, sizeof(float));
    registers_set_holding_register(addr, (uint16_t)(uval >> 16));      // High word
    registers_set_holding_register(addr + 1, (uint16_t)(uval & 0xFFFF)); // Low word
    resp["value"] = value;
    resp["type"] = "real";
    resp["registers"] = 2;
  }
  else {
    return api_send_error(req, 400, "Invalid type (use: uint, int, dint, dword, real)");
  }

  // FEAT-089: log kun hvis vaerdien REELT aendrede sig (ikke et "skriv samme
  // vaerdi"-no-op) — se system_log.h's kommentar om at kalderen selv skal
  // dedupliere.
  uint16_t syslog_new_val = registers_get_holding_register(addr);
  if (syslog_new_val != syslog_old_val) {
    char ip[16], user[24];
    http_get_client_info(req, ip, sizeof(ip), user, sizeof(user));
    system_log_add_reg_change((uint8_t)SYSLOG_SRC_REST, user, ip,
                               (uint16_t)addr, false, syslog_old_val, syslog_new_val);
  }

  char buf[256];
  serializeJson(resp, buf, sizeof(buf));

  return api_send_json(req, buf);
}

/* ============================================================================
 * GET /api/registers/ir/{addr}
 * ============================================================================ */

esp_err_t api_handler_ir_read(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  int addr = api_extract_id_from_uri(req, "/api/registers/ir/");
  if (addr < 0 || addr >= INPUT_REGS_SIZE) {
    return api_send_error(req, 400, "Invalid register address");
  }

  uint16_t value = registers_get_input_register(addr);

  JsonDocument doc;
  doc["address"] = addr;
  doc["value"] = value;

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

/* ============================================================================
 * GET /api/registers/coils/{addr}
 * ============================================================================ */

esp_err_t api_handler_coil_read(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  int addr = api_extract_id_from_uri(req, "/api/registers/coils/");
  if (addr < 0 || addr >= COILS_SIZE * 8) {
    return api_send_error(req, 400, "Invalid coil address");
  }

  uint8_t value = registers_get_coil(addr);

  JsonDocument doc;
  doc["address"] = addr;
  doc["value"] = value ? true : false;

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

/* ============================================================================
 * POST /api/registers/coils/{addr}
 * ============================================================================ */

esp_err_t api_handler_coil_write(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  int addr = api_extract_id_from_uri(req, "/api/registers/coils/");
  if (addr < 0 || addr >= COILS_SIZE * 8) {
    return api_send_error(req, 400, "Invalid coil address");
  }

  // Read request body
  char content[128];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    return api_send_error(req, 400, "Failed to read request body");
  }
  content[ret] = '\0';

  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, content);
  if (error) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  if (!doc.containsKey("value")) {
    return api_send_error(req, 400, "Missing 'value' field");
  }

  // Accept bool or int
  uint8_t value = 0;
  if (doc["value"].is<bool>()) {
    value = doc["value"].as<bool>() ? 1 : 0;
  } else {
    value = doc["value"].as<int>() ? 1 : 0;
  }

  // FEAT-089: gammel vaerdi FOER skrivningen
  uint8_t syslog_old_val = registers_get_coil(addr);

  // Write coil
  registers_set_coil(addr, value);

  if (value != syslog_old_val) {
    char ip[16], user[24];
    http_get_client_info(req, ip, sizeof(ip), user, sizeof(user));
    system_log_add_reg_change((uint8_t)SYSLOG_SRC_REST, user, ip,
                               (uint16_t)addr, true, syslog_old_val, value);
  }

  // Response
  JsonDocument resp;
  resp["address"] = addr;
  resp["value"] = value ? true : false;
  resp["status"] = 200;

  char buf[256];
  serializeJson(resp, buf, sizeof(buf));

  return api_send_json(req, buf);
}

/* ============================================================================
 * GET /api/registers/di/{addr}
 * ============================================================================ */

esp_err_t api_handler_di_read(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  int addr = api_extract_id_from_uri(req, "/api/registers/di/");
  if (addr < 0 || addr >= DISCRETE_INPUTS_SIZE * 8) {
    return api_send_error(req, 400, "Invalid discrete input address");
  }

  uint8_t value = registers_get_discrete_input(addr);

  JsonDocument doc;
  doc["address"] = addr;
  doc["value"] = value ? true : false;

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

/* ============================================================================
 * GET /api/logic
 * ============================================================================ */

esp_err_t api_handler_logic(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  st_logic_engine_state_t *state = st_logic_get_state();
  if (!state) {
    return api_send_error(req, 500, "ST Logic not initialized");
  }

  JsonDocument doc;
  doc["enabled"] = state->enabled ? true : false;
  doc["execution_interval_ms"] = state->execution_interval_ms;
  doc["total_cycles"] = state->total_cycles;

  // Compiler resource info (realtime heap + pool stats)
  JsonObject res = doc["resources"].to<JsonObject>();
  res["heap_free"] = (uint32_t)esp_get_free_heap_size();
  res["largest_block"] = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  res["min_free"] = (uint32_t)esp_get_minimum_free_heap_size();
  uint32_t pool_used = 0, pool_free = 0, pool_largest = 0;
  st_logic_get_pool_stats(state, &pool_used, &pool_free, &pool_largest);
  res["pool_total"] = (uint32_t)ST_LOGIC_POOL_SIZE;
  res["pool_used"] = pool_used;
  res["pool_free"] = pool_free;
  // Estimated max AST nodes that can be allocated (node_size ~84 bytes + 24KB reserve for compiler)
  uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  uint32_t available_for_ast = (largest > 24576) ? (largest - 24576) : 0;
  res["ast_node_size"] = (uint32_t)sizeof(st_ast_node_t);
  res["max_ast_nodes"] = (available_for_ast > 0) ? (uint32_t)(available_for_ast / sizeof(st_ast_node_t)) : 0;

  JsonArray programs = doc["programs"].to<JsonArray>();

  for (int i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
    st_logic_program_config_t *prog = &state->programs[i];
    JsonObject p = programs.add<JsonObject>();
    p["id"] = i + 1;
    p["name"] = prog->name;
    p["enabled"] = prog->enabled ? true : false;
    p["compiled"] = prog->compiled ? true : false;
    p["priority"] = (prog->priority == ST_LOGIC_PRIORITY_HIGH) ? "HIGH" : "NORMAL";  // FEAT-010
    p["interval_ms"] = prog->interval_ms;  // FEAT-010
    p["binding_count"] = prog->binding_count;  // FEAT-010: GUI needs this to explain why priority-change might be rejected
    p["source_size"] = prog->source_size;
    p["execution_count"] = prog->execution_count;
    p["error_count"] = prog->error_count;

    if (prog->last_error[0] != '\0') {
      p["last_error"] = prog->last_error;
    }
  }

  char buf[HTTP_JSON_DOC_SIZE];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

/* ============================================================================
 * GET /api/logic/{id}
 * ============================================================================ */

esp_err_t api_handler_logic_single(httpd_req_t *req)
{
  // Internal suffix routing - ESP-IDF wildcard only supports * at end of URI,
  // so /api/logic/*/source, /api/logic/*/enable etc. never match.
  // All requests to /api/logic/{id}/xxx land here via /api/logic/* wildcard.
  const char *uri = req->uri;
  size_t uri_len = strlen(uri);

  // FEAT-007: GLOBAL_VAR routes (/api/logic/globals, /api/logic/globals/source)
  // — no numeric {id} in these, route before ID-dependent suffixes below.
  if (strstr(uri, "/logic/globals") != NULL) {
    return api_handler_logic_globals(req);
  }

  // FEAT-020: Debug routes (contains /debug/) — route before other suffixes
  if (strstr(uri, "/debug/") != NULL) {
    return api_handler_logic_debug(req);
  }

  // GET suffixes
  if (req->method == HTTP_GET) {
    if (uri_len >= 7 && strcmp(uri + uri_len - 7, "/source") == 0) {
      return api_handler_logic_source_get(req);
    }
    if (uri_len >= 6 && strcmp(uri + uri_len - 6, "/stats") == 0) {
      return api_handler_logic_stats(req);
    }
  }

  // POST suffixes
  if (req->method == HTTP_POST) {
    if (uri_len >= 7 && strcmp(uri + uri_len - 7, "/source") == 0) {
      return api_handler_logic_source_post(req);
    }
    if (uri_len >= 7 && strcmp(uri + uri_len - 7, "/enable") == 0) {
      return api_handler_logic_enable(req);
    }
    if (uri_len >= 8 && strcmp(uri + uri_len - 8, "/disable") == 0) {
      return api_handler_logic_disable(req);
    }
    if (uri_len >= 7 && strcmp(uri + uri_len - 7, "/reinit") == 0) {
      return api_handler_logic_reinit(req);
    }
    // GAP-13: Variable binding
    if (uri_len >= 5 && strcmp(uri + uri_len - 5, "/bind") == 0) {
      return api_handler_logic_bind_post(req);
    }
    // FEAT-010: per-program priority/interval
    if (uri_len >= 9 && strcmp(uri + uri_len - 9, "/priority") == 0) {
      return api_handler_logic_priority_post(req);
    }
    if (uri_len >= 9 && strcmp(uri + uri_len - 9, "/interval") == 0) {
      return api_handler_logic_program_interval_post(req);
    }
    // GAP-26/FEAT-164: /api/logic/settings er en EXACT route registreret i
    // http_server.cpp, men registreret EFTER dette wildcard-handler
    // (/api/logic/*) — ESP-IDF's httpd matcher tester registrerede URI'er i
    // registrerings-raekkefoelge, saa wildcarden fanger "/settings" FoeR den
    // mere specifikke, senere-registrerede exacte rute naar naaes. Samme
    // klasse shadowing-bug som /api/modbus/activity (se
    // api_handler_modbus_get/post ovenfor) — samme fix: delegér paa suffiks
    // FoeR ID-parsingen nedenfor, som ellers fejlagtigt afviste "settings"
    // som et ugyldigt program-ID.
    if (uri_len >= 9 && strcmp(uri + uri_len - 9, "/settings") == 0) {
      return api_handler_logic_settings_post(req);
    }
  }

  // Normal logic/{id} handling
  http_server_stat_request();
  CHECK_AUTH(req);

  int id = api_extract_id_from_uri(req, "/api/logic/");
  if (id < 1 || id > ST_LOGIC_MAX_PROGRAMS) {
    return api_send_error(req, 400, "Invalid logic program ID");
  }

  st_logic_engine_state_t *state = st_logic_get_state();
  if (!state) {
    return api_send_error(req, 500, "ST Logic not initialized");
  }

  st_logic_program_config_t *prog = &state->programs[id - 1];

  JsonDocument doc;

  doc["id"] = id;
  doc["name"] = prog->name;
  doc["enabled"] = prog->enabled ? true : false;
  doc["compiled"] = prog->compiled ? true : false;
  doc["priority"] = (prog->priority == ST_LOGIC_PRIORITY_HIGH) ? "HIGH" : "NORMAL";  // FEAT-010
  doc["interval_ms"] = prog->interval_ms;  // FEAT-010
  doc["execution_count"] = prog->execution_count;
  doc["error_count"] = prog->error_count;
  doc["last_execution_us"] = prog->last_execution_us;
  doc["min_execution_us"] = prog->min_execution_us;
  doc["max_execution_us"] = prog->max_execution_us;
  doc["overrun_count"] = prog->overrun_count;

  if (prog->last_error[0] != '\0') {
    doc["last_error"] = prog->last_error;
  }

  // Variables (if compiled)
  if (prog->compiled && prog->bytecode.var_count > 0) {
    JsonArray vars = doc["variables"].to<JsonArray>();
    for (int i = 0; i < prog->bytecode.var_count && i < 32; i++) {
      JsonObject v = vars.add<JsonObject>();
      v["index"] = i;
      v["name"] = prog->bytecode.var_names[i];

      const char *type_str = "INT";
      switch (prog->bytecode.var_types[i]) {
        case ST_TYPE_BOOL: type_str = "BOOL"; break;
        case ST_TYPE_INT:  type_str = "INT"; break;
        case ST_TYPE_DINT: type_str = "DINT"; break;
        case ST_TYPE_REAL: type_str = "REAL"; break;
        case ST_TYPE_TIME: type_str = "TIME"; break;
        case ST_TYPE_STRING: type_str = "STRING"; break;  // FEAT-005
        default: break;
      }
      v["type"] = type_str;

      // Get current value
      st_value_t val = prog->bytecode.variables[i];
      switch (prog->bytecode.var_types[i]) {
        case ST_TYPE_BOOL:
          v["value"] = val.bool_val ? true : false;
          break;
        case ST_TYPE_INT:
          v["value"] = val.int_val;
          break;
        case ST_TYPE_DINT:
          v["value"] = val.dint_val;
          break;
        case ST_TYPE_REAL:
          v["value"] = val.real_val;
          break;
        case ST_TYPE_TIME:
          v["value"] = val.dint_val;
          break;
        case ST_TYPE_STRING:
          // FEAT-005: en variabels egen str_ref peger altid paa sit eget
          // slot (self-referencing, sat af compileren/VM'en) — laes direkte,
          // ingen VM-kontekst noedvendig for at resolve en KIND_VAR-reference.
          v["value"] = prog->bytecode.string_vars[i];
          break;
        default:
          v["value"] = val.int_val;
          break;
      }
    }
  }

  char buf[HTTP_SERVER_MAX_RESP_SIZE];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

/* ============================================================================
 * GET /api/logic/{id}/source - Download ST source code
 * ============================================================================ */

esp_err_t api_handler_logic_source_get(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  // Extract program ID from URI (e.g., "/api/logic/1/source" -> 1)
  int id = api_extract_id_from_uri(req, "/api/logic/");
  if (id < 1 || id > ST_LOGIC_MAX_PROGRAMS) {
    return api_send_error(req, 400, "Invalid logic program ID");
  }

  st_logic_engine_state_t *state = st_logic_get_state();
  if (!state) {
    return api_send_error(req, 500, "ST Logic not initialized");
  }

  st_logic_program_config_t *prog = &state->programs[id - 1];
  const char *source = st_logic_get_source_code(state, id - 1);
  if (!source || prog->source_size == 0) {
    return api_send_error(req, 404, "No source code uploaded for this program");
  }

  // IMPORTANT: source_pool entries are NOT null-terminated (contiguous in shared pool).
  // Must use prog->source_size, NOT strlen(source).
  size_t source_len = prog->source_size;
  size_t buf_size = source_len + 256;  // Extra space for JSON wrapper
  char *buf = (char *)malloc(buf_size);
  if (!buf) {
    return api_send_error(req, 500, "Out of memory");
  }

  // Make null-terminated copy for JSON serialization
  char *source_copy = (char *)malloc(source_len + 1);
  if (!source_copy) {
    free(buf);
    return api_send_error(req, 500, "Out of memory");
  }
  memcpy(source_copy, source, source_len);
  source_copy[source_len] = '\0';

  JsonDocument doc;
  doc["id"] = id;
  doc["name"] = prog->name;
  doc["source"] = source_copy;
  doc["size"] = source_len;

  size_t json_len = serializeJson(doc, buf, buf_size);
  free(source_copy);

  if (json_len >= buf_size) {
    free(buf);
    return api_send_error(req, 500, "Response too large");
  }

  esp_err_t ret = api_send_json(req, buf);
  free(buf);
  return ret;
}

/* ============================================================================
 * POST /api/logic/{id}/source - Upload ST source code
 * ============================================================================ */

esp_err_t api_handler_logic_source_post(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  // Extract program ID from URI
  int id = api_extract_id_from_uri(req, "/api/logic/");
  if (id < 1 || id > ST_LOGIC_MAX_PROGRAMS) {
    return api_send_error(req, 400, "Invalid logic program ID");
  }

  st_logic_engine_state_t *state = st_logic_get_state();
  if (!state) {
    return api_send_error(req, 500, "ST Logic not initialized");
  }

  // Get content length
  size_t content_len = req->content_len;
  if (content_len == 0) {
    return api_send_error(req, 400, "Empty request body");
  }
  if (content_len > 8192) {
    return api_send_error(req, 400, "Request too large (max 8KB)");
  }

  // Phase 1: Read HTTP body, parse JSON, extract source, upload to pool.
  // All temporary allocations are freed before Phase 2 (compile).
  uint32_t source_len = 0;
  bool upload_ok = false;
  {
    char *content = (char *)malloc(content_len + 1);
    if (!content) {
      return api_send_error(req, 500, "Out of memory");
    }

    int received = 0;
    while (received < (int)content_len) {
      int ret = httpd_req_recv(req, content + received, content_len - received);
      if (ret <= 0) {
        free(content);
        return api_send_error(req, 400, "Failed to read request body");
      }
      received += ret;
    }
    content[content_len] = '\0';

    // Parse JSON in inner scope — JsonDocument destructor frees heap on scope exit
    {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, content);
      if (error) {
        free(content);
        return api_send_error(req, 400, "Invalid JSON");
      }

      if (!doc.containsKey("source")) {
        free(content);
        return api_send_error(req, 400, "Missing 'source' field");
      }

      const char *source = doc["source"].as<const char *>();
      if (!source || strlen(source) == 0) {
        free(content);
        return api_send_error(req, 400, "Empty source code");
      }

      // Upload to source pool (memcpy's the data)
      source_len = strlen(source);
      upload_ok = st_logic_upload(state, id - 1, source, source_len);
    } // <-- JsonDocument destructor frees ArduinoJson heap here

    free(content);
  } // <-- content freed here

  if (!upload_ok) {
    st_logic_program_config_t *prog = &state->programs[id - 1];
    return api_send_error(req, 500, prog->last_error[0] ? prog->last_error : "Upload failed");
  }

  // Phase 2: Compile — all temporary buffers are freed, maximum heap available
  st_logic_compile(state, id - 1);

  // Phase 3: Build response
  st_logic_program_config_t *prog = &state->programs[id - 1];

  char buf[256];
  snprintf(buf, sizeof(buf),
    "{\"status\":200,\"id\":%d,\"name\":\"%s\",\"compiled\":%s,"
    "\"source_size\":%lu,\"instr_count\":%u%s%s%s}",
    id, prog->name,
    prog->compiled ? "true" : "false",
    (unsigned long)source_len,
    (unsigned)prog->bytecode.instr_count,
    (!prog->compiled && prog->last_error[0]) ? ",\"compile_error\":\"" : "",
    (!prog->compiled && prog->last_error[0]) ? prog->last_error : "",
    (!prog->compiled && prog->last_error[0]) ? "\"" : "");

  return api_send_json(req, buf);
}

/* ============================================================================
 * FEAT-007: GLOBAL_VAR (inter-program shared variables)
 *   GET  /api/logic/globals         - list current globals + live values
 *   GET  /api/logic/globals/source  - get GLOBAL_VAR declaration source
 *   POST /api/logic/globals/source  - upload + (re)compile GLOBAL_VAR block
 * ============================================================================ */

esp_err_t api_handler_logic_globals(httpd_req_t *req)
{
  http_server_stat_request();

  const char *uri = req->uri;
  size_t uri_len = strlen(uri);
  bool is_source = (uri_len >= 7 && strcmp(uri + uri_len - 7, "/source") == 0);

  st_logic_engine_state_t *state = st_logic_get_state();
  if (!state) {
    return api_send_error(req, 500, "ST Logic not initialized");
  }

  if (req->method == HTTP_GET) {
    CHECK_AUTH(req);

    if (is_source) {
      size_t buf_size = state->global_source_size + 256;
      char *buf = (char *)malloc(buf_size);
      if (!buf) {
        return api_send_error(req, 500, "Out of memory");
      }
      JsonDocument doc;
      doc["source"] = state->global_source;
      doc["size"] = state->global_source_size;
      size_t json_len = serializeJson(doc, buf, buf_size);
      if (json_len >= buf_size) {
        free(buf);
        return api_send_error(req, 500, "Response too large");
      }
      esp_err_t ret = api_send_json(req, buf);
      free(buf);
      return ret;
    }

    // GET /api/logic/globals — list current globals + live values
    JsonDocument doc;
    JsonArray arr = doc["globals"].to<JsonArray>();
    st_logic_lock_variables();
    for (uint8_t i = 0; i < state->global_count; i++) {
      JsonObject v = arr.add<JsonObject>();
      v["index"] = i;
      v["name"] = state->globals[i].name;

      const char *type_str = "INT";
      switch (state->globals[i].type) {
        case ST_TYPE_BOOL:  type_str = "BOOL"; break;
        case ST_TYPE_INT:   type_str = "INT"; break;
        case ST_TYPE_DINT:  type_str = "DINT"; break;
        case ST_TYPE_DWORD: type_str = "DWORD"; break;
        case ST_TYPE_REAL:  type_str = "REAL"; break;
        case ST_TYPE_TIME:  type_str = "TIME"; break;
        default: break;
      }
      v["type"] = type_str;

      st_value_t val = state->globals[i].value;
      switch (state->globals[i].type) {
        case ST_TYPE_BOOL:  v["value"] = val.bool_val ? true : false; break;
        case ST_TYPE_INT:   v["value"] = val.int_val; break;
        case ST_TYPE_DINT:  v["value"] = val.dint_val; break;
        case ST_TYPE_DWORD: v["value"] = val.dword_val; break;
        case ST_TYPE_REAL:  v["value"] = val.real_val; break;
        case ST_TYPE_TIME:  v["value"] = val.dint_val; break;
        default: v["value"] = val.int_val; break;
      }
    }
    st_logic_unlock_variables();
    doc["count"] = state->global_count;
    if (state->global_last_error[0]) {
      doc["last_error"] = state->global_last_error;
    }

    char buf[HTTP_JSON_DOC_SIZE];
    size_t json_len = serializeJson(doc, buf, sizeof(buf));
    if (json_len >= sizeof(buf)) {
      return api_send_error(req, 500, "Response too large");
    }
    return api_send_json(req, buf);
  }

  if (req->method == HTTP_POST && is_source) {
    CHECK_AUTH_WRITE(req);

    size_t content_len = req->content_len;
    if (content_len == 0) {
      return api_send_error(req, 400, "Empty request body");
    }
    if (content_len > ST_GLOBAL_SOURCE_MAX + 256) {
      return api_send_error(req, 400, "Request too large");
    }

    char *content = (char *)malloc(content_len + 1);
    if (!content) {
      return api_send_error(req, 500, "Out of memory");
    }

    int received = 0;
    while (received < (int)content_len) {
      int ret = httpd_req_recv(req, content + received, content_len - received);
      if (ret <= 0) {
        free(content);
        return api_send_error(req, 400, "Failed to read request body");
      }
      received += ret;
    }
    content[content_len] = '\0';

    uint32_t source_len = 0;
    bool upload_ok = false;
    {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, content);
      if (error) {
        free(content);
        return api_send_error(req, 400, "Invalid JSON");
      }
      if (!doc.containsKey("source")) {
        free(content);
        return api_send_error(req, 400, "Missing 'source' field");
      }
      const char *source = doc["source"].as<const char *>();
      if (!source) {
        free(content);
        return api_send_error(req, 400, "Empty source code");
      }
      source_len = strlen(source);
      upload_ok = st_logic_globals_upload(state, source, source_len);
    }
    free(content);

    if (!upload_ok) {
      return api_send_error(req, 500, state->global_last_error[0] ? state->global_last_error : "Upload failed");
    }

    // Compiling also cascades a recompile of any already-compiled Logic1-4
    // program, so name->index bindings stay correct against the new layout
    // (see st_logic_globals_compile's doc comment).
    bool compiled = st_logic_globals_compile(state);

    char buf[384];
    snprintf(buf, sizeof(buf),
      "{\"status\":200,\"compiled\":%s,\"count\":%d,\"source_size\":%lu%s%s%s}",
      compiled ? "true" : "false",
      (int)state->global_count,
      (unsigned long)source_len,
      (!compiled && state->global_last_error[0]) ? ",\"compile_error\":\"" : "",
      (!compiled && state->global_last_error[0]) ? state->global_last_error : "",
      (!compiled && state->global_last_error[0]) ? "\"" : "");

    return api_send_json(req, buf);
  }

  return api_send_error(req, 405, "Method not allowed");
}

/* ============================================================================
 * SYSTEM ENDPOINTS (v6.0.4+)
 * ============================================================================ */

esp_err_t api_handler_system_reboot(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  JsonDocument doc;
  doc["status"] = 200;
  doc["message"] = "Rebooting in 1 second...";

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));

  esp_err_t ret = api_send_json(req, buf);

  // FEAT-086: log FOER selve genstarten (ellers naar loggen aldrig at blive skrevet)
  {
    char ip[16], user[24];
    http_get_client_info(req, ip, sizeof(ip), user, sizeof(user));
    system_log_add_event((uint8_t)SYSLOG_SRC_REST, user, ip, "Reboot udloest via REST API");
  }

  // Schedule reboot after response is sent
  delay(1000);
  ESP.restart();

  return ret;
}

esp_err_t api_handler_system_save(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  // Copy ST Logic programs to persistent config before saving (same as CLI save)
  st_logic_save_to_persist_config(&g_persist_config);

  // Calculate CRC before saving
  g_persist_config.crc16 = config_calculate_crc16(&g_persist_config);

  bool success = config_save_to_nvs(&g_persist_config);

  if (!success) {
    return api_send_error(req, 500, "Failed to save configuration");
  }

  JsonDocument doc;
  doc["status"] = 200;
  doc["message"] = "Configuration saved to NVS";

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

esp_err_t api_handler_system_load(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  bool success = config_load_from_nvs(&g_persist_config);
  if (success) {
    success = config_apply(&g_persist_config);
  }

  if (!success) {
    return api_send_error(req, 500, "Failed to load configuration");
  }

  JsonDocument doc;
  doc["status"] = 200;
  doc["message"] = "Configuration loaded and applied";

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

esp_err_t api_handler_system_defaults(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  config_struct_create_default();
  bool success = config_apply(&g_persist_config);

  if (!success) {
    return api_send_error(req, 500, "Failed to apply defaults");
  }

  JsonDocument doc;
  doc["status"] = 200;
  doc["message"] = "Reset to factory defaults (not saved)";

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

/* ============================================================================
 * COUNTER CONTROL ENDPOINTS (v6.0.4+)
 * ============================================================================ */

esp_err_t api_handler_counter_reset(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  int id = api_extract_id_from_uri(req, "/api/counters/");
  if (id < 1 || id > COUNTER_COUNT) {
    return api_send_error(req, 400, "Invalid counter ID (must be 1-4)");
  }

  counter_engine_reset(id);

  JsonDocument doc;
  doc["status"] = 200;
  doc["counter"] = id;
  doc["message"] = "Counter reset to start value";

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

esp_err_t api_handler_counter_start(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  int id = api_extract_id_from_uri(req, "/api/counters/");
  if (id < 1 || id > COUNTER_COUNT) {
    return api_send_error(req, 400, "Invalid counter ID (must be 1-4)");
  }

  CounterConfig cfg;
  if (!counter_engine_get_config(id, &cfg)) {
    return api_send_error(req, 404, "Counter not configured");
  }

  if (cfg.ctrl_reg >= HOLDING_REGS_SIZE) {
    return api_send_error(req, 500, "Counter has no control register");
  }

  // Set start bit (bit 1)
  uint16_t ctrl_val = registers_get_holding_register(cfg.ctrl_reg);
  ctrl_val |= 0x0002;
  registers_set_holding_register(cfg.ctrl_reg, ctrl_val);

  JsonDocument doc;
  doc["status"] = 200;
  doc["counter"] = id;
  doc["message"] = "Counter started";

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

esp_err_t api_handler_counter_stop(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  int id = api_extract_id_from_uri(req, "/api/counters/");
  if (id < 1 || id > COUNTER_COUNT) {
    return api_send_error(req, 400, "Invalid counter ID (must be 1-4)");
  }

  CounterConfig cfg;
  if (!counter_engine_get_config(id, &cfg)) {
    return api_send_error(req, 404, "Counter not configured");
  }

  if (cfg.ctrl_reg >= HOLDING_REGS_SIZE) {
    return api_send_error(req, 500, "Counter has no control register");
  }

  // Set stop bit (bit 2)
  uint16_t ctrl_val = registers_get_holding_register(cfg.ctrl_reg);
  ctrl_val |= 0x0004;
  registers_set_holding_register(cfg.ctrl_reg, ctrl_val);

  JsonDocument doc;
  doc["status"] = 200;
  doc["counter"] = id;
  doc["message"] = "Counter stopped";

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

/* ============================================================================
 * GPIO ENDPOINTS (v6.0.4+)
 * ============================================================================ */

esp_err_t api_handler_gpio(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  JsonDocument doc;
  JsonArray gpios = doc["gpios"].to<JsonArray>();

  // List GPIO mappings from var_maps array
  for (int i = 0; i < g_persist_config.var_map_count; i++) {
    const VariableMapping *m = &g_persist_config.var_maps[i];
    if (m->source_type != MAPPING_SOURCE_GPIO) continue;
    if (m->gpio_pin == 0 || m->gpio_pin == 0xFF) continue;

    JsonObject gpio = gpios.add<JsonObject>();
    gpio["pin"] = m->gpio_pin;
    gpio["direction"] = m->is_input ? "input" : "output";

    // Read current value
    uint8_t level = gpio_read(m->gpio_pin);
    gpio["value"] = level ? 1 : 0;

    // Show register binding if configured
    if (m->output_reg != 0xFFFF) {
      gpio["coil"] = m->output_reg;
    }
    if (m->input_reg != 0xFFFF) {
      gpio["register"] = m->input_reg;
    }
  }

  char buf[HTTP_JSON_DOC_SIZE];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

esp_err_t api_handler_gpio_single(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  int pin = api_extract_id_from_uri(req, "/api/gpio/");
  if (pin < 0 || (pin > 39 && (pin < 101 || pin > 108) && (pin < 201 || pin > 208))) {
    return api_send_error(req, 400, "Invalid GPIO pin (must be 0-39 or virtual 101-108/201-208)");
  }

  // Find GPIO mapping in var_maps
  const VariableMapping *found = NULL;
  for (int i = 0; i < g_persist_config.var_map_count; i++) {
    const VariableMapping *m = &g_persist_config.var_maps[i];
    if (m->source_type == MAPPING_SOURCE_GPIO && m->gpio_pin == pin) {
      found = m;
      break;
    }
  }

  JsonDocument doc;
  doc["pin"] = pin;
  doc["value"] = gpio_read(pin) ? 1 : 0;

  if (found) {
    doc["configured"] = true;
    doc["direction"] = found->is_input ? "input" : "output";
    if (found->output_reg != 0xFFFF) {
      doc["coil"] = found->output_reg;
    }
    if (found->input_reg != 0xFFFF) {
      doc["register"] = found->input_reg;
    }
  } else {
    doc["configured"] = false;
  }

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

esp_err_t api_handler_gpio_write(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  // GAP-11: Suffix routing for /config
  const char *uri = req->uri;
  size_t uri_len = strlen(uri);
  if (uri_len >= 7 && strcmp(uri + uri_len - 7, "/config") == 0) {
    return api_handler_gpio_config_post(req);
  }

  int pin = api_extract_id_from_uri(req, "/api/gpio/");
  if (pin < 0 || (pin > 39 && (pin < 101 || pin > 108) && (pin < 201 || pin > 208))) {
    return api_send_error(req, 400, "Invalid GPIO pin (must be 0-39 or virtual 101-108/201-208)");
  }

  // Read request body
  char content[128];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    return api_send_error(req, 400, "Failed to read request body");
  }
  content[ret] = '\0';

  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, content);
  if (error) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  if (!doc.containsKey("value")) {
    return api_send_error(req, 400, "Missing 'value' field");
  }

  uint8_t value = 0;
  if (doc["value"].is<bool>()) {
    value = doc["value"].as<bool>() ? 1 : 0;
  } else {
    value = doc["value"].as<int>() ? 1 : 0;
  }

  // Validate pin is configured as output in var_maps
  bool pin_configured_output = false;
  for (int i = 0; i < g_persist_config.var_map_count; i++) {
    const VariableMapping *m = &g_persist_config.var_maps[i];
    if (m->gpio_pin == pin && m->is_input == 0) {
      pin_configured_output = true;
      break;
    }
  }
  if (!pin_configured_output) {
    return api_send_error(req, 400, "GPIO pin not configured as output");
  }

  // Write GPIO
  gpio_write(pin, value);

  JsonDocument resp;
  resp["status"] = 200;
  resp["pin"] = pin;
  resp["value"] = value ? 1 : 0;

  char buf[256];
  serializeJson(resp, buf, sizeof(buf));

  return api_send_json(req, buf);
}

/* ============================================================================
 * LOGIC CONTROL ENDPOINTS (v6.0.4+)
 * ============================================================================ */

esp_err_t api_handler_logic_enable(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  int id = api_extract_id_from_uri(req, "/api/logic/");
  if (id < 1 || id > ST_LOGIC_MAX_PROGRAMS) {
    return api_send_error(req, 400, "Invalid logic program ID");
  }

  st_logic_engine_state_t *state = st_logic_get_state();
  if (!state) {
    return api_send_error(req, 500, "ST Logic not initialized");
  }

  bool success = st_logic_set_enabled(state, id - 1, 1);

  if (!success) {
    return api_send_error(req, 500, "Failed to enable program");
  }

  JsonDocument doc;
  doc["status"] = 200;
  doc["program"] = id;
  doc["enabled"] = true;
  doc["message"] = "Program enabled";

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

esp_err_t api_handler_logic_disable(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  int id = api_extract_id_from_uri(req, "/api/logic/");
  if (id < 1 || id > ST_LOGIC_MAX_PROGRAMS) {
    return api_send_error(req, 400, "Invalid logic program ID");
  }

  st_logic_engine_state_t *state = st_logic_get_state();
  if (!state) {
    return api_send_error(req, 500, "ST Logic not initialized");
  }

  bool success = st_logic_set_enabled(state, id - 1, 0);

  if (!success) {
    return api_send_error(req, 500, "Failed to disable program");
  }

  JsonDocument doc;
  doc["status"] = 200;
  doc["program"] = id;
  doc["enabled"] = false;
  doc["message"] = "Program disabled";

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

esp_err_t api_handler_logic_reinit(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  int id = api_extract_id_from_uri(req, "/api/logic/");
  if (id < 1 || id > ST_LOGIC_MAX_PROGRAMS) {
    return api_send_error(req, 400, "Invalid logic program ID");
  }

  st_logic_engine_state_t *state = st_logic_get_state();
  if (!state) {
    return api_send_error(req, 500, "ST Logic not initialized");
  }

  st_logic_program_config_t *prog = st_logic_get_program(state, id - 1);
  if (!prog || !prog->compiled) {
    return api_send_error(req, 400, "Program not compiled");
  }

  if (!st_logic_reinit(state, id - 1)) {
    return api_send_error(req, 500, "Failed to reinitialize program");
  }

  JsonDocument doc;
  doc["status"] = 200;
  doc["program"] = id;
  doc["message"] = "Cold restart: variables reset to initial values";

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

/* ============================================================================
 * FEAT-010: POST /api/logic/{id}/priority — {"priority":"normal"|"high"}
 * ============================================================================ */
esp_err_t api_handler_logic_priority_post(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  int id = api_extract_id_from_uri(req, "/api/logic/");
  if (id < 1 || id > ST_LOGIC_MAX_PROGRAMS) {
    return api_send_error(req, 400, "Invalid logic program ID");
  }

  st_logic_engine_state_t *state = st_logic_get_state();
  if (!state) {
    return api_send_error(req, 500, "ST Logic not initialized");
  }

  char content[128];
  int received = httpd_req_recv(req, content, sizeof(content) - 1);
  if (received <= 0) {
    return api_send_error(req, 400, "Failed to read request body");
  }
  content[received] = '\0';

  JsonDocument doc;
  if (deserializeJson(doc, content)) {
    return api_send_error(req, 400, "Invalid JSON");
  }
  if (!doc.containsKey("priority")) {
    return api_send_error(req, 400, "Missing 'priority' field");
  }
  const char *pstr = doc["priority"].as<const char *>();
  uint8_t priority;
  if (pstr && strcasecmp(pstr, "high") == 0) {
    priority = ST_LOGIC_PRIORITY_HIGH;
  } else if (pstr && strcasecmp(pstr, "normal") == 0) {
    priority = ST_LOGIC_PRIORITY_NORMAL;
  } else {
    return api_send_error(req, 400, "priority must be \"normal\" or \"high\"");
  }

  char err[96] = "";
  if (!st_logic_set_program_priority(state, id - 1, priority, err, sizeof(err))) {
    return api_send_error(req, 400, err[0] ? err : "Could not set priority");
  }

  JsonDocument resp;
  resp["status"] = 200;
  resp["program"] = id;
  resp["priority"] = (priority == ST_LOGIC_PRIORITY_HIGH) ? "HIGH" : "NORMAL";

  char buf[192];
  serializeJson(resp, buf, sizeof(buf));
  return api_send_json(req, buf);
}

/* ============================================================================
 * FEAT-010: POST /api/logic/{id}/interval — {"interval_ms":N}
 * ============================================================================ */
esp_err_t api_handler_logic_program_interval_post(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  int id = api_extract_id_from_uri(req, "/api/logic/");
  if (id < 1 || id > ST_LOGIC_MAX_PROGRAMS) {
    return api_send_error(req, 400, "Invalid logic program ID");
  }

  st_logic_engine_state_t *state = st_logic_get_state();
  if (!state) {
    return api_send_error(req, 500, "ST Logic not initialized");
  }

  char content[128];
  int received = httpd_req_recv(req, content, sizeof(content) - 1);
  if (received <= 0) {
    return api_send_error(req, 400, "Failed to read request body");
  }
  content[received] = '\0';

  JsonDocument doc;
  if (deserializeJson(doc, content)) {
    return api_send_error(req, 400, "Invalid JSON");
  }
  if (!doc.containsKey("interval_ms")) {
    return api_send_error(req, 400, "Missing 'interval_ms' field");
  }
  uint32_t interval_ms = doc["interval_ms"].as<uint32_t>();

  if (!st_logic_set_program_interval(state, id - 1, interval_ms)) {
    char msg[96];
    snprintf(msg, sizeof(msg), "interval_ms must be %u-%u", ST_LOGIC_INTERVAL_MIN_MS, ST_LOGIC_INTERVAL_MAX_MS);
    return api_send_error(req, 400, msg);
  }

  JsonDocument resp;
  resp["status"] = 200;
  resp["program"] = id;
  resp["interval_ms"] = state->programs[id - 1].interval_ms;

  char buf[192];
  serializeJson(resp, buf, sizeof(buf));
  return api_send_json(req, buf);
}

esp_err_t api_handler_logic_delete(httpd_req_t *req)
{
  // FEAT-020: Route debug DELETE to debug handler
  if (strstr(req->uri, "/debug/") != NULL) {
    return api_handler_logic_debug(req);
  }

  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  int id = api_extract_id_from_uri(req, "/api/logic/");
  if (id < 1 || id > ST_LOGIC_MAX_PROGRAMS) {
    return api_send_error(req, 400, "Invalid logic program ID");
  }

  st_logic_engine_state_t *state = st_logic_get_state();
  if (!state) {
    return api_send_error(req, 500, "ST Logic not initialized");
  }

  bool success = st_logic_delete(state, id - 1);

  if (!success) {
    return api_send_error(req, 500, "Failed to delete program");
  }

  JsonDocument doc;
  doc["status"] = 200;
  doc["program"] = id;
  doc["message"] = "Program deleted";

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

esp_err_t api_handler_logic_stats(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  int id = api_extract_id_from_uri(req, "/api/logic/");
  if (id < 1 || id > ST_LOGIC_MAX_PROGRAMS) {
    return api_send_error(req, 400, "Invalid logic program ID");
  }

  st_logic_engine_state_t *state = st_logic_get_state();
  if (!state) {
    return api_send_error(req, 500, "ST Logic not initialized");
  }

  st_logic_program_config_t *prog = &state->programs[id - 1];

  JsonDocument doc;
  doc["program"] = id;
  doc["name"] = prog->name;
  doc["enabled"] = prog->enabled ? true : false;
  doc["compiled"] = prog->compiled ? true : false;
  doc["execution_count"] = prog->execution_count;
  doc["error_count"] = prog->error_count;
  doc["last_execution_us"] = prog->last_execution_us;
  doc["min_execution_us"] = prog->min_execution_us;
  doc["max_execution_us"] = prog->max_execution_us;
  doc["overrun_count"] = prog->overrun_count;

  // Calculate average if we have executions
  if (prog->execution_count > 0) {
    doc["avg_execution_us"] = prog->total_execution_us / prog->execution_count;
  } else {
    doc["avg_execution_us"] = 0;
  }

  char *buf = (char *)malloc(HTTP_JSON_DOC_SIZE);
  if (!buf) {
    return api_send_error(req, 500, "Out of memory");
  }
  serializeJson(doc, buf, HTTP_JSON_DOC_SIZE);

  esp_err_t ret = api_send_json(req, buf);
  free(buf);
  return ret;
}

/* ============================================================================
 * CONFIG & DEBUG ENDPOINTS (v6.0.4+)
 * ============================================================================ */

esp_err_t api_handler_config_get(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  // Full config can be 6-8KB JSON — allocate on heap
  const size_t BUF_SIZE = 8192;
  char *buf = (char *)malloc(BUF_SIZE);
  if (!buf) {
    return api_send_error(req, 500, "Out of memory");
  }

  JsonDocument doc;

  // ── SYSTEM ──
  JsonObject sys = doc["system"].to<JsonObject>();
  sys["version"] = PROJECT_VERSION;
  sys["build"] = BUILD_NUMBER;
  sys["hostname"] = g_persist_config.hostname;
  sys["schema_version"] = g_persist_config.schema_version;

  // ── MODBUS MODE ──
  const char *mode_str = "slave";
  if (g_persist_config.modbus_mode == MODBUS_MODE_MASTER) mode_str = "master";
  else if (g_persist_config.modbus_mode == MODBUS_MODE_OFF) mode_str = "off";
  doc["modbus_mode"] = mode_str;

  // ── MODBUS SLAVE ──
  JsonObject slave = doc["modbus_slave"].to<JsonObject>();
  slave["enabled"] = g_persist_config.modbus_slave.enabled ? true : false;
  slave["slave_id"] = g_persist_config.modbus_slave.slave_id;
  slave["baudrate"] = g_persist_config.modbus_slave.baudrate;
  const char *par_str = "NONE";
  if (g_persist_config.modbus_slave.parity == 1) par_str = "EVEN";
  else if (g_persist_config.modbus_slave.parity == 2) par_str = "ODD";
  slave["parity"] = par_str;
  slave["stop_bits"] = g_persist_config.modbus_slave.stop_bits;
  slave["inter_frame_delay_ms"] = g_persist_config.modbus_slave.inter_frame_delay;

  // ── MODBUS MASTER ──
  JsonObject master = doc["modbus_master"].to<JsonObject>();
  master["enabled"] = g_persist_config.modbus_master.enabled ? true : false;
  if (g_persist_config.modbus_master.enabled) {
    master["baudrate"] = g_persist_config.modbus_master.baudrate;
    const char *mpar = "NONE";
    if (g_persist_config.modbus_master.parity == 1) mpar = "EVEN";
    else if (g_persist_config.modbus_master.parity == 2) mpar = "ODD";
    master["parity"] = mpar;
    master["stop_bits"] = g_persist_config.modbus_master.stop_bits;
    master["timeout_ms"] = g_persist_config.modbus_master.timeout_ms;
    master["inter_frame_delay_ms"] = g_persist_config.modbus_master.inter_frame_delay;
    master["max_requests_per_cycle"] = g_persist_config.modbus_master.max_requests_per_cycle;
    master["cache_ttl_ms"] = g_persist_config.modbus_master.cache_ttl_ms;
  }

  // ── ANALOG OUTPUTS (AO mode) ──
  JsonObject ao = doc["analog_outputs"].to<JsonObject>();
  ao["ao1_mode"] = g_persist_config.ao1_mode == AO_MODE_CURRENT ? "current" : "voltage";
  ao["ao2_mode"] = g_persist_config.ao2_mode == AO_MODE_CURRENT ? "current" : "voltage";

  // ── NETWORK ──
  JsonObject network = doc["network"].to<JsonObject>();
  network["enabled"] = g_persist_config.network.enabled ? true : false;
  network["ssid"] = g_persist_config.network.ssid;
  network["dhcp"] = g_persist_config.network.dhcp_enabled ? true : false;
  network["power_save"] = g_persist_config.network.wifi_power_save ? true : false;
  if (!g_persist_config.network.dhcp_enabled) {
    char ip_buf[16];
    struct in_addr addr;
    addr.s_addr = g_persist_config.network.static_ip;
    strncpy(ip_buf, inet_ntoa(addr), 15); ip_buf[15] = '\0';
    network["static_ip"] = ip_buf;
    addr.s_addr = g_persist_config.network.static_gateway;
    strncpy(ip_buf, inet_ntoa(addr), 15); ip_buf[15] = '\0';
    network["static_gateway"] = ip_buf;
    addr.s_addr = g_persist_config.network.static_netmask;
    strncpy(ip_buf, inet_ntoa(addr), 15); ip_buf[15] = '\0';
    network["static_netmask"] = ip_buf;
    addr.s_addr = g_persist_config.network.static_dns;
    strncpy(ip_buf, inet_ntoa(addr), 15); ip_buf[15] = '\0';
    network["static_dns"] = ip_buf;
  }

  // ── TELNET ──
  JsonObject telnet = doc["telnet"].to<JsonObject>();
  telnet["enabled"] = g_persist_config.network.telnet_enabled ? true : false;
  telnet["port"] = g_persist_config.network.telnet_port;

  // ── HTTP API ──
  JsonObject http = doc["http"].to<JsonObject>();
  http["enabled"] = g_persist_config.network.http.enabled ? true : false;
  http["port"] = g_persist_config.network.http.port;
  http["tls_enabled"] = g_persist_config.network.http.tls_enabled ? true : false;
  http["https_port"] = g_persist_config.https_port;  // BUG-350: dedikeret, ikke samme som "port"
  http["api_enabled"] = g_persist_config.network.http.api_enabled ? true : false;
  http["auth_enabled"] = g_persist_config.network.http.auth_enabled ? true : false;
  http["username"] = g_persist_config.network.http.username;  // FEAT-166: username isn't a secret (unlike password) — needed so the system.html settings form can show the CURRENT legacy admin username instead of leaving it blank
  const char *prio_str = "NORMAL";
  if (g_persist_config.network.http.priority == 0) prio_str = "LOW";
  else if (g_persist_config.network.http.priority == 2) prio_str = "HIGH";
  http["priority"] = prio_str;

  // ── SSE (FEAT: GUI-oprydning — tidligere kun laesbar via backup) ──
  JsonObject sse = doc["sse"].to<JsonObject>();
  sse["enabled"] = g_persist_config.network.http.sse_enabled ? true : false;
  sse["port"] = g_persist_config.network.http.sse_port;
  sse["max_clients"] = g_persist_config.network.http.sse_max_clients;
  sse["check_interval_ms"] = g_persist_config.network.http.sse_check_interval_ms;
  sse["heartbeat_ms"] = g_persist_config.network.http.sse_heartbeat_ms;

  // ── COUNTERS ──
  JsonArray counters = doc["counters"].to<JsonArray>();
  for (int i = 0; i < COUNTER_COUNT; i++) {
    const CounterConfig *c = &g_persist_config.counters[i];
    if (!c->enabled) continue;
    JsonObject co = counters.add<JsonObject>();
    co["id"] = i + 1;
    const char *hw = "SW";
    if (c->hw_mode == COUNTER_HW_SW_ISR) hw = "SW_ISR";
    else if (c->hw_mode == COUNTER_HW_PCNT) hw = "HW_PCNT";
    co["hw_mode"] = hw;
    const char *edge = "rising";
    if (c->edge_type == COUNTER_EDGE_FALLING) edge = "falling";
    else if (c->edge_type == COUNTER_EDGE_BOTH) edge = "both";
    co["edge"] = edge;
    co["direction"] = (c->direction == COUNTER_DIR_DOWN) ? "down" : "up";
    co["prescaler"] = c->prescaler;
    co["bit_width"] = c->bit_width;
    co["scale_factor"] = c->scale_factor;
    co["input_dis"] = c->input_dis;
    co["value_reg"] = c->value_reg;
    if (c->raw_reg != 0xFFFF) co["raw_reg"] = c->raw_reg;
    if (c->freq_reg != 0xFFFF) co["freq_reg"] = c->freq_reg;
    if (c->ctrl_reg != 0xFFFF) co["ctrl_reg"] = c->ctrl_reg;
    co["start_value"] = c->start_value;
    if (c->hw_gpio > 0) co["hw_gpio"] = c->hw_gpio;
    if (c->interrupt_pin > 0) co["interrupt_pin"] = c->interrupt_pin;
    co["debounce"] = (c->debounce_enabled && c->debounce_ms > 0) ? true : false;
    if (c->debounce_enabled && c->debounce_ms > 0) co["debounce_ms"] = c->debounce_ms;
  }

  // ── TIMERS ──
  JsonArray timers = doc["timers"].to<JsonArray>();
  for (int i = 0; i < TIMER_COUNT; i++) {
    const TimerConfig *t = &g_persist_config.timers[i];
    if (!t->enabled) continue;
    JsonObject ti = timers.add<JsonObject>();
    ti["id"] = i + 1;
    const char *tmode = "DISABLED";
    switch (t->mode) {
      case TIMER_MODE_1_ONESHOT:         tmode = "ONESHOT"; break;
      case TIMER_MODE_2_MONOSTABLE:      tmode = "MONOSTABLE"; break;
      case TIMER_MODE_3_ASTABLE:         tmode = "ASTABLE"; break;
      case TIMER_MODE_4_INPUT_TRIGGERED: tmode = "INPUT_TRIGGERED"; break;
      default: break;
    }
    ti["mode"] = tmode;
    ti["output_coil"] = t->output_coil;
    if (t->ctrl_reg != 0xFFFF) ti["ctrl_reg"] = t->ctrl_reg;
    switch (t->mode) {
      case TIMER_MODE_1_ONESHOT:
        ti["phase1_ms"] = t->phase1_duration_ms;
        ti["phase2_ms"] = t->phase2_duration_ms;
        ti["phase3_ms"] = t->phase3_duration_ms;
        break;
      case TIMER_MODE_2_MONOSTABLE:
        ti["pulse_ms"] = t->pulse_duration_ms;
        break;
      case TIMER_MODE_3_ASTABLE:
        ti["on_ms"] = t->on_duration_ms;
        ti["off_ms"] = t->off_duration_ms;
        break;
      case TIMER_MODE_4_INPUT_TRIGGERED:
        ti["input_dis"] = t->input_dis;
        ti["delay_ms"] = t->delay_ms;
        break;
      default: break;
    }
  }

  // ── GPIO MAPPINGS ──
  JsonArray gpios = doc["gpio"].to<JsonArray>();
  for (int i = 0; i < g_persist_config.var_map_count; i++) {
    const VariableMapping *m = &g_persist_config.var_maps[i];
    if (m->source_type != MAPPING_SOURCE_GPIO) continue;
    JsonObject g = gpios.add<JsonObject>();
    g["pin"] = m->gpio_pin;
    g["direction"] = m->is_input ? "input" : "output";
    if (m->is_input) {
      g["register"] = m->input_reg;
    } else {
      g["coil"] = m->output_reg;
    }
  }

  // ── ST LOGIC ──
  JsonObject logic = doc["st_logic"].to<JsonObject>();
  logic["interval_ms"] = g_persist_config.st_logic_interval_ms;
  st_logic_engine_state_t *st_state = st_logic_get_state();
  if (st_state) {
    logic["enabled"] = st_state->enabled ? true : false;
    JsonArray progs = logic["programs"].to<JsonArray>();
    for (int i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
      st_logic_program_config_t *p = &st_state->programs[i];
      if (p->source_size == 0 && !p->compiled) continue;
      JsonObject pr = progs.add<JsonObject>();
      pr["id"] = i + 1;
      pr["name"] = p->name;
      pr["enabled"] = p->enabled ? true : false;
      pr["compiled"] = p->compiled ? true : false;
      pr["source_size"] = p->source_size;
      pr["bindings"] = p->binding_count;
    }
  }

  // ── MODULES ──
  JsonObject modules = doc["modules"].to<JsonObject>();
  modules["counters"] = (g_persist_config.module_flags & MODULE_FLAG_COUNTERS_DISABLED) ? false : true;
  modules["timers"] = (g_persist_config.module_flags & MODULE_FLAG_TIMERS_DISABLED) ? false : true;
  modules["st_logic"] = (g_persist_config.module_flags & MODULE_FLAG_ST_LOGIC_DISABLED) ? false : true;

  // ── PERSISTENCE ──
  JsonObject persist = doc["persistence"].to<JsonObject>();
  persist["enabled"] = g_persist_config.persist_regs.enabled ? true : false;
  uint8_t grp_count = g_persist_config.persist_regs.group_count;
  if (grp_count > PERSIST_MAX_GROUPS) grp_count = PERSIST_MAX_GROUPS;
  persist["group_count"] = grp_count;

  size_t json_len = serializeJson(doc, buf, BUF_SIZE);
  if (json_len >= BUF_SIZE) {
    free(buf);
    return api_send_error(req, 500, "Response too large");
  }

  esp_err_t ret = api_send_json(req, buf);
  free(buf);
  return ret;
}

esp_err_t api_handler_debug_get(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  DebugFlags *dbg = debug_flags_get();

  JsonDocument doc;
  doc["all"] = dbg->all ? true : false;
  doc["config_save"] = dbg->config_save ? true : false;
  doc["config_load"] = dbg->config_load ? true : false;
  doc["wifi_connect"] = dbg->wifi_connect ? true : false;
  doc["network_validate"] = dbg->network_validate ? true : false;
  doc["http_server"] = dbg->http_server ? true : false;
  doc["http_api"] = dbg->http_api ? true : false;

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

esp_err_t api_handler_debug_set(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  // Read request body
  char content[256];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    return api_send_error(req, 400, "Failed to read request body");
  }
  content[ret] = '\0';

  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, content);
  if (error) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  // Apply debug flags
  if (doc.containsKey("all")) {
    debug_flags_set_all(doc["all"].as<bool>() ? 1 : 0);
  }
  if (doc.containsKey("config_save")) {
    debug_flags_set_config_save(doc["config_save"].as<bool>() ? 1 : 0);
  }
  if (doc.containsKey("config_load")) {
    debug_flags_set_config_load(doc["config_load"].as<bool>() ? 1 : 0);
  }
  if (doc.containsKey("wifi_connect")) {
    debug_flags_set_wifi_connect(doc["wifi_connect"].as<bool>() ? 1 : 0);
  }
  if (doc.containsKey("network_validate")) {
    debug_flags_set_network_validate(doc["network_validate"].as<bool>() ? 1 : 0);
  }
  if (doc.containsKey("http_server")) {
    debug_flags_set_http_server(doc["http_server"].as<bool>() ? 1 : 0);
  }
  if (doc.containsKey("http_api")) {
    debug_flags_set_http_api(doc["http_api"].as<bool>() ? 1 : 0);
  }

  // Return updated state
  DebugFlags *dbg = debug_flags_get();

  JsonDocument resp;
  resp["status"] = 200;
  resp["all"] = dbg->all ? true : false;
  resp["config_save"] = dbg->config_save ? true : false;
  resp["config_load"] = dbg->config_load ? true : false;
  resp["wifi_connect"] = dbg->wifi_connect ? true : false;
  resp["network_validate"] = dbg->network_validate ? true : false;
  resp["http_server"] = dbg->http_server ? true : false;
  resp["http_api"] = dbg->http_api ? true : false;

  char buf[256];
  serializeJson(resp, buf, sizeof(buf));

  return api_send_json(req, buf);
}

/* ============================================================================
 * GET /api/modbus/* - Modbus slave/master config + stats (GAP-4, GAP-5, GAP-18)
 * ============================================================================ */

esp_err_t api_handler_modbus_get(httpd_req_t *req)
{
  // FEAT-149: /api/modbus/activity has its own dedicated handler (own
  // stat-tracking/auth requirements). This wildcard handler (/api/modbus/*)
  // is registered before it, so ESP-IDF's httpd would otherwise route
  // /api/modbus/activity here first — where it matches neither "/slave" nor
  // "/master" below and gets rejected with 400 before ever reaching the
  // dedicated handler. Delegate immediately, before this handler's own
  // stat/auth housekeeping runs, to avoid double-counting.
  if (strstr(req->uri, "/activity") != NULL) {
    return api_handler_modbus_activity_get(req);
  }

  http_server_stat_request();
  CHECK_AUTH(req);

  const char *uri = req->uri;

  // Route based on suffix: /api/modbus/slave or /api/modbus/master
  bool is_slave = (strstr(uri, "/slave") != NULL);
  bool is_master = (strstr(uri, "/master") != NULL);

  if (!is_slave && !is_master) {
    return api_send_error(req, 400, "Use /api/modbus/slave or /api/modbus/master");
  }

  JsonDocument doc;

  // FEAT: GUI-oprydning — hvilken mode/UART selve RS485-transceiveren
  // bruger (tidligere kun laesbar/saetbar via en fuld config-restore).
  // Inkluderet i baade /slave og /master-svar, da valget er faelles.
  JsonObject xcvr = doc["transceiver"].to<JsonObject>();
  const char *mode_str = "slave";
  if (g_persist_config.modbus_mode == MODBUS_MODE_MASTER) mode_str = "master";
  else if (g_persist_config.modbus_mode == MODBUS_MODE_OFF) mode_str = "off";
  xcvr["mode"] = mode_str;
  xcvr["slave_uart"] = g_persist_config.modbus_slave_uart;
  xcvr["master_uart"] = g_persist_config.modbus_master_uart;

  if (is_slave) {
    JsonObject cfg = doc["config"].to<JsonObject>();
    cfg["enabled"] = g_persist_config.modbus_slave.enabled ? true : false;
    cfg["slave_id"] = g_persist_config.modbus_slave.slave_id;
    cfg["baudrate"] = g_persist_config.modbus_slave.baudrate;
    const char *par = "none";
    if (g_persist_config.modbus_slave.parity == 1) par = "even";
    else if (g_persist_config.modbus_slave.parity == 2) par = "odd";
    cfg["parity"] = par;
    cfg["stop_bits"] = g_persist_config.modbus_slave.stop_bits;
    cfg["inter_frame_delay_ms"] = g_persist_config.modbus_slave.inter_frame_delay;

    JsonObject stats = doc["stats"].to<JsonObject>();
    stats["total_requests"] = g_persist_config.modbus_slave.total_requests;
    stats["successful_requests"] = g_persist_config.modbus_slave.successful_requests;
    stats["crc_errors"] = g_persist_config.modbus_slave.crc_errors;
    stats["exception_errors"] = g_persist_config.modbus_slave.exception_errors;
  } else {
    JsonObject cfg = doc["config"].to<JsonObject>();
    cfg["enabled"] = g_modbus_master_config.enabled ? true : false;
    cfg["baudrate"] = g_modbus_master_config.baudrate;
    const char *par = "none";
    if (g_modbus_master_config.parity == 1) par = "even";
    else if (g_modbus_master_config.parity == 2) par = "odd";
    cfg["parity"] = par;
    cfg["stop_bits"] = g_modbus_master_config.stop_bits;
    cfg["timeout_ms"] = g_modbus_master_config.timeout_ms;
    cfg["inter_frame_delay_ms"] = g_modbus_master_config.inter_frame_delay;
    cfg["max_requests_per_cycle"] = g_modbus_master_config.max_requests_per_cycle;
    cfg["cache_ttl_ms"] = g_modbus_master_config.cache_ttl_ms;

    JsonObject stats = doc["stats"].to<JsonObject>();
    stats["total_requests"] = g_modbus_master_config.total_requests;
    stats["successful_requests"] = g_modbus_master_config.successful_requests;
    stats["timeout_errors"] = g_modbus_master_config.timeout_errors;
    stats["crc_errors"] = g_modbus_master_config.crc_errors;
    stats["exception_errors"] = g_modbus_master_config.exception_errors;
    stats["bus_busy_errors"] = g_modbus_bus_busy_errors;
  }

  char buf[HTTP_JSON_DOC_SIZE];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

/* ============================================================================
 * POST /api/modbus/* - Configure Modbus slave/master (GAP-4, GAP-5, GAP-18)
 * ============================================================================ */

esp_err_t api_handler_modbus_post(httpd_req_t *req)
{
  // FEAT-149: same wildcard-shadowing issue as api_handler_modbus_get() —
  // delegate /api/modbus/activity/* before this handler's own
  // stat/auth housekeeping runs.
  if (strstr(req->uri, "/activity") != NULL) {
    // FEAT-153: start/stop af logningen. Tjekkes FOER /clear, saa de tre
    // suffikser ikke kan forveksles.
    if (strstr(req->uri, "/activity/start") != NULL) {
      return api_handler_modbus_activity_toggle(req, true);
    }
    if (strstr(req->uri, "/activity/stop") != NULL) {
      return api_handler_modbus_activity_toggle(req, false);
    }
    return api_handler_modbus_activity_clear(req);
  }

  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  const char *uri = req->uri;
  bool is_slave = (strstr(uri, "/slave") != NULL);
  bool is_master = (strstr(uri, "/master") != NULL);

  // POST /api/modbus/master/reset-stats — reset all master statistics (v7.9.3.2)
  if (strstr(uri, "/master/reset-stats") != NULL) {
    mb_async_reset_stats();
    return api_send_json(req, "{\"status\":\"ok\",\"message\":\"Master statistics reset\"}");
  }

  // POST /api/modbus/master/rw — async read/write via cache+queue (v7.9.6.6)
  if (strstr(uri, "/master/rw") != NULL) {
    // FEAT-149: attribute any request queued from here to the dashboard's
    // manual Read/Write mini-form in the activity log.
    g_mb_activity_next_source = MB_SRC_DASHBOARD;

    char body[256];
    int blen = httpd_req_recv(req, body, sizeof(body) - 1);
    if (blen <= 0) return api_send_error(req, 400, "Empty body");
    body[blen] = '\0';

    JsonDocument jdoc;
    if (deserializeJson(jdoc, body)) return api_send_error(req, 400, "Invalid JSON");

    const char *op = jdoc["op"] | "";
    const char *type = jdoc["type"] | "holding";
    uint8_t slave_id = jdoc["slave"] | 0;
    uint16_t addr = jdoc["addr"] | 0;

    if (slave_id < 1 || slave_id > 247) return api_send_error(req, 400, "slave must be 1-247");

    // Determine request type
    mb_request_type_t rtype = MB_REQ_READ_HOLDING;
    if (strcasecmp(type, "coil") == 0) rtype = (strcasecmp(op, "write") == 0) ? MB_REQ_WRITE_COIL : MB_REQ_READ_COIL;
    else if (strcasecmp(type, "input") == 0) rtype = MB_REQ_READ_INPUT;
    else if (strcasecmp(type, "input-reg") == 0 || strcasecmp(type, "ireg") == 0) rtype = MB_REQ_READ_INPUT_REG;
    else if (strcasecmp(type, "holding") == 0) rtype = (strcasecmp(op, "write") == 0) ? MB_REQ_WRITE_HOLDING : MB_REQ_READ_HOLDING;

    char resp[256];

    if (strcasecmp(op, "read") == 0) {
      // Check cache first
      uint8_t cache_type = (uint8_t)rtype;
      mb_cache_entry_t *entry = mb_cache_find(slave_id, addr, cache_type);

      if (entry && entry->status == MB_CACHE_VALID) {
        uint32_t age_ms = (uint32_t)(millis() - entry->last_update_ms);
        snprintf(resp, sizeof(resp),
          "{\"status\":\"ok\",\"value\":%d,\"hex\":\"0x%04X\",\"signed\":%d,\"age_ms\":%u,\"source\":\"cache\"}",
          (uint16_t)entry->value.int_val, (uint16_t)entry->value.int_val,
          (int16_t)entry->value.int_val, age_ms);
      } else if (entry && entry->status == MB_CACHE_PENDING) {
        snprintf(resp, sizeof(resp), "{\"status\":\"pending\",\"message\":\"Request queued\"}");
      } else if (entry && entry->status == MB_CACHE_ERROR) {
        // Stale error — enqueue fresh read
        mb_async_queue_read(rtype, slave_id, addr);
        snprintf(resp, sizeof(resp), "{\"status\":\"pending\",\"message\":\"Re-queued (last: error)\"}");
      } else {
        // No cache entry — enqueue
        bool ok = mb_async_queue_read(rtype, slave_id, addr);
        snprintf(resp, sizeof(resp), "{\"status\":\"%s\",\"message\":\"%s\"}",
          ok ? "pending" : "error", ok ? "Queued for read" : "Queue full");
      }
    } else if (strcasecmp(op, "write") == 0) {
      int32_t val = jdoc["value"] | 0;
      st_value_t sv;
      memset(&sv, 0, sizeof(sv));
      sv.int_val = (int16_t)val;
      bool ok = mb_async_queue_write(rtype, slave_id, addr, sv);
      snprintf(resp, sizeof(resp), "{\"status\":\"%s\",\"message\":\"%s\",\"value\":%d}",
        ok ? "queued" : "error", ok ? "Write queued" : "Queue full", (int)val);
    } else {
      return api_send_error(req, 400, "op must be 'read' or 'write'");
    }

    return api_send_json(req, resp);
  }

  if (!is_slave && !is_master) {
    return api_send_error(req, 400, "Use /api/modbus/slave or /api/modbus/master");
  }

  // Read request body
  char content[512];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    return api_send_error(req, 400, "Failed to read request body");
  }
  content[ret] = '\0';

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, content);
  if (error) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  // FEAT: GUI-oprydning — transceiver mode/UART, tidligere kun saetbar via
  // en fuld config-restore. Kan sendes med enten /slave- eller
  // /master-POST'et, da valget er faelles for begge (samme fysiske RS485).
  if (doc.containsKey("transceiver")) {
    JsonObject x = doc["transceiver"];
    if (x.containsKey("mode")) {
      const char *m = x["mode"].as<const char*>();
      if (m) {
        if (strcasecmp(m, "slave") == 0) g_persist_config.modbus_mode = MODBUS_MODE_SLAVE;
        else if (strcasecmp(m, "master") == 0) g_persist_config.modbus_mode = MODBUS_MODE_MASTER;
        else if (strcasecmp(m, "off") == 0) g_persist_config.modbus_mode = MODBUS_MODE_OFF;
      }
    }
    if (x.containsKey("slave_uart")) {
      uint8_t u = x["slave_uart"].as<uint8_t>();
      if (u <= 2) g_persist_config.modbus_slave_uart = u;
    }
    if (x.containsKey("master_uart")) {
      uint8_t u = x["master_uart"].as<uint8_t>();
      if (u <= 2) g_persist_config.modbus_master_uart = u;
    }
  }

  if (is_slave) {
    if (doc.containsKey("slave_id")) {
      uint8_t sid = doc["slave_id"].as<uint8_t>();
      if (sid < 1 || sid > 247) {
        return api_send_error(req, 400, "slave_id must be 1-247");
      }
      g_persist_config.modbus_slave.slave_id = sid;
    }
    if (doc.containsKey("baudrate")) {
      g_persist_config.modbus_slave.baudrate = doc["baudrate"].as<uint32_t>();
    }
    if (doc.containsKey("parity")) {
      const char *p = doc["parity"].as<const char*>();
      if (p) {
        if (strcmp(p, "none") == 0) g_persist_config.modbus_slave.parity = 0;
        else if (strcmp(p, "even") == 0) g_persist_config.modbus_slave.parity = 1;
        else if (strcmp(p, "odd") == 0) g_persist_config.modbus_slave.parity = 2;
      }
    }
    if (doc.containsKey("stop_bits")) {
      g_persist_config.modbus_slave.stop_bits = doc["stop_bits"].as<uint8_t>();
    }
    if (doc.containsKey("inter_frame_delay_ms")) {
      g_persist_config.modbus_slave.inter_frame_delay = doc["inter_frame_delay_ms"].as<uint16_t>();
    }
  } else {
    if (doc.containsKey("enabled")) {
      g_modbus_master_config.enabled = doc["enabled"].as<bool>();
      g_persist_config.modbus_master.enabled = g_modbus_master_config.enabled;
    }
    if (doc.containsKey("baudrate")) {
      uint32_t baud = doc["baudrate"].as<uint32_t>();
      g_modbus_master_config.baudrate = baud;
      g_persist_config.modbus_master.baudrate = baud;
    }
    if (doc.containsKey("parity")) {
      const char *p = doc["parity"].as<const char*>();
      if (p) {
        uint8_t pval = 0;
        if (strcmp(p, "even") == 0) pval = 1;
        else if (strcmp(p, "odd") == 0) pval = 2;
        g_modbus_master_config.parity = pval;
        g_persist_config.modbus_master.parity = pval;
      }
    }
    if (doc.containsKey("stop_bits")) {
      uint8_t sb = doc["stop_bits"].as<uint8_t>();
      g_modbus_master_config.stop_bits = sb;
      g_persist_config.modbus_master.stop_bits = sb;
    }
    if (doc.containsKey("timeout_ms")) {
      uint16_t t = doc["timeout_ms"].as<uint16_t>();
      g_modbus_master_config.timeout_ms = t;
      g_persist_config.modbus_master.timeout_ms = t;
    }
    if (doc.containsKey("inter_frame_delay_ms")) {
      uint16_t d = doc["inter_frame_delay_ms"].as<uint16_t>();
      g_modbus_master_config.inter_frame_delay = d;
      g_persist_config.modbus_master.inter_frame_delay = d;
    }
    if (doc.containsKey("max_requests_per_cycle")) {
      uint8_t m = doc["max_requests_per_cycle"].as<uint8_t>();
      g_modbus_master_config.max_requests_per_cycle = m;
      g_persist_config.modbus_master.max_requests_per_cycle = m;
    }
    if (doc.containsKey("cache_ttl_ms")) {
      uint16_t ttl = doc["cache_ttl_ms"].as<uint16_t>();
      g_modbus_master_config.cache_ttl_ms = ttl;
      g_persist_config.modbus_master.cache_ttl_ms = ttl;
    }
    // Reconfigure if master is enabled
    if (g_modbus_master_config.enabled) {
      modbus_master_reconfigure();
    }
  }

  JsonDocument resp;
  resp["status"] = 200;
  resp["message"] = is_slave ? "Modbus slave config updated" : "Modbus master config updated";

  char buf2[256];
  serializeJson(resp, buf2, sizeof(buf2));

  return api_send_json(req, buf2);
}

/* ============================================================================
 * GET /api/wifi - Extended WiFi status (GAP-6, GAP-19, GAP-21)
 * ============================================================================ */

esp_err_t api_handler_wifi_get(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  JsonDocument doc;

  // Config
  JsonObject cfg = doc["config"].to<JsonObject>();
  cfg["enabled"] = g_persist_config.network.enabled ? true : false;
  cfg["ssid"] = g_persist_config.network.ssid;
  cfg["dhcp"] = g_persist_config.network.dhcp_enabled ? true : false;
  cfg["power_save"] = g_persist_config.network.wifi_power_save ? true : false;

  if (!g_persist_config.network.dhcp_enabled) {
    char ip_buf[16];
    struct in_addr addr;
    addr.s_addr = g_persist_config.network.static_ip;
    strncpy(ip_buf, inet_ntoa(addr), 15); ip_buf[15] = '\0';
    cfg["static_ip"] = (const char*)ip_buf;
    addr.s_addr = g_persist_config.network.static_gateway;
    strncpy(ip_buf, inet_ntoa(addr), 15); ip_buf[15] = '\0';
    cfg["static_gateway"] = (const char*)ip_buf;
    addr.s_addr = g_persist_config.network.static_netmask;
    strncpy(ip_buf, inet_ntoa(addr), 15); ip_buf[15] = '\0';
    cfg["static_netmask"] = (const char*)ip_buf;
    addr.s_addr = g_persist_config.network.static_dns;
    strncpy(ip_buf, inet_ntoa(addr), 15); ip_buf[15] = '\0';
    cfg["static_dns"] = (const char*)ip_buf;
  }

  // Runtime status
  JsonObject runtime = doc["runtime"].to<JsonObject>();
  runtime["connected"] = wifi_driver_is_connected() ? true : false;

  if (wifi_driver_is_connected()) {
    struct in_addr addr;
    char ip_buf[16];

    addr.s_addr = wifi_driver_get_local_ip();
    strncpy(ip_buf, inet_ntoa(addr), 15); ip_buf[15] = '\0';
    runtime["ip"] = (const char*)ip_buf;

    addr.s_addr = wifi_driver_get_gateway();
    strncpy(ip_buf, inet_ntoa(addr), 15); ip_buf[15] = '\0';
    runtime["gateway"] = (const char*)ip_buf;

    addr.s_addr = wifi_driver_get_netmask();
    strncpy(ip_buf, inet_ntoa(addr), 15); ip_buf[15] = '\0';
    runtime["netmask"] = (const char*)ip_buf;

    addr.s_addr = wifi_driver_get_dns();
    strncpy(ip_buf, inet_ntoa(addr), 15); ip_buf[15] = '\0';
    runtime["dns"] = (const char*)ip_buf;

    runtime["rssi"] = wifi_driver_get_rssi();
    runtime["uptime_ms"] = wifi_driver_get_uptime_ms();

    char ssid_buf[WIFI_SSID_MAX_LEN];
    if (wifi_driver_get_ssid(ssid_buf) == 0) {
      runtime["ssid"] = (const char*)ssid_buf;
    }
  }

  runtime["state"] = wifi_driver_get_state_string();

  char buf[HTTP_JSON_DOC_SIZE];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

/* ============================================================================
 * POST /api/wifi/* - WiFi config/connect/disconnect (GAP-6, GAP-19, GAP-21)
 * ============================================================================ */

esp_err_t api_handler_wifi_post(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  const char *uri = req->uri;
  size_t uri_len = strlen(uri);

  // POST /api/wifi/connect
  if (uri_len >= 8 && strcmp(uri + uri_len - 8, "/connect") == 0) {
    int result = network_manager_connect(&g_persist_config.network);
    if (result != 0) {
      return api_send_error(req, 500, "Failed to connect WiFi");
    }
    JsonDocument doc;
    doc["status"] = 200;
    doc["message"] = "WiFi connect initiated";
    char buf[256];
    serializeJson(doc, buf, sizeof(buf));
    return api_send_json(req, buf);
  }

  // POST /api/wifi/disconnect
  if (uri_len >= 11 && strcmp(uri + uri_len - 11, "/disconnect") == 0) {
    int result = network_manager_stop();
    if (result != 0) {
      return api_send_error(req, 500, "Failed to disconnect WiFi");
    }
    JsonDocument doc;
    doc["status"] = 200;
    doc["message"] = "WiFi disconnected";
    char buf[256];
    serializeJson(doc, buf, sizeof(buf));
    return api_send_json(req, buf);
  }

  // POST /api/wifi - WiFi configuration
  char content[512];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    return api_send_error(req, 400, "Failed to read request body");
  }
  content[ret] = '\0';

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, content);
  if (error) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  if (doc.containsKey("ssid")) {
    const char *ssid = doc["ssid"].as<const char*>();
    if (ssid) {
      strncpy(g_persist_config.network.ssid, ssid, WIFI_SSID_MAX_LEN - 1);
      g_persist_config.network.ssid[WIFI_SSID_MAX_LEN - 1] = '\0';
    }
  }
  if (doc.containsKey("password")) {
    const char *pw = doc["password"].as<const char*>();
    if (pw) {
      strncpy(g_persist_config.network.password, pw, WIFI_PASSWORD_MAX_LEN - 1);
      g_persist_config.network.password[WIFI_PASSWORD_MAX_LEN - 1] = '\0';
    }
  }
  if (doc.containsKey("dhcp")) {
    g_persist_config.network.dhcp_enabled = doc["dhcp"].as<bool>() ? 1 : 0;
  }
  if (doc.containsKey("enabled")) {
    g_persist_config.network.enabled = doc["enabled"].as<bool>() ? 1 : 0;
  }
  if (doc.containsKey("power_save")) {
    g_persist_config.network.wifi_power_save = doc["power_save"].as<bool>() ? 1 : 0;
  }
  if (doc.containsKey("static_ip")) {
    const char *ip = doc["static_ip"].as<const char*>();
    if (ip) g_persist_config.network.static_ip = inet_addr(ip);
  }
  if (doc.containsKey("static_gateway")) {
    const char *gw = doc["static_gateway"].as<const char*>();
    if (gw) g_persist_config.network.static_gateway = inet_addr(gw);
  }
  if (doc.containsKey("static_netmask")) {
    const char *nm = doc["static_netmask"].as<const char*>();
    if (nm) g_persist_config.network.static_netmask = inet_addr(nm);
  }
  if (doc.containsKey("static_dns")) {
    const char *dns = doc["static_dns"].as<const char*>();
    if (dns) g_persist_config.network.static_dns = inet_addr(dns);
  }

  JsonDocument resp;
  resp["status"] = 200;
  resp["message"] = "WiFi config updated";

  char buf2[256];
  serializeJson(resp, buf2, sizeof(buf2));

  return api_send_json(req, buf2);
}

/* ============================================================================
 * GET /api/ethernet - Ethernet (W5500) status (v6.1.0+)
 * ============================================================================ */

esp_err_t api_handler_ethernet_get(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  JsonDocument doc;

  // Config
  JsonObject cfg = doc["config"].to<JsonObject>();
  cfg["enabled"] = g_persist_config.network.ethernet.enabled ? true : false;
  cfg["dhcp"] = g_persist_config.network.ethernet.dhcp_enabled ? true : false;

  if (!g_persist_config.network.ethernet.dhcp_enabled) {
    char ip_buf[16];
    struct in_addr addr;
    addr.s_addr = g_persist_config.network.ethernet.static_ip;
    strncpy(ip_buf, inet_ntoa(addr), 15); ip_buf[15] = '\0';
    cfg["static_ip"] = (const char*)ip_buf;
    addr.s_addr = g_persist_config.network.ethernet.static_gateway;
    strncpy(ip_buf, inet_ntoa(addr), 15); ip_buf[15] = '\0';
    cfg["static_gateway"] = (const char*)ip_buf;
    addr.s_addr = g_persist_config.network.ethernet.static_netmask;
    strncpy(ip_buf, inet_ntoa(addr), 15); ip_buf[15] = '\0';
    cfg["static_netmask"] = (const char*)ip_buf;
    addr.s_addr = g_persist_config.network.ethernet.static_dns;
    strncpy(ip_buf, inet_ntoa(addr), 15); ip_buf[15] = '\0';
    cfg["static_dns"] = (const char*)ip_buf;
  }

  if (g_persist_config.network.ethernet.hostname[0]) {
    cfg["hostname"] = g_persist_config.network.ethernet.hostname;
  }

  // Runtime status
  JsonObject runtime = doc["runtime"].to<JsonObject>();
  runtime["connected"] = ethernet_driver_is_connected() ? true : false;

  if (ethernet_driver_is_connected()) {
    struct in_addr addr;
    char ip_buf[16];

    addr.s_addr = ethernet_driver_get_local_ip();
    strncpy(ip_buf, inet_ntoa(addr), 15); ip_buf[15] = '\0';
    runtime["ip"] = (const char*)ip_buf;

    addr.s_addr = ethernet_driver_get_gateway();
    strncpy(ip_buf, inet_ntoa(addr), 15); ip_buf[15] = '\0';
    runtime["gateway"] = (const char*)ip_buf;

    addr.s_addr = ethernet_driver_get_netmask();
    strncpy(ip_buf, inet_ntoa(addr), 15); ip_buf[15] = '\0';
    runtime["netmask"] = (const char*)ip_buf;

    addr.s_addr = ethernet_driver_get_dns();
    strncpy(ip_buf, inet_ntoa(addr), 15); ip_buf[15] = '\0';
    runtime["dns"] = (const char*)ip_buf;

    runtime["speed_mbps"] = ethernet_driver_get_speed();
    runtime["full_duplex"] = ethernet_driver_is_full_duplex() ? true : false;
    runtime["uptime_ms"] = ethernet_driver_get_uptime_ms();

    char mac_str[18];
    ethernet_driver_get_mac_str(mac_str);
    runtime["mac"] = (const char*)mac_str;
  }

  runtime["state"] = ethernet_driver_get_state_string();

  char buf[HTTP_JSON_DOC_SIZE];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

/* ============================================================================
 * POST /api/ethernet - Ethernet (W5500) configuration (v6.1.0+)
 * ============================================================================ */

esp_err_t api_handler_ethernet_post(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  char content[512];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    return api_send_error(req, 400, "Failed to read request body");
  }
  content[ret] = '\0';

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, content);
  if (error) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  if (doc.containsKey("enabled")) {
    g_persist_config.network.ethernet.enabled = doc["enabled"].as<bool>() ? 1 : 0;
  }
  if (doc.containsKey("dhcp")) {
    g_persist_config.network.ethernet.dhcp_enabled = doc["dhcp"].as<bool>() ? 1 : 0;
  }
  if (doc.containsKey("static_ip")) {
    const char *ip = doc["static_ip"].as<const char*>();
    if (ip) g_persist_config.network.ethernet.static_ip = inet_addr(ip);
  }
  if (doc.containsKey("static_gateway")) {
    const char *gw = doc["static_gateway"].as<const char*>();
    if (gw) g_persist_config.network.ethernet.static_gateway = inet_addr(gw);
  }
  if (doc.containsKey("static_netmask")) {
    const char *nm = doc["static_netmask"].as<const char*>();
    if (nm) g_persist_config.network.ethernet.static_netmask = inet_addr(nm);
  }
  if (doc.containsKey("static_dns")) {
    const char *dns = doc["static_dns"].as<const char*>();
    if (dns) g_persist_config.network.ethernet.static_dns = inet_addr(dns);
  }
  if (doc.containsKey("hostname")) {
    const char *hn = doc["hostname"].as<const char*>();
    if (hn) {
      strncpy(g_persist_config.network.ethernet.hostname, hn,
              sizeof(g_persist_config.network.ethernet.hostname) - 1);
      g_persist_config.network.ethernet.hostname[sizeof(g_persist_config.network.ethernet.hostname) - 1] = '\0';
      // BUG-371: anvend straks — se api_handler_hostname_post()'s kommentar
      // for den fulde forklaring (hostname havde foer ingen netvaerkseffekt).
      ethernet_driver_set_hostname(g_persist_config.network.ethernet.hostname[0]
        ? g_persist_config.network.ethernet.hostname
        : g_persist_config.hostname);
    }
  }

  JsonDocument resp;
  resp["status"] = 200;
  resp["message"] = "Ethernet config updated";

  char buf2[256];
  serializeJson(resp, buf2, sizeof(buf2));

  return api_send_json(req, buf2);
}

/* ============================================================================
 * POST /api/http - HTTP server configuration (GAP-7)
 * ============================================================================ */

esp_err_t api_handler_http_config_post(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  char content[512];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    return api_send_error(req, 400, "Failed to read request body");
  }
  content[ret] = '\0';

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, content);
  if (error) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  if (doc.containsKey("enabled")) {
    g_persist_config.network.http.enabled = doc["enabled"].as<bool>() ? 1 : 0;
  }
  if (doc.containsKey("port")) {
    g_persist_config.network.http.port = doc["port"].as<uint16_t>();
  }
  if (doc.containsKey("auth_enabled")) {
    g_persist_config.network.http.auth_enabled = doc["auth_enabled"].as<bool>() ? 1 : 0;
  }
  if (doc.containsKey("api_enabled")) {
    g_persist_config.network.http.api_enabled = doc["api_enabled"].as<bool>() ? 1 : 0;
  }
  if (doc.containsKey("tls_enabled")) {
    g_persist_config.network.http.tls_enabled = doc["tls_enabled"].as<bool>() ? 1 : 0;
  }
  if (doc.containsKey("https_port")) {
    // BUG-350: dedikeret HTTPS-port — deler ikke "port" med almindelig HTTP
    uint16_t p = doc["https_port"].as<uint16_t>();
    if (p >= 1) g_persist_config.https_port = p;
  }
  if (doc.containsKey("username")) {
    const char *u = doc["username"].as<const char*>();
    if (u) {
      strncpy(g_persist_config.network.http.username, u, HTTP_AUTH_USERNAME_MAX_LEN - 1);
      g_persist_config.network.http.username[HTTP_AUTH_USERNAME_MAX_LEN - 1] = '\0';
    }
  }
  if (doc.containsKey("password")) {
    const char *p = doc["password"].as<const char*>();
    if (p) {
      rbac_hash_and_store_legacy_password(&g_persist_config, p);  // BUG-352
    }
  }
  if (doc.containsKey("priority")) {
    const char *prio = doc["priority"].as<const char*>();
    if (prio) {
      if (strcmp(prio, "LOW") == 0 || strcmp(prio, "low") == 0) g_persist_config.network.http.priority = 0;
      else if (strcmp(prio, "HIGH") == 0 || strcmp(prio, "high") == 0) g_persist_config.network.http.priority = 2;
      else g_persist_config.network.http.priority = 1;
    }
  }

  // FEAT: GUI-oprydning — SSE-serverindstillinger, tidligere kun saetbare
  // via en fuld config-restore (samme "network.http"-struct som resten af
  // denne handler, saa ingen ny endpoint noedvendig). Sendes valgfrit som
  // et indlejret "sse"-objekt: {"sse":{"enabled":true,"port":81,...}}.
  if (doc.containsKey("sse")) {
    JsonObject s = doc["sse"];
    if (s.containsKey("enabled"))           g_persist_config.network.http.sse_enabled           = s["enabled"].as<bool>() ? 1 : 0;
    if (s.containsKey("port"))              g_persist_config.network.http.sse_port              = s["port"].as<uint16_t>();
    if (s.containsKey("max_clients")) {
      uint8_t mc = s["max_clients"].as<uint8_t>();
      if (mc >= 1 && mc <= 5) g_persist_config.network.http.sse_max_clients = mc;
    }
    if (s.containsKey("check_interval_ms")) {
      uint16_t iv = s["check_interval_ms"].as<uint16_t>();
      if (iv >= 50 && iv <= 5000) g_persist_config.network.http.sse_check_interval_ms = iv;
    }
    if (s.containsKey("heartbeat_ms")) {
      uint16_t hb = s["heartbeat_ms"].as<uint16_t>();
      if (hb >= 1000 && hb <= 60000) g_persist_config.network.http.sse_heartbeat_ms = hb;
    }
  }

  JsonDocument resp;
  resp["status"] = 200;
  resp["message"] = "HTTP config updated (reboot required for port/TLS changes)";

  char buf2[256];
  serializeJson(resp, buf2, sizeof(buf2));

  return api_send_json(req, buf2);
}

/* ============================================================================
 * Counter config POST + control (GAP-1, GAP-2, GAP-16) - suffix routing
 * ============================================================================ */

static esp_err_t api_handler_counter_config_post(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  int id = api_extract_id_from_uri(req, "/api/counters/");
  if (id < 1 || id > COUNTER_COUNT) {
    return api_send_error(req, 400, "Invalid counter ID (must be 1-4)");
  }

  char content[512];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    return api_send_error(req, 400, "Failed to read request body");
  }
  content[ret] = '\0';

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, content);
  if (error) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  // Get current config as base
  CounterConfig cfg;
  counter_config_get(id, &cfg);

  if (doc.containsKey("enabled")) cfg.enabled = doc["enabled"].as<bool>() ? 1 : 0;
  if (doc.containsKey("hw_mode")) {
    const char *m = doc["hw_mode"].as<const char*>();
    if (m) {
      if (strcmp(m, "sw") == 0 || strcmp(m, "SW") == 0) cfg.hw_mode = COUNTER_HW_SW;
      else if (strcmp(m, "sw_isr") == 0 || strcmp(m, "SW_ISR") == 0) cfg.hw_mode = COUNTER_HW_SW_ISR;
      else if (strcmp(m, "hw") == 0 || strcmp(m, "HW_PCNT") == 0 || strcmp(m, "hw_pcnt") == 0) cfg.hw_mode = COUNTER_HW_PCNT;
    }
  }
  if (doc.containsKey("edge")) {
    const char *e = doc["edge"].as<const char*>();
    if (e) {
      if (strcmp(e, "rising") == 0) cfg.edge_type = COUNTER_EDGE_RISING;
      else if (strcmp(e, "falling") == 0) cfg.edge_type = COUNTER_EDGE_FALLING;
      else if (strcmp(e, "both") == 0) cfg.edge_type = COUNTER_EDGE_BOTH;
    }
  }
  if (doc.containsKey("direction")) {
    const char *d = doc["direction"].as<const char*>();
    if (d) {
      cfg.direction = (strcmp(d, "down") == 0) ? COUNTER_DIR_DOWN : COUNTER_DIR_UP;
    }
  }
  if (doc.containsKey("prescaler")) cfg.prescaler = doc["prescaler"].as<uint16_t>();
  if (doc.containsKey("bit_width")) cfg.bit_width = doc["bit_width"].as<uint8_t>();
  if (doc.containsKey("scale_factor")) cfg.scale_factor = doc["scale_factor"].as<float>();
  if (doc.containsKey("value_reg")) cfg.value_reg = doc["value_reg"].as<uint16_t>();
  if (doc.containsKey("raw_reg")) cfg.raw_reg = doc["raw_reg"].as<uint16_t>();
  if (doc.containsKey("freq_reg")) cfg.freq_reg = doc["freq_reg"].as<uint16_t>();
  if (doc.containsKey("ctrl_reg")) cfg.ctrl_reg = doc["ctrl_reg"].as<uint16_t>();
  if (doc.containsKey("start_value")) cfg.start_value = doc["start_value"].as<uint64_t>();
  if (doc.containsKey("hw_gpio")) cfg.hw_gpio = doc["hw_gpio"].as<uint8_t>();
  if (doc.containsKey("interrupt_pin")) cfg.interrupt_pin = doc["interrupt_pin"].as<uint8_t>();
  if (doc.containsKey("input_dis")) cfg.input_dis = doc["input_dis"].as<uint8_t>();
  if (doc.containsKey("debounce_ms")) {
    cfg.debounce_ms = doc["debounce_ms"].as<uint16_t>();
    cfg.debounce_enabled = (cfg.debounce_ms > 0) ? 1 : 0;
  }
  if (doc.containsKey("compare_enabled")) cfg.compare_enabled = doc["compare_enabled"].as<bool>() ? 1 : 0;
  if (doc.containsKey("compare_value")) cfg.compare_value = doc["compare_value"].as<uint64_t>();
  if (doc.containsKey("compare_mode")) cfg.compare_mode = doc["compare_mode"].as<uint8_t>();
  // FEAT-171: fandtes i struct+motor men manglede i denne handler — kun
  // tilgængelige via fuld backup/restore før. compare_source klampes til de
  // 3 gyldige værdier (0=raw,1=prescaled,2=scaled) for at matche sanitize().
  if (doc.containsKey("compare_source")) {
    uint8_t cs = doc["compare_source"].as<uint8_t>();
    cfg.compare_source = (cs > 2) ? 1 : cs;
  }
  if (doc.containsKey("reset_on_read")) cfg.reset_on_read = doc["reset_on_read"].as<bool>() ? 1 : 0;

  // Apply
  counter_config_set(id, &cfg);
  counter_engine_configure(id, &cfg);

  JsonDocument resp;
  resp["status"] = 200;
  resp["counter"] = id;
  resp["message"] = "Counter configured";

  char buf2[256];
  serializeJson(resp, buf2, sizeof(buf2));

  return api_send_json(req, buf2);
}

static esp_err_t api_handler_counter_control_post(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  int id = api_extract_id_from_uri(req, "/api/counters/");
  if (id < 1 || id > COUNTER_COUNT) {
    return api_send_error(req, 400, "Invalid counter ID (must be 1-4)");
  }

  char content[256];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    return api_send_error(req, 400, "Failed to read request body");
  }
  content[ret] = '\0';

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, content);
  if (error) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  CounterConfig cfg;
  if (!counter_engine_get_config(id, &cfg)) {
    return api_send_error(req, 404, "Counter not configured");
  }

  if (cfg.ctrl_reg >= HOLDING_REGS_SIZE) {
    return api_send_error(req, 500, "Counter has no control register");
  }

  uint16_t ctrl_val = registers_get_holding_register(cfg.ctrl_reg);

  // FEAT-171: dette skrev hidtil kun de TRANSIENTE bit1(start)/bit2(stop)-
  // kommandobits — de faar counteren til rent faktisk at starte/stoppe (jf.
  // counter_engine_handle_control()), men rører ALDRIG bit7, som er den
  // ENESTE bit motoren bruger som vedvarende running-status (og som GET
  // /api/counters/{id}'s "running"-felt læser, jf. BUG-376-rettelsen
  // ovenfor). Resultat: et REST-startet counter viste for evigt
  // running:false. Sæt/ryd nu bit7 direkte — motoren tjekker den hver
  // loop-tick og starter/stopper selv (counter_engine.cpp:328-365),
  // samme bit som CLI'ens `set counter <id> control running:on` allerede
  // bruger, saa GET-status nu bliver korrekt uanset hvilken vej der brugtes.
  if (doc.containsKey("running")) {
    if (doc["running"].as<bool>()) {
      ctrl_val |= 0x0080;
    } else {
      ctrl_val &= ~0x0080;
    }
  }
  if (doc.containsKey("reset") && doc["reset"].as<bool>()) {
    ctrl_val |= 0x0001;  // Reset bit
  }

  registers_set_holding_register(cfg.ctrl_reg, ctrl_val);

  JsonDocument resp;
  resp["status"] = 200;
  resp["counter"] = id;
  resp["message"] = "Counter control updated";

  char buf2[256];
  serializeJson(resp, buf2, sizeof(buf2));

  return api_send_json(req, buf2);
}

/* ============================================================================
 * DELETE /api/counters/{id} - Delete counter (GAP-16)
 * ============================================================================ */

esp_err_t api_handler_counter_delete(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  int id = api_extract_id_from_uri(req, "/api/counters/");
  if (id < 1 || id > COUNTER_COUNT) {
    return api_send_error(req, 400, "Invalid counter ID (must be 1-4)");
  }

  // Reset to defaults (disabled)
  CounterConfig cfg = counter_config_defaults(id);
  counter_config_set(id, &cfg);
  counter_engine_configure(id, &cfg);

  JsonDocument doc;
  doc["status"] = 200;
  doc["counter"] = id;
  doc["message"] = "Counter deleted (reset to defaults)";

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

static esp_err_t api_handler_timer_control_post(httpd_req_t *req);

/* ============================================================================
 * POST /api/timers/{id} - Configure timer (GAP-3)
 * ============================================================================ */

esp_err_t api_handler_timer_config_post(httpd_req_t *req)
{
  const char *uri = req->uri;

  // Extract timer ID
  int id = api_extract_id_from_uri(req, "/api/timers/");
  if (id < 1 || id > TIMER_COUNT) {
    return api_send_error(req, 400, "Invalid timer ID (must be 1-4)");
  }

  // FEAT-171: /control er en action-suffix, ikke en del af konfigurations-
  // body'en — ESP-IDFs wildcard matcher kun i slutningen af URI'en (samme
  // aarsag som api_handler_counter_single's egen suffix-dispatch), saa
  // POST /api/timers/{id}/control ramler ind i DENNE handler (den er
  // registreret paa /api/timers/*) og skal delegeres videre her, FOER det
  // strenge exact-match-tjek nedenfor ellers ville afvise den som ukendt.
  size_t uri_len = strlen(uri);
  if (uri_len >= 8 && strcmp(uri + uri_len - 8, "/control") == 0) {
    return api_handler_timer_control_post(req);
  }

  // Check if URI is exactly /api/timers/{id} (no suffix)
  char expected_uri[32];
  snprintf(expected_uri, sizeof(expected_uri), "/api/timers/%d", id);
  if (strcmp(uri, expected_uri) != 0) {
    return api_send_error(req, 404, "Unknown timer action");
  }

  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  char content[512];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    return api_send_error(req, 400, "Failed to read request body");
  }
  content[ret] = '\0';

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, content);
  if (error) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  // Get current config as base
  TimerConfig cfg;
  timer_engine_get_config(id, &cfg);

  if (doc.containsKey("enabled")) cfg.enabled = doc["enabled"].as<bool>() ? 1 : 0;
  if (doc.containsKey("mode")) {
    const char *m = doc["mode"].as<const char*>();
    if (m) {
      if (strcmp(m, "ONESHOT") == 0 || strcmp(m, "oneshot") == 0) cfg.mode = TIMER_MODE_1_ONESHOT;
      else if (strcmp(m, "MONOSTABLE") == 0 || strcmp(m, "monostable") == 0) cfg.mode = TIMER_MODE_2_MONOSTABLE;
      else if (strcmp(m, "ASTABLE") == 0 || strcmp(m, "astable") == 0) cfg.mode = TIMER_MODE_3_ASTABLE;
      else if (strcmp(m, "INPUT_TRIGGERED") == 0 || strcmp(m, "input_triggered") == 0) cfg.mode = TIMER_MODE_4_INPUT_TRIGGERED;
    }
  }
  if (doc.containsKey("output_coil")) cfg.output_coil = doc["output_coil"].as<uint16_t>();
  if (doc.containsKey("ctrl_reg")) cfg.ctrl_reg = doc["ctrl_reg"].as<uint16_t>();

  // Mode-specific params
  if (doc.containsKey("phase1_duration_ms")) cfg.phase1_duration_ms = doc["phase1_duration_ms"].as<uint32_t>();
  if (doc.containsKey("phase2_duration_ms")) cfg.phase2_duration_ms = doc["phase2_duration_ms"].as<uint32_t>();
  if (doc.containsKey("phase3_duration_ms")) cfg.phase3_duration_ms = doc["phase3_duration_ms"].as<uint32_t>();
  if (doc.containsKey("pulse_duration_ms")) cfg.pulse_duration_ms = doc["pulse_duration_ms"].as<uint32_t>();
  if (doc.containsKey("on_duration_ms") || doc.containsKey("on_ms")) {
    cfg.on_duration_ms = doc.containsKey("on_duration_ms") ? doc["on_duration_ms"].as<uint32_t>() : doc["on_ms"].as<uint32_t>();
  }
  if (doc.containsKey("off_duration_ms") || doc.containsKey("off_ms")) {
    cfg.off_duration_ms = doc.containsKey("off_duration_ms") ? doc["off_duration_ms"].as<uint32_t>() : doc["off_ms"].as<uint32_t>();
  }
  if (doc.containsKey("input_dis")) cfg.input_dis = doc["input_dis"].as<uint8_t>();
  if (doc.containsKey("delay_ms")) cfg.delay_ms = doc["delay_ms"].as<uint32_t>();
  // FEAT-171: fandtes i struct'en men manglede i denne handler — output-
  // polaritet og Mode-4-triggerkant kunne før kun sættes via fuld
  // backup/restore. trigger_level er BEVIDST ikke tilføjet her — bekræftet
  // aldrig læst af mode_monostable() (timer_engine.cpp), et dødt felt.
  if (doc.containsKey("phase1_output_state")) cfg.phase1_output_state = doc["phase1_output_state"].as<bool>() ? 1 : 0;
  if (doc.containsKey("phase2_output_state")) cfg.phase2_output_state = doc["phase2_output_state"].as<bool>() ? 1 : 0;
  if (doc.containsKey("phase3_output_state")) cfg.phase3_output_state = doc["phase3_output_state"].as<bool>() ? 1 : 0;
  if (doc.containsKey("trigger_edge")) cfg.trigger_edge = doc["trigger_edge"].as<bool>() ? 1 : 0;

  // Apply config
  memcpy(&g_persist_config.timers[id - 1], &cfg, sizeof(TimerConfig));
  timer_engine_configure(id, &cfg);

  JsonDocument resp;
  resp["status"] = 200;
  resp["timer"] = id;
  resp["message"] = "Timer configured";

  char buf2[256];
  serializeJson(resp, buf2, sizeof(buf2));

  return api_send_json(req, buf2);
}

/* ============================================================================
 * POST /api/timers/{id}/control - Start/stop/reset a timer (FEAT-171)
 *
 * Timere havde hidtil INGEN REST-vej til at starte/stoppe/nulstille (kun
 * counters har det, se api_handler_counter_control_post ovenfor) — kun
 * muligt via CLI eller en raw holding-register-skrivning til ctrl_reg.
 * Samme body-kontrakt og bit-mønster som counter-varianten, tilpasset
 * timerens egen ctrl_reg-semantik (bit0=RESET, bit1=START, bit2=STOP, alle
 * transiente/selv-clearende, jf. timer_engine.cpp:231-276).
 * ============================================================================ */

static esp_err_t api_handler_timer_control_post(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  int id = api_extract_id_from_uri(req, "/api/timers/");
  if (id < 1 || id > TIMER_COUNT) {
    return api_send_error(req, 400, "Invalid timer ID (must be 1-4)");
  }

  char content[256];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    return api_send_error(req, 400, "Failed to read request body");
  }
  content[ret] = '\0';

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, content);
  if (error) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  TimerConfig cfg;
  if (!timer_engine_get_config(id, &cfg)) {
    return api_send_error(req, 404, "Timer not configured");
  }

  if (cfg.ctrl_reg >= HOLDING_REGS_SIZE) {
    return api_send_error(req, 500, "Timer has no control register");
  }

  uint16_t ctrl_val = registers_get_holding_register(cfg.ctrl_reg);

  if (doc.containsKey("running")) {
    if (doc["running"].as<bool>()) {
      ctrl_val |= 0x0001;  // Start bit
    } else {
      ctrl_val &= ~0x0001;
      ctrl_val |= 0x0002;  // Stop
    }
  }
  if (doc.containsKey("reset") && doc["reset"].as<bool>()) {
    ctrl_val |= 0x0004;  // Reset bit
  }

  registers_set_holding_register(cfg.ctrl_reg, ctrl_val);

  JsonDocument resp;
  resp["status"] = 200;
  resp["timer"] = id;
  resp["message"] = "Timer control updated";

  char buf3[256];
  serializeJson(resp, buf3, sizeof(buf3));

  return api_send_json(req, buf3);
}

/* ============================================================================
 * DELETE /api/timers/{id} - Delete timer (GAP-3)
 * ============================================================================ */

esp_err_t api_handler_timer_delete(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  int id = api_extract_id_from_uri(req, "/api/timers/");
  if (id < 1 || id > TIMER_COUNT) {
    return api_send_error(req, 400, "Invalid timer ID (must be 1-4)");
  }

  // Reset to disabled
  TimerConfig cfg;
  memset(&cfg, 0, sizeof(TimerConfig));
  cfg.mode = TIMER_MODE_DISABLED;
  cfg.output_coil = 0xFFFF;
  cfg.ctrl_reg = 0xFFFF;

  memcpy(&g_persist_config.timers[id - 1], &cfg, sizeof(TimerConfig));
  timer_engine_configure(id, &cfg);

  JsonDocument doc;
  doc["status"] = 200;
  doc["timer"] = id;
  doc["message"] = "Timer deleted (reset to defaults)";

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

/* ============================================================================
 * DELETE /api/gpio/{pin} - Remove GPIO mapping (GAP-11)
 * ============================================================================ */

esp_err_t api_handler_gpio_config_delete(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  int pin = api_extract_id_from_uri(req, "/api/gpio/");
  if (pin < 0 || (pin > 39 && (pin < 101 || pin > 108) && (pin < 201 || pin > 208))) {
    return api_send_error(req, 400, "Invalid GPIO pin (must be 0-39 or virtual 101-108/201-208)");
  }

  // Find and remove mapping
  bool found = false;
  for (int i = 0; i < g_persist_config.var_map_count; i++) {
    if (g_persist_config.var_maps[i].source_type == MAPPING_SOURCE_GPIO &&
        g_persist_config.var_maps[i].gpio_pin == pin) {
      // Shift remaining mappings down
      for (int j = i; j < g_persist_config.var_map_count - 1; j++) {
        memcpy(&g_persist_config.var_maps[j], &g_persist_config.var_maps[j + 1], sizeof(VariableMapping));
      }
      g_persist_config.var_map_count--;
      found = true;
      break;
    }
  }

  if (!found) {
    return api_send_error(req, 404, "GPIO pin not mapped");
  }

  JsonDocument doc;
  doc["status"] = 200;
  doc["pin"] = pin;
  doc["message"] = "GPIO mapping removed";

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

/* ============================================================================
 * POST /api/gpio/{pin}/config - Configure GPIO mapping (GAP-11)
 * ============================================================================ */

esp_err_t api_handler_gpio_config_post(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  int pin = api_extract_id_from_uri(req, "/api/gpio/");
  if (pin < 0 || (pin > 39 && (pin < 101 || pin > 108) && (pin < 201 || pin > 208))) {
    return api_send_error(req, 400, "Invalid GPIO pin (must be 0-39 or virtual 101-108/201-208)");
  }

  // Check if URI ends with /config
  const char *uri = req->uri;
  if (strstr(uri, "/config") == NULL) {
    // Not a config request, this shouldn't happen due to routing
    return api_send_error(req, 400, "Use /api/gpio/{pin}/config");
  }

  char content[256];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    return api_send_error(req, 400, "Failed to read request body");
  }
  content[ret] = '\0';

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, content);
  if (error) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  // Validate required field: direction
  if (!doc.containsKey("direction")) {
    return api_send_error(req, 400, "Missing 'direction' field (input/output)");
  }

  const char *dir = doc["direction"].as<const char*>();
  if (!dir || (strcmp(dir, "input") != 0 && strcmp(dir, "output") != 0)) {
    return api_send_error(req, 400, "direction must be 'input' or 'output'");
  }

  bool is_input = (strcmp(dir, "input") == 0);

  // Get register/coil address
  uint16_t reg_addr = 0xFFFF;
  if (is_input) {
    // Input mode: needs a discrete input index or register
    if (doc.containsKey("register")) {
      reg_addr = doc["register"].as<uint16_t>();
      if (reg_addr >= HOLDING_REGS_SIZE) {
        return api_send_error(req, 400, "register must be 0-255");
      }
    } else {
      return api_send_error(req, 400, "Input mode requires 'register' field");
    }
  } else {
    // Output mode: needs a coil index
    if (doc.containsKey("coil")) {
      reg_addr = doc["coil"].as<uint16_t>();
      if (reg_addr >= 256) {
        return api_send_error(req, 400, "coil must be 0-255");
      }
    } else {
      return api_send_error(req, 400, "Output mode requires 'coil' field");
    }
  }

  // Find or create mapping
  VariableMapping *existing = NULL;
  for (int i = 0; i < g_persist_config.var_map_count; i++) {
    if (g_persist_config.var_maps[i].source_type == MAPPING_SOURCE_GPIO &&
        g_persist_config.var_maps[i].gpio_pin == pin) {
      existing = &g_persist_config.var_maps[i];
      break;
    }
  }

  if (!existing) {
    // Create new mapping
    if (g_persist_config.var_map_count >= 32) {
      return api_send_error(req, 500, "Maximum GPIO mappings (32) reached");
    }
    existing = &g_persist_config.var_maps[g_persist_config.var_map_count++];
    memset(existing, 0, sizeof(VariableMapping));
    existing->source_type = MAPPING_SOURCE_GPIO;
    existing->gpio_pin = pin;
    existing->associated_counter = 0xFF;
    existing->associated_timer = 0xFF;
    existing->st_program_id = 0xFF;
    existing->st_var_index = 0xFF;
    existing->input_reg = 0xFFFF;
    existing->output_reg = 0xFFFF;
  }

  // Update mapping
  existing->is_input = is_input ? 1 : 0;
  if (is_input) {
    existing->input_reg = reg_addr;
    existing->input_type = 0;  // Holding register
  } else {
    existing->output_reg = reg_addr;
    existing->output_type = 1;  // Coil
  }
  existing->word_count = 1;

  // Configure GPIO direction
  gpio_set_direction(pin, is_input ? GPIO_INPUT : GPIO_OUTPUT);

  JsonDocument resp;
  resp["status"] = 200;
  resp["pin"] = pin;
  resp["direction"] = dir;
  if (is_input) {
    resp["register"] = reg_addr;
  } else {
    resp["coil"] = reg_addr;
  }
  resp["message"] = "GPIO mapping configured";

  char buf[256];
  serializeJson(resp, buf, sizeof(buf));

  return api_send_json(req, buf);
}

/* ============================================================================
 * POST /api/logic/{id}/bind - ST Logic variable binding (GAP-13)
 * ============================================================================ */

esp_err_t api_handler_logic_bind_post(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  int id = api_extract_id_from_uri(req, "/api/logic/");
  if (id < 1 || id > ST_LOGIC_MAX_PROGRAMS) {
    return api_send_error(req, 400, "Invalid logic ID (must be 1-4)");
  }

  // Get logic state
  st_logic_engine_state_t *logic_state = st_logic_get_state();
  if (!logic_state) {
    return api_send_error(req, 500, "Logic engine not initialized");
  }

  st_logic_program_config_t *prog = st_logic_get_program(logic_state, id - 1);
  if (!prog || !prog->compiled) {
    return api_send_error(req, 400, "Program not compiled. Upload source code first.");
  }

  char content[512];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    return api_send_error(req, 400, "Failed to read request body");
  }
  content[ret] = '\0';

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, content);
  if (error) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  // Required: variable name
  if (!doc.containsKey("variable")) {
    return api_send_error(req, 400, "Missing 'variable' field");
  }
  const char *var_name = doc["variable"].as<const char*>();
  if (!var_name || strlen(var_name) == 0) {
    return api_send_error(req, 400, "Invalid variable name");
  }

  // Required: binding spec (reg:N, coil:N, or input:N)
  if (!doc.containsKey("binding")) {
    return api_send_error(req, 400, "Missing 'binding' field (e.g., 'reg:100', 'coil:10', 'input:5')");
  }
  const char *binding = doc["binding"].as<const char*>();
  if (!binding || strlen(binding) == 0) {
    return api_send_error(req, 400, "Invalid binding spec");
  }

  // Optional: direction override
  const char *direction = NULL;
  if (doc.containsKey("direction")) {
    direction = doc["direction"].as<const char*>();
  }

  // Find variable by name
  uint8_t var_index = 0xFF;
  for (uint8_t i = 0; i < prog->bytecode.var_count; i++) {
    if (strcmp(prog->bytecode.var_names[i], var_name) == 0) {
      var_index = i;
      break;
    }
  }

  if (var_index == 0xFF) {
    // Build list of available variables
    char vars_list[256] = "";
    int pos = 0;
    for (uint8_t i = 0; i < prog->bytecode.var_count && pos < 250; i++) {
      if (i > 0) pos += snprintf(vars_list + pos, sizeof(vars_list) - pos, ", ");
      pos += snprintf(vars_list + pos, sizeof(vars_list) - pos, "%s", prog->bytecode.var_names[i]);
    }
    char errmsg[384];
    snprintf(errmsg, sizeof(errmsg), "Variable '%s' not found. Available: %s", var_name, vars_list);
    return api_send_error(req, 404, errmsg);
  }

  // FEAT-005: STRING kan ikke bindes til et Modbus-register/coil — der
  // findes ingen meningsfuld 1-2-register-mapping for en variabel-laengde
  // tekststreng, og at tillade det ville lade en registerskrivning
  // overskrive variablens str_ref (en intern reference, IKKE en vaerdi) med
  // vilkaarlige bits — en reel hukommelseskorruptionsrisiko, ikke kun en
  // "meningsloes vaerdi"-ulempe.
  if (prog->bytecode.var_types[var_index] == ST_TYPE_STRING) {
    return api_send_error(req, 400, "STRING variables cannot be bound to Modbus registers/coils");
  }

  // FEAT-010: HIGH-priority programs run on their own independent Core-0
  // task, decoupled from the main loop's gpio_mapping read-before/write-
  // after cadence — a binding would read/write at unpredictable times
  // relative to that synchronization (a timing-correctness gap, not just a
  // memory-safety one). See BUGS_INDEX.md FEAT-010.
  if (prog->priority == ST_LOGIC_PRIORITY_HIGH) {
    return api_send_error(req, 400, "HIGH-priority programs cannot use Modbus/GPIO bindings");
  }

  // Parse binding spec
  uint16_t register_addr = 0;
  uint8_t input_type = 0;  // 0=HR, 1=DI, 2=Coil
  uint8_t output_type = 0; // 0=HR, 1=Coil
  const char *default_dir = "output";

  if (strncmp(binding, "reg:", 4) == 0) {
    register_addr = atoi(binding + 4);
    input_type = 0;  // HR
    output_type = 0; // HR
    default_dir = "output";
  } else if (strncmp(binding, "coil:", 5) == 0) {
    register_addr = atoi(binding + 5);
    input_type = 2;  // Coil for input
    output_type = 1; // Coil
    default_dir = "output";
  } else if (strncmp(binding, "input:", 6) == 0 || strncmp(binding, "input-dis:", 10) == 0) {
    register_addr = (strncmp(binding, "input-dis:", 10) == 0) ? atoi(binding + 10) : atoi(binding + 6);
    input_type = 1;  // DI
    output_type = 0;
    default_dir = "input";
  } else {
    return api_send_error(req, 400, "Invalid binding (use 'reg:N', 'coil:N', or 'input:N')");
  }

  // Validate register range
  if (register_addr >= HOLDING_REGS_SIZE) {
    return api_send_error(req, 400, "Register address out of range (0-255)");
  }

  // Use direction override or default
  if (!direction) direction = default_dir;
  if (strcmp(direction, "input") != 0 && strcmp(direction, "output") != 0 && strcmp(direction, "both") != 0) {
    return api_send_error(req, 400, "direction must be 'input', 'output', or 'both'");
  }

  bool is_input = (strcmp(direction, "input") == 0 || strcmp(direction, "both") == 0);
  bool is_output = (strcmp(direction, "output") == 0 || strcmp(direction, "both") == 0);

  // Delete existing bindings for this variable
  for (int i = 0; i < g_persist_config.var_map_count; i++) {
    VariableMapping *m = &g_persist_config.var_maps[i];
    if (m->source_type == MAPPING_SOURCE_ST_VAR &&
        m->st_program_id == (id - 1) &&
        m->st_var_index == var_index) {
      // Shift down
      for (int j = i; j < g_persist_config.var_map_count - 1; j++) {
        memcpy(&g_persist_config.var_maps[j], &g_persist_config.var_maps[j + 1], sizeof(VariableMapping));
      }
      g_persist_config.var_map_count--;
      i--;
    }
  }

  // Create new binding(s)
  int created = 0;
  if (is_input) {
    if (g_persist_config.var_map_count >= 32) {
      return api_send_error(req, 500, "Maximum variable mappings (32) reached");
    }
    VariableMapping *m = &g_persist_config.var_maps[g_persist_config.var_map_count++];
    memset(m, 0, sizeof(VariableMapping));
    m->source_type = MAPPING_SOURCE_ST_VAR;
    m->st_program_id = id - 1;
    m->st_var_index = var_index;
    m->is_input = 1;
    m->input_type = input_type;
    m->input_reg = register_addr;
    m->output_reg = 0xFFFF;
    m->gpio_pin = 0xFF;
    m->associated_counter = 0xFF;
    m->associated_timer = 0xFF;
    m->word_count = 1;
    // Check variable type for 32-bit
    if (prog->bytecode.var_types[var_index] == ST_TYPE_DINT ||
        prog->bytecode.var_types[var_index] == ST_TYPE_DWORD ||
        prog->bytecode.var_types[var_index] == ST_TYPE_REAL) {
      m->word_count = 2;
    }
    created++;
  }
  if (is_output) {
    if (g_persist_config.var_map_count >= 32) {
      return api_send_error(req, 500, "Maximum variable mappings (32) reached");
    }
    VariableMapping *m = &g_persist_config.var_maps[g_persist_config.var_map_count++];
    memset(m, 0, sizeof(VariableMapping));
    m->source_type = MAPPING_SOURCE_ST_VAR;
    m->st_program_id = id - 1;
    m->st_var_index = var_index;
    m->is_input = 0;
    m->output_type = output_type;
    m->output_reg = register_addr;
    m->input_reg = 0xFFFF;
    m->gpio_pin = 0xFF;
    m->associated_counter = 0xFF;
    m->associated_timer = 0xFF;
    m->word_count = 1;
    if (prog->bytecode.var_types[var_index] == ST_TYPE_DINT ||
        prog->bytecode.var_types[var_index] == ST_TYPE_DWORD ||
        prog->bytecode.var_types[var_index] == ST_TYPE_REAL) {
      m->word_count = 2;
    }
    created++;
  }

  // Update binding count cache
  st_logic_update_binding_counts(logic_state);

  JsonDocument resp;
  resp["status"] = 200;
  resp["program"] = id;
  resp["variable"] = var_name;
  resp["binding"] = binding;
  resp["direction"] = direction;
  resp["mappings_created"] = created;
  resp["message"] = "Variable binding created";

  char *buf = (char *)malloc(512);
  if (!buf) {
    return api_send_error(req, 500, "Out of memory");
  }
  serializeJson(resp, buf, 512);

  esp_err_t result = api_send_json(req, buf);
  free(buf);
  return result;
}

/* ============================================================================
 * POST /api/cli - Execute CLI command via web (FEAT-030)
 *
 * Captures CLI output into a buffer and returns it as JSON.
 * Uses a temporary Console implementation that writes to a string buffer.
 * ============================================================================ */

// Buffer console for capturing CLI output
typedef struct {
  char *buf;
  size_t capacity;
  size_t pos;
} CliBufferCtx;

static int cli_buf_write_str(void *ctx, const char *str) {
  CliBufferCtx *c = (CliBufferCtx *)ctx;
  size_t len = strlen(str);
  if (c->pos + len >= c->capacity - 1) len = c->capacity - c->pos - 1;
  if (len > 0) { memcpy(c->buf + c->pos, str, len); c->pos += len; }
  c->buf[c->pos] = '\0';
  return (int)len;
}

static int cli_buf_write_line(void *ctx, const char *str) {
  int n = cli_buf_write_str(ctx, str);
  CliBufferCtx *c = (CliBufferCtx *)ctx;
  if (c->pos + 1 < c->capacity) { c->buf[c->pos++] = '\n'; c->buf[c->pos] = '\0'; }
  return n + 1;
}

static int cli_buf_write_char(void *ctx, char ch) {
  CliBufferCtx *c = (CliBufferCtx *)ctx;
  if (c->pos + 1 < c->capacity) { c->buf[c->pos++] = ch; c->buf[c->pos] = '\0'; }
  return 1;
}

static int cli_buf_flush(void *ctx) { return 0; }
static int cli_buf_connected(void *ctx) { return 1; }
static int cli_buf_has_input(void *ctx) { return 0; }
static int cli_buf_read_char(void *ctx, char *out) { return 0; }

/* ============================================================================
 * GET /api/user/me - Current authenticated user info (RBAC)
 * POST /api/login, /api/logout (BUG-353, session tokens) - see below
 * ============================================================================ */

// BUG-353: shared by api_handler_user_me and api_handler_login so the two
// don't duplicate the uid-to-JSON branching. `token`, when non-NULL, adds a
// "token" field (only api_handler_login passes one).
static void build_user_info_json(int uid, const char *token, char *buf, size_t buf_len)
{
  char token_field[40] = "";
  if (token) {
    snprintf(token_field, sizeof(token_field), ",\"token\":\"%s\"", token);
  }

  if (uid < 0) {
    // Not authenticated
    snprintf(buf, buf_len,
      "{\"authenticated\":false,\"username\":null,\"roles\":null,\"privilege\":null}");
  } else if (uid == 99) {
    // Virtual admin (legacy or no-auth)
    snprintf(buf, buf_len,
      "{\"authenticated\":true,\"username\":\"admin\",\"roles\":\"all\",\"privilege\":\"read/write\",\"mode\":\"legacy\"%s}",
      token_field);
  } else {
    // RBAC user
    const RbacUser *u = rbac_get_user(uid);
    if (u) {
      char role_str[40];
      rbac_roles_to_str(u->roles, role_str, sizeof(role_str));
      const char *priv_str = (u->privilege == PRIV_RW) ? "read/write" :
                             (u->privilege == PRIV_WRITE) ? "write" : "read";
      snprintf(buf, buf_len,
        "{\"authenticated\":true,\"username\":\"%s\",\"roles\":\"%s\",\"privilege\":\"%s\",\"mode\":\"rbac\",\"index\":%d%s}",
        u->username, role_str, priv_str, uid, token_field);
    } else {
      snprintf(buf, buf_len,
        "{\"authenticated\":true,\"username\":\"unknown\",\"roles\":\"all\",\"privilege\":\"read/write\",\"mode\":\"rbac\"%s}",
        token_field);
    }
  }
}

esp_err_t api_handler_user_me(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_API_ENABLED(req);
  if (!http_rate_limit_check(req)) {
    return api_send_error(req, 429, "Too many requests");
  }

  int uid = http_server_auth_user(req);
  char buf[256];
  build_user_info_json(uid, NULL, buf, sizeof(buf));
  return api_send_json(req, buf);
}

/**
 * POST /api/login — verify credentials via the SAME Basic Auth header
 * parsing already used everywhere else (http_server_auth_user() ->
 * rbac_check_http()), then issue a session token so the client can stop
 * resending username:password on every subsequent request. Basic Auth
 * itself keeps working unchanged for anything that doesn't call this.
 */
esp_err_t api_handler_login(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_API_ENABLED(req);
  if (!http_rate_limit_check(req)) {
    return api_send_error(req, 429, "Too many requests");
  }

  int uid = http_server_auth_user(req);
  if (uid < 0) {
    // api_send_error(401) already does everything a failed login needs:
    // decodes the ATTEMPTED username straight from the Authorization header
    // (http_get_client_info() below only works POST-success, so duplicating
    // that here would just log an empty username), records it for the
    // alarm system, and adds a "Login fejlede (401)" system_log event.
    // See api_send_error()'s status==401 branch (api_handlers.cpp).
    return api_send_error(req, 401, "Authentication required");
  }

  const char *token = rbac_session_token_issue(uid);
  if (!token) {
    return api_send_error(req, 500, "Could not issue session token");
  }

  // BUG-353: login er nu en diskret, sjaelden handling (ikke et per-request
  // Basic-Auth-tjek laengere) — FEAT-086s oprindelige begrundelse for IKKE
  // at logge login-succes ("stateless Basic Auth ville flode loggen")
  // gaelder ikke laengere for selve login-KALDET. Log det (kun succes-stien
  // — api_send_error() daekker allerede fejl-stien, se ovenfor).
  {
    char ip[16], user[24];
    http_get_client_info(req, ip, sizeof(ip), user, sizeof(user));
    system_log_add_event((uint8_t)SYSLOG_SRC_REST, user, ip, "Login lykkedes");
  }

  char buf[300];
  build_user_info_json(uid, token, buf, sizeof(buf));

  http_server_stat_success();
  return api_send_json(req, buf);
}

/**
 * POST /api/logout — revoke the session token from this request's Bearer
 * header, if any. Always reports success: logging out should never
 * visibly fail, regardless of whether the token was valid.
 */
esp_err_t api_handler_logout(httpd_req_t *req)
{
  http_server_stat_request();

  char auth_buf[256] = {0};
  if (httpd_req_get_hdr_value_str(req, "Authorization", auth_buf, sizeof(auth_buf)) == ESP_OK &&
      strncmp(auth_buf, "Bearer ", 7) == 0) {
    rbac_session_token_revoke(auth_buf + 7);
  }

  http_server_stat_success();
  return api_send_json(req, "{\"status\":\"ok\"}");
}

esp_err_t api_handler_cli_exec(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_ROLE(req, ROLE_CLI);

  // Also require write privilege (same as CHECK_AUTH_WRITE but after role check)
  {
    int _uid = http_server_auth_user(req);
    if (!rbac_has_write(_uid)) {
      return api_send_error(req, 403, "Write privilege required");
    }
  }

  char content[512];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    return api_send_error(req, 400, "Failed to read request body");
  }
  content[ret] = '\0';

  JsonDocument doc;
  if (deserializeJson(doc, content)) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  if (!doc.containsKey("command")) {
    return api_send_error(req, 400, "Missing 'command' field");
  }

  const char *cmd_str = doc["command"].as<const char*>();
  if (!cmd_str || strlen(cmd_str) == 0 || strlen(cmd_str) > 256) {
    return api_send_error(req, 400, "Invalid command (empty or too long)");
  }

  // Block dangerous commands from web
  if (strncmp(cmd_str, "reboot", 6) == 0 || strncmp(cmd_str, "defaults", 8) == 0) {
    return api_send_error(req, 403, "Command not allowed via web CLI (use dedicated API)");
  }

  /* BUG-347: langvarige kommandoer maa IKKE koere via web-CLI.
   *
   * Denne handler eksekverer kommandoen SYNKRONT nedenfor
   * (cli_shell_execute_command) — altsaa paa httpd's EGEN task. ESP-IDF's
   * httpd behandler requests én ad gangen, saa saa laenge kommandoen koerer,
   * kan webserveren ikke svare paa NOGET: ikke dashboardets polling, ikke SSE,
   * ikke engang en frisk sideindlaesning. En `mb scan 1 247` blokerer dermed
   * hele web-UI'et i flere minutter.
   *
   * Det var den faktiske aarsag til "dashboard/GUI dødt under mb scan", som
   * BUG-336/336b/336c/341/342 alle forsoegte at loese det forkerte sted
   * (yield-granularitet, core-pinning, Wi-Fi-genopkobling). Bekraeftet af
   * brugeren: koert fra telnet er der intet problem overhovedet — dashboardet
   * opdaterer og kan betjenes imens — mens praecis samme kommando fra
   * GUI'ens CLI fryser alt.
   *
   * Scanningen afvises derfor her, med besked om hvor den skal koeres. */
  {
    // Case-insensitiv "starter med mb ... scan"-test (tillader flere mellemrum)
    char probe[24];
    size_t n = 0;
    for (const char *p = cmd_str; *p && n < sizeof(probe) - 1; p++) {
      probe[n++] = (char)tolower((unsigned char)*p);
    }
    probe[n] = '\0';

    if (strncmp(probe, "mb", 2) == 0 && strstr(probe, "scan") != NULL) {
      return api_send_error(req, 409,
        "'mb scan' kan ikke koeres fra web-CLI: den ville blokere hele "
        "webserveren indtil scanningen var faerdig (httpd behandler én "
        "request ad gangen). Koer den fra telnet eller seriel konsol "
        "i stedet — dashboardet forbliver tilgaengeligt imens. Se BUG-347.");
    }
  }

  // Allocate output buffer (12KB max — show config can be 6-8KB)
  const size_t OUT_SIZE = 12288;
  char *out_buf = (char *)malloc(OUT_SIZE);
  if (!out_buf) {
    return api_send_error(req, 500, "Out of memory");
  }
  out_buf[0] = '\0';

  CliBufferCtx buf_ctx = { out_buf, OUT_SIZE, 0 };

  Console buf_console;
  memset(&buf_console, 0, sizeof(Console));
  buf_console.context = &buf_ctx;
  buf_console.echo_enabled = 0;
  buf_console.close_requested = 0;
  buf_console.write_str = cli_buf_write_str;
  buf_console.write_line = cli_buf_write_line;
  buf_console.write_char = cli_buf_write_char;
  buf_console.flush = cli_buf_flush;
  buf_console.is_connected = cli_buf_connected;
  buf_console.has_input = cli_buf_has_input;
  buf_console.read_char = cli_buf_read_char;

  // Make a mutable copy of the command (cli_parser modifies input)
  char cmd_copy[260];
  strncpy(cmd_copy, cmd_str, sizeof(cmd_copy) - 1);
  cmd_copy[sizeof(cmd_copy) - 1] = '\0';

  // Execute command on buffer console
  cli_shell_execute_command(&buf_console, cmd_copy);

  // Build JSON response — escape output for JSON safety
  const size_t RESP_SIZE = OUT_SIZE * 2;
  char *resp_buf = (char *)malloc(RESP_SIZE);
  if (!resp_buf) {
    free(out_buf);
    return api_send_error(req, 500, "Out of memory");
  }

  JsonDocument resp;
  resp["status"] = 200;
  resp["command"] = cmd_str;
  resp["output"] = out_buf;

  serializeJson(resp, resp_buf, RESP_SIZE);

  esp_err_t result = api_send_json(req, resp_buf);
  free(out_buf);
  free(resp_buf);
  return result;
}

/* ============================================================================
 * GET /api/bindings - List all variable-to-register bindings (FEAT-030)
 * ============================================================================ */

esp_err_t api_handler_bindings_list(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  st_logic_engine_state_t *st = st_logic_get_state();

  JsonDocument doc;
  doc["status"] = 200;
  doc["count"] = g_persist_config.var_map_count;

  JsonArray bindings = doc["bindings"].to<JsonArray>();

  for (int i = 0; i < g_persist_config.var_map_count; i++) {
    const VariableMapping *m = &g_persist_config.var_maps[i];
    if (m->source_type != MAPPING_SOURCE_ST_VAR) continue;

    JsonObject b = bindings.add<JsonObject>();
    b["index"] = i;  // Global var_maps index for DELETE
    b["program"] = m->st_program_id + 1;
    b["var_index"] = m->st_var_index;

    // Get variable name from compiled program
    if (st && m->st_program_id < ST_LOGIC_MAX_PROGRAMS) {
      st_logic_program_config_t *prog = &st->programs[m->st_program_id];
      if (prog->compiled && m->st_var_index < prog->bytecode.var_count) {
        b["name"] = prog->bytecode.var_names[m->st_var_index];
        // Type
        const char *type_str = "INT";
        switch (prog->bytecode.var_types[m->st_var_index]) {
          case ST_TYPE_BOOL: type_str = "BOOL"; break;
          case ST_TYPE_DINT: type_str = "DINT"; break;
          case ST_TYPE_REAL: type_str = "REAL"; break;
          case ST_TYPE_TIME: type_str = "TIME"; break;
          case ST_TYPE_STRING: type_str = "STRING"; break;  // FEAT-005 (not bindable, see binding-creation validation)
          default: break;
        }
        b["type"] = type_str;
      }
    }

    b["direction"] = m->is_input ? "input" : "output";
    b["word_count"] = m->word_count;

    if (m->is_input) {
      b["register_type"] = (m->input_type == 0) ? "HR" : "DI";
      b["register_addr"] = m->input_reg;
    } else {
      b["register_type"] = (m->output_type == 0) ? "HR" : "Coil";
      b["register_addr"] = m->output_reg;
    }
  }

  const size_t BUF_SIZE = 2048;
  char *buf = (char *)malloc(BUF_SIZE);
  if (!buf) return api_send_error(req, 500, "Out of memory");
  serializeJson(doc, buf, BUF_SIZE);
  esp_err_t result = api_send_json(req, buf);
  free(buf);
  return result;
}

/* ============================================================================
 * DELETE /api/bindings/{index} - Remove a specific binding (FEAT-030)
 * ============================================================================ */

esp_err_t api_handler_bindings_delete(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  int idx = api_extract_id_from_uri(req, "/api/bindings/");
  if (idx < 0 || idx >= g_persist_config.var_map_count) {
    return api_send_error(req, 400, "Invalid binding index");
  }

  // Shift remaining entries down
  for (int j = idx; j < g_persist_config.var_map_count - 1; j++) {
    memcpy(&g_persist_config.var_maps[j], &g_persist_config.var_maps[j + 1], sizeof(VariableMapping));
  }
  g_persist_config.var_map_count--;

  // Update binding counts
  st_logic_engine_state_t *st = st_logic_get_state();
  if (st) st_logic_update_binding_counts(st);

  char resp[128];
  snprintf(resp, sizeof(resp), "{\"status\":200,\"message\":\"Binding %d deleted\"}", idx);
  return api_send_json(req, resp);
}

/* ============================================================================
 * POST /api/logic/settings - ST Logic engine settings (GAP-26)
 * ============================================================================ */

esp_err_t api_handler_logic_settings_post(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  char content[256];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    return api_send_error(req, 400, "Failed to read request body");
  }
  content[ret] = '\0';

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, content);
  if (error) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  if (doc.containsKey("interval_ms")) {
    uint32_t interval = doc["interval_ms"].as<uint32_t>();
    if (interval < 1 || interval > 60000) {
      return api_send_error(req, 400, "interval_ms must be 1-60000");
    }
    g_persist_config.st_logic_interval_ms = interval;

    // FEAT-010: cascades to every NORMAL-priority program's own interval_ms
    // (HIGH programs are scheduled independently, untouched by this).
    st_logic_engine_state_t *state = st_logic_get_state();
    if (state) {
      st_logic_set_global_interval(state, interval);
    }
  }

  JsonDocument resp;
  resp["status"] = 200;
  resp["interval_ms"] = g_persist_config.st_logic_interval_ms;
  resp["message"] = "Logic settings updated";

  char buf2[256];
  serializeJson(resp, buf2, sizeof(buf2));

  return api_send_json(req, buf2);
}

/* ============================================================================
 * GET /api/modules - Module flags (GAP-28)
 * ============================================================================ */

esp_err_t api_handler_modules_get(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  JsonDocument doc;
  doc["counters"] = (g_persist_config.module_flags & MODULE_FLAG_COUNTERS_DISABLED) ? false : true;
  doc["timers"] = (g_persist_config.module_flags & MODULE_FLAG_TIMERS_DISABLED) ? false : true;
  doc["st_logic"] = (g_persist_config.module_flags & MODULE_FLAG_ST_LOGIC_DISABLED) ? false : true;

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));

  return api_send_json(req, buf);
}

/* ============================================================================
 * POST /api/modules - Set module flags (GAP-28)
 * ============================================================================ */

esp_err_t api_handler_modules_post(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  char content[256];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    return api_send_error(req, 400, "Failed to read request body");
  }
  content[ret] = '\0';

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, content);
  if (error) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  uint8_t flags = g_persist_config.module_flags;

  if (doc.containsKey("counters")) {
    if (doc["counters"].as<bool>()) {
      flags &= ~MODULE_FLAG_COUNTERS_DISABLED;
    } else {
      flags |= MODULE_FLAG_COUNTERS_DISABLED;
    }
  }
  if (doc.containsKey("timers")) {
    if (doc["timers"].as<bool>()) {
      flags &= ~MODULE_FLAG_TIMERS_DISABLED;
    } else {
      flags |= MODULE_FLAG_TIMERS_DISABLED;
    }
  }
  if (doc.containsKey("st_logic")) {
    if (doc["st_logic"].as<bool>()) {
      flags &= ~MODULE_FLAG_ST_LOGIC_DISABLED;
    } else {
      flags |= MODULE_FLAG_ST_LOGIC_DISABLED;
    }
  }

  g_persist_config.module_flags = flags;

  // BUG-362: module_flags alene styrer INTET ved kortsigtet — den faktiske
  // eksekverings-loekke (st_logic_engine_loop()) tjekker den separate,
  // RUNTIME st_logic_get_state()->enabled, som ellers kun blev sat ved boot
  // (config_apply.cpp). Uden denne synkronisering havde et POST her ingen
  // maalelig effekt foer en reboot — brugeren saa "deaktiveret" i UI'et,
  // mens motoren fortsatte uaendret.
  if (doc.containsKey("st_logic")) {
    st_logic_engine_state_t *st_state = st_logic_get_state();
    if (st_state) {
      st_state->enabled = (flags & MODULE_FLAG_ST_LOGIC_DISABLED) ? 0 : 1;
    }
  }

  JsonDocument resp;
  resp["status"] = 200;
  resp["counters"] = (flags & MODULE_FLAG_COUNTERS_DISABLED) ? false : true;
  resp["timers"] = (flags & MODULE_FLAG_TIMERS_DISABLED) ? false : true;
  resp["st_logic"] = (flags & MODULE_FLAG_ST_LOGIC_DISABLED) ? false : true;
  resp["message"] = "Module flags updated";

  char buf2[256];
  serializeJson(resp, buf2, sizeof(buf2));

  return api_send_json(req, buf2);
}

/* ============================================================================
 * RBAC USER MANAGEMENT ENDPOINTS — web GUI parity with the CLI-only
 * "set user" / "set rbac" / "delete user" / "show users" commands
 * (src/cli_parser.cpp). Reuses the SAME rbac_set_user()/rbac_delete_user()/
 * rbac_parse_roles()/rbac_parse_privilege()/rbac_roles_to_str() functions
 * the CLI already calls (include/rbac.h) — no new parsing/validation logic.
 *
 * SECURITY: gated behind CHECK_AUTH_WRITE (write privilege), deliberately
 * matching the CLI's OWN existing authorization model — rbac_cli_allowed()
 * already lets any user with CLI role + write privilege run
 * "set user X roles all privilege read/write" (i.e. escalate/create an
 * admin account) today. Requiring anything stricter here (e.g. an "all"
 * role check) would be a NEW, inconsistent restriction not present in the
 * CLI path, so this mirrors CLI parity rather than inventing a second model.
 * Documented in SECURITY_INDEX.md.
 * ============================================================================ */

esp_err_t api_handler_rbac_get(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);  // write privilege required just to enumerate users — see file-header note

  JsonDocument doc;
  doc["enabled"] = g_persist_config.rbac.enabled ? true : false;
  doc["user_count"] = rbac_get_user_count();
  doc["max_users"] = RBAC_MAX_USERS;

  JsonArray users = doc["users"].to<JsonArray>();
  for (int i = 0; i < RBAC_MAX_USERS; i++) {
    const RbacUser *u = rbac_get_user(i);
    if (!u) continue;
    JsonObject uo = users.add<JsonObject>();
    uo["index"] = i;
    uo["username"] = u->username;
    char role_str[40];
    rbac_roles_to_str(u->roles, role_str, sizeof(role_str));
    uo["roles"] = role_str;
    uo["privilege"] = (u->privilege == PRIV_RW) ? "read/write" :
                       (u->privilege == PRIV_WRITE) ? "write" :
                       (u->privilege == PRIV_READ) ? "read" : "none";
    // Deliberately NO password/hash/salt field — see BUG-352, backup JSON
    // is the only place those are ever serialized, and only hex-encoded.
  }

  char buf[HTTP_JSON_DOC_SIZE];
  serializeJson(doc, buf, sizeof(buf));
  return api_send_json(req, buf);
}

esp_err_t api_handler_rbac_post(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  char content[128];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    return api_send_error(req, 400, "Failed to read request body");
  }
  content[ret] = '\0';

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, content);
  if (error) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  bool warn_no_users = false;
  if (doc.containsKey("enabled")) {
    bool want_enabled = doc["enabled"].as<bool>();
    // Mirror CLI's "set rbac enable" warning (cli_parser.cpp): still allows
    // it (matches CLI behavior exactly), just surfaces the same lockout
    // risk back to the caller instead of only printing it to a console.
    if (want_enabled && rbac_get_user_count() == 0) {
      warn_no_users = true;
    }
    g_persist_config.rbac.enabled = want_enabled ? 1 : 0;
  }

  char resp[256];
  if (warn_no_users) {
    snprintf(resp, sizeof(resp),
      "{\"status\":200,\"enabled\":true,\"warning\":\"Ingen brugere konfigureret endnu — opret mindst én admin-bruger foer du gemmer og genstarter, ellers laases adgangen ude\"}");
  } else {
    snprintf(resp, sizeof(resp), "{\"status\":200,\"enabled\":%s,\"message\":\"RBAC-status opdateret. Brug 'Gem Config' for at overleve reboot.\"}",
      g_persist_config.rbac.enabled ? "true" : "false");
  }
  return api_send_json(req, resp);
}

esp_err_t api_handler_rbac_users_post(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  char content[256];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    return api_send_error(req, 400, "Failed to read request body");
  }
  content[ret] = '\0';

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, content);
  if (error) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  const char *username = doc["username"] | "";
  const char *password = doc["password"] | "";
  const char *roles_str = doc["roles"] | "monitor";
  const char *priv_str = doc["privilege"] | "read";

  if (!username[0] || !password[0]) {
    return api_send_error(req, 400, "username and password are required");
  }

  uint8_t roles = rbac_parse_roles(roles_str);
  uint8_t priv = rbac_parse_privilege(priv_str);

  int idx = rbac_set_user(username, password, roles, priv);
  if (idx < 0) {
    return api_send_error(req, 400, "Could not save user (max users reached, or username/password too long)");
  }

  char role_str[40];
  rbac_roles_to_str(roles, role_str, sizeof(role_str));
  char resp[256];
  snprintf(resp, sizeof(resp),
    "{\"status\":200,\"index\":%d,\"roles\":\"%s\",\"message\":\"User saved. Use 'Gem Config' to persist.\"}",
    idx, role_str);
  return api_send_json(req, resp);
}

esp_err_t api_handler_rbac_user_delete(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  const char *prefix = "/api/rbac/users/";
  const char *uri = req->uri;
  if (strncmp(uri, prefix, strlen(prefix)) != 0) {
    return api_send_error(req, 400, "Invalid URI");
  }
  const char *username = uri + strlen(prefix);
  if (!username[0]) {
    return api_send_error(req, 400, "Missing username");
  }

  if (!rbac_delete_user(username)) {
    return api_send_error(req, 404, "User not found");
  }

  char buf[256];
  snprintf(buf, sizeof(buf), "{\"status\":200,\"message\":\"User '%s' deleted. Use 'Gem Config' to persist.\"}", username);
  return api_send_json(req, buf);
}

/* ============================================================================
 * BACKUP / RESTORE ENDPOINTS
 * ============================================================================ */

// BUG-352: HTTP/RBAC password fields in backup JSON are hash+salt pairs
// (raw bytes, not printable/UTF-8-safe as-is) — hex-encode for JSON transport.
static void bytes_to_hex(const uint8_t *bytes, size_t len, char *out_hex) {
  static const char hexchars[] = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) {
    out_hex[i * 2]     = hexchars[(bytes[i] >> 4) & 0x0F];
    out_hex[i * 2 + 1] = hexchars[bytes[i] & 0x0F];
  }
  out_hex[len * 2] = '\0';
}

// Returns true and fills out_bytes[expected_len] iff hex is exactly
// 2*expected_len valid hex chars.
static bool hex_to_bytes(const char *hex, uint8_t *out_bytes, size_t expected_len) {
  if (!hex || strlen(hex) != expected_len * 2) return false;
  for (size_t i = 0; i < expected_len; i++) {
    char c1 = hex[i * 2], c2 = hex[i * 2 + 1];
    int v1, v2;
    if (c1 >= '0' && c1 <= '9') v1 = c1 - '0';
    else if (c1 >= 'a' && c1 <= 'f') v1 = c1 - 'a' + 10;
    else if (c1 >= 'A' && c1 <= 'F') v1 = c1 - 'A' + 10;
    else return false;
    if (c2 >= '0' && c2 <= '9') v2 = c2 - '0';
    else if (c2 >= 'a' && c2 <= 'f') v2 = c2 - 'a' + 10;
    else if (c2 >= 'A' && c2 <= 'F') v2 = c2 - 'A' + 10;
    else return false;
    out_bytes[i] = (uint8_t)((v1 << 4) | v2);
  }
  return true;
}

esp_err_t api_handler_system_backup(httpd_req_t *req)
{
  http_server_stat_request();
  // SECURITY: backup includes WiFi/telnet passwords in cleartext (WiFi must
  // stay plaintext for the WPA2 handshake; Telnet is a separate, unhashed
  // credential system — see BUG-352). HTTP/RBAC passwords are, since
  // BUG-352, hex-encoded hash+salt pairs (password_hash/password_salt), not
  // reversible plaintext. Regardless, require write privilege (this RBAC
  // system's admin-level tier; legacy/no-RBAC mode's virtual admin still
  // passes), not just any authenticated (incl. read-only) user.
  CHECK_AUTH_WRITE(req);

  JsonDocument doc;

  // ── METADATA ──
  doc["backup_version"] = 1;
  doc["firmware_version"] = PROJECT_VERSION;
  doc["build"] = BUILD_NUMBER;
  doc["schema_version"] = g_persist_config.schema_version;
  doc["hostname"] = g_persist_config.hostname;

  // ── MODBUS MODE ──
  doc["modbus_mode"] = g_persist_config.modbus_mode;

  // ── MODBUS SLAVE ──
  JsonObject slave = doc["modbus_slave"].to<JsonObject>();
  slave["enabled"] = g_persist_config.modbus_slave.enabled ? true : false;
  slave["slave_id"] = g_persist_config.modbus_slave.slave_id;
  slave["baudrate"] = g_persist_config.modbus_slave.baudrate;
  slave["parity"] = g_persist_config.modbus_slave.parity;
  slave["stop_bits"] = g_persist_config.modbus_slave.stop_bits;
  slave["inter_frame_delay"] = g_persist_config.modbus_slave.inter_frame_delay;

  // ── MODBUS MASTER ──
  JsonObject master = doc["modbus_master"].to<JsonObject>();
  master["enabled"] = g_persist_config.modbus_master.enabled ? true : false;
  master["baudrate"] = g_persist_config.modbus_master.baudrate;
  master["parity"] = g_persist_config.modbus_master.parity;
  master["stop_bits"] = g_persist_config.modbus_master.stop_bits;
  master["timeout_ms"] = g_persist_config.modbus_master.timeout_ms;
  master["inter_frame_delay"] = g_persist_config.modbus_master.inter_frame_delay;
  master["max_requests_per_cycle"] = g_persist_config.modbus_master.max_requests_per_cycle;
  master["cache_ttl_ms"] = g_persist_config.modbus_master.cache_ttl_ms;

  // ── ANALOG OUTPUTS ──
  doc["ao1_mode"] = g_persist_config.ao1_mode;
  doc["ao2_mode"] = g_persist_config.ao2_mode;

  // ── UART SELECTION ──
  doc["modbus_slave_uart"] = g_persist_config.modbus_slave_uart;
  doc["modbus_master_uart"] = g_persist_config.modbus_master_uart;

  // ── UART PIN CONFIG ──
  doc["uart1_tx_pin"] = g_persist_config.uart1_tx_pin;
  doc["uart1_rx_pin"] = g_persist_config.uart1_rx_pin;
  doc["uart1_dir_pin"] = g_persist_config.uart1_dir_pin;
  doc["uart2_tx_pin"] = g_persist_config.uart2_tx_pin;
  doc["uart2_rx_pin"] = g_persist_config.uart2_rx_pin;
  doc["uart2_dir_pin"] = g_persist_config.uart2_dir_pin;

  // ── NETWORK ──
  JsonObject network = doc["network"].to<JsonObject>();
  network["enabled"] = g_persist_config.network.enabled ? true : false;
  network["ssid"] = g_persist_config.network.ssid;
  network["password"] = g_persist_config.network.password;
  network["dhcp"] = g_persist_config.network.dhcp_enabled ? true : false;
  // IP addresses as human-readable dotted strings (ESP32 stores little-endian uint32_t)
  {
    char ip_str[16];
    uint32_t ip;

    ip = g_persist_config.network.static_ip;
    snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
    network["static_ip"] = ip_str;

    ip = g_persist_config.network.static_gateway;
    snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
    network["static_gateway"] = ip_str;

    ip = g_persist_config.network.static_netmask;
    snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
    network["static_netmask"] = ip_str;

    ip = g_persist_config.network.static_dns;
    snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
    network["static_dns"] = ip_str;
  }
  network["wifi_power_save"] = g_persist_config.network.wifi_power_save ? true : false;

  // ── TELNET ──
  JsonObject telnet = doc["telnet"].to<JsonObject>();
  telnet["enabled"] = g_persist_config.network.telnet_enabled ? true : false;
  telnet["port"] = g_persist_config.network.telnet_port;
  telnet["username"] = g_persist_config.network.telnet_username;
  telnet["password"] = g_persist_config.network.telnet_password;

  // ── HTTP ──
  JsonObject http = doc["http"].to<JsonObject>();
  http["enabled"] = g_persist_config.network.http.enabled ? true : false;
  http["port"] = g_persist_config.network.http.port;
  http["tls_enabled"] = g_persist_config.network.http.tls_enabled ? true : false;
  http["https_port"] = g_persist_config.https_port;  // BUG-350: dedikeret, ikke samme som "port"
  http["api_enabled"] = g_persist_config.network.http.api_enabled ? true : false;
  http["auth_enabled"] = g_persist_config.network.http.auth_enabled ? true : false;
  http["username"] = g_persist_config.network.http.username;
  {
    // BUG-352: password[] holder en raw 32-byte SHA-256-hash, ikke en
    // null-termineret streng — hex-encode i stedet for at tildele den
    // direkte (som ville fejle paa manglende/forkert null-terminering).
    char hash_hex[65], salt_hex[33];
    bytes_to_hex((const uint8_t *)g_persist_config.network.http.password, 32, hash_hex);
    bytes_to_hex(g_persist_config.http_legacy_salt, 16, salt_hex);
    http["password_hash"] = hash_hex;
    http["password_salt"] = salt_hex;
  }
  http["priority"] = g_persist_config.network.http.priority;

  // ── SSE ──
  JsonObject sse = doc["sse"].to<JsonObject>();
  sse["enabled"] = g_persist_config.network.http.sse_enabled ? true : false;
  sse["port"] = g_persist_config.network.http.sse_port;
  sse["max_clients"] = g_persist_config.network.http.sse_max_clients;
  sse["check_interval_ms"] = g_persist_config.network.http.sse_check_interval_ms;
  sse["heartbeat_ms"] = g_persist_config.network.http.sse_heartbeat_ms;

  // ── NTP ──
  JsonObject ntp = doc["ntp"].to<JsonObject>();
  ntp["enabled"] = g_persist_config.ntp.enabled ? true : false;
  ntp["server"] = g_persist_config.ntp.server;
  ntp["timezone"] = g_persist_config.ntp.timezone;
  ntp["sync_interval_min"] = g_persist_config.ntp.sync_interval_min;

  // ── MISC ──
  doc["remote_echo"] = g_persist_config.remote_echo ? true : false;
  doc["gpio2_user_mode"] = g_persist_config.gpio2_user_mode ? true : false;
  doc["st_logic_interval_ms"] = g_persist_config.st_logic_interval_ms;
  doc["module_flags"] = g_persist_config.module_flags;

  // ── COUNTERS ──
  JsonArray counters = doc["counters"].to<JsonArray>();
  for (int i = 0; i < COUNTER_COUNT; i++) {
    const CounterConfig *c = &g_persist_config.counters[i];
    JsonObject co = counters.add<JsonObject>();
    co["id"] = i;
    co["enabled"] = c->enabled ? true : false;
    co["mode_enable"] = (uint8_t)c->mode_enable;
    co["edge_type"] = (uint8_t)c->edge_type;
    co["direction"] = (uint8_t)c->direction;
    co["hw_mode"] = (uint8_t)c->hw_mode;
    co["prescaler"] = c->prescaler;
    co["bit_width"] = c->bit_width;
    co["scale_factor"] = c->scale_factor;
    co["value_reg"] = c->value_reg;
    co["raw_reg"] = c->raw_reg;
    co["freq_reg"] = c->freq_reg;
    co["ctrl_reg"] = c->ctrl_reg;
    co["compare_value_reg"] = c->compare_value_reg;
    co["start_value"] = c->start_value;
    co["debounce_enabled"] = c->debounce_enabled ? true : false;
    co["debounce_ms"] = c->debounce_ms;
    co["input_dis"] = c->input_dis;
    co["interrupt_pin"] = c->interrupt_pin;
    co["hw_gpio"] = c->hw_gpio;
    co["compare_enabled"] = c->compare_enabled ? true : false;
    co["compare_mode"] = c->compare_mode;
    co["compare_value"] = c->compare_value;
    co["reset_on_read"] = c->reset_on_read;
    co["compare_source"] = c->compare_source;
  }

  // ── TIMERS ──
  JsonArray timers = doc["timers"].to<JsonArray>();
  for (int i = 0; i < TIMER_COUNT; i++) {
    const TimerConfig *t = &g_persist_config.timers[i];
    JsonObject ti = timers.add<JsonObject>();
    ti["id"] = i;
    ti["enabled"] = t->enabled ? true : false;
    ti["mode"] = (uint8_t)t->mode;
    ti["phase1_duration_ms"] = t->phase1_duration_ms;
    ti["phase2_duration_ms"] = t->phase2_duration_ms;
    ti["phase3_duration_ms"] = t->phase3_duration_ms;
    ti["phase1_output_state"] = t->phase1_output_state;
    ti["phase2_output_state"] = t->phase2_output_state;
    ti["phase3_output_state"] = t->phase3_output_state;
    ti["pulse_duration_ms"] = t->pulse_duration_ms;
    ti["trigger_level"] = t->trigger_level;
    ti["on_duration_ms"] = t->on_duration_ms;
    ti["off_duration_ms"] = t->off_duration_ms;
    ti["input_dis"] = t->input_dis;
    ti["delay_ms"] = t->delay_ms;
    ti["trigger_edge"] = t->trigger_edge;
    ti["output_coil"] = t->output_coil;
    ti["ctrl_reg"] = t->ctrl_reg;
  }

  // ── STATIC REGISTERS ──
  JsonArray static_regs = doc["static_regs"].to<JsonArray>();
  for (int i = 0; i < g_persist_config.static_reg_count && i < MAX_DYNAMIC_REGS; i++) {
    const StaticRegisterMapping *r = &g_persist_config.static_regs[i];
    JsonObject ro = static_regs.add<JsonObject>();
    ro["address"] = r->register_address;
    ro["value_type"] = r->value_type;
    ro["value_16"] = r->value_16;
    ro["value_32"] = r->value_32;
  }

  // ── DYNAMIC REGISTERS ──
  JsonArray dynamic_regs = doc["dynamic_regs"].to<JsonArray>();
  for (int i = 0; i < g_persist_config.dynamic_reg_count && i < MAX_DYNAMIC_REGS; i++) {
    const DynamicRegisterMapping *r = &g_persist_config.dynamic_regs[i];
    JsonObject ro = dynamic_regs.add<JsonObject>();
    ro["address"] = r->register_address;
    ro["source_type"] = r->source_type;
    ro["source_id"] = r->source_id;
    ro["source_function"] = r->source_function;
  }

  // ── STATIC COILS ──
  JsonArray static_coils = doc["static_coils"].to<JsonArray>();
  for (int i = 0; i < g_persist_config.static_coil_count && i < MAX_DYNAMIC_COILS; i++) {
    const StaticCoilMapping *c = &g_persist_config.static_coils[i];
    JsonObject co = static_coils.add<JsonObject>();
    co["address"] = c->coil_address;
    co["value"] = c->static_value;
  }

  // ── DYNAMIC COILS ──
  JsonArray dynamic_coils = doc["dynamic_coils"].to<JsonArray>();
  for (int i = 0; i < g_persist_config.dynamic_coil_count && i < MAX_DYNAMIC_COILS; i++) {
    const DynamicCoilMapping *c = &g_persist_config.dynamic_coils[i];
    JsonObject co = dynamic_coils.add<JsonObject>();
    co["address"] = c->coil_address;
    co["source_type"] = c->source_type;
    co["source_id"] = c->source_id;
    co["source_function"] = c->source_function;
  }

  // ── VARIABLE MAPPINGS ──
  JsonArray var_maps = doc["var_maps"].to<JsonArray>();
  for (int i = 0; i < g_persist_config.var_map_count && i < 32; i++) {
    const VariableMapping *m = &g_persist_config.var_maps[i];
    if (m->source_type == 0 && m->gpio_pin == 0 && m->input_reg == 0xFFFF && m->output_reg == 0xFFFF) continue;
    JsonObject mo = var_maps.add<JsonObject>();
    mo["source_type"] = m->source_type;
    mo["gpio_pin"] = m->gpio_pin;
    mo["associated_counter"] = m->associated_counter;
    mo["associated_timer"] = m->associated_timer;
    mo["st_program_id"] = m->st_program_id;
    mo["st_var_index"] = m->st_var_index;
    mo["is_input"] = m->is_input;
    mo["input_type"] = m->input_type;
    mo["output_type"] = m->output_type;
    mo["input_reg"] = m->input_reg;
    // BUG-011: C-feltet er omdøbt til output_reg, men JSON-nøglen holdes
    // bevidst "coil_reg" for bagudkompatibilitet med eksisterende backup-filer.
    mo["coil_reg"] = m->output_reg;
    mo["word_count"] = m->word_count;
  }

  // ── PERSIST REGS ──
  JsonObject persist = doc["persist_regs"].to<JsonObject>();
  persist["enabled"] = g_persist_config.persist_regs.enabled ? true : false;
  persist["auto_load_enabled"] = g_persist_config.persist_regs.auto_load_enabled ? true : false;
  JsonArray auto_ids = persist["auto_load_group_ids"].to<JsonArray>();
  for (int i = 0; i < 7; i++) {
    auto_ids.add(g_persist_config.persist_regs.auto_load_group_ids[i]);
  }
  uint8_t grp_count = g_persist_config.persist_regs.group_count;
  if (grp_count > PERSIST_MAX_GROUPS) grp_count = PERSIST_MAX_GROUPS;
  JsonArray groups = persist["groups"].to<JsonArray>();
  for (int i = 0; i < grp_count; i++) {
    const PersistGroup *pg = &g_persist_config.persist_regs.groups[i];
    JsonObject go = groups.add<JsonObject>();
    go["name"] = pg->name;
    go["reg_count"] = pg->reg_count;
    JsonArray addrs = go["reg_addresses"].to<JsonArray>();
    JsonArray vals = go["reg_values"].to<JsonArray>();
    for (int j = 0; j < pg->reg_count && j < PERSIST_GROUP_MAX_REGS; j++) {
      addrs.add(pg->reg_addresses[j]);
      vals.add(pg->reg_values[j]);
    }
  }

  // ── LOGIC PROGRAMS (with source code) ──
  JsonArray logic_programs = doc["logic_programs"].to<JsonArray>();
  st_logic_engine_state_t *st_state = st_logic_get_state();
  if (st_state) {
    for (int i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
      st_logic_program_config_t *p = &st_state->programs[i];
      JsonObject pr = logic_programs.add<JsonObject>();
      pr["id"] = i;
      pr["name"] = p->name;
      pr["enabled"] = p->enabled ? true : false;
      // FEAT-010/BUG-378: manglede her — restore kaldte st_logic_delete()
      // (memset af hele program-structen) uden nogensinde at saette disse
      // felter tilbage, saa ETHVERT backup/restore-cyklus stille nulstillede
      // priority til NORMAL og (langt vaerre) interval_ms til 0 — hvilket
      // faar programmet til at koere paa HVER ENESTE loop-tick i stedet for
      // det tiltaenkte interval. Opdaget under egen live-test paa 10.1.1.153
      // (Logic1, brugerens beskyttede program, koerte pludselig med
      // interval_ms:0 efter en backup+restore-rundtur — rettet manuelt med
      // det samme, se BUGS_INDEX.md BUG-378 for hele forloebet).
      pr["priority"] = (p->priority == ST_LOGIC_PRIORITY_HIGH) ? "high" : "normal";
      pr["interval_ms"] = p->interval_ms;
      const char *src = st_logic_get_source_code(st_state, i);
      if (src && p->source_size > 0) {
        // BUG-FIX: Source pool entries are NOT null-terminated (BUG-212).
        // Must create null-terminated copy for JSON serialization.
        char *src_copy = (char *)malloc(p->source_size + 1);
        if (src_copy) {
          memcpy(src_copy, src, p->source_size);
          src_copy[p->source_size] = '\0';
          pr["source"] = src_copy;
          free(src_copy);
        } else {
          pr["source"] = (const char *)nullptr;
        }
      } else {
        pr["source"] = (const char *)nullptr;
      }
    }
  }

  // ── RBAC USERS ──
  if (g_persist_config.rbac.enabled) {
    JsonObject rbac = doc["rbac"].to<JsonObject>();
    rbac["enabled"] = true;
    JsonArray users = rbac["users"].to<JsonArray>();
    for (int i = 0; i < RBAC_MAX_USERS; i++) {
      const RbacUser *u = &g_persist_config.rbac.users[i];
      if (!u->active) continue;
      JsonObject uo = users.add<JsonObject>();
      uo["username"] = u->username;
      {
        // BUG-352: u->password er en raw 32-byte SHA-256-hash — hex-encode
        // (se tilsvarende kommentar ved http["password_hash"] ovenfor).
        char hash_hex[65], salt_hex[33];
        bytes_to_hex((const uint8_t *)u->password, 32, hash_hex);
        bytes_to_hex(g_persist_config.rbac_salt[i], 16, salt_hex);
        uo["password_hash"] = hash_hex;
        uo["password_salt"] = salt_hex;
      }
      uo["roles"] = u->roles;
      uo["privilege"] = u->privilege;
    }
  }

  // Measure needed buffer size, then allocate dynamically
  size_t json_len = measureJson(doc);
  size_t buf_size = json_len + 64;  // margin for null-terminator + safety
  char *buf = (char *)malloc(buf_size);
  if (!buf) {
    return api_send_error(req, 500, "Out of memory");
  }

  serializeJson(doc, buf, buf_size);

  // Set Content-Disposition for browser download
  httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"backup.json\"");

  esp_err_t ret = api_send_json(req, buf);
  free(buf);
  return ret;
}

// Helper: parse dotted IP string "a.b.c.d" to ESP32 little-endian uint32_t.
// Also accepts raw integer for backward compatibility with old backups.
static uint32_t parse_ip_field(JsonVariant v) {
  if (v.is<const char *>()) {
    const char *s = v.as<const char *>();
    if (s) {
      uint8_t a = 0, b = 0, c = 0, d = 0;
      sscanf(s, "%hhu.%hhu.%hhu.%hhu", &a, &b, &c, &d);
      return (uint32_t)a | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24);
    }
  }
  // Fallback: raw uint32_t (backward compatible with old backup format)
  return v.as<uint32_t>();
}

esp_err_t api_handler_system_restore(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  // Read request body (up to 32KB)
  int content_len = req->content_len;
  if (content_len <= 0 || content_len > 32768) {
    return api_send_error(req, 400, "Invalid body size (max 32KB)");
  }

  char *body = (char *)malloc(content_len + 1);
  if (!body) {
    return api_send_error(req, 500, "Out of memory");
  }

  int received = 0;
  while (received < content_len) {
    int ret = httpd_req_recv(req, body + received, content_len - received);
    if (ret <= 0) {
      free(body);
      return api_send_error(req, 400, "Failed to read body");
    }
    received += ret;
  }
  body[content_len] = '\0';

  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body, content_len);
  free(body);

  if (error) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  // Validate backup version
  int backup_ver = doc["backup_version"] | 0;
  if (backup_ver != 1) {
    return api_send_error(req, 400, "Unsupported backup_version");
  }

  // ── RESTORE MODBUS MODE ──
  if (doc.containsKey("modbus_mode")) {
    g_persist_config.modbus_mode = doc["modbus_mode"].as<uint8_t>();
    if (g_persist_config.modbus_mode > MODBUS_MODE_OFF) {
      g_persist_config.modbus_mode = MODBUS_MODE_SLAVE;  // Sanitize
    }
  }

  // ── RESTORE AO MODE ──
  if (doc.containsKey("ao1_mode")) {
    g_persist_config.ao1_mode = doc["ao1_mode"].as<uint8_t>();
    if (g_persist_config.ao1_mode > AO_MODE_CURRENT) g_persist_config.ao1_mode = AO_MODE_VOLTAGE;
  }
  if (doc.containsKey("ao2_mode")) {
    g_persist_config.ao2_mode = doc["ao2_mode"].as<uint8_t>();
    if (g_persist_config.ao2_mode > AO_MODE_CURRENT) g_persist_config.ao2_mode = AO_MODE_VOLTAGE;
  }

  // ── RESTORE UART SELECTION ──
  if (doc.containsKey("modbus_slave_uart")) {
    uint8_t u = doc["modbus_slave_uart"].as<uint8_t>();
    if (u <= 2) g_persist_config.modbus_slave_uart = u;
  }
  if (doc.containsKey("modbus_master_uart")) {
    uint8_t u = doc["modbus_master_uart"].as<uint8_t>();
    if (u <= 2) g_persist_config.modbus_master_uart = u;
  }

  // ── RESTORE UART PIN CONFIG ──
  if (doc.containsKey("uart1_tx_pin"))  g_persist_config.uart1_tx_pin  = doc["uart1_tx_pin"].as<uint8_t>();
  if (doc.containsKey("uart1_rx_pin"))  g_persist_config.uart1_rx_pin  = doc["uart1_rx_pin"].as<uint8_t>();
  if (doc.containsKey("uart1_dir_pin")) g_persist_config.uart1_dir_pin = doc["uart1_dir_pin"].as<uint8_t>();
  if (doc.containsKey("uart2_tx_pin"))  g_persist_config.uart2_tx_pin  = doc["uart2_tx_pin"].as<uint8_t>();
  if (doc.containsKey("uart2_rx_pin"))  g_persist_config.uart2_rx_pin  = doc["uart2_rx_pin"].as<uint8_t>();
  if (doc.containsKey("uart2_dir_pin")) g_persist_config.uart2_dir_pin = doc["uart2_dir_pin"].as<uint8_t>();

  // ── RESTORE MODBUS SLAVE ──
  if (doc.containsKey("modbus_slave")) {
    JsonObject s = doc["modbus_slave"];
    if (s.containsKey("enabled")) g_persist_config.modbus_slave.enabled = s["enabled"].as<bool>();
    if (s.containsKey("slave_id")) g_persist_config.modbus_slave.slave_id = s["slave_id"];
    if (s.containsKey("baudrate")) g_persist_config.modbus_slave.baudrate = s["baudrate"];
    if (s.containsKey("parity")) g_persist_config.modbus_slave.parity = s["parity"];
    if (s.containsKey("stop_bits")) g_persist_config.modbus_slave.stop_bits = s["stop_bits"];
    if (s.containsKey("inter_frame_delay")) g_persist_config.modbus_slave.inter_frame_delay = s["inter_frame_delay"];
  }

  // ── RESTORE MODBUS MASTER ──
  if (doc.containsKey("modbus_master")) {
    JsonObject m = doc["modbus_master"];
    if (m.containsKey("enabled")) g_persist_config.modbus_master.enabled = m["enabled"].as<bool>();
    if (m.containsKey("baudrate")) g_persist_config.modbus_master.baudrate = m["baudrate"];
    if (m.containsKey("parity")) g_persist_config.modbus_master.parity = m["parity"];
    if (m.containsKey("stop_bits")) g_persist_config.modbus_master.stop_bits = m["stop_bits"];
    if (m.containsKey("timeout_ms")) g_persist_config.modbus_master.timeout_ms = m["timeout_ms"];
    if (m.containsKey("inter_frame_delay")) g_persist_config.modbus_master.inter_frame_delay = m["inter_frame_delay"];
    if (m.containsKey("max_requests_per_cycle")) g_persist_config.modbus_master.max_requests_per_cycle = m["max_requests_per_cycle"];
    if (m.containsKey("cache_ttl_ms")) g_persist_config.modbus_master.cache_ttl_ms = m["cache_ttl_ms"];
  }

  // ── RESTORE HOSTNAME ──
  if (doc.containsKey("hostname")) {
    strncpy(g_persist_config.hostname, doc["hostname"] | "", sizeof(g_persist_config.hostname) - 1);
    g_persist_config.hostname[sizeof(g_persist_config.hostname) - 1] = '\0';
  }

  // ── RESTORE NETWORK ──
  if (doc.containsKey("network")) {
    JsonObject n = doc["network"];
    if (n.containsKey("enabled")) g_persist_config.network.enabled = n["enabled"].as<bool>() ? 1 : 0;
    if (n.containsKey("ssid")) {
      strncpy(g_persist_config.network.ssid, n["ssid"] | "", sizeof(g_persist_config.network.ssid) - 1);
      g_persist_config.network.ssid[sizeof(g_persist_config.network.ssid) - 1] = '\0';
    }
    if (n.containsKey("password")) {
      strncpy(g_persist_config.network.password, n["password"] | "", sizeof(g_persist_config.network.password) - 1);
      g_persist_config.network.password[sizeof(g_persist_config.network.password) - 1] = '\0';
    }
    if (n.containsKey("dhcp")) g_persist_config.network.dhcp_enabled = n["dhcp"].as<bool>() ? 1 : 0;
    if (n.containsKey("static_ip")) g_persist_config.network.static_ip = parse_ip_field(n["static_ip"]);
    if (n.containsKey("static_gateway")) g_persist_config.network.static_gateway = parse_ip_field(n["static_gateway"]);
    if (n.containsKey("static_netmask")) g_persist_config.network.static_netmask = parse_ip_field(n["static_netmask"]);
    if (n.containsKey("static_dns")) g_persist_config.network.static_dns = parse_ip_field(n["static_dns"]);
    if (n.containsKey("wifi_power_save")) g_persist_config.network.wifi_power_save = n["wifi_power_save"].as<bool>() ? 1 : 0;
  }

  // ── RESTORE TELNET ──
  if (doc.containsKey("telnet")) {
    JsonObject t = doc["telnet"];
    if (t.containsKey("enabled")) g_persist_config.network.telnet_enabled = t["enabled"].as<bool>() ? 1 : 0;
    if (t.containsKey("port")) g_persist_config.network.telnet_port = t["port"];
    if (t.containsKey("username")) {
      strncpy(g_persist_config.network.telnet_username, t["username"] | "", sizeof(g_persist_config.network.telnet_username) - 1);
      g_persist_config.network.telnet_username[sizeof(g_persist_config.network.telnet_username) - 1] = '\0';
    }
    if (t.containsKey("password")) {
      strncpy(g_persist_config.network.telnet_password, t["password"] | "", sizeof(g_persist_config.network.telnet_password) - 1);
      g_persist_config.network.telnet_password[sizeof(g_persist_config.network.telnet_password) - 1] = '\0';
    }
  }

  // ── RESTORE HTTP ──
  if (doc.containsKey("http")) {
    JsonObject h = doc["http"];
    if (h.containsKey("enabled")) g_persist_config.network.http.enabled = h["enabled"].as<bool>() ? 1 : 0;
    if (h.containsKey("port")) g_persist_config.network.http.port = h["port"];
    if (h.containsKey("tls_enabled")) g_persist_config.network.http.tls_enabled = h["tls_enabled"].as<bool>() ? 1 : 0;
    if (h.containsKey("https_port")) g_persist_config.https_port = h["https_port"];  // BUG-350
    if (h.containsKey("api_enabled")) g_persist_config.network.http.api_enabled = h["api_enabled"].as<bool>() ? 1 : 0;
    if (h.containsKey("auth_enabled")) g_persist_config.network.http.auth_enabled = h["auth_enabled"].as<bool>() ? 1 : 0;
    if (h.containsKey("username")) {
      strncpy(g_persist_config.network.http.username, h["username"] | "", sizeof(g_persist_config.network.http.username) - 1);
      g_persist_config.network.http.username[sizeof(g_persist_config.network.http.username) - 1] = '\0';
    }
    // BUG-352: nyt format (hash+salt fra en backup taget EFTER hashing blev
    // indfoert) skrives raw, ingen re-hashing. Gammelt format (klartekst
    // "password" fra en aeldre backup) hashes friskt her, for bagudkompatibilitet.
    if (h.containsKey("password_hash") && h.containsKey("password_salt")) {
      uint8_t hash[32], salt[16];
      if (hex_to_bytes(h["password_hash"] | "", hash, 32) &&
          hex_to_bytes(h["password_salt"] | "", salt, 16)) {
        memcpy(g_persist_config.http_legacy_salt, salt, 16);
        memcpy(g_persist_config.network.http.password, hash, 32);
        memset(g_persist_config.network.http.password + 32, 0,
               sizeof(g_persist_config.network.http.password) - 32);
      }
    } else if (h.containsKey("password")) {
      rbac_hash_and_store_legacy_password(&g_persist_config, h["password"] | "");
    }
    if (h.containsKey("priority")) g_persist_config.network.http.priority = h["priority"];
  }

  // ── RESTORE SSE ──
  if (doc.containsKey("sse")) {
    JsonObject s = doc["sse"];
    if (s.containsKey("enabled"))           g_persist_config.network.http.sse_enabled           = s["enabled"].as<bool>() ? 1 : 0;
    if (s.containsKey("port"))              g_persist_config.network.http.sse_port               = s["port"];
    if (s.containsKey("max_clients"))       g_persist_config.network.http.sse_max_clients        = s["max_clients"];
    if (s.containsKey("check_interval_ms")) g_persist_config.network.http.sse_check_interval_ms = s["check_interval_ms"];
    if (s.containsKey("heartbeat_ms"))      g_persist_config.network.http.sse_heartbeat_ms      = s["heartbeat_ms"];
  }

  // ── RESTORE NTP ──
  if (doc.containsKey("ntp")) {
    JsonObject n = doc["ntp"];
    if (n.containsKey("enabled"))           g_persist_config.ntp.enabled = n["enabled"].as<bool>() ? 1 : 0;
    if (n.containsKey("server")) {
      strncpy(g_persist_config.ntp.server, n["server"].as<const char*>(), sizeof(g_persist_config.ntp.server) - 1);
      g_persist_config.ntp.server[sizeof(g_persist_config.ntp.server) - 1] = '\0';
    }
    if (n.containsKey("timezone")) {
      strncpy(g_persist_config.ntp.timezone, n["timezone"].as<const char*>(), sizeof(g_persist_config.ntp.timezone) - 1);
      g_persist_config.ntp.timezone[sizeof(g_persist_config.ntp.timezone) - 1] = '\0';
    }
    if (n.containsKey("sync_interval_min")) {
      uint16_t mins = n["sync_interval_min"].as<uint16_t>();
      if (mins >= 1 && mins <= 1440) g_persist_config.ntp.sync_interval_min = mins;
    }
  }

  // ── RESTORE MISC ──
  if (doc.containsKey("remote_echo")) g_persist_config.remote_echo = doc["remote_echo"];
  if (doc.containsKey("gpio2_user_mode")) g_persist_config.gpio2_user_mode = doc["gpio2_user_mode"];
  if (doc.containsKey("st_logic_interval_ms")) g_persist_config.st_logic_interval_ms = doc["st_logic_interval_ms"];
  if (doc.containsKey("module_flags")) g_persist_config.module_flags = doc["module_flags"];

  // ── RESTORE COUNTERS ──
  if (doc.containsKey("counters")) {
    JsonArray ca = doc["counters"];
    for (JsonObject co : ca) {
      int id = co["id"] | -1;
      if (id < 0 || id >= COUNTER_COUNT) continue;
      CounterConfig *c = &g_persist_config.counters[id];
      if (co.containsKey("enabled")) c->enabled = co["enabled"].as<bool>() ? 1 : 0;
      if (co.containsKey("mode_enable")) c->mode_enable = (CounterModeEnable)(uint8_t)co["mode_enable"];
      if (co.containsKey("edge_type")) c->edge_type = (CounterEdgeType)(uint8_t)co["edge_type"];
      if (co.containsKey("direction")) c->direction = (CounterDirection)(uint8_t)co["direction"];
      if (co.containsKey("hw_mode")) c->hw_mode = (CounterHWMode)(uint8_t)co["hw_mode"];
      if (co.containsKey("prescaler")) c->prescaler = co["prescaler"];
      if (co.containsKey("bit_width")) c->bit_width = co["bit_width"];
      if (co.containsKey("scale_factor")) c->scale_factor = co["scale_factor"];
      if (co.containsKey("value_reg")) c->value_reg = co["value_reg"];
      if (co.containsKey("raw_reg")) c->raw_reg = co["raw_reg"];
      if (co.containsKey("freq_reg")) c->freq_reg = co["freq_reg"];
      if (co.containsKey("ctrl_reg")) c->ctrl_reg = co["ctrl_reg"];
      if (co.containsKey("compare_value_reg")) c->compare_value_reg = co["compare_value_reg"];
      if (co.containsKey("start_value")) c->start_value = co["start_value"];
      if (co.containsKey("debounce_enabled")) c->debounce_enabled = co["debounce_enabled"].as<bool>() ? 1 : 0;
      if (co.containsKey("debounce_ms")) c->debounce_ms = co["debounce_ms"];
      if (co.containsKey("input_dis")) c->input_dis = co["input_dis"];
      if (co.containsKey("interrupt_pin")) c->interrupt_pin = co["interrupt_pin"];
      if (co.containsKey("hw_gpio")) c->hw_gpio = co["hw_gpio"];
      if (co.containsKey("compare_enabled")) c->compare_enabled = co["compare_enabled"].as<bool>() ? 1 : 0;
      if (co.containsKey("compare_mode")) c->compare_mode = co["compare_mode"];
      if (co.containsKey("compare_value")) c->compare_value = co["compare_value"];
      if (co.containsKey("reset_on_read")) c->reset_on_read = co["reset_on_read"];
      if (co.containsKey("compare_source")) c->compare_source = co["compare_source"];
    }
  }

  // ── RESTORE TIMERS ──
  if (doc.containsKey("timers")) {
    JsonArray ta = doc["timers"];
    for (JsonObject ti : ta) {
      int id = ti["id"] | -1;
      if (id < 0 || id >= TIMER_COUNT) continue;
      TimerConfig *t = &g_persist_config.timers[id];
      if (ti.containsKey("enabled")) t->enabled = ti["enabled"].as<bool>() ? 1 : 0;
      if (ti.containsKey("mode")) t->mode = (TimerMode)(uint8_t)ti["mode"];
      if (ti.containsKey("phase1_duration_ms")) t->phase1_duration_ms = ti["phase1_duration_ms"];
      if (ti.containsKey("phase2_duration_ms")) t->phase2_duration_ms = ti["phase2_duration_ms"];
      if (ti.containsKey("phase3_duration_ms")) t->phase3_duration_ms = ti["phase3_duration_ms"];
      if (ti.containsKey("phase1_output_state")) t->phase1_output_state = ti["phase1_output_state"];
      if (ti.containsKey("phase2_output_state")) t->phase2_output_state = ti["phase2_output_state"];
      if (ti.containsKey("phase3_output_state")) t->phase3_output_state = ti["phase3_output_state"];
      if (ti.containsKey("pulse_duration_ms")) t->pulse_duration_ms = ti["pulse_duration_ms"];
      if (ti.containsKey("trigger_level")) t->trigger_level = ti["trigger_level"];
      if (ti.containsKey("on_duration_ms")) t->on_duration_ms = ti["on_duration_ms"];
      if (ti.containsKey("off_duration_ms")) t->off_duration_ms = ti["off_duration_ms"];
      if (ti.containsKey("input_dis")) t->input_dis = ti["input_dis"];
      if (ti.containsKey("delay_ms")) t->delay_ms = ti["delay_ms"];
      if (ti.containsKey("trigger_edge")) t->trigger_edge = ti["trigger_edge"];
      if (ti.containsKey("output_coil")) t->output_coil = ti["output_coil"];
      if (ti.containsKey("ctrl_reg")) t->ctrl_reg = ti["ctrl_reg"];
    }
  }

  // ── RESTORE STATIC REGISTERS ──
  if (doc.containsKey("static_regs")) {
    JsonArray sra = doc["static_regs"];
    g_persist_config.static_reg_count = 0;
    for (JsonObject ro : sra) {
      if (g_persist_config.static_reg_count >= MAX_DYNAMIC_REGS) break;
      StaticRegisterMapping *r = &g_persist_config.static_regs[g_persist_config.static_reg_count];
      r->register_address = ro["address"] | 0;
      r->value_type = ro["value_type"] | 0;
      r->value_16 = ro["value_16"] | 0;
      r->value_32 = ro["value_32"] | (uint32_t)0;
      g_persist_config.static_reg_count++;
    }
  }

  // ── RESTORE DYNAMIC REGISTERS ──
  if (doc.containsKey("dynamic_regs")) {
    JsonArray dra = doc["dynamic_regs"];
    g_persist_config.dynamic_reg_count = 0;
    for (JsonObject ro : dra) {
      if (g_persist_config.dynamic_reg_count >= MAX_DYNAMIC_REGS) break;
      DynamicRegisterMapping *r = &g_persist_config.dynamic_regs[g_persist_config.dynamic_reg_count];
      r->register_address = ro["address"] | 0;
      r->source_type = ro["source_type"] | 0;
      r->source_id = ro["source_id"] | 0;
      r->source_function = ro["source_function"] | 0;
      g_persist_config.dynamic_reg_count++;
    }
  }

  // ── RESTORE STATIC COILS ──
  if (doc.containsKey("static_coils")) {
    JsonArray sca = doc["static_coils"];
    g_persist_config.static_coil_count = 0;
    for (JsonObject co : sca) {
      if (g_persist_config.static_coil_count >= MAX_DYNAMIC_COILS) break;
      StaticCoilMapping *c = &g_persist_config.static_coils[g_persist_config.static_coil_count];
      c->coil_address = co["address"] | 0;
      c->static_value = co["value"] | 0;
      g_persist_config.static_coil_count++;
    }
  }

  // ── RESTORE DYNAMIC COILS ──
  if (doc.containsKey("dynamic_coils")) {
    JsonArray dca = doc["dynamic_coils"];
    g_persist_config.dynamic_coil_count = 0;
    for (JsonObject co : dca) {
      if (g_persist_config.dynamic_coil_count >= MAX_DYNAMIC_COILS) break;
      DynamicCoilMapping *c = &g_persist_config.dynamic_coils[g_persist_config.dynamic_coil_count];
      c->coil_address = co["address"] | 0;
      c->source_type = co["source_type"] | 0;
      c->source_id = co["source_id"] | 0;
      c->source_function = co["source_function"] | 0;
      g_persist_config.dynamic_coil_count++;
    }
  }

  // NOTE: var_maps restore moved AFTER logic_programs restore.
  // st_logic_delete() clears var_map entries as side-effect,
  // so var_maps must be restored after all st_logic_delete() calls.

  // ── RESTORE PERSIST REGS ──
  if (doc.containsKey("persist_regs")) {
    JsonObject pr = doc["persist_regs"];
    if (pr.containsKey("enabled")) g_persist_config.persist_regs.enabled = pr["enabled"].as<bool>() ? 1 : 0;
    if (pr.containsKey("auto_load_enabled")) g_persist_config.persist_regs.auto_load_enabled = pr["auto_load_enabled"].as<bool>() ? 1 : 0;
    if (pr.containsKey("auto_load_group_ids")) {
      JsonArray aids = pr["auto_load_group_ids"];
      for (int i = 0; i < 7 && i < (int)aids.size(); i++) {
        g_persist_config.persist_regs.auto_load_group_ids[i] = aids[i];
      }
    }
    if (pr.containsKey("groups")) {
      JsonArray ga = pr["groups"];
      g_persist_config.persist_regs.group_count = 0;
      for (JsonObject go : ga) {
        if (g_persist_config.persist_regs.group_count >= PERSIST_MAX_GROUPS) break;
        PersistGroup *pg = &g_persist_config.persist_regs.groups[g_persist_config.persist_regs.group_count];
        memset(pg, 0, sizeof(PersistGroup));
        strncpy(pg->name, go["name"] | "", sizeof(pg->name) - 1);
        pg->name[sizeof(pg->name) - 1] = '\0';
        pg->reg_count = go["reg_count"] | 0;
        if (pg->reg_count > PERSIST_GROUP_MAX_REGS) pg->reg_count = PERSIST_GROUP_MAX_REGS;
        if (go.containsKey("reg_addresses")) {
          JsonArray addrs = go["reg_addresses"];
          for (int j = 0; j < pg->reg_count && j < (int)addrs.size(); j++) {
            pg->reg_addresses[j] = addrs[j];
          }
        }
        if (go.containsKey("reg_values")) {
          JsonArray vals = go["reg_values"];
          for (int j = 0; j < pg->reg_count && j < (int)vals.size(); j++) {
            pg->reg_values[j] = vals[j];
          }
        }
        g_persist_config.persist_regs.group_count++;
      }
    }
  }

  // ── RESTORE LOGIC PROGRAMS ──
  if (doc.containsKey("logic_programs")) {
    st_logic_engine_state_t *st = st_logic_get_state();
    if (st) {
      JsonArray lpa = doc["logic_programs"];
      for (JsonObject pr : lpa) {
        int id = pr["id"] | -1;
        if (id < 0 || id >= ST_LOGIC_MAX_PROGRAMS) continue;

        // Delete existing program
        st_logic_delete(st, id);

        // Upload source if present
        const char *src = pr["source"] | (const char *)nullptr;
        if (src && strlen(src) > 0) {
          st_logic_upload(st, id, src, strlen(src));
          st_logic_compile(st, id);
        }

        // Set name
        if (pr.containsKey("name")) {
          strncpy(st->programs[id].name, pr["name"] | "", sizeof(st->programs[id].name) - 1);
          st->programs[id].name[sizeof(st->programs[id].name) - 1] = '\0';
        }

        // Set enabled
        if (pr.containsKey("enabled")) {
          st_logic_set_enabled(st, id, pr["enabled"].as<bool>() ? 1 : 0);
        }

        // BUG-378: priority/interval_ms — se eksport-siden ovenfor for hvorfor
        // dette er kritisk (st_logic_delete() lige ovenfor nulstillede dem
        // begge, uden dette ville ETHVERT program ende med interval_ms=0
        // efter restore). Ældre backups uden disse felter falder tilbage til
        // NORMAL/10ms (samme default som et helt nyt/tomt program får i
        // st_logic_init()), ikke det farlige 0.
        char prio_err[64];
        const char *prio_str = pr["priority"] | "normal";
        uint8_t prio_val = (strcmp(prio_str, "high") == 0) ? ST_LOGIC_PRIORITY_HIGH : ST_LOGIC_PRIORITY_NORMAL;
        st_logic_set_program_priority(st, id, prio_val, prio_err, sizeof(prio_err));
        uint32_t interval_val = pr["interval_ms"] | 10;
        if (interval_val < ST_LOGIC_INTERVAL_MIN_MS) interval_val = ST_LOGIC_INTERVAL_MIN_MS;
        st_logic_set_program_interval(st, id, interval_val);
      }

      // Save ST Logic to SPIFFS
      st_logic_save_to_persist_config(&g_persist_config);
    }
  }

  // ── RESTORE VARIABLE MAPPINGS ──
  // Must be AFTER logic_programs restore because st_logic_delete()
  // clears var_map entries as side-effect (bindings + GPIO maps).
  if (doc.containsKey("var_maps")) {
    JsonArray vma = doc["var_maps"];
    g_persist_config.var_map_count = 0;
    for (JsonObject mo : vma) {
      if (g_persist_config.var_map_count >= 32) break;
      VariableMapping *m = &g_persist_config.var_maps[g_persist_config.var_map_count];
      m->source_type = mo["source_type"] | 0;
      m->gpio_pin = mo["gpio_pin"] | 0;
      m->associated_counter = mo["associated_counter"] | 0xFF;
      m->associated_timer = mo["associated_timer"] | 0xFF;
      m->st_program_id = mo["st_program_id"] | 0xFF;
      m->st_var_index = mo["st_var_index"] | 0;
      m->is_input = mo["is_input"] | 0;
      m->input_type = mo["input_type"] | 0;
      m->output_type = mo["output_type"] | 0;
      m->input_reg = mo["input_reg"] | 0xFFFF;
      // BUG-011: JSON-nøglen "coil_reg" bevares for bagudkompatibilitet med
      // gamle backup-filer, selvom C-feltet nu hedder output_reg.
      m->output_reg = mo["coil_reg"] | 0xFFFF;
      m->word_count = mo["word_count"] | 1;
      g_persist_config.var_map_count++;
    }
  }

  // ── RESTORE RBAC USERS ──
  if (doc.containsKey("rbac")) {
    JsonObject rb = doc["rbac"];
    memset(&g_persist_config.rbac, 0, sizeof(RbacConfig));
    if (rb.containsKey("enabled") && rb["enabled"].as<bool>()) {
      g_persist_config.rbac.enabled = 1;
      if (rb.containsKey("users")) {
        JsonArray ua = rb["users"];
        int idx = 0;
        for (JsonObject uo : ua) {
          if (idx >= RBAC_MAX_USERS) break;
          RbacUser *u = &g_persist_config.rbac.users[idx];
          u->active = 1;
          strncpy(u->username, uo["username"] | "", RBAC_USERNAME_MAX - 1);
          u->username[RBAC_USERNAME_MAX - 1] = '\0';
          // BUG-352: nyt format (hash+salt) skrives raw; gammelt format
          // (klartekst "password" fra en aeldre backup) hashes friskt her —
          // se tilsvarende kommentar ved RESTORE HTTP ovenfor.
          if (uo.containsKey("password_hash") && uo.containsKey("password_salt")) {
            uint8_t hash[32], salt[16];
            if (hex_to_bytes(uo["password_hash"] | "", hash, 32) &&
                hex_to_bytes(uo["password_salt"] | "", salt, 16)) {
              memcpy(g_persist_config.rbac_salt[idx], salt, 16);
              memcpy(u->password, hash, 32);
            }
          } else if (uo.containsKey("password")) {
            rbac_generate_salt(g_persist_config.rbac_salt[idx]);
            rbac_hash_password(uo["password"] | "", g_persist_config.rbac_salt[idx], (uint8_t *)u->password);
          }
          u->roles = uo["roles"] | ROLE_ALL;
          u->privilege = uo["privilege"] | PRIV_RW;
          g_persist_config.rbac.user_count++;
          idx++;
        }
      }
    }
  }

  // Save PersistConfig to NVS
  bool save_ok = config_save_to_nvs(&g_persist_config);
  if (!save_ok) {
    return api_send_error(req, 500, "Config saved partially - NVS write failed");
  }

  // Apply config
  config_apply(&g_persist_config);

  JsonDocument resp;
  resp["status"] = 200;
  resp["message"] = "Configuration restored and applied";
  resp["warning"] = "Full config replaced. Reboot recommended.";

  char respbuf[256];
  serializeJson(resp, respbuf, sizeof(respbuf));

  return api_send_json(req, respbuf);
}

/* ============================================================================
 * v6.3.0 API EXTENSIONS (FEAT-019 to FEAT-027)
 * ============================================================================ */

/* ============================================================================
 * FEAT-027: OPTIONS preflight handler for CORS
 * ============================================================================ */

esp_err_t api_handler_cors_preflight(httpd_req_t *req)
{
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Authorization, Content-Type");
  httpd_resp_set_hdr(req, "Access-Control-Max-Age", "86400");
  httpd_resp_set_status(req, "204 No Content");
  httpd_resp_sendstr(req, "");
  return ESP_OK;
}

/* ============================================================================
 * FEAT-019: GET /api/telnet — Telnet configuration + status
 * ============================================================================ */

esp_err_t api_handler_telnet_get(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  JsonDocument doc;
  doc["enabled"] = g_persist_config.network.telnet_enabled ? true : false;
  doc["port"] = g_persist_config.network.telnet_port;
  doc["username"] = g_persist_config.network.telnet_username;
  doc["auth_required"] = (strlen(g_persist_config.network.telnet_username) > 0) ? true : false;

  char buf[HTTP_JSON_DOC_SIZE];
  serializeJson(doc, buf, sizeof(buf));
  return api_send_json(req, buf);
}

/* ============================================================================
 * FEAT-019: POST /api/telnet — Configure Telnet
 * ============================================================================ */

esp_err_t api_handler_telnet_post(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  char body[512];
  int len = httpd_req_recv(req, body, sizeof(body) - 1);
  if (len <= 0) {
    return api_send_error(req, 400, "Empty request body");
  }
  body[len] = '\0';

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body);
  if (error) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  if (doc.containsKey("enabled")) {
    g_persist_config.network.telnet_enabled = doc["enabled"].as<bool>() ? 1 : 0;
  }
  if (doc.containsKey("port")) {
    uint16_t port = doc["port"].as<uint16_t>();
    if (port > 0 && port <= 65535) {
      g_persist_config.network.telnet_port = port;
    }
  }
  if (doc.containsKey("username")) {
    strncpy(g_persist_config.network.telnet_username,
            doc["username"].as<const char*>(),
            sizeof(g_persist_config.network.telnet_username) - 1);
    g_persist_config.network.telnet_username[sizeof(g_persist_config.network.telnet_username) - 1] = '\0';
  }
  if (doc.containsKey("password")) {
    strncpy(g_persist_config.network.telnet_password,
            doc["password"].as<const char*>(),
            sizeof(g_persist_config.network.telnet_password) - 1);
    g_persist_config.network.telnet_password[sizeof(g_persist_config.network.telnet_password) - 1] = '\0';
  }

  char resp[128];
  snprintf(resp, sizeof(resp), "{\"status\":200,\"message\":\"Telnet config updated. Save + reboot to apply.\"}");
  return api_send_json(req, resp);
}

/* ============================================================================
 * NTP API (v7.8.1)
 * GET  /api/ntp — Return NTP config + sync status
 * POST /api/ntp — Update NTP config
 * ============================================================================ */

esp_err_t api_handler_ntp_get(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  JsonDocument doc;
  doc["enabled"] = g_persist_config.ntp.enabled ? true : false;
  doc["server"] = g_persist_config.ntp.server;
  doc["timezone"] = g_persist_config.ntp.timezone;
  doc["sync_interval_min"] = g_persist_config.ntp.sync_interval_min;
  doc["synced"] = ntp_driver_is_synced();
  doc["sync_count"] = ntp_driver_get_sync_count();
  doc["error_count"] = ntp_driver_get_error_count();

  if (ntp_driver_is_synced()) {
    char timebuf[32];
    ntp_driver_get_time_str(timebuf, sizeof(timebuf));
    doc["local_time"] = timebuf;

    char isobuf[32];
    ntp_driver_get_iso_time(isobuf, sizeof(isobuf));
    doc["iso_time"] = isobuf;

    doc["epoch"] = (unsigned long)ntp_driver_get_epoch();
    doc["last_sync_age_ms"] = ntp_driver_get_last_sync_age_ms();
  }

  char buf[512];
  serializeJson(doc, buf, sizeof(buf));
  return api_send_json(req, buf);
}

esp_err_t api_handler_ntp_post(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  char body[512];
  int len = httpd_req_recv(req, body, sizeof(body) - 1);
  if (len <= 0) {
    return api_send_error(req, 400, "Empty request body");
  }
  body[len] = '\0';

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body);
  if (error) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  if (doc.containsKey("enabled")) {
    g_persist_config.ntp.enabled = doc["enabled"].as<bool>() ? 1 : 0;
  }
  if (doc.containsKey("server")) {
    strncpy(g_persist_config.ntp.server,
            doc["server"].as<const char*>(),
            sizeof(g_persist_config.ntp.server) - 1);
    g_persist_config.ntp.server[sizeof(g_persist_config.ntp.server) - 1] = '\0';
  }
  if (doc.containsKey("timezone")) {
    strncpy(g_persist_config.ntp.timezone,
            doc["timezone"].as<const char*>(),
            sizeof(g_persist_config.ntp.timezone) - 1);
    g_persist_config.ntp.timezone[sizeof(g_persist_config.ntp.timezone) - 1] = '\0';
  }
  if (doc.containsKey("sync_interval_min")) {
    uint16_t mins = doc["sync_interval_min"].as<uint16_t>();
    if (mins >= 1 && mins <= 1440) {
      g_persist_config.ntp.sync_interval_min = mins;
    }
  }

  // Apply immediately
  ntp_driver_reconfigure();

  char resp[128];
  snprintf(resp, sizeof(resp), "{\"status\":200,\"message\":\"NTP config updated. Save to persist.\"}");
  return api_send_json(req, resp);
}

/* ============================================================================
 * FEAT-024: GET /api/hostname
 * ============================================================================ */

esp_err_t api_handler_hostname_get(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  char buf[128];
  snprintf(buf, sizeof(buf), "{\"hostname\":\"%s\"}", g_persist_config.hostname);
  return api_send_json(req, buf);
}

/* ============================================================================
 * FEAT-024: POST /api/hostname
 * ============================================================================ */

esp_err_t api_handler_hostname_post(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  char body[256];
  int len = httpd_req_recv(req, body, sizeof(body) - 1);
  if (len <= 0) {
    return api_send_error(req, 400, "Empty request body");
  }
  body[len] = '\0';

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body);
  if (error) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  if (!doc.containsKey("hostname")) {
    return api_send_error(req, 400, "Missing 'hostname' field");
  }

  const char *hostname = doc["hostname"].as<const char*>();
  if (!hostname || strlen(hostname) == 0 || strlen(hostname) >= sizeof(g_persist_config.hostname)) {
    return api_send_error(req, 400, "Invalid hostname (1-31 chars)");
  }

  strncpy(g_persist_config.hostname, hostname, sizeof(g_persist_config.hostname) - 1);
  g_persist_config.hostname[sizeof(g_persist_config.hostname) - 1] = '\0';

  // BUG-371: hostname havde FOER ingen reel netvaerkseffekt overhovedet (kun
  // vist i telnet-banner/REST-svar) — anvendes nu straks paa netif'en. Selve
  // NAVNET er sat med det samme, men den FAKTISKE DHCP-broadcastede vaert
  // opdateres foerst ved naeste DHCP-lease/reconnect (routerens/DHCP-
  // serverens client-liste kan derfor stadig vise det gamle navn indtil da).
  // Ethernet har sit eget separate hostname-felt (/api/ethernet) — ryk kun
  // Ethernet's netif med her hvis DEN ikke selv har en override sat.
  wifi_driver_set_hostname(g_persist_config.hostname);
  if (!g_persist_config.network.ethernet.hostname[0]) {
    ethernet_driver_set_hostname(g_persist_config.hostname);
  }

  char resp[192];
  snprintf(resp, sizeof(resp),
    "{\"status\":200,\"hostname\":\"%s\",\"message\":\"Anvendt paa interfacet — reconnect/genstart for at opdatere DHCP-broadcast hostname\"}",
    g_persist_config.hostname);
  return api_send_json(req, resp);
}

/* ============================================================================
 * FEAT-108: GET /api/dashboard/layout — Get dashboard card order
 * ============================================================================ */

esp_err_t api_handler_dashboard_layout_get(httpd_req_t *req)
{
  http_server_stat_request();
  // No auth required — layout is non-sensitive UI preference
  CHECK_API_ENABLED(req);

  char buf[700];
  snprintf(buf, sizeof(buf),
    "{\"card_order\":\"%s\",\"card_tabs\":\"%s\",\"card_hidden\":\"%s\",\"card_custom\":\"%s\"}",
    g_persist_config.dashboard_card_order,
    g_persist_config.dashboard_card_tabs,
    g_persist_config.dashboard_card_hidden,
    g_persist_config.dashboard_card_custom);
  return api_send_json(req, buf);
}

/* ============================================================================
 * FEAT-108: POST /api/dashboard/layout — Set dashboard card order
 * ============================================================================ */

esp_err_t api_handler_dashboard_layout_post(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_API_ENABLED(req);
  // Auth optional — layout is non-sensitive UI preference (matches GET handler)

  char body[700];
  int len = httpd_req_recv(req, body, sizeof(body) - 1);
  if (len <= 0) {
    return api_send_error(req, 400, "Empty request body");
  }
  body[len] = '\0';

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body);
  if (error) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  // card_order (optional now — may send only tabs/hidden)
  if (doc.containsKey("card_order")) {
    const char *order = doc["card_order"].as<const char*>();
    if (order && strlen(order) < sizeof(g_persist_config.dashboard_card_order)) {
      strncpy(g_persist_config.dashboard_card_order, order, sizeof(g_persist_config.dashboard_card_order) - 1);
      g_persist_config.dashboard_card_order[sizeof(g_persist_config.dashboard_card_order) - 1] = '\0';
    }
  }

  // card_tabs (schema 18)
  if (doc.containsKey("card_tabs")) {
    const char *tabs = doc["card_tabs"].as<const char*>();
    if (tabs && strlen(tabs) < sizeof(g_persist_config.dashboard_card_tabs)) {
      strncpy(g_persist_config.dashboard_card_tabs, tabs, sizeof(g_persist_config.dashboard_card_tabs) - 1);
      g_persist_config.dashboard_card_tabs[sizeof(g_persist_config.dashboard_card_tabs) - 1] = '\0';
    }
  }

  // card_hidden (schema 18)
  if (doc.containsKey("card_hidden")) {
    const char *hidden = doc["card_hidden"].as<const char*>();
    if (hidden && strlen(hidden) < sizeof(g_persist_config.dashboard_card_hidden)) {
      strncpy(g_persist_config.dashboard_card_hidden, hidden, sizeof(g_persist_config.dashboard_card_hidden) - 1);
      g_persist_config.dashboard_card_hidden[sizeof(g_persist_config.dashboard_card_hidden) - 1] = '\0';
    }
  }

  // card_custom (schema 23) — "Custom"-fane medlemsskab, uafhaengigt af card_tabs
  if (doc.containsKey("card_custom")) {
    const char *custom = doc["card_custom"].as<const char*>();
    if (custom && strlen(custom) < sizeof(g_persist_config.dashboard_card_custom)) {
      strncpy(g_persist_config.dashboard_card_custom, custom, sizeof(g_persist_config.dashboard_card_custom) - 1);
      g_persist_config.dashboard_card_custom[sizeof(g_persist_config.dashboard_card_custom) - 1] = '\0';
    }
  }

  char resp[700];
  snprintf(resp, sizeof(resp),
    "{\"status\":200,\"card_order\":\"%s\",\"card_tabs\":\"%s\",\"card_hidden\":\"%s\",\"card_custom\":\"%s\"}",
    g_persist_config.dashboard_card_order,
    g_persist_config.dashboard_card_tabs,
    g_persist_config.dashboard_card_hidden,
    g_persist_config.dashboard_card_custom);
  return api_send_json(req, resp);
}

/* ============================================================================
 * FEAT-025: GET /api/system/watchdog
 * ============================================================================ */

esp_err_t api_handler_system_watchdog(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  WatchdogState *wd = watchdog_get_state();

  JsonDocument doc;
  doc["enabled"] = wd->enabled ? true : false;
  doc["timeout_ms"] = wd->timeout_ms;
  doc["reboot_count"] = wd->reboot_counter;
  doc["last_reset_reason"] = wd->last_reset_reason;
  doc["last_error"] = wd->last_error;
  doc["last_reboot_uptime_ms"] = wd->last_reboot_uptime_ms;
  doc["uptime_ms"] = millis();
  doc["heap_free"] = ESP.getFreeHeap();
  doc["heap_min_free"] = ESP.getMinFreeHeap();

  char buf[512];
  serializeJson(doc, buf, sizeof(buf));
  return api_send_json(req, buf);
}

/* ============================================================================
 * FEAT-021: Bulk register read — GET /api/registers/hr (with ?start=&count=)
 * ============================================================================ */

static int api_parse_query_int(httpd_req_t *req, const char *key, int default_val)
{
  char qstr[128];
  if (httpd_req_get_url_query_str(req, qstr, sizeof(qstr)) != ESP_OK) return default_val;
  char val[16];
  if (httpd_query_key_value(qstr, key, val, sizeof(val)) != ESP_OK) return default_val;
  return atoi(val);
}

esp_err_t api_handler_hr_bulk_read(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  int start = api_parse_query_int(req, "start", 0);
  int count = api_parse_query_int(req, "count", 10);

  if (start < 0 || start >= HOLDING_REGS_SIZE) {
    return api_send_error(req, 400, "Invalid start address");
  }
  if (count < 1 || count > 200) {
    return api_send_error(req, 400, "Count must be 1-200");
  }
  if (start + count > HOLDING_REGS_SIZE) {
    count = HOLDING_REGS_SIZE - start;
  }

  // Allocate on heap: ~25 bytes per register entry in JSON
  size_t buf_size = (size_t)count * 30 + 128;
  char *buf = (char *)malloc(buf_size);
  if (!buf) {
    return api_send_error(req, 500, "Out of memory");
  }

  int pos = snprintf(buf, buf_size, "{\"start\":%d,\"count\":%d,\"registers\":[", start, count);
  for (int i = 0; i < count && pos < (int)buf_size - 32; i++) {
    uint16_t val = registers_get_holding_register(start + i);
    if (i > 0) buf[pos++] = ',';
    pos += snprintf(buf + pos, buf_size - pos, "{\"addr\":%d,\"value\":%u}", start + i, val);
  }
  pos += snprintf(buf + pos, buf_size - pos, "]}");

  esp_err_t ret = api_send_json(req, buf);
  free(buf);
  return ret;
}

/* ============================================================================
 * FEAT-021: Bulk register write — POST /api/registers/hr/bulk
 * ============================================================================ */

esp_err_t api_handler_hr_bulk_write(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  char *body = (char *)malloc(2048);
  if (!body) {
    return api_send_error(req, 500, "Out of memory");
  }

  int len = httpd_req_recv(req, body, 2047);
  if (len <= 0) {
    free(body);
    return api_send_error(req, 400, "Empty request body");
  }
  body[len] = '\0';

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body);
  free(body);
  if (error) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  if (!doc.containsKey("writes")) {
    return api_send_error(req, 400, "Missing 'writes' array");
  }

  JsonArray writes = doc["writes"];
  int written = 0;
  for (JsonObject w : writes) {
    int addr = w["addr"] | -1;
    if (addr < 0 || addr >= HOLDING_REGS_SIZE) continue;
    uint16_t val = w["value"] | 0;
    registers_set_holding_register(addr, val);
    written++;
  }

  // FEAT-089: ét batch-event, ikke ét pr. register (undgaar at oversvoemme
  // loggen ved en bulk-skrivning paa fx 50 registre)
  if (written > 0) {
    char ip[16], user[24], msg[48];
    http_get_client_info(req, ip, sizeof(ip), user, sizeof(user));
    snprintf(msg, sizeof(msg), "Bulk HR-skriv (%d registre)", written);
    system_log_add_event((uint8_t)SYSLOG_SRC_REST, user, ip, msg);
  }

  char resp[128];
  snprintf(resp, sizeof(resp), "{\"status\":200,\"written\":%d}", written);
  return api_send_json(req, resp);
}

/* ============================================================================
 * FEAT-021: Bulk IR read — GET /api/registers/ir (with ?start=&count=)
 * ============================================================================ */

esp_err_t api_handler_ir_bulk_read(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  int start = api_parse_query_int(req, "start", 0);
  int count = api_parse_query_int(req, "count", 10);

  if (start < 0 || start >= INPUT_REGS_SIZE) {
    return api_send_error(req, 400, "Invalid start address");
  }
  if (count < 1 || count > 200) {
    return api_send_error(req, 400, "Count must be 1-200");
  }
  if (start + count > INPUT_REGS_SIZE) {
    count = INPUT_REGS_SIZE - start;
  }

  size_t buf_size = (size_t)count * 30 + 128;
  char *buf = (char *)malloc(buf_size);
  if (!buf) {
    return api_send_error(req, 500, "Out of memory");
  }

  int pos = snprintf(buf, buf_size, "{\"start\":%d,\"count\":%d,\"registers\":[", start, count);
  for (int i = 0; i < count && pos < (int)buf_size - 32; i++) {
    uint16_t val = registers_get_input_register(start + i);
    if (i > 0) buf[pos++] = ',';
    pos += snprintf(buf + pos, buf_size - pos, "{\"addr\":%d,\"value\":%u}", start + i, val);
  }
  pos += snprintf(buf + pos, buf_size - pos, "]}");

  esp_err_t ret = api_send_json(req, buf);
  free(buf);
  return ret;
}

/* ============================================================================
 * FEAT-021: Bulk coils read — GET /api/registers/coils (with ?start=&count=)
 * ============================================================================ */

esp_err_t api_handler_coils_bulk_read(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  int start = api_parse_query_int(req, "start", 0);
  int count = api_parse_query_int(req, "count", 32);

  if (start < 0 || start >= 256) {
    return api_send_error(req, 400, "Invalid start address");
  }
  if (count < 1 || count > 256) {
    return api_send_error(req, 400, "Count must be 1-256");
  }
  if (start + count > 256) {
    count = 256 - start;
  }

  size_t buf_size = (size_t)count * 28 + 128;
  char *buf = (char *)malloc(buf_size);
  if (!buf) {
    return api_send_error(req, 500, "Out of memory");
  }

  int pos = snprintf(buf, buf_size, "{\"start\":%d,\"count\":%d,\"coils\":[", start, count);
  for (int i = 0; i < count && pos < (int)buf_size - 32; i++) {
    uint8_t val = registers_get_coil(start + i);
    if (i > 0) buf[pos++] = ',';
    pos += snprintf(buf + pos, buf_size - pos, "{\"addr\":%d,\"value\":%s}", start + i, val ? "true" : "false");
  }
  pos += snprintf(buf + pos, buf_size - pos, "]}");

  esp_err_t ret = api_send_json(req, buf);
  free(buf);
  return ret;
}

/* ============================================================================
 * FEAT-021: Bulk coils write — POST /api/registers/coils/bulk
 * ============================================================================ */

esp_err_t api_handler_coils_bulk_write(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  char *body = (char *)malloc(2048);
  if (!body) {
    return api_send_error(req, 500, "Out of memory");
  }

  int len = httpd_req_recv(req, body, 2047);
  if (len <= 0) {
    free(body);
    return api_send_error(req, 400, "Empty request body");
  }
  body[len] = '\0';

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body);
  free(body);
  if (error) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  if (!doc.containsKey("writes")) {
    return api_send_error(req, 400, "Missing 'writes' array");
  }

  JsonArray writes = doc["writes"];
  int written = 0;
  for (JsonObject w : writes) {
    int addr = w["addr"] | -1;
    if (addr < 0 || addr >= 256) continue;
    bool val = w["value"].as<bool>();
    registers_set_coil(addr, val ? 1 : 0);
    written++;
  }

  // FEAT-089: ét batch-event, ikke ét pr. coil
  if (written > 0) {
    char ip[16], user[24], msg[48];
    http_get_client_info(req, ip, sizeof(ip), user, sizeof(user));
    snprintf(msg, sizeof(msg), "Bulk coil-skriv (%d coils)", written);
    system_log_add_event((uint8_t)SYSLOG_SRC_REST, user, ip, msg);
  }

  char resp[128];
  snprintf(resp, sizeof(resp), "{\"status\":200,\"written\":%d}", written);
  return api_send_json(req, resp);
}

/* ============================================================================
 * FEAT-021: Bulk DI read — GET /api/registers/di (with ?start=&count=)
 * ============================================================================ */

esp_err_t api_handler_di_bulk_read(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  int start = api_parse_query_int(req, "start", 0);
  int count = api_parse_query_int(req, "count", 32);

  if (start < 0 || start >= 256) {
    return api_send_error(req, 400, "Invalid start address");
  }
  if (count < 1 || count > 256) {
    return api_send_error(req, 400, "Count must be 1-256");
  }
  if (start + count > 256) {
    count = 256 - start;
  }

  size_t buf_size = (size_t)count * 28 + 128;
  char *buf = (char *)malloc(buf_size);
  if (!buf) {
    return api_send_error(req, 500, "Out of memory");
  }

  int pos = snprintf(buf, buf_size, "{\"start\":%d,\"count\":%d,\"inputs\":[", start, count);
  for (int i = 0; i < count && pos < (int)buf_size - 32; i++) {
    uint8_t val = registers_get_discrete_input(start + i);
    if (i > 0) buf[pos++] = ',';
    pos += snprintf(buf + pos, buf_size - pos, "{\"addr\":%d,\"value\":%s}", start + i, val ? "true" : "false");
  }
  pos += snprintf(buf + pos, buf_size - pos, "]}");

  esp_err_t ret = api_send_json(req, buf);
  free(buf);
  return ret;
}

/* ============================================================================
 * FEAT-020: ST Logic Debug API — suffix routing via /api/logic/{id}/debug/*
 * ============================================================================ */

esp_err_t api_handler_logic_debug(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  // Parse: /api/logic/{id}/debug/{action}
  const char *uri = req->uri;
  int id = api_extract_id_from_uri(req, "/api/logic/");
  if (id < 1 || id > ST_LOGIC_MAX_PROGRAMS) {
    return api_send_error(req, 400, "Invalid program ID (must be 1-4)");
  }

  st_logic_engine_state_t *st = st_logic_get_state();
  st_debug_state_t *dbg = &st->debugger[id - 1];

  // Find /debug/ suffix
  const char *debug_pos = strstr(uri, "/debug/");
  if (!debug_pos) {
    return api_send_error(req, 400, "Missing debug action");
  }
  const char *action = debug_pos + 7; // skip "/debug/"

  // Strip query params
  char action_buf[32];
  int ai = 0;
  while (*action && *action != '?' && ai < 31) {
    action_buf[ai++] = *action++;
  }
  action_buf[ai] = '\0';

  // GET /api/logic/{id}/debug/state — return snapshot
  if (req->method == HTTP_GET && strcmp(action_buf, "state") == 0) {
    JsonDocument doc;
    doc["program_id"] = id;
    doc["mode"] = (dbg->mode == ST_DEBUG_OFF) ? "off" :
                  (dbg->mode == ST_DEBUG_PAUSED) ? "paused" :
                  (dbg->mode == ST_DEBUG_STEP) ? "step" : "run";
    doc["pause_reason"] = (int)dbg->pause_reason;
    doc["breakpoint_count"] = dbg->breakpoint_count;
    doc["total_steps"] = dbg->total_steps_debugged;
    doc["breakpoints_hit"] = dbg->breakpoints_hit_count;

    JsonArray bps = doc["breakpoints"].to<JsonArray>();
    for (int i = 0; i < dbg->breakpoint_count; i++) {
      bps.add(dbg->breakpoints[i]);
    }

    if (dbg->snapshot_valid) {
      JsonObject snap = doc["snapshot"].to<JsonObject>();
      snap["pc"] = dbg->snapshot.pc;
      snap["sp"] = dbg->snapshot.sp;
      snap["halted"] = dbg->snapshot.halted ? true : false;
      snap["error"] = dbg->snapshot.error ? true : false;
      snap["step_count"] = dbg->snapshot.step_count;
      if (dbg->snapshot.error) {
        snap["error_msg"] = dbg->snapshot.error_msg;
      }
      JsonArray vars = snap["variables"].to<JsonArray>();
      for (int i = 0; i < dbg->snapshot.var_count && i < 32; i++) {
        JsonObject v = vars.add<JsonObject>();
        v["index"] = i;
        if (dbg->snapshot.var_types[i] == ST_TYPE_REAL) {
          v["type"] = "REAL";
          v["value"] = dbg->snapshot.variables[i].real_val;
        } else if (dbg->snapshot.var_types[i] == ST_TYPE_DINT) {
          v["type"] = "DINT";
          v["value"] = dbg->snapshot.variables[i].dint_val;
        } else if (dbg->snapshot.var_types[i] == ST_TYPE_BOOL) {
          v["type"] = "BOOL";
          v["value"] = dbg->snapshot.variables[i].bool_val ? true : false;
        } else if (dbg->snapshot.var_types[i] == ST_TYPE_STRING) {
          // FEAT-005: snapshottet kopierer kun st_value_t (str_ref), ikke
          // selve teksten — resolves her direkte fra det LEVENDE programs
          // string_vars[] (gyldigt saa laenge programmet stadig er
          // compileret/kompileret, hvilket det er naar en snapshot findes).
          v["type"] = "STRING";
          v["value"] = st->programs[id - 1].bytecode.string_vars[i];
        } else {
          v["type"] = "INT";
          v["value"] = dbg->snapshot.variables[i].int_val;
        }
      }
    }

    char *buf = (char *)malloc(2048);
    if (!buf) return api_send_error(req, 500, "Out of memory");
    serializeJson(doc, buf, 2048);
    esp_err_t ret = api_send_json(req, buf);
    free(buf);
    return ret;
  }

  // POST actions
  if (req->method == HTTP_POST) {
    if (strcmp(action_buf, "pause") == 0) {
      st_debug_alloc_vm();
      st_debug_pause(dbg);
      char resp[96];
      snprintf(resp, sizeof(resp), "{\"status\":200,\"message\":\"Program %d paused\"}", id);
      return api_send_json(req, resp);
    }

    if (strcmp(action_buf, "continue") == 0) {
      st_debug_continue(dbg);
      char resp[96];
      snprintf(resp, sizeof(resp), "{\"status\":200,\"message\":\"Program %d continued\"}", id);
      return api_send_json(req, resp);
    }

    if (strcmp(action_buf, "step") == 0) {
      st_debug_alloc_vm();
      st_debug_step(dbg);
      char resp[96];
      snprintf(resp, sizeof(resp), "{\"status\":200,\"message\":\"Program %d stepped\"}", id);
      return api_send_json(req, resp);
    }

    if (strcmp(action_buf, "stop") == 0) {
      st_debug_stop(dbg);
      st_debug_free_vm();
      char resp[96];
      snprintf(resp, sizeof(resp), "{\"status\":200,\"message\":\"Program %d debug stopped\"}", id);
      return api_send_json(req, resp);
    }

    if (strcmp(action_buf, "breakpoint") == 0) {
      char body[128];
      int blen = httpd_req_recv(req, body, sizeof(body) - 1);
      if (blen <= 0) {
        return api_send_error(req, 400, "Missing breakpoint data");
      }
      body[blen] = '\0';

      JsonDocument bdoc;
      if (deserializeJson(bdoc, body)) {
        return api_send_error(req, 400, "Invalid JSON");
      }

      uint16_t pc = bdoc["pc"] | 0xFFFF;
      if (pc == 0xFFFF) {
        return api_send_error(req, 400, "Missing 'pc' field");
      }

      bool ok = st_debug_add_breakpoint(dbg, pc);
      if (!ok) {
        return api_send_error(req, 400, "Max breakpoints reached (8)");
      }

      char resp[128];
      snprintf(resp, sizeof(resp), "{\"status\":200,\"message\":\"Breakpoint set at PC %u\"}", pc);
      return api_send_json(req, resp);
    }
  }

  // DELETE /api/logic/{id}/debug/breakpoint
  if (req->method == HTTP_DELETE && strcmp(action_buf, "breakpoint") == 0) {
    char body[128];
    int blen = httpd_req_recv(req, body, sizeof(body) - 1);
    if (blen > 0) {
      body[blen] = '\0';
      JsonDocument bdoc;
      if (!deserializeJson(bdoc, body)) {
        uint16_t pc = bdoc["pc"] | 0xFFFF;
        if (pc != 0xFFFF) {
          st_debug_remove_breakpoint(dbg, pc);
          char resp[128];
          snprintf(resp, sizeof(resp), "{\"status\":200,\"message\":\"Breakpoint removed at PC %u\"}", pc);
          return api_send_json(req, resp);
        }
      }
    }
    // No body or no pc = clear all
    st_debug_clear_breakpoints(dbg);
    return api_send_json(req, "{\"status\":200,\"message\":\"All breakpoints cleared\"}");
  }

  return api_send_error(req, 404, "Unknown debug action");
}

/* ============================================================================
 * FEAT-026: POST /api/gpio/2/heartbeat — GPIO2 heartbeat control
 * ============================================================================ */

esp_err_t api_handler_heartbeat(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  if (req->method == HTTP_GET) {
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"enabled\":%s,\"gpio2_user_mode\":%s}",
             g_persist_config.gpio2_user_mode ? "false" : "true",
             g_persist_config.gpio2_user_mode ? "true" : "false");
    return api_send_json(req, buf);
  }

  // POST — requires write privilege
  {
    int _uid = http_server_auth_user(req);
    if (_uid >= 0 && !rbac_has_write(_uid)) {
      return api_send_error(req, 403, "Write privilege required");
    }
  }
  char body[128];
  int len = httpd_req_recv(req, body, sizeof(body) - 1);
  if (len <= 0) {
    return api_send_error(req, 400, "Empty request body");
  }
  body[len] = '\0';

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  if (doc.containsKey("enabled")) {
    bool enable = doc["enabled"].as<bool>();
    if (enable) {
      g_persist_config.gpio2_user_mode = 0;  // heartbeat mode
      heartbeat_enable();
    } else {
      g_persist_config.gpio2_user_mode = 1;  // user mode
      heartbeat_disable();
    }
  }

  char resp[128];
  snprintf(resp, sizeof(resp), "{\"status\":200,\"heartbeat_enabled\":%s}",
           g_persist_config.gpio2_user_mode ? "false" : "true");
  return api_send_json(req, resp);
}

/* ============================================================================
 * FEAT-030: GET /api/version — API Version Info (v7.0.0)
 * ============================================================================ */

esp_err_t api_handler_api_version(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  char buf[256];
  snprintf(buf, sizeof(buf),
    "{\"api_version\":1,\"api_version_str\":\"v1\","
    "\"firmware_version\":\"%s\",\"build\":%d,"
    "\"min_supported_api\":1,"
    "\"versioned_prefix\":\"/api/v1\","
    "\"unversioned_prefix\":\"/api\"}",
    PROJECT_VERSION, BUILD_NUMBER);

  return api_send_json(req, buf);
}

/* ============================================================================
 * FEAT-032: GET /api/metrics — Prometheus Metrics Endpoint (v7.0.4)
 *
 * Returns metrics in Prometheus text exposition format (text/plain).
 * Scrape-ready for Prometheus/Grafana integration.
 * ============================================================================ */

esp_err_t api_handler_metrics(httpd_req_t *req)
{
  http_server_stat_request();
  // No auth required — metrics are read-only, used by dashboard and Prometheus scrapers
  CHECK_API_ENABLED(req);
  if (!http_rate_limit_check(req)) {
    return api_send_error(req, 429, "Too many requests");
  }

  // Buffer for Prometheus text format. BUG-358: var 12KB, men de to
  // register-dump-loekker laengere nede (modbus_holding_register/
  // modbus_input_register, HOLDING_REGS_SIZE+INPUT_REGS_SIZE=512 registre)
  // kan alene fylde op mod ~21KB hvis alle registre er non-zero (plausibelt
  // paa et aktivt system med mange taellere/registre i brug) — PROM_APPEND
  // dropper stille enhver linje der ikke er plads til, uden fejl/advarsel,
  // saa alt skrevet EFTER at bufferen blev fuld forsvandt usynligt (fx NTP-,
  // alarm- og syslog-status, som viste forkert i dashboardet selvom CLI var
  // korrekt). Samme bug-klasse som BUG-336c/BUG-354 (fast graense uden
  // margin, overskredet uden synlig fejl). Sat til 32KB med reel margin;
  // register-dumpet er desuden flyttet til SIDST i funktionen, saa smaa,
  // faste status-metrics altid skrives foerst og er sikre uanset register-
  // antal. Tjek denne kommentar igen hvis flere ubegraensede loekker
  // tilfoejes.
  char *buf = (char *)malloc(32768);
  if (!buf) {
    return api_send_error(req, 500, "Out of memory");
  }
  int pos = 0;
  int remaining = 32768;

  #define PROM_APPEND(...) do { \
    int n = snprintf(buf + pos, remaining, __VA_ARGS__); \
    if (n > 0 && n < remaining) { pos += n; remaining -= n; } \
  } while(0)

  // --- System metrics ---
  PROM_APPEND("# HELP esp32_uptime_seconds Device uptime in seconds\n");
  PROM_APPEND("# TYPE esp32_uptime_seconds gauge\n");
  PROM_APPEND("esp32_uptime_seconds %lu\n", (unsigned long)(millis() / 1000));

  PROM_APPEND("# HELP esp32_heap_free_bytes Free heap memory in bytes\n");
  PROM_APPEND("# TYPE esp32_heap_free_bytes gauge\n");
  PROM_APPEND("esp32_heap_free_bytes %lu\n", (unsigned long)ESP.getFreeHeap());

  PROM_APPEND("# HELP esp32_heap_min_free_bytes Minimum free heap since boot\n");
  PROM_APPEND("# TYPE esp32_heap_min_free_bytes gauge\n");
  PROM_APPEND("esp32_heap_min_free_bytes %lu\n", (unsigned long)ESP.getMinFreeHeap());

  // --- Flash chip metrics ---
  PROM_APPEND("# HELP esp32_flash_total_bytes Total flash size (from eFuse) in bytes\n");
  PROM_APPEND("# TYPE esp32_flash_total_bytes gauge\n");
  PROM_APPEND("esp32_flash_total_bytes %lu\n", (unsigned long)ESP.getFlashChipSize());
  PROM_APPEND("# HELP esp32_flash_speed_hz Flash access speed in Hz\n");
  PROM_APPEND("# TYPE esp32_flash_speed_hz gauge\n");
  PROM_APPEND("esp32_flash_speed_hz %lu\n", (unsigned long)ESP.getFlashChipSpeed());

  // --- PSRAM metrics (if available) ---
  uint32_t psram_total = ESP.getPsramSize();
  if (psram_total > 0) {
    PROM_APPEND("# HELP esp32_psram_total_bytes Total PSRAM in bytes\n");
    PROM_APPEND("# TYPE esp32_psram_total_bytes gauge\n");
    PROM_APPEND("esp32_psram_total_bytes %lu\n", (unsigned long)psram_total);
    PROM_APPEND("# HELP esp32_psram_free_bytes Free PSRAM in bytes\n");
    PROM_APPEND("# TYPE esp32_psram_free_bytes gauge\n");
    PROM_APPEND("esp32_psram_free_bytes %lu\n", (unsigned long)ESP.getFreePsram());
  }

  // --- NVS usage metrics (FEAT-081) ---
  // nvs_get_stats(NULL, ...) summerer paa TVAERS af alle partitioner/namespaces
  // — samme granularitet "show config"-lignende diagnostik i dette projekt
  // allerede bruger. Enheder er 32-bytes ENTRIES, ikke raa bytes (NVS'
  // interne allokeringsgranularitet) — vist som saadan for at undgaa et
  // falsk praecist byte-tal.
  {
    nvs_stats_t nvs_stats;
    if (nvs_get_stats(NULL, &nvs_stats) == ESP_OK) {
      PROM_APPEND("# HELP nvs_used_entries NVS brugte entries (32 bytes/entry)\n");
      PROM_APPEND("# TYPE nvs_used_entries gauge\n");
      PROM_APPEND("nvs_used_entries %u\n", (unsigned)nvs_stats.used_entries);
      PROM_APPEND("# HELP nvs_free_entries NVS ledige entries\n");
      PROM_APPEND("# TYPE nvs_free_entries gauge\n");
      PROM_APPEND("nvs_free_entries %u\n", (unsigned)nvs_stats.free_entries);
      PROM_APPEND("# HELP nvs_total_entries NVS entries totalt\n");
      PROM_APPEND("# TYPE nvs_total_entries gauge\n");
      PROM_APPEND("nvs_total_entries %u\n", (unsigned)nvs_stats.total_entries);
      PROM_APPEND("# HELP nvs_namespace_count Antal NVS-namespaces i brug\n");
      PROM_APPEND("# TYPE nvs_namespace_count gauge\n");
      PROM_APPEND("nvs_namespace_count %u\n", (unsigned)nvs_stats.namespace_count);
    }
  }

  // --- SPIFFS usage metrics (FEAT-082) ---
  // SPIFFS bruges allerede i projektet til ST Logic bytecode/kildekode
  // (st_bytecode_persist.cpp, st_logic_config.cpp) og er dermed allerede
  // monteret paa dette tidspunkt i boot.
  {
    size_t spiffs_total = SPIFFS.totalBytes();
    if (spiffs_total > 0) {
      PROM_APPEND("# HELP spiffs_used_bytes SPIFFS brugt plads i bytes\n");
      PROM_APPEND("# TYPE spiffs_used_bytes gauge\n");
      PROM_APPEND("spiffs_used_bytes %lu\n", (unsigned long)SPIFFS.usedBytes());
      PROM_APPEND("# HELP spiffs_total_bytes SPIFFS total plads i bytes\n");
      PROM_APPEND("# TYPE spiffs_total_bytes gauge\n");
      PROM_APPEND("spiffs_total_bytes %lu\n", (unsigned long)spiffs_total);
    }
  }

  // --- HTTP API metrics ---
  const HttpServerStats *stats = http_server_get_stats();
  if (stats) {
    PROM_APPEND("# HELP http_requests_total Total HTTP API requests\n");
    PROM_APPEND("# TYPE http_requests_total counter\n");
    PROM_APPEND("http_requests_total %lu\n", stats->total_requests);

    PROM_APPEND("# HELP http_requests_success_total Successful HTTP responses (2xx)\n");
    PROM_APPEND("# TYPE http_requests_success_total counter\n");
    PROM_APPEND("http_requests_success_total %lu\n", stats->successful_requests);

    PROM_APPEND("# HELP http_requests_client_errors_total Client error responses (4xx)\n");
    PROM_APPEND("# TYPE http_requests_client_errors_total counter\n");
    PROM_APPEND("http_requests_client_errors_total %lu\n", stats->client_errors);

    PROM_APPEND("# HELP http_requests_server_errors_total Server error responses (5xx)\n");
    PROM_APPEND("# TYPE http_requests_server_errors_total counter\n");
    PROM_APPEND("http_requests_server_errors_total %lu\n", stats->server_errors);

    PROM_APPEND("# HELP http_auth_failures_total Authentication failures\n");
    PROM_APPEND("# TYPE http_auth_failures_total counter\n");
    PROM_APPEND("http_auth_failures_total %lu\n", stats->auth_failures);
  }

  // --- Modbus Slave config metrics ---
  PROM_APPEND("# HELP modbus_slave_config_enabled Modbus slave enabled (1=yes, 0=no)\n");
  PROM_APPEND("# TYPE modbus_slave_config_enabled gauge\n");
  PROM_APPEND("modbus_slave_config_enabled %d\n", g_persist_config.modbus_slave.enabled ? 1 : 0);
  PROM_APPEND("# HELP modbus_slave_config_id Modbus slave ID\n");
  PROM_APPEND("# TYPE modbus_slave_config_id gauge\n");
  PROM_APPEND("modbus_slave_config_id %d\n", g_persist_config.modbus_slave.slave_id);
  PROM_APPEND("# HELP modbus_slave_config_baudrate Modbus slave baudrate\n");
  PROM_APPEND("# TYPE modbus_slave_config_baudrate gauge\n");
  PROM_APPEND("modbus_slave_config_baudrate %lu\n", (unsigned long)g_persist_config.modbus_slave.baudrate);
  PROM_APPEND("# HELP modbus_slave_config_parity Modbus slave parity (0=N, 1=E, 2=O)\n");
  PROM_APPEND("# TYPE modbus_slave_config_parity gauge\n");
  PROM_APPEND("modbus_slave_config_parity %d\n", g_persist_config.modbus_slave.parity);
  PROM_APPEND("# HELP modbus_slave_config_stopbits Modbus slave stop bits\n");
  PROM_APPEND("# TYPE modbus_slave_config_stopbits gauge\n");
  PROM_APPEND("modbus_slave_config_stopbits %d\n", g_persist_config.modbus_slave.stop_bits);

  // --- Modbus Slave metrics ---
  PROM_APPEND("# HELP modbus_slave_requests_total Total Modbus slave requests\n");
  PROM_APPEND("# TYPE modbus_slave_requests_total counter\n");
  PROM_APPEND("modbus_slave_requests_total %lu\n", g_persist_config.modbus_slave.total_requests);

  PROM_APPEND("# HELP modbus_slave_success_total Successful Modbus slave responses\n");
  PROM_APPEND("# TYPE modbus_slave_success_total counter\n");
  PROM_APPEND("modbus_slave_success_total %lu\n", g_persist_config.modbus_slave.successful_requests);

  PROM_APPEND("# HELP modbus_slave_crc_errors_total Modbus slave CRC errors\n");
  PROM_APPEND("# TYPE modbus_slave_crc_errors_total counter\n");
  PROM_APPEND("modbus_slave_crc_errors_total %lu\n", g_persist_config.modbus_slave.crc_errors);

  PROM_APPEND("# HELP modbus_slave_exceptions_total Modbus slave exception responses\n");
  PROM_APPEND("# TYPE modbus_slave_exceptions_total counter\n");
  PROM_APPEND("modbus_slave_exceptions_total %lu\n", g_persist_config.modbus_slave.exception_errors);

  // --- Heap detailed metrics ---
  PROM_APPEND("# HELP esp32_heap_largest_free_block Largest contiguous free heap block\n");
  PROM_APPEND("# TYPE esp32_heap_largest_free_block gauge\n");
  PROM_APPEND("esp32_heap_largest_free_block %lu\n", (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  // --- Modbus Master config metrics ---
  PROM_APPEND("# HELP modbus_master_config_enabled Modbus master enabled (1=yes, 0=no)\n");
  PROM_APPEND("# TYPE modbus_master_config_enabled gauge\n");
  PROM_APPEND("modbus_master_config_enabled %d\n", g_modbus_master_config.enabled ? 1 : 0);
  PROM_APPEND("# HELP modbus_master_config_baudrate Modbus master baudrate\n");
  PROM_APPEND("# TYPE modbus_master_config_baudrate gauge\n");
  PROM_APPEND("modbus_master_config_baudrate %lu\n", (unsigned long)g_modbus_master_config.baudrate);
  PROM_APPEND("# HELP modbus_master_config_parity Modbus master parity (0=N, 1=E, 2=O)\n");
  PROM_APPEND("# TYPE modbus_master_config_parity gauge\n");
  PROM_APPEND("modbus_master_config_parity %d\n", g_modbus_master_config.parity);
  PROM_APPEND("# HELP modbus_master_config_stopbits Modbus master stop bits\n");
  PROM_APPEND("# TYPE modbus_master_config_stopbits gauge\n");
  PROM_APPEND("modbus_master_config_stopbits %d\n", g_modbus_master_config.stop_bits);

  // --- Modbus Master metrics ---
  PROM_APPEND("# HELP modbus_master_stats_age_ms Milliseconds since last stats reset\n");
  PROM_APPEND("# TYPE modbus_master_stats_age_ms gauge\n");
  PROM_APPEND("modbus_master_stats_age_ms %lu\n",
    g_modbus_master_config.stats_since_ms > 0 ? (unsigned long)(millis() - g_modbus_master_config.stats_since_ms) : (unsigned long)millis());

  PROM_APPEND("# HELP modbus_master_requests_total Total Modbus master requests\n");
  PROM_APPEND("# TYPE modbus_master_requests_total counter\n");
  PROM_APPEND("modbus_master_requests_total %lu\n", g_modbus_master_config.total_requests);

  PROM_APPEND("# HELP modbus_master_success_total Successful Modbus master responses\n");
  PROM_APPEND("# TYPE modbus_master_success_total counter\n");
  PROM_APPEND("modbus_master_success_total %lu\n", g_modbus_master_config.successful_requests);

  PROM_APPEND("# HELP modbus_master_timeout_errors_total Modbus master timeout errors\n");
  PROM_APPEND("# TYPE modbus_master_timeout_errors_total counter\n");
  PROM_APPEND("modbus_master_timeout_errors_total %lu\n", g_modbus_master_config.timeout_errors);

  PROM_APPEND("# HELP modbus_master_crc_errors_total Modbus master CRC errors\n");
  PROM_APPEND("# TYPE modbus_master_crc_errors_total counter\n");
  PROM_APPEND("modbus_master_crc_errors_total %lu\n", g_modbus_master_config.crc_errors);

  PROM_APPEND("# HELP modbus_master_exception_errors_total Modbus master exception errors\n");
  PROM_APPEND("# TYPE modbus_master_exception_errors_total counter\n");
  PROM_APPEND("modbus_master_exception_errors_total %lu\n", g_modbus_master_config.exception_errors);

  PROM_APPEND("# HELP modbus_master_bus_busy_errors_total Modbus master UART-mutex ikke opnaaet (bus optaget)\n");
  PROM_APPEND("# TYPE modbus_master_bus_busy_errors_total counter\n");
  PROM_APPEND("modbus_master_bus_busy_errors_total %lu\n", g_modbus_bus_busy_errors);

  // --- Modbus Master Async Cache metrics ---
  const mb_async_state_t *mb_async = mb_async_get_state();
  if (mb_async && mb_async->task_running) {
    PROM_APPEND("# HELP modbus_master_cache_hits Async cache hit count\n");
    PROM_APPEND("# TYPE modbus_master_cache_hits counter\n");
    PROM_APPEND("modbus_master_cache_hits %lu\n", (unsigned long)mb_async->cache_hits);
    PROM_APPEND("# HELP modbus_master_cache_misses Async cache miss count\n");
    PROM_APPEND("# TYPE modbus_master_cache_misses counter\n");
    PROM_APPEND("modbus_master_cache_misses %lu\n", (unsigned long)mb_async->cache_misses);
    PROM_APPEND("# HELP modbus_master_cache_entries Active cache entries\n");
    PROM_APPEND("# TYPE modbus_master_cache_entries gauge\n");
    PROM_APPEND("modbus_master_cache_entries %d\n", mb_async->entry_count);
    PROM_APPEND("# HELP modbus_master_queue_full_count Queue full rejections\n");
    PROM_APPEND("# TYPE modbus_master_queue_full_count counter\n");
    PROM_APPEND("modbus_master_queue_full_count %lu\n", (unsigned long)mb_async->queue_full_count);
    PROM_APPEND("# HELP modbus_master_queue_depth Current queue depth\n");
    PROM_APPEND("# TYPE modbus_master_queue_depth gauge\n");
    PROM_APPEND("modbus_master_queue_depth %u\n", (unsigned)mb_async->pq_count);
    PROM_APPEND("# HELP modbus_master_queue_hwm Queue high watermark\n");
    PROM_APPEND("# TYPE modbus_master_queue_hwm gauge\n");
    PROM_APPEND("modbus_master_queue_hwm %u\n", (unsigned)mb_async->queue_high_watermark);
    PROM_APPEND("# HELP modbus_master_priority_drops Requests dropped by priority eviction\n");
    PROM_APPEND("# TYPE modbus_master_priority_drops counter\n");
    PROM_APPEND("modbus_master_priority_drops %lu\n", (unsigned long)mb_async->priority_drops);
    PROM_APPEND("# HELP modbus_master_cache_hit_rate Cache hit rate percent\n");
    PROM_APPEND("# TYPE modbus_master_cache_hit_rate gauge\n");
    {
      uint32_t total_lookups = mb_async->cache_hits + mb_async->cache_misses;
      uint8_t hit_pct = total_lookups > 0 ? (uint8_t)(mb_async->cache_hits * 100 / total_lookups) : 0;
      PROM_APPEND("modbus_master_cache_hit_rate %u\n", hit_pct);
    }
    PROM_APPEND("# HELP modbus_master_cache_utilization Cache slot utilization percent\n");
    PROM_APPEND("# TYPE modbus_master_cache_utilization gauge\n");
    PROM_APPEND("modbus_master_cache_utilization %u\n",
                (unsigned)(mb_async->entry_count * 100 / MB_CACHE_MAX_ENTRIES));
    PROM_APPEND("# HELP modbus_master_cache_ttl_ms Cache entry TTL in ms (0=never expire)\n");
    PROM_APPEND("# TYPE modbus_master_cache_ttl_ms gauge\n");
    PROM_APPEND("modbus_master_cache_ttl_ms %u\n", (unsigned)g_modbus_master_config.cache_ttl_ms);

    // Per-slave cache entries with status
    PROM_APPEND("# HELP modbus_master_slave_status Per-slave cache entry status\n");
    PROM_APPEND("# TYPE modbus_master_slave_status gauge\n");
    for (int i = 0; i < mb_async->entry_count && i < MB_CACHE_MAX_ENTRIES; i++) {
      const mb_cache_entry_t *e = &mb_async->entries[i];
      if (e->status != MB_CACHE_EMPTY) {
        const char *st = (e->status == MB_CACHE_VALID) ? "valid" :
                         (e->status == MB_CACHE_PENDING) ? "pending" :
                         (e->status == MB_CACHE_ERROR) ? "error" : "empty";
        uint32_t age_ms = (e->last_update_ms > 0) ? (millis() - e->last_update_ms) : 0;
        uint8_t disp_fc = (e->last_fc > 0) ? e->last_fc : e->key.req_type;
        PROM_APPEND("modbus_master_slave_status{slave=\"%d\",addr=\"%d\",fc=\"%d\",status=\"%s\",age_ms=\"%u\"} %d\n",
                     e->key.slave_id, e->key.address, disp_fc, st, age_ms,
                     (e->status == MB_CACHE_VALID) ? 1 : (e->status == MB_CACHE_ERROR) ? -1 : 0);
      }
    }
    // Per-slave adaptive backoff status
    PROM_APPEND("# HELP modbus_master_slave_backoff Per-slave adaptive backoff delay in ms\n");
    PROM_APPEND("# TYPE modbus_master_slave_backoff gauge\n");
    for (int i = 0; i < MB_SLAVE_BACKOFF_MAX; i++) {
      if (mb_async->slave_backoff[i].slave_id > 0) {
        PROM_APPEND("modbus_master_slave_backoff{slave=\"%d\",timeouts=\"%d\",successes=\"%d\"} %d\n",
                     mb_async->slave_backoff[i].slave_id,
                     mb_async->slave_backoff[i].timeout_count,
                     mb_async->slave_backoff[i].success_count,
                     mb_async->slave_backoff[i].backoff_ms);
      }
    }
  }

  // --- SSE metrics ---
  PROM_APPEND("# HELP sse_clients_active Active SSE client connections\n");
  PROM_APPEND("# TYPE sse_clients_active gauge\n");
  PROM_APPEND("sse_clients_active %d\n", sse_get_client_count());

  // --- Network metrics ---
  PROM_APPEND("# HELP wifi_connected WiFi connection status (1=connected, 0=disconnected)\n");
  PROM_APPEND("# TYPE wifi_connected gauge\n");
  PROM_APPEND("wifi_connected %d\n", wifi_driver_is_connected() ? 1 : 0);

  int rssi = wifi_driver_get_rssi();
  if (wifi_driver_is_connected() && rssi != 0) {
    PROM_APPEND("# HELP wifi_rssi_dbm WiFi signal strength in dBm\n");
    PROM_APPEND("# TYPE wifi_rssi_dbm gauge\n");
    PROM_APPEND("wifi_rssi_dbm %d\n", rssi);
  }

  PROM_APPEND("# HELP ethernet_connected Ethernet connection status (1=connected, 0=disconnected)\n");
  PROM_APPEND("# TYPE ethernet_connected gauge\n");
  PROM_APPEND("ethernet_connected %d\n", ethernet_driver_is_connected() ? 1 : 0);

  const NetworkState *net_state = network_manager_get_state();
  if (net_state) {
    PROM_APPEND("# HELP telnet_connected Telnet client connection status (1=connected, 0=disconnected)\n");
    PROM_APPEND("# TYPE telnet_connected gauge\n");
    PROM_APPEND("telnet_connected %d\n", net_state->telnet_client_connected ? 1 : 0);

    // FEAT-075: Telnet client details for TCP connection monitor
    {
      uint32_t tel_ip = 0, tel_uptime = 0;
      char tel_user[32] = {0};
      if (network_manager_get_telnet_client_info(&tel_ip, &tel_uptime, tel_user)) {
        uint8_t *ip = (uint8_t *)&tel_ip;
        PROM_APPEND("# HELP telnet_client_ip Telnet client IP as label\n");
        PROM_APPEND("# TYPE telnet_client_ip gauge\n");
        PROM_APPEND("telnet_client_ip{ip=\"%d.%d.%d.%d\",user=\"%s\"} 1\n",
                     ip[0], ip[1], ip[2], ip[3], tel_user[0] ? tel_user : "(auth)");
        PROM_APPEND("# HELP telnet_client_uptime_seconds Telnet client connection uptime\n");
        PROM_APPEND("# TYPE telnet_client_uptime_seconds gauge\n");
        PROM_APPEND("telnet_client_uptime_seconds %lu\n", (unsigned long)tel_uptime);
      }
    }

    PROM_APPEND("# HELP wifi_reconnect_retries WiFi reconnect retry count\n");
    PROM_APPEND("# TYPE wifi_reconnect_retries counter\n");
    PROM_APPEND("wifi_reconnect_retries %lu\n", (unsigned long)net_state->wifi_reconnect_retries);
  }

  // --- Counter metrics (expanded) ---
  PROM_APPEND("# HELP counter_value Current counter values\n");
  PROM_APPEND("# TYPE counter_value gauge\n");
  PROM_APPEND("# HELP counter_frequency_hz Measured counter frequency in Hz\n");
  PROM_APPEND("# TYPE counter_frequency_hz gauge\n");
  for (int i = 0; i < COUNTER_COUNT; i++) {
    CounterConfig cfg;
    if (counter_engine_get_config(i + 1, &cfg) && cfg.enabled) {
      uint64_t val = counter_engine_get_value(i + 1);
      PROM_APPEND("counter_value{id=\"%d\"} %llu\n", i + 1, (unsigned long long)val);
      uint16_t hz = counter_frequency_get(i + 1);
      PROM_APPEND("counter_frequency_hz{id=\"%d\"} %u\n", i + 1, (unsigned)hz);
    }
  }

  // --- Timer metrics (expanded) ---
  PROM_APPEND("# HELP timer_output Current timer output coil state (1=on, 0=off)\n");
  PROM_APPEND("# TYPE timer_output gauge\n");
  PROM_APPEND("# HELP timer_is_running Timer active state (1=running, 0=stopped)\n");
  PROM_APPEND("# TYPE timer_is_running gauge\n");
  PROM_APPEND("# HELP timer_current_phase Timer current phase (0-3)\n");
  PROM_APPEND("# TYPE timer_current_phase gauge\n");
  for (int i = 0; i < TIMER_COUNT; i++) {
    TimerConfig cfg;
    if (timer_engine_get_config(i + 1, &cfg) && cfg.enabled) {
      uint8_t coil_val = registers_get_coil(cfg.output_coil);
      PROM_APPEND("timer_output{id=\"%d\"} %d\n", i + 1, coil_val ? 1 : 0);
      uint8_t phase = 0, active = 0;
      timer_engine_get_runtime(i + 1, &phase, &active);
      PROM_APPEND("timer_is_running{id=\"%d\"} %d\n", i + 1, active ? 1 : 0);
      PROM_APPEND("timer_current_phase{id=\"%d\"} %d\n", i + 1, phase);
    }
  }

  // --- ST Logic metrics ---
  st_logic_engine_state_t *logic_state = st_logic_get_state();
  if (logic_state) {
    PROM_APPEND("# HELP st_logic_enabled ST Logic engine global enabled state\n");
    PROM_APPEND("# TYPE st_logic_enabled gauge\n");
    PROM_APPEND("st_logic_enabled %d\n", logic_state->enabled ? 1 : 0);

    PROM_APPEND("# HELP st_logic_total_cycles Total ST Logic execution cycles\n");
    PROM_APPEND("# TYPE st_logic_total_cycles counter\n");
    PROM_APPEND("st_logic_total_cycles %lu\n", (unsigned long)logic_state->total_cycles);

    PROM_APPEND("# HELP st_logic_cycle_overruns Total cycle overruns (cycle > interval)\n");
    PROM_APPEND("# TYPE st_logic_cycle_overruns counter\n");
    PROM_APPEND("st_logic_cycle_overruns %lu\n", (unsigned long)logic_state->cycle_overrun_count);

    PROM_APPEND("# HELP st_logic_execution_count Program execution count\n");
    PROM_APPEND("# TYPE st_logic_execution_count counter\n");
    PROM_APPEND("# HELP st_logic_error_count Program error count\n");
    PROM_APPEND("# TYPE st_logic_error_count counter\n");
    PROM_APPEND("# HELP st_logic_exec_time_us Last execution time in microseconds\n");
    PROM_APPEND("# TYPE st_logic_exec_time_us gauge\n");
    PROM_APPEND("# HELP st_logic_min_exec_us Minimum execution time in microseconds\n");
    PROM_APPEND("# TYPE st_logic_min_exec_us gauge\n");
    PROM_APPEND("# HELP st_logic_max_exec_us Maximum execution time in microseconds\n");
    PROM_APPEND("# TYPE st_logic_max_exec_us gauge\n");
    PROM_APPEND("# HELP st_logic_overrun_count Program overrun count\n");
    PROM_APPEND("# TYPE st_logic_overrun_count counter\n");

    for (int i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
      st_logic_program_config_t *prog = st_logic_get_program(logic_state, i);
      if (prog && prog->enabled) {
        PROM_APPEND("st_logic_execution_count{slot=\"%d\",name=\"%s\"} %u\n",
                     i + 1, prog->name, (unsigned)prog->execution_count);
        PROM_APPEND("st_logic_error_count{slot=\"%d\",name=\"%s\"} %u\n",
                     i + 1, prog->name, (unsigned)prog->error_count);
        PROM_APPEND("st_logic_exec_time_us{slot=\"%d\",name=\"%s\"} %lu\n",
                     i + 1, prog->name, (unsigned long)prog->last_execution_us);
        PROM_APPEND("st_logic_min_exec_us{slot=\"%d\",name=\"%s\"} %lu\n",
                     i + 1, prog->name, (unsigned long)prog->min_execution_us);
        PROM_APPEND("st_logic_max_exec_us{slot=\"%d\",name=\"%s\"} %lu\n",
                     i + 1, prog->name, (unsigned long)prog->max_execution_us);
        PROM_APPEND("st_logic_overrun_count{slot=\"%d\",name=\"%s\"} %lu\n",
                     i + 1, prog->name, (unsigned long)prog->overrun_count);
      }
    }
  }

#ifdef SHIFT_REGISTER_ENABLED
  // --- GPIO Digital Input metrics (74HC165, GPIO 101-108) ---
  PROM_APPEND("# HELP gpio_digital_input Digital input state (1=high, 0=low)\n");
  PROM_APPEND("# TYPE gpio_digital_input gauge\n");
  for (int i = 0; i < VGPIO_SR_INPUT_COUNT; i++) {
    uint8_t pin = VGPIO_SR_INPUT_BASE + i;
    PROM_APPEND("gpio_digital_input{pin=\"%d\"} %d\n", pin, gpio_read(pin) ? 1 : 0);
  }

  // --- GPIO Digital Output metrics (74HC595, GPIO 201-208) ---
  PROM_APPEND("# HELP gpio_digital_output Digital output state (1=high, 0=low)\n");
  PROM_APPEND("# TYPE gpio_digital_output gauge\n");
  for (int i = 0; i < VGPIO_SR_OUTPUT_COUNT; i++) {
    uint8_t pin = VGPIO_SR_OUTPUT_BASE + i;
    PROM_APPEND("gpio_digital_output{pin=\"%d\"} %d\n", pin, gpio_read(pin) ? 1 : 0);
  }
#endif

#if defined(ANALOG_IO_ENABLED)
  // --- Analog I/O metrics (FEAT-034/035/036/037) — vaerdi som ×100 fixed-point ---
  {
    static const char *V_NAMES[4] = { "vi1", "vi2", "vi3", "vi4" };
    static const char *I_NAMES[4] = { "ii1", "ii2", "ii3", "ii4" };
    static const char *AO_NAMES[2] = { "ao1", "ao2" };

    PROM_APPEND("# HELP analog_input_value Kalibreret analog indgangsvaerdi (×100, fx 500=5.00V/mA)\n");
    PROM_APPEND("# TYPE analog_input_value gauge\n");
    for (int i = 0; i < 4; i++) {
      if (!g_persist_config.analog_ai_v[i].enabled) continue;
      PROM_APPEND("analog_input_value{channel=\"%s\",type=\"voltage\"} %d\n",
                   V_NAMES[i], (int)registers_get_holding_register(g_persist_config.analog_ai_v[i].value_reg));
    }
    for (int i = 0; i < 4; i++) {
      if (!g_persist_config.analog_ai_i[i].enabled) continue;
      PROM_APPEND("analog_input_value{channel=\"%s\",type=\"current\"} %d\n",
                   I_NAMES[i], (int)registers_get_holding_register(g_persist_config.analog_ai_i[i].value_reg));
    }

    PROM_APPEND("# HELP analog_output_setpoint Analog udgangs-setpoint (×100)\n");
    PROM_APPEND("# TYPE analog_output_setpoint gauge\n");
    for (int i = 0; i < 2; i++) {
      if (!g_persist_config.analog_ao[i].enabled) continue;
      PROM_APPEND("analog_output_setpoint{channel=\"%s\"} %d\n",
                   AO_NAMES[i], (int)registers_get_holding_register(g_persist_config.analog_ao[i].value_reg));
    }
  }
#endif

  // --- Persistence Group metrics ---
  PersistentRegisterData *pr = &g_persist_config.persist_regs;
  if (pr->enabled && pr->group_count > 0) {
    PROM_APPEND("# HELP persist_group_reg_count Number of registers in persistence group\n");
    PROM_APPEND("# TYPE persist_group_reg_count gauge\n");
    PROM_APPEND("# HELP persist_group_last_save_ms Last save timestamp (ms since boot)\n");
    PROM_APPEND("# TYPE persist_group_last_save_ms gauge\n");
    for (int i = 0; i < pr->group_count && i < PERSIST_MAX_GROUPS; i++) {
      PersistGroup *grp = &pr->groups[i];
      PROM_APPEND("persist_group_reg_count{group=\"%s\"} %d\n", grp->name, grp->reg_count);
      PROM_APPEND("persist_group_last_save_ms{group=\"%s\"} %lu\n", grp->name, (unsigned long)grp->last_save_ms);
    }
  }

  // --- Watchdog metrics ---
  WatchdogState *wd = watchdog_get_state();
  if (wd) {
    PROM_APPEND("# HELP watchdog_reboot_count Total reboots tracked by watchdog\n");
    PROM_APPEND("# TYPE watchdog_reboot_count counter\n");
    PROM_APPEND("watchdog_reboot_count %lu\n", wd->reboot_counter);

    PROM_APPEND("# HELP watchdog_reset_reason Last reset reason (ESP_RST enum)\n");
    PROM_APPEND("# TYPE watchdog_reset_reason gauge\n");
    PROM_APPEND("watchdog_reset_reason %lu\n", wd->last_reset_reason);
  }

  // --- FreeRTOS task metrics ---
  {
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    PROM_APPEND("# HELP freertos_task_count Number of FreeRTOS tasks\n");
    PROM_APPEND("# TYPE freertos_task_count gauge\n");
    PROM_APPEND("freertos_task_count %u\n", (unsigned)task_count);

    // Report stack HWM for known tasks by handle
    PROM_APPEND("# HELP freertos_task_stack_hwm Task stack high-water mark in bytes\n");
    PROM_APPEND("# TYPE freertos_task_stack_hwm gauge\n");

    // Main loop task (current task on Core 1)
    TaskHandle_t cur = xTaskGetCurrentTaskHandle();
    if (cur) {
      PROM_APPEND("freertos_task_stack_hwm{task=\"loopTask\"} %lu\n",
                   (unsigned long)(uxTaskGetStackHighWaterMark(cur) * 4));
    }

    // Async Modbus Master task
    if (mb_async && mb_async->task_handle) {
      PROM_APPEND("freertos_task_stack_hwm{task=\"mb_async\"} %lu\n",
                   (unsigned long)(uxTaskGetStackHighWaterMark(mb_async->task_handle) * 4));
    }

    // IDLE tasks (core 0 and core 1)
    TaskHandle_t idle0 = xTaskGetIdleTaskHandleForCPU(0);
    TaskHandle_t idle1 = xTaskGetIdleTaskHandleForCPU(1);
    if (idle0) {
      PROM_APPEND("freertos_task_stack_hwm{task=\"IDLE0\"} %lu\n",
                   (unsigned long)(uxTaskGetStackHighWaterMark(idle0) * 4));
    }
    if (idle1) {
      PROM_APPEND("freertos_task_stack_hwm{task=\"IDLE1\"} %lu\n",
                   (unsigned long)(uxTaskGetStackHighWaterMark(idle1) * 4));
    }
  }

  // --- Firmware info ---
  PROM_APPEND("# HELP firmware_info Firmware version info\n");
  PROM_APPEND("# TYPE firmware_info gauge\n");
  PROM_APPEND("firmware_info{version=\"%s\",build=\"%d\"} 1\n", PROJECT_VERSION, BUILD_NUMBER);

  // --- NTP metrics ---
  PROM_APPEND("# HELP ntp_enabled NTP enabled (1=yes, 0=no)\n");
  PROM_APPEND("# TYPE ntp_enabled gauge\n");
  PROM_APPEND("ntp_enabled %d\n", g_persist_config.ntp.enabled ? 1 : 0);

  PROM_APPEND("# HELP ntp_synced NTP time synchronized (1=yes, 0=no)\n");
  PROM_APPEND("# TYPE ntp_synced gauge\n");
  PROM_APPEND("ntp_synced %d\n", ntp_driver_is_synced() ? 1 : 0);

  PROM_APPEND("# HELP ntp_sync_count Total NTP synchronizations since boot\n");
  PROM_APPEND("# TYPE ntp_sync_count counter\n");
  PROM_APPEND("ntp_sync_count %lu\n", (unsigned long)ntp_driver_get_sync_count());

  if (ntp_driver_is_synced()) {
    PROM_APPEND("# HELP ntp_epoch_seconds Current epoch time\n");
    PROM_APPEND("# TYPE ntp_epoch_seconds gauge\n");
    PROM_APPEND("ntp_epoch_seconds %lu\n", (unsigned long)ntp_driver_get_epoch());

    PROM_APPEND("# HELP ntp_last_sync_age_ms Milliseconds since last sync\n");
    PROM_APPEND("# TYPE ntp_last_sync_age_ms gauge\n");
    PROM_APPEND("ntp_last_sync_age_ms %lu\n", (unsigned long)ntp_driver_get_last_sync_age_ms());
  }

  // --- Alarm log metrics ---
  alarm_check_thresholds();
  PROM_APPEND("# HELP alarm_log_count Total alarm entries in log\n");
  PROM_APPEND("# TYPE alarm_log_count gauge\n");
  PROM_APPEND("alarm_log_count %d\n", alarm_log_count);

  uint8_t unack = 0;
  for (int i = 0; i < alarm_log_count; i++) {
    int idx = (alarm_log_head - alarm_log_count + i + ALARM_LOG_MAX) % ALARM_LOG_MAX;
    if (!alarm_log[idx].acknowledged) unack++;
  }
  PROM_APPEND("# HELP alarm_unacknowledged_count Unacknowledged alarms\n");
  PROM_APPEND("# TYPE alarm_unacknowledged_count gauge\n");
  PROM_APPEND("alarm_unacknowledged_count %d\n", unack);

  // --- FEAT-086/089: System event / register change log metrics ---
  PROM_APPEND("# HELP syslog_count Total entries in haendelses-/registerandringslog\n");
  PROM_APPEND("# TYPE syslog_count gauge\n");
  PROM_APPEND("syslog_count %u\n", (unsigned)system_log_count());
  PROM_APPEND("# HELP syslog_enabled Om haendelseslog aktivt logger (1) eller er stoppet (0)\n");
  PROM_APPEND("# TYPE syslog_enabled gauge\n");
  PROM_APPEND("syslog_enabled %d\n", system_log_is_enabled() ? 1 : 0);

  // --- Modbus Register metrics (non-zero holding & input registers) ---
  // BUG-358: flyttet hertil (var foer analog/gpio-metrics, langt tidligere i
  // funktionen) — denne dump er UBEGRAeNSET i stoerrelse (op til 256+256
  // linjer, ~21KB i vaerste fald hvis alle registre er non-zero), og
  // PROM_APPEND dropper stille alt der ikke er plads til i bufferen naar
  // `remaining` er brugt op (se advarslen ved malloc() ovenfor). Alt der
  // staar FOeR dette punkt i funktionen (NTP/alarm/syslog/firmware-status
  // osv., som dashboardets badges laeser) er nu ALTID skrevet foerst og
  // dermed sikret uanset hvor mange registre der er non-zero. Tilfoej ALDRIG
  // nye bulk/ubegraensede loekker foer dette punkt — kun faste, faa metrics.
  PROM_APPEND("# HELP modbus_holding_register Modbus holding register value\n");
  PROM_APPEND("# TYPE modbus_holding_register gauge\n");
  for (int addr = 0; addr < HOLDING_REGS_SIZE; addr++) {
    uint16_t val = registers_get_holding_register(addr);
    if (val != 0) {
      PROM_APPEND("modbus_holding_register{addr=\"%d\"} %u\n", addr, (unsigned)val);
    }
  }

  PROM_APPEND("# HELP modbus_input_register Modbus input register value\n");
  PROM_APPEND("# TYPE modbus_input_register gauge\n");
  for (int addr = 0; addr < INPUT_REGS_SIZE; addr++) {
    uint16_t val = registers_get_input_register(addr);
    if (val != 0) {
      PROM_APPEND("modbus_input_register{addr=\"%d\"} %u\n", addr, (unsigned)val);
    }
  }

  #undef PROM_APPEND

  // Send as text/plain (Prometheus format)
  httpd_resp_set_type(req, "text/plain; version=0.0.4; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_sendstr(req, buf);
  free(buf);

  http_server_stat_success();
  return ESP_OK;
}

/* ============================================================================
 * FEAT-085: Alarm History API (v7.8.0)
 * GET  /api/alarms         — Return alarm log as JSON array
 * POST /api/alarms/ack     — Acknowledge all alarms
 * ============================================================================ */

esp_err_t api_handler_alarms_get(httpd_req_t *req)
{
  http_server_stat_request();
  // SECURITY_INDEX #12: this used to be CHECK_API_ENABLED-only (no auth at
  // all) — leaked source-IP/username of failed login attempts to anyone
  // unauthenticated. Matches api_handler_alarms_ack's existing auth level
  // in spirit (that one already correctly required CHECK_AUTH_WRITE).
  CHECK_AUTH(req);

  DynamicJsonDocument doc(6144);
  JsonArray arr = doc.to<JsonArray>();

  // Output oldest first
  for (int i = 0; i < alarm_log_count; i++) {
    int idx = (alarm_log_head - alarm_log_count + i + ALARM_LOG_MAX) % ALARM_LOG_MAX;
    const alarm_entry_t *e = &alarm_log[idx];
    JsonObject obj = arr.createNestedObject();
    obj["timestamp_ms"] = e->timestamp_ms;
    obj["message"] = e->message;
    obj["severity"] = e->severity;
    obj["acknowledged"] = e->acknowledged;

    // Format uptime for readability
    uint32_t s = e->timestamp_ms / 1000;
    char uptime[32];
    snprintf(uptime, sizeof(uptime), "%lud %02lu:%02lu:%02lu",
             (unsigned long)(s / 86400), (unsigned long)((s % 86400) / 3600),
             (unsigned long)((s % 3600) / 60), (unsigned long)(s % 60));
    obj["uptime"] = uptime;

    // Add real-time timestamp if available (uses NTP timezone via localtime_r)
    if (e->epoch > 0) {
      obj["epoch"] = (unsigned long)e->epoch;
      struct tm timeinfo;
      localtime_r(&e->epoch, &timeinfo);
      char timebuf[24];
      strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &timeinfo);
      obj["time"] = timebuf;
    }

    // Add source details if available (e.g. auth failure context)
    if (e->source_ip[0]) {
      obj["source_ip"] = e->source_ip;
    }
    if (e->username[0]) {
      obj["username"] = e->username;
    }
  }

  char *buf = (char *)malloc(6144);
  if (!buf) return api_send_error(req, 500, "Out of memory");
  serializeJson(doc, buf, 6144);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_sendstr(req, buf);
  free(buf);

  http_server_stat_success();
  return ESP_OK;
}

esp_err_t api_handler_alarms_ack(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);
  if (!http_rate_limit_check(req)) {
    return api_send_error(req, 429, "Too many requests");
  }

  // FEAT-154: i modsaetning til de oevrige loekker er denne IKKE afgraenset
  // af alarm_log_count, saa den skal selv tjekke at bufferen findes.
  if (alarm_log) {
    for (int i = 0; i < ALARM_LOG_MAX; i++) {
      alarm_log[i].acknowledged = true;
    }
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"All alarms acknowledged\"}");

  http_server_stat_success();
  return ESP_OK;
}

/* ============================================================================
 * FEAT-149: Modbus Activity Log API
 * GET  /api/modbus/activity        — Return wire-level Master+Slave log (RAM-only)
 * POST /api/modbus/activity/clear  — Clear the log
 * ============================================================================ */

esp_err_t api_handler_modbus_activity_get(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_API_ENABLED(req);
  if (!http_rate_limit_check(req)) {
    return api_send_error(req, 429, "Too many requests");
  }

  uint16_t n = mb_activity_log_count();

  /* FEAT-153: ?limit=N — returnér kun de NYESTE N poster.
   * Dashboardet poller hvert 3. sekund og viser som standard 100 linjer;
   * uden dette ville hver polling traekke hele loggen (op til 500 poster,
   * ~70 KB) over WiFi 20 gange i minuttet. Eksport-knappen henter derimod
   * det hele ved at udelade limit. */
  uint16_t first = 0;
  {
    char q[64];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
      char val[12];
      if (httpd_query_key_value(q, "limit", val, sizeof(val)) == ESP_OK) {
        long lim = strtol(val, NULL, 10);
        if (lim > 0 && lim < (long)n) {
          first = (uint16_t)(n - lim);  // spring de aeldste over
        }
      }
    }
  }

  /* FEAT-153: svaret bygges og sendes nu i CHUNKS i stedet for at blive
   * samlet i én stor buffer foerst.
   *
   * Baggrund: BUG-332 opstod fordi outputtet blev skrevet i en fast, for
   * lille buffer og blev afkortet midt i et JSON-objekt — dashboardet
   * stoppede saa bare med at opdatere. Det blev loest med measureJson(),
   * men det kraevede stadig TO store samtidige allokeringer (ArduinoJson-
   * dokumentet + output-bufferen). Da loggen nu rummer 100 poster i stedet
   * for 40 (~14 KB output) ville det blive ~40 KB heap i spidsbelastning paa
   * en enhed med ~108 KB fri — unoedigt skroebeligt, saerligt ved
   * fragmentering. Chunked afsendelse bruger kun én lille stak-buffer pr.
   * post og skalerer derfor uanset logstoerrelse.
   *
   * Svarformatet er samtidig udvidet med logningens til/fra-tilstand. For
   * ikke at braekke eksisterende forbrugere sendes posterne fortsat under
   * "entries", og dashboardet haandterer baade det gamle (bar array) og det
   * nye format. */
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char head[128];
  snprintf(head, sizeof(head),
           "{\"logging\":%s,\"capacity\":%d,\"total\":%u,\"entries\":[",
           mb_activity_log_is_enabled() ? "true" : "false",
           (int)MB_ACTIVITY_LOG_MAX, (unsigned)n);
  httpd_resp_send_chunk(req, head, HTTPD_RESP_USE_STRLEN);

  // Output oldest first (matches alarm log convention)
  char item[256];
  for (uint16_t i = first; i < n; i++) {
    mb_activity_entry_t e;
    if (!mb_activity_log_get(i, &e)) break;

    const char *src = "unknown";
    switch (e.source) {
      case MB_SRC_ST_LOGIC:  src = "st_logic"; break;
      case MB_SRC_CLI:       src = "cli"; break;
      case MB_SRC_DASHBOARD: src = "dashboard"; break;
      case MB_SRC_EXTERNAL:  src = "external"; break;
      default:               break;
    }

    snprintf(item, sizeof(item),
      "%s{\"timestamp_ms\":%lu,\"epoch_s\":%lu,\"role\":\"%s\",\"source\":\"%s\",\"slave_id\":%u,"
      "\"fc\":%u,\"address\":%u,\"count\":%u,\"value\":%ld,\"error\":%d,\"success\":%s}",
      (i == first) ? "" : ",",
      (unsigned long)e.timestamp_ms,
      (unsigned long)e.epoch_s,
      (e.role == MB_ACTIVITY_ROLE_MASTER) ? "master" : "slave",
      src,
      (unsigned)e.slave_id,
      (unsigned)e.function_code,
      (unsigned)e.address,
      (unsigned)e.count,
      (long)e.value,
      (int)e.error,
      (e.error == 0) ? "true" : "false");

    httpd_resp_send_chunk(req, item, HTTPD_RESP_USE_STRLEN);
  }

  httpd_resp_send_chunk(req, "]}", HTTPD_RESP_USE_STRLEN);
  httpd_resp_send_chunk(req, NULL, 0);  // afslut chunked svar

  http_server_stat_success();
  return ESP_OK;
}

/* FEAT-153: POST /api/modbus/activity/start | /stop — start/stop logning
 * uden at rydde det allerede opsamlede. */
esp_err_t api_handler_modbus_activity_toggle(httpd_req_t *req, bool enable)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);
  if (!http_rate_limit_check(req)) {
    return api_send_error(req, 429, "Too many requests");
  }

  mb_activity_log_set_enabled(enable);

  char resp[96];
  snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"logging\":%s}",
           enable ? "true" : "false");

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_sendstr(req, resp);

  http_server_stat_success();
  return ESP_OK;
}

esp_err_t api_handler_modbus_activity_clear(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);
  if (!http_rate_limit_check(req)) {
    return api_send_error(req, 429, "Too many requests");
  }

  mb_activity_log_clear();

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Activity log cleared\"}");

  http_server_stat_success();
  return ESP_OK;
}

/* ============================================================================
 * FEAT-034/035/036/037: Analog I/O API (ES32D26 only)
 *
 *   GET  /api/analog        — alle 10 kanaler (config + live vaerdi)
 *   POST /api/analog        — opdatér én kanal (body: channel + felter)
 * ============================================================================ */

#if defined(ANALOG_IO_ENABLED)
static void analog_add_ai(JsonArray &arr, const char *ch, const AnalogInputConfig *cfg, bool adc2) {
  JsonObject o = arr.add<JsonObject>();
  o["channel"] = ch;
  o["enabled"] = cfg->enabled ? true : false;
  o["adc2"] = adc2;
  bool blocked = adc2 && cfg->enabled && wifi_driver_is_connected();
  o["wifi_blocked"] = blocked;
  o["raw_mv"] = blocked ? -1 : (int)registers_get_holding_register(cfg->raw_reg);
  o["value"] = blocked ? -1 : (int)registers_get_holding_register(cfg->value_reg);  // ×100 fixed-point
  o["scale"] = cfg->scale;
  o["offset"] = cfg->offset;
  o["raw_reg"] = cfg->raw_reg;
  o["value_reg"] = cfg->value_reg;
}
#endif

esp_err_t api_handler_analog_get(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

#if !defined(ANALOG_IO_ENABLED)
  return api_send_error(req, 404, "Analog I/O not supported on this board");
#else
  JsonDocument doc;
  JsonArray ai_v = doc["ai_voltage"].to<JsonArray>();
  static const char *V_NAMES[4] = { "vi1", "vi2", "vi3", "vi4" };
  static const bool  V_ADC2[4]  = { true, false, true, false };
  for (int i = 0; i < 4; i++) {
    analog_add_ai(ai_v, V_NAMES[i], &g_persist_config.analog_ai_v[i], V_ADC2[i]);
  }

  JsonArray ai_i = doc["ai_current"].to<JsonArray>();
  static const char *I_NAMES[4] = { "ii1", "ii2", "ii3", "ii4" };
  for (int i = 0; i < 4; i++) {
    analog_add_ai(ai_i, I_NAMES[i], &g_persist_config.analog_ai_i[i], false);
  }

  JsonArray ao = doc["ao"].to<JsonArray>();
  static const char *AO_NAMES[2] = { "ao1", "ao2" };
  for (int i = 0; i < 2; i++) {
    const AnalogOutputConfig *cfg = &g_persist_config.analog_ao[i];
    uint8_t mode = (i == 0) ? g_persist_config.ao1_mode : g_persist_config.ao2_mode;
    JsonObject o = ao.add<JsonObject>();
    o["channel"] = AO_NAMES[i];
    o["enabled"] = cfg->enabled ? true : false;
    o["mode"] = (mode == AO_MODE_CURRENT) ? "current" : "voltage";
    o["setpoint"] = (int)registers_get_holding_register(cfg->value_reg);  // ×100 fixed-point
    o["scale"] = cfg->scale;
    o["offset"] = cfg->offset;
    o["value_reg"] = cfg->value_reg;
  }

  char buf[2048];
  serializeJson(doc, buf, sizeof(buf));
  return api_send_json(req, buf);
#endif
}

esp_err_t api_handler_analog_post(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

#if !defined(ANALOG_IO_ENABLED)
  return api_send_error(req, 404, "Analog I/O not supported on this board");
#else
  char body[256];
  int blen = httpd_req_recv(req, body, sizeof(body) - 1);
  if (blen <= 0) return api_send_error(req, 400, "Empty body");
  body[blen] = '\0';

  JsonDocument jdoc;
  if (deserializeJson(jdoc, body)) return api_send_error(req, 400, "Invalid JSON");

  const char *ch = jdoc["channel"] | "";
  bool *p_enabled = NULL;
  float *p_scale = NULL;
  float *p_offset = NULL;
  uint8_t *p_mode = NULL;  // FEAT: GUI-oprydning — kun sat for ao1/ao2

  for (int i = 0; i < 4; i++) {
    char name[5];
    snprintf(name, sizeof(name), "vi%d", i + 1);
    if (strcasecmp(ch, name) == 0) {
      p_enabled = &g_persist_config.analog_ai_v[i].enabled;
      p_scale = &g_persist_config.analog_ai_v[i].scale;
      p_offset = &g_persist_config.analog_ai_v[i].offset;
    }
    snprintf(name, sizeof(name), "ii%d", i + 1);
    if (strcasecmp(ch, name) == 0) {
      p_enabled = &g_persist_config.analog_ai_i[i].enabled;
      p_scale = &g_persist_config.analog_ai_i[i].scale;
      p_offset = &g_persist_config.analog_ai_i[i].offset;
    }
  }
  for (int i = 0; i < 2; i++) {
    char name[5];
    snprintf(name, sizeof(name), "ao%d", i + 1);
    if (strcasecmp(ch, name) == 0) {
      p_enabled = &g_persist_config.analog_ao[i].enabled;
      p_scale = &g_persist_config.analog_ao[i].scale;
      p_offset = &g_persist_config.analog_ao[i].offset;
      p_mode = (i == 0) ? &g_persist_config.ao1_mode : &g_persist_config.ao2_mode;
    }
  }

  if (!p_enabled) {
    return api_send_error(req, 400, "Unknown channel (use vi1-4, ii1-4, ao1-2)");
  }

  if (jdoc.containsKey("enabled")) *p_enabled = jdoc["enabled"].as<bool>();
  if (jdoc.containsKey("scale"))   *p_scale   = jdoc["scale"].as<float>();
  if (jdoc.containsKey("offset"))  *p_offset  = jdoc["offset"].as<float>();
  // FEAT: GUI-oprydning — AO1/AO2 mode (0=voltage/0-10V, 1=current/4-20mA),
  // tidligere kun saetbar via en fuld config-restore (BUGS_INDEX.md).
  if (jdoc.containsKey("mode") && p_mode) {
    const char *m = jdoc["mode"].as<const char*>();
    if (m && strcasecmp(m, "current") == 0) *p_mode = AO_MODE_CURRENT;
    else if (m && strcasecmp(m, "voltage") == 0) *p_mode = AO_MODE_VOLTAGE;
  }

  // Setpoint er RUNTIME data (som en counters vaerdi), ikke persisteret config
  // — skriv direkte til holding-registret, virker med det samme paa naeste
  // analog_driver_flush_outputs() (loop()). Kun relevant for AO-kanaler.
  if (jdoc.containsKey("setpoint")) {
    bool is_ao = false;
    uint16_t reg = 0;
    for (int i = 0; i < 2; i++) {
      char name[5];
      snprintf(name, sizeof(name), "ao%d", i + 1);
      if (strcasecmp(ch, name) == 0) { is_ao = true; reg = g_persist_config.analog_ao[i].value_reg; }
    }
    if (!is_ao) {
      return api_send_error(req, 400, "'setpoint' gaelder kun AO-kanaler (ao1/ao2)");
    }
    float sp = jdoc["setpoint"].as<float>();
    if (sp < -327.0f || sp > 327.0f) {
      return api_send_error(req, 400, "setpoint uden for gyldigt omraade");
    }
    registers_set_holding_register(reg, (uint16_t)lroundf(sp * 100.0f));
  }

  return api_send_json(req, "{\"status\":\"ok\",\"note\":\"enabled kraever save+reboot; scale/offset/setpoint virker straks\"}");
#endif
}

/* ============================================================================
 * FEAT-086/089: Haendelses- og registerandringslog (system_log.h)
 *
 *   GET  /api/syslog        — alle entries (?limit=N, ?category=event|regchange)
 *   POST /api/syslog/clear  — ryd loggen
 *   POST /api/syslog/start  — genoptag logning
 *   POST /api/syslog/stop   — stop logning (bevar indhold)
 * ============================================================================ */

esp_err_t api_handler_syslog_get(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  uint16_t n = system_log_count();

  // ?category= filter (samme moenster som ?limit=)
  int category_filter = -1;  // -1 = alle
  char q[64];
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
    char val[16];
    if (httpd_query_key_value(q, "category", val, sizeof(val)) == ESP_OK) {
      if (!strcasecmp(val, "event")) category_filter = SYSLOG_CAT_EVENT;
      else if (!strcasecmp(val, "regchange")) category_filter = SYSLOG_CAT_REG_CHANGE;
    }
  }

  // ?limit=N — kun de NYESTE N (matcher FEAT-153's moenster for /api/modbus/activity)
  uint16_t first = 0;
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
    char val[12];
    if (httpd_query_key_value(q, "limit", val, sizeof(val)) == ESP_OK) {
      long lim = strtol(val, NULL, 10);
      if (lim > 0 && lim < (long)n) {
        first = (uint16_t)(n - lim);
      }
    }
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char head[96];
  snprintf(head, sizeof(head), "{\"logging\":%s,\"capacity\":%d,\"total\":%u,\"entries\":[",
           system_log_is_enabled() ? "true" : "false", (int)SYSTEM_LOG_MAX, (unsigned)n);
  httpd_resp_send_chunk(req, head, HTTPD_RESP_USE_STRLEN);

  char item[256];
  bool first_written = true;
  for (uint16_t i = first; i < n; i++) {
    syslog_entry_t e;
    if (!system_log_get(i, &e)) break;
    if (category_filter >= 0 && e.category != (uint8_t)category_filter) continue;

    static const char *SRC_NAMES[3] = { "rest", "modbus_slave", "system" };
    const char *src = (e.source < 3) ? SRC_NAMES[e.source] : "unknown";

    snprintf(item, sizeof(item),
      "%s{\"timestamp_ms\":%lu,\"epoch_s\":%lu,\"category\":\"%s\",\"source\":\"%s\","
      "\"username\":\"%s\",\"ip\":\"%s\",\"reg_addr\":%u,\"is_coil\":%s,"
      "\"old_value\":%ld,\"new_value\":%ld,\"message\":\"%s\"}",
      first_written ? "" : ",",
      (unsigned long)e.timestamp_ms, (unsigned long)e.epoch_s,
      (e.category == SYSLOG_CAT_EVENT) ? "event" : "regchange",
      src, e.username, e.ip, (unsigned)e.reg_addr, e.is_coil ? "true" : "false",
      (long)e.old_value, (long)e.new_value, e.message);
    httpd_resp_send_chunk(req, item, HTTPD_RESP_USE_STRLEN);
    first_written = false;
  }

  httpd_resp_send_chunk(req, "]}", HTTPD_RESP_USE_STRLEN);
  httpd_resp_send_chunk(req, NULL, 0);

  http_server_stat_success();
  return ESP_OK;
}

esp_err_t api_handler_syslog_clear(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);
  if (!http_rate_limit_check(req)) {
    return api_send_error(req, 429, "Too many requests");
  }
  system_log_clear();
  return api_send_json(req, "{\"status\":\"ok\",\"message\":\"Log ryddet\"}");
}

esp_err_t api_handler_syslog_toggle(httpd_req_t *req, bool enable)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);
  if (!http_rate_limit_check(req)) {
    return api_send_error(req, 429, "Too many requests");
  }
  system_log_set_enabled(enable);
  char resp[64];
  snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"logging\":%s}", enable ? "true" : "false");
  return api_send_json(req, resp);
}

// POST /api/syslog/{clear|start|stop} — ESP-IDF wildcard matcher matcher kun
// paa slutningen af en URI, saa /api/syslog/* registreres én gang og
// suffiksen dispatches manuelt her (samme moenster som /api/modbus/activity/*)
esp_err_t api_handler_syslog_post_dispatch(httpd_req_t *req)
{
  const char *uri = req->uri;
  if (strstr(uri, "/clear") != NULL) return api_handler_syslog_clear(req);
  if (strstr(uri, "/start") != NULL) return api_handler_syslog_toggle(req, true);
  if (strstr(uri, "/stop") != NULL)  return api_handler_syslog_toggle(req, false);
  return api_send_error(req, 404, "Ukendt /api/syslog-underrute (brug /clear, /start eller /stop)");
}

/* ============================================================================
 * FEAT-099: Trend Recorder API
 *
 *   GET  /api/trend/config  — nuvaerende watch-liste, interval, status
 *   POST /api/trend/config  — saet watch-liste + interval (stopper+rydder)
 *   POST /api/trend/start   — start optagelse (med eksisterende config)
 *   POST /api/trend/stop    — stop optagelse (data bevares)
 *   POST /api/trend/clear   — ryd data (config bevares)
 *   GET  /api/trend/data    — fuld sample-dump, chunked (samme BUG-332-lektie
 *                             som /api/modbus/activity — se den kommentar)
 *
 * Samme URI-suffix-dispatch-moenster som /api/syslog/* ovenfor.
 * ============================================================================ */

static const char *trend_reg_type_str(uint8_t t) {
  switch (t) {
    case TREND_REG_HR:   return "hr";
    case TREND_REG_IR:   return "ir";
    case TREND_REG_COIL: return "coil";
    case TREND_REG_DI:   return "di";
    default:             return "?";
  }
}
static bool trend_reg_type_parse(const char *s, uint8_t *out) {
  if (strcmp(s, "hr") == 0)   { *out = TREND_REG_HR; return true; }
  if (strcmp(s, "ir") == 0)   { *out = TREND_REG_IR; return true; }
  if (strcmp(s, "coil") == 0) { *out = TREND_REG_COIL; return true; }
  if (strcmp(s, "di") == 0)   { *out = TREND_REG_DI; return true; }
  return false;
}

esp_err_t api_handler_trend_config_get(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  trend_point_t points[TREND_MAX_POINTS];
  uint8_t n = trend_recorder_get_points(points, TREND_MAX_POINTS);

  JsonDocument doc;
  doc["recording"] = trend_recorder_is_recording();
  doc["interval_ms"] = trend_recorder_get_interval_ms();
  doc["sample_count"] = trend_recorder_count();
  doc["capacity"] = TREND_MAX_SAMPLES;
  JsonArray pts = doc["points"].to<JsonArray>();
  for (uint8_t i = 0; i < n; i++) {
    JsonObject p = pts.add<JsonObject>();
    p["type"] = trend_reg_type_str(points[i].reg_type);
    p["addr"] = points[i].addr;
  }

  char buf[512];
  serializeJson(doc, buf, sizeof(buf));
  return api_send_json(req, buf);
}

esp_err_t api_handler_trend_config_post(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  char content[512];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    return api_send_error(req, 400, "Failed to read request body");
  }
  content[ret] = '\0';

  JsonDocument doc;
  if (deserializeJson(doc, content)) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  uint16_t interval_ms = doc["interval_ms"] | 5000;

  trend_point_t points[TREND_MAX_POINTS];
  uint8_t count = 0;
  if (doc.containsKey("points")) {
    JsonArray arr = doc["points"];
    for (JsonObject p : arr) {
      if (count >= TREND_MAX_POINTS) {
        return api_send_error(req, 400, "Max 8 punkter tilladt");
      }
      const char *type_str = p["type"] | "";
      uint8_t reg_type;
      if (!trend_reg_type_parse(type_str, &reg_type)) {
        return api_send_error(req, 400, "Ugyldig 'type' (skal vaere hr/ir/coil/di)");
      }
      points[count].reg_type = reg_type;
      points[count].addr = p["addr"] | 0;
      count++;
    }
  }

  if (!trend_recorder_configure(points, count, interval_ms)) {
    return api_send_error(req, 400, "Kunne ikke konfigurere trend recorder");
  }

  char resp[128];
  snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"points\":%u,\"interval_ms\":%u}", count, trend_recorder_get_interval_ms());
  return api_send_json(req, resp);
}

esp_err_t api_handler_trend_start(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);
  trend_recorder_set_recording(true);
  return api_send_json(req, "{\"status\":\"ok\",\"recording\":true}");
}

esp_err_t api_handler_trend_stop(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);
  trend_recorder_set_recording(false);
  return api_send_json(req, "{\"status\":\"ok\",\"recording\":false}");
}

esp_err_t api_handler_trend_clear(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);
  trend_recorder_clear();
  return api_send_json(req, "{\"status\":\"ok\",\"message\":\"Data ryddet\"}");
}

esp_err_t api_handler_trend_data_get(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  trend_point_t points[TREND_MAX_POINTS];
  uint8_t point_count = trend_recorder_get_points(points, TREND_MAX_POINTS);
  uint16_t n = trend_recorder_count();

  // FEAT-149/BUG-332-lektie: chunked afsendelse, ikke én stor buffer — se
  // api_handler_modbus_activity_get()'s kommentar for hvorfor.
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char head[256];
  int pos = snprintf(head, sizeof(head), "{\"points\":[");
  for (uint8_t i = 0; i < point_count; i++) {
    pos += snprintf(head + pos, sizeof(head) - pos, "%s{\"type\":\"%s\",\"addr\":%u}",
                     (i == 0) ? "" : ",", trend_reg_type_str(points[i].reg_type), points[i].addr);
  }
  pos += snprintf(head + pos, sizeof(head) - pos, "],\"samples\":[");
  httpd_resp_send_chunk(req, head, HTTPD_RESP_USE_STRLEN);

  char item[256];
  for (uint16_t i = 0; i < n; i++) {
    trend_sample_t s;
    if (!trend_recorder_get(i, &s)) break;

    int p = snprintf(item, sizeof(item), "%s{\"t\":%lu,\"v\":[", (i == 0) ? "" : ",", (unsigned long)s.timestamp_ms);
    for (uint8_t k = 0; k < point_count; k++) {
      p += snprintf(item + p, sizeof(item) - p, "%s%ld", (k == 0) ? "" : ",", (long)s.values[k]);
    }
    p += snprintf(item + p, sizeof(item) - p, "]}");
    httpd_resp_send_chunk(req, item, HTTPD_RESP_USE_STRLEN);
  }

  httpd_resp_send_chunk(req, "]}", HTTPD_RESP_USE_STRLEN);
  httpd_resp_send_chunk(req, NULL, 0);  // end chunked response

  http_server_stat_success();
  return ESP_OK;
}

// POST /api/trend/{config|start|stop|clear}, GET /api/trend/{config|data} —
// samme wildcard-suffix-dispatch-moenster som /api/syslog/* ovenfor.
esp_err_t api_handler_trend_dispatch(httpd_req_t *req)
{
  const char *uri = req->uri;
  if (req->method == HTTP_GET) {
    if (strstr(uri, "/config") != NULL) return api_handler_trend_config_get(req);
    if (strstr(uri, "/data") != NULL)   return api_handler_trend_data_get(req);
  } else if (req->method == HTTP_POST) {
    if (strstr(uri, "/config") != NULL) return api_handler_trend_config_post(req);
    if (strstr(uri, "/start") != NULL)  return api_handler_trend_start(req);
    if (strstr(uri, "/stop") != NULL)   return api_handler_trend_stop(req);
    if (strstr(uri, "/clear") != NULL)  return api_handler_trend_clear(req);
  }
  return api_send_error(req, 404, "Ukendt /api/trend-underrute (brug /config, /start, /stop, /clear eller /data)");
}

/* ============================================================================
 * FEAT-033: Request Audit Log API
 *
 *   GET  /api/system/logs         — Liste over seneste API-requests
 *   POST /api/system/logs/clear   — Ryd loggen
 *
 * Samme moenster som /api/syslog (system_log.h) — se api_audit_log.h for
 * hvorfor loggen fyldes fra api_send_error()/api_send_json() i stedet for
 * fra hver enkelt handler.
 * ============================================================================ */

esp_err_t api_handler_audit_log_get(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  uint16_t n = api_audit_log_count();

  // ?limit=N — kun de NYESTE N (samme moenster som /api/syslog)
  uint16_t first = 0;
  char q[64];
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
    char val[12];
    if (httpd_query_key_value(q, "limit", val, sizeof(val)) == ESP_OK) {
      long lim = strtol(val, NULL, 10);
      if (lim > 0 && lim < (long)n) {
        first = (uint16_t)(n - lim);
      }
    }
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char head[96];
  snprintf(head, sizeof(head), "{\"logging\":%s,\"capacity\":%d,\"total\":%u,\"entries\":[",
           api_audit_log_is_enabled() ? "true" : "false", (int)API_AUDIT_LOG_MAX, (unsigned)n);
  httpd_resp_send_chunk(req, head, HTTPD_RESP_USE_STRLEN);

  char item[192];
  bool first_written = true;
  for (uint16_t i = first; i < n; i++) {
    api_audit_entry_t e;
    if (!api_audit_log_get(i, &e)) break;

    snprintf(item, sizeof(item),
      "%s{\"timestamp_ms\":%lu,\"epoch_s\":%lu,\"method\":\"%s\",\"path\":\"%s\","
      "\"status\":%u,\"ip\":\"%s\",\"username\":\"%s\"}",
      first_written ? "" : ",",
      (unsigned long)e.timestamp_ms, (unsigned long)e.epoch_s,
      e.method, e.path, (unsigned)e.status, e.ip, e.username);
    httpd_resp_send_chunk(req, item, HTTPD_RESP_USE_STRLEN);
    first_written = false;
  }

  httpd_resp_send_chunk(req, "]}", HTTPD_RESP_USE_STRLEN);
  httpd_resp_send_chunk(req, NULL, 0);

  http_server_stat_success();
  return ESP_OK;
}

esp_err_t api_handler_audit_log_clear(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);
  if (!http_rate_limit_check(req)) {
    return api_send_error(req, 429, "Too many requests");
  }
  api_audit_log_clear();
  return api_send_json(req, "{\"status\":\"ok\",\"message\":\"Audit-log ryddet\"}");
}

// POST /api/system/logs/clear — samme wildcard-suffiks-dispatch-moenster
// som /api/syslog/* (kun én underrute i dag, men holder samme struktur
// klar til flere hvis der senere tilfoejes fx start/stop-toggle)
esp_err_t api_handler_audit_log_post_dispatch(httpd_req_t *req)
{
  const char *uri = req->uri;
  if (strstr(uri, "/clear") != NULL) return api_handler_audit_log_clear(req);
  return api_send_error(req, 404, "Ukendt /api/system/logs-underrute (brug /clear)");
}

/* ============================================================================
 * FEAT-022: Persistence Group Management API (v7.0.4)
 *
 * REST endpoints for managing persistence groups:
 *   GET    /api/persist/groups         — List all groups
 *   GET    /api/persist/groups/*       — Get single group detail
 *   POST   /api/persist/groups/*       — Create/modify group
 *   DELETE /api/persist/groups/*       — Delete group
 *   POST   /api/persist/save           — Save group(s)
 *   POST   /api/persist/restore        — Restore group(s)
 * ============================================================================ */

esp_err_t api_handler_persist_groups_list(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  PersistentRegisterData *pr = &g_persist_config.persist_regs;

  char *buf = (char *)malloc(2048);
  if (!buf) return api_send_error(req, 500, "Out of memory");

  int pos = 0;
  pos += snprintf(buf + pos, 2048 - pos,
    "{\"enabled\":%s,\"group_count\":%d,\"max_groups\":%d,\"auto_load_enabled\":%s,\"groups\":[",
    pr->enabled ? "true" : "false",
    pr->group_count,
    PERSIST_MAX_GROUPS,
    pr->auto_load_enabled ? "true" : "false");

  for (int i = 0; i < pr->group_count && i < PERSIST_MAX_GROUPS; i++) {
    PersistGroup *grp = &pr->groups[i];
    if (i > 0) pos += snprintf(buf + pos, 2048 - pos, ",");
    pos += snprintf(buf + pos, 2048 - pos,
      "{\"id\":%d,\"name\":\"%s\",\"reg_count\":%d,\"max_regs\":%d,\"last_save_ms\":%lu}",
      i + 1, grp->name, grp->reg_count, PERSIST_GROUP_MAX_REGS,
      (unsigned long)grp->last_save_ms);
  }

  pos += snprintf(buf + pos, 2048 - pos, "]}");

  esp_err_t ret = api_send_json(req, buf);
  free(buf);
  return ret;
}

esp_err_t api_handler_persist_group_single(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  // Extract group name from URI: /api/persist/groups/<name>
  const char *prefix = "/api/persist/groups/";
  const char *uri = req->uri;
  if (strncmp(uri, prefix, strlen(prefix)) != 0) {
    return api_send_error(req, 400, "Invalid persist group URI");
  }
  const char *group_name = uri + strlen(prefix);

  if (strlen(group_name) == 0 || strlen(group_name) > 15) {
    return api_send_error(req, 400, "Invalid group name");
  }

  PersistGroup *grp = registers_persist_group_find(group_name);
  if (!grp) {
    return api_send_error(req, 404, "Group not found");
  }

  // Find group ID
  PersistentRegisterData *pr = &g_persist_config.persist_regs;
  int group_id = 0;
  for (int i = 0; i < pr->group_count; i++) {
    if (&pr->groups[i] == grp) { group_id = i + 1; break; }
  }

  char *buf = (char *)malloc(1024);
  if (!buf) return api_send_error(req, 500, "Out of memory");

  int pos = 0;
  pos += snprintf(buf + pos, 1024 - pos,
    "{\"id\":%d,\"name\":\"%s\",\"reg_count\":%d,\"max_regs\":%d,\"last_save_ms\":%lu,\"registers\":[",
    group_id, grp->name, grp->reg_count, PERSIST_GROUP_MAX_REGS,
    (unsigned long)grp->last_save_ms);

  for (int i = 0; i < grp->reg_count && i < PERSIST_GROUP_MAX_REGS; i++) {
    if (i > 0) pos += snprintf(buf + pos, 1024 - pos, ",");
    pos += snprintf(buf + pos, 1024 - pos,
      "{\"addr\":%d,\"saved_value\":%d,\"current_value\":%d}",
      grp->reg_addresses[i], grp->reg_values[i],
      registers_get_holding_register(grp->reg_addresses[i]));
  }

  pos += snprintf(buf + pos, 1024 - pos, "]}");

  esp_err_t ret = api_send_json(req, buf);
  free(buf);
  return ret;
}

esp_err_t api_handler_persist_group_post(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  // Extract group name from URI
  const char *prefix = "/api/persist/groups/";
  const char *uri = req->uri;
  if (strncmp(uri, prefix, strlen(prefix)) != 0) {
    return api_send_error(req, 400, "Invalid persist group URI");
  }
  const char *group_name = uri + strlen(prefix);

  if (strlen(group_name) == 0 || strlen(group_name) > 15) {
    return api_send_error(req, 400, "Invalid group name (max 15 chars)");
  }

  // Read body
  char content[512];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    return api_send_error(req, 400, "Failed to read request body");
  }
  content[ret] = '\0';

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, content);
  if (error) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  // Create group if doesn't exist
  PersistGroup *grp = registers_persist_group_find(group_name);
  if (!grp) {
    if (!registers_persist_group_create(group_name)) {
      return api_send_error(req, 409, "Cannot create group (max groups reached)");
    }
    grp = registers_persist_group_find(group_name);
    if (!grp) {
      return api_send_error(req, 500, "Group creation failed");
    }
  }

  // Add registers if specified: {"registers": [0, 1, 5, 10]}
  if (doc.containsKey("registers")) {
    JsonArray regs = doc["registers"].as<JsonArray>();
    int added = 0;
    for (JsonVariant v : regs) {
      uint16_t addr = v.as<uint16_t>();
      if (registers_persist_group_add_reg(group_name, addr)) {
        added++;
      }
    }
  }

  // Remove registers if specified: {"remove": [5, 10]}
  if (doc.containsKey("remove")) {
    JsonArray regs = doc["remove"].as<JsonArray>();
    for (JsonVariant v : regs) {
      uint16_t addr = v.as<uint16_t>();
      registers_persist_group_remove_reg(group_name, addr);
    }
  }

  // Enable persistence if not already
  if (!registers_persist_is_enabled()) {
    registers_persist_set_enabled(true);
  }

  // Return updated group
  return api_handler_persist_group_single(req);
}

esp_err_t api_handler_persist_group_delete(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  const char *prefix = "/api/persist/groups/";
  const char *uri = req->uri;
  if (strncmp(uri, prefix, strlen(prefix)) != 0) {
    return api_send_error(req, 400, "Invalid persist group URI");
  }
  const char *group_name = uri + strlen(prefix);

  if (!registers_persist_group_find(group_name)) {
    return api_send_error(req, 404, "Group not found");
  }

  if (!registers_persist_group_delete(group_name)) {
    return api_send_error(req, 500, "Failed to delete group");
  }

  char buf[128];
  snprintf(buf, sizeof(buf), "{\"status\":200,\"message\":\"Group '%s' deleted\"}", group_name);
  return api_send_json(req, buf);
}

esp_err_t api_handler_persist_save(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  char content[128];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    // No body = save all
    registers_persist_save_all_groups();
    g_persist_config.crc16 = config_calculate_crc16(&g_persist_config);
    if (config_save_to_nvs(&g_persist_config)) {
      return api_send_json(req, "{\"status\":200,\"message\":\"All groups saved to NVS\"}");
    }
    return api_send_error(req, 500, "NVS write failed");
  }
  content[ret] = '\0';

  JsonDocument doc;
  if (deserializeJson(doc, content)) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  // {"group": "name"} or {"group_id": 1} or {"all": true}
  if (doc.containsKey("group")) {
    const char *name = doc["group"].as<const char*>();
    if (!registers_persist_group_save(name)) {
      return api_send_error(req, 404, "Group not found");
    }
  } else if (doc.containsKey("group_id")) {
    uint8_t id = doc["group_id"].as<uint8_t>();
    if (!registers_persist_group_save_by_id(id)) {
      return api_send_error(req, 404, "Group not found");
    }
  } else {
    registers_persist_save_all_groups();
  }

  g_persist_config.crc16 = config_calculate_crc16(&g_persist_config);
  if (config_save_to_nvs(&g_persist_config)) {
    return api_send_json(req, "{\"status\":200,\"message\":\"Saved to NVS\"}");
  }
  return api_send_error(req, 500, "NVS write failed");
}

esp_err_t api_handler_persist_restore(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  char content[128];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    // No body = restore all
    if (registers_persist_restore_all_groups()) {
      return api_send_json(req, "{\"status\":200,\"message\":\"All groups restored\"}");
    }
    return api_send_error(req, 500, "Restore failed");
  }
  content[ret] = '\0';

  JsonDocument doc;
  if (deserializeJson(doc, content)) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  if (doc.containsKey("group")) {
    const char *name = doc["group"].as<const char*>();
    if (!registers_persist_group_restore(name)) {
      return api_send_error(req, 404, "Group not found");
    }
  } else if (doc.containsKey("group_id")) {
    uint8_t id = doc["group_id"].as<uint8_t>();
    if (!registers_persist_group_restore_by_id(id)) {
      return api_send_error(req, 404, "Group not found");
    }
  } else {
    if (!registers_persist_restore_all_groups()) {
      return api_send_error(req, 500, "Restore failed");
    }
  }

  return api_send_json(req, "{\"status\":200,\"message\":\"Restored\"}");
}

esp_err_t api_handler_persist_config_post(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  char content[128];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    return api_send_error(req, 400, "Failed to read request body");
  }
  content[ret] = '\0';

  JsonDocument doc;
  if (deserializeJson(doc, content)) {
    return api_send_error(req, 400, "Invalid JSON");
  }

  if (doc.containsKey("enabled")) {
    registers_persist_set_enabled(doc["enabled"].as<bool>());
  }
  if (doc.containsKey("auto_load_enabled")) {
    g_persist_config.persist_regs.auto_load_enabled = doc["auto_load_enabled"].as<bool>() ? 1 : 0;
  }

  char buf[128];
  snprintf(buf, sizeof(buf), "{\"status\":200,\"enabled\":%s,\"auto_load_enabled\":%s}",
    registers_persist_is_enabled() ? "true" : "false",
    g_persist_config.persist_regs.auto_load_enabled ? "true" : "false");
  return api_send_json(req, buf);
}

/* ============================================================================
 * FEAT-028: Request Rate Limiting (v7.0.4)
 *
 * Token bucket rate limiter per client IP.
 * Default: 30 requests/second burst, refill 10/sec.
 * Returns 429 Too Many Requests when exceeded.
 * ============================================================================ */

#define RATE_LIMIT_MAX_CLIENTS  8
#define RATE_LIMIT_BUCKET_SIZE  30   // Max burst
#define RATE_LIMIT_REFILL_RATE  10   // Tokens per second

typedef struct {
  uint32_t ip_addr;          // Client IP (network byte order)
  uint16_t tokens;           // Available tokens
  uint32_t last_refill_ms;   // Last token refill time
} RateLimitEntry;

static RateLimitEntry rate_limit_table[RATE_LIMIT_MAX_CLIENTS];
static bool rate_limit_enabled = true;

// Find or create entry for client IP
static RateLimitEntry* rate_limit_find(uint32_t ip)
{
  uint32_t now = millis();
  int oldest_idx = 0;
  uint32_t oldest_time = UINT32_MAX;

  for (int i = 0; i < RATE_LIMIT_MAX_CLIENTS; i++) {
    if (rate_limit_table[i].ip_addr == ip) {
      return &rate_limit_table[i];
    }
    if (rate_limit_table[i].last_refill_ms < oldest_time) {
      oldest_time = rate_limit_table[i].last_refill_ms;
      oldest_idx = i;
    }
  }

  // Not found — reuse oldest slot
  RateLimitEntry *e = &rate_limit_table[oldest_idx];
  e->ip_addr = ip;
  e->tokens = RATE_LIMIT_BUCKET_SIZE;
  e->last_refill_ms = now;
  return e;
}

// Check rate limit for a request. Returns true if allowed.
bool http_rate_limit_check(httpd_req_t *req)
{
  if (!rate_limit_enabled) return true;

  // Get client IP from socket
  int sockfd = httpd_req_to_sockfd(req);
  struct sockaddr_in addr;
  socklen_t addr_len = sizeof(addr);
  if (getpeername(sockfd, (struct sockaddr *)&addr, &addr_len) != 0) {
    return true;  // Can't get IP — allow
  }

  uint32_t ip = addr.sin_addr.s_addr;
  RateLimitEntry *e = rate_limit_find(ip);

  // Refill tokens based on elapsed time
  uint32_t now = millis();
  uint32_t elapsed_ms = now - e->last_refill_ms;
  if (elapsed_ms > 0) {
    uint32_t new_tokens = (elapsed_ms * RATE_LIMIT_REFILL_RATE) / 1000;
    if (new_tokens > 0) {
      e->tokens = (e->tokens + new_tokens > RATE_LIMIT_BUCKET_SIZE)
                    ? RATE_LIMIT_BUCKET_SIZE
                    : e->tokens + new_tokens;
      e->last_refill_ms = now;
    }
  }

  // Consume a token
  if (e->tokens > 0) {
    e->tokens--;
    return true;
  }

  return false;  // Rate limited
}

void http_rate_limit_set_enabled(bool enabled)
{
  rate_limit_enabled = enabled;
}

bool http_rate_limit_is_enabled(void)
{
  return rate_limit_enabled;
}

/* ============================================================================
 * FEAT: GUI-oprydning — GET/POST /api/system/rate-limit
 * Tidligere kun tilgaengelig via CLI ("set rate-limit enable/disable"), og
 * bevidst IKKE persisteret her heller — matcher CLI'ens eksisterende
 * runtime-only opfoersel (rate_limit_enabled er en ren static bool, ikke en
 * del af g_persist_config).
 * ============================================================================ */
esp_err_t api_handler_rate_limit_get(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);
  char buf[96];
  snprintf(buf, sizeof(buf), "{\"enabled\":%s,\"persisted\":false}", http_rate_limit_is_enabled() ? "true" : "false");
  return api_send_json(req, buf);
}

esp_err_t api_handler_rate_limit_post(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  char content[64];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) return api_send_error(req, 400, "Failed to read request body");
  content[ret] = '\0';

  JsonDocument doc;
  if (deserializeJson(doc, content)) return api_send_error(req, 400, "Invalid JSON");
  if (!doc.containsKey("enabled")) return api_send_error(req, 400, "Missing 'enabled' field");

  bool en = doc["enabled"].as<bool>();
  http_rate_limit_set_enabled(en);

  char buf[128];
  snprintf(buf, sizeof(buf), "{\"status\":200,\"enabled\":%s,\"message\":\"Not persisted across reboot\"}", en ? "true" : "false");
  return api_send_json(req, buf);
}

/* ============================================================================
 * FEAT-030: /api/v1/* Dispatch Handlers (v7.0.0)
 *
 * These handlers receive requests for /api/v1/... URIs, strip the "/v1"
 * segment in-place, call the appropriate existing handler, then restore
 * the URI. This avoids duplicating 56+ URI registrations.
 *
 * ESP-IDF httpd_req_t.uri is a char[] array — safe to modify in-place.
 * ============================================================================ */

// Rewrite "/api/v1/xyz" -> "/api/xyz" in-place, returns true if rewritten
static bool v1_rewrite_uri(httpd_req_t *req)
{
  char *uri = (char *)req->uri;
  size_t len = strlen(uri);

  // Must start with "/api/v1/" or be exactly "/api/v1"
  if (len >= 7 && strncmp(uri, "/api/v1", 7) == 0) {
    if (len == 7 || uri[7] == '/' || uri[7] == '?') {
      // Shift everything after "/api/v1" to after "/api"
      // "/api/v1/counters/1" -> "/api/counters/1"
      memmove(uri + 4, uri + 7, len - 7 + 1);  // +1 for null terminator
      return true;
    }
  }
  return false;
}

// Undo rewrite: "/api/xyz" -> "/api/v1/xyz"
static void v1_restore_uri(httpd_req_t *req, size_t original_len)
{
  char *uri = (char *)req->uri;
  size_t cur_len = strlen(uri);
  // Shift back: make room for "/v1"
  memmove(uri + 7, uri + 4, cur_len - 4 + 1);
  memcpy(uri + 4, "/v1", 3);
}

// Internal dispatch based on rewritten URI
static esp_err_t v1_dispatch(httpd_req_t *req);

esp_err_t api_v1_dispatch_get(httpd_req_t *req)
{
  return v1_dispatch(req);
}

esp_err_t api_v1_dispatch_post(httpd_req_t *req)
{
  return v1_dispatch(req);
}

esp_err_t api_v1_dispatch_delete(httpd_req_t *req)
{
  return v1_dispatch(req);
}

// Routing table entry
typedef struct {
  const char *prefix;     // URI prefix to match (after v1 rewrite)
  bool exact;             // true = exact match, false = prefix match
  int method;             // HTTP_GET, HTTP_POST, HTTP_DELETE, or -1 for any
  esp_err_t (*handler)(httpd_req_t *req);
} V1Route;

// Forward-declared handlers we need
extern esp_err_t api_handler_endpoints(httpd_req_t *req);
extern esp_err_t api_handler_config_get(httpd_req_t *req);
extern esp_err_t api_handler_gpio(httpd_req_t *req);
extern esp_err_t api_handler_gpio_single(httpd_req_t *req);
extern esp_err_t api_handler_gpio_write(httpd_req_t *req);
extern esp_err_t api_handler_debug_get(httpd_req_t *req);
extern esp_err_t api_handler_debug_set(httpd_req_t *req);
extern esp_err_t api_handler_system_reboot(httpd_req_t *req);
extern esp_err_t api_handler_system_save(httpd_req_t *req);
extern esp_err_t api_handler_system_load(httpd_req_t *req);
extern esp_err_t api_handler_system_defaults(httpd_req_t *req);
extern esp_err_t api_handler_ethernet_get(httpd_req_t *req);
extern esp_err_t api_handler_ethernet_post(httpd_req_t *req);
extern esp_err_t api_handler_system_backup(httpd_req_t *req);
extern esp_err_t api_handler_system_restore(httpd_req_t *req);
extern esp_err_t api_handler_hr_bulk_read(httpd_req_t *req);
extern esp_err_t api_handler_ir_bulk_read(httpd_req_t *req);
extern esp_err_t api_handler_coils_bulk_read(httpd_req_t *req);
extern esp_err_t api_handler_di_bulk_read(httpd_req_t *req);
extern esp_err_t api_handler_heartbeat(httpd_req_t *req);
extern esp_err_t api_handler_sse_status(httpd_req_t *req);
extern esp_err_t api_handler_sse_clients(httpd_req_t *req);
extern esp_err_t api_handler_sse_disconnect(httpd_req_t *req);
extern esp_err_t api_handler_api_version(httpd_req_t *req);
extern esp_err_t api_handler_gpio_config_delete(httpd_req_t *req);
extern esp_err_t api_handler_metrics(httpd_req_t *req);
extern esp_err_t api_handler_persist_groups_list(httpd_req_t *req);
extern esp_err_t api_handler_persist_group_single(httpd_req_t *req);
extern esp_err_t api_handler_persist_group_post(httpd_req_t *req);
extern esp_err_t api_handler_persist_group_delete(httpd_req_t *req);
extern esp_err_t api_handler_persist_save(httpd_req_t *req);
extern esp_err_t api_handler_persist_restore(httpd_req_t *req);
extern esp_err_t api_handler_cli_exec(httpd_req_t *req);
extern esp_err_t api_handler_bindings_list(httpd_req_t *req);
extern esp_err_t api_handler_bindings_delete(httpd_req_t *req);

// Routing table — order matters (more specific first)
static const V1Route v1_routes[] = {
  // Exact matches first
  {"/api/status",           true,  HTTP_GET,    api_handler_status},
  {"/api/config",           true,  HTTP_GET,    api_handler_config_get},
  {"/api/counters",         true,  HTTP_GET,    api_handler_counters},
  {"/api/timers",           true,  HTTP_GET,    api_handler_timers},
  {"/api/logic",            true,  HTTP_GET,    api_handler_logic},
  {"/api/gpio",             true,  HTTP_GET,    api_handler_gpio},
  {"/api/wifi",             true,  HTTP_GET,    api_handler_wifi_get},
  {"/api/wifi",             true,  HTTP_POST,   api_handler_wifi_post},
  {"/api/ethernet",         true,  HTTP_GET,    api_handler_ethernet_get},
  {"/api/ethernet",         true,  HTTP_POST,   api_handler_ethernet_post},
  {"/api/debug",            true,  HTTP_GET,    api_handler_debug_get},
  {"/api/debug",            true,  HTTP_POST,   api_handler_debug_set},
  {"/api/modules",          true,  HTTP_GET,    api_handler_modules_get},
  {"/api/modules",          true,  HTTP_POST,   api_handler_modules_post},
  {"/api/hostname",         true,  HTTP_GET,    api_handler_hostname_get},
  {"/api/hostname",         true,  HTTP_POST,   api_handler_hostname_post},
  {"/api/telnet",           true,  HTTP_GET,    api_handler_telnet_get},
  {"/api/telnet",           true,  HTTP_POST,   api_handler_telnet_post},
  {"/api/system/reboot",    true,  HTTP_POST,   api_handler_system_reboot},
  {"/api/system/save",      true,  HTTP_POST,   api_handler_system_save},
  {"/api/system/load",      true,  HTTP_POST,   api_handler_system_load},
  {"/api/system/defaults",  true,  HTTP_POST,   api_handler_system_defaults},
  {"/api/system/watchdog",  true,  HTTP_GET,    api_handler_system_watchdog},
  {"/api/system/backup",    true,  HTTP_GET,    api_handler_system_backup},
  {"/api/system/restore",   true,  HTTP_POST,   api_handler_system_restore},
  {"/api/http",             true,  HTTP_POST,   api_handler_http_config_post},
  {"/api/logic/settings",   true,  HTTP_POST,   api_handler_logic_settings_post},
  {"/api/dashboard/layout", true,  HTTP_GET,    api_handler_dashboard_layout_get},
  {"/api/dashboard/layout", true,  HTTP_POST,   api_handler_dashboard_layout_post},
  {"/api/events/status",    true,  HTTP_GET,    api_handler_sse_status},
  {"/api/events/clients",   true,  HTTP_GET,    api_handler_sse_clients},
  {"/api/events/disconnect", true, HTTP_POST,   api_handler_sse_disconnect},
  {"/api/version",          true,  HTTP_GET,    api_handler_api_version},
  {"/api/metrics",          true,  HTTP_GET,    api_handler_metrics},
  {"/api/persist/groups",   true,  HTTP_GET,    api_handler_persist_groups_list},
  {"/api/persist/save",     true,  HTTP_POST,   api_handler_persist_save},
  {"/api/persist/restore",  true,  HTTP_POST,   api_handler_persist_restore},
  {"/api/persist/config",   true,  HTTP_POST,   api_handler_persist_config_post},
  {"/api/user/me",          true,  HTTP_GET,    api_handler_user_me},
  {"/api/cli",              true,  HTTP_POST,   api_handler_cli_exec},
  {"/api/bindings",         true,  HTTP_GET,    api_handler_bindings_list},

  // Bulk register operations (before wildcards)
  {"/api/registers/hr",     true,  HTTP_GET,    api_handler_hr_bulk_read},
  {"/api/registers/ir",     true,  HTTP_GET,    api_handler_ir_bulk_read},
  {"/api/registers/coils",  true,  HTTP_GET,    api_handler_coils_bulk_read},
  {"/api/registers/di",     true,  HTTP_GET,    api_handler_di_bulk_read},
  {"/api/registers/hr/bulk", true,  HTTP_POST,  api_handler_hr_bulk_write},
  {"/api/registers/coils/bulk", true, HTTP_POST, api_handler_coils_bulk_write},

  // Heartbeat (before gpio wildcard — more specific match first)
  {"/api/gpio/2/heartbeat", true,  HTTP_GET,    api_handler_heartbeat},
  {"/api/gpio/2/heartbeat", true,  HTTP_POST,   api_handler_heartbeat},

  // Wildcard prefix matches
  {"/api/counters/",        false, HTTP_GET,    api_handler_counter_single},
  {"/api/counters/",        false, HTTP_POST,   api_handler_counter_single},
  {"/api/counters/",        false, HTTP_DELETE,  api_handler_counter_delete},
  {"/api/timers/",          false, HTTP_GET,    api_handler_timer_single},
  {"/api/timers/",          false, HTTP_POST,   api_handler_timer_config_post},
  {"/api/timers/",          false, HTTP_DELETE,  api_handler_timer_delete},
  {"/api/registers/hr/",    false, HTTP_GET,    api_handler_hr_read},
  {"/api/registers/hr/",    false, HTTP_POST,   api_handler_hr_write},
  {"/api/registers/ir/",    false, HTTP_GET,    api_handler_ir_read},
  {"/api/registers/coils/", false, HTTP_GET,    api_handler_coil_read},
  {"/api/registers/coils/", false, HTTP_POST,   api_handler_coil_write},
  {"/api/registers/di/",    false, HTTP_GET,    api_handler_di_read},
  {"/api/gpio/",            false, HTTP_GET,    api_handler_gpio_single},
  {"/api/gpio/",            false, HTTP_POST,   api_handler_gpio_write},
  {"/api/gpio/",            false, HTTP_DELETE,  api_handler_gpio_config_delete},
  {"/api/logic/",           false, HTTP_GET,    api_handler_logic_single},
  {"/api/logic/",           false, HTTP_POST,   api_handler_logic_single},
  {"/api/logic/",           false, HTTP_DELETE,  api_handler_logic_delete},
  {"/api/modbus/",          false, HTTP_GET,    api_handler_modbus_get},
  {"/api/modbus/",          false, HTTP_POST,   api_handler_modbus_post},
  {"/api/wifi/",            false, HTTP_POST,   api_handler_wifi_post},
  {"/api/persist/groups/",  false, HTTP_GET,    api_handler_persist_group_single},
  {"/api/persist/groups/",  false, HTTP_POST,   api_handler_persist_group_post},
  {"/api/persist/groups/",  false, HTTP_DELETE,  api_handler_persist_group_delete},
  {"/api/bindings/",        false, HTTP_DELETE,  api_handler_bindings_delete},

  // Sentinel
  {NULL, false, -1, NULL}
};

static esp_err_t v1_dispatch(httpd_req_t *req)
{
  http_server_stat_request();

  // Save original length before rewrite
  size_t orig_len = strlen(req->uri);

  // Rewrite: /api/v1/xxx -> /api/xxx
  if (!v1_rewrite_uri(req)) {
    return api_send_error(req, 400, "Invalid v1 API path");
  }

  const char *uri = req->uri;
  int method = req->method;

  // Try routing table
  for (int i = 0; v1_routes[i].prefix != NULL; i++) {
    const V1Route *r = &v1_routes[i];

    // Check method
    if (r->method != -1 && r->method != method) continue;

    // Check URI match
    if (r->exact) {
      if (strcmp(uri, r->prefix) != 0) continue;
    } else {
      if (strncmp(uri, r->prefix, strlen(r->prefix)) != 0) continue;
    }

    // Match found — call handler
    esp_err_t result = r->handler(req);

    // Restore URI
    v1_restore_uri(req, orig_len);
    return result;
  }

  // No match — restore URI and return 404
  v1_restore_uri(req, orig_len);
  return api_send_error(req, 404, "Endpoint not found in API v1");
}
