/**
 * @file uart_driver.h
 * @brief UART hardware abstraction driver (LAYER 0)
 *
 * LAYER 0: Hardware Abstraction Driver
 * Provides UART read/write abstraction for Modbus RTU
 *
 * This file handles:
 * - UART0 (USB/Debug) initialization
 * - UART1 (Modbus RTU) initialization on GPIO4/5
 * - Non-blocking read/write operations
 * - Buffering
 */

#ifndef uart_driver_H
#define uart_driver_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * UART INITIALIZATION
 * ============================================================================ */

/**
 * @brief Initialize UART driver (UART0 for debug, UART1 for Modbus)
 */
void uart_driver_init(void);

/**
 * @brief Initialize UART0 (USB/Debug)
 * @param baudrate Baud rate (e.g., 115200)
 */
void uart0_init(uint32_t baudrate);

/**
 * @brief Initialize UART1 (Modbus RTU) on configured pins
 * @param baudrate Baud rate (e.g., 115200)
 * NOTE: On ES32D26, not called at boot (shares GPIO1/3 with USB)
 */
void uart1_init(uint32_t baudrate);

/**
 * @brief Stop Modbus Slave UART and release pins
 * On ES32D26: reclaims GPIO1/3 for USB console
 */
void uart1_stop(void);

/**
 * @brief Check if Modbus Slave UART is active
 * @return true if initialized and running
 */
bool uart1_is_active(void);

/**
 * @brief Get resolved RS485 DIR pin for Modbus Slave
 * Returns configured pin or board default from constants.h
 */
uint8_t uart_get_slave_dir_pin(void);

/**
 * @brief Get resolved RS485 DIR pin for Modbus Master
 * Returns configured pin or board default from constants.h
 */
uint8_t uart_get_master_dir_pin(void);

/**
 * @brief Get active TX/RX pins (resolved from config or defaults)
 */
uint8_t uart_get_active_tx_pin(void);
uint8_t uart_get_active_rx_pin(void);

/* ============================================================================
 * UART0 (DEBUG) OPERATIONS
 * ============================================================================ */

/**
 * @brief Check if data is available on UART0
 * @return Number of bytes available
 */
uint16_t uart0_available(void);

/**
 * @brief Read byte from UART0
 * @return Byte read (-1 if no data)
 */
int uart0_read(void);

/**
 * @brief Write byte to UART0
 * @param byte Byte to write
 */
void uart0_write(uint8_t byte);

/**
 * @brief Write buffer to UART0
 * @param data Data buffer
 * @param length Number of bytes to write
 */
void uart0_write_buffer(const uint8_t* data, uint16_t length);

/* ============================================================================
 * UART1 (MODBUS) OPERATIONS
 * ============================================================================ */

/**
 * @brief Check if data is available on UART1
 * @return Number of bytes available
 */
uint16_t uart1_available(void);

/**
 * @brief Read byte from UART1
 * @return Byte read (-1 if no data)
 */
int uart1_read(void);

/**
 * @brief Write byte to UART1
 * @param byte Byte to write
 */
void uart1_write(uint8_t byte);

/**
 * @brief Write buffer to UART1
 * @param data Data buffer
 * @param length Number of bytes to write
 */
void uart1_write_buffer(const uint8_t* data, uint16_t length);

/**
 * @brief Flush UART1 RX buffer (clear all pending data)
 */
void uart1_flush_rx(void);

/**
 * @brief Wait for UART1 TX to complete
 */
void uart1_flush_tx(void);

#endif // uart_driver_H
