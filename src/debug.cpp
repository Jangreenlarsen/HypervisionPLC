// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file debug.cpp
 * @brief Debug output implementation - now uses Console abstraction
 *
 * REFACTORED: Removed fragile "context routing" system.
 * Now uses cli_shell_get_debug_console() to get current Console*.
 *
 * This is a MUCH cleaner architecture:
 * - No global callbacks
 * - No manual context switching
 * - Works automatically with any Console (Serial, Telnet, etc.)
 */

#include "debug.h"
#include "cli_shell.h"  // For cli_shell_get_debug_console()
#include <Arduino.h>
#include <stdio.h>
#include <stdarg.h>

/* ============================================================================
 * PUBLIC DEBUG FUNCTIONS
 * ============================================================================ */

void debug_println(const char* str) {
  if (!str) return;

  Console *console = cli_shell_get_debug_console();
  if (console && console->write_line) {
    console->write_line(console->context, str);
  }
}

void debug_print(const char* str) {
  if (!str) return;

  Console *console = cli_shell_get_debug_console();
  if (console && console->write_str) {
    console->write_str(console->context, str);
  }
}

void debug_print_uint(uint32_t value) {
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)value);

  Console *console = cli_shell_get_debug_console();
  if (console && console->write_str) {
    console->write_str(console->context, buffer);
  }
}

void debug_print_ulong(uint64_t value) {
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)value);

  Console *console = cli_shell_get_debug_console();
  if (console && console->write_str) {
    console->write_str(console->context, buffer);
  }
}

void debug_print_float(double value) {
  // Print with 2 decimal places
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%.2f", value);

  Console *console = cli_shell_get_debug_console();
  if (console && console->write_str) {
    console->write_str(console->context, buffer);
  }
}

void debug_newline(void) {
  Console *console = cli_shell_get_debug_console();
  if (console && console->write_line) {
    console->write_line(console->context, "");
  }
}

void debug_printf(const char* fmt, ...) {
  if (!fmt) return;

  char buffer[2048];  // Large buffer for source code, etc.
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  Console *console = cli_shell_get_debug_console();
  if (console && console->write_str) {
    console->write_str(console->context, buffer);
  }
}
