// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file heartbeat.h
 * @brief Heartbeat/watchdog - LED blink and system monitoring
 */

#ifndef heartbeat_H
#define heartbeat_H

/**
 * @brief Initialize heartbeat (checks gpio2_user_mode flag)
 */
void heartbeat_init(void);

/**
 * @brief Heartbeat loop (blinks LED if enabled)
 */
void heartbeat_loop(void);

/**
 * @brief Enable heartbeat (reserve GPIO2 for LED)
 */
void heartbeat_enable(void);

/**
 * @brief Disable heartbeat (release GPIO2 for user)
 */
void heartbeat_disable(void);

#endif // heartbeat_H
