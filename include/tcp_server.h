/**
 * @file tcp_server.h
 * @brief TCP server base functionality for ESP32 (Layer 1)
 *
 * Provides low-level TCP server operations:
 * - Listening on configurable port
 * - Client connection management
 * - Non-blocking socket operations
 * - Per-client receive/transmit buffers
 *
 * This is the base layer for Telnet and future protocols (SSH, MQTT, etc).
 */

#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * DATA STRUCTURES
 * ============================================================================ */

typedef struct {
  int socket;                           // Socket descriptor (-1 if unused)
  uint32_t client_ip;                   // Client IP address
  uint16_t client_port;                 // Client port
  uint32_t connected_ms;                // millis() when client connected (FEAT-075)
  uint32_t last_activity_ms;            // Timestamp of last activity
  uint8_t connected;                    // 1 if actively connected
} TcpClient;

typedef struct {
  int listen_socket;                    // Listening socket
  uint16_t listen_port;                 // Port to listen on
  uint8_t active;                       // 1 if listening
  uint32_t created_time_ms;             // When server was created

  TcpClient clients[1];                 // Single client support for now
  uint8_t client_count;
} TcpServer;

/* ============================================================================
 * INITIALIZATION & CONTROL
 * ============================================================================ */

/**
 * Initialize TCP server (does not start listening yet)
 * @param port Port to listen on
 * @return server instance pointer, NULL on error
 */
TcpServer* tcp_server_create(uint16_t port);

/**
 * Start listening for connections
 * @param server Server instance
 * @return 0 on success, -1 on error
 */
int tcp_server_start(TcpServer *server);

/**
 * Stop listening and close server
 * @param server Server instance
 * @return 0 on success, -1 on error
 */
int tcp_server_stop(TcpServer *server);

/**
 * Destroy server and free resources
 * @param server Server instance
 */
void tcp_server_destroy(TcpServer *server);

/* ============================================================================
 * CLIENT MANAGEMENT
 * ============================================================================ */

/**
 * Accept pending connections (non-blocking)
 * Processes any pending client connections from the listening socket.
 * @param server Server instance
 * @return number of new connections accepted (0 if none)
 */
int tcp_server_accept(TcpServer *server);

/**
 * Get client by index
 * @param server Server instance
 * @param index Client index (0 for single-client mode)
 * @return client pointer, NULL if not connected
 */
TcpClient* tcp_server_get_client(TcpServer *server, uint8_t index);

/**
 * Disconnect specific client
 * @param server Server instance
 * @param client_index Index of client to disconnect
 * @return 0 on success, -1 on error
 */
int tcp_server_disconnect_client(TcpServer *server, uint8_t client_index);

/**
 * Disconnect all clients
 * @param server Server instance
 * @return 0 on success, -1 on error
 */
int tcp_server_disconnect_all(TcpServer *server);

/**
 * Check if any client is connected
 * @param server Server instance
 * @return 1 if at least one client connected, 0 otherwise
 */
uint8_t tcp_server_has_clients(TcpServer *server);

/* ============================================================================
 * DATA TRANSMISSION (Non-blocking, buffered)
 * ============================================================================ */

/**
 * Send data to specific client (non-blocking)
 * @param server Server instance
 * @param client_index Client index
 * @param data Data buffer
 * @param len Data length
 * @return bytes sent, -1 on error
 */
int tcp_server_send(TcpServer *server, uint8_t client_index, const uint8_t *data, size_t len);

/**
 * Send formatted string to client (printf-style)
 * @param server Server instance
 * @param client_index Client index
 * @param format Format string
 * @return bytes sent, -1 on error
 */
int tcp_server_sendf(TcpServer *server, uint8_t client_index, const char *format, ...);

/**
 * Receive data from specific client (non-blocking)
 * @param server Server instance
 * @param client_index Client index
 * @param buf Output buffer
 * @param max_len Maximum bytes to read
 * @return bytes read, 0 if no data, -1 on error/disconnect
 */
int tcp_server_recv(TcpServer *server, uint8_t client_index, uint8_t *buf, size_t max_len);

/**
 * Receive single byte from client (non-blocking)
 * @param server Server instance
 * @param client_index Client index
 * @param out_byte Output byte
 * @return 1 if byte available, 0 if none, -1 on error/disconnect
 */
int tcp_server_recv_byte(TcpServer *server, uint8_t client_index, uint8_t *out_byte);

/* ============================================================================
 * STATUS & INFORMATION
 * ============================================================================ */

/**
 * Check if server is listening
 * @param server Server instance
 * @return 1 if listening, 0 otherwise
 */
uint8_t tcp_server_is_listening(const TcpServer *server);

/**
 * Check if specific client is connected
 * @param server Server instance
 * @param client_index Client index
 * @return 1 if connected, 0 otherwise
 */
uint8_t tcp_server_client_is_connected(const TcpServer *server, uint8_t client_index);

/**
 * Get client count (connected clients)
 * @param server Server instance
 * @return number of connected clients
 */
uint8_t tcp_server_get_client_count(const TcpServer *server);

/**
 * Get data available from client
 * @param server Server instance
 * @param client_index Client index
 * @return bytes available to read, 0 if none
 */
uint16_t tcp_server_available(TcpServer *server, uint8_t client_index);

/* ============================================================================
 * BACKGROUND TASKS
 * ============================================================================ */

/**
 * Main loop (should be called periodically)
 * Processes socket events, timeouts, disconnections, etc.
 * @param server Server instance
 * @return number of events processed (0 if idle)
 */
int tcp_server_loop(TcpServer *server);

/* ============================================================================
 * DEBUGGING
 * ============================================================================ */

/**
 * Print server status to console
 * @param server Server instance
 */
void tcp_server_print_status(const TcpServer *server);

#endif // TCP_SERVER_H
