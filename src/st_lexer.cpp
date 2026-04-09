/**
 * @file st_lexer.cpp
 * @brief Structured Text Lexer implementation
 *
 * Tokenizes ST source code according to IEC 61131-3.
 */

#include "st_lexer.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* ============================================================================
 * LEXER INITIALIZATION & UTILITIES
 * ============================================================================ */

void st_lexer_init(st_lexer_t *lexer, const char *input) {
  lexer->input = input;
  lexer->pos = 0;
  lexer->line = 1;
  lexer->column = 1;
  lexer->current_char = input ? input[0] : '\0';
}

/* Advance to next character */
static void lexer_advance(st_lexer_t *lexer) {
  if (lexer->current_char == '\n') {
    lexer->line++;
    lexer->column = 1;
  } else {
    lexer->column++;
  }
  lexer->pos++;
  lexer->current_char = lexer->input[lexer->pos];
}

/* Peek next character without consuming */
static char lexer_peek(st_lexer_t *lexer, int offset) {
  return lexer->input[lexer->pos + offset];
}

/* Skip whitespace (except newlines, which don't matter) */
static void lexer_skip_whitespace(st_lexer_t *lexer) {
  while (isspace(lexer->current_char)) {
    lexer_advance(lexer);
  }
}

/* Skip comment: (* comment *)
 * BUG-167 FIX: Return false if comment is not terminated */
static bool lexer_skip_comment(st_lexer_t *lexer) {
  if (lexer->current_char == '(' && lexer_peek(lexer, 1) == '*') {
    lexer_advance(lexer); // skip (
    lexer_advance(lexer); // skip *
    while (lexer->current_char != '\0') {
      if (lexer->current_char == '*' && lexer_peek(lexer, 1) == ')') {
        lexer_advance(lexer); // skip *
        lexer_advance(lexer); // skip )
        return true;  // BUG-167 FIX: Successfully found comment terminator
      }
      lexer_advance(lexer);
    }
    // BUG-167 FIX: Reached EOF without finding *) terminator
    return false;
  }
  return true;  // Not a comment start
}

/* ============================================================================
 * KEYWORD RECOGNITION
 * ============================================================================ */

typedef struct {
  const char *keyword;
  st_token_type_t token_type;
} keyword_entry_t;

static const keyword_entry_t keywords[] = {
  // Literals
  {"TRUE", ST_TOK_BOOL_TRUE},
  {"FALSE", ST_TOK_BOOL_FALSE},

  // Data types
  {"BOOL", ST_TOK_BOOL},
  {"INT", ST_TOK_INT_KW},
  {"DINT", ST_TOK_DINT_KW},
  {"DWORD", ST_TOK_DWORD},
  {"REAL", ST_TOK_REAL_KW},
  {"TIME", ST_TOK_TIME_KW},  // FEAT-121: TIME datatype keyword

  // Variable declarations
  {"VAR", ST_TOK_VAR},
  {"VAR_INPUT", ST_TOK_VAR_INPUT},
  {"VAR_OUTPUT", ST_TOK_VAR_OUTPUT},
  {"VAR_IN_OUT", ST_TOK_VAR_IN_OUT},
  {"END_VAR", ST_TOK_END_VAR},
  {"CONST", ST_TOK_CONST},
  {"EXPORT", ST_TOK_EXPORT},  // v5.1.0 - IR pool export modifier

  // Control structures
  {"IF", ST_TOK_IF},
  {"THEN", ST_TOK_THEN},
  {"ELSE", ST_TOK_ELSE},
  {"ELSIF", ST_TOK_ELSIF},
  {"END_IF", ST_TOK_END_IF},

  {"CASE", ST_TOK_CASE},
  {"OF", ST_TOK_OF},
  {"END_CASE", ST_TOK_END_CASE},

  {"FOR", ST_TOK_FOR},
  {"TO", ST_TOK_TO},
  {"BY", ST_TOK_BY},
  {"DO", ST_TOK_DO},
  {"END_FOR", ST_TOK_END_FOR},

  {"WHILE", ST_TOK_WHILE},
  {"END_WHILE", ST_TOK_END_WHILE},

  {"REPEAT", ST_TOK_REPEAT},
  {"UNTIL", ST_TOK_UNTIL},
  {"END_REPEAT", ST_TOK_END_REPEAT},

  {"EXIT", ST_TOK_EXIT},
  {"RETURN", ST_TOK_RETURN},

  {"PROGRAM", ST_TOK_PROGRAM},
  {"END_PROGRAM", ST_TOK_END_PROGRAM},
  {"BEGIN", ST_TOK_BEGIN},
  {"END", ST_TOK_END},

  // FEAT-003: User-defined functions (IEC 61131-3)
  {"FUNCTION", ST_TOK_FUNCTION},
  {"END_FUNCTION", ST_TOK_END_FUNCTION},
  {"FUNCTION_BLOCK", ST_TOK_FUNCTION_BLOCK},
  {"END_FUNCTION_BLOCK", ST_TOK_END_FUNCTION_BLOCK},

  // FEAT-004: Array support
  {"ARRAY", ST_TOK_ARRAY},

  // Operators
  {"AND", ST_TOK_AND},
  {"OR", ST_TOK_OR},
  {"NOT", ST_TOK_NOT},
  {"XOR", ST_TOK_XOR},
  {"MOD", ST_TOK_MOD},
  {"SHL", ST_TOK_SHL},
  {"SHR", ST_TOK_SHR},

  {NULL, ST_TOK_ERROR}
};

/* Look up keyword, return token type or ST_TOK_ERROR if not found */
static st_token_type_t lexer_lookup_keyword(const char *text) {
  for (int i = 0; keywords[i].keyword != NULL; i++) {
    if (strcasecmp(text, keywords[i].keyword) == 0) {
      return keywords[i].token_type;
    }
  }
  return ST_TOK_IDENT;  // Not a keyword, treat as identifier
}

/* ============================================================================
 * NUMBER PARSING
 * ============================================================================ */

/* Parse integer: 123, -456, 0x1A2B, 2#1010 (binary) */
static bool lexer_read_integer(st_lexer_t *lexer, st_token_t *token) {
  char buffer[64] = {0};
  int i = 0;

  // Check for hex prefix (0x)
  if (lexer->current_char == '0' && (lexer_peek(lexer, 1) == 'x' || lexer_peek(lexer, 1) == 'X')) {
    buffer[i++] = lexer->current_char;
    lexer_advance(lexer);
    buffer[i++] = lexer->current_char;
    lexer_advance(lexer);

    while (isxdigit(lexer->current_char) && i < 63) {
      buffer[i++] = lexer->current_char;
      lexer_advance(lexer);
    }
    buffer[i] = '\0';  // BUG-067: Explicit null terminator
    strncpy(token->value, buffer, 255);
    token->value[255] = '\0';
    token->type = ST_TOK_INT;
    return true;
  }

  // Check for binary prefix (2#)
  if (isdigit(lexer->current_char) && lexer_peek(lexer, 1) == '#') {
    lexer_advance(lexer); // skip digit
    lexer_advance(lexer); // skip #

    while ((lexer->current_char == '0' || lexer->current_char == '1') && i < 63) {
      buffer[i++] = lexer->current_char;
      lexer_advance(lexer);
    }
    buffer[i] = '\0';  // BUG-067: Explicit null terminator
    strncpy(token->value, buffer, 255);
    token->value[255] = '\0';
    token->type = ST_TOK_INT;
    return true;
  }

  // Standard decimal number
  while (isdigit(lexer->current_char) && i < 63) {
    buffer[i++] = lexer->current_char;
    lexer_advance(lexer);
  }

  buffer[i] = '\0';  // BUG-067: Explicit null terminator
  strncpy(token->value, buffer, 255);
  token->value[255] = '\0';
  token->type = ST_TOK_INT;
  return true;
}

/* Parse real number: 1.23, 4.56e-10 */
static bool lexer_read_real(st_lexer_t *lexer, st_token_t *token) {
  char buffer[64] = {0};
  int i = 0;

  // Read integer part
  while (isdigit(lexer->current_char) && i < 60) {
    buffer[i++] = lexer->current_char;
    lexer_advance(lexer);
  }

  // Read decimal part
  if (lexer->current_char == '.' && isdigit(lexer_peek(lexer, 1))) {
    buffer[i++] = '.';
    lexer_advance(lexer);

    while (isdigit(lexer->current_char) && i < 60) {
      buffer[i++] = lexer->current_char;
      lexer_advance(lexer);
    }
  }

  // Read exponent part (e or E)
  if ((lexer->current_char == 'e' || lexer->current_char == 'E') && i < 60) {
    buffer[i++] = lexer->current_char;
    lexer_advance(lexer);

    if (lexer->current_char == '+' || lexer->current_char == '-') {
      buffer[i++] = lexer->current_char;
      lexer_advance(lexer);
    }

    while (isdigit(lexer->current_char) && i < 63) {
      buffer[i++] = lexer->current_char;
      lexer_advance(lexer);
    }
  }

  buffer[i] = '\0';  // BUG-067: Explicit null terminator
  strncpy(token->value, buffer, 255);
  token->value[255] = '\0';
  token->type = ST_TOK_REAL;
  return true;
}

/* ============================================================================
 * STRING PARSING
 * ============================================================================ */

/* Parse string: 'hello' or "hello" */
static bool lexer_read_string(st_lexer_t *lexer, st_token_t *token) {
  char quote = lexer->current_char;
  lexer_advance(lexer); // skip opening quote

  char buffer[256] = {0};
  int i = 0;

  // BUG-155 FIX: Read string with explicit length limit
  while (lexer->current_char != quote && lexer->current_char != '\0' && i < 255) {  // BUG-068: Changed from 250 to 255
    if (lexer->current_char == '\\' && lexer_peek(lexer, 1) == quote) {
      // Escaped quote
      buffer[i++] = quote;
      lexer_advance(lexer);
      lexer_advance(lexer);
    } else {
      buffer[i++] = lexer->current_char;
      lexer_advance(lexer);
    }
  }

  // BUG-155 FIX: Check if string was truncated (hit length limit before closing quote)
  if (i >= 255 && lexer->current_char != quote && lexer->current_char != '\0') {
    token->type = ST_TOK_ERROR;
    snprintf(token->value, sizeof(token->value), "String literal too long (max 255 chars)");
    // Skip to closing quote or EOF
    while (lexer->current_char != quote && lexer->current_char != '\0') {
      lexer_advance(lexer);
    }
    if (lexer->current_char == quote) {
      lexer_advance(lexer); // skip closing quote
    }
    return false;
  }

  if (lexer->current_char != quote) {
    token->type = ST_TOK_ERROR;
    strncpy(token->value, "Unterminated string", 255);
    token->value[255] = '\0';
    return false;
  }

  lexer_advance(lexer); // skip closing quote
  buffer[i] = '\0';  // BUG-068: Explicit null terminator
  strncpy(token->value, buffer, 255);
  token->value[255] = '\0';
  token->type = ST_TOK_STRING;
  return true;
}

/* ============================================================================
 * TIME LITERAL PARSING (FEAT-006: IEC 61131-3 TIME literals)
 * ============================================================================ */

/**
 * @brief Parse TIME literal: T#5s, T#100ms, T#1h30m, T#2d5h30m15s100ms
 *
 * IEC 61131-3 TIME format:
 *   T#[days]d[hours]h[minutes]m[seconds]s[milliseconds]ms
 *
 * Examples:
 *   T#5s      → 5000 ms
 *   T#100ms   → 100 ms
 *   T#1h30m   → 5400000 ms
 *   T#1d      → 86400000 ms
 *   T#2h30m15s → 9015000 ms
 *
 * The value is stored as DINT (int32_t) milliseconds in token->value.
 * Max representable: ~24.8 days (2147483647 ms)
 */
static bool lexer_read_time(st_lexer_t *lexer, st_token_t *token) {
  // Skip 'T' or 't'
  lexer_advance(lexer);

  // Expect '#'
  if (lexer->current_char != '#') {
    token->type = ST_TOK_ERROR;
    snprintf(token->value, sizeof(token->value), "Expected '#' after 'T' in TIME literal");
    return false;
  }
  lexer_advance(lexer);  // skip '#'

  int32_t total_ms = 0;
  bool has_value = false;

  // Parse components: [number][unit] where unit is d/h/m/s/ms
  while (isdigit(lexer->current_char)) {
    // Read number
    int32_t num = 0;
    while (isdigit(lexer->current_char)) {
      num = num * 10 + (lexer->current_char - '0');
      // Overflow check
      if (num < 0) {
        token->type = ST_TOK_ERROR;
        snprintf(token->value, sizeof(token->value), "TIME literal overflow");
        return false;
      }
      lexer_advance(lexer);
    }

    // Read unit (case-insensitive)
    char unit1 = tolower(lexer->current_char);
    char unit2 = tolower(lexer_peek(lexer, 1));

    if (unit1 == 'm' && unit2 == 's') {
      // Milliseconds
      total_ms += num;
      lexer_advance(lexer);  // skip 'm'
      lexer_advance(lexer);  // skip 's'
      has_value = true;
    } else if (unit1 == 's') {
      // Seconds → milliseconds
      total_ms += num * 1000;
      lexer_advance(lexer);  // skip 's'
      has_value = true;
    } else if (unit1 == 'm') {
      // Minutes → milliseconds
      total_ms += num * 60 * 1000;
      lexer_advance(lexer);  // skip 'm'
      has_value = true;
    } else if (unit1 == 'h') {
      // Hours → milliseconds
      total_ms += num * 60 * 60 * 1000;
      lexer_advance(lexer);  // skip 'h'
      has_value = true;
    } else if (unit1 == 'd') {
      // Days → milliseconds
      total_ms += num * 24 * 60 * 60 * 1000;
      lexer_advance(lexer);  // skip 'd'
      has_value = true;
    } else {
      // Unknown unit
      token->type = ST_TOK_ERROR;
      snprintf(token->value, sizeof(token->value),
               "Unknown TIME unit '%c' (expected d/h/m/s/ms)", lexer->current_char);
      return false;
    }

    // Overflow check after each component
    if (total_ms < 0) {
      token->type = ST_TOK_ERROR;
      snprintf(token->value, sizeof(token->value), "TIME literal overflow (max ~24.8 days)");
      return false;
    }
  }

  if (!has_value) {
    token->type = ST_TOK_ERROR;
    snprintf(token->value, sizeof(token->value), "Empty TIME literal (expected T#<value><unit>)");
    return false;
  }

  // Store as string representation of milliseconds
  snprintf(token->value, sizeof(token->value), "%ld", (long)total_ms);
  token->type = ST_TOK_TIME;
  return true;
}

/* ============================================================================
 * IDENTIFIER PARSING
 * ============================================================================ */

/* Parse identifier or keyword */
static bool lexer_read_identifier(st_lexer_t *lexer, st_token_t *token) {
  char buffer[64] = {0};
  int i = 0;

  // BUG-155 FIX: Read identifier with explicit length limit
  while ((isalnum(lexer->current_char) || lexer->current_char == '_') && i < 63) {
    buffer[i++] = lexer->current_char;
    lexer_advance(lexer);
  }

  // BUG-155 FIX: Check if identifier was truncated (more characters available)
  if (isalnum(lexer->current_char) || lexer->current_char == '_') {
    // Identifier exceeds maximum length - return error
    token->type = ST_TOK_ERROR;
    snprintf(token->value, sizeof(token->value), "Identifier too long (max 63 chars)");
    // Skip remaining characters
    while (isalnum(lexer->current_char) || lexer->current_char == '_') {
      lexer_advance(lexer);
    }
    return false;
  }

  buffer[i] = '\0';  // BUG-067: Explicit null terminator
  strncpy(token->value, buffer, 255);
  token->value[255] = '\0';

  // Check if it's a keyword
  st_token_type_t kw_type = lexer_lookup_keyword(buffer);
  token->type = kw_type;

  return true;
}

/* ============================================================================
 * TOKEN GENERATION
 * ============================================================================ */

bool st_lexer_next_token(st_lexer_t *lexer, st_token_t *token) {
  // Skip whitespace and comments
  while (1) {
    lexer_skip_whitespace(lexer);
    if (lexer->current_char == '(' && lexer_peek(lexer, 1) == '*') {
      // BUG-167 FIX: Check for unterminated comment
      if (!lexer_skip_comment(lexer)) {
        // Unterminated comment - return error token
        token->type = ST_TOK_ERROR;
        token->line = lexer->line;
        token->column = lexer->column;
        snprintf(token->value, sizeof(token->value), "Unterminated comment (* ... *)");
        return false;
      }
    } else {
      break;
    }
  }

  token->line = lexer->line;
  token->column = lexer->column;
  memset(token->value, 0, sizeof(token->value));

  // EOF
  if (lexer->current_char == '\0') {
    token->type = ST_TOK_EOF;
    return true;
  }

  // FEAT-006: TIME literals (T#5s, T#100ms, etc.) - check before identifiers
  if ((lexer->current_char == 'T' || lexer->current_char == 't') &&
      lexer_peek(lexer, 1) == '#') {
    return lexer_read_time(lexer, token);
  }

  // Identifiers and keywords
  if (isalpha(lexer->current_char) || lexer->current_char == '_') {
    return lexer_read_identifier(lexer, token);
  }

  // Numbers
  if (isdigit(lexer->current_char)) {
    // Check if it's a real (contains . or e/E later)
    // FEAT-004: '..' is range operator, not decimal point (e.g., 0..7)
    uint32_t temp_pos = lexer->pos;
    uint32_t temp_line = lexer->line;
    uint32_t temp_col = lexer->column;
    while (isdigit(lexer->current_char)) {
      lexer_advance(lexer);
    }
    bool is_real = false;
    if (lexer->current_char == '.' && lexer_peek(lexer, 1) != '.') {
      is_real = true;  // Decimal point, not range operator
    } else if (lexer->current_char == 'e' || lexer->current_char == 'E') {
      is_real = true;
    }
    lexer->pos = temp_pos;
    lexer->line = temp_line;
    lexer->column = temp_col;
    lexer->current_char = lexer->input[lexer->pos];

    if (is_real) {
      return lexer_read_real(lexer, token);
    } else {
      return lexer_read_integer(lexer, token);
    }
  }

  // Strings
  if (lexer->current_char == '\'' || lexer->current_char == '"') {
    return lexer_read_string(lexer, token);
  }

  // Two-character operators
  char two_char[3] = {lexer->current_char, lexer_peek(lexer, 1), '\0'};

  if (strcmp(two_char, ":=") == 0) {
    token->type = ST_TOK_ASSIGN;
    strncpy(token->value, ":=", 255);
    token->value[255] = '\0';
    lexer_advance(lexer);
    lexer_advance(lexer);
    return true;
  }
  if (strcmp(two_char, "<>") == 0) {
    token->type = ST_TOK_NE;
    strncpy(token->value, "<>", 255);
    token->value[255] = '\0';
    lexer_advance(lexer);
    lexer_advance(lexer);
    return true;
  }
  if (strcmp(two_char, "<=") == 0) {
    token->type = ST_TOK_LE;
    strncpy(token->value, "<=", 255);
    token->value[255] = '\0';
    lexer_advance(lexer);
    lexer_advance(lexer);
    return true;
  }
  if (strcmp(two_char, ">=") == 0) {
    token->type = ST_TOK_GE;
    strncpy(token->value, ">=", 255);
    token->value[255] = '\0';
    lexer_advance(lexer);
    lexer_advance(lexer);
    return true;
  }
  // FEAT-122: Output parameter arrow (=>) for FB output bindings
  if (strcmp(two_char, "=>") == 0) {
    token->type = ST_TOK_OUTPUT_ARROW;
    strncpy(token->value, "=>", 255);
    token->value[255] = '\0';
    lexer_advance(lexer);
    lexer_advance(lexer);
    return true;
  }
  if (strcmp(two_char, "**") == 0) {
    token->type = ST_TOK_POWER;
    strncpy(token->value, "**", 255);
    token->value[255] = '\0';
    lexer_advance(lexer);
    lexer_advance(lexer);
    return true;
  }
  // FEAT-004: Range operator (..) for ARRAY declarations
  if (strcmp(two_char, "..") == 0) {
    token->type = ST_TOK_DOTDOT;
    strncpy(token->value, "..", 255);
    token->value[255] = '\0';
    lexer_advance(lexer);
    lexer_advance(lexer);
    return true;
  }

  // Single-character operators and delimiters
  switch (lexer->current_char) {
    case '=':
      token->type = ST_TOK_EQ;
      token->value[0] = '=';
      lexer_advance(lexer);
      return true;
    case '<':
      token->type = ST_TOK_LT;
      token->value[0] = '<';
      lexer_advance(lexer);
      return true;
    case '>':
      token->type = ST_TOK_GT;
      token->value[0] = '>';
      lexer_advance(lexer);
      return true;
    case '+':
      token->type = ST_TOK_PLUS;
      token->value[0] = '+';
      lexer_advance(lexer);
      return true;
    case '-':
      token->type = ST_TOK_MINUS;
      token->value[0] = '-';
      lexer_advance(lexer);
      return true;
    case '*':
      token->type = ST_TOK_MUL;
      token->value[0] = '*';
      lexer_advance(lexer);
      return true;
    case '/':
      token->type = ST_TOK_DIV;
      token->value[0] = '/';
      lexer_advance(lexer);
      return true;
    case '(':
      token->type = ST_TOK_LPAREN;
      token->value[0] = '(';
      lexer_advance(lexer);
      return true;
    case ')':
      token->type = ST_TOK_RPAREN;
      token->value[0] = ')';
      lexer_advance(lexer);
      return true;
    case '[':
      token->type = ST_TOK_LBRACKET;
      token->value[0] = '[';
      lexer_advance(lexer);
      return true;
    case ']':
      token->type = ST_TOK_RBRACKET;
      token->value[0] = ']';
      lexer_advance(lexer);
      return true;
    case ';':
      token->type = ST_TOK_SEMICOLON;
      token->value[0] = ';';
      lexer_advance(lexer);
      return true;
    case ',':
      token->type = ST_TOK_COMMA;
      token->value[0] = ',';
      lexer_advance(lexer);
      return true;
    case ':':
      token->type = ST_TOK_COLON;
      token->value[0] = ':';
      lexer_advance(lexer);
      return true;
    default:
      token->type = ST_TOK_ERROR;
      snprintf(token->value, sizeof(token->value), "Unexpected character: '%c'", lexer->current_char);
      lexer_advance(lexer);
      return false;
  }
}

bool st_lexer_peek_token(st_lexer_t *lexer, st_token_t *token) {
  // Save current position
  uint32_t saved_pos = lexer->pos;
  uint32_t saved_line = lexer->line;
  uint32_t saved_column = lexer->column;
  char saved_char = lexer->current_char;

  // Get next token
  bool result = st_lexer_next_token(lexer, token);

  // Restore position
  lexer->pos = saved_pos;
  lexer->line = saved_line;
  lexer->column = saved_column;
  lexer->current_char = saved_char;

  return result;
}

/* ============================================================================
 * DEBUGGING SUPPORT
 * ============================================================================ */

const char *st_token_type_to_string(st_token_type_t type) {
  switch (type) {
    case ST_TOK_BOOL_TRUE:      return "BOOL_TRUE";
    case ST_TOK_BOOL_FALSE:     return "BOOL_FALSE";
    case ST_TOK_INT:            return "INT";
    case ST_TOK_REAL:           return "REAL";
    case ST_TOK_STRING:         return "STRING";
    case ST_TOK_IDENT:          return "IDENT";
    case ST_TOK_CONST:          return "CONST";
    case ST_TOK_BOOL:           return "BOOL";
    case ST_TOK_INT_KW:         return "INT_KW";
    case ST_TOK_DINT_KW:        return "DINT_KW";
    case ST_TOK_DWORD:          return "DWORD";
    case ST_TOK_REAL_KW:        return "REAL_KW";
    case ST_TOK_VAR:            return "VAR";
    case ST_TOK_VAR_INPUT:      return "VAR_INPUT";
    case ST_TOK_VAR_OUTPUT:     return "VAR_OUTPUT";
    case ST_TOK_VAR_IN_OUT:     return "VAR_IN_OUT";
    case ST_TOK_EXPORT:         return "EXPORT";
    case ST_TOK_IF:             return "IF";
    case ST_TOK_THEN:           return "THEN";
    case ST_TOK_ELSE:           return "ELSE";
    case ST_TOK_ELSIF:          return "ELSIF";
    case ST_TOK_END_IF:         return "END_IF";
    case ST_TOK_CASE:           return "CASE";
    case ST_TOK_OF:             return "OF";
    case ST_TOK_END_CASE:       return "END_CASE";
    case ST_TOK_FOR:            return "FOR";
    case ST_TOK_TO:             return "TO";
    case ST_TOK_BY:             return "BY";
    case ST_TOK_DO:             return "DO";
    case ST_TOK_END_FOR:        return "END_FOR";
    case ST_TOK_WHILE:          return "WHILE";
    case ST_TOK_END_WHILE:      return "END_WHILE";
    case ST_TOK_REPEAT:         return "REPEAT";
    case ST_TOK_UNTIL:          return "UNTIL";
    case ST_TOK_END_REPEAT:     return "END_REPEAT";
    case ST_TOK_EXIT:           return "EXIT";
    case ST_TOK_RETURN:         return "RETURN";
    case ST_TOK_PROGRAM:        return "PROGRAM";
    case ST_TOK_END_PROGRAM:    return "END_PROGRAM";
    case ST_TOK_BEGIN:          return "BEGIN";
    case ST_TOK_END:            return "END";
    case ST_TOK_FUNCTION:       return "FUNCTION";
    case ST_TOK_END_FUNCTION:   return "END_FUNCTION";
    case ST_TOK_FUNCTION_BLOCK: return "FUNCTION_BLOCK";
    case ST_TOK_END_FUNCTION_BLOCK: return "END_FUNCTION_BLOCK";
    case ST_TOK_ASSIGN:         return "ASSIGN";
    case ST_TOK_EQ:             return "EQ";
    case ST_TOK_NE:             return "NE";
    case ST_TOK_LT:             return "LT";
    case ST_TOK_GT:             return "GT";
    case ST_TOK_LE:             return "LE";
    case ST_TOK_GE:             return "GE";
    case ST_TOK_OUTPUT_ARROW:   return "OUTPUT_ARROW";
    case ST_TOK_TIME_KW:        return "TIME_KW";
    case ST_TOK_PLUS:           return "PLUS";
    case ST_TOK_MINUS:          return "MINUS";
    case ST_TOK_MUL:            return "MUL";
    case ST_TOK_DIV:            return "DIV";
    case ST_TOK_MOD:            return "MOD";
    case ST_TOK_AND:            return "AND";
    case ST_TOK_OR:             return "OR";
    case ST_TOK_NOT:            return "NOT";
    case ST_TOK_DOTDOT:         return "DOTDOT";
    case ST_TOK_ARRAY:          return "ARRAY";
    case ST_TOK_EOF:            return "EOF";
    case ST_TOK_ERROR:          return "ERROR";
    default:                    return "UNKNOWN";
  }
}
