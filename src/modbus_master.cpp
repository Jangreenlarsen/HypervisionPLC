/**
 * @file modbus_master.cpp
 * @brief Modbus Master Implementation (UART1)
 *
 * Implements Modbus RTU Master on UART1 for reading/writing remote slaves.
 */

#include "modbus_master.h"
#include <HardwareSerial.h>
#if MODBUS_SINGLE_TRANSCEIVER
#include "uart_driver.h"
#include "gpio_driver.h"
#endif

/* ============================================================================
 * GLOBAL CONFIGURATION
 * ============================================================================ */

modbus_master_config_t g_modbus_master_config = {
  .enabled = false,
  .baudrate = MODBUS_MASTER_DEFAULT_BAUDRATE,
  .parity = MODBUS_MASTER_DEFAULT_PARITY,
  .stop_bits = MODBUS_MASTER_DEFAULT_STOP_BITS,
  .timeout_ms = MODBUS_MASTER_DEFAULT_TIMEOUT,
  .inter_frame_delay = MODBUS_MASTER_DEFAULT_INTER_FRAME,
  .max_requests_per_cycle = MODBUS_MASTER_DEFAULT_MAX_REQUESTS,
  .total_requests = 0,
  .successful_requests = 0,
  .timeout_errors = 0,
  .crc_errors = 0,
  .exception_errors = 0
};

/* ============================================================================
 * HARDWARE SERIAL
 * ============================================================================ */

#if !MODBUS_SINGLE_TRANSCEIVER
HardwareSerial ModbusSerial(1); // UART1 — dedicated master port (non-ES32D26)
#endif

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

void modbus_master_init() {
#if MODBUS_SINGLE_TRANSCEIVER
  // ES32D26: shared transceiver — DIR pin already configured by uart_driver
  // Nothing to do here; uart1_init() handles UART setup
#else
  // Configure DE/RE pin (MAX485 direction control)
  pinMode(MODBUS_MASTER_DE_PIN, OUTPUT);
  digitalWrite(MODBUS_MASTER_DE_PIN, LOW); // Receive mode
#endif

  // Initialize UART if enabled
  if (g_modbus_master_config.enabled) {
    modbus_master_reconfigure();
  }
}

void modbus_master_set_enabled(bool enabled) {
  g_modbus_master_config.enabled = enabled;

  if (enabled) {
    modbus_master_reconfigure();
  } else {
#if MODBUS_SINGLE_TRANSCEIVER
    uart1_stop();
#else
    ModbusSerial.end();
#endif
  }
}

void modbus_master_reconfigure() {
  if (!g_modbus_master_config.enabled) {
    return;
  }

#if MODBUS_SINGLE_TRANSCEIVER
  // ES32D26: reuse shared UART via uart_driver
  uart1_stop();
  uart1_init(g_modbus_master_config.baudrate);
  // DIR pin setup
  pinMode(MODBUS_MASTER_DE_PIN, OUTPUT);
  digitalWrite(MODBUS_MASTER_DE_PIN, LOW); // Receive mode
#else
  // Stop existing UART
  ModbusSerial.end();

  // Determine parity mode
  uint32_t config = SERIAL_8N1; // Default: 8 data bits, no parity, 1 stop bit

  if (g_modbus_master_config.parity == 1) { // Even parity
    config = (g_modbus_master_config.stop_bits == 2) ? SERIAL_8E2 : SERIAL_8E1;
  } else if (g_modbus_master_config.parity == 2) { // Odd parity
    config = (g_modbus_master_config.stop_bits == 2) ? SERIAL_8O2 : SERIAL_8O1;
  } else { // No parity
    config = (g_modbus_master_config.stop_bits == 2) ? SERIAL_8N2 : SERIAL_8N1;
  }

  // Start UART1
  ModbusSerial.begin(
    g_modbus_master_config.baudrate,
    config,
    MODBUS_MASTER_RX_PIN,
    MODBUS_MASTER_TX_PIN
  );

  // Flush any pending data
  ModbusSerial.flush();
  while (ModbusSerial.available()) {
    ModbusSerial.read();
  }
#endif
}

void modbus_master_reset_stats() {
  g_modbus_master_config.total_requests = 0;
  g_modbus_master_config.successful_requests = 0;
  g_modbus_master_config.timeout_errors = 0;
  g_modbus_master_config.crc_errors = 0;
  g_modbus_master_config.exception_errors = 0;
}

/* ============================================================================
 * CRC16 CALCULATION (Modbus RTU)
 * ============================================================================ */

uint16_t modbus_master_calc_crc(const uint8_t *buffer, uint8_t len) {
  uint16_t crc = 0xFFFF;

  for (uint8_t i = 0; i < len; i++) {
    crc ^= buffer[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }

  return crc;
}

/* ============================================================================
 * REQUEST/RESPONSE HANDLING
 * ============================================================================ */

mb_error_code_t modbus_master_send_request(
  const uint8_t *request,
  uint8_t request_len,
  uint8_t *response,
  uint8_t *response_len,
  uint8_t max_response_len
) {
  if (!g_modbus_master_config.enabled) {
    return MB_NOT_ENABLED;
  }

  // Flush RX buffer
#if MODBUS_SINGLE_TRANSCEIVER
  uart1_flush_rx();
#else
  while (ModbusSerial.available()) {
    ModbusSerial.read();
  }
#endif

  // Set DE/RE to transmit mode
  digitalWrite(MODBUS_MASTER_DE_PIN, HIGH);
  delayMicroseconds(50); // Small delay for transceiver switching

  // Send request
#if MODBUS_SINGLE_TRANSCEIVER
  uart1_write_buffer(request, request_len);
  uart1_flush_tx();
#else
  ModbusSerial.write(request, request_len);
  ModbusSerial.flush(); // Wait for TX complete
#endif

  // Set DE/RE to receive mode
  delayMicroseconds(50);
  digitalWrite(MODBUS_MASTER_DE_PIN, LOW);

  // Wait for response with timeout
  uint32_t start_time = millis();
  uint8_t bytes_received = 0;
  bool timeout = false;

  while (bytes_received < max_response_len) {
    // Check timeout
    if (millis() - start_time > g_modbus_master_config.timeout_ms) {
      timeout = true;
      break;
    }

    // Check for available data
#if MODBUS_SINGLE_TRANSCEIVER
    if (uart1_available()) {
      int b = uart1_read();
      if (b >= 0) {
        response[bytes_received++] = (uint8_t)b;
        start_time = millis();
      }
#else
    if (ModbusSerial.available()) {
      response[bytes_received++] = ModbusSerial.read();
      start_time = millis(); // Reset timeout on each received byte
#endif

      // Check if we have minimum response (slave_id + function + data + CRC)
      if (bytes_received >= 5) {
        // For exceptions: slave_id + (function | 0x80) + exception_code + CRC (5 bytes)
        // For normal: depends on function
        uint8_t function_code = response[1];

        if (function_code & 0x80) {
          // BUG-149 FIX: Exception response is always exactly 5 bytes
          // (slave_id + function + exception_code + CRC)
          // We already checked bytes_received >= 5 in outer condition, so break immediately
          break; // Complete exception response
        } else {
          // Normal response - check expected length
          if (function_code == 0x01 || function_code == 0x02) {
            // Read Coils/Inputs: slave_id + fc + byte_count + data + CRC
            if (bytes_received >= 3) {
              uint8_t byte_count = response[2];
              if (bytes_received >= (uint8_t)(3 + byte_count + 2)) {
                break; // Complete response
              }
            }
          } else if (function_code == 0x03 || function_code == 0x04) {
            // Read Registers: slave_id + fc + byte_count + data + CRC
            if (bytes_received >= 3) {
              uint8_t byte_count = response[2];
              if (bytes_received >= (uint8_t)(3 + byte_count + 2)) {
                break; // Complete response
              }
            }
          } else if (function_code == 0x05 || function_code == 0x06) {
            // Write Single: slave_id + fc + address(2) + value(2) + CRC(2) = 8 bytes
            if (bytes_received >= 8) {
              break; // Complete response
            }
          }
        }
      }
    }
  }

  *response_len = bytes_received;

  // Check timeout
  if (timeout || bytes_received == 0) {
    g_modbus_master_config.timeout_errors++;
    return MB_TIMEOUT;
  }

  // Verify CRC
  if (bytes_received >= 3) {
    uint16_t received_crc = (response[bytes_received - 1] << 8) | response[bytes_received - 2];
    uint16_t calculated_crc = modbus_master_calc_crc(response, bytes_received - 2);

    if (received_crc != calculated_crc) {
      g_modbus_master_config.crc_errors++;
      return MB_CRC_ERROR;
    }
  } else {
    return MB_CRC_ERROR;
  }

  // Check for Modbus exception
  if (response[1] & 0x80) {
    g_modbus_master_config.exception_errors++;
    return MB_EXCEPTION;
  }

  g_modbus_master_config.successful_requests++;
  return MB_OK;
}

/* ============================================================================
 * MODBUS FUNCTIONS
 * ============================================================================ */

mb_error_code_t modbus_master_read_coil(uint8_t slave_id, uint16_t address, bool *result) {
  uint8_t request[8];
  uint8_t response[8];
  uint8_t response_len;

  // Build request: slave_id + FC01 + address(2) + quantity(2) + CRC(2)
  request[0] = slave_id;
  request[1] = 0x01; // FC01: Read Coils
  request[2] = (address >> 8) & 0xFF;
  request[3] = address & 0xFF;
  request[4] = 0x00; // Quantity high byte (1 coil)
  request[5] = 0x01; // Quantity low byte
  uint16_t crc = modbus_master_calc_crc(request, 6);
  request[6] = crc & 0xFF;
  request[7] = (crc >> 8) & 0xFF;

  g_modbus_master_config.total_requests++;

  mb_error_code_t err = modbus_master_send_request(request, 8, response, &response_len, sizeof(response));
  if (err != MB_OK) {
    *result = false;
    return err;
  }

  // Parse response: slave_id + FC01 + byte_count + data + CRC
  if (response_len >= 5 && response[1] == 0x01) {
    *result = (response[3] & 0x01) != 0;
    return MB_OK;
  }

  return MB_CRC_ERROR;
}

mb_error_code_t modbus_master_read_input(uint8_t slave_id, uint16_t address, bool *result) {
  uint8_t request[8];
  uint8_t response[8];
  uint8_t response_len;

  // Build request: FC02 (Read Discrete Inputs)
  request[0] = slave_id;
  request[1] = 0x02;
  request[2] = (address >> 8) & 0xFF;
  request[3] = address & 0xFF;
  request[4] = 0x00;
  request[5] = 0x01;
  uint16_t crc = modbus_master_calc_crc(request, 6);
  request[6] = crc & 0xFF;
  request[7] = (crc >> 8) & 0xFF;

  g_modbus_master_config.total_requests++;

  mb_error_code_t err = modbus_master_send_request(request, 8, response, &response_len, sizeof(response));
  if (err != MB_OK) {
    *result = false;
    return err;
  }

  if (response_len >= 5 && response[1] == 0x02) {
    *result = (response[3] & 0x01) != 0;
    return MB_OK;
  }

  return MB_CRC_ERROR;
}

mb_error_code_t modbus_master_read_holding(uint8_t slave_id, uint16_t address, uint16_t *result) {
  uint8_t request[8];
  uint8_t response[9];
  uint8_t response_len;

  // Build request: FC03 (Read Holding Registers)
  request[0] = slave_id;
  request[1] = 0x03;
  request[2] = (address >> 8) & 0xFF;
  request[3] = address & 0xFF;
  request[4] = 0x00;
  request[5] = 0x01; // Read 1 register
  uint16_t crc = modbus_master_calc_crc(request, 6);
  request[6] = crc & 0xFF;
  request[7] = (crc >> 8) & 0xFF;

  g_modbus_master_config.total_requests++;

  mb_error_code_t err = modbus_master_send_request(request, 8, response, &response_len, sizeof(response));
  if (err != MB_OK) {
    *result = 0;
    return err;
  }

  // Parse response: slave_id + FC03 + byte_count(1) + data(2) + CRC(2)
  if (response_len >= 7 && response[1] == 0x03) {
    *result = (response[3] << 8) | response[4];
    return MB_OK;
  }

  return MB_CRC_ERROR;
}

mb_error_code_t modbus_master_read_input_register(uint8_t slave_id, uint16_t address, uint16_t *result) {
  uint8_t request[8];
  uint8_t response[9];
  uint8_t response_len;

  // Build request: FC04 (Read Input Registers)
  request[0] = slave_id;
  request[1] = 0x04;
  request[2] = (address >> 8) & 0xFF;
  request[3] = address & 0xFF;
  request[4] = 0x00;
  request[5] = 0x01;
  uint16_t crc = modbus_master_calc_crc(request, 6);
  request[6] = crc & 0xFF;
  request[7] = (crc >> 8) & 0xFF;

  g_modbus_master_config.total_requests++;

  mb_error_code_t err = modbus_master_send_request(request, 8, response, &response_len, sizeof(response));
  if (err != MB_OK) {
    *result = 0;
    return err;
  }

  if (response_len >= 7 && response[1] == 0x04) {
    *result = (response[3] << 8) | response[4];
    return MB_OK;
  }

  return MB_CRC_ERROR;
}

mb_error_code_t modbus_master_write_coil(uint8_t slave_id, uint16_t address, bool value) {
  uint8_t request[8];
  uint8_t response[8];
  uint8_t response_len;

  // Build request: FC05 (Write Single Coil)
  request[0] = slave_id;
  request[1] = 0x05;
  request[2] = (address >> 8) & 0xFF;
  request[3] = address & 0xFF;
  request[4] = value ? 0xFF : 0x00; // 0xFF00 = ON, 0x0000 = OFF
  request[5] = 0x00;
  uint16_t crc = modbus_master_calc_crc(request, 6);
  request[6] = crc & 0xFF;
  request[7] = (crc >> 8) & 0xFF;

  g_modbus_master_config.total_requests++;

  mb_error_code_t err = modbus_master_send_request(request, 8, response, &response_len, sizeof(response));
  return err;
}

mb_error_code_t modbus_master_write_holding(uint8_t slave_id, uint16_t address, uint16_t value) {
  uint8_t request[8];
  uint8_t response[8];
  uint8_t response_len;

  // Build request: FC06 (Write Single Register)
  request[0] = slave_id;
  request[1] = 0x06;
  request[2] = (address >> 8) & 0xFF;
  request[3] = address & 0xFF;
  request[4] = (value >> 8) & 0xFF;
  request[5] = value & 0xFF;
  uint16_t crc = modbus_master_calc_crc(request, 6);
  request[6] = crc & 0xFF;
  request[7] = (crc >> 8) & 0xFF;

  g_modbus_master_config.total_requests++;

  mb_error_code_t err = modbus_master_send_request(request, 8, response, &response_len, sizeof(response));
  return err;
}
