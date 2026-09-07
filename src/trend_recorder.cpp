/**
 * @file trend_recorder.cpp
 * @brief Trend Recorder implementation (FEAT-099)
 */

#include "trend_recorder.h"
#include "registers.h"
#include <freertos/FreeRTOS.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <stdlib.h>

// Same PSRAM-first allocation pattern as mb_activity_log.cpp — 720 samples
// x 8 points x ~36 bytes = ~26KB, trivial in PSRAM, falls back to regular
// heap (or disables the recorder entirely, sample_capacity=0) if PSRAM is
// unavailable, rather than risking a NULL-dereference.
static trend_sample_t *sample_buf = NULL;
static uint16_t sample_capacity = 0;
static uint16_t sample_head = 0;   // Next write position
static uint16_t sample_count = 0;  // Total samples held (max sample_capacity)

static trend_point_t g_points[TREND_MAX_POINTS];
static uint8_t g_point_count = 0;
static uint16_t g_interval_ms = 5000;
static volatile bool g_recording = false;
static uint32_t g_last_sample_ms = 0;

// Reader (REST handler, httpd task) and writer (main loop(), trend_recorder_loop())
// run on potentially different cores — same spinlock-per-ringbuffer pattern
// as mb_activity_log.cpp's activity_log_spinlock.
static portMUX_TYPE trend_spinlock = portMUX_INITIALIZER_UNLOCKED;

void trend_recorder_init(void) {
  if (!sample_buf) {
    size_t bytes = (size_t)TREND_MAX_SAMPLES * sizeof(trend_sample_t);
    sample_buf = (trend_sample_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (!sample_buf) {
      sample_buf = (trend_sample_t *)malloc(bytes);
    }
    sample_capacity = sample_buf ? TREND_MAX_SAMPLES : 0;
  }

  portENTER_CRITICAL(&trend_spinlock);
  if (sample_buf) {
    memset(sample_buf, 0, (size_t)sample_capacity * sizeof(trend_sample_t));
  }
  sample_head = 0;
  sample_count = 0;
  portEXIT_CRITICAL(&trend_spinlock);

  g_point_count = 0;
  g_interval_ms = 5000;
  g_recording = false;
}

bool trend_recorder_configure(const trend_point_t *points, uint8_t count, uint16_t interval_ms) {
  if (count > TREND_MAX_POINTS) return false;

  g_recording = false;

  if (interval_ms < TREND_INTERVAL_MIN_MS) interval_ms = TREND_INTERVAL_MIN_MS;
  if (interval_ms > TREND_INTERVAL_MAX_MS) interval_ms = TREND_INTERVAL_MAX_MS;
  g_interval_ms = interval_ms;

  g_point_count = count;
  for (uint8_t i = 0; i < count; i++) {
    g_points[i] = points[i];
  }

  portENTER_CRITICAL(&trend_spinlock);
  sample_head = 0;
  sample_count = 0;
  portEXIT_CRITICAL(&trend_spinlock);

  return true;
}

void trend_recorder_set_recording(bool enabled) {
  if (enabled) {
    g_last_sample_ms = millis();  // BUG-guard: don't sample instantly on start, wait one full interval
  }
  g_recording = enabled;
}

bool trend_recorder_is_recording(void) {
  return g_recording;
}

void trend_recorder_clear(void) {
  portENTER_CRITICAL(&trend_spinlock);
  sample_head = 0;
  sample_count = 0;
  portEXIT_CRITICAL(&trend_spinlock);
}

static int32_t trend_read_point(const trend_point_t *p) {
  switch (p->reg_type) {
    case TREND_REG_HR:   return (int32_t)registers_get_holding_register(p->addr);
    case TREND_REG_IR:   return (int32_t)registers_get_input_register(p->addr);
    case TREND_REG_COIL: return (int32_t)registers_get_coil(p->addr);
    case TREND_REG_DI:   return (int32_t)registers_get_discrete_input(p->addr);
    default:             return 0;
  }
}

void trend_recorder_loop(void) {
  if (!g_recording || !sample_buf || g_point_count == 0) return;

  uint32_t now = millis();
  if (now - g_last_sample_ms < g_interval_ms) return;
  g_last_sample_ms = now;

  trend_sample_t s;
  s.timestamp_ms = now;
  for (uint8_t i = 0; i < g_point_count; i++) {
    s.values[i] = trend_read_point(&g_points[i]);
  }

  portENTER_CRITICAL(&trend_spinlock);
  sample_buf[sample_head] = s;
  sample_head = (sample_head + 1) % sample_capacity;
  if (sample_count < sample_capacity) {
    sample_count++;
  }
  portEXIT_CRITICAL(&trend_spinlock);
}

uint8_t trend_recorder_get_points(trend_point_t *out, uint8_t max_out) {
  uint8_t n = (g_point_count < max_out) ? g_point_count : max_out;
  for (uint8_t i = 0; i < n; i++) {
    out[i] = g_points[i];
  }
  return n;
}

uint16_t trend_recorder_get_interval_ms(void) {
  return g_interval_ms;
}

uint16_t trend_recorder_count(void) {
  return sample_count;
}

bool trend_recorder_get(uint16_t idx, trend_sample_t *out) {
  if (!out || !sample_buf) return false;
  portENTER_CRITICAL(&trend_spinlock);
  if (idx >= sample_count) {
    portEXIT_CRITICAL(&trend_spinlock);
    return false;
  }
  // idx 0 = oldest sample currently held
  uint16_t real_idx = (uint16_t)((sample_head - sample_count + idx + sample_capacity) % sample_capacity);
  *out = sample_buf[real_idx];
  portEXIT_CRITICAL(&trend_spinlock);
  return true;
}
