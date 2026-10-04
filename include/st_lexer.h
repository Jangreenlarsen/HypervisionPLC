// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file st_lexer.h
 * @brief Structured Text Lexer (tokenizer)
 *
 * Converts ST source code (text) into tokens (ST_TOK_*).
 * Implements IEC 61131-3 6.3.1 lexical elements.
 *
 * Usage:
 *   st_lexer_t lexer;
 *   st_lexer_init(&lexer, "IF x > 10 THEN y := 1; END_IF;");
 *   st_token_t token;
 *   while (st_lexer_next_token(&lexer, &token) && token.type != ST_TOK_EOF) {
 *     // Process token
 *   }
 */

#ifndef ST_LEXER_H
#define ST_LEXER_H

#include "st_types.h"

/* Lexer state machine */
typedef struct {
  const char *input;        // Input string pointer
  uint32_t pos;             // Current position
  uint32_t line;            // Current line number
  uint32_t column;          // Current column number
  char current_char;        // Current character
} st_lexer_t;

/**
 * @brief Initialize lexer with input string
 * @param lexer Lexer state
 * @param input Source code string
 */
void st_lexer_init(st_lexer_t *lexer, const char *input);

/**
 * @brief Get next token from input
 * @param lexer Lexer state
 * @param token Output token structure
 * @return true if token valid, false on error
 */
bool st_lexer_next_token(st_lexer_t *lexer, st_token_t *token);

/**
 * @brief Peek next token without consuming
 * @param lexer Lexer state
 * @param token Output token structure
 * @return true if token valid
 */
bool st_lexer_peek_token(st_lexer_t *lexer, st_token_t *token);

/**
 * @brief Convert token type to string (for debugging)
 * @param type Token type
 * @return String representation
 */
const char *st_token_type_to_string(st_token_type_t type);

#endif // ST_LEXER_H
