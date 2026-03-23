/**
 * @file uart_driver.cpp
 * @brief UART hardware abstraction driver implementation (LAYER 0)
 *
 * Uses Arduino Serial/Serial1/Serial2 for ESP32 UART access
 * - UART0 (Serial):  USB/Debug on default pins (GPIO1/3)
 * - UART1 (Serial1): Modbus RTU on configurable pins
 * - UART2 (Serial2): Modbus RTU on configurable pins (ES32D26)
 *
 * UART selection is runtime-configurable via PersistConfig.modbus_slave_uart
 * ES32D26 NOTE: RS485 transceiver shares GPIO1/3 with USB debug.
 * Modbus Slave UART is NOT initialized at boot to preserve USB console.
 * RS485 must be explicitly enabled via CLI/API (USB console is then lost).
 * Use WiFi/Telnet for console when RS485 is active.
 */

#include "uart_driver.h"
#include "constants.h"
#include "config_struct.h"
#include "debug.h"
#include <Arduino.h>

// All three UART peripherals available
static HardwareSerial Serial1_inst(1);
static HardwareSerial Serial2_inst(2);

// Runtime-selected Modbus UART (set by uart_driver_init based on config)
static HardwareSerial* ModbusSlaveSerial = &Serial1_inst;
static bool modbus_slave_uart_active = false;
static uint8_t modbus_slave_uart_num = 1;  // Track which UART is in use

/* ============================================================================
 * UART INITIALIZATION
 * ============================================================================ */

void uart_driver_init(void) {
  // UART0 (USB/Debug) - already initialized by Arduino framework
  uart0_init(SERIAL_BAUD_DEBUG);

  // Select UART peripheral for Modbus slave based on config
  modbus_slave_uart_num = g_persist_config.modbus_slave_uart;
  if (modbus_slave_uart_num == 0) {
    ModbusSlaveSerial = &Serial;    // UART0 (shares with USB!)
  } else if (modbus_slave_uart_num == 2) {
    ModbusSlaveSerial = &Serial2_inst;  // UART2
  } else {
    modbus_slave_uart_num = 1;
    ModbusSlaveSerial = &Serial1_inst;  // UART1 (default)
  }

  debug_print("Modbus Slave UART: ");
  debug_print_uint(modbus_slave_uart_num);
  debug_println("");

  // Determine if safe to init at boot
  bool shares_usb = (PIN_UART1_RX == 3 && PIN_UART1_TX == 1) ||
                    (modbus_slave_uart_num == 0);
  if (shares_usb) {
    // UART shares GPIO1/3 with USB console — defer init
    modbus_slave_uart_active = false;
    debug_println("RS485: Deferred (shares GPIO1/3 with USB console)");
    debug_println("  USB console tabes naar RS485 aktiveres");
    debug_println("  Brug WiFi/Telnet for konsol-adgang");
  } else {
    // Separate pins, safe to init immediately
    uart1_init(SERIAL_BAUD_MODBUS);
  }
}

void uart0_init(uint32_t baudrate) {
  Serial.begin(baudrate);
  while (!Serial) {
    ; // Wait for serial port to connect (USB)
  }
}

void uart1_init(uint32_t baudrate) {
  // Remap to configured pins (PIN_UART1_RX/TX from constants.h)
  ModbusSlaveSerial->begin(baudrate, SERIAL_8N1, PIN_UART1_RX, PIN_UART1_TX);
  modbus_slave_uart_active = true;
}

void uart1_stop(void) {
  ModbusSlaveSerial->end();
  modbus_slave_uart_active = false;
  // If UART shares GPIO1/3 with USB, reclaim for console
  bool shares_usb = (PIN_UART1_RX == 3 && PIN_UART1_TX == 1) ||
                    (modbus_slave_uart_num == 0);
  if (shares_usb) {
    Serial.begin(SERIAL_BAUD_DEBUG);
  }
}

bool uart1_is_active(void) {
  return modbus_slave_uart_active;
}

/* ============================================================================
 * UART0 (DEBUG) OPERATIONS
 * ============================================================================ */

uint16_t uart0_available(void) {
  return Serial.available();
}

int uart0_read(void) {
  return Serial.read();
}

void uart0_write(uint8_t byte) {
  Serial.write(byte);
}

void uart0_write_buffer(const uint8_t* data, uint16_t length) {
  if (data == NULL || length == 0) return;
  Serial.write(data, length);
}

/* ============================================================================
 * UART1/UART2 (MODBUS SLAVE) OPERATIONS
 * Uses ModbusSlaveSerial reference (Serial1 or Serial2 depending on board)
 * ============================================================================ */

uint16_t uart1_available(void) {
  if (!modbus_slave_uart_active) return 0;
  return ModbusSlaveSerial->available();
}

int uart1_read(void) {
  if (!modbus_slave_uart_active) return -1;
  return ModbusSlaveSerial->read();
}

void uart1_write(uint8_t byte) {
  if (!modbus_slave_uart_active) return;
  ModbusSlaveSerial->write(byte);
}

void uart1_write_buffer(const uint8_t* data, uint16_t length) {
  if (!modbus_slave_uart_active) return;
  if (data == NULL || length == 0) return;
  ModbusSlaveSerial->write(data, length);
}

void uart1_flush_rx(void) {
  if (!modbus_slave_uart_active) return;
  // Clear all pending RX data
  while (ModbusSlaveSerial->available() > 0) {
    ModbusSlaveSerial->read();
  }
}

void uart1_flush_tx(void) {
  if (!modbus_slave_uart_active) return;
  // Wait for TX to complete
  ModbusSlaveSerial->flush();
}
