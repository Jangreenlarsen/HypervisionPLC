/**
 * @file st_wdt.cpp
 * @brief FEAT-427 lag B: konfigurerbar watchdog pr. ST-program (se st_wdt.h)
 */

#include "st_wdt.h"
#include "st_logic_engine.h"
#include "st_debug.h"
#include "watchdog_monitor.h"
#include "gpio_mapping.h"
#include "registers.h"
#include "config_struct.h"
#include "debug.h"
#include <Arduino.h>
#include <nvs.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

extern void alarm_raise(uint8_t severity, const char *msg);  // api_handlers.cpp

typedef struct __attribute__((packed)) {
  uint8_t version;
  st_wdt_cfg_t cfg[ST_LOGIC_MAX_PROGRAMS];
} st_wdt_blob_t;

static st_wdt_blob_t g_blob;
static st_wdt_rt_t   g_rt[ST_LOGIC_MAX_PROGRAMS];

void st_wdt_init(void) {
  memset(&g_blob, 0, sizeof(g_blob));
  g_blob.version = 1;
  memset(g_rt, 0, sizeof(g_rt));
  nvs_handle_t h;
  if (nvs_open("modbus_cfg", NVS_READONLY, &h) == ESP_OK) {
    st_wdt_blob_t b;
    size_t len = sizeof(b);
    if (nvs_get_blob(h, "st_wdt", &b, &len) == ESP_OK && len == sizeof(b) && b.version == 1) {
      g_blob = b;
    }
    nvs_close(h);
  }
}

bool st_wdt_save(void) {
  nvs_handle_t h;
  if (nvs_open("modbus_cfg", NVS_READWRITE, &h) != ESP_OK) return false;
  bool ok = nvs_set_blob(h, "st_wdt", &g_blob, sizeof(g_blob)) == ESP_OK && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}

st_wdt_cfg_t *st_wdt_cfg(uint8_t id) {
  return (id < ST_LOGIC_MAX_PROGRAMS) ? &g_blob.cfg[id] : NULL;
}

const st_wdt_rt_t *st_wdt_rt(uint8_t id) {
  return (id < ST_LOGIC_MAX_PROGRAMS) ? &g_rt[id] : NULL;
}

bool st_wdt_enabled(uint8_t id) {
  const st_wdt_cfg_t *c = st_wdt_cfg(id);
  return c && (c->err_limit || c->exec_us || c->heartbeat_ms || c->stall_ms);
}

const char *st_wdt_action_str(uint8_t a) {
  switch (a) {
    case ST_WDT_ACT_STOP:    return "stop";
    case ST_WDT_ACT_RESTART: return "restart";
    case ST_WDT_ACT_SAFE:    return "safe";
    case ST_WDT_ACT_REBOOT:  return "reboot";
    default:                 return "alarm";
  }
}

const char *st_wdt_reason_str(uint8_t r) {
  switch (r) {
    case ST_WDT_REASON_ERRORS:    return "fejl i traek";
    case ST_WDT_REASON_EXEC:      return "udfoerelsestid";
    case ST_WDT_REASON_HEARTBEAT: return "heartbeat (WDT_FEED) udeblev";
    case ST_WDT_REASON_STALL:     return "programmet koerer ikke";
    default:                      return "-";
  }
}

bool st_wdt_parse_action(const char *s, uint8_t *out) {
  static const char *names[] = {"alarm", "stop", "restart", "safe", "reboot"};
  for (uint8_t i = 0; i < 5; i++) {
    if (!strcasecmp(s, names[i])) { *out = i; return true; }
  }
  return false;
}

/* Kaldes fra baade loopTask (NORMAL) og HIGH-tasken — kun enkle felt-
 * opdateringer; selve evalueringen sker i st_wdt_loop(). */
void st_wdt_after_exec(uint8_t id, bool ok, uint32_t exec_us) {
  if (id >= ST_LOGIC_MAX_PROGRAMS) return;
  st_wdt_rt_t *r = &g_rt[id];
  uint32_t now = millis();
  r->last_exec_ms = now ? now : 1;
  if (r->last_feed_ms == 0) r->last_feed_ms = r->last_exec_ms;  // heartbeat-frist starter ved foerste koersel
  if (ok) {
    r->consec_err = 0;
  } else {
    r->last_err_ms = now ? now : 1;
    if (r->consec_err < 255) r->consec_err++;
  }
  const st_wdt_cfg_t *c = &g_blob.cfg[id];
  if (c->exec_us && exec_us > c->exec_us) {
    if (r->consec_exec < 255) r->consec_exec++;
  } else {
    r->consec_exec = 0;
  }
}

void st_wdt_feed(const void *bytecode) {
  st_logic_engine_state_t *st = st_logic_get_state();
  if (!st || !bytecode) return;
  for (uint8_t i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
    if ((const void *)&st->programs[i].bytecode == bytecode) {
      uint32_t now = millis();
      g_rt[i].last_feed_ms = now ? now : 1;
      return;
    }
  }
}

/* "safe": programmets udgange (output-bindinger til coils) saettes til deres
 * sikre tilstand — defineret pr. GPIO-udgang (gpio_mapping_safe_value), en
 * coil uden GPIO-udgang saettes OFF (beslutning 3). Programmet stoppes
 * bagefter, saa det ikke overskriver dem igen. */
static void st_wdt_apply_safe_outputs(uint8_t id) {
  for (uint8_t i = 0; i < g_persist_config.var_map_count; i++) {
    const VariableMapping *m = &g_persist_config.var_maps[i];
    if (m->source_type != MAPPING_SOURCE_ST_VAR || m->st_program_id != id) continue;
    if (m->is_input || m->output_type != 1 || m->output_reg == 0xFFFF) continue;  // kun output -> coil
    uint16_t coil = m->output_reg;
    uint8_t val = 0;
    for (uint8_t j = 0; j < g_persist_config.var_map_count; j++) {
      const VariableMapping *g = &g_persist_config.var_maps[j];
      if (g->source_type == MAPPING_SOURCE_GPIO && !g->is_input && g->output_reg == coil) {
        val = gpio_mapping_safe_value(g->gpio_pin);
        break;
      }
    }
    registers_set_coil(coil, val);
  }
}

static void st_wdt_trip(st_logic_engine_state_t *st, uint8_t id, uint8_t reason) {
  st_wdt_rt_t *r = &g_rt[id];
  const st_wdt_cfg_t *c = &g_blob.cfg[id];
  uint8_t action = c->action;
  if (action == ST_WDT_ACT_RESTART && r->restarts >= ST_WDT_MAX_RESTARTS) {
    action = ST_WDT_ACT_SAFE;  // eskalér: genstart hjalp ikke -> sikker tilstand + stop
  }

  char msg[96];
  snprintf(msg, sizeof(msg), "ST-watchdog Logic%u: %s -> %s", (unsigned)(id + 1),
           st_wdt_reason_str(reason), st_wdt_action_str(action));
  alarm_raise(2, msg);
  debug_println(msg);

  r->trip_count++;
  r->trip_ms = millis();
  r->reason = reason;
  st_logic_program_config_t *prog = &st->programs[id];

  switch (action) {
    case ST_WDT_ACT_RESTART:
      // Selvhelbredende: nulstil programmet og fortsaet, uden "tripped"
      r->restarts++;
      st_logic_reinit(st, id);
      r->consec_err = 0;
      r->consec_exec = 0;
      r->last_feed_ms = millis();
      r->last_exec_ms = 0;
      return;
    case ST_WDT_ACT_SAFE:
      st_wdt_apply_safe_outputs(id);
      // fall through: stop
    case ST_WDT_ACT_STOP:
      if (prog->enabled) {
        prog->enabled = 0;           // kun runtime — gemt config er uaendret
        prog->bytecode.enabled = 0;
        r->stopped_by_wdt = 1;
      }
      break;
    case ST_WDT_ACT_REBOOT:
      watchdog_reboot_for(msg);       // taeller mod boot-loop-beskyttelsen
      break;
    default:
      break;
  }
  r->tripped = 1;
}

void st_wdt_loop(void) {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < 100) return;
  last = now;
  if (watchdog_safe_mode()) return;   // ST koerer ikke i safe mode

  st_logic_engine_state_t *st = st_logic_get_state();
  if (!st) return;
  for (uint8_t id = 0; id < ST_LOGIC_MAX_PROGRAMS; id++) {
    const st_wdt_cfg_t *c = &g_blob.cfg[id];
    st_wdt_rt_t *r = &g_rt[id];
    if (r->tripped || !st_wdt_enabled(id)) continue;
    // 10 min uden trip og uden runtime-fejl -> tidligere restarts glemmes,
    // saa kun genstarter taet efter hinanden eskalerer
    if (r->restarts && (now - r->trip_ms) >= ST_WDT_RESTART_HEAL_MS &&
        (!r->last_err_ms || (now - r->last_err_ms) >= ST_WDT_RESTART_HEAL_MS)) {
      r->restarts = 0;
    }
    st_logic_program_config_t *prog = &st->programs[id];
    if (!st->enabled || !prog->enabled || !prog->compiled) continue;
    if (st->debugger[id].mode == ST_DEBUG_PAUSED) continue;  // bevidst stoppet i debuggeren

    uint8_t reason = ST_WDT_REASON_NONE;
    if (c->err_limit && r->consec_err >= c->err_limit) reason = ST_WDT_REASON_ERRORS;
    else if (c->exec_us && r->consec_exec >= 3) reason = ST_WDT_REASON_EXEC;
    else if (c->heartbeat_ms && r->last_feed_ms && (int32_t)(now - r->last_feed_ms) > (int32_t)c->heartbeat_ms)
      reason = ST_WDT_REASON_HEARTBEAT;
    else if (c->stall_ms && r->last_exec_ms && (int32_t)(now - r->last_exec_ms) > (int32_t)c->stall_ms)
      reason = ST_WDT_REASON_STALL;

    if (reason != ST_WDT_REASON_NONE) st_wdt_trip(st, id, reason);
  }
}

void st_wdt_clear(uint8_t id) {
  if (id >= ST_LOGIC_MAX_PROGRAMS) return;
  st_logic_engine_state_t *st = st_logic_get_state();
  st_wdt_rt_t *r = &g_rt[id];
  if (r->stopped_by_wdt && st) {
    st->programs[id].enabled = 1;
    st->programs[id].bytecode.enabled = 1;
  }
  uint16_t trips = r->trip_count;
  memset(r, 0, sizeof(*r));
  r->trip_count = trips;
  r->last_feed_ms = millis();
}

/* ============================================================================
 * CLI
 * ============================================================================ */

static void st_wdt_print_usage(void) {
  debug_println("Brug: set logic <id> wdt errors <n>|off        (runtime-fejl i traek)");
  debug_println("      set logic <id> wdt exec <us>|off         (udfoerelsestid, 3 gange i traek)");
  debug_println("      set logic <id> wdt heartbeat <ms>|off    (programmet skal kalde WDT_FEED())");
  debug_println("      set logic <id> wdt stall <ms>|off        (programmet koerer ikke)");
  debug_println("      set logic <id> wdt action alarm|stop|restart|safe|reboot");
  debug_println("      show logic <id> wdt  /  clear logic <id> wdt");
}

void st_wdt_cli_set(uint8_t id, int argc, char **argv) {
  st_wdt_cfg_t *c = st_wdt_cfg(id);
  if (!c || argc < 2) { st_wdt_print_usage(); return; }
  const char *param = argv[0];
  const char *val = argv[1];
  bool off = !strcasecmp(val, "off");
  long n = off ? 0 : atol(val);
  if (!strcasecmp(param, "action")) {
    uint8_t a;
    if (!st_wdt_parse_action(val, &a)) { st_wdt_print_usage(); return; }
    c->action = a;
  } else if (!strcasecmp(param, "errors")) {
    if (n < 0 || n > 255) { debug_println("FEJL: errors skal vaere 1-255 eller off"); return; }
    c->err_limit = (uint8_t)n;
  } else if (!strcasecmp(param, "exec")) {
    if (n < 0) { debug_println("FEJL: exec i mikrosekunder, eller off"); return; }
    c->exec_us = (uint32_t)n;
  } else if (!strcasecmp(param, "heartbeat")) {
    if (n != 0 && n < 100) { debug_println("FEJL: heartbeat mindst 100 ms, eller off"); return; }
    c->heartbeat_ms = (uint32_t)n;
  } else if (!strcasecmp(param, "stall")) {
    if (n != 0 && n < 100) { debug_println("FEJL: stall mindst 100 ms, eller off"); return; }
    c->stall_ms = (uint32_t)n;
  } else {
    st_wdt_print_usage();
    return;
  }
  st_wdt_save();
  debug_printf("Logic%u watchdog: %s = %s (gemt)\n", (unsigned)(id + 1), param, off ? "off" : val);
}

void st_wdt_cli_show(uint8_t id) {
  const st_wdt_cfg_t *c = st_wdt_cfg(id);
  const st_wdt_rt_t *r = st_wdt_rt(id);
  if (!c || !r) return;
  uint32_t now = millis();
  debug_printf("=== ST-watchdog Logic%u ===\n", (unsigned)(id + 1));
  if (!st_wdt_enabled(id)) debug_println("  (ingen betingelser slaaet til)");
  debug_printf("  errors    : %s%u\n", c->err_limit ? "" : "off ", (unsigned)c->err_limit);
  debug_printf("  exec      : %s%lu us (3 i traek)\n", c->exec_us ? "" : "off ", (unsigned long)c->exec_us);
  debug_printf("  heartbeat : %s%lu ms (WDT_FEED)\n", c->heartbeat_ms ? "" : "off ", (unsigned long)c->heartbeat_ms);
  debug_printf("  stall     : %s%lu ms\n", c->stall_ms ? "" : "off ", (unsigned long)c->stall_ms);
  debug_printf("  action    : %s\n", st_wdt_action_str(c->action));
  debug_printf("  Status    : %s\n", r->tripped ? "*** UDLOEST ***" : "OK");
  if (r->tripped) {
    debug_printf("  Aarsag    : %s (for %lu s siden)%s\n", st_wdt_reason_str(r->reason),
                 (unsigned long)((now - r->trip_ms) / 1000), r->stopped_by_wdt ? " — program stoppet" : "");
  }
  debug_printf("  Fejl i traek: %u, tids-overskridelser i traek: %u, restarts: %u, udloest i alt: %u\n",
               (unsigned)r->consec_err, (unsigned)r->consec_exec, (unsigned)r->restarts, (unsigned)r->trip_count);
  if (r->last_feed_ms) debug_printf("  Sidste WDT_FEED: %lu ms siden\n", (unsigned long)(now - r->last_feed_ms));
  if (r->last_exec_ms) debug_printf("  Sidste udfoerelse: %lu ms siden\n", (unsigned long)(now - r->last_exec_ms));
}
