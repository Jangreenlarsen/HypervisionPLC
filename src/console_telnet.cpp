/**
 * @file console_telnet.cpp
 * @brief Telnet console implementation
 *
 * Implements Console interface for Telnet server
 */

#include "console_telnet.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * TELNET CONSOLE CONTEXT
 * ============================================================================ */

typedef struct {
  TelnetServer *telnet_server;
  uint8_t input_buffer_pos;      // Position for partial character reads
  char last_char;                 // For multi-byte read handling
  uint8_t has_buffered_char;      // 1 if last_char contains unread data
} TelnetConsoleContext;

/* ============================================================================
 * I/O FUNCTION IMPLEMENTATIONS
 * ============================================================================ */

static int telnet_read_char(void *ctx, char *out_char) {
  if (!ctx || !out_char) return -1;

  TelnetConsoleContext *tctx = (TelnetConsoleContext*)ctx;

  // Check if we have a buffered character from previous read
  if (tctx->has_buffered_char) {
    *out_char = tctx->last_char;
    tctx->has_buffered_char = 0;
    return 1;
  }

  // Try to read from Telnet server (byte-by-byte)
  uint8_t byte;
  if (tcp_server_recv_byte(tctx->telnet_server->tcp_server, 0, &byte) > 0) {
    *out_char = (char)byte;
    return 1;
  }

  return 0;  // No input available
}

static int telnet_write_char(void *ctx, char ch) {
  if (!ctx) return -1;

  TelnetConsoleContext *tctx = (TelnetConsoleContext*)ctx;
  return telnet_server_writech(tctx->telnet_server, ch);
}

static int telnet_write_str(void *ctx, const char *str) {
  if (!ctx || !str) return -1;

  TelnetConsoleContext *tctx = (TelnetConsoleContext*)ctx;
  return telnet_server_write(tctx->telnet_server, str);
}

static int telnet_write_line(void *ctx, const char *str) {
  if (!ctx || !str) return -1;

  TelnetConsoleContext *tctx = (TelnetConsoleContext*)ctx;
  return telnet_server_writeline(tctx->telnet_server, str);
}

static int telnet_write_fmt(void *ctx, const char *fmt, ...) {
  if (!ctx || !fmt) return -1;

  TelnetConsoleContext *tctx = (TelnetConsoleContext*)ctx;

  char buffer[256];
  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  if (len < 0 || len >= (int)sizeof(buffer)) {
    return -1;
  }

  return telnet_server_write(tctx->telnet_server, buffer);
}

static int telnet_has_input(void *ctx) {
  if (!ctx) return 0;

  TelnetConsoleContext *tctx = (TelnetConsoleContext*)ctx;

  if (tctx->has_buffered_char) {
    return 1;
  }

  return telnet_server_available(tctx->telnet_server) > 0 ? 1 : 0;
}

static int telnet_is_connected(void *ctx) {
  if (!ctx) return 0;

  TelnetConsoleContext *tctx = (TelnetConsoleContext*)ctx;
  return telnet_server_client_connected(tctx->telnet_server) ? 1 : 0;
}

static int telnet_flush(void *ctx) {
  // Telnet doesn't need explicit flushing (TCP handles it)
  return 0;
}

/* ============================================================================
 * CONSOLE CREATION/DESTRUCTION
 * ============================================================================ */

TelnetServer* console_telnet_get_server(Console *console) {
  if (!console || !console->context || console->write_line != telnet_write_line) {
    return NULL;
  }
  return ((TelnetConsoleContext*)console->context)->telnet_server;
}

Console* console_telnet_create(TelnetServer *telnet_server) {
  if (!telnet_server) {
    return NULL;
  }

  // Allocate context
  TelnetConsoleContext *ctx = (TelnetConsoleContext*)malloc(sizeof(TelnetConsoleContext));
  if (!ctx) {
    return NULL;
  }

  ctx->telnet_server = telnet_server;
  ctx->input_buffer_pos = 0;
  ctx->has_buffered_char = 0;

  // Allocate Console instance
  Console *console = (Console*)malloc(sizeof(Console));
  if (!console) {
    free(ctx);
    return NULL;
  }

  // Initialize Console struct
  memset(console, 0, sizeof(Console));
  console->context = ctx;
  console->echo_enabled = 1;  // Telnet echo controlled by server

  // Assign function pointers
  console->read_char = telnet_read_char;
  console->write_char = telnet_write_char;
  console->write_str = telnet_write_str;
  console->write_line = telnet_write_line;
  console->write_fmt = telnet_write_fmt;
  console->has_input = telnet_has_input;
  console->is_connected = telnet_is_connected;
  console->flush = telnet_flush;

  return console;
}

void console_telnet_destroy(Console *console) {
  if (!console) return;

  if (console->context) {
    free(console->context);
  }

  free(console);
}
