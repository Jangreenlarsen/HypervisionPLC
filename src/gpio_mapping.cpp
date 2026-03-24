/**
 * @file gpio_mapping.cpp
 * @brief UNIFIED VARIABLE MAPPING - sync GPIO pins + ST variables with Modbus registers/coils
 *
 * LAYER 4: Register/Coil Storage
 * Responsibility: Synchronize GPIO pins and ST Logic variables with Modbus data
 *
 * This handles ALL variable I/O:
 * - GPIO pins (INPUT/OUTPUT modes)
 * - ST Logic program variables (INPUT/OUTPUT modes)
 */

#include "gpio_mapping.h"
#include "config_struct.h"
#include "gpio_driver.h"
#include "registers.h"
#include "st_logic_config.h"
#include "st_logic_engine.h"  // BUG-038 FIX: For variable locking
#include <string.h>            // BUG-105: For memcpy() (REAL type conversion)

/**
 * @brief Read all INPUT mappings (GPIO + ST variables)
 *
 * Called BEFORE st_logic_engine_loop() to provide fresh inputs.
 * - GPIO INPUT mode: Read GPIO pin → write to discrete input
 * - ST VAR INPUT mode: Read holding register/discrete input → write to ST variable
 */
static void gpio_mapping_read_inputs(void) {
  for (uint8_t i = 0; i < g_persist_config.var_map_count; i++) {
    const VariableMapping* map = &g_persist_config.var_maps[i];

    // ========================================================================
    // GPIO MAPPING - INPUT ONLY
    // ========================================================================
    if (map->source_type == MAPPING_SOURCE_GPIO) {
      // Skip mappings associated with counters/timers (handled by their engines)
      if (map->associated_counter != 0xff || map->associated_timer != 0xff) {
        continue;
      }

      if (map->is_input) {
        // INPUT mode: GPIO pin → discrete input
        if (map->input_reg != 65535) {
          // gpio_read() handles both real GPIOs (0-39) and
          // virtual shift register pins (101-108, 201-224)
          uint8_t level = gpio_read(map->gpio_pin);
          registers_set_discrete_input(map->input_reg, level);
        }
      }
    }
    // ========================================================================
    // ST LOGIC VARIABLE MAPPING - INPUT ONLY
    // ========================================================================
    else if (map->source_type == MAPPING_SOURCE_ST_VAR) {
      st_logic_engine_state_t *st_state = st_logic_get_state();
      if (!st_state) continue;

      st_logic_program_config_t *prog = st_logic_get_program(st_state, map->st_program_id);
      if (!prog || !prog->compiled || map->st_var_index >= prog->bytecode.var_count) {
        continue;
      }

      if (map->is_input) {
        // INPUT mode: Read from Modbus, write to ST variable
        if (map->input_reg != 65535) {
          // BUG-010 FIX: Type-aware bounds check (DI uses different size than HR)
          // BUG-049 FIX: Add bounds check for Coil type
          // BUG-105 FIX: Multi-register bounds check (word_count)
          uint8_t word_count = (map->word_count > 0) ? map->word_count : 1;

          if (map->input_type == 1) {
            // Discrete Input - check DI array size (32 bytes = 256 bits)
            if (map->input_reg >= DISCRETE_INPUTS_SIZE * 8) continue;
          } else if (map->input_type == 2) {
            // Coil - check Coil array size (32 bytes = 256 bits)
            if (map->input_reg >= COILS_SIZE * 8) continue;
          } else {
            // Holding Register - check HR array size (multi-register aware)
            if (map->input_reg + word_count > HOLDING_REGS_SIZE) continue;
          }

          // BUG-038 FIX: Lock before writing to ST variable
          st_logic_lock_variables();

          // BUG-002 & BUG-105 FIX: Store value with correct type conversion
          st_datatype_t var_type = prog->bytecode.var_types[map->st_var_index];

          if (var_type == ST_TYPE_BOOL) {
            // BOOL: read from DI or Coil
            uint16_t reg_value;
            if (map->input_type == 1) {
              reg_value = registers_get_discrete_input(map->input_reg) ? 1 : 0;
            } else if (map->input_type == 2) {
              reg_value = registers_get_coil(map->input_reg) ? 1 : 0;
            } else {
              reg_value = registers_get_holding_register(map->input_reg);
            }
            prog->bytecode.variables[map->st_var_index].bool_val = (reg_value != 0);
          }
          else if (var_type == ST_TYPE_INT) {
            // INT: 16-bit signed, 1 register
            uint16_t reg_value = registers_get_holding_register(map->input_reg);
            prog->bytecode.variables[map->st_var_index].int_val = (int16_t)reg_value;
          }
          else if (var_type == ST_TYPE_DINT) {
            // BUG-125 FIX: DINT: 32-bit signed, 2 registers (LSW first, MSW second)
            uint16_t low_word = registers_get_holding_register(map->input_reg);      // LSW at base
            uint16_t high_word = registers_get_holding_register(map->input_reg + 1); // MSW at base+1
            int32_t dint_value = ((int32_t)high_word << 16) | low_word;
            prog->bytecode.variables[map->st_var_index].dint_val = dint_value;
          }
          else if (var_type == ST_TYPE_DWORD) {
            // BUG-125 FIX: DWORD: 32-bit unsigned, 2 registers (LSW first, MSW second)
            uint16_t low_word = registers_get_holding_register(map->input_reg);      // LSW at base
            uint16_t high_word = registers_get_holding_register(map->input_reg + 1); // MSW at base+1
            uint32_t dword_value = ((uint32_t)high_word << 16) | low_word;
            prog->bytecode.variables[map->st_var_index].dword_val = dword_value;
          }
          else if (var_type == ST_TYPE_REAL) {
            // BUG-125 FIX: REAL: 32-bit float, 2 registers (IEEE 754, LSW first, MSW second)
            uint16_t low_word = registers_get_holding_register(map->input_reg);      // LSW at base
            uint16_t high_word = registers_get_holding_register(map->input_reg + 1); // MSW at base+1
            uint32_t bits = ((uint32_t)high_word << 16) | low_word;
            float real_value;
            memcpy(&real_value, &bits, sizeof(float));  // Reinterpret bits as float
            prog->bytecode.variables[map->st_var_index].real_val = real_value;
          }

          st_logic_unlock_variables();
        }
      }
    }
  }
}

/**
 * @brief Write all OUTPUT mappings (GPIO + ST variables)
 *
 * Called AFTER st_logic_engine_loop() to push results to registers.
 * - GPIO OUTPUT mode: Read coil → write to GPIO pin
 * - ST VAR OUTPUT mode: Read ST variable → write to holding register/coil
 */
static void gpio_mapping_write_outputs(void) {
  for (uint8_t i = 0; i < g_persist_config.var_map_count; i++) {
    const VariableMapping* map = &g_persist_config.var_maps[i];

    // ========================================================================
    // GPIO MAPPING - OUTPUT ONLY
    // ========================================================================
    if (map->source_type == MAPPING_SOURCE_GPIO) {
      // Skip mappings associated with counters/timers (handled by their engines)
      if (map->associated_counter != 0xff || map->associated_timer != 0xff) {
        continue;
      }

      if (!map->is_input) {
        // OUTPUT mode: Coil → GPIO pin
        if (map->coil_reg != 65535) {
          uint8_t value = registers_get_coil(map->coil_reg);
          gpio_write(map->gpio_pin, value);
        }
      }
    }
    // ========================================================================
    // ST LOGIC VARIABLE MAPPING - OUTPUT ONLY
    // ========================================================================
    else if (map->source_type == MAPPING_SOURCE_ST_VAR) {
      st_logic_engine_state_t *st_state = st_logic_get_state();
      if (!st_state) continue;

      st_logic_program_config_t *prog = st_logic_get_program(st_state, map->st_program_id);
      if (!prog || !prog->compiled || map->st_var_index >= prog->bytecode.var_count) {
        continue;
      }

      if (!map->is_input) {
        // OUTPUT mode: Read from ST variable, write to Modbus
        // BUG-038 FIX: Lock before reading ST variable
        st_logic_lock_variables();

        // BUG-002 & BUG-105 FIX: Read value with correct type conversion
        st_datatype_t var_type = prog->bytecode.var_types[map->st_var_index];

        // Check output_type to determine destination
        if (map->coil_reg != 65535) {
          // output_type: 0 = Holding Register, 1 = Coil
          if (map->output_type == 1) {
            // Output to COIL (BOOL variables)
            uint8_t coil_value = 0;
            if (var_type == ST_TYPE_BOOL) {
              coil_value = prog->bytecode.variables[map->st_var_index].bool_val ? 1 : 0;
            } else {
              // Convert INT/DINT/REAL to BOOL (non-zero = 1)
              coil_value = (prog->bytecode.variables[map->st_var_index].int_val != 0) ? 1 : 0;
            }
            registers_set_coil(map->coil_reg, coil_value);
          }
          else {
            // Output to HOLDING REGISTER (multi-register aware)
            if (var_type == ST_TYPE_BOOL) {
              // BOOL: 1 register
              uint16_t reg_value = prog->bytecode.variables[map->st_var_index].bool_val ? 1 : 0;
              registers_set_holding_register(map->coil_reg, reg_value);
            }
            else if (var_type == ST_TYPE_INT) {
              // INT: 16-bit signed, 1 register
              int16_t int_value = prog->bytecode.variables[map->st_var_index].int_val;
              registers_set_holding_register(map->coil_reg, (uint16_t)int_value);
            }
            else if (var_type == ST_TYPE_DINT) {
              // BUG-125 FIX: DINT: 32-bit signed, 2 registers (LSW first, MSW second)
              int32_t dint_value = prog->bytecode.variables[map->st_var_index].dint_val;
              uint16_t low_word = (uint16_t)(dint_value & 0xFFFF);
              uint16_t high_word = (uint16_t)((dint_value >> 16) & 0xFFFF);
              registers_set_holding_register(map->coil_reg, low_word);      // LSW at base
              registers_set_holding_register(map->coil_reg + 1, high_word); // MSW at base+1
            }
            else if (var_type == ST_TYPE_DWORD) {
              // BUG-125 FIX: DWORD: 32-bit unsigned, 2 registers (LSW first, MSW second)
              uint32_t dword_value = prog->bytecode.variables[map->st_var_index].dword_val;
              uint16_t low_word = (uint16_t)(dword_value & 0xFFFF);
              uint16_t high_word = (uint16_t)((dword_value >> 16) & 0xFFFF);
              registers_set_holding_register(map->coil_reg, low_word);      // LSW at base
              registers_set_holding_register(map->coil_reg + 1, high_word); // MSW at base+1
            }
            else if (var_type == ST_TYPE_REAL) {
              // BUG-125 FIX: REAL: 32-bit float, 2 registers (IEEE 754, LSW first, MSW second)
              float real_value = prog->bytecode.variables[map->st_var_index].real_val;
              uint32_t bits;
              memcpy(&bits, &real_value, sizeof(float));  // Reinterpret float as bits
              uint16_t low_word = (uint16_t)(bits & 0xFFFF);
              uint16_t high_word = (uint16_t)((bits >> 16) & 0xFFFF);
              registers_set_holding_register(map->coil_reg, low_word);      // LSW at base
              registers_set_holding_register(map->coil_reg + 1, high_word); // MSW at base+1
            }
          }
        }

        st_logic_unlock_variables();
      }
    }
  }
}

/**
 * @brief Update all variable mappings (GPIO + ST variables)
 *
 * This function is called once per main loop iteration.
 * It synchronizes ALL mapped sources with Modbus registers/coils:
 * - GPIO INPUT mode: Read GPIO pin → write to discrete input
 * - GPIO OUTPUT mode: Read coil → write to GPIO pin
 * - ST VAR INPUT mode: Read holding register → write to ST variable
 * - ST VAR OUTPUT mode: Read ST variable → write to holding register
 *
 * DEPRECATED: Use gpio_mapping_read_inputs() and gpio_mapping_write_outputs() separately
 * to avoid INPUT overwriting OUTPUT in the same loop iteration.
 */
void gpio_mapping_update(void) {
  gpio_mapping_read_inputs();
  gpio_mapping_write_outputs();
}

/**
 * @brief Read all INPUT mappings BEFORE ST logic execution
 *
 * Wrapper for gpio_mapping_read_inputs() for use in main.cpp
 */
void gpio_mapping_read_before_st_logic(void) {
  gpio_mapping_read_inputs();
}

/**
 * @brief Write all OUTPUT mappings AFTER ST logic execution
 *
 * Wrapper for gpio_mapping_write_outputs() for use in main.cpp
 */
void gpio_mapping_write_after_st_logic(void) {
  gpio_mapping_write_outputs();
}

