/**
 * @file system_log.h
 * @brief Delt haendelses- og registerændringslog (FEAT-086 + FEAT-089)
 *
 * Ét modul, to kategorier — bevidst delt fremfor to separate ringbuffere
 * (som alarmlog og Modbus-aktivitetslog er) for at holde flash-forbruget
 * nede (se BUGS_INDEX.md FEAT-086/089 for begrundelsen).
 *
 * - SYSLOG_CAT_EVENT: systemhaendelser (config-gem, reboot, login, OTA)
 * - SYSLOG_CAT_REG_CHANGE: hvem aendrede hvilket register/coil, hvornaar
 *   (KUN REST + Modbus Slave (ekstern master) i v1 — se BUGS_INDEX for
 *   hvorfor CLI og ST Logic's periodiske output-binding er udenfor)
 *
 * RAM-only (PSRAM naar tilgaengelig), nulstilles ved reboot — samme
 * arkitektur som mb_activity_log.h (FEAT-149/153).
 */

#ifndef SYSTEM_LOG_H
#define SYSTEM_LOG_H

#include <Arduino.h>

#define SYSTEM_LOG_MAX  200  // RAM-only ring buffer size

typedef enum {
  SYSLOG_CAT_EVENT      = 0,  // Config-gem, reboot, login, OTA, config-anvendt
  SYSLOG_CAT_REG_CHANGE = 1   // Register/coil aendret af en ekstern aktoer
} syslog_category_t;

typedef enum {
  SYSLOG_SRC_REST         = 0,  // REST API (kendt bruger + IP)
  SYSLOG_SRC_MODBUS_SLAVE = 1,  // Ekstern Modbus master (RTU-bus, ingen finere identitet)
  SYSLOG_SRC_SYSTEM       = 2   // Firmwaren selv (boot, watchdog, o.lign.)
} syslog_source_t;

typedef struct {
  uint32_t timestamp_ms;   // millis() ved logning
  uint32_t epoch_s;        // Unix-tid hvis NTP synkroniseret, 0 ellers (FEAT-153-moenster)
  uint8_t  category;       // syslog_category_t
  uint8_t  source;         // syslog_source_t
  char     username[24];   // RBAC-brugernavn ved REST, "-" ellers
  char     ip[16];         // Klient-IP ved REST, "-" ellers
  uint16_t reg_addr;       // Kun REG_CHANGE — 0xFFFF = ikke relevant (EVENT)
  bool     is_coil;        // Kun REG_CHANGE — true=coil, false=holding register
  int32_t  old_value;      // Kun REG_CHANGE
  int32_t  new_value;      // Kun REG_CHANGE
  char     message[48];    // Fritekst, fx "Config gemt til NVS" eller "REST bulk write (12 registre)"
} syslog_entry_t;

void system_log_init(void);

/**
 * @brief Log et systemhaendelse (SYSLOG_CAT_EVENT).
 * @param source syslog_source_t
 * @param username RBAC-brugernavn, eller NULL/"" hvis ukendt/n.a.
 * @param ip Klient-IP, eller NULL/"" hvis ukendt/n.a.
 * @param message Fritekst-beskrivelse (afkortes til 47 tegn)
 */
void system_log_add_event(uint8_t source, const char *username, const char *ip, const char *message);

/**
 * @brief Log en register-/coil-aendring (SYSLOG_CAT_REG_CHANGE). Kald KUN
 * naar old_value != new_value er allerede bekraeftet af kalderen — dette
 * modul dedupliker ikke selv, for at holde choke-point'et simpelt.
 */
void system_log_add_reg_change(uint8_t source, const char *username, const char *ip,
                                uint16_t reg_addr, bool is_coil,
                                int32_t old_value, int32_t new_value);

void system_log_clear(void);
void system_log_set_enabled(bool enabled);
bool system_log_is_enabled(void);
uint16_t system_log_count(void);
bool system_log_get(uint16_t idx, syslog_entry_t *out);

#endif // SYSTEM_LOG_H
