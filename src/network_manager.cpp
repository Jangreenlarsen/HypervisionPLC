/**
 * @file network_manager.cpp
 * @brief Network subsystem orchestration implementation
 *
 * Coordinates Wi-Fi driver, TCP server, Telnet protocol, and HTTP REST API.
 */

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <Arduino.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <lwip/inet.h>
#include <esp_log.h>

#include "network_manager.h"
#include "wifi_driver.h"
#include "ethernet_driver.h"
#include "tcp_server.h"
#include "telnet_server.h"
#include "http_server.h"
#include "network_config.h"
#include "constants.h"
#include "debug.h"
#include "debug_flags.h"

static const char *TAG = "NET_MGR";

/* ============================================================================
 * INTERNAL STATE
 * ============================================================================ */

static struct {
  uint8_t initialized;
  TelnetServer *telnet_server;
  NetworkState state;
  NetworkConfig current_config;
} network_mgr = {
  .initialized = 0,
  .telnet_server = NULL,
  .state = {0},
  .current_config = {0}
};

/* ============================================================================
 * INITIALIZATION & CONTROL
 * ============================================================================ */

int network_manager_init(void)
{
  if (network_mgr.initialized) {
    ESP_LOGI(TAG, "Network manager already initialized");
    return 0;
  }

  memset(&network_mgr.state, 0, sizeof(NetworkState));
  network_mgr.state.telnet_socket = -1;

  // Initialize global networking stack (must be called once before any interface)
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  ESP_LOGI(TAG, "Global networking stack initialized");

  // Initialize Wi-Fi driver
  if (wifi_driver_init() != 0) {
    ESP_LOGE(TAG, "Failed to initialize Wi-Fi driver");
    return -1;
  }

  // Initialize W5500 Ethernet driver (v6.1.0+)
  // Note: Non-fatal if Ethernet init fails (hardware may not be present)
  if (ethernet_driver_init() != 0) {
    ESP_LOGW(TAG, "Ethernet driver init failed (W5500 not present?)");
    // Continue without Ethernet - WiFi-only mode
  }

  // Create Telnet server (but don't start yet)
  // Note: We pass NULL for network_config here, will be updated when config is available
  network_mgr.telnet_server = telnet_server_create(TELNET_PORT, NULL);
  if (!network_mgr.telnet_server) {
    ESP_LOGE(TAG, "Failed to create Telnet server");
    return -1;
  }

  // Initialize HTTP REST API server (v6.0.0+)
  if (http_server_init() != 0) {
    ESP_LOGE(TAG, "Failed to initialize HTTP server");
    // Non-fatal - continue without HTTP
  }

  network_mgr.initialized = 1;
  ESP_LOGI(TAG, "Network manager initialized");

  return 0;
}

int network_manager_connect(const NetworkConfig *config)
{
  if (!config || !network_mgr.initialized) {
    ESP_LOGE(TAG, "Invalid config or not initialized");
    return -1;
  }

  DebugFlags *debug = debug_flags_get();
  if (debug && debug->wifi_connect) {
    ESP_LOGI(TAG, "network_manager_connect() called");
    ESP_LOGI(TAG, "  SSID: %s", config->ssid);
    ESP_LOGI(TAG, "  DHCP: %d, Telnet: %d", config->dhcp_enabled, config->telnet_enabled);
  }

  // Validate config
  if (!network_config_validate(config)) {
    ESP_LOGE(TAG, "Invalid network config - validation failed");
    return -1;
  }

  if (debug && debug->wifi_connect) {
    ESP_LOGI(TAG, "Config validation PASSED");
  }

  // Save config
  memcpy(&network_mgr.current_config, config, sizeof(NetworkConfig));

  // Update Telnet server with network config (for credentials)
  if (network_mgr.telnet_server) {
    network_mgr.telnet_server->network_config = &network_mgr.current_config;
  }

  // Configure DHCP vs static IP
  if (config->dhcp_enabled) {
    wifi_driver_enable_dhcp();
    ESP_LOGI(TAG, "DHCP enabled");
  } else {
    wifi_driver_set_static_ip(config->static_ip, config->static_gateway,
                             config->static_netmask, config->static_dns);
    ESP_LOGI(TAG, "Static IP configured");
  }

  // Start Telnet server
  if (config->telnet_enabled) {
    if (telnet_server_start(network_mgr.telnet_server) != 0) {
      ESP_LOGE(TAG, "Failed to start Telnet server");
      return -1;
    }
    ESP_LOGI(TAG, "Telnet server started on port %d", TELNET_PORT);
  }

  // Start HTTP REST API server (v6.0.0+)
  if (config->http.enabled) {
    if (http_server_start(&config->http) != 0) {
      ESP_LOGE(TAG, "Failed to start HTTP server");
      // Non-fatal - continue without HTTP
    } else {
      ESP_LOGI(TAG, "HTTP server started on port %d", config->http.port);
    }
  }

  // Connect to Wi-Fi
  if (debug && debug->wifi_connect) {
    ESP_LOGI(TAG, "Calling wifi_driver_connect('%s', ...)", config->ssid);
  }

  if (wifi_driver_connect(config->ssid, config->password) != 0) {
    ESP_LOGE(TAG, "Failed to start Wi-Fi connection (wifi_driver_connect returned non-zero)");
    return -1;
  }

  ESP_LOGI(TAG, "Connecting to Wi-Fi network: %s", config->ssid);

  // Start Ethernet interface (v6.1.0+ W5500)
  if (config->ethernet.enabled) {
    ESP_LOGI(TAG, "Starting Ethernet interface (W5500)");

    // Configure DHCP or static IP for Ethernet
    if (config->ethernet.dhcp_enabled) {
      ethernet_driver_enable_dhcp();
    } else {
      ethernet_driver_set_static_ip(config->ethernet.static_ip,
                                     config->ethernet.static_gateway,
                                     config->ethernet.static_netmask,
                                     config->ethernet.static_dns);
    }

    if (ethernet_driver_start() != 0) {
      ESP_LOGE(TAG, "Failed to start Ethernet (W5500 not present?)");
      // Non-fatal - continue with Wi-Fi only
    } else {
      ESP_LOGI(TAG, "Ethernet started (DHCP: %s)", config->ethernet.dhcp_enabled ? "on" : "off");
    }
  }

  return 0;
}

int network_manager_stop(void)
{
  if (!network_mgr.initialized) {
    return -1;
  }

  // Stop HTTP REST API server (v6.0.0+)
  http_server_stop();

  // Stop Telnet server
  if (network_mgr.telnet_server) {
    telnet_server_stop(network_mgr.telnet_server);
  }

  // Stop Ethernet (v6.1.0+)
  ethernet_driver_stop();

  // Disconnect Wi-Fi
  wifi_driver_disconnect();

  ESP_LOGI(TAG, "Network manager stopped");

  return 0;
}

/* ============================================================================
 * STATUS & INFORMATION
 * ============================================================================ */

uint8_t network_manager_is_wifi_connected(void)
{
  return wifi_driver_is_connected();
}

uint8_t network_manager_is_ethernet_connected(void)
{
  return ethernet_driver_is_connected();
}

uint8_t network_manager_is_telnet_connected(void)
{
  if (!network_mgr.telnet_server) {
    return 0;
  }

  return telnet_server_client_connected(network_mgr.telnet_server);
}

uint32_t network_manager_get_local_ip(void)
{
  return wifi_driver_get_local_ip();
}

const NetworkState* network_manager_get_state(void)
{
  return &network_mgr.state;
}

/* ============================================================================
 * TELNET I/O
 * ============================================================================ */

int network_manager_telnet_readline(char *buf, size_t max_len)
{
  if (!network_mgr.telnet_server || !buf || !max_len) {
    return -1;
  }

  return telnet_server_readline(network_mgr.telnet_server, buf, max_len);
}

int network_manager_telnet_writeline(const char *line)
{
  if (!network_mgr.telnet_server || !line) {
    return -1;
  }

  return telnet_server_writeline(network_mgr.telnet_server, line);
}

int network_manager_telnet_writelinef(const char *format, ...)
{
  if (!network_mgr.telnet_server || !format) {
    return -1;
  }

  char buf[256];
  va_list args;
  va_start(args, format);
  int len = vsnprintf(buf, sizeof(buf) - 1, format, args);
  va_end(args);

  if (len < 0 || len >= (int)sizeof(buf)) {
    return -1;
  }

  return telnet_server_writeline(network_mgr.telnet_server, buf);
}

int network_manager_telnet_write(const char *text)
{
  if (!network_mgr.telnet_server || !text) {
    return -1;
  }

  return telnet_server_write(network_mgr.telnet_server, text);
}

uint8_t network_manager_telnet_has_input(void)
{
  if (!network_mgr.telnet_server) {
    return 0;
  }

  return telnet_server_has_input(network_mgr.telnet_server);
}

/* ============================================================================
 * BACKGROUND TASKS
 * ============================================================================ */

int network_manager_loop(void)
{
  int events = 0;

  if (!network_mgr.initialized) {
    return 0;
  }

  // Process Wi-Fi events (auto-reconnect, etc.)
  events += wifi_driver_loop();

  // Process Ethernet events (v6.1.0+)
  events += ethernet_driver_loop();

  // Update runtime state from Wi-Fi driver
  if (wifi_driver_is_connected()) {
    network_mgr.state.wifi_connected = 1;
    network_mgr.state.local_ip = wifi_driver_get_local_ip();
    network_mgr.state.gateway = wifi_driver_get_gateway();
    network_mgr.state.netmask = wifi_driver_get_netmask();
    network_mgr.state.dns = wifi_driver_get_dns();
  } else {
    network_mgr.state.wifi_connected = 0;
    network_mgr.state.local_ip = 0;
  }

  // Update runtime state from Ethernet driver (v6.1.0+)
  if (ethernet_driver_is_connected()) {
    network_mgr.state.eth_connected = 1;
    network_mgr.state.eth_local_ip = ethernet_driver_get_local_ip();
    network_mgr.state.eth_gateway = ethernet_driver_get_gateway();
    network_mgr.state.eth_netmask = ethernet_driver_get_netmask();
    network_mgr.state.eth_dns = ethernet_driver_get_dns();
  } else {
    network_mgr.state.eth_connected = 0;
    network_mgr.state.eth_local_ip = 0;
  }

  // Process Telnet server events (independent of Wi-Fi status)
  if (network_mgr.telnet_server) {
    events += telnet_server_loop(network_mgr.telnet_server);

    // Update Telnet client state
    if (telnet_server_client_connected(network_mgr.telnet_server)) {
      network_mgr.state.telnet_client_connected = 1;
    } else {
      network_mgr.state.telnet_client_connected = 0;
    }
  }

  return events;
}

/* ============================================================================
 * DEBUGGING & STATUS
 * ============================================================================ */

void network_manager_print_status(void)
{
  debug_printf("\n╔════════════════════════════════════════╗\n");
  debug_printf("║     NETWORK MANAGER STATUS            ║\n");
  debug_printf("╚════════════════════════════════════════╝\n\n");

  debug_printf("Wi-Fi Status: %s\n", network_manager_get_wifi_state_string());

  if (network_mgr.state.wifi_connected) {
    char ip_str[16];
    struct in_addr addr;
    addr.s_addr = network_mgr.state.local_ip;
    debug_printf("Local IP:     %s\n", inet_ntoa(addr));

    addr.s_addr = network_mgr.state.gateway;
    debug_printf("Gateway:      %s\n", inet_ntoa(addr));

    int8_t rssi = wifi_driver_get_rssi();
    debug_printf("Signal:       %d dBm\n", rssi);
  }

  debug_printf("Telnet:       %s\n", network_mgr.state.telnet_client_connected ? "Connected" : "Waiting");

  if (network_mgr.telnet_server) {
    debug_printf("Telnet Port:  %d\n", TELNET_PORT);
  }

  // Ethernet status (v6.1.0+)
  debug_printf("\nEthernet:     %s\n", ethernet_driver_get_state_string());
  if (network_mgr.state.eth_connected) {
    char ip_str[16];
    struct in_addr addr;
    addr.s_addr = network_mgr.state.eth_local_ip;
    debug_printf("ETH IP:       %s\n", inet_ntoa(addr));

    addr.s_addr = network_mgr.state.eth_gateway;
    debug_printf("ETH Gateway:  %s\n", inet_ntoa(addr));

    debug_printf("ETH Speed:    %lu Mbps %s\n",
                 ethernet_driver_get_speed(),
                 ethernet_driver_is_full_duplex() ? "Full-Duplex" : "Half-Duplex");

    char mac_str[18];
    ethernet_driver_get_mac_str(mac_str);
    debug_printf("ETH MAC:      %s\n", mac_str);
  }

  // HTTP REST API status (v6.0.0+)
  debug_printf("\nHTTP API:     %s\n", http_server_is_running() ? "Running" : "Stopped");
  if (http_server_is_running()) {
    const HttpConfig *http_cfg = http_server_get_config();
    if (http_cfg) {
      debug_printf("HTTP Port:    %d\n", http_cfg->port);
      debug_printf("HTTP Auth:    %s\n", http_cfg->auth_enabled ? "Enabled" : "Disabled");
    }
  }

  debug_printf("\n");
}

const char* network_manager_get_wifi_state_string(void)
{
  return wifi_driver_get_state_string();
}
