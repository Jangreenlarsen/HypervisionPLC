// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file pcnt_driver.cpp
 * @brief PCNT driver implementation using ESP32 SDK
 *
 * LAYER 0: Hardware Abstraction Driver
 * Provides pulse counter functionality via ESP32 PCNT peripheral
 */

#include "pcnt_driver.h"
#include <string.h>
#include <driver/pcnt.h>
#include <esp_log.h>

static const char* TAG = "PCNT";

// PCNT configuration state
static uint8_t pcnt_configured[4] = {0};

/* ============================================================================
 * PCNT UNIT INITIALIZATION
 * ============================================================================ */

void pcnt_unit_init(uint8_t unit) {
  if (unit >= 4) return;

  // BUG FIX P0.0: CRITICAL - Don't pause/clear PCNT until AFTER pcnt_unit_config()
  // Calling pause/clear on unconfigured PCNT unit causes "PCNT driver error"
  // Configuration happens later in pcnt_unit_configure(), which will pause/clear/resume

  pcnt_configured[unit] = 0;
  // Do NOT call pcnt_counter_pause/clear here - PCNT not configured yet!
}

/* ============================================================================
 * PCNT UNIT CONFIGURATION
 * ============================================================================ */

void pcnt_unit_configure(uint8_t unit, uint8_t gpio_pin,
                        pcnt_edge_mode_t pos_edge, pcnt_edge_mode_t neg_edge) {
  if (unit >= 4) return;

  // Convert edge modes to PCNT count modes
  // ESP32 PCNT hardware can selectively count rising, falling, or both edges
  // PCNT_COUNT_DIS = ignore this edge (don't count)
  // PCNT_COUNT_INC = increment counter on this edge
  // pos_mode applies to LOW→HIGH transitions (rising edges)
  // neg_mode applies to HIGH→LOW transitions (falling edges)
  pcnt_count_mode_t pos_mode = PCNT_COUNT_DIS;
  pcnt_count_mode_t neg_mode = PCNT_COUNT_DIS;

  // Configure rising edge counting
  if (pos_edge == PCNT_EDGE_RISING) {
    pos_mode = PCNT_COUNT_INC;  // Count on rising edge (LOW→HIGH)
  }

  // Configure falling edge counting
  if (neg_edge == PCNT_EDGE_FALLING) {
    neg_mode = PCNT_COUNT_INC;  // Count on falling edge (HIGH→LOW)
  }

  // DEBUG: Log PCNT configuration (EXPANDED)
  ESP_LOGI(TAG, "Unit%d GPIO%d: pos_edge=%d neg_edge=%d → pos_mode=%d neg_mode=%d",
           unit, gpio_pin, pos_edge, neg_edge, pos_mode, neg_mode);
  ESP_LOGI(TAG, "  PCNT_EDGE_DISABLE=%d, PCNT_EDGE_RISING=%d, PCNT_EDGE_FALLING=%d",
           PCNT_EDGE_DISABLE, PCNT_EDGE_RISING, PCNT_EDGE_FALLING);
  ESP_LOGI(TAG, "  PCNT_COUNT_DIS=%d, PCNT_COUNT_INC=%d",
           PCNT_COUNT_DIS, PCNT_COUNT_INC);

  // BUG-182 FIX: PCNT limits must allow full 16-bit unsigned range (0-65535)
  // PCNT hardware is signed 16-bit, but we want unsigned behavior:
  // - For UP counting: need to count from 0 to 65535 (via signed -32768 to 32767 mapping)
  // - Set h_lim very high to avoid premature wrapping at 32767
  // - Set l_lim very low to allow full negative range (maps to 32768-65535 unsigned)
  //
  // NOTE: ESP32 PCNT is ALWAYS signed! We convert to unsigned in pcnt_unit_get_count()
  // Setting limits to INT16_MIN/MAX allows natural signed overflow at ±32768,
  // which maps to continuous unsigned sequence 0→65535 after uint16_t cast.
  pcnt_config_t pcnt_config = {
    .pulse_gpio_num = gpio_pin,
    .ctrl_gpio_num = PCNT_PIN_NOT_USED,
    .lctrl_mode = PCNT_MODE_KEEP,
    .hctrl_mode = PCNT_MODE_KEEP,
    .pos_mode = pos_mode,
    .neg_mode = neg_mode,
    .counter_h_lim = INT16_MAX,    // 32767 - full positive range
    .counter_l_lim = INT16_MIN,    // -32768 - full negative range
    .unit = (pcnt_unit_t)unit,
    .channel = PCNT_CHANNEL_0,
  };

  pcnt_unit_config(&pcnt_config);

  // BUG-182 FIX: DISABLE watchpoint events at limits (prevent hardware interrupt triggers)
  // We handle overflow in software (counter_hw.cpp), not via hardware events
  pcnt_set_event_value((pcnt_unit_t)unit, PCNT_EVT_H_LIM, INT16_MAX);
  pcnt_set_event_value((pcnt_unit_t)unit, PCNT_EVT_L_LIM, INT16_MIN);
  pcnt_event_disable((pcnt_unit_t)unit, PCNT_EVT_H_LIM);
  pcnt_event_disable((pcnt_unit_t)unit, PCNT_EVT_L_LIM);
  pcnt_event_disable((pcnt_unit_t)unit, PCNT_EVT_ZERO);

  // BUG FIX P0.1: DISABLE filter for high-frequency counting
  // Filter/debounce only for low-frequency (manual button press, noisy environments)
  // At 5 kHz, filter can cause missed pulses - disable completely
  pcnt_filter_disable((pcnt_unit_t)unit);

  // Clear and start counter
  pcnt_counter_pause((pcnt_unit_t)unit);
  pcnt_counter_clear((pcnt_unit_t)unit);
  pcnt_counter_resume((pcnt_unit_t)unit);

  pcnt_configured[unit] = 1;
}

/* ============================================================================
 * PCNT COUNTER OPERATIONS
 * ============================================================================ */

uint32_t pcnt_unit_get_count(uint8_t unit) {
  if (unit >= 4) return 0;

  // BUG-015 FIX: Check if PCNT unit is configured before reading
  if (!pcnt_configured[unit]) {
    ESP_LOGW(TAG, "PCNT unit %d not configured - returning 0", unit);
    return 0;  // Graceful return instead of error
  }

  // BUG-024 EXTENDED FIX: PCNT hardware is 16-bit signed (-32768 to 32767)
  // ESP-IDF API uses int16_t, not uint32_t!
  // We need to handle the 16-bit wrap at ±32768 in counter_hw.cpp
  int16_t count = 0;
  pcnt_get_counter_value((pcnt_unit_t)unit, &count);

  // Convert signed 16-bit to unsigned 16-bit (0-65535 range)
  // This ensures monotonic increase before wrap at 65536
  uint16_t unsigned_count = (uint16_t)count;

  // Return as uint32_t for compatibility (but only uses lower 16 bits)
  return (uint32_t)unsigned_count;
}

void pcnt_unit_clear(uint8_t unit) {
  if (unit >= 4) return;

  // Reset hardware counter
  pcnt_counter_pause((pcnt_unit_t)unit);
  pcnt_counter_clear((pcnt_unit_t)unit);
  pcnt_counter_resume((pcnt_unit_t)unit);
}

void pcnt_unit_set_count(uint8_t unit, uint32_t value) {
  if (unit >= 4) return;

  // ESP32 PCNT can only clear, not set arbitrary values
  // This is a limitation of the hardware
  // We handle absolute values in counter_hw.cpp software layer
  (void)value;  // Unused
}
