/**
 * @file ntp_driver.h
 * @brief NTP time synchronization driver (v7.8.1)
 *
 * LAYER 3: Driver — NTP/SNTP client using ESP-IDF lwIP SNTP
 * Provides real-time clock via configurable NTP server.
 */

#ifndef NTP_DRIVER_H
#define NTP_DRIVER_H

#include <Arduino.h>
#include <time.h>
#include "types.h"

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

/**
 * @brief Initialize NTP client with current config
 * Call after network is connected.
 */
void ntp_driver_init(void);

/**
 * @brief Stop NTP client
 */
void ntp_driver_stop(void);

/**
 * @brief Re-initialize NTP after config change
 */
void ntp_driver_reconfigure(void);

/**
 * @brief Check if time has been synchronized at least once
 * @return true if NTP sync has completed
 */
bool ntp_driver_is_synced(void);

/**
 * @brief Get current time as struct tm (local time)
 * @param[out] timeinfo Pointer to struct tm to fill
 * @return true if time is valid (synced)
 */
bool ntp_driver_get_local_time(struct tm *timeinfo);

/**
 * @brief Get current time as formatted string
 * @param[out] buf Buffer to write formatted time (min 32 bytes)
 * @param bufsize Buffer size
 * @return true if time is valid
 */
bool ntp_driver_get_time_str(char *buf, size_t bufsize);

/**
 * @brief Get current time as ISO 8601 string (YYYY-MM-DDTHH:MM:SS)
 * @param[out] buf Buffer to write (min 24 bytes)
 * @param bufsize Buffer size
 * @return true if time is valid
 */
bool ntp_driver_get_iso_time(char *buf, size_t bufsize);

/**
 * @brief Get epoch time (seconds since 1970-01-01)
 * @return time_t or 0 if not synced
 */
time_t ntp_driver_get_epoch(void);

/**
 * @brief Get milliseconds since last successful sync
 * @return ms since sync, or UINT32_MAX if never synced
 */
uint32_t ntp_driver_get_last_sync_age_ms(void);

/**
 * @brief Get total sync count since boot
 */
uint32_t ntp_driver_get_sync_count(void);

/**
 * @brief Get total sync error count since boot
 */
uint32_t ntp_driver_get_error_count(void);

#endif // NTP_DRIVER_H
