/**
 * @file tcp_server.cpp
 * @brief TCP server implementation for ESP32
 *
 * Non-blocking socket operations using lwIP sockets (POSIX-like API).
 * Handles single client for Telnet protocol.
 */

#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <Arduino.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>

#include "tcp_server.h"
#include "constants.h"

static const char *TAG = "TCP_SRV";

// Cached W5500 RX task handle for direct notification during send retries
static TaskHandle_t s_w5500_rx_task = NULL;

#define TCP_RECV_BUFFER_SIZE    256

/* ============================================================================
 * INITIALIZATION & CONTROL
 * ============================================================================ */

TcpServer* tcp_server_create(uint16_t port)
{
  TcpServer *server = (TcpServer*)malloc(sizeof(TcpServer));
  if (!server) {
    ESP_LOGE(TAG, "Failed to allocate server");
    return NULL;
  }

  memset(server, 0, sizeof(TcpServer));
  server->listen_socket = -1;
  server->listen_port = port;
  server->active = 0;
  server->client_count = 0;

  // Initialize clients
  for (int i = 0; i < 1; i++) {
    server->clients[i].socket = -1;
    server->clients[i].connected = 0;
  }

  ESP_LOGI(TAG, "TCP server created for port %d", port);
  return server;
}

int tcp_server_start(TcpServer *server)
{
  if (!server || server->active) {
    return -1;
  }

  // Create listening socket
  server->listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (server->listen_socket < 0) {
    ESP_LOGE(TAG, "Failed to create socket (errno: %d)", errno);
    return -1;
  }

  // Set socket options
  int opt = 1;
  if (setsockopt(server->listen_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    ESP_LOGW(TAG, "Failed to set SO_REUSEADDR");
  }

  // Set non-blocking mode
  int flags = fcntl(server->listen_socket, F_GETFL, 0);
  if (fcntl(server->listen_socket, F_SETFL, flags | O_NONBLOCK) < 0) {
    ESP_LOGE(TAG, "Failed to set non-blocking mode");
    close(server->listen_socket);
    server->listen_socket = -1;
    return -1;
  }

  // Bind socket
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(server->listen_port);

  if (bind(server->listen_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    ESP_LOGE(TAG, "Failed to bind socket (errno: %d)", errno);
    close(server->listen_socket);
    server->listen_socket = -1;
    return -1;
  }

  // Listen
  if (listen(server->listen_socket, 1) < 0) {
    ESP_LOGE(TAG, "Failed to listen on socket (errno: %d)", errno);
    close(server->listen_socket);
    server->listen_socket = -1;
    return -1;
  }

  server->active = 1;
  server->created_time_ms = millis();
  ESP_LOGI(TAG, "TCP server listening on port %d", server->listen_port);

  return 0;
}

int tcp_server_stop(TcpServer *server)
{
  if (!server || !server->active) {
    return -1;
  }

  // Disconnect all clients
  tcp_server_disconnect_all(server);

  // Close listening socket
  if (server->listen_socket >= 0) {
    close(server->listen_socket);
    server->listen_socket = -1;
  }

  server->active = 0;
  ESP_LOGI(TAG, "TCP server stopped");

  return 0;
}

void tcp_server_destroy(TcpServer *server)
{
  if (!server) {
    return;
  }

  tcp_server_stop(server);
  free(server);
}

/* ============================================================================
 * CLIENT MANAGEMENT
 * ============================================================================ */

int tcp_server_accept(TcpServer *server)
{
  if (!server || !server->active) {
    return 0;
  }

  int accepted = 0;

  // Try to accept one connection (single-client mode)
  if (!tcp_server_client_is_connected(server, 0)) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    int client_sock = accept(server->listen_socket, (struct sockaddr*)&client_addr, &client_addr_len);
    if (client_sock >= 0) {
      // Set non-blocking mode for client socket
      int flags = fcntl(client_sock, F_GETFL, 0);
      fcntl(client_sock, F_SETFL, flags | O_NONBLOCK);

      // Disable Nagle's algorithm for low-latency character echo (Telnet)
      // Without this, small TCP packets (1-byte echo) are delayed up to 200ms
      int nodelay = 1;
      setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

      server->clients[0].socket = client_sock;
      server->clients[0].client_ip = client_addr.sin_addr.s_addr;
      server->clients[0].client_port = ntohs(client_addr.sin_port);
      server->clients[0].last_activity_ms = millis();
      server->clients[0].connected = 1;
      server->client_count = 1;
      accepted = 1;

      struct in_addr addr;
      addr.s_addr = server->clients[0].client_ip;
      ESP_LOGI(TAG, "Client connected: %s:%d", inet_ntoa(addr), server->clients[0].client_port);
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
      ESP_LOGE(TAG, "Accept error (errno: %d)", errno);
    }
  }

  return accepted;
}

TcpClient* tcp_server_get_client(TcpServer *server, uint8_t index)
{
  if (!server || index >= 1) {
    return NULL;
  }

  if (server->clients[index].connected && server->clients[index].socket >= 0) {
    return &server->clients[index];
  }

  return NULL;
}

int tcp_server_disconnect_client(TcpServer *server, uint8_t client_index)
{
  if (!server || client_index >= 1) {
    return -1;
  }

  TcpClient *client = &server->clients[client_index];

  if (client->socket >= 0) {
    shutdown(client->socket, SHUT_RDWR);
    close(client->socket);
    client->socket = -1;
  }

  client->connected = 0;
  if (server->client_count > 0) {
    server->client_count--;
  }

  ESP_LOGI(TAG, "Client %d disconnected", client_index);
  return 0;
}

int tcp_server_disconnect_all(TcpServer *server)
{
  if (!server) {
    return -1;
  }

  for (int i = 0; i < 1; i++) {
    if (server->clients[i].connected) {
      tcp_server_disconnect_client(server, i);
    }
  }

  return 0;
}

uint8_t tcp_server_has_clients(TcpServer *server)
{
  if (!server) {
    return 0;
  }

  return server->client_count > 0;
}

/* ============================================================================
 * DATA TRANSMISSION
 * ============================================================================ */

int tcp_server_send(TcpServer *server, uint8_t client_index, const uint8_t *data, size_t len)
{
  if (!server || !data || !len) {
    return -1;
  }

  TcpClient *client = tcp_server_get_client(server, client_index);
  if (!client) {
    return -1;
  }

  // Send with retry on EAGAIN (TCP buffer full)
  // Without retry, large CLI outputs (sh config) get silently truncated.
  // IMPORTANT: We must notify W5500 RX task during retries so TCP ACKs
  // are processed. Using delay() alone blocks main loop and W5500 only
  // wakes on its 1000ms timeout — causing 1-second output bursts.
  size_t total_sent = 0;
  int retries = 0;
  const int max_retries = 100;  // 100 * ~2ms = ~200ms max wait

  // Lazy-resolve W5500 RX task handle (same pattern as ethernet_driver.cpp)
  if (!s_w5500_rx_task) {
    s_w5500_rx_task = xTaskGetHandle("w5500_tsk");
  }

  while (total_sent < len && retries < max_retries) {
    int sent = send(client->socket, (const char*)(data + total_sent), len - total_sent, 0);
    if (sent > 0) {
      total_sent += sent;
      retries = 0;  // Reset retry counter on progress
    } else if (sent < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Buffer full — kick W5500 RX task to process incoming TCP ACKs
        if (s_w5500_rx_task) {
          if (gpio_get_level((gpio_num_t)PIN_W5500_INT) == 0) {
            xTaskNotifyGive(s_w5500_rx_task);
          }
        }
        vTaskDelay(1);  // Yield 1 tick (~1ms) — non-blocking
        retries++;
      } else {
        ESP_LOGE(TAG, "Send error (errno: %d)", errno);
        tcp_server_disconnect_client(server, client_index);
        return -1;
      }
    } else {
      // sent == 0: no data sent, treat as EAGAIN
      if (s_w5500_rx_task) {
        if (gpio_get_level((gpio_num_t)PIN_W5500_INT) == 0) {
          xTaskNotifyGive(s_w5500_rx_task);
        }
      }
      vTaskDelay(1);
      retries++;
    }
  }

  if (total_sent > 0) {
    client->last_activity_ms = millis();
  }

  return (int)total_sent;
}

int tcp_server_sendf(TcpServer *server, uint8_t client_index, const char *format, ...)
{
  if (!server || !format) {
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

  return tcp_server_send(server, client_index, (uint8_t*)buf, len);
}

int tcp_server_recv(TcpServer *server, uint8_t client_index, uint8_t *buf, size_t max_len)
{
  if (!server || !buf || !max_len) {
    return -1;
  }

  TcpClient *client = tcp_server_get_client(server, client_index);
  if (!client) {
    return -1;
  }

  int received = recv(client->socket, (char*)buf, max_len, 0);
  if (received < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return 0;  // No data available
    } else {
      ESP_LOGE(TAG, "Recv error (errno: %d)", errno);
      tcp_server_disconnect_client(server, client_index);
      return -1;
    }
  } else if (received == 0) {
    // Connection closed by client
    tcp_server_disconnect_client(server, client_index);
    return -1;
  }

  client->last_activity_ms = millis();
  return received;
}

int tcp_server_recv_byte(TcpServer *server, uint8_t client_index, uint8_t *out_byte)
{
  if (!out_byte) {
    return -1;
  }

  return tcp_server_recv(server, client_index, out_byte, 1);
}

/* ============================================================================
 * STATUS & INFORMATION
 * ============================================================================ */

uint8_t tcp_server_is_listening(const TcpServer *server)
{
  return server && server->active;
}

uint8_t tcp_server_client_is_connected(const TcpServer *server, uint8_t client_index)
{
  if (!server || client_index >= 1) {
    return 0;
  }

  return server->clients[client_index].connected && server->clients[client_index].socket >= 0;
}

uint8_t tcp_server_get_client_count(const TcpServer *server)
{
  return server ? server->client_count : 0;
}

uint16_t tcp_server_available(TcpServer *server, uint8_t client_index)
{
  if (!server) {
    return 0;
  }

  TcpClient *client = tcp_server_get_client(server, client_index);
  if (!client) {
    return 0;
  }

  // For non-blocking sockets, we'd need to peek or use select
  // For now, return simple estimate
  return 256;  // Optimistic: assume data might be available
}

/* ============================================================================
 * BACKGROUND TASKS
 * ============================================================================ */

int tcp_server_loop(TcpServer *server)
{
  if (!server || !server->active) {
    return 0;
  }

  int events = 0;

  // Accept new connections
  events += tcp_server_accept(server);

  // Check for dead connections (socket closed by client)
  for (int i = 0; i < 1; i++) {
    if (server->clients[i].connected && server->clients[i].socket >= 0) {
      // Try to peek at socket to detect if connection is dead
      uint8_t peek_byte;
      int peek_result = recv(server->clients[i].socket, &peek_byte, 1, MSG_PEEK | MSG_DONTWAIT);

      // If recv returns 0, the connection is closed
      if (peek_result == 0) {
        ESP_LOGI(TAG, "Client %d disconnected (socket closed)", i);
        tcp_server_disconnect_client(server, i);
        events++;
      }
      // If recv returns -1 and errno is not EAGAIN/EWOULDBLOCK, there's an error
      else if (peek_result < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        ESP_LOGW(TAG, "Client %d socket error (errno: %d)", i, errno);
        tcp_server_disconnect_client(server, i);
        events++;
      }
    }
  }

  // Check for client timeouts (only if timeout is enabled)
  if (TELNET_READ_TIMEOUT_MS > 0) {
    for (int i = 0; i < 1; i++) {
      if (server->clients[i].connected) {
        uint32_t idle_ms = millis() - server->clients[i].last_activity_ms;
        if (idle_ms > TELNET_READ_TIMEOUT_MS) {
          ESP_LOGW(TAG, "Client %d timeout (idle %lu ms)", i, idle_ms);
          tcp_server_disconnect_client(server, i);
          events++;
        }
      }
    }
  }

  return events;
}

/* ============================================================================
 * DEBUGGING
 * ============================================================================ */

void tcp_server_print_status(const TcpServer *server)
{
  if (!server) {
    printf("TCP Server: NULL\n");
    return;
  }

  printf("\n=== TCP Server Status ===\n");
  printf("Port: %d\n", server->listen_port);
  printf("Listening: %s\n", server->active ? "Yes" : "No");
  printf("Clients: %d\n", server->client_count);

  if (server->client_count > 0) {
    for (int i = 0; i < 1; i++) {
      if (server->clients[i].connected) {
        struct in_addr addr;
        addr.s_addr = server->clients[i].client_ip;
        printf("  Client %d: %s:%d (idle %lu ms)\n",
               i,
               inet_ntoa(addr),
               server->clients[i].client_port,
               millis() - server->clients[i].last_activity_ms);
      }
    }
  }

  printf("========================\n\n");
}
