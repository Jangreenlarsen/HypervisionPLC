// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file modbus_frame.cpp
 * @brief Modbus frame implementation - CRC16 and validation (LAYER 1)
 *
 * CRC16-MODBUS (also called CRC16-CCITT-FALSE):
 * - Polynomial: 0x8005 (bit-reversed: 0xA001)
 * - Initial value: 0xFFFF
 * - Final XOR: 0x0000
 * - Reflected input: YES
 * - Reflected output: YES
 */

#include "modbus_frame.h"
#include "debug.h"
#include <string.h>

/* ============================================================================
 * CRC16-MODBUS CALCULATION
 * ============================================================================ */

/**
 * @brief CRC16 lookup table (pre-computed for performance)
 * Generated using polynomial 0xA001 (reflected 0x8005)
 */
static const uint16_t crc16_table[256] = {
  0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401,
  0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400,
  0xC801, 0x0400, 0x1000, 0xDC01, 0x1800, 0xD401, 0xC001, 0x0C00,
  0x8801, 0x4400, 0x5000, 0x9C01, 0xB000, 0x7C01, 0x6800, 0xA401,
  0x2800, 0xE401, 0xF001, 0x3C00, 0xD801, 0x1400, 0x0000, 0xCC01,
  0x6800, 0xA401, 0xB001, 0x7C00, 0x5800, 0x9401, 0x8001, 0x4C00,
  0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400,
  0x0800, 0xC401, 0xD001, 0x1C00, 0xF800, 0x3401, 0x2000, 0xEC01,
  0xD801, 0x1400, 0x0000, 0xCC01, 0x0800, 0xC401, 0xD001, 0x1C00,
  0x5800, 0x9401, 0x8001, 0x4C00, 0xA001, 0x6C00, 0x7800, 0xB401,
  0xB001, 0x7C00, 0x6800, 0xA401, 0x6000, 0xAC01, 0xB801, 0x7400,
  0x1000, 0xDC01, 0xC801, 0x0400, 0xF001, 0x3C00, 0x2800, 0xE401,
  0xF801, 0x3400, 0x2000, 0xEC01, 0x2800, 0xE401, 0xF001, 0x3C00,
  0x7800, 0xB401, 0xA001, 0x6C00, 0xA801, 0x6400, 0x7000, 0xBC01,
  0x5000, 0x9C01, 0x8801, 0x4400, 0xC001, 0x0C00, 0x1800, 0xD401,
  0x1800, 0xD401, 0xC001, 0x0C00, 0xEC01, 0x2400, 0x3000, 0xFC00,
  0x6800, 0xA401, 0xB001, 0x7C00, 0x5800, 0x9401, 0x8001, 0x4C00,
  0x4400, 0x8801, 0x9C01, 0x5000, 0xD401, 0x1800, 0x0C00, 0xC001,
  0x7000, 0xBC01, 0xA801, 0x6400, 0xA001, 0x6C00, 0x7800, 0xB401,
  0x0800, 0xC401, 0xD001, 0x1C00, 0xF800, 0x3401, 0x2000, 0xEC01,
  0x8801, 0x4400, 0x5000, 0x9C01, 0xB000, 0x7C01, 0x6800, 0xA401,
  0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400,
  0xD801, 0x1400, 0x0000, 0xCC01, 0x0800, 0xC401, 0xD001, 0x1C00,
  0x1800, 0xD401, 0xC001, 0x0C00, 0xEC01, 0x2400, 0x3000, 0xFC00,
  0x2800, 0xE401, 0xF001, 0x3C00, 0xD801, 0x1400, 0x0000, 0xCC01,
  0xB001, 0x7C00, 0x6800, 0xA401, 0x6000, 0xAC01, 0xB801, 0x7400,
  0x5000, 0x9C01, 0x8801, 0x4400, 0xC001, 0x0C00, 0x1800, 0xD401,
  0xF001, 0x3C00, 0x2800, 0xE401, 0x2000, 0xEC01, 0xF801, 0x3400,
  0x7800, 0xB401, 0xA001, 0x6C00, 0xA801, 0x6400, 0x7000, 0xBC01,
  0x5800, 0x9401, 0x8001, 0x4C00, 0x0C00, 0xC401, 0xD001, 0x1C00,
  0xF800, 0x3401, 0x2000, 0xEC01, 0x0800, 0xC401, 0xD001, 0x1C00,
  0xB801, 0x7400, 0x6000, 0xAC01, 0x6800, 0xA401, 0xB001, 0x7C00
};

uint16_t modbus_crc16(const uint8_t* data, uint16_t length) {
  if (data == NULL || length == 0) return 0;

  uint16_t crc = 0xFFFF;

  for (uint16_t i = 0; i < length; i++) {
    uint8_t tbl_idx = (crc ^ data[i]) & 0xFF;
    crc = (crc >> 8) ^ crc16_table[tbl_idx];
  }

  return crc;
}

bool modbus_frame_verify_crc(const ModbusFrame* frame) {
  if (frame == NULL || frame->length < 3) return false;

  // CRC is calculated over slave_id + function_code + data
  // Data length = total length - 2 (CRC bytes)
  uint16_t data_len = frame->length - 2;

  // BUG-147 FIX: Validate data_len before calculating data payload size
  // If data_len < 2, then data_len - 2 would underflow and cause buffer overflow in memcpy
  if (data_len < 2) return false;

  uint8_t frame_data[254];

  // Copy slave_id + function_code + data (exclude CRC)
  frame_data[0] = frame->slave_id;
  frame_data[1] = frame->function_code;
  memcpy(&frame_data[2], frame->data, data_len - 2);

  uint16_t calculated_crc = modbus_crc16(frame_data, data_len);

  return (calculated_crc == frame->crc16);
}

void modbus_frame_set_crc(ModbusFrame* frame) {
  if (frame == NULL || frame->length < 3) return;

  // CRC is calculated over slave_id + function_code + data
  uint16_t data_len = frame->length - 2;

  // BUG-147 FIX: Validate data_len before calculating data payload size
  // If data_len < 2, then data_len - 2 would underflow and cause buffer overflow in memcpy
  if (data_len < 2) return;

  uint8_t frame_data[254];

  frame_data[0] = frame->slave_id;
  frame_data[1] = frame->function_code;
  memcpy(&frame_data[2], frame->data, data_len - 2);

  frame->crc16 = modbus_crc16(frame_data, data_len);
}

/* ============================================================================
 * FRAME VALIDATION
 * ============================================================================ */

bool modbus_frame_is_valid(const ModbusFrame* frame) {
  if (frame == NULL) return false;

  // Minimum frame: slave_id + function_code + CRC (3 bytes)
  if (frame->length < 3 || frame->length > 256) return false;

  // Valid Modbus function codes: 1-127 (bit 7 = error flag)
  if (frame->function_code == 0 || frame->function_code > 127) return false;

  // Verify CRC
  if (!modbus_frame_verify_crc(frame)) {
    debug_println("ERROR: Invalid CRC in Modbus frame");
    return false;
  }

  return true;
}
