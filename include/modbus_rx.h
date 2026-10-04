// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file modbus_rx.h
 * @brief Modbus RX handler - Serial reception with timeout (LAYER 3)
 *
 * LAYER 3: Modbus Server Runtime - RX Handling
 * Responsibility: Receive Modbus frames via UART with timeout detection
 *
 * This file handles:
 * - Non-blocking serial RX
 * - Inter-character timeout detection (3.5 char time)
 * - Frame assembly
 * - CRC validation
 *
 * Does NOT handle:
 * - TX operations (→ modbus_tx.h)
 * - Frame processing (→ modbus_fc_dispatch.h)
 */

#ifndef modbus_rx_H
#define modbus_rx_H

#include <stdint.h>
#include <stdbool.h>
#include "modbus_frame.h"

/* ============================================================================
 * MODBUS RX STATE
 * ============================================================================ */

typedef enum {
  MODBUS_RX_IDLE,       // Waiting for first byte
  MODBUS_RX_RECEIVING,  // Receiving frame bytes
  MODBUS_RX_COMPLETE,   // Frame complete (timeout detected)
  MODBUS_RX_ERROR       // Frame error (CRC mismatch, etc.)
} modbus_rx_state_t;

/* ============================================================================
 * MODBUS RX FUNCTIONS
 * ============================================================================ */

/**
 * @brief Initialize Modbus RX
 */
void modbus_rx_init(void);

/**
 * @brief Process Modbus RX (call in main loop)
 * @param frame Output frame (populated if complete)
 * @return Current RX state
 */
modbus_rx_state_t modbus_rx_process(ModbusFrame* frame);

/**
 * @brief Reset RX state to idle
 */
void modbus_rx_reset(void);

/**
 * @brief Get current RX state
 * @return Current RX state
 */
modbus_rx_state_t modbus_rx_get_state(void);

#endif // modbus_rx_H
