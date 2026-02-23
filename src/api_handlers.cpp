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

#include "api_handlers.h"
#include "http_server.h"
#include "constants.h"
#include "types.h"
#include "config_struct.h"
#include "registers.h"
#include "counter_engine.h"
#include "counter_config.h"
#include "timer_engine.h"
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

static const char *TAG = "API_HDLR";

// External functions from http_server.cpp for stats
extern void http_server_stat_request(void);
extern void http_server_stat_success(void);
extern void http_server_stat_client_error(void);
extern void http_server_stat_server_error(void);
extern void http_server_stat_auth_failure(void);
extern bool http_server_check_auth(httpd_req_t *req);

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
  httpd_resp_set_status(req, status == 404 ? "404 Not Found" :
                             status == 400 ? "400 Bad Request" :
                             status == 401 ? "401 Unauthorized" :
                             status == 500 ? "500 Internal Server Error" : "400 Bad Request");

  // For 401, add WWW-Authenticate header to trigger browser login dialog
  if (status == 401) {
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Modbus ESP32\"");
  }

  httpd_resp_sendstr(req, buf);

  if (status == 401) {
    http_server_stat_auth_failure();
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
  } while(0)

/* ============================================================================
 * GET /api/ - API Discovery (list all endpoints)
 * ============================================================================ */

esp_err_t api_handler_endpoints(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

  // Use heap allocation for larger response (endpoints list ~8KB with v6.1.0+ GAP additions)
  char *buf = (char *)malloc(8192);
  if (!buf) {
    return api_send_error(req, 500, "Out of memory");
  }

  // Build JSON manually for efficiency
  int len = snprintf(buf, 8192,
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
    "{\"method\":\"POST\",\"path\":\"/api/system/restore\",\"desc\":\"Restore config from backup\"}"
    "]"
    "}",
    PROJECT_VERSION, BUILD_NUMBER);

  if (len < 0 || len >= 8192) {
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
  CHECK_AUTH(req);

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
  CHECK_AUTH(req);

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
  CHECK_AUTH(req);

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

  // Allocate buffer for request body
  char *content = (char *)malloc(content_len + 1);
  if (!content) {
    return api_send_error(req, 500, "Out of memory");
  }

  // Read request body
  int received = 0;
  while (received < content_len) {
    int ret = httpd_req_recv(req, content + received, content_len - received);
    if (ret <= 0) {
      free(content);
      return api_send_error(req, 400, "Failed to read request body");
    }
    received += ret;
  }
  content[content_len] = '\0';

  // Parse JSON
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

  // Upload source code
  uint32_t source_len = strlen(source);
  bool success = st_logic_upload(state, id - 1, source, source_len);
  free(content);

  if (!success) {
    return api_send_error(req, 500, "Failed to upload - pool full or compile error");
  }

  // Auto-compile after upload (BUG-210: was missing)
  st_logic_compile(state, id - 1);

  // Build success response
  st_logic_program_config_t *prog = &state->programs[id - 1];

  JsonDocument resp;
  resp["status"] = 200;
  resp["id"] = id;
  resp["name"] = prog->name;
  resp["compiled"] = prog->compiled ? true : false;
  resp["source_size"] = source_len;

  if (prog->last_error[0] != '\0') {
    resp["error"] = prog->last_error;
  }

  char buf[512];
  serializeJson(resp, buf, sizeof(buf));

  return api_send_json(req, buf);
}

/* ============================================================================
 * SYSTEM ENDPOINTS (v6.0.4+)
 * ============================================================================ */

esp_err_t api_handler_system_reboot(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

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
  CHECK_AUTH(req);

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
  CHECK_AUTH(req);

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
  CHECK_AUTH(req);

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
  CHECK_AUTH(req);

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
  CHECK_AUTH(req);

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
  CHECK_AUTH(req);

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
  if (pin < 0 || pin > 39) {
    return api_send_error(req, 400, "Invalid GPIO pin (must be 0-39)");
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
  CHECK_AUTH(req);

  // GAP-11: Suffix routing for /config
  const char *uri = req->uri;
  size_t uri_len = strlen(uri);
  if (uri_len >= 7 && strcmp(uri + uri_len - 7, "/config") == 0) {
    return api_handler_gpio_config_post(req);
  }

  int pin = api_extract_id_from_uri(req, "/api/gpio/");
  if (pin < 0 || pin > 39) {
    return api_send_error(req, 400, "Invalid GPIO pin (must be 0-39)");
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
  CHECK_AUTH(req);

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
  CHECK_AUTH(req);

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
  CHECK_AUTH(req);

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
  CHECK_AUTH(req);

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
  CHECK_AUTH(req);

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
  CHECK_AUTH(req);

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
  CHECK_AUTH(req);

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
  CHECK_AUTH(req);

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
  CHECK_AUTH(req);

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
  CHECK_AUTH(req);

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
  CHECK_AUTH(req);

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
  CHECK_AUTH(req);

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
  CHECK_AUTH(req);

  int pin = api_extract_id_from_uri(req, "/api/gpio/");
  if (pin < 0 || pin > 39) {
    return api_send_error(req, 400, "Invalid GPIO pin (must be 0-39)");
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
  CHECK_AUTH(req);

  int pin = api_extract_id_from_uri(req, "/api/gpio/");
  if (pin < 0 || pin > 39) {
    return api_send_error(req, 400, "Invalid GPIO pin (must be 0-39)");
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
  CHECK_AUTH(req);

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
 * POST /api/logic/settings - ST Logic engine settings (GAP-26)
 * ============================================================================ */

esp_err_t api_handler_logic_settings_post(httpd_req_t *req)
{
  http_server_stat_request();
  CHECK_AUTH(req);

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
  CHECK_AUTH(req);

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
  CHECK_AUTH(req);

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
