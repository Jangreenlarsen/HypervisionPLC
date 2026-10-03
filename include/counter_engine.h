/**
 * @file counter_engine.h
 * @brief Counter orchestration and state machine (LAYER 5)
 *
 * LAYER 5: Feature Engines - Counter Engine Orchestration
 * Responsibility: State machine, mode dispatching, and prescaler division
 *
 * This file handles:
 * - Counter initialization and configuration
 * - Mode dispatching (SW/SW-ISR/HW)
 * - Register value writing (with prescaler division)
 * - Control register handling (reset, start, stop)
 * - Frequency register updates
 * - Overflow register updates
 *
 * Does NOT handle:
 * - Mode-specific logic (→ counter_sw/isr/hw.h)
 * - Configuration validation (→ counter_config.h)
 * - Frequency calculation (→ counter_frequency.h)
 *
 * UNIFIED PRESCALER STRATEGY (v3.6.0-v3.6.1):
 * - ALL modes count EVERY edge/pulse (no skipping in mode files)
 * - Prescaler division happens ONLY here at output:
 *   - value register = counterValue × scale (full precision)
 *   - raw register = counterValue / prescaler (reduced size)
 *   - frequency register = measured Hz (no prescaler compensation)
 *
 * Context: This is the "glue" between modes and Modbus registers
 * Ensures consistent behavior across HW/SW/SW-ISR modes
 */

#ifndef COUNTER_ENGINE_H
#define COUNTER_ENGINE_H

#include <stdint.h>
#include "types.h"

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

/**
 * @brief Initialize counter engine (all 4 counters to default/disabled)
 */
void counter_engine_init(void);

/**
 * @brief Main counter engine loop (call once per main loop iteration)
 * Dispatches to mode-specific handlers and updates registers
 */
void counter_engine_loop(void);

/**
 * @brief Configure a counter (validation, mode setup, register binding)
 * @param id Counter ID (1-4)
 * @param cfg Configuration to apply
 * @return true if successful
 */
bool counter_engine_configure(uint8_t id, const CounterConfig* cfg);

/**
 * @brief Reset a counter to start value
 * @param id Counter ID (1-4)
 */
void counter_engine_reset(uint8_t id);

/**
 * @brief Reset all counters
 */
void counter_engine_reset_all(void);

/**
 * @brief Handle control register commands (reset, start, stop)
 * @param id Counter ID (1-4)
 * Called by counter_engine_loop when control register has pending commands
 */
void counter_engine_handle_control(uint8_t id);

/**
 * @brief Write counter value to Modbus registers (with prescaler division)
 * Internal function, applies:
 * - value register = counterValue × scale
 * - raw register = counterValue / prescaler
 * - overflow register = overflow flag
 * @param id Counter ID (1-4)
 */
void counter_engine_store_value_to_registers(uint8_t id);

/**
 * @brief Get counter configuration
 * @param id Counter ID (1-4)
 * @param out Output configuration
 * @return true if found
 */
bool counter_engine_get_config(uint8_t id, CounterConfig* out);

/**
 * @brief Get counter value (before prescaler/scale)
 * @param id Counter ID (1-4)
 * @return Raw counter value
 */
uint64_t counter_engine_get_value(uint8_t id);

/**
 * BUG-434: tællerens overflow-flag (0/1) uanset hw_mode — samme kilde som
 * ctrl_reg bit 3. Bruges af DYNAMIC-registre/coils (counter<id>:overflow).
 */
uint8_t counter_engine_get_overflow(uint8_t id);

/**
 * @brief Set counter value (for testing/initialization)
 * @param id Counter ID (1-4)
 * @param value Value to set
 */
void counter_engine_set_value(uint8_t id, uint64_t value);

/**
 * @brief Check if counter is currently locked for atomic write (ISSUE-1 FIX)
 * Used by Modbus FC handlers to respect write-lock during multi-word updates
 * @param id Counter ID (1-4)
 * @return 1 if write-locked, 0 if available
 */
uint8_t counter_engine_is_write_locked(uint8_t id);

#endif // COUNTER_ENGINE_H

