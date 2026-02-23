/**
 * @file wifi_driver.cpp
 * @brief ESP32 Wi-Fi driver implementation (Layer 0 hardware abstraction)
 *
 * Uses ESP-IDF Wi-Fi API directly (Arduino-ESP32 wrapper)
 * Handles WPA2 client mode, DHCP, ICMP ping, and auto-reconnect
 */

#include <string.h>
#include <Arduino.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_log.h>
#include <lwip/inet.h>
#include <lwip/dns.h>
#include <lwip/icmp.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <ping/ping_sock.h>

#include "wifi_driver.h"
#include "constants.h"
#include "types.h"

static const char *TAG = "WIFI_DRV";

/* ============================================================================
 * INTERNAL STATE
 * ============================================================================ */

typedef enum {
  WIFI_STATE_UNINITIALIZED = 0,
  WIFI_STATE_IDLE = 1,
  WIFI_STATE_CONNECTING = 2,
  WIFI_STATE_CONNECTED = 3,
  WIFI_STATE_DISCONNECTED = 4,
  WIFI_STATE_ERROR = 5
} WifiState;

typedef struct {
  WifiState state;
  uint32_t local_ip;
  uint32_t gateway;
  uint32_t netmask;
  uint32_t dns;
  char ssid[WIFI_SSID_MAX_LEN];
  char password[WIFI_PASSWORD_MAX_LEN];
  int8_t rssi;
  uint32_t connect_time_ms;
  uint32_t reconnect_retries;
  uint32_t last_reconnect_attempt_ms;

  // Static IP config
  uint32_t static_ip;
  uint32_t static_gateway;
  uint32_t static_netmask;
  uint32_t static_dns;
  uint8_t use_static_ip;

  // Scan state
  uint16_t scan_index;
  uint16_t scan_result_count;
} WifiDriverState;

static WifiDriverState wifi_state = {
  .state = WIFI_STATE_UNINITIALIZED,
  .local_ip = 0,
  .gateway = 0,
  .netmask = 0,
  .dns = 0,
  .ssid = {0},
  .password = {0},
  .rssi = 0,
  .connect_time_ms = 0,
  .reconnect_retries = 0,
  .last_reconnect_attempt_ms = 0,
  .static_ip = 0,
  .static_gateway = 0,
  .static_netmask = 0,
  .static_dns = 0,
  .use_static_ip = 0,
  .scan_index = 0,
  .scan_result_count = 0
};

/* ============================================================================
 * EVENT HANDLERS (Wi-Fi events)
 * ============================================================================ */

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
  if (event_base == WIFI_EVENT) {
    switch (event_id) {
      case WIFI_EVENT_STA_START:
        ESP_LOGI(TAG, "Wi-Fi started, connecting...");
        wifi_state.state = WIFI_STATE_CONNECTING;
        break;

      case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "Wi-Fi connected to AP");
        wifi_state.state = WIFI_STATE_CONNECTED;
        wifi_state.reconnect_retries = 0;
        break;

      case WIFI_EVENT_STA_DISCONNECTED: {
        wifi_event_sta_disconnected_t *disconn = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGI(TAG, "Wi-Fi disconnected (reason: %d)", disconn->reason);
        wifi_state.state = WIFI_STATE_DISCONNECTED;
        wifi_state.local_ip = 0;

        // Auto-reconnect if enabled
        if (wifi_state.reconnect_retries < WIFI_RECONNECT_MAX_RETRIES) {
          wifi_state.reconnect_retries++;
          wifi_state.last_reconnect_attempt_ms = millis();
          ESP_LOGI(TAG, "Reconnect attempt %lu/%d in %dms",
                   wifi_state.reconnect_retries, WIFI_RECONNECT_MAX_RETRIES,
                   WIFI_RECONNECT_INTERVAL_MS);
        }
        break;
      }

      default:
        break;
    }
  }
}

static void ip_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data)
{
  if (event_base == IP_EVENT) {
    switch (event_id) {
      case IP_EVENT_STA_GOT_IP: {
        ip_event_got_ip_t *event = (ip_event_got_ip_t*) event_data;
        wifi_state.local_ip = event->ip_info.ip.addr;
        wifi_state.gateway = event->ip_info.gw.addr;
        wifi_state.netmask = event->ip_info.netmask.addr;

        // Get DNS from dhcp
        const ip_addr_t *dns = dns_getserver(0);
        if (dns) {
          wifi_state.dns = dns->u_addr.ip4.addr;
        }

        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&event->ip_info.gw));
        ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&event->ip_info.netmask));

        wifi_state.connect_time_ms = millis();
        break;
      }

      case IP_EVENT_STA_LOST_IP:
        ESP_LOGW(TAG, "Lost IP address");
        wifi_state.local_ip = 0;
        break;

      default:
        break;
    }
  }
}

/* ============================================================================
 * INITIALIZATION & CONTROL
 * ============================================================================ */

int wifi_driver_init(void)
{
  if (wifi_state.state != WIFI_STATE_UNINITIALIZED) {
    ESP_LOGI(TAG, "Wi-Fi already initialized");
    return 0;
  }

  esp_err_t err;

  // NOTE: esp_netif_init() and esp_event_loop_create_default() are called
  // by network_manager_init() before this function. Do NOT call them here.

  // Create Wi-Fi station interface
  esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
  if (!sta_netif) {
    ESP_LOGE(TAG, "Failed to create default Wi-Fi STA interface");
    wifi_state.state = WIFI_STATE_ERROR;
    return -1;
  }

  // Initialize Wi-Fi with default config
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
    wifi_state.state = WIFI_STATE_ERROR;
    return -1;
  }

  // Register event handlers
  err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register WIFI_EVENT handler");
    wifi_state.state = WIFI_STATE_ERROR;
    return -1;
  }

  err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &ip_event_handler, NULL);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register IP_EVENT handler");
    wifi_state.state = WIFI_STATE_ERROR;
    return -1;
  }

  // Set Wi-Fi mode to station (client mode)
  err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set Wi-Fi mode: %s", esp_err_to_name(err));
    wifi_state.state = WIFI_STATE_ERROR;
    return -1;
  }

  // Start Wi-Fi
  err = esp_wifi_start();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start Wi-Fi: %s", esp_err_to_name(err));
    wifi_state.state = WIFI_STATE_ERROR;
    return -1;
  }

  // Set Wi-Fi power save mode based on config (v6.0.4+)
  // wifi_power_save: 0 = off (fast), 1 = on (power save)
  // Note: Config not loaded yet at init time, so default to power save OFF
  // The actual setting will be applied later when config is loaded
  err = esp_wifi_set_ps(WIFI_PS_NONE);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to set Wi-Fi power save: %s", esp_err_to_name(err));
    // Continue anyway - not critical
  }

  wifi_state.state = WIFI_STATE_IDLE;
  ESP_LOGI(TAG, "Wi-Fi driver initialized successfully");

  return 0;
}

int wifi_driver_connect(const char *ssid, const char *password)
{
  if (!ssid || !password) {
    ESP_LOGE(TAG, "Invalid SSID or password");
    return -1;
  }

  if (strlen(ssid) > WIFI_SSID_MAX_LEN - 1) {
    ESP_LOGE(TAG, "SSID too long (max %d)", WIFI_SSID_MAX_LEN - 1);
    return -1;
  }

  if (strlen(password) > WIFI_PASSWORD_MAX_LEN - 1) {
    ESP_LOGE(TAG, "Password too long (max %d)", WIFI_PASSWORD_MAX_LEN - 1);
    return -1;
  }

  // Save credentials
  strncpy(wifi_state.ssid, ssid, WIFI_SSID_MAX_LEN - 1);
  strncpy(wifi_state.password, password, WIFI_PASSWORD_MAX_LEN - 1);
  wifi_state.ssid[WIFI_SSID_MAX_LEN - 1] = '\0';
  wifi_state.password[WIFI_PASSWORD_MAX_LEN - 1] = '\0';

  // Configure Wi-Fi credentials
  wifi_sta_config_t sta_config;
  memset(&sta_config, 0, sizeof(sta_config));
  sta_config.threshold.authmode = WIFI_AUTH_WPA2_PSK;

  strncpy((char*)sta_config.ssid, ssid, sizeof(sta_config.ssid) - 1);
  strncpy((char*)sta_config.password, password, sizeof(sta_config.password) - 1);

  wifi_config_t wifi_config = {
    .sta = sta_config
  };

  esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set Wi-Fi config: %s", esp_err_to_name(err));
    return -1;
  }

  // Connect
  err = esp_wifi_connect();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to connect to Wi-Fi: %s", esp_err_to_name(err));
    return -1;
  }

  wifi_state.state = WIFI_STATE_CONNECTING;
  wifi_state.reconnect_retries = 0;
  ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);

  return 0;
}

int wifi_driver_disconnect(void)
{
  esp_err_t err = esp_wifi_disconnect();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to disconnect: %s", esp_err_to_name(err));
    return -1;
  }

  wifi_state.state = WIFI_STATE_IDLE;
  wifi_state.local_ip = 0;
  wifi_state.reconnect_retries = 0;
  ESP_LOGI(TAG, "Disconnected from Wi-Fi");

  return 0;
}

int wifi_driver_scan_start(void)
{
  wifi_scan_config_t scan_config;
  memset(&scan_config, 0, sizeof(scan_config));
  scan_config.ssid = NULL;
  scan_config.bssid = NULL;
  scan_config.channel = 0;
  scan_config.show_hidden = true;
  scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  scan_config.scan_time.active.min = 0;
  scan_config.scan_time.active.max = 120;

  esp_err_t err = esp_wifi_scan_start(&scan_config, false);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start scan: %s", esp_err_to_name(err));
    return -1;
  }

  wifi_state.scan_index = 0;
  ESP_LOGI(TAG, "Wi-Fi scan started");

  return 0;
}

int wifi_driver_scan_next(char *out_ssid, int8_t *out_rssi)
{
  if (!out_ssid || !out_rssi) {
    return -1;
  }

  // Get results (on first call)
  if (wifi_state.scan_index == 0) {
    esp_err_t err = esp_wifi_scan_get_ap_num(&wifi_state.scan_result_count);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to get scan count: %s", esp_err_to_name(err));
      return -1;
    }
  }

  if (wifi_state.scan_index >= wifi_state.scan_result_count) {
    wifi_state.scan_index = 0;
    wifi_state.scan_result_count = 0;
    return -1;  // No more results
  }

  wifi_ap_record_t ap_info;
  esp_err_t err = esp_wifi_scan_get_ap_records(&wifi_state.scan_index, &ap_info);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get scan record: %s", esp_err_to_name(err));
    return -1;
  }

  strncpy(out_ssid, (const char*)ap_info.ssid, WIFI_SSID_MAX_LEN - 1);
  out_ssid[WIFI_SSID_MAX_LEN - 1] = '\0';
  *out_rssi = ap_info.rssi;

  wifi_state.scan_index++;

  return 0;
}

/* ============================================================================
 * STATUS & INFORMATION
 * ============================================================================ */

uint8_t wifi_driver_is_connected(void)
{
  return (wifi_state.state == WIFI_STATE_CONNECTED) && (wifi_state.local_ip != 0);
}

uint32_t wifi_driver_get_local_ip(void)
{
  return wifi_state.local_ip;
}

uint32_t wifi_driver_get_gateway(void)
{
  return wifi_state.gateway;
}

uint32_t wifi_driver_get_netmask(void)
{
  return wifi_state.netmask;
}

uint32_t wifi_driver_get_dns(void)
{
  return wifi_state.dns;
}

int wifi_driver_get_ssid(char *out_ssid)
{
  if (!out_ssid) {
    return -1;
  }

  if (!wifi_driver_is_connected()) {
    return -1;
  }

  strncpy(out_ssid, wifi_state.ssid, WIFI_SSID_MAX_LEN - 1);
  out_ssid[WIFI_SSID_MAX_LEN - 1] = '\0';

  return 0;
}

int8_t wifi_driver_get_rssi(void)
{
  if (!wifi_driver_is_connected()) {
    return 0;
  }

  wifi_ap_record_t ap_info;
  esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
  if (err == ESP_OK) {
    wifi_state.rssi = ap_info.rssi;
  }

  return wifi_state.rssi;
}

/* ============================================================================
 * STATIC IP CONFIGURATION
 * ============================================================================ */

int wifi_driver_set_static_ip(uint32_t ip_addr, uint32_t gateway, uint32_t netmask, uint32_t dns)
{
  wifi_state.static_ip = ip_addr;
  wifi_state.static_gateway = gateway;
  wifi_state.static_netmask = netmask;
  wifi_state.static_dns = dns;
  wifi_state.use_static_ip = 1;

  ESP_LOGI(TAG, "Static IP configured (not yet applied)");
  return 0;
}

int wifi_driver_apply_static_ip(void)
{
  if (!wifi_state.use_static_ip) {
    ESP_LOGI(TAG, "Static IP not configured");
    return -1;
  }

  esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (!sta_netif) {
    ESP_LOGE(TAG, "Failed to get STA netif handle");
    return -1;
  }

  // Disable DHCP first
  esp_netif_dhcp_status_t status;
  esp_netif_dhcpc_get_status(sta_netif, &status);
  if (status == ESP_NETIF_DHCP_STARTED) {
    esp_netif_dhcpc_stop(sta_netif);
  }

  // Set static IP
  esp_netif_ip_info_t ip_info;
  ip_info.ip.addr = wifi_state.static_ip;
  ip_info.gw.addr = wifi_state.static_gateway;
  ip_info.netmask.addr = wifi_state.static_netmask;

  esp_err_t err = esp_netif_set_ip_info(sta_netif, &ip_info);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set static IP: %s", esp_err_to_name(err));
    return -1;
  }

  // Set DNS
  ip_addr_t dns_server;
  dns_server.u_addr.ip4.addr = wifi_state.static_dns;
  dns_server.type = IPADDR_TYPE_V4;
  dns_setserver(0, &dns_server);

  wifi_state.local_ip = wifi_state.static_ip;
  wifi_state.gateway = wifi_state.static_gateway;
  wifi_state.netmask = wifi_state.static_netmask;
  wifi_state.dns = wifi_state.static_dns;

  ESP_LOGI(TAG, "Static IP applied");
  return 0;
}

int wifi_driver_enable_dhcp(void)
{
  esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (!sta_netif) {
    ESP_LOGE(TAG, "Failed to get STA netif handle");
    return -1;
  }

  esp_err_t err = esp_netif_dhcpc_start(sta_netif);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to enable DHCP: %s", esp_err_to_name(err));
    return -1;
  }

  wifi_state.use_static_ip = 0;
  ESP_LOGI(TAG, "DHCP enabled");
  return 0;
}

/* ============================================================================
 * NETWORK UTILITIES (ICMP)
 * ============================================================================ */

int wifi_driver_ping(const char *host, uint32_t *out_time_ms)
{
  if (!host || !out_time_ms) {
    return -1;
  }

  // Note: ESP32 ping_sock API is asynchronous
  // For simplicity, we'll return -1 (not yet implemented properly)
  // Full implementation would use lwIP ping with callbacks
  ESP_LOGW(TAG, "ICMP ping not fully implemented yet");
  return -1;
}

uint32_t wifi_driver_resolve_hostname(const char *hostname)
{
  if (!hostname) {
    return 0;
  }

  struct in_addr inaddr;
  int err = inet_aton(hostname, &inaddr);
  if (err > 0) {
    return inaddr.s_addr;  // Already an IP address
  }

  // Try DNS resolution
  struct hostent *he = gethostbyname(hostname);
  if (!he || !he->h_addr) {
    ESP_LOGE(TAG, "Failed to resolve hostname: %s", hostname);
    return 0;
  }

  return *(uint32_t*)he->h_addr;
}

/* ============================================================================
 * BACKGROUND TASKS
 * ============================================================================ */

int wifi_driver_loop(void)
{
  // Auto-reconnect logic
  if (wifi_state.state == WIFI_STATE_DISCONNECTED &&
      wifi_state.reconnect_retries < WIFI_RECONNECT_MAX_RETRIES &&
      (millis() - wifi_state.last_reconnect_attempt_ms) > WIFI_RECONNECT_INTERVAL_MS) {

    if (strlen(wifi_state.ssid) > 0) {
      ESP_LOGI(TAG, "Auto-reconnect attempt %lu", wifi_state.reconnect_retries + 1);
      esp_wifi_connect();
      wifi_state.state = WIFI_STATE_CONNECTING;
      wifi_state.last_reconnect_attempt_ms = millis();
    }
  }

  return 0;
}

uint32_t wifi_driver_get_uptime_ms(void)
{
  if (wifi_state.state != WIFI_STATE_CONNECTED) {
    return 0;
  }

  return millis() - wifi_state.connect_time_ms;
}

/* ============================================================================
 * DEBUGGING
 * ============================================================================ */

void wifi_driver_print_status(void)
{
  printf("\n=== Wi-Fi Status ===\n");
  printf("State: %s\n", wifi_driver_get_state_string());

  if (wifi_driver_is_connected()) {
    char ip_str[16];
    struct in_addr addr;
    addr.s_addr = wifi_state.local_ip;
    strncpy(ip_str, inet_ntoa(addr), sizeof(ip_str) - 1);

    printf("SSID: %s\n", wifi_state.ssid);
    printf("IP: %s\n", ip_str);
    printf("RSSI: %d dBm\n", wifi_driver_get_rssi());
    printf("Uptime: %lu ms\n", wifi_driver_get_uptime_ms());
  } else {
    printf("Not connected\n");
  }
  printf("====================\n\n");
}

const char* wifi_driver_get_state_string(void)
{
  switch (wifi_state.state) {
    case WIFI_STATE_UNINITIALIZED:  return "Uninitialized";
    case WIFI_STATE_IDLE:           return "Idle";
    case WIFI_STATE_CONNECTING:     return "Connecting";
    case WIFI_STATE_CONNECTED:      return "Connected";
    case WIFI_STATE_DISCONNECTED:   return "Disconnected";
    case WIFI_STATE_ERROR:          return "Error";
    default:                        return "Unknown";
  }
}
