// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file modbus_tx.cpp
 * @brief Modbus TX handler implementation (LAYER 3)
 *
 * Transmits Modbus RTU frames via UART with RS485 DIR control.
 * RS485 DIR = HIGH during TX, LOW during RX.
 */

#include "modbus_tx.h"
#include "uart_driver.h"
#include "gpio_driver.h"
#include "constants.h"
#include "debug.h"
#include <Arduino.h>
#include "rs485_stats.h"

/* ============================================================================
 * STATIC STATE
 * ============================================================================ */

static bool tx_busy = false;

/* ============================================================================
 * MODBUS TX FUNCTIONS
 * ============================================================================ */

void modbus_tx_init(void) {
  // Initialize RS485 DIR control (GPIO15)
  gpio_set_direction(PIN_RS485_DIR, GPIO_OUTPUT);
  gpio_write(PIN_RS485_DIR, 0);  // RX mode by default
  tx_busy = false;
}

bool modbus_tx_send_frame(const ModbusFrame* frame) {
  if (frame == NULL || tx_busy) return false;

  tx_busy = true;

  // Enable RS485 TX mode (DIR = HIGH)
  gpio_write(PIN_RS485_DIR, 1);
  delayMicroseconds(10);  // Small delay for RS485 transceiver switching

  // Build raw frame bytes: [slave_id] [FC] [data...] [CRC_LO] [CRC_HI]
  uint8_t tx_buffer[MODBUS_FRAME_MAX];
  uint16_t tx_index = 0;

  tx_buffer[tx_index++] = frame->slave_id;
  tx_buffer[tx_index++] = frame->function_code;

  // Copy data (length includes ID, FC, data, CRC)
  uint16_t data_len = frame->length - 4;  // Total - ID - FC - CRC(2)
  for (uint16_t i = 0; i < data_len; i++) {
    tx_buffer[tx_index++] = frame->data[i];
  }

  // Append CRC (little-endian: CRC_LO, CRC_HI)
  tx_buffer[tx_index++] = frame->crc16 & 0xFF;
  tx_buffer[tx_index++] = (frame->crc16 >> 8) & 0xFF;

  // Transmit via UART1
  uart1_write_buffer(tx_buffer, tx_index);
  rs485_count_tx_frame(RS485_ROLE_SLAVE, tx_index);  // FEAT-450

  // Wait for TX to complete
  uart1_flush_tx();

  // Small delay for last byte to be transmitted
  delayMicroseconds(100);

  // Disable RS485 TX mode (DIR = LOW, back to RX)
  gpio_write(PIN_RS485_DIR, 0);

  tx_busy = false;

  return true;
}

bool modbus_tx_is_busy(void) {
  return tx_busy;
}

