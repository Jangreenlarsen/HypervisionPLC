/**
 * @file st_stateful.cpp
 * @brief Stateful Storage Implementation
 *
 * Manages persistent state for ST function blocks (timers, edges, counters).
 */

#include "st_stateful.h"
#include <string.h>

/* ============================================================================
 * STORAGE INITIALIZATION
 * ============================================================================ */

void st_stateful_init(st_stateful_storage_t* storage) {
  if (!storage) return;

  // Clear all memory
  memset(storage, 0, sizeof(st_stateful_storage_t));

  // Reset counters
  storage->timer_count = 0;
  storage->edge_count = 0;
  storage->counter_count = 0;
  storage->latch_count = 0;  // v4.7.3: SR/RS latches
  storage->hysteresis_count = 0;  // v4.8: Signal processing
  storage->blink_count = 0;
  storage->filter_count = 0;

  // BUG-153 FIX: Default cycle time (will be overridden by engine)
  storage->cycle_time_ms = 10;  // 10ms default (100Hz)

  // Mark as initialized
  storage->initialized = true;
}

st_stateful_storage_t* st_stateful_create(const uint8_t counts[7]) {
  if (!counts) return NULL;
  bool any = false;
  for (int i = 0; i < 7; i++) any |= (counts[i] > 0);
  if (!any) return NULL;
  st_stateful_storage_t *s = (st_stateful_storage_t *)malloc(sizeof(st_stateful_storage_t));
  if (!s) return NULL;
  st_stateful_init(s);
  s->edge_count = counts[0];
  s->timer_count = counts[1];
  s->counter_count = counts[2];
  s->latch_count = counts[3];       // BUG-432: blev aldrig sat — SR/RS fejlede altid
  s->hysteresis_count = counts[4];
  s->blink_count = counts[5];
  s->filter_count = counts[6];
  return s;
}

void st_stateful_reset(st_stateful_storage_t* storage) {
  if (!storage || !storage->initialized) return;

  // Reset all timers
  for (uint8_t i = 0; i < storage->timer_count; i++) {
    storage->timers[i].Q = false;
    storage->timers[i].ET = 0;
    storage->timers[i].running = false;
    storage->timers[i].last_IN = false;
  }

  // Reset all edge detectors
  for (uint8_t i = 0; i < storage->edge_count; i++) {
    storage->edges[i].last_state = false;
  }

  // Reset all counters
  for (uint8_t i = 0; i < storage->counter_count; i++) {
    storage->counters[i].CV = 0;
    storage->counters[i].Q = false;
    storage->counters[i].QU = false;
    storage->counters[i].QD = false;
    storage->counters[i].last_CU = false;
    storage->counters[i].last_CD = false;
    storage->counters[i].last_RESET = false;
    storage->counters[i].last_LOAD = false;
  }

  // Reset all latches (v4.7.3)
  for (uint8_t i = 0; i < storage->latch_count; i++) {
    storage->latches[i].Q = false;
  }

  // Reset all signal processing instances (v4.8)
  for (uint8_t i = 0; i < storage->hysteresis_count; i++) {
    storage->hysteresis[i].Q = false;
  }

  for (uint8_t i = 0; i < storage->blink_count; i++) {
    storage->blinks[i].Q = false;
    storage->blinks[i].state = 0;  // IDLE
    storage->blinks[i].timer = 0;
  }

  for (uint8_t i = 0; i < storage->filter_count; i++) {
    storage->filters[i].out_prev = 0.0f;
  }
}

/* ============================================================================
 * TIMER ALLOCATION
 * ============================================================================ */

st_timer_instance_t* st_stateful_alloc_timer(st_stateful_storage_t* storage, st_timer_type_t type) {
  if (!storage || !storage->initialized) return NULL;
  if (storage->timer_count >= ST_MAX_TIMER_INSTANCES) return NULL;

  // Get next slot
  st_timer_instance_t* timer = &storage->timers[storage->timer_count];
  storage->timer_count++;

  // Initialize timer
  memset(timer, 0, sizeof(st_timer_instance_t));
  timer->type = type;
  timer->Q = false;
  timer->ET = 0;
  timer->running = false;
  timer->last_IN = false;

  return timer;
}

st_timer_instance_t* st_stateful_get_timer(st_stateful_storage_t* storage, uint8_t instance_id) {
  if (!storage || !storage->initialized) return NULL;
  if (instance_id >= storage->timer_count) return NULL;
  return &storage->timers[instance_id];
}

/* ============================================================================
 * EDGE DETECTOR ALLOCATION
 * ============================================================================ */

st_edge_instance_t* st_stateful_alloc_edge(st_stateful_storage_t* storage, st_edge_type_t type) {
  if (!storage || !storage->initialized) return NULL;
  if (storage->edge_count >= ST_MAX_EDGE_INSTANCES) return NULL;

  // Get next slot
  st_edge_instance_t* edge = &storage->edges[storage->edge_count];
  storage->edge_count++;

  // Initialize edge detector
  memset(edge, 0, sizeof(st_edge_instance_t));
  edge->type = type;
  edge->last_state = false;

  return edge;
}

st_edge_instance_t* st_stateful_get_edge(st_stateful_storage_t* storage, uint8_t instance_id) {
  if (!storage || !storage->initialized) return NULL;
  if (instance_id >= storage->edge_count) return NULL;
  return &storage->edges[instance_id];
}

/* ============================================================================
 * COUNTER ALLOCATION
 * ============================================================================ */

st_counter_instance_t* st_stateful_alloc_counter(st_stateful_storage_t* storage, st_counter_type_t type) {
  if (!storage || !storage->initialized) return NULL;
  if (storage->counter_count >= ST_MAX_COUNTER_INSTANCES) return NULL;

  // Get next slot
  st_counter_instance_t* counter = &storage->counters[storage->counter_count];
  storage->counter_count++;

  // Initialize counter
  memset(counter, 0, sizeof(st_counter_instance_t));
  counter->type = type;
  counter->CV = 0;
  counter->PV = 0;
  counter->Q = false;
  counter->QU = false;
  counter->QD = false;
  counter->last_CU = false;
  counter->last_CD = false;
  counter->last_RESET = false;
  counter->last_LOAD = false;

  return counter;
}

st_counter_instance_t* st_stateful_get_counter(st_stateful_storage_t* storage, uint8_t instance_id) {
  if (!storage || !storage->initialized) return NULL;
  if (instance_id >= storage->counter_count) return NULL;
  return &storage->counters[instance_id];
}

/* ============================================================================
 * LATCH ALLOCATION
 * ============================================================================ */

st_latch_instance_t* st_stateful_alloc_latch(st_stateful_storage_t* storage, st_latch_type_t type) {
  if (!storage || !storage->initialized) return NULL;
  if (storage->latch_count >= ST_MAX_LATCH_INSTANCES) return NULL;

  // Get next slot
  st_latch_instance_t* latch = &storage->latches[storage->latch_count];
  storage->latch_count++;

  // Initialize latch
  memset(latch, 0, sizeof(st_latch_instance_t));
  latch->type = type;
  latch->Q = false;

  return latch;
}

st_latch_instance_t* st_stateful_get_latch(st_stateful_storage_t* storage, uint8_t instance_id) {
  if (!storage || !storage->initialized) return NULL;
  if (instance_id >= storage->latch_count) return NULL;
  return &storage->latches[instance_id];
}

/* ============================================================================
 * SIGNAL PROCESSING ALLOCATION (v4.8)
 * ============================================================================ */

st_hysteresis_instance_t* st_stateful_alloc_hysteresis(st_stateful_storage_t* storage) {
  if (!storage || !storage->initialized) return NULL;
  if (storage->hysteresis_count >= ST_MAX_HYSTERESIS_INSTANCES) return NULL;

  // Get next slot
  st_hysteresis_instance_t* hyst = &storage->hysteresis[storage->hysteresis_count];
  storage->hysteresis_count++;

  // Initialize hysteresis
  memset(hyst, 0, sizeof(st_hysteresis_instance_t));
  hyst->Q = false;

  return hyst;
}

st_hysteresis_instance_t* st_stateful_get_hysteresis(st_stateful_storage_t* storage, uint8_t instance_id) {
  if (!storage || !storage->initialized) return NULL;
  if (instance_id >= storage->hysteresis_count) return NULL;
  return &storage->hysteresis[instance_id];
}

st_blink_instance_t* st_stateful_alloc_blink(st_stateful_storage_t* storage) {
  if (!storage || !storage->initialized) return NULL;
  if (storage->blink_count >= ST_MAX_BLINK_INSTANCES) return NULL;

  // Get next slot
  st_blink_instance_t* blink = &storage->blinks[storage->blink_count];
  storage->blink_count++;

  // Initialize blink
  memset(blink, 0, sizeof(st_blink_instance_t));
  blink->Q = false;
  blink->state = 0;  // IDLE
  blink->timer = 0;

  return blink;
}

st_blink_instance_t* st_stateful_get_blink(st_stateful_storage_t* storage, uint8_t instance_id) {
  if (!storage || !storage->initialized) return NULL;
  if (instance_id >= storage->blink_count) return NULL;
  return &storage->blinks[instance_id];
}

st_filter_instance_t* st_stateful_alloc_filter(st_stateful_storage_t* storage) {
  if (!storage || !storage->initialized) return NULL;
  if (storage->filter_count >= ST_MAX_FILTER_INSTANCES) return NULL;

  // Get next slot
  st_filter_instance_t* filter = &storage->filters[storage->filter_count];
  storage->filter_count++;

  // Initialize filter
  memset(filter, 0, sizeof(st_filter_instance_t));
  filter->out_prev = 0.0f;

  return filter;
}

st_filter_instance_t* st_stateful_get_filter(st_stateful_storage_t* storage, uint8_t instance_id) {
  if (!storage || !storage->initialized) return NULL;
  if (instance_id >= storage->filter_count) return NULL;
  return &storage->filters[instance_id];
}
