// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file cli_remote.cpp
 * @brief Unified CLI interface implementation (serial + telnet)
 *
 * Routes CLI input/output to serial console and/or Telnet client.
 * Abstracts the difference between the two channels.
 */

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <Arduino.h>
#include <esp_log.h>

#include "cli_remote.h"
#include "network_manager.h"

static const char *TAG = "CLI_REM";

/* ============================================================================
 * INTERNAL STATE
 * ============================================================================ */

typedef struct {
  uint8_t active;                       // 1 if channel active
  uint32_t last_activity_ms;
} CliChannelState;

static struct {
  uint8_t initialized;
  CliChannelState channels[CLI_CHANNEL_COUNT];
} cli_state = {
  .initialized = 0,
  .channels = {
    {.active = 1, .last_activity_ms = 0},  // Serial always active
    {.active = 0, .last_activity_ms = 0}   // Telnet (checked in loop)
  }
};

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

int cli_remote_init(void)
{
  if (cli_state.initialized) {
    return 0;
  }

  // Serial is always available
  cli_state.channels[CLI_CHANNEL_SERIAL].active = 1;
  cli_state.channels[CLI_CHANNEL_TELNET].active = 0;

  cli_state.initialized = 1;
  ESP_LOGI(TAG, "CLI remote initialized");

  return 0;
}

/* ============================================================================
 * INPUT/OUTPUT
 * ============================================================================ */

int cli_remote_readline(char *buf, size_t max_len, uint8_t *out_channel)
{
  if (!buf || !max_len || !out_channel) {
    return -1;
  }

  // Try Telnet first (if connected)
  if (cli_state.channels[CLI_CHANNEL_TELNET].active && network_manager_is_telnet_connected()) {
    int len = network_manager_telnet_readline(buf, max_len);
    if (len > 0) {
      *out_channel = CLI_CHANNEL_TELNET;
      cli_state.channels[CLI_CHANNEL_TELNET].last_activity_ms = millis();
      return len;
    }
  }

  // Try serial
  if (cli_state.channels[CLI_CHANNEL_SERIAL].active) {
    // Serial.available() returns number of bytes waiting
    if (Serial.available()) {
      // Read until newline or buffer full
      int pos = 0;
      while (pos < (int)max_len - 1 && Serial.available()) {
        int ch = Serial.read();
        if (ch < 0) break;

        if (ch == '\r') {
          // Skip carriage returns
          continue;
        } else if (ch == '\n') {
          // Line complete
          buf[pos] = '\0';
          *out_channel = CLI_CHANNEL_SERIAL;
          cli_state.channels[CLI_CHANNEL_SERIAL].last_activity_ms = millis();
          return pos;
        } else if (ch >= 32 && ch < 127) {
          // Printable character
          buf[pos++] = ch;
          Serial.write(ch);  // Echo back
        } else if (ch == 8 || ch == 127) {
          // Backspace
          if (pos > 0) {
            pos--;
            Serial.write("\b \b");  // Erase character
          }
        }
      }
    }
  }

  return 0;  // No complete line
}

int cli_remote_writeline(uint8_t channel, const char *line)
{
  if (!line) {
    return -1;
  }

  int total_sent = 0;

  if (channel == CLI_CHANNEL_ALL || channel == CLI_CHANNEL_SERIAL) {
    if (cli_state.channels[CLI_CHANNEL_SERIAL].active) {
      Serial.println(line);
      total_sent += strlen(line) + 2;  // +2 for \r\n
    }
  }

  if (channel == CLI_CHANNEL_ALL || channel == CLI_CHANNEL_TELNET) {
    if (cli_state.channels[CLI_CHANNEL_TELNET].active && network_manager_is_telnet_connected()) {
      int sent = network_manager_telnet_writeline(line);
      if (sent > 0) {
        total_sent = sent;
      }
    }
  }

  return total_sent > 0 ? total_sent : -1;
}

int cli_remote_writelinef(uint8_t channel, const char *format, ...)
{
  if (!format) {
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

  return cli_remote_writeline(channel, buf);
}

int cli_remote_write(uint8_t channel, const char *text)
{
  if (!text) {
    return -1;
  }

  int total_sent = 0;

  if (channel == CLI_CHANNEL_ALL || channel == CLI_CHANNEL_SERIAL) {
    if (cli_state.channels[CLI_CHANNEL_SERIAL].active) {
      Serial.print(text);
      total_sent += strlen(text);
    }
  }

  if (channel == CLI_CHANNEL_ALL || channel == CLI_CHANNEL_TELNET) {
    if (cli_state.channels[CLI_CHANNEL_TELNET].active && network_manager_is_telnet_connected()) {
      int sent = network_manager_telnet_write(text);
      if (sent > 0) {
        total_sent = sent;
      }
    }
  }

  return total_sent > 0 ? total_sent : -1;
}

int cli_remote_writef(uint8_t channel, const char *format, ...)
{
  if (!format) {
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

  return cli_remote_write(channel, buf);
}

int cli_remote_writech(uint8_t channel, char ch)
{
  int total_sent = 0;

  if (channel == CLI_CHANNEL_ALL || channel == CLI_CHANNEL_SERIAL) {
    if (cli_state.channels[CLI_CHANNEL_SERIAL].active) {
      Serial.write(ch);
      total_sent++;
    }
  }

  if (channel == CLI_CHANNEL_ALL || channel == CLI_CHANNEL_TELNET) {
    if (cli_state.channels[CLI_CHANNEL_TELNET].active && network_manager_is_telnet_connected()) {
      network_manager_telnet_write(&ch);
      total_sent++;
    }
  }

  return total_sent > 0 ? total_sent : -1;
}

/* ============================================================================
 * STATUS & INFORMATION
 * ============================================================================ */

uint8_t cli_remote_has_input(uint8_t channel)
{
  if (channel == CLI_CHANNEL_SERIAL) {
    return Serial.available() > 0;
  } else if (channel == CLI_CHANNEL_TELNET) {
    return network_manager_is_telnet_connected() && network_manager_telnet_has_input();
  } else if (channel == CLI_CHANNEL_ALL) {
    // "Any" channel
    return (Serial.available() > 0) ||
           (network_manager_is_telnet_connected() && network_manager_telnet_has_input());
  }

  return 0;
}

uint8_t cli_remote_is_channel_active(uint8_t channel)
{
  if (channel >= CLI_CHANNEL_COUNT) {
    return 0;
  }

  if (channel == CLI_CHANNEL_SERIAL) {
    return 1;  // Always active
  } else if (channel == CLI_CHANNEL_TELNET) {
    return network_manager_is_telnet_connected();
  }

  return 0;
}

uint8_t cli_remote_get_active_channels(void)
{
  uint8_t count = 1;  // Serial always

  if (network_manager_is_telnet_connected()) {
    count++;
  }

  return count;
}

uint16_t cli_remote_available(uint8_t channel)
{
  if (channel == CLI_CHANNEL_SERIAL) {
    return Serial.available();
  } else if (channel == CLI_CHANNEL_TELNET) {
    if (network_manager_is_telnet_connected()) {
      return 256;  // Estimate
    }
  }

  return 0;
}

/* ============================================================================
 * BACKGROUND TASKS
 * ============================================================================ */

int cli_remote_loop(void)
{
  // Update Telnet channel state
  cli_state.channels[CLI_CHANNEL_TELNET].active = network_manager_is_telnet_connected();

  return 0;
}
