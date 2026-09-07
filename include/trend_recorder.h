/**
 * @file trend_recorder.h
 * @brief Trend Recorder — periodic sampling of arbitrary registers for
 * commissioning/debugging, with CSV export (FEAT-099)
 *
 * Unlike system_log.h's regchange log (event-driven, fires only on an
 * actual WRITE), this samples the CURRENT value of a user-chosen set of
 * registers on a fixed interval, regardless of whether anything changed —
 * a genuine time-series recorder, not an audit trail.
 *
 * RAM-only ring buffer (PSRAM-first, same allocation pattern as
 * mb_activity_log.h), not persisted to NVS/flash — config and data both
 * reset on reboot, matching the tool's "record a session, export, maybe
 * start over" nature.
 */

#ifndef TREND_RECORDER_H
#define TREND_RECORDER_H

#include <Arduino.h>

#define TREND_MAX_POINTS    8    // Max simultaneous watched registers
#define TREND_MAX_SAMPLES   720  // Ring buffer depth (~26KB in PSRAM at 8 points — trivial, see mb_activity_log.h)
#define TREND_INTERVAL_MIN_MS 500
#define TREND_INTERVAL_MAX_MS 60000

typedef enum {
  TREND_REG_HR   = 0,  // Holding register
  TREND_REG_IR   = 1,  // Input register
  TREND_REG_COIL = 2,  // Coil
  TREND_REG_DI   = 3   // Discrete input
} trend_reg_type_t;

typedef struct {
  uint8_t  reg_type;   // trend_reg_type_t
  uint16_t addr;
} trend_point_t;

typedef struct {
  uint32_t timestamp_ms;
  int32_t  values[TREND_MAX_POINTS];  // only [0..point_count-1] are meaningful
} trend_sample_t;

/**
 * @brief Allocate the ring buffer (called once at boot, mirrors mb_activity_log_init())
 */
void trend_recorder_init(void);

/**
 * @brief Configure the watch list + sample interval. Stops recording and
 * clears any existing samples — a reconfiguration is always a fresh start.
 * @param points Array of up to TREND_MAX_POINTS entries
 * @param count Number of valid entries in points (0 = clear watch list)
 * @param interval_ms Sample interval, clamped to [TREND_INTERVAL_MIN_MS, TREND_INTERVAL_MAX_MS]
 * @return false if count > TREND_MAX_POINTS
 */
bool trend_recorder_configure(const trend_point_t *points, uint8_t count, uint16_t interval_ms);

/**
 * @brief Start/stop sampling without touching the configured watch list or
 * already-collected samples (mirrors mb_activity_log_set_enabled()).
 */
void trend_recorder_set_recording(bool enabled);
bool trend_recorder_is_recording(void);

/**
 * @brief Clear all collected samples without changing the watch list/config.
 */
void trend_recorder_clear(void);

/**
 * @brief Called from the main loop() every tick — takes a new sample if
 * recording is active and the configured interval has elapsed.
 */
void trend_recorder_loop(void);

/**
 * @brief Current configuration (for REST GET /api/trend/config).
 */
uint8_t trend_recorder_get_points(trend_point_t *out, uint8_t max_out);
uint16_t trend_recorder_get_interval_ms(void);

/**
 * @brief Number of valid samples currently held (0..TREND_MAX_SAMPLES).
 */
uint16_t trend_recorder_count(void);

/**
 * @brief Read one sample, oldest-first (idx 0 = oldest, count()-1 = newest).
 * @return true if idx valid and *out was filled
 */
bool trend_recorder_get(uint16_t idx, trend_sample_t *out);

#endif // TREND_RECORDER_H
