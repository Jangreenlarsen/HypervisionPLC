// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file encoder_sw.h
 * @brief FEAT-470: quadrature-dekoder for drejeenkodere på skifteregister-DI
 *
 * ST kører kun hvert ~10 ms, så en encoder dekodet i ST taber trin ved hurtig
 * drejning. Dekodningen sker derfor i gpio_driver's scan-task (hvert 2 ms,
 * samme sted som FEAT-438's hurtige tællere).
 *
 * Kontrakt (bevidst den samme som et fremtidigt encoder-expansion board vil
 * have i et holding-register): en FRITLØBENDE 16-bit positionstæller i rå
 * overgange (4 pr. hak på en KY-040), der wrapper ved ±32768. ST'en regner
 * selv forskel og hak ud, så kun kilden skifter:
 *   pos := ENC_POS(2, 1);                      (* lokal DI *)
 *   pos := MBX_READ_HOLDING(1, 'A', 20, 0);    (* evt. expansion board *)
 *
 * En encoder registreres ved første ENC_POS-kald (ingen config/NVS).
 */

#ifndef ENCODER_SW_H
#define ENCODER_SW_H

#include <stdint.h>

#define ENCODER_SW_MAX  2   // samtidige encodere

/**
 * Ren quadrature-overgang (ingen hardware/RTOS — kan genbruges i andre
 * firmwares, fx et expansion board). ab = (clk << 1) | dt.
 * @return +1 når CLK fører DT, -1 modsat, 0 ved ingen/ugyldig overgang
 *         (begge signaler skiftet samtidig).
 */
static inline int8_t encoder_quad_step(uint8_t last_ab, uint8_t ab) {
  static const int8_t table[16] = { 0, -1, +1,  0,
                                   +1,  0,  0, -1,
                                   -1,  0,  0, +1,
                                    0, +1, -1,  0 };
  return table[((last_ab & 3) << 2) | (ab & 3)];
}

/**
 * Encoderens position i rå overgange (wrapper som 16-bit).
 * @param clk_di CLK-indgang, DI-nummer 1-8
 * @param dt_di  DT-indgang, DI-nummer 1-8 (≠ clk_di)
 * @return position; ugyldige argumenter eller ingen ledig plads → 0
 */
int16_t encoder_sw_pos(int16_t clk_di, int16_t dt_di);

/** Kaldes fra scan-tasken med de rå DI-bits (bit 0 = DI1). */
void encoder_sw_fast_scan(uint8_t sr_bits);

#endif // ENCODER_SW_H
