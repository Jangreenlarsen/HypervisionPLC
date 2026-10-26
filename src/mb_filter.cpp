// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file mb_filter.cpp
 * @brief FEAT-508: Filter mod fejlmaalinger fra Modbus-slaver — se mb_filter.h
 */

#include "mb_filter.h"
#include "mb_async.h"
#include "modbus_expansion_async.h"
#include <nvs.h>
#include <string.h>

#define MB_FILTER_NVS_NAMESPACE "mb_filter"
#define MB_FILTER_NVS_KEY       "rules"
#define MB_FILTER_BLOB_VERSION  2   // 2: board/kanal tilfoejet (version 1 blev aldrig udrullet)

mb_filter_rule_t  g_mb_filter_rules[MB_FILTER_MAX_RULES];
uint8_t           g_mb_filter_count = 0;
mb_filter_stats_t g_mb_filter_stats[MB_FILTER_MAX_RULES];
portMUX_TYPE      g_mb_filter_lock = portMUX_INITIALIZER_UNLOCKED;

// Filterlog: ringbuffer i RAM (beskyttet af g_mb_filter_lock)
static mb_filter_log_t s_log[MB_FILTER_LOG_SIZE];
static uint16_t        s_log_head = 0;     // naeste plads der skrives
static uint16_t        s_log_count = 0;
static uint32_t        s_log_seq = 0;      // seq for seneste post
static int8_t          s_trace_rule = -1;  // "vis alle aflaesninger" for denne regel
static uint32_t        s_trace_until = 0;  // millis() hvor trace slukker

typedef struct {
  uint8_t          version;
  uint8_t          count;
  mb_filter_rule_t rules[MB_FILTER_MAX_RULES];
} mb_filter_blob_t;

static const char *validate(const mb_filter_rule_t *r) {
  if (r->board > 8) return "Kilde skal vaere egen RS485 eller board 1-8";
  if (r->board > 0 && (r->channel < 1 || r->channel > 8)) return "Vaelg boardets kanal (A-H)";
  if (r->board == 0 && r->channel != 0) return "Egen RS485 har ingen kanal";
  if (r->slave_id < 1 || r->slave_id > 247) return "Slave-ID skal vaere 1-247";
  if (r->reg_type != MB_REQ_READ_HOLDING && r->reg_type != MB_REQ_READ_INPUT_REG)
    return "Registertype skal vaere holding (HR) eller input (IR)";
  if (r->addr_to < r->addr_from) return "Til-adresse skal vaere stoerre end eller lig fra-adresse";
  if (r->confirm < 1 || r->confirm > MB_FILTER_MAX_CONFIRM) return "Bekraeft skal vaere 1-5 laesninger";
  if (r->max_jump == 0 && !r->invalid_enabled) return "Saet et maks. spring eller en ugyldig vaerdi (ellers goer reglen intet)";
  return NULL;
}

void mb_filter_load() {
  memset(g_mb_filter_rules, 0, sizeof(g_mb_filter_rules));
  memset(g_mb_filter_stats, 0, sizeof(g_mb_filter_stats));
  g_mb_filter_count = 0;
  nvs_handle_t h;
  if (nvs_open(MB_FILTER_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;  // aldrig gemt: ingen regler
  mb_filter_blob_t blob;
  size_t len = sizeof(blob);
  esp_err_t err = nvs_get_blob(h, MB_FILTER_NVS_KEY, &blob, &len);
  nvs_close(h);
  if (err != ESP_OK || len != sizeof(blob) || blob.version != MB_FILTER_BLOB_VERSION ||
      blob.count > MB_FILTER_MAX_RULES) {
    return;  // ukendt/beskadiget format: start uden regler frem for at gaette
  }
  uint8_t n = 0;
  for (uint8_t i = 0; i < blob.count; i++) {
    blob.rules[i].name[MB_FILTER_NAME_LEN - 1] = '\0';
    if (validate(&blob.rules[i]) == NULL) g_mb_filter_rules[n++] = blob.rules[i];
  }
  g_mb_filter_count = n;
}

const char *mb_filter_set(const mb_filter_rule_t *rules, uint8_t count) {
  if (count > MB_FILTER_MAX_RULES) return "Hoejst 8 regler";
  for (uint8_t i = 0; i < count; i++) {
    const char *e = validate(&rules[i]);
    if (e) return e;
  }
  mb_filter_blob_t blob;
  memset(&blob, 0, sizeof(blob));
  blob.version = MB_FILTER_BLOB_VERSION;
  blob.count = count;
  memcpy(blob.rules, rules, count * sizeof(mb_filter_rule_t));
  for (uint8_t i = 0; i < count; i++) blob.rules[i].name[MB_FILTER_NAME_LEN - 1] = '\0';

  nvs_handle_t h;
  if (nvs_open(MB_FILTER_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return "Kunne ikke aabne NVS";
  esp_err_t err = nvs_set_blob(h, MB_FILTER_NVS_KEY, &blob, sizeof(blob));
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  if (err != ESP_OK) return "Kunne ikke gemme i NVS";

  // Skift reglerne under filter-laasen, saa ingen task ser et halvt skrevet saet
  portENTER_CRITICAL(&g_mb_filter_lock);
  memset(g_mb_filter_rules, 0, sizeof(g_mb_filter_rules));
  memcpy(g_mb_filter_rules, blob.rules, count * sizeof(mb_filter_rule_t));
  g_mb_filter_count = count;
  memset(g_mb_filter_stats, 0, sizeof(g_mb_filter_stats));
  s_log_head = s_log_count = 0;   // regelnumrene i loggen passer ikke laengere (seq fortsaetter)
  s_trace_rule = -1;
  portEXIT_CRITICAL(&g_mb_filter_lock);
  // Ventende spring hoerer til de gamle regler: glem dem (hver cache under sin egen laas)
  portENTER_CRITICAL(&mb_cache_spinlock);
  for (uint8_t i = 0; i < g_mb_async.entry_count; i++) g_mb_async.entries[i].cand_count = 0;
  portEXIT_CRITICAL(&mb_cache_spinlock);
  portENTER_CRITICAL(&mbx_cache_spinlock);
  for (uint16_t i = 0; i < g_mbx_async.entry_count; i++) g_mbx_async.entries[i].cand_count = 0;
  portEXIT_CRITICAL(&mbx_cache_spinlock);
  return NULL;
}

void mb_filter_reset_stats() {
  portENTER_CRITICAL(&g_mb_filter_lock);
  memset(g_mb_filter_stats, 0, sizeof(g_mb_filter_stats));
  s_log_head = s_log_count = 0;
  portEXIT_CRITICAL(&g_mb_filter_lock);
}

void mb_filter_trace(int8_t rule, uint32_t seconds) {
  if (seconds > MB_FILTER_TRACE_MAX_S) seconds = MB_FILTER_TRACE_MAX_S;
  portENTER_CRITICAL(&g_mb_filter_lock);
  if (rule < 0 || rule >= (int8_t)g_mb_filter_count || seconds == 0) {
    s_trace_rule = -1;
  } else {
    s_trace_rule = rule;
    s_trace_until = millis() + seconds * 1000UL;
  }
  portEXIT_CRITICAL(&g_mb_filter_lock);
}

void mb_filter_trace_state(int8_t *rule, uint32_t *remaining_s) {
  portENTER_CRITICAL(&g_mb_filter_lock);
  int32_t left = (int32_t)(s_trace_until - millis());
  if (s_trace_rule >= 0 && left <= 0) s_trace_rule = -1;   // udloebet
  *rule = s_trace_rule;
  *remaining_s = s_trace_rule >= 0 ? (uint32_t)(left + 999) / 1000 : 0;
  portEXIT_CRITICAL(&g_mb_filter_lock);
}

uint16_t mb_filter_log_copy(uint32_t since, mb_filter_log_t *out, uint16_t max, uint32_t *latest) {
  uint16_t n = 0;
  portENTER_CRITICAL(&g_mb_filter_lock);
  uint16_t start = (uint16_t)((s_log_head + MB_FILTER_LOG_SIZE - s_log_count) % MB_FILTER_LOG_SIZE);
  for (uint16_t i = 0; i < s_log_count && n < max; i++) {
    const mb_filter_log_t *e = &s_log[(start + i) % MB_FILTER_LOG_SIZE];
    if (e->seq > since) out[n++] = *e;
  }
  *latest = s_log_seq;
  portEXIT_CRITICAL(&g_mb_filter_lock);
  return n;
}

static inline int32_t as_num(const mb_filter_rule_t *r, int32_t raw) {
  return r->is_signed ? (int32_t)(int16_t)(uint16_t)raw : (int32_t)(uint16_t)raw;
}

static inline int32_t absdiff(int32_t a, int32_t b) {
  return a > b ? a - b : b - a;
}

// Kaldes med g_mb_filter_lock taget
static void log_add(uint8_t rule, uint8_t board, uint8_t channel, uint8_t slave, uint8_t reg_type, uint16_t addr,
                    uint8_t outcome, int32_t received, bool has_kept, int32_t kept) {
  mb_filter_log_t *e = &s_log[s_log_head];
  e->seq = ++s_log_seq;
  e->ms = millis();
  e->rule = rule; e->board = board; e->channel = channel; e->slave = slave; e->reg_type = reg_type;
  e->outcome = outcome; e->has_kept = has_kept ? 1 : 0; e->addr = addr; e->received = received; e->kept = kept;
  s_log_head = (uint16_t)((s_log_head + 1) % MB_FILTER_LOG_SIZE);
  if (s_log_count < MB_FILTER_LOG_SIZE) s_log_count++;
}

static mb_filter_result_t check_locked(uint8_t board, uint8_t channel, uint8_t slave_id, uint8_t reg_type,
                                       uint16_t addr, bool have_old, int32_t old_val, int32_t new_val,
                                       int32_t *cand_val, uint8_t *cand_count) {
  int8_t idx = -1;
  for (uint8_t i = 0; i < g_mb_filter_count; i++) {
    const mb_filter_rule_t *r = &g_mb_filter_rules[i];
    if (r->enabled && r->board == board && (board == 0 || r->channel == channel) &&
        r->slave_id == slave_id && r->reg_type == reg_type && addr >= r->addr_from && addr <= r->addr_to) {
      idx = (int8_t)i;
      break;
    }
  }
  if (idx < 0) return MB_FILTER_ACCEPT;
  const mb_filter_rule_t *r = &g_mb_filter_rules[idx];
  mb_filter_stats_t *st = &g_mb_filter_stats[idx];
  int32_t nv = as_num(r, new_val), ov = as_num(r, old_val);
  bool trace = (s_trace_rule == idx && (int32_t)(s_trace_until - millis()) > 0);
#define FLOG(outcome) log_add((uint8_t)idx, board, channel, slave_id, reg_type, addr, (outcome), nv, have_old, ov)

  if (r->invalid_enabled && (uint16_t)new_val == r->invalid_raw) {
    st->invalid++;
    *cand_count = 0;
    FLOG(MB_FLOG_INVALID);
    return MB_FILTER_INVALID;
  }
  if (!have_old || r->max_jump == 0) {
    *cand_count = 0;   // intet at sammenligne med: foerste maaling godkendes
    if (trace) FLOG(MB_FLOG_ACCEPTED);
    return MB_FILTER_ACCEPT;
  }
  if (absdiff(nv, ov) <= (int32_t)r->max_jump) {
    *cand_count = 0;   // normalt skridt — et evt. ventende spring var en enkeltmaaling
    if (trace) FLOG(MB_FLOG_ACCEPTED);
    return MB_FILTER_ACCEPT;
  }
  // Spring: tael laesninger paa det nye niveau
  if (*cand_count > 0 && absdiff(nv, as_num(r, *cand_val)) <= (int32_t)r->max_jump) {
    if (*cand_count < 255) (*cand_count)++;
  } else {
    *cand_val = new_val;
    *cand_count = 1;
  }
  if (*cand_count > r->confirm) {   // springet + "confirm" bekraeftende laesninger
    *cand_count = 0;
    st->confirmed++;
    FLOG(MB_FLOG_CONFIRMED);
    return MB_FILTER_ACCEPT;
  }
  st->rejected++;
  st->last_addr = addr;
  st->last_value = nv;
  st->last_kept = ov;
  st->last_ms = millis();
  if (st->last_ms == 0) st->last_ms = 1;
  FLOG(MB_FLOG_HELD);
  return MB_FILTER_HOLD;
#undef FLOG
}

mb_filter_result_t mb_filter_check(uint8_t board, uint8_t channel, uint8_t slave_id, uint8_t reg_type, uint16_t addr,
                                   bool have_old, int32_t old_val, int32_t new_val,
                                   int32_t *cand_val, uint8_t *cand_count) {
  if (g_mb_filter_count == 0) return MB_FILTER_ACCEPT;   // intet filter: ingen laas i den travle sti
  portENTER_CRITICAL(&g_mb_filter_lock);
  mb_filter_result_t r = check_locked(board, channel, slave_id, reg_type, addr, have_old, old_val, new_val,
                                      cand_val, cand_count);
  portEXIT_CRITICAL(&g_mb_filter_lock);
  return r;
}
