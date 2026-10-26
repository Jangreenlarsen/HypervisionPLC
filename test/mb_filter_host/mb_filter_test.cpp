// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
// PC-test af FEAT-508 (filter mod fejlmaalinger). Koer: bash test/mb_filter_host/run.sh
#include <stdio.h>
#include "mb_filter.h"
#include "mb_async.h"
#include "modbus_expansion_async.h"

uint32_t g_now = 1000;
uint32_t millis() { return g_now; }
portMUX_TYPE mb_cache_spinlock = 0;
mb_async_state_t g_mb_async = {};
mbx_async_state_t g_mbx_async = {};
portMUX_TYPE mbx_cache_spinlock = 0;

static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FEJL: %s (linje %d)\n", msg, __LINE__); fails++; } } while (0)

// En "cache-post" som mb_async: gem kun ved ACCEPT
struct Entry { bool have = false; int32_t val = 0; int32_t cand = 0; uint8_t cnt = 0; bool err = false; };
static mb_filter_result_t feed(Entry &e, int32_t raw, uint8_t slave = 10, uint16_t addr = 3, uint8_t type = MB_REQ_READ_HOLDING,
                                uint8_t board = 0, uint8_t channel = 0) {
  mb_filter_result_t r = mb_filter_check(board, channel, slave, type, addr, e.have && !e.err, e.val, raw & 0xFFFF, &e.cand, &e.cnt);
  if (r == MB_FILTER_ACCEPT) { e.val = raw & 0xFFFF; e.have = true; e.err = false; }
  if (r == MB_FILTER_INVALID) e.err = true;
  return r;
}

static void rule(uint8_t i, uint16_t jump, uint8_t confirm, bool sig = true, bool inv = false, uint16_t inv_raw = 0) {
  mb_filter_rule_t &r = g_mb_filter_rules[i];
  memset(&r, 0, sizeof(r));
  r.enabled = 1; r.slave_id = 10; r.reg_type = MB_REQ_READ_HOLDING; r.addr_from = 0; r.addr_to = 7;
  r.is_signed = sig; r.max_jump = jump; r.confirm = confirm; r.invalid_enabled = inv; r.invalid_raw = inv_raw;
  g_mb_filter_count = i + 1;
}

int main() {
  // 1. enkeltstaaende 0,0 (som paa temp_tank3) naar aldrig cachen
  rule(0, 15, 1);
  { Entry e; feed(e, 371);
    CHECK(feed(e, 0) == MB_FILTER_HOLD && e.val == 371, "0,0 holdes tilbage");
    CHECK(feed(e, 371) == MB_FILTER_ACCEPT && e.val == 371, "naeste normale maaling godkendes");
    CHECK(e.cnt == 0, "ventende spring glemmes efter en normal maaling");
    CHECK(g_mb_filter_stats[0].rejected == 1 && g_mb_filter_stats[0].last_value == 0 && g_mb_filter_stats[0].last_kept == 371, "statistik for afvisningen"); }
  // 2. +2,0 grader (DS18B20 bit 5: 36,5 -> 38,5) holdes tilbage
  { Entry e; feed(e, 365);
    CHECK(feed(e, 385) == MB_FILTER_HOLD && e.val == 365, "+2,0 holdes tilbage");
    CHECK(feed(e, 365) == MB_FILTER_ACCEPT && e.val == 365, "tilbage til normal"); }
  // 3. et aegte spring godkendes ved naeste bekraeftende maaling
  { Entry e; feed(e, 200);
    CHECK(feed(e, 400) == MB_FILTER_HOLD, "spring venter");
    CHECK(feed(e, 402) == MB_FILTER_ACCEPT && e.val == 402, "bekraeftet spring godkendes (inden for maks. spring af det nye niveau)"); }
  // 4. confirm=2 kraever to bekraeftelser
  rule(0, 15, 2);
  { Entry e; feed(e, 200);
    CHECK(feed(e, 400) == MB_FILTER_HOLD && feed(e, 400) == MB_FILTER_HOLD, "to bekraeftelser kraeves");
    CHECK(feed(e, 400) == MB_FILTER_ACCEPT && e.val == 400, "godkendt efter 2. bekraeftelse"); }
  // 5. to forskellige fejl i traek starter forfra (ingen falsk bekraeftelse)
  rule(0, 15, 1);
  { Entry e; feed(e, 371);
    CHECK(feed(e, 0) == MB_FILTER_HOLD && feed(e, 1000) == MB_FILTER_HOLD && e.val == 371, "forskellige fejl bekraefter ikke hinanden"); }
  // 6. fortegn: -0,5 -> +0,3 er et lille skridt med INT16, et kaempe spring uden
  { Entry e; feed(e, -5);
    CHECK(feed(e, 3) == MB_FILTER_ACCEPT, "INT16: -5 -> 3 godkendes"); }
  rule(0, 15, 1, false);
  { Entry e; feed(e, -5);
    CHECK(feed(e, 3) == MB_FILTER_HOLD, "uden fortegn: 65531 -> 3 er et spring"); }
  // 7. foerste maaling (intet at sammenligne med) godkendes
  rule(0, 15, 1);
  { Entry e; CHECK(feed(e, 0) == MB_FILTER_ACCEPT, "foerste maaling godkendes"); }
  // 8. ugyldig vaerdi -> fejl; derefter godkendes naeste maaling uden springkontrol mod den gamle
  rule(0, 15, 1, true, true, 0x8000);
  { Entry e; feed(e, 371);
    CHECK(feed(e, 0x8000) == MB_FILTER_INVALID && e.err, "ugyldig vaerdi giver fejl");
    CHECK(feed(e, 380) == MB_FILTER_ACCEPT, "efter fejl godkendes naeste maaling"); }
  // 9. anden slave/adresse/type er ikke omfattet
  { Entry e; feed(e, 371, 11);
    CHECK(feed(e, 0, 11) == MB_FILTER_ACCEPT, "anden slave filtreres ikke");
    Entry f; feed(f, 371, 10, 8);
    CHECK(feed(f, 0, 10, 8) == MB_FILTER_ACCEPT, "adresse uden for omraadet filtreres ikke");
    Entry g; feed(g, 371, 10, 3, MB_REQ_READ_INPUT_REG);
    CHECK(feed(g, 0, 10, 3, MB_REQ_READ_INPUT_REG) == MB_FILTER_ACCEPT, "anden registertype filtreres ikke"); }
  // 10. slaaet fra = ingen filtrering
  g_mb_filter_rules[0].enabled = 0;
  { Entry e; feed(e, 371); CHECK(feed(e, 0) == MB_FILTER_ACCEPT, "regel slaaet fra"); }
  // 11. expansion board: reglen goelder kun det board og den kanal, den er sat til
  rule(0, 15, 1);
  g_mb_filter_rules[0].board = 2; g_mb_filter_rules[0].channel = 1;
  { Entry e; feed(e, 371, 10, 3, MB_REQ_READ_HOLDING, 2, 1);
    CHECK(feed(e, 0, 10, 3, MB_REQ_READ_HOLDING, 2, 1) == MB_FILTER_HOLD, "board 2 kanal A filtreres");
    Entry f; feed(f, 371, 10, 3, MB_REQ_READ_HOLDING, 2, 2);
    CHECK(feed(f, 0, 10, 3, MB_REQ_READ_HOLDING, 2, 2) == MB_FILTER_ACCEPT, "anden kanal filtreres ikke");
    Entry g; feed(g, 371);
    CHECK(feed(g, 0) == MB_FILTER_ACCEPT, "egen RS485 filtreres ikke af en board-regel"); }
  // 12. mb_filter_set validerer (NVS findes ikke paa PC, saa gyldige regler fejler foerst ved gemning)
  { mb_filter_rule_t r; memset(&r, 0, sizeof(r)); r.slave_id = 0; r.reg_type = MB_REQ_READ_HOLDING; r.confirm = 1; r.max_jump = 5;
    CHECK(mb_filter_set(&r, 1) != NULL && strstr(mb_filter_set(&r, 1), "Slave-ID"), "slave 0 afvises");
    r.slave_id = 10; r.addr_from = 5; r.addr_to = 2;
    CHECK(strstr(mb_filter_set(&r, 1), "Til-adresse") != NULL, "omvendt adresseomraade afvises");
    r.addr_to = 7; r.max_jump = 0;
    CHECK(strstr(mb_filter_set(&r, 1), "maks. spring") != NULL, "regel uden virkning afvises");
    r.max_jump = 5; r.confirm = 9;
    CHECK(strstr(mb_filter_set(&r, 1), "Bekraeft") != NULL, "confirm uden for 1-5 afvises");
    r.confirm = 1; r.board = 3; r.channel = 0;
    CHECK(strstr(mb_filter_set(&r, 1), "kanal") != NULL, "board uden kanal afvises");
    r.board = 9; r.channel = 1;
    CHECK(strstr(mb_filter_set(&r, 1), "board 1-8") != NULL, "board 9 afvises");
    r.board = 0; r.channel = 0;
    CHECK(strstr(mb_filter_set(&r, 1), "NVS") != NULL, "gyldig regel naar til gemning"); }

  if (fails) { printf("%d TEST(S) FEJLEDE\n", fails); return 1; }
  printf("ALLE TESTS OK\n");
  return 0;
}
