// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file cli_remote.h
 * @brief Unified CLI interface for serial and remote (Telnet) connections
 *
 * Provides a single CLI interface that works identically over:
 * - Serial console (via UART0, always available)
 * - Telnet (over network, when connected)
 *
 * This layer abstracts away the difference between serial and Telnet,
 * so CLI commands can be implemented once and work on both interfaces.
 */

#ifndef CLI_REMOTE_H
#define CLI_REMOTE_H

#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * CHANNEL TYPES
 * ============================================================================ */

typedef enum {
  CLI_CHANNEL_SERIAL = 0,       // Serial console (always available)
  CLI_CHANNEL_TELNET = 1,       // Telnet remote (if connected)
  CLI_CHANNEL_COUNT = 2
} CliChannelType;

/* ============================================================================
 * INPUT/OUTPUT
 * ============================================================================ */

/**
 * Initialize CLI remote system
 * @return 0 on success, -1 on error
 */
int cli_remote_init(void);

/**
 * Get complete line from any connected CLI channel (serial or telnet)
 * @param buf Output buffer for command line
 * @param max_len Maximum buffer size
 * @param out_channel Output: which channel provided the line (serial=0, telnet=1)
 * @return line length, 0 if no complete line, -1 on error
 */
int cli_remote_readline(char *buf, size_t max_len, uint8_t *out_channel);

/**
 * Send line to CLI channel(s)
 * @param channel Which channel to send to (0=serial, 1=telnet, 2=both)
 * @param line Text line
 * @return bytes sent to first active channel, -1 if no active channels
 */
int cli_remote_writeline(uint8_t channel, const char *line);

/**
 * Send formatted line (printf-style)
 * @param channel Which channel to send to
 * @param format Format string
 * @return bytes sent, -1 on error
 */
int cli_remote_writelinef(uint8_t channel, const char *format, ...);

/**
 * Send raw text (no line ending)
 * @param channel Which channel to send to
 * @param text Text to send
 * @return bytes sent, -1 on error
 */
int cli_remote_write(uint8_t channel, const char *text);

/**
 * Send formatted raw text
 * @param channel Which channel to send to
 * @param format Format string
 * @return bytes sent, -1 on error
 */
int cli_remote_writef(uint8_t channel, const char *format, ...);

/**
 * Send single character
 * @param channel Which channel to send to
 * @param ch Character
 * @return 1 on success, -1 on error
 */
int cli_remote_writech(uint8_t channel, char ch);

/* ============================================================================
 * STATUS & INFORMATION
 * ============================================================================ */

/**
 * Check if CLI channel has input ready
 * @param channel Channel to check (or 2 for "any")
 * @return 1 if input available, 0 otherwise
 */
uint8_t cli_remote_has_input(uint8_t channel);

/**
 * Check if channel is active/connected
 * @param channel Channel to check
 * @return 1 if channel active, 0 otherwise
 */
uint8_t cli_remote_is_channel_active(uint8_t channel);

/**
 * Get count of active channels
 * @return number of active CLI channels (1-2)
 */
uint8_t cli_remote_get_active_channels(void);

/**
 * Get pending bytes on channel
 * @param channel Channel to check
 * @return number of bytes pending
 */
uint16_t cli_remote_available(uint8_t channel);

/* ============================================================================
 * BACKGROUND TASKS
 * ============================================================================ */

/**
 * Main loop (should be called periodically)
 * @return number of events processed
 */
int cli_remote_loop(void);

/* ============================================================================
 * CHANNEL CONSTANTS
 * ============================================================================ */

#define CLI_CHANNEL_SERIAL_ONLY     0       // Send to serial only
#define CLI_CHANNEL_TELNET_ONLY     1       // Send to telnet only
#define CLI_CHANNEL_ALL             2       // Send to all active channels

#endif // CLI_REMOTE_H
