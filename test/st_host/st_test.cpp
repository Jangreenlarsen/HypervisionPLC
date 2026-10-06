// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
// Host-test af ST builtins (BUG-433): kompilér ST-kilde, kør cyklusser, tjek variabler.
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "st_builtins.h"
#include "st_parser.h"
#include "st_compiler.h"
#include "st_vm.h"
#include "st_stateful.h"
extern uint32_t g_fake_ms;
static int fails = 0;

static st_bytecode_program_t *compile(const char *src) {
  st_parser_t *p = (st_parser_t *)calloc(1, sizeof(st_parser_t));
  st_parser_init(p, src);
  st_program_t *prog = st_parser_parse_program(p);
  if (!prog) { printf("PARSE FEJL: %s\n", p->error_msg); return NULL; }
  st_compiler_t *c = (st_compiler_t *)calloc(1, sizeof(st_compiler_t));
  st_compiler_init(c);
  st_bytecode_program_t *bc = (st_bytecode_program_t *)calloc(1, sizeof(st_bytecode_program_t));
  if (!st_compiler_compile(c, prog, bc)) { printf("COMPILE FEJL: %s\n", c->error_msg); return NULL; }
  return bc;
}
static bool cycle(st_bytecode_program_t *bc, uint32_t dt_ms = 10) {
  if (!bc) return false;
  g_fake_ms += dt_ms;
  if (bc->stateful) ((st_stateful_storage_t *)bc->stateful)->cycle_time_ms = dt_ms;
  st_vm_t *vm = (st_vm_t *)calloc(1, sizeof(st_vm_t));
  st_vm_init(vm, bc);
  int steps = 0;
  while (!vm->halted && !vm->error && steps++ < 10000) if (!st_vm_step(vm)) break;
  bool ok = !vm->error;
  if (!ok) printf("   VM-fejl: %s\n", vm->error_msg);
  else {
    memcpy(bc->variables, vm->variables, vm->var_count * sizeof(st_value_t));
    memcpy(bc->string_vars, vm->string_vars, sizeof(bc->string_vars));
  }
  free(vm);
  return ok;
}
static int vidx(st_bytecode_program_t *bc, const char *n) {
  for (int i = 0; i < bc->var_count; i++) if (!strcmp(bc->var_names[i], n)) return i;
  printf("ukendt var %s\n", n); return 0;
}
static void setb(st_bytecode_program_t *bc, const char *n, bool v) { int i = vidx(bc, n); bc->variables[i].dint_val = 0; bc->variables[i].bool_val = v; }
static bool getb(st_bytecode_program_t *bc, const char *n) { return bc->variables[vidx(bc, n)].bool_val; }
static float getr(st_bytecode_program_t *bc, const char *n) { return bc->variables[vidx(bc, n)].real_val; }
static int geti(st_bytecode_program_t *bc, const char *n) { return bc->variables[vidx(bc, n)].int_val; }
static void ok(bool c, const char *m) { printf("%s %s\n", c ? "PASS" : "FAIL", m); if (!c) fails++; }

int main() {
  { // CTU (3 args) — tidligere aldrig udført
    st_bytecode_program_t *bc = compile("PROGRAM t VAR cu:BOOL; rst:BOOL; q:BOOL; END_VAR q := CTU(cu, rst, 3); END_PROGRAM");
    bool early = false;
    for (int i = 0; bc && i < 3; i++) { setb(bc, "cu", true); cycle(bc); if (i < 2 && getb(bc, "q")) early = true; setb(bc, "cu", false); cycle(bc); }
    ok(bc && !early && getb(bc, "q"), "CTU: Q efter 3 flanker (PV=3 som INT)");
    if (bc) { setb(bc, "rst", true); cycle(bc); ok(!getb(bc, "q"), "CTU: RESET -> Q=FALSE"); }
  }
  { // CTD
    st_bytecode_program_t *bc = compile("PROGRAM t VAR cd:BOOL; ld:BOOL; q:BOOL; END_VAR q := CTD(cd, ld, 2); END_PROGRAM");
    if (bc) {
      setb(bc, "ld", true); cycle(bc); setb(bc, "ld", false); cycle(bc);
      for (int i = 0; i < 2; i++) { setb(bc, "cd", true); cycle(bc); setb(bc, "cd", false); cycle(bc); }
    }
    ok(bc && getb(bc, "q"), "CTD: Q efter LOAD + 2 nedtællinger");
  }
  { // CTUD (5 args)
    st_bytecode_program_t *bc = compile("PROGRAM t VAR cu:BOOL; q:BOOL; END_VAR q := CTUD(cu, FALSE, FALSE, FALSE, 2); END_PROGRAM");
    for (int i = 0; bc && i < 2; i++) { setb(bc, "cu", true); cycle(bc); setb(bc, "cu", false); cycle(bc); }
    ok(bc && getb(bc, "q"), "CTUD: QU efter 2 optællinger");
  }
  { // HYSTERESIS (3 args) — tidligere aldrig udført
    st_bytecode_program_t *bc = compile("PROGRAM t VAR x:REAL; q:BOOL; END_VAR q := HYSTERESIS(x, 50.0, 20.0); END_PROGRAM");
    bool a = false, b = false, c = true;
    if (bc) {
      int xi = vidx(bc, "x");
      bc->variables[xi].real_val = 60; cycle(bc); a = getb(bc, "q");
      bc->variables[xi].real_val = 30; cycle(bc); b = getb(bc, "q");
      bc->variables[xi].real_val = 10; cycle(bc); c = getb(bc, "q");
    }
    ok(a && b && !c, "HYSTERESIS: TRUE over 50, holder ved 30, FALSE under 20");
  }
  { // BLINK (3 args)
    st_bytecode_program_t *bc = compile("PROGRAM t VAR q:BOOL; n:INT; last:BOOL; END_VAR q := BLINK(TRUE, 50, 50); IF q <> last THEN n := n + 1; END_IF; last := q; END_PROGRAM");
    for (int i = 0; bc && i < 40; i++) cycle(bc);
    ok(bc && geti(bc, "n") >= 6, "BLINK: skifter (50/50 ms over 400 ms)");
  }
  { // SCALE -> REAL
    st_bytecode_program_t *bc = compile("PROGRAM t VAR y:REAL; END_VAR y := SCALE(50, 0, 100, 0.0, 10.0); END_PROGRAM");
    cycle(bc); ok(bc && fabsf(getr(bc, "y") - 5.0f) < 0.01f, "SCALE(50,0,100,0,10) = 5.0 i REAL");
  }
  { // FILTER -> REAL
    st_bytecode_program_t *bc = compile("PROGRAM t VAR y:REAL; END_VAR y := FILTER(100.0, 50); END_PROGRAM");
    for (int i = 0; bc && i < 100; i++) cycle(bc);
    ok(bc && getr(bc, "y") > 90.0f && getr(bc, "y") <= 100.01f, "FILTER(100.0, 50) konvergerer mod 100");
  }
  { // SR / RS (BOOL resultat)
    st_bytecode_program_t *bc = compile("PROGRAM t VAR s:BOOL; r:BOOL; q:BOOL; q2:BOOL; END_VAR q := SR(s, r); q2 := RS(s, r); END_PROGRAM");
    bool a = false, a2 = false, b = false, c = true, c2 = true;
    if (bc) {
      setb(bc, "s", true); cycle(bc); a = getb(bc, "q"); a2 = getb(bc, "q2");
      setb(bc, "s", false); cycle(bc); b = getb(bc, "q");
      setb(bc, "r", true); cycle(bc); c = getb(bc, "q"); c2 = getb(bc, "q2");
    }
    ok(a && b && !c && a2 && !c2, "SR/RS: sæt, hold, nulstil");
  }
  { // Matematik med INT-argumenter
    st_bytecode_program_t *bc = compile("PROGRAM t VAR a:REAL; b:REAL; c:REAL; d:REAL; e:INT; END_VAR a := SQRT(16); b := POW(2, 3); c := SQRT(16.0); d := LN(1); e := ROUND(2.6); END_PROGRAM");
    cycle(bc);
    ok(bc && fabsf(getr(bc, "a") - 4) < 0.001f, "SQRT(16) med INT = 4.0");
    ok(bc && fabsf(getr(bc, "b") - 8) < 0.001f, "POW(2, 3) med INT = 8.0");
    ok(bc && fabsf(getr(bc, "c") - 4) < 0.001f, "SQRT(16.0) = 4.0");
    ok(bc && fabsf(getr(bc, "d")) < 0.001f, "LN(1) = 0");
    ok(bc && geti(bc, "e") == 3, "ROUND(2.6) = 3");
  }
  { // TON med INT- og TIME-preset
    st_bytecode_program_t *bc = compile("PROGRAM t VAR s:BOOL; q:BOOL; q2:BOOL; END_VAR q := TON(s, 50); q2 := TON(s, T#50ms); END_PROGRAM");
    bool early = true;
    if (bc) {
      setb(bc, "s", true); cycle(bc); early = getb(bc, "q") || getb(bc, "q2");
      for (int i = 0; i < 6; i++) cycle(bc);
    }
    ok(bc && !early && getb(bc, "q") && getb(bc, "q2"), "TON: 50 (INT) og T#50ms udløser efter ~50 ms");
  }
  { // R_TRIG
    st_bytecode_program_t *bc = compile("PROGRAM t VAR x:INT; q:BOOL; END_VAR q := R_TRIG(x > 0); END_PROGRAM");
    bool a = false, b = true;
    if (bc) {
      int xi = vidx(bc, "x"); bc->variables[xi].int_val = 0; cycle(bc);
      bc->variables[xi].int_val = 5; cycle(bc); a = getb(bc, "q"); cycle(bc); b = getb(bc, "q");
    }
    ok(a && !b, "R_TRIG: én puls på stigende flanke");
  }
  { // BIT / ROL / MUX / SEL / LIMIT / MIN med REAL
    st_bytecode_program_t *bc = compile("PROGRAM t VAR a:INT; b:BOOL; c:DINT; d:DINT; m:INT; s:INT; l:REAL; mn:REAL; END_VAR a := BIT_SET(0, 3); b := BIT_TST(8, 3); d := 5; c := ROL(d, 0); m := MUX(2, 10, 20, 30); s := SEL(TRUE, 1, 2); l := LIMIT(0.0, 7.5, 5.0); mn := MIN(2.5, 3); END_PROGRAM");
    cycle(bc);
    ok(bc && geti(bc, "a") == 8 && getb(bc, "b"), "BIT_SET/BIT_TST");
    ok(bc && bc->variables[vidx(bc, "c")].dint_val == 5, "ROL(DINT, 0) = uændret");
    ok(bc && geti(bc, "m") == 30 && geti(bc, "s") == 2, "MUX/SEL");
    ok(bc && fabsf(getr(bc, "l") - 5.0f) < 0.001f && fabsf(getr(bc, "mn") - 2.5f) < 0.001f, "LIMIT/MIN med REAL");
  }
  { // STRING-literaler
    st_bytecode_program_t *bc = compile("PROGRAM t VAR s:STRING; n:INT; END_VAR s := CONCAT('Temp:', 'OK'); n := LEN(s); END_PROGRAM");
    cycle(bc); ok(bc && geti(bc, "n") == 7, "CONCAT/LEN med literaler");
  }
  { // Resten af matematik/konvertering
    st_bytecode_program_t *bc = compile("PROGRAM t VAR ab:INT; abr:REAL; tr:INT; fl:INT; ce:INT; ex:REAL; lg:REAL; sn:REAL; cs:REAL; ir:REAL; ri:INT; bi:INT; ib:BOOL; mx:INT; sm:INT; smr:REAL; ror1:INT; bc1:INT; END_VAR "
                                        "ab := ABS(-5); abr := ABS(-2.5); tr := TRUNC(-2.7); fl := FLOOR(-2.2); ce := CEIL(2.2); ex := EXP(0); lg := LOG(100); "
                                        "sn := SIN(0); cs := COS(0); ir := INT_TO_REAL(7); ri := REAL_TO_INT(9.9); bi := BOOL_TO_INT(TRUE); ib := INT_TO_BOOL(3); "
                                        "mx := MAX(4, 9); sm := SUM(2, 3); smr := SUM(1.5, 2); ror1 := ROR(1, 1); bc1 := BIT_CLR(15, 0); END_PROGRAM");
    cycle(bc);
    ok(bc && geti(bc, "ab") == 5 && fabsf(getr(bc, "abr") - 2.5f) < 1e-3f, "ABS (INT og REAL)");
    ok(bc && geti(bc, "tr") == -2 && geti(bc, "fl") == -3 && geti(bc, "ce") == 3, "TRUNC/FLOOR/CEIL");
    ok(bc && fabsf(getr(bc, "ex") - 1) < 1e-3f && fabsf(getr(bc, "lg") - 2) < 1e-3f, "EXP(0)=1, LOG(100)=2 (INT-argumenter)");
    ok(bc && fabsf(getr(bc, "sn")) < 1e-3f && fabsf(getr(bc, "cs") - 1) < 1e-3f, "SIN(0)=0, COS(0)=1");
    ok(bc && fabsf(getr(bc, "ir") - 7) < 1e-3f && geti(bc, "ri") == 9, "INT_TO_REAL/REAL_TO_INT");
    ok(bc && geti(bc, "bi") == 1 && getb(bc, "ib"), "BOOL_TO_INT/INT_TO_BOOL");
    ok(bc && geti(bc, "mx") == 9 && geti(bc, "sm") == 5 && fabsf(getr(bc, "smr") - 3.5f) < 1e-3f, "MAX/SUM (INT og REAL)");
    ok(bc && (uint16_t)geti(bc, "ror1") == 0x8000 && geti(bc, "bc1") == 14, "ROR/BIT_CLR");
  }
  { // F_TRIG, TOF, TP
    st_bytecode_program_t *bc = compile("PROGRAM t VAR s:BOOL; f:BOOL; tof1:BOOL; p:BOOL; END_VAR f := F_TRIG(s); tof1 := TOF(s, 30); p := TP(s, 30); END_PROGRAM");
    bool f1 = false, of1 = false, of2 = true, p1 = false, p2 = true;
    if (bc) {
      setb(bc, "s", true); cycle(bc); p1 = getb(bc, "p");
      for (int i = 0; i < 5; i++) cycle(bc); p2 = getb(bc, "p");
      setb(bc, "s", false); cycle(bc); f1 = getb(bc, "f"); of1 = getb(bc, "tof1");
      for (int i = 0; i < 5; i++) cycle(bc); of2 = getb(bc, "tof1");
    }
    ok(f1, "F_TRIG: puls på faldende flanke");
    ok(of1 && !of2, "TOF: holder TRUE ~30 ms efter IN går FALSE");
    ok(p1 && !p2, "TP: puls på 30 ms");
  }
  { // STRING: LEFT/RIGHT/MID
    st_bytecode_program_t *bc = compile("PROGRAM t VAR a:STRING; b:STRING; c:STRING; la:INT; lb:INT; lc:INT; END_VAR a := LEFT('Hypervision', 5); b := RIGHT('Hypervision', 6); c := MID('Hypervision', 3, 2); la := LEN(a); lb := LEN(b); lc := LEN(c); END_PROGRAM");
    cycle(bc);
    ok(bc && !strcmp(bc->string_vars[vidx(bc, "a")], "Hyper") && !strcmp(bc->string_vars[vidx(bc, "b")], "vision") && !strcmp(bc->string_vars[vidx(bc, "c")], "pe"), "LEFT/RIGHT/MID");
  }
  {  // FEAT-461: arr := MBX_READ_HOLDINGS(board, kanal, slave, addr, count)
    st_bytecode_program_t *bc = compile("PROGRAM t VAR r: ARRAY[0..3] OF INT; END_VAR r := MBX_READ_HOLDINGS(1, 1, 9, 0, 4); END_PROGRAM");
    ok(bc != NULL, "MBX_READ_HOLDINGS: array-tildeling compiler");
    bool found = false;
    if (bc) {
      for (uint32_t i = 0; i < bc->instr_count; i++) {
        if (bc->instructions[i].opcode == ST_OP_CALL_BUILTIN &&
            bc->instructions[i].arg.int_arg == (int32_t)ST_BUILTIN_MBX_READ_HOLDINGS) found = true;
      }
      ok(cycle(bc), "MBX_READ_HOLDINGS: cyklus kører uden fejl");
    }
    ok(found, "MBX_READ_HOLDINGS: CALL_BUILTIN med det nye ID");
    st_bytecode_program_t *bad = compile("PROGRAM t VAR x: INT; END_VAR x := MBX_READ_HOLDINGS(1, 1, 9, 0, 4); END_PROGRAM");
    ok(bad == NULL, "MBX_READ_HOLDINGS: afvises uden ARRAY");
  }
  {  // FEAT-467: kanal som bogstav i MBX_*
    st_bytecode_program_t *bc = compile("PROGRAM t VAR v: INT; r: ARRAY[0..1] OF INT; END_VAR v := MBX_READ_HOLDING(1, 'C', 9, 2); r := MBX_READ_HOLDINGS(1, 'd', 9, 0, 2); MBX_WRITE_HOLDINGS(1, 'B', 9, 0, 2) := r; END_PROGRAM");
    ok(bc != NULL, "MBX kanal-bogstav: 'C', 'd' og 'B' compiler");
    int pushes3 = 0, pushes4 = 0, pushes2 = 0;
    if (bc) {
      for (uint32_t i = 0; i + 1 < bc->instr_count; i++) {
        if (bc->instructions[i].opcode == ST_OP_PUSH_INT && bc->instructions[i].arg.int_arg == 1 &&
            bc->instructions[i + 1].opcode == ST_OP_PUSH_INT) {
          int ch = bc->instructions[i + 1].arg.int_arg;
          if (ch == 3) pushes3++;
          if (ch == 4) pushes4++;
          if (ch == 2) pushes2++;
        }
      }
    }
    ok(pushes3 >= 1 && pushes4 >= 1 && pushes2 >= 1, "MBX kanal-bogstav: 'C'->3, 'd'->4, 'B'->2");
    st_bytecode_program_t *bad = compile("PROGRAM t VAR v: INT; END_VAR v := MBX_READ_HOLDING(1, 'Z', 9, 2); END_PROGRAM");
    ok(bad == NULL, "MBX kanal-bogstav: 'Z' afvises");
    st_bytecode_program_t *num = compile("PROGRAM t VAR v: INT; END_VAR v := MBX_READ_HOLDING(1, 2, 9, 2); END_PROGRAM");
    ok(num != NULL, "MBX kanal som tal virker stadig");
  }
  {  // FEAT-467b: kanal-bogstav uden anførselstegn
    st_bytecode_program_t *bc = compile("PROGRAM t VAR x: INT; okx: BOOL; END_VAR x := MBX_READ_HOLDING(1, c, 9, 2); okx := MBX_SUCCESS(); END_PROGRAM");
    ok(bc != NULL, "MBX kanal uden anfoerselstegn: c compiler");
    bool found = false;
    if (bc) for (uint32_t i = 0; i + 1 < bc->instr_count; i++)
      if (bc->instructions[i].opcode == ST_OP_PUSH_INT && bc->instructions[i].arg.int_arg == 1 &&
          bc->instructions[i + 1].opcode == ST_OP_PUSH_INT && bc->instructions[i + 1].arg.int_arg == 3) found = true;
    ok(found, "MBX kanal uden anfoerselstegn: c -> 3");
    st_bytecode_program_t *v = compile("PROGRAM t VAR x: INT; c: INT; END_VAR c := 2; x := MBX_READ_HOLDING(1, c, 9, 2); END_PROGRAM");
    bool usesvar = false;
    if (v) for (uint32_t i = 0; i < v->instr_count; i++) if (v->instructions[i].opcode == ST_OP_LOAD_VAR) usesvar = true;
    ok(v != NULL && usesvar, "MBX kanal: findes variablen c, bruges variablen");
  }
  {  // FEAT-469: TIME_VALID/HOUR/MINUTE/DAY
    st_bytecode_program_t *bc = compile("PROGRAM t VAR v: BOOL; h: INT; m: INT; d: INT; END_VAR v := TIME_VALID(); h := TIME_HOUR(); m := TIME_MINUTE(); d := TIME_DAY(); END_PROGRAM");
    ok(bc != NULL, "TIME_*: compiler");
    if (bc) {
      cycle(bc);
      ok(getb(bc, "v"), "TIME_VALID: TRUE paa host (systemuret er sat)");
      ok(geti(bc, "h") >= 0 && geti(bc, "h") <= 23 && geti(bc, "m") >= 0 && geti(bc, "m") <= 59 &&
         geti(bc, "d") >= 1 && geti(bc, "d") <= 31, "TIME_HOUR/MINUTE/DAY: gyldige vaerdier");
    }
  }
  printf(fails ? "%d FEJL\n" : "ALLE TESTS OK\n", fails);
  return fails ? 1 : 0;
}
