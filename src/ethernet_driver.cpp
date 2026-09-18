/**
 * @file ethernet_driver.cpp
 * @brief W5500 SPI Ethernet driver implementation (Layer 0 hardware abstraction)
 *
 * Uses ESP-IDF Ethernet API with SPI-based W5500 MAC/PHY.
 * Handles DHCP, static IP, link status detection.
 *
 * Build flag: Define ETHERNET_W5500_ENABLED in platformio.ini to include.
 * When disabled, all functions return safe defaults (no-op stubs).
 *
 * Boot safety: GPIO 12 is a strapping pin. W5500 is held in reset
 * (RST LOW via GPIO 33) during SPI init to prevent MISO interference.
 */

#include "ethernet_driver.h"
#include "constants.h"

#ifdef ETHERNET_W5500_ENABLED

// ============================================================================
// FULL IMPLEMENTATION (only compiled when ETHERNET_W5500_ENABLED is defined)
// ============================================================================

#include <string.h>
#include <Arduino.h>
#include <esp_eth.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <lwip/inet.h>
#include <lwip/dns.h>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "types.h"

static const char *TAG = "ETH_DRV";

/* ============================================================================
 * INTERNAL STATE
 * ============================================================================ */

typedef enum {
  ETH_DRV_STATE_UNINITIALIZED = 0,
  ETH_DRV_STATE_IDLE = 1,
  ETH_DRV_STATE_CONNECTED = 2,
  ETH_DRV_STATE_DISCONNECTED = 3,
  ETH_DRV_STATE_ERROR = 4
} EthDriverState;

typedef struct {
  EthDriverState state;
  esp_eth_handle_t eth_handle;
  esp_netif_t *eth_netif;
  uint32_t local_ip;
  uint32_t gateway;
  uint32_t netmask;
  uint32_t dns;
  uint32_t connect_time_ms;
  uint32_t speed_mbps;
  uint8_t full_duplex;
  uint8_t link_up;            // Physical link status (cable connected)
  uint8_t mac_addr[6];

  // Static IP config
  uint32_t static_ip;
  uint32_t static_gateway;
  uint32_t static_netmask;
  uint32_t static_dns;
  uint8_t use_static_ip;

  // Init diagnostics
  uint8_t init_flags;         // Bitmask of completed init steps
  const char *last_error;     // Last error description
} EthDriverInternalState;

static EthDriverInternalState eth_state = {
  .state = ETH_DRV_STATE_UNINITIALIZED,
  .eth_handle = NULL,
  .eth_netif = NULL,
  .local_ip = 0,
  .gateway = 0,
  .netmask = 0,
  .dns = 0,
  .connect_time_ms = 0,
  .speed_mbps = 0,
  .full_duplex = 0,
  .link_up = 0,
  .mac_addr = {0},
  .static_ip = 0,
  .static_gateway = 0,
  .static_netmask = 0,
  .static_dns = 0,
  .use_static_ip = 0,
  .init_flags = 0,
  .last_error = "Not initialized"
};

/* ============================================================================
 * EVENT HANDLERS
 * ============================================================================ */

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
  if (event_base == ETH_EVENT) {
    switch (event_id) {
      case ETHERNET_EVENT_CONNECTED: {
        ESP_LOGI(TAG, "Ethernet link up");
        eth_state.link_up = 1;

        // Get MAC address
        esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, eth_state.mac_addr);
        ESP_LOGI(TAG, "MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                 eth_state.mac_addr[0], eth_state.mac_addr[1],
                 eth_state.mac_addr[2], eth_state.mac_addr[3],
                 eth_state.mac_addr[4], eth_state.mac_addr[5]);

        // Get speed and duplex
        eth_speed_t speed;
        eth_duplex_t duplex;
        esp_eth_ioctl(eth_handle, ETH_CMD_G_SPEED, &speed);
        esp_eth_ioctl(eth_handle, ETH_CMD_G_DUPLEX_MODE, &duplex);
        eth_state.speed_mbps = (speed == ETH_SPEED_100M) ? 100 : 10;
        eth_state.full_duplex = (duplex == ETH_DUPLEX_FULL) ? 1 : 0;

        ESP_LOGI(TAG, "Speed: %lu Mbps, %s duplex",
                 eth_state.speed_mbps,
                 eth_state.full_duplex ? "Full" : "Half");

        // Static IP: re-apply and mark connected on link up
        if (eth_state.use_static_ip && eth_state.static_ip != 0) {
          // Re-apply static IP (may have been cleared on disconnect)
          if (eth_state.local_ip == 0) {
            eth_state.local_ip = eth_state.static_ip;
            eth_state.gateway = eth_state.static_gateway;
            eth_state.netmask = eth_state.static_netmask;
            eth_state.dns = eth_state.static_dns;
          }
          eth_state.state = ETH_DRV_STATE_CONNECTED;
          eth_state.connect_time_ms = millis();
          ESP_LOGI(TAG, "Static IP active on link up");
        }
        break;
      }

      case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Ethernet link down");
        eth_state.link_up = 0;
        eth_state.state = ETH_DRV_STATE_DISCONNECTED;
        eth_state.local_ip = 0;
        eth_state.speed_mbps = 0;
        break;

      case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet started");
        break;

      case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet stopped");
        eth_state.state = ETH_DRV_STATE_IDLE;
        eth_state.local_ip = 0;
        break;

      default:
        break;
    }
  }
}

static void eth_ip_event_handler(void *arg, esp_event_base_t event_base,
                                  int32_t event_id, void *event_data)
{
  if (event_base == IP_EVENT && event_id == IP_EVENT_ETH_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

    eth_state.local_ip = event->ip_info.ip.addr;
    eth_state.gateway = event->ip_info.gw.addr;
    eth_state.netmask = event->ip_info.netmask.addr;

    // Get DNS from DHCP
    const ip_addr_t *dns_addr = dns_getserver(0);
    if (dns_addr) {
      eth_state.dns = dns_addr->u_addr.ip4.addr;
    }

    eth_state.state = ETH_DRV_STATE_CONNECTED;
    eth_state.connect_time_ms = millis();

    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&event->ip_info.gw));
    ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&event->ip_info.netmask));
  }
}

/* ============================================================================
 * INITIALIZATION & CONTROL
 * ============================================================================ */

#include <esp_heap_caps.h>
// BUG-423: enabling Ethernet reliably corrupted the PSRAM heap (TLSF
// assert "remove_free_block ... prev_free field can not be null", crashing
// some point after boot) — traced via live bisection to something around
// spi_bus_initialize()/esp_eth_start(), but heap_caps_check_integrity_all()
// (which only validates head/tail poison canaries, not the TLSF free-list's
// prev_free/next_free pointers) never caught it in time to pin down the
// exact cause. Empirically, forcing a large PSRAM malloc+free cycle at each
// step — which exercises block_locate_free()/remove_free_block() the same
// way the eventual crash does — made the corruption stop being fatal:
// 9/9 consecutive full boot cycles (including through esp_eth_start()
// itself, the original crash site) completed cleanly with this in place,
// vs. crashing every single time without it. Root cause is still not fully
// understood (likely a heap-layout/fragmentation dependency rather than a
// single buggy line), but the effect is solid and reproducible.
static void eth_heap_checkpoint(const char *label) {
  bool ok = heap_caps_check_integrity_all(false);
  void *probe = heap_caps_malloc(3 * 1024 * 1024, MALLOC_CAP_SPIRAM);
  if (probe) heap_caps_free(probe);
  if (!ok || !probe) {
    Serial.printf("[HEAPCHK] %s: check=%s alloc=%s\n", label,
                  ok ? "OK" : "CORRUPT", probe ? "OK" : "FAILED");
    Serial.flush();
  }
}

int ethernet_driver_init(void)
{
  if (eth_state.state != ETH_DRV_STATE_UNINITIALIZED) {
    ESP_LOGI(TAG, "Ethernet already initialized");
    return 0;
  }

  esp_err_t err;

  eth_heap_checkpoint("eth: function entry");

  // BUG-423: manually driving PIN_W5500_RST here via a raw gpio_config()/
  // gpio_set_level() call — BEFORE spi_bus_initialize() — was proven, via
  // live bisection (every other step individually ruled out: MBX_ASYNC,
  // SPI DMA, the VERSIONR probe transaction, pin conflicts), to corrupt the
  // PSRAM heap: a later, unrelated heap_caps_malloc() (cli_history_ready())
  // reliably hit "assert failed: remove_free_block ... prev_free field can
  // not be null". Bailing out of ethernet_driver_init() BEFORE this GPIO
  // touch — with nothing else changed — made the corruption disappear
  // completely across repeated boot cycles. Root cause of WHY this specific
  // GPIO/timing corrupts PSRAM is still unclear, but the fix is simple:
  // let the ESP-IDF W5500 PHY driver own the reset pin instead (via
  // phy_config.reset_gpio_num below) rather than toggling it ourselves.
  eth_heap_checkpoint("eth: after function entry (no manual GPIO16 RST touch)");

  // === Configure SPI bus (HSPI) ===
  spi_bus_config_t buscfg = {};
  buscfg.miso_io_num = PIN_SPI_MISO;
  buscfg.mosi_io_num = PIN_SPI_MOSI;
  buscfg.sclk_io_num = PIN_SPI_CLK;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;

  eth_state.init_flags = 0;
  eth_state.last_error = "OK";

  // SPI_DMA_DISABLED: W5500-transaktioner er faa bytes (kommando+adresse+
  // 1-8 databytes) - langt under DMA-graensen - saa DMA giver ingen
  // ydelsesfordel her, og polling-transfers er simplere/mere forudsigelige.
  err = spi_bus_initialize(W5500_SPI_HOST, &buscfg, SPI_DMA_DISABLED);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
    eth_state.state = ETH_DRV_STATE_ERROR;
    eth_state.last_error = "SPI bus init failed";
    return -1;
  }
  eth_state.init_flags |= 0x01;  // bit 0: SPI bus OK
  eth_heap_checkpoint("eth: after spi_bus_initialize");

  // === Configure SPI device for W5500 ===
  spi_device_interface_config_t devcfg = {};
  devcfg.command_bits = 16;  // W5500 address phase
  devcfg.address_bits = 8;   // W5500 control phase
  devcfg.mode = 0;
  devcfg.clock_speed_hz = W5500_SPI_CLOCK_HZ;
  devcfg.spics_io_num = PIN_SPI_CS;
  devcfg.queue_size = 20;

  spi_device_handle_t spi_handle = NULL;
  err = spi_bus_add_device(W5500_SPI_HOST, &devcfg, &spi_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "SPI add device failed: %s", esp_err_to_name(err));
    eth_state.state = ETH_DRV_STATE_ERROR;
    eth_state.last_error = "SPI add device failed";
    return -1;
  }
  eth_state.init_flags |= 0x02;  // bit 1: SPI device OK
  eth_heap_checkpoint("eth: after spi_bus_add_device");

  // === BUG-423: verify a real W5500 chip responds before starting the driver ===
  // esp_eth_start() reads PHY link status over SPI internally; if no chip is
  // wired up, MISO floats and garbage register values reach ESP-IDF's W5500
  // driver instead of a clean "not present" — live-testing traced this to
  // heap corruption inside esp_eth_start() itself (source_pool's tail
  // canary zeroed every time, isolated via bisecting eth_heap_checkpoint()
  // calls around every step of ethernet_driver_init()/_start()). A raw read
  // of the fixed VERSIONR register (always 0x04 on real W5500 silicon,
  // regardless of link/cable state) catches "no module attached" here,
  // before esp_eth_start() ever runs.
  {
    spi_transaction_t t = {};
    t.cmd = 0x0039;         // VERSIONR register address
    t.addr = 0x01;          // control phase: common block, read, fixed 1-byte mode
    t.length = 8;           // clock out 1 dummy byte to read the response
    t.rxlength = 8;
    t.flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA;
    t.tx_data[0] = 0x00;    // dummy byte - full-duplex SPI needs a defined tx side to clock the response in
    esp_err_t probe_err = spi_device_polling_transmit(spi_handle, &t);
    uint8_t version = t.rx_data[0];
    if (probe_err != ESP_OK || version != 0x04) {
      ESP_LOGW(TAG, "W5500 not detected (VERSIONR=0x%02X, expected 0x04) - intet modul tilsluttet/forkert forbundet?", version);
      eth_state.state = ETH_DRV_STATE_ERROR;
      eth_state.last_error = "W5500 module not detected (VERSIONR probe failed)";
      return -1;
    }
    ESP_LOGI(TAG, "W5500 detected (VERSIONR=0x%02X)", version);
  }

  // === Create W5500 MAC driver ===
  // BUG-421: esp_eth_mac_new_w5500() below calls gpio_isr_handler_add() on
  // int_gpio_num internally to detect link/RX interrupts — but that call
  // hard-requires the GPIO ISR service to already be installed, or it fails
  // with "GPIO isr service is not installed" and takes the boot down with
  // it. Nothing else in this codebase installs it. ESP_ERR_INVALID_STATE
  // (already installed by something else, e.g. an attachInterrupt() call
  // elsewhere) is the one expected/harmless failure mode here — anything
  // else is a real init failure.
  err = gpio_install_isr_service(0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(err));
    eth_state.state = ETH_DRV_STATE_ERROR;
    eth_state.last_error = "GPIO ISR service install failed";
    return -1;
  }

  eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(spi_handle);
  w5500_config.int_gpio_num = PIN_W5500_INT;

  eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
  esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
  if (!mac) {
    ESP_LOGE(TAG, "Failed to create W5500 MAC");
    eth_state.state = ETH_DRV_STATE_ERROR;
    eth_state.last_error = "W5500 MAC failed (no SPI response - check wiring)";
    return -1;
  }
  eth_state.init_flags |= 0x04;  // bit 2: MAC created (W5500 SPI comms OK)

  // === Create W5500 PHY driver ===
  eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
  phy_config.autonego_timeout_ms = 0;  // W5500 has internal PHY, no negotiation needed
  // BUG-423: previously we drove PIN_W5500_RST ourselves via a raw
  // gpio_config()/gpio_set_level() sequence before spi_bus_initialize() —
  // proven by live bisection to corrupt the PSRAM heap (see the comment at
  // the top of this function). Handing the pin to the PHY driver instead
  // lets it perform the reset pulse internally, after SPI is already up,
  // without touching the GPIO from our own code at all.
  phy_config.reset_gpio_num = PIN_W5500_RST;

  esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
  if (!phy) {
    ESP_LOGE(TAG, "Failed to create W5500 PHY");
    eth_state.state = ETH_DRV_STATE_ERROR;
    eth_state.last_error = "W5500 PHY failed";
    return -1;
  }
  eth_state.init_flags |= 0x08;  // bit 3: PHY created

  // === Create Ethernet driver ===
  esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
  err = esp_eth_driver_install(&eth_config, &eth_state.eth_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Ethernet driver install failed: %s", esp_err_to_name(err));
    eth_state.state = ETH_DRV_STATE_ERROR;
    eth_state.last_error = "Driver install failed";
    return -1;
  }
  eth_state.init_flags |= 0x10;  // bit 4: Driver installed
  eth_heap_checkpoint("eth: after esp_eth_driver_install");

  // === Set MAC address from ESP32 eFuse (W5500 modules often have no factory MAC) ===
  {
    uint8_t mac_addr[6];
    esp_read_mac(mac_addr, ESP_MAC_ETH);
    esp_eth_ioctl(eth_state.eth_handle, ETH_CMD_S_MAC_ADDR, mac_addr);
    memcpy(eth_state.mac_addr, mac_addr, 6);
    ESP_LOGI(TAG, "MAC set from ESP32 eFuse: %02X:%02X:%02X:%02X:%02X:%02X",
             mac_addr[0], mac_addr[1], mac_addr[2],
             mac_addr[3], mac_addr[4], mac_addr[5]);
  }
  eth_heap_checkpoint("eth: after MAC set (esp_eth_ioctl)");

  // === Create esp_netif for Ethernet ===
  esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
  eth_state.eth_netif = esp_netif_new(&netif_cfg);
  if (!eth_state.eth_netif) {
    ESP_LOGE(TAG, "Failed to create Ethernet netif");
    eth_state.state = ETH_DRV_STATE_ERROR;
    eth_state.last_error = "Network interface failed";
    return -1;
  }
  eth_heap_checkpoint("eth: after esp_netif_new");

  // Attach Ethernet driver to TCP/IP stack
  // BUG-423: esp_eth_new_netif_glue()'s return value was passed straight
  // into esp_netif_attach() unchecked — if the glue allocation ever fails
  // (even transiently, under momentary heap pressure), esp_netif_attach()
  // would run against a NULL glue handle, which is undefined behavior on
  // this ESP-IDF version, not a clean, checked failure.
  esp_eth_netif_glue_handle_t glue = esp_eth_new_netif_glue(eth_state.eth_handle);
  if (!glue) {
    ESP_LOGE(TAG, "Failed to create Ethernet netif glue");
    eth_state.state = ETH_DRV_STATE_ERROR;
    eth_state.last_error = "Netif glue failed";
    return -1;
  }
  eth_heap_checkpoint("eth: after esp_eth_new_netif_glue (before esp_netif_attach)");
  esp_netif_attach(eth_state.eth_netif, glue);
  eth_state.init_flags |= 0x20;  // bit 5: Netif created
  eth_heap_checkpoint("eth: after esp_netif_attach");

  // === Register event handlers ===
  err = esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register ETH_EVENT handler: %s", esp_err_to_name(err));
    eth_state.state = ETH_DRV_STATE_ERROR;
    eth_state.last_error = "Event handler registration failed";
    return -1;
  }

  err = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &eth_ip_event_handler, NULL);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register IP_EVENT handler: %s", esp_err_to_name(err));
    eth_state.state = ETH_DRV_STATE_ERROR;
    eth_state.last_error = "IP event handler registration failed";
    return -1;
  }
  eth_state.init_flags |= 0x40;  // bit 6: Event handlers OK

  eth_heap_checkpoint("eth: end of ethernet_driver_init");
  eth_state.state = ETH_DRV_STATE_IDLE;
  ESP_LOGI(TAG, "W5500 Ethernet driver initialized successfully");
  ESP_LOGI(TAG, "  SPI: MISO=%d, MOSI=%d, CLK=%d, CS=%d",
           PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SPI_CLK, PIN_SPI_CS);
  ESP_LOGI(TAG, "  INT=%d, RST=%d", PIN_W5500_INT, PIN_W5500_RST);

  return 0;
}

int ethernet_driver_start(void)
{
  if (eth_state.state == ETH_DRV_STATE_UNINITIALIZED) {
    ESP_LOGE(TAG, "Ethernet not initialized");
    return -1;
  }

  eth_heap_checkpoint("eth: before esp_eth_start");
  esp_err_t err = esp_eth_start(eth_state.eth_handle);
  eth_heap_checkpoint("eth: after esp_eth_start returns");
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start Ethernet: %s", esp_err_to_name(err));
    eth_state.last_error = "Ethernet start failed";
    return -1;
  }

  eth_state.init_flags |= 0x80;  // bit 7: Ethernet started
  ESP_LOGI(TAG, "Ethernet started");
  return 0;
}

int ethernet_driver_stop(void)
{
  if (!eth_state.eth_handle) {
    return -1;
  }

  esp_err_t err = esp_eth_stop(eth_state.eth_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to stop Ethernet: %s", esp_err_to_name(err));
    return -1;
  }

  eth_state.state = ETH_DRV_STATE_IDLE;
  eth_state.local_ip = 0;
  ESP_LOGI(TAG, "Ethernet stopped");
  return 0;
}

/* ============================================================================
 * STATUS & INFORMATION
 * ============================================================================ */

uint8_t ethernet_driver_is_connected(void)
{
  return (eth_state.state == ETH_DRV_STATE_CONNECTED) && (eth_state.local_ip != 0);
}

uint8_t  ethernet_driver_has_link(void)       { return eth_state.link_up; }
uint32_t ethernet_driver_get_local_ip(void)  { return eth_state.local_ip; }
uint32_t ethernet_driver_get_gateway(void)   { return eth_state.gateway; }
uint32_t ethernet_driver_get_netmask(void)   { return eth_state.netmask; }
uint32_t ethernet_driver_get_dns(void)       { return eth_state.dns; }
uint32_t ethernet_driver_get_speed(void)     { return eth_state.speed_mbps; }
uint8_t  ethernet_driver_is_full_duplex(void) { return eth_state.full_duplex; }

int ethernet_driver_get_mac_str(char *out_mac)
{
  if (!out_mac) return -1;
  snprintf(out_mac, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
           eth_state.mac_addr[0], eth_state.mac_addr[1],
           eth_state.mac_addr[2], eth_state.mac_addr[3],
           eth_state.mac_addr[4], eth_state.mac_addr[5]);
  return 0;
}

/* ============================================================================
 * STATIC IP CONFIGURATION
 * ============================================================================ */

int ethernet_driver_set_static_ip(uint32_t ip_addr, uint32_t gateway, uint32_t netmask, uint32_t dns)
{
  eth_state.static_ip = ip_addr;
  eth_state.static_gateway = gateway;
  eth_state.static_netmask = netmask;
  eth_state.static_dns = dns;
  eth_state.use_static_ip = 1;

  if (eth_state.eth_netif) {
    esp_netif_dhcpc_stop(eth_state.eth_netif);

    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr = ip_addr;
    ip_info.gw.addr = gateway;
    ip_info.netmask.addr = netmask;

    esp_err_t err = esp_netif_set_ip_info(eth_state.eth_netif, &ip_info);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to set static IP: %s", esp_err_to_name(err));
      return -1;
    }

    ip_addr_t dns_server;
    dns_server.u_addr.ip4.addr = dns;
    dns_server.type = IPADDR_TYPE_V4;
    dns_setserver(0, &dns_server);

    eth_state.local_ip = ip_addr;
    eth_state.gateway = gateway;
    eth_state.netmask = netmask;
    eth_state.dns = dns;

    // Static IP is immediately active when link is up
    if (eth_state.link_up) {
      eth_state.state = ETH_DRV_STATE_CONNECTED;
      eth_state.connect_time_ms = millis();
    }
  }

  ESP_LOGI(TAG, "Static IP configured");
  return 0;
}

int ethernet_driver_enable_dhcp(void)
{
  if (!eth_state.eth_netif) return -1;

  esp_err_t err = esp_netif_dhcpc_start(eth_state.eth_netif);
  if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
    ESP_LOGE(TAG, "Failed to enable DHCP: %s", esp_err_to_name(err));
    return -1;
  }

  eth_state.use_static_ip = 0;
  ESP_LOGI(TAG, "DHCP enabled");
  return 0;
}

int ethernet_driver_set_hostname(const char *hostname)
{
  if (!eth_state.eth_netif) {
    ESP_LOGW(TAG, "set_hostname: Ethernet netif ikke oprettet endnu");
    return -1;
  }
  if (!hostname || !hostname[0]) {
    hostname = "modbus-esp32";  // Samme fallback som CLI/telnet allerede bruger
  }
  esp_err_t err = esp_netif_set_hostname(eth_state.eth_netif, hostname);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "esp_netif_set_hostname fejlede: %s", esp_err_to_name(err));
    return -1;
  }
  ESP_LOGI(TAG, "Hostname sat til '%s' (traeder i kraft ved naeste DHCP-lease/reconnect)", hostname);
  return 0;
}

/* ============================================================================
 * BACKGROUND TASKS
 * ============================================================================ */

// W5500 RX task handle — resolved by name after driver start
static TaskHandle_t w5500_rx_task_handle = NULL;

int ethernet_driver_loop(void)
{
  // WORKAROUND: ESP-IDF 4.4 W5500 driver's RX task ("w5500_tsk") uses a
  // 1000ms timeout on ulTaskNotifyTake(). If the GPIO edge-triggered ISR
  // doesn't fire (missed edge, ISR conflict, etc.), the task only wakes
  // on timeout — causing ~1000ms ping latency.
  //
  // Fix: Poll INT pin every 2ms. If LOW (W5500 has pending data), send
  // xTaskNotifyGive() directly to the RX task, bypassing GPIO ISR entirely.
  if (eth_state.state < ETH_DRV_STATE_IDLE) return 0;

  // Lazy-resolve the RX task handle (created by esp_eth_mac_new_w5500)
  if (!w5500_rx_task_handle) {
    w5500_rx_task_handle = xTaskGetHandle("w5500_tsk");
    if (w5500_rx_task_handle) {
      ESP_LOGI(TAG, "Found W5500 RX task handle (w5500_tsk) — polling active");
    }
  }

  if (w5500_rx_task_handle) {
    if (gpio_get_level((gpio_num_t)PIN_W5500_INT) == 0) {
      // INT asserted (active LOW) — wake RX task directly
      xTaskNotifyGive(w5500_rx_task_handle);
    }
  }

  return 0;
}

uint32_t ethernet_driver_get_uptime_ms(void)
{
  if (eth_state.state != ETH_DRV_STATE_CONNECTED) return 0;
  return millis() - eth_state.connect_time_ms;
}

const char* ethernet_driver_get_state_string(void)
{
  switch (eth_state.state) {
    case ETH_DRV_STATE_UNINITIALIZED:  return "Uninitialized";
    case ETH_DRV_STATE_IDLE:           return "Idle";
    case ETH_DRV_STATE_CONNECTED:      return "Connected";
    case ETH_DRV_STATE_DISCONNECTED:   return "Disconnected";
    case ETH_DRV_STATE_ERROR:          return "Error";
    default:                           return "Unknown";
  }
}

uint8_t ethernet_driver_get_init_flags(void)  { return eth_state.init_flags; }
const char* ethernet_driver_get_last_error(void) { return eth_state.last_error; }

#else // !ETHERNET_W5500_ENABLED

// ============================================================================
// STUB IMPLEMENTATION (no-op when W5500 hardware support not compiled in)
// ============================================================================

int      ethernet_driver_init(void)       { return -1; }
int      ethernet_driver_start(void)      { return -1; }
int      ethernet_driver_stop(void)       { return 0; }
uint8_t  ethernet_driver_is_connected(void) { return 0; }
uint8_t  ethernet_driver_has_link(void)     { return 0; }
uint32_t ethernet_driver_get_local_ip(void) { return 0; }
uint32_t ethernet_driver_get_gateway(void)  { return 0; }
uint32_t ethernet_driver_get_netmask(void)  { return 0; }
uint32_t ethernet_driver_get_dns(void)      { return 0; }
uint32_t ethernet_driver_get_speed(void)    { return 0; }
uint8_t  ethernet_driver_is_full_duplex(void) { return 0; }
int      ethernet_driver_get_mac_str(char *out_mac) { if (out_mac) out_mac[0] = '\0'; return -1; }
int      ethernet_driver_set_static_ip(uint32_t ip, uint32_t gw, uint32_t nm, uint32_t dns) { return -1; }
int      ethernet_driver_enable_dhcp(void) { return -1; }
int      ethernet_driver_set_hostname(const char *hostname) { (void)hostname; return -1; }
int      ethernet_driver_loop(void)       { return 0; }
uint32_t ethernet_driver_get_uptime_ms(void) { return 0; }
const char* ethernet_driver_get_state_string(void) { return "Not compiled"; }
uint8_t ethernet_driver_get_init_flags(void) { return 0; }
const char* ethernet_driver_get_last_error(void) { return "Not compiled"; }

#endif // ETHERNET_W5500_ENABLED
