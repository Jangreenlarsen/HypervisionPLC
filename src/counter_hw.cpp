// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file counter_hw.cpp
 * @brief Hardware PCNT mode counter (LAYER 5)
 *
 * Ported from: Mega2560 v3.6.5 modbus_counters_hw.cpp (Timer5 only)
 * Adapted to: ESP32 PCNT (Pulse Counter) hardware
 *
 * Responsibility:
 * - PCNT unit initialization and configuration
 * - Reading pulse counts directly from PCNT hardware
 * - Overflow handling
 * - Value accumulation and tracking
 * - BUG FIX P0.6: Dedicated RTOS task for high-frequency PCNT polling
 */

#include "counter_hw.h"
#include "counter_config.h"
#include "pcnt_driver.h"
#include "registers.h"
#include "constants.h"
#include "types.h"
#include <string.h>
#include <driver/pcnt.h>  // BUG FIX P0: Include ESP-IDF PCNT for pause/resume
#include <esp_log.h>
#include <Arduino.h>  // For millis()
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/* ============================================================================
 * HW MODE RUNTIME STATE (per counter)
 * Uses CounterHWState from types.h
 * ============================================================================ */

static CounterHWState hw_state[COUNTER_COUNT] = {0};

/* ============================================================================
 * PCNT UNIT MAPPING
 * For now: Counter 1 → PCNT0, Counter 2 → PCNT1, etc.
 * ============================================================================ */

static const uint8_t counter_to_pcnt[COUNTER_COUNT] = {0, 1, 2, 3};

/* ============================================================================
 * RTOS TASK FOR HIGH-FREQUENCY PCNT POLLING (BUG FIX P0.6)
 * Priority 10 = higher than loop() task (priority 1)
 * Polls every 10ms to prevent PCNT wrap being missed
 * ============================================================================ */

static TaskHandle_t pcnt_task_handle = NULL;

static void pcnt_poll_task(void* pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(10);  // 10ms = 100 Hz poll rate

  for (;;) {
    // Poll all PCNT counters
    for (uint8_t id = 1; id <= COUNTER_COUNT; id++) {
      CounterConfig cfg;
      if (!counter_config_get(id, &cfg)) continue;
      if (!cfg.enabled || cfg.hw_mode != COUNTER_HW_PCNT) continue;

      CounterHWState* state = &hw_state[id - 1];
      if (!state->is_counting) continue;

      uint8_t pcnt_unit = counter_to_pcnt[id - 1];
      uint32_t hw_count = pcnt_unit_get_count(pcnt_unit);

      // BUG-024 EXTENDED FIX: Use 16-bit wrap handling (PCNT hardware is 16-bit)
      // pcnt_driver.cpp now returns uint16_t (0-65535) properly converted from int16_t
      // Must detect wrap at 65536, not 2^32
      if (hw_count != state->last_count) {
        // Calculate delta with 16-bit wrap handling
        int64_t delta;
        if (hw_count >= state->last_count) {
          // Normal case: counter increased
          delta = (int64_t)hw_count - (int64_t)state->last_count;
        } else {
          // Wrap case: counter wrapped around at 65536 (2^16)
          // delta = (hw_count + 65536) - last_count = (hw_count - last_count) + 65536
          delta = (int64_t)hw_count - (int64_t)state->last_count;
          // delta is negative, add 65536 to get positive wrap delta
          delta += 65536;  // 2^16 for PCNT 16-bit hardware
        }

        // Apply direction
        if (cfg.direction == COUNTER_DIR_DOWN) {
          delta = -delta;
        }

        // Apply delta
        if (delta > 0) {
          state->pcnt_value += (uint64_t)delta;
        } else if (delta < 0) {
          // BUG-181 FIX: DOWN counting with underflow wrapping to start_value
          uint64_t abs_delta = (uint64_t)(-delta);
          if (state->pcnt_value >= abs_delta) {
            state->pcnt_value -= abs_delta;
          } else {
            // Underflow: wrap to start_value and continue counting
            // Example: value=2, delta=-5, start=1000 → (2 - 5) = -3 → wrap to 1000 - 3 = 997
            uint64_t underflow = abs_delta - state->pcnt_value;
            state->pcnt_value = cfg.start_value - underflow;
            state->overflow_count++;  // Set overflow flag on underflow
          }
        }

        state->last_count = hw_count;

        // DEBUG: Log PCNT activity every 5 seconds (FIX P0.7 verification)
        static uint32_t last_log[COUNTER_COUNT] = {0};
        uint32_t now = millis();
        if (now - last_log[id - 1] >= 5000 && state->pcnt_value > 0) {
          // BUG-024 EXTENDED: hw_count is 16-bit unsigned (0-65535) from PCNT hardware
          ESP_LOGI("CNTR_HW", "C%d PCNT: hw_count=%lu delta=%lld pcnt_value=%llu edge=%d",
                   id, hw_count, delta, state->pcnt_value, cfg.edge_type);
          last_log[id - 1] = now;
        }
      }

      // Check overflow
      uint64_t max_val = 0xFFFFFFFFFFFFFFFFULL;
      switch (cfg.bit_width) {
        case 8: max_val = 0xFFULL; break;
        case 16: max_val = 0xFFFFULL; break;
        case 32: max_val = 0xFFFFFFFFULL; break;
      }

      // BUG-180 FIX: Preserve overflow counts when wrapping to start_value
      if (state->pcnt_value > max_val) {
        uint64_t overflow_amt = state->pcnt_value - max_val - 1;
        state->pcnt_value = (cfg.start_value + overflow_amt) & max_val;
        state->overflow_count++;
      }
    }

    // Wait for next poll cycle
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

void counter_hw_init(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return;

  CounterHWState* state = &hw_state[id - 1];
  state->pcnt_value = 0;
  state->last_count = 0;
  state->overflow_count = 0;
  state->is_counting = 0;

  // Initialize PCNT unit (zero counter)
  uint8_t pcnt_unit = counter_to_pcnt[id - 1];
  pcnt_unit_init(pcnt_unit);

  // Get config to set start value
  CounterConfig cfg;
  if (counter_config_get(id, &cfg)) {
    state->pcnt_value = cfg.start_value;
  }

  // BUG FIX P0.6: Start RTOS task on first init (only once)
  if (id == 1 && pcnt_task_handle == NULL) {
    xTaskCreatePinnedToCore(
      pcnt_poll_task,       // Task function
      "pcnt_poll",          // Task name
      4096,                 // Stack size (bytes)
      NULL,                 // Parameters
      10,                   // Priority (higher than loop = 1)
      &pcnt_task_handle,    // Task handle
      1                     // Core 1 (same as loop)
    );
    ESP_LOGI("CNTR_HW", "RTOS task created for PCNT polling (100Hz, priority 10)");
  }
}

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

bool counter_hw_configure(uint8_t id, uint8_t gpio_pin) {
  if (id < 1 || id > COUNTER_COUNT) return false;

  CounterHWState* state = &hw_state[id - 1];
  uint8_t pcnt_unit = counter_to_pcnt[id - 1];

  // Configure PCNT with GPIO pin
  CounterConfig cfg;
  if (!counter_config_get(id, &cfg)) return false;

  // Determine edge mode for PCNT
  pcnt_edge_mode_t pos_edge = PCNT_EDGE_RISING;  // Default
  pcnt_edge_mode_t neg_edge = PCNT_EDGE_FALLING;

  if (cfg.edge_type == COUNTER_EDGE_RISING) {
    pos_edge = PCNT_EDGE_RISING;
    neg_edge = PCNT_EDGE_DISABLE;
  } else if (cfg.edge_type == COUNTER_EDGE_FALLING) {
    pos_edge = PCNT_EDGE_DISABLE;
    neg_edge = PCNT_EDGE_FALLING;
  } else {
    // COUNTER_EDGE_BOTH: count all edges
    pos_edge = PCNT_EDGE_RISING;
    neg_edge = PCNT_EDGE_FALLING;
  }

  // Direction support: PCNT hardware counts incrementally (can be positive/negative)
  // We apply direction in delta calculation (lines 82-84 in pcnt_poll_task)
  // DOWN direction: delta is inverted to decrement counter value

  // Configure PCNT unit
  pcnt_unit_configure(pcnt_unit, gpio_pin, pos_edge, neg_edge);

  // Set start value in pcnt_value
  state->pcnt_value = cfg.start_value;
  state->last_count = 0;
  state->is_counting = 1;

  // DEBUG: Verify edge configuration with readable enum string
  ESP_LOGI("CNTR_HW", "C%d configured: PCNT_U%d GPIO%d edge=%s dir=%d start=%llu",
           id, pcnt_unit, gpio_pin,
           (cfg.edge_type == COUNTER_EDGE_RISING) ? "RISING" :
           (cfg.edge_type == COUNTER_EDGE_FALLING) ? "FALLING" : "BOTH",
           cfg.direction, cfg.start_value);
  ESP_LOGI("CNTR_HW", "  hw_gpio=%d (pos_edge=%d neg_edge=%d)",
           cfg.hw_gpio, pos_edge, neg_edge);

  return true;
}

/* ============================================================================
 * MAIN LOOP - READ PCNT AND TRACK CHANGES
 * ============================================================================ */

void counter_hw_loop(uint8_t id) {
  // BUG FIX P0.6: PCNT polling now handled by dedicated RTOS task
  // This function kept for compatibility but does nothing
  // All PCNT reading/delta calculation happens in pcnt_poll_task() at 100Hz
  (void)id;  // Suppress unused warning
}

/* ============================================================================
 * RESET
 * ============================================================================ */

void counter_hw_reset(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return;

  CounterConfig cfg;
  if (!counter_config_get(id, &cfg)) return;

  CounterHWState* state = &hw_state[id - 1];
  uint8_t pcnt_unit = counter_to_pcnt[id - 1];

  // FIX P1.1: Disable is_counting flag to prevent polling task from accumulating delta
  // During reset window, polling task will skip this counter (line 63 checks is_counting)
  state->is_counting = 0;

  // Step 1: Pause PCNT counting (hardware gate)
  pcnt_counter_pause((pcnt_unit_t)pcnt_unit);

  // Step 2: Clear PCNT hardware counter
  pcnt_counter_clear((pcnt_unit_t)pcnt_unit);

  // Step 3: Reset software state
  state->pcnt_value = cfg.start_value;
  state->overflow_count = 0;
  state->last_count = 0;  // Sync with hardware (both now 0)

  // Step 4: Resume PCNT hardware counting
  pcnt_counter_resume((pcnt_unit_t)pcnt_unit);

  // Step 5: Re-enable counting for polling task
  state->is_counting = 1;
}


/* ============================================================================
 * VALUE ACCESS
 * ============================================================================ */

uint64_t counter_hw_get_value(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return 0;

  CounterHWState* state = &hw_state[id - 1];
  return state->pcnt_value;
}

void counter_hw_set_value(uint8_t id, uint64_t value) {
  if (id < 1 || id > COUNTER_COUNT) return;

  CounterHWState* state = &hw_state[id - 1];
  state->pcnt_value = value;

  // Also clear PCNT so next loop will read from 0
  uint8_t pcnt_unit = counter_to_pcnt[id - 1];
  pcnt_unit_clear(pcnt_unit);
  state->last_count = 0;
}

uint8_t counter_hw_get_overflow(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return 0;
  // Return 1 if overflow has occurred
  return (hw_state[id - 1].overflow_count > 0) ? 1 : 0;
}

void counter_hw_clear_overflow(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return;
  hw_state[id - 1].overflow_count = 0;
}

/* ============================================================================
 * START/STOP CONTROL (BUG FIX 2.1: Control register bits)
 * ============================================================================ */

void counter_hw_start(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return;

  // BUG FIX P0.2: CRITICAL - Resume PCNT hardware when starting
  hw_state[id - 1].is_counting = 1;

  // Resume PCNT hardware counter
  uint8_t pcnt_unit = counter_to_pcnt[id - 1];
  pcnt_counter_resume((pcnt_unit_t)pcnt_unit);
}

void counter_hw_stop(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return;

  // BUG FIX P0.2: CRITICAL - Stop PCNT hardware, not just software flag
  hw_state[id - 1].is_counting = 0;

  // Stop PCNT hardware counter
  uint8_t pcnt_unit = counter_to_pcnt[id - 1];
  pcnt_counter_pause((pcnt_unit_t)pcnt_unit);
}
