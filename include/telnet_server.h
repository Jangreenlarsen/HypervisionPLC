/**
 * @file telnet_server.h
 * @brief Telnet protocol server for ESP32 (Layer 1.5)
 *
 * Handles Telnet protocol specifics:
 * - IAC (Interpret As Command) command parsing
 * - Telnet option negotiation (ECHO, SUPPRESS_GA, LINEMODE)
 * - Line buffering (CR/LF handling)
 * - Raw vs. cooked input modes
 *
 * This sits on top of tcp_server and provides line-oriented input/output.
 */

#ifndef TELNET_SERVER_H
#define TELNET_SERVER_H

#include <stdint.h>
#include <stddef.h>
#include "tcp_server.h"
#include "types.h"  // For NetworkConfig struct definition

/* ============================================================================
 * DATA STRUCTURES
 * ============================================================================ */

#define TELNET_INPUT_BUFFER_SIZE    256    // Input buffer size (from constants.h)

typedef enum {
  TELNET_STATE_NONE = 0,
  TELNET_STATE_IAC = 1,          // Saw 0xFF (IAC)
  TELNET_STATE_IAC_CMD = 2,       // Saw command byte after IAC
  TELNET_STATE_IAC_OPTION = 3,    // Saw option byte after IAC command
} TelnetParseState;

typedef enum {
  TELNET_AUTH_NONE = 0,        // No authentication (disabled)
  TELNET_AUTH_WAITING = 1,     // Waiting for login
  TELNET_AUTH_USERNAME = 2,    // Username entered, waiting for password
  TELNET_AUTH_AUTHENTICATED = 3  // Authentication successful
} TelnetAuthState;

typedef struct {
  // Configuration
  uint16_t port;
  uint8_t echo_enabled;           // Echo input back to client
  uint8_t linemode_enabled;       // Line-mode (vs character-by-character)
  uint8_t auth_required;          // 1 = require authentication, 0 = no auth
  NetworkConfig *network_config;  // Pointer to network config (for Telnet credentials)

  // Internal state
  TcpServer *tcp_server;
  TelnetParseState parse_state;

  // Authentication state (v3.0+, RBAC v7.7.0)
  TelnetAuthState auth_state;
  char auth_username[32];
  uint8_t auth_attempts;          // Failed login attempt counter
  uint32_t auth_lockout_time;     // Timestamp when lockout expires (0 = no lockout)
  int8_t rbac_user_index;         // RBAC user index (0-7), 99=virtual admin, -1=not authenticated

  // Per-client input buffer (cooked mode)
  char input_buffer[TELNET_INPUT_BUFFER_SIZE];
  uint16_t input_pos;             // Write position (end of buffer)
  uint16_t cursor_pos;            // Current cursor position (for LEFT/RIGHT arrows)
  uint8_t input_ready;            // 1 if complete line available
  uint8_t escape_seq_state;       // For parsing arrow keys (0=none, 1=ESC, 2=ESC[)

  // Telnet option negotiation state (per client)
  struct {
    uint8_t client_echo;          // Client will echo to server?
    uint8_t client_suppress_ga;   // Client will suppress GA?
    uint8_t server_echo;          // Server will echo?
    uint8_t server_suppress_ga;   // Server will suppress GA?
  } options;
} TelnetServer;

/* ============================================================================
 * INITIALIZATION & CONTROL
 * ============================================================================ */

/**
 * Create Telnet server
 * @param port Port (typically 23)
 * @param network_config Pointer to network config (for Telnet credentials)
 * @return server instance, NULL on error
 */
TelnetServer* telnet_server_create(uint16_t port, NetworkConfig *network_config);

/**
 * Start Telnet server (starts underlying TCP server)
 * @param server Telnet server instance
 * @return 0 on success, -1 on error
 */
int telnet_server_start(TelnetServer *server);

/**
 * Stop Telnet server
 * @param server Telnet server instance
 * @return 0 on success, -1 on error
 */
int telnet_server_stop(TelnetServer *server);

/**
 * Destroy Telnet server
 * @param server Telnet server instance
 */
void telnet_server_destroy(TelnetServer *server);

/* ============================================================================
 * CLIENT MANAGEMENT
 * ============================================================================ */

/**
 * Check if client is connected
 * @param server Telnet server instance
 * @return 1 if connected, 0 otherwise
 */
uint8_t telnet_server_client_connected(const TelnetServer *server);

/**
 * Disconnect client
 * @param server Telnet server instance
 * @return 0 on success, -1 on error
 */
int telnet_server_disconnect_client(TelnetServer *server);

/* ============================================================================
 * LINE-ORIENTED INPUT/OUTPUT (Telnet cooked mode)
 * ============================================================================ */

/**
 * Get complete line from client (if available)
 * @param server Telnet server instance
 * @param buf Output buffer
 * @param max_len Maximum length
 * @return line length (excluding newline), 0 if no complete line, -1 on error
 */
int telnet_server_readline(TelnetServer *server, char *buf, size_t max_len);

/**
 * Send line to client (with CRLF termination)
 * @param server Telnet server instance
 * @param line Text line
 * @return bytes sent, -1 on error
 */
int telnet_server_writeline(TelnetServer *server, const char *line);

/**
 * Send formatted line (printf-style)
 * @param server Telnet server instance
 * @param format Format string
 * @return bytes sent, -1 on error
 */
int telnet_server_writelinef(TelnetServer *server, const char *format, ...);

/**
 * Send raw text (no line ending added)
 * @param server Telnet server instance
 * @param text Text to send
 * @return bytes sent, -1 on error
 */
int telnet_server_write(TelnetServer *server, const char *text);

/**
 * Send raw formatted text
 * @param server Telnet server instance
 * @param format Format string
 * @return bytes sent, -1 on error
 */
int telnet_server_writef(TelnetServer *server, const char *format, ...);

/**
 * Send single character
 * @param server Telnet server instance
 * @param ch Character
 * @return 1 on success, -1 on error
 */
int telnet_server_writech(TelnetServer *server, char ch);

/* ============================================================================
 * STATUS & INFORMATION
 * ============================================================================ */

/**
 * Check if line input is ready
 * @param server Telnet server instance
 * @return 1 if complete line available, 0 otherwise
 */
uint8_t telnet_server_has_input(const TelnetServer *server);

/**
 * Get pending input bytes
 * @param server Telnet server instance
 * @return number of bytes pending
 */
uint16_t telnet_server_available(const TelnetServer *server);

/* ============================================================================
 * BACKGROUND TASKS
 * ============================================================================ */

/**
 * Main loop (process telnet protocol, should be called often)
 * @param server Telnet server instance
 * @return number of events processed
 */
int telnet_server_loop(TelnetServer *server);

/* ============================================================================
 * DEBUGGING
 * ============================================================================ */

/**
 * Print Telnet server status
 * @param server Telnet server instance
 */
void telnet_server_print_status(const TelnetServer *server);

#endif // TELNET_SERVER_H
