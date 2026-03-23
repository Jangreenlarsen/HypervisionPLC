/**
 * @file gpio_driver.h
 * @brief GPIO hardware abstraction driver
 *
 * LAYER 0: Hardware Abstraction Driver
 * Provides GPIO read/write and interrupt handling
 */

#ifndef GPIO_DRIVER_H
#define GPIO_DRIVER_H

#include <stdint.h>

/* GPIO direction */
typedef enum {
  GPIO_INPUT = 0,
  GPIO_OUTPUT = 1
} gpio_direction_t;

/* GPIO edge types */
typedef enum {
  GPIO_RISING = 0,
  GPIO_FALLING = 1,
  GPIO_BOTH = 2
} gpio_edge_t;

/* ISR handler type */
typedef void (*gpio_isr_handler_t)(void* arg);

/**
 * @brief Initialize GPIO
 */
void gpio_driver_init(void);

/**
 * @brief Set GPIO direction
 */
void gpio_set_direction(uint8_t pin, gpio_direction_t dir);

/**
 * @brief Read GPIO level
 */
uint8_t gpio_read(uint8_t pin);

/**
 * @brief Write GPIO level
 */
void gpio_write(uint8_t pin, uint8_t level);

/**
 * @brief Poll shift register I/O (call from main loop)
 *
 * Reads SN74HC165 inputs into cache, flushes SN74HC595 output cache.
 * No-op if SHIFT_REGISTER_ENABLED is not defined.
 */
void gpio_driver_poll(void);

/**
 * @brief Read shift register inputs only (SN74HC165)
 * Call BEFORE reading input mappings.
 */
void gpio_driver_poll_inputs(void);

/**
 * @brief Flush shift register outputs only (SN74HC595)
 * Call AFTER writing output mappings.
 */
void gpio_driver_flush_outputs(void);

/**
 * @brief Test shift register outputs (toggle all relays ON then OFF)
 * For hardware debugging. Prints pin states to serial.
 */
void gpio_driver_test_sr(void);

/**
 * @brief Test shift register inputs (SN74HC165 diagnostik)
 * Reads inputs multiple times with debug output per bit.
 */
void gpio_driver_test_sr_input(void);

/**
 * @brief Attach GPIO interrupt
 */
void gpio_interrupt_attach(uint8_t pin, gpio_edge_t edge, gpio_isr_handler_t handler);

/**
 * @brief Detach GPIO interrupt
 */
void gpio_interrupt_detach(uint8_t pin);

#endif // GPIO_DRIVER_H
