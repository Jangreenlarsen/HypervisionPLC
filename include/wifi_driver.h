/**
 * @file wifi_driver.h
 * @brief ESP32 Wi-Fi driver for client mode (WPA2, DHCP, ICMP)
 *
 * This is Layer 0 hardware abstraction for Wi-Fi connectivity.
 * Provides Wi-Fi initialization, SSID/password management, DHCP, and auto-reconnect.
 *
 * Features:
 * - WPA2 mode only (client/station)
 * - DHCP automatic IP assignment
 * - Static IP fallback
 * - ICMP ping support
 * - Auto-reconnect on disconnect
 * - Event callbacks (connect, disconnect, got-IP)
 */

#ifndef WIFI_DRIVER_H
#define WIFI_DRIVER_H

#include <stdint.h>
#include <stdio.h>
#include "types.h"

/* ============================================================================
 * INITIALIZATION & CONTROL
 * ============================================================================ */

/**
 * Initialize Wi-Fi subsystem (must be called once at startup)
 * @return 0 on success, -1 on error
 */
int wifi_driver_init(void);

/**
 * Connect to Wi-Fi network
 * @param ssid Network name (up to WIFI_SSID_MAX_LEN)
 * @param password Network password (up to WIFI_PASSWORD_MAX_LEN)
 * @return 0 on start, -1 on error (async, check wifi_driver_is_connected())
 */
int wifi_driver_connect(const char *ssid, const char *password);

/**
 * Disconnect from Wi-Fi network
 * @return 0 on success, -1 on error
 */
int wifi_driver_disconnect(void);

/**
 * Start Wi-Fi scan for available networks (async)
 * @return 0 on scan started, -1 on error
 */
int wifi_driver_scan_start(void);

/**
 * Get next scan result
 * @param out_ssid Output buffer for SSID (must be WIFI_SSID_MAX_LEN bytes)
 * @param out_rssi Output for signal strength (dBm, negative)
 * @return 0 if result found, -1 if no more results
 */
int wifi_driver_scan_next(char *out_ssid, int8_t *out_rssi);

/* ============================================================================
 * STATUS & INFORMATION
 * ============================================================================ */

/**
 * Check if Wi-Fi is connected
 * @return 1 if connected, 0 if not
 */
uint8_t wifi_driver_is_connected(void);

/**
 * Get current local IP address
 * @return IP address in network byte order (0 if not connected)
 */
uint32_t wifi_driver_get_local_ip(void);

/**
 * Get current gateway IP
 * @return Gateway IP in network byte order
 */
uint32_t wifi_driver_get_gateway(void);

/**
 * Get current netmask
 * @return Netmask in network byte order
 */
uint32_t wifi_driver_get_netmask(void);

/**
 * Get current DNS server
 * @return DNS IP in network byte order
 */
uint32_t wifi_driver_get_dns(void);

/**
 * Get SSID of current network (if connected)
 * @param out_ssid Output buffer (must be WIFI_SSID_MAX_LEN bytes)
 * @return 0 on success, -1 if not connected
 */
int wifi_driver_get_ssid(char *out_ssid);

/**
 * Get RSSI (signal strength) in dBm
 * @return Signal strength (negative dBm), 0 if not connected
 */
int8_t wifi_driver_get_rssi(void);

/* ============================================================================
 * STATIC IP CONFIGURATION
 * ============================================================================ */

/**
 * Configure static IP (used if DHCP disabled)
 * @param ip_addr IP address (network byte order)
 * @param gateway Gateway IP (network byte order)
 * @param netmask Netmask (network byte order)
 * @param dns DNS server (network byte order)
 * @return 0 on success, -1 on error
 */
int wifi_driver_set_static_ip(uint32_t ip_addr, uint32_t gateway, uint32_t netmask, uint32_t dns);

/**
 * Apply static IP configuration to current connection
 * @return 0 on success, -1 on error
 */
int wifi_driver_apply_static_ip(void);

/**
 * Enable DHCP client
 * @return 0 on success, -1 on error
 */
int wifi_driver_enable_dhcp(void);

/* ============================================================================
 * NETWORK UTILITIES (ICMP)
 * ============================================================================ */

/**
 * Ping remote host (blocking, sends count ICMP echo requests)
 * @param host IP address or hostname
 * @param count Number of pings to send (1-100)
 * @return 0 on success (at least one reply), -1 if all unreachable/error
 */
int wifi_driver_ping(const char *host, uint32_t count);

/**
 * Resolve hostname to IP address (blocking, ~5 second timeout)
 * @param hostname Hostname string
 * @return IP address in network byte order, 0 on error
 */
uint32_t wifi_driver_resolve_hostname(const char *hostname);

/* ============================================================================
 * BACKGROUND TASKS
 * ============================================================================ */

/**
 * Main loop task (should be called periodically from application loop)
 * Handles auto-reconnect, event processing, etc.
 * @return number of events processed (0 if idle)
 */
int wifi_driver_loop(void);

/**
 * Get elapsed time since last Wi-Fi connection
 * @return milliseconds since connected (0 if not connected)
 */
uint32_t wifi_driver_get_uptime_ms(void);

/* ============================================================================
 * DEBUGGING
 * ============================================================================ */

/**
 * Print Wi-Fi status to serial console
 */
void wifi_driver_print_status(void);

/**
 * Get current state as human-readable string
 * @return State string ("Disconnected", "Connecting", "Connected", etc)
 */
const char* wifi_driver_get_state_string(void);

#endif // WIFI_DRIVER_H
