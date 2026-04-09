/**
 * @file modbus_master.cpp
 * @brief Modbus Master Implementation (UART1)
 *
 * Implements Modbus RTU Master on UART1 for reading/writing remote slaves.
 */

#include "modbus_master.h"
#include "uart_driver.h"
#include "config_struct.h"
#include <HardwareSerial.h>
#if MODBUS_SINGLE_TRANSCEIVER
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
  .cache_ttl_ms = 0,  // 0 = never expire
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
  // BUG-239 FIX: Sync runtime config from persistent config at boot
  // g_modbus_master_config is initialized with .enabled=false at compile time,
  // but g_persist_config.modbus_master contains the NVS-loaded values.
  // Without this sync, modbus_master_send_request() always returns MB_NOT_ENABLED.
  g_modbus_master_config.enabled = g_persist_config.modbus_master.enabled;
  g_modbus_master_config.baudrate = g_persist_config.modbus_master.baudrate;
  g_modbus_master_config.parity = g_persist_config.modbus_master.parity;
  g_modbus_master_config.stop_bits = g_persist_config.modbus_master.stop_bits;
  g_modbus_master_config.timeout_ms = g_persist_config.modbus_master.timeout_ms;
  g_modbus_master_config.inter_frame_delay = g_persist_config.modbus_master.inter_frame_delay;
  g_modbus_master_config.max_requests_per_cycle = g_persist_config.modbus_master.max_requests_per_cycle;
  g_modbus_master_config.cache_ttl_ms = g_persist_config.modbus_master.cache_ttl_ms;
  g_modbus_master_config.stats_since_ms = millis();

#if MODBUS_SINGLE_TRANSCEIVER
  // ES32D26: shared transceiver — DIR pin already configured by uart_driver
  // Nothing to do here; uart1_init() handles UART setup
#else
  // Configure DE/RE pin (MAX485 direction control)
  pinMode(uart_get_master_dir_pin(), OUTPUT);
  digitalWrite(uart_get_master_dir_pin(), LOW); // Receive mode
#endif

  // Initialize UART if enabled
#if MODBUS_SINGLE_TRANSCEIVER
  // ES32D26: DEFER UART reconfigure — GPIO1/3 shares with USB serial.
  // If we reconfigure now, USB console dies BEFORE WiFi/Telnet is ready.
  // main.cpp calls modbus_master_activate_uart() after network services start.
  // Config is synced above, so async task and ST builtins work once UART is live.
#else
  if (g_modbus_master_config.enabled) {
    modbus_master_reconfigure();
  }
#endif
}

void modbus_master_activate_uart() {
  // Called from main.cpp AFTER network services are started.
  // Safe to take over GPIO1/3 now — Telnet is available as fallback console.
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
  pinMode(uart_get_master_dir_pin(), OUTPUT);
  digitalWrite(uart_get_master_dir_pin(), LOW); // Receive mode
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

  // Start UART — resolve pins from config (0xFF=board default)
  uint8_t mu = g_persist_config.modbus_master_uart;
  uint8_t rx = (mu == 2 && g_persist_config.uart2_rx_pin != 0xFF) ? g_persist_config.uart2_rx_pin :
               (mu == 1 && g_persist_config.uart1_rx_pin != 0xFF) ? g_persist_config.uart1_rx_pin :
               MODBUS_MASTER_RX_PIN;
  uint8_t tx = (mu == 2 && g_persist_config.uart2_tx_pin != 0xFF) ? g_persist_config.uart2_tx_pin :
               (mu == 1 && g_persist_config.uart1_tx_pin != 0xFF) ? g_persist_config.uart1_tx_pin :
               MODBUS_MASTER_TX_PIN;
  ModbusSerial.begin(
    g_modbus_master_config.baudrate,
    config,
    rx,
    tx
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
  digitalWrite(uart_get_master_dir_pin(), HIGH);
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
  digitalWrite(uart_get_master_dir_pin(), LOW);

  // Wait for response with timeout
  // Two-phase timeout: full timeout_ms for first byte, then shorter inter-char timeout
  uint32_t start_time = millis();
  uint8_t bytes_received = 0;
  bool timeout = false;
  // Inter-character timeout: T3.5 at baudrate (min 2ms, max 20ms)
  uint32_t interchar_ms = (uint32_t)(38500UL / g_modbus_master_config.baudrate);
  if (interchar_ms < 2) interchar_ms = 2;
  if (interchar_ms > 20) interchar_ms = 20;

  while (bytes_received < max_response_len) {
    // Check timeout: use full timeout for first byte, inter-char after that
    uint32_t active_timeout = (bytes_received == 0) ? g_modbus_master_config.timeout_ms : interchar_ms;
    if (millis() - start_time > active_timeout) {
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
      start_time = millis(); // Reset for inter-character timeout
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
          } else if (function_code == 0x10) {
            // FC16 Write Multiple: slave_id + fc + address(2) + count(2) + CRC(2) = 8 bytes
            if (bytes_received >= 8) {
              break; // Complete response
            }
          }
        }
      }
    }
  }

  *response_len = bytes_received;

  // Extract slave_id and address from request for error tracking
  uint8_t req_slave_id = request[0];
  uint16_t req_address = (request_len >= 4) ? ((uint16_t)request[2] << 8 | request[3]) : 0;

  // Check timeout
  if (timeout || bytes_received == 0) {
    g_modbus_master_config.timeout_errors++;
    g_modbus_master_config.last_error_slave_id = req_slave_id;
    g_modbus_master_config.last_error_address = req_address;
    g_modbus_master_config.last_error_type = MB_TIMEOUT;
    return MB_TIMEOUT;
  }

  // Verify CRC
  if (bytes_received >= 3) {
    uint16_t received_crc = (response[bytes_received - 1] << 8) | response[bytes_received - 2];
    uint16_t calculated_crc = modbus_master_calc_crc(response, bytes_received - 2);

    if (received_crc != calculated_crc) {
      g_modbus_master_config.crc_errors++;
      g_modbus_master_config.last_error_slave_id = req_slave_id;
      g_modbus_master_config.last_error_address = req_address;
      g_modbus_master_config.last_error_type = MB_CRC_ERROR;
      return MB_CRC_ERROR;
    }
  } else {
    g_modbus_master_config.last_error_slave_id = req_slave_id;
    g_modbus_master_config.last_error_address = req_address;
    g_modbus_master_config.last_error_type = MB_CRC_ERROR;
    return MB_CRC_ERROR;
  }

  // Check for Modbus exception
  if (response[1] & 0x80) {
    g_modbus_master_config.exception_errors++;
    g_modbus_master_config.last_error_slave_id = req_slave_id;
    g_modbus_master_config.last_error_address = req_address;
    g_modbus_master_config.last_error_type = MB_EXCEPTION;
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

mb_error_code_t modbus_master_read_holdings(uint8_t slave_id, uint16_t address, uint8_t count, uint16_t *results) {
  if (count == 0 || count > 16) return MB_INVALID_ADDRESS;

  uint8_t request[8];
  // Response: slave(1) + FC(1) + byte_count(1) + data(count*2) + CRC(2)
  uint8_t response[5 + 16 * 2];  // Max 16 registers
  uint8_t response_len;

  // Build request: FC03 (Read Holding Registers) with count
  request[0] = slave_id;
  request[1] = 0x03;
  request[2] = (address >> 8) & 0xFF;
  request[3] = address & 0xFF;
  request[4] = 0x00;
  request[5] = count;
  uint16_t crc = modbus_master_calc_crc(request, 6);
  request[6] = crc & 0xFF;
  request[7] = (crc >> 8) & 0xFF;

  g_modbus_master_config.total_requests++;

  mb_error_code_t err = modbus_master_send_request(request, 8, response, &response_len, sizeof(response));
  if (err != MB_OK) {
    memset(results, 0, count * sizeof(uint16_t));
    return err;
  }

  // Parse response: slave_id + FC03 + byte_count + data[count*2] + CRC
  uint8_t expected_bytes = count * 2;
  if (response_len >= (uint8_t)(5 + expected_bytes) && response[1] == 0x03 && response[2] == expected_bytes) {
    for (uint8_t i = 0; i < count; i++) {
      results[i] = (response[3 + i * 2] << 8) | response[4 + i * 2];
    }
    return MB_OK;
  }

  return MB_CRC_ERROR;
}

mb_error_code_t modbus_master_write_holdings(uint8_t slave_id, uint16_t address, uint8_t count, const uint16_t *values) {
  if (count == 0 || count > 16) return MB_INVALID_ADDRESS;

  // Request: slave(1) + FC16(1) + addr(2) + count(2) + byte_count(1) + data(count*2) + CRC(2)
  uint8_t request[9 + 16 * 2];  // Max 16 registers
  uint8_t response[8];
  uint8_t response_len;

  uint8_t byte_count = count * 2;

  // Build request: FC16 (Write Multiple Registers)
  request[0] = slave_id;
  request[1] = 0x10;  // FC16
  request[2] = (address >> 8) & 0xFF;
  request[3] = address & 0xFF;
  request[4] = 0x00;
  request[5] = count;
  request[6] = byte_count;
  for (uint8_t i = 0; i < count; i++) {
    request[7 + i * 2] = (values[i] >> 8) & 0xFF;
    request[8 + i * 2] = values[i] & 0xFF;
  }
  uint8_t req_len = 7 + byte_count;
  uint16_t crc = modbus_master_calc_crc(request, req_len);
  request[req_len] = crc & 0xFF;
  request[req_len + 1] = (crc >> 8) & 0xFF;

  g_modbus_master_config.total_requests++;

  mb_error_code_t err = modbus_master_send_request(request, req_len + 2, response, &response_len, sizeof(response));
  return err;
}
