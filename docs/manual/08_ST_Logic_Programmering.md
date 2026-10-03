# 8. ST Logic-programmering

[← 7. REST API](07_REST_API.md) · [Indeks](00_INDEKS.md) · Næste: [9. Tællere & Timere →](09_Taellere_og_Timere.md)

---

## 8.1 Overblik

ST Logic er systemets programmerbare logiklag — et sprog inspireret af **IEC 61131-3 Structured Text**, kompileret til bytecode og eksekveret på en indbygget VM. Op til **4 uafhængige programmer** kan køre samtidig, hver med egen scan-cyklus, eget variabelrum og egen fejltilstand.

Programmer skrives, kompileres, uploades og fejlsøges enten i [web-editoren](04_Web_Dashboard_og_Monitor.md#43-st-logic-editor) eller via [REST API](07_REST_API.md) (`/api/logic/{id}/source`) — praktisk til CI/CD-baseret deployment af logik fra en ekstern kilde.

## 8.2 Grundlæggende struktur

```st
PROGRAM Eksempel
VAR
  teller: INT := 0;
  aktiv: BOOL;
  temperatur: REAL;
END_VAR

BEGIN
  teller := teller + 1;
  IF teller > 100 THEN
    aktiv := TRUE;
    teller := 0;
  END_IF;
END_PROGRAM
```

Hvert program eksekveres fra `BEGIN` til `END_PROGRAM` **én gang pr. scan-cyklus** — cyklustiden er konfigurerbar (typisk titals til hundredvis af millisekunder). Der er ingen løkke omkring hele programmet i kildekoden; scheduleren kalder det gentagne gange.

## 8.3 Typesystem

| Type | Størrelse | Beskrivelse |
|------|-----------|-------------|
| `BOOL` | 1 bit (repræsenteret som int) | Sand/falsk |
| `INT` | 16-bit signeret | -32768 til 32767 |
| `DINT` | 32-bit signeret | Store heltal |
| `DWORD` | 32-bit usigneret | Bitmasker, store positive tal |
| `REAL` | 32-bit IEEE 754 flydende | Decimaltal |
| `TIME` | 32-bit (ms), skrives `T#5s`, `T#100ms`, `T#1h30m`, `T#2d5h30m15s100ms` | Tidsintervaller til timere |
| `STRING` | Fast kapacitet, op til 32 tegn, skrives `'tekst'` | Tekststrenge — se boks nedenfor |

Literaler over `INT`s grænse (>32767) auto-forfremmes til `DINT`. Arrays understøttes: `regs: ARRAY[0..15] OF INT;` (ikke for `STRING`).

> **`STRING`-begrænsninger:** alle strenge har en fast maks-længde på 32 tegn (længere literaler/resultater afkortes stille, ingen fejl) — der er ingen `STRING[N]`-størrelsessyntaks. Indbyggede funktioner: `LEN(s)` (→ INT), `CONCAT(s1,s2)`, `LEFT(s,n)`, `RIGHT(s,n)`, `MID(s,start,len)` (1-indekseret `start`, IEC-stil) — alle → STRING undtagen `LEN`. `STRING`-variabler kan **ikke** `EXPORT`'es til IR-poolen og kan **ikke** bindes til Modbus-registre/coils (en registerskrivning ville ellers overskrive variablens interne reference med vilkårlige bits). Programmer der bruger `STRING` genkompileres altid fra kildekode ved boot (ingen bytecode-cache).

## 8.4 Kontrolstrukturer

```st
IF <betingelse> THEN ... ELSIF <betingelse> THEN ... ELSE ... END_IF;
CASE <udtryk> OF
  1: ...
  2, 3: ...    (* komma-separerede vaerdier, deler samme kode *)
  -1: ...      (* negative labels understoettet *)
ELSE ...
END_CASE;
FOR i := 0 TO 9 DO ... END_FOR;
FOR i := 10 TO 1 BY -1 DO ... END_FOR;  (* nedtaelling: BY med negativt step *)
WHILE <betingelse> DO ... END_WHILE;
REPEAT ... UNTIL <betingelse> END_REPEAT;
```

Der er en **sikkerhedsgrænse på 10.000 VM-instruktioner pr. scan-cyklus** — et program der låser sig fast i en uendelig løkke stoppes af systemet i stedet for at blokere resten af PLC'en permanent.

## 8.5 Indbyggede funktioner (overblik)

> Tabellen nedenfor er et overblik. For fulde signaturer, parametertyper, kaldeformer, IEC-navngivne parametre og alle kendte kant-tilfælde/faldgruber, se [Appendiks D: ST Logic Funktionsreference](D_ST_Logic_Funktionsreference.md).

| Gruppe | Funktioner |
|--------|-----------|
| **Matematik** | `ABS SQRT POW LOG LN EXP SIN COS TAN CEIL FLOOR ROUND TRUNC MIN MAX LIMIT SUM` |
| **Bit-operationer** | `BIT_SET BIT_CLR BIT_TST ROL ROR` |
| **Type-konvertering** | `INT_TO_REAL REAL_TO_INT INT_TO_DWORD DWORD_TO_INT BOOL_TO_INT INT_TO_BOOL` |
| **Valg/multipleks** | `SEL MUX` |
| **Kant-detektion** | `R_TRIG` (stigende flanke), `F_TRIG` (faldende flanke) |
| **Timere (funktionsblokke)** | `TON` (forsinket til), `TOF` (forsinket fra), `TP` (puls) |
| **Tællere (funktionsblokke)** | `CTU` (op), `CTD` (ned), `CTUD` (op/ned) |
| **Hardware-tællere** | `CNT_SETUP CNT_SETUP_ADV CNT_SETUP_CMP CNT_CTRL CNT_ENABLE CNT_VALUE CNT_RAW CNT_FREQ CNT_STATUS` — se [kapitel 9](09_Taellere_og_Timere.md) |
| **Modbus Master** | `MB_READ_COIL MB_READ_INPUT MB_READ_HOLDING MB_READ_INPUT_REG MB_READ_HOLDINGS MB_WRITE_COIL MB_WRITE_HOLDING MB_WRITE_HOLDINGS MB_WRITE_COILS MB_SUCCESS MB_ERROR MB_BUSY MB_CACHE` — se §8.7 |
| **Modbus Expansion Board** | `MBX_READ_COIL MBX_READ_INPUT MBX_READ_HOLDING MBX_READ_INPUT_REG MBX_WRITE_COIL MBX_WRITE_HOLDING MBX_WRITE_HOLDINGS MBX_WRITE_COILS MBX_SUCCESS MBX_BUSY MBX_ERROR` — samme cache/kø-mønster som `MB_*`, men mod et eksternt board (`board`/`kanal` som de to første argumenter), se [§6.7](06_Modbus_Interface.md#67-modbus-expansion-boards-feat-409) og [Appendiks D.5.9b](D_ST_Logic_Funktionsreference.md#d59b-modbus-expansion-board-mbx_-feat-410) |
| **Persistens** | `SAVE LOAD` — gem/genindlæs registergrupper til/fra NVS på tværs af reboot |
| **Bistabile latches** | `SR(S1, R)`, `RS(S, R1)` |
| **Signalbehandling** | `SCALE(IN, IN_MIN, IN_MAX, OUT_MIN, OUT_MAX)`, `HYSTERESIS(IN, HIGH, LOW)`, `BLINK(ENABLE, ON_TIME, OFF_TIME)`, `FILTER(IN, TIME_CONSTANT)` |

> **Latches og signalbehandling** — ligesom `TON`/`CTU`/osv. huskes tilstanden internt pr. **kaldested** i koden, ikke i en navngivet variabel, så to kald af samme funktion i samme program er automatisk to uafhængige instanser.
>
> - `SR(S1, R)` — bistabil latch med **reset-prioritet**: `R=1` → `Q=0` uanset `S1`; `R=0, S1=1` → `Q=1`; ellers holder `Q` sin forrige værdi.
> - `RS(S, R1)` — bistabil latch med **set-prioritet**: `S=1` → `Q=1` uanset `R1`; `S=0, R1=1` → `Q=0`; ellers holder `Q` sin forrige værdi. Bemærk navngivningen: `SR` er reset-dominant og `RS` er set-dominant — modsat af hvad man intuitivt ville gætte ud fra navnet.
> - `SCALE(IN, IN_MIN, IN_MAX, OUT_MIN, OUT_MAX)` → REAL — lineær skalering fra ét interval til et andet. `IN` klippes til `[IN_MIN, IN_MAX]` før skalering. Returnerer `0.0` hvis `IN_MIN=IN_MAX` (undgår division med nul).
> - `HYSTERESIS(IN, HIGH, LOW)` → BOOL — Schmitt-trigger: slår **til** når `IN > HIGH`, slår **fra** når `IN < LOW`, holder forrige tilstand i dødzonen (`LOW ≤ IN ≤ HIGH`). Ugyldige grænser (`HIGH ≤ LOW`) giver altid `FALSE`.
> - `BLINK(ENABLE, ON_TIME, OFF_TIME)` → BOOL — periodisk pulsgenerator; `ON_TIME`/`OFF_TIME` er `INT` i millisekunder. Starter i ON-fasen når `ENABLE` går sand. Negative tidsværdier deaktiverer blink (returnerer `FALSE`).
> - `FILTER(IN, TIME_CONSTANT)` → REAL — 1.-ordens lavpasfilter (eksponentielt glidende gennemsnit), `TIME_CONSTANT` er `INT` i millisekunder, baseret på programmets faktiske scan-cyklustid. `TIME_CONSTANT ≤ 0` slår filtrering fra (værdien passerer uændret igennem).

> **Vigtig afvigelse fra standard IEC 61131-3:** timere og tællere kaldes som **funktioner**, ikke som instansierede funktionsblokke. Der findes **ingen** `blink_timer: TON;`-deklaration i `VAR`-blokken, og **ingen** `.Q`/`.ET`-punktum-adgang — det er den mest almindelige begynderfejl, og parseren fejler på det med det samme (typisk "Expected data type (BOOL, INT, DINT, DWORD, REAL, TIME, ARRAY)" hvis man deklarerer `navn: TON;`).
>
> Kald i stedet timeren/tælleren direkte som en funktion, med IEC-navngivne parametre — inputs med `:=`, outputs med `=>` ind i almindelige variabler:
> ```st
> VAR start: BOOL; motor: BOOL; elapsed: TIME; END_VAR
> TON(IN := start, PT := T#2s, Q => motor, ET => elapsed);
> ```
> Samme mønster gælder `TOF`, `TP`, `CTU`, `CTD`, `CTUD`, `R_TRIG`, `F_TRIG`. En simplere **positionel** syntaks understøttes også for TON/CTU/CTD (uden navngivne outputs, returværdien er `Q`): `res := TON(start, T#2s);`.
>
> Tilstanden (elapsed tid, om timeren løber, osv.) huskes internt af systemet pr. **kaldested** i koden — ikke i en navngivet variabel — så to forskellige `TON(...)`-kald i samme program er automatisk to uafhængige timere, uden at man selv skal navngive eller allokere dem.

Se [`../ST_USAGE_GUIDE.md`](../ST_USAGE_GUIDE.md) og [`../ST_IEC61131_COMPLIANCE.md`](../ST_IEC61131_COMPLIANCE.md) for fuld syntaksdetalje og afvigelser fra standarden.

## 8.6 Kom i gang: et første program

```st
PROGRAM Blink
VAR
  led_state: BOOL := FALSE;
  timer_in: BOOL;
  timer_q: BOOL;
END_VAR

BEGIN
  timer_in := NOT timer_q;
  TON(IN := timer_in, PT := T#500ms, Q => timer_q);
  IF timer_q THEN
    led_state := NOT led_state;
  END_IF;
END_PROGRAM
```

Kompilér (**Kompilér**-knappen i editoren, eller `POST /api/logic/1/source` efterfulgt af kompilering), aktivér programmet (`set logic 1 enabled on`), og bekræft i Runtime Monitor at `led_state` og `timer_q` skifter ca. hvert 500 ms. (`timer_q` er kun sand i det scan hvor tiden netop er udløbet — det er derfor selve toggle-flowet går via `led_state`, ikke `timer_q` direkte.)

Bind `led_state` til en fysisk digital udgang via **Bindings**-fanen i editoren for at se det på hardware.

## 8.7 Modbus Master fra ST Logic

Alle `MB_*`-kald er **ikke-blokerende**: et kald returnerer med det samme (evt. med en cachet værdi fra sidste vellykkede læsning), mens den faktiske forespørgsel sendes i baggrunden. Det betyder ST-scannet aldrig venter på et langsomt eller udeblevet svar fra en ekstern enhed.

```st
PROGRAM RemotePolling
VAR
  slave_id: INT := 1;
  temp_raw: INT;
  ok: BOOL;
END_VAR

BEGIN
  temp_raw := MB_READ_HOLDING(slave_id, 0);
  ok := MB_SUCCESS();

  IF ok AND temp_raw > 500 THEN
    MB_WRITE_COIL(slave_id, 0, TRUE);   (* alarm-relæ på ekstern enhed *)
  END_IF;
END_PROGRAM
```

**Multi-register-varianter** (`MB_READ_HOLDINGS`/`MB_WRITE_HOLDINGS`) læser/skriver op til 16 registre i én forespørgsel, ind i/fra et ST-array. Bemærk at retningen af `:=` afgør læs vs. skriv — arrayet står til **venstre** ved læsning, til **højre** ved skrivning (kompilatoren afviser eksplicit den omvendte form med en fejlbesked):

```st
VAR
  regs: ARRAY[0..7] OF INT;
END_VAR
BEGIN
  regs := MB_READ_HOLDINGS(slave_id, 100, 8);   (* læs 8 registre fra adresse 100 ind i regs[] *)
  MB_WRITE_HOLDINGS(slave_id, 200, 8) := regs;  (* skriv regs[] til 8 registre fra adresse 200 *)
END_PROGRAM
```

**`MB_WRITE_COILS`** (v7.9.68.0, FC15) er coil-modstykket til `MB_WRITE_HOLDINGS` — samme array-syntaks, men kræver `ARRAY OF BOOL`:

```st
VAR
  bits: ARRAY[0..7] OF BOOL;
END_VAR
BEGIN
  bits[0] := TRUE;
  bits[1] := FALSE;
  MB_WRITE_COILS(slave_id, 300, 2) := bits;   (* skriv bits[0..1] til 2 coils fra adresse 300 *)
END_PROGRAM
```

Se [kapitel 6](06_Modbus_Interface.md#65-modbus-master--konfiguration) for baggrund om cache/kø-mekanismen bag disse kald, og hvordan man diagnosticerer det hvis en adresse "hænger" ([kapitel 13](13_Fejlfinding.md)).

### Faldgruber ved Modbus fra ST

- **Læsning er én værdi bagud.** `MB_READ_*`/`MBX_READ_*` returnerer straks den seneste cachede værdi og sætter en ny læsning i kø. Kaldes funktionen fx én gang i sekundet, er værdien op til ét sekund gammel. `MB_READ_OK()` er `FALSE` indtil første svar er modtaget.
- **Skrivning af samme værdi springes over.** `MB_WRITE_HOLDING`/`MB_WRITE_COIL` (enkelt-værdi) sender IKKE, hvis cachen allerede har bekræftet netop den værdi på den adresse (write-dedup). Skifter et program mellem to kilder, der tilfældigvis har samme værdi (fx to temperaturer på et display), bliver den anden skrivning derfor aldrig sendt. Brug `MB_WRITE_HOLDINGS` (FC16 — dedupliceres ikke), eller slå dedup fra med `MB_CACHE(FALSE)`.
- **`/` giver altid REAL** (appendiks D.4), også mellem to INT. Til at dele et heltal op i cifre (fx ASCII til et display) giver `(v / 10) MOD 10` derfor forkerte cifre — brug gentagen subtraktion (`WHILE v >= 10 DO v := v - 10; d := d + 1; END_WHILE;`) eller konvertér eksplicit med `REAL_TO_INT`.
- **INT er 16-bit med fortegn.** Et register over 32767 (fx fra en måler) læses som negativt; læg 65536 til i en `DINT` for at få værdien uden fortegn.

## 8.8 Demo: fjernovervågning og fjernstyring via REST API

Dette eksempel viser den fulde kæde fra kapitlets emne: et ST-program der poller en ekstern Modbus-enhed (Master-rolle), gemmer resultatet i en variabel eksporteret til Modbus-registrene (så både lokale SCADA-systemer *og* REST API kan se den), og hvordan man fra en ekstern klient både **overvåger** og **fjernstyrer** programmet via REST API.

**ST-programmet** (poller en ekstern flowmåler på slave-ID 5, register 10; skriver en alarmgrænse tilbage til den samme enhed):

```st
PROGRAM FlowMonitor
VAR
  flow_value: INT;
  high_limit: INT := 800;
  alarm_active: BOOL;
EXPORT flow_value, alarm_active;   (* synlig i Modbus Input Registers, se §6.3 *)
END_VAR

BEGIN
  flow_value := MB_READ_HOLDING(5, 10);

  IF MB_SUCCESS() AND flow_value > high_limit THEN
    alarm_active := TRUE;
    MB_WRITE_COIL(5, 0, TRUE);     (* udløs alarm-relæ på selve flowmåleren *)
  ELSE
    alarm_active := FALSE;
  END_IF;
END_PROGRAM
```

**Overvåg programmets variabler fra en ekstern klient** (poller `flow_value`/`alarm_active` uden at røre ST-koden):

```bash
curl -u admin:modbus123 http://192.168.1.100/api/logic/1
```

**Skift alarmgrænsen live, uden at genkompilere** — via en Modbus-binding (variabel bundet til et holding-register, se editorens **Bindings**-fane) skrevet direkte over REST:

```bash
curl -u admin:modbus123 -X POST http://192.168.1.100/api/registers/holding/50 \
     -H "Content-Type: application/json" -d '{"value": 750}'
```

**Følg det hele i realtid** — abonnér på SSE-strømmen i stedet for at polle (se [`../SSE_USER_GUIDE.md`](../SSE_USER_GUIDE.md)), eller åbn Modbus Aktivitetsloggen i dashboardet for at se selve `MB_READ_HOLDING`/`MB_WRITE_COIL`-transaktionerne mod flowmåleren live, med kilde `st_logic` ([§4.2](04_Web_Dashboard_og_Monitor.md)).

Dette mønster — ST Logic som lokal, altid-kørende beslutningslogik + REST API som fjern-overvågnings-/konfigurationslag — er den centrale arkitektur-idé i Hypervision PLC (se [§1.5](01_Systembeskrivelse.md#15-arkitektur-i-fugleperspektiv)).

## 8.9 Fejlhåndtering og grænser

- **Maks 64 variabel-slots pr. program** (FEAT-426, v7.9.68.34 — før 32). Hvert element i et `ARRAY` tæller som ét slot, så `txt : ARRAY[0..3] OF INT` bruger 4. Et enkelt ARRAY kan højst have 24 elementer. **STRING-variabler skal erklæres blandt de første 32** (de deler slot-nummer med en fast 32-pladsers strengtabel) — flyt dem øverst i VAR-blokken hvis compileren klager. Brug `GLOBAL_VAR` (§8.10) til værdier der deles mellem programmer.
- **Runtime-fejl** (fx division med nul) stopper *ikke* hele systemet — kun det pågældende program markeres fejlet, og dets variabler holdes bevidst tilbage fra at blive skrevet (så en enkelt fejlberegning ikke overskriver gode data med skrald). Se `Fejl`-tælleren i Runtime Monitor.
- **`Reinit`** nulstiller variabler, timere/tællere og statistik til udgangspunktet — brug det til en ren "kold genstart" af ét program uden at genstarte hele enheden.
- Se [kapitel 13](13_Fejlfinding.md#133-st-program-ser-ud-til-at-køre-men-intet-opdateres) hvis et program viser stigende `Udførelser` men ingen variabel-ændringer og nul fejl — det er typisk *ikke* et VM-problem, men en ekstern afhængighed (fx Modbus Master) der venter på noget der ikke sker.
- **`MB_SUCCESS()` betyder noget forskelligt efter et READ vs. et WRITE-kald** — en almindelig kilde til netop den fastlåsning ovenfor: efter `MB_READ_*` afspejler den om cachen reelt har en gyldig, ikke-udløbet værdi (ægte succes/fejl). Efter `MB_WRITE_*` afspejler den derimod kun om skrivningen blev **lagt i kø** — IKKE om den faktisk blev udført på Modbus-bussen — og den sættes én gang, i selve write-kaldet, uden nogensinde at blive opdateret igen. Er køen fuld i netop det øjeblik (kan ske ved bustravlhed, eller mens `mb scan` har køen på pause), forbliver `MB_SUCCESS()` falsk for evigt. **Byg derfor aldrig en tilstandsmaskine der venter ubegrænset på `MB_SUCCESS()` efter et write** — læg altid en tæller-baseret timeout ind (samme mønster som en scan-baseret delay, §8.6), så tilstanden garanteret kommer videre uanset hvad:
  ```st
  4: (* Vent på write, MED timeout — aldrig ubegraenset ventetid *)
    IF MB_SUCCESS() THEN
      writeWaitCounter := 0;
      step := 0;                    (* gaa videre normalt *)
    ELSE
      writeWaitCounter := writeWaitCounter + 1;
      IF writeWaitCounter >= 20 THEN  (* ~200ms ved 10ms scan-interval — giv op og forts�t alligevel *)
        writeWaitCounter := 0;
        step := 0;
      END_IF;
    END_IF;
  ```

### 8.9.1 Watchdog pr. program (FEAT-427)

Hvert af de 4 programmer kan overvåges af sin egen watchdog. Den har fire uafhængige betingelser — hver slås til med en grænse (0/tom = fra) — og **én** handling:

| Betingelse | Udløses når |
|---|---|
| **Fejl i træk** (`errors`) | programmet har haft *n* runtime-fejl i træk. Fanger også uendelige løkker, som VM'en afbryder (max-steps) og tæller som fejl |
| **Udførelsestid** (`exec`, µs) | en udførelse har taget længere end grænsen **3 gange i træk** |
| **Heartbeat** (`heartbeat`, ms) | programmet har ikke kaldt `WDT_FEED()` inden for tiden |
| **Stilstand** (`stall`, ms) | programmet er slet ikke blevet udført inden for tiden |

| Handling | Virkning |
|---|---|
| `alarm` (standard) | Alarm i alarmloggen — programmet kører videre |
| `stop` | Alarm + programmet stoppes |
| `restart` | Alarm + kold genstart af programmet (som Reinit). Hjælper det ikke — 3 restarts, uden 10 minutters fejlfri drift imellem — eskaleres til `safe` (udgange i sikker tilstand + stop). 10 min uden udløsning og uden runtime-fejl nulstiller tælleren |
| `safe` | Alarm + programmets udgangs-bindings (coils koblet til GPIO-udgange) sættes i deres **sikre tilstand** (Bindings → "Sikker tilstand"; ikke defineret = OFF), og programmet stoppes |
| `reboot` | Alarm + genstart af hele PLC'en. Tæller med i boot-loop-beskyttelsen — 3 i træk inden for 10 min → safe mode |

Handlingen udføres **én gang**; watchdog'en står derefter som **UDLØST**, til den kvitteres (Monitor-fanen "Kvittér", Indstillinger, `clear logic <id> wdt` eller REST). Kvittering nulstiller tællerne og starter et program igen, hvis det var watchdog'en der stoppede det. Overvågningen evalueres fra hovedløkken hvert 100 ms og er sat på pause i safe mode, mens programmet er deaktiveret/ikke kompileret, og mens debuggeren holder det.

**Heartbeat — kald kun `WDT_FEED()` når programmet er sundt.** Et ubetinget kald i toppen af programmet beviser kun at det kører (det dækker `stall` allerede). Pointen er at fodre når programmets egentlige arbejde lykkes:

```st
raw := MB_READ_HOLDING(15, 0);
IF MB_SUCCESS() THEN
  temp := raw;
  WDT_FEED();        (* kun naar slaven svarer *)
END_IF;
```

Med `set logic 1 wdt heartbeat 5000` og `set logic 1 wdt action safe` går udgangene i sikker tilstand, hvis slaven har været tavs i 5 s.

**Konfiguration:** ST-editoren → Indstillinger → "Watchdog pr. program", CLI `set logic <id> wdt errors|exec|heartbeat|stall <værdi>|off` og `set logic <id> wdt action <handling>` (se [Appendiks A](A_CLI_Kommando_Reference.md)), eller `POST /api/logic/{id}/wdt` ([Appendiks B](B_REST_API_Reference.md)). Status: Monitor-fanen viser "Watchdog: fra / OK / UDLØST" for det valgte program; `show logic <id> wdt` viser også tællere og tid siden sidste `WDT_FEED()`. Konfigurationen gemmes straks i NVS (egen nøgle) — den er endnu ikke med i backup/restore.

## 8.10 Delte variable mellem programmer (GLOBAL_VAR)

Normalt er Logic1-4 **fuldstændigt isolerede** — hvert program har sit eget variabelrum, og et program kan ikke se eller ændre et andet programs variabler. **`GLOBAL_VAR`** er en separat, delt deklarationsblok (uafhængig af de 4 programmer) hvis variabler alle 4 programmer kan læse og skrive direkte, uden om Modbus-bindinger:

```st
GLOBAL_VAR
  produktion_i_gang: BOOL;
  total_tæller: DINT;
END_VAR
```

Ethvert program der refererer til `produktion_i_gang` eller `total_tæller` som et almindeligt variabelnavn (uden lokal deklaration af samme navn) læser/skriver automatisk den delte værdi:

```st
(* Program 1: tæller op hver gang en cyklus fra sensoren registreres *)
PROGRAM Tæller
VAR
  puls: BOOL;
END_VAR
BEGIN
  IF puls THEN
    total_tæller := total_tæller + 1;
  END_IF;
END_PROGRAM
```

```st
(* Program 2: bruger den SAMME tæller til at afgøre alarmgrænse — uden nogen Modbus-binding mellem de to programmer *)
PROGRAM Overvågning
BEGIN
  IF total_tæller > 10000 THEN
    produktion_i_gang := FALSE;
  END_IF;
END_PROGRAM
```

**Håndtering via REST API:**
```bash
curl -u admin:modbus123 -X POST http://192.168.1.100/api/logic/globals/source \
     -H "Content-Type: application/json" -d '{"source": "GLOBAL_VAR\n  x: INT;\nEND_VAR\n"}'
curl -u admin:modbus123 http://192.168.1.100/api/logic/globals   # se aktuelle værdier
```
CLI: `show logic globals` (læs-kun status — redigering sker via REST/web-editoren, samme princip som almindelig program-kildekode).

**Begrænsninger og faldgruber:**
- Kun **scalar-typer** (`BOOL INT DINT DWORD REAL TIME`) — hverken `STRING`, `ARRAY` eller FB-instanser kan deles.
- Et globalt navn kan **skygges** af en lokal variabel med samme navn i et program — den lokale vinder altid, uden fejl eller advarsel. Undgå at genbruge navne mellem lokal og global scope.
- **Genupload af `GLOBAL_VAR`-blokken nulstiller alle globale værdier til 0/FALSE** og udløser automatisk genkompilering af ethvert program der rent faktisk bruger en global variabel (nødvendigt, fordi variabelnavne-til-indeks-bindingen kan skifte ved omstrukturering) — programmer der ikke bruger nogen global røres slet ikke. Kun selve **deklarationerne** (kildeteksten) overlever reboot, ligesom almindelig program-kildekode — de aktuelle **værdier** nulstilles altid ved genstart.
- Da flere programmer nu reelt kan dele hukommelse, er der (ligesom med Modbus-bindinger) intet der forhindrer et "race" hvor to programmer skriver til samme globale variabel i samme scan-cyklus — adgangen er tråd-sikker (ingen korruption), men *rækkefølgen* mellem programmerne inden for én cyklus er ikke noget man bør designe logik der er afhængig af.

## 8.11 Program-prioritet: NORMAL vs. HIGH

Hvert af Logic1-4 har nu sin **egen** eksekveringsinterval og prioritet — i stedet for ét fælles interval for alle fire.

- **NORMAL** (standard): kører kooperativt sammen med resten af systemet (Modbus RTU-scan, GPIO, dashboard) — uændret opførsel, bare med sit eget interval.
- **HIGH**: kører på en **dedikeret, uafhængig baggrundstask**, vækket af en hardware-timer med lav jitter — helt afkoblet fra hovedloopets kadence. Til brug hvis et program skal reagere hurtigere/mere præcist end resten af systemet tillader (fx et hurtigt sikkerhedsinterlock).

```bash
curl -u admin:modbus123 -X POST http://192.168.1.100/api/logic/1/priority \
     -H "Content-Type: application/json" -d '{"priority": "high"}'
curl -u admin:modbus123 -X POST http://192.168.1.100/api/logic/1/interval \
     -H "Content-Type: application/json" -d '{"interval_ms": 5}'
```
CLI: `set logic 1 priority high`, `set logic 1 interval 5`. Det gamle `set logic interval <ms>` (uden program-nummer) findes stadig — det sætter nu blot samme interval på alle NORMAL-programmer på én gang. **Web-GUI**: ST Editor → Settings-fanen → "Program-prioritet & interval"-tabellen (viser og redigerer alle 4 programmer samlet).

**Vigtig begrænsning: et HIGH-program kan ikke bindes til Modbus-registre eller GPIO.** Bindings-tabellen synkroniseres med hovedloopets egen kadence — et HIGH-program der kører på sin egen, uafhængige timer ville læse/skrive bundne variable på uforudsigelige tidspunkter i forhold til den synkronisering. Forsøg på at binde et HIGH-program (eller sætte et allerede bundet program til HIGH) afvises med en klar fejl. Et HIGH-program kan stadig:
- bruge almindelige lokale variable
- læse/skrive [GLOBAL_VAR](#810-delte-variable-mellem-programmer-global_var) (delt sikkert med de øvrige programmer)
- kalde Modbus Master-funktioner (`MB_READ_*`/`MB_WRITE_*`) — disse går allerede gennem en asynkron, trådsikker kø

**Skærpet sikkerhedsnet for HIGH:** et HIGH-program der låser sig fast (fx en uendelig løkke) stoppes automatisk efter færre instruktioner OG et kortere tidsbudget end et NORMAL-program (for at aldrig kunne blokere resten af systemet) — programmet markeres fejlet (`error_count` stiger, `last_error` viser årsagen), men enheden som helhed forbliver upåvirket og fuldt responsiv.

**Persistens:** priority/interval gemmes sammen med programmets egen kildekode (samme fil som `enabled`-status) — de overlever reboot ligesom resten af programmet.

## 8.12 Brugerdefinerede FUNCTION'er

Ud over de indbyggede funktioner (§8.5) kan man selv definere en **stateless FUNCTION** direkte i et programs kildekode, før `BEGIN`:

```st
PROGRAM Eksempel
VAR
  x: INT;
  resultat: INT;
END_VAR

FUNCTION DOUBLE : INT
VAR_INPUT
  val: INT;
END_VAR
BEGIN
  DOUBLE := val * 2;   (* tildel funktionsnavnet for at saette returvaerdien *)
END_FUNCTION

BEGIN
  resultat := DOUBLE(x);
END_PROGRAM
```

- Parametre angives **positionelt** (`DOUBLE(x)`, ikke `DOUBLE(val := x)`) — den navngivne `:=`/`=>`-syntaks fra §8.7's timer/tæller-eksempler findes kun for de indbyggede funktionsblokke (TON/TOF/TP/CTU/CTD/CTUD), ikke for egne FUNCTION'er.
- Returværdien sættes ved at tildele til **funktionens eget navn** som en almindelig variabel (`DOUBLE := ...`) — samme mønster som Pascal/Delphi.
- **`VAR_INPUT`-parametre kan omtildeles i funktionskroppen**, og en efterfølgende læsning af parameteren inden i samme kald ser den nye værdi korrekt (fx en lokal akkumulering før returnering).
- Kald kan **nestes og rekursion er tilladt** (op til 8 niveauer) — hver funktions egne lokale variable/parametre er isoleret i sit eget navnerum, uafhængigt af den kaldende funktions.
- **`VAR_OUTPUT`/`VAR_IN_OUT`-parametre accepteres af parseren, men skrives ikke tilbage til kalderens variabel efter returnering** — der findes i dag ingen kalde-syntaks for egne FUNCTION'er der kan modtage en outputbinding (kun de indbyggede FB'er understøtter `=>`). Brug **funktionens egen returværdi** til at levere ét resultat tilbage; skal du levere flere resultater, brug [`GLOBAL_VAR`](#810-delte-variable-mellem-programmer-global_var) i stedet for `VAR_OUTPUT`.
- `FUNCTION_BLOCK ... END_FUNCTION_BLOCK` (stateful variant, tilstand bevares mellem scan-cyklusser pr. kaldested) findes også i grammatikken, men dens fulde kalde-konventioner (navngivne parametre, `.felt`-adgang til output) er ikke efterprøvet i denne manual — brug indtil videre de indbyggede FB'er (TON/TOF/TP/CTU/CTD/CTUD, §8.5) til stateful logik, og en almindelig stateless `FUNCTION` til beregninger.

## 8.13 GPIO-indgange i ST Logic (bindings mode)

Et ST-program kan ikke pege direkte på en GPIO-pin — det læser i stedet en almindelig **Modbus-adresse**, ligesom `MB_READ_HOLDING` gør mod en ekstern enhed (§8.7). Kæden fra en fysisk (eller multiplexet, se [§2.2.1](02_Hardware_og_Moduler.md#221-hvorfor-virtuelle-gpioer--skifteregistrene-bag-de-88-kanaler)) digital indgang til en ST-variabel går derfor altid gennem **to uafhængige trin**:

1. **GPIO → Modbus discrete input** (`set gpio`/`/api/gpio/{pin}/config`, [§2.2.2](02_Hardware_og_Moduler.md#222-sådan-konfigureres-og-bruges-en-gpio-indgang)) — rent hardware-lag, ved intet om ST Logic.
2. **Modbus discrete input → ST-variabel** (`set logic <id> bind`/`POST /api/logic/{id}/bind`, dette afsnit) — rent software-lag, ved intet om GPIO'en fysisk sad på en direkte pin eller bag et skifteregister.

**Vigtigt om de multiplexede indgange (DI1-8 = virtuel GPIO 101-108 på ES32D26, se tabellen i [§2.2](02_Hardware_og_Moduler.md#22-io-oversigt-es32d26)):** fra ST Logic's synsvinkel er der **ingen forskel** på en almindelig fysisk GPIO-pin og en af de 8 skifteregister-multiplexede kanaler — begge ender som en helt almindelig discrete input-adresse efter trin 1. "DI1" er blot navnet du ser i web-GUI'en/på klemmen; i alle kommandoer nedenfor er det tallet (101) der bruges. Selve multiplexingen (SN74HC165-udlæsningen, §2.2.1) sker automatisk hver loop-iteration, længe før ST Logic-scanneren kører — programmet ser aldrig chippen, kun den færdig-udlæste bit-værdi.

**Eksempel: start/stop-knapper på to multiplexede indgange**

Fysisk opsætning: en startknap på **DI1** (virtuel GPIO 101) og en stopknap på **DI2** (virtuel GPIO 102).

**Trin 1 — map GPIO'erne til discrete input-adresser** (CLI, eller REST/§2.2.2):
```
set gpio 101 input 0      (* DI1 *)
set gpio 102 input 1      (* DI2 *)
```

**Trin 2 — skriv og upload ST-programmet**, med almindelige `BOOL`-variable (ingen særlig syntaks for at markere dem som "GPIO-forbundet" — det er bindingen i trin 3, ikke deklarationen, der afgør det):
```st
PROGRAM MotorStyring
VAR
  startKnap : BOOL;
  stopKnap : BOOL;
  motorKoerer : BOOL;
END_VAR

BEGIN
  IF startKnap AND NOT stopKnap THEN
    motorKoerer := TRUE;
  ELSIF stopKnap THEN
    motorKoerer := FALSE;
  END_IF;
END_PROGRAM
```

**Trin 3 — bind de to variable til deres discrete input-adresser** (samme adresser som i trin 1):
```
set logic 1 bind startKnap input:0
set logic 1 bind stopKnap input:1
set logic 1 enabled on
```

Herefter opdateres `startKnap`/`stopKnap` automatisk fra deres fysiske indgange **før** hver scan-cyklus (samme read-before/write-after-model som Modbus-registerbindinger generelt, se [§7](07_REST_API.md)) — programmet selv behøver aldrig kalde noget for at "hente" værdien, den er allerede korrekt når `BEGIN` når frem til `IF`.

**Faldgrube:** binding af `input:<addr>` bruger — ligesom `set gpio ... input <idx>` — **discrete input-adresserummet**, ikke holding-register- eller coil-adresserummet. Adresse 0 i `input:0` er derfor en helt anden fysisk ting end adresse 0 i `reg:0` eller `coil:0`, selvom tallet er det samme — bekræft altid med `show gpio` hvilken discrete input-adresse en given GPIO faktisk er mappet til, før du binder den.

For output-retningen (ST-variabel → fysisk relæ/DO, fx de tilsvarende 8 multiplexede udgangskanaler **DO1-8 = virtuel GPIO 201-208** via SN74HC595) gælder samme to-trins-princip, blot med `coil:<addr>` i stedet for `input:<addr>` i bindingen: `set gpio 201 coil 0` (DO1, trin 1) + `set logic 1 bind motorKoerer coil:0` (trin 3, samme program).

## 8.14 Testcase: to programmer deler ét display (Logic1 + Logic4)

Denne testcase samler flere funktioner i ét gennemprøvet eksempel: **Modbus Master** (sensorer og display), en **expansion board**-kanal, **GPIO-bindinger**, **tællermodulet** og **GLOBAL_VAR** som signal mellem to programmer. Opsætningen er kørt og verificeret på en ES32D26 (v7.9.68.47).

**Formål**

- **Logic1** viser i rotation temperaturen fra to kanaler og en værdi fra et expansion board på et 4-cifret display. DI1–DI3 låser visningen på én kanal og får den tilhørende udgang til at blinke, DI4 er en global udgangsspærre.
- **Logic4** viser antallet af aktiveringer på DI8 i 3 sekunder, hver gang der kommer en ny — og beder imens Logic1 om at holde pause med displayet.

### 8.14.1 Opbygning

```
 DI8 (GPIO108) ──► discrete input 7 ──► Counter 1 (sw, faldende flanke)
                                              │ CNT_VALUE(1)
                                              ▼
 GLOBAL_VAR ◄── disp_busy := TRUE ◄──── Logic4: ny værdi? ──► display (vis tæller 3 s)
     │
     └──► Logic1: IF ... AND NOT disp_busy THEN ──► display (rotation: 1, CH0, 2, CH1, 3, expansion)

 Modbus-slave 90 (temperaturmodul, reg 0-1) ──► Logic1
 Expansion board 1, kanal 1, slave 9, reg 2 ──► Logic1
 DI1-DI4 (GPIO101-104) ──► discrete input 0-3 ──► Logic1 in1..in4
 Logic1 out0..out2 ──► coil 201-203 ──► DO1-DO3 (GPIO201-203)
```

Kun ét program skriver til displayet ad gangen: Logic4 sætter `disp_busy`, og Logic1 springer hele sin display-blok over, så længe flaget er sat.

**Displayet (DM56A04, Modbus-slave 1)**

| Register | Indhold |
|---|---|
| 0–3 | Fire tegn som ASCII (bruges til labels "   1" og "----") |
| 6 | Format: høj byte, nederste nibble = antal decimaler, øverste nibble = fortegn (1 = negativ). `256` = 1 decimal, `4096` = negativ |
| 7 | Tallet (heltal; decimalerne angives i reg 6) |
| 8 | Blinkmaske (15 = alle fire cifre blinker, 0 = intet blink) |

**I/O**

| Signal | Virtuel GPIO | Modbus-adresse | Bruges af |
|---|---|---|---|
| DI1–DI4 | 101–104 | discrete input 0–3 | Logic1 `in1`–`in4` (aktiv-lave: 0 = aktiveret) |
| DO1–DO3 | 201–203 | coil 201–203 | Logic1 `out0`–`out2` |
| DI8 | 108 | discrete input 7 | Counter 1 (tæller aktiveringer) |

### 8.14.2 Opsætning trin for trin

Rækkefølgen betyder noget: **GLOBAL_VAR skal findes, før et program der bruger `disp_busy`, kan kompilere.**

**Trin 1 — GLOBAL_VAR** (ST-editoren → GLOBAL_VAR, eller `POST /api/logic/globals/source`):

```st
GLOBAL_VAR
  disp_busy : BOOL;   (* TRUE = Logic4 bruger displayet - Logic1 holder pause *)
  di8_count : INT;    (* DI8-taelleren (0-9999), kan laeses af alle programmer *)
END_VAR
```

**Trin 2 — DI8 og tællermodulet** (CLI):

```
set gpio 108 input 7
set counter 1 mode 1 hw-mode:sw input-dis:7 edge:falling bit-width:32 start-value:0 enable:on
set counter 1 control auto-start:on running:on
save
show counter 1          (Status: ENABLED, Running: YES, Auto-Start: YES)
```

`edge:falling`, fordi DI'erne er aktiv-lave (1 i hvile) — der tælles, når indgangen *aktiveres*. **`enable:on` alene starter ikke tællingen**: tælleren skal også køre (`running:on`), og `auto-start:on` får den til at starte igen efter hver genstart (se [§9.1](09_Taellere_og_Timere.md)). På ES32D26 kan kun `sw` bruges (DI1–8 sidder bag et skifteregister).

**Trin 3 — DI1–DI4 og DO1–DO3** (CLI):

```
set gpio 101 input 0
set gpio 102 input 1
set gpio 103 input 2
set gpio 104 input 3
set gpio 201 coil 201
set gpio 202 coil 202
set gpio 203 coil 203
```

**Trin 4 — Logic1** (upload + kompilér i Logic1, derefter bindings):

```st
PROGRAM temp_display
VAR
  (* --- I/O (bindes, samme variable mapping som fÃƒÂ¸r) --- *)
  in1      : BOOL;       (* DI1: lÃƒÂ¥s visning pÃƒÂ¥ kanal 1 + DO1 blinker *)
  in2      : BOOL;       (* DI2: lÃƒÂ¥s visning pÃƒÂ¥ kanal 2 + DO2 blinker *)
  in3      : BOOL;       (* DI3: lÃƒÂ¥s visning pÃƒÂ¥ kanal 3 + DO3 blinker *)
  in4      : BOOL;       (* DI4: global udgangsspÃƒÂ¦rre *)
  out0     : BOOL;       (* DO1 *)
  out1     : BOOL;       (* DO2 *)
  out2     : BOOL;       (* DO3 *) 

  (* --- takt og sekvens --- *) 
  t_q      : BOOL;
  blink    : BOOL;
  step     : INT;        (* 0="1", 1=CH0, 2="2", 3=CH1, 4="3", 5=expansion *)
  sel      : INT;
  dstep    : INT;

  (* --- data/visning --- *)
  t0       : INT;        (* CH0 i 0,1 Ã‚Â°C *)
  t1       : INT;        (* CH1 i 0,1 Ã‚Â°C *)
  x        : INT;        (* expansion, tiendedele (register uden fortegn) *)
  ok0      : BOOL;
  ok1      : BOOL;
  okx      : BOOL;
  lbl      : INT;        (* > 0 = vis label "lbl", 0 = vis vÃƒÂ¦rdi *)
  val      : INT;        (* vÃƒÂ¦rdi i tiendedele, fx 253 = 25.3 *)
  vok      : BOOL;
  nosensor : BOOL;
  neg      : BOOL;       (* vÃƒÂ¦rdien er negativ *)
  w        : DINT;       (* |vÃƒÂ¦rdi|, evt. ÃƒÂ·10 *)
  q        : DINT;       (* kvotient ved ÃƒÂ·10 *)
  dec      : INT;        (* antal decimaler: 1 eller 0 *)
  txt      : ARRAY[0..3] OF INT;   (* ASCII til reg 0-3 (labels og "----") *)
  num      : ARRAY[0..1] OF INT;   (* reg 6 = format, reg 7 = tal *)
END_VAR

BEGIN
  TON(IN := NOT t_q, PT := T#250ms, Q => t_q);

  IF t_q THEN
    blink := NOT blink;

    IF NOT in1 THEN sel := 1;
    ELSIF NOT in2 THEN sel := 2;
    ELSIF NOT in3 THEN sel := 3;
    ELSE sel := 0;
    END_IF;

    IF sel > 0 THEN
      CASE sel OF
        1: dstep := 1;
        2: dstep := 3;
        3: dstep := 5;
      END_CASE;
    ELSE
      dstep := step;
    END_IF;
  END_IF;

  out0 := blink AND in1 AND in4;
  out1 := blink AND in2 AND in4;
  out2 := blink AND in3 AND in4;

  (* disp_busy (GLOBAL_VAR): Logic4 viser DI8-taelleren - lad displayet vaere *)
  IF t_q AND blink AND NOT disp_busy THEN
    t0 := MB_READ_HOLDING(90, 0);         ok0 := MB_READ_OK();
    t1 := MB_READ_HOLDING(90, 1);         ok1 := MB_READ_OK();
    x  := MBX_READ_HOLDING(1, 1, 9, 2);   okx := MBX_SUCCESS();

    lbl := 0;
    nosensor := FALSE;
    CASE dstep OF
      0: lbl := 1;
      1: val := t0; vok := ok0; nosensor := t0 < -32767;
      2: lbl := 2;
      3: val := t1; vok := ok1; nosensor := t1 < -32767;
      4: lbl := 3;
      5: val := x;  vok := okx;
    END_CASE;

    (* StÃƒÂ¸rrelse og fortegn. Expansion-registeret er uden fortegn (0..65535). *)
    neg := FALSE;
    w := val;
    IF dstep = 5 THEN
      IF w < 0 THEN w := w + 65536; END_IF;
    ELSIF w < 0 THEN
      neg := TRUE;
      w := 0 - w;
    END_IF;

    (* Helst 1 decimal (max 999.9 / -99.9 pÃƒÂ¥ 4 cifre).
       Passer det ikke: vis som heltal (ÃƒÂ·10), som i den tidligere version. *)
    dec := 1;
    IF ((NOT neg) AND (w > 9999)) OR (neg AND (w > 999)) THEN
      q := 0;
      WHILE w >= 10000 DO w := w - 10000; q := q + 1000; END_WHILE;
      WHILE w >= 1000  DO w := w - 1000;  q := q + 100;  END_WHILE;
      WHILE w >= 100   DO w := w - 100;   q := q + 10;   END_WHILE;
      WHILE w >= 10    DO w := w - 10;    q := q + 1;    END_WHILE;
      w := q;
      dec := 0;
      IF ((NOT neg) AND (w > 9999)) OR (neg AND (w > 999)) THEN
        nosensor := TRUE;   (* passer heller ikke som heltal *)
      END_IF;
    END_IF;

    IF lbl > 0 THEN
      txt[0] := 32; txt[1] := 32; txt[2] := 32; txt[3] := 48 + lbl;   (* "   1" / "   2" / "   3" *)
      MB_WRITE_HOLDINGS(1, 0, 4) := txt;

    ELSIF (NOT vok) OR nosensor THEN
      txt[0] := 45; txt[1] := 45; txt[2] := 45; txt[3] := 45;          (* "----" *)
      MB_WRITE_HOLDINGS(1, 0, 4) := txt;

    ELSE
      (* Reg 6 hÃƒÂ¸j byte: ÃƒÂ¸verste nibble = fortegn (1 = negativ),
         nederste nibble = antal decimaler. 256 = 0x0100, 4096 = 0x1000. *)
      num[0] := dec * 256;
      IF neg THEN num[0] := num[0] + 4096; END_IF;
      num[1] := w;
      MB_WRITE_HOLDINGS(1, 6, 2) := num;
    END_IF;

    IF sel > 0 THEN
      MB_WRITE_HOLDING(1, 8) := 15;
    ELSE
      MB_WRITE_HOLDING(1, 8) := 0;
    END_IF;

    IF sel = 0 THEN
      step := step + 1;
      IF step > 5 THEN step := 0; END_IF;
    ELSE
      step := dstep - 1;
    END_IF;
  END_IF;
END_PROGRAM
```

```
set logic 1 bind in1 input-dis:0
set logic 1 bind in2 input-dis:1
set logic 1 bind in3 input-dis:2
set logic 1 bind in4 input-dis:3
set logic 1 bind out0 coil:201
set logic 1 bind out1 coil:202
set logic 1 bind out2 coil:203
```

Sådan virker Logic1:

| Del | Forklaring |
|---|---|
| `TON(IN := NOT t_q, PT := T#250ms, Q => t_q)` | Selvnulstillende takt: `t_q` er TRUE i én cyklus hvert 250 ms |
| `blink := NOT blink` | Skifter hvert 250 ms → udgangene blinker med 2 Hz, og displayet opdateres hver anden takt (500 ms) |
| `IF NOT in1 THEN sel := 1 …` | DI1–DI3 (aktiv-lave) låser visningen på kanal 1/2/3 (`dstep` 1, 3, 5) |
| `out0 := blink AND in1 AND in4` | Udgangene blinker; DI4 er global spærre |
| `IF t_q AND blink AND NOT disp_busy THEN` | **Koordineringen:** hele display-blokken (læsning, formatering, skrivning, rotation) springes over, mens Logic4 viser tælleren |
| `MB_READ_HOLDING(90, 0/1)`, `MBX_READ_HOLDING(1, 1, 9, 2)` | Temperatur CH0/CH1 (0,1 °C) og expansion-værdien (uden fortegn) — `MB_READ_OK()`/`MBX_SUCCESS()` afgør om værdien er gyldig |
| Formatering | Helst 1 decimal (maks 999,9 / −99,9); passer det ikke, vises heltal. `----` ved manglende sensor eller ugyldig værdi |
| `MB_WRITE_HOLDINGS(1, 0, 4) := txt` / `(1, 6, 2) := num` | Label eller "----" som tekst — eller tal + format |
| `MB_WRITE_HOLDING(1, 8) := 15/0` | Displayet blinker, når visningen er låst med DI1–DI3 |
| `step := step + 1` | Rotation 0→5: "1", CH0, "2", CH1, "3", expansion |

**Trin 5 — Logic4** (upload + kompilér + aktivér i Logic4 — ingen bindings):

```st
PROGRAM di8_counter
(* DI8 (GPIO108) taelles af PLC'ens taellermodul: Counter 1, sw-tilstand,
   discrete input 7, faldende flanke (DI er aktiv-lav: 1 = hvile, saa der
   taelles naar indgangen AKTIVERES). Taellingen sker uafhaengigt af ST.

   Programmet viser vaerdien paa DM56A04-displayet (Modbus-slave 1) i 3 s,
   hver gang den aendrer sig. Imens er GLOBAL_VAR disp_busy = TRUE, og
   Logic1 lader displayet vaere. Derefter fortsaetter Logic1's visning. *)

(* ===================== SW-TAELLER: OPSAETNING =====================
   Taellingen laves IKKE i dette program, men af taellermodulet (CLI):

   1) DI8 -> discrete input 7:
        set gpio 108 input 7
   2) Counter 1 i sw-tilstand (polling) paa discrete input 7:
        set counter 1 mode 1 hw-mode:sw input-dis:7 edge:falling bit-width:32 start-value:0 enable:on
      - edge:falling: DI er aktiv-lav (1 = hvile), saa der taelles naar
        indgangen AKTIVERES. Taeller den ved slip: brug edge:rising.
      - Debounce er som standard 10 ms.
   3) Start taelleren - nu og efter hver genstart - og gem:
        set counter 1 control auto-start:on running:on
        save
      NB: "enable:on" alene starter IKKE taellingen - den skal ogsaa koere.
   4) Kontrol:  show counter 1   (Running/Auto-Start, Scaled Value)
      Nulstil:  reset counter 1

   - ES32D26: kun sw (poll) er mulig - DI1-8 sidder bag et skifteregister,
     saa hw (PCNT) og sw-isr ikke kan bruges. Max pulsfrekvens begraenses
     af hvor ofte DI'erne laeses (fint til knapper/langsomme pulser).
   - Vaerdien ligger ogsaa i HR100-101 (32 bit) og laeses her med CNT_VALUE(1).
   - Taelleren starter fra 0 efter genstart (persist-gruppe paa HR100-101,
     hvis den skal huskes).
   ================================================================ *)
VAR
  cv       : DINT;                 (* Counter 1's vaerdi *)
  last_cv  : DINT;
  started  : BOOL;
  changed  : BOOL;                 (* vaerdien er aendret i denne cyklus *)
  hold_q   : BOOL;                 (* 3 s er gaaet siden seneste aendring *)
  rq       : BOOL;                 (* 500 ms genskrivnings-takt *)
  num      : ARRAY[0..1] OF INT;   (* reg 6 = format, reg 7 = tal *)
END_VAR

BEGIN
  cv := CNT_VALUE(1);

  changed := FALSE;
  IF NOT started THEN
    last_cv := cv;                 (* ingen visning ved opstart *)
    started := TRUE;
  ELSIF cv <> last_cv THEN
    last_cv := cv;
    changed := TRUE;
    disp_busy := TRUE;
  END_IF;

  di8_count := cv MOD 10000;       (* displayet har 4 cifre: vis de sidste 4 *)

  (* 3 s fra SENESTE aendring: TON nulstilles i hver aendringscyklus *)
  TON(IN := disp_busy AND NOT changed, PT := T#3s, Q => hold_q);
  IF hold_q THEN
    disp_busy := FALSE;
  END_IF;

  (* Skriv ved aendring og hvert 500 ms mens vi viser - saa en skrivning
     fra Logic1 der allerede stod i koeen, ikke bliver staaende *)
  TON(IN := disp_busy AND NOT rq, PT := T#500ms, Q => rq);
  IF disp_busy AND (changed OR rq) THEN
    num[0] := 0;                     (* 0 decimaler, positivt tal *)
    num[1] := di8_count;
    MB_WRITE_HOLDINGS(1, 6, 2) := num;
    MB_WRITE_HOLDING(1, 8) := 0;     (* ingen blink *)
  END_IF;
END_PROGRAM
```

Sådan virker Logic4:

| Del | Forklaring |
|---|---|
| `cv := CNT_VALUE(1)` | Læser tællermodulet — selve tællingen sker uden for ST |
| `IF NOT started …` | Første cyklus efter start/genstart gemmer blot værdien — ingen visning ved opstart |
| `ELSIF cv <> last_cv` | Ny værdi → `changed` i én cyklus og `disp_busy := TRUE` |
| `di8_count := cv MOD 10000` | De sidste 4 cifre (displayet har 4) — også synlig for andre programmer |
| `TON(IN := disp_busy AND NOT changed, PT := T#3s, …)` | 3 s fra **seneste** ændring: TON nulstilles i hver ændringscyklus, så hurtige pulser holder visningen fremme |
| `TON(IN := disp_busy AND NOT rq, PT := T#500ms, …)` | Genskriver displayet hvert 500 ms, mens den viser — så en skrivning fra Logic1, der allerede lå i Modbus-køen, ikke bliver stående |
| `MB_WRITE_HOLDINGS(1, 6, 2) := num` + `MB_WRITE_HOLDING(1, 8) := 0` | Tallet uden decimaler og uden blink |

### 8.14.3 Testprocedure

**Manuel test (på anlægget)**

| # | Handling | Forventet resultat |
|---|---|---|
| 1 | Lad anlægget køre uden at røre noget | Displayet roterer: `   1` → CH0 → `   2` → CH1 → `   3` → expansion, et skift pr. 0,5 s. DO1–DO3 slukket |
| 2 | Aktivér DI8 én gang | Displayet viser `1` (tælleren) med det samme. Rotationen står stille |
| 3 | Vent 3 s | Rotationen fortsætter, hvor den slap |
| 4 | Aktivér DI8 flere gange med under 3 s mellemrum | Tallet tæller op; visningen bliver stående, til 3 s efter **sidste** aktivering |
| 5 | Hold DI1 aktiveret | Displayet låses på CH0 og blinker; DO1 blinker. Slip → rotationen fortsætter |
| 6 | Aktivér DI8, mens DI1 holdes | Tælleren vises 3 s (uden blink), derefter tilbage til den låste CH0-visning |
| 7 | Hold DI4 aktiveret sammen med DI1 | DO1 blinker ikke (udgangsspærre); visningen er stadig låst |
| 8 | Fjern temperatursensoren på CH0 | CH0 vises som `----` |
| 9 | Genstart PLC'en | Tælleren starter fra 0 og tæller igen uden indgriben (`show counter 1`: Running YES). Logic1 og Logic4 kører med 0 fejl |

**Fjern-test via REST** (uden at røre anlægget — sådan blev opsætningen verificeret). Tællerens værdi sættes med startværdi + reset i stedet for pulser på DI8:

```bash
PLC=http://192.168.1.100
curl -s -X POST -c c.txt -u admin:<adgangskode> $PLC/api/login           # session-cookie

# 1. Udgangspunkt: disp_busy=false, Logic1's "step" skifter
curl -s -b c.txt $PLC/api/logic/globals
curl -s -b c.txt $PLC/api/logic/1          # se variablen "step" (kør et par gange)

# 2. Simulér 41 tællinger
curl -s -b c.txt -X POST -H "Content-Type: application/json" $PLC/api/cli \
     -d '{"command":"set counter 1 mode 1 hw-mode:sw input-dis:7 edge:falling bit-width:32 start-value:41 enable:on"}'
curl -s -b c.txt -X POST -H "Content-Type: application/json" $PLC/api/cli -d '{"command":"reset counter 1"}'

# 3. Inden for 3 s: disp_busy=true, di8_count=41, Logic1's "step" står stille,
#    og displayets reg 7 er 41 (Modbus-cachen viser den senest skrevne værdi)
curl -s -b c.txt $PLC/api/logic/globals
curl -s -b c.txt -X POST -H "Content-Type: application/json" $PLC/api/modbus/master/rw \
     -d '{"op":"read","type":"holding","slave":1,"addr":7}'

# 4. Efter 3 s: disp_busy=false, "step" skifter igen
# 5. Ryd op: start-value:0 + reset counter 1
```

Resultatet ved verifikationen: alle punkter som forventet, `12345` vist som `2345`, og 0 fejl i begge programmer. DI8 er desuden testet fysisk: der tælles på flanken high → low (`edge:falling`), som vist på displayet.

### 8.14.4 Fejlfinding

| Symptom | Årsag | Løsning |
|---|---|---|
| Intet på displayet, når DI8 aktiveres; `show counter 1` viser værdi 0 | Tælleren er aktiveret, men kører ikke | `set counter 1 control auto-start:on running:on` + `save` |
| Tæller, når DI8 *slippes* | Forkert flanke for indgangen | `edge:rising` i stedet for `edge:falling` |
| Tæller ikke efter genstart (før v7.9.68.47) | Auto-start blev ikke gemt (BUG-445) | Opdatér firmwaren; sæt auto-start igen og `save` |
| Logic1 kompilerer ikke: ukendt variabel `disp_busy` | GLOBAL_VAR mangler | Upload GLOBAL_VAR først (trin 1), kompilér Logic1 igen |
| Displayet flimrer mellem tæller og rotation | Logic1 mangler `AND NOT disp_busy`, eller et andet program skriver også til slave 1 | Ret Logic1; kun programmer der respekterer `disp_busy`, må skrive til displayet |
| Tallet bliver stående efter 3 s | Logic1 kører ikke (deaktiveret/fejl) | Tjek Logic1 i ST-editoren (Runtime Monitor) |
| Genupload af GLOBAL_VAR | Nulstiller `disp_busy`/`di8_count` og genkompilerer programmer der bruger dem | Forventet — tælleren selv (Counter 1) påvirkes ikke |
| Tælleren starter fra 0 efter genstart | Tællerværdier gemmes ikke som standard | Persist-gruppe på HR100–101, hvis værdien skal huskes |


---

[← 7. REST API](07_REST_API.md) · [Indeks](00_INDEKS.md) · Næste: [9. Tællere & Timere →](09_Taellere_og_Timere.md)
