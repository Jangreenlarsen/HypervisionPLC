/**
 * @file ethernet_driver.h
 * @brief W5500 SPI Ethernet driver for ESP32 (Layer 0 hardware abstraction)
 *
 * Uses ESP-IDF Ethernet API with W5500 SPI MAC/PHY.
 * Handles DHCP, static IP, link status, and auto-reconnect.
 *
 * GPIO allocation:
 *   SPI MISO  = GPIO 12 (HSPI)
 *   SPI MOSI  = GPIO 13 (HSPI)
 *   SPI CLK   = GPIO 14 (HSPI)
 *   SPI CS    = GPIO 23 (HSPI)
 *   W5500 INT = GPIO 34 (input-only, needs external 10K pullup)
 *   W5500 RST = GPIO 33 (hardware reset)
 *
 * NOTE: GPIO 12 is a strapping pin. W5500 RST is held LOW during boot
 *       to prevent MISO from pulling GPIO 12 HIGH (flash voltage issue).
 */

#ifndef ETHERNET_DRIVER_H
#define ETHERNET_DRIVER_H

#include <stdint.h>

/* ============================================================================
 * INITIALIZATION & CONTROL
 * ============================================================================ */

/**
 * Initialize W5500 SPI Ethernet driver
 * - Configures SPI bus (HSPI)
 * - Creates W5500 MAC and PHY
 * - Creates esp_netif for Ethernet
 * - Registers event handlers
 * @return 0 on success, -1 on error
 */
int ethernet_driver_init(void);

/**
 * Start Ethernet interface (enable link)
 * @return 0 on success, -1 on error
 */
int ethernet_driver_start(void);

/**
 * Stop Ethernet interface
 * @return 0 on success, -1 on error
 */
int ethernet_driver_stop(void);

/* ============================================================================
 * STATUS & INFORMATION
 * ============================================================================ */

/**
 * Check if Ethernet is connected with valid IP
 * @return 1 if connected, 0 if not
 */
uint8_t ethernet_driver_is_connected(void);

/**
 * Get current local IP address
 * @return IP address in network byte order (0 if not connected)
 */
uint32_t ethernet_driver_get_local_ip(void);

/**
 * Get current gateway IP
 * @return Gateway IP in network byte order
 */
uint32_t ethernet_driver_get_gateway(void);

/**
 * Get current netmask
 * @return Netmask in network byte order
 */
uint32_t ethernet_driver_get_netmask(void);

/**
 * Get current DNS server
 * @return DNS IP in network byte order
 */
uint32_t ethernet_driver_get_dns(void);

/**
 * Get link speed
 * @return Link speed in Mbps (10 or 100), 0 if not connected
 */
uint32_t ethernet_driver_get_speed(void);

/**
 * Check if full duplex
 * @return 1 if full duplex, 0 if half duplex or not connected
 */
uint8_t ethernet_driver_is_full_duplex(void);

/**
 * Get MAC address as string
 * @param out_mac Output buffer (at least 18 bytes for "XX:XX:XX:XX:XX:XX\0")
 * @return 0 on success, -1 on error
 */
int ethernet_driver_get_mac_str(char *out_mac);

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
int ethernet_driver_set_static_ip(uint32_t ip_addr, uint32_t gateway, uint32_t netmask, uint32_t dns);

/**
 * Enable DHCP client
 * @return 0 on success, -1 on error
 */
int ethernet_driver_enable_dhcp(void);

/* ============================================================================
 * BACKGROUND TASKS
 * ============================================================================ */

/**
 * Main loop task (should be called periodically from application loop)
 * @return number of events processed (0 if idle)
 */
int ethernet_driver_loop(void);

/**
 * Get elapsed time since Ethernet connected
 * @return milliseconds since connected (0 if not connected)
 */
uint32_t ethernet_driver_get_uptime_ms(void);

/* ============================================================================
 * DEBUGGING
 * ============================================================================ */

/**
 * Get current state as human-readable string
 * @return State string ("Disconnected", "Connected", etc)
 */
const char* ethernet_driver_get_state_string(void);

#endif // ETHERNET_DRIVER_H
