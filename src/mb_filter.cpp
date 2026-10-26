// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file mb_filter.cpp
 * @brief FEAT-508: Filter mod fejlmaalinger fra Modbus-slaver — se mb_filter.h
 */

#include "mb_filter.h"
#include "mb_async.h"
#include <nvs.h>
#include <string.h>

#define MB_FILTER_NVS_NAMESPACE "mb_filter"
#define MB_FILTER_NVS_KEY       "rules"
#define MB_FILTER_BLOB_VERSION  1

mb_filter_rule_t  g_mb_filter_rules[MB_FILTER_MAX_RULES];
uint8_t           g_mb_filter_count = 0;
mb_filter_stats_t g_mb_filter_stats[MB_FILTER_MAX_RULES];

typedef struct {
  uint8_t          version;
  uint8_t          count;
  mb_filter_rule_t rules[MB_FILTER_MAX_RULES];
} mb_filter_blob_t;

static const char *validate(const mb_filter_rule_t *r) {
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

  // Skift reglerne under cache-laasen, saa async-tasken aldrig ser et halvt skrevet saet
  portENTER_CRITICAL(&mb_cache_spinlock);
  memset(g_mb_filter_rules, 0, sizeof(g_mb_filter_rules));
  memcpy(g_mb_filter_rules, blob.rules, count * sizeof(mb_filter_rule_t));
  g_mb_filter_count = count;
  memset(g_mb_filter_stats, 0, sizeof(g_mb_filter_stats));
  for (uint8_t i = 0; i < g_mb_async.entry_count; i++) g_mb_async.entries[i].cand_count = 0;
  portEXIT_CRITICAL(&mb_cache_spinlock);
  return NULL;
}

void mb_filter_reset_stats() {
  portENTER_CRITICAL(&mb_cache_spinlock);
  memset(g_mb_filter_stats, 0, sizeof(g_mb_filter_stats));
  portEXIT_CRITICAL(&mb_cache_spinlock);
}

static inline int32_t as_num(const mb_filter_rule_t *r, int32_t raw) {
  return r->is_signed ? (int32_t)(int16_t)(uint16_t)raw : (int32_t)(uint16_t)raw;
}

static inline int32_t absdiff(int32_t a, int32_t b) {
  return a > b ? a - b : b - a;
}

mb_filter_result_t mb_filter_check(uint8_t slave_id, uint8_t reg_type, uint16_t addr,
                                   bool have_old, int32_t old_val, int32_t new_val,
                                   int32_t *cand_val, uint8_t *cand_count) {
  int8_t idx = -1;
  for (uint8_t i = 0; i < g_mb_filter_count; i++) {
    const mb_filter_rule_t *r = &g_mb_filter_rules[i];
    if (r->enabled && r->slave_id == slave_id && r->reg_type == reg_type &&
        addr >= r->addr_from && addr <= r->addr_to) {
      idx = (int8_t)i;
      break;
    }
  }
  if (idx < 0) return MB_FILTER_ACCEPT;
  const mb_filter_rule_t *r = &g_mb_filter_rules[idx];
  mb_filter_stats_t *st = &g_mb_filter_stats[idx];

  if (r->invalid_enabled && (uint16_t)new_val == r->invalid_raw) {
    st->invalid++;
    *cand_count = 0;
    return MB_FILTER_INVALID;
  }
  if (!have_old || r->max_jump == 0) {
    *cand_count = 0;   // intet at sammenligne med: foerste maaling godkendes
    return MB_FILTER_ACCEPT;
  }
  int32_t nv = as_num(r, new_val);
  if (absdiff(nv, as_num(r, old_val)) <= (int32_t)r->max_jump) {
    *cand_count = 0;   // normalt skridt — et evt. ventende spring var en enkeltmaaling
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
    return MB_FILTER_ACCEPT;
  }
  st->rejected++;
  st->last_addr = addr;
  st->last_value = nv;
  st->last_kept = as_num(r, old_val);
  st->last_ms = millis();
  if (st->last_ms == 0) st->last_ms = 1;
  return MB_FILTER_HOLD;
}
