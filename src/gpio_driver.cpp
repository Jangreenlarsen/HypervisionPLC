/**
 * @file gpio_driver.cpp
 * @brief GPIO driver implementation using ESP32 HAL
 *
 * Maps abstract GPIO functions to ESP32 hardware via Arduino API.
 * Supports virtual GPIO pins for shift register I/O on ES32D26:
 *   - GPIO 0-39:    Real ESP32 GPIO pins
 *   - GPIO 100-107: Shift register inputs  (SN74HC165, ES32D26 DI1-DI8)
 *   - GPIO 200-223: Shift register outputs (SN74HC595, ES32D26 DO1-DO8 relæer)
 */

#include "gpio_driver.h"
#include "constants.h"
#include <Arduino.h>
#include <driver/gpio.h>

/* ============================================================================
 * INTERRUPT HANDLERS
 * ============================================================================ */

// Store handlers for up to 40 GPIO pins (ESP32 max)
static gpio_isr_handler_t gpio_handlers[40] = {NULL};
static void* gpio_handler_args[40] = {NULL};

/**
 * @brief Generic interrupt handler wrapper
 * Called from Arduino attachInterrupt, dispatches to user handler
 */
static void IRAM_ATTR gpio_isr_wrapper(void) {
  // Note: We don't know which pin triggered in this context
  // Real implementation would need pin-specific callbacks
  // For now, this is a placeholder
}

/* ============================================================================
 * SHIFT REGISTER SUPPORT (ES32D26)
 * ============================================================================ */

#ifdef SHIFT_REGISTER_ENABLED

// Cached shift register state (updated by shift_register_poll())
static uint8_t sr_input_cache[SR_IN_COUNT] = {0};    // Last read from 74HC165
static uint8_t sr_output_cache[SR_OUT_COUNT] = {0};   // Current state of 74HC595
static bool sr_output_dirty = false;                   // Needs flush to hardware
static bool sr_initialized = false;

/**
 * @brief Initialize shift register GPIO pins
 */
static void shift_register_init(void) {
  // 74HC595 output pins
  pinMode(PIN_SR_OUT_DATA, OUTPUT);
  pinMode(PIN_SR_OUT_CLOCK, OUTPUT);
  pinMode(PIN_SR_OUT_LATCH, OUTPUT);
  pinMode(PIN_SR_OUT_OE, OUTPUT);

  // Start with outputs disabled, all LOW
  digitalWrite(PIN_SR_OUT_OE, HIGH);   // OE active LOW → disable
  digitalWrite(PIN_SR_OUT_DATA, LOW);
  digitalWrite(PIN_SR_OUT_CLOCK, LOW);
  digitalWrite(PIN_SR_OUT_LATCH, LOW);

  // Flush all zeros to shift registers
  for (int i = 0; i < SR_OUT_COUNT; i++) {
    sr_output_cache[i] = 0;
  }
  sr_output_dirty = true;

  // 74HC165 input pins
  pinMode(PIN_SR_IN_DATA, INPUT);
  // GPIO 0 has external pullup for boot — set to floating so 74HC165 can drive it
  gpio_set_pull_mode((gpio_num_t)PIN_SR_IN_DATA, GPIO_FLOATING);
  pinMode(PIN_SR_IN_CLOCK, OUTPUT);
  pinMode(PIN_SR_IN_LOAD, OUTPUT);

  digitalWrite(PIN_SR_IN_CLOCK, LOW);
  digitalWrite(PIN_SR_IN_LOAD, HIGH);  // LOAD active LOW → idle HIGH

  sr_initialized = true;
}

/**
 * @brief Read all shift register inputs (SN74HC165)
 *
 * Pulses LOAD to latch parallel inputs, then clocks out serial data.
 * Results stored in sr_input_cache[].
 */
static void shift_register_read_inputs(void) {
  // Ensure clock is LOW before LOAD pulse
  digitalWrite(PIN_SR_IN_CLOCK, LOW);
  delayMicroseconds(10);

  // Pulse LOAD (active LOW) to latch parallel inputs
  digitalWrite(PIN_SR_IN_LOAD, LOW);
  delayMicroseconds(10);
  digitalWrite(PIN_SR_IN_LOAD, HIGH);
  delayMicroseconds(10);

  // Clock out data from all cascaded 74HC165 chips
  // After LOAD, QH already has D7 — read before first clock
  for (int chip = SR_IN_COUNT - 1; chip >= 0; chip--) {
    uint8_t data = 0;
    for (int bit = 7; bit >= 0; bit--) {
      data |= (digitalRead(PIN_SR_IN_DATA) ? 1 : 0) << bit;
      digitalWrite(PIN_SR_IN_CLOCK, HIGH);
      delayMicroseconds(10);
      digitalWrite(PIN_SR_IN_CLOCK, LOW);
      delayMicroseconds(10);
    }
    sr_input_cache[chip] = data;
  }
}

/**
 * @brief Flush output cache to shift registers (SN74HC595)
 *
 * Shifts out all bytes in sr_output_cache[], then pulses LATCH.
 * Only called when sr_output_dirty is true.
 */
static void shift_register_flush_outputs(void) {
  if (!sr_output_dirty) return;

  // Shift out data to all cascaded 74HC595 chips (MSB first, last chip first)
  for (int chip = SR_OUT_COUNT - 1; chip >= 0; chip--) {
    uint8_t data = sr_output_cache[chip];
    for (int bit = 7; bit >= 0; bit--) {
      digitalWrite(PIN_SR_OUT_DATA, (data >> bit) & 1);
      digitalWrite(PIN_SR_OUT_CLOCK, HIGH);
      delayMicroseconds(5);
      digitalWrite(PIN_SR_OUT_CLOCK, LOW);
      delayMicroseconds(5);
    }
  }

  // Pulse LATCH to transfer shift register → output register
  digitalWrite(PIN_SR_OUT_LATCH, HIGH);
  delayMicroseconds(5);
  digitalWrite(PIN_SR_OUT_LATCH, LOW);

  // Enable outputs (OE active LOW)
  digitalWrite(PIN_SR_OUT_OE, LOW);

  sr_output_dirty = false;
}

/**
 * @brief Read a virtual input pin from shift register cache
 * @param vpin Virtual pin 100-107 (maps to DI1-DI8)
 * @return 0 or 1
 */
static uint8_t shift_register_read(uint8_t vpin) {
  uint8_t index = vpin - VGPIO_SR_INPUT_BASE;
  if (index >= VGPIO_SR_INPUT_COUNT) return 0;
  uint8_t chip = index / 8;
  uint8_t bit = index % 8;
  return (sr_input_cache[chip] >> bit) & 1;
}

/**
 * @brief Write a virtual output pin to shift register cache
 * @param vpin Virtual pin 200-207 (maps to DO1-DO8 relæer)
 * @param level 0 or 1
 */
static void shift_register_write(uint8_t vpin, uint8_t level) {
  uint8_t index = vpin - VGPIO_SR_OUTPUT_BASE;
  if (index >= VGPIO_SR_OUTPUT_COUNT) return;
  uint8_t chip = index / 8;
  uint8_t bit = index % 8;

  uint8_t old = sr_output_cache[chip];
  if (level) {
    sr_output_cache[chip] |= (1 << bit);
  } else {
    sr_output_cache[chip] &= ~(1 << bit);
  }

  if (sr_output_cache[chip] != old) {
    sr_output_dirty = true;
  }
}

#endif // SHIFT_REGISTER_ENABLED

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

void gpio_driver_init(void) {
  // GPIO driver uses Arduino HAL which is already initialized
#ifdef SHIFT_REGISTER_ENABLED
  shift_register_init();
#endif
}

/* ============================================================================
 * GPIO CONTROL (real + virtual pins)
 * ============================================================================ */

void gpio_set_direction(uint8_t pin, gpio_direction_t dir) {
#ifdef SHIFT_REGISTER_ENABLED
  // Virtual pins: direction is fixed by hardware, ignore
  if (pin >= VGPIO_SR_INPUT_BASE) return;
#endif
  if (pin >= 40) return;

  if (dir == GPIO_INPUT) {
    pinMode(pin, INPUT);
  } else {
    pinMode(pin, OUTPUT);
  }
}

uint8_t gpio_read(uint8_t pin) {
#ifdef SHIFT_REGISTER_ENABLED
  // Virtual input pins: read from shift register cache
  if (pin >= VGPIO_SR_INPUT_BASE && pin < VGPIO_SR_INPUT_BASE + VGPIO_SR_INPUT_COUNT) {
    return shift_register_read(pin);
  }
  // Virtual output pins: read back current output state
  if (pin >= VGPIO_SR_OUTPUT_BASE && pin < VGPIO_SR_OUTPUT_BASE + VGPIO_SR_OUTPUT_COUNT) {
    uint8_t index = pin - VGPIO_SR_OUTPUT_BASE;
    uint8_t chip = index / 8;
    uint8_t bit = index % 8;
    return (sr_output_cache[chip] >> bit) & 1;
  }
#endif
  if (pin >= 40) return 0;
  return digitalRead(pin) ? 1 : 0;
}

void gpio_write(uint8_t pin, uint8_t level) {
#ifdef SHIFT_REGISTER_ENABLED
  // Virtual output pins: write to shift register cache
  if (pin >= VGPIO_SR_OUTPUT_BASE && pin < VGPIO_SR_OUTPUT_BASE + VGPIO_SR_OUTPUT_COUNT) {
    shift_register_write(pin, level);
    return;
  }
  // Virtual input pins: ignore writes
  if (pin >= VGPIO_SR_INPUT_BASE && pin < VGPIO_SR_INPUT_BASE + VGPIO_SR_INPUT_COUNT) {
    return;
  }
#endif
  if (pin >= 40) return;
  digitalWrite(pin, level ? HIGH : LOW);
}

/* ============================================================================
 * SHIFT REGISTER POLL (call from main loop)
 * ============================================================================ */

void gpio_driver_poll(void) {
#ifdef SHIFT_REGISTER_ENABLED
  if (!sr_initialized) return;
  shift_register_read_inputs();
  shift_register_flush_outputs();
#endif
}

void gpio_driver_poll_inputs(void) {
#ifdef SHIFT_REGISTER_ENABLED
  if (!sr_initialized) return;
  shift_register_read_inputs();
#endif
}

void gpio_driver_flush_outputs(void) {
#ifdef SHIFT_REGISTER_ENABLED
  if (!sr_initialized) return;
  shift_register_flush_outputs();
#endif
}

/* ============================================================================
 * SHIFT REGISTER HARDWARE TEST
 * ============================================================================ */

void gpio_driver_test_sr(void) {
#ifdef SHIFT_REGISTER_ENABLED
  Serial.println("[SR TEST] Pin configuration:");
  Serial.printf("  595 DATA=%d  CLK=%d  LATCH=%d  OE=%d\n",
    PIN_SR_OUT_DATA, PIN_SR_OUT_CLOCK, PIN_SR_OUT_LATCH, PIN_SR_OUT_OE);
  Serial.printf("  165 DATA=%d  CLK=%d  LOAD=%d\n",
    PIN_SR_IN_DATA, PIN_SR_IN_CLOCK, PIN_SR_IN_LOAD);

  // Test 1: Read current input state
  shift_register_read_inputs();
  Serial.printf("[SR TEST] Input cache: 0x%02X (", sr_input_cache[0]);
  for (int b = 7; b >= 0; b--) {
    Serial.print((sr_input_cache[0] >> b) & 1);
  }
  Serial.println(")");

  // Test 2: Set all outputs ON
  Serial.println("[SR TEST] Setting ALL outputs ON...");
  sr_output_cache[0] = 0xFF;
  sr_output_dirty = true;
  shift_register_flush_outputs();
  Serial.println("[SR TEST] Flushed 0xFF — relæer skal nu være ON");
  Serial.println("[SR TEST] Vent 3 sekunder...");
  delay(3000);

  // Test 3: Set all outputs OFF
  Serial.println("[SR TEST] Setting ALL outputs OFF...");
  sr_output_cache[0] = 0x00;
  sr_output_dirty = true;
  shift_register_flush_outputs();
  Serial.println("[SR TEST] Flushed 0x00 — relæer skal nu være OFF");
  delay(1000);

  // Test 4: Walk through each output individually
  Serial.println("[SR TEST] Walking outputs 1-8...");
  for (int i = 0; i < 8; i++) {
    sr_output_cache[0] = (1 << i);
    sr_output_dirty = true;
    shift_register_flush_outputs();
    Serial.printf("[SR TEST] Relay %d ON (0x%02X)\n", i + 1, sr_output_cache[0]);
    delay(500);
  }

  // Reset all OFF
  sr_output_cache[0] = 0x00;
  sr_output_dirty = true;
  shift_register_flush_outputs();
  Serial.println("[SR TEST] All OFF — test done");

#else
  Serial.println("[SR TEST] SHIFT_REGISTER_ENABLED not defined");
#endif
}

void gpio_driver_test_sr_input(void) {
#ifdef SHIFT_REGISTER_ENABLED
  Serial.println("[SR INPUT TEST] 74HC165 diagnostik");
  Serial.printf("  LOAD=GPIO%d (pin 1)  CLK=GPIO%d (pin 2)  DATA=GPIO%d (pin 9/QH)\n",
    PIN_SR_IN_LOAD, PIN_SR_IN_CLOCK, PIN_SR_IN_DATA);

  // Test 1: Læs 5 gange med 500ms mellemrum
  Serial.println("\n[TEST 1] Læser inputs 5 gange (tryk/slip en input under testen):");
  for (int i = 0; i < 5; i++) {
    shift_register_read_inputs();
    Serial.printf("  Læsning %d: 0x%02X  (IN8..IN1: ", i + 1, sr_input_cache[0]);
    for (int b = 7; b >= 0; b--) {
      Serial.print((sr_input_cache[0] >> b) & 1);
    }
    Serial.println(")");
    if (i < 4) delay(500);
  }

  // Test 2: Manuel bit-bang med debug per bit
  Serial.println("\n[TEST 2] Manuel bit-bang:");
  digitalWrite(PIN_SR_IN_CLOCK, LOW);
  delayMicroseconds(10);
  digitalWrite(PIN_SR_IN_LOAD, LOW);
  delayMicroseconds(10);
  digitalWrite(PIN_SR_IN_LOAD, HIGH);
  delayMicroseconds(10);

  for (int bit = 7; bit >= 0; bit--) {
    int val = digitalRead(PIN_SR_IN_DATA);
    Serial.printf("  IN%d: %d\n", bit + 1, val);
    digitalWrite(PIN_SR_IN_CLOCK, HIGH);
    delayMicroseconds(10);
    digitalWrite(PIN_SR_IN_CLOCK, LOW);
    delayMicroseconds(10);
  }

  Serial.println("[SR INPUT TEST] Done");
#else
  Serial.println("[SR INPUT TEST] SHIFT_REGISTER_ENABLED not defined");
#endif
}

/* ============================================================================
 * INTERRUPT HANDLING
 * ============================================================================ */

void gpio_interrupt_attach(uint8_t pin, gpio_edge_t edge, gpio_isr_handler_t handler) {
  if (pin >= 40 || handler == NULL) return;

  // Set GPIO as input first
  gpio_set_direction(pin, GPIO_INPUT);

  // Store handler
  gpio_handlers[pin] = handler;

  // Map edge type to Arduino interrupt mode
  int mode;
  switch (edge) {
    case GPIO_RISING:
      mode = RISING;
      break;
    case GPIO_FALLING:
      mode = FALLING;
      break;
    case GPIO_BOTH:
      mode = CHANGE;
      break;
    default:
      return;
  }

  // Attach interrupt - note: Arduino's attachInterrupt is limited
  // For full ISR with custom handlers, would need direct ESP32 GPIO API
  // This is a simplified implementation using Arduino API
  // Real implementation would use esp_gpio_isr_register() for more control

  // For now, just store the handler - interrupt dispatch needs deeper ESP32 integration
  // TODO: Implement proper ISR dispatch via ESP32 GPIO interrupt controller
}

void gpio_interrupt_detach(uint8_t pin) {
  if (pin >= 40) return;

  // Detach Arduino interrupt if applicable
  // detachInterrupt(pin);  // Arduino API limitation

  // Clear handler
  gpio_handlers[pin] = NULL;
  gpio_handler_args[pin] = NULL;
}
