/**
 * @file sse_events.cpp
 * @brief Server-Sent Events (SSE) implementation (FEAT-023, v7.0.1)
 *
 * LAYER 1.5: Protocol
 * Responsibility: SSE stream handler with change detection
 *
 * Architecture:
 * - Raw TCP socket server (NOT httpd) — supports true multi-client SSE
 * - Acceptor task listens on SSE port, spawns per-client FreeRTOS tasks
 * - Each client task: parses HTTP, checks auth, streams SSE events
 * - Change detection by polling current values vs last-sent values
 * - Max 3 simultaneous clients (configurable via SSE_MAX_CLIENTS)
 * - Topic-based subscription filtering (counters, timers, registers, system)
 * - Configurable register watch lists via query params (hr, ir, coils, di)
 * - TCP keepalive for zombie detection, automatic cleanup
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_http_server.h>
#include <lwip/sockets.h>
#include <esp_log.h>
#include <mbedtls/base64.h>

#include "sse_events.h"
#include "constants.h"
#include "types.h"
#include "registers.h"
#include "counter_engine.h"
#include "timer_engine.h"
#include "config_struct.h"
#include "build_version.h"
#include "debug.h"
#include "rbac.h"

// External functions from http_server.cpp
extern void http_server_stat_request(void);
extern void http_server_stat_success(void);

static const char *TAG = "SSE";

/* ============================================================================
 * INTERNAL STATE
 * ============================================================================ */

static volatile int sse_active_clients = 0;
static int sse_listen_fd = -1;
static uint16_t sse_port = 0;
static TaskHandle_t sse_accept_task_handle = NULL;

// Client registry for CLI visibility and disconnect control
typedef struct {
  volatile bool active;
  volatile bool disconnect_requested;
  int fd;
  uint32_t connected_ms;
  uint32_t ip_addr;       // IPv4 in network byte order
  char username[24];      // RBAC username (or "(legacy)" / "(no-auth)")
  uint8_t topics;         // Subscribed topic bitmask
} SseClientInfo;

static SseClientInfo sse_clients[SSE_MAX_CLIENTS];

static int sse_registry_add(int fd, uint32_t ip, const char *username, uint8_t topics) {
  for (int i = 0; i < SSE_MAX_CLIENTS; i++) {
    if (!sse_clients[i].active) {
      sse_clients[i].fd = fd;
      sse_clients[i].ip_addr = ip;
      sse_clients[i].connected_ms = millis();
      sse_clients[i].disconnect_requested = false;
      strncpy(sse_clients[i].username, username ? username : "(unknown)", sizeof(sse_clients[i].username) - 1);
      sse_clients[i].username[sizeof(sse_clients[i].username) - 1] = '\0';
      sse_clients[i].topics = topics;
      sse_clients[i].active = true;
      return i;
    }
  }
  return -1;
}

static void sse_registry_remove(int slot) {
  if (slot >= 0 && slot < SSE_MAX_CLIENTS) {
    sse_clients[slot].active = false;
    sse_clients[slot].disconnect_requested = false;
  }
}

// Config accessors with safe fallback to defaults
static inline uint8_t sse_cfg_max_clients(void) {
  uint8_t v = g_persist_config.network.http.sse_max_clients;
  return (v >= 1 && v <= 5) ? v : SSE_MAX_CLIENTS;
}
static inline uint16_t sse_cfg_check_interval(void) {
  uint16_t v = g_persist_config.network.http.sse_check_interval_ms;
  return (v >= 50 && v <= 5000) ? v : SSE_CHECK_INTERVAL_MS;
}
static inline uint16_t sse_cfg_heartbeat(void) {
  uint16_t v = g_persist_config.network.http.sse_heartbeat_ms;
  return (v >= 1000 && v <= 60000) ? v : SSE_HEARTBEAT_MS;
}
static inline bool sse_cfg_enabled(void) {
  return g_persist_config.network.http.sse_enabled != 0;
}

// Watch list: which addresses to monitor per register type
typedef struct {
  uint16_t hr_addrs[SSE_MAX_WATCH_PER_TYPE];
  uint16_t ir_addrs[SSE_MAX_WATCH_PER_TYPE];
  uint16_t coil_addrs[SSE_MAX_WATCH_PER_TYPE];
  uint16_t di_addrs[SSE_MAX_WATCH_PER_TYPE];
  uint8_t  hr_count;
  uint8_t  ir_count;
  uint8_t  coil_count;
  uint8_t  di_count;
  bool     watch_all;   // true = scan ALL addresses (subscribe=all without explicit addrs)
} SseWatchList;

// Tracked state for change detection (per SSE client session)
typedef struct {
  uint64_t counter_values[COUNTER_COUNT];
  uint8_t  counter_enabled[COUNTER_COUNT];
  uint8_t  timer_output[TIMER_COUNT];
  uint8_t  timer_enabled[TIMER_COUNT];
  uint16_t watched_hr[SSE_MAX_WATCH_PER_TYPE];
  uint16_t watched_ir[SSE_MAX_WATCH_PER_TYPE];
  uint8_t  watched_coils[SSE_MAX_WATCH_PER_TYPE];
  uint8_t  watched_di[SSE_MAX_WATCH_PER_TYPE];
  SseWatchList watch;
  uint32_t last_heartbeat_ms;
} SseClientState;

// Full-range state for watch_all mode (heap-allocated, ~1.5 KB per client)
typedef struct {
  uint16_t hr[HOLDING_REGS_SIZE];     // 256 holding registers
  uint16_t ir[INPUT_REGS_SIZE];       // 256 input registers
  uint8_t  coils[256];                // 256 coils
  uint8_t  di[256];                   // 256 discrete inputs
} SseWatchAllState;

// Per-client task parameters
typedef struct {
  int fd;
  uint8_t topics;
  int registry_slot;
  SseWatchList watch;
} SseClientParams;

/* ============================================================================
 * UTILITY: Parse address list from query param (e.g. "0,5,10-15")
 * ============================================================================ */

static uint8_t sse_parse_addr_list(const char *str, uint16_t *addrs, uint8_t max_count, uint16_t max_addr)
{
  uint8_t count = 0;
  const char *p = str;

  while (*p && count < max_count) {
    while (*p == ',' || *p == ' ') p++;
    if (!*p) break;

    char *end;
    long start = strtol(p, &end, 10);
    if (end == p) break;
    p = end;

    if (*p == '-') {
      p++;
      long range_end = strtol(p, &end, 10);
      if (end == p) break;
      p = end;
      if (start < 0) start = 0;
      if (range_end > max_addr) range_end = max_addr;
      for (long a = start; a <= range_end && count < max_count; a++) {
        addrs[count++] = (uint16_t)a;
      }
    } else {
      if (start >= 0 && start <= max_addr) {
        addrs[count++] = (uint16_t)start;
      }
    }
  }
  return count;
}

/* ============================================================================
 * UTILITY: Extract query parameter value from query string
 * ============================================================================ */

static bool sse_get_query_param(const char *query, const char *key, char *value, size_t value_len)
{
  if (!query || !key) return false;

  size_t key_len = strlen(key);
  const char *p = query;

  while ((p = strstr(p, key)) != NULL) {
    // Check it's at start or preceded by & or ?
    if (p != query && *(p - 1) != '&' && *(p - 1) != '?') {
      p += key_len;
      continue;
    }
    if (p[key_len] != '=') {
      p += key_len;
      continue;
    }

    const char *val_start = p + key_len + 1;
    const char *val_end = strchr(val_start, '&');
    size_t len = val_end ? (size_t)(val_end - val_start) : strlen(val_start);
    if (len >= value_len) len = value_len - 1;
    memcpy(value, val_start, len);
    value[len] = '\0';
    return true;
  }
  return false;
}

/* ============================================================================
 * UTILITY: Parse topics + watch lists from query string
 * ============================================================================ */

static uint8_t sse_parse_query(const char *query, SseWatchList *watch)
{
  memset(watch, 0, sizeof(SseWatchList));

  if (!query || !*query) {
    // No query at all → watch_all mode (scan everything)
    watch->watch_all = true;
    return SSE_TOPIC_ALL;
  }

  char subscribe[64] = {0};
  uint8_t topics = 0;
  bool is_subscribe_all = false;
  if (sse_get_query_param(query, "subscribe", subscribe, sizeof(subscribe))) {
    if (strstr(subscribe, "counters")) topics |= SSE_TOPIC_COUNTERS;
    if (strstr(subscribe, "timers"))   topics |= SSE_TOPIC_TIMERS;
    if (strstr(subscribe, "registers")) topics |= SSE_TOPIC_REGISTERS;
    if (strstr(subscribe, "system"))   topics |= SSE_TOPIC_SYSTEM;
    if (strstr(subscribe, "all"))      { topics = SSE_TOPIC_ALL; is_subscribe_all = true; }
  }
  if (!topics) { topics = SSE_TOPIC_ALL; is_subscribe_all = true; }

  if (topics & SSE_TOPIC_REGISTERS) {
    char param[128] = {0};
    if (sse_get_query_param(query, "hr", param, sizeof(param)))
      watch->hr_count = sse_parse_addr_list(param, watch->hr_addrs, SSE_MAX_WATCH_PER_TYPE, 159);
    if (sse_get_query_param(query, "ir", param, sizeof(param)))
      watch->ir_count = sse_parse_addr_list(param, watch->ir_addrs, SSE_MAX_WATCH_PER_TYPE, 159);
    if (sse_get_query_param(query, "coils", param, sizeof(param)))
      watch->coil_count = sse_parse_addr_list(param, watch->coil_addrs, SSE_MAX_WATCH_PER_TYPE, 255);
    if (sse_get_query_param(query, "di", param, sizeof(param)))
      watch->di_count = sse_parse_addr_list(param, watch->di_addrs, SSE_MAX_WATCH_PER_TYPE, 255);

    if (watch->hr_count == 0 && watch->ir_count == 0 &&
        watch->coil_count == 0 && watch->di_count == 0) {
      if (is_subscribe_all) {
        // subscribe=all without explicit addresses → scan ALL registers
        watch->watch_all = true;
      } else {
        // subscribe=registers without addresses → default HR 0-15
        watch->hr_count = 16;
        for (int i = 0; i < 16; i++) watch->hr_addrs[i] = i;
      }
    }
  }

  return topics;
}

/* ============================================================================
 * UTILITY: Send raw data on socket (with retry)
 * ============================================================================ */

static bool sse_sock_send(int fd, const char *data, int len)
{
  int sent = 0;
  int retries = 0;
  while (sent < len) {
    int n = send(fd, data + sent, len - sent, 0);
    if (n > 0) {
      sent += n;
      retries = 0;
    } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && retries < 5) {
      // TCP send buffer full — brief retry (common under load)
      retries++;
      vTaskDelay(pdMS_TO_TICKS(10));
    } else {
      return false;  // Real error or too many retries
    }
  }
  return true;
}

/* ============================================================================
 * UTILITY: Send SSE-formatted event on raw socket
 * ============================================================================ */

static bool sse_send_event_fd(int fd, const char *event_name, const char *data)
{
  char buf[384];
  int len = snprintf(buf, sizeof(buf), "event: %s\ndata: %s\n\n", event_name, data);
  if (len <= 0 || len >= (int)sizeof(buf)) return false;
  return sse_sock_send(fd, buf, len);
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
  for (int i = 0; i < state->watch.hr_count; i++)
    state->watched_hr[i] = registers_get_holding_register(state->watch.hr_addrs[i]);
  for (int i = 0; i < state->watch.ir_count; i++)
    state->watched_ir[i] = registers_get_input_register(state->watch.ir_addrs[i]);
  for (int i = 0; i < state->watch.coil_count; i++)
    state->watched_coils[i] = registers_get_coil(state->watch.coil_addrs[i]);
  for (int i = 0; i < state->watch.di_count; i++)
    state->watched_di[i] = registers_get_discrete_input(state->watch.di_addrs[i]);
}

/* ============================================================================
 * AUTH: Check Basic Auth credentials from raw HTTP header
 * ============================================================================ */

static int sse_check_auth(const char *auth_header)
{
  return rbac_check_sse(auth_header);
}

/* ============================================================================
 * PER-CLIENT TASK: SSE event loop on raw socket
 * ============================================================================ */

static void sse_client_task(void *arg)
{
  SseClientParams *params = (SseClientParams *)arg;
  int fd = params->fd;
  uint8_t topics = params->topics;
  int reg_slot = params->registry_slot;
  SseWatchList watch;
  memcpy(&watch, &params->watch, sizeof(SseWatchList));
  free(params);

  sse_active_clients++;
  if (watch.watch_all) {
    ESP_LOGI(TAG, "SSE client task started (fd=%d, topics=0x%02x, watch_all=true, active=%d)",
      fd, topics, (int)sse_active_clients);
  } else {
    ESP_LOGI(TAG, "SSE client task started (fd=%d, topics=0x%02x, hr=%d, ir=%d, coils=%d, di=%d, active=%d)",
      fd, topics, watch.hr_count, watch.ir_count, watch.coil_count, watch.di_count, (int)sse_active_clients);
  }

  // Send initial connected event (heap-allocated to save stack for main loop)
  {
    char *init_buf = (char *)malloc(384);
    if (!init_buf) { goto done; }
    if (watch.watch_all) {
      snprintf(init_buf, 384,
        "{\"status\":\"connected\",\"topics\":\"0x%02x\",\"max_clients\":%d,\"active_clients\":%d,\"port\":%d,"
        "\"watching\":{\"mode\":\"all\",\"hr\":%d,\"ir\":%d,\"coils\":256,\"di\":256}}",
        topics, (int)sse_cfg_max_clients(), (int)sse_active_clients, sse_port,
        (int)HOLDING_REGS_SIZE, (int)INPUT_REGS_SIZE);
    } else {
      snprintf(init_buf, 384,
        "{\"status\":\"connected\",\"topics\":\"0x%02x\",\"max_clients\":%d,\"active_clients\":%d,\"port\":%d,"
        "\"watching\":{\"hr\":%d,\"ir\":%d,\"coils\":%d,\"di\":%d}}",
        topics, (int)sse_cfg_max_clients(), (int)sse_active_clients, sse_port,
        watch.hr_count, watch.ir_count, watch.coil_count, watch.di_count);
    }
    bool ok = sse_send_event_fd(fd, "connected", init_buf);
    free(init_buf);
    if (!ok) goto done;
  }

  // Initialize change detection (heap-allocated to save stack)
  {
    SseClientState *state = (SseClientState *)malloc(sizeof(SseClientState));
    if (!state) {
      ESP_LOGE(TAG, "Failed to allocate SSE client state");
      goto done;
    }
    memset(state, 0, sizeof(SseClientState));
    memcpy(&state->watch, &watch, sizeof(SseWatchList));
    sse_snapshot_counters(state);
    if (!watch.watch_all) {
      sse_snapshot_registers(state);
    }
    sse_snapshot_timers(state);
    state->last_heartbeat_ms = millis();

    // Allocate full-range state for watch_all mode (~1.5 KB)
    SseWatchAllState *all_state = NULL;
    if (watch.watch_all) {
      all_state = (SseWatchAllState *)calloc(1, sizeof(SseWatchAllState));
      if (!all_state) {
        ESP_LOGE(TAG, "Failed to allocate watch_all state");
        free(state);
        goto done;
      }
      // Snapshot all registers
      for (int i = 0; i < HOLDING_REGS_SIZE; i++)
        all_state->hr[i] = registers_get_holding_register(i);
      for (int i = 0; i < INPUT_REGS_SIZE; i++)
        all_state->ir[i] = registers_get_input_register(i);
      for (int i = 0; i < 256; i++) {
        all_state->coils[i] = registers_get_coil(i);
        all_state->di[i] = registers_get_discrete_input(i);
      }
      ESP_LOGI(TAG, "watch_all mode: monitoring %d HR + %d IR + 256 coils + 256 DI",
        HOLDING_REGS_SIZE, INPUT_REGS_SIZE);
    }

    // Main SSE loop
    while (true) {
      vTaskDelay(pdMS_TO_TICKS(sse_cfg_check_interval()));

      // Check if CLI requested disconnect
      if (reg_slot >= 0 && sse_clients[reg_slot].disconnect_requested) {
        ESP_LOGI(TAG, "SSE client %d disconnect requested via CLI", reg_slot);
        free(all_state);
        free(state);
        goto done;
      }

      // Counter change detection
      if (topics & SSE_TOPIC_COUNTERS) {
        for (int i = 0; i < COUNTER_COUNT; i++) {
          uint64_t val = counter_engine_get_value(i + 1);
          CounterConfig cfg;
          uint8_t enabled = counter_engine_get_config(i + 1, &cfg) ? cfg.enabled : 0;
          if (val != state->counter_values[i] || enabled != state->counter_enabled[i]) {
            char data[128];
            snprintf(data, sizeof(data),
              "{\"id\":%d,\"value\":%llu,\"enabled\":%s,\"running\":%s}",
              i + 1, (unsigned long long)val,
              enabled ? "true" : "false",
              (cfg.enabled && cfg.mode_enable != COUNTER_MODE_DISABLED) ? "true" : "false");
            if (!sse_send_event_fd(fd, "counter", data)) { free(all_state); free(state); goto done; }
            state->counter_values[i] = val;
            state->counter_enabled[i] = enabled;
          }
        }
      }

      // Timer change detection
      if (topics & SSE_TOPIC_TIMERS) {
        for (int i = 0; i < TIMER_COUNT; i++) {
          TimerConfig cfg;
          if (timer_engine_get_config(i + 1, &cfg)) {
            uint8_t output = (cfg.output_coil != 0xFFFF) ? registers_get_coil(cfg.output_coil) : 0;
            if (output != state->timer_output[i] || cfg.enabled != state->timer_enabled[i]) {
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
              if (!sse_send_event_fd(fd, "timer", data)) { free(all_state); free(state); goto done; }
              state->timer_output[i] = output;
              state->timer_enabled[i] = cfg.enabled;
            }
          }
        }
      }

      // Register change detection
      if ((topics & SSE_TOPIC_REGISTERS) && watch.watch_all && all_state) {
        // watch_all mode: scan ALL addresses
        char data[64];
        for (int i = 0; i < HOLDING_REGS_SIZE; i++) {
          uint16_t val = registers_get_holding_register(i);
          if (val != all_state->hr[i]) {
            snprintf(data, sizeof(data), "{\"type\":\"hr\",\"addr\":%d,\"value\":%u}", i, val);
            if (!sse_send_event_fd(fd, "register", data)) { free(all_state); free(state); goto done; }
            all_state->hr[i] = val;
          }
        }
        for (int i = 0; i < INPUT_REGS_SIZE; i++) {
          uint16_t val = registers_get_input_register(i);
          if (val != all_state->ir[i]) {
            snprintf(data, sizeof(data), "{\"type\":\"ir\",\"addr\":%d,\"value\":%u}", i, val);
            if (!sse_send_event_fd(fd, "register", data)) { free(all_state); free(state); goto done; }
            all_state->ir[i] = val;
          }
        }
        for (int i = 0; i < 256; i++) {
          uint8_t val = registers_get_coil(i);
          if (val != all_state->coils[i]) {
            snprintf(data, sizeof(data), "{\"type\":\"coil\",\"addr\":%d,\"value\":%u}", i, val);
            if (!sse_send_event_fd(fd, "register", data)) { free(all_state); free(state); goto done; }
            all_state->coils[i] = val;
          }
        }
        for (int i = 0; i < 256; i++) {
          uint8_t val = registers_get_discrete_input(i);
          if (val != all_state->di[i]) {
            snprintf(data, sizeof(data), "{\"type\":\"di\",\"addr\":%d,\"value\":%u}", i, val);
            if (!sse_send_event_fd(fd, "register", data)) { free(all_state); free(state); goto done; }
            all_state->di[i] = val;
          }
        }
      } else if (topics & SSE_TOPIC_REGISTERS) {
        // Watch-list mode: scan only specified addresses
        for (int i = 0; i < state->watch.hr_count; i++) {
          uint16_t addr = state->watch.hr_addrs[i];
          uint16_t val = registers_get_holding_register(addr);
          if (val != state->watched_hr[i]) {
            char data[64];
            snprintf(data, sizeof(data), "{\"type\":\"hr\",\"addr\":%u,\"value\":%u}", addr, val);
            if (!sse_send_event_fd(fd, "register", data)) { free(all_state); free(state); goto done; }
            state->watched_hr[i] = val;
          }
        }
        for (int i = 0; i < state->watch.ir_count; i++) {
          uint16_t addr = state->watch.ir_addrs[i];
          uint16_t val = registers_get_input_register(addr);
          if (val != state->watched_ir[i]) {
            char data[64];
            snprintf(data, sizeof(data), "{\"type\":\"ir\",\"addr\":%u,\"value\":%u}", addr, val);
            if (!sse_send_event_fd(fd, "register", data)) { free(all_state); free(state); goto done; }
            state->watched_ir[i] = val;
          }
        }
        for (int i = 0; i < state->watch.coil_count; i++) {
          uint16_t addr = state->watch.coil_addrs[i];
          uint8_t val = registers_get_coil(addr);
          if (val != state->watched_coils[i]) {
            char data[64];
            snprintf(data, sizeof(data), "{\"type\":\"coil\",\"addr\":%u,\"value\":%u}", addr, val);
            if (!sse_send_event_fd(fd, "register", data)) { free(all_state); free(state); goto done; }
            state->watched_coils[i] = val;
          }
        }
        for (int i = 0; i < state->watch.di_count; i++) {
          uint16_t addr = state->watch.di_addrs[i];
          uint8_t val = registers_get_discrete_input(addr);
          if (val != state->watched_di[i]) {
            char data[64];
            snprintf(data, sizeof(data), "{\"type\":\"di\",\"addr\":%u,\"value\":%u}", addr, val);
            if (!sse_send_event_fd(fd, "register", data)) { free(all_state); free(state); goto done; }
            state->watched_di[i] = val;
          }
        }
      }

      // Heartbeat keepalive
      uint32_t now = millis();
      if (now - state->last_heartbeat_ms >= sse_cfg_heartbeat()) {
        char data[96];
        snprintf(data, sizeof(data),
          "{\"uptime_ms\":%lu,\"heap_free\":%lu,\"sse_clients\":%d}",
          (unsigned long)now, (unsigned long)ESP.getFreeHeap(), (int)sse_active_clients);
        if (!sse_send_event_fd(fd, "heartbeat", data)) { free(all_state); free(state); goto done; }
        state->last_heartbeat_ms = now;
      }
    }
    free(all_state);
    free(state);
  }

done:
  close(fd);
  sse_registry_remove(reg_slot);
  if (sse_active_clients > 0) sse_active_clients--;
  ESP_LOGI(TAG, "SSE client disconnected (slot=%d, active=%d)", reg_slot, (int)sse_active_clients);
  vTaskDelete(NULL);
}

/* ============================================================================
 * ACCEPTOR TASK: Listen for new SSE connections
 * ============================================================================ */

static void sse_accept_task(void *arg)
{
  ESP_LOGI(TAG, "SSE acceptor task running on port %d", sse_port);

  while (true) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_fd = accept(sse_listen_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (client_fd < 0) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // Rate limit: prevent rapid reconnect storms (e.g., Node-RED auto-reconnect)
    // Check heap before allocating task resources
    if (esp_get_free_heap_size() < 10000) {
      ESP_LOGW(TAG, "Low heap (%lu), rejecting SSE client", (unsigned long)esp_get_free_heap_size());
      const char *resp = "HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n\r\n";
      send(client_fd, resp, strlen(resp), 0);
      close(client_fd);
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    // TCP_NODELAY: Disable Nagle algorithm — SSE events are small (64-384 bytes)
    // and must be pushed immediately, not buffered waiting for ACK
    int nodelay = 1;
    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    // Set socket timeouts for recv/send
    struct timeval tv;
    tv.tv_sec = 30;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // TCP keepalive
    int enable = 1;
    int idle = 15, interval = 5, count = 3;
    setsockopt(client_fd, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable));
    setsockopt(client_fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    setsockopt(client_fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
    setsockopt(client_fd, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));

    // Read HTTP request (max 1KB)
    char req_buf[1024] = {0};
    int req_len = recv(client_fd, req_buf, sizeof(req_buf) - 1, 0);
    if (req_len <= 0) {
      close(client_fd);
      continue;
    }
    req_buf[req_len] = '\0';

    // Check it's a GET request
    if (strncmp(req_buf, "GET ", 4) != 0) {
      const char *resp = "HTTP/1.1 405 Method Not Allowed\r\nConnection: close\r\n\r\n";
      send(client_fd, resp, strlen(resp), 0);
      close(client_fd);
      continue;
    }

    // Check API enabled
    if (!g_persist_config.network.http.api_enabled) {
      const char *resp = "HTTP/1.1 403 Forbidden\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"error\":\"API disabled\",\"status\":403}";
      send(client_fd, resp, strlen(resp), 0);
      close(client_fd);
      continue;
    }

    // Check Basic Auth
    char *auth_line = strstr(req_buf, "Authorization:");
    if (!auth_line) auth_line = strstr(req_buf, "authorization:");
    char auth_value[256] = {0};
    if (auth_line) {
      auth_line += 14; // skip "Authorization:"
      while (*auth_line == ' ') auth_line++;
      char *eol = strstr(auth_line, "\r\n");
      if (eol) {
        size_t len = eol - auth_line;
        if (len < sizeof(auth_value)) {
          memcpy(auth_value, auth_line, len);
          auth_value[len] = '\0';
        }
      }
    }

    int sse_user_idx = sse_check_auth(auth_value);
    if (sse_user_idx < 0) {
      const char *resp = "HTTP/1.1 401 Unauthorized\r\n"
        "Content-Type: application/json\r\n"
        "WWW-Authenticate: Basic realm=\"Modbus ESP32\"\r\n"
        "Connection: close\r\n\r\n"
        "{\"error\":\"Authentication required\",\"status\":401}";
      send(client_fd, resp, strlen(resp), 0);
      close(client_fd);
      continue;
    }

    // RBAC: require MONITOR or API role for SSE
    if (!rbac_has_role(sse_user_idx, ROLE_MONITOR | ROLE_API)) {
      const char *resp = "HTTP/1.1 403 Forbidden\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n\r\n"
        "{\"error\":\"MONITOR or API role required\",\"status\":403}";
      send(client_fd, resp, strlen(resp), 0);
      close(client_fd);
      continue;
    }

    // Check client limit
    if (sse_active_clients >= sse_cfg_max_clients()) {
      const char *resp = "HTTP/1.1 503 Service Unavailable\r\n"
        "Content-Type: application/json\r\nConnection: close\r\n\r\n"
        "{\"error\":\"Max SSE clients reached\",\"status\":503}";
      send(client_fd, resp, strlen(resp), 0);
      close(client_fd);
      continue;
    }

    // Extract query string from GET /api/events?...
    char *query = NULL;
    char *qmark = strchr(req_buf + 4, '?');
    char *space = strchr(req_buf + 4, ' ');
    char query_buf[256] = {0};
    if (qmark && space && qmark < space) {
      size_t qlen = space - qmark - 1;
      if (qlen < sizeof(query_buf)) {
        memcpy(query_buf, qmark + 1, qlen);
        query_buf[qlen] = '\0';
        query = query_buf;
      }
    }

    // Parse topics + watch list
    SseWatchList watch;
    uint8_t topics = sse_parse_query(query, &watch);

    // Send HTTP SSE response headers
    const char *headers = "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/event-stream\r\n"
      "Cache-Control: no-cache\r\n"
      "Connection: keep-alive\r\n"
      "Access-Control-Allow-Origin: *\r\n"
      "X-Accel-Buffering: no\r\n\r\n";
    if (!sse_sock_send(client_fd, headers, strlen(headers))) {
      close(client_fd);
      continue;
    }

    // Resolve username for registry
    const char *sse_username = "(no-auth)";
    if (sse_user_idx >= 0 && sse_user_idx < RBAC_MAX_USERS) {
      const RbacUser *ru = rbac_get_user(sse_user_idx);
      if (ru) sse_username = ru->username;
    } else if (sse_user_idx == 99) {
      sse_username = "(legacy)";
    }

    // Register client in registry
    int slot = sse_registry_add(client_fd, client_addr.sin_addr.s_addr, sse_username, topics);
    if (slot < 0) {
      const char *resp = "HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n\r\n";
      send(client_fd, resp, strlen(resp), 0);
      close(client_fd);
      continue;
    }

    // Spawn client task
    SseClientParams *params = (SseClientParams *)malloc(sizeof(SseClientParams));
    if (!params) {
      sse_registry_remove(slot);
      close(client_fd);
      continue;
    }
    params->fd = client_fd;
    params->topics = topics;
    params->registry_slot = slot;
    memcpy(&params->watch, &watch, sizeof(SseWatchList));

    char task_name[16];
    snprintf(task_name, sizeof(task_name), "sse_%d", client_fd);
    BaseType_t ret = xTaskCreate(sse_client_task, task_name, 6144, params, 3, NULL);
    if (ret != pdPASS) {
      ESP_LOGE(TAG, "Failed to create SSE client task");
      free(params);
      sse_registry_remove(slot);
      close(client_fd);
      vTaskDelay(pdMS_TO_TICKS(1000));
    } else {
      // Cooldown after successful spawn — prevents task stacking from rapid reconnects
      vTaskDelay(pdMS_TO_TICKS(500));
    }
  }
}

/* ============================================================================
 * GET /api/events/status — SSE subsystem info (runs on MAIN server, port 80)
 * ============================================================================ */

esp_err_t api_handler_sse_status(httpd_req_t *req)
{
  http_server_stat_request();

  if (!g_persist_config.network.http.api_enabled) {
    httpd_resp_set_status(req, "403 Forbidden");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"error\":\"API disabled\",\"status\":403}");
    return ESP_OK;
  }

  extern bool http_server_check_auth(httpd_req_t *req);
  if (!http_server_check_auth(req)) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Modbus ESP32\"");
    httpd_resp_sendstr(req, "{\"error\":\"Authentication required\",\"status\":401}");
    return ESP_OK;
  }

  extern esp_err_t api_send_json(httpd_req_t *req, const char *json_str);

  char buf[320];
  snprintf(buf, sizeof(buf),
    "{\"sse_enabled\":%s,\"sse_port\":%d,\"max_clients\":%d,\"active_clients\":%d,"
    "\"check_interval_ms\":%d,\"heartbeat_ms\":%d,"
    "\"topics\":[\"counters\",\"timers\",\"registers\",\"system\"],"
    "\"endpoint\":\"http://<ip>:%d/api/events?subscribe=<topics>\"}",
    sse_cfg_enabled() ? "true" : "false",
    sse_port, (int)sse_cfg_max_clients(), (int)sse_active_clients,
    (int)sse_cfg_check_interval(), (int)sse_cfg_heartbeat(), sse_port);

  return api_send_json(req, buf);
}

/**
 * GET /api/events/clients — list connected SSE clients with details
 */
esp_err_t api_handler_sse_clients(httpd_req_t *req)
{
  http_server_stat_request();

  if (!g_persist_config.network.http.api_enabled) {
    httpd_resp_set_status(req, "403 Forbidden");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"error\":\"API disabled\",\"status\":403}");
    return ESP_OK;
  }

  extern bool http_server_check_auth(httpd_req_t *req);
  if (!http_server_check_auth(req)) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Modbus ESP32\"");
    httpd_resp_sendstr(req, "{\"error\":\"Authentication required\",\"status\":401}");
    return ESP_OK;
  }

  extern esp_err_t api_send_json(httpd_req_t *req, const char *json_str);

  SseClientInfoPublic clients[SSE_MAX_CLIENTS];
  int count = sse_get_client_info(clients);

  // Build JSON array — max ~150 bytes per client
  char buf[1024];
  int pos = 0;
  pos += snprintf(buf + pos, sizeof(buf) - pos, "{\"active_clients\":%d,\"clients\":[", count);

  bool first = true;
  for (int i = 0; i < SSE_MAX_CLIENTS && pos < (int)sizeof(buf) - 200; i++) {
    if (!clients[i].active) continue;
    uint8_t *ip = (uint8_t *)&clients[i].ip_addr;
    uint32_t uptime_s = (millis() - clients[i].connected_ms) / 1000;

    // Topics to string
    char topics_str[32];
    uint8_t t = clients[i].topics;
    if (t == 0x0F) {
      strcpy(topics_str, "all");
    } else {
      topics_str[0] = '\0';
      if (t & 0x01) strcat(topics_str, "cnt,");
      if (t & 0x02) strcat(topics_str, "tmr,");
      if (t & 0x04) strcat(topics_str, "reg,");
      if (t & 0x08) strcat(topics_str, "sys,");
      size_t len = strlen(topics_str);
      if (len > 0) topics_str[len - 1] = '\0'; // remove trailing comma
    }

    if (!first) pos += snprintf(buf + pos, sizeof(buf) - pos, ",");
    first = false;
    pos += snprintf(buf + pos, sizeof(buf) - pos,
      "{\"slot\":%d,\"ip\":\"%d.%d.%d.%d\",\"username\":\"%s\",\"topics\":\"%s\",\"uptime_s\":%lu}",
      clients[i].slot, ip[0], ip[1], ip[2], ip[3],
      clients[i].username, topics_str, (unsigned long)uptime_s);
  }
  pos += snprintf(buf + pos, sizeof(buf) - pos, "]}");

  return api_send_json(req, buf);
}

/**
 * POST /api/events/disconnect — disconnect SSE client(s)
 * Body: {"slot": N} for single, {"slot": -1} for all
 */
esp_err_t api_handler_sse_disconnect(httpd_req_t *req)
{
  http_server_stat_request();

  if (!g_persist_config.network.http.api_enabled) {
    httpd_resp_set_status(req, "403 Forbidden");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"error\":\"API disabled\",\"status\":403}");
    return ESP_OK;
  }

  extern bool http_server_check_auth(httpd_req_t *req);
  if (!http_server_check_auth(req)) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Modbus ESP32\"");
    httpd_resp_sendstr(req, "{\"error\":\"Authentication required\",\"status\":401}");
    return ESP_OK;
  }

  extern esp_err_t api_send_json(httpd_req_t *req, const char *json_str);
  extern esp_err_t api_send_error(httpd_req_t *req, int code, const char *msg);

  char body[128];
  int ret = httpd_req_recv(req, body, sizeof(body) - 1);
  if (ret <= 0) {
    return api_send_error(req, 400, "Missing request body");
  }
  body[ret] = '\0';

  // Parse slot from JSON — simple extraction
  int slot = -2; // invalid default
  const char *slot_ptr = strstr(body, "\"slot\"");
  if (slot_ptr) {
    slot_ptr = strchr(slot_ptr, ':');
    if (slot_ptr) slot = atoi(slot_ptr + 1);
  }

  char buf[128];
  if (slot == -1) {
    // Disconnect all
    int n = sse_disconnect_all();
    snprintf(buf, sizeof(buf), "{\"ok\":true,\"disconnected\":%d}", n);
  } else if (slot >= 0 && slot < SSE_MAX_CLIENTS) {
    bool ok = sse_disconnect_client(slot);
    snprintf(buf, sizeof(buf), "{\"ok\":%s,\"slot\":%d}", ok ? "true" : "false", slot);
  } else {
    return api_send_error(req, 400, "Invalid slot number");
  }

  return api_send_json(req, buf);
}

/* ============================================================================
 * INIT / START / STOP
 * ============================================================================ */

void sse_init(void)
{
  sse_active_clients = 0;
  sse_listen_fd = -1;
  sse_accept_task_handle = NULL;
  memset(sse_clients, 0, sizeof(sse_clients));
  ESP_LOGI(TAG, "SSE subsystem initialized (max %d clients, interval %dms, heartbeat %dms)",
    (int)sse_cfg_max_clients(), (int)sse_cfg_check_interval(), (int)sse_cfg_heartbeat());
}

int sse_start(uint16_t port)
{
  if (sse_listen_fd >= 0) {
    ESP_LOGI(TAG, "SSE server already running");
    return 0;
  }

  sse_port = port;

  // Create TCP listening socket
  sse_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (sse_listen_fd < 0) {
    ESP_LOGE(TAG, "Failed to create SSE socket");
    return -1;
  }

  int opt = 1;
  setsockopt(sse_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(sse_port);
  addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(sse_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    ESP_LOGE(TAG, "Failed to bind SSE socket on port %d", sse_port);
    close(sse_listen_fd);
    sse_listen_fd = -1;
    return -1;
  }

  if (listen(sse_listen_fd, sse_cfg_max_clients() + 1) < 0) {
    ESP_LOGE(TAG, "Failed to listen on SSE socket");
    close(sse_listen_fd);
    sse_listen_fd = -1;
    return -1;
  }

  // Start acceptor task
  BaseType_t ret = xTaskCreate(sse_accept_task, "sse_accept", 4096, NULL, 4, &sse_accept_task_handle);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create SSE acceptor task");
    close(sse_listen_fd);
    sse_listen_fd = -1;
    return -1;
  }

  ESP_LOGI(TAG, "SSE server started on port %d (raw TCP, multi-client)", sse_port);
  return 0;
}

void sse_stop(void)
{
  if (sse_accept_task_handle) {
    vTaskDelete(sse_accept_task_handle);
    sse_accept_task_handle = NULL;
  }
  if (sse_listen_fd >= 0) {
    close(sse_listen_fd);
    sse_listen_fd = -1;
  }
  ESP_LOGI(TAG, "SSE server stopped");
}

int sse_get_client_count(void)
{
  return sse_active_clients;
}

uint16_t sse_get_port(void)
{
  return sse_port;
}

int sse_get_client_info(SseClientInfoPublic *out)
{
  int count = 0;
  for (int i = 0; i < SSE_MAX_CLIENTS; i++) {
    out[i].active = sse_clients[i].active;
    out[i].slot = i;
    out[i].fd = sse_clients[i].fd;
    out[i].ip_addr = sse_clients[i].ip_addr;
    out[i].connected_ms = sse_clients[i].connected_ms;
    memcpy(out[i].username, sse_clients[i].username, sizeof(out[i].username));
    out[i].topics = sse_clients[i].topics;
    if (sse_clients[i].active) count++;
  }
  return count;
}

bool sse_disconnect_client(int slot)
{
  if (slot < 0 || slot >= SSE_MAX_CLIENTS) return false;
  if (!sse_clients[slot].active) return false;
  sse_clients[slot].disconnect_requested = true;
  ESP_LOGI(TAG, "Disconnect requested for SSE client slot %d", slot);
  return true;
}

int sse_disconnect_all(void)
{
  int count = 0;
  for (int i = 0; i < SSE_MAX_CLIENTS; i++) {
    if (sse_clients[i].active) {
      sse_clients[i].disconnect_requested = true;
      count++;
    }
  }
  if (count > 0) {
    ESP_LOGI(TAG, "Disconnect requested for all %d SSE clients", count);
  }
  return count;
}
