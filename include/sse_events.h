/**
 * @file sse_events.h
 * @brief Server-Sent Events (SSE) for real-time push notifications (FEAT-023, v7.0.0)
 *
 * LAYER 1.5: Protocol (same level as HTTP/Telnet)
 * Responsibility: SSE connection management and event broadcasting
 *
 * Architecture: Runs on dedicated httpd instance (separate port) to avoid
 * blocking the main API server. Port is configurable via HttpConfig.sse_port.
 *
 * Endpoint: GET /api/events?subscribe=counters,timers,registers,system&hr=0-15&ir=0-3&coils=0-7&di=0-3
 */

#ifndef SSE_EVENTS_H
#define SSE_EVENTS_H

#include <stdint.h>
#include <esp_http_server.h>

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

#define SSE_MAX_CLIENTS         3       // Max simultaneous SSE connections (RAM limited)
#define SSE_CHECK_INTERVAL_MS   100     // Change detection interval (10 Hz)
#define SSE_HEARTBEAT_MS        15000   // SSE keepalive comment interval
#define SSE_DEFAULT_PORT        81      // Default SSE port (main port + 1)
#define SSE_MAX_WATCH_PER_TYPE  32      // Max watched addresses per register type

/* ============================================================================
 * TOPIC SUBSCRIPTION BITMASK
 * ============================================================================ */

#define SSE_TOPIC_COUNTERS      0x01
#define SSE_TOPIC_TIMERS        0x02
#define SSE_TOPIC_REGISTERS     0x04
#define SSE_TOPIC_SYSTEM        0x08
#define SSE_TOPIC_ALL           0x0F

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

/**
 * Initialize SSE subsystem (no server started yet)
 */
void sse_init(void);

/**
 * Start dedicated SSE httpd server
 * @param port SSE server port (0 = use default SSE_DEFAULT_PORT)
 * @return 0 on success, -1 on error
 */
int sse_start(uint16_t port);

/**
 * Stop SSE httpd server
 */
void sse_stop(void);

/**
 * HTTP handler: GET /api/events?subscribe=counters,timers,registers,system
 * Blocks the SSE httpd thread — sends SSE stream until client disconnects.
 * Main API server on port 80 is unaffected.
 */
esp_err_t api_handler_sse_events(httpd_req_t *req);

/**
 * HTTP handler: GET /api/events/status — SSE subsystem info (runs on main server)
 */
esp_err_t api_handler_sse_status(httpd_req_t *req);

/**
 * Get number of active SSE clients
 */
int sse_get_client_count(void);

/**
 * Get SSE server port
 */
uint16_t sse_get_port(void);

/**
 * Client info for CLI display
 */
typedef struct {
  bool active;
  int slot;
  int fd;
  uint32_t ip_addr;       // Network byte order
  uint32_t connected_ms;  // millis() at connect time
  char username[24];      // RBAC username
  uint8_t topics;         // Subscribed topic bitmask
} SseClientInfoPublic;

/**
 * Get info about active SSE clients
 * @param out Array of SSE_MAX_CLIENTS entries
 * @return Number of active clients
 */
int sse_get_client_info(SseClientInfoPublic *out);

/**
 * Request disconnect of specific client by slot index
 * @param slot Client slot (0-based)
 * @return true if client was active and flagged
 */
bool sse_disconnect_client(int slot);

/**
 * Request disconnect of all active clients
 * @return Number of clients flagged
 */
int sse_disconnect_all(void);

#endif // SSE_EVENTS_H
