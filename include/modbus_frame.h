// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file modbus_frame.h
 * @brief Modbus frame definition and CRC16 calculation (LAYER 1)
 *
 * LAYER 1: Protocol Core - Modbus Framing
 * Responsibility: Frame structures, CRC16 calculation, validation
 *
 * This file handles:
 * - ModbusFrame struct definition
 * - CRC16-MODBUS (CRC16-CCITT-FALSE) calculation
 * - Frame validation
 * - Byte order handling (big-endian Modbus)
 *
 * Does NOT handle:
 * - Parsing raw bytes (→ modbus_parser.h)
 * - Building responses (→ modbus_serializer.h)
 */

#ifndef modbus_frame_H
#define modbus_frame_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * MODBUS FRAME STRUCTURE
 * ============================================================================ */

/**
 * @brief Modbus RTU Frame (max 256 bytes)
 * Structure: [SLAVE_ID] [FC] [DATA...] [CRC_LO] [CRC_HI]
 */
typedef struct {
  uint8_t slave_id;
  uint8_t function_code;
  uint8_t data[252];      // Max 252 bytes of data (256 - ID - FC - CRC)
  uint16_t crc16;
  uint16_t length;        // Total frame length (ID + FC + data + CRC)
} ModbusFrame;

/* ============================================================================
 * CRC16-MODBUS (CRC16-CCITT-FALSE)
 * ============================================================================ */

/**
 * @brief Calculate CRC16 for Modbus frame
 * @param data Data to calculate CRC over
 * @param length Length of data (excluding CRC bytes)
 * @return Calculated CRC16 value
 */
uint16_t modbus_crc16(const uint8_t* data, uint16_t length);

/**
 * @brief Verify CRC16 in frame
 * @param frame Frame with CRC to verify
 * @return true if CRC is valid, false otherwise
 */
bool modbus_frame_verify_crc(const ModbusFrame* frame);

/**
 * @brief Set CRC16 in frame
 * @param frame Frame to update with CRC
 */
void modbus_frame_set_crc(ModbusFrame* frame);

/* ============================================================================
 * FRAME VALIDATION
 * ============================================================================ */

/**
 * @brief Validate Modbus frame
 * @param frame Frame to validate
 * @return true if frame is valid, false otherwise
 */
bool modbus_frame_is_valid(const ModbusFrame* frame);

#endif // modbus_frame_H
