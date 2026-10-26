// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file mb_filter.h
 * @brief FEAT-508: Filter mod fejlmaalinger fra Modbus-slaver (PLC'ens egen RS485 og expansion boards)
 *
 * Nogle slaver sender en gang imellem en forkert, men CRC-gyldig vaerdi — fx et
 * R4DCB08-temperaturmodul, der laeser sine DS18B20-foelere uden at tjekke deres
 * CRC (enkeltbit-fejl som +2,0 °C, eller 0,0). Modbus-CRC'en fanger kun fejl paa
 * bussen, ikke i slaven, saa PLC'en kan ikke se det paa selve svaret.
 *
 * En regel daekker en kilde (PLC'ens egen RS485 eller board+kanal) + slave + registertype + adresseomraade. Et spring stoerre
 * end "max_jump" (raa enheder) fra den sidst godkendte vaerdi accepteres foerst,
 * naar "confirm" efterfoelgende laesninger bekraefter det (ligger inden for
 * max_jump af det nye niveau). Indtil da beholder cachen den gamle vaerdi.
 * Valgfrit: en "ugyldig vaerdi" (fx en foeler-mangler-kode) giver fejlstatus.
 *
 * Gemmes i egen NVS-noegle (namespace "mb_filter", noegle "rules"), ikke i
 * PersistConfig — ingen schema-migration.
 */

#ifndef MB_FILTER_H
#define MB_FILTER_H

#include <Arduino.h>

#define MB_FILTER_MAX_RULES   8
#define MB_FILTER_NAME_LEN    24
#define MB_FILTER_MAX_CONFIRM 5

typedef struct {
  uint8_t  enabled;                 // 0/1
  uint8_t  board;                   // 0 = PLC'ens egen RS485 (Modbus Master), 1-8 = expansion board
  uint8_t  channel;                 // kun board: kanal 1-8 (A-H); 0 for egen RS485
  uint8_t  slave_id;                // 1-247
  uint8_t  reg_type;                // mb_request_type_t: MB_REQ_READ_HOLDING (3) eller MB_REQ_READ_INPUT_REG (4)
  uint8_t  is_signed;               // 1 = registrene er INT16 (fx temperaturer under 0)
  uint16_t addr_from;               // foerste adresse
  uint16_t addr_to;                 // sidste adresse (inkl.)
  uint16_t max_jump;                // stoerste spring (raa enheder) der accepteres straks; 0 = intet springfilter
  uint8_t  confirm;                 // antal efterfoelgende laesninger der skal bekraefte et stoerre spring (1-5)
  uint8_t  invalid_enabled;         // 1 = invalid_raw markerer maalingen ugyldig
  uint16_t invalid_raw;             // raa vaerdi der betyder "ugyldig" (fx foeler mangler)
  char     name[MB_FILTER_NAME_LEN];
} mb_filter_rule_t;

typedef struct {
  uint32_t rejected;                // spring der blev holdt tilbage (enkeltmaalinger der aldrig blev bekraeftet + ventende)
  uint32_t confirmed;               // spring der blev bekraeftet og godkendt
  uint32_t invalid;                 // maalinger med den ugyldige vaerdi
  uint16_t last_addr;               // adresse for seneste afviste maaling
  int32_t  last_value;              // seneste afviste vaerdi (raa, med fortegn hvis is_signed)
  int32_t  last_kept;               // vaerdien der blev beholdt i stedet
  uint32_t last_ms;                 // millis() for seneste afvisning (0 = aldrig)
} mb_filter_stats_t;

typedef enum {
  MB_FILTER_ACCEPT = 0,             // gem den nye vaerdi
  MB_FILTER_HOLD,                   // behold den gamle vaerdi (spring venter paa bekraeftelse)
  MB_FILTER_INVALID                 // marker maalingen ugyldig (fejlstatus)
} mb_filter_result_t;

extern mb_filter_rule_t  g_mb_filter_rules[MB_FILTER_MAX_RULES];
extern uint8_t           g_mb_filter_count;
extern mb_filter_stats_t g_mb_filter_stats[MB_FILTER_MAX_RULES];
/** Beskytter regler og statistik: tages kort inde i mb_check (som kaldes fra baade Modbus Master-tasken og
 *  expansion-workers, hver med sin egen cache-laas) og ved aflaesning/aendring fra REST. */
extern portMUX_TYPE      g_mb_filter_lock;

/** Indlaes reglerne fra NVS (kaldes fra setup efter nvs_flash_init). */
void mb_filter_load();

/**
 * Valider og gem et nyt saet regler (erstatter alle). Nulstiller statistikken.
 * @return NULL ved succes, ellers en dansk fejltekst.
 */
const char *mb_filter_set(const mb_filter_rule_t *rules, uint8_t count);

/**
 * Afgoer om en ny laest vaerdi maa gemmes i cachen. Ren beregning (ingen I/O),
 * maa kaldes inde i cachens spinlock (tager selv g_mb_filter_lock kortvarigt).
 * @param board      0 = PLC'ens egen RS485, 1-8 = expansion board
 * @param channel    kanal paa boardet (ignoreres for board 0)
 * @param have_old   cachen har en tidligere godkendt vaerdi
 * @param old_val    den tidligere godkendte vaerdi (raa 16 bit)
 * @param new_val    den nye laeste vaerdi (raa 16 bit)
 * @param cand_val   ind/ud: kandidat-niveau for et ventende spring
 * @param cand_count ind/ud: antal laesninger paa kandidat-niveauet (0 = intet ventende spring)
 */
mb_filter_result_t mb_filter_check(uint8_t board, uint8_t channel, uint8_t slave_id, uint8_t reg_type, uint16_t addr,
                                   bool have_old, int32_t old_val, int32_t new_val,
                                   int32_t *cand_val, uint8_t *cand_count);

/** Nulstil statistikken og filterloggen (fx fra web-UI). */
void mb_filter_reset_stats();

/* ----------------------------------------------------------------------------
 * Filterlog (FEAT-508): de seneste beslutninger i RAM — holdt tilbage, bekraeftet
 * spring og ugyldige. Med "vis alle aflaesninger" (trace) for en regel logges
 * ogsaa de almindelige, godkendte aflaesninger, i en begraenset periode.
 * -------------------------------------------------------------------------- */

#define MB_FILTER_LOG_SIZE      64
#define MB_FILTER_TRACE_MAX_S   900   // "vis alle aflaesninger" slukker selv efter hoejst 15 min

typedef enum {
  MB_FLOG_HELD = 1,                 // holdt tilbage (spring venter paa bekraeftelse)
  MB_FLOG_CONFIRMED,                // spring bekraeftet og godkendt
  MB_FLOG_INVALID,                  // ugyldig vaerdi -> fejlstatus
  MB_FLOG_ACCEPTED                  // almindelig godkendt aflaesning (kun under trace)
} mb_flog_outcome_t;

typedef struct {
  uint32_t seq;                     // loebenummer (1, 2, ...) — til trinvis hentning
  uint32_t ms;                      // millis() for haendelsen
  uint8_t  rule;                    // regelnummer (0-7)
  uint8_t  board, channel, slave;
  uint8_t  reg_type;                // 3 = HR, 4 = IR
  uint8_t  outcome;                 // mb_flog_outcome_t
  uint8_t  has_kept;                // 0 = der var ingen tidligere vaerdi
  uint16_t addr;
  int32_t  received;                // modtaget vaerdi (med fortegn hvis reglen er INT16)
  int32_t  kept;                    // beholdt vaerdi (HELD/INVALID) eller vaerdien foer (CONFIRMED/ACCEPTED)
} mb_filter_log_t;

/**
 * Kopier loggens poster med seq > since (aeldste foerst) til out.
 * @return antal kopierede; *latest = hoejeste seq i loggen.
 */
uint16_t mb_filter_log_copy(uint32_t since, mb_filter_log_t *out, uint16_t max, uint32_t *latest);

/** Slaa "vis alle aflaesninger" til for en regel i `seconds` sekunder (rule < 0 eller seconds = 0: fra). */
void mb_filter_trace(int8_t rule, uint32_t seconds);

/** Aktiv trace: regelnummer og resterende sekunder (rule = -1 hvis fra). */
void mb_filter_trace_state(int8_t *rule, uint32_t *remaining_s);

#endif // MB_FILTER_H
