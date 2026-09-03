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

Literaler over `INT`s grænse (>32767) auto-forfremmes til `DINT`. Arrays understøttes: `regs: ARRAY[0..15] OF INT;`.

## 8.4 Kontrolstrukturer

```st
IF <betingelse> THEN ... ELSIF <betingelse> THEN ... ELSE ... END_IF;
CASE <udtryk> OF
  1: ...
  2, 3: ...
ELSE ...
END_CASE;
FOR i := 0 TO 9 DO ... END_FOR;
WHILE <betingelse> DO ... END_WHILE;
REPEAT ... UNTIL <betingelse> END_REPEAT;
```

Der er en **sikkerhedsgrænse på 10.000 VM-instruktioner pr. scan-cyklus** — et program der låser sig fast i en uendelig løkke stoppes af systemet i stedet for at blokere resten af PLC'en permanent.

## 8.5 Indbyggede funktioner (overblik)

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
| **Modbus Master** | `MB_READ_COIL MB_READ_INPUT MB_READ_HOLDING MB_READ_INPUT_REG MB_READ_HOLDINGS MB_WRITE_COIL MB_WRITE_HOLDING MB_WRITE_HOLDINGS MB_SUCCESS MB_ERROR MB_BUSY MB_CACHE` — se §8.7 |
| **Persistens** | `SAVE LOAD` — gem/genindlæs registergrupper til/fra NVS på tværs af reboot |

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

**Multi-register-varianter** (`MB_READ_HOLDINGS`/`MB_WRITE_HOLDINGS`) læser/skriver op til 16 registre i én forespørgsel, ind i/fra et ST-array:

```st
VAR
  regs: ARRAY[0..7] OF INT;
END_VAR
BEGIN
  MB_READ_HOLDINGS(slave_id, 100, 8) := regs;   (* læs 8 registre fra adresse 100 ind i regs[] *)
END_PROGRAM
```

Se [kapitel 6](06_Modbus_Interface.md#65-modbus-master--konfiguration) for baggrund om cache/kø-mekanismen bag disse kald, og hvordan man diagnosticerer det hvis en adresse "hænger" ([kapitel 13](13_Fejlfinding.md)).

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

- **Runtime-fejl** (fx division med nul) stopper *ikke* hele systemet — kun det pågældende program markeres fejlet, og dets variabler holdes bevidst tilbage fra at blive skrevet (så en enkelt fejlberegning ikke overskriver gode data med skrald). Se `Fejl`-tælleren i Runtime Monitor.
- **`Reinit`** nulstiller variabler, timere/tællere og statistik til udgangspunktet — brug det til en ren "kold genstart" af ét program uden at genstarte hele enheden.
- Se [kapitel 13](13_Fejlfinding.md#st-program-ser-ud-til-at-koere-men-intet-opdateres) hvis et program viser stigende `Udførelser` men ingen variabel-ændringer og nul fejl — det er typisk *ikke* et VM-problem, men en ekstern afhængighed (fx Modbus Master) der venter på noget der ikke sker.

---

[← 7. REST API](07_REST_API.md) · [Indeks](00_INDEKS.md) · Næste: [9. Tællere & Timere →](09_Taellere_og_Timere.md)
