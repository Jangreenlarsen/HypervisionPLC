/**
 * @file sse_events.cpp
 * @brief Server-Sent Events (SSE) implementation (FEAT-023, v7.0.0)
 *
 * LAYER 1.5: Protocol
 * Responsibility: SSE stream handler with change detection
 *
 * Architecture:
 * - Runs on a SEPARATE httpd instance (port 81) to avoid blocking main API
 * - ESP-IDF httpd is single-threaded: blocking SSE handler would block all API calls
 * - Solution: dedicated SSE server on port=main+1, own task, own stack
 * - Handler blocks httpd thread of SSE server, sending events until client disconnect
 * - Change detection by polling current values vs last-sent values
 * - Max 3 simultaneous clients (configurable via SSE_MAX_CLIENTS)
 * - Topic-based subscription filtering (counters, timers, registers, system)
 * - Automatic keepalive every 15 seconds
 */

#include <string.h>
#include <stdio.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_http_server.h>
#include <esp_log.h>

#include "sse_events.h"
#include "constants.h"
#include "types.h"
#include "registers.h"
#include "counter_engine.h"
#include "timer_engine.h"
#include "config_struct.h"
#include "build_version.h"
#include "debug.h"

// External functions from http_server.cpp
extern void http_server_stat_request(void);
extern void http_server_stat_success(void);
extern bool http_server_check_auth(httpd_req_t *req);

static const char *TAG = "SSE";

/* ============================================================================
 * INTERNAL STATE
 * ============================================================================ */

static volatile int sse_active_clients = 0;
static httpd_handle_t sse_server = NULL;
static uint16_t sse_port = 0;

// Tracked state for change detection (per SSE client session)
typedef struct {
  uint64_t counter_values[COUNTER_COUNT];
  uint8_t  counter_enabled[COUNTER_COUNT];
  uint8_t  timer_output[TIMER_COUNT];
  uint8_t  timer_enabled[TIMER_COUNT];
  uint16_t watched_hr[16];       // Watched holding registers (first 16)
  uint32_t last_heartbeat_ms;
} SseClientState;

/* ============================================================================
 * UTILITY: Parse subscription topics from query string
 * ============================================================================ */

static uint8_t sse_parse_topics(httpd_req_t *req)
{
  char query[128] = {0};
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
    return SSE_TOPIC_ALL;  // Default: subscribe to everything
  }

  char subscribe[64] = {0};
  if (httpd_query_key_value(query, "subscribe", subscribe, sizeof(subscribe)) != ESP_OK) {
    return SSE_TOPIC_ALL;
  }

  uint8_t topics = 0;
  if (strstr(subscribe, "counters")) topics |= SSE_TOPIC_COUNTERS;
  if (strstr(subscribe, "timers"))   topics |= SSE_TOPIC_TIMERS;
  if (strstr(subscribe, "registers")) topics |= SSE_TOPIC_REGISTERS;
  if (strstr(subscribe, "system"))   topics |= SSE_TOPIC_SYSTEM;
  if (strstr(subscribe, "all"))      topics = SSE_TOPIC_ALL;

  return topics ? topics : SSE_TOPIC_ALL;
}

/* ============================================================================
 * UTILITY: Send SSE-formatted event via chunked response
 * ============================================================================ */

static esp_err_t sse_send_event(httpd_req_t *req, const char *event_name, const char *data)
{
  char buf[384];
  int len = snprintf(buf, sizeof(buf), "event: %s\ndata: %s\n\n", event_name, data);
  if (len <= 0 || len >= (int)sizeof(buf)) return ESP_FAIL;
  return httpd_resp_send_chunk(req, buf, len);
}

/* ============================================================================
 * SNAPSHOT: Capture current state
 * ============================================================================ */

static void sse_snapshot_counters(SseClientState *state)
{
  for (int i = 0; i < COUNTER_COUNT; i++) {
    state->counter_values[i] = counter_engine_get_value(i + 1);
    CounterConfig cfg;
    state->counter_enabled[i] = counter_engine_get_config(i + 1, &cfg) ? cfg.enabled : 0;
  }
}

static void sse_snapshot_timers(SseClientState *state)
{
  for (int i = 0; i < TIMER_COUNT; i++) {
    TimerConfig cfg;
    if (timer_engine_get_config(i + 1, &cfg)) {
      state->timer_enabled[i] = cfg.enabled;
      state->timer_output[i] = (cfg.output_coil != 0xFFFF) ? registers_get_coil(cfg.output_coil) : 0;
    }
  }
}

static void sse_snapshot_registers(SseClientState *state)
{
  for (int i = 0; i < 16; i++) {
    state->watched_hr[i] = registers_get_holding_register(i);
  }
}

/* ============================================================================
 * ERROR RESPONSE (standalone — SSE server doesn't share api_send_error)
 * ============================================================================ */

static esp_err_t sse_send_error(httpd_req_t *req, int status, const char *msg)
{
  char buf[128];
  snprintf(buf, sizeof(buf), "{\"error\":\"%s\",\"status\":%d}", msg, status);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  const char *status_str = status == 401 ? "401 Unauthorized" :
                           status == 403 ? "403 Forbidden" :
                           status == 503 ? "503 Service Unavailable" : "400 Bad Request";
  httpd_resp_set_status(req, status_str);
  if (status == 401) {
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Modbus ESP32\"");
  }
  httpd_resp_sendstr(req, buf);
  return ESP_OK;
}

/* ============================================================================
 * MAIN SSE HANDLER: GET /api/events
 *
 * Runs on dedicated SSE server (port 81). Blocks until client disconnects.
 * Main API on port 80 remains fully responsive.
 * ============================================================================ */

esp_err_t api_handler_sse_events(httpd_req_t *req)
{
  http_server_stat_request();

  // Auth check (uses same config as main server)
  if (!g_persist_config.network.http.api_enabled) {
    return sse_send_error(req, 403, "API disabled");
  }
  if (!http_server_check_auth(req)) {
    return sse_send_error(req, 401, "Authentication required");
  }

  // Check client limit
  if (sse_active_clients >= SSE_MAX_CLIENTS) {
    return sse_send_error(req, 503, "Max SSE clients reached");
  }

  // Parse subscription topics
  uint8_t topics = sse_parse_topics(req);

  ESP_LOGI(TAG, "SSE client connected (topics=0x%02x, active=%d)", topics, sse_active_clients + 1);

  // Set SSE headers
  httpd_resp_set_type(req, "text/event-stream");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(req, "Connection", "keep-alive");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "X-Accel-Buffering", "no");

  // Send initial connected event
  char init_buf[256];
  snprintf(init_buf, sizeof(init_buf),
    "{\"status\":\"connected\",\"topics\":\"0x%02x\",\"max_clients\":%d,\"active_clients\":%d,\"port\":%d}",
    topics, SSE_MAX_CLIENTS, sse_active_clients + 1, sse_port);

  esp_err_t err = sse_send_event(req, "connected", init_buf);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "SSE client disconnected during init");
    return ESP_OK;
  }

  // Track this client
  sse_active_clients++;

  // Initialize change detection state
  SseClientState state;
  memset(&state, 0, sizeof(state));
  sse_snapshot_counters(&state);
  sse_snapshot_timers(&state);
  sse_snapshot_registers(&state);
  state.last_heartbeat_ms = millis();

  // Main SSE loop — blocks this SSE server thread (main API on port 80 unaffected)
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(SSE_CHECK_INTERVAL_MS));

    // --- Counter change detection ---
    if (topics & SSE_TOPIC_COUNTERS) {
      for (int i = 0; i < COUNTER_COUNT; i++) {
        uint64_t val = counter_engine_get_value(i + 1);
        CounterConfig cfg;
        uint8_t enabled = counter_engine_get_config(i + 1, &cfg) ? cfg.enabled : 0;

        if (val != state.counter_values[i] || enabled != state.counter_enabled[i]) {
          char data[128];
          snprintf(data, sizeof(data),
            "{\"id\":%d,\"value\":%llu,\"enabled\":%s,\"running\":%s}",
            i + 1, (unsigned long long)val,
            enabled ? "true" : "false",
            (cfg.enabled && cfg.mode_enable != COUNTER_MODE_DISABLED) ? "true" : "false");

          err = sse_send_event(req, "counter", data);
          if (err != ESP_OK) goto disconnected;

          state.counter_values[i] = val;
          state.counter_enabled[i] = enabled;
        }
      }
    }

    // --- Timer change detection ---
    if (topics & SSE_TOPIC_TIMERS) {
      for (int i = 0; i < TIMER_COUNT; i++) {
        TimerConfig cfg;
        if (timer_engine_get_config(i + 1, &cfg)) {
          uint8_t output = (cfg.output_coil != 0xFFFF) ? registers_get_coil(cfg.output_coil) : 0;

          if (output != state.timer_output[i] || cfg.enabled != state.timer_enabled[i]) {
            const char *mode_str = "DISABLED";
            switch (cfg.mode) {
              case TIMER_MODE_1_ONESHOT: mode_str = "ONESHOT"; break;
              case TIMER_MODE_2_MONOSTABLE: mode_str = "MONOSTABLE"; break;
              case TIMER_MODE_3_ASTABLE: mode_str = "ASTABLE"; break;
              case TIMER_MODE_4_INPUT_TRIGGERED: mode_str = "INPUT_TRIGGERED"; break;
              default: break;
            }

            char data[128];
            snprintf(data, sizeof(data),
              "{\"id\":%d,\"enabled\":%s,\"mode\":\"%s\",\"output\":%s}",
              i + 1, cfg.enabled ? "true" : "false", mode_str,
              output ? "true" : "false");

            err = sse_send_event(req, "timer", data);
            if (err != ESP_OK) goto disconnected;

            state.timer_output[i] = output;
            state.timer_enabled[i] = cfg.enabled;
          }
        }
      }
    }

    // --- Register change detection (HR 0-15) ---
    if (topics & SSE_TOPIC_REGISTERS) {
      for (int i = 0; i < 16; i++) {
        uint16_t val = registers_get_holding_register(i);
        if (val != state.watched_hr[i]) {
          char data[64];
          snprintf(data, sizeof(data), "{\"type\":\"hr\",\"addr\":%d,\"value\":%u}", i, val);

          err = sse_send_event(req, "register", data);
          if (err != ESP_OK) goto disconnected;

          state.watched_hr[i] = val;
        }
      }
    }

    // --- Keepalive heartbeat ---
    uint32_t now = millis();
    if (now - state.last_heartbeat_ms >= SSE_HEARTBEAT_MS) {
      char data[96];
      snprintf(data, sizeof(data),
        "{\"uptime_ms\":%lu,\"heap_free\":%lu,\"sse_clients\":%d}",
        (unsigned long)now, (unsigned long)ESP.getFreeHeap(), (int)sse_active_clients);

      err = sse_send_event(req, "heartbeat", data);
      if (err != ESP_OK) goto disconnected;

      state.last_heartbeat_ms = now;
    }
  }

disconnected:
  sse_active_clients--;
  ESP_LOGI(TAG, "SSE client disconnected (active=%d)", sse_active_clients);

  // Send terminating chunk (may fail if already disconnected, that's OK)
  httpd_resp_send_chunk(req, NULL, 0);

  return ESP_OK;
}

/* ============================================================================
 * GET /api/events/status — SSE subsystem info (runs on MAIN server, port 80)
 * ============================================================================ */

esp_err_t api_handler_sse_status(httpd_req_t *req)
{
  http_server_stat_request();

  // Auth uses main server's check
  if (!g_persist_config.network.http.api_enabled) {
    return sse_send_error(req, 403, "API disabled");
  }
  if (!http_server_check_auth(req)) {
    return sse_send_error(req, 401, "Authentication required");
  }

  extern esp_err_t api_send_json(httpd_req_t *req, const char *json_str);

  char buf[320];
  snprintf(buf, sizeof(buf),
    "{\"sse_enabled\":true,\"sse_port\":%d,\"max_clients\":%d,\"active_clients\":%d,"
    "\"check_interval_ms\":%d,\"heartbeat_ms\":%d,"
    "\"topics\":[\"counters\",\"timers\",\"registers\",\"system\"],"
    "\"endpoint\":\"http://<ip>:%d/api/events?subscribe=<topics>\"}",
    sse_port, SSE_MAX_CLIENTS, (int)sse_active_clients,
    SSE_CHECK_INTERVAL_MS, SSE_HEARTBEAT_MS, sse_port);

  return api_send_json(req, buf);
}

/* ============================================================================
 * INIT: Start dedicated SSE httpd server
 * ============================================================================ */

void sse_init(void)
{
  sse_active_clients = 0;
  sse_server = NULL;
  ESP_LOGI(TAG, "SSE subsystem initialized (max %d clients)", SSE_MAX_CLIENTS);
}

int sse_start(uint16_t port)
{
  if (sse_server) {
    ESP_LOGI(TAG, "SSE server already running");
    return 0;
  }

  sse_port = port;

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = sse_port;
  config.ctrl_port = config.server_port + 1;  // ctrl on port 82
  config.max_uri_handlers = 4;
  config.max_open_sockets = SSE_MAX_CLIENTS + 1;  // +1 for new connections
  config.stack_size = 6144;   // SSE loop needs decent stack
  config.recv_wait_timeout = 30;  // 30s recv timeout (keepalive)
  config.send_wait_timeout = 10;  // 10s send timeout
  config.uri_match_fn = httpd_uri_match_wildcard;

  esp_err_t err = httpd_start(&sse_server, &config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start SSE server on port %d: %d", sse_port, err);
    return -1;
  }

  // Register SSE event handler
  static const httpd_uri_t uri_sse = {
    .uri      = "/api/events",
    .method   = HTTP_GET,
    .handler  = api_handler_sse_events,
    .user_ctx = NULL
  };
  httpd_register_uri_handler(sse_server, &uri_sse);

  // CORS preflight for SSE
  static const httpd_uri_t uri_sse_options = {
    .uri      = "/api/events",
    .method   = HTTP_OPTIONS,
    .handler  = [](httpd_req_t *req) -> esp_err_t {
      httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
      httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, OPTIONS");
      httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Authorization, Content-Type");
      httpd_resp_set_hdr(req, "Access-Control-Max-Age", "86400");
      httpd_resp_set_status(req, "204 No Content");
      httpd_resp_sendstr(req, "");
      return ESP_OK;
    },
    .user_ctx = NULL
  };
  httpd_register_uri_handler(sse_server, &uri_sse_options);

  ESP_LOGI(TAG, "SSE server started on port %d", sse_port);
  return 0;
}

void sse_stop(void)
{
  if (sse_server) {
    httpd_stop(sse_server);
    sse_server = NULL;
    ESP_LOGI(TAG, "SSE server stopped");
  }
}

int sse_get_client_count(void)
{
  return sse_active_clients;
}

uint16_t sse_get_port(void)
{
  return sse_port;
}
