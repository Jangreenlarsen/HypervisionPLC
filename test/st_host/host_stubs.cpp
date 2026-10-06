// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
// Stubs for at koere ST-compiler + VM paa PC'en (BUG-433-test). Ingen hardware.
#include <stdarg.h>
#include <stdio.h>
#include "st_types.h"
#include "st_logic_config.h"
#include "types.h"
#include "encoder_sw.h"

uint32_t g_fake_ms = 1000;
uint32_t millis(void) { return g_fake_ms; }
uint32_t micros(void) { return g_fake_ms * 1000; }


extern "C" int debug_printf(const char *, ...) { return 0; }
extern "C" void debug_println(const char *) {}

uint16_t g_mb_multi_reg_buf[32]; bool g_mb_multi_coil_buf[32];
uint16_t g_mbx_multi_reg_buf[32]; bool g_mbx_multi_coil_buf[32];

static st_value_t z() { st_value_t v; v.dint_val = 0; return v; }
st_value_t st_builtin_mb_busy_func() { return z(); }
st_value_t st_builtin_mb_cache_func(st_value_t) { return z(); }
st_value_t st_builtin_mb_error_func() { return z(); }
st_value_t st_builtin_mb_read_coil(st_value_t, st_value_t) { return z(); }
st_value_t st_builtin_mb_read_holding(st_value_t, st_value_t) { return z(); }
st_value_t st_builtin_mb_read_holdings(st_value_t, st_value_t, st_value_t) { return z(); }
st_value_t st_builtin_mb_read_input(st_value_t, st_value_t) { return z(); }
st_value_t st_builtin_mb_read_input_reg(st_value_t, st_value_t) { return z(); }
st_value_t st_builtin_mb_read_ok_func() { return z(); }
st_value_t st_builtin_mb_success_func() { return z(); }
st_value_t st_builtin_mb_write_coil(st_value_t, st_value_t, st_value_t) { return z(); }
st_value_t st_builtin_mb_write_coils(st_value_t, st_value_t, st_value_t) { return z(); }
st_value_t st_builtin_mb_write_holding(st_value_t, st_value_t, st_value_t) { return z(); }
st_value_t st_builtin_mb_write_holdings(st_value_t, st_value_t, st_value_t) { return z(); }
st_value_t st_builtin_mb_write_queued_func() { return z(); }
st_value_t st_builtin_mbx_busy_func() { return z(); }
st_value_t st_builtin_mbx_error_func() { return z(); }
st_value_t st_builtin_mbx_read_coil(st_value_t, st_value_t, st_value_t, st_value_t) { return z(); }
st_value_t st_builtin_mbx_read_holding(st_value_t, st_value_t, st_value_t, st_value_t) { return z(); }
st_value_t st_builtin_mbx_read_input(st_value_t, st_value_t, st_value_t, st_value_t) { return z(); }
st_value_t st_builtin_mbx_read_input_reg(st_value_t, st_value_t, st_value_t, st_value_t) { return z(); }
st_value_t st_builtin_mbx_success_func() { return z(); }
st_value_t st_builtin_mbx_write_coil(st_value_t, st_value_t, st_value_t, st_value_t, st_value_t) { return z(); }
st_value_t st_builtin_mbx_write_coils(st_value_t, st_value_t, st_value_t, st_value_t, st_value_t) { return z(); }
st_value_t st_builtin_mbx_write_holding(st_value_t, st_value_t, st_value_t, st_value_t, st_value_t) { return z(); }
st_value_t st_builtin_mbx_write_holdings(st_value_t, st_value_t, st_value_t, st_value_t, st_value_t) { return z(); }
st_value_t st_builtin_mbx_read_holdings(st_value_t, st_value_t, st_value_t, st_value_t, st_value_t) { return z(); }  // FEAT-461
st_value_t st_builtin_persist_load(st_value_t) { return z(); }
st_value_t st_builtin_persist_save(st_value_t) { return z(); }

bool counter_config_get(uint8_t, CounterConfig *) { return false; }
bool counter_config_set(uint8_t, const CounterConfig *) { return false; }
bool counter_engine_configure(uint8_t, const CounterConfig *) { return false; }
uint64_t counter_engine_get_value(uint8_t) { return 0; }
int16_t encoder_sw_pos(int16_t clk, int16_t dt) { return (int16_t)(clk * 10 + dt); }  // FEAT-470: testbar markør
void counter_engine_reset(uint8_t) {}
uint16_t counter_frequency_get(uint8_t) { return 0; }
uint16_t registers_get_holding_register(uint16_t) { return 0; }
void registers_set_holding_register(uint16_t, uint16_t) {}

static st_logic_engine_state_t *g_state = nullptr;
st_logic_engine_state_t *st_logic_get_state() {
  if (!g_state) g_state = (st_logic_engine_state_t *)calloc(1, sizeof(st_logic_engine_state_t));
  return g_state;
}
uint8_t st_logic_globals_lookup(st_logic_engine_state_t *, const char *) { return 0xFF; }
void st_logic_lock_variables() {}
void st_logic_unlock_variables() {}
void st_wdt_feed(const void *) {}
