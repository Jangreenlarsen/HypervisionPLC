// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file cli_parser.h
 * @brief CLI command parser and dispatcher (LAYER 7)
 *
 * LAYER 7: User Interface - CLI Parser
 * Responsibility: Tokenize input, dispatch to handlers
 *
 * This file handles:
 * - Command line tokenization
 * - Alias normalization (sh→show, wr→write, etc.)
 * - Main command dispatch (show, set, read, write, etc.)
 * - Error messages
 *
 * Does NOT handle:
 * - Command implementation (→ cli_commands.h, cli_show.h)
 * - Serial I/O (→ cli_shell.h)
 * - Command history (→ cli_history.h)
 */

#ifndef CLI_PARSER_H
#define CLI_PARSER_H

#include <stdint.h>
#include "types.h"

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

/**
 * @brief Parse and execute a command line
 * @param line Command line string (will be modified during tokenization)
 * @return true if command executed, false if error
 */
bool cli_parser_execute(char* line);

/**
 * @brief Execute ST Logic upload from multi-line CLI mode
 * @param program_id Program ID (0-3) for Logic1-Logic4
 * @param source_code Complete source code with newlines
 */
void cli_parser_execute_st_upload(uint8_t program_id, const char* source_code);

/**
 * @brief Print command help
 */
void cli_parser_print_help(void);

#endif // CLI_PARSER_H

