// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
// Haandskrevne stubs til PC-testen af CLI-parseren: fanger CLI-output og de
// konfigurationer parseren sender videre, saa testen kan tjekke dem. Alle
// oevrige eksterne funktioner stubbes automatisk (gen_stubs.py).
#include <string>
#include <vector>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "config_struct.h"
#include "counter_config.h"
#include "timer_config.h"
#include "cli_host.h"
#include "debug.h"

PersistConfig g_persist_config;
void *g_serial_console = nullptr;

std::string g_out;
std::vector<std::string> g_calls;
CounterConfig g_last_counter_cfg; int g_last_counter_id = -1;
TimerConfig g_last_timer_cfg; int g_last_timer_id = -1;

extern "C" void cli_host_stub_hit(const char *name) { g_calls.push_back(name); }

void debug_println(const char *s) { g_out += s ? s : ""; g_out += "\n"; }
void debug_print(const char *s) { g_out += s ? s : ""; }
void debug_print_uint(uint32_t v) { g_out += std::to_string(v); }
void debug_print_ulong(uint64_t v) { g_out += std::to_string(v); }
void debug_print_float(double v) { char b[32]; snprintf(b, sizeof(b), "%g", v); g_out += b; }
void debug_printf(const char *fmt, ...) {
  char b[512]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof(b), fmt, ap); va_end(ap); g_out += b;
}

bool counter_engine_configure(uint8_t id, const CounterConfig *cfg) {
  g_last_counter_id = id; g_last_counter_cfg = *cfg; g_calls.push_back("counter_engine_configure");
  counter_config_set(id, cfg);  // som den rigtige motor: config-tabellen opdateres
  extern void counter_sw_init(uint8_t);
  counter_sw_init(id);          // som counter_engine_configure() for en SW-tæller
  return true;
}
bool timer_engine_configure(uint8_t id, const TimerConfig *cfg) {
  g_last_timer_id = id; g_last_timer_cfg = *cfg; g_calls.push_back("timer_engine_configure");
  return true;
}
bool config_save_to_nvs(const PersistConfig *) { g_calls.push_back("config_save_to_nvs"); return true; }

extern "C" int setenv(const char *, const char *, int) { return 0; }
extern "C" void tzset(void) {}
void esp_restart(void) { g_calls.push_back("esp_restart"); }

// Registerallokering: alt er ledigt (true = fri), ellers afviser parseren alt
#include "register_allocator.h"
bool register_allocator_check(uint16_t, RegisterOwner *owner) {
  if (owner) memset(owner, 0, sizeof(*owner));
  return true;
}
