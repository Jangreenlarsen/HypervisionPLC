/**
 * @file network_manager.h
 * @brief Network subsystem orchestration (Layer 8)
 *
 * Coordinates Wi-Fi connectivity, TCP/Telnet server, and CLI remote access.
 * This is the main interface for network functionality.
 *
 * Architecture:
 * - Layer 0: wifi_driver, tcp_server (hardware)
 * - Layer 1.5: telnet_server (protocol)
 * - Layer 8: network_manager (orchestration)
 */

#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <stdint.h>
#include <stddef.h>
#include "types.h"

/* ============================================================================
 * INITIALIZATION & CONTROL
 * ============================================================================ */

/**
 * Initialize network subsystem (Wi-Fi driver, Telnet server)
 * @return 0 on success, -1 on error
 */
int network_manager_init(void);

/**
 * Start network services (Telnet, HTTP) independently of Wi-Fi/Ethernet (v6.0.9+)
 * Must be called before connect/start_ethernet so services are available on any interface.
 * Safe to call multiple times (services check if already running).
 * @param config Network configuration (uses telnet/http sub-configs)
 * @return 0 on success, -1 on error
 */
int network_manager_start_services(const NetworkConfig *config);

/**
 * Start Wi-Fi connection with config
 * @param config Network configuration (from PersistConfig)
 * @return 0 on start, -1 on error (connection is asynchronous)
 */
int network_manager_connect(const NetworkConfig *config);

/**
 * Start Ethernet interface independently of Wi-Fi (v6.0.9+)
 * Can be called even when Wi-Fi is disabled.
 * Safe to call multiple times (ethernet_driver_init has double-init guard).
 * @param config Network configuration (uses config->ethernet sub-struct)
 * @return 0 on success, -1 on error or ethernet disabled
 */
int network_manager_start_ethernet(const NetworkConfig *config);

/**
 * Stop Wi-Fi connection and Telnet server
 * @return 0 on success, -1 on error
 */
int network_manager_stop(void);

/* ============================================================================
 * STATUS & INFORMATION
 * ============================================================================ */

/**
 * Check if Wi-Fi is connected with valid IP
 * @return 1 if connected, 0 otherwise
 */
uint8_t network_manager_is_wifi_connected(void);

/**
 * Check if Ethernet is connected with valid IP (v6.1.0+)
 * @return 1 if connected, 0 otherwise
 */
uint8_t network_manager_is_ethernet_connected(void);

/**
 * Check if Telnet client is connected
 * @return 1 if client connected, 0 otherwise
 */
uint8_t network_manager_is_telnet_connected(void);

/**
 * Get local IP address (Wi-Fi)
 * @return IP address (network byte order), 0 if not connected
 */
uint32_t network_manager_get_local_ip(void);

/**
 * Get current network state
 * @return pointer to NetworkState (runtime state)
 */
const NetworkState* network_manager_get_state(void);

/* ============================================================================
 * TELNET I/O (Line-oriented)
 * ============================================================================ */

/**
 * Read complete line from Telnet client (if connected)
 * @param buf Output buffer
 * @param max_len Maximum length
 * @return line length, 0 if no complete line, -1 on error
 */
int network_manager_telnet_readline(char *buf, size_t max_len);

/**
 * Send line to Telnet client (if connected)
 * @param line Text line
 * @return bytes sent, -1 on error
 */
int network_manager_telnet_writeline(const char *line);

/**
 * Send formatted line to Telnet client
 * @param format Format string
 * @return bytes sent, -1 on error
 */
int network_manager_telnet_writelinef(const char *format, ...);

/**
 * Send raw text to Telnet client (no line ending)
 * @param text Text to send
 * @return bytes sent, -1 on error
 */
int network_manager_telnet_write(const char *text);

/**
 * Check if Telnet has complete input line
 * @return 1 if line ready, 0 otherwise
 */
uint8_t network_manager_telnet_has_input(void);

/* ============================================================================
 * BACKGROUND TASKS
 * ============================================================================ */

/**
 * Main network loop (should be called periodically from application loop)
 * Handles Wi-Fi auto-reconnect, Telnet server events, etc.
 * @return number of events processed (0 if idle)
 */
int network_manager_loop(void);

/* ============================================================================
 * DEBUGGING & STATUS
 * ============================================================================ */

/**
 * Print all network status to console
 */
void network_manager_print_status(void);

/**
 * Get human-readable Wi-Fi state
 * @return state string
 */
const char* network_manager_get_wifi_state_string(void);

#endif // NETWORK_MANAGER_H
