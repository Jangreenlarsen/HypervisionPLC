/**
 * @file ntp_driver.cpp
 * @brief NTP time synchronization driver (v7.8.1)
 *
 * LAYER 3: Driver — NTP/SNTP client using ESP-IDF lwIP SNTP
 * Uses esp_sntp component for automatic time synchronization.
 */

#include "ntp_driver.h"
#include "config_struct.h"
#include "debug.h"
#include <esp_sntp.h>
#include <sys/time.h>

/* ============================================================================
 * STATE
 * ============================================================================ */

static volatile bool     s_synced         = false;
static volatile uint32_t s_last_sync_ms   = 0;
static volatile uint32_t s_sync_count     = 0;
static volatile uint32_t s_error_count    = 0;
static bool              s_initialized    = false;

/* ============================================================================
 * SNTP CALLBACK
 * ============================================================================ */

static void ntp_sync_callback(struct timeval *tv)
{
  s_synced = true;
  s_last_sync_ms = millis();
  s_sync_count++;
  debug_println("[NTP] Time synchronized");
}

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

void ntp_driver_init(void)
{
  NtpConfig *cfg = &g_persist_config.ntp;

  if (!cfg->enabled) {
    debug_println("[NTP] Disabled in config");
    return;
  }

  if (cfg->server[0] == '\0') {
    debug_println("[NTP] No server configured");
    return;
  }

  // Set timezone first
  if (cfg->timezone[0] != '\0') {
    setenv("TZ", cfg->timezone, 1);
    tzset();
  }

  // Configure SNTP
  if (s_initialized) {
    esp_sntp_stop();
  }

  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, cfg->server);

  // Set sync interval (ESP-IDF default is 3600s = 1 hour)
  uint32_t interval_ms = (uint32_t)cfg->sync_interval_min * 60 * 1000;
  if (interval_ms < 60000) interval_ms = 60000;  // Minimum 1 minute
  esp_sntp_set_sync_interval(interval_ms);

  sntp_set_time_sync_notification_cb(ntp_sync_callback);

  esp_sntp_init();
  s_initialized = true;

  debug_print("[NTP] Started — server: ");
  debug_print(cfg->server);
  debug_print(", TZ: ");
  debug_print(cfg->timezone[0] ? cfg->timezone : "(UTC)");
  debug_print(", interval: ");
  debug_print_uint(cfg->sync_interval_min);
  debug_println(" min");
}

void ntp_driver_stop(void)
{
  if (s_initialized) {
    esp_sntp_stop();
    s_initialized = false;
    debug_println("[NTP] Stopped");
  }
}

void ntp_driver_reconfigure(void)
{
  ntp_driver_stop();
  s_synced = false;
  ntp_driver_init();
}

bool ntp_driver_is_synced(void)
{
  return s_synced;
}

bool ntp_driver_get_local_time(struct tm *timeinfo)
{
  if (!s_synced || !timeinfo) return false;
  time_t now;
  time(&now);
  localtime_r(&now, timeinfo);
  return (timeinfo->tm_year > (2020 - 1900));  // Sanity check
}

bool ntp_driver_get_time_str(char *buf, size_t bufsize)
{
  struct tm timeinfo;
  if (!ntp_driver_get_local_time(&timeinfo)) {
    snprintf(buf, bufsize, "Ikke synkroniseret");
    return false;
  }
  strftime(buf, bufsize, "%Y-%m-%d %H:%M:%S", &timeinfo);
  return true;
}

bool ntp_driver_get_iso_time(char *buf, size_t bufsize)
{
  struct tm timeinfo;
  if (!ntp_driver_get_local_time(&timeinfo)) {
    snprintf(buf, bufsize, "-");
    return false;
  }
  strftime(buf, bufsize, "%Y-%m-%dT%H:%M:%S", &timeinfo);
  return true;
}

time_t ntp_driver_get_epoch(void)
{
  if (!s_synced) return 0;
  return time(NULL);
}

uint32_t ntp_driver_get_last_sync_age_ms(void)
{
  if (!s_synced) return UINT32_MAX;
  return millis() - s_last_sync_ms;
}

uint32_t ntp_driver_get_sync_count(void)
{
  return s_sync_count;
}

uint32_t ntp_driver_get_error_count(void)
{
  return s_error_count;
}
