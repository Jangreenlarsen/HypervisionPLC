// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file modbus_tx.h
 * @brief Modbus TX handler - Serial transmission with RS485 control (LAYER 3)
 *
 * LAYER 3: Modbus Server Runtime - TX Handling
 * Responsibility: Transmit Modbus frames via UART with RS485 DIR control
 *
 * This file handles:
 * - RS485 DIR control (GPIO15)
 * - Frame transmission
 * - TX completion detection
 *
 * Does NOT handle:
 * - RX operations (→ modbus_rx.h)
 * - Frame building (→ modbus_serializer.h)
 */

#ifndef modbus_tx_H
#define modbus_tx_H

#include <stdint.h>
#include <stdbool.h>
#include "modbus_frame.h"

/* ============================================================================
 * MODBUS TX FUNCTIONS
 * ============================================================================ */

/**
 * @brief Initialize Modbus TX (RS485 DIR control)
 */
void modbus_tx_init(void);

/**
 * @brief Transmit Modbus frame
 * @param frame Frame to transmit
 * @return true if transmitted successfully, false otherwise
 */
bool modbus_tx_send_frame(const ModbusFrame* frame);

/**
 * @brief Check if TX is in progress
 * @return true if TX in progress, false otherwise
 */
bool modbus_tx_is_busy(void);

#endif // modbus_tx_H
