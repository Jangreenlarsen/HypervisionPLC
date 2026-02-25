/**
 * @file telnet_server.cpp
 * @brief Telnet protocol server implementation
 *
 * Handles Telnet IAC protocol, line buffering, and basic option negotiation.
 */

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <esp_log.h>
#include <Arduino.h>

#include "telnet_server.h"
#include "constants.h"
#include "types.h"  // For NetworkConfig
#include "config_struct.h"  // For g_persist_config (hostname + live credentials)
#include "console.h"
#include "console_telnet.h"
#include "cli_shell.h"  // For cli_shell_execute_command()
#include "cli_history.h"  // For command history (up/down arrow support)

static const char *TAG = "TELNET_SRV";

/* Telnet command macros */
#define TELNET_CMD_NULL     0
#define TELNET_CMD_SE       240    // Subnegotiation End
#define TELNET_CMD_NOP      241    // No Operation
#define TELNET_CMD_DM       242    // Data Mark
#define TELNET_CMD_BRK      243    // Break
#define TELNET_CMD_IP       244    // Interrupt Process
#define TELNET_CMD_AO       245    // Abort Output
#define TELNET_CMD_AYT      246    // Are You There
#define TELNET_CMD_EC       247    // Erase Character
#define TELNET_CMD_EL       248    // Erase Line
#define TELNET_CMD_GA       249    // Go Ahead
#define TELNET_CMD_SB       250    // Subnegotiation Begin
#define TELNET_CMD_WILL     251
#define TELNET_CMD_WONT     252
#define TELNET_CMD_DO       253
#define TELNET_CMD_DONT     254
#define TELNET_CMD_IAC      255

/* ============================================================================
 * INITIALIZATION & CONTROL
 * ============================================================================ */

TelnetServer* telnet_server_create(uint16_t port, NetworkConfig *network_config)
{
  TelnetServer *server = (TelnetServer*)malloc(sizeof(TelnetServer));
  if (!server) {
    ESP_LOGE(TAG, "Failed to allocate Telnet server");
    return NULL;
  }

  memset(server, 0, sizeof(TelnetServer));

  server->tcp_server = tcp_server_create(port);
  if (!server->tcp_server) {
    free(server);
    return NULL;
  }

  server->port = port;
  server->network_config = network_config;  // Store config pointer
  server->parse_state = TELNET_STATE_NONE;
  server->escape_seq_state = 0;             // Initialize escape sequence state
  server->input_pos = 0;
  server->cursor_pos = 0;                   // Initialize cursor position
  server->input_ready = 0;
  server->echo_enabled = 1;
  server->linemode_enabled = 1;

  // SECURITY: Initialize authentication (enabled by default)
  server->auth_required = 1;
  server->auth_state = TELNET_AUTH_WAITING;
  server->auth_attempts = 0;
  server->auth_lockout_time = 0;
  memset(server->auth_username, 0, sizeof(server->auth_username));

  ESP_LOGI(TAG, "Telnet server created for port %d", port);
  return server;
}

int telnet_server_start(TelnetServer *server)
{
  if (!server || !server->tcp_server) {
    return -1;
  }

  int ret = tcp_server_start(server->tcp_server);
  if (ret != 0) {
    return ret;
  }

  ESP_LOGI(TAG, "Telnet server started");
  return 0;
}

int telnet_server_stop(TelnetServer *server)
{
  if (!server || !server->tcp_server) {
    return -1;
  }

  tcp_server_stop(server->tcp_server);
  ESP_LOGI(TAG, "Telnet server stopped");
  return 0;
}

void telnet_server_destroy(TelnetServer *server)
{
  if (!server) {
    return;
  }

  telnet_server_stop(server);
  if (server->tcp_server) {
    tcp_server_destroy(server->tcp_server);
  }
  free(server);
}

/* ============================================================================
 * CLIENT MANAGEMENT
 * ============================================================================ */

uint8_t telnet_server_client_connected(const TelnetServer *server)
{
  if (!server) {
    return 0;
  }

  return tcp_server_client_is_connected(server->tcp_server, 0);
}

int telnet_server_disconnect_client(TelnetServer *server)
{
  if (!server || !server->tcp_server) {
    return -1;
  }

  server->input_pos = 0;
  server->cursor_pos = 0;
  server->input_ready = 0;
  server->parse_state = TELNET_STATE_NONE;

  return tcp_server_disconnect_client(server->tcp_server, 0);
}

/* ============================================================================
 * TELNET IAC PROTOCOL HANDLING
 * ============================================================================ */

static void telnet_send_iac_command(TelnetServer *server, uint8_t cmd, uint8_t opt)
{
  uint8_t buf[3] = {TELNET_CMD_IAC, cmd, opt};
  tcp_server_send(server->tcp_server, 0, buf, 3);
}

static void telnet_handle_iac_command(TelnetServer *server, uint8_t cmd, uint8_t opt)
{
  switch (cmd) {
    case TELNET_CMD_WILL:
      // Client says it will enable option
      if (opt == TELNET_OPT_ECHO) {
        server->options.client_echo = 1;
        telnet_send_iac_command(server, TELNET_CMD_DO, TELNET_OPT_ECHO);
      } else if (opt == TELNET_OPT_SUPPRESS_GA) {
        server->options.client_suppress_ga = 1;
        telnet_send_iac_command(server, TELNET_CMD_DO, TELNET_OPT_SUPPRESS_GA);
      }
      break;

    case TELNET_CMD_WONT:
      // Client says it won't enable option
      if (opt == TELNET_OPT_ECHO) {
        server->options.client_echo = 0;
        telnet_send_iac_command(server, TELNET_CMD_DONT, TELNET_OPT_ECHO);
      } else if (opt == TELNET_OPT_SUPPRESS_GA) {
        server->options.client_suppress_ga = 0;
        telnet_send_iac_command(server, TELNET_CMD_DONT, TELNET_OPT_SUPPRESS_GA);
      }
      break;

    case TELNET_CMD_DO:
      // Client asks server to enable option
      if (opt == TELNET_OPT_ECHO) {
        server->options.server_echo = 1;
        telnet_send_iac_command(server, TELNET_CMD_WILL, TELNET_OPT_ECHO);
      } else if (opt == TELNET_OPT_SUPPRESS_GA) {
        server->options.server_suppress_ga = 1;
        telnet_send_iac_command(server, TELNET_CMD_WILL, TELNET_OPT_SUPPRESS_GA);
      }
      break;

    case TELNET_CMD_DONT:
      // Client asks server not to enable option
      if (opt == TELNET_OPT_ECHO) {
        server->options.server_echo = 0;
        telnet_send_iac_command(server, TELNET_CMD_WONT, TELNET_OPT_ECHO);
      } else if (opt == TELNET_OPT_SUPPRESS_GA) {
        server->options.server_suppress_ga = 0;
        telnet_send_iac_command(server, TELNET_CMD_WONT, TELNET_OPT_SUPPRESS_GA);
      }
      break;

    default:
      break;
  }
}

/* ============================================================================
 * AUTHENTICATION (v3.0+)
 * ============================================================================ */

// Authentication configuration
#define TELNET_MAX_AUTH_ATTEMPTS 3
#define TELNET_LOCKOUT_TIME_MS 30000  // 30 second lockout
// Credentials now come from NetworkConfig in telnet_server->network_config

static void telnet_send_auth_prompt(TelnetServer *server) {
  if (server->auth_state == TELNET_AUTH_WAITING) {
    // USERNAME: Enable server echo
    server->echo_enabled = 1;

    // Full banner only for initial prompt
    telnet_server_writeline(server, "");
    const char *hn = g_persist_config.hostname[0] ? g_persist_config.hostname : "modbus-esp32";
    telnet_server_writelinef(server, "=== %s - Telnet Srv ===", hn);
    telnet_server_writeline(server, "LOGIN REQUIRED");
    telnet_server_writeline(server, "");

    telnet_server_write(server, "Username: ");

    // CRITICAL: Force flush to ensure prompt reaches client
    delay(10);  // Small delay to ensure TCP packet is sent
  } else if (server->auth_state == TELNET_AUTH_USERNAME) {
    // PASSWORD: DISABLE server echo (security - password must not be visible!)
    server->echo_enabled = 0;

    telnet_server_write(server, "Password: ");
  }
}

// Simpler version that just sends the prompt without full banner
static void telnet_send_retry_prompt(TelnetServer *server) {
  if (server->auth_state == TELNET_AUTH_WAITING) {
    // Enable SERVER echo for username retry
    server->echo_enabled = 1;  // SERVER WILL ECHO
    telnet_server_write(server, "Username: ");
    delay(10);  // Ensure prompt reaches client
  } else if (server->auth_state == TELNET_AUTH_USERNAME) {
    // Disable SERVER echo for password retry
    server->echo_enabled = 0;  // SERVER WONT ECHO (password invisible)
    telnet_server_write(server, "Password: ");
    delay(10);  // Ensure prompt reaches client
  }
}

/**
 * @brief Trim leading and trailing whitespace from string (in-place)
 */
static void trim_string(char *str) {
  if (!str || !*str) return;

  // Trim trailing whitespace
  char *end = str + strlen(str) - 1;
  while (end > str && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
    *end = '\0';
    end--;
  }

  // Trim leading whitespace (shift string left)
  char *start = str;
  while (*start && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')) {
    start++;
  }

  if (start != str) {
    memmove(str, start, strlen(start) + 1);
  }
}

static void telnet_handle_auth_input(TelnetServer *server, const char *input) {
  if (!input || !server) return;

  // Trim whitespace from input (copy to avoid modifying const)
  char trimmed_input[256];
  strncpy(trimmed_input, input, sizeof(trimmed_input) - 1);
  trimmed_input[sizeof(trimmed_input) - 1] = '\0';
  trim_string(trimmed_input);

  // Check if in lockout period
  if (server->auth_lockout_time > 0) {
    if (millis() < server->auth_lockout_time) {
      telnet_server_writeline(server, "ERROR: Too many failed attempts. Please try again later.");
      server->input_pos = 0;
      server->cursor_pos = 0;
      server->input_ready = 0;
      return;
    } else {
      // Lockout expired
      server->auth_lockout_time = 0;
      server->auth_attempts = 0;
      telnet_send_auth_prompt(server);
      server->input_pos = 0;
      server->cursor_pos = 0;
      server->input_ready = 0;
      return;
    }
  }

  if (server->auth_state == TELNET_AUTH_WAITING) {
    // Username entry
    strncpy(server->auth_username, trimmed_input, sizeof(server->auth_username) - 1);
    server->auth_username[sizeof(server->auth_username) - 1] = '\0';
    server->auth_state = TELNET_AUTH_USERNAME;
    telnet_send_auth_prompt(server);
  } else if (server->auth_state == TELNET_AUTH_USERNAME) {
    // Password entry
    // Use credentials from live config (always up-to-date, even after 'set telnet' without reboot)
    const char *expected_user = g_persist_config.network.telnet_username[0] ? g_persist_config.network.telnet_username : "admin";
    const char *expected_pass = g_persist_config.network.telnet_password[0] ? g_persist_config.network.telnet_password : "telnet123";

    // DEBUG: Log password validation attempt (temporary - can be removed later)
    ESP_LOGI(TAG, "Auth attempt: user='%s' (expected='%s'), pass_len=%d (expected_len=%d)",
             server->auth_username, expected_user, strlen(trimmed_input), strlen(expected_pass));

    if (strcmp(server->auth_username, expected_user) == 0 &&
        strcmp(trimmed_input, expected_pass) == 0) {
      // Authentication successful!
      server->auth_state = TELNET_AUTH_AUTHENTICATED;
      server->auth_attempts = 0;

      // Re-enable SERVER echo for normal CLI interaction
      server->echo_enabled = 1;  // SERVER WILL ECHO

      telnet_server_writeline(server, "");
      telnet_server_writeline(server, "Authentication successful. Welcome!");
      telnet_server_writeline(server, "");

      // Send initial CLI prompt
      telnet_server_write(server, "> ");
    } else {
      // Authentication failed
      server->auth_attempts++;
      telnet_server_writeline(server, "");
      telnet_server_writeline(server, "ERROR: Invalid username or password");

      if (server->auth_attempts >= TELNET_MAX_AUTH_ATTEMPTS) {
        // Trigger lockout
        server->auth_lockout_time = millis() + TELNET_LOCKOUT_TIME_MS;
        telnet_server_writeline(server, "Too many failed attempts. Locking out for 30 seconds.");
        telnet_server_writeline(server, "");
      } else {
        // Reset to username prompt (show simple retry prompt, not full banner)
        server->auth_state = TELNET_AUTH_WAITING;
        memset(server->auth_username, 0, sizeof(server->auth_username));

        // telnet_send_retry_prompt() will set server->echo_enabled = 1
        telnet_send_retry_prompt(server);
      }
    }
  }

  server->input_pos = 0;
  server->cursor_pos = 0;
  server->input_ready = 0;
}

/* ============================================================================
 * INPUT PROCESSING
 * ============================================================================ */

// Store for IAC state machine
static uint8_t telnet_iac_cmd = 0;

static void telnet_process_input(TelnetServer *server, uint8_t byte)
{
  switch (server->parse_state) {
    case TELNET_STATE_NONE:
      // ====================================================================
      // ARROW KEY / ESCAPE SEQUENCE HANDLING
      // ====================================================================
      if (server->escape_seq_state == 1) {
        // We received ESC, now expecting [
        if (byte == '[') {
          server->escape_seq_state = 2;
          return;
        } else {
          // Invalid escape sequence - just silently discard
          server->escape_seq_state = 0;
          return;  // Don't process this byte as normal input
        }
      }

      if (server->escape_seq_state == 2) {
        // We received ESC[, now expecting arrow key (A, B, C, D)
        server->escape_seq_state = 0;

        if (byte == 'A') {
          // Up arrow - get previous command from history
          const char* prev_cmd = cli_history_get_prev();
          if (prev_cmd != NULL && prev_cmd[0] != '\0') {
            // Save old length for clearing
            uint16_t old_len = server->input_pos;

            // Clear current line and redraw with history command
            memset(server->input_buffer, 0, TELNET_INPUT_BUFFER_SIZE);
            strncpy(server->input_buffer, prev_cmd, TELNET_INPUT_BUFFER_SIZE - 1);
            server->input_pos = strlen(server->input_buffer);
            server->cursor_pos = server->input_pos;  // Cursor at end of recalled command

            // Redraw line using compatible method (no ANSI codes)
            if (server->echo_enabled) {
              // 1. Return to start of line
              telnet_server_write(server, "\r> ");
              // 2. Print new command
              telnet_server_write(server, server->input_buffer);
              // 3. Clear remaining chars from old command with spaces
              for (uint16_t i = server->input_pos; i < old_len + 2; i++) {
                tcp_server_send(server->tcp_server, 0, (uint8_t*)" ", 1);
              }
              // 4. Move cursor back after spaces
              for (uint16_t i = server->input_pos; i < old_len + 2; i++) {
                tcp_server_send(server->tcp_server, 0, (uint8_t*)"\b", 1);
              }
            }
          }
          return;
        } else if (byte == 'B') {
          // Down arrow - get next command from history
          const char* next_cmd = cli_history_get_next();
          if (next_cmd != NULL) {
            // Save old length for clearing
            uint16_t old_len = server->input_pos;

            // Clear current line and redraw with history command
            memset(server->input_buffer, 0, TELNET_INPUT_BUFFER_SIZE);
            if (next_cmd[0] != '\0') {
              strncpy(server->input_buffer, next_cmd, TELNET_INPUT_BUFFER_SIZE - 1);
              server->input_pos = strlen(server->input_buffer);
              server->cursor_pos = server->input_pos;  // Cursor at end of recalled command
            } else {
              server->input_pos = 0;
              server->cursor_pos = 0;
            }

            // Redraw line using compatible method (no ANSI codes)
            if (server->echo_enabled) {
              // 1. Return to start of line
              telnet_server_write(server, "\r> ");
              // 2. Print new command (if any)
              if (server->input_pos > 0) {
                telnet_server_write(server, server->input_buffer);
              }
              // 3. Clear remaining chars from old command with spaces
              for (uint16_t i = server->input_pos; i < old_len + 2; i++) {
                tcp_server_send(server->tcp_server, 0, (uint8_t*)" ", 1);
              }
              // 4. Move cursor back after spaces
              for (uint16_t i = server->input_pos; i < old_len + 2; i++) {
                tcp_server_send(server->tcp_server, 0, (uint8_t*)"\b", 1);
              }
            }
          }
          return;
        } else if (byte == 'C') {
          // Right arrow - move cursor right
          server->escape_seq_state = 0;
          if (server->cursor_pos < server->input_pos) {
            server->cursor_pos++;
            if (server->echo_enabled) {
              tcp_server_send(server->tcp_server, 0, (uint8_t*)"\x1B[C", 3);
            }
          }
          return;
        } else if (byte == 'D') {
          // Left arrow - move cursor left
          server->escape_seq_state = 0;
          if (server->cursor_pos > 0) {
            server->cursor_pos--;
            if (server->echo_enabled) {
              tcp_server_send(server->tcp_server, 0, (uint8_t*)"\x1B[D", 3);
            }
          }
          return;
        } else {
          // Not a recognized arrow key - discard silently
          return;
        }
      }

      // Check if this is start of escape sequence
      if (byte == 0x1B) {  // ESC
        server->escape_seq_state = 1;
        // NOTE: Don't echo ESC character - it's part of control sequence
        return;
      }

      // ====================================================================
      // NORMAL CHARACTER PROCESSING
      // ====================================================================
      if (byte == TELNET_CMD_IAC) {
        server->parse_state = TELNET_STATE_IAC;
      } else if (byte == '\r' || byte == '\n') {
        // Line ending - process command
        // Handle both \r and \n independently (for copy/paste compatibility)
        // Skip if this is \n immediately after \r (Windows CRLF)
        static uint8_t last_was_cr = 0;

        if (byte == '\n' && last_was_cr) {
          last_was_cr = 0;
          return;  // Skip \n after \r
        }

        last_was_cr = (byte == '\r') ? 1 : 0;

        // Line complete (with boundary check)
        if (server->input_pos >= TELNET_INPUT_BUFFER_SIZE) {
          server->input_pos = TELNET_INPUT_BUFFER_SIZE - 1;
        }
        server->input_buffer[server->input_pos] = '\0';
        server->input_ready = 1;
        server->input_pos = 0;
        server->cursor_pos = 0;  // Reset cursor position for new line

        // Echo if enabled
        if (server->echo_enabled) {
          tcp_server_send(server->tcp_server, 0, (uint8_t*)"\r\n", 2);
        }
      } else if (byte >= 32 && byte < 127) {
        // Printable character - insert at cursor position (insert mode)
        // NOTE: Escape sequences are already handled above and return early,
        // so if we reach here, we're guaranteed escape_seq_state == 0
        if (server->input_pos < TELNET_INPUT_BUFFER_SIZE - 1) {
          // If cursor is at end, simple append
          if (server->cursor_pos == server->input_pos) {
            server->input_buffer[server->input_pos++] = byte;
            server->cursor_pos++;

            // Echo back
            if (server->echo_enabled) {
              tcp_server_send(server->tcp_server, 0, &byte, 1);
            }
          } else {
            // Insert mode: shift characters to the right
            // Move all characters from cursor_pos to end, one position right
            for (int i = server->input_pos; i > server->cursor_pos; i--) {
              server->input_buffer[i] = server->input_buffer[i - 1];
            }

            // Insert new character at cursor position
            server->input_buffer[server->cursor_pos] = byte;
            server->input_pos++;
            server->cursor_pos++;

            // Redraw line from cursor position to end
            if (server->echo_enabled) {
              // Send the new character + all characters after it
              tcp_server_send(server->tcp_server, 0, (uint8_t*)&server->input_buffer[server->cursor_pos - 1],
                             server->input_pos - server->cursor_pos + 1);

              // Move cursor back to correct position
              int chars_to_move_back = server->input_pos - server->cursor_pos;
              for (int i = 0; i < chars_to_move_back; i++) {
                tcp_server_send(server->tcp_server, 0, (uint8_t*)"\x1B[D", 3);  // Move left
              }
            }
          }
        }
      } else if (byte == 8 || byte == 127) {
        // Backspace - delete character at cursor position
        // Reset escape state if user is trying to backspace
        server->escape_seq_state = 0;

        if (server->cursor_pos > 0) {
          // If cursor is at end, simple delete
          if (server->cursor_pos == server->input_pos) {
            server->input_pos--;
            server->cursor_pos--;

            if (server->echo_enabled) {
              tcp_server_send(server->tcp_server, 0, (uint8_t*)"\b \b", 3);
            }
          } else {
            // Delete mode: shift characters to the left
            // Move all characters from cursor_pos to end, one position left
            for (int i = server->cursor_pos - 1; i < server->input_pos - 1; i++) {
              server->input_buffer[i] = server->input_buffer[i + 1];
            }

            server->input_pos--;
            server->cursor_pos--;

            // Redraw line from cursor position to end
            if (server->echo_enabled) {
              // Move cursor back one position
              tcp_server_send(server->tcp_server, 0, (uint8_t*)"\b", 1);

              // Send all characters from cursor to end + space to clear last char
              tcp_server_send(server->tcp_server, 0, (uint8_t*)&server->input_buffer[server->cursor_pos],
                             server->input_pos - server->cursor_pos);
              tcp_server_send(server->tcp_server, 0, (uint8_t*)" ", 1);  // Clear last character

              // Move cursor back to correct position
              int chars_to_move_back = server->input_pos - server->cursor_pos + 1;
              for (int i = 0; i < chars_to_move_back; i++) {
                tcp_server_send(server->tcp_server, 0, (uint8_t*)"\x1B[D", 3);  // Move left
              }
            }
          }
        }
      }
      break;

    case TELNET_STATE_IAC:
      // SECURITY FIX: Properly handle IAC state machine
      if (byte >= TELNET_CMD_WILL && byte <= TELNET_CMD_DONT) {
        // Store command byte for next state
        telnet_iac_cmd = byte;
        server->parse_state = TELNET_STATE_IAC_CMD;
      } else if (byte == TELNET_CMD_IAC) {
        // Escaped IAC (literal 255)
        if (server->input_pos < TELNET_INPUT_BUFFER_SIZE - 1) {
          server->input_buffer[server->input_pos++] = byte;
          server->cursor_pos = server->input_pos;  // Keep cursor at end
          if (server->echo_enabled) {
            tcp_server_send(server->tcp_server, 0, &byte, 1);
          }
        }
        server->parse_state = TELNET_STATE_NONE;
      } else {
        server->parse_state = TELNET_STATE_NONE;
      }
      break;

    case TELNET_STATE_IAC_CMD:
      // Now we have: IAC, cmd (WILL/WONT/DO/DONT), opt
      // cmd is stored in telnet_iac_cmd, current byte is the option
      // For now, we just ignore the negotiation and return to NONE state
      // (In a full implementation, we'd handle the negotiation here)
      server->parse_state = TELNET_STATE_NONE;
      break;

    default:
      server->parse_state = TELNET_STATE_NONE;
      break;
  }
}

/* ============================================================================
 * LINE-ORIENTED I/O
 * ============================================================================ */

int telnet_server_readline(TelnetServer *server, char *buf, size_t max_len)
{
  if (!server || !buf || !max_len) {
    return -1;
  }

  if (!server->input_ready) {
    return 0;  // No complete line yet
  }

  int len = strlen(server->input_buffer);
  if (len >= (int)max_len) {
    len = max_len - 1;
  }

  strncpy(buf, server->input_buffer, len);
  buf[len] = '\0';

  server->input_ready = 0;
  return len;
}

int telnet_server_writeline(TelnetServer *server, const char *line)
{
  if (!server || !line || !server->tcp_server) {
    return -1;
  }

  int len = 0;
  size_t line_len = strlen(line);

  // Send line content if non-empty
  if (line_len > 0) {
    int sent = tcp_server_send(server->tcp_server, 0, (uint8_t*)line, line_len);
    if (sent < 0) {
      return -1;
    }
    len += sent;
  }

  // Always send line terminator (even for empty lines - important for newlines!)
  int sent = tcp_server_send(server->tcp_server, 0, (uint8_t*)"\r\n", 2);
  if (sent < 0) {
    return -1;
  }
  len += sent;

  return len;
}

int telnet_server_writelinef(TelnetServer *server, const char *format, ...)
{
  if (!server || !format) {
    return -1;
  }

  char buf[256];
  va_list args;
  va_start(args, format);
  int len = vsnprintf(buf, sizeof(buf) - 1, format, args);
  va_end(args);

  if (len < 0 || len >= (int)sizeof(buf)) {
    return -1;
  }

  return telnet_server_writeline(server, buf);
}

int telnet_server_write(TelnetServer *server, const char *text)
{
  if (!server || !text || !server->tcp_server) {
    return -1;
  }

  // Telnet protocol requires CRLF (\r\n) for newlines
  // We need to convert any standalone \n to \r\n
  const char *src = text;
  int total_sent = 0;

  while (*src) {
    const char *next_newline = strchr(src, '\n');

    if (next_newline) {
      // Send text up to (but not including) the \n
      size_t chunk_len = next_newline - src;
      if (chunk_len > 0) {
        int sent = tcp_server_send(server->tcp_server, 0, (uint8_t*)src, chunk_len);
        if (sent < 0) return -1;
        total_sent += sent;
      }

      // Send \r\n instead of just \n
      int sent = tcp_server_send(server->tcp_server, 0, (uint8_t*)"\r\n", 2);
      if (sent < 0) return -1;
      total_sent += sent;

      // Move past the \n
      src = next_newline + 1;
    } else {
      // No more newlines - send remaining text
      size_t remaining = strlen(src);
      if (remaining > 0) {
        int sent = tcp_server_send(server->tcp_server, 0, (uint8_t*)src, remaining);
        if (sent < 0) return -1;
        total_sent += sent;
      }
      break;
    }
  }

  return total_sent;
}

int telnet_server_writef(TelnetServer *server, const char *format, ...)
{
  if (!server || !format) {
    return -1;
  }

  char buf[256];
  va_list args;
  va_start(args, format);
  int len = vsnprintf(buf, sizeof(buf) - 1, format, args);
  va_end(args);

  if (len < 0 || len >= (int)sizeof(buf)) {
    return -1;
  }

  return telnet_server_write(server, buf);
}

int telnet_server_writech(TelnetServer *server, char ch)
{
  if (!server || !server->tcp_server) {
    return -1;
  }

  return tcp_server_send(server->tcp_server, 0, (uint8_t*)&ch, 1);
}

/* ============================================================================
 * STATUS & INFORMATION
 * ============================================================================ */

uint8_t telnet_server_has_input(const TelnetServer *server)
{
  return server ? server->input_ready : 0;
}

uint16_t telnet_server_available(const TelnetServer *server)
{
  if (!server || !server->tcp_server) {
    return 0;
  }

  return tcp_server_available(server->tcp_server, 0);
}

/* ============================================================================
 * BACKGROUND TASKS
 * ============================================================================ */

int telnet_server_loop(TelnetServer *server)
{
  if (!server || !server->tcp_server) {
    return 0;
  }

  int events = 0;

  // SECURITY: Check if TCP connection died unexpectedly
  // If TCP client is disconnected but Telnet still thinks it's connected,
  // reset all Telnet state so next connection requires authentication
  static uint8_t was_connected = 0;
  uint8_t is_connected = telnet_server_client_connected(server);

  if (was_connected && !is_connected) {
    // Connection just died - reset authentication state
    if (server->auth_required) {
      server->auth_state = TELNET_AUTH_WAITING;
      server->auth_attempts = 0;
      server->auth_lockout_time = 0;
      memset(server->auth_username, 0, sizeof(server->auth_username));
    }
    server->escape_seq_state = 0;
    server->input_pos = 0;
    server->input_ready = 0;
    memset(server->input_buffer, 0, TELNET_INPUT_BUFFER_SIZE);
  }

  // Process TCP server events (this calls tcp_server_accept() internally)
  // Detect NEW connection by checking before/after connection state
  uint8_t was_connected_before_loop = is_connected;
  events += tcp_server_loop(server->tcp_server);
  is_connected = telnet_server_client_connected(server);
  was_connected = is_connected;

  // NEW CONNECTION DETECTED: was disconnected, now connected
  if (!was_connected_before_loop && is_connected) {
    // New client detected - reset all state
    server->escape_seq_state = 0;  // Reset escape sequence parser
    server->input_pos = 0;         // Clear input buffer
    server->cursor_pos = 0;        // Reset cursor position
    memset(server->input_buffer, 0, TELNET_INPUT_BUFFER_SIZE);

    // Send IAC negotiation to configure client properly
    // WILL ECHO = Server will handle echo, client should NOT echo locally
    // WILL SUPPRESS-GO-AHEAD = Character-by-character mode (not line mode)
    uint8_t iac_will_echo[] = { 0xFF, 0xFB, 0x01 };        // IAC WILL ECHO
    uint8_t iac_will_sga[] = { 0xFF, 0xFB, 0x03 };         // IAC WILL SUPPRESS-GO-AHEAD
    uint8_t iac_dont_echo[] = { 0xFF, 0xFE, 0x01 };        // IAC DONT ECHO (client should not echo)
    tcp_server_send(server->tcp_server, 0, iac_will_echo, 3);
    tcp_server_send(server->tcp_server, 0, iac_will_sga, 3);
    tcp_server_send(server->tcp_server, 0, iac_dont_echo, 3);

    // Small delay to allow client to process IAC and be ready
    delay(100);

    if (server->auth_required) {
      server->auth_state = TELNET_AUTH_WAITING;
      server->auth_attempts = 0;
      server->auth_lockout_time = 0;
      memset(server->auth_username, 0, sizeof(server->auth_username));

      // telnet_send_auth_prompt() will set server->echo_enabled = 1
      telnet_send_auth_prompt(server);
      events++;
    } else {
      // Auth disabled - go straight to authenticated
      server->auth_state = TELNET_AUTH_AUTHENTICATED;
      server->echo_enabled = 1;  // Enable SERVER echo for no-auth mode

      // Send welcome message and initial CLI prompt
      telnet_server_writeline(server, "");
      const char *hn2 = g_persist_config.hostname[0] ? g_persist_config.hostname : "modbus-esp32";
      telnet_server_writelinef(server, "=== %s - Telnet Srv ===", hn2);
      telnet_server_writeline(server, "");
      telnet_server_write(server, "> ");
    }
  }

  // Process incoming data from client
  if (telnet_server_client_connected(server)) {
    uint8_t byte;
    while (tcp_server_recv_byte(server->tcp_server, 0, &byte) > 0) {
      telnet_process_input(server, byte);
      events++;

      // CRITICAL: Stop reading bytes if a complete line is ready!
      // This prevents subsequent lines from overwriting the current line buffer
      // during copy/paste (when multiple \n arrive rapidly)
      if (server->input_ready) {
        break;
      }
    }

    // SECURITY FIX: Process authentication if input is ready and auth is needed
    if (server->input_ready && server->auth_required && server->auth_state != TELNET_AUTH_AUTHENTICATED) {
      telnet_handle_auth_input(server, server->input_buffer);
      // NOTE: telnet_handle_auth_input() already sends the next prompt if needed
      // DO NOT send prompt again here to avoid duplication!
    }

    // FEATURE: Execute CLI commands if authenticated and input is ready
    if (server->input_ready && server->auth_state == TELNET_AUTH_AUTHENTICATED) {
      // Check if CLI is in upload mode BEFORE processing line
      uint8_t was_in_upload_mode = cli_shell_is_in_upload_mode();

      // Create a temporary Telnet console for this command
      Console *telnet_console = console_telnet_create(server);
      if (telnet_console) {

        if (was_in_upload_mode) {
          // Feed this line to upload mode (handles ST code and END_UPLOAD)
          cli_shell_feed_upload_line(telnet_console, server->input_buffer);
        } else {
          // Execute as normal command
          cli_shell_execute_command(telnet_console, server->input_buffer);
        }

        // Check if "exit" command was issued
        if (telnet_console->close_requested) {
          // Close connection gracefully
          console_telnet_destroy(telnet_console);

          // SECURITY: Reset authentication state before disconnect
          if (server->auth_required) {
            server->auth_state = TELNET_AUTH_WAITING;
            server->auth_attempts = 0;
            server->auth_lockout_time = 0;
            memset(server->auth_username, 0, sizeof(server->auth_username));
          }

          tcp_server_disconnect_client(server->tcp_server, 0);
          events++;

          // Clear the input buffer
          server->input_pos = 0;
          server->cursor_pos = 0;
          server->input_ready = 0;
          memset(server->input_buffer, 0, TELNET_INPUT_BUFFER_SIZE);

          // Skip prompt sending - connection is closing
          return events;
        }

        // Check if we just entered upload mode AND there's pending data (copy/paste scenario)
        uint8_t is_in_upload_mode = cli_shell_is_in_upload_mode();
        if (!was_in_upload_mode && is_in_upload_mode) {
          // We just entered upload mode - check if there's more data pending (copy/paste)
          uint16_t pending = tcp_server_available(server->tcp_server, 0);
          if (pending > 0) {
            // COPY/PASTE DETECTED: Process all pending lines immediately
            // This prevents them from being executed as commands
            while (tcp_server_available(server->tcp_server, 0) > 0) {
              // Clear current buffer
              server->input_pos = 0;
              server->cursor_pos = 0;
              server->input_ready = 0;
              memset(server->input_buffer, 0, TELNET_INPUT_BUFFER_SIZE);

              // Read pending bytes into buffer (reuse existing telnet input logic)
              // Process bytes until we get a complete line or run out of data
              uint8_t byte;
              int timeout = 100; // Safety: max 100 bytes per line
              while (timeout-- > 0 && tcp_server_recv_byte(server->tcp_server, 0, &byte) == 1) {
                // Handle CRLF line endings
                if (byte == '\r' || byte == '\n') {
                  if (server->input_pos > 0) {
                    // Got a complete line
                    server->input_buffer[server->input_pos] = '\0';
                    server->input_ready = 1;
                    break;
                  } else {
                    // Skip empty line (double CRLF)
                    continue;
                  }
                } else if (byte >= 32 && byte < 127) {
                  // Printable character
                  if (server->input_pos < TELNET_INPUT_BUFFER_SIZE - 1) {
                    server->input_buffer[server->input_pos++] = byte;
                  }
                }
              }

              // If we got a complete line, feed it to upload mode
              if (server->input_ready) {
                cli_shell_feed_upload_line(telnet_console, server->input_buffer);

                // Check if we exited upload mode (END_UPLOAD was received)
                if (!cli_shell_is_in_upload_mode()) {
                  break;
                }
              } else {
                // No complete line yet - exit loop
                break;
              }
            }
          }
        }

        // Destroy the temporary console
        console_telnet_destroy(telnet_console);
      }

      // Show the CLI prompt after command execution
      // Only send prompt if:
      // 1. We're still in upload mode AND we were already in it (not just entered)
      // 2. We're in normal mode
      // DO NOT send prompt if we just entered upload mode - cli_shell_start_st_upload() already sent it
      uint8_t is_in_upload_mode = cli_shell_is_in_upload_mode();

      if (is_in_upload_mode && was_in_upload_mode) {
        // Still in upload mode - send upload prompt for next line
        telnet_server_write(server, ">>> ");
      } else if (!is_in_upload_mode) {
        // Normal mode - send normal prompt
        // (Note: Don't send if we just entered upload mode - that prompt was already sent)
        telnet_server_write(server, "> ");
      }
      // If we just entered upload mode (!was_in_upload_mode && is_in_upload_mode), don't send prompt

      // Clear the input buffer
      server->input_pos = 0;
      server->cursor_pos = 0;
      server->input_ready = 0;
      memset(server->input_buffer, 0, TELNET_INPUT_BUFFER_SIZE);
      events++;
    }
  }

  return events;
}

/* ============================================================================
 * DEBUGGING
 * ============================================================================ */

void telnet_server_print_status(const TelnetServer *server)
{
  if (!server) {
    printf("Telnet Server: NULL\n");
    return;
  }

  printf("\n=== Telnet Server Status ===\n");
  printf("Port: %d\n", server->port);
  printf("Client connected: %s\n", telnet_server_client_connected(server) ? "Yes" : "No");
  printf("Echo enabled: %s\n", server->echo_enabled ? "Yes" : "No");
  printf("Input buffer: %d/%d bytes\n", server->input_pos, TELNET_BUFFER_SIZE);
  printf("Input ready: %s\n", server->input_ready ? "Yes" : "No");
  printf("============================\n\n");
}
