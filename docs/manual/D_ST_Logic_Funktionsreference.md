# Appendiks D: ST Logic Sprog- og Funktionsreference

[← Appendiks C: Ordliste](C_Ordliste.md) · [Indeks](00_INDEKS.md)

---

> Denne reference er udtrukket direkte fra kildekoden (`src/st_parser.cpp`, `src/st_compiler.cpp`, `src/st_vm.cpp`, `src/st_builtin_*.cpp`, `include/st_types.h`, `include/constants.h`) og er derfor autoritativ i forhold til hvad systemet faktisk understøtter. Se [kapitel 8](08_ST_Logic_Programmering.md) for en indførende gennemgang med eksempler — dette appendiks er et opslagsværk, ikke en tutorial.
>
> **Om de to ældre baggrundsdokumenter** `../ST_USAGE_GUIDE.md` og `../ST_IEC61131_COMPLIANCE.md`: sidstnævnte er stærkt forældet (beskriver en tidlig udviklingsfase — forkert INT-bredde, "FB'er ikke understøttet", ingen omtale af GLOBAL_VAR/STRING/STRUCT m.m.) og bør **ikke** bruges som kilde. `ST_USAGE_GUIDE.md` er langt mere opdateret, men mangler helt GLOBAL_VAR, brugerdefinerede FUNCTION/FUNCTION_BLOCK, TYPE/STRUCT, SAVE/LOAD, bit-funktionerne og STRING-funktionerne. Dette appendiks dækker alle af disse.

## D.1 Datatyper

| Type | Repræsentation | Grænser | Bemærkning |
|---|---|---|---|
| `BOOL` | 1 byte | FALSE(0) / TRUE(1) | |
| `INT` | `int16_t` | -32768 til 32767 | **16-bit** |
| `DINT` | `int32_t` | -2.147.483.648 til 2.147.483.647 | Heltalsliteraler over `INT`s grænse auto-forfremmes hertil |
| `DWORD` | `uint32_t` | 0 til 4.294.967.295 | Usigneret 32-bit |
| `REAL` | `float`, IEEE754 32-bit | ~±3,4×10³⁸ | Ingen `LREAL`/64-bit-variant |
| `TIME` | `int32_t` millisekunder internt | 0 til ~24,8 dage | Literal `T#5s`, `T#100ms`, `T#1h30m`, `T#2d5h30m15s100ms` — enheder `d h m s ms` kombinerbare |
| `STRING` | Fast kapacitet, reference-baseret internt | Maks 32 tegn | Literal `'tekst'`. Se D.5.13 for funktioner. **Kan ikke** `EXPORT`'es eller bindes til Modbus-registre/coils |
| `ARRAY[lo..hi] OF T` | — | Maks 24 elementer | Element-type ∈ {BOOL, INT, DINT, DWORD, REAL, TIME} — **ikke** STRING |

Der findes **ingen** `BYTE`, `WORD`, `LREAL`, `DATE`, `DATE_AND_TIME` eller `LTIME`.

## D.2 Deklarationsformer

| Konstruktion | Formål | Begrænsninger |
|---|---|---|
| `PROGRAM <navn> ... END_PROGRAM` | Navngiver programmet | **Valgfri** — både `PROGRAM`/`BEGIN` og den afsluttende `END_PROGRAM` kan udelades (ren VAR-blok + statements accepteres, bagudkompatibilitet) |
| `VAR ... END_VAR` | Lokale variable | Init-værdi valgfri (`x: INT := 5;`). **Rettet fejl (BUG-397f, v7.9.40.0):** en `REAL`-initialværdi hvis bitmønster tilfældigvis havde nul i de nederste 16 bit (fx `1.5`, `1.0`, `2.0`, `10.0` — ret almindelige værdier) blev tidligere tavst til `0.0` ved programstart. Ramte enhver `VAR`-deklaration med denne type værdi, ikke kun `CONST` |
| `VAR_INPUT ... END_VAR` | Læses fra Modbus-binding ved scan-start | Program-scope + FUNCTION/FUNCTION_BLOCK-parametre |
| `VAR_OUTPUT ... END_VAR` | Skrives til Modbus-binding ved scan-slut | I FUNCTION/FUNCTION_BLOCK-parametre: accepteres af parseren, men skrives **ikke** tilbage til kalderens variabel (ingen writeback-mekanisme findes) |
| `VAR_IN_OUT ... END_VAR` | Kun gyldig i FUNCTION/FUNCTION_BLOCK-parameterlister, ikke i selve programmets top-niveau | Samme non-writeback-begrænsning som `VAR_OUTPUT` |
| `EXPORT var1, var2, ...;` | Gør variable synlige i Modbus Input Registers 220-251 ("IR-poolen") | **Ikke** tilladt for STRUCT- eller STRING-variable (afvises af parseren med en klar fejl) |
| `GLOBAL_VAR ... END_VAR` | Separat, delt deklarationsblok — alle 4 programmer læser/skriver samme lager, uden om Modbus-bindinger | Kun skalar-typer (`BOOL INT DINT DWORD REAL TIME`) — ingen `VAR_INPUT/OUTPUT`, ingen `EXPORT`, ingen `ARRAY`/`STRING`, ingen init-værdi (alt starter på 0/FALSE). Maks 16 variable, kildetekst maks 1024 bytes |
| `FUNCTION <navn> : <returtype> ... END_FUNCTION` | Brugerdefineret, **stateless** funktion | Returtype påkrævet, skalar (ikke ARRAY/STRUCT). Returværdi sættes ved tildeling til funktionsnavnet (`DOUBLE := val*2;`). Parametre kaldes **kun positionelt** |
| `FUNCTION_BLOCK ... END_FUNCTION_BLOCK` | Brugerdefineret, **stateful** funktionsblok (tilstand bevares pr. kaldested) | Ingen returtype. Fuld kaldekonvention (feltadgang til output) er ikke udtømmende efterprøvet — brug indtil videre de indbyggede FB'er (D.5.6/D.5.7) til stateful logik |
| `TYPE <navn> : STRUCT ... END_STRUCT END_TYPE` | Struct-type, scoped til ét program | Kun skalar-felter, ingen nested STRUCT/ARRAY/STRING-felt. Maks 4 struct-typer og 8 felter pr. type. Feltadgang: `variabel.felt` |
| `CONST navn : TYPE := værdi;` | **Implementeret (BUG-397d, v7.9.39.0).** Præfiks på en almindelig VAR-deklaration — ikke IEC's separate `VAR CONSTANT...END_VAR`-blok | Kun skalar-typer (`BOOL INT DINT DWORD REAL TIME`), kun i almindelige `VAR`-blokke (afvist i `VAR_INPUT`/`VAR_OUTPUT`, `ARRAY`, `STRUCT`). Kræver en literal initialværdi ved deklaration. Enhver `:=`-tildeling til en `CONST` (almindelig, i en `FOR`-løkke, eller som FB-output-binding `Q=>const_var`) afvises ved kompilering med en klar fejl. **Håndhæves kun ved kompilering** — bindes en `CONST`-variabel som Modbus-**input** via REST/CLI, kan selve binding-mekanismen (et separat runtime-system) stadig overskrive den ved hver scan; brug ikke `CONST` til en variabel du også binder som input |

## D.3 Kontrolstrukturer (statements)

```st
IF <udtryk> THEN
  <statements>
[ELSIF <udtryk> THEN <statements>]...
[ELSE <statements>]
END_IF;

CASE <udtryk> OF
  <int>[, <int>...]: <statements>
  ...
[ELSE <statements>]
END_CASE;

FOR <var> := <start> TO <slut> [BY <step>] DO
  <statements>
END_FOR;

WHILE <udtryk> DO
  <statements>
END_WHILE;

REPEAT
  <statements>
UNTIL <udtryk>
[END_REPEAT];

EXIT;              (* bryder ud af inderste FOR/WHILE/REPEAT *)
RETURN [<udtryk>];  (* kun meningsfuld i FUNCTION/FUNCTION_BLOCK-krop *)
```

**Bemærkninger:**
- `CASE`-labels er **kun diskrete heltalskonstanter** (maks 8 kommaseparerede værdier pr. branch, maks 16 branches i alt) — der findes **ingen range-syntaks** (`1..5:`), på trods af at `..`-tokenet findes i lexeren (bruges kun til ARRAY-grænser).
- `FOR`s `BY <step>` kan være negativt (nedtælling). `BY 0` giver en uendelig løkke, som stoppes af den generelle 10.000-instruktioners scan-grænse (D.6) — ikke af en dedikeret nul-step-fejl.
- `END_REPEAT` er valgfri (selve `UNTIL <udtryk>` afslutter allerede løkken).
- Semikolon efter `END_IF`/`END_CASE` er valgfri.
- Løkke-nesting: maks **8 indlejrede** FOR/WHILE/REPEAT.
- Funktionskald/rekursion: maks **8 indlejrede** kald.

## D.4 Operatorer og præcedens

Fra **svagest** til **stærkest** binding:

| Niveau | Operatorer | Bemærkning |
|---|---|---|
| 1 | `OR`, `XOR` | Samme præcedensniveau, venstre-til-højre (afviger fra "streng" IEC 61131-3, hvor XOR normalt har sin egen præcedens mellem AND og OR) |
| 2 | `AND` | |
| 3 | `< > <= >= = <>` | Alle seks sammenligningsoperatorer har **samme** præcedensniveau |
| 4 | `+ -` (binær) | |
| 5 | `* / MOD SHL SHR` | `MOD`/`SHL`/`SHR` ligger på **samme** niveau som `*`/`/`, ikke separat |
| 6 | unær `-`, `NOT` | Højre-associativ |
| 7 | `()` `[]` `.` | Parentes, array-indeksering, struct-feltadgang, funktionskald |

**Aritmetik (`+ - * /  MOD`):**
- `+ - *`: type-forfremmelse INT→DINT→REAL. Wrapper stille ved overløb (to's-komplement) — ingen exception.
- `/` (DIV) **returnerer altid REAL**, uanset operandtyper. Division med `0.0` → runtime-fejl ("Division by zero"), programmet markeres fejlet.
- `MOD`: **C-remainder-semantik** (fortegn følger dividenden — `-7 MOD 3 = -1`, ikke matematisk modulo `2`). Modulo med 0 → runtime-fejl.
- `BOOL` er **ikke** en gyldig operand til `+ - * / MOD` — giver en runtime-typefejl. Konvertér først med `BOOL_TO_INT`.

> **`AND`/`OR`/`NOT`/`XOR` er rent booleske, ikke bitvise (BUG-397, rettet v7.9.36.0).** VM'en kræver nu eksplicit `BOOL`-operander til alle fire — et forsøg på fx `status AND 16#0020` (det almindelige IEC-idiom for at maskere/teste én bit i et heltal, set i flere ældre projektdokumenter) giver en klar runtime-fejl ("...requires BOOL operands, use BIT_SET/BIT_CLR/BIT_TST...") i stedet for tidligere at give et tavst forkert boolesk resultat. **Brug `BIT_SET`/`BIT_CLR`/`BIT_TST` (D.5.2) til bitmanipulation og `SHL`/`SHR` til skift.** Ingen kortslutningsevaluering — begge sider af `AND`/`OR` evalueres altid.
>
> **Hex- og radix-literaler (BUG-397, rettet v7.9.36.0):** `0xFF` (C-stil) og IEC-radix-formen `<radix>#<cifre>` (radix 2-16, fx `2#1010`, `8#17`, `16#FF`) er begge gyldige og beregner nu den korrekte værdi. Ældre firmware-versioner fejlfortolkede radix-formen tavst som decimal — hvis denne manual læses for en enhed flashet FØR v7.9.36.0, undgå radix-literaler og brug kun `0x..`.

**Bitvise skift (`SHL x,n` / `SHR x,n`):** ægte, type-bevidste bitoperationer. `DINT` og `DWORD`: skift 0-31 gyldigt (DWORD fik sin egen 32-bit-gren i BUG-397, rettet v7.9.36.0 — havde tidligere samme 16-bit-begrænsning som INT nedenfor, uden advarsel). `INT` (og enhver anden type): behandlet som 16-bit, skift 0-15 gyldigt. Skift udenfor det gyldige interval → runtime-fejl.

> **`DWORD`-aritmetik og -sammenligning (BUG-397c, rettet v7.9.38.0).** `+ - * MOD`, unær minus, og alle 6 sammenligningsoperatorer (`= <> < > <= >=`) understøtter nu `DWORD` korrekt (32-bit usigneret) — havde tidligere samme slags hul som `SHL`/`SHR` (kun en `REAL`- og en `DINT`-gren, `DWORD` faldt igennem til 16-bit `INT`-fortolkning uden fejl eller advarsel). `/` (DIV) var aldrig ramt (returnerer altid `REAL`). Overløb/underløb på `DWORD` wrapper som ventet for en usigneret 32-bit type (fx `UINT32_MAX + 1 = 0`). Blandes en `DWORD`-operand med en `DINT`-operand, vinder `DINT` (resultatet er `DINT`, `DWORD`-operandens bitmønster genfortolkes som signeret) — det praktiske, almindelige tilfælde er dog `DWORD`+`DWORD` eller `DWORD`+`INT`, som giver et `DWORD`-resultat.

**Sammenligning (`= <> < > <= >=`):** type-bevidst (REAL/DINT/INT efter samme forfremmelsesregel som aritmetik). `=`/`<>` understøtter **også STRING** (kun hvis begge operander er STRING — blanding giver runtime-fejl). `< > <= >=` understøtter **ikke** STRING.

## D.5 Indbyggede funktioner

**Kalderegel:** alle indbyggede funktioner kaldes **positionelt** (`FUNK(arg1, arg2)`), **undtagen** `TON, TOF, TP, CTU, CTD, CTUD` — disse seks understøtter **derudover** IEC-navngivet syntaks (`IN:=..., PT:=..., Q=>var`). Forsøg på navngivet syntaks for enhver anden funktion (inkl. `R_TRIG`/`F_TRIG`/`SR`/`RS`, som ellers også er IEC-standard-FB'er) afvises med en klar fejl.

### D.5.1 Matematik

| Funktion | Parametre | Retur | Særtilfælde |
|---|---|---|---|
| `ABS(x)` | INT\|DINT\|REAL | samme type | `INT16_MIN`/`INT32_MIN` klemmes til max (kan ikke negeres uden overløb) |
| `MIN(a,b)` / `MAX(a,b)` | INT\|DINT\|REAL (polymorf) | forfremmet fælles type | — |
| `SUM(a,b)` | samme | samme | Alias for `a+b` |
| `SQRT(x)` | REAL | REAL | Negativt input → `0.0` |
| `ROUND(x)` / `TRUNC(x)` / `FLOOR(x)` / `CEIL(x)` | REAL | INT | Klemmes til INT16-området |
| `SIN(x)` / `COS(x)` / `TAN(x)` | REAL (radianer) | REAL | — |
| `EXP(x)` | REAL | REAL | Overløb (x≳88,7) → `0.0` (ikke INF) |
| `LN(x)` / `LOG(x)` | REAL | REAL | x≤0 → `0.0` |
| `POW(x,y)` | REAL, REAL | REAL | NaN/INF-resultat → `0.0` |
| `LIMIT(min,val,max)` | INT\|DINT\|REAL (polymorf) | forfremmet fælles type | Ingen validering af `min≤max` — omvendte grænser kan give kontraintuitivt resultat |

### D.5.2 Bit-operationer

| Funktion | Parametre | Retur | Særtilfælde |
|---|---|---|---|
| `BIT_SET(value, bit_pos)` | INT, INT | INT | `bit_pos` maskes altid til `& 0x0F` (0-15) — værdier udenfor wrapper stille rundt |
| `BIT_CLR(value, bit_pos)` | INT, INT | INT | Samme maskering |
| `BIT_TST(value, bit_pos)` | INT, INT | **BOOL** | Samme maskering |
| `ROL(in, n)` / `ROR(in, n)` | INT\|DINT\|DWORD, INT | samme type som `in` | 16-bit rotation for INT (n mod 16), 32-bit for DINT/DWORD (n mod 32). Negative `n` normaliseres korrekt |

`BIT_SET`/`BIT_CLR`/`BIT_TST` er kun bekræftet korrekte for 16-bit-værdier (adressebredden er altid 4 bit) — brug ikke til bit 16-31 på DINT/DWORD.

### D.5.3 Type-konvertering

| Funktion | Fra → Til | Særtilfælde |
|---|---|---|
| `INT_TO_REAL(i)` | INT → REAL | — |
| `REAL_TO_INT(r)` | REAL → INT | Trunkerer mod nul, klemmes til INT16 |
| `BOOL_TO_INT(b)` | BOOL → INT | TRUE→1, FALSE→0 |
| `INT_TO_BOOL(i)` | INT → BOOL | 0→FALSE, alt andet→TRUE |
| `DWORD_TO_INT(d)` | DWORD → INT | Værdier over `INT16_MAX` klemmes til 32767 (ikke bit-trunkering) |
| `INT_TO_DWORD(i)` | INT → DWORD | Negative tal wrapper til store usignerede værdier (to's-komplement-reinterpretation) |

### D.5.4 Valg/multipleks

| Funktion | Parametre | Retur | Semantik |
|---|---|---|---|
| `SEL(g, in0, in1)` | BOOL, T, T | forfremmet fælles type | `g=FALSE→in0`, `g=TRUE→in1` |
| `MUX(k, in0, in1, in2)` | INT, T, T, T | forfremmet fælles type | `k=0/1/2 → in0/in1/in2`. `k` udenfor 0-2 → returnerer `in0` (fallback, ingen fejl) |

### D.5.5 Kant-detektion

| FB | Parameter | Retur | Semantik |
|---|---|---|---|
| `R_TRIG(CLK)` | `CLK: BOOL` | BOOL | TRUE i præcis ét scan ved 0→1-flanke |
| `F_TRIG(CLK)` | `CLK: BOOL` | BOOL | TRUE i præcis ét scan ved 1→0-flanke |

Maks 8 instanser pr. program (én pr. syntaktisk kaldested i koden, se D.6). **Kun positionel** kaldeform, selvom det er ægte IEC-FB'er med et `CLK`-navn.

### D.5.6 Timere — funktionsblokke (TON/TOF/TP)

| FB | Positionelt | Navngivet input | Navngivet output | Semantik |
|---|---|---|---|---|
| `TON(IN, PT)` | `IN:BOOL, PT:TIME` → BOOL | `IN`, `PT` | `Q`(BOOL), `ET`(elapsed, TIME) | On-delay: `Q`→TRUE når `IN` har været TRUE i `PT`. `Q`→FALSE øjeblikkeligt når `IN`→FALSE |
| `TOF(IN, PT)` | samme | samme | samme | Off-delay: `Q`→TRUE øjeblikkeligt ved `IN`→TRUE. `Q`→FALSE først `PT` efter `IN`→FALSE |
| `TP(IN, PT)` | samme | samme | samme | Pulstimer: rising edge på `IN` udløser en fast puls af `PT`'s varighed på `Q` — et nyt rising-edge mens pulsen kører ignoreres |

Negativ `PT` behandles som `PT=0` (ingen fejl). Eksempel på begge kaldeformer:
```st
q_var := TON(start, T#2s);
TON(IN := start, PT := T#2s, Q => motor, ET => elapsed);
```
Maks 8 timer-instanser pr. program.

### D.5.7 Tællere — funktionsblokke (CTU/CTD/CTUD)

> **Adskil disse fra de fysiske hardware-tællere `CNT_*` i D.5.8** — helt forskellige mekanismer.

| FB | Positionelt | Navngivne input | Navngivne output | Semantik |
|---|---|---|---|---|
| `CTU(CU, RESET, PV)` | `CU,RESET:BOOL, PV:DINT` → BOOL | `CU`, `RESET`, `PV` | `Q`(BOOL), `CV`(DINT) | Tæller op ved rising edge på `CU`. `RESET=TRUE` nulstiller `CV` (højeste prioritet). `Q=TRUE` når `CV≥PV` |
| `CTD(CD, LOAD, PV)` | `CD,LOAD:BOOL, PV:DINT` → BOOL | `CD`, `LOAD`, `PV` | `Q`, `CV` | Tæller ned ved rising edge på `CD`. `LOAD`-rising-edge sætter `CV:=PV`. `Q=TRUE` når `CV≤0`. `CV` klemmes til 0 (intet underløb) |
| `CTUD(CU, CD, RESET, LOAD, PV)` | alle BOOL/DINT → BOOL (QU) | `CU`,`CD`,`RESET`,`LOAD`,`PV` | `QU`,`QD`(BOOL),`CV`(DINT) | Op/ned. Prioritet `RESET`>`LOAD`>`CU`/`CD`. `QU` når `CV≥PV`, `QD` når `CV≤0`. `CV` klemmes til minimum 0 |

`CV` klemmes ved `INT32_MAX` (stopper med at stige i stedet for at wrappe). Maks 8 tæller-instanser pr. program.

### D.5.8 Hardware-tællere (CNT_*)

Styrer 4 dedikerede fysiske GPIO/PCNT-tællere (id 1-4) — se [kapitel 9](09_Taellere_og_Timere.md) for den fulde konfigurationsmodel. Kun **positionel** kaldeform (ikke IEC-standard-FB'er).

| Funktion | Parametre | Retur | Semantik |
|---|---|---|---|
| `CNT_SETUP(id, hw_mode, edge, dir, prescaler, gpio)` | alle INT | BOOL | `hw_mode`(0=SW,1=SW_ISR,2=HW_PCNT), `edge`(0=RISING,1=FALLING,2=BOTH), `dir`(0=UP,1=DOWN). `id` udenfor 1-4 → FALSE |
| `CNT_SETUP_ADV(id, scale, bit_width, debounce_ms, start_value)` | INT,REAL,INT,INT,DINT | BOOL | `bit_width` accepterer kun 8/16/32/64 (andet ignoreres stille) |
| `CNT_SETUP_CMP(id, cmp_mode, cmp_value, cmp_source, reset_on_read)` | INT'e | BOOL | `cmp_mode`(0=≥,1=>,2=eksakt), `cmp_source`(0=raw,1=prescaled,2=scaled) |
| `CNT_ENABLE(id, on_off)` | INT, BOOL | BOOL | Aktiverer/deaktiverer |
| `CNT_CTRL(id, cmd)` | INT, INT | BOOL | `cmd`: 0=reset, 1=start, 2=stop |
| `CNT_VALUE(id)` | INT | **DINT** | Skaleret værdi (`raw × scale_factor`, afrundet) |
| `CNT_RAW(id)` | INT | **DINT** | Rå tællerværdi ÷ prescaler |
| `CNT_FREQ(id)` | INT | INT | Frekvens i Hz |
| `CNT_STATUS(id)` | INT | INT | Bitfelt: bit0=running, bit1=overflow, bit2=compare_hit |

### D.5.9 Modbus Master

Alle kald er **asynkrone/non-blocking**: en læsning returnerer en cachet værdi med det samme og kø'er en baggrundsopdatering; en skrivning kø'es og returnerer straks. Se [§8.7](08_ST_Logic_Programmering.md#87-modbus-master-fra-st-logic).

| Funktion | Parametre | Retur | FC | Semantik |
|---|---|---|---|---|
| `MB_READ_COIL(slave, addr)` | INT, INT | BOOL | 01 | Cachet coil |
| `MB_READ_INPUT(slave, addr)` | INT, INT | BOOL | 02 | Cachet discrete input |
| `MB_READ_HOLDING(slave, addr)` | INT, INT | INT | 03 | Cachet holding register |
| `MB_READ_INPUT_REG(slave, addr)` | INT, INT | INT | 04 | Cachet input register |
| `MB_WRITE_COIL(slave, addr, val)` | INT, INT, BOOL | BOOL (queued) | 05 | Kø'er skrivning |
| `MB_WRITE_HOLDING(slave, addr, val)` | INT, INT, INT | BOOL (queued) | 06 | Kø'er skrivning |
| `MB_READ_HOLDINGS(slave, addr, count)` | INT, INT, INT (1-16) | `ARRAY OF INT` | 03 (multi) | Se særskilt syntaks nedenfor |
| `MB_WRITE_HOLDINGS(slave, addr, count)` | INT, INT, INT (1-16) | `ARRAY OF INT` | 16 | Se særskilt syntaks nedenfor |
| `MB_SUCCESS()` | — | BOOL | — | Se semantik-advarsel i [§8.9](08_ST_Logic_Programmering.md#89-fejlhaandtering-og-graenser) |
| `MB_READ_OK()` | — | BOOL | — | **BUG-397e (v7.9.39.0).** Uafhængig af `MB_SUCCESS()` — afspejler altid seneste `MB_READ_*`-kald, uanset hvor mange `MB_WRITE_*`-kald der er sket siden. For `MB_READ_HOLDINGS` (multi-register) betyder den "blev sat i kø", ikke "arrayet har gyldige data" — samme asymmetri som `MB_SUCCESS()` altid har haft der |
| `MB_WRITE_QUEUED()` | — | BOOL | — | **BUG-397e (v7.9.39.0).** Uafhængig af `MB_SUCCESS()` — afspejler altid seneste `MB_WRITE_*`-kald, uanset hvor mange `MB_READ_*`-kald der er sket siden |
| `MB_BUSY()` | — | BOOL | — | TRUE hvis async-køen har ventende requests |
| `MB_ERROR()` | — | INT | — | Sidste fejlkode, se tabel nedenfor |
| `MB_CACHE(enabled)` | BOOL | BOOL (forrige tilstand) | — | Til/fra for cache-dedup |

> **`MB_READ_HOLDINGS`/`MB_WRITE_HOLDINGS` har en påkrævet, særskilt array-syntaks** — de kan **ikke** kaldes som almindelige udtryk (compileren afviser det med en direkte fejlbesked):
> ```st
> VAR regs: ARRAY[0..7] OF INT; END_VAR
> regs := MB_READ_HOLDINGS(1, 100, 8);        (* LÆS: array PÅ VENSTRE side af := *)
> MB_WRITE_HOLDINGS(1, 200, 8) := regs;       (* SKRIV: array PÅ HØJRE side af := *)
> ```
> `MB_WRITE_COIL`/`MB_WRITE_HOLDING` (ental) har derimod to gyldige former: almindeligt kald (`ok := MB_WRITE_COIL(1,0,TRUE);`) **og** assignment-form (`MB_WRITE_COIL(1,0) := TRUE;`).

**Modbus-fejlkoder** (`MB_ERROR()`s returværdi):

| Kode | Navn | Betydning |
|---|---|---|
| 0 | `MB_OK` | Succes |
| 1 | `MB_TIMEOUT` | Intet svar fra slave indenfor timeout |
| 2 | `MB_CRC_ERROR` | CRC-fejl i svar |
| 3 | `MB_EXCEPTION` | Modbus exception-svar |
| 4 | `MB_MAX_REQUESTS_EXCEEDED` | Rate-limit/kø/cache fuld |
| 5 | `MB_NOT_ENABLED` | Modbus Master ikke aktiveret |
| 6 | `MB_INVALID_SLAVE` | Slave-ID udenfor 1-247 |
| 7 | `MB_INVALID_ADDRESS` | Adresse udenfor 0-65535 |
| 8 | `MB_BUS_BUSY` | Kunne ikke opnå UART-mutex indenfor 2s (bussen optaget) |

### D.5.9b Modbus Expansion Board (MBX_*, FEAT-410)

Samme non-blocking cache/kø-mønster som D.5.9's `MB_*`-familie, blot mod en ekstern "HypervisionPLC Extension Board" over Modbus TCP i stedet for den lokale RS485-bus — de to første argumenter (`board`, `kanal`) vælger hvilket board (1-8, se [kapitel 6.7](06_Modbus_Interface.md#67-modbus-expansion-boards-feat-409)) og hvilken kanal (1-8, A=1/B=2) forespørgslen gælder. **v1: kun enkelt-register-operationer** — der findes ingen `MBX_READ_HOLDINGS`/`MBX_WRITE_HOLDINGS` (multi-register array-form) endnu.

| Funktion | Parametre | Retur | FC | Semantik |
|---|---|---|---|---|
| `MBX_READ_COIL(board, kanal, slave, addr)` | INT×4 | BOOL | 01 | Cachet coil på det eksterne board |
| `MBX_READ_INPUT(board, kanal, slave, addr)` | INT×4 | BOOL | 02 | Cachet discrete input |
| `MBX_READ_HOLDING(board, kanal, slave, addr)` | INT×4 | INT | 03 | Cachet holding register |
| `MBX_READ_INPUT_REG(board, kanal, slave, addr)` | INT×4 | INT | 04 | Cachet input register |
| `MBX_WRITE_COIL(board, kanal, slave, addr, val)` | INT×4, BOOL | BOOL (queued) | 05 | Kø'er skrivning |
| `MBX_WRITE_HOLDING(board, kanal, slave, addr, val)` | INT×4, INT | BOOL (queued) | 06 | Kø'er skrivning |
| `MBX_SUCCESS()` | — | BOOL | — | TRUE hvis seneste `MBX_*`-kald lykkedes |
| `MBX_BUSY()` | — | BOOL | — | TRUE hvis expansion-data-kø har ventende forespørgsler |
| `MBX_ERROR()` | — | INT | — | Seneste fejlkode — samme `mb_error_code_t`-tabel som D.5.9's `MB_ERROR()` |

`board` skal referere til et faktisk konfigureret board (se System-siden/`show modbus-expansion`), ellers returnerer kaldet `MB_INVALID_ADDRESS` uden at forsøge noget netværkskald. Diagnostik: `show modbus-expansion queue` (CLI) viser kø-dybde, cache-hits/misses, og aktiv backoff pr. (board, kanal, slave).

### D.5.10 Persistens

| Funktion | Parameter | Retur | Semantik |
|---|---|---|---|
| `SAVE(group_id)` | INT (0=alle, 1-8=specifik gruppe) | INT (0=success, -1=fejl, -2=rate-limited) | Snapshotter register-gruppe(r) til NVS. **Rate-limit: maks 1 kald pr. 5 sekunder**, delt på tværs af alle kald i programmet |
| `LOAD(group_id)` | INT (0=alle, 1-8) | INT (0=success, -1=fejl) | Genindlæser fra NVS |

### D.5.11 Bistabile latches (SR/RS)

| FB | Parametre | Retur | Prioritet | Sandhedstabel |
|---|---|---|---|---|
| `SR(S1, R)` | BOOL, BOOL | BOOL | **Reset**-dominant | `R=1→Q=0` (uanset S1); `R=0,S1=1→Q=1`; ellers hold |
| `RS(S, R1)` | BOOL, BOOL | BOOL | **Set**-dominant | `S=1→Q=1` (uanset R1); `S=0,R1=1→Q=0`; ellers hold |

> Navngivningen er modsat af hvad man intuitivt ville gætte: `SR` er reset-dominant, `RS` er set-dominant.

### D.5.12 Signalbehandling

| Funktion | Parametre | Retur | Semantik | Særtilfælde |
|---|---|---|---|---|
| `SCALE(IN, IN_MIN, IN_MAX, OUT_MIN, OUT_MAX)` | REAL×5 | REAL | Lineær skalering, `IN` klemmes til `[IN_MIN,IN_MAX]` før skalering | `IN_MAX=IN_MIN` → returnerer `0.0` |
| `HYSTERESIS(IN, HIGH, LOW)` | REAL, REAL, REAL | BOOL | Schmitt-trigger: til ved `IN>HIGH`, fra ved `IN<LOW`, hold i dødzonen | `HIGH≤LOW` (ugyldige grænser) → altid FALSE |
| `BLINK(ENABLE, ON_TIME, OFF_TIME)` | BOOL, INT(ms), INT(ms) | BOOL | Periodisk puls, starter i ON-fase ved `ENABLE`→TRUE | Negativ `ON_TIME`/`OFF_TIME` → deaktiveret, returnerer FALSE |
| `FILTER(IN, TIME_CONSTANT)` | REAL, INT(ms) | REAL | 1.-ordens lavpasfilter, baseret på programmets faktiske scan-cyklustid | `TIME_CONSTANT≤0` → ingen filtrering (værdien passerer uændret) |

### D.5.13 String-funktioner

| Funktion | Parametre | Retur | Semantik | Særtilfælde |
|---|---|---|---|---|
| `LEN(s)` | STRING | INT | Antal tegn (ekskl. terminator) | — |
| `CONCAT(s1, s2)` | STRING, STRING | STRING | Sammenkæder | Afkortes stille ved overløb af 32-tegns-grænsen |
| `LEFT(s, n)` | STRING, INT | STRING | De `n` første tegn | `n<0`→0. `n`>længde→klemmes til hele strengen |
| `RIGHT(s, n)` | STRING, INT | STRING | De `n` sidste tegn | Samme klemning som LEFT |
| `MID(s, start, len)` | STRING, INT, INT | STRING | Delstreng fra `start` (**1-indekseret**, IEC-stil) med længde `len` | `start<1` eller udenfor strengen, eller `len≤0` → tom streng |

STRING-variable kan ikke `EXPORT`'es eller bindes til Modbus-registre/coils (D.1).

## D.6 Grænser (opslagstabel)

| Ressource | Grænse |
|---|---|
| Programmer (Logic1-4) | 4 |
| Kildekode pr. program | 5.000 bytes |
| Variable pr. program (inkl. array-elementer) | 32 |
| Array-størrelse | 24 elementer |
| STRING-længde | 32 tegn |
| STRING-literaler pr. program | 8 |
| GLOBAL_VAR-variable | 16 (kildetekst maks 1.024 bytes) |
| STRUCT-typer pr. program | 4 (maks 8 felter pr. type) |
| Brugerdefinerede funktioner pr. program | 16 |
| Parametre pr. FUNCTION/FUNCTION_BLOCK | 8 |
| Nestede/rekursive funktionskald | 8 |
| Indlejrede FOR/WHILE/REPEAT | 8 |
| CASE-branches / værdier pr. label | 16 / 8 |
| VM-stakdybde (udtryksevaluering) | 64 værdier |
| VM-instruktioner pr. scan (NORMAL) | 10.000 |
| VM-instruktioner pr. scan (HIGH) | 1.000 (+ 5 ms hårdt wallclock-loft) |
| Timer-/edge-/tæller-/latch-instanser (hver type) | 8 pr. program |
| Hardware-tællere (CNT_*, fysiske) | 4 (id 1-4) |
| Modbus Master multi-register (HOLDINGS) | 1-16 registre pr. kald |
| Persistensgrupper (SAVE/LOAD) | 8 (id 1-8) + id 0 = alle |
| SAVE()-rate-limit | 1 kald / 5 sekunder |

> **Instans-grænserne gælder pr. syntaktisk kaldested i kildekoden**, ikke pr. variabelnavn: kompilatoren allokerer et nyt instans-ID hver gang den under kompilering møder et nyt `TON(...)`/`SR(...)`/osv.-kald i teksten. To tekstuelt forskellige kald er automatisk to uafhængige timere/tællere/latches — men kaldes den **samme** kaldelinje gentagne gange (fx i en `FOR`-løkke), er det stadig kun **én** fysisk instans, genbrugt hver iteration.

## D.7 Kendte begrænsninger og faldgruber

- **`CASE` understøtter ikke værdi-ranges** (`1..5:`), kun diskrete kommaseparerede heltal.
- ~~`CONST` er reserveret, men ikke implementeret~~ — **implementeret i BUG-397d, v7.9.39.0** (se D.2). Håndhæves kun ved kompilering, ikke ved Modbus-input-binding — se advarslen i D.2.
- **`MB_READ_HOLDINGS`/`MB_WRITE_HOLDINGS`** kræver den særlige array-tildelingssyntaks i D.5.9 — almindelig funktionskaldssyntaks afvises af kompilatoren.
- **`MB_SUCCESS()` betyder noget forskelligt efter READ vs. WRITE** — se advarslen i [§8.9](08_ST_Logic_Programmering.md#89-fejlhaandtering-og-graenser). Brug `MB_READ_OK()`/`MB_WRITE_QUEUED()` (D.5.9) i nyt ST-kode for en retnings-uafhængig, altid-korrekt værdi.
- ~~`AND`/`OR`/`NOT`/`XOR` er rent booleske~~ / ~~hex-radix-literaler fejlfortolkes~~ / ~~`SHL`/`SHR` på `DWORD` er 16-bit-begrænset~~ — alle tre **rettet i BUG-397, v7.9.36.0** (se D.4).
- ~~`DWORD`-aritmetik/-sammenligning falder igennem til 16-bit `INT`~~ — **rettet i BUG-397c, v7.9.38.0** (se D.4).

---

[← Appendiks C: Ordliste](C_Ordliste.md) · [Indeks](00_INDEKS.md)
