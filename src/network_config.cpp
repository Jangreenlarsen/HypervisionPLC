/**
 * @file network_config.cpp
 * @brief Network configuration validation and utility functions
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <lwip/inet.h>
#include <esp_log.h>

#include "network_config.h"
#include "constants.h"
#include "debug_flags.h"
#include "debug.h"

static const char *TAG = "NET_CFG";

/* ============================================================================
 * INITIALIZATION & DEFAULTS
 * ============================================================================ */

void network_config_init_defaults(NetworkConfig *config)
{
  if (!config) {
    return;
  }

  memset(config, 0, sizeof(NetworkConfig));

  config->enabled = 1;                    // Wi-Fi enabled by default
  config->dhcp_enabled = 1;               // DHCP enabled by default
  config->telnet_enabled = 1;             // Telnet enabled by default
  config->telnet_port = TELNET_PORT;      // Standard Telnet port

  // Default SSID (empty - must be set by user)
  strncpy(config->ssid, "", WIFI_SSID_MAX_LEN - 1);
  config->ssid[WIFI_SSID_MAX_LEN - 1] = '\0';

  // Default password (empty)
  strncpy(config->password, "", WIFI_PASSWORD_MAX_LEN - 1);
  config->password[WIFI_PASSWORD_MAX_LEN - 1] = '\0';

  // Default Telnet credentials (MUST be changed by user for security!)
  strncpy(config->telnet_username, "admin", sizeof(config->telnet_username) - 1);
  config->telnet_username[sizeof(config->telnet_username) - 1] = '\0';
  strncpy(config->telnet_password, "telnet123", sizeof(config->telnet_password) - 1);
  config->telnet_password[sizeof(config->telnet_password) - 1] = '\0';

  // Default HTTP REST API configuration (v6.0.0+)
  config->http.enabled = 1;                       // HTTP enabled by default
  config->http.port = HTTP_SERVER_PORT;           // Port 80
  config->http.auth_enabled = 0;                  // No auth by default (for ease of testing)
  strncpy(config->http.username, "admin", sizeof(config->http.username) - 1);
  config->http.username[sizeof(config->http.username) - 1] = '\0';
  strncpy(config->http.password, "modbus123", sizeof(config->http.password) - 1);
  config->http.password[sizeof(config->http.password) - 1] = '\0';
  config->http.tls_enabled = 0;                   // Default: plain HTTP
  config->http.api_enabled = 1;                   // API enabled by default
  config->http.priority = 1;                      // NORMAL priority (0=LOW, 1=NORMAL, 2=HIGH)
  memset(config->http.reserved, 0, sizeof(config->http.reserved));

  // Default static IP (192.168.1.100)
  config->static_ip = htonl(0xC0A80164);       // 192.168.1.100
  config->static_gateway = htonl(0xC0A80101);  // 192.168.1.1
  config->static_netmask = htonl(0xFFFFFF00);  // 255.255.255.0
  config->static_dns = htonl(0x08080808);      // 8.8.8.8 (Google DNS)

  // Default Ethernet configuration (v6.1.0+ W5500)
  config->ethernet.enabled = 0;                 // Disabled by default
  config->ethernet.dhcp_enabled = 1;            // DHCP by default
  config->ethernet.static_ip = htonl(0xC0A80165);       // 192.168.1.101
  config->ethernet.static_gateway = htonl(0xC0A80101);  // 192.168.1.1
  config->ethernet.static_netmask = htonl(0xFFFFFF00);  // 255.255.255.0
  config->ethernet.static_dns = htonl(0x08080808);      // 8.8.8.8
  memset(config->ethernet.hostname, 0, sizeof(config->ethernet.hostname));
  memset(config->ethernet.reserved, 0, sizeof(config->ethernet.reserved));

  ESP_LOGI(TAG, "Network config initialized with defaults");
}

/* ============================================================================
 * VALIDATION
 * ============================================================================ */

uint8_t network_config_validate(const NetworkConfig *config)
{
  if (!config) {
    ESP_LOGE(TAG, "Config is NULL");
    return 0;
  }

  DebugFlags *debug = debug_flags_get();

  if (debug && debug->network_validate) {
    ESP_LOGI(TAG, "Validating network config:");
    ESP_LOGI(TAG, "  enabled=%d", config->enabled);
    if (config->enabled) {
      ESP_LOGI(TAG, "  ssid='%s' (len=%d)", config->ssid, strlen(config->ssid));
      ESP_LOGI(TAG, "  password=(len=%d)", strlen(config->password));
      ESP_LOGI(TAG, "  dhcp_enabled=%d", config->dhcp_enabled);
      ESP_LOGI(TAG, "  telnet_enabled=%d, port=%d", config->telnet_enabled, config->telnet_port);
    }
  }

  // If enabled, require SSID
  if (config->enabled) {
    if (!network_config_is_valid_ssid(config->ssid)) {
      ESP_LOGE(TAG, "Invalid SSID (failed is_valid_ssid check)");
      return 0;
    }

    // SSID cannot be empty if enabled
    if (strlen(config->ssid) == 0) {
      ESP_LOGE(TAG, "SSID cannot be empty when WiFi is enabled (len=0)");
      return 0;
    }

    if (!network_config_is_valid_password(config->password)) {
      int pwd_len = strlen(config->password);
      ESP_LOGE(TAG, "Invalid password (len=%d, must be 0 or 8-63)", pwd_len);
      return 0;
    }
  }

  // If using static IP, validate
  if (!config->dhcp_enabled) {
    if (!network_config_is_valid_ip(config->static_ip)) {
      ESP_LOGE(TAG, "Invalid static IP");
      return 0;
    }

    if (!network_config_is_valid_netmask(config->static_netmask)) {
      ESP_LOGE(TAG, "Invalid netmask");
      return 0;
    }
  }

  // Validate Telnet port
  if (config->telnet_port < 1 || config->telnet_port > 65535) {
    ESP_LOGE(TAG, "Invalid Telnet port: %d", config->telnet_port);
    return 0;
  }

  // Validate HTTP port (v6.0.0+)
  if (config->http.enabled) {
    if (config->http.port < 1 || config->http.port > 65535) {
      ESP_LOGE(TAG, "Invalid HTTP port: %d", config->http.port);
      return 0;
    }
    // Check port conflict with Telnet
    if (config->telnet_enabled && config->http.port == config->telnet_port) {
      ESP_LOGE(TAG, "HTTP port conflicts with Telnet port: %d", config->http.port);
      return 0;
    }
  }

  return 1;
}

uint8_t network_config_is_valid_ssid(const char *ssid)
{
  if (!ssid) {
    return 0;
  }

  int len = strlen(ssid);

  // SSID can be 0-32 characters (empty SSID = disabled)
  if (len > WIFI_SSID_MAX_LEN - 1) {
    return 0;
  }

  // If non-empty, check for printable characters
  if (len > 0) {
    for (int i = 0; i < len; i++) {
      unsigned char c = (unsigned char)ssid[i];
      // Allow printable ASCII (0x20-0x7E)
      if (c < 0x20 || c > 0x7E) {
        return 0;
      }
    }
  }

  return 1;
}

uint8_t network_config_is_valid_password(const char *password)
{
  if (!password) {
    return 0;
  }

  int len = strlen(password);

  // WPA2 password: 8-63 characters (or 0 for open network)
  if (len > WIFI_PASSWORD_MAX_LEN - 1) {
    return 0;
  }

  if (len > 0 && len < 8) {
    // If non-empty, must be at least 8 characters (WPA2 minimum)
    return 0;
  }

  return 1;
}

uint8_t network_config_is_valid_ip(uint32_t ip)
{
  // 0.0.0.0 is invalid (reserved)
  if (ip == 0) {
    return 0;
  }

  // 255.255.255.255 is invalid (broadcast)
  if (ip == 0xFFFFFFFF) {
    return 0;
  }

  return 1;
}

uint8_t network_config_is_valid_netmask(uint32_t netmask)
{
  if (netmask == 0) {
    return 0;
  }

  // Netmask should be contiguous bits (e.g., 255.255.255.0)
  // Simple check: flip bits and add 1 should be power of 2
  uint32_t flipped = ~netmask;
  uint32_t check = flipped + 1;

  // Check if power of 2 (single bit set)
  return (check & flipped) == 0;
}

/* ============================================================================
 * CONVERSION UTILITIES
 * ============================================================================ */

uint8_t network_config_str_to_ip(const char *ip_str, uint32_t *out_ip)
{
  if (!ip_str || !out_ip) {
    return 0;
  }

  struct in_addr addr;
  int ret = inet_aton(ip_str, &addr);

  if (ret > 0) {
    *out_ip = addr.s_addr;
    return 1;
  }

  return 0;
}

char* network_config_ip_to_str(uint32_t ip, char *out_str)
{
  if (!out_str) {
    return NULL;
  }

  struct in_addr addr;
  addr.s_addr = ip;
  char *result = inet_ntoa(addr);

  if (result) {
    strncpy(out_str, result, 15);
    out_str[15] = '\0';
  } else {
    strcpy(out_str, "0.0.0.0");
  }

  return out_str;
}

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

void network_config_print(const NetworkConfig *config)
{
  if (!config) {
    debug_printf("Network Config: NULL\n");
    return;
  }

  debug_printf("\n=== Network Configuration ===\n");
  debug_printf("Enabled:       %d\n", config->enabled);
  debug_printf("SSID:          %s\n", config->ssid[0] ? config->ssid : "(empty)");
  debug_printf("Password:      %s\n", config->password[0] ? "***" : "(empty)");
  debug_printf("DHCP:          %s\n", config->dhcp_enabled ? "Enabled" : "Disabled");

  if (!config->dhcp_enabled) {
    char ip_str[16], gw_str[16], nm_str[16], dns_str[16];
    debug_printf("Static IP:     %s\n", network_config_ip_to_str(config->static_ip, ip_str));
    debug_printf("Gateway:       %s\n", network_config_ip_to_str(config->static_gateway, gw_str));
    debug_printf("Netmask:       %s\n", network_config_ip_to_str(config->static_netmask, nm_str));
    debug_printf("DNS:           %s\n", network_config_ip_to_str(config->static_dns, dns_str));
  }

  debug_printf("Telnet:        %s (port %d)\n", config->telnet_enabled ? "Enabled" : "Disabled", config->telnet_port);
  debug_printf("==============================\n\n");
}
