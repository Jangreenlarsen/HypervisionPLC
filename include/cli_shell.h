// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file cli_shell.h
 * @brief CLI shell - console input/output and command loop (LAYER 7)
 *
 * LAYER 7: User Interface - CLI Shell
 * Responsibility: Console I/O, prompt display, command execution loop
 *
 * This file handles:
 * - Console input buffering and line reading
 * - Prompt display
 * - Command loop integration
 * - User feedback
 *
 * REFACTORED: Now uses Console abstraction instead of hardcoded Serial
 */

#ifndef cli_shell_H
#define cli_shell_H

#include <stdint.h>
#include "types.h"
#include "console.h"

/**
 * @brief Initialize CLI shell (called once from setup)
 * @param console Console instance to use (Serial, Telnet, etc.)
 */
void cli_shell_init(Console *console);

/**
 * @brief Main CLI loop (called from main loop, non-blocking)
 * @param console Console instance to process
 */
void cli_shell_loop(Console *console);

/**
 * @brief Enable/disable remote echo (for serial terminals)
 * @param enabled 1 to enable echo, 0 to disable
 */
void cli_shell_set_remote_echo(uint8_t enabled);

/**
 * @brief Get current remote echo state
 * @return 1 if enabled, 0 if disabled
 */
uint8_t cli_shell_get_remote_echo(void);

/**
 * @brief Start ST Logic multi-line upload mode
 * @param program_id Program ID (0-3) for Logic1-Logic4
 */
void cli_shell_start_st_upload(uint8_t program_id);

/**
 * @brief Reset from ST Logic upload mode back to normal CLI
 */
void cli_shell_reset_upload_mode(void);

/**
 * @brief Check if currently in ST Logic upload mode
 * @return 1 if in upload mode, 0 if in normal mode
 */
uint8_t cli_shell_is_in_upload_mode(void);

/**
 * @brief Feed a line to ST Logic upload mode (for Telnet copy/paste support)
 * @param console Console to write output to
 * @param line Line of ST code (or END_UPLOAD)
 * @note Only call this if cli_shell_is_in_upload_mode() returns 1
 */
void cli_shell_feed_upload_line(Console *console, const char *line);

/**
 * @brief Execute a CLI command on a specific console
 * @param console Console to execute on
 * @param cmd Command string to execute
 *
 * This function allows external interfaces to execute CLI commands.
 * Output is directed to the specified console.
 */
void cli_shell_execute_command(Console *console, const char *cmd);

/**
 * @brief Set the active console for debug output
 * @param console Console to use for debug_print/debug_println
 *
 * This allows debug output to be redirected to different consoles.
 * Set to NULL to disable debug output to console.
 */
void cli_shell_set_debug_console(Console *console);

/**
 * @brief Get the active console for debug output
 * @return Current debug console, or NULL if not set
 */
Console* cli_shell_get_debug_console(void);

#endif // cli_shell_H
