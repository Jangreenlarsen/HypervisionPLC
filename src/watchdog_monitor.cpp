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
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <string.h>
#include <time.h>
#include "ntp_driver.h"  // FEAT-429: tidsstempel paa sidste fejl
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

#define WATCHDOG_TIMEOUT_MIN_MS   5000
#define WATCHDOG_TIMEOUT_MAX_MS 120000

/* FEAT-427: tilmeldte tasks (loopTask + baggrunds-tasks) og hvornaar de sidst
 * fodrede — kun til diagnostik i `show watchdog`; selve overvaagningen
 * goeres af ESP-IDF's task-watchdog. */
#define WATCHDOG_MAX_TASKS 8
typedef struct {
  TaskHandle_t handle;
  char name[16];
  uint32_t last_feed_ms;
} WatchdogTaskSlot;
static WatchdogTaskSlot g_wdt_tasks[WATCHDOG_MAX_TASKS];

/* FEAT-427 (A3): oppetid i RTC-hukommelse. RTC_NOINIT overlever software-
 * reset, panic og watchdog-reset (men ikke stroemsvigt), saa vi ved opstart
 * kan se hvor laenge enheden koerte foer et crash. Opdateres fra loopTask. */
#define WDT_RTC_MAGIC 0x57445432UL  // "WDT2" (FEAT-429: + epoch)
typedef struct {
  uint32_t magic;
  uint32_t uptime_ms;
  uint32_t epoch;      // FEAT-429: senest kendte Unix-tid (NTP), 0 = ukendt
} WdtRtcData;
RTC_NOINIT_ATTR static WdtRtcData g_wdt_rtc;
static bool g_reset_was_crash = false;
static bool g_streak_cleared = false;

/* FEAT-429: aktuel Unix-tid hvis NTP er synkroniseret, ellers 0 */
static uint32_t wdt_now_epoch(void) {
  return ntp_driver_is_synced() ? (uint32_t)ntp_driver_get_epoch() : 0;
}

/* Tidsstempler foer 2020 / efter 2100 er ugyldige — fx de 4 bytes der foer
 * FEAT-429 var slutningen af en 128-tegns last_error i en gammel NVS-blob. */
uint32_t watchdog_last_error_epoch(void) {
  uint32_t e = g_watchdog_state.last_error_epoch;
  return (e >= 1577836800UL && e < 4102444800UL) ? e : 0;
}

bool watchdog_last_error_time_str(char *buf, size_t n) {
  if (!buf || n == 0) return false;
  buf[0] = '\0';
  uint32_t e = watchdog_last_error_epoch();
  if (!e) return false;
  time_t t = (time_t)e;
  struct tm tmv;
  localtime_r(&t, &tmv);   // NTP-driveren saetter TZ
  strftime(buf, n, "%Y-%m-%d %H:%M:%S", &tmv);
  return true;
}

static bool reset_reason_is_crash(esp_reset_reason_t r) {
  return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT;
}
static portMUX_TYPE g_wdt_tasks_mux = portMUX_INITIALIZER_UNLOCKED;

static void wdt_task_register(TaskHandle_t h) {
  taskENTER_CRITICAL(&g_wdt_tasks_mux);
  for (int i = 0; i < WATCHDOG_MAX_TASKS; i++) {
    if (g_wdt_tasks[i].handle == NULL) {
      g_wdt_tasks[i].handle = h;
      strncpy(g_wdt_tasks[i].name, pcTaskGetName(h), sizeof(g_wdt_tasks[i].name) - 1);
      g_wdt_tasks[i].name[sizeof(g_wdt_tasks[i].name) - 1] = '\0';
      g_wdt_tasks[i].last_feed_ms = millis();
      break;
    }
  }
  taskEXIT_CRITICAL(&g_wdt_tasks_mux);
}

/* FEAT-427: alder siden sidste fodring. En task paa den anden core kan fodre
 * EFTER at "now" er laest — saa er last > now, og en ren uint32-subtraktion
 * loeber rundt til ~4294967 s. Det betyder "lige fodret" -> 0. */
static uint32_t wdt_age_ms(uint32_t now, uint32_t last) {
  int32_t d = (int32_t)(now - last);
  return d < 0 ? 0 : (uint32_t)d;
}

static void wdt_task_touch(TaskHandle_t h) {
  uint32_t now = millis();
  for (int i = 0; i < WATCHDOG_MAX_TASKS; i++) {
    if (g_wdt_tasks[i].handle == h) { g_wdt_tasks[i].last_feed_ms = now; return; }
  }
}

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
  if (watchdog_load_state()) {
    // FEAT-427: brug de GEMTE indstillinger (blev tidligere indlaest men
    // ignoreret — g_watchdog_enabled/timeout var altid true/30 s).
    g_watchdog_enabled = (g_watchdog_state.enabled != 0);
    if (g_watchdog_state.timeout_ms >= WATCHDOG_TIMEOUT_MIN_MS &&
        g_watchdog_state.timeout_ms <= WATCHDOG_TIMEOUT_MAX_MS) {
      g_watchdog_timeout_ms = g_watchdog_state.timeout_ms;
    } else {
      g_watchdog_state.timeout_ms = g_watchdog_timeout_ms;
    }
  } else {
    // First boot - initialize state
    memset(&g_watchdog_state, 0, sizeof(WatchdogState));
    g_watchdog_state.enabled = 1;
    g_watchdog_state.timeout_ms = WATCHDOG_TIMEOUT_MS;
    g_watchdog_state.reboot_counter = 0;
    g_watchdog_state.last_reset_reason = (uint32_t)esp_reset_reason();
    g_watchdog_state.last_reboot_uptime_ms = 0;
    strcpy(g_watchdog_state.last_error, "First boot");
    g_watchdog_state.last_error_epoch = 0;

    debug_println("WATCHDOG: First boot - initialized state");
  }

  // Increment reboot counter
  g_watchdog_state.reboot_counter++;
  g_watchdog_state.last_reset_reason = (uint32_t)esp_reset_reason();

  // FEAT-427 (A3): oppetid foer genstarten (fra RTC) — tidligere sattes feltet
  // til millis() ved opstart, altsaa altid ~1 s.
  esp_reset_reason_t rr = esp_reset_reason();
  bool rtc_valid = (g_wdt_rtc.magic == WDT_RTC_MAGIC) && rr != ESP_RST_POWERON && rr != ESP_RST_BROWNOUT;
  uint32_t prev_uptime = rtc_valid ? g_wdt_rtc.uptime_ms : 0;
  uint32_t prev_epoch = rtc_valid ? g_wdt_rtc.epoch : 0;  // FEAT-429: ~tidspunkt for crashet
  g_watchdog_state.last_reboot_uptime_ms = prev_uptime;
  g_wdt_rtc.magic = WDT_RTC_MAGIC;
  g_wdt_rtc.uptime_ms = 0;
  g_wdt_rtc.epoch = 0;

  g_reset_was_crash = reset_reason_is_crash(rr);
  if (g_reset_was_crash) {
    g_watchdog_state.crash_counter++;
    // Crash efter kort drift (eller ukendt drift) = endnu et crash i traek
    if (prev_uptime < WATCHDOG_STABLE_UPTIME_MS) {
      if (g_watchdog_state.crash_streak < 255) g_watchdog_state.crash_streak++;
    } else {
      g_watchdog_state.crash_streak = 1;
    }
    snprintf(g_watchdog_state.last_error, sizeof(g_watchdog_state.last_error),
             "Crash: %s efter %lu s drift (%u i traek)", watchdog_reset_reason_to_str(rr),
             (unsigned long)(prev_uptime / 1000), (unsigned)g_watchdog_state.crash_streak);
    g_watchdog_state.last_error_epoch = prev_epoch;  // FEAT-429: senest kendte tid foer crashet
    if (g_watchdog_state.crash_streak >= WATCHDOG_SAFE_MODE_STREAK) {
      g_watchdog_state.safe_mode = 1;
    }
  }
  // FEAT-427 lag B: ogsaa kontrollerede genstarter fra ST-watchdog'en
  // (watchdog_reboot_for, reset-aarsag = software) taeller i crash_streak.
  if (g_watchdog_state.crash_streak >= WATCHDOG_SAFE_MODE_STREAK) {
    g_watchdog_state.safe_mode = 1;
  }
  if (g_watchdog_state.safe_mode) {
    debug_println("WATCHDOG: *** SAFE MODE *** ST stoppet, udgange i sikker tilstand ('clear safemode')");
  }

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
        wdt_task_register(xTaskGetCurrentTaskHandle());  // FEAT-427: loopTask
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
  // FEAT-427 (A3): oppetid til RTC + nulstil "crash i traek" efter stabil
  // drift — ogsaa naar watchdog'en er slaaet fra, ellers ville ethvert crash
  // se ud som "efter 0 s drift" og fejlagtigt taelle mod safe mode.
  uint32_t now = millis();
  g_wdt_rtc.uptime_ms = now;
  g_wdt_rtc.epoch = wdt_now_epoch();  // FEAT-429
  if (!g_streak_cleared && now >= WATCHDOG_STABLE_UPTIME_MS) {
    g_streak_cleared = true;
    if (g_watchdog_state.crash_streak != 0) {
      g_watchdog_state.crash_streak = 0;   // safe mode forlades IKKE automatisk
      watchdog_save_state();
    }
  }

  if (!g_watchdog_initialized || !g_watchdog_enabled) {
    return;
  }

  // Reset watchdog timer
  esp_task_wdt_reset();
  wdt_task_touch(xTaskGetCurrentTaskHandle());  // FEAT-427
}

bool watchdog_safe_mode(void) {
  return g_watchdog_state.safe_mode != 0;
}

void watchdog_clear_safe_mode(void) {
  g_watchdog_state.safe_mode = 0;
  g_watchdog_state.crash_streak = 0;
  watchdog_save_state();
}

bool watchdog_reset_was_crash(void) {
  return g_reset_was_crash;
}

void watchdog_reboot_for(const char *reason) {
  uint32_t up = millis();
  if (up < WATCHDOG_STABLE_UPTIME_MS && g_watchdog_state.crash_streak < 255) {
    g_watchdog_state.crash_streak++;
  } else {
    g_watchdog_state.crash_streak = 1;
  }
  g_watchdog_state.crash_counter++;
  snprintf(g_watchdog_state.last_error, sizeof(g_watchdog_state.last_error),
           "Genstart: %s (%u i traek)", reason ? reason : "?", (unsigned)g_watchdog_state.crash_streak);
  g_watchdog_state.last_error_epoch = wdt_now_epoch();  // FEAT-429
  watchdog_save_state();
  delay(200);
  esp_restart();
}

bool watchdog_is_active(void) {
  return g_watchdog_initialized && g_watchdog_enabled;
}

void watchdog_task_subscribe(void) {
  if (!watchdog_is_active()) return;
  if (esp_task_wdt_add(NULL) == ESP_OK) {
    wdt_task_register(xTaskGetCurrentTaskHandle());
  }
}

void watchdog_task_feed(void) {
  if (!watchdog_is_active()) return;
  esp_task_wdt_reset();
  wdt_task_touch(xTaskGetCurrentTaskHandle());
}

void watchdog_task_unsubscribe(void) {
  TaskHandle_t h = xTaskGetCurrentTaskHandle();
  bool found = false;
  taskENTER_CRITICAL(&g_wdt_tasks_mux);
  for (int i = 0; i < WATCHDOG_MAX_TASKS; i++) {
    if (g_wdt_tasks[i].handle == h) { g_wdt_tasks[i].handle = NULL; found = true; break; }
  }
  taskEXIT_CRITICAL(&g_wdt_tasks_mux);
  if (found) esp_task_wdt_delete(NULL);
}

uint8_t watchdog_get_tasks(char (*names)[16], uint32_t *age_ms, uint8_t max_out) {
  uint32_t now = millis();
  uint8_t n = 0;
  taskENTER_CRITICAL(&g_wdt_tasks_mux);
  for (int i = 0; i < WATCHDOG_MAX_TASKS && n < max_out; i++) {
    if (g_wdt_tasks[i].handle == NULL) continue;
    memcpy(names[n], g_wdt_tasks[i].name, 16);
    age_ms[n] = wdt_age_ms(now, g_wdt_tasks[i].last_feed_ms);
    n++;
  }
  taskEXIT_CRITICAL(&g_wdt_tasks_mux);
  return n;
}

void watchdog_print_tasks(void) {
  uint32_t now = millis();
  debug_println("Overvaagede tasks (sek. siden sidste fodring):");
  bool any = false;
  for (int i = 0; i < WATCHDOG_MAX_TASKS; i++) {
    if (g_wdt_tasks[i].handle == NULL) continue;
    any = true;
    debug_printf("  %-14s %lu.%01lu s\n", g_wdt_tasks[i].name,
                 (unsigned long)(wdt_age_ms(now, g_wdt_tasks[i].last_feed_ms) / 1000),
                 (unsigned long)((wdt_age_ms(now, g_wdt_tasks[i].last_feed_ms) % 1000) / 100));
  }
  if (!any) debug_println("  (ingen — watchdog'en er ikke aktiv)");
}

/* ============================================================================
 * PUBLIC API - ENABLE/DISABLE
 * ============================================================================ */

// FEAT-427: til/fra gemmes og traeder i kraft ved naeste genstart. En live
// afmelding virker ikke paalideligt: kommandoen kan komme fra web-CLI'en
// (httpd-tasken), som ikke selv er tilmeldt, og baggrunds-tasks er tilmeldt
// hver for sig.
void watchdog_enable(bool enable) {
  g_watchdog_state.enabled = enable ? 1 : 0;
  watchdog_save_state();
}

// FEAT-427: timeout gemmes og anvendes straks (ESP-IDF 4.4's
// esp_task_wdt_init() rekonfigurerer en allerede initialiseret watchdog).
bool watchdog_set_timeout(uint32_t timeout_ms) {
  if (timeout_ms < WATCHDOG_TIMEOUT_MIN_MS || timeout_ms > WATCHDOG_TIMEOUT_MAX_MS) return false;
  g_watchdog_timeout_ms = timeout_ms;
  g_watchdog_state.timeout_ms = timeout_ms;
  if (g_watchdog_initialized) {
    esp_task_wdt_init(timeout_ms / 1000, true);
  }
  watchdog_save_state();
  return true;
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
  g_watchdog_state.last_error_epoch = wdt_now_epoch();  // FEAT-429

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
    char ts[24];
    debug_print("  Last error: ");
    debug_print(g_watchdog_state.last_error);
    debug_print(watchdog_last_error_time_str(ts, sizeof(ts)) ? "  [" : "  [tidspunkt ukendt");
    debug_print(ts);
    debug_println("]");
  }

  return true;
}
