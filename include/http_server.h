// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file http_server.h
 * @brief HTTP REST API server interface (v6.0.0+)
 *
 * Provides HTTP REST API endpoints for system monitoring and control.
 * Uses ESP-IDF esp_http_server component wrapped for Arduino framework.
 *
 * Endpoints:
 *   GET  /api/status         - System status (version, uptime, heap, wifi)
 *   GET  /api/counters       - All 4 counters
 *   GET  /api/counters/{id}  - Single counter (1-4)
 *   GET  /api/timers         - All 4 timers
 *   GET  /api/timers/{id}    - Single timer (1-4)
 *   GET  /api/registers/hr/{addr}   - Read holding register
 *   POST /api/registers/hr/{addr}   - Write holding register
 *   GET  /api/registers/ir/{addr}   - Read input register
 *   GET  /api/registers/coils/{addr} - Read coil
 *   POST /api/registers/coils/{addr} - Write coil
 *   GET  /api/registers/di/{addr}   - Read discrete input
 *   GET  /api/logic          - All ST Logic programs status
 *   GET  /api/logic/{id}     - Program with variables
 */

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <stdint.h>
#include "types.h"

/* ============================================================================
 * INITIALIZATION & CONTROL
 * ============================================================================ */

/**
 * Initialize HTTP server (creates server handle but doesn't start)
 * @return 0 on success, -1 on error
 */
int http_server_init(void);

/**
 * Start HTTP server with given configuration
 * @param config HTTP configuration (from NetworkConfig.http)
 * @return 0 on success, -1 on error
 */
int http_server_start(const HttpConfig *config);

/**
 * Stop HTTP server
 * @return 0 on success, -1 on error
 */
int http_server_stop(void);

/**
 * Check if HTTP server is running
 * @return 1 if running, 0 otherwise
 */
uint8_t http_server_is_running(void);

/**
 * Check if TLS/HTTPS is currently active
 * @return 1 if TLS active, 0 otherwise
 */
uint8_t http_server_is_tls_active(void);

/**
 * Get current HTTP server configuration
 * @return pointer to HttpConfig, or NULL if not initialized
 */
const HttpConfig* http_server_get_config(void);

/* ============================================================================
 * RUNTIME STATISTICS
 * ============================================================================ */

/**
 * HTTP server statistics
 */
typedef struct {
  uint32_t total_requests;      // Total HTTP requests received
  uint32_t successful_requests; // 2xx responses
  uint32_t client_errors;       // 4xx responses
  uint32_t server_errors;       // 5xx responses
  uint32_t auth_failures;       // 401 responses (failed auth)
} HttpServerStats;

/**
 * Get HTTP server statistics
 * @return pointer to statistics struct
 */
const HttpServerStats* http_server_get_stats(void);

/**
 * Reset HTTP server statistics
 */
void http_server_reset_stats(void);

/* ============================================================================
 * DEBUGGING
 * ============================================================================ */

/**
 * Print HTTP server status to console
 */
void http_server_print_status(void);

#endif // HTTP_SERVER_H
