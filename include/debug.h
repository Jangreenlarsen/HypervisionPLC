// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file debug.h
 * @brief Debug output helpers - now uses Console abstraction
 *
 * REFACTORED: Removed fragile "context routing" system.
 * Now uses cli_shell_get_debug_console() to get current Console*.
 *
 * This is a MUCH cleaner architecture:
 * - No global callbacks
 * - No manual context switching
 * - Works automatically with any Console (Serial, Telnet, etc.)
 */

#ifndef debug_H
#define debug_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Print string followed by newline
 * @param str String to print (null-terminated)
 */
void debug_println(const char* str);

/**
 * @brief Print string without newline
 * @param str String to print (null-terminated)
 */
void debug_print(const char* str);

/**
 * @brief Print unsigned integer (decimal)
 * @param value Integer value to print
 */
void debug_print_uint(uint32_t value);

/**
 * @brief Print unsigned long (decimal)
 * @param value Long integer value to print
 */
void debug_print_ulong(uint64_t value);

/**
 * @brief Print double/float (with 2 decimal places)
 * @param value Float value to print
 */
void debug_print_float(double value);

/**
 * @brief Print newline character
 */
void debug_newline(void);

/**
 * @brief Print formatted string (like printf)
 * @param fmt Format string
 * @param ... Variable arguments
 */
void debug_printf(const char* fmt, ...);

#ifdef __cplusplus
}
#endif

#endif // debug_H
