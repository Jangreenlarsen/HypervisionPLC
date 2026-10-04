// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
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
#include <stddef.h>  // FEAT-429: size_t
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
bool watchdog_set_timeout(uint32_t timeout_ms);  // FEAT-427: 5000-120000 ms, false = ugyldig

/**
 * @brief Get current watchdog state (for CLI display)
 * @return Pointer to WatchdogState struct
 */
WatchdogState* watchdog_get_state(void);

/* FEAT-427: TRUE hvis task-watchdog'en reelt er initialiseret og loopTask er
 * tilmeldt (kan vaere FALSE selvom state.enabled er 1, hvis init fejlede). */
bool watchdog_is_active(void);

/* FEAT-427: safe mode (A4). Aktiveres ved opstart efter
 * WATCHDOG_SAFE_MODE_STREAK crashes i traek. Mens den er aktiv: ingen ST-
 * udfoerelse, og alle udgangs-mappings tvinges til deres sikre tilstand
 * (gpio_mapping_safe_value(); udefineret = OFF). */
#define WATCHDOG_SAFE_MODE_STREAK   3
#define WATCHDOG_STABLE_UPTIME_MS   600000UL   // 10 min drift = ikke laengere "crash i traek"
bool watchdog_safe_mode(void);
void watchdog_clear_safe_mode(void);
bool watchdog_reset_was_crash(void);   // seneste opstart skyldtes et crash/watchdog

/* FEAT-427 lag B: kontrolleret genstart fra ST-watchdog'ens "reboot"-handling.
 * Gemmer aarsagen i last_error og taeller som et crash i traek (saa en
 * gentagen "reboot" ogsaa ender i safe mode). Vender aldrig tilbage. */
void watchdog_reboot_for(const char *reason);

/* FEAT-429: tidspunkt for "sidste fejl". 0 = ukendt (ingen NTP-tid da fejlen
 * skete). Formateret som lokal tid "YYYY-MM-DD HH:MM:SS" (tom streng hvis ukendt). */
uint32_t watchdog_last_error_epoch(void);
bool watchdog_last_error_time_str(char *buf, size_t n);

/* FEAT-427: overvaagning af baggrunds-tasks (mb_async, expansion-workers).
 * subscribe() kaldes EN gang oeverst i taskens funktion, feed() i hver
 * loekke-runde, og unsubscribe() SKAL kaldes foer vTaskDelete(NULL) — ellers
 * udloeser watchdog'en, fordi en tilmeldt task holder op med at fodre.
 * Alle er no-ops naar watchdog'en ikke er aktiv. */
void watchdog_task_subscribe(void);
void watchdog_task_feed(void);
void watchdog_task_unsubscribe(void);

/* FEAT-427: udskriv tilmeldte tasks + tid siden sidste fodring (show watchdog). */
void watchdog_print_tasks(void);

/* FEAT-427: overvaagede tasks til REST/GUI. Returnerer antal (max max_out). */
uint8_t watchdog_get_tasks(char (*names)[16], uint32_t *age_ms, uint8_t max_out);

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
