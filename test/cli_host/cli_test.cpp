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
#include "counter_sw.h"

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

  // --- FEAT-438: hurtig SW-tælling via scan-tasken (DI8 = virtuel GPIO 108 → DI 7) ---
  {
    memset(&g_persist_config.var_maps[0], 0, sizeof(VariableMapping));
    g_persist_config.var_maps[0].source_type = MAPPING_SOURCE_GPIO;
    g_persist_config.var_maps[0].gpio_pin = 108;
    g_persist_config.var_maps[0].is_input = 1;
    g_persist_config.var_maps[0].input_reg = 7;
    g_persist_config.var_maps[0].associated_counter = 0xff;
    g_persist_config.var_maps[0].associated_timer = 0xff;
    g_persist_config.var_map_count = 1;
    // 300 Hz firkantbølge i 1 s, samplet hvert 1 ms af scan-tasken (bit 7 = DI8)
    auto run_wave = [](uint8_t id, int hz, int ms) {
      counter_sw_init(id); counter_sw_start(id);
      counter_sw_loop(id);  // spejler "kører" til scan-tasken
      uint64_t start = counter_sw_get_value(id);
      for (int t = 0; t < ms; t++) {
        uint32_t us = (uint32_t)t * 1000 + 1;
        uint8_t level = ((us * (uint64_t)hz * 2 / 1000000) % 2) ? 0 : 1;  // starter højt (hvile)
        counter_sw_fast_scan((uint8_t)(level << 7), us);
        if (t % 10 == 0) counter_sw_loop(id);  // hovedløkken hvert 10 ms
      }
      counter_sw_loop(id);
      return (long)(counter_sw_get_value(id) - start);
    };
    run("set counter 3 mode 1 hw-mode:sw input-dis:7 edge:falling bit-width:32 debounce:off");
    check(counter_sw_fast_active(3), "FEAT-438: DI fra skifteregister bruger scan-tasken");
    long n = run_wave(3, 300, 1000);
    check(n >= 299 && n <= 301, ("FEAT-438: 300 Hz uden debounce → " + std::to_string(n) + " flanker (forventet 300)").c_str());
    n = run_wave(3, 450, 1000);
    check(n >= 449 && n <= 451, ("FEAT-438: 450 Hz uden debounce → " + std::to_string(n) + " (grænse for 1 ms-sampling ≈ 500 Hz)").c_str());
    run("set counter 3 mode 1 hw-mode:sw input-dis:7 edge:falling bit-width:32 debounce:on debounce-ms:10");
    n = run_wave(3, 300, 1000);
    check(n >= 70 && n <= 80, ("FEAT-438: 300 Hz med debounce 10 ms → " + std::to_string(n) + " flanker (spærretid 10 ms + næste flanke ≈ 13,3 ms → ≈ 75)").c_str());
    run("set counter 3 mode 1 hw-mode:sw input-dis:7 edge:both bit-width:32 debounce:off");
    n = run_wave(3, 100, 1000);
    check(n >= 199 && n <= 201, ("FEAT-438: edge:both 100 Hz → " + std::to_string(n) + " (forventet 200)").c_str());
    counter_sw_stop(3); counter_sw_loop(3);
    uint64_t before = counter_sw_get_value(3);
    for (int t = 0; t < 100; t++) counter_sw_fast_scan((uint8_t)((t & 1) << 7), (uint32_t)t * 1000 + 1);
    counter_sw_loop(3);
    check(counter_sw_get_value(3) == before, "FEAT-438: stoppet tæller tæller ikke");
    run("set counter 3 mode 1 hw-mode:sw input-dis:5 edge:falling bit-width:32");
    check(!counter_sw_fast_active(3), "FEAT-438: DI uden skifteregister-mapping → pollet sti");
    g_persist_config.var_map_count = 0;
  }

  // --- FEAT-448: reset watchdog stats ---
  run("reset watchdog stats");  check(called("watchdog_reset_stats") && out_has("nulstillet"), "reset watchdog stats");
  run("reset watchdog");        check(!called("watchdog_reset_stats") && out_has("Brug: reset watchdog stats"), "reset watchdog uden 'stats' → hjælp");

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
