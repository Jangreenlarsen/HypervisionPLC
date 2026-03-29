/**
 * @file st_source_scanner.cpp
 * @brief ST source code chunk scanner for multi-pass compilation
 *
 * Uses the lexer to find chunk boundaries without building an AST.
 * This allows the compiler to process one chunk at a time with a small
 * AST pool (~4.5 KB) instead of needing the full pool (23-82 KB).
 */

#include "st_source_scanner.h"
#include "st_lexer.h"
#include "st_types.h"
#include <string.h>

/* Helper: record current lexer position as byte offset */
static uint32_t lexer_offset(const st_lexer_t *lexer) {
  return lexer->pos;
}

bool st_source_scan(const char *source, st_scan_result_t *result) {
  if (!source || !result) return false;

  memset(result, 0, sizeof(*result));

  st_lexer_t lexer;
  st_token_t token;
  st_lexer_init(&lexer, source);

  // Get first token
  if (!st_lexer_next_token(&lexer, &token)) return false;

  uint32_t source_len = strlen(source);

  // 1. Optional PROGRAM header
  if (token.type == ST_TOK_PROGRAM) {
    result->has_program_keyword = true;
    uint32_t start = 0;  // Start of source

    // Advance past PROGRAM
    if (!st_lexer_next_token(&lexer, &token)) return false;

    // Program name
    if (token.type == ST_TOK_IDENT) {
      strncpy(result->program_name, token.value, 31);
      result->program_name[31] = '\0';
      if (!st_lexer_next_token(&lexer, &token)) return false;
    }

    // Optional semicolon
    if (token.type == ST_TOK_SEMICOLON) {
      if (!st_lexer_next_token(&lexer, &token)) return false;
    }

    // Record PROGRAM header chunk (from start to current position)
    // We don't actually need to re-parse this chunk separately
  }

  // 2. VAR blocks
  while (token.type == ST_TOK_VAR || token.type == ST_TOK_VAR_INPUT ||
         token.type == ST_TOK_VAR_OUTPUT) {
    if (result->chunk_count >= ST_MAX_CHUNKS) return false;

    st_chunk_t *chunk = &result->chunks[result->chunk_count];
    chunk->type = ST_CHUNK_VAR_BLOCK;
    // Approximate start offset: current lexer position minus token length
    chunk->start_offset = lexer.pos > strlen(token.value) ?
                          lexer.pos - strlen(token.value) : 0;

    // Skip until END_VAR
    while (token.type != ST_TOK_END_VAR && token.type != ST_TOK_EOF) {
      if (!st_lexer_next_token(&lexer, &token)) return false;
    }
    if (token.type == ST_TOK_END_VAR) {
      if (!st_lexer_next_token(&lexer, &token)) return false;
      // Optional semicolon after END_VAR
      if (token.type == ST_TOK_SEMICOLON) {
        if (!st_lexer_next_token(&lexer, &token)) return false;
      }
    }
    chunk->end_offset = lexer_offset(&lexer);
    result->chunk_count++;
  }

  // 3. FUNCTION / FUNCTION_BLOCK definitions
  while (token.type == ST_TOK_FUNCTION || token.type == ST_TOK_FUNCTION_BLOCK) {
    if (result->chunk_count >= ST_MAX_CHUNKS) return false;

    st_chunk_t *chunk = &result->chunks[result->chunk_count];
    bool is_fb = (token.type == ST_TOK_FUNCTION_BLOCK);
    chunk->type = is_fb ? ST_CHUNK_FUNCTION_BLOCK : ST_CHUNK_FUNCTION;

    // Estimate start offset
    chunk->start_offset = lexer.pos > strlen(token.value) ?
                          lexer.pos - strlen(token.value) : 0;

    // Advance to get function name
    if (!st_lexer_next_token(&lexer, &token)) return false;
    if (token.type == ST_TOK_IDENT) {
      strncpy(chunk->name, token.value, 31);
      chunk->name[31] = '\0';
      if (!st_lexer_next_token(&lexer, &token)) return false;
    }

    // Skip until END_FUNCTION or END_FUNCTION_BLOCK
    st_token_type_t end_tok = is_fb ? ST_TOK_END_FUNCTION_BLOCK : ST_TOK_END_FUNCTION;
    while (token.type != end_tok && token.type != ST_TOK_EOF) {
      if (!st_lexer_next_token(&lexer, &token)) return false;
    }
    if (token.type == end_tok) {
      if (!st_lexer_next_token(&lexer, &token)) return false;
      // Optional semicolon
      if (token.type == ST_TOK_SEMICOLON) {
        if (!st_lexer_next_token(&lexer, &token)) return false;
      }
    }
    chunk->end_offset = lexer_offset(&lexer);
    result->chunk_count++;
  }

  // 4. Optional BEGIN keyword
  if (token.type == ST_TOK_BEGIN) {
    if (!st_lexer_next_token(&lexer, &token)) return false;
  }

  // 5. Main body = everything remaining until END/END_PROGRAM/EOF
  if (token.type != ST_TOK_EOF &&
      token.type != ST_TOK_END &&
      token.type != ST_TOK_END_PROGRAM) {
    if (result->chunk_count >= ST_MAX_CHUNKS) return false;

    st_chunk_t *chunk = &result->chunks[result->chunk_count];
    chunk->type = ST_CHUNK_MAIN_BODY;
    chunk->start_offset = lexer.pos > strlen(token.value) ?
                          lexer.pos - strlen(token.value) : 0;
    chunk->end_offset = source_len;
    strncpy(chunk->name, "main", 31);
    result->chunk_count++;
  }

  return true;
}
