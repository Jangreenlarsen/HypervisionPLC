/**
 * @file analog_driver.h
 * @brief Analog I/O hardware abstraction driver (FEAT-034/035/036, ES32D26)
 *
 * LAYER 0: Hardware Abstraction Driver
 *
 * 4x 0-10V spaendingsindgange (Vi1-Vi4), 4x 4-20mA stroemindgange (Ii1-Ii4),
 * 2x analog udgang (AO1-AO2, DAC, 0-10V eller 4-20mA per ao1_mode/ao2_mode).
 *
 * Mirrorer gpio_driver.h's cache/poll/flush-moenster: analog_driver_poll_inputs()
 * laeser ADC ind i en cache (kald FOER ST Logic-eksekvering, ligesom
 * gpio_driver_poll_inputs()), analog_driver_flush_outputs() skriver DAC ud fra
 * cachet setpoint (kald EFTER, ligesom gpio_driver_flush_outputs()).
 *
 * Kalibrering: engineering_x100 = offset + scale * raw_mv (samme princip som
 * CounterConfig.scale_factor) — se AnalogInputConfig/AnalogOutputConfig i
 * types.h for detaljer og begrundelse.
 *
 * Kun relevant paa ES32D26 (ANALOG_IO_ENABLED) — no-op ellers.
 */

#ifndef ANALOG_DRIVER_H
#define ANALOG_DRIVER_H

#include <stdint.h>
#include "types.h"

/**
 * @brief Saet fornuftige kalibrerings-defaults + faste register-adresser.
 *
 * Kaldes baade fra config_init_defaults() (fabriksny config) og fra
 * config_load.cpp's schema 19->20 migrationsblok (eksisterende device der
 * opgraderer) — samme defaults begge steder, saa der ikke er to steder der
 * kan komme ud af trit med hinanden.
 *
 * Register-adresser (HR, faste i v1 — ikke bruger-omkonfigurerbare endnu):
 *   Vi1-4: HR 0-7   (raw,value par pr. kanal)
 *   Ii1-4: HR 8-15  (raw,value par pr. kanal)
 *   AO1-2: HR 16-17 (setpoint)
 */
void analog_io_set_defaults(PersistConfig *cfg);

/**
 * @brief Initialiser ADC-attenuering for alle 8 AI-pins + registrer HR-adresser
 * hos register_allocator. Kaldes én gang ved boot (main.cpp, kun BOARD_ES32D26).
 */
void analog_driver_init(void);

/**
 * @brief Laes alle enabled AI-kanaler (ADC) ind i cache + skriv raw/value
 * til deres Modbus-registre. Kald FOER st_logic_engine_loop() i loop().
 *
 * Vi1 (GPIO14) og Vi3 (GPIO27) er ADC2 — ubrugelige mens WiFi er aktivt
 * (kendt ESP32 HW-begraensning: WiFi-radioen bruger ADC2 internt). Er WiFi
 * tilsluttet springes disse to kanaler over (registrene beholder deres
 * sidste gyldige vaerdi i stedet for at blive overskrevet med stoej).
 */
void analog_driver_poll_inputs(void);

/**
 * @brief Skriv AO1/AO2 DAC-udgang ud fra deres setpoint-register. Kald
 * EFTER st_logic_engine_loop() i loop().
 */
void analog_driver_flush_outputs(void);

/**
 * @brief Er kanalen ADC2-baseret (Vi1/Vi3) og dermed WiFi-inkompatibel?
 * Bruges af CLI/API/dashboard til at vise "utilgaengelig (WiFi aktiv)".
 */
bool analog_driver_ai_v_is_adc2(uint8_t channel_idx_0based);

#endif // ANALOG_DRIVER_H
