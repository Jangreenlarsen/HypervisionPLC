/**
 * @file cli_commands_modbus_expansion.cpp
 * @brief FEAT-409: CLI commands for Modbus Expansion Board management.
 *
 * Alle netvaerkskald (status/kanal-liste/config-push/diagnostisk read-write)
 * genbruger expansion_api_client.cpp's async engine — CLI'en starter kaldet
 * og blokerer derefter SYNKRONT paa expansion_api_wait_result() (sikkert her,
 * i modsaetning til i en httpd-handler, fordi CLI-tasken har sin egen,
 * rummelige stack — se expansion_api_client.h's designnote).
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include "cli_commands_modbus_expansion.h"
#include "expansion_api_client.h"
#include "modbus_expansion_async.h"  // FEAT-410
#include "config_struct.h"
#include "debug.h"

#define MBX_WAIT_TIMEOUT_MS 6000

/* ============================================================================
 * HJAELPEFUNKTIONER
 * ============================================================================ */

// Loeser et board-argument — enten et board-NUMMER (1-8, samme nummer man
// valgte ved 'add', vist som "nr" i "show modbus-expansion") eller boardets
// navn (case-insensitive). Returnerer det interne 0-based array-index
// (number-1) som resten af funktionerne i denne fil bruger.
static int mbx_resolve_board(const char *arg) {
  if (!arg || !*arg) return -1;

  bool all_digits = true;
  for (const char *p = arg; *p; p++) {
    if (*p < '0' || *p > '9') { all_digits = false; break; }
  }
  if (all_digits) {
    int number = atoi(arg);
    int idx = number - 1;
    if (idx >= 0 && idx < EXPANSION_BOARD_MAX && g_persist_config.expansion_boards[idx].configured) {
      return idx;
    }
    return -1;
  }
  return expansion_board_find_by_name(arg);
}

// Kanal A/B (eller fremtidige 1-8) — accepterer bogstav- eller talnotation.
static int mbx_resolve_channel(const char *arg) {
  if (!arg || !*arg || arg[1] != '\0') {
    // Ikke-enkeltkarakter — proev alligevel som rent tal (fremtidssikring
    // hvis et board nogensinde faar >9 kanaler).
    int v = atoi(arg);
    return (v >= 1 && v <= 8) ? v : -1;
  }
  char c = toupper((unsigned char)arg[0]);
  if (c == 'A') return 1;
  if (c == 'B') return 2;
  if (c >= '1' && c <= '8') return c - '0';
  return -1;
}

static void mbx_print_unknown_board(const char *arg) {
  debug_printf("FEJL: Ukendt expansion board '%s' (brug index eller navn — se 'show modbus-expansion')\n", arg);
}

// Faelles "start kald, vent, print raa/pae\u0301nt resultat"-flow. `kind` bruges
// kun til en menneskelig label i output, ikke til logik.
static void mbx_print_result(const char *label) {
  ExpansionApiResult res;
  bool finished = expansion_api_wait_result(MBX_WAIT_TIMEOUT_MS, &res);
  if (!finished) {
    debug_printf("FEJL: %s - intet svar fra boardet indenfor %d ms (tjek IP/netvaerk/at boardet er taendt)\n",
                 label, MBX_WAIT_TIMEOUT_MS);
    return;
  }
  if (!res.transport_ok) {
    debug_printf("FEJL: %s - kunne ikke naa boardet (http=%d)\n", label, res.http_status);
    debug_printf("  %s\n", res.response_json);
    return;
  }
  debug_printf("[%s] HTTP %d:\n", label, res.http_status);
  debug_printf("  %s\n", res.response_json);
}

/* ============================================================================
 * SET MODBUS-EXPANSION ...
 * ============================================================================ */

void cli_cmd_set_modbus_expansion_add(uint8_t argc, char **argv) {
  // set modbus-expansion add <nr> <type> <name> <ip> <token>
  if (argc < 5) {
    debug_println("Brug: set modbus-expansion add <nr> <type> <navn> <ip> <token>");
    debug_println("  <nr>: 1-8, boardets faste nummer (og id i REST/CLI resten af vejen)");
    debug_println("  <type>: modbus_2ch (eneste understoettede type i dag)");
    debug_println("  <token> er Bearer-tokenet vist paa boardets serielle CLI ved provisionering ('status')");
    return;
  }
  uint8_t number = atoi(argv[0]);
  if (number < 1 || number > EXPANSION_BOARD_MAX) {
    debug_printf("FEJL: <nr> skal vaere 1-%d\n", EXPANSION_BOARD_MAX);
    return;
  }
  if (!expansion_board_type_valid(argv[1])) {
    debug_printf("FEJL: Ukendt board-type '%s' (kun understoettet: %s)\n", argv[1], EXPANSION_BOARD_TYPE_MODBUS_2CH);
    return;
  }
  int idx = expansion_board_add(number, argv[1], argv[2], argv[3], argv[4]);
  if (idx < 0) {
    debug_printf("FEJL: Board nr %u er allerede i brug, eller ugyldigt navn/IP/token\n", number);
  }
}

void cli_cmd_set_modbus_expansion_edit(uint8_t argc, char **argv) {
  // set modbus-expansion edit <board> <name> <ip> [token] [type]
  if (argc < 3) {
    debug_println("Brug: set modbus-expansion edit <board> <navn> <ip> [token] [type]");
    debug_println("  <board>: nr eller navn. [token]/[type] udeladt = bevar eksisterende værdi.");
    return;
  }
  int idx = mbx_resolve_board(argv[0]);
  if (idx < 0) { mbx_print_unknown_board(argv[0]); return; }

  const char *token = (argc >= 4) ? argv[3] : NULL;
  const char *type = (argc >= 5) ? argv[4] : NULL;
  if (type && *type && !expansion_board_type_valid(type)) {
    debug_printf("FEJL: Ukendt board-type '%s' (kun understoettet: %s)\n", type, EXPANSION_BOARD_TYPE_MODBUS_2CH);
    return;
  }
  if (!expansion_board_edit((uint8_t)idx, argv[1], argv[2], token, type)) {
    debug_println("FEJL: Kunne ikke opdatere board (ugyldigt navn/IP/token)");
  }
}

void cli_cmd_set_modbus_expansion_remove(uint8_t argc, char **argv) {
  // set modbus-expansion remove <board>
  if (argc < 1) {
    debug_println("Brug: set modbus-expansion remove <board>");
    return;
  }
  int idx = mbx_resolve_board(argv[0]);
  if (idx < 0) { mbx_print_unknown_board(argv[0]); return; }
  expansion_board_remove((uint8_t)idx);
}

void cli_cmd_set_modbus_expansion_channel(uint8_t argc, char **argv) {
  // set modbus-expansion channel <board> <kanal> <mode> <baud> <parity> <stopbits> <timeout_ms> <inter_frame_ms> <on|off>
  if (argc < 9) {
    debug_println("Brug: set modbus-expansion channel <board> <kanal> <mode> <baud> <parity> <stopbits> <timeout_ms> <inter_frame_ms> <on|off>");
    debug_println("  <board>: index eller navn. <kanal>: A/B (eller 1-8)");
    debug_println("  <mode>: rs485|rs232   <parity>: none|even|odd");
    debug_println("  Konfigurationen sendes ATOMISK til boardet (alle felter i ét kald) — samme princip som boardets egen API.");
    debug_println("Eksempel: set modbus-expansion channel skab3 A rs485 9600 none 1 500 0 on");
    return;
  }
  int idx = mbx_resolve_board(argv[0]);
  if (idx < 0) { mbx_print_unknown_board(argv[0]); return; }
  int ch = mbx_resolve_channel(argv[1]);
  if (ch < 0) { debug_printf("FEJL: Ugyldig kanal '%s' (brug A/B eller 1-8)\n", argv[1]); return; }

  const char *mode = argv[2];
  if (strcasecmp(mode, "rs485") != 0 && strcasecmp(mode, "rs232") != 0) {
    debug_println("FEJL: mode skal vaere rs485 eller rs232");
    return;
  }
  uint32_t baud = atol(argv[3]);
  const char *parity = argv[4];
  if (strcasecmp(parity, "none") != 0 && strcasecmp(parity, "even") != 0 && strcasecmp(parity, "odd") != 0) {
    debug_println("FEJL: parity skal vaere none, even eller odd");
    return;
  }
  uint8_t stop_bits = atoi(argv[5]);
  uint16_t timeout_ms = atoi(argv[6]);
  uint16_t inter_frame = atoi(argv[7]);
  bool enabled = (strcasecmp(argv[8], "on") == 0 || strcasecmp(argv[8], "1") == 0 || strcasecmp(argv[8], "true") == 0);

  if (expansion_api_is_busy()) {
    debug_println("FEJL: Et andet expansion-board-kald er allerede i gang — vent til det er faerdigt");
    return;
  }
  if (!expansion_api_start_config_push((uint8_t)idx, (uint8_t)ch, enabled, mode, baud, parity, stop_bits, timeout_ms, inter_frame)) {
    debug_println("FEJL: Kunne ikke starte kald mod boardet");
    return;
  }
  debug_printf("[MBX] Sender kanal-config til board %d, kanal %d ...\n", idx + 1, ch);
  mbx_print_result("kanal-config");
}

/* ============================================================================
 * SHOW MODBUS-EXPANSION [board]
 * ============================================================================ */

void cli_cmd_show_modbus_expansion(uint8_t argc, char **argv) {
  if (argc < 1) {
    // Liste over alle konfigurerede boards (lokal data, intet netvaerkskald).
    debug_println("");
    debug_println("=== MODBUS EXPANSION BOARDS ===");
    bool any = false;
    for (uint8_t i = 0; i < EXPANSION_BOARD_MAX; i++) {
      if (!g_persist_config.expansion_boards[i].configured) continue;
      any = true;
      char ip_str[16];
      expansion_board_ip_str(i, ip_str, sizeof(ip_str));
      debug_printf("  nr %u  %-20s %-16s type=%-12s token=%s\n", i + 1,
                   g_persist_config.expansion_boards[i].name, ip_str,
                   g_persist_config.expansion_boards[i].board_type,
                   g_persist_config.expansion_boards[i].token[0] ? "sat" : "IKKE SAT");
    }
    if (!any) {
      debug_println("  (ingen boards konfigureret — se 'set modbus-expansion add')");
    }
    debug_println("");
    debug_println("Brug 'show modbus-expansion <board>' for live status+kanaler fra boardet selv.");
    debug_println("Brug 'show modbus-expansion queue' for data-plan kø/cache-diagnostik (MBX_*-builtins).");
    return;
  }

  if (!strcasecmp(argv[0], "queue")) {
    const mbx_async_state_t *st = modbus_expansion_async_get_state();
    debug_println("");
    debug_println("=== MODBUS EXPANSION DATA-PLAN (MBX_*) ===");
    debug_printf("  Queue depth: %u / %u (high watermark: %u)\n", st->pq_count, MBX_ASYNC_QUEUE_SIZE, st->queue_high_watermark);
    debug_printf("  Cache entries: %u / %u\n", st->entry_count, MBX_ASYNC_CACHE_MAX_ENTRIES);
    debug_printf("  Cache hits/misses: %lu / %lu\n", (unsigned long)st->cache_hits, (unsigned long)st->cache_misses);
    debug_printf("  Requests total: %lu (fejl: %lu, timeout: %lu)\n",
                 (unsigned long)st->total_requests, (unsigned long)st->total_errors, (unsigned long)st->total_timeouts);
    debug_printf("  Queue full drops: %lu, priority drops: %lu\n", (unsigned long)st->queue_full_count, (unsigned long)st->priority_drops);
    debug_printf("  Stale PENDING recovered (BUG-333-lektionen): %lu\n", (unsigned long)st->stale_pending_recovered);
    debug_println("  Backoff (board/kanal/slave med aktiv cooldown):");
    bool any_backoff = false;
    for (uint8_t i = 0; i < MBX_SLAVE_BACKOFF_MAX; i++) {
      if (st->slave_backoff[i].board == 0 || st->slave_backoff[i].backoff_ms == 0) continue;
      any_backoff = true;
      debug_printf("    board=%u kanal=%u slave=%u backoff=%ums (timeouts=%u, successes=%u)\n",
                   st->slave_backoff[i].board, st->slave_backoff[i].channel, st->slave_backoff[i].slave_id,
                   st->slave_backoff[i].backoff_ms, st->slave_backoff[i].timeout_count, st->slave_backoff[i].success_count);
    }
    if (!any_backoff) debug_println("    (ingen)");
    debug_println("");
    return;
  }

  int idx = mbx_resolve_board(argv[0]);
  if (idx < 0) { mbx_print_unknown_board(argv[0]); return; }

  if (expansion_api_is_busy()) {
    debug_println("FEJL: Et andet expansion-board-kald er allerede i gang — vent til det er faerdigt");
    return;
  }
  debug_printf("[MBX] Henter status fra board %d (%s) ...\n", idx + 1, g_persist_config.expansion_boards[idx].name);
  if (!expansion_api_start_status((uint8_t)idx)) {
    debug_println("FEJL: Kunne ikke starte kald mod boardet");
    return;
  }
  mbx_print_result("status");

  if (expansion_api_is_busy()) return;  // usandsynligt, men vaer defensiv
  if (!expansion_api_start_channels((uint8_t)idx)) return;
  mbx_print_result("kanaler");
}

/* ============================================================================
 * MBX <board> status | <board> <kanal> read/write ...
 * ============================================================================ */

void cli_cmd_mbx_status(uint8_t argc, char **argv) {
  cli_cmd_show_modbus_expansion(argc, argv);
}

void cli_cmd_mbx_read(uint8_t argc, char **argv) {
  // mbx <board> <kanal> read <fc> <slave_id> <address> [quantity]
  if (argc < 5) {
    debug_println("Brug: mbx <board> <kanal> read <fc> <slave_id> <address> [quantity]");
    debug_println("  fc: 1=coils 2=discrete-inputs 3=holding 4=input-registers");
    debug_println("Eksempel: mbx skab3 A read 3 9 0 2");
    return;
  }
  int idx = mbx_resolve_board(argv[0]);
  if (idx < 0) { mbx_print_unknown_board(argv[0]); return; }
  int ch = mbx_resolve_channel(argv[1]);
  if (ch < 0) { debug_printf("FEJL: Ugyldig kanal '%s' (brug A/B eller 1-8)\n", argv[1]); return; }
  // argv[2] er selve "read"-ordet (allerede matchet af kalderen) — parametrene starter ved argv[3]

  uint8_t fc = atoi(argv[3]);
  uint8_t slave_id = atoi(argv[4]);
  if (slave_id < 1 || slave_id > 247) { debug_println("FEJL: slave_id skal vaere 1-247"); return; }
  uint16_t address = (argc >= 6) ? atoi(argv[5]) : 0;
  uint16_t quantity = (argc >= 7) ? atoi(argv[6]) : 1;

  if (expansion_api_is_busy()) {
    debug_println("FEJL: Et andet expansion-board-kald er allerede i gang — vent til det er faerdigt");
    return;
  }
  if (!expansion_api_start_diag_read((uint8_t)idx, (uint8_t)ch, fc, slave_id, address, quantity)) {
    debug_println("FEJL: Kunne ikke starte kald mod boardet");
    return;
  }
  debug_printf("[MBX READ] board=%d kanal=%d fc=%d slave=%d addr=%d qty=%d ...\n", idx + 1, ch, fc, slave_id, address, quantity);
  mbx_print_result("read");
}

void cli_cmd_mbx_write(uint8_t argc, char **argv) {
  // mbx <board> <kanal> write <fc> <slave_id> <address> <value> [value2 value3 ...]
  if (argc < 6) {
    debug_println("Brug: mbx <board> <kanal> write <fc> <slave_id> <address> <value...>");
    debug_println("  fc: 5=coil(0/1) 6=holding(ét register) 16=holding (flere registre, maks 32)");
    debug_println("Eksempel: mbx skab3 A write 6 9 10 1234");
    debug_println("          mbx skab3 A write 16 9 0 1 2 3");
    return;
  }
  int idx = mbx_resolve_board(argv[0]);
  if (idx < 0) { mbx_print_unknown_board(argv[0]); return; }
  int ch = mbx_resolve_channel(argv[1]);
  if (ch < 0) { debug_printf("FEJL: Ugyldig kanal '%s' (brug A/B eller 1-8)\n", argv[1]); return; }

  uint8_t fc = atoi(argv[3]);
  uint8_t slave_id = atoi(argv[4]);
  if (slave_id < 1 || slave_id > 247) { debug_println("FEJL: slave_id skal vaere 1-247"); return; }
  uint16_t address = atoi(argv[5]);

  if (expansion_api_is_busy()) {
    debug_println("FEJL: Et andet expansion-board-kald er allerede i gang — vent til det er faerdigt");
    return;
  }

  bool started;
  if (fc == 16) {
    // argv[6..] = vaerdier
    uint8_t count = (argc >= 7) ? (argc - 6) : 0;
    if (count == 0 || count > 32) { debug_println("FEJL: fc=16 kraever 1-32 vaerdier"); return; }
    uint16_t vals[32];
    for (uint8_t i = 0; i < count; i++) vals[i] = (uint16_t)atol(argv[6 + i]);
    started = expansion_api_start_diag_write_multi((uint8_t)idx, (uint8_t)ch, slave_id, address, vals, count);
    debug_printf("[MBX WRITE] board=%d kanal=%d fc=16 slave=%d addr=%d count=%d ...\n", idx + 1, ch, slave_id, address, count);
  } else {
    if (argc < 7) { debug_println("FEJL: mangler <value>"); return; }
    uint32_t value;
    if (fc == 5) {
      value = (strcasecmp(argv[6], "on") == 0 || strcasecmp(argv[6], "1") == 0 || strcasecmp(argv[6], "true") == 0) ? 1 : 0;
    } else {
      value = (uint32_t)atol(argv[6]);
    }
    started = expansion_api_start_diag_write_single((uint8_t)idx, (uint8_t)ch, fc, slave_id, address, value);
    debug_printf("[MBX WRITE] board=%d kanal=%d fc=%d slave=%d addr=%d value=%u ...\n", idx + 1, ch, fc, slave_id, address, value);
  }

  if (!started) {
    debug_println("FEJL: Kunne ikke starte kald mod boardet");
    return;
  }
  mbx_print_result("write");
}
