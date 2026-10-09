/**
 * @file api_ext_registers.cpp
 * @brief FEAT-486: REST-adgang til eksterne Modbus-registre — se
 *        api_ext_registers.h for URL-skemaet.
 *
 * Design:
 *  - Læsning returnerer cache-værdien og sætter en opfriskning i kø, hvis
 *    posten mangler eller er ældre end max_age (standard 1000 ms) og ikke
 *    allerede venter. Med ?wait=<ms> (max 2000) venter kaldet på svaret,
 *    så det opfører sig som et internt register. Uden wait er første læsning
 *    af en ny adresse "pending" (value null) — en SCADA, der poller, har
 *    værdien fra næste kald.
 *  - Skrivning sættes i kø (samme vej som ST's MB_WRITE_- og MBX_WRITE_-funktioner);
 *    med ?wait returneres slavens resultat (ok/error), ellers "queued".
 *  - httpd-tasken rører aldrig bussen selv — kun kø og cache.
 *  - Cachen er delt med ST (RTU 32, MBX 48 poster, LRU) — derfor max 16
 *    registre pr. kald.
 */

#include "api_ext_registers.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>
#include <stdlib.h>

#include "api_handlers.h"
#include "config_struct.h"
#include "expansion_api_client.h"
#include "mb_async.h"
#include "modbus_expansion_async.h"
#include "modbus_master.h"
#include "system_log.h"

#define EXT_MAX_COUNT     16
#define EXT_MAX_WAIT_MS   2000
#define EXT_DEFAULT_AGE   1000

enum ext_kind_t { EXT_COILS = 1, EXT_DI = 2, EXT_HR = 3, EXT_IR = 4 };  // = FC01-04 = *_REQ_READ_*
enum ext_fmt_t { FMT_UINT, FMT_INT, FMT_DINT, FMT_DWORD, FMT_REAL };

typedef struct {
  bool mbx;
  uint8_t board, channel, slave;
  uint8_t kind;      // ext_kind_t
  uint16_t addr;
} ext_target_t;

typedef struct {
  bool exists;
  uint8_t status;    // 0 empty, 1 pending, 2 valid, 3 error (begge caches bruger samme numre)
  uint16_t raw;      // HR/IR: 16 bit; coils/DI: 0/1
  uint32_t updated;  // last_update_ms (0 = aldrig)
  int32_t err;       // last_error (MB_OK = 0) — BUG-489: afgør om sidste svar var gyldigt
} ext_snap_t;

static const char *ext_kind_name(uint8_t k) {
  switch (k) { case EXT_COILS: return "coils"; case EXT_DI: return "di"; case EXT_HR: return "hr"; default: return "ir"; }
}

// "/api/ext/rtu/1/hr/10?…" eller "/api/ext/mbx/1/D/1/coils/0?…"
static const char *ext_parse_target(httpd_req_t *req, ext_target_t *t) {
  memset(t, 0, sizeof(*t));
  const char *p = req->uri + strlen("/api/ext/");
  char seg[6][12];
  int n = 0;
  while (*p && *p != '?' && n < 6) {
    size_t len = strcspn(p, "/?");
    if (len == 0 || len >= sizeof(seg[0])) return "Ugyldig sti";
    memcpy(seg[n], p, len);
    seg[n][len] = '\0';
    n++;
    p += len;
    if (*p == '/') p++;
  }
  if (*p && *p != '?') return "Ugyldig sti";
  if (n < 1) return "Ugyldig sti";
  int i;
  if (strcmp(seg[0], "rtu") == 0 && n == 4) {
    t->mbx = false;
    i = 1;
  } else if (strcmp(seg[0], "mbx") == 0 && n == 6) {
    t->mbx = true;
    long b = strtol(seg[1], NULL, 10);
    if (b < 1 || b > EXPANSION_BOARD_MAX) return "Ugyldigt board (1-8)";
    char c = seg[2][0];
    long ch = (c >= 'A' && c <= 'H') ? c - 'A' + 1 : (c >= 'a' && c <= 'h') ? c - 'a' + 1 : strtol(seg[2], NULL, 10);
    if (ch < 1 || ch > 8 || seg[2][1] != '\0') return "Ugyldig kanal (A-H eller 1-8)";
    t->board = (uint8_t)b;
    t->channel = (uint8_t)ch;
    i = 3;
  } else {
    return "Brug /api/ext/rtu/{slave}/{type}/{addr} eller /api/ext/mbx/{board}/{kanal}/{slave}/{type}/{addr}";
  }
  char *end = NULL;
  long s = strtol(seg[i], &end, 10);
  if (*end || s < 1 || s > 247) return "Ugyldig slave (1-247)";
  t->slave = (uint8_t)s;
  const char *k = seg[i + 1];
  if (!strcmp(k, "hr")) t->kind = EXT_HR;
  else if (!strcmp(k, "ir")) t->kind = EXT_IR;
  else if (!strcmp(k, "coils") || !strcmp(k, "coil")) t->kind = EXT_COILS;
  else if (!strcmp(k, "di")) t->kind = EXT_DI;
  else return "Ugyldig type (hr, ir, coils, di)";
  long a = strtol(seg[i + 2], &end, 10);
  if (*end || a < 0 || a > 65535) return "Ugyldig adresse (0-65535)";
  t->addr = (uint16_t)a;
  return NULL;
}

// Kan målet overhovedet nås? Returnerer HTTP-status (0 = ok) + besked.
static int ext_check_reachable(const ext_target_t *t, const char **msg) {
  if (!t->mbx) {
    if (!g_modbus_master_config.enabled) { *msg = "Modbus Master (RS485) er ikke aktiveret"; return 409; }
    return 0;
  }
  if (!g_persist_config.expansion_boards[t->board - 1].configured) { *msg = "Board ikke konfigureret"; return 404; }
  uint8_t nch = expansion_board_active_channels((uint8_t)(t->board - 1));
  if (nch > 0 && t->channel > nch) { *msg = "Boardet har ikke den kanal"; return 400; }  // BUG-478
  return 0;
}

static void ext_snapshot(const ext_target_t *t, uint16_t addr, ext_snap_t *s) {
  memset(s, 0, sizeof(*s));
  const bool bit = (t->kind == EXT_COILS || t->kind == EXT_DI);
  if (!t->mbx) {
    mb_cache_entry_t *e = mb_cache_find(t->slave, addr, t->kind);
    if (!e) return;
    portENTER_CRITICAL(&mb_cache_spinlock);
    s->exists = true; s->status = (uint8_t)e->status; s->updated = e->last_update_ms; s->err = e->last_error;
    s->raw = bit ? (e->value.bool_val ? 1 : 0) : (uint16_t)e->value.int_val;
    portEXIT_CRITICAL(&mb_cache_spinlock);
  } else {
    mbx_cache_entry_t *e = mbx_cache_find(t->board, t->channel, t->slave, addr, t->kind);
    if (!e) return;
    portENTER_CRITICAL(&mbx_cache_spinlock);
    s->exists = true; s->status = (uint8_t)e->status; s->updated = e->last_update_ms; s->err = e->last_error;
    s->raw = bit ? (e->value.bool_val ? 1 : 0) : (uint16_t)e->value.int_val;
    portEXIT_CRITICAL(&mbx_cache_spinlock);
  }
}

static bool ext_is_stale(const ext_snap_t *s, uint32_t now, uint32_t max_age) {
  if (!s->exists) return true;
  if (s->status == 1) return false;  // venter allerede
  return s->updated == 0 || (now - s->updated) >= max_age;
}

// Færdig = ikke længere pending og opdateret efter t0 (eller frisk nok fra start)
static bool ext_done(const ext_snap_t *s, uint32_t t0, bool queued) {
  if (!s->exists || s->status == 1) return false;
  if (!queued) return true;
  return s->updated != 0 && (int32_t)(s->updated - t0) >= 0;
}

// BUG-489: en post kan staa som VALID/PENDING med en fejl som sidste resultat
// (stale-PENDING-oprydningen saetter VALID + last_error=TIMEOUT, og en ny
// laesning markerer PENDING oven paa et fejlsvar) — saa er vaerdien ikke gyldig
static bool ext_ok(const ext_snap_t *s) {
  return s->exists && s->updated != 0 && s->status != 3 && s->err == 0;
}

static const char *ext_status_name(const ext_snap_t *s) {
  if (!s->exists) return "pending";
  if (s->updated != 0 && s->err != 0) return "error";
  switch (s->status) {
    case 2: return "ok";
    case 3: return "error";
    case 1: return s->updated ? "ok" : "pending";  // FEAT-447-princippet: opdatering ≠ "venter"
    default: return "pending";
  }
}

static int ext_query_int(httpd_req_t *req, const char *key, int def) {
  char q[160], v[16];
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) return def;
  if (httpd_query_key_value(q, key, v, sizeof(v)) != ESP_OK) return def;
  return atoi(v);
}

static ext_fmt_t ext_query_fmt(httpd_req_t *req, bool *ok) {
  char q[160], v[8];
  *ok = true;
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK || httpd_query_key_value(q, "type", v, sizeof(v)) != ESP_OK)
    return FMT_UINT;
  if (!strcmp(v, "uint")) return FMT_UINT;
  if (!strcmp(v, "int")) return FMT_INT;
  if (!strcmp(v, "dint")) return FMT_DINT;
  if (!strcmp(v, "dword")) return FMT_DWORD;
  if (!strcmp(v, "real")) return FMT_REAL;
  *ok = false;
  return FMT_UINT;
}

static int ext_print_target(char *out, size_t cap, const ext_target_t *t) {
  if (t->mbx)
    return snprintf(out, cap, "{\"src\":\"mbx\",\"board\":%u,\"channel\":\"%c\",\"slave\":%u,\"type\":\"%s\",\"address\":%u",
                    t->board, 'A' + t->channel - 1, t->slave, ext_kind_name(t->kind), t->addr);
  return snprintf(out, cap, "{\"src\":\"rtu\",\"slave\":%u,\"type\":\"%s\",\"address\":%u", t->slave,
                  ext_kind_name(t->kind), t->addr);
}

static void ext_wait(const ext_target_t *t, uint16_t nregs, const bool *queued, uint32_t t0, int wait_ms) {
  if (wait_ms <= 0) return;
  while ((int)(millis() - t0) < wait_ms) {
    bool all = true;
    for (uint16_t i = 0; i < nregs && all; i++) {
      ext_snap_t s;
      ext_snapshot(t, t->addr + i, &s);
      if (!ext_done(&s, t0, queued[i])) all = false;
    }
    if (all) return;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

/* ============================================================================
 * GET
 * ============================================================================ */
esp_err_t api_ext_registers_get(httpd_req_t *req) {
  ext_target_t t;
  const char *err = ext_parse_target(req, &t);
  if (err) return api_send_error(req, 400, err);
  int code = ext_check_reachable(&t, &err);
  if (code) return api_send_error(req, code, err);

  bool fmt_ok;
  ext_fmt_t fmt = ext_query_fmt(req, &fmt_ok);
  if (!fmt_ok) return api_send_error(req, 400, "Ugyldig type-parameter (uint, int, dint, dword, real)");
  const bool bit = (t.kind == EXT_COILS || t.kind == EXT_DI);
  if (bit) fmt = FMT_UINT;
  const uint8_t width = (fmt == FMT_DINT || fmt == FMT_DWORD || fmt == FMT_REAL) ? 2 : 1;
  int count = ext_query_int(req, "count", 1);
  if (count < 1 || count * width > EXT_MAX_COUNT) return api_send_error(req, 400, "count: 1-16 registre pr. kald");
  const uint16_t nregs = (uint16_t)(count * width);
  if ((uint32_t)t.addr + nregs > 65536) return api_send_error(req, 400, "Adresse + antal over 65535");
  int wait_ms = ext_query_int(req, "wait", 0);
  if (wait_ms < 0) wait_ms = 0;
  if (wait_ms > EXT_MAX_WAIT_MS) wait_ms = EXT_MAX_WAIT_MS;
  int max_age = ext_query_int(req, "max_age", EXT_DEFAULT_AGE);
  if (max_age < 0) max_age = 0;

  // Opfrisk det, der mangler/er forældet
  const uint32_t t0 = millis();
  bool queued[EXT_MAX_COUNT] = {false};
  bool any_stale = false;
  for (uint16_t i = 0; i < nregs; i++) {
    ext_snap_t s;
    ext_snapshot(&t, t.addr + i, &s);
    queued[i] = ext_is_stale(&s, t0, (uint32_t)max_age);
    any_stale |= queued[i];
  }
  bool queue_full = false;
  if (any_stale) {
    if (t.kind == EXT_HR && nregs > 1) {  // ét FC03 med flere registre
      bool ok = t.mbx ? modbus_expansion_async_queue_read_multi_holdings(t.board, t.channel, t.slave, t.addr, (uint8_t)nregs)
                      : mb_async_queue_read_multi(t.slave, t.addr, (uint8_t)nregs);
      queue_full = !ok;
      if (ok) for (uint16_t i = 0; i < nregs; i++) queued[i] = true;
    } else {
      for (uint16_t i = 0; i < nregs; i++) {
        if (!queued[i]) continue;
        bool ok = t.mbx ? modbus_expansion_async_queue_read((mbx_request_type_t)t.kind, t.board, t.channel, t.slave, t.addr + i)
                        : mb_async_queue_read((mb_request_type_t)t.kind, t.slave, t.addr + i);
        if (!ok) { queue_full = true; queued[i] = false; }
      }
    }
  }
  ext_wait(&t, nregs, queued, t0, wait_ms);

  // Svar: "value"/"status"/"age_ms" for første værdi (samme form som
  // /api/registers/...), plus "values"/"states"/"ages" når count > 1.
  char vals[EXT_MAX_COUNT][16];
  const char *states[EXT_MAX_COUNT];
  long ages[EXT_MAX_COUNT];
  const uint32_t now = millis();
  for (int v = 0; v < count; v++) {
    ext_snap_t a, b;
    ext_snapshot(&t, t.addr + v * width, &a);
    if (width == 2) ext_snapshot(&t, t.addr + v * width + 1, &b);
    const bool valid = ext_ok(&a) && (width == 1 || ext_ok(&b));  // BUG-489
    if (!valid) {
      strcpy(vals[v], "null");
    } else if (bit) {
      strcpy(vals[v], a.raw ? "true" : "false");
    } else if (fmt == FMT_UINT) {
      snprintf(vals[v], sizeof(vals[v]), "%u", (unsigned)a.raw);
    } else if (fmt == FMT_INT) {
      snprintf(vals[v], sizeof(vals[v]), "%d", (int)(int16_t)a.raw);
    } else {
      uint32_t u = ((uint32_t)a.raw << 16) | b.raw;  // high word først (som /api/registers/hr)
      if (fmt == FMT_DINT) snprintf(vals[v], sizeof(vals[v]), "%ld", (long)(int32_t)u);
      else if (fmt == FMT_DWORD) snprintf(vals[v], sizeof(vals[v]), "%lu", (unsigned long)u);
      else { float f; memcpy(&f, &u, 4); snprintf(vals[v], sizeof(vals[v]), "%g", (double)f); }
    }
    states[v] = ext_status_name(&a);
    if (width == 2 && !strcmp(states[v], "ok")) states[v] = ext_status_name(&b);
    ages[v] = a.updated ? (long)(now - a.updated) : -1L;
  }

  static const char *const fmt_names[] = {"uint", "int", "dint", "dword", "real"};
  const size_t cap = 256 + (size_t)count * 48;
  char *out = (char *)malloc(cap);
  if (!out) return api_send_error(req, 500, "Out of memory");
  int pos = ext_print_target(out, cap, &t);
  pos += snprintf(out + pos, cap - pos, ",\"format\":\"%s\",\"value\":%s,\"status\":\"%s\",\"age_ms\":%ld",
                  bit ? "bool" : fmt_names[fmt], vals[0], states[0], ages[0]);
  if (count > 1) {
    pos += snprintf(out + pos, cap - pos, ",\"count\":%d,\"values\":[", count);
    for (int v = 0; v < count; v++) pos += snprintf(out + pos, cap - pos, "%s%s", v ? "," : "", vals[v]);
    pos += snprintf(out + pos, cap - pos, "],\"states\":[");
    for (int v = 0; v < count; v++) pos += snprintf(out + pos, cap - pos, "%s\"%s\"", v ? "," : "", states[v]);
    pos += snprintf(out + pos, cap - pos, "],\"ages_ms\":[");
    for (int v = 0; v < count; v++) pos += snprintf(out + pos, cap - pos, "%s%ld", v ? "," : "", ages[v]);
    pos += snprintf(out + pos, cap - pos, "]");
  }
  snprintf(out + pos, cap - pos, "%s}", queue_full ? ",\"queue_full\":true" : "");
  esp_err_t r = api_send_json(req, out);
  free(out);
  return r;
}

/* ============================================================================
 * POST
 * ============================================================================ */
esp_err_t api_ext_registers_post(httpd_req_t *req, const char *user, const char *ip) {
  ext_target_t t;
  const char *err = ext_parse_target(req, &t);
  if (err) return api_send_error(req, 400, err);
  if (t.kind == EXT_IR || t.kind == EXT_DI) return api_send_error(req, 405, "Input registers og discrete inputs kan kun læses");
  int code = ext_check_reachable(&t, &err);
  if (code) return api_send_error(req, code, err);
  int wait_ms = ext_query_int(req, "wait", 0);
  if (wait_ms < 0) wait_ms = 0;
  if (wait_ms > EXT_MAX_WAIT_MS) wait_ms = EXT_MAX_WAIT_MS;

  char body[512];
  int ret = httpd_req_recv(req, body, sizeof(body) - 1);
  if (ret <= 0) return api_send_error(req, 400, "Failed to read request body");
  body[ret] = '\0';
  JsonDocument doc;
  if (deserializeJson(doc, body)) return api_send_error(req, 400, "Invalid JSON");

  uint16_t regs[EXT_MAX_COUNT];
  bool bits[EXT_MAX_COUNT];
  uint16_t n = 0;
  const char *type_str = doc["type"] | "uint";

  if (doc["values"].is<JsonArray>()) {
    JsonArray arr = doc["values"].as<JsonArray>();
    if (arr.size() < 1 || arr.size() > EXT_MAX_COUNT) return api_send_error(req, 400, "values: 1-16 elementer");
    for (JsonVariant v : arr) {
      if (t.kind == EXT_COILS) bits[n] = v.is<bool>() ? v.as<bool>() : (v.as<int>() != 0);
      else regs[n] = !strcmp(type_str, "int") ? (uint16_t)v.as<int16_t>() : v.as<uint16_t>();
      n++;
    }
  } else if (!doc["value"].isNull()) {
    JsonVariant v = doc["value"];
    if (t.kind == EXT_COILS) {
      bits[0] = v.is<bool>() ? v.as<bool>() : (v.as<int>() != 0);
      n = 1;
    } else if (!strcmp(type_str, "uint") || !strcmp(type_str, "int")) {
      regs[0] = !strcmp(type_str, "int") ? (uint16_t)v.as<int16_t>() : v.as<uint16_t>();
      n = 1;
    } else {
      uint32_t u;
      if (!strcmp(type_str, "dint")) u = (uint32_t)v.as<int32_t>();
      else if (!strcmp(type_str, "dword")) u = v.as<uint32_t>();
      else if (!strcmp(type_str, "real")) { float f = v.as<float>(); memcpy(&u, &f, 4); }
      else return api_send_error(req, 400, "Invalid type (use: uint, int, dint, dword, real)");
      regs[0] = (uint16_t)(u >> 16);  // high word først
      regs[1] = (uint16_t)(u & 0xFFFF);
      n = 2;
    }
  } else {
    return api_send_error(req, 400, "Missing 'value' or 'values' field");
  }
  if ((uint32_t)t.addr + n > 65536) return api_send_error(req, 400, "Adresse + antal over 65535");

  const uint32_t t0 = millis();
  bool ok;
  if (t.kind == EXT_COILS) {
    if (n == 1) {
      st_value_t sv; memset(&sv, 0, sizeof(sv)); sv.bool_val = bits[0];
      ok = t.mbx ? modbus_expansion_async_queue_write(MBX_REQ_WRITE_COIL, t.board, t.channel, t.slave, t.addr, sv, true)
                 : mb_async_queue_write(MB_REQ_WRITE_COIL, t.slave, t.addr, sv, true);
    } else {
      ok = t.mbx ? modbus_expansion_async_queue_write_multi_coils(t.board, t.channel, t.slave, t.addr, (uint8_t)n, bits)
                 : mb_async_queue_write_multi_coils(t.slave, t.addr, (uint8_t)n, bits);
    }
  } else {
    if (n == 1) {
      st_value_t sv; memset(&sv, 0, sizeof(sv)); sv.int_val = (int16_t)regs[0];
      ok = t.mbx ? modbus_expansion_async_queue_write(MBX_REQ_WRITE_HOLDING, t.board, t.channel, t.slave, t.addr, sv, true)
                 : mb_async_queue_write(MB_REQ_WRITE_HOLDING, t.slave, t.addr, sv, true);
    } else {
      ok = t.mbx ? modbus_expansion_async_queue_write_multi_holdings(t.board, t.channel, t.slave, t.addr, (uint8_t)n, regs)
                 : mb_async_queue_write_multi(t.slave, t.addr, (uint8_t)n, regs);
    }
  }
  if (!ok) return api_send_error(req, 503, "Modbus-koeen er fuld - proev igen");

  {
    char msg[96];
    snprintf(msg, sizeof(msg), "Ekstern skrivning %s%u%s%c slave %u %s %u (%u stk.)", t.mbx ? "board " : "RS485",
             t.mbx ? t.board : 0, t.mbx ? " kanal " : "", t.mbx ? (char)('A' + t.channel - 1) : ' ', t.slave,
             ext_kind_name(t.kind), t.addr, n);
    system_log_add_event((uint8_t)SYSLOG_SRC_REST, user, ip, msg);
  }

  // Resultat: kun enkelt-skrivninger og RTU-multi opdaterer cachen (MBX FC15/16 gør ikke)
  const bool confirmable = (n == 1) || !t.mbx;
  const char *state = "queued";
  if (wait_ms > 0 && confirmable) {
    bool q[EXT_MAX_COUNT];
    for (uint16_t i = 0; i < n; i++) q[i] = true;
    ext_wait(&t, n, q, t0, wait_ms);
    state = "ok";
    for (uint16_t i = 0; i < n; i++) {
      ext_snap_t s;
      ext_snapshot(&t, t.addr + i, &s);
      if (!ext_done(&s, t0, true)) { state = "timeout"; break; }
      if (s.status == 3 || s.err != 0) { state = "error"; break; }  // BUG-489
    }
  }
  char out[256];
  int pos = ext_print_target(out, sizeof(out), &t);
  snprintf(out + pos, sizeof(out) - pos, ",\"registers\":%u,\"result\":\"%s\"}", n, state);
  return api_send_json_status(req, (!strcmp(state, "error") || !strcmp(state, "timeout")) ? 502 : 200, out);
}
