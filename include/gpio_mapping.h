/**
 * @file gpio_mapping.h
 * @brief GPIO STATIC mapping - sync GPIO pins with Modbus registers/coils
 *
 * LAYER 4: Register/Coil Storage
 * Responsibility: Synchronize GPIO pin states with Modbus data
 *
 * Two modes:
 * - INPUT mode: GPIO pin → discrete input (Modbus master reads GPIO state)
 * - OUTPUT mode: Coil → GPIO pin (Modbus master controls GPIO output)
 */

#ifndef gpio_mapping_H
#define gpio_mapping_H

#include <stdint.h>
#include "types.h"

/**
 * @brief Update all GPIO STATIC mappings
 *
 * This function should be called once per main loop iteration.
 * It synchronizes GPIO pins with Modbus registers/coils:
 * - INPUT mode: Read GPIO pin → write to discrete input
 * - OUTPUT mode: Read coil → write to GPIO pin
 *
 * DEPRECATED: Use gpio_mapping_read_before_st_logic() and gpio_mapping_write_after_st_logic()
 * separately to avoid INPUT overwriting OUTPUT in the same loop iteration.
 */
void gpio_mapping_update(void);

/**
 * @brief Read all INPUT mappings BEFORE ST logic execution
 *
 * Call this BEFORE st_logic_engine_loop() to provide fresh inputs.
 * Only processes INPUT mappings (Modbus → ST variables, GPIO → discrete inputs).
 */
void gpio_mapping_read_before_st_logic(void);

/**
 * @brief Write all OUTPUT mappings AFTER ST logic execution
 *
 * Call this AFTER st_logic_engine_loop() to push results to Modbus.
 * Only processes OUTPUT mappings (ST variables → Modbus, coils → GPIO).
 */
void gpio_mapping_write_after_st_logic(void);

/**
 * BUG-428: er en FYSISK pin (0-39) optaget/ugyldig til bruger-mapping?
 * Returnerer NULL hvis pinnen maa bruges, ellers en kort dansk begrundelse
 * (fx "PSRAM (WROVER)"). Virtuelle pins (>= 100) returnerer altid NULL.
 * @param is_output TRUE for udgang (coil -> pin) — GPIO34-39 er input-only.
 * Afhaenger af config (Ethernet til/fra), derfor her og ikke i gpio_driver.
 */
const char *gpio_mapping_pin_reserved(uint16_t pin, bool is_output);

/* FEAT-427 (A4): sikker tilstand pr. udgang (fysisk GPIO eller DO 201-208).
 * Gemmes i egen NVS-noegle ("safe_out"). Bruges af safe mode og (senere)
 * ST-watchdog'ens "safe"-handling. */
int8_t gpio_mapping_safe_get(uint16_t pin);              // -1 = ikke defineret, 0 = OFF, 1 = ON
bool   gpio_mapping_safe_set(uint16_t pin, int8_t state);  // state -1 = fjern definition
uint8_t gpio_mapping_safe_value(uint16_t pin);            // defineret, ellers OFF (0)

#endif // gpio_mapping_H
