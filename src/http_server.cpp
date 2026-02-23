/**
 * @file http_server.cpp
 * @brief HTTP REST API server implementation (v6.0.0+)
 *
 * LAYER 1.5: Protocol (same level as Telnet server)
 * Responsibility: HTTP server lifecycle and request routing
 *
 * Uses ESP-IDF esp_http_server wrapped for simplicity.
 */

#include <string.h>
#include <stdio.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <Arduino.h>
#include <mbedtls/base64.h>

#include "http_server.h"
#include "https_wrapper.h"
#include "api_handlers.h"
#include "constants.h"
#include "debug.h"

static const char *TAG = "HTTP_SRV";

/* ============================================================================
 * INTERNAL STATE
 * ============================================================================ */

static struct {
  httpd_handle_t server;
  HttpConfig config;
  HttpServerStats stats;
  uint8_t initialized;
  uint8_t running;
  uint8_t tls_active;
} http_state = {
  .server = NULL,
  .config = {0},
  .stats = {0},
  .initialized = 0,
  .running = 0,
  .tls_active = 0
};

/* ============================================================================
 * FORWARD DECLARATIONS FOR URI HANDLERS
 * ============================================================================ */

// These are implemented in api_handlers.cpp
extern esp_err_t api_handler_endpoints(httpd_req_t *req);
extern esp_err_t api_handler_status(httpd_req_t *req);
extern esp_err_t api_handler_config_get(httpd_req_t *req);
extern esp_err_t api_handler_counters(httpd_req_t *req);
extern esp_err_t api_handler_counter_single(httpd_req_t *req);
extern esp_err_t api_handler_timers(httpd_req_t *req);
extern esp_err_t api_handler_timer_single(httpd_req_t *req);
extern esp_err_t api_handler_hr_read(httpd_req_t *req);
extern esp_err_t api_handler_hr_write(httpd_req_t *req);
extern esp_err_t api_handler_ir_read(httpd_req_t *req);
extern esp_err_t api_handler_coil_read(httpd_req_t *req);
extern esp_err_t api_handler_coil_write(httpd_req_t *req);
extern esp_err_t api_handler_di_read(httpd_req_t *req);
extern esp_err_t api_handler_gpio(httpd_req_t *req);
extern esp_err_t api_handler_gpio_single(httpd_req_t *req);
extern esp_err_t api_handler_gpio_write(httpd_req_t *req);
extern esp_err_t api_handler_logic(httpd_req_t *req);
extern esp_err_t api_handler_logic_single(httpd_req_t *req);
extern esp_err_t api_handler_logic_delete(httpd_req_t *req);
extern esp_err_t api_handler_debug_get(httpd_req_t *req);
extern esp_err_t api_handler_debug_set(httpd_req_t *req);
extern esp_err_t api_handler_system_reboot(httpd_req_t *req);
extern esp_err_t api_handler_system_save(httpd_req_t *req);
extern esp_err_t api_handler_system_load(httpd_req_t *req);
extern esp_err_t api_handler_system_defaults(httpd_req_t *req);
// GAP-ANALYSE handlers (v6.1.0+)
extern esp_err_t api_handler_counter_delete(httpd_req_t *req);
extern esp_err_t api_handler_timer_config_post(httpd_req_t *req);
extern esp_err_t api_handler_timer_delete(httpd_req_t *req);
extern esp_err_t api_handler_modbus_get(httpd_req_t *req);
extern esp_err_t api_handler_modbus_post(httpd_req_t *req);
extern esp_err_t api_handler_wifi_get(httpd_req_t *req);
extern esp_err_t api_handler_wifi_post(httpd_req_t *req);
extern esp_err_t api_handler_ethernet_get(httpd_req_t *req);
extern esp_err_t api_handler_ethernet_post(httpd_req_t *req);
extern esp_err_t api_handler_http_config_post(httpd_req_t *req);
extern esp_err_t api_handler_gpio_config_delete(httpd_req_t *req);
extern esp_err_t api_handler_logic_settings_post(httpd_req_t *req);
extern esp_err_t api_handler_modules_get(httpd_req_t *req);
extern esp_err_t api_handler_modules_post(httpd_req_t *req);
// Backup/restore handlers
extern esp_err_t api_handler_system_backup(httpd_req_t *req);
extern esp_err_t api_handler_system_restore(httpd_req_t *req);

/* ============================================================================
 * URI DEFINITIONS
 * ============================================================================ */

static const httpd_uri_t uri_endpoints = {
  .uri      = "/api",
  .method   = HTTP_GET,
  .handler  = api_handler_endpoints,
  .user_ctx = NULL
};

static const httpd_uri_t uri_endpoints_slash = {
  .uri      = "/api/",
  .method   = HTTP_GET,
  .handler  = api_handler_endpoints,
  .user_ctx = NULL
};

static const httpd_uri_t uri_status = {
  .uri      = "/api/status",
  .method   = HTTP_GET,
  .handler  = api_handler_status,
  .user_ctx = NULL
};

static const httpd_uri_t uri_counters = {
  .uri      = "/api/counters",
  .method   = HTTP_GET,
  .handler  = api_handler_counters,
  .user_ctx = NULL
};

static const httpd_uri_t uri_config = {
  .uri      = "/api/config",
  .method   = HTTP_GET,
  .handler  = api_handler_config_get,
  .user_ctx = NULL
};

// Single wildcard handles GET /api/counters/{id} and internal suffix routing
// for POST /api/counters/{id}/reset, /start, /stop (ESP-IDF wildcard only at end)
static const httpd_uri_t uri_counter_single_get = {
  .uri      = "/api/counters/*",
  .method   = HTTP_GET,
  .handler  = api_handler_counter_single,
  .user_ctx = NULL
};

static const httpd_uri_t uri_counter_single_post = {
  .uri      = "/api/counters/*",
  .method   = HTTP_POST,
  .handler  = api_handler_counter_single,
  .user_ctx = NULL
};

static const httpd_uri_t uri_timers = {
  .uri      = "/api/timers",
  .method   = HTTP_GET,
  .handler  = api_handler_timers,
  .user_ctx = NULL
};

static const httpd_uri_t uri_timer_single = {
  .uri      = "/api/timers/*",
  .method   = HTTP_GET,
  .handler  = api_handler_timer_single,
  .user_ctx = NULL
};

static const httpd_uri_t uri_hr_read = {
  .uri      = "/api/registers/hr/*",
  .method   = HTTP_GET,
  .handler  = api_handler_hr_read,
  .user_ctx = NULL
};

static const httpd_uri_t uri_hr_write = {
  .uri      = "/api/registers/hr/*",
  .method   = HTTP_POST,
  .handler  = api_handler_hr_write,
  .user_ctx = NULL
};

static const httpd_uri_t uri_ir_read = {
  .uri      = "/api/registers/ir/*",
  .method   = HTTP_GET,
  .handler  = api_handler_ir_read,
  .user_ctx = NULL
};

static const httpd_uri_t uri_coil_read = {
  .uri      = "/api/registers/coils/*",
  .method   = HTTP_GET,
  .handler  = api_handler_coil_read,
  .user_ctx = NULL
};

static const httpd_uri_t uri_coil_write = {
  .uri      = "/api/registers/coils/*",
  .method   = HTTP_POST,
  .handler  = api_handler_coil_write,
  .user_ctx = NULL
};

static const httpd_uri_t uri_di_read = {
  .uri      = "/api/registers/di/*",
  .method   = HTTP_GET,
  .handler  = api_handler_di_read,
  .user_ctx = NULL
};

static const httpd_uri_t uri_gpio = {
  .uri      = "/api/gpio",
  .method   = HTTP_GET,
  .handler  = api_handler_gpio,
  .user_ctx = NULL
};

static const httpd_uri_t uri_gpio_single = {
  .uri      = "/api/gpio/*",
  .method   = HTTP_GET,
  .handler  = api_handler_gpio_single,
  .user_ctx = NULL
};

static const httpd_uri_t uri_gpio_write = {
  .uri      = "/api/gpio/*",
  .method   = HTTP_POST,
  .handler  = api_handler_gpio_write,
  .user_ctx = NULL
};

static const httpd_uri_t uri_logic = {
  .uri      = "/api/logic",
  .method   = HTTP_GET,
  .handler  = api_handler_logic,
  .user_ctx = NULL
};

// Single wildcard handles all /api/logic/{id}/* with internal suffix routing
// (ESP-IDF wildcard only supports * at end of URI)
static const httpd_uri_t uri_logic_single_get = {
  .uri      = "/api/logic/*",
  .method   = HTTP_GET,
  .handler  = api_handler_logic_single,
  .user_ctx = NULL
};

static const httpd_uri_t uri_logic_single_post = {
  .uri      = "/api/logic/*",
  .method   = HTTP_POST,
  .handler  = api_handler_logic_single,
  .user_ctx = NULL
};

static const httpd_uri_t uri_logic_single_delete = {
  .uri      = "/api/logic/*",
  .method   = HTTP_DELETE,
  .handler  = api_handler_logic_delete,
  .user_ctx = NULL
};

static const httpd_uri_t uri_debug_get = {
  .uri      = "/api/debug",
  .method   = HTTP_GET,
  .handler  = api_handler_debug_get,
  .user_ctx = NULL
};

static const httpd_uri_t uri_debug_set = {
  .uri      = "/api/debug",
  .method   = HTTP_POST,
  .handler  = api_handler_debug_set,
  .user_ctx = NULL
};

static const httpd_uri_t uri_system_reboot = {
  .uri      = "/api/system/reboot",
  .method   = HTTP_POST,
  .handler  = api_handler_system_reboot,
  .user_ctx = NULL
};

static const httpd_uri_t uri_system_save = {
  .uri      = "/api/system/save",
  .method   = HTTP_POST,
  .handler  = api_handler_system_save,
  .user_ctx = NULL
};

static const httpd_uri_t uri_system_load = {
  .uri      = "/api/system/load",
  .method   = HTTP_POST,
  .handler  = api_handler_system_load,
  .user_ctx = NULL
};

static const httpd_uri_t uri_system_defaults = {
  .uri      = "/api/system/defaults",
  .method   = HTTP_POST,
  .handler  = api_handler_system_defaults,
  .user_ctx = NULL
};

/* ============================================================================
 * GAP-ANALYSE URI DEFINITIONS (v6.1.0+)
 * ============================================================================ */

// Counter DELETE (GAP-16)
static const httpd_uri_t uri_counter_delete = {
  .uri      = "/api/counters/*",
  .method   = HTTP_DELETE,
  .handler  = api_handler_counter_delete,
  .user_ctx = NULL
};

// Timer POST (GAP-3) - config via wildcard
static const httpd_uri_t uri_timer_config_post = {
  .uri      = "/api/timers/*",
  .method   = HTTP_POST,
  .handler  = api_handler_timer_config_post,
  .user_ctx = NULL
};

// Timer DELETE (GAP-3)
static const httpd_uri_t uri_timer_delete = {
  .uri      = "/api/timers/*",
  .method   = HTTP_DELETE,
  .handler  = api_handler_timer_delete,
  .user_ctx = NULL
};

// Modbus GET (GAP-4, GAP-5, GAP-18) - slave/master suffix routing
static const httpd_uri_t uri_modbus_get = {
  .uri      = "/api/modbus/*",
  .method   = HTTP_GET,
  .handler  = api_handler_modbus_get,
  .user_ctx = NULL
};

// Modbus POST (GAP-4, GAP-5, GAP-18)
static const httpd_uri_t uri_modbus_post = {
  .uri      = "/api/modbus/*",
  .method   = HTTP_POST,
  .handler  = api_handler_modbus_post,
  .user_ctx = NULL
};

// WiFi GET (GAP-6, GAP-19)
static const httpd_uri_t uri_wifi_get = {
  .uri      = "/api/wifi",
  .method   = HTTP_GET,
  .handler  = api_handler_wifi_get,
  .user_ctx = NULL
};

// WiFi POST (GAP-6, GAP-19, GAP-21) - config/connect/disconnect suffix routing
static const httpd_uri_t uri_wifi_post = {
  .uri      = "/api/wifi/*",
  .method   = HTTP_POST,
  .handler  = api_handler_wifi_post,
  .user_ctx = NULL
};

// WiFi POST for /api/wifi (no suffix = config)
static const httpd_uri_t uri_wifi_config_post = {
  .uri      = "/api/wifi",
  .method   = HTTP_POST,
  .handler  = api_handler_wifi_post,
  .user_ctx = NULL
};

// Ethernet GET (v6.1.0+)
static const httpd_uri_t uri_ethernet_get = {
  .uri      = "/api/ethernet",
  .method   = HTTP_GET,
  .handler  = api_handler_ethernet_get,
  .user_ctx = NULL
};

// Ethernet POST (v6.1.0+)
static const httpd_uri_t uri_ethernet_post = {
  .uri      = "/api/ethernet",
  .method   = HTTP_POST,
  .handler  = api_handler_ethernet_post,
  .user_ctx = NULL
};

// HTTP config POST (GAP-7)
static const httpd_uri_t uri_http_config_post = {
  .uri      = "/api/http",
  .method   = HTTP_POST,
  .handler  = api_handler_http_config_post,
  .user_ctx = NULL
};

// GPIO DELETE (GAP-11)
static const httpd_uri_t uri_gpio_delete = {
  .uri      = "/api/gpio/*",
  .method   = HTTP_DELETE,
  .handler  = api_handler_gpio_config_delete,
  .user_ctx = NULL
};

// Logic settings POST (GAP-26)
static const httpd_uri_t uri_logic_settings_post = {
  .uri      = "/api/logic/settings",
  .method   = HTTP_POST,
  .handler  = api_handler_logic_settings_post,
  .user_ctx = NULL
};

// Modules GET (GAP-28)
static const httpd_uri_t uri_modules_get = {
  .uri      = "/api/modules",
  .method   = HTTP_GET,
  .handler  = api_handler_modules_get,
  .user_ctx = NULL
};

// Modules POST (GAP-28)
static const httpd_uri_t uri_modules_post = {
  .uri      = "/api/modules",
  .method   = HTTP_POST,
  .handler  = api_handler_modules_post,
  .user_ctx = NULL
};

// System Backup GET
static const httpd_uri_t uri_system_backup = {
  .uri      = "/api/system/backup",
  .method   = HTTP_GET,
  .handler  = api_handler_system_backup,
  .user_ctx = NULL
};

// System Restore POST
static const httpd_uri_t uri_system_restore = {
  .uri      = "/api/system/restore",
  .method   = HTTP_POST,
  .handler  = api_handler_system_restore,
  .user_ctx = NULL
};

/* ============================================================================
 * INITIALIZATION & CONTROL
 * ============================================================================ */

int http_server_init(void)
{
  if (http_state.initialized) {
    ESP_LOGI(TAG, "HTTP server already initialized");
    return 0;
  }

  memset(&http_state.stats, 0, sizeof(HttpServerStats));
  memset(&http_state.config, 0, sizeof(HttpConfig));
  http_state.server = NULL;
  http_state.running = 0;
  http_state.initialized = 1;

  ESP_LOGI(TAG, "HTTP server initialized");
  return 0;
}

int http_server_start(const HttpConfig *config)
{
  if (!http_state.initialized) {
    ESP_LOGE(TAG, "HTTP server not initialized");
    return -1;
  }

  if (http_state.running) {
    ESP_LOGI(TAG, "HTTP server already running");
    return 0;
  }

  if (!config) {
    ESP_LOGE(TAG, "HTTP config is NULL");
    return -1;
  }

  // Store config
  memcpy(&http_state.config, config, sizeof(HttpConfig));

  // Start server (HTTPS or HTTP depending on tls_enabled)
  if (config->tls_enabled) {
    // HTTPS mode: use custom TLS wrapper with heap-limited connections
    uint8_t prio = (config->priority == 0) ? 3 : (config->priority == 2) ? 6 : 5;
    int ret = https_wrapper_start(&http_state.server,
                                   config->port,
                                   48,       // max URI handlers
                                   10240,    // stack (TLS handshake needs ~8-10KB)
                                   prio,
                                   1);       // core 1
    if (ret != 0) {
      ESP_LOGE(TAG, "Failed to start HTTPS server on port %d", config->port);
      return -1;
    }
    http_state.tls_active = 1;
  } else {
    // Plain HTTP mode
    httpd_config_t httpd_config = HTTPD_DEFAULT_CONFIG();
    httpd_config.server_port = config->port;
    httpd_config.max_uri_handlers = 48;
    httpd_config.stack_size = 8192;
    httpd_config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t err = httpd_start(&http_state.server, &httpd_config);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to start HTTP server: %d", err);
      return -1;
    }
    http_state.tls_active = 0;
  }

  // Register URI handlers (38 total)
  // NOTE: ESP-IDF httpd_uri_match_wildcard only supports * at END of URI.
  // Middle-wildcards like /api/logic/*/source NEVER match.
  // Instead, wildcard handlers do internal suffix-based routing.
  //
  // Discovery + status
  httpd_register_uri_handler(http_state.server, &uri_endpoints);
  httpd_register_uri_handler(http_state.server, &uri_endpoints_slash);
  httpd_register_uri_handler(http_state.server, &uri_status);
  httpd_register_uri_handler(http_state.server, &uri_config);
  // Counters (wildcard handles GET + suffix routing for POST /reset, /start, /stop)
  httpd_register_uri_handler(http_state.server, &uri_counters);
  httpd_register_uri_handler(http_state.server, &uri_counter_single_get);
  httpd_register_uri_handler(http_state.server, &uri_counter_single_post);
  // Timers
  httpd_register_uri_handler(http_state.server, &uri_timers);
  httpd_register_uri_handler(http_state.server, &uri_timer_single);
  // Registers
  httpd_register_uri_handler(http_state.server, &uri_hr_read);
  httpd_register_uri_handler(http_state.server, &uri_hr_write);
  httpd_register_uri_handler(http_state.server, &uri_ir_read);
  httpd_register_uri_handler(http_state.server, &uri_coil_read);
  httpd_register_uri_handler(http_state.server, &uri_coil_write);
  httpd_register_uri_handler(http_state.server, &uri_di_read);
  // GPIO
  httpd_register_uri_handler(http_state.server, &uri_gpio);
  httpd_register_uri_handler(http_state.server, &uri_gpio_single);
  httpd_register_uri_handler(http_state.server, &uri_gpio_write);
  // ST Logic (wildcard handles GET/POST/DELETE + suffix routing)
  httpd_register_uri_handler(http_state.server, &uri_logic);
  httpd_register_uri_handler(http_state.server, &uri_logic_single_get);
  httpd_register_uri_handler(http_state.server, &uri_logic_single_post);
  httpd_register_uri_handler(http_state.server, &uri_logic_single_delete);
  // Debug
  httpd_register_uri_handler(http_state.server, &uri_debug_get);
  httpd_register_uri_handler(http_state.server, &uri_debug_set);
  // System
  httpd_register_uri_handler(http_state.server, &uri_system_reboot);
  httpd_register_uri_handler(http_state.server, &uri_system_save);
  httpd_register_uri_handler(http_state.server, &uri_system_load);
  httpd_register_uri_handler(http_state.server, &uri_system_defaults);
  // GAP-ANALYSE routes (v6.1.0+)
  httpd_register_uri_handler(http_state.server, &uri_counter_delete);
  httpd_register_uri_handler(http_state.server, &uri_timer_config_post);
  httpd_register_uri_handler(http_state.server, &uri_timer_delete);
  httpd_register_uri_handler(http_state.server, &uri_modbus_get);
  httpd_register_uri_handler(http_state.server, &uri_modbus_post);
  httpd_register_uri_handler(http_state.server, &uri_wifi_get);
  httpd_register_uri_handler(http_state.server, &uri_wifi_post);
  httpd_register_uri_handler(http_state.server, &uri_wifi_config_post);
  httpd_register_uri_handler(http_state.server, &uri_ethernet_get);
  httpd_register_uri_handler(http_state.server, &uri_ethernet_post);
  httpd_register_uri_handler(http_state.server, &uri_http_config_post);
  httpd_register_uri_handler(http_state.server, &uri_gpio_delete);
  httpd_register_uri_handler(http_state.server, &uri_logic_settings_post);
  httpd_register_uri_handler(http_state.server, &uri_modules_get);
  httpd_register_uri_handler(http_state.server, &uri_modules_post);
  // Backup/restore
  httpd_register_uri_handler(http_state.server, &uri_system_backup);
  httpd_register_uri_handler(http_state.server, &uri_system_restore);

  http_state.running = 1;
  ESP_LOGI(TAG, "HTTP server started on port %d", config->port);

  return 0;
}

int http_server_stop(void)
{
  if (!http_state.running || !http_state.server) {
    ESP_LOGI(TAG, "HTTP server not running");
    return 0;
  }

  if (http_state.tls_active) {
    https_wrapper_stop(http_state.server);
  } else {
    esp_err_t err = httpd_stop(http_state.server);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to stop HTTP server: %d", err);
      return -1;
    }
  }

  http_state.server = NULL;
  http_state.running = 0;
  http_state.tls_active = 0;
  ESP_LOGI(TAG, "HTTP%s server stopped", http_state.tls_active ? "S" : "");

  return 0;
}

uint8_t http_server_is_running(void)
{
  return http_state.running;
}

uint8_t http_server_is_tls_active(void)
{
  return http_state.tls_active;
}

const HttpConfig* http_server_get_config(void)
{
  if (!http_state.initialized) {
    return NULL;
  }
  return &http_state.config;
}

/* ============================================================================
 * STATISTICS
 * ============================================================================ */

const HttpServerStats* http_server_get_stats(void)
{
  return &http_state.stats;
}

void http_server_reset_stats(void)
{
  memset(&http_state.stats, 0, sizeof(HttpServerStats));
  ESP_LOGI(TAG, "HTTP server statistics reset");
}

// Called by api_handlers to update stats
void http_server_stat_request(void)
{
  http_state.stats.total_requests++;
}

void http_server_stat_success(void)
{
  http_state.stats.successful_requests++;
}

void http_server_stat_client_error(void)
{
  http_state.stats.client_errors++;
}

void http_server_stat_server_error(void)
{
  http_state.stats.server_errors++;
}

void http_server_stat_auth_failure(void)
{
  http_state.stats.auth_failures++;
}

/* ============================================================================
 * AUTHENTICATION
 * ============================================================================ */

// Check Basic Authentication (returns true if OK or auth not required)
bool http_server_check_auth(httpd_req_t *req)
{
  if (!http_state.config.auth_enabled) {
    return true;  // Auth not required
  }

  char auth_header[128];
  esp_err_t err = httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header));
  if (err != ESP_OK) {
    return false;  // No auth header
  }

  // Check for "Basic " prefix
  if (strncmp(auth_header, "Basic ", 6) != 0) {
    return false;
  }

  // Decode Base64 credentials
  // Format: base64(username:password)
  char* b64_creds = auth_header + 6;

  // Simple Base64 decode (ESP-IDF has mbedtls_base64_decode)
  // For simplicity, we'll use a manual check
  // Expected: base64 of "username:password"

  // Build expected credentials
  char expected[128];
  snprintf(expected, sizeof(expected), "%s:%s",
           http_state.config.username, http_state.config.password);

  // Base64 encode expected
  unsigned char encoded[128];
  size_t encoded_len = 0;
  int ret = mbedtls_base64_encode(encoded, sizeof(encoded), &encoded_len,
                                   (unsigned char*)expected, strlen(expected));
  if (ret != 0) {
    return false;
  }

  // Compare
  if (strncmp(b64_creds, (char*)encoded, encoded_len) == 0) {
    return true;
  }

  return false;
}

/* ============================================================================
 * DEBUGGING
 * ============================================================================ */

void http_server_print_status(void)
{
  debug_printf("\n╔════════════════════════════════════════╗\n");
  debug_printf("║        HTTP SERVER STATUS             ║\n");
  debug_printf("╚════════════════════════════════════════╝\n\n");

  debug_printf("Status:           %s\n", http_state.running ? "Running" : "Stopped");

  if (http_state.running) {
    debug_printf("Protocol:         %s\n", http_state.tls_active ? "HTTPS (TLS)" : "HTTP");
    debug_printf("Port:             %d\n", http_state.config.port);
    debug_printf("API Endpoints:    %s\n", http_state.config.api_enabled ? "Enabled" : "Disabled");
    debug_printf("Auth Enabled:     %s\n", http_state.config.auth_enabled ? "Yes" : "No");
    if (http_state.config.auth_enabled) {
      debug_printf("Username:         %s\n", http_state.config.username);
    }
  }

  debug_printf("\nStatistics:\n");
  debug_printf("  Total Requests:     %lu\n", http_state.stats.total_requests);
  debug_printf("  Successful (2xx):   %lu\n", http_state.stats.successful_requests);
  debug_printf("  Client Errors (4xx):%lu\n", http_state.stats.client_errors);
  debug_printf("  Server Errors (5xx):%lu\n", http_state.stats.server_errors);
  if (http_state.config.auth_enabled) {
    debug_printf("  Auth Failures:      %lu\n", http_state.stats.auth_failures);
  }

  debug_printf("\n");
}
