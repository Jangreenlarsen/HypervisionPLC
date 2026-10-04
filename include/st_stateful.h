// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file st_stateful.h
 * @brief Stateful Storage for ST Function Blocks (Timers, Edges, Counters)
 *
 * Provides persistent state storage between ST program execution cycles
 * for stateful functions like TON/TOF/TP, R_TRIG/F_TRIG, CTU/CTD.
 *
 * Design:
 * - Each stateful function instance gets a unique storage slot
 * - Storage persists across program cycles (execution to execution)
 * - Compiler allocates instance IDs at compile-time
 * - VM passes instance pointer to builtin functions
 *
 * Memory Usage:
 * - Max 16 timer instances per program (TON/TOF/TP)
 * - Max 16 edge instances per program (R_TRIG/F_TRIG)
 * - Max 16 counter instances per program (CTU/CTD/CTUD)
 * - Total: ~1-2 KB per ST program
 */

#ifndef ST_STATEFUL_H
#define ST_STATEFUL_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * CONFIGURATION CONSTANTS
 * ============================================================================ */

#define ST_MAX_TIMER_INSTANCES   8    // Max TON/TOF/TP instances per program
#define ST_MAX_EDGE_INSTANCES    8    // Max R_TRIG/F_TRIG instances per program
#define ST_MAX_COUNTER_INSTANCES 8    // Max CTU/CTD/CTUD instances per program
#define ST_MAX_LATCH_INSTANCES   8    // Max SR/RS latch instances per program

/* ============================================================================
 * TIMER INSTANCE (TON/TOF/TP)
 * ============================================================================ */

/**
 * @brief Timer type enumeration
 */
typedef enum {
  ST_TIMER_TON = 0,  // On-Delay Timer
  ST_TIMER_TOF = 1,  // Off-Delay Timer
  ST_TIMER_TP  = 2   // Pulse Timer
} st_timer_type_t;

/**
 * @brief Timer instance state
 *
 * Stores state for a single TON/TOF/TP timer instance.
 * Each timer tracks:
 * - Previous input state (for edge detection)
 * - Start timestamp (when timer was triggered)
 * - Timer parameters (preset time)
 * - Current output state
 * - Elapsed time
 */
typedef struct {
  st_timer_type_t type;  // Timer type (TON/TOF/TP)
  bool last_IN;          // Previous input state
  uint32_t start_time;   // Timer start timestamp (millis)
  uint32_t PT;           // Preset time (milliseconds)
  bool Q;                // Output state (timer active)
  uint32_t ET;           // Elapsed time (milliseconds)
  bool running;          // Timer currently running
} st_timer_instance_t;

/* ============================================================================
 * EDGE DETECTOR INSTANCE (R_TRIG/F_TRIG)
 * ============================================================================ */

/**
 * @brief Edge detection type
 */
typedef enum {
  ST_EDGE_RISING  = 0,  // R_TRIG (0→1)
  ST_EDGE_FALLING = 1   // F_TRIG (1→0)
} st_edge_type_t;

/**
 * @brief Edge detector instance state
 *
 * Stores state for a single R_TRIG or F_TRIG instance.
 * Tracks previous signal state to detect edges.
 */
typedef struct {
  st_edge_type_t type;  // Edge type (rising or falling)
  bool last_state;      // Previous signal state
} st_edge_instance_t;

/* ============================================================================
 * COUNTER INSTANCE (CTU/CTD/CTUD)
 * ============================================================================ */

/**
 * @brief Counter type enumeration
 */
typedef enum {
  ST_COUNTER_CTU  = 0,  // Count Up
  ST_COUNTER_CTD  = 1,  // Count Down
  ST_COUNTER_CTUD = 2   // Count Up/Down
} st_counter_type_t;

/**
 * @brief Counter instance state
 *
 * Stores state for a single CTU/CTD/CTUD counter instance.
 * Tracks:
 * - Current count value
 * - Previous input states (for edge detection)
 * - Preset value (limit)
 * - Output flags
 */
typedef struct {
  st_counter_type_t type;  // Counter type (CTU/CTD/CTUD)
  int32_t CV;              // Current count value
  int32_t PV;              // Preset value (limit)
  bool last_CU;            // Previous count-up input (for CTU/CTUD)
  bool last_CD;            // Previous count-down input (for CTUD only)
  bool last_RESET;         // Previous reset input
  bool last_LOAD;          // Previous load input (for CTD/CTUD)
  bool Q;                  // Output: TRUE when CV >= PV (CTU) or CV <= 0 (CTD)
  bool QU;                 // Output: Count-up done (CTUD only)
  bool QD;                 // Output: Count-down done (CTUD only)
} st_counter_instance_t;

/* ============================================================================
 * LATCH INSTANCE (SR/RS)
 * ============================================================================ */

/**
 * @brief Latch type enumeration
 */
typedef enum {
  ST_LATCH_SR = 0,  // Set-Reset (Reset priority)
  ST_LATCH_RS = 1   // Reset-Set (Set priority)
} st_latch_type_t;

/**
 * @brief Latch instance state
 *
 * Stores state for a single SR or RS bistable latch instance.
 *
 * SR (Set-Reset): Reset input has priority
 * - If R=1, output Q=0 (regardless of S)
 * - If R=0 and S=1, output Q=1
 * - If both R=0 and S=0, output holds previous state
 *
 * RS (Reset-Set): Set input has priority
 * - If S=1, output Q=1 (regardless of R)
 * - If S=0 and R=1, output Q=0
 * - If both S=0 and R=0, output holds previous state
 *
 * IEC 61131-3 Standard Function Blocks.
 */
typedef struct {
  st_latch_type_t type;  // Latch type (SR or RS)
  bool Q;                // Output state (latched value)
} st_latch_instance_t;

/* ============================================================================
 * SIGNAL PROCESSING INSTANCES (v4.8+)
 * ============================================================================ */

/**
 * @brief Hysteresis instance state
 *
 * Stores state for HYSTERESIS Schmitt trigger.
 * Tracks output state to provide hysteresis behavior.
 */
typedef struct {
  bool Q;  // Output state (holds value in dead zone)
} st_hysteresis_instance_t;

/**
 * @brief Blink generator instance state
 *
 * Stores state for BLINK periodic pulse generator.
 * Tracks state machine state and timing.
 */
typedef struct {
  bool Q;           // Current output state
  uint8_t state;    // State machine state (0=IDLE, 1=ON, 2=OFF)
  uint32_t timer;   // Timestamp in milliseconds
} st_blink_instance_t;

/**
 * @brief Low-pass filter instance state
 *
 * Stores state for FILTER first-order low-pass filter.
 * Tracks previous output for exponential moving average.
 */
typedef struct {
  float out_prev;  // Previous output value
} st_filter_instance_t;

#define ST_MAX_HYSTERESIS_INSTANCES 8  // Max HYSTERESIS instances per program
#define ST_MAX_BLINK_INSTANCES      8  // Max BLINK instances per program
#define ST_MAX_FILTER_INSTANCES     8  // Max FILTER instances per program

/* ============================================================================
 * STATEFUL STORAGE CONTAINER
 * ============================================================================ */

/**
 * @brief Complete stateful storage for one ST program
 *
 * Holds all stateful instances (timers, edges, counters, latches, signal) for a single
 * ST Logic program. Allocated per-program and persists across cycles.
 *
 * Memory: ~540 bytes per program (v4.8+ with signal processing)
 * - Timers: 8 × ~24 bytes = 192 bytes
 * - Edges: 8 × 8 bytes = 64 bytes
 * - Counters: 8 × ~20 bytes = 160 bytes
 * - Latches: 8 × 4 bytes = 32 bytes (v4.7.3)
 * - Hysteresis: 8 × 1 byte = 8 bytes (v4.8)
 * - Blink: 8 × 6 bytes = 48 bytes (v4.8)
 * - Filter: 8 × 4 bytes = 32 bytes (v4.8)
 * - Total: ~540 bytes
 */
typedef struct {
  // Timer instances (TON/TOF/TP)
  st_timer_instance_t timers[ST_MAX_TIMER_INSTANCES];
  uint8_t timer_count;  // Number of allocated timer instances

  // Edge detector instances (R_TRIG/F_TRIG)
  st_edge_instance_t edges[ST_MAX_EDGE_INSTANCES];
  uint8_t edge_count;   // Number of allocated edge instances

  // Counter instances (CTU/CTD/CTUD)
  st_counter_instance_t counters[ST_MAX_COUNTER_INSTANCES];
  uint8_t counter_count;  // Number of allocated counter instances

  // Latch instances (SR/RS) - v4.7.3
  st_latch_instance_t latches[ST_MAX_LATCH_INSTANCES];
  uint8_t latch_count;  // Number of allocated latch instances

  // Signal processing instances - v4.8
  st_hysteresis_instance_t hysteresis[ST_MAX_HYSTERESIS_INSTANCES];
  uint8_t hysteresis_count;

  st_blink_instance_t blinks[ST_MAX_BLINK_INSTANCES];
  uint8_t blink_count;

  st_filter_instance_t filters[ST_MAX_FILTER_INSTANCES];
  uint8_t filter_count;

  // Execution cycle time (v4.8.1 - BUG-153 fix)
  uint32_t cycle_time_ms;  // Actual execution interval from engine state

  // Initialization flag
  bool initialized;
} st_stateful_storage_t;

/* ============================================================================
 * STORAGE MANAGEMENT FUNCTIONS
 * ============================================================================ */

/**
 * @brief Initialize stateful storage
 *
 * Clears all instances and resets counters to zero.
 * Must be called once when program is loaded.
 *
 * @param storage Pointer to storage structure
 */
void st_stateful_init(st_stateful_storage_t* storage);

/**
 * BUG-432: opret stateful storage med antal instanser af ALLE 7 typer
 * (edge, timer, counter, latch, hysteresis, blink, filter). Bruges af
 * compileren (begge stier) og af bytecode-cachen ved opstart.
 * @return ny storage, eller NULL hvis alle antal er 0 eller malloc fejler
 */
st_stateful_storage_t* st_stateful_create(const uint8_t counts[7]);

/**
 * @brief Reset all stateful instances
 *
 * Resets all timers, edges, and counters to initial state.
 * Used when program is stopped or reloaded.
 *
 * @param storage Pointer to storage structure
 */
void st_stateful_reset(st_stateful_storage_t* storage);

/**
 * @brief Allocate a new timer instance
 *
 * Returns pointer to next available timer slot, or NULL if full.
 *
 * @param storage Pointer to storage structure
 * @param type Timer type (TON/TOF/TP)
 * @return Pointer to allocated timer instance, or NULL if no slots available
 */
st_timer_instance_t* st_stateful_alloc_timer(st_stateful_storage_t* storage, st_timer_type_t type);

/**
 * @brief Allocate a new edge detector instance
 *
 * Returns pointer to next available edge slot, or NULL if full.
 *
 * @param storage Pointer to storage structure
 * @param type Edge type (rising/falling)
 * @return Pointer to allocated edge instance, or NULL if no slots available
 */
st_edge_instance_t* st_stateful_alloc_edge(st_stateful_storage_t* storage, st_edge_type_t type);

/**
 * @brief Allocate a new counter instance
 *
 * Returns pointer to next available counter slot, or NULL if full.
 *
 * @param storage Pointer to storage structure
 * @param type Counter type (CTU/CTD/CTUD)
 * @return Pointer to allocated counter instance, or NULL if no slots available
 */
st_counter_instance_t* st_stateful_alloc_counter(st_stateful_storage_t* storage, st_counter_type_t type);

/**
 * @brief Get timer instance by ID
 *
 * @param storage Pointer to storage structure
 * @param instance_id Timer instance ID (0-15)
 * @return Pointer to timer instance, or NULL if invalid ID
 */
st_timer_instance_t* st_stateful_get_timer(st_stateful_storage_t* storage, uint8_t instance_id);

/**
 * @brief Get edge instance by ID
 *
 * @param storage Pointer to storage structure
 * @param instance_id Edge instance ID (0-15)
 * @return Pointer to edge instance, or NULL if invalid ID
 */
st_edge_instance_t* st_stateful_get_edge(st_stateful_storage_t* storage, uint8_t instance_id);

/**
 * @brief Get counter instance by ID
 *
 * @param storage Pointer to storage structure
 * @param instance_id Counter instance ID (0-15)
 * @return Pointer to counter instance, or NULL if invalid ID
 */
st_counter_instance_t* st_stateful_get_counter(st_stateful_storage_t* storage, uint8_t instance_id);

/**
 * @brief Allocate a new latch instance
 *
 * Returns pointer to next available latch slot, or NULL if full.
 *
 * @param storage Pointer to storage structure
 * @param type Latch type (SR or RS)
 * @return Pointer to allocated latch instance, or NULL if no slots available
 */
st_latch_instance_t* st_stateful_alloc_latch(st_stateful_storage_t* storage, st_latch_type_t type);

/**
 * @brief Get latch instance by ID
 *
 * @param storage Pointer to storage structure
 * @param instance_id Latch instance ID (0-7)
 * @return Pointer to latch instance, or NULL if invalid ID
 */
st_latch_instance_t* st_stateful_get_latch(st_stateful_storage_t* storage, uint8_t instance_id);

/**
 * @brief Allocate a new hysteresis instance (v4.8)
 *
 * @param storage Pointer to storage structure
 * @return Pointer to allocated hysteresis instance, or NULL if no slots available
 */
st_hysteresis_instance_t* st_stateful_alloc_hysteresis(st_stateful_storage_t* storage);

/**
 * @brief Get hysteresis instance by ID (v4.8)
 *
 * @param storage Pointer to storage structure
 * @param instance_id Hysteresis instance ID (0-7)
 * @return Pointer to hysteresis instance, or NULL if invalid ID
 */
st_hysteresis_instance_t* st_stateful_get_hysteresis(st_stateful_storage_t* storage, uint8_t instance_id);

/**
 * @brief Allocate a new blink instance (v4.8)
 *
 * @param storage Pointer to storage structure
 * @return Pointer to allocated blink instance, or NULL if no slots available
 */
st_blink_instance_t* st_stateful_alloc_blink(st_stateful_storage_t* storage);

/**
 * @brief Get blink instance by ID (v4.8)
 *
 * @param storage Pointer to storage structure
 * @param instance_id Blink instance ID (0-7)
 * @return Pointer to blink instance, or NULL if invalid ID
 */
st_blink_instance_t* st_stateful_get_blink(st_stateful_storage_t* storage, uint8_t instance_id);

/**
 * @brief Allocate a new filter instance (v4.8)
 *
 * @param storage Pointer to storage structure
 * @return Pointer to allocated filter instance, or NULL if no slots available
 */
st_filter_instance_t* st_stateful_alloc_filter(st_stateful_storage_t* storage);

/**
 * @brief Get filter instance by ID (v4.8)
 *
 * @param storage Pointer to storage structure
 * @param instance_id Filter instance ID (0-7)
 * @return Pointer to filter instance, or NULL if invalid ID
 */
st_filter_instance_t* st_stateful_get_filter(st_stateful_storage_t* storage, uint8_t instance_id);

#endif // ST_STATEFUL_H
