// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
// PC-test af CLI-parseren (FEAT-436): koerer rigtige kommandolinjer gennem
// cli_parser_execute() og tjekker hvilken funktion der blev kaldt, outputtet
// og de konfigurationer der blev sendt videre.
#include <stdio.h>
#include <string.h>
#include <string>
#include "cli_parser.h"
#include "cli_host.h"
#include "config_struct.h"
#include "counter_config.h"
#include "modbus_fc_read.h"

extern PersistConfig g_persist_config;
extern const char* const CLI_WORDS_TOP[];
extern const char* const CLI_WORDS_SHOW[];
extern const char* const CLI_WORDS_SET[];
extern const char* const CLI_WORDS_RESET[];
static int fails = 0, passes = 0;

static void run(const char *line) {
  g_out.clear(); g_calls.clear();
  char buf[512]; strncpy(buf, line, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
  cli_parser_execute(buf);
}
static bool called(const char *fn) {
  for (auto &c : g_calls) if (c.find(fn) != std::string::npos) return true;
  return false;
}
static bool out_has(const char *s) { return g_out.find(s) != std::string::npos; }
static void check(bool ok, const char *name) {
  printf("%s %s\n", ok ? "PASS" : "FAIL", name);
  if (ok) passes++; else { fails++; printf("     output: %s\n", g_out.substr(0, 300).c_str()); }
}

int main() {
  memset(&g_persist_config, 0, sizeof(g_persist_config));

  // --- show-kommandoer og aliaser ---
  run("show version");   check(called("cli_cmd_show_version"), "show version");
  run("sh ver");         check(called("cli_cmd_show_version"), "alias: sh ver");
  run("SHOW VERSION");   check(called("cli_cmd_show_version"), "store bogstaver: SHOW VERSION");
  run("show ota");       check(called("cli_cmd_show_ota"), "show ota (BUG-448)");
  run("show OTA");       check(called("cli_cmd_show_ota"), "show OTA");

  // --- tællerkonfiguration (BUG-446: show config-formatet skal kunne koeres igen) ---
  run("set counter 1 mode 1 parameter hw-mode:sw edge:falling prescaler:100 resolution:32 direction:down scale:1.00 start:50 debounce:off input-dis:7");
  for (auto &c : g_calls) printf("   kald: %s\n", c.c_str());
  check(g_last_counter_id == 1, "set counter: tæller 1 konfigureret");
  check(g_last_counter_cfg.enabled == 1, "set counter: aktiv uden enable:");
  check(g_last_counter_cfg.bit_width == 32, "set counter: resolution:32 → bit_width 32");
  check(g_last_counter_cfg.start_value == 50, "set counter: start:50");
  check(g_last_counter_cfg.prescaler == 100, "set counter: prescaler:100");
  check(g_last_counter_cfg.direction == COUNTER_DIR_DOWN, "set counter: direction:down");
  check(g_last_counter_cfg.debounce_enabled == 0, "set counter: debounce:off");
  check(g_last_counter_cfg.input_dis == 7, "set counter: input-dis:7");

  run("set counter 2 mode 1 hw-mode:sw input-dis:6 edge:rising bit-width:16 start-value:5 debounce:on debounce-ms:20 compare-enabled:on compare-value:1000 compare-mode:1 compare-source:2 reset-on-read:on");
  check(g_last_counter_id == 2 && g_last_counter_cfg.bit_width == 16 && g_last_counter_cfg.start_value == 5, "set counter: nye nøglenavne");
  check(g_last_counter_cfg.debounce_enabled == 1 && g_last_counter_cfg.debounce_ms == 20, "set counter: debounce-ms:20");
  check(g_last_counter_cfg.compare_enabled == 1 && g_last_counter_cfg.compare_value == 1000, "set counter: compare");
  check(g_last_counter_cfg.compare_mode == 1 && g_last_counter_cfg.compare_source == 2 && g_last_counter_cfg.reset_on_read == 1, "set counter: compare-mode/-source/reset-on-read");

  run("set counter 1 mode 1 hw-mode:sw input-dis:7 index-reg:10");
  check(out_has("disabled"), "set counter: index-reg afvises med besked");

  // --- BUG-447: "nulstil tælleren ved læsning" er et gemt flag, ikke ctrl-bit 0 ---
  run("set counter 1 mode 1 hw-mode:sw input-dis:7 bit-width:32 reset-on-read:off");
  run("set counter 1 control counter-reg-reset-on-read:on");
  {
    CounterConfig c; counter_config_get(1, &c);
    check((c.reset_on_read & COUNTER_ROR_VALUE) && !(c.reset_on_read & COUNTER_ROR_COMPARE), "BUG-447: control-linjen sætter kun værdi-flaget");
    check(g_persist_config.counters[0].reset_on_read & COUNTER_ROR_VALUE, "BUG-447: flaget gemmes i persist-config");
    check(out_has("counter-reg-reset-on-read: ENABLED"), "BUG-447: status viser ENABLED");
  }
  run("set counter 1 mode 1 hw-mode:sw input-dis:7 bit-width:32 reset-on-read:on");
  check((g_last_counter_cfg.reset_on_read & COUNTER_ROR_VALUE) && (g_last_counter_cfg.reset_on_read & COUNTER_ROR_COMPARE), "BUG-447: ny mode-linje bevarer værdi-flaget");
  run("read h-reg 100 2");  // CLI-læsning går gennem samme hook som FC03
  g_calls.clear(); modbus_handle_reset_on_read(100, 2);
  check(called("counter_engine_reset"), "BUG-447: læsning af HR100-101 nulstiller tælleren");
  g_calls.clear(); modbus_handle_reset_on_read(110, 1);
  check(!called("counter_engine_reset"), "BUG-447: læsning af kun ctrl-reg nulstiller ikke");
  run("set counter 1 mode 1 hw-mode:sw input-dis:7 bit-width:32 reset-on-read:off");
  g_calls.clear(); modbus_handle_reset_on_read(100, 2);
  check(called("counter_engine_reset"), "BUG-447: virker uden compare-reset-on-read");
  run("set counter 1 control counter-reg-reset-on-read:off");
  g_calls.clear(); modbus_handle_reset_on_read(100, 2);
  check(!called("counter_engine_reset"), "BUG-447: slået fra → ingen nulstilling");

  // --- timer (BUG-446) ---
  run("set timer 1 mode 3 on-ms:1000 off-ms:500 p1-output:1 p2-output:0 output-coil:150");
  check(g_last_timer_id == 1 && g_last_timer_cfg.mode == 3, "set timer: mode 3");
  check(g_last_timer_cfg.output_coil == 150, "set timer: output-coil:150");

  // --- ukendte kommandoer: "mente du …?" (FEAT-437) ---
  run("show otaa");      check(out_has("ukendt argument 'otaa'") && out_has("Mente du: ota"), "show otaa → mente du: ota");
  run("sh vers");        check(out_has("version"), "sh vers → version (præfiks)");
  run("show countr");    check(out_has("counter"), "show countr → counter");
  run("set countr 1");   check(out_has("SET: ukendt argument") && out_has("counter"), "set countr → counter");
  run("shwo version");   check(out_has("ukendt kommando 'shwo'") && out_has("show"), "shwo → show");
  run("blabla");         check(out_has("ukendt kommando 'blabla'") && !out_has("Mente du"), "blabla → ingen forslag");
  run("reset countr 1"); check(out_has("RESET: ukendt argument") && out_has("counter"), "reset countr → counter");
  run("show xyzzy");     check(out_has("show ?") && !out_has("Mente du"), "show xyzzy → kun hjælpehenvisning");

  // Hvert ord i forslagslisterne skal accepteres af parseren (ellers foreslås noget ugyldigt)
  int bad = 0;
  struct { const char *verb; const char *const *words; const char *reject; } lists[] = {
    {"show ", CLI_WORDS_SHOW, "ukendt"}, {"set ", CLI_WORDS_SET, "ukendt argument"},
    {"reset ", CLI_WORDS_RESET, "ukendt argument"}, {"", CLI_WORDS_TOP, "ukendt kommando"}};
  for (auto &L : lists) {
    for (int i = 0; L.words[i]; i++) {
      std::string l = std::string(L.verb) + L.words[i];
      run(l.c_str());
      if (out_has(L.reject)) { printf("     afvist: %s\n", l.c_str()); bad++; }
    }
  }
  check(bad == 0, "alle foreslåede ord accepteres af parseren");

  printf("\n%d bestået, %d fejlet\n", passes, fails);
  printf(fails ? "%d FEJL\n" : "ALLE TESTS OK\n", fails);
  return fails ? 1 : 0;
}
