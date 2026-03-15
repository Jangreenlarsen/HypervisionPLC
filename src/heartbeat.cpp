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
}

void heartbeat_loop(void) {
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

  // TODO: Add watchdog timer reset
  // TODO: Add system monitoring (RAM, CPU, etc.)
}

void heartbeat_enable(void) {
  if (!heartbeat_enabled) {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    led_state = 0;
    last_blink = millis();
    heartbeat_enabled = 1;
    debug_println("Heartbeat enabled (GPIO2 reserved for LED)");
  }
}

void heartbeat_disable(void) {
  if (heartbeat_enabled) {
    digitalWrite(LED_PIN, LOW);  // Turn off LED
    pinMode(LED_PIN, INPUT);     // Release pin
    heartbeat_enabled = 0;
    debug_println("Heartbeat disabled (GPIO2 released for user)");
  }
}
