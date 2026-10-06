// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file encoder_sw.cpp
 * @brief FEAT-470: quadrature-dekoder for drejeenkodere (LAYER 5)
 *
 * Responsibility:
 * - Dekode CLK/DT-par fra skifteregistrets rå DI-bits i scan-tasken (2 ms)
 * - Føre en fritløbende positionstæller pr. encoder; ST læser den med ENC_POS
 *
 * encoder_quad_step() afviser ugyldige spring (begge signaler skifter
 * samtidig), og kontaktprel på ét signal giver +1/-1 der ophæver hinanden.
 */

#include "encoder_sw.h"

#include <freertos/FreeRTOS.h>

typedef struct {
  uint8_t active;
  uint8_t clk_bit;
  uint8_t dt_bit;
  uint8_t need_init;   // første scanning sætter last_ab uden at tælle
  uint8_t last_ab;     // (clk << 1) | dt
  uint16_t pos;        // rå overgange, wrapper
} encoder_sw_t;

static encoder_sw_t g_enc[ENCODER_SW_MAX];
static portMUX_TYPE g_enc_mux = portMUX_INITIALIZER_UNLOCKED;

void encoder_sw_fast_scan(uint8_t sr_bits) {
  portENTER_CRITICAL(&g_enc_mux);
  for (uint8_t i = 0; i < ENCODER_SW_MAX; i++) {
    encoder_sw_t *e = &g_enc[i];
    if (!e->active) continue;
    uint8_t ab = (uint8_t)((((sr_bits >> e->clk_bit) & 1) << 1) | ((sr_bits >> e->dt_bit) & 1));
    if (e->need_init) {
      e->last_ab = ab;
      e->need_init = 0;
      continue;
    }
    if (ab != e->last_ab) {
      e->pos = (uint16_t)(e->pos + encoder_quad_step(e->last_ab, ab));
      e->last_ab = ab;
    }
  }
  portEXIT_CRITICAL(&g_enc_mux);
}

int16_t encoder_sw_pos(int16_t clk_di, int16_t dt_di) {
  if (clk_di < 1 || clk_di > 8 || dt_di < 1 || dt_di > 8 || clk_di == dt_di) return 0;
  const uint8_t cb = (uint8_t)(clk_di - 1);
  const uint8_t db = (uint8_t)(dt_di - 1);

  uint16_t pos = 0;
  portENTER_CRITICAL(&g_enc_mux);
  encoder_sw_t *e = NULL;
  encoder_sw_t *free_slot = NULL;
  for (uint8_t i = 0; i < ENCODER_SW_MAX; i++) {
    if (g_enc[i].active && g_enc[i].clk_bit == cb && g_enc[i].dt_bit == db) { e = &g_enc[i]; break; }
    if (!g_enc[i].active && free_slot == NULL) free_slot = &g_enc[i];
  }
  if (e == NULL && free_slot != NULL) {
    e = free_slot;
    e->clk_bit = cb;
    e->dt_bit = db;
    e->pos = 0;
    e->need_init = 1;
    e->active = 1;
  }
  if (e != NULL) pos = e->pos;
  portEXIT_CRITICAL(&g_enc_mux);
  return (int16_t)pos;
}
