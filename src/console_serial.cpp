// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file console_serial.cpp
 * @brief Serial console implementation (USB debug port)
 *
 * Implements Console interface for Arduino Serial (UART0)
 */

#include "console_serial.h"
#include "uart_driver.h"  // BUG-424
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * SERIAL CONSOLE CONTEXT
 * ============================================================================ */

typedef struct {
  uint8_t initialized;
} SerialConsoleContext;

/* ============================================================================
 * I/O FUNCTION IMPLEMENTATIONS
 * ============================================================================ */

static int serial_read_char(void *ctx, char *out_char) {
  if (!out_char) return -1;
  if (uart_rs485_owns_usb_pins()) return 0;  // BUG-424

  if (Serial.available() > 0) {
    int ch = Serial.read();
    if (ch >= 0) {
      *out_char = (char)ch;
      return 1;
    }
  }

  return 0;  // No input available
}

static int serial_write_char(void *ctx, char ch) {
  if (uart_rs485_owns_usb_pins()) return 1;  // BUG-424
  Serial.write(ch);
  return 1;
}

static int serial_write_str(void *ctx, const char *str) {
  if (!str) return -1;
  if (uart_rs485_owns_usb_pins()) return strlen(str);  // BUG-424
  Serial.print(str);
  return strlen(str);
}

static int serial_write_line(void *ctx, const char *str) {
  if (!str) return -1;
  if (uart_rs485_owns_usb_pins()) return strlen(str) + 2;  // BUG-424
  Serial.println(str);
  return strlen(str) + 2;  // +2 for \r\n
}

static int serial_write_fmt(void *ctx, const char *fmt, ...) {
  if (!fmt) return -1;
  if (uart_rs485_owns_usb_pins()) return 0;  // BUG-424

  char buffer[256];
  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  if (len < 0 || len >= (int)sizeof(buffer)) {
    return -1;
  }

  Serial.print(buffer);
  return len;
}

static int serial_has_input(void *ctx) {
  if (uart_rs485_owns_usb_pins()) return 0;  // BUG-424
  return Serial.available() > 0 ? 1 : 0;
}

static int serial_is_connected(void *ctx) {
  // Serial is always "connected" (USB debug port)
  return 1;
}

static int serial_flush(void *ctx) {
  if (uart_rs485_owns_usb_pins()) return 0;  // BUG-424
  Serial.flush();
  return 0;
}

/* ============================================================================
 * CONSOLE CREATION/DESTRUCTION
 * ============================================================================ */

Console* console_serial_create(void) {
  // Allocate context
  SerialConsoleContext *ctx = (SerialConsoleContext*)malloc(sizeof(SerialConsoleContext));
  if (!ctx) {
    return NULL;
  }

  ctx->initialized = 1;

  // Allocate Console instance
  Console *console = (Console*)malloc(sizeof(Console));
  if (!console) {
    free(ctx);
    return NULL;
  }

  // Initialize Console struct
  memset(console, 0, sizeof(Console));
  console->context = ctx;
  console->echo_enabled = 1;  // Serial always echoes

  // Assign function pointers
  console->read_char = serial_read_char;
  console->write_char = serial_write_char;
  console->write_str = serial_write_str;
  console->write_line = serial_write_line;
  console->write_fmt = serial_write_fmt;
  console->has_input = serial_has_input;
  console->is_connected = serial_is_connected;
  console->flush = serial_flush;

  return console;
}

void console_serial_destroy(Console *console) {
  if (!console) return;

  if (console->context) {
    free(console->context);
  }

  free(console);
}
