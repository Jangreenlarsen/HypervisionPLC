// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file encoder_quad.h
 * @brief FEAT-470: quadrature-overgang for drejeenkodere (ren C, ingen RTOS)
 *
 * Bruges af tællerens encoder-tilstand (COUNTER_HW_ENCODER, counter_sw.cpp)
 * og kan genbruges uændret i et encoder-expansion boards firmware, så begge
 * leverer samme kontrakt: en positionstæller i rå overgange i et register.
 */

#ifndef ENCODER_QUAD_H
#define ENCODER_QUAD_H

#include <stdint.h>

/**
 * ab = (clk << 1) | dt.
 * @return +1 når CLK fører DT, -1 modsat, 0 ved ingen/ugyldig overgang
 *         (begge signaler skiftet samtidig). Kontaktprel på ét signal giver
 *         +1/-1, der ophæver hinanden.
 */
static inline int8_t encoder_quad_step(uint8_t last_ab, uint8_t ab) {
  static const int8_t table[16] = { 0, -1, +1,  0,
                                   +1,  0,  0, -1,
                                   -1,  0,  0, +1,
                                    0, +1, -1,  0 };
  return table[((last_ab & 3) << 2) | (ab & 3)];
}

#endif // ENCODER_QUAD_H
