// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file timer_config.cpp
 * @brief Timer configuration and validation (LAYER 5)
 *
 * Minimal implementation - stores timer configuration
 */

#include "timer_config.h"
#include "registers.h"
#include "constants.h"
#include "types.h"
#include <string.h>

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================ */

static TimerConfig timer_configs[TIMER_COUNT];

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

void timer_config_init(void) {
  for (uint8_t i = 0; i < TIMER_COUNT; i++) {
    timer_configs[i] = timer_config_defaults(i + 1);
  }
}

/* ============================================================================
 * DEFAULTS
 * ============================================================================ */

TimerConfig timer_config_defaults(uint8_t id) {
  TimerConfig cfg = {0};

  cfg.enabled = 0;
  cfg.mode = TIMER_MODE_1_ONESHOT;

  // Mode 1: One-shot
  cfg.phase1_duration_ms = 1000;
  cfg.phase2_duration_ms = 1000;
  cfg.phase3_duration_ms = 1000;
  cfg.phase1_output_state = 0;
  cfg.phase2_output_state = 1;
  cfg.phase3_output_state = 0;

  // Mode 2: Monostable
  cfg.pulse_duration_ms = 1000;
  cfg.trigger_level = 1;

  // Mode 3: Astable
  cfg.on_duration_ms = 500;
  cfg.off_duration_ms = 500;

  // Mode 4: Input-triggered
  cfg.input_dis = 0;
  cfg.delay_ms = 0;
  cfg.trigger_edge = 0;

  // Output
  cfg.output_coil = 0;

  // BUG-187 FIX: Smart defaults for ctrl_reg (like counters)
  // Timer 1 → HR180, Timer 2 → HR185, Timer 3 → HR190, Timer 4 → HR195
  // This prevents overlap with Counter registers (HR100-174) and ST Logic (HR200-237)
  cfg.ctrl_reg = 180 + ((id - 1) * 5);

  return cfg;
}

/* ============================================================================
 * VALIDATION
 * ============================================================================ */

bool timer_config_validate(const TimerConfig* cfg) {
  if (cfg == NULL) return false;

  // Check mode is valid (1-4)
  if (cfg->mode < TIMER_MODE_1_ONESHOT || cfg->mode > TIMER_MODE_4_INPUT_TRIGGERED) {
    return false;
  }

  return true;
}

void timer_config_sanitize(TimerConfig* cfg) {
  if (cfg == NULL) return;

  // Clamp mode to valid range
  if (cfg->mode < TIMER_MODE_1_ONESHOT || cfg->mode > TIMER_MODE_4_INPUT_TRIGGERED) {
    cfg->mode = TIMER_MODE_1_ONESHOT;
  }

  // Ensure binary states
  cfg->phase1_output_state = (cfg->phase1_output_state ? 1 : 0);
  cfg->phase2_output_state = (cfg->phase2_output_state ? 1 : 0);
  cfg->phase3_output_state = (cfg->phase3_output_state ? 1 : 0);
  cfg->trigger_level = (cfg->trigger_level ? 1 : 0);
}

/* ============================================================================
 * CONFIGURATION ACCESS
 * ============================================================================ */

bool timer_config_get(uint8_t id, TimerConfig* out) {
  if (id < 1 || id > TIMER_COUNT || out == NULL) return false;

  *out = timer_configs[id - 1];
  return true;
}

bool timer_config_set(uint8_t id, const TimerConfig* cfg) {
  if (id < 1 || id > TIMER_COUNT || cfg == NULL) return false;

  // Validate before setting
  if (!timer_config_validate(cfg)) {
    return false;
  }

  // Sanitize and store
  TimerConfig sanitized = *cfg;
  timer_config_sanitize(&sanitized);
  timer_configs[id - 1] = sanitized;

  return true;
}

TimerConfig* timer_config_get_all(void) {
  return timer_configs;
}
