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
#include "types.h"
#include "config_struct.h"
#include "registers.h"
#include "counter_engine.h"
#include "counter_config.h"
#include "counter_frequency.h"
#include "timer_engine.h"
#include "timer_config.h"
#include "st_logic_config.h"
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
esp_err_t api_handler_logic_stats(httpd_req_t *req);
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

static alarm_entry_t alarm_log[ALARM_LOG_MAX];
static uint8_t alarm_log_head = 0;   // Next write position
static uint8_t alarm_log_count = 0;  // Total entries (max ALARM_LOG_MAX)
static uint32_t alarm_check_prev_ms = 0;
static uint32_t alarm_prev_slave_crc = 0;
static uint32_t alarm_prev_master_timeout = 0;
static uint32_t alarm_prev_auth_fail = 0;
static uint32_t alarm_prev_write_denied = 0;
static bool alarm_sse_full_active = false;

static void alarm_log_add(uint8_t severity, const char *msg) {
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
  httpd_resp_set_status(req, status == 404 ? "404 Not Found" :
                             status == 400 ? "400 Bad Request" :
                             status == 401 ? "401 Unauthorized" :
                             status == 403 ? "403 Forbidden" :
                             status == 500 ? "500 Internal Server Error" : "400 Bad Request");

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

  httpd_resp_sendstr(req, buf);

  if (status == 401) {
    http_server_stat_auth_failure();
    alarm_record_auth_failure_info(fail_ip, fail_user);
  } else if (status == 403) {
    http_server_stat_client_error();
    alarm_record_write_denied_info(fail_ip, fail_user);
  } else if (status >= 500) {
    http_server_stat_server_error();
  } else {
    http_server_stat_client_error();
  }

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
  httpd_resp_sendstr(req, json_str);
  http_server_stat_success();
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

#define CHECK_AUTH(req) \
  do { \
    CHECK_API_ENABLED(req); \
    if (!http_server_check_auth(req)) { \
      return api_send_error(req, 401, "Authentication required"); \
    } \
    if (!http_rate_limit_check(req)) { \
      return api_send_error(req, 429, "Too many requests"); \
    } \
  } while(0)

#define CHECK_AUTH_WRITE(req) \
  do { \
    CHECK_API_ENABLED(req); \
    int _uid = http_server_auth_user(req); \
    if (_uid < 0) { \
      return api_send_error(req, 401, "Authentication required"); \
    } \
    if (!rbac_has_write(_uid)) { \
      return api_send_error(req, 403, "Write privilege required"); \
    } \
    if (!http_rate_limit_check(req)) { \
      return api_send_error(req, 429, "Too many requests"); \
    } \
  } while(0)

#define CHECK_AUTH_ROLE(req, role) \
  do { \
    CHECK_API_ENABLED(req); \
    int _uid = http_server_auth_user(req); \
    if (_uid < 0) { \
      return api_send_error(req, 401, "Authentication required"); \
    } \
    if (!rbac_has_role(_uid, role)) { \
      return api_send_error(req, 403, "Insufficient role"); \
    } \
    if (!http_rate_limit_check(req)) { \
      return api_send_error(req, 429, "Too many requests"); \
    } \
  } while(0)

/* ============================================================================
 * GET /api/ - API Discovery (list all endpoints)
 * ============================================================================ */

esp_err_t api_handler_endpoints(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  // Use heap allocation for larger response (endpoints list ~10KB with v6.3.0 additions)
  char *buf = (char *)malloc(10240);
  if (!buf) {
    return api_send_error(req, 500, "Out of memory");
  }

  // Build JSON manually for efficiency
  int len = snprintf(buf, 10240,
    "{"
    "\"name\":\"Modbus ESP32 REST API\","
    "\"version\":\"%s\","
    "\"build\":%d,"
    "\"endpoints\":["
    "{\"method\":\"GET\",\"path\":\"/api/\",\"desc\":\"List endpoints\"},"
    "{\"method\":\"GET\",\"path\":\"/api/status\",\"desc\":\"System status\"},"
    "{\"method\":\"GET\",\"path\":\"/api/config\",\"desc\":\"Full configuration\"},"
    "{\"method\":\"GET\",\"path\":\"/api/counters\",\"desc\":\"All counters\"},"
    "{\"method\":\"GET\",\"path\":\"/api/counters/{1-4}\",\"desc\":\"Single counter\"},"
    "{\"method\":\"POST\",\"path\":\"/api/counters/{1-4}\",\"desc\":\"Configure counter\"},"
    "{\"method\":\"POST\",\"path\":\"/api/counters/{1-4}/reset\",\"desc\":\"Reset counter\"},"
    "{\"method\":\"POST\",\"path\":\"/api/counters/{1-4}/start\",\"desc\":\"Start counter\"},"
    "{\"method\":\"POST\",\"path\":\"/api/counters/{1-4}/stop\",\"desc\":\"Stop counter\"},"
    "{\"method\":\"POST\",\"path\":\"/api/counters/{1-4}/control\",\"desc\":\"Counter control\"},"
    "{\"method\":\"DELETE\",\"path\":\"/api/counters/{1-4}\",\"desc\":\"Delete counter\"},"
    "{\"method\":\"GET\",\"path\":\"/api/timers\",\"desc\":\"All timers\"},"
    "{\"method\":\"GET\",\"path\":\"/api/timers/{1-4}\",\"desc\":\"Single timer\"},"
    "{\"method\":\"POST\",\"path\":\"/api/timers/{1-4}\",\"desc\":\"Configure timer\"},"
    "{\"method\":\"DELETE\",\"path\":\"/api/timers/{1-4}\",\"desc\":\"Delete timer\"},"
    "{\"method\":\"GET\",\"path\":\"/api/registers/hr/{addr}\",\"desc\":\"Read HR\"},"
    "{\"method\":\"POST\",\"path\":\"/api/registers/hr/{addr}\",\"desc\":\"Write HR\"},"
    "{\"method\":\"GET\",\"path\":\"/api/registers/ir/{addr}\",\"desc\":\"Read IR\"},"
    "{\"method\":\"GET\",\"path\":\"/api/registers/coils/{addr}\",\"desc\":\"Read coil\"},"
    "{\"method\":\"POST\",\"path\":\"/api/registers/coils/{addr}\",\"desc\":\"Write coil\"},"
    "{\"method\":\"GET\",\"path\":\"/api/registers/di/{addr}\",\"desc\":\"Read DI\"},"
    "{\"method\":\"GET\",\"path\":\"/api/gpio\",\"desc\":\"All GPIO mappings\"},"
    "{\"method\":\"GET\",\"path\":\"/api/gpio/{pin}\",\"desc\":\"Single GPIO\"},"
    "{\"method\":\"POST\",\"path\":\"/api/gpio/{pin}\",\"desc\":\"Write GPIO\"},"
    "{\"method\":\"DELETE\",\"path\":\"/api/gpio/{pin}\",\"desc\":\"Remove GPIO mapping\"},"
    "{\"method\":\"GET\",\"path\":\"/api/logic\",\"desc\":\"ST Logic programs\"},"
    "{\"method\":\"GET\",\"path\":\"/api/logic/{1-4}\",\"desc\":\"Single program\"},"
    "{\"method\":\"GET\",\"path\":\"/api/logic/{1-4}/source\",\"desc\":\"Download ST code\"},"
    "{\"method\":\"POST\",\"path\":\"/api/logic/{1-4}/source\",\"desc\":\"Upload ST code\"},"
    "{\"method\":\"POST\",\"path\":\"/api/logic/{1-4}/enable\",\"desc\":\"Enable program\"},"
    "{\"method\":\"POST\",\"path\":\"/api/logic/{1-4}/disable\",\"desc\":\"Disable program\"},"
    "{\"method\":\"DELETE\",\"path\":\"/api/logic/{1-4}\",\"desc\":\"Delete program\"},"
    "{\"method\":\"GET\",\"path\":\"/api/logic/{1-4}/stats\",\"desc\":\"Program stats\"},"
    "{\"method\":\"POST\",\"path\":\"/api/logic/settings\",\"desc\":\"Logic engine settings\"},"
    "{\"method\":\"GET\",\"path\":\"/api/modbus/slave\",\"desc\":\"Slave config+stats\"},"
    "{\"method\":\"POST\",\"path\":\"/api/modbus/slave\",\"desc\":\"Configure slave\"},"
    "{\"method\":\"GET\",\"path\":\"/api/modbus/master\",\"desc\":\"Master config+stats\"},"
    "{\"method\":\"POST\",\"path\":\"/api/modbus/master\",\"desc\":\"Configure master\"},"
    "{\"method\":\"GET\",\"path\":\"/api/wifi\",\"desc\":\"WiFi config+status\"},"
    "{\"method\":\"POST\",\"path\":\"/api/wifi\",\"desc\":\"Configure WiFi\"},"
    "{\"method\":\"POST\",\"path\":\"/api/wifi/connect\",\"desc\":\"Connect WiFi\"},"
    "{\"method\":\"POST\",\"path\":\"/api/wifi/disconnect\",\"desc\":\"Disconnect WiFi\"},"
    "{\"method\":\"GET\",\"path\":\"/api/ethernet\",\"desc\":\"Ethernet (W5500) config+status\"},"
    "{\"method\":\"POST\",\"path\":\"/api/ethernet\",\"desc\":\"Configure Ethernet\"},"
    "{\"method\":\"POST\",\"path\":\"/api/http\",\"desc\":\"Configure HTTP server\"},"
    "{\"method\":\"GET\",\"path\":\"/api/modules\",\"desc\":\"Module flags\"},"
    "{\"method\":\"POST\",\"path\":\"/api/modules\",\"desc\":\"Set module flags\"},"
    "{\"method\":\"GET\",\"path\":\"/api/debug\",\"desc\":\"Debug flags\"},"
    "{\"method\":\"POST\",\"path\":\"/api/debug\",\"desc\":\"Set debug flags\"},"
    "{\"method\":\"POST\",\"path\":\"/api/system/reboot\",\"desc\":\"Reboot ESP32\"},"
    "{\"method\":\"POST\",\"path\":\"/api/system/save\",\"desc\":\"Save config to NVS\"},"
    "{\"method\":\"POST\",\"path\":\"/api/system/load\",\"desc\":\"Load config from NVS\"},"
    "{\"method\":\"POST\",\"path\":\"/api/system/defaults\",\"desc\":\"Reset to defaults\"},"
    "{\"method\":\"GET\",\"path\":\"/api/system/backup\",\"desc\":\"Download config backup\"},"
    "{\"method\":\"POST\",\"path\":\"/api/system/restore\",\"desc\":\"Restore config from backup\"},"
    "{\"method\":\"GET\",\"path\":\"/api/telnet\",\"desc\":\"Telnet config+status\"},"
    "{\"method\":\"POST\",\"path\":\"/api/telnet\",\"desc\":\"Configure Telnet\"},"
    "{\"method\":\"GET\",\"path\":\"/api/hostname\",\"desc\":\"Get hostname\"},"
    "{\"method\":\"POST\",\"path\":\"/api/hostname\",\"desc\":\"Set hostname\"},"
    "{\"method\":\"GET\",\"path\":\"/api/system/watchdog\",\"desc\":\"Watchdog status\"},"
    "{\"method\":\"GET\",\"path\":\"/api/registers/hr\",\"desc\":\"Bulk read HRs (start,count)\"},"
    "{\"method\":\"POST\",\"path\":\"/api/registers/hr/bulk\",\"desc\":\"Bulk write HRs\"},"
    "{\"method\":\"GET\",\"path\":\"/api/registers/ir\",\"desc\":\"Bulk read IRs (start,count)\"},"
    "{\"method\":\"GET\",\"path\":\"/api/registers/coils\",\"desc\":\"Bulk read coils (start,count)\"},"
    "{\"method\":\"POST\",\"path\":\"/api/registers/coils/bulk\",\"desc\":\"Bulk write coils\"},"
    "{\"method\":\"GET\",\"path\":\"/api/registers/di\",\"desc\":\"Bulk read DIs (start,count)\"},"
    "{\"method\":\"POST\",\"path\":\"/api/logic/{1-4}/debug/pause\",\"desc\":\"Pause program\"},"
    "{\"method\":\"POST\",\"path\":\"/api/logic/{1-4}/debug/continue\",\"desc\":\"Continue program\"},"
    "{\"method\":\"POST\",\"path\":\"/api/logic/{1-4}/debug/step\",\"desc\":\"Step instruction\"},"
    "{\"method\":\"POST\",\"path\":\"/api/logic/{1-4}/debug/breakpoint\",\"desc\":\"Set breakpoint\"},"
    "{\"method\":\"DELETE\",\"path\":\"/api/logic/{1-4}/debug/breakpoint\",\"desc\":\"Remove breakpoint\"},"
    "{\"method\":\"POST\",\"path\":\"/api/logic/{1-4}/debug/stop\",\"desc\":\"Stop debug\"},"
    "{\"method\":\"GET\",\"path\":\"/api/logic/{1-4}/debug/state\",\"desc\":\"Debug snapshot\"},"
    "{\"method\":\"POST\",\"path\":\"/api/gpio/2/heartbeat\",\"desc\":\"Heartbeat control\"},"
    "{\"method\":\"GET\",\"path\":\"/api/events\",\"desc\":\"SSE real-time event stream (FEAT-023)\"},"
    "{\"method\":\"GET\",\"path\":\"/api/events/status\",\"desc\":\"SSE subsystem info\"},"
    "{\"method\":\"GET\",\"path\":\"/api/version\",\"desc\":\"API version info (FEAT-030)\"},"
    "{\"method\":\"GET\",\"path\":\"/api/v1/*\",\"desc\":\"API v1 versioned endpoint (FEAT-030)\"},"
    "{\"method\":\"POST\",\"path\":\"/api/system/ota\",\"desc\":\"Upload firmware (OTA, FEAT-031)\"},"
    "{\"method\":\"GET\",\"path\":\"/api/system/ota/status\",\"desc\":\"OTA progress status (FEAT-031)\"},"
    "{\"method\":\"POST\",\"path\":\"/api/system/ota/rollback\",\"desc\":\"Rollback firmware (FEAT-031)\"}"
    "]"
    "}",
    PROJECT_VERSION, BUILD_NUMBER);

  if (len < 0 || len >= 10240) {
    free(buf);
    return api_send_error(req, 500, "Buffer overflow");
  }

  esp_err_t ret = api_send_json(req, buf);
  free(buf);
  return ret;
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
    doc["running"] = (ctrl & 0x04) ? true : false;
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

  // Mode-specific parameters
  switch (cfg.mode) {
    case TIMER_MODE_1_ONESHOT:
      doc["phase1_duration_ms"] = cfg.phase1_duration_ms;
      doc["phase2_duration_ms"] = cfg.phase2_duration_ms;
      doc["phase3_duration_ms"] = cfg.phase3_duration_ms;
      break;
    case TIMER_MODE_2_MONOSTABLE:
      doc["pulse_duration_ms"] = cfg.pulse_duration_ms;
      break;
    case TIMER_MODE_3_ASTABLE:
      doc["on_duration_ms"] = cfg.on_duration_ms;
      doc["off_duration_ms"] = cfg.off_duration_ms;
      break;
    case TIMER_MODE_4_INPUT_TRIGGERED:
      doc["input_dis"] = cfg.input_dis;
      doc["delay_ms"] = cfg.delay_ms;
      break;
    default:
      break;
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

  // Write coil
  registers_set_coil(addr, value);

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

  JsonArray programs = doc["programs"].to<JsonArray>();

  for (int i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
    st_logic_program_config_t *prog = &state->programs[i];
    JsonObject p = programs.add<JsonObject>();
    p["id"] = i + 1;
    p["name"] = prog->name;
    p["enabled"] = prog->enabled ? true : false;
    p["compiled"] = prog->compiled ? true : false;
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
    // GAP-13: Variable binding
    if (uri_len >= 5 && strcmp(uri + uri_len - 5, "/bind") == 0) {
      return api_handler_logic_bind_post(req);
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
    if (m->coil_reg != 0xFFFF) {
      gpio["coil"] = m->coil_reg;
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
    if (found->coil_reg != 0xFFFF) {
      doc["coil"] = found->coil_reg;
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
  http["api_enabled"] = g_persist_config.network.http.api_enabled ? true : false;
  http["auth_enabled"] = g_persist_config.network.http.auth_enabled ? true : false;
  const char *prio_str = "NORMAL";
  if (g_persist_config.network.http.priority == 0) prio_str = "LOW";
  else if (g_persist_config.network.http.priority == 2) prio_str = "HIGH";
  http["priority"] = prio_str;

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
      g["coil"] = m->coil_reg;
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

    JsonObject stats = doc["stats"].to<JsonObject>();
    stats["total_requests"] = g_modbus_master_config.total_requests;
    stats["successful_requests"] = g_modbus_master_config.successful_requests;
    stats["timeout_errors"] = g_modbus_master_config.timeout_errors;
    stats["crc_errors"] = g_modbus_master_config.crc_errors;
    stats["exception_errors"] = g_modbus_master_config.exception_errors;
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
  http_server_stat_request();
  CHECK_AUTH_WRITE(req);

  const char *uri = req->uri;
  bool is_slave = (strstr(uri, "/slave") != NULL);
  bool is_master = (strstr(uri, "/master") != NULL);

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
      strncpy(g_persist_config.network.http.password, p, HTTP_AUTH_PASSWORD_MAX_LEN - 1);
      g_persist_config.network.http.password[HTTP_AUTH_PASSWORD_MAX_LEN - 1] = '\0';
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

  if (doc.containsKey("running")) {
    if (doc["running"].as<bool>()) {
      ctrl_val |= 0x0002;  // Start bit
    } else {
      ctrl_val &= ~0x0002;
      ctrl_val |= 0x0004;  // Stop
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
    existing->coil_reg = 0xFFFF;
  }

  // Update mapping
  existing->is_input = is_input ? 1 : 0;
  if (is_input) {
    existing->input_reg = reg_addr;
    existing->input_type = 0;  // Holding register
  } else {
    existing->coil_reg = reg_addr;
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
    m->coil_reg = 0xFFFF;
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
    m->coil_reg = register_addr;
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
 * ============================================================================ */

esp_err_t api_handler_user_me(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_API_ENABLED(req);
  if (!http_rate_limit_check(req)) {
    return api_send_error(req, 429, "Too many requests");
  }

  int uid = http_server_auth_user(req);

  char buf[256];
  if (uid < 0) {
    // Not authenticated
    snprintf(buf, sizeof(buf),
      "{\"authenticated\":false,\"username\":null,\"roles\":null,\"privilege\":null}");
  } else if (uid == 99) {
    // Virtual admin (legacy or no-auth)
    snprintf(buf, sizeof(buf),
      "{\"authenticated\":true,\"username\":\"admin\",\"roles\":\"all\",\"privilege\":\"read/write\",\"mode\":\"legacy\"}");
  } else {
    // RBAC user
    const RbacUser *u = rbac_get_user(uid);
    if (u) {
      char role_str[40];
      rbac_roles_to_str(u->roles, role_str, sizeof(role_str));
      const char *priv_str = (u->privilege == PRIV_RW) ? "read/write" :
                             (u->privilege == PRIV_WRITE) ? "write" : "read";
      snprintf(buf, sizeof(buf),
        "{\"authenticated\":true,\"username\":\"%s\",\"roles\":\"%s\",\"privilege\":\"%s\",\"mode\":\"rbac\",\"index\":%d}",
        u->username, role_str, priv_str, uid);
    } else {
      snprintf(buf, sizeof(buf),
        "{\"authenticated\":true,\"username\":\"unknown\",\"roles\":\"all\",\"privilege\":\"read/write\",\"mode\":\"rbac\"}");
    }
  }

  return api_send_json(req, buf);
}

esp_err_t api_handler_cli_exec(httpd_req_t *req)
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
      b["register_addr"] = m->coil_reg;
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

    // Also update runtime state
    st_logic_engine_state_t *state = st_logic_get_state();
    if (state) {
      state->execution_interval_ms = interval;
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
 * BACKUP / RESTORE ENDPOINTS
 * ============================================================================ */

esp_err_t api_handler_system_backup(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

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
  http["api_enabled"] = g_persist_config.network.http.api_enabled ? true : false;
  http["auth_enabled"] = g_persist_config.network.http.auth_enabled ? true : false;
  http["username"] = g_persist_config.network.http.username;
  http["password"] = g_persist_config.network.http.password;
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
    if (m->source_type == 0 && m->gpio_pin == 0 && m->input_reg == 0xFFFF && m->coil_reg == 0xFFFF) continue;
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
    mo["coil_reg"] = m->coil_reg;
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
      uo["password"] = u->password;
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
    if (h.containsKey("api_enabled")) g_persist_config.network.http.api_enabled = h["api_enabled"].as<bool>() ? 1 : 0;
    if (h.containsKey("auth_enabled")) g_persist_config.network.http.auth_enabled = h["auth_enabled"].as<bool>() ? 1 : 0;
    if (h.containsKey("username")) {
      strncpy(g_persist_config.network.http.username, h["username"] | "", sizeof(g_persist_config.network.http.username) - 1);
      g_persist_config.network.http.username[sizeof(g_persist_config.network.http.username) - 1] = '\0';
    }
    if (h.containsKey("password")) {
      strncpy(g_persist_config.network.http.password, h["password"] | "", sizeof(g_persist_config.network.http.password) - 1);
      g_persist_config.network.http.password[sizeof(g_persist_config.network.http.password) - 1] = '\0';
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
      m->coil_reg = mo["coil_reg"] | 0xFFFF;
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
          strncpy(u->password, uo["password"] | "", RBAC_PASSWORD_MAX - 1);
          u->password[RBAC_PASSWORD_MAX - 1] = '\0';
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

  char resp[128];
  snprintf(resp, sizeof(resp), "{\"status\":200,\"hostname\":\"%s\"}", g_persist_config.hostname);
  return api_send_json(req, resp);
}

/* ============================================================================
 * FEAT-108: GET /api/dashboard/layout — Get dashboard card order
 * ============================================================================ */

esp_err_t api_handler_dashboard_layout_get(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  char buf[256];
  snprintf(buf, sizeof(buf), "{\"card_order\":\"%s\"}", g_persist_config.dashboard_card_order);
  return api_send_json(req, buf);
}

/* ============================================================================
 * FEAT-108: POST /api/dashboard/layout — Set dashboard card order
 * ============================================================================ */

esp_err_t api_handler_dashboard_layout_post(httpd_req_t *req)
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

  if (!doc.containsKey("card_order")) {
    return api_send_error(req, 400, "Missing 'card_order' field");
  }

  const char *order = doc["card_order"].as<const char*>();
  if (!order || strlen(order) >= sizeof(g_persist_config.dashboard_card_order)) {
    return api_send_error(req, 400, "Invalid card_order (max 159 chars)");
  }

  strncpy(g_persist_config.dashboard_card_order, order, sizeof(g_persist_config.dashboard_card_order) - 1);
  g_persist_config.dashboard_card_order[sizeof(g_persist_config.dashboard_card_order) - 1] = '\0';

  char resp[256];
  snprintf(resp, sizeof(resp), "{\"status\":200,\"card_order\":\"%s\"}", g_persist_config.dashboard_card_order);
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

  // Buffer for Prometheus text format (12KB for expanded metrics incl. tasks/cache)
  char *buf = (char *)malloc(12288);
  if (!buf) {
    return api_send_error(req, 500, "Out of memory");
  }
  int pos = 0;
  int remaining = 12288;

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

    // Per-slave cache entries with status
    PROM_APPEND("# HELP modbus_master_slave_status Per-slave cache entry status\n");
    PROM_APPEND("# TYPE modbus_master_slave_status gauge\n");
    for (int i = 0; i < mb_async->entry_count && i < MB_CACHE_MAX_ENTRIES; i++) {
      const mb_cache_entry_t *e = &mb_async->entries[i];
      if (e->status != MB_CACHE_EMPTY) {
        const char *st = (e->status == MB_CACHE_VALID) ? "valid" :
                         (e->status == MB_CACHE_PENDING) ? "pending" :
                         (e->status == MB_CACHE_ERROR) ? "error" : "empty";
        PROM_APPEND("modbus_master_slave_status{slave=\"%d\",addr=\"%d\",fc=\"%d\",status=\"%s\"} %d\n",
                     e->key.slave_id, e->key.address, e->key.req_type, st,
                     (e->status == MB_CACHE_VALID) ? 1 : (e->status == MB_CACHE_ERROR) ? -1 : 0);
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

  // --- Modbus Register metrics (non-zero holding & input registers) ---
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
  CHECK_API_ENABLED(req);
  if (!http_rate_limit_check(req)) {
    return api_send_error(req, 429, "Too many requests");
  }

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

  for (int i = 0; i < ALARM_LOG_MAX; i++) {
    alarm_log[i].acknowledged = true;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"All alarms acknowledged\"}");

  http_server_stat_success();
  return ESP_OK;
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
