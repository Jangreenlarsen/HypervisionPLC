/**
 * @file st_builtin_counters.cpp
 * @brief ST Counter Implementation (CTU, CTD, CTUD)
 *
 * Implements IEC 61131-3 standard counters with edge detection.
 */

#include "st_builtin_counters.h"

/* ============================================================================
 * CTU - Count Up Counter
 * ============================================================================ */

st_value_t st_builtin_ctu(st_value_t CU, st_value_t RESET, st_value_t PV, st_counter_instance_t* instance) {
  st_value_t result;
  result.bool_val = false;

  if (!instance) {
    return result;  // No storage
  }

  bool current_CU = CU.bool_val;
  bool current_RESET = RESET.bool_val;
  int32_t preset = PV.dint_val;

  // Update preset value
  instance->PV = preset;

  // Handle RESET (high priority)
  if (current_RESET) {
    instance->CV = 0;
    instance->Q = false;
  } else {
    // Detect rising edge on CU input
    bool rising_edge_CU = (current_CU && !instance->last_CU);

    if (rising_edge_CU) {
      // v4.7+: Overflow protection - clamp at INT32_MAX
      if (instance->CV < INT32_MAX) {
        instance->CV++;
      }
    }

    // Check if count reached preset
    if (instance->CV >= preset) {
      instance->Q = true;
    } else {
      instance->Q = false;
    }
  }

  // Update last states
  instance->last_CU = current_CU;
  instance->last_RESET = current_RESET;

  // Return Q output
  result.bool_val = instance->Q;
  return result;
}

/* ============================================================================
 * CTD - Count Down Counter
 * ============================================================================ */

st_value_t st_builtin_ctd(st_value_t CD, st_value_t LOAD, st_value_t PV, st_counter_instance_t* instance) {
  st_value_t result;
  result.bool_val = false;

  if (!instance) {
    return result;  // No storage
  }

  bool current_CD = CD.bool_val;
  bool current_LOAD = LOAD.bool_val;
  int32_t preset = PV.dint_val;

  // Update preset value
  instance->PV = preset;

  // Handle LOAD (high priority)
  if (current_LOAD && !instance->last_LOAD) {
    // Load PV into CV on rising edge of LOAD
    instance->CV = preset;
  }

  // Detect rising edge on CD input
  bool rising_edge_CD = (current_CD && !instance->last_CD);

  if (rising_edge_CD && !current_LOAD) {
    // Decrement count on rising edge (if not loading)
    instance->CV--;
  }

  // Check if count reached zero or below
  if (instance->CV <= 0) {
    instance->Q = true;
    instance->CV = 0;  // Clamp at zero
  } else {
    instance->Q = false;
  }

  // Update last states
  instance->last_CD = current_CD;
  instance->last_LOAD = current_LOAD;

  // Return Q output
  result.bool_val = instance->Q;
  return result;
}

/* ============================================================================
 * CTUD - Count Up/Down Counter
 * ============================================================================ */

st_value_t st_builtin_ctud(st_value_t CU, st_value_t CD, st_value_t RESET, st_value_t LOAD, st_value_t PV, st_counter_instance_t* instance) {
  st_value_t result;
  result.bool_val = false;

  if (!instance) {
    return result;  // No storage
  }

  bool current_CU = CU.bool_val;
  bool current_CD = CD.bool_val;
  bool current_RESET = RESET.bool_val;
  bool current_LOAD = LOAD.bool_val;
  int32_t preset = PV.dint_val;

  // Update preset value
  instance->PV = preset;

  // Handle RESET (highest priority)
  if (current_RESET) {
    instance->CV = 0;
    instance->QU = false;
    instance->QD = true;   // At zero
  }
  // Handle LOAD (second priority)
  else if (current_LOAD && !instance->last_LOAD) {
    // Load PV into CV on rising edge of LOAD
    instance->CV = preset;
  }
  else {
    // Detect rising edges
    bool rising_edge_CU = (current_CU && !instance->last_CU);
    bool rising_edge_CD = (current_CD && !instance->last_CD);

    // Count up on CU rising edge
    if (rising_edge_CU) {
      // v4.7+: Overflow protection - clamp at INT32_MAX
      if (instance->CV < INT32_MAX) {
        instance->CV++;
      }
    }

    // Count down on CD rising edge
    if (rising_edge_CD) {
      instance->CV--;  // Underflow protection already exists below (line 164)
    }
  }

  // Update outputs
  instance->QU = (instance->CV >= preset);  // Up done
  instance->QD = (instance->CV <= 0);       // Down done

  // Clamp CV at zero (optional)
  if (instance->CV < 0) {
    instance->CV = 0;
  }

  // Update last states
  instance->last_CU = current_CU;
  instance->last_CD = current_CD;
  instance->last_RESET = current_RESET;
  instance->last_LOAD = current_LOAD;

  // Return QU as primary output
  result.bool_val = instance->QU;
  return result;
}

/* ============================================================================
 * HELPER FUNCTIONS
 * ============================================================================ */

int32_t st_counter_get_cv(st_counter_instance_t* instance) {
  if (!instance) return 0;
  return instance->CV;
}
