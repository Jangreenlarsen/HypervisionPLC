// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file console.h
 * @brief Console abstraction layer - unified interface for Serial/Telnet CLI
 *
 * LAYER 7: User Interface - Console Abstraction
 *
 * This provides a unified console interface that works identically for:
 * - Serial console (USB debug port)
 * - Telnet console (network)
 * - Future: WebSocket, BLE, etc.
 *
 * The Console abstraction allows CLI shell code to be completely hardware-agnostic.
 * All input/output operations go through function pointers, enabling transparent
 * switching between different I/O backends.
 *
 * Design Philosophy:
 * - CLI shell doesn't know or care what backend it's using
 * - Each backend implements the same interface
 * - No global state - context passed explicitly
 * - Clean separation of concerns
 */

#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * CONSOLE INTERFACE
 * ============================================================================ */

/**
 * Console interface - function pointers for I/O operations
 *
 * Each console backend (Serial, Telnet, etc.) implements these functions.
 * The CLI shell calls these functions through a Console pointer.
 */
typedef struct Console {
  /* Configuration */
  void *context;                // Backend-specific context (SerialConsole*, TelnetConsole*, etc.)
  uint8_t echo_enabled;         // 1 = echo input back to user, 0 = silent
  uint8_t close_requested;      // 1 = "exit" command issued, backend should close connection

  /* I/O Functions */

  /**
   * Read one character from console (non-blocking)
   * @param ctx Console context
   * @param out_char Pointer to store character (if available)
   * @return 1 if character read, 0 if no input, -1 on error
   */
  int (*read_char)(void *ctx, char *out_char);

  /**
   * Write one character to console
   * @param ctx Console context
   * @param ch Character to write
   * @return 1 on success, -1 on error
   */
  int (*write_char)(void *ctx, char ch);

  /**
   * Write string to console (without newline)
   * @param ctx Console context
   * @param str String to write
   * @return bytes written, -1 on error
   */
  int (*write_str)(void *ctx, const char *str);

  /**
   * Write string to console (with newline)
   * @param ctx Console context
   * @param str String to write
   * @return bytes written, -1 on error
   */
  int (*write_line)(void *ctx, const char *str);

  /**
   * Write formatted string (printf-style)
   * @param ctx Console context
   * @param fmt Format string
   * @param ... Variable arguments
   * @return bytes written, -1 on error
   */
  int (*write_fmt)(void *ctx, const char *fmt, ...);

  /**
   * Check if console has input available
   * @param ctx Console context
   * @return 1 if input available, 0 otherwise
   */
  int (*has_input)(void *ctx);

  /**
   * Check if console is still connected
   * @param ctx Console context
   * @return 1 if connected, 0 otherwise
   */
  int (*is_connected)(void *ctx);

  /**
   * Flush output buffer
   * @param ctx Console context
   * @return 0 on success, -1 on error
   */
  int (*flush)(void *ctx);

} Console;

/* ============================================================================
 * CONSOLE HELPER FUNCTIONS
 * ============================================================================ */

/**
 * Print string to console (without newline)
 * @param console Console instance
 * @param str String to print
 * @return bytes written, -1 on error
 */
static inline int console_print(Console *console, const char *str) {
  if (!console || !console->write_str) return -1;
  return console->write_str(console->context, str);
}

/**
 * Print string to console (with newline)
 * @param console Console instance
 * @param str String to print
 * @return bytes written, -1 on error
 */
static inline int console_println(Console *console, const char *str) {
  if (!console || !console->write_line) return -1;
  return console->write_line(console->context, str);
}

/**
 * Print character to console
 * @param console Console instance
 * @param ch Character to print
 * @return 1 on success, -1 on error
 */
static inline int console_putchar(Console *console, char ch) {
  if (!console || !console->write_char) return -1;
  return console->write_char(console->context, ch);
}

/**
 * Read character from console (non-blocking)
 * @param console Console instance
 * @param out_char Pointer to store character
 * @return 1 if character read, 0 if no input, -1 on error
 */
static inline int console_getchar(Console *console, char *out_char) {
  if (!console || !console->read_char) return -1;
  return console->read_char(console->context, out_char);
}

/**
 * Check if console has input
 * @param console Console instance
 * @return 1 if input available, 0 otherwise
 */
static inline int console_available(Console *console) {
  if (!console || !console->has_input) return 0;
  return console->has_input(console->context);
}

/**
 * Check if console is connected
 * @param console Console instance
 * @return 1 if connected, 0 otherwise
 */
static inline int console_connected(Console *console) {
  if (!console || !console->is_connected) return 0;
  return console->is_connected(console->context);
}

/**
 * Flush console output
 * @param console Console instance
 * @return 0 on success, -1 on error
 */
static inline int console_flush(Console *console) {
  if (!console || !console->flush) return -1;
  return console->flush(console->context);
}

#endif // CONSOLE_H
