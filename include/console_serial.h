// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file console_serial.h
 * @brief Serial console implementation (USB debug port)
 *
 * Implements Console interface for Arduino Serial (UART0)
 */

#ifndef CONSOLE_SERIAL_H
#define CONSOLE_SERIAL_H

#include "console.h"

/* ============================================================================
 * SERIAL CONSOLE INITIALIZATION
 * ============================================================================ */

/**
 * Create Serial console instance
 * @return Console instance, NULL on error
 *
 * NOTE: This assumes Serial.begin() has already been called in setup()
 */
Console* console_serial_create(void);

/**
 * Destroy Serial console instance
 * @param console Console instance to destroy
 */
void console_serial_destroy(Console *console);

#endif // CONSOLE_SERIAL_H
