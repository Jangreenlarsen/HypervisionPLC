/**
 * @file watchdog_monitor.cpp
 * @brief ESP32 Task Watchdog Monitor Implementation (LAYER 8)
 *
 * LAYER 8: System - Watchdog Monitor
 * Responsibility: Monitor system health and auto-restart on hang
 *
 * This module wraps ESP32 Task Watchdog Timer (TWDT) and provides:
 * - Automatic system restart if main loop hangs (default 30s timeout)
 * - Reboot counter persistence in NVS
 * - Last error message tracking
 * - Subsystem health monitoring
 */

#include "watchdog_monitor.h"
#include "debug.h"
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <string.h>
#include <Arduino.h>

/* ============================================================================
 * CONSTANTS
 * ============================================================================ */

#define WATCHDOG_TIMEOUT_MS      30000  // 30 seconds default timeout
#define WATCHDOG_NVS_KEY         "watchdog"
#define WATCHDOG_NVS_NAMESPACE   "modbus_cfg"

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================ */

static WatchdogState g_watchdog_state = {0};
static bool g_watchdog_initialized = false;
static bool g_watchdog_enabled = true;
static uint32_t g_watchdog_timeout_ms = WATCHDOG_TIMEOUT_MS;

/* ============================================================================
 * PRIVATE HELPERS
 * ============================================================================ */

/**
 * @brief Get ESP32 reset reason as string
 * @return Reset reason string
 */
const char* watchdog_reset_reason_to_str(uint32_t reason_val) {
  esp_reset_reason_t reason = (esp_reset_reason_t)reason_val;

  switch (reason) {
    case ESP_RST_UNKNOWN:    return "Unknown";
    case ESP_RST_POWERON:    return "Power-on";
    case ESP_RST_EXT:        return "External reset";
    case ESP_RST_SW:         return "Software reset";
    case ESP_RST_PANIC:      return "Panic/Exception";
    case ESP_RST_INT_WDT:    return "Interrupt watchdog";
    case ESP_RST_TASK_WDT:   return "Task watchdog";
    case ESP_RST_WDT:        return "Other watchdog";
    case ESP_RST_DEEPSLEEP:  return "Deep sleep";
    case ESP_RST_BROWNOUT:   return "Brownout";
    case ESP_RST_SDIO:       return "SDIO reset";
    default:                 return "Unknown";
  }
}

/* ============================================================================
 * PUBLIC API - INITIALIZATION
 * ============================================================================ */

void watchdog_init(void) {
  if (g_watchdog_initialized) {
    debug_println("WATCHDOG: Already initialized");
    return;
  }

  debug_println("WATCHDOG: Initializing...");

  // Load previous state from NVS
  if (!watchdog_load_state()) {
    // First boot - initialize state
    memset(&g_watchdog_state, 0, sizeof(WatchdogState));
    g_watchdog_state.enabled = 1;
    g_watchdog_state.timeout_ms = WATCHDOG_TIMEOUT_MS;
    g_watchdog_state.reboot_counter = 0;
    g_watchdog_state.last_reset_reason = (uint32_t)esp_reset_reason();
    g_watchdog_state.last_reboot_uptime_ms = 0;
    strcpy(g_watchdog_state.last_error, "First boot");

    debug_println("WATCHDOG: First boot - initialized state");
  }

  // Increment reboot counter
  g_watchdog_state.reboot_counter++;
  g_watchdog_state.last_reset_reason = (uint32_t)esp_reset_reason();
  g_watchdog_state.last_reboot_uptime_ms = millis();

  // Log reset reason
  debug_print("WATCHDOG: Reset reason: ");
  debug_println(watchdog_reset_reason_to_str(g_watchdog_state.last_reset_reason));
  debug_print("WATCHDOG: Reboot counter: ");
  debug_print_uint(g_watchdog_state.reboot_counter);
  debug_println("");

  // Configure ESP32 Task WDT
  if (g_watchdog_enabled) {
    // Initialize watchdog with timeout (in seconds)
    esp_err_t err = esp_task_wdt_init(g_watchdog_timeout_ms / 1000, true);  // timeout in seconds, trigger panic
    if (err == ESP_OK) {
      // Add current task to watchdog
      err = esp_task_wdt_add(NULL);  // NULL = current task
      if (err == ESP_OK) {
        debug_print("WATCHDOG: Enabled with ");
        debug_print_uint(g_watchdog_timeout_ms / 1000);
        debug_println("s timeout");
        g_watchdog_initialized = true;
      } else {
        debug_print("WATCHDOG: Failed to add task (error ");
        debug_print_uint(err);
        debug_println(")");
      }
    } else {
      debug_print("WATCHDOG: Failed to init (error ");
      debug_print_uint(err);
      debug_println(")");
    }
  } else {
    debug_println("WATCHDOG: Disabled in configuration");
  }

  // Save new state to NVS
  watchdog_save_state();

  debug_println("WATCHDOG: Initialization complete");
}

/* ============================================================================
 * PUBLIC API - FEED WATCHDOG
 * ============================================================================ */

void watchdog_feed(void) {
  if (!g_watchdog_initialized || !g_watchdog_enabled) {
    return;
  }

  // Reset watchdog timer
  esp_task_wdt_reset();
}

/* ============================================================================
 * PUBLIC API - ENABLE/DISABLE
 * ============================================================================ */

void watchdog_enable(bool enable) {
  g_watchdog_enabled = enable;
  g_watchdog_state.enabled = enable ? 1 : 0;

  if (g_watchdog_initialized) {
    if (enable) {
      esp_task_wdt_add(NULL);  // Re-add current task
      debug_println("WATCHDOG: Enabled");
    } else {
      esp_task_wdt_delete(NULL);  // Remove current task
      debug_println("WATCHDOG: Disabled");
    }
  }
}

void watchdog_set_timeout(uint32_t timeout_ms) {
  g_watchdog_timeout_ms = timeout_ms;
  g_watchdog_state.timeout_ms = timeout_ms;
  debug_print("WATCHDOG: Timeout set to ");
  debug_print_uint(timeout_ms / 1000);
  debug_println("s");
}

/* ============================================================================
 * PUBLIC API - STATE ACCESS
 * ============================================================================ */

WatchdogState* watchdog_get_state(void) {
  return &g_watchdog_state;
}

/* ============================================================================
 * PUBLIC API - SUBSYSTEM HEALTH TRACKING (OPTIONAL)
 * ============================================================================ */

void watchdog_track_modbus_rx(void) {
  // Future: Track last Modbus RX timestamp
  // For now: No-op
}

void watchdog_track_st_logic(void) {
  // Future: Track last ST Logic execution timestamp
  // For now: No-op
}

void watchdog_track_heartbeat(void) {
  // Future: Track last heartbeat timestamp
  // For now: No-op
}

/* ============================================================================
 * PUBLIC API - ERROR RECORDING
 * ============================================================================ */

void watchdog_record_error(const char* error_msg) {
  if (!error_msg) return;

  // Copy error message (max 127 chars + null terminator)
  strncpy(g_watchdog_state.last_error, error_msg, sizeof(g_watchdog_state.last_error) - 1);
  g_watchdog_state.last_error[sizeof(g_watchdog_state.last_error) - 1] = '\0';

  debug_print("WATCHDOG: Error recorded: ");
  debug_println(error_msg);

  // Save state immediately
  watchdog_save_state();
}

/* ============================================================================
 * PUBLIC API - NVS PERSISTENCE
 * ============================================================================ */

bool watchdog_save_state(void) {
  nvs_handle_t handle;
  esp_err_t err;

  // Open NVS
  err = nvs_open(WATCHDOG_NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    debug_println("WATCHDOG: Failed to open NVS for write");
    return false;
  }

  // Write watchdog state blob
  err = nvs_set_blob(handle, WATCHDOG_NVS_KEY, &g_watchdog_state, sizeof(WatchdogState));
  if (err != ESP_OK) {
    debug_println("WATCHDOG: Failed to write blob to NVS");
    nvs_close(handle);
    return false;
  }

  // Commit
  err = nvs_commit(handle);
  if (err != ESP_OK) {
    debug_println("WATCHDOG: Failed to commit NVS");
    nvs_close(handle);
    return false;
  }

  nvs_close(handle);
  debug_println("WATCHDOG: State saved to NVS");
  return true;
}

bool watchdog_load_state(void) {
  nvs_handle_t handle;
  esp_err_t err;

  // Open NVS
  err = nvs_open(WATCHDOG_NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    debug_println("WATCHDOG: No previous state found in NVS");
    return false;
  }

  // Read watchdog state blob
  size_t length = sizeof(WatchdogState);
  err = nvs_get_blob(handle, WATCHDOG_NVS_KEY, &g_watchdog_state, &length);
  nvs_close(handle);

  if (err != ESP_OK || length != sizeof(WatchdogState)) {
    debug_println("WATCHDOG: No previous state found in NVS");
    return false;
  }

  debug_println("WATCHDOG: State loaded from NVS");
  debug_print("  Previous reboot counter: ");
  debug_print_uint(g_watchdog_state.reboot_counter);
  debug_println("");
  debug_print("  Previous uptime: ");
  debug_print_uint(g_watchdog_state.last_reboot_uptime_ms / 1000);
  debug_println("s");

  if (strlen(g_watchdog_state.last_error) > 0) {
    debug_print("  Last error: ");
    debug_println(g_watchdog_state.last_error);
  }

  return true;
}
