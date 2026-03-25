/**
 * @file timer_engine.cpp
 * @brief Timer orchestration and state machine (LAYER 5)
 *
 * Ported from: Mega2560 v3.6.5 modbus_timers.cpp
 * Adapted to: ESP32 modular architecture
 *
 * Minimal implementation matching types.h TimerConfig
 */

#include "timer_engine.h"
#include "timer_config.h"
#include "registers.h"
#include "debug.h"
#include "constants.h"
#include "types.h"
#include <string.h>

/* ============================================================================
 * TIMER RUNTIME STATE (per timer)
 * ============================================================================ */

typedef struct {
  uint32_t phase_start_ms;
  uint8_t current_phase;
  uint8_t is_active;
} TimerRuntimeState;

static TimerRuntimeState timer_state[TIMER_COUNT] = {0};

/* ============================================================================
 * HELPER - SET COIL LEVEL
 * ============================================================================ */

static void set_coil_level(uint16_t coil_idx, uint8_t high) {
  if (coil_idx >= (COILS_SIZE * 8)) return;
  registers_set_coil(coil_idx, high ? 1 : 0);
}

static uint8_t get_discrete_input_level(uint16_t input_idx) {
  if (input_idx >= (DISCRETE_INPUTS_SIZE * 8)) return 0;
  return registers_get_discrete_input(input_idx) ? 1 : 0;
}

/* ============================================================================
 * MODE 1: ONE-SHOT (3-phase sequence)
 * ============================================================================ */

static void mode_one_shot(uint8_t id, TimerConfig* cfg, uint32_t now_ms) {
  TimerRuntimeState* state = &timer_state[id - 1];

  if (!state->is_active) return;

  switch (state->current_phase) {
    case 0:
      // Phase 1: set P1 level, start timing
      set_coil_level(cfg->output_coil, cfg->phase1_output_state);
      if (cfg->phase1_duration_ms == 0 || (now_ms - state->phase_start_ms >= cfg->phase1_duration_ms)) {
        state->current_phase = 1;
        state->phase_start_ms = now_ms;
      }
      break;

    case 1:
      // Phase 2: set P2 level
      set_coil_level(cfg->output_coil, cfg->phase2_output_state);
      if (cfg->phase2_duration_ms == 0 || (now_ms - state->phase_start_ms >= cfg->phase2_duration_ms)) {
        state->current_phase = 2;
        state->phase_start_ms = now_ms;
      }
      break;

    case 2:
      // Phase 3: set P3 level
      set_coil_level(cfg->output_coil, cfg->phase3_output_state);
      if (cfg->phase3_duration_ms == 0 || (now_ms - state->phase_start_ms >= cfg->phase3_duration_ms)) {
        // Sequence complete
        state->is_active = 0;
        state->current_phase = 0;
      }
      break;

    default:
      state->is_active = 0;
      state->current_phase = 0;
      break;
  }
}

/* ============================================================================
 * MODE 2: MONOSTABLE (retriggerable pulse)
 * ============================================================================ */

static void mode_monostable(uint8_t id, TimerConfig* cfg, uint32_t now_ms) {
  TimerRuntimeState* state = &timer_state[id - 1];

  if (!state->is_active) {
    // Not active: output at P1 level
    set_coil_level(cfg->output_coil, cfg->phase1_output_state);
    return;
  }

  // Active: start pulse
  if (state->current_phase == 0) {
    state->current_phase = 1;
    state->phase_start_ms = now_ms;
    set_coil_level(cfg->output_coil, cfg->phase2_output_state);
  }
  // Pulse ongoing: check if time expired
  else if (state->current_phase == 1 && (now_ms - state->phase_start_ms >= cfg->pulse_duration_ms)) {
    // Pulse complete: return to P1 level
    set_coil_level(cfg->output_coil, cfg->phase1_output_state);
    state->is_active = 0;
    state->current_phase = 0;
  }
}

/* ============================================================================
 * MODE 3: ASTABLE (oscillate between P1/P2)
 * ============================================================================ */

static void mode_astable(uint8_t id, TimerConfig* cfg, uint32_t now_ms) {
  TimerRuntimeState* state = &timer_state[id - 1];

  if (!state->is_active) return;  // Not running, stay silent

  if (state->current_phase == 0) {
    // Phase 0: output P1, timing T1
    set_coil_level(cfg->output_coil, cfg->phase1_output_state);
    if (cfg->on_duration_ms == 0 || (now_ms - state->phase_start_ms >= cfg->on_duration_ms)) {
      state->current_phase = 1;
      state->phase_start_ms = now_ms;
    }
  } else {
    // Phase 1: output P2, timing T2
    set_coil_level(cfg->output_coil, cfg->phase2_output_state);
    if (cfg->off_duration_ms == 0 || (now_ms - state->phase_start_ms >= cfg->off_duration_ms)) {
      state->current_phase = 0;
      state->phase_start_ms = now_ms;
    }
  }
}

/* ============================================================================
 * MODE 4: TRIGGER-DRIVEN (edge-triggered, simple output toggle)
 * ============================================================================ */

// Static tracking of previous input levels for Mode 4 edge detection
static uint8_t mode4_prev_input[TIMER_COUNT] = {0};
static uint8_t mode4_initialized[TIMER_COUNT] = {0};

static void mode_trigger(uint8_t id, TimerConfig* cfg, uint32_t now_ms) {
  TimerRuntimeState* state = &timer_state[id - 1];

  // For Mode 4: Read directly from a COIL as input trigger
  // This allows testing without GPIO mapping complexity
  // cfg->input_dis specifies which COIL to read as input
  // Example: input-dis:30 means read from COIL 30
  uint8_t input_level = registers_get_coil(cfg->input_dis) ? 1 : 0;

  // Initialize prev_input on first call after configuration
  if (!mode4_initialized[id - 1]) {
    mode4_prev_input[id - 1] = input_level;
    mode4_initialized[id - 1] = 1;
    return;  // Skip edge detection on initialization
  }

  // Track previous level for edge detection
  uint8_t prev_level = mode4_prev_input[id - 1];
  mode4_prev_input[id - 1] = input_level;

  // Detect edge
  uint8_t rising_edge = (prev_level == 0 && input_level == 1);
  uint8_t falling_edge = (prev_level == 1 && input_level == 0);
  uint8_t any_edge = rising_edge || falling_edge;

  // Check if configured trigger matches detected edge
  uint8_t trigger_detected = 0;
  if (cfg->trigger_edge == 1 && rising_edge) {
    trigger_detected = 1;  // Rising edge trigger
  } else if (cfg->trigger_edge == 0 && falling_edge) {
    trigger_detected = 1;  // Falling edge trigger
  }

  if (trigger_detected) {
    // Matching edge detected - set output immediately (or after delay)
    if (cfg->delay_ms == 0) {
      // Immediate: set output coil
      set_coil_level(cfg->output_coil, cfg->phase1_output_state);
      state->is_active = 1;
      state->current_phase = 1;
      state->phase_start_ms = now_ms;
    } else {
      // Delayed: wait delay_ms then set output
      if (!state->is_active) {
        state->is_active = 1;
        state->current_phase = 0;
        state->phase_start_ms = now_ms;
      }
    }
  }

  // Handle delayed output
  if (state->is_active && cfg->delay_ms > 0 && state->current_phase == 0) {
    if (now_ms - state->phase_start_ms >= cfg->delay_ms) {
      // Delay expired - set output
      set_coil_level(cfg->output_coil, cfg->phase1_output_state);
      state->current_phase = 1;
    }
  }

  // If not waiting for delay, keep output active
  if (state->is_active && state->current_phase == 1) {
    set_coil_level(cfg->output_coil, cfg->phase1_output_state);
  }
}

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

void timer_engine_init(void) {
  timer_config_init();
  memset(timer_state, 0, sizeof(timer_state));
}

/* ============================================================================
 * CONTROL REGISTER HANDLING (start/stop/reset via Modbus register)
 * ============================================================================ */

void timer_engine_handle_control(uint8_t id) {
  if (id < 1 || id > TIMER_COUNT) return;

  TimerConfig cfg;
  if (!timer_config_get(id, &cfg) || cfg.ctrl_reg >= HOLDING_REGS_SIZE) {
    return;
  }

  uint16_t ctrl_val = registers_get_holding_register(cfg.ctrl_reg);

  // Bit 0: Start command (triggers timer)
  if (ctrl_val & 0x0001) {
    timer_state[id - 1].is_active = 1;
    timer_state[id - 1].current_phase = 0;
    timer_state[id - 1].phase_start_ms = registers_get_millis();

    // Clear the start bit after executing
    registers_set_holding_register(cfg.ctrl_reg, ctrl_val & ~0x0001);
  }

  // Bit 1: Stop command
  if (ctrl_val & 0x0002) {
    timer_state[id - 1].is_active = 0;
    timer_state[id - 1].current_phase = 0;
    set_coil_level(cfg.output_coil, 0);  // Turn off output when stopped

    // Clear the stop bit after executing
    registers_set_holding_register(cfg.ctrl_reg, ctrl_val & ~0x0002);
  }

  // Bit 2: Reset command (clears timer state)
  if (ctrl_val & 0x0004) {
    timer_state[id - 1].is_active = 0;
    timer_state[id - 1].current_phase = 0;
    timer_state[id - 1].phase_start_ms = 0;
    set_coil_level(cfg.output_coil, 0);  // Turn off output when reset

    // For Mode 4, also reset edge detection
    if (cfg.mode == TIMER_MODE_4_INPUT_TRIGGERED) {
      mode4_initialized[id - 1] = 0;
    }

    // Clear the reset bit after executing
    registers_set_holding_register(cfg.ctrl_reg, ctrl_val & ~0x0004);
  }
}

/* ============================================================================
 * MAIN LOOP
 * ============================================================================ */

void timer_engine_loop(void) {
  uint32_t now_ms = registers_get_millis();

  for (uint8_t i = 0; i < TIMER_COUNT; i++) {
    TimerConfig cfg;
    if (!timer_config_get(i + 1, &cfg) || !cfg.enabled) {
      continue;
    }

    // Handle control register commands (start/stop/reset)
    timer_engine_handle_control(i + 1);

    // Run mode-specific state machine
    switch (cfg.mode) {
      case TIMER_MODE_1_ONESHOT:
        if (timer_state[i].is_active) {
          mode_one_shot(i + 1, &cfg, now_ms);
        }
        break;

      case TIMER_MODE_2_MONOSTABLE:
        mode_monostable(i + 1, &cfg, now_ms);
        break;

      case TIMER_MODE_3_ASTABLE:
        mode_astable(i + 1, &cfg, now_ms);
        break;

      case TIMER_MODE_4_INPUT_TRIGGERED:
        mode_trigger(i + 1, &cfg, now_ms);
        break;

      default:
        break;
    }
  }
}

/* ============================================================================
 * CONFIGURATION & CONTROL
 * ============================================================================ */

bool timer_engine_configure(uint8_t id, const TimerConfig* cfg) {
  if (cfg == NULL) return false;

  // Validate and set
  if (!timer_config_set(id, cfg)) {
    return false;
  }

  // Initialize runtime state
  if (id >= 1 && id <= TIMER_COUNT) {
    timer_state[id - 1].is_active = 0;
    timer_state[id - 1].current_phase = 0;
    timer_state[id - 1].phase_start_ms = 0;

    // Reset Mode 4 edge detection on reconfiguration
    if (cfg->mode == TIMER_MODE_4_INPUT_TRIGGERED) {
      mode4_initialized[id - 1] = 0;  // Will re-initialize on next read
    }

    // Auto-start astable mode when enabled (continuous oscillation)
    if (cfg->mode == TIMER_MODE_3_ASTABLE && cfg->enabled) {
      timer_state[id - 1].is_active = 1;
      timer_state[id - 1].phase_start_ms = registers_get_millis();
    }
  }

  return true;
}

bool timer_engine_get_config(uint8_t id, TimerConfig* out) {
  return timer_config_get(id, out);
}

bool timer_engine_get_runtime(uint8_t id, uint8_t* out_phase, uint8_t* out_active) {
  if (id < 1 || id > TIMER_COUNT) return false;
  if (out_phase)  *out_phase  = timer_state[id - 1].current_phase;
  if (out_active) *out_active = timer_state[id - 1].is_active;
  return true;
}

/* ============================================================================
 * COIL WRITE CALLBACK
 * ============================================================================ */

void timer_engine_on_coil_write(uint16_t coil_idx, uint8_t value) {
  (void)value;  // Value not used, just trigger on write
  uint32_t now_ms = registers_get_millis();

  for (uint8_t i = 0; i < TIMER_COUNT; i++) {
    TimerConfig cfg;
    if (!timer_config_get(i + 1, &cfg) || !cfg.enabled) {
      continue;
    }

    if (cfg.output_coil != coil_idx) continue;

    // Skip if astable already running (no retrigger for astable)
    if (cfg.mode == TIMER_MODE_3_ASTABLE && timer_state[i].is_active) continue;

    // Trigger this timer
    timer_state[i].is_active = 1;
    timer_state[i].current_phase = 0;
    timer_state[i].phase_start_ms = now_ms;
  }
}

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

bool timer_engine_has_coil(uint16_t coil_idx) {
  for (uint8_t i = 0; i < TIMER_COUNT; i++) {
    TimerConfig cfg;
    if (!timer_config_get(i + 1, &cfg) || !cfg.enabled) {
      continue;
    }

    if (cfg.output_coil == coil_idx) return true;
  }

  return false;
}

void timer_engine_disable_all(void) {
  for (uint8_t i = 0; i < TIMER_COUNT; i++) {
    TimerConfig cfg;
    if (!timer_config_get(i + 1, &cfg)) continue;

    cfg.enabled = 0;
    timer_config_set(i + 1, &cfg);
    timer_state[i].is_active = 0;
  }
}

void timer_engine_clear_alarms(void) {
  // Stub: alarm handling not yet implemented
}
