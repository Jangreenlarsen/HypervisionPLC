// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file counter_sw.cpp
 * @brief Software polling mode counter (LAYER 5)
 *
 * Ported from: Mega2560 v3.6.5 modbus_counters.cpp
 * Adapted to: ESP32 modular architecture
 *
 * Responsibility:
 * - Polling discrete inputs for edge detection
 * - Counting edges (all edges - no prescaler skipping here)
 * - Debounce filtering
 * - Overflow detection
 */

#include "counter_sw.h"
#include "counter_config.h"
#include "registers.h"
#include "constants.h"
#include "types.h"
#include "config_struct.h"
#include <string.h>
#include <freertos/FreeRTOS.h>  // FEAT-438: portMUX_TYPE
#include "encoder_quad.h"        // FEAT-470: encoder-tilstand

/* ============================================================================
 * SW MODE RUNTIME STATE (per counter)
 * Uses CounterSWState from types.h
 * ============================================================================ */

static CounterSWState sw_state[COUNTER_COUNT] = {0};

/* ============================================================================
 * FEAT-438: HURTIG STI VIA SCAN-TASKEN
 *
 * Hovedloekken naar kun at se indgangen een gang pr. gennemloeb (typisk 5-20
 * ms paa ES32D26), saa SW-taelleren klarede ca. 50-150 Hz. Er taellerens
 * discrete input mappet fra en skifteregister-indgang (virtuel GPIO 101-108),
 * samples den i stedet hvert 1 ms af gpio_driver's scan-task, som taeller
 * flankerne her og afleverer dem via fast_pending. Debounce = spaerretid
 * efter en talt flanke (samme betydning som i den pollede sti).
 * Felterne skrives fra hovedloekken og laeses af tasken; fast_active
 * nulstilles foerst ved omkonfiguration, saa tasken aldrig ser halve vaerdier.
 * ============================================================================ */
typedef struct {
  volatile uint8_t  active;        // 1 = brug scan-tasken
  volatile uint8_t  counting;      // spejl af is_counting
  uint8_t           bit;           // bit i 74HC165-byten
  uint8_t           edge;          // CounterEdgeType
  uint32_t          debounce_us;   // 0 = ingen debounce
  uint8_t           last_level;    // kun tasken
  uint32_t          last_edge_us;  // kun tasken
  volatile uint32_t pending;       // talte flanker, ikke afleveret endnu
  // FEAT-470: encoder-tilstand (COUNTER_HW_ENCODER): bit = CLK, bit_b = DT
  uint8_t           enc;           // 1 = quadrature-dekodning
  uint8_t           bit_b;
  uint8_t           last_ab;       // kun tasken: (clk << 1) | dt
  volatile int32_t  enc_pending;   // overgange med fortegn, ikke afleveret endnu
} CounterSWFast;

static CounterSWFast sw_fast[COUNTER_COUNT];
static portMUX_TYPE sw_fast_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t sw_fast_refresh_ms[COUNTER_COUNT] = {0};

#ifdef SHIFT_REGISTER_ENABLED
// Find skifteregister-bit for en discrete input: en GPIO-input-mapping
// fra virtuel pin 101-108 til samme DI-index. -1 = ingen (pollet sti bruges).
static int sw_fast_find_bit(uint16_t input_dis) {
  for (uint8_t i = 0; i < g_persist_config.var_map_count; i++) {
    const VariableMapping *m = &g_persist_config.var_maps[i];
    if (m->source_type != MAPPING_SOURCE_GPIO || !m->is_input) continue;
    if (m->input_reg != input_dis) continue;
    if (m->gpio_pin >= VGPIO_SR_INPUT_BASE && m->gpio_pin < VGPIO_SR_INPUT_BASE + 8) {
      return m->gpio_pin - VGPIO_SR_INPUT_BASE;
    }
  }
  return -1;
}
#endif

// (Gen)beregn den hurtige sti for en taeller ud fra config + GPIO-mapping
static void sw_fast_configure(uint8_t id, const CounterConfig *cfg) {
  CounterSWFast *f = &sw_fast[id - 1];
  int bit = -1, bit_b = -1;
  const uint8_t enc = (cfg->hw_mode == COUNTER_HW_ENCODER) ? 1 : 0;
#ifdef SHIFT_REGISTER_ENABLED
  if (cfg->enabled && (cfg->hw_mode == COUNTER_HW_SW || enc)) bit = sw_fast_find_bit(cfg->input_dis);
  if (bit >= 0 && enc) {
    bit_b = sw_fast_find_bit(COUNTER_ENC_DT(cfg));
    if (bit_b < 0 || bit_b == bit) bit = -1;  // begge skal vaere skifteregister-indgange
  }
#endif
  uint32_t deb_us = cfg->debounce_enabled ? (uint32_t)(cfg->debounce_ms > 0 ? cfg->debounce_ms : 10) * 1000UL : 0;
  if (enc) deb_us = 0;  // encoder: tilstandstabellen haandterer prel
  if (bit < 0) { f->active = 0; return; }
  if (f->active && f->enc == enc && f->bit == (uint8_t)bit && (!enc || f->bit_b == (uint8_t)bit_b) &&
      f->edge == (uint8_t)cfg->edge_type && f->debounce_us == deb_us) return;
  f->active = 0;  // tasken springer over mens felterne skiftes
  f->enc = enc;
  f->bit = (uint8_t)bit;
  f->bit_b = enc ? (uint8_t)bit_b : 0;
  f->edge = (uint8_t)cfg->edge_type;
  f->debounce_us = deb_us;
  f->last_level = (uint8_t)(registers_get_discrete_input(cfg->input_dis) ? 1 : 0);
  f->last_ab = (uint8_t)((f->last_level << 1) | ((enc && registers_get_discrete_input(COUNTER_ENC_DT(cfg))) ? 1 : 0));
  f->last_edge_us = 0;
  f->active = 1;
}

void counter_sw_fast_scan(uint8_t sr_bits, uint32_t now_us) {
  for (uint8_t i = 0; i < COUNTER_COUNT; i++) {
    CounterSWFast *f = &sw_fast[i];
    if (!f->active) continue;
    if (f->enc) {  // FEAT-470
      uint8_t ab = (uint8_t)((((sr_bits >> f->bit) & 1) << 1) | ((sr_bits >> f->bit_b) & 1));
      if (ab != f->last_ab) {
        int8_t d = encoder_quad_step(f->last_ab, ab);
        f->last_ab = ab;
        if (d && f->counting) {
          portENTER_CRITICAL(&sw_fast_mux);
          f->enc_pending += d;
          portEXIT_CRITICAL(&sw_fast_mux);
        }
      }
      continue;
    }
    uint8_t level = (sr_bits >> f->bit) & 1;
    if (f->debounce_us && f->last_edge_us && (uint32_t)(now_us - f->last_edge_us) < f->debounce_us) {
      continue;  // spaerretid efter talt flanke — niveauet opdateres ikke (som den pollede sti)
    }
    uint8_t edge = 0;
    if (f->edge == COUNTER_EDGE_RISING) edge = (f->last_level == 0 && level == 1);
    else if (f->edge == COUNTER_EDGE_FALLING) edge = (f->last_level == 1 && level == 0);
    else edge = (f->last_level != level);
    f->last_level = level;
    if (edge && f->counting) {
      portENTER_CRITICAL(&sw_fast_mux);
      f->pending++;
      portEXIT_CRITICAL(&sw_fast_mux);
      if (f->debounce_us) f->last_edge_us = now_us ? now_us : 1;
    }
  }
}

uint8_t counter_sw_fast_active(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return 0;
  return sw_fast[id - 1].active;
}

static uint32_t sw_fast_take(uint8_t id) {
  CounterSWFast *f = &sw_fast[id - 1];
  portENTER_CRITICAL(&sw_fast_mux);
  uint32_t n = f->pending;
  f->pending = 0;
  f->enc_pending = 0;   // FEAT-470: ogsaa encoder-overgange (kaldes kun for at kassere)
  portEXIT_CRITICAL(&sw_fast_mux);
  return n;
}

static int32_t sw_fast_take_enc(uint8_t id) {
  CounterSWFast *f = &sw_fast[id - 1];
  portENTER_CRITICAL(&sw_fast_mux);
  int32_t n = f->enc_pending;
  f->enc_pending = 0;
  portEXIT_CRITICAL(&sw_fast_mux);
  return n;
}

// FEAT-470: encoder - laeg overgange (med fortegn) til og wrap paa bit-bredden
// i begge retninger (ingen start_value-wrap: positionen er en ren taeller)
static void sw_apply_enc(CounterSWState *state, const CounterConfig *cfg, int32_t d) {
  if (d == 0) return;
  if (cfg->direction == COUNTER_DIR_DOWN) d = -d;
  uint64_t mask = 0xFFFFFFFFFFFFFFFFULL;
  switch (cfg->bit_width) {
    case 8:  mask = 0xFFULL; break;
    case 16: mask = 0xFFFFULL; break;
    case 32: mask = 0xFFFFFFFFULL; break;
  }
  uint64_t before = state->counter_value & mask;
  uint64_t after = (before + (uint64_t)(int64_t)d) & mask;
  if ((d > 0 && after < before) || (d < 0 && after > before)) state->overflow_flag = 1;
  state->counter_value = after;
}

// Tael een flanke (retning, under-/overloeb) — faelles for begge stier
static void sw_apply_edge(CounterSWState *state, const CounterConfig *cfg) {
  if (cfg->direction == COUNTER_DIR_UP) {
    state->counter_value++;
    uint64_t max_val = 0xFFFFFFFFFFFFFFFFULL;
    switch (cfg->bit_width) {
      case 8:  max_val = 0xFFULL; break;
      case 16: max_val = 0xFFFFULL; break;
      case 32: max_val = 0xFFFFFFFFULL; break;
    }
    // BUG-180 FIX: Preserve overflow counts when wrapping to start_value
    if (state->counter_value > max_val) {
      uint64_t overflow_amt = state->counter_value - max_val - 1;
      state->counter_value = (cfg->start_value + overflow_amt) & max_val;
      state->overflow_flag = 1;
    }
  } else {
    // BUG-181 FIX: DOWN counting: decrement with underflow wrap to start_value
    if (state->counter_value > 0) {
      state->counter_value--;
    } else {
      state->counter_value = cfg->start_value;
      state->overflow_flag = 1;
    }
  }
}

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

void counter_sw_init(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return;

  CounterSWState* state = &sw_state[id - 1];
  state->counter_value = 0;
  state->last_level = 0;
  // BUG FIX 1.7: Initialize debounce_timer to current time to prevent initial false window
  state->debounce_timer = registers_get_millis();
  state->is_counting = 0;
  state->overflow_flag = 0;  // BUG FIX 1.1: Initialize overflow flag

  // Get config to initialize last_level
  CounterConfig cfg;
  if (counter_config_get(id, &cfg)) {
    // Read initial level from discrete input
    if (cfg.input_dis < (DISCRETE_INPUTS_SIZE * 8)) {
      state->last_level = registers_get_discrete_input(cfg.input_dis) ? 1 : 0;
    }

    // FEAT-470: encoder - pollet sti gemmer (clk << 1) | dt i last_level
    if (cfg.hw_mode == COUNTER_HW_ENCODER) {
      uint16_t dt = COUNTER_ENC_DT(&cfg);
      uint8_t dt_l = (dt < (DISCRETE_INPUTS_SIZE * 8) && registers_get_discrete_input(dt)) ? 1 : 0;
      state->last_level = (state->last_level << 1) | dt_l;
    }

    // Set start value
    state->counter_value = cfg.start_value;
    sw_fast_configure(id, &cfg);  // FEAT-438
    sw_fast_take(id);             // kassér flanker fra foer (om)konfigurationen
  }
}

/* ============================================================================
 * MAIN LOOP - POLLING & EDGE DETECTION
 * ============================================================================ */

void counter_sw_loop(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return;

  CounterConfig cfg;
  if (!counter_config_get(id, &cfg)) return;

  if (!cfg.enabled || (cfg.hw_mode != COUNTER_HW_SW && cfg.hw_mode != COUNTER_HW_ENCODER)) {
    return;
  }
  const bool enc = (cfg.hw_mode == COUNTER_HW_ENCODER);

  CounterSWState* state = &sw_state[id - 1];

  // FEAT-438: hold den hurtige sti i takt med config/GPIO-mapping (billigt, hvert 500 ms)
  uint32_t now_chk = registers_get_millis();
  if (now_chk - sw_fast_refresh_ms[id - 1] >= 500) {
    sw_fast_refresh_ms[id - 1] = now_chk;
    sw_fast_configure(id, &cfg);
  }
  sw_fast[id - 1].counting = state->is_counting;

  if (sw_fast[id - 1].active) {
    if (enc) {
      int32_t d = sw_fast_take_enc(id);
      if (state->is_counting) sw_apply_enc(state, &cfg, d);
      return;
    }
    uint32_t n = sw_fast_take(id);
    if (state->is_counting) {
      while (n--) sw_apply_edge(state, &cfg);
    }
    return;
  }

  // BUG FIX 2.1: Check if counting is enabled (start/stop control)
  if (!state->is_counting) {
    return;  // Counter stopped, skip counting
  }

  if (enc) {
    // FEAT-470: pollet encoder (indgange uden skifteregister) - kun saa hurtigt
    // som hovedloekken; last_level holder (clk << 1) | dt
    const uint16_t dt = COUNTER_ENC_DT(&cfg);
    uint8_t clk_l = (cfg.input_dis < (DISCRETE_INPUTS_SIZE * 8) && registers_get_discrete_input(cfg.input_dis)) ? 1 : 0;
    uint8_t dt_l = (dt < (DISCRETE_INPUTS_SIZE * 8) && registers_get_discrete_input(dt)) ? 1 : 0;
    uint8_t ab = (uint8_t)((clk_l << 1) | dt_l);
    if (ab != (uint8_t)state->last_level) {
      sw_apply_enc(state, &cfg, encoder_quad_step((uint8_t)state->last_level, ab));
      state->last_level = ab;
    }
    return;
  }

  // Read current level from discrete input
  uint8_t current_level = (cfg.input_dis < (DISCRETE_INPUTS_SIZE * 8)) ?
    registers_get_discrete_input(cfg.input_dis) ? 1 : 0 : 0;

  // BUG FIX 1.7: Check debounce_enabled before applying debounce
  uint32_t now_ms = registers_get_millis();

  if (cfg.debounce_enabled) {
    // Debounce: only count if enough time has passed
    uint32_t debounce_ms = cfg.debounce_ms > 0 ? cfg.debounce_ms : 10;  // Default 10ms

    if (now_ms - state->debounce_timer < debounce_ms) {
      return;  // Still in debounce window
    }
  }

  // Edge detection based on mode
  uint8_t edge_detected = 0;

  if (cfg.edge_type == COUNTER_EDGE_RISING && state->last_level == 0 && current_level == 1) {
    edge_detected = 1;
  } else if (cfg.edge_type == COUNTER_EDGE_FALLING && state->last_level == 1 && current_level == 0) {
    edge_detected = 1;
  } else if (cfg.edge_type == COUNTER_EDGE_BOTH && state->last_level != current_level) {
    edge_detected = 1;
  }

  // Update last level for next iteration
  state->last_level = current_level;

  // Count the edge
  if (edge_detected) {
    sw_apply_edge(state, &cfg);  // BUG FIX 1.5 + BUG-180/181 (faelles med FEAT-438)

    // BUG FIX 1.7: Only update debounce timer if debounce is enabled
    if (cfg.debounce_enabled) {
      state->debounce_timer = now_ms;  // Reset debounce timer
    }
  }
}

/* ============================================================================
 * RESET
 * ============================================================================ */

void counter_sw_reset(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return;

  CounterConfig cfg;
  if (!counter_config_get(id, &cfg)) return;

  CounterSWState* state = &sw_state[id - 1];
  state->counter_value = cfg.start_value;
  state->debounce_timer = 0;
  sw_fast_take(id);  // FEAT-438: flanker fra foer nulstillingen taeller ikke med
}

/* ============================================================================
 * VALUE ACCESS
 * ============================================================================ */

uint64_t counter_sw_get_value(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return 0;
  return sw_state[id - 1].counter_value;
}

void counter_sw_set_value(uint8_t id, uint64_t value) {
  if (id < 1 || id > COUNTER_COUNT) return;
  sw_state[id - 1].counter_value = value;
}

uint8_t counter_sw_get_overflow(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return 0;
  // BUG FIX 1.1: Return actual overflow flag
  return sw_state[id - 1].overflow_flag;
}

void counter_sw_clear_overflow(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return;
  // BUG FIX 1.1: Clear overflow flag
  sw_state[id - 1].overflow_flag = 0;
}

/* ============================================================================
 * START/STOP CONTROL (BUG FIX 2.1: Control register bits)
 * ============================================================================ */

void counter_sw_start(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return;
  sw_state[id - 1].is_counting = 1;
}

void counter_sw_stop(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return;
  sw_state[id - 1].is_counting = 0;
}
