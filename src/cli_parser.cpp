/**
 * @file cli_parser.cpp
 * @brief CLI command parser and dispatcher (LAYER 7)
 *
 * Ported from: Mega2560 v3.6.5 cli_shell.cpp (tokenizer + dispatcher)
 * Adapted to: ESP32 modular architecture
 *
 * Responsibility:
 * - Command line tokenization (split by whitespace)
 * - Alias normalization (sh→show, wr→write, etc.)
 * - Command dispatch based on first token
 * - Parameter parsing and validation
 * - Error messages
 *
 * Context: This is the CLI "router" - maps user input to handlers
 */

#include "cli_parser.h"
#include "cli_shell.h"
#include "cli_commands.h"
#include "cli_show.h"
#include "cli_config_regs.h"
#include "cli_config_coils.h"
#include "cli_commands_logic.h"
#include "cli_commands_modbus_master.h"
#include "cli_commands_modbus_slave.h"
#include "st_logic_config.h"
#include "debug.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ============================================================================
 * TOKENIZER
 * ============================================================================ */

static bool is_whitespace(char c) {
  return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

/**
 * @brief Tokenize a command line into argv array (with bounds checking)
 * @param line Input line (must be NULL-terminated)
 * @param argv Output argv array
 * @param max_argv Maximum number of tokens
 * @return Number of tokens
 */
static uint8_t tokenize(char* line, char* argv[], uint8_t max_argv) {
  // SECURITY: Validate input
  if (!line || !argv || max_argv == 0) {
    return 0;
  }

  // Verify line is properly null-terminated (defensive check)
  // This prevents reading past buffer boundaries if input is malformed
  size_t line_len = strnlen(line, 256);  // Max length for CLI_INPUT_BUFFER_SIZE
  if (line_len == 0 || line_len >= 256) {
    return 0;  // Reject oversized or empty input
  }

  uint8_t argc = 0;
  char* p = line;
  char* line_end = line + line_len;  // Boundary marker

  while (*p && argc < max_argv && p < line_end) {
    // Skip whitespace (with boundary check)
    while (*p && is_whitespace(*p) && p < line_end) p++;
    if (!*p || p >= line_end) break;

    // Mark token start
    argv[argc++] = p;

    // Handle quoted strings
    if (*p == '"' && p < line_end) {
      // Skip opening quote
      p++;

      // Find closing quote (with boundary check)
      while (*p && *p != '"' && p < line_end) {
        p++;
      }

      // Remove closing quote by null-terminating
      if (*p == '"' && p < line_end) {
        *p = '\0';
        p++;
      }

      // Shift token start to skip opening quote
      argv[argc - 1]++;
    } else {
      // Skip until whitespace (unquoted token, with boundary check)
      while (*p && !is_whitespace(*p) && p < line_end) p++;

      // Null-terminate token (with boundary check)
      if (*p && p < line_end) {
        *p = '\0';
        p++;
      }
    }
  }

  return argc;
}

/* ============================================================================
 * ALIAS NORMALIZATION
 * ============================================================================ */

/**
 * @brief Case-insensitive string comparison
 * @param a First string
 * @param b Second string
 * @return true if strings match (ignoring case)
 */
static bool str_eq_i(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *b) {
    char ca = *a;
    char cb = *b;
    // Convert to uppercase for comparison
    if (ca >= 'a' && ca <= 'z') ca -= 32;
    if (cb >= 'a' && cb <= 'z') cb -= 32;
    if (ca != cb) return false;
    a++;
    b++;
  }
  return (*a == *b);  // Both must be at null terminator
}

static const char* normalize_alias(const char* s) {
  if (!s) return "";

  // Verbs (all case-insensitive)
  if (str_eq_i(s, "SHOW") || str_eq_i(s, "SH") || str_eq_i(s, "S")) return "SHOW";
  if (str_eq_i(s, "SET") || str_eq_i(s, "CONF")) return "SET";
  if (str_eq_i(s, "NO")) return "NO";
  if (str_eq_i(s, "RESET") || str_eq_i(s, "RST")) return "RESET";
  if (str_eq_i(s, "CLEAR") || str_eq_i(s, "CLR")) return "CLEAR";
  if (str_eq_i(s, "SAVE") || str_eq_i(s, "SV")) return "SAVE";
  if (str_eq_i(s, "LOAD") || str_eq_i(s, "LD")) return "LOAD";
  if (str_eq_i(s, "DEFAULTS") || str_eq_i(s, "DEF")) return "DEFAULTS";
  if (str_eq_i(s, "REBOOT") || str_eq_i(s, "RESTART")) return "REBOOT";
  if (str_eq_i(s, "EXIT") || str_eq_i(s, "QUIT") || str_eq_i(s, "Q")) return "EXIT";
  if (str_eq_i(s, "CONNECT") || str_eq_i(s, "CONN") || str_eq_i(s, "CON")) return "CONNECT";
  if (str_eq_i(s, "DISCONNECT") || str_eq_i(s, "DISC") || str_eq_i(s, "DC")) return "DISCONNECT";
  if (str_eq_i(s, "HELP") || !strcmp(s, "?") || str_eq_i(s, "H")) return "HELP";
  if (str_eq_i(s, "READ") || str_eq_i(s, "RD") || str_eq_i(s, "R")) return "READ";
  if (str_eq_i(s, "WRITE") || str_eq_i(s, "WR") || str_eq_i(s, "W")) return "WRITE";
  if (str_eq_i(s, "COMMANDS") || str_eq_i(s, "CMDS")) return "COMMANDS";

  // Nouns
  if (str_eq_i(s, "COUNTER") || str_eq_i(s, "CNT") || str_eq_i(s, "CNTR")) return "COUNTER";
  if (str_eq_i(s, "COUNTERS") || str_eq_i(s, "CNTS")) return "COUNTERS";
  if (str_eq_i(s, "TIMER") || str_eq_i(s, "TMR")) return "TIMER";
  if (str_eq_i(s, "TIMERS") || str_eq_i(s, "TMRS")) return "TIMERS";
  if (str_eq_i(s, "LOGIC") || str_eq_i(s, "LOG")) return "LOGIC";
  if (str_eq_i(s, "CONFIG") || str_eq_i(s, "CFG")) return "CONFIG";
  if (str_eq_i(s, "REGISTERS") || str_eq_i(s, "REGS")) return "REGISTERS";
  if (str_eq_i(s, "COILS")) return "COILS";
  if (str_eq_i(s, "INPUTS") || str_eq_i(s, "INS")) return "INPUTS";
  if (str_eq_i(s, "INPUT") || str_eq_i(s, "IN")) return "INPUT";
  if (str_eq_i(s, "INPUT-REG") || str_eq_i(s, "INPUT_REG") || str_eq_i(s, "I-REG") || str_eq_i(s, "IREG")) return "I-REG";
  if (str_eq_i(s, "VERSION") || str_eq_i(s, "VER") || str_eq_i(s, "V")) return "VERSION";
  if (str_eq_i(s, "GPIO")) return "GPIO";
  if (str_eq_i(s, "ECHO")) return "ECHO";
  if (str_eq_i(s, "DEBUG") || str_eq_i(s, "DBG")) return "DEBUG";
  if (str_eq_i(s, "WATCHDOG") || str_eq_i(s, "WDG")) return "WATCHDOG";
  if (str_eq_i(s, "VERBOSE") || str_eq_i(s, "VERB")) return "VERBOSE";

  // Modbus Master/Slave commands
  if (str_eq_i(s, "MODBUS-MASTER") || str_eq_i(s, "MB-MASTER")) return "MODBUS-MASTER";
  if (str_eq_i(s, "MODBUS-SLAVE") || str_eq_i(s, "MB-SLAVE")) return "MODBUS-SLAVE";
  if (str_eq_i(s, "ENABLED")) return "ENABLED";
  if (str_eq_i(s, "DISABLED")) return "DISABLED";
  if (str_eq_i(s, "INTERVAL")) return "INTERVAL";
  if (str_eq_i(s, "SLAVE-ID") || str_eq_i(s, "SLAVEID") || str_eq_i(s, "ID")) return "SLAVE-ID";
  if (str_eq_i(s, "BAUDRATE") || str_eq_i(s, "BAUD")) return "BAUDRATE";
  if (str_eq_i(s, "PARITY")) return "PARITY";
  if (str_eq_i(s, "STOP-BITS") || str_eq_i(s, "STOPBITS")) return "STOP-BITS";
  if (str_eq_i(s, "TIMEOUT")) return "TIMEOUT";
  if (str_eq_i(s, "INTER-FRAME-DELAY") || str_eq_i(s, "DELAY")) return "INTER-FRAME-DELAY";
  if (str_eq_i(s, "MAX-REQUESTS") || str_eq_i(s, "MAXREQUESTS")) return "MAX-REQUESTS";

  // Logic subcommands
  if (str_eq_i(s, "PROGRAM") || str_eq_i(s, "PROGRAMS")) return "PROGRAM";
  if (str_eq_i(s, "STATS") || str_eq_i(s, "ST-STATS")) return "STATS";
  if (str_eq_i(s, "ERRORS")) return "ERRORS";
  if (str_eq_i(s, "ALL")) return "ALL";
  if (str_eq_i(s, "CODE")) return "CODE";
  if (str_eq_i(s, "BYTECODE")) return "BYTECODE";
  if (str_eq_i(s, "TIMING")) return "TIMING";
  if (str_eq_i(s, "ST")) return "ST";

  // System commands (for SET context)
  if (str_eq_i(s, "REG") || str_eq_i(s, "HOLDING-REG") || str_eq_i(s, "HOLDING_REG") ||
      str_eq_i(s, "H-REG") || str_eq_i(s, "HREG")) return "H-REG";
  if (str_eq_i(s, "COIL")) return "COIL";
  if (str_eq_i(s, "HOSTNAME")) return "HOSTNAME";
  if (str_eq_i(s, "WIFI")) return "WIFI";
  if (str_eq_i(s, "ETHERNET") || str_eq_i(s, "ETH")) return "ETHERNET";
  if (str_eq_i(s, "HTTP") || str_eq_i(s, "REST") || str_eq_i(s, "API")) return "HTTP";
  if (str_eq_i(s, "BACKUP")) return "BACKUP";
  if (str_eq_i(s, "ENABLE")) return "ENABLE";
  if (str_eq_i(s, "DISABLE")) return "DISABLE";
  if (str_eq_i(s, "PERSIST")) return "PERSIST";
  if (str_eq_i(s, "GROUP")) return "GROUP";
  if (str_eq_i(s, "ADD")) return "ADD";
  if (str_eq_i(s, "REMOVE")) return "REMOVE";
  if (str_eq_i(s, "AUTO-LOAD") || str_eq_i(s, "AUTOLOAD")) return "AUTO-LOAD";

  // Boolean values
  if (str_eq_i(s, "ON")) return "ON";
  if (str_eq_i(s, "OFF")) return "OFF";
  if (str_eq_i(s, "TRUE")) return "TRUE";
  if (str_eq_i(s, "FALSE")) return "FALSE";

  // Logic Mode subcommands
  if (str_eq_i(s, "UPLOAD")) return "UPLOAD";
  if (str_eq_i(s, "BIND")) return "BIND";
  if (str_eq_i(s, "DELETE")) return "DELETE";

  // Debug subcommands (FEAT-008)
  if (str_eq_i(s, "PAUSE")) return "PAUSE";
  if (str_eq_i(s, "CONTINUE") || str_eq_i(s, "CONT")) return "CONTINUE";
  if (str_eq_i(s, "STEP")) return "STEP";
  if (str_eq_i(s, "BREAK") || str_eq_i(s, "BP")) return "BREAK";
  if (str_eq_i(s, "STOP")) return "STOP";
  if (str_eq_i(s, "VARS") || str_eq_i(s, "VARIABLES")) return "VARS";
  if (str_eq_i(s, "STACK")) return "STACK";
  if (str_eq_i(s, "LINE") || str_eq_i(s, "LN")) return "LINE";

  return s;  // Return as-is if not an alias
}

/* ============================================================================
 * HELP SYSTEM
 * ============================================================================ */

static void print_show_help(void) {
  debug_println("");
  debug_println("Available 'show' commands:");
  debug_println("  show config          - Vis fuld konfiguration");
  debug_println("  show wifi            - Vis Wi-Fi status og IP");
  debug_println("  show ethernet        - Vis Ethernet (W5500) status");
  debug_println("  show counters        - Vis alle counters");
  debug_println("  show counter <id> [verbose] - Vis specifik counter (1-4)");
  debug_println("  show timers          - Vis alle timers");
  debug_println("  show timer <id> [verbose] - Vis specifik timer (1-4)");
  debug_println("  show logic           - Vis alle ST Logic programmer");
  debug_println("  show logic <id>      - Vis program (uden source)");
  debug_println("  show logic <id> st   - Vis program med ST source code (v5.1.0)");
  debug_println("  show logic <id> code - Vis compiled bytecode");
  debug_println("  show gpio            - Vis GPIO mappings");
  debug_println("  show gpio <pin>      - Vis specifik GPIO pin (0-39)");
  debug_println("  show registers       - Vis holding registers");
  debug_println("  show inputs          - Vis input registers");
  debug_println("  show st-stats        - Vis ST Logic stats (Modbus IR 252-293)");
  debug_println("  show coils           - Vis coils");
  debug_println("  show debug           - Vis debug flags");
  debug_println("  show persist         - Vis persistence groups (v4.0+)");
  debug_println("  show watchdog        - Vis watchdog monitor status (v4.0+)");
  debug_println("  show modbus-master   - Vis Modbus Master config (v4.4+)");
  debug_println("  show modbus-slave    - Vis Modbus Slave config (v4.4.1+)");
  debug_println("  show backup          - Vis backup/restore URL");
  debug_println("  show version         - Vis firmware version");
  debug_println("  show echo            - Vis echo status");
  debug_println("");
}

static void print_set_help(void) {
  debug_println("");
  debug_println("Available 'set' commands:");
  debug_println("  set hostname <name>     - Sæt hostname");
  debug_println("  set baud <rate>         - Sæt baudrate");
  debug_println("  set id <slave_id>       - Sæt Modbus slave ID (1-247)");
  debug_println("  set reg <addr> <value>  - Skriv holding register");
  debug_println("  set coil <idx> <0|1>    - Skriv coil");
  debug_println("  set wifi ?              - Vis Wi-Fi kommandoer");
  debug_println("  set ethernet ?          - Vis Ethernet (W5500) kommandoer");
  debug_println("  set counter ?           - Vis counter kommandoer");
  debug_println("  set timer ?             - Vis timer kommandoer");
  debug_println("  set gpio ?              - Vis GPIO kommandoer");
  debug_println("  set debug ?             - Vis debug kommandoer");
  debug_println("  set logic interval:<ms> - Sæt ST Logic execution interval (2,5,10,20,25,50,75,100)");
  debug_println("  set persist ?           - Vis persistence kommandoer (v4.0+)");
  debug_println("  set modbus-master ?     - Vis Modbus Master kommandoer (v4.4+)");
  debug_println("  set modbus-slave ?      - Vis Modbus Slave kommandoer (v4.4.1+)");
  debug_println("  set echo <on|off>       - Sæt remote echo");
  debug_println("");
}

static void print_ethernet_help(void) {
  debug_println("");
  debug_println("Available 'set ethernet' commands:");
  debug_println("  set ethernet enable        - Aktivér Ethernet (W5500)");
  debug_println("  set ethernet disable       - Deaktivér Ethernet");
  debug_println("  set ethernet dhcp <on|off> - Aktivér/deaktivér DHCP");
  debug_println("  set ethernet ip <ip>       - Sæt statisk IP (hvis DHCP off)");
  debug_println("  set ethernet gateway <ip>  - Sæt gateway IP");
  debug_println("  set ethernet netmask <mask>- Sæt netmask");
  debug_println("  set ethernet dns <ip>      - Sæt DNS server");
  debug_println("");
  debug_println("  Note: Requires -DETHERNET_W5500_ENABLED build flag");
  debug_println("");
}

static void print_wifi_help(void) {
  debug_println("");
  debug_println("Available 'set wifi' commands:");
  debug_println("  set wifi ssid <name>       - Sæt Wi-Fi SSID");
  debug_println("  set wifi password <pass>   - Sæt Wi-Fi password");
  debug_println("  set wifi dhcp <on|off>     - Aktivér/deaktivér DHCP");
  debug_println("  set wifi ip <ip>           - Sæt statisk IP (hvis DHCP off)");
  debug_println("  set wifi gateway <ip>      - Sæt gateway IP");
  debug_println("  set wifi netmask <mask>    - Sæt netmask");
  debug_println("  set wifi dns <ip>          - Sæt DNS server");
  debug_println("  set wifi port <port>       - Sæt Telnet port (default 23)");
  debug_println("  set wifi telnet_user <u>   - Sæt Telnet username");
  debug_println("  set wifi telnet_pass <p>   - Sæt Telnet password");
  debug_println("");
}

static void print_http_help(void) {
  debug_println("");
  debug_println("Available 'set http' commands:");
  debug_println("  set http enabled <on|off>  - Aktivér/deaktivér HTTP server");
  debug_println("  set http port <port>       - Sæt HTTP port (default 80, HTTPS: 443)");
  debug_println("  set http auth <on|off>     - Aktivér/deaktivér Basic Auth");
  debug_println("  set http username <user>   - Sæt HTTP username");
  debug_println("  set http password <pass>   - Sæt HTTP password");
  debug_println("  set http api <on|off>      - Aktivér/deaktivér API endpoints");
  debug_println("  set http tls <on|off>      - Aktivér/deaktivér HTTPS/TLS (kræver reboot)");
  debug_println("");
  debug_println("API Endpoints:");
  debug_println("  GET  /api/status           - System info (version, uptime, heap)");
  debug_println("  GET  /api/counters         - Alle counters");
  debug_println("  GET  /api/counters/{1-4}   - Enkelt counter");
  debug_println("  GET  /api/timers           - Alle timers");
  debug_println("  GET  /api/timers/{1-4}     - Enkelt timer");
  debug_println("  GET  /api/registers/hr/{addr}  - Læs holding register");
  debug_println("  POST /api/registers/hr/{addr}  - Skriv holding register");
  debug_println("  GET  /api/registers/coils/{addr} - Læs coil");
  debug_println("  POST /api/registers/coils/{addr} - Skriv coil");
  debug_println("  GET  /api/logic            - ST Logic status");
  debug_println("");
}

static void print_modbus_master_help(void) {
  debug_println("");
  debug_println("Available 'set modbus-master' commands:");
  debug_println("  set modbus-master enabled <on|off>        - Aktivér/deaktivér Modbus Master");
  debug_println("  set modbus-master baudrate <rate>         - Sæt baudrate (default: 9600)");
  debug_println("  set modbus-master parity <none|even|odd>  - Sæt parity (default: none)");
  debug_println("  set modbus-master stop-bits <1|2>         - Sæt stop bits (default: 1)");
  debug_println("  set modbus-master timeout <ms>            - Sæt timeout (default: 500ms)");
  debug_println("  set modbus-master inter-frame-delay <ms>  - Sæt inter-frame delay (default: 10ms)");
  debug_println("  set modbus-master max-requests <count>    - Sæt max requests per cycle (default: 10)");
  debug_println("");
  debug_println("Hardware:");
  debug_println("  UART1: TX=GPIO25, RX=GPIO26, DE/RE=GPIO27");
  debug_println("");
  debug_println("ST Logic Functions:");
  debug_println("  MB_READ_COIL(slave_id, address) → BOOL");
  debug_println("  MB_READ_INPUT(slave_id, address) → BOOL");
  debug_println("  MB_READ_HOLDING(slave_id, address) → INT");
  debug_println("  MB_READ_INPUT_REG(slave_id, address) → INT");
  debug_println("  MB_WRITE_COIL(slave_id, address, value) → BOOL");
  debug_println("  MB_WRITE_HOLDING(slave_id, address, value) → BOOL");
  debug_println("");
  debug_println("Global ST Variables:");
  debug_println("  mb_last_error (INT)  - Last error code (0=OK, 1=TIMEOUT, 2=CRC, 3=EXCEPTION, 4=MAX_REQ, 5=DISABLED)");
  debug_println("  mb_success (BOOL)    - TRUE if last operation succeeded");
  debug_println("");
}

static void print_modbus_slave_help(void) {
  debug_println("");
  debug_println("Available 'set modbus-slave' commands:");
  debug_println("  set modbus-slave enabled <on|off>        - Aktivér/deaktivér Modbus Slave");
  debug_println("  set modbus-slave slave-id <1-247>        - Sæt slave ID (default: 1)");
  debug_println("  set modbus-slave baudrate <rate>         - Sæt baudrate (default: 115200)");
  debug_println("  set modbus-slave parity <none|even|odd>  - Sæt parity (default: none)");
  debug_println("  set modbus-slave stop-bits <1|2>         - Sæt stop bits (default: 1)");
  debug_println("  set modbus-slave inter-frame-delay <ms>  - Sæt inter-frame delay (default: 10ms)");
  debug_println("");
  debug_println("Hardware:");
  debug_println("  UART0: Serial (shared with CLI)");
  debug_println("");
  debug_println("NOTE: All changes require 'save' + 'reboot' to take effect");
  debug_println("");
}

static void print_counter_help(void) {
  debug_println("");
  debug_println("Available 'set counter' commands:");
  debug_println("  set counter <id> mode 1 <key:value> ...");
  debug_println("");
  debug_println("Parameters (key:value format):");
  debug_println("  hw-mode:<sw|sw-isr|hw>     - Hardware mode");
  debug_println("  edge:<rising|falling|both> - Edge detection type");
  debug_println("  prescaler:<value>          - Prescaler divisor (1-65535)");
  debug_println("  scale:<float>              - Scale factor (default 1.0)");
  debug_println("  start-value:<value>        - Initial counter value");
  debug_println("  bit-width:<8|16|32|64>     - Counter bit width");
  debug_println("  direction:<up|down>        - Count direction");
  debug_println("  debounce:<on|off>          - Enable debounce");
  debug_println("  debounce-ms:<ms>           - Debounce time (default 10ms)");
  debug_println("  input-dis:<idx>            - Input discrete index (SW mode)");
  debug_println("  interrupt-pin:<pin>        - ISR mode GPIO pin");
  debug_println("  hw-gpio:<pin>              - HW mode GPIO pin (PCNT)");
  debug_println("  compare:<on|off>           - Enable compare feature");
  debug_println("  compare-value:<value>      - Compare threshold");
  debug_println("  compare-mode:<0|1|2>       - 0:≥, 1:>, 2:exact");
  debug_println("  reset-on-read:<on|off>     - Reset counter on read");
  debug_println("  enable:<on|off>            - Enable/disable counter");
  debug_println("  disable:<on|off>           - Disable counter (opposite of enable)");
  debug_println("");
  debug_println("NOTE: Register addresses are AUTO-ASSIGNED (v4.2.4+):");
  debug_println("  Counter 1 → HR100-114 (supports 64-bit multi-word values)");
  debug_println("    HR100-103: Index (scaled value, 1-4 words depending on bit-width)");
  debug_println("    HR104-107: Raw (prescaled value, 1-4 words)");
  debug_println("    HR108:     Frequency (Hz)");
  debug_println("    HR109:     Overload flag");
  debug_println("    HR110:     Control register (bit4=compare-match)");
  debug_println("    HR111-114: Compare value (1-4 words, runtime modifiable)");
  debug_println("  Counter 2 → HR120-134, Counter 3 → HR140-154, Counter 4 → HR160-174");
  debug_println("  Manual register configuration is DISABLED for safety.");
  debug_println("");
  debug_println("IMPORTANT: When copying from 'show config' output:");
  debug_println("  - Remove any 'index-reg', 'raw-reg', 'freq-reg', 'ctrl-reg', 'overload-reg' parameters");
  debug_println("  - These registers are auto-assigned and cannot be set manually");
  debug_println("");
  debug_println("Control commands:");
  debug_println("  set counter <id> control counter-reg-reset-on-read:<on|off>");
  debug_println("  set counter <id> control compare-reg-reset-on-read:<on|off>");
  debug_println("  set counter <id> control auto-start:<on|off>");
  debug_println("  set counter <id> control running:<on|off>");
  debug_println("  reset counter <id>         - Nulstil counter værdi");
  debug_println("  no set counter <id>        - Slet counter (disable)");
  debug_println("  clear counters             - Nulstil alle counters");
  debug_println("");
  debug_println("Examples:");
  debug_println("  set counter 1 mode 1 hw-mode:hw edge:rising prescaler:16 hw-gpio:25 \\");
  debug_println("    bit-width:32 scale:2.5 compare:on compare-value:2500 compare-mode:0");
  debug_println("  set counter 1 control running:on");
  debug_println("");
}

static void print_timer_help(void) {
  debug_println("");
  debug_println("Available 'set timer' commands:");
  debug_println("  set timer <id> mode <1|2|3|4> <key:value> ...");
  debug_println("");
  debug_println("Timer Modes:");
  debug_println("  1 - One-shot (3-phase sequence)");
  debug_println("  2 - Monostable (retriggerable pulse)");
  debug_println("  3 - Astable (blink/toggle)");
  debug_println("  4 - Input-triggered (responds to discrete inputs)");
  debug_println("");
  debug_println("Mode 1 Parameters (One-shot):");
  debug_println("  p1-duration:<ms>    - Phase 1 duration");
  debug_println("  p1-output:<0|1>     - Phase 1 output state");
  debug_println("  p2-duration:<ms>    - Phase 2 duration");
  debug_println("  p2-output:<0|1>     - Phase 2 output state");
  debug_println("  p3-duration:<ms>    - Phase 3 duration");
  debug_println("  p3-output:<0|1>     - Phase 3 output state");
  debug_println("");
  debug_println("Mode 2 Parameters (Monostable):");
  debug_println("  pulse-ms:<ms>       - Pulse duration");
  debug_println("  trigger-level:<0|1> - Trigger on LOW or HIGH");
  debug_println("");
  debug_println("Mode 3 Parameters (Astable):");
  debug_println("  on-ms:<ms>          - ON duration");
  debug_println("  off-ms:<ms>         - OFF duration");
  debug_println("");
  debug_println("Mode 4 Parameters (Input-triggered):");
  debug_println("  input-dis:<idx>     - Discrete input index");
  debug_println("  delay-ms:<ms>       - Delay before trigger");
  debug_println("  trigger-edge:<0|1>  - 0:falling, 1:rising");
  debug_println("");
  debug_println("Common Parameters:");
  debug_println("  output-coil:<idx>   - Output coil index");
  debug_println("  ctrl-reg:<addr>     - Control register address");
  debug_println("  enabled:<on|off>    - Enable/disable timer");
  debug_println("");
  debug_println("Example:");
  debug_println("  set timer 1 mode 3 on-ms:1000 off-ms:500 output-coil:0");
  debug_println("");
}

static void print_gpio_help(void) {
  debug_println("");
  debug_println("Available 'set gpio' commands:");
  debug_println("  set gpio <pin> coil <idx>         - Map GPIO til coil output");
  debug_println("  set gpio <pin> input <idx>        - Map GPIO til discrete input");
  debug_println("  set gpio <pin> mode <in|out|...>  - Sæt GPIO mode");
  debug_println("  no set gpio <pin>                 - Fjern GPIO mapping");
  debug_println("");
}

static void print_debug_help(void) {
  debug_println("");
  debug_println("Available 'set debug' commands:");
  debug_println("  set debug <flag> <on|off>  - Sæt debug flag");
  debug_println("Available flags:");
  debug_println("  modbus, counter, timer, logic, wifi, telnet, cli");
  debug_println("");
}

static void print_persist_help(void) {
  debug_println("");
  debug_println("Available 'set persist' commands (v4.0+):");
  debug_println("  set persist group <name> add <reg1> [reg2] ...  - Tilføj registre til gruppe");
  debug_println("  set persist group <name> remove <reg>           - Fjern register fra gruppe");
  debug_println("  set persist group <name> delete                 - Slet gruppe");
  debug_println("  set persist enable on|off                       - Aktivér/deaktivér system");
  debug_println("  set persist reset                               - Slet ALLE groups (nødvendigt ved corruption)");
  debug_println("");
  debug_println("Auto-Load on Boot (v4.3.0):");
  debug_println("  set persist auto-load enable                    - Aktivér auto-load ved boot");
  debug_println("  set persist auto-load disable                   - Deaktivér auto-load");
  debug_println("  set persist auto-load add <group_id>            - Tilføj gruppe til auto-load");
  debug_println("  set persist auto-load remove <group_id>         - Fjern gruppe fra auto-load");
  debug_println("");
  debug_println("Save & Restore:");
  debug_println("  save registers all             - Gem alle grupper til NVS");
  debug_println("  save registers group <name>    - Gem specifik gruppe til NVS");
  debug_println("  load registers all             - Gendan alle grupper fra NVS");
  debug_println("  load registers group <name>    - Gendan specifik gruppe fra NVS");
  debug_println("  show persist                   - Vis alle persistence groups (med auto-load status)");
  debug_println("");
  debug_println("ST Logic Integration (v4.3.0):");
  debug_println("  SAVE(0)         - Gem alle grupper fra ST program (rate limited)");
  debug_println("  SAVE(id)        - Gem specifik gruppe (id = 1-8, se 'show persist' for IDs)");
  debug_println("  LOAD(0)         - Gendan alle grupper fra ST program");
  debug_println("  LOAD(id)        - Gendan specifik gruppe (id = 1-8)");
  debug_println("");
  debug_println("Eksempel:");
  debug_println("  set persist group \"sensors\" add 100 101 102");
  debug_println("  save registers group \"sensors\"");
  debug_println("  set persist auto-load add 1         # Auto-load gruppe #1 ved boot");
  debug_println("  set persist auto-load enable        # Aktivér auto-load");
  debug_println("  show persist");
  debug_println("");
}

static void print_logic_help(void) {
  debug_println("");
  debug_println("Available 'show logic' commands:");
  debug_println("  show logic <id>          - Vis specifikt program (1-4, uden source)");
  debug_println("  show logic <id> st       - Vis program med ST source code (v5.1.0)");
  debug_println("  show logic all           - Vis alle programmer");
  debug_println("  show logic program       - Vis oversigt over alle programmer");
  debug_println("  show logic errors        - Vis kun programmer med fejl");
  debug_println("  show logic stats         - Vis statistik");
  debug_println("  show logic <id> code     - Vis program source code");
  debug_println("  show logic all code      - Vis alle programmer source code");
  debug_println("  show logic <id> timing   - Vis timing info (execution times)");
  debug_println("  show logic <id> bytecode - Vis compileret bytecode instruktioner");
  debug_println("  show logic <id> functions- Vis user-defined functions (FEAT-003)");
  debug_println("");
  debug_println("Available 'reset logic' commands:");
  debug_println("  reset logic stats      - Nulstil alle programs statistik");
  debug_println("  reset logic stats <id> - Nulstil specifik programs statistik");
  debug_println("");
}

/* ============================================================================
 * COMMAND DISPATCH
 * ============================================================================ */

bool cli_parser_execute(char* line) {
  if (!line || !line[0]) return false;

  // Tokenize
  #define MAX_ARGV 32
  char* argv[MAX_ARGV];
  uint8_t argc = tokenize(line, argv, MAX_ARGV);

  if (argc == 0) return false;

  // Normalize first token
  const char* cmd = normalize_alias(argv[0]);

  // Dispatch
  if (!strcmp(cmd, "SHOW")) {
    // show <what> [params...]
    if (argc < 2) {
      debug_println("SHOW: missing argument");
      return false;
    }

    const char* what = normalize_alias(argv[1]);

    // CHECK FOR HELP REQUEST
    if (!strcmp(what, "HELP") || !strcmp(what, "?")) {
      print_show_help();
      return true;
    }

    if (!strcmp(what, "CONFIG")) {
      // Support section filter: "show config wifi" or "show config | s wifi"
      const char *section_filter = nullptr;
      if (argc >= 3) {
        int idx = 2;
        // Skip pipe operator and "s"/"section" keyword if present
        if (!strcmp(argv[idx], "|")) idx++;
        if (idx < argc) {
          const char *kw = normalize_alias(argv[idx]);
          if (!strcmp(kw, "SHOW") || !strcmp(kw, "SECTION")) idx++;  // "s" normalizes to "SHOW"
        }
        if (idx < argc) {
          section_filter = argv[idx];
        }
      }
      cli_cmd_show_config(section_filter);
      return true;
    } else if (!strcmp(what, "COUNTERS")) {
      cli_cmd_show_counters();
      return true;
    } else if (!strcmp(what, "COUNTER")) {
      // show counter <id> [verbose]
      if (argc < 3) {
        debug_println("SHOW COUNTER: missing ID (use: show counter 1-4 [verbose])");
        return false;
      }
      uint8_t id = atoi(argv[2]);
      bool verbose = false;
      if (argc >= 4) {
        const char* flag = normalize_alias(argv[3]);
        verbose = (!strcmp(flag, "VERBOSE") || !strcmp(flag, "VERB"));
      }
      cli_cmd_show_counter(id, verbose);
      return true;
    } else if (!strcmp(what, "TIMERS")) {
      cli_cmd_show_timers();
      return true;
    } else if (!strcmp(what, "TIMER")) {
      // show timer <id> [verbose]
      if (argc < 3) {
        debug_println("SHOW TIMER: missing ID (use: show timer 1-4 [verbose])");
        return false;
      }
      uint8_t id = atoi(argv[2]);
      bool verbose = false;
      if (argc >= 4) {
        const char* flag = normalize_alias(argv[3]);
        verbose = (!strcmp(flag, "VERBOSE") || !strcmp(flag, "VERB"));
      }
      cli_cmd_show_timer(id, verbose);
      return true;
    } else if (!strcmp(what, "REGISTERS")) {
      uint16_t start = 0, count = 0;
      if (argc > 2) start = atoi(argv[2]);
      if (argc > 3) count = atoi(argv[3]);
      cli_cmd_show_registers(start, count);
      return true;
    } else if (!strcmp(what, "COILS")) {
      cli_cmd_show_coils();
      return true;
    } else if (!strcmp(what, "INPUTS")) {
      cli_cmd_show_inputs();
      return true;
    } else if (!strcmp(what, "ST-STATS") || !strcmp(what, "STATS")) {
      // show st-stats or show stats - ST Logic performance stats from Modbus IR 252-293
      cli_cmd_show_st_logic_stats_modbus();
      return true;
    } else if (!strcmp(what, "VERSION")) {
      cli_cmd_show_version();
      return true;
    } else if (!strcmp(what, "GPIO")) {
      // show gpio [pin]
      if (argc >= 3) {
        uint8_t pin = atoi(argv[2]);
        cli_cmd_show_gpio_pin(pin);
      } else {
        cli_cmd_show_gpio();
      }
      return true;
    } else if (!strcmp(what, "ECHO")) {
      cli_cmd_show_echo();
      return true;
    } else if (!strcmp(what, "WIFI")) {
      cli_cmd_show_wifi();
      return true;
    } else if (!strcmp(what, "ETHERNET")) {
      cli_cmd_show_ethernet();
      return true;
    } else if (!strcmp(what, "HTTP")) {
      cli_cmd_show_http();
      return true;
    } else if (!strcmp(what, "BACKUP")) {
      cli_cmd_show_backup();
      return true;
    } else if (!strcmp(what, "DEBUG")) {
      cli_cmd_show_debug();
      return true;
    } else if (!strcmp(what, "PERSIST")) {
      cli_cmd_show_persist();
      return true;
    } else if (!strcmp(what, "WATCHDOG")) {
      cli_cmd_show_watchdog();
      return true;
    } else if (!strcmp(what, "MODBUS-MASTER") || !strcmp(what, "MB-MASTER")) {
      cli_cmd_show_modbus_master();
      return true;
    } else if (!strcmp(what, "MODBUS-SLAVE") || !strcmp(what, "MB-SLAVE")) {
      cli_cmd_show_modbus_slave();
      return true;
    } else if (!strcmp(what, "H-REG")) {
      // show h-reg - Display register configuration
      cli_cmd_show_regs();
      return true;
    } else if (!strcmp(what, "COIL")) {
      // show coil - Display coil configuration
      cli_cmd_show_coils();
      return true;
    } else if (!strcmp(what, "LOGIC")) {
      // show logic <id|all|stats|program|errors|all code|1-4 code>
      if (argc < 3) {
        debug_println("SHOW LOGIC: missing argument. Use 'show logic ?' for help.");
        return false;
      }

      const char* subcommand = argv[2];
      const char* subcommand_norm = normalize_alias(subcommand);

      // CHECK FOR HELP REQUEST
      if (!strcmp(subcommand_norm, "HELP") || !strcmp(subcommand_norm, "?")) {
        print_logic_help();
        return true;
      }

      // Check for "code" subcommand FIRST (takes priority)
      // Syntax: show logic <id|all> code
      if (argc >= 4) {
        const char* subcommand2 = argv[3];
        const char* subcommand2_norm = normalize_alias(subcommand2);

        if (!strcmp(subcommand2_norm, "CODE")) {
          if (!strcmp(subcommand_norm, "ALL")) {
            // show logic all code
            cli_cmd_show_logic_code_all(st_logic_get_state());
            return true;
          } else {
            // show logic <id> code
            uint8_t program_id = atoi(subcommand);
            if (program_id > 0 && program_id <= ST_LOGIC_MAX_PROGRAMS) {
              cli_cmd_show_logic_code(st_logic_get_state(), program_id - 1);
              return true;
            } else {
              debug_printf("ERROR: Invalid program ID '%s' (expected 1-%d)\n", subcommand, ST_LOGIC_MAX_PROGRAMS);
              return false;
            }
          }
        } else if (!strcmp(subcommand2_norm, "TIMING")) {
          // show logic <id> timing - BUG-122 FIX
          uint8_t program_id = atoi(subcommand);
          if (program_id > 0 && program_id <= ST_LOGIC_MAX_PROGRAMS) {
            cli_cmd_show_logic_timing(st_logic_get_state(), program_id - 1);
            return true;
          } else {
            debug_printf("ERROR: Invalid program ID '%s' (expected 1-%d)\n", subcommand, ST_LOGIC_MAX_PROGRAMS);
            return false;
          }
        } else if (!strcmp(subcommand2_norm, "BYTECODE")) {
          // show logic <id> bytecode
          uint8_t program_id = atoi(subcommand);
          if (program_id > 0 && program_id <= ST_LOGIC_MAX_PROGRAMS) {
            cli_cmd_show_logic_bytecode(st_logic_get_state(), program_id - 1);
            return true;
          } else {
            debug_printf("ERROR: Invalid program ID '%s' (expected 1-%d)\n", subcommand, ST_LOGIC_MAX_PROGRAMS);
            return false;
          }
        } else if (!strcmp(subcommand2_norm, "FUNCTIONS")) {
          // FEAT-003: show logic <id> functions
          uint8_t program_id = atoi(subcommand);
          if (program_id > 0 && program_id <= ST_LOGIC_MAX_PROGRAMS) {
            cli_cmd_show_logic_functions(st_logic_get_state(), program_id - 1);
            return true;
          } else {
            debug_printf("ERROR: Invalid program ID '%s' (expected 1-%d)\n", subcommand, ST_LOGIC_MAX_PROGRAMS);
            return false;
          }
        } else if (!strcmp(subcommand2_norm, "ST")) {
          // show logic <id> st - v5.1.0: Show with source code
          uint8_t program_id = atoi(subcommand);
          if (program_id > 0 && program_id <= ST_LOGIC_MAX_PROGRAMS) {
            cli_cmd_show_logic_program(st_logic_get_state(), program_id - 1, 1);  // show_source=1
            return true;
          } else {
            debug_printf("ERROR: Invalid program ID '%s' (expected 1-%d)\n", subcommand, ST_LOGIC_MAX_PROGRAMS);
            return false;
          }
        } else if (!strcmp(subcommand2_norm, "DEBUG")) {
          // FEAT-008: show logic <id> debug [vars|stack]
          uint8_t program_id = atoi(subcommand);
          if (program_id < 1 || program_id > ST_LOGIC_MAX_PROGRAMS) {
            debug_printf("ERROR: Invalid program ID '%s' (expected 1-%d)\n", subcommand, ST_LOGIC_MAX_PROGRAMS);
            return false;
          }

          // Check for optional 4th argument (vars, stack)
          if (argc >= 5) {
            const char* debug_subcmd = argv[4];
            const char* debug_subcmd_norm = normalize_alias(debug_subcmd);
            if (!strcmp(debug_subcmd_norm, "VARS")) {
              cli_cmd_show_logic_debug_vars(st_logic_get_state(), program_id - 1);
              return true;
            } else if (!strcmp(debug_subcmd_norm, "STACK")) {
              cli_cmd_show_logic_debug_stack(st_logic_get_state(), program_id - 1);
              return true;
            }
          }
          // Default: show debug state
          cli_cmd_show_logic_debug(st_logic_get_state(), program_id - 1);
          return true;
        }
        // If argv[3] exists but is not "code"/"timing"/"bytecode"/"st"/"debug", fall through to normal handling
      }

      // Handle other subcommands (without code)
      if (!strcmp(subcommand_norm, "ALL")) {
        cli_cmd_show_logic_all(st_logic_get_state());
        return true;
      } else if (!strcmp(subcommand_norm, "STATS")) {
        cli_cmd_show_logic_stats(st_logic_get_state());
        return true;
      } else if (!strcmp(subcommand_norm, "PROGRAM")) {
        cli_cmd_show_logic_programs(st_logic_get_state());
        return true;
      } else if (!strcmp(subcommand_norm, "ERRORS")) {
        cli_cmd_show_logic_errors(st_logic_get_state());
        return true;
      } else {
        // show logic <id> - specific program (hide source code by default - v5.1.0)
        uint8_t program_id = atoi(subcommand);
        if (program_id > 0 && program_id <= ST_LOGIC_MAX_PROGRAMS) {
          cli_cmd_show_logic_program(st_logic_get_state(), program_id - 1, 0);  // show_source=0
          return true;
        } else {
          debug_printf("ERROR: Invalid program ID '%s' (expected 1-%d or all|stats|program|errors)\n", subcommand, ST_LOGIC_MAX_PROGRAMS);
          return false;
        }
      }
    } else if (!strcmp(what, "MODBUS-MASTER") || !strcmp(what, "MB-MASTER")) {
      // show modbus-master - Display Modbus Master configuration
      cli_cmd_show_modbus_master();
      return true;
    } else if (!strcmp(what, "MODBUS-SLAVE") || !strcmp(what, "MB-SLAVE")) {
      // show modbus-slave - Display Modbus Slave configuration
      cli_cmd_show_modbus_slave();
      return true;
    } else {
      debug_println("SHOW: unknown argument");
      return false;
    }

  } else if (!strcmp(cmd, "SET")) {
    // set <what> <params...>
    if (argc < 2) {
      debug_println("SET: missing argument. Use 'set ?' for help.");
      return false;
    }

    const char* what = normalize_alias(argv[1]);

    // CHECK FOR HELP REQUEST
    if (!strcmp(what, "HELP") || !strcmp(what, "?")) {
      print_set_help();
      return true;
    }

    if (!strcmp(what, "COUNTER")) {
      // Check for help
      if (argc >= 3) {
        const char* subwhat = normalize_alias(argv[2]);
        if (!strcmp(subwhat, "HELP") || !strcmp(subwhat, "?")) {
          print_counter_help();
          return true;
        }
      }
      // Check if third argument is "control"
      if (argc >= 4 && (!strcmp(argv[3], "control") || !strcmp(argv[3], "CONTROL"))) {
        // set counter <id> control ... → skip "counter" and "control", pass <id> + params
        // argv[0] = "set", argv[1] = "counter", argv[2] = id, argv[3] = "control", argv[4+] = params
        char* control_argv[32];
        control_argv[0] = argv[2];  // counter id
        for (uint8_t i = 4; i < argc && i < 32; i++) {
          control_argv[i - 3] = argv[i];
        }
        cli_cmd_set_counter_control(argc - 3, control_argv);
      } else {
        cli_cmd_set_counter(argc - 2, argv + 2);
      }
      return true;
    } else if (!strcmp(what, "TIMER")) {
      // Check for help
      if (argc >= 3) {
        const char* subwhat = normalize_alias(argv[2]);
        if (!strcmp(subwhat, "HELP") || !strcmp(subwhat, "?")) {
          print_timer_help();
          return true;
        }
      }
      cli_cmd_set_timer(argc - 2, argv + 2);
      return true;
    } else if (!strcmp(what, "HOSTNAME")) {
      if (argc < 3) {
        debug_println("SET HOSTNAME: missing value");
        return false;
      }
      cli_cmd_set_hostname(argv[2]);
      return true;
    } else if (!strcmp(what, "BAUD")) {
      if (argc < 3) {
        debug_println("SET BAUD: missing value");
        return false;
      }
      uint32_t baud = atol(argv[2]);
      cli_cmd_set_baud(baud);
      return true;
    } else if (!strcmp(what, "ID")) {
      if (argc < 3) {
        debug_println("SET ID: missing value");
        return false;
      }
      uint8_t id = atoi(argv[2]);
      cli_cmd_set_id(id);
      return true;
    } else if (!strcmp(what, "H-REG")) {
      if (argc < 3) {
        debug_println("SET HOLDING-REG: missing parameters");
        debug_println("  Usage: set h-reg STATIC <address> Value [type] <value>");
        debug_println("         set h-reg DYNAMIC <address> counter<id>:<function> or timer<id>:<function>");
        debug_println("  Types: uint (default), int, dint, dword, real");
        return false;
      }

      const char* mode = normalize_alias(argv[2]);

      if (!strcmp(mode, "STATIC")) {
        // set holding-reg STATIC <address> Value <value>
        cli_cmd_set_reg_static(argc - 3, argv + 3);
      } else if (!strcmp(mode, "DYNAMIC")) {
        // set holding-reg DYNAMIC <address> counter<id>:<function> or timer<id>:<function>
        cli_cmd_set_reg_dynamic(argc - 3, argv + 3);
      } else {
        debug_println("SET HOLDING-REG: invalid mode (must be STATIC or DYNAMIC)");
        return false;
      }
      return true;

    } else if (!strcmp(what, "COIL")) {
      if (argc < 3) {
        debug_println("SET COIL: missing parameters");
        debug_println("  Usage: set coil STATIC <address> Value <ON|OFF>");
        debug_println("         set coil DYNAMIC <address> counter<id>:<function> or timer<id>:<function>");
        return false;
      }

      const char* mode = normalize_alias(argv[2]);

      if (!strcmp(mode, "STATIC")) {
        // set coil STATIC <address> Value <ON|OFF>
        cli_cmd_set_coil_static(argc - 3, argv + 3);
      } else if (!strcmp(mode, "DYNAMIC")) {
        // set coil DYNAMIC <address> counter<id>:<function> or timer<id>:<function>
        cli_cmd_set_coil_dynamic(argc - 3, argv + 3);
      } else {
        debug_println("SET COIL: invalid mode (must be STATIC or DYNAMIC)");
        return false;
      }
      return true;
    } else if (!strcmp(what, "GPIO")) {
      // Check for help
      if (argc >= 3) {
        const char* subwhat = normalize_alias(argv[2]);
        if (!strcmp(subwhat, "HELP") || !strcmp(subwhat, "?")) {
          print_gpio_help();
          return true;
        }
      }
      // Check if it's GPIO 2 enable/disable command
      if (argc >= 4 && atoi(argv[2]) == 2) {
        const char* action = normalize_alias(argv[3]);
        if (!strcmp(action, "ENABLE") || !strcmp(action, "DISABLE")) {
          // set gpio 2 enable/disable
          cli_cmd_set_gpio2(argc - 2, argv + 2);
          return true;
        }
      }
      // Otherwise, handle as normal GPIO mapping
      cli_cmd_set_gpio(argc - 2, argv + 2);
      return true;
    } else if (!strcmp(what, "ECHO")) {
      cli_cmd_set_echo(argc - 2, argv + 2);
      return true;
    } else if (!strcmp(what, "DEBUG")) {
      // Check for help
      if (argc >= 3) {
        const char* subwhat = normalize_alias(argv[2]);
        if (!strcmp(subwhat, "HELP") || !strcmp(subwhat, "?")) {
          print_debug_help();
          return true;
        }
      }
      cli_cmd_set_debug(argc - 2, argv + 2);
      return true;
    } else if (!strcmp(what, "WIFI")) {
      // Check for help
      if (argc >= 3) {
        const char* subwhat = normalize_alias(argv[2]);
        if (!strcmp(subwhat, "HELP") || !strcmp(subwhat, "?")) {
          print_wifi_help();
          return true;
        }
      }
      // set wifi <option> <value>
      if (argc < 3) {
        print_wifi_help();
        return true;
      }
      cli_cmd_set_wifi(argc - 2, argv + 2);
      return true;
    } else if (!strcmp(what, "ETHERNET")) {
      // set ethernet <option> [value]
      if (argc >= 3) {
        const char* subwhat = normalize_alias(argv[2]);
        if (!strcmp(subwhat, "HELP") || !strcmp(subwhat, "?")) {
          print_ethernet_help();
          return true;
        }
      }
      if (argc < 3) {
        print_ethernet_help();
        return true;
      }
      cli_cmd_set_ethernet(argc - 2, argv + 2);
      return true;
    } else if (!strcmp(what, "HTTP")) {
      // Check for help
      if (argc >= 3) {
        const char* subwhat = normalize_alias(argv[2]);
        if (!strcmp(subwhat, "HELP") || !strcmp(subwhat, "?")) {
          print_http_help();
          return true;
        }
      }
      // set http <option> <value>
      if (argc < 3) {
        print_http_help();
        return true;
      }
      cli_cmd_set_http(argc - 2, argv + 2);
      return true;
    } else if (!strcmp(what, "PERSIST")) {
      // Check for help
      if (argc >= 3) {
        const char* subwhat_check = normalize_alias(argv[2]);
        if (!strcmp(subwhat_check, "HELP") || !strcmp(subwhat_check, "?")) {
          print_persist_help();
          return true;
        }
      }

      // set persist group <name> add|remove|delete [regs...]
      // set persist enable on|off
      if (argc < 3) {
        print_persist_help();
        return false;
      }

      const char* subwhat = normalize_alias(argv[2]);

      if (!strcmp(subwhat, "GROUP")) {
        // set persist group ...
        cli_cmd_set_persist_group(argc - 3, argv + 3);
        return true;
      } else if (!strcmp(subwhat, "ENABLE")) {
        // set persist enable on|off
        if (argc < 4) {
          debug_println("SET PERSIST ENABLE: missing value (on|off)");
          return false;
        }
        const char* onoff = normalize_alias(argv[3]);
        bool enabled = (!strcmp(onoff, "ON") || !strcmp(onoff, "1") || !strcmp(onoff, "TRUE"));
        cli_cmd_set_persist_enable(enabled);
        return true;
      } else if (!strcmp(subwhat, "AUTO-LOAD")) {
        // set persist auto-load enable|disable|add|remove ...
        cli_cmd_set_persist_auto_load(argc - 3, argv + 3);
        return true;
      } else if (!strcmp(subwhat, "RESET") || !strcmp(subwhat, "CLEAR")) {
        // set persist reset - BUG-140 fix for corrupted groups
        cli_cmd_set_persist_reset();
        return true;
      } else {
        debug_println("SET PERSIST: unknown argument (expected group, enable, auto-load, or reset)");
        return false;
      }
    } else if (!strcmp(what, "LOGIC")) {
      // set logic debug:true|false  (global debug flag - no program ID needed)
      if (argc >= 3) {
        const char* arg = argv[2];
        if (strstr(arg, "debug:")) {
          bool debug = (strstr(arg, "true")) ? true : false;
          cli_cmd_set_logic_debug(st_logic_get_state(), debug);
          return true;
        }

        // set logic interval:X  (global execution interval - v4.1.0)
        if (strstr(arg, "interval:")) {
          const char* interval_str = strchr(arg, ':') + 1;
          uint32_t interval_ms = atoi(interval_str);
          cli_cmd_set_logic_interval(st_logic_get_state(), interval_ms);
          return true;
        }

        // set logic interval <X>  (space-separated variant)
        const char* arg_norm = normalize_alias(arg);
        if (!strcmp(arg_norm, "INTERVAL")) {
          if (argc >= 4) {
            uint32_t interval_ms = atoi(argv[3]);
            cli_cmd_set_logic_interval(st_logic_get_state(), interval_ms);
          } else {
            debug_println("SET LOGIC INTERVAL: missing value");
            debug_println("  Usage: set logic interval <ms>  (10,20,25,50,75,100)");
          }
          return true;
        }
      }

      // set logic <id> <subcommand> [params...]
      if (argc < 4) {
        debug_println("SET LOGIC: missing arguments");
        debug_println("  Usage (inline):    set logic <id> upload \"<code>\"");
        debug_println("  Usage (multi-line): set logic <id> upload");
        debug_println("                      [type code line by line]");
        debug_println("                      [then type END_UPLOAD]");
        debug_println("");
        debug_println("  Also:");
        debug_println("         set logic <id> enabled:true|false");
        debug_println("         set logic <id> delete");
        debug_println("         set logic <id> bind <var_name> reg:100|coil:10|input:5");
        debug_println("         set logic debug:true|false");
        debug_println("         set logic interval:X  (X = 10,20,25,50,75,100 ms)");
        return false;
      }

      uint8_t program_id = atoi(argv[2]);
      const char* subcommand = argv[3];  // Don't normalize yet - may be key:value

      // Validate program_id (1-4 user facing, 0-3 internal)
      if (program_id < 1 || program_id > ST_LOGIC_MAX_PROGRAMS) {
        debug_printf("ERROR: Invalid program ID %d (expected 1-%d)\n", program_id, ST_LOGIC_MAX_PROGRAMS);
        return false;
      }
      uint8_t prog_idx = program_id - 1;  // Convert to 0-based index

      // Check for enabled:true|false format first (special case)
      if (strstr(subcommand, "enabled:")) {
        bool enabled = (strstr(subcommand, "true")) ? true : false;
        cli_cmd_set_logic_enabled(st_logic_get_state(), prog_idx, enabled);
        return true;
      }

      // Now normalize for other commands
      const char* cmd_normalized = normalize_alias(subcommand);

      // set logic <id> enabled|disabled (space-separated variant)
      if (!strcmp(cmd_normalized, "ENABLED") || !strcmp(cmd_normalized, "ENABLE")) {
        cli_cmd_set_logic_enabled(st_logic_get_state(), prog_idx, true);
        return true;
      } else if (!strcmp(cmd_normalized, "DISABLED") || !strcmp(cmd_normalized, "DISABLE")) {
        cli_cmd_set_logic_enabled(st_logic_get_state(), prog_idx, false);
        return true;
      }

      if (!strcmp(cmd_normalized, "UPLOAD")) {
        // set logic <id> upload "<code>"   OR   set logic <id> upload (multi-line mode)

        if (argc < 5) {
          // No code provided - start multi-line upload mode
          // Start multi-line upload mode
          cli_shell_start_st_upload(prog_idx);  // Use 0-based index
          return true;
        }

        // Code provided in same command - use inline upload (backward compatible)
        cli_cmd_set_logic_upload(st_logic_get_state(), prog_idx, argv[4]);
        return true;
      } else if (!strcmp(cmd_normalized, "DELETE")) {
        // set logic <id> delete
        cli_cmd_set_logic_delete(st_logic_get_state(), prog_idx);
        return true;
      } else if (!strcmp(cmd_normalized, "BIND")) {
        // set logic <id> bind <var_spec> <register_spec> [direction]
        if (argc < 6) {
          debug_println("SET LOGIC BIND: missing parameters");
          debug_println("  Usage (NEW):  set logic <id> bind <var_name> reg:100|coil:10|input:5 [input|output|both]");
          debug_println("  Usage (OLD):  set logic <id> bind <var_idx> <register> [input|output|both]");
          return false;
        }

        const char* arg4 = argv[4];
        const char* arg5 = argv[5];
        const char* arg6 = (argc >= 7) ? argv[6] : NULL;  // optional direction

        // Detect new syntax: check if arg5 contains "reg:", "coil:", "input-dis:", or "input:"
        if (strstr(arg5, "reg:") || strstr(arg5, "coil:") || strstr(arg5, "input-dis:") || strstr(arg5, "input:")) {
          // NEW SYNTAX: variable name + binding spec + optional direction
          cli_cmd_set_logic_bind_by_name(st_logic_get_state(), prog_idx, arg4, arg5, arg6);
          return true;
        }

        // OLD SYNTAX: variable index + register + direction
        // Backward compatible: set logic <id> bind <var_idx> <register> [input|output|both]
        uint8_t var_idx = atoi(arg4);
        uint16_t register_addr = atoi(arg5);
        const char* direction = (argc > 6) ? argv[6] : "both";
        uint8_t input_type = 0;  // Default: Holding Register (HR), not Discrete Input
        uint8_t output_type = 0;  // Default: Holding Register (HR), not Coil

        cli_cmd_set_logic_bind(st_logic_get_state(), prog_idx, var_idx, register_addr, direction, input_type, output_type);
        return true;
      } else if (!strcmp(cmd_normalized, "DEBUG")) {
        // FEAT-008: set logic <id> debug <pause|continue|step|break|clear|stop>
        if (argc < 5) {
          debug_println("SET LOGIC DEBUG: missing subcommand");
          debug_println("  Usage: set logic <id> debug pause");
          debug_println("         set logic <id> debug continue");
          debug_println("         set logic <id> debug step");
          debug_println("         set logic <id> debug break <pc>");
          debug_println("         set logic <id> debug clear [<pc>]");
          debug_println("         set logic <id> debug stop");
          return false;
        }

        const char* debug_cmd = normalize_alias(argv[4]);

        // Help request
        if (!strcmp(debug_cmd, "HELP") || !strcmp(argv[4], "?")) {
          debug_println("\nST Logic Debug Commands:");
          debug_println("  set logic <id> debug pause         - Pause at next instruction");
          debug_println("  set logic <id> debug continue      - Continue until breakpoint/halt");
          debug_println("  set logic <id> debug step          - Execute one instruction");
          debug_println("  set logic <id> debug break <pc>    - Set breakpoint at PC address");
          debug_println("  set logic <id> debug break line <n>- Set breakpoint at source line");
          debug_println("  set logic <id> debug clear [pc]    - Clear breakpoint(s)");
          debug_println("  set logic <id> debug stop          - Stop debugging, resume normal");
          debug_println("\nShow Commands:");
          debug_println("  show logic <id> debug              - Show debug state");
          debug_println("  show logic <id> debug vars         - Show variable values");
          debug_println("");
          return true;
        } else if (!strcmp(debug_cmd, "PAUSE")) {
          cli_cmd_set_logic_debug_pause(st_logic_get_state(), prog_idx);
          return true;
        } else if (!strcmp(debug_cmd, "CONTINUE")) {
          cli_cmd_set_logic_debug_continue(st_logic_get_state(), prog_idx);
          return true;
        } else if (!strcmp(debug_cmd, "STEP")) {
          cli_cmd_set_logic_debug_step(st_logic_get_state(), prog_idx);
          return true;
        } else if (!strcmp(debug_cmd, "BREAK")) {
          if (argc < 6) {
            debug_println("SET LOGIC DEBUG BREAK: missing address");
            debug_println("  Usage: set logic <id> debug break <pc>");
            debug_println("         set logic <id> debug break line <line_number>");
            return false;
          }

          // Check for "line" keyword
          const char* break_arg = normalize_alias(argv[5]);
          if (!strcmp(break_arg, "LINE")) {
            // Breakpoint by source line number
            if (argc < 7) {
              debug_println("SET LOGIC DEBUG BREAK LINE: missing line number");
              debug_println("  Usage: set logic <id> debug break line <line_number>");
              return false;
            }
            uint16_t line = atoi(argv[6]);
            cli_cmd_set_logic_debug_breakpoint_line(st_logic_get_state(), prog_idx, line);
          } else {
            // Breakpoint by PC address
            uint16_t pc = atoi(argv[5]);
            cli_cmd_set_logic_debug_breakpoint(st_logic_get_state(), prog_idx, pc);
          }
          return true;
        } else if (!strcmp(debug_cmd, "CLEAR")) {
          int pc = -1;  // Default: clear all
          if (argc >= 6) {
            pc = atoi(argv[5]);  // Clear specific breakpoint
          }
          cli_cmd_set_logic_debug_clear(st_logic_get_state(), prog_idx, pc);
          return true;
        } else if (!strcmp(debug_cmd, "STOP")) {
          cli_cmd_set_logic_debug_stop(st_logic_get_state(), prog_idx);
          return true;
        } else {
          debug_printf("SET LOGIC DEBUG: unknown command '%s'\n", argv[4]);
          debug_println("  Valid: pause, continue, step, break, clear, stop");
          return false;
        }
      } else {
        debug_println("SET LOGIC: unknown subcommand");
        return false;
      }
    } else if (!strcmp(what, "MODBUS-MASTER") || !strcmp(what, "MB-MASTER")) {
      // Check for help request
      if (argc >= 3) {
        const char* subwhat = normalize_alias(argv[2]);
        if (!strcmp(subwhat, "HELP") || !strcmp(subwhat, "?")) {
          print_modbus_master_help();
          return true;
        }
      }

      // set modbus-master <param> <value>
      if (argc < 4) {
        debug_println("SET MODBUS-MASTER: missing parameters");
        debug_println("  Usage: set modbus-master <param> <value>");
        debug_println("  Params: enabled, baudrate, parity, stop-bits, timeout, inter-frame-delay, max-requests");
        debug_println("  Brug 'set modbus-master ?' for detaljeret hjælp");
        return false;
      }

      const char* param = normalize_alias(argv[2]);
      const char* value = argv[3];

      if (!strcmp(param, "ENABLED")) {
        bool enabled = (!strcmp(value, "on") || !strcmp(value, "ON") || !strcmp(value, "1") || !strcmp(value, "true"));
        cli_cmd_set_modbus_master_enabled(enabled);
        return true;
      } else if (!strcmp(param, "BAUDRATE") || !strcmp(param, "BAUD")) {
        uint32_t baudrate = atol(value);
        cli_cmd_set_modbus_master_baudrate(baudrate);
        return true;
      } else if (!strcmp(param, "PARITY")) {
        cli_cmd_set_modbus_master_parity(value);
        return true;
      } else if (!strcmp(param, "STOP-BITS")) {
        uint8_t bits = atoi(value);
        cli_cmd_set_modbus_master_stop_bits(bits);
        return true;
      } else if (!strcmp(param, "TIMEOUT")) {
        uint16_t timeout = atoi(value);
        cli_cmd_set_modbus_master_timeout(timeout);
        return true;
      } else if (!strcmp(param, "INTER-FRAME-DELAY") || !strcmp(param, "DELAY")) {
        uint16_t delay = atoi(value);
        cli_cmd_set_modbus_master_inter_frame_delay(delay);
        return true;
      } else if (!strcmp(param, "MAX-REQUESTS")) {
        uint8_t count = atoi(value);
        cli_cmd_set_modbus_master_max_requests(count);
        return true;
      } else {
        debug_println("SET MODBUS-MASTER: unknown parameter");
        return false;
      }
    } else if (!strcmp(what, "MODBUS-SLAVE") || !strcmp(what, "MB-SLAVE")) {
      // Check for help request
      if (argc >= 3) {
        const char* subwhat = normalize_alias(argv[2]);
        if (!strcmp(subwhat, "HELP") || !strcmp(subwhat, "?")) {
          print_modbus_slave_help();
          return true;
        }
      }

      // set modbus-slave <param> <value>
      if (argc < 4) {
        debug_println("SET MODBUS-SLAVE: missing parameters");
        debug_println("  Usage: set modbus-slave <param> <value>");
        debug_println("  Params: enabled, slave-id, baudrate, parity, stop-bits, inter-frame-delay");
        debug_println("  Brug 'set modbus-slave ?' for detaljeret hjælp");
        return false;
      }

      const char* param = normalize_alias(argv[2]);
      const char* value = argv[3];

      if (!strcmp(param, "ENABLED")) {
        bool enabled = (!strcmp(value, "on") || !strcmp(value, "ON") || !strcmp(value, "1") || !strcmp(value, "true"));
        cli_cmd_set_modbus_slave_enabled(enabled);
        return true;
      } else if (!strcmp(param, "SLAVE-ID") || !strcmp(param, "ID")) {
        uint8_t id = atoi(value);
        cli_cmd_set_modbus_slave_slave_id(id);
        return true;
      } else if (!strcmp(param, "BAUDRATE") || !strcmp(param, "BAUD")) {
        uint32_t baudrate = atol(value);
        cli_cmd_set_modbus_slave_baudrate(baudrate);
        return true;
      } else if (!strcmp(param, "PARITY")) {
        cli_cmd_set_modbus_slave_parity(value);
        return true;
      } else if (!strcmp(param, "STOP-BITS")) {
        uint8_t bits = atoi(value);
        cli_cmd_set_modbus_slave_stop_bits(bits);
        return true;
      } else if (!strcmp(param, "INTER-FRAME-DELAY") || !strcmp(param, "DELAY")) {
        uint16_t delay = atoi(value);
        cli_cmd_set_modbus_slave_inter_frame_delay(delay);
        return true;
      } else {
        debug_println("SET MODBUS-SLAVE: unknown parameter");
        return false;
      }
    } else {
      debug_println("SET: unknown argument");
      return false;
    }

  } else if (!strcmp(cmd, "NO")) {
    // no set <what> <params...>
    if (argc < 2) {
      debug_println("NO: missing argument (expected 'set')");
      return false;
    }

    if (!strcmp(normalize_alias(argv[1]), "SET")) {
      // no set <what> <params...>
      if (argc < 3) {
        debug_println("NO SET: missing argument");
        return false;
      }

      const char* what = normalize_alias(argv[2]);

      if (!strcmp(what, "GPIO")) {
        cli_cmd_no_set_gpio(argc - 3, argv + 3);
        return true;
      } else if (!strcmp(what, "COUNTER")) {
        // no set counter <id>
        cli_cmd_delete_counter(argc - 3, argv + 3);
        return true;
      } else {
        debug_println("NO SET: unknown argument (supported: GPIO, COUNTER)");
        return false;
      }
    } else {
      debug_println("NO: unknown argument (expected 'set')");
      return false;
    }

  } else if (!strcmp(cmd, "RESET")) {
    // reset counter <id>
    if (argc < 2) {
      debug_println("RESET: missing argument");
      return false;
    }

    const char* what = normalize_alias(argv[1]);

    if (!strcmp(what, "COUNTER")) {
      cli_cmd_reset_counter(argc - 2, argv + 2);
      return true;
    } else if (!strcmp(what, "LOGIC")) {
      // reset logic stats [all|<id>] - BUG-122 FIX
      if (argc < 3) {
        debug_println("RESET LOGIC: missing argument (expected 'stats')");
        return false;
      }
      const char* sub = normalize_alias(argv[2]);
      if (!strcmp(sub, "STATS")) {
        // reset logic stats [all|<id>]
        const char* target = (argc >= 4) ? argv[3] : "all";
        cli_cmd_reset_logic_stats(st_logic_get_state(), target);
        return true;
      } else {
        debug_println("RESET LOGIC: unknown subcommand (expected 'stats')");
        return false;
      }
    } else {
      debug_println("RESET: unknown argument");
      return false;
    }

  } else if (!strcmp(cmd, "CLEAR")) {
    // clear counters
    if (argc < 2) {
      debug_println("CLEAR: missing argument");
      return false;
    }

    const char* what = normalize_alias(argv[1]);

    if (!strcmp(what, "COUNTERS")) {
      cli_cmd_clear_counters();
      return true;
    } else {
      debug_println("CLEAR: unknown argument");
      return false;
    }

  } else if (!strcmp(cmd, "SAVE")) {
    // save  OR  save registers all  OR  save registers group <name>
    if (argc >= 2 && !strcmp(normalize_alias(argv[1]), "REGISTERS")) {
      // save registers all|group <name>
      cli_cmd_save_registers(argc - 2, argv + 2);
      return true;
    } else {
      // save (traditional - save config only)
      cli_cmd_save();
      return true;
    }

  } else if (!strcmp(cmd, "LOAD")) {
    // load  OR  load registers all  OR  load registers group <name>
    if (argc >= 2 && !strcmp(normalize_alias(argv[1]), "REGISTERS")) {
      // load registers all|group <name>
      cli_cmd_load_registers(argc - 2, argv + 2);
      return true;
    } else {
      // load (traditional - load entire config)
      cli_cmd_load();
      return true;
    }

  } else if (!strcmp(cmd, "CONFIG")) {
    // config save (alias for 'save') OR config load (alias for 'load')
    if (argc >= 2) {
      const char* subcmd = normalize_alias(argv[1]);
      if (!strcmp(subcmd, "SAVE")) {
        cli_cmd_save();
        return true;
      } else if (!strcmp(subcmd, "LOAD")) {
        cli_cmd_load();
        return true;
      }
    }
    debug_println("CONFIG: Use 'config save' or 'config load'");
    return false;

  } else if (!strcmp(cmd, "DEFAULTS")) {
    cli_cmd_defaults();
    return true;

  } else if (!strcmp(cmd, "REBOOT")) {
    cli_cmd_reboot();
    return true;

  } else if (!strcmp(cmd, "EXIT")) {
    cli_cmd_exit();
    return true;

  } else if (!strcmp(cmd, "CONNECT")) {
    // connect wifi
    if (argc >= 2 && !strcmp(normalize_alias(argv[1]), "WIFI")) {
      cli_cmd_connect_wifi();
      return true;
    } else {
      debug_println("CONNECT: unknown target (use: wifi)");
      return false;
    }

  } else if (!strcmp(cmd, "DISCONNECT")) {
    // disconnect wifi
    if (argc >= 2 && !strcmp(normalize_alias(argv[1]), "WIFI")) {
      cli_cmd_disconnect_wifi();
      return true;
    } else {
      debug_println("DISCONNECT: unknown target (use: wifi)");
      return false;
    }

  } else if (!strcmp(cmd, "HELP")) {
    cli_parser_print_help();
    return true;

  } else if (!strcmp(cmd, "COMMANDS")) {
    // Print compact list of all available commands
    debug_println("\n=== AVAILABLE COMMANDS ===\n");
    debug_println("Quick reference - use 'help' for detailed info\n");

    debug_println("System:");
    debug_println("  help, ?, h              - Show detailed help");
    debug_println("  commands, cmds          - Show this command list");
    debug_println("  save, sv, config save   - Save config to NVS");
    debug_println("  load, ld, config load   - Load config from NVS");
    debug_println("  defaults, def           - Reset to defaults");
    debug_println("  reboot, restart         - Reboot ESP32");
    debug_println("  exit, quit, q           - Exit telnet session\n");

    debug_println("Show/Display (sh, s):");
    debug_println("  show config, cfg        - Full configuration");
    debug_println("  show version, ver, v    - Firmware version");
    debug_println("  show wifi               - WiFi status + RSSI + MAC");
    debug_println("  show ethernet, eth      - Ethernet (W5500) status");
    debug_println("  show counters, cnts     - All counters table");
    debug_println("  show counter <id> [verbose] - Counter 1-4 details");
    debug_println("  show timers, tmrs       - All timers table");
    debug_println("  show timer <id> [verbose]   - Timer 1-4 details");
    debug_println("  show logic, log         - ST Logic programs");
    debug_println("  show gpio [pin]         - GPIO mappings");
    debug_println("  show registers, regs    - Holding registers");
    debug_println("  show inputs, ins        - Discrete inputs");
    debug_println("  show coils              - Coils");
    debug_println("  show debug, dbg         - Debug flags");
    debug_println("  show watchdog, wdg      - Watchdog status");
    debug_println("  show persist            - Persistence groups");
    debug_println("  show modbus-master, mb-master - Modbus master config");
    debug_println("  show modbus-slave, mb-slave   - Modbus slave config");
    debug_println("  show backup             - Backup/restore URL\n");

    debug_println("Set/Configure:");
    debug_println("  set counter <id> ?      - Counter help");
    debug_println("  set timer <id> ?        - Timer help");
    debug_println("  set gpio ?              - GPIO help");
    debug_println("  set wifi ?              - WiFi help");
    debug_println("  set ethernet ?          - Ethernet (W5500) help");
    debug_println("  set debug ?             - Debug help");
    debug_println("  set persist ?           - Persistence help");
    debug_println("  set modbus-master ?     - Modbus master help");
    debug_println("  set modbus-slave ?      - Modbus slave help");
    debug_println("  set hostname <name>     - Set hostname");
    debug_println("  set echo on|off         - Remote echo\n");

    debug_println("Modbus Read/Write (r, w):");
    debug_println("  read h-reg <addr> [count] [type]  - Read holding registers");
    debug_println("  read coil <addr> [count]           - Read coils");
    debug_println("  read input <addr> [count]          - Read discrete inputs");
    debug_println("  read i-reg <addr> [count] [type]   - Read input registers");
    debug_println("  write h-reg <addr> value uint <val> - Write unsigned holding register");
    debug_println("  write h-reg <addr> value int <val>  - Write signed holding register");
    debug_println("  write coil <addr> value <0|1>      - Write coil\n");

    debug_println("Network:");
    debug_println("  connect wifi, con       - Connect to WiFi");
    debug_println("  disconnect wifi, dc     - Disconnect WiFi\n");

    debug_println("Reset/Clear (rst, clr):");
    debug_println("  reset counter <id>      - Reset counter value");
    debug_println("  reset logic stats [id]  - Reset logic stats (all or specific)");
    debug_println("  clear counters          - Reset all counters\n");

    debug_println("Delete:");
    debug_println("  no set counter <id>     - Delete counter config");
    debug_println("  no set timer <id>       - Delete timer config");
    debug_println("  no set gpio <pin>       - Delete GPIO mapping\n");

    debug_println("Aliases:");
    debug_println("  sh|s → show");
    debug_println("  cnt|cntr → counter");
    debug_println("  tmr → timer");
    debug_println("  cfg → config");
    debug_println("  ver|v → version");
    debug_println("  regs → registers");
    debug_println("  ins → inputs");
    debug_println("  log → logic");
    debug_println("  dbg → debug");
    debug_println("  wdg → watchdog");
    debug_println("  verb → verbose");
    debug_println("  sv → save");
    debug_println("  ld → load");
    debug_println("  def → defaults");
    debug_println("  rst → reset/reboot");
    debug_println("  clr → clear");
    debug_println("  con → connect");
    debug_println("  dc → disconnect");
    debug_println("  rd|r → read");
    debug_println("  wr|w → write");
    debug_println("  q → quit\n");

    debug_println("Type 'help' for detailed command documentation.");
    debug_println("");
    return true;

  } else if (!strcmp(cmd, "READ")) {
    // read <what> <params...>
    if (argc < 2) {
      debug_println("READ: manglende argument");
      debug_println("  Brug: read h-reg <id> [antal] [type]");
      debug_println("        read i-reg <id> [antal] [type]");
      debug_println("        read coil <id> [antal]");
      debug_println("        read input <id> [antal]");
      return false;
    }

    const char* what = normalize_alias(argv[1]);

    if (!strcmp(what, "H-REG")) {
      cli_cmd_read_reg(argc - 2, argv + 2);
      return true;
    } else if (!strcmp(what, "COIL")) {
      cli_cmd_read_coil(argc - 2, argv + 2);
      return true;
    } else if (!strcmp(what, "INPUT")) {
      cli_cmd_read_input(argc - 2, argv + 2);
      return true;
    } else if (!strcmp(what, "I-REG")) {
      cli_cmd_read_input_reg(argc - 2, argv + 2);
      return true;
    } else {
      debug_println("READ: ukendt argument (brug: h-reg, coil, input, i-reg)");
      return false;
    }

  } else if (!strcmp(cmd, "WRITE")) {
    // write <what> <params...>
    if (argc < 2) {
      debug_println("WRITE: manglende argument");
      debug_println("  Brug: write h-reg <addr> value uint <v\u00e6rdi>");
      debug_println("        write h-reg <addr> value int <v\u00e6rdi>");
      debug_println("        write coil <id> value <on|off>");
      return false;
    }

    const char* what = normalize_alias(argv[1]);

    if (!strcmp(what, "H-REG")) {
      cli_cmd_write_reg(argc - 2, argv + 2);
      return true;
    } else if (!strcmp(what, "COIL")) {
      cli_cmd_write_coil(argc - 2, argv + 2);
      return true;
    } else {
      debug_println("WRITE: ukendt argument (brug: reg, coil)");
      return false;
    }

  } else {
    debug_println("Unknown command");
    return false;
  }
}

/* ============================================================================
 * ST LOGIC MULTI-LINE UPLOAD HANDLER
 * ============================================================================ */

void cli_parser_execute_st_upload(uint8_t program_id, const char* source_code) {
  /**
   * This function is called from cli_shell.cpp when the user finishes
   * a multi-line ST Logic upload (ends with END_UPLOAD).
   *
   * program_id: 0-3 (Logic1-Logic4)
   * source_code: Complete multi-line source code collected from CLI
   */

  if (program_id >= ST_LOGIC_MAX_PROGRAMS) {
    debug_printf("ERROR: Invalid program ID (0-%d)\n", ST_LOGIC_MAX_PROGRAMS - 1);
    return;
  }

  if (!source_code || strlen(source_code) == 0) {
    debug_println("ERROR: Source code is empty");
    return;
  }

  // Call the standard upload handler with the collected code
  cli_cmd_set_logic_upload(st_logic_get_state(), program_id, source_code);
}

/* ============================================================================
 * HELP
 * ============================================================================ */

void cli_parser_print_help(void) {
  debug_print("\n=== Modbus RTU Server v");
  debug_print(PROJECT_VERSION);
  debug_println(" (ESP32) ===\n");
  debug_println("Commands:");
  debug_println("  show config         - Display full configuration");
  debug_println("  show counters       - Display counter status");
  debug_println("  show timers         - Display timer status");
  debug_println("  show logic          - Display ST Logic programs status");
  debug_println("  show logic stats    - Display ST Logic detailed statistics");
  debug_println("  show registers [start] [count]");
  debug_println("  show coils          - Display coil states");
  debug_println("  show inputs         - Display discrete inputs");
  debug_println("  show st-stats       - Display ST Logic stats (Modbus IR 252-293)");
  debug_println("  show version        - Display firmware version");
  debug_println("  show gpio           - Display GPIO mappings");
  debug_println("  show echo           - Display remote echo status");
  debug_println("  show reg            - Display register mappings");
  debug_println("  show coil           - Display coil mappings");
  debug_println("");
  debug_println("Modbus Read/Write Commands:");
  debug_println("  === HOLDING REGISTERS (FC03 Read / FC06-FC10 Write) ===");
  debug_println("  read h-reg <id> [count] [type]             - Read holding registers (HR)");
  debug_println("  write h-reg <addr> value uint <0..65535>   - Write unsigned holding register");
  debug_println("  write h-reg <addr> value int <-32768..32767> - Write signed holding register (two's complement)");
  debug_println("");
  debug_println("  === INPUT REGISTERS (FC04 Read only) ===");
  debug_println("  read i-reg <id> [count] [type]      - Read input registers (IR 0-1023)");
  debug_println("    IR 200-203:   ST Logic Status (enabled, compiled, running, error)");
  debug_println("    IR 204-207:   Execution Count");
  debug_println("    IR 208-211:   Error Count");
  debug_println("    IR 216-219:   Variable Binding Count");
  debug_println("    IR 220-251:   Variable Values (8 vars × 4 programs)");
  debug_println("    IR 252-293:   Timing Stats (min/max/avg execution µs)");
  debug_println("");
  debug_println("  === COILS & DISCRETE INPUTS ===");
  debug_println("  read coil <id> <count>             - Read coils");
  debug_println("  write coil <id> value <on|off>     - Write coil");
  debug_println("  read input <id> <count>            - Read discrete inputs");
  debug_println("");
  debug_println("Configuration:");
  debug_println("  set holding-reg STATIC <address> Value [type] <value>");
  debug_println("  set holding-reg DYNAMIC <address> counter<id>:<func> or timer<id>:<func>");
  debug_println("    Counter functions: index, raw, freq, overflow, ctrl");
  debug_println("    Timer functions: output");
  debug_println("");
  debug_println("  set coil STATIC <address> Value <ON|OFF>");
  debug_println("  set coil DYNAMIC <address> counter<id>:<func> or timer<id>:<func>");
  debug_println("    Counter functions: overflow");
  debug_println("    Timer functions: output");
  debug_println("");
  debug_println("Counters & Timers:");
  debug_println("  set counter <id> mode 1 parameter ...");
  debug_println("");
  debug_println("    Common parameters:");
  debug_println("      hw-mode:<sw|sw-isr|hw>    - Hardware mode");
  debug_println("        sw     = Software polling (needs input-dis:<pin>)");
  debug_println("        sw-isr = Software interrupt (needs interrupt-pin:<gpio>)");
  debug_println("        hw     = Hardware PCNT (GPIO 19/25/27/33)");
  debug_println("      edge:<rising|falling|both> - Edge detection");
  debug_println("      prescaler:<1|4|8|16|64|256|1024> - Divide counter");
  debug_println("      start-value:<n>           - Initial counter value");
  debug_println("      scale:<float>             - Multiply output (e.g. 2.5)");
  debug_println("      bit-width:<8|16|32|64>    - Counter resolution");
  debug_println("      dir:<up|down>             - Count direction");
  debug_println("      debounce:<on|off>         - Enable/disable debounce (default: on)");
  debug_println("      debounce-ms:<ms>          - Debounce time in ms (default: 10)");
  debug_println("");
  debug_println("    Register mapping:");
  debug_println("      index-reg:<addr>    - Scaled value register");
  debug_println("      raw-reg:<addr>      - Prescaled value register");
  debug_println("      freq-reg:<addr>     - Frequency (Hz) register");
  debug_println("      ctrl-reg:<addr>     - Control register");
  debug_println("      overload-reg:<addr> - Overflow flag register");
  debug_println("");
  debug_println("    Mode-specific:");
  debug_println("      input-dis:<pin>       - For SW mode: discrete input pin");
  debug_println("      interrupt-pin:<gpio>  - For SW-ISR mode: GPIO interrupt pin");
  debug_println("      hw-gpio:<gpio>        - For HW mode: PCNT GPIO pin (BUG FIX 1.9)");
  debug_println("");
  debug_println("    Examples:");
  debug_println("      HW mode:  set counter 1 mode 1 hw-mode:hw edge:rising \\");
  debug_println("                hw-gpio:19 prescaler:16 index-reg:20 raw-reg:30 \\");
  debug_println("                freq-reg:35 ctrl-reg:40 debounce:on");
  debug_println("      ISR mode: set counter 2 mode 1 hw-mode:sw-isr edge:falling \\");
  debug_println("                interrupt-pin:13 index-reg:40 raw-reg:45 \\");
  debug_println("                debounce:on debounce-ms:50");
  debug_println("      SW mode:  set counter 3 mode 1 hw-mode:sw edge:rising \\");
  debug_println("                input-dis:50 index-reg:60 raw-reg:65 debounce:off");
  debug_println("");
  debug_println("  Counter control:");
  debug_println("    set counter <id> control counter-reg-reset-on-read:<on|off>");
  debug_println("    set counter <id> control compare-reg-reset-on-read:<on|off>");
  debug_println("    set counter <id> control auto-start:<on|off>");
  debug_println("    set counter <id> control running:<on|off>");
  debug_println("    Example: set counter 1 control auto-start:on running:on");
  debug_println("");
  debug_println("  Counter operations:");
  debug_println("    reset counter <id>  - Reset single counter to start-value");
  debug_println("    clear counters      - Reset all counters");
  debug_println("");
  debug_println("  Timers (4 max, Mode 1-4 with control via ctrl_reg):");
  debug_println("    set timer <id> mode 1 p1-duration:<ms> p1-output:<0|1> \\");
  debug_println("                      p2-duration:<ms> p2-output:<0|1> \\");
  debug_println("                      p3-duration:<ms> p3-output:<0|1> \\");
  debug_println("                      output-coil:<addr> [ctrl-reg:<reg>]");
  debug_println("      Mode 1: One-shot (3-phase sequence) - manual START via ctrl_reg");
  debug_println("");
  debug_println("    set timer <id> mode 2 pulse-ms:<ms> \\");
  debug_println("                      p1-output:<level> p2-output:<level> \\");
  debug_println("                      output-coil:<addr> [ctrl-reg:<reg>]");
  debug_println("      Mode 2: Monostable (retriggerable pulse)");
  debug_println("");
  debug_println("    set timer <id> mode 3 on-ms:<ms> off-ms:<ms> \\");
  debug_println("                      p1-output:<level> p2-output:<level> \\");
  debug_println("                      output-coil:<addr> [ctrl-reg:<reg>] enabled:<0|1>");
  debug_println("      Mode 3: Astable (oscillator/blink - auto-start when enabled)");
  debug_println("");
  debug_println("    set timer <id> mode 4 input-dis:<coil> trigger-edge:<0|1> \\");
  debug_println("                      delay-ms:<ms> output-coil:<addr> [ctrl-reg:<reg>]");
  debug_println("      Mode 4: Input-triggered (edge detection)");
  debug_println("        trigger-edge: 1=rising (0->1), 0=falling (1->0)");
  debug_println("        input-dis: COIL address to monitor (can be virtual GPIO 100-255)");
  debug_println("");
  debug_println("    Control Register (ctrl_reg) - Control timer via Modbus register:");
  debug_println("      write h-reg <ctrl-reg> value uint 1   - START timer (Bit 0)");
  debug_println("      write h-reg <ctrl-reg> value uint 2   - STOP timer (Bit 1)");
  debug_println("      write h-reg <ctrl-reg> value uint 4   - RESET timer (Bit 2)");
  debug_println("      Bits auto-clear after execution");
  debug_println("");
  debug_println("    Timer examples:");
  debug_println("      Mode 1 with START: set timer 1 mode 1 p1-dur:500 p1-out:1 ctrl-reg:100 \\");
  debug_println("                         output-coil:200");
  debug_println("                         write h-reg 100 value uint 1   ← START!");
  debug_println("      Mode 3: set timer 1 mode 3 on-ms:1000 off-ms:1000 \\");
  debug_println("              p1-output:1 p2-output:0 output-coil:200 enabled:1");
  debug_println("      Mode 4: set timer 2 mode 4 input-dis:30 trigger-edge:1 \\");
  debug_println("              delay-ms:0 output-coil:250");
  debug_println("");
  debug_println("GPIO Management:");
  debug_println("  Physical GPIO: 0-39 (direct ESP32 pins)");
  debug_println("  Virtual GPIO:  100-255 (reads/writes COIL directly - perfect for testing!)");
  debug_println("");
  debug_println("  set gpio <pin> input <idx>   - Map GPIO input to discrete input");
  debug_println("  set gpio <pin> coil <idx>    - Map GPIO output to coil");
  debug_println("  no set gpio <pin>            - Remove GPIO mapping");
  debug_println("");
  debug_println("  Virtual GPIO - Simulate GPIO without hardware!");
  debug_println("    FORMULA: Virtual GPIO N (N>=100) reads COIL (N-100) → Discrete Input");
  debug_println("");
  debug_println("    Example: set gpio 140 input 10");
  debug_println("      Virtual GPIO 140 → reads COIL 40 (140-100=40) → Discrete Input 10");
  debug_println("");
  debug_println("    HOW IT WORKS:");
  debug_println("      write coil 40 value 1");
  debug_println("        ↓ COIL 40 = 1");
  debug_println("        ↓ Virtual GPIO 140 detects level");
  debug_println("        ↓ Discrete Input 10 = 1");
  debug_println("      read input 10 1  →  Result: 1 ✅");
  debug_println("");
  debug_println("    USE CASE - Test Timer Mode 4 without GPIO hardware:");
  debug_println("      set gpio 140 input 10");
  debug_println("      set timer 1 mode 4 input-dis:10 trigger-edge:1 output-coil:250");
  debug_println("      write coil 40 value 0");
  debug_println("      write coil 40 value 1     ← Rising edge (0→1) triggers Timer!");
  debug_println("      read coil 250 1           ← Output = 1 ✅");
  debug_println("");
  debug_println("  GPIO2 special (heartbeat LED):");
  debug_println("    set gpio 2 enable   - Release GPIO2 for user code (disable LED)");
  debug_println("    set gpio 2 disable  - Reserve GPIO2 for heartbeat (enable LED, default)");
  debug_println("");
  debug_println("System:");
  debug_println("  set hostname <name>      - Set system name");
  debug_println("  set baud <rate>          - Set Modbus baudrate (300-115200, requires reboot)");
  debug_println("  set id <slave_id>        - Set Modbus slave ID (0-247)");
  debug_println("  set echo <on|off>        - Enable/disable remote echo");
  debug_println("");
  debug_println("ST Logic - Structured Text Programs (4 independent programs):");
  debug_println("  upload logic <id> <source>         - Upload ST source code");
  debug_println("    Example: upload logic 1 \"VAR x: INT; END_VAR x := x + 1;\"");
  debug_println("  set logic <id> compile             - Compile program to bytecode");
  debug_println("  set logic <id> enable              - Enable/disable program");
  debug_println("  set logic <id> interval:<ms>       - Set execution interval (10,20,25,50,75,100)");
  debug_println("  set logic <id> debug:<true|false>  - Enable timing debug output");
  debug_println("");
  debug_println("  Variable Bindings (CLI method - permanent):");
  debug_println("    set logic <id> bind <var> reg:<addr>       - Bind ST var → HR (output)");
  debug_println("    set logic <id> bind <var> input:<addr>     - Bind HR → ST var (input)");
  debug_println("    set logic <id> bind <var> coil:<addr>      - Bind ST var → Coil");
  debug_println("");
  debug_println("  Modbus Direct Write (NEW v4.2.0 - temporary, no setup needed):");
  debug_println("    write h-reg <addr> value uint <value> - Write unsigned to ST Logic variables");
  debug_println("    write h-reg <addr> value int <value>  - Write signed to ST Logic variables");
  debug_println("      HR 204-211: Logic1 var[0-7]");
  debug_println("      HR 212-219: Logic2 var[0-7]");
  debug_println("      HR 220-227: Logic3 var[0-7]");
  debug_println("      HR 228-235: Logic4 var[0-7]");
  debug_println("      Type-aware: BOOL/INT/REAL conversion automatic");
  debug_println("");
  debug_println("  Read ST Logic Status (via Modbus FC04 - INPUT REGISTERS):");
  debug_println("    read i-reg 200 10     - Status, counts, binding count");
  debug_println("    read i-reg 220 32     - Variable values (all programs)");
  debug_println("    read i-reg 252 42     - Timing stats (min/max/avg µs)");
  debug_println("");
  debug_println("  Control via Modbus (HOLDING REGISTERS):");
  debug_println("    write h-reg 200 <bits>      - Logic1 control (enable, reset error)");
  debug_println("    write h-reg 236 <interval>  - Execution interval (ms)");
  debug_println("");
  debug_println("Persistence (NVS - Non-Volatile Storage):");
  debug_println("  save                     - Save all configs to NVS (persistent across reboot)");
  debug_println("  load                     - Load configs from NVS");
  debug_println("  defaults                 - Reset to factory defaults");
  debug_println("  reboot                   - Restart ESP32");
  debug_println("");
  debug_println("Persistent Registers (v4.0+):");
  debug_println("  set persist group <name> add <reg1> [reg2] ...  - Create/extend group");
  debug_println("  save registers all                              - Save all groups to NVS");
  debug_println("  save registers group <name>                     - Save specific group");
  debug_println("  load registers all                              - Load all groups from NVS");
  debug_println("  load registers group <name>                     - Load specific group");
  debug_println("  show persist                                    - Show all groups");
  debug_println("  set persist ?                                   - Show detailed help");
  debug_println("");
  debug_println("Persistence features:");
  debug_println("  - All timers, counters, GPIO mappings saved");
  debug_println("  - Persistent register groups (8 groups × 16 registers)");
  debug_println("  - ST Logic SAVE()/LOAD() built-in functions");
  debug_println("  - Schema versioning (v8 with v7 migration)");
  debug_println("  - CRC16 validation for data integrity");
  debug_println("");
  debug_println("Quick examples:");
  debug_println("  show config              - View all settings");
  debug_println("  help  or  ?              - This help message");
  debug_println("");
}
