// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file st_wdt.h
 * @brief FEAT-427 lag B: konfigurerbar watchdog pr. ST-program
 *
 * Hvert af de 4 programmer kan overvaages paa fire uafhaengige betingelser
 * (0 = fra) og har EN handling, der udfoeres naar en af dem udloeses:
 *
 *   errors <n>         n runtime-fejl i traek (fanger ogsaa uendelige loekker,
 *                      som afbrydes af max-steps og taeller som fejl)
 *   exec <us>          udfoerelsestid over graensen 3 gange i traek
 *   heartbeat <ms>     programmet skal kalde WDT_FEED() inden for <ms>
 *   stall <ms>         programmet er ikke blevet udfoert i <ms>
 *
 *   action alarm|stop|restart|safe|reboot   (standard: alarm)
 *
 * En udloest watchdog staar som "tripped", til den kvitteres (clear).
 * Konfigurationen gemmes i egen NVS-noegle ("st_wdt"), saa PersistConfig's
 * layout er uaendret. Overvaagningen evalueres fra hovedloekken
 * (st_wdt_loop), aldrig fra selve ST-udfoerelsen.
 */

#ifndef ST_WDT_H
#define ST_WDT_H

#include <stdint.h>
#include <stdbool.h>
#include "st_logic_config.h"

typedef enum {
  ST_WDT_ACT_ALARM   = 0,
  ST_WDT_ACT_STOP    = 1,
  ST_WDT_ACT_RESTART = 2,
  ST_WDT_ACT_SAFE    = 3,
  ST_WDT_ACT_REBOOT  = 4
} st_wdt_action_t;

typedef enum {
  ST_WDT_REASON_NONE      = 0,
  ST_WDT_REASON_ERRORS    = 1,
  ST_WDT_REASON_EXEC      = 2,
  ST_WDT_REASON_HEARTBEAT = 3,
  ST_WDT_REASON_STALL     = 4
} st_wdt_reason_t;

typedef struct __attribute__((packed)) {
  uint8_t  err_limit;     // 0 = fra
  uint8_t  action;        // st_wdt_action_t
  uint16_t reserved;
  uint32_t exec_us;       // 0 = fra
  uint32_t heartbeat_ms;  // 0 = fra
  uint32_t stall_ms;      // 0 = fra
} st_wdt_cfg_t;

typedef struct {
  uint8_t  tripped;       // 1 = udloest, venter paa kvittering
  uint8_t  reason;        // st_wdt_reason_t
  uint8_t  stopped_by_wdt;// programmet blev stoppet af watchdog'en (clear genstarter det)
  uint8_t  restarts;      // antal "restart"-handlinger siden sidste clear
  uint8_t  consec_err;
  uint8_t  consec_exec;
  uint16_t trip_count;
  uint32_t last_feed_ms;  // 0 = ikke startet endnu
  uint32_t last_exec_ms;  // 0 = ikke udfoert endnu
  uint32_t trip_ms;
  uint32_t last_err_ms;   // seneste runtime-fejl (0 = ingen)
} st_wdt_rt_t;

#define ST_WDT_MAX_RESTARTS   3   // efter 3 restart-handlinger eskaleres til safe
#define ST_WDT_RESTART_HEAL_MS 600000UL  // 10 min fejlfri drift nulstiller restart-taelleren

void st_wdt_init(void);
st_wdt_cfg_t *st_wdt_cfg(uint8_t prog_id);
const st_wdt_rt_t *st_wdt_rt(uint8_t prog_id);
bool st_wdt_save(void);
bool st_wdt_enabled(uint8_t prog_id);   // mindst en betingelse slaaet til

/* Kaldes af st_logic_execute_program() efter hver udfoerelse (begge tasks). */
void st_wdt_after_exec(uint8_t prog_id, bool ok, uint32_t exec_us);
/* WDT_FEED() fra ST — programmet findes via sin bytecode-pointer. */
void st_wdt_feed(const void *bytecode);
/* Evaluering + handlinger. Fra loop(); selv rate-begraenset til 100 ms. */
void st_wdt_loop(void);
/* Kvittering: nulstil tilstand, genstart et program watchdog'en stoppede. */
void st_wdt_clear(uint8_t prog_id);

const char *st_wdt_action_str(uint8_t a);
const char *st_wdt_reason_str(uint8_t r);
bool st_wdt_parse_action(const char *s, uint8_t *out);

/* CLI (prog_id 0-baseret): set logic <id> wdt <param> <vaerdi> / show logic <id> wdt */
void st_wdt_cli_set(uint8_t prog_id, int argc, char **argv);
void st_wdt_cli_show(uint8_t prog_id);

#endif // ST_WDT_H
