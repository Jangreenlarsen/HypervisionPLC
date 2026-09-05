/**
 * @file analog_driver.cpp
 * @brief Analog I/O hardware abstraction driver implementation (FEAT-034/035/036)
 */

#include "analog_driver.h"
#include "constants.h"

#if defined(ANALOG_IO_ENABLED)

#include "config_struct.h"
#include "wifi_driver.h"
#include "registers.h"
#include <Arduino.h>
#include <math.h>

/* ============================================================================
 * PIN TABLES
 * ============================================================================ */

static const uint8_t AI_V_PINS[4] = { PIN_AI_V1, PIN_AI_V2, PIN_AI_V3, PIN_AI_V4 };
static const uint8_t AI_I_PINS[4] = { PIN_AI_I1, PIN_AI_I2, PIN_AI_I3, PIN_AI_I4 };
static const uint8_t AO_PINS[2]   = { PIN_AO1, PIN_AO2 };

/* Vi1 (index 0, GPIO14) og Vi3 (index 2, GPIO27) er ADC2 — ubrugelige mens
 * WiFi er aktivt. Vi2/Vi4 og alle Ii1-4 er ADC1 (upaavirket af WiFi). */
static const bool AI_V_IS_ADC2[4] = { true, false, true, false };

bool analog_driver_ai_v_is_adc2(uint8_t channel_idx_0based) {
  if (channel_idx_0based >= 4) return false;
  return AI_V_IS_ADC2[channel_idx_0based];
}

/* ============================================================================
 * DEFAULTS (delt mellem config_init_defaults() og schema 19->20 migration)
 * ============================================================================ */

void analog_io_set_defaults(PersistConfig *cfg) {
  if (!cfg) return;

  // Spaendingskanaler (0-10V): fuld ADC-skala (~3300mV @ 11dB atten) => 10,00V.
  // Fornuftigt startgaet, IKKE en garanteret noejagtig kalibrering — boardets
  // praecise delerforhold kendes ikke fra software. Se AnalogInputConfig i
  // types.h for begrundelsen. Brugeren kalibrerer endeligt mod en kendt
  // spaendingskilde via `set analog vi1 scale/offset`.
  const float V_SCALE  = 1000.0f / 3300.0f;
  const float V_OFFSET = 0.0f;

  // Stroemkanaler (4-20mA): 0mV => 4,00mA (loop-nulpunkt), fuld ADC-skala => 20,00mA.
  const float I_SCALE  = (2000.0f - 400.0f) / 3300.0f;
  const float I_OFFSET = 400.0f;

  // AO (DAC, 8-bit 0-255): matcher AO_MODE_VOLTAGE-default (ao1_mode/ao2_mode).
  // 0-1000 (0,00-10,00V) => 0-255 DAC-taelleenheder.
  const float AO_SCALE_VOLT  = 1000.0f / 255.0f;
  const float AO_OFFSET_VOLT = 0.0f;

  for (uint8_t i = 0; i < 4; i++) {
    cfg->analog_ai_v[i].enabled  = false;  // Bruger slaar til naar kanalen er tilsluttet
    cfg->analog_ai_v[i].scale    = V_SCALE;
    cfg->analog_ai_v[i].offset   = V_OFFSET;
    cfg->analog_ai_v[i].raw_reg   = (uint16_t)(i * 2);       // HR 0,2,4,6
    cfg->analog_ai_v[i].value_reg = (uint16_t)(i * 2 + 1);   // HR 1,3,5,7

    cfg->analog_ai_i[i].enabled  = false;
    cfg->analog_ai_i[i].scale    = I_SCALE;
    cfg->analog_ai_i[i].offset   = I_OFFSET;
    cfg->analog_ai_i[i].raw_reg   = (uint16_t)(8 + i * 2);     // HR 8,10,12,14
    cfg->analog_ai_i[i].value_reg = (uint16_t)(8 + i * 2 + 1); // HR 9,11,13,15
  }

  for (uint8_t i = 0; i < 2; i++) {
    cfg->analog_ao[i].enabled  = false;
    cfg->analog_ao[i].scale    = AO_SCALE_VOLT;
    cfg->analog_ao[i].offset   = AO_OFFSET_VOLT;
    cfg->analog_ao[i].value_reg = (uint16_t)(16 + i);  // HR 16,17
  }
}

/* ============================================================================
 * INIT
 * ============================================================================ */

void analog_driver_init(void) {
  // NB: kaldes tidligt i boot (main.cpp, foer register_allocator_init()).
  // Register-allokeringen for AI/AO-kanaler sker derfor IKKE her, men inde
  // i register_allocator_init() selv (register_allocator.cpp) — samme sted
  // som counter/timer-defaults allokeres. register_allocator_init() nulstiller
  // hele allocation_map ved start, saa et allokerings-kald herfra ville blive
  // overskrevet, hvis det laa foer i boot-raekkefoelgen (hvilket det goer).
  for (uint8_t i = 0; i < 4; i++) {
    analogSetPinAttenuation(AI_V_PINS[i], ADC_11db);
    analogSetPinAttenuation(AI_I_PINS[i], ADC_11db);
  }
  // DAC (GPIO25/26) kraever ingen initialisering — dacWrite() er klar til brug.
}

/* ============================================================================
 * POLL INPUTS (AI)
 * ============================================================================ */

void analog_driver_poll_inputs(void) {
  bool wifi_active = wifi_driver_is_connected();

  for (uint8_t i = 0; i < 4; i++) {
    const AnalogInputConfig *cfg = &g_persist_config.analog_ai_v[i];
    if (!cfg->enabled) continue;
    if (wifi_active && AI_V_IS_ADC2[i]) continue;  // ADC2+WiFi: behold sidste gyldige vaerdi

    uint32_t raw_mv = analogReadMilliVolts(AI_V_PINS[i]);
    float value = cfg->offset + cfg->scale * (float)raw_mv;
    registers_set_holding_register(cfg->raw_reg, (uint16_t)raw_mv);
    registers_set_holding_register(cfg->value_reg, (uint16_t)lroundf(value));
  }

  for (uint8_t i = 0; i < 4; i++) {
    const AnalogInputConfig *cfg = &g_persist_config.analog_ai_i[i];
    if (!cfg->enabled) continue;  // Alle Ii1-4 er ADC1 — upaavirket af WiFi

    uint32_t raw_mv = analogReadMilliVolts(AI_I_PINS[i]);
    float value = cfg->offset + cfg->scale * (float)raw_mv;
    registers_set_holding_register(cfg->raw_reg, (uint16_t)raw_mv);
    registers_set_holding_register(cfg->value_reg, (uint16_t)lroundf(value));
  }
}

/* ============================================================================
 * FLUSH OUTPUTS (AO)
 * ============================================================================ */

void analog_driver_flush_outputs(void) {
  for (uint8_t i = 0; i < 2; i++) {
    const AnalogOutputConfig *cfg = &g_persist_config.analog_ao[i];
    if (!cfg->enabled) continue;

    uint16_t setpoint_x100 = registers_get_holding_register(cfg->value_reg);
    float dac_f = (cfg->scale != 0.0f)
                    ? (((float)setpoint_x100 - cfg->offset) / cfg->scale)
                    : 0.0f;
    if (dac_f < 0.0f) dac_f = 0.0f;
    if (dac_f > 255.0f) dac_f = 255.0f;

    dacWrite(AO_PINS[i], (uint8_t)lroundf(dac_f));
  }
}

#else  // !ANALOG_IO_ENABLED — no-op paa boards uden analog I/O

void analog_io_set_defaults(PersistConfig *cfg) { (void)cfg; }
void analog_driver_init(void) {}
void analog_driver_poll_inputs(void) {}
void analog_driver_flush_outputs(void) {}
bool analog_driver_ai_v_is_adc2(uint8_t channel_idx_0based) { (void)channel_idx_0based; return false; }

#endif // ANALOG_IO_ENABLED
