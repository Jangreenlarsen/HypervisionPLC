/**
 * @file cli_shell.cpp
 * @brief CLI shell implementation - console I/O and command loop (LAYER 7)
 *
 * REFACTORED: Now uses Console abstraction instead of hardcoded Serial
 *
 * Handles:
 * - Non-blocking console input reading
 * - Command line buffering
 * - Prompt display
 * - Command execution via cli_parser
 * - Command history navigation (up/down arrows)
 * - Remote echo control
 *
 * All I/O is now done through Console* (can be Serial, Telnet, etc.)
 */

#include "cli_shell.h"
#include "cli_parser.h"
#include "cli_history.h"
#include <Arduino.h>
#include <string.h>

/* Forward declarations */
extern void cli_parser_execute_st_upload(uint8_t program_id, const char* source_code);

/* ============================================================================
 * GLOBAL DEBUG CONSOLE
 * ============================================================================ */

static Console *g_debug_console = NULL;

void cli_shell_set_debug_console(Console *console) {
  g_debug_console = console;
}

Console* cli_shell_get_debug_console(void) {
  return g_debug_console;
}

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * Trim whitespace from both ends of a string (in-place)
 * @param str Null-terminated string to trim
 * @param len Pointer to string length (will be updated)
 */
static void cli_trim_string(char* str, uint16_t* len) {
  if (!str || !len || *len == 0) return;

  // Trim from end
  while (*len > 0 && (str[*len - 1] == ' ' || str[*len - 1] == '\t')) {
    str[*len - 1] = '\0';
    (*len)--;
  }

  // Trim from start
  uint16_t start = 0;
  while (start < *len && (str[start] == ' ' || str[start] == '\t')) {
    start++;
  }

  if (start > 0) {
    // Shift remaining string to the beginning
    for (uint16_t i = start; i < *len; i++) {
      str[i - start] = str[i];
    }
    *len -= start;
    str[*len] = '\0';
  }
}

/* ============================================================================
 * INPUT BUFFER & STATE
 * ============================================================================ */

#define CLI_INPUT_BUFFER_SIZE 256
#define CLI_UPLOAD_BUFFER_SIZE 5000

#define CLI_MODE_NORMAL 0
#define CLI_MODE_ST_UPLOAD 1

static char cli_input_buffer[CLI_INPUT_BUFFER_SIZE];
static uint16_t cli_input_pos = 0;          // Length of input
static uint16_t cli_cursor_pos = 0;         // Cursor position (0 = start)
static uint8_t cli_initialized = 0;
static uint8_t cli_remote_echo_enabled = 1;  // Default: echo enabled
static uint8_t cli_escape_seq_state = 0;    // For parsing arrow keys

// Multi-line ST Logic Upload Mode
static uint8_t cli_mode = CLI_MODE_NORMAL;
static uint8_t cli_upload_program_id = 0xFF;
// Dynamic allocation: upload buffer only needed during upload mode (~5 KB saved)
static char *cli_upload_buffer = NULL;
static uint16_t cli_upload_buffer_pos = 0;

/* ============================================================================
 * CONSOLE OUTPUT HELPERS
 * ============================================================================ */

static void cli_console_print(Console *console, const char *str) {
  if (console && console->write_str) {
    console->write_str(console->context, str);
  }
}

static void cli_console_println(Console *console, const char *str) {
  if (console && console->write_line) {
    console->write_line(console->context, str);
  }
}

static void cli_console_putchar(Console *console, char ch) {
  if (console && console->write_char) {
    console->write_char(console->context, ch);
  }
}

static void cli_console_flush(Console *console) {
  if (console && console->flush) {
    console->flush(console->context);
  }
}

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

void cli_shell_init(Console *console) {
  if (cli_initialized) return;

  cli_history_init();

  // Set as debug console
  g_debug_console = console;

  cli_console_println(console, "\nModbus CLI Ready. Type 'help' for commands.\n");
  cli_console_print(console, "> ");

  cli_input_pos = 0;
  cli_cursor_pos = 0;
  memset(cli_input_buffer, 0, CLI_INPUT_BUFFER_SIZE);
  cli_escape_seq_state = 0;
  cli_initialized = 1;
}

/* ============================================================================
 * MULTI-LINE ST UPLOAD MODE CONTROL
 * ============================================================================ */

void cli_shell_start_st_upload(uint8_t program_id) {
  // Allocate upload buffer dynamically (only needed during upload)
  if (!cli_upload_buffer) {
    cli_upload_buffer = (char *)malloc(CLI_UPLOAD_BUFFER_SIZE);
    if (!cli_upload_buffer) {
      if (g_debug_console) {
        cli_console_println(g_debug_console, "ERROR: Insufficient heap for upload buffer");
      }
      return;
    }
  }
  cli_mode = CLI_MODE_ST_UPLOAD;
  cli_upload_program_id = program_id;
  cli_upload_buffer_pos = 0;
  memset(cli_upload_buffer, 0, CLI_UPLOAD_BUFFER_SIZE);

  // Clear input buffer for next command entry
  cli_input_pos = 0;
  cli_cursor_pos = 0;
  memset(cli_input_buffer, 0, CLI_INPUT_BUFFER_SIZE);

  // Note: Echo setting is now preserved in upload mode
  // Users can control echo with "set echo on/off" command

  if (g_debug_console) {
    cli_console_println(g_debug_console, "Entering ST Logic upload mode. Type code and end with 'END_UPLOAD':");
    cli_console_print(g_debug_console, ">>> ");
  }
}

void cli_shell_reset_upload_mode(void) {
  cli_mode = CLI_MODE_NORMAL;
  cli_upload_program_id = 0xFF;
  cli_upload_buffer_pos = 0;
  // Free dynamically allocated upload buffer
  if (cli_upload_buffer) {
    free(cli_upload_buffer);
    cli_upload_buffer = NULL;
  }

  // Note: Echo setting is preserved (not modified by upload mode)

  if (g_debug_console) {
    cli_console_print(g_debug_console, "> ");
  }
}

uint8_t cli_shell_is_in_upload_mode(void) {
  return (cli_mode == CLI_MODE_ST_UPLOAD);
}

void cli_shell_feed_upload_line(Console *console, const char *line) {
  if (cli_mode != CLI_MODE_ST_UPLOAD) {
    return;  // Not in upload mode, ignore
  }

  if (!line) {
    return;
  }

  // Set debug console temporarily
  Console *prev_debug_console = g_debug_console;
  g_debug_console = console;

  // Create a trimmed copy for END_UPLOAD check
  char trimmed_check[256];
  uint16_t check_len = strlen(line);
  if (check_len >= sizeof(trimmed_check)) {
    check_len = sizeof(trimmed_check) - 1;
  }
  strncpy(trimmed_check, line, check_len);
  trimmed_check[check_len] = '\0';
  cli_trim_string(trimmed_check, &check_len);

  if (strcasecmp(trimmed_check, "END_UPLOAD") == 0) {
    // End of upload - compile and return to normal mode
    cli_console_println(console, "Compiling ST Logic program...");

    // Null-terminate the upload buffer (CRITICAL: always terminate)
    if (cli_upload_buffer_pos < CLI_UPLOAD_BUFFER_SIZE) {
      cli_upload_buffer[cli_upload_buffer_pos] = '\0';
    } else {
      cli_upload_buffer[CLI_UPLOAD_BUFFER_SIZE - 1] = '\0';
    }

    // Execute upload with collected code
    cli_parser_execute_st_upload(cli_upload_program_id, cli_upload_buffer);

    // Reset to normal mode
    cli_shell_reset_upload_mode();
  } else {
    // Add line to upload buffer (with newline)
    uint16_t line_len = strlen(line);
    if (cli_upload_buffer_pos + line_len + 1 < CLI_UPLOAD_BUFFER_SIZE) {
      // Copy line
      strncpy(&cli_upload_buffer[cli_upload_buffer_pos], line, line_len);
      cli_upload_buffer_pos += line_len;

      // Add newline
      cli_upload_buffer[cli_upload_buffer_pos] = '\n';
      cli_upload_buffer_pos++;
    } else {
      cli_console_println(console, "ERROR: Upload buffer full!");
      cli_shell_reset_upload_mode();
    }
  }

  // Restore previous debug console
  g_debug_console = prev_debug_console;
}

/* ============================================================================
 * REMOTE ECHO CONTROL
 * ============================================================================ */

void cli_shell_set_remote_echo(uint8_t enabled) {
  cli_remote_echo_enabled = enabled ? 1 : 0;
}

uint8_t cli_shell_get_remote_echo(void) {
  return cli_remote_echo_enabled;
}

/* ============================================================================
 * HELPER FUNCTIONS
 * ============================================================================ */

static void cli_redraw_line(Console *console) {
  // Choose prompt based on mode
  const char* prompt = (cli_mode == CLI_MODE_ST_UPLOAD) ? ">>> " : "> ";

  // Move to start of line after prompt
  cli_console_putchar(console, '\r');  // Carriage return
  cli_console_print(console, prompt);

  // Clear rest of line with spaces
  for (uint16_t i = 0; i < CLI_INPUT_BUFFER_SIZE - 10; i++) {
    cli_console_putchar(console, ' ');
  }

  // Go back to start of input area
  cli_console_putchar(console, '\r');
  cli_console_print(console, prompt);

  // Display buffer content
  if (cli_input_pos > 0) {
    for (uint16_t i = 0; i < cli_input_pos; i++) {
      cli_console_putchar(console, cli_input_buffer[i]);
    }
  }

  // Position cursor at correct location
  if (cli_cursor_pos < cli_input_pos) {
    // Move cursor back to position
    uint16_t move_back = cli_input_pos - cli_cursor_pos;
    for (uint16_t i = 0; i < move_back; i++) {
      cli_console_putchar(console, '\b');  // Backspace
    }
  }

  cli_console_flush(console);
}

static void cli_insert_char_at_cursor(Console *console, char c) {
  if (cli_input_pos >= CLI_INPUT_BUFFER_SIZE - 1) {
    return;  // Buffer full
  }

  if (!cli_remote_echo_enabled) {
    // Simple case: no echo, just add to buffer
    if (cli_cursor_pos < cli_input_pos) {
      // Shift characters forward
      for (uint16_t i = cli_input_pos; i > cli_cursor_pos; i--) {
        cli_input_buffer[i] = cli_input_buffer[i - 1];
      }
    }
    cli_input_buffer[cli_cursor_pos] = c;
    cli_input_pos++;
    cli_cursor_pos++;
    return;
  }

  // Echo enabled: need to redraw
  if (cli_cursor_pos == cli_input_pos) {
    // Simple case: cursor at end, just append
    cli_input_buffer[cli_cursor_pos] = c;
    cli_input_pos++;
    cli_cursor_pos++;
    cli_console_putchar(console, c);
    cli_console_flush(console);
  } else {
    // Complex case: cursor in middle, need to redraw
    // Shift characters forward
    for (uint16_t i = cli_input_pos; i > cli_cursor_pos; i--) {
      cli_input_buffer[i] = cli_input_buffer[i - 1];
    }
    cli_input_buffer[cli_cursor_pos] = c;
    cli_input_pos++;
    cli_cursor_pos++;

    // Redraw from current position to end
    for (uint16_t i = cli_cursor_pos - 1; i < cli_input_pos; i++) {
      cli_console_putchar(console, cli_input_buffer[i]);
    }

    // Move cursor back to right position
    uint16_t move_back = cli_input_pos - cli_cursor_pos;
    for (uint16_t i = 0; i < move_back; i++) {
      cli_console_putchar(console, '\b');
    }
    cli_console_flush(console);
  }
}

/* ============================================================================
 * MAIN LOOP
 * ============================================================================ */

void cli_shell_loop(Console *console) {
  if (!console) return;

  // Non-blocking console input reading
  char c;
  while (console_getchar(console, &c) > 0) {
    // ========================================================================
    // ESCAPE SEQUENCE PARSING FOR ARROW KEYS
    // ========================================================================

    if (cli_escape_seq_state == 1) {
      // We received ESC, now expecting [
      if (c == '[') {
        cli_escape_seq_state = 2;
        continue;
      } else {
        // Invalid sequence, reset state and process this character
        cli_escape_seq_state = 0;
        // Fall through to normal character processing
      }
    } else if (cli_escape_seq_state == 2) {
      // We received ESC[, now expecting arrow key (A, B, C, D)
      cli_escape_seq_state = 0;

      if (c == 'A') {
        // Up arrow - get previous command
        const char* prev_cmd = cli_history_get_prev();
        if (prev_cmd != NULL && prev_cmd[0] != '\0') {
          cli_redraw_line(console);  // Clear current line
          memset(cli_input_buffer, 0, CLI_INPUT_BUFFER_SIZE);
          strncpy(cli_input_buffer, prev_cmd, CLI_INPUT_BUFFER_SIZE - 1);
          cli_input_pos = strlen(cli_input_buffer);
          cli_cursor_pos = cli_input_pos;  // Cursor at end
          cli_redraw_line(console);
        }
        continue;
      } else if (c == 'B') {
        // Down arrow - get next command
        const char* next_cmd = cli_history_get_next();
        if (next_cmd != NULL) {
          cli_redraw_line(console);  // Clear current line
          memset(cli_input_buffer, 0, CLI_INPUT_BUFFER_SIZE);
          if (next_cmd[0] != '\0') {
            strncpy(cli_input_buffer, next_cmd, CLI_INPUT_BUFFER_SIZE - 1);
            cli_input_pos = strlen(cli_input_buffer);
          } else {
            cli_input_pos = 0;
          }
          cli_cursor_pos = cli_input_pos;  // Cursor at end
          cli_redraw_line(console);
        }
        continue;
      } else if (c == 'C') {
        // Right arrow - move cursor forward
        if (cli_cursor_pos < cli_input_pos) {
          cli_cursor_pos++;
        }
        // Send control sequence back
        cli_console_putchar(console, 0x1B);
        cli_console_putchar(console, '[');
        cli_console_putchar(console, 'C');
        cli_console_flush(console);
        continue;
      } else if (c == 'D') {
        // Left arrow - move cursor backward
        if (cli_cursor_pos > 0) {
          cli_cursor_pos--;
        }
        // Send control sequence back
        cli_console_putchar(console, 0x1B);
        cli_console_putchar(console, '[');
        cli_console_putchar(console, 'D');
        cli_console_flush(console);
        continue;
      }
      // For any other character, fall through to normal processing
    }

    // Check if this is start of escape sequence
    if (c == 0x1B) {
      cli_escape_seq_state = 1;
      continue;
    }

    // ========================================================================
    // NORMAL CHARACTER PROCESSING
    // ========================================================================

    if (c == '\r' || c == '\n') {
      // End of line
      cli_input_buffer[cli_input_pos] = '\0';

      if (cli_remote_echo_enabled) {
        cli_console_println(console, "");  // Echo newline
      } else {
        cli_console_putchar(console, '\n');  // Just newline, no echo
      }

      // ====================================================================
      // ST LOGIC UPLOAD MODE
      // ====================================================================
      if (cli_mode == CLI_MODE_ST_UPLOAD) {
        // Check for END_UPLOAD command (case-insensitive)
        // Create a trimmed copy just for comparison (don't modify original source code!)
        char trimmed_check[256];
        uint16_t check_len = cli_input_pos;
        strncpy(trimmed_check, cli_input_buffer, cli_input_pos);
        trimmed_check[cli_input_pos] = '\0';
        cli_trim_string(trimmed_check, &check_len);

        if (strcasecmp(trimmed_check, "END_UPLOAD") == 0) {
          // End of upload - compile and return to normal mode
          cli_console_println(console, "Compiling ST Logic program...");

          // Null-terminate the upload buffer (CRITICAL: always terminate)
          if (cli_upload_buffer_pos < CLI_UPLOAD_BUFFER_SIZE) {
            cli_upload_buffer[cli_upload_buffer_pos] = '\0';
          } else {
            cli_upload_buffer[CLI_UPLOAD_BUFFER_SIZE - 1] = '\0';
          }

          // Execute upload with collected code
          cli_parser_execute_st_upload(cli_upload_program_id, cli_upload_buffer);

          // Reset to normal mode
          cli_shell_reset_upload_mode();
        } else if (cli_input_pos > 0) {
          // Add line to upload buffer (with newline)
          if (cli_upload_buffer_pos + cli_input_pos + 1 < CLI_UPLOAD_BUFFER_SIZE) {
            // Copy line
            strncpy(&cli_upload_buffer[cli_upload_buffer_pos],
                    cli_input_buffer,
                    cli_input_pos);
            cli_upload_buffer_pos += cli_input_pos;

            // Add newline
            cli_upload_buffer[cli_upload_buffer_pos] = '\n';
            cli_upload_buffer_pos++;

            cli_console_print(console, ">>> ");  // Show upload prompt for next line
          } else {
            cli_console_println(console, "ERROR: Upload buffer full!");
            cli_shell_reset_upload_mode();
          }
        } else {
          // Empty line in upload mode - just show prompt again
          cli_console_print(console, ">>> ");
        }

        // Reset input buffer for next line
        cli_input_pos = 0;
        cli_cursor_pos = 0;
        memset(cli_input_buffer, 0, CLI_INPUT_BUFFER_SIZE);
      }
      // ====================================================================
      // NORMAL COMMAND MODE
      // ====================================================================
      else {
        if (cli_input_pos > 0) {
          // Trim whitespace from input
          cli_trim_string(cli_input_buffer, &cli_input_pos);

          // Store in history and execute (if not empty after trim)
          if (cli_input_pos > 0) {
            cli_history_add(cli_input_buffer);
            cli_history_reset_nav();
            cli_parser_execute(cli_input_buffer);
          }
        }

        // Reset buffer and show new prompt
        cli_input_pos = 0;
        cli_cursor_pos = 0;
        memset(cli_input_buffer, 0, CLI_INPUT_BUFFER_SIZE);
        cli_console_print(console, "> ");
      }
    }
    else if (c == 0x08 || c == 0x7F) {
      // Backspace - delete character before cursor
      if (cli_cursor_pos > 0) {
        cli_cursor_pos--;

        // Shift characters back
        for (uint16_t i = cli_cursor_pos; i < cli_input_pos - 1; i++) {
          cli_input_buffer[i] = cli_input_buffer[i + 1];
        }
        cli_input_pos--;
        cli_input_buffer[cli_input_pos] = '\0';

        if (cli_remote_echo_enabled) {
          // Send backspace to terminal
          cli_console_putchar(console, '\b');
          cli_console_putchar(console, ' ');
          cli_console_putchar(console, '\b');

          // If there are characters after cursor, redraw them
          if (cli_cursor_pos < cli_input_pos) {
            for (uint16_t i = cli_cursor_pos; i < cli_input_pos; i++) {
              cli_console_putchar(console, cli_input_buffer[i]);
            }
            cli_console_putchar(console, ' ');  // Clear the last position

            // Move cursor back to original position
            uint16_t move_back = cli_input_pos - cli_cursor_pos + 1;
            for (uint16_t i = 0; i < move_back; i++) {
              cli_console_putchar(console, '\b');
            }
          }
          cli_console_flush(console);
        }
      }
    }
    else if (c >= 32 && c < 127) {
      // Printable character - insert at cursor position
      cli_insert_char_at_cursor(console, c);
    }
    // Ignore all other characters (control codes, etc.)
  }
}

/* ============================================================================
 * EXTERNAL COMMAND EXECUTION (Telnet, Remote, etc.)
 * ============================================================================ */

void cli_shell_execute_command(Console *console, const char *cmd) {
  if (!cmd || cmd[0] == '\0') {
    return;
  }

  // Copy command to a local buffer for processing (trim, etc.)
  char cmd_buffer[CLI_INPUT_BUFFER_SIZE];
  strncpy(cmd_buffer, cmd, sizeof(cmd_buffer) - 1);
  cmd_buffer[sizeof(cmd_buffer) - 1] = '\0';

  // Trim whitespace
  uint16_t cmd_len = strlen(cmd_buffer);
  cli_trim_string(cmd_buffer, &cmd_len);

  // Execute only if not empty
  if (cmd_len > 0) {
    /* BUG-344: vaern mod reentrant kommando-eksekvering.
     *
     * Opdaget da en `mb scan` startede sig selv igen og igen (scan-headeren
     * printet 5 gange i traek) og til sidst tog telnet-sessionen med sig:
     * telnet_server_loop() eksekverer selv CLI-kommandoer (telnet_server.cpp
     * :984) og nulstiller foerst `input_ready` EFTER kommandoen returnerer.
     * Kaldte en langvarig kommando saa noget der endte i telnet_server_loop()
     * igen, saa den samme kommando stadig som "klar" og startede den forfra —
     * inden i sig selv. Hver runde aad stak, indtil tasken doede.
     *
     * Det samme gaelder paa tvaers af tasks: web-CLI'ens handler kalder denne
     * funktion fra httpd-taskens kontekst, mens seriel/telnet kan koere en
     * kommando i loopTask. To samtidige eksekveringer deler den GLOBALE
     * g_debug_console — den ene ville kapre den andens output midtvejs.
     *
     * Derfor: kun én kommando ad gangen. Naegtes hoefligt i stedet for at
     * fejle uforudsigeligt. `volatile` + tjek-og-saet er tilstraekkeligt her:
     * begge veje ind koerer i task-kontekst (ikke ISR), og det vaerste et
     * teoretisk kapløb kan koste er én afvist kommando. */
    static volatile bool cmd_in_progress = false;
    if (cmd_in_progress) {
      if (console && console->write_line) {
        console->write_line(console->context,
          "OPTAGET: en anden CLI-kommando koerer allerede (se BUG-344)");
      }
      return;
    }
    cmd_in_progress = true;

    // Temporarily set debug console to this console
    Console *prev_debug_console = g_debug_console;
    g_debug_console = console;

    cli_history_add(cmd_buffer);
    cli_history_reset_nav();
    cli_parser_execute(cmd_buffer);

    // Restore previous debug console
    g_debug_console = prev_debug_console;
    cmd_in_progress = false;
  }
}
