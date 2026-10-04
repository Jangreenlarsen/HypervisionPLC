// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file modbus_server.h
 * @brief Modbus server main state machine (LAYER 3)
 *
 * LAYER 3: Modbus Server Runtime - Main State Machine
 * Responsibility: Orchestrate RX → Process → TX cycle
 *
 * This file handles:
 * - Main Modbus state machine (Idle → RX → Process → TX → Idle)
 * - Slave ID filtering
 * - Timeout handling
 * - Integration of RX, TX, and FC dispatch
 *
 * Does NOT handle:
 * - RX details (→ modbus_rx.h)
 * - TX details (→ modbus_tx.h)
 * - Function code implementation (→ modbus_fc_*.h)
 */

#ifndef modbus_server_H
#define modbus_server_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * MODBUS SERVER STATE
 * ============================================================================ */

typedef enum {
  MODBUS_STATE_IDLE,      // Idle, waiting for request
  MODBUS_STATE_RX,        // Receiving request
  MODBUS_STATE_PROCESS,   // Processing request
  MODBUS_STATE_TX,        // Transmitting response
  MODBUS_STATE_ERROR      // Error occurred
} modbus_server_state_t;

/* ============================================================================
 * MODBUS SERVER FUNCTIONS
 * ============================================================================ */

/**
 * @brief Initialize Modbus server
 * @param slave_id Modbus slave ID (1-247)
 */
void modbus_server_init(uint8_t slave_id);

/**
 * @brief Process Modbus server (call in main loop)
 */
void modbus_server_loop(void);

/**
 * @brief Get current server state
 * @return Current state
 */
modbus_server_state_t modbus_server_get_state(void);

/**
 * @brief Set Modbus slave ID
 * @param slave_id Slave ID (1-247)
 */
void modbus_server_set_slave_id(uint8_t slave_id);

/**
 * @brief Get Modbus slave ID
 * @return Current slave ID
 */
uint8_t modbus_server_get_slave_id(void);

#endif // modbus_server_H
