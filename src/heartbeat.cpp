/**
 * @file heartbeat.cpp
 * @brief Heartbeat/watchdog implementation - blink LED and monitor system
 */

#include "heartbeat.h"
#include "constants.h"
#include "config_struct.h"
#include "debug.h"
#include <Arduino.h>

#define LED_PIN PIN_LED  // Board-specifik LED pin fra constants.h
#define BLINK_INTERVAL 1000  // 1 second

static uint32_t last_blink = 0;
static uint8_t led_state = 0;
static uint8_t heartbeat_enabled = 1;  // Start enabled by default

void heartbeat_init(void) {
#if PIN_LED < 0
  // Board har ingen ledig LED pin (f.eks. ES32D26)
  heartbeat_enabled = 0;
  debug_println("Heartbeat disabled (no LED pin available)");
#else
  // Only initialize if not in user mode
  if (g_persist_config.gpio2_user_mode == 0) {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    heartbeat_enabled = 1;
    debug_println("Heartbeat initialized (LED on GPIO2)");
  } else {
    heartbeat_enabled = 0;
    debug_println("Heartbeat disabled (GPIO2 in user mode)");
  }
#endif
}

void heartbeat_loop(void) {
#if PIN_LED < 0
  return;  // Ingen LED pin tilgængelig
#else
  // Skip if heartbeat is disabled (GPIO2 in user mode)
  if (!heartbeat_enabled) {
    return;
  }

  uint32_t now = millis();

  if (now - last_blink >= BLINK_INTERVAL) {
    led_state = !led_state;
    digitalWrite(LED_PIN, led_state ? HIGH : LOW);
    last_blink = now;
  }
#endif

  // TODO: Add watchdog timer reset
  // TODO: Add system monitoring (RAM, CPU, etc.)
}

void heartbeat_enable(void) {
#if PIN_LED < 0
  return;  // Ingen LED pin tilgængelig
#else
  if (!heartbeat_enabled) {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    led_state = 0;
    last_blink = millis();
    heartbeat_enabled = 1;
    debug_println("Heartbeat enabled (GPIO2 reserved for LED)");
  }
#endif
}

void heartbeat_disable(void) {
#if PIN_LED < 0
  return;  // Ingen LED pin tilgængelig
#else
  if (heartbeat_enabled) {
    digitalWrite(LED_PIN, LOW);  // Turn off LED
    pinMode(LED_PIN, INPUT);     // Release pin
    heartbeat_enabled = 0;
    debug_println("Heartbeat disabled (GPIO2 released for user)");
  }
#endif
}
