# Tæller-skabeloner (Counter Configuration Templates)

**Gælder:** firmware v7.9.68.50+ · **Opdateret:** 2026-10-04 · Hovedreference: [manualens kapitel 9](manual/09_Taellere_og_Timere.md) (inkl. diagrammet i §9.1.1) og [appendiks A](manual/A_CLI_Kommando_Reference.md).

Kopiér og tilpas. Alle kommandoer følger den aktuelle syntaks og er kontrolleret mod CLI-parseren.

---

## Grundregler

1. **Én `set counter <id> mode 1 …`-linje = hele tællerens opsætning.** Linjen bygges op fra standardværdierne; nøgler der ikke står på linjen, får standardværdien (undtagen auto-start, som bevares). Del derfor *aldrig* opsætningen over flere `mode`-linjer — den sidste overskriver de forrige.
2. **Aktiveret ≠ kørende.** Tælleren tæller først, når den er startet: `set counter <id> control running:on`. Med `auto-start:on` starter den også efter hver genstart.
3. **Gem:** `save` efter ændringer.
4. **Startværdi er i pulser.** Prescaler og skala ændrer kun udgangsregistrene, ikke tællingen (se §9.1.1).
5. `show config counter` viser opsætningen i et format, der kan sættes direkte ind igen.

**Nøgler** (`set counter <id> mode 1 key:value …`): `hw-mode` (`sw`/`sw-isr`/`hw`), `input-dis`, `interrupt-pin`, `hw-gpio`, `edge` (`rising`/`falling`/`both`), `direction` (`up`/`down`), `bit-width` (8/16/32/64), `start-value`, `prescaler`, `scale`, `debounce` (`on`/`off`), `debounce-ms`, `compare-enabled` (`on`/`off`), `compare-value`, `compare-mode` (0 = ≥, 1 = >, 2 = =), `compare-source` (0 = tælleværdi, 1 = prescaled, 2 = scaled), `reset-on-read` (compare-bit), `enable`/`disable`. De ældre navne `resolution`, `start`, `debounce-time`, `compare` og ordet `parameter` accepteres også.

> **ES32D26:** kun `hw-mode:sw` kan bruges (DI1–8 sidder bag et skifteregister, og der er ingen ledige GPIO-pins). `sw-isr` og `hw` afvises med en forklaring (FEAT-430). Skabelon 2 gælder kun andre boards.

---

## Skabelon 1 — Pulstælling på en digital indgang (sw, alle boards)

DI8 (GPIO108) er discrete input 7. Indgangene på ES32D26 er aktiv-lave (1 = hvile), så der tælles på faldende flanke = når indgangen aktiveres.

```bash
set gpio 108 input 7
set counter 1 mode 1 hw-mode:sw input-dis:7 edge:falling bit-width:32 start-value:0 debounce:on debounce-ms:10
set counter 1 control auto-start:on running:on
save
show counter 1
```

- `debounce:on debounce-ms:10` til mekaniske kontakter. Til rene elektroniske pulser kan `debounce:off` give lidt højere hastighed.
- `sw` læser indgangen én gang pr. hovedløkke: realistisk ca. 50 Hz med debounce 10 ms, ca. 100–150 Hz uden. Pulser kan tabes, mens PLC'en gemmer til flash eller kompilerer.

---

## Skabelon 2 — Hardware-tæller (hw/PCNT) og interrupt (sw-isr) — kun andre boards end ES32D26

```bash
# PCNT på en direkte GPIO (højeste hastighed)
set counter 1 mode 1 hw-mode:hw hw-gpio:25 edge:rising bit-width:32
set counter 1 control auto-start:on running:on
save

# GPIO-interrupt
set counter 2 mode 1 hw-mode:sw-isr interrupt-pin:26 edge:rising bit-width:32
set counter 2 control auto-start:on running:on
save
```

Undgå strapping-pins (0, 2, 5, 12, 15) og pins der bruges af andet udstyr på boardet.

---

## Skabelon 3 — Prescaler (dele tællingen ned i HR104)

Prescaleren tæller **alle** pulser; den dividerer kun værdien, der skrives i HR104-105 (`CNT_RAW`). Den udvider ikke tællerens område og påvirker ikke HR100 eller frekvensen.

```bash
# Flowmåler: 100 pulser pr. liter → HR104 = hele liter, HR100 = pulser
set counter 1 mode 1 hw-mode:sw input-dis:7 edge:falling bit-width:32 prescaler:100
set counter 1 control auto-start:on running:on
save
```

| Pulser | HR100 (`CNT_VALUE`) | HR104 (`CNT_RAW`) |
|---|---|---|
| 99 | 99 | 0 |
| 100 | 100 | 1 |
| 2537 | 2537 | 25 |

Skal HR104 starte på 50 liter, sættes `start-value:5000` (i pulser).

---

## Skabelon 4 — Skala (enhedsomregning i HR100)

```bash
# Hver puls = 0,5 enhed → HR100 = pulser × 0,5 (afrundet)
set counter 1 mode 1 hw-mode:sw input-dis:7 edge:falling bit-width:32 scale:0.5
set counter 1 control auto-start:on running:on
save
```

100 pulser → HR100-101 = 50, HR104-105 = 100 (prescaler 1). HR101 er det **høje ord** af HR100's 32-bit værdi — ikke et separat register.

---

## Skabelon 5 — Compare (statusbit ved en tærskel)

```bash
# Sæt bit 4 i kontrolregistret (HR110), når tælleværdien ≥ 1000
set counter 1 mode 1 hw-mode:sw input-dis:7 edge:falling bit-width:32 compare-enabled:on compare-value:1000 compare-mode:0 compare-source:0 reset-on-read:on
set counter 1 control auto-start:on running:on
save
```

- Statusbit: **HR110 bit 4** (Counter 2: HR130, 3: HR150, 4: HR170). Compare-værdien ligger i HR111-114.
- `reset-on-read:on`: bit 4 slettes, når en Modbus-master læser kontrolregistret.
- `compare-source`: 0 = tælleværdien, 1 = prescaled (HR104), 2 = scaled (HR100).

Se også [COUNTER_COMPARE_QUICK_START.md](COUNTER_COMPARE_QUICK_START.md).

---

## Skabelon 6 — Nedtælling fra en mængde

```bash
# Tæller 500, 499, … 0 — ved næste puls tilbage til 500, og overflow-bit (HR110 bit 3) sættes
set counter 1 mode 1 hw-mode:sw input-dis:7 edge:falling bit-width:32 direction:down start-value:500
set counter 1 control auto-start:on running:on
save
reset counter 1
```

`start-value` træder i kraft ved `reset counter`, ved (re)konfiguration og ved opstart.

---

## Skabelon 7 — Læs og nulstil fra en Modbus-master

Tælleren nulstilles til `start-value`, hver gang en Modbus-master læser værdi- eller prescaled-registrene (HR100-103 / HR104-107) med FC03 — fx til "antal siden sidste aflæsning":

```bash
set counter 1 mode 1 hw-mode:sw input-dis:7 edge:falling bit-width:32
set counter 1 control auto-start:on running:on counter-reg-reset-on-read:on
save
```

- Flaget gemmes i tællerens konfiguration (v7.9.68.57+, BUG-447) og bevares, når `mode 1`-linjen køres igen. `show config counter` eksporterer det.
- Læsning af kun kontrolregistret (HR110) nulstiller ikke. Læsning via REST eller dashboardet nulstiller heller ikke.
- Pulser, der kommer mellem masterens læsning og nulstillingen (samme hovedløkke), tælles ikke med i næste aflæsning.
- Alternativ uden flaget: masteren læser HR100-101 og skriver derefter **1** (bit 0 = reset-kommando) i HR110.

---

## Skabelon 8 — Fuld opsætning (ES32D26)

```bash
# Tæller 1: aktiveringer på DI8 (discrete input 7)
set gpio 108 input 7
set counter 1 mode 1 hw-mode:sw input-dis:7 edge:falling bit-width:32 start-value:0 debounce:on debounce-ms:10
set counter 1 control auto-start:on running:on

# Tæller 2: flowmåler på DI7 (discrete input 6), 100 pulser pr. liter
set gpio 107 input 6
set counter 2 mode 1 hw-mode:sw input-dis:6 edge:falling bit-width:32 prescaler:100 debounce:off
set counter 2 control auto-start:on running:on

# Tæller 3 og 4 bruges ikke
set counter 3 disable
set counter 4 disable

save
show config counter
```

---

## Registre

Registrene tildeles automatisk og kan ikke ændres (`index-reg:`, `raw-reg:` m.fl. afvises):

| Tæller | Værdi (× scale) | Prescaled (÷ prescaler) | Frekvens | Kontrol | Compare-værdi |
|---|---|---|---|---|---|
| 1 | HR100-103 | HR104-107 | HR108 | HR110 | HR111-114 |
| 2 | HR120-123 | HR124-127 | HR128 | HR130 | HR131-134 |
| 3 | HR140-143 | HR144-147 | HR148 | HR150 | HR151-154 |
| 4 | HR160-163 | HR164-167 | HR168 | HR170 | HR171-174 |

Antal ord efter `bit-width`: 1 (≤ 16 bit), 2 (32 bit), 4 (64 bit) — **lavt ord først**.

**Kontrolregistret (HR110/130/150/170):**

| Bit | Betydning | Adgang |
|---|---|---|
| 0 | Reset til `start-value` (kommando, slettes automatisk) | Skriv |
| 1 | Start (kommando, slettes automatisk) | Skriv |
| 2 | Stop (kommando, slettes automatisk) | Skriv |
| 3 | Overflow/underflow | Læs |
| 4 | Compare-match | Læs |
| 7 | Kører (vedvarende tilstand — sæt for at starte, slet for at stoppe) | Læs/skriv |

`control auto-start:on` gemmes i tællerens konfiguration, ikke i kontrolregistret (BUG-445).

---

## Fejlfinding

| Symptom | Årsag | Løsning |
|---|---|---|
| Tælleren tæller ikke, `show counter` viser værdien uændret | Aktiveret, men ikke startet | `set counter 1 control auto-start:on running:on` + `save` |
| Tæller ikke efter genstart | auto-start ikke sat (eller firmware før v7.9.68.47) | Som ovenfor |
| Tæller ved *slip* i stedet for tryk | Forkert flanke for indgangen | `edge:rising` ↔ `edge:falling` |
| HR104 viser 0 | Tælleværdi < prescaler (heltalsdivision) | Forventet — se Skabelon 3 |
| Indstillinger "forsvinder" | Opsætningen er delt over flere `mode 1`-linjer | Saml alt på én linje |
| `hw-mode:hw`/`sw-isr` afvises | ES32D26 | Brug `sw` |
| Frekvensen (HR108) er 0 | Ingen pulser, eller tælleren kører ikke | Tjek signal og `running` |
