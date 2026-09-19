/**
 * @file watchdog_monitor.h
 * @brief ESP32 Task Watchdog Monitor (LAYER 8)
 *
 * LAYER 8: System - Watchdog Monitor
 * Responsibility: Monitor system health and auto-restart on hang
 *
 * This module wraps ESP32 Task Watchdog Timer (TWDT) and provides:
 * - Automatic system restart if main loop hangs (default 30s timeout)
 * - Reboot counter persistence in NVS
 * - Last error message tracking
 * - Subsystem health monitoring
 *
 * Usage:
 *   setup():
 *     watchdog_init();  // Enable watchdog with 30s timeout
 *
 *   loop():
 *     watchdog_feed();  // CRITICAL: Must be called < 30s interval!
 *
 * IMPORTANT: If loop() takes >30s, ESP32 will auto-reboot!
 */

#ifndef WATCHDOG_MONITOR_H
#define WATCHDOG_MONITOR_H

#include <stdint.h>
#include <stdbool.h>
#include "types.h"

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

/**
 * @brief Initialize watchdog monitor
 *
 * This function:
 * - Loads previous watchdog state from NVS
 * - Increments reboot counter
 * - Configures ESP32 Task WDT (30s timeout, trigger panic on timeout)
 * - Adds current task to watchdog
 * - Saves new state to NVS
 *
 * Must be called once in setup()
 */
void watchdog_init(void);

/**
 * @brief Feed the watchdog (reset timeout counter)
 *
 * CRITICAL: This function MUST be called from main loop() at least once
 * every 30 seconds (default timeout). If not called within timeout period,
 * ESP32 will trigger panic and reboot.
 *
 * Must be called in loop()
 */
void watchdog_feed(void);

/**
 * @brief Enable/disable watchdog monitoring
 * @param enable true to enable, false to disable
 *
 * Note: Disabling watchdog removes auto-restart protection!
 */
void watchdog_enable(bool enable);

/**
 * @brief Set watchdog timeout (in milliseconds)
 * @param timeout_ms Timeout in milliseconds (default 30000 = 30s)
 *
 * WARNING: Requires watchdog reconfiguration. Call before watchdog_init().
 */
void watchdog_set_timeout(uint32_t timeout_ms);

/**
 * @brief Get current watchdog state (for CLI display)
 * @return Pointer to WatchdogState struct
 */
WatchdogState* watchdog_get_state(void);

/**
 * @brief Convert an esp_reset_reason_t value (as stored in
 *        WatchdogState.last_reset_reason) to a human-readable string
 * @param reason_val Value from WatchdogState.last_reset_reason
 * @return Reset reason string
 */
const char* watchdog_reset_reason_to_str(uint32_t reason_val);

/**
 * @brief Track Modbus RX activity (optional health monitoring)
 *
 * Call this when Modbus frame is successfully received.
 * Used for subsystem health tracking.
 */
void watchdog_track_modbus_rx(void);

/**
 * @brief Track ST Logic execution (optional health monitoring)
 *
 * Call this when ST Logic programs are executed successfully.
 * Used for subsystem health tracking.
 */
void watchdog_track_st_logic(void);

/**
 * @brief Track heartbeat activity (optional health monitoring)
 *
 * Call this when heartbeat LED toggles successfully.
 * Used for subsystem health tracking.
 */
void watchdog_track_heartbeat(void);

/**
 * @brief Save watchdog state to NVS
 * @return true if successful, false if NVS write failed
 *
 * Saves reboot counter, last error, uptime, etc. to NVS for persistence.
 */
bool watchdog_save_state(void);

/**
 * @brief Load watchdog state from NVS
 * @return true if successful, false if no data or CRC error
 *
 * Loads previous reboot counter, last error, etc. from NVS.
 */
bool watchdog_load_state(void);

/**
 * @brief Record error message before potential watchdog trigger
 * @param error_msg Error message string (max 127 chars)
 *
 * Use this to record the last error before a potential hang/crash.
 * Error will be visible after reboot via "show watchdog" CLI command.
 */
void watchdog_record_error(const char* error_msg);

#endif // WATCHDOG_MONITOR_H
