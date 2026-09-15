/**
 * @file modbus_master.h
 * @brief Modbus Master functionality (UART1)
 *
 * Provides Modbus RTU Master capability on UART1 for ST Logic programs
 * to read/write remote Modbus slave devices.
 */

#ifndef MODBUS_MASTER_H
#define MODBUS_MASTER_H

#include <Arduino.h>
#include "types.h"
#include "constants.h"

/* ============================================================================
 * GLOBAL CONFIGURATION
 * ============================================================================ */

extern modbus_master_config_t g_modbus_master_config;

/* ============================================================================
 * INITIALIZATION & CONTROL
 * ============================================================================ */

/**
 * @brief Initialize Modbus Master hardware (UART1)
 *
 * Sets up UART1 with configured baudrate, parity, stop bits.
 * Configures DE/RE pin for MAX485 transceiver.
 */
void modbus_master_init();

/**
 * @brief Enable/disable Modbus Master
 *
 * @param enabled true to enable, false to disable
 */
void modbus_master_set_enabled(bool enabled);

/**
 * @brief Reconfigure UART parameters
 *
 * Restarts UART1 with new baudrate/parity/stop bits.
 */
void modbus_master_reconfigure();

/**
 * @brief Activate UART for master mode (ES32D26 deferred init)
 *
 * On ES32D26, UART shares GPIO1/3 with USB serial. This function
 * should be called AFTER network services (WiFi/Telnet) are started,
 * so there's a fallback console before USB serial is taken over.
 */
void modbus_master_activate_uart();

/**
 * @brief BUG-334: true hvis RS485-aktivering blev afbrudt ved boot, saa
 * masteren er slaaet fra i RAM mens den gemte config stadig siger 'on'.
 * Ryddes af modbus_master_set_enabled(true).
 */
extern bool g_modbus_master_boot_aborted;

/**
 * @brief BUG-338/339: taeller for MB_BUS_BUSY (UART-mutex ikke opnaaet i tide).
 * Bevidst IKKE et felt i modbus_master_config_t — den struct er indlejret i
 * PersistConfig (raw NVS-blob), og et rent runtime-taelle-felt hoerer ikke
 * hjemme i noget der aendrer NVS-layoutet. Nulstilles af modbus_master_reset_stats()
 * og mb_async_reset_stats() ligesom de oevrige stats-taellere.
 */
extern uint32_t g_modbus_bus_busy_errors;

/**
 * @brief Reset statistics counters
 */
void modbus_master_reset_stats();

/* ============================================================================
 * MODBUS PROTOCOL FUNCTIONS (FC01-FC06)
 * ============================================================================ */

/**
 * @brief Read Coil (FC01)
 *
 * @param slave_id Slave address (1-247)
 * @param address Coil address (0-65535)
 * @param result Pointer to store result (true/false)
 * @return mb_error_code_t Error code (MB_OK on success)
 */
mb_error_code_t modbus_master_read_coil(uint8_t slave_id, uint16_t address, bool *result);

/**
 * @brief Read Discrete Input (FC02)
 *
 * @param slave_id Slave address (1-247)
 * @param address Input address (0-65535)
 * @param result Pointer to store result (true/false)
 * @return mb_error_code_t Error code (MB_OK on success)
 */
mb_error_code_t modbus_master_read_input(uint8_t slave_id, uint16_t address, bool *result);

/**
 * @brief Read Holding Register (FC03)
 *
 * @param slave_id Slave address (1-247)
 * @param address Register address (0-65535)
 * @param result Pointer to store result (16-bit value)
 * @return mb_error_code_t Error code (MB_OK on success)
 */
mb_error_code_t modbus_master_read_holding(uint8_t slave_id, uint16_t address, uint16_t *result);

/**
 * @brief Read Input Register (FC04)
 *
 * @param slave_id Slave address (1-247)
 * @param address Register address (0-65535)
 * @param result Pointer to store result (16-bit value)
 * @return mb_error_code_t Error code (MB_OK on success)
 */
mb_error_code_t modbus_master_read_input_register(uint8_t slave_id, uint16_t address, uint16_t *result);

/**
 * @brief Write Single Coil (FC05)
 *
 * @param slave_id Slave address (1-247)
 * @param address Coil address (0-65535)
 * @param value Value to write (true/false)
 * @return mb_error_code_t Error code (MB_OK on success)
 */
mb_error_code_t modbus_master_write_coil(uint8_t slave_id, uint16_t address, bool value);

/**
 * @brief Write Single Register (FC06)
 *
 * @param slave_id Slave address (1-247)
 * @param address Register address (0-65535)
 * @param value Value to write (16-bit)
 * @return mb_error_code_t Error code (MB_OK on success)
 */
mb_error_code_t modbus_master_write_holding(uint8_t slave_id, uint16_t address, uint16_t value);

/* ============================================================================
 * MULTI-REGISTER FUNCTIONS (FC03 multi / FC16)
 * ============================================================================ */

/**
 * @brief Read Multiple Holding Registers (FC03, count > 1)
 *
 * @param slave_id Slave address (1-247)
 * @param address Start register address (0-65535)
 * @param count Number of registers to read (1-16)
 * @param results Array to store results (must hold count uint16_t's)
 * @return mb_error_code_t Error code (MB_OK on success)
 */
mb_error_code_t modbus_master_read_holdings(uint8_t slave_id, uint16_t address, uint8_t count, uint16_t *results);

/**
 * @brief Write Multiple Holding Registers (FC16)
 *
 * @param slave_id Slave address (1-247)
 * @param address Start register address (0-65535)
 * @param count Number of registers to write (1-16)
 * @param values Array of values to write (count uint16_t's)
 * @return mb_error_code_t Error code (MB_OK on success)
 */
mb_error_code_t modbus_master_write_holdings(uint8_t slave_id, uint16_t address, uint8_t count, const uint16_t *values);

/**
 * @brief Write Multiple Coils (FC15, v7.9.68.0)
 *
 * @param slave_id Slave address (1-247)
 * @param address Start coil address (0-65535)
 * @param count Number of coils to write (1-16)
 * @param values Array of values to write (count bools)
 * @return mb_error_code_t Error code (MB_OK on success)
 */
mb_error_code_t modbus_master_write_coils(uint8_t slave_id, uint16_t address, uint8_t count, const bool *values);

/* ============================================================================
 * INTERNAL FUNCTIONS
 * ============================================================================ */

/**
 * @brief Send Modbus request and wait for response
 *
 * @param request Request frame buffer
 * @param request_len Request length in bytes
 * @param response Response frame buffer
 * @param response_len Pointer to store response length
 * @param max_response_len Maximum response buffer size
 * @return mb_error_code_t Error code
 */
mb_error_code_t modbus_master_send_request(
  const uint8_t *request,
  uint8_t request_len,
  uint8_t *response,
  uint8_t *response_len,
  uint8_t max_response_len
);

/**
 * @brief Calculate Modbus RTU CRC16
 *
 * @param buffer Data buffer
 * @param len Buffer length
 * @return uint16_t CRC16 value
 */
uint16_t modbus_master_calc_crc(const uint8_t *buffer, uint8_t len);

#endif // MODBUS_MASTER_H
