/**
 * @file api_handlers.h
 * @brief HTTP REST API endpoint handlers (v6.0.0+)
 *
 * JSON response builders and HTTP request handlers for REST API.
 * Uses ArduinoJson for efficient JSON serialization.
 */

#ifndef API_HANDLERS_H
#define API_HANDLERS_H

#include <esp_http_server.h>

/* ============================================================================
 * API ENDPOINT HANDLERS
 *
 * All handlers follow ESP-IDF esp_http_server pattern:
 *   esp_err_t handler(httpd_req_t *req)
 *
 * Return ESP_OK on success, ESP_FAIL on error.
 * ============================================================================ */

/**
 * GET /api/status
 * System status (version, uptime, heap, wifi, modbus_slave_id)
 */
esp_err_t api_handler_status(httpd_req_t *req);

/**
 * GET /api/counters
 * All 4 counters summary
 */
esp_err_t api_handler_counters(httpd_req_t *req);

/**
 * GET /api/counters/{id}
 * Single counter details (1-4)
 */
esp_err_t api_handler_counter_single(httpd_req_t *req);

/**
 * GET /api/timers
 * All 4 timers summary
 */
esp_err_t api_handler_timers(httpd_req_t *req);

/**
 * GET /api/timers/{id}
 * Single timer details (1-4)
 */
esp_err_t api_handler_timer_single(httpd_req_t *req);

/**
 * GET /api/registers/hr/{addr}
 * Read holding register
 */
esp_err_t api_handler_hr_read(httpd_req_t *req);

/**
 * POST /api/registers/hr/{addr}
 * Write holding register
 * Request body: {"value": 12345}
 */
esp_err_t api_handler_hr_write(httpd_req_t *req);

/**
 * GET /api/registers/ir/{addr}
 * Read input register
 */
esp_err_t api_handler_ir_read(httpd_req_t *req);

/**
 * GET /api/registers/coils/{addr}
 * Read coil
 */
esp_err_t api_handler_coil_read(httpd_req_t *req);

/**
 * POST /api/registers/coils/{addr}
 * Write coil
 * Request body: {"value": true} or {"value": 1}
 */
esp_err_t api_handler_coil_write(httpd_req_t *req);

/**
 * GET /api/registers/di/{addr}
 * Read discrete input
 */
esp_err_t api_handler_di_read(httpd_req_t *req);

/**
 * GET /api/logic
 * All ST Logic programs status
 */
esp_err_t api_handler_logic(httpd_req_t *req);

/**
 * GET /api/logic/{id}
 * Single ST Logic program details with variables (1-4)
 */
esp_err_t api_handler_logic_single(httpd_req_t *req);

/* ============================================================================
 * COUNTER/TIMER CONFIG ENDPOINTS (GAP-1, GAP-2, GAP-3, GAP-16)
 * ============================================================================ */

/**
 * POST /api/counters/{id} - Configure counter
 * POST /api/counters/{id}/control - Counter control flags
 * (Routed via suffix in existing counter_single handler)
 */

/**
 * DELETE /api/counters/{id} - Delete/disable counter
 */
esp_err_t api_handler_counter_delete(httpd_req_t *req);

/**
 * POST /api/timers/{id} - Configure timer
 * (Routed via suffix in timer wildcard handler)
 */
esp_err_t api_handler_timer_config_post(httpd_req_t *req);

/**
 * DELETE /api/timers/{id} - Delete/disable timer
 */
esp_err_t api_handler_timer_delete(httpd_req_t *req);

/* ============================================================================
 * MODBUS CONFIG ENDPOINTS (GAP-4, GAP-5, GAP-18)
 * ============================================================================ */

/**
 * GET /api/modbus/slave - Slave config + stats
 * GET /api/modbus/master - Master config + stats
 */
esp_err_t api_handler_modbus_get(httpd_req_t *req);

/**
 * POST /api/modbus/slave - Configure slave
 * POST /api/modbus/master - Configure master
 */
esp_err_t api_handler_modbus_post(httpd_req_t *req);

/* ============================================================================
 * WIFI ENDPOINTS (GAP-6, GAP-19, GAP-21)
 * ============================================================================ */

/**
 * GET /api/wifi - Extended WiFi status (config + runtime RSSI, MAC, etc.)
 */
esp_err_t api_handler_wifi_get(httpd_req_t *req);

/**
 * POST /api/wifi - WiFi configuration
 * POST /api/wifi/connect - Connect WiFi
 * POST /api/wifi/disconnect - Disconnect WiFi
 */
esp_err_t api_handler_wifi_post(httpd_req_t *req);

/* ============================================================================
 * HTTP CONFIG ENDPOINT (GAP-7)
 * ============================================================================ */

/**
 * POST /api/http - HTTP server configuration
 */
esp_err_t api_handler_http_config_post(httpd_req_t *req);

/* ============================================================================
 * GPIO CONFIG ENDPOINT (GAP-11)
 * ============================================================================ */

/**
 * POST /api/gpio/{pin}/config - Configure GPIO mapping
 */
esp_err_t api_handler_gpio_config_post(httpd_req_t *req);

/**
 * DELETE /api/gpio/{pin} - Remove GPIO mapping
 */
esp_err_t api_handler_gpio_config_delete(httpd_req_t *req);

/* ============================================================================
 * LOGIC BIND ENDPOINT (GAP-13)
 * ============================================================================ */

/**
 * POST /api/logic/{id}/bind - ST Logic variable binding
 */
esp_err_t api_handler_logic_bind_post(httpd_req_t *req);

/* ============================================================================
 * LOGIC SETTINGS ENDPOINT (GAP-26)
 * ============================================================================ */

/**
 * POST /api/logic/settings - ST Logic engine settings
 */
esp_err_t api_handler_logic_settings_post(httpd_req_t *req);

/* ============================================================================
 * MODULE FLAGS ENDPOINTS (GAP-28)
 * ============================================================================ */

/**
 * GET /api/modules - Module enabled/disabled status
 */
esp_err_t api_handler_modules_get(httpd_req_t *req);

/**
 * POST /api/modules - Set module flags
 */
esp_err_t api_handler_modules_post(httpd_req_t *req);

/* ============================================================================
 * BACKUP / RESTORE ENDPOINTS
 * ============================================================================ */

/**
 * GET /api/system/backup - Download full config backup as JSON
 */
esp_err_t api_handler_system_backup(httpd_req_t *req);

/**
 * POST /api/system/restore - Restore config from backup JSON
 */
esp_err_t api_handler_system_restore(httpd_req_t *req);

/* ============================================================================
 * v6.3.0 API EXTENSIONS (FEAT-019 to FEAT-027)
 * ============================================================================ */

/** FEAT-019: GET /api/telnet — Telnet config+status */
esp_err_t api_handler_telnet_get(httpd_req_t *req);
/** FEAT-019: POST /api/telnet — Configure Telnet */
esp_err_t api_handler_telnet_post(httpd_req_t *req);

/** FEAT-024: GET /api/hostname */
esp_err_t api_handler_hostname_get(httpd_req_t *req);
/** FEAT-024: POST /api/hostname */
esp_err_t api_handler_hostname_post(httpd_req_t *req);

/** FEAT-025: GET /api/system/watchdog — Watchdog status */
esp_err_t api_handler_system_watchdog(httpd_req_t *req);

/** FEAT-021: Bulk register operations */
esp_err_t api_handler_hr_bulk_read(httpd_req_t *req);
esp_err_t api_handler_hr_bulk_write(httpd_req_t *req);
esp_err_t api_handler_ir_bulk_read(httpd_req_t *req);
esp_err_t api_handler_coils_bulk_read(httpd_req_t *req);
esp_err_t api_handler_coils_bulk_write(httpd_req_t *req);
esp_err_t api_handler_di_bulk_read(httpd_req_t *req);

/** FEAT-020: ST Logic Debug API */
esp_err_t api_handler_logic_debug(httpd_req_t *req);

/** FEAT-026: GPIO2 Heartbeat control */
esp_err_t api_handler_heartbeat(httpd_req_t *req);

/** FEAT-027: CORS preflight handler */
esp_err_t api_handler_cors_preflight(httpd_req_t *req);

/* ============================================================================
 * v7.0.0 API EXTENSIONS (FEAT-023, FEAT-030)
 * ============================================================================ */

/** FEAT-023: GET /api/events/status — SSE subsystem info (SSE stream runs on raw TCP) */
esp_err_t api_handler_sse_status(httpd_req_t *req);

/** FEAT-030: GET /api/version — API version info */
esp_err_t api_handler_api_version(httpd_req_t *req);

/** FEAT-032: GET /api/metrics — Prometheus metrics */
esp_err_t api_handler_metrics(httpd_req_t *req);

/** FEAT-022: Persistence Group Management API */
esp_err_t api_handler_persist_groups_list(httpd_req_t *req);
esp_err_t api_handler_persist_group_single(httpd_req_t *req);
esp_err_t api_handler_persist_group_post(httpd_req_t *req);
esp_err_t api_handler_persist_group_delete(httpd_req_t *req);
esp_err_t api_handler_persist_save(httpd_req_t *req);
esp_err_t api_handler_persist_restore(httpd_req_t *req);

/** FEAT-028: Rate limiting */
bool http_rate_limit_check(httpd_req_t *req);
void http_rate_limit_set_enabled(bool enabled);
bool http_rate_limit_is_enabled(void);

/** FEAT-030: /api/v1/* dispatchers — forward to existing handlers */
esp_err_t api_v1_dispatch_get(httpd_req_t *req);
esp_err_t api_v1_dispatch_post(httpd_req_t *req);
esp_err_t api_v1_dispatch_delete(httpd_req_t *req);

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * Extract ID from wildcard URI (e.g., "/api/counters/2" → 2)
 * @param req HTTP request
 * @param prefix URI prefix (e.g., "/api/counters/")
 * @return ID as integer, or -1 on error
 */
int api_extract_id_from_uri(httpd_req_t *req, const char *prefix);

/**
 * Send JSON error response
 * @param req HTTP request
 * @param status HTTP status code (e.g., 400, 404, 500)
 * @param error_msg Error message
 */
esp_err_t api_send_error(httpd_req_t *req, int status, const char *error_msg);

/**
 * Send JSON success response
 * @param req HTTP request
 * @param json_str JSON string to send
 */
esp_err_t api_send_json(httpd_req_t *req, const char *json_str);

#endif // API_HANDLERS_H
