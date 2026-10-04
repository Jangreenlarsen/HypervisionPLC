// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file cli_history.h
 * @brief CLI command history and navigation (LAYER 7)
 *
 * Handles:
 * - Storing command history in circular buffer
 * - Navigation via up/down arrow keys
 * - Retrieval of previous/next commands
 */

#ifndef cli_history_H
#define cli_history_H

#include <stdint.h>
#include "types.h"
#include "constants.h"

#define CLI_HISTORY_LINE_LENGTH 256

/**
 * @brief Initialize command history
 */
void cli_history_init(void);

/**
 * @brief Add command to history (called when command is executed)
 * @param command The command string to store
 */
void cli_history_add(const char* command);

/**
 * @brief Get previous command (up arrow)
 * @return Pointer to previous command, or NULL if none
 */
const char* cli_history_get_prev(void);

/**
 * @brief Get next command (down arrow)
 * @return Pointer to next command, or NULL if at end
 */
const char* cli_history_get_next(void);

/**
 * @brief Reset history navigation to current (end of history)
 */
void cli_history_reset_nav(void);

#endif // cli_history_H
