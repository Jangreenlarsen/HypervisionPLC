// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file modbus_rx.cpp
 * @brief Modbus RX handler implementation (LAYER 3)
 *
 * Receives Modbus RTU frames via UART with timeout detection.
 * Inter-character timeout = 3.5 character times (MODBUS_TIMEOUT_MS).
 */

#include "modbus_rx.h"
#include "uart_driver.h"
#include "constants.h"
#include "debug.h"
#include <Arduino.h>
#include <string.h>
#include "rs485_stats.h"

/* ============================================================================
 * STATIC STATE
 * ============================================================================ */

static modbus_rx_state_t rx_state = MODBUS_RX_IDLE;
static uint8_t rx_buffer[MODBUS_FRAME_MAX];
static uint16_t rx_index = 0;
static uint32_t last_rx_time = 0;

/* ============================================================================
 * MODBUS RX FUNCTIONS
 * ============================================================================ */

void modbus_rx_init(void) {
  rx_state = MODBUS_RX_IDLE;
  rx_index = 0;
  last_rx_time = 0;
  memset(rx_buffer, 0, sizeof(rx_buffer));
}

modbus_rx_state_t modbus_rx_process(ModbusFrame* frame) {
  if (frame == NULL) return MODBUS_RX_ERROR;

  uint32_t current_time = millis();

  switch (rx_state) {
    case MODBUS_RX_IDLE:
      // Wait for first byte
      if (uart1_available() > 0) {
        int byte = uart1_read();
        if (byte >= 0) {
          rx_buffer[0] = (uint8_t)byte;
          rx_index = 1;
          rs485_count_rx_bytes(RS485_ROLE_SLAVE, 1);  // FEAT-450
          last_rx_time = current_time;
          rx_state = MODBUS_RX_RECEIVING;
        }
      }
      break;

    case MODBUS_RX_RECEIVING:
      // Receive subsequent bytes
      while (uart1_available() > 0 && rx_index < MODBUS_FRAME_MAX) {
        int byte = uart1_read();
        if (byte >= 0) {
          rx_buffer[rx_index++] = (uint8_t)byte;
          last_rx_time = current_time;
          rs485_count_rx_bytes(RS485_ROLE_SLAVE, 1);  // FEAT-450
        }
      }

      // Check for timeout (3.5 character times)
      if ((current_time - last_rx_time) >= MODBUS_TIMEOUT_MS) {
        // Timeout detected - frame complete
        rs485_count_rx_frame(RS485_ROLE_SLAVE);  // FEAT-450
        if (rx_index >= 5) {  // Minimum: slave_id + FC + data (1 byte) + CRC (2)
          // Parse frame
          frame->slave_id = rx_buffer[0];
          frame->function_code = rx_buffer[1];
          frame->length = rx_index;

          // Extract CRC (last 2 bytes, little-endian)
          uint16_t crc_lo = rx_buffer[rx_index - 2];
          uint16_t crc_hi = rx_buffer[rx_index - 1];
          frame->crc16 = (crc_hi << 8) | crc_lo;

          // Copy data (excluding slave_id, FC, CRC)
          uint16_t data_len = rx_index - 4;  // Total - ID - FC - CRC(2)
          memcpy(frame->data, &rx_buffer[2], data_len);

          // Validate frame
          if (modbus_frame_is_valid(frame)) {
            rx_state = MODBUS_RX_COMPLETE;
          } else {
            debug_println("ERROR: Invalid Modbus frame (CRC mismatch)");
            rx_state = MODBUS_RX_ERROR;
          }
        } else {
          debug_println("ERROR: Modbus frame too short");
          rx_state = MODBUS_RX_ERROR;
        }
      }
      break;

    case MODBUS_RX_COMPLETE:
    case MODBUS_RX_ERROR:
      // Do nothing - wait for reset
      break;
  }

  return rx_state;
}

void modbus_rx_reset(void) {
  rx_state = MODBUS_RX_IDLE;
  rx_index = 0;
  last_rx_time = 0;
  memset(rx_buffer, 0, sizeof(rx_buffer));
}

modbus_rx_state_t modbus_rx_get_state(void) {
  return rx_state;
}

