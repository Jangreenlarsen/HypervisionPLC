# Hardware-testplan — v7.9.68.47 → v7.9.68.63

**Til:** test af alt, der er bygget og PC-testet, men ikke testet på PLC'en. **Test-PLC:** 10.1.1.30 (ES32D26). **Filer:** `.pio/build/es32d26/` — `firmware.bin` (usigneret), `firmware_signed.bin` (signeret), `firmware_tampered.bin` (én bit ændret, signaturen passer ikke).

Kør afsnittene i rækkefølge: afsnit 1 skifter til signeret OTA, og hvis noget dér fejler, er vejen tilbage Rollback-knappen.

Notér resultatet i kolonnen ✓/✗ og send mig fejlene (gerne med `show status`-/CLI-output).

---

## 1. OTA og signeret firmware (FEAT-439, BUG-448)

PLC'en kører i dag v7.9.68.55, som ikke tjekker signatur.

| # | Trin | Forventet | ✓/✗ |
|---|---|---|---|
| 1.1 | System → OTA: upload **`firmware.bin`** (usigneret — .55 accepterer den) | Genstarter på v7.9.68.63 | |
| 1.2 | System-siden → Firmware Information | *Rollback mulig: Ja — v7.9.68.55.2696 i ota_1* (eller *ukendt version* hvis .55 er ældre end markøren) | |
| 1.3 | CLI `show ota` | Kører fra ota_0, begge slots `VALID` | |
| 1.4 | CLI `show version` | `License: AGPL-3.0-or-later`, `Source: https://github.com/…` | |
| 1.5 | Upload **`firmware.bin`** igen | **Afvist:** *"Firmware er ikke signeret - upload firmware_signed.bin"*; PLC'en kører videre uændret | |
| 1.6 | Upload **`firmware_tampered.bin`** | **Afvist:** *"Ugyldig signatur - firmware afvist"*; hændelsesloggen har en linje | |
| 1.7 | Upload **`firmware_signed.bin`** | Accepteres, genstarter på .63 (nu i ota_1); *Rollback mulig: Ja — v7.9.68.63.xxxx i ota_0* | |

**Hvis 1.7 fejler** (korrekt signeret fil afvises): tryk **Rollback** → tilbage på .55 → send mig fejlteksten.

## 2. CLI (FEAT-437, BUG-448)

| # | Kommando | Forventet | ✓/✗ |
|---|---|---|---|
| 2.1 | `show otaa` | `SHOW: ukendt argument 'otaa'` + `Mente du: ota?` | |
| 2.2 | `shwo version` | `ukendt kommando 'shwo'` + `Mente du: show?` | |
| 2.3 | `set countr 1` | Forslag `counter` | |

## 3. Tællere (BUG-447, FEAT-438)

Tæller 1 = sw på DI8 (`input-dis:7`), som i dag.

| # | Trin | Forventet | ✓/✗ |
|---|---|---|---|
| 3.1 | `show counter 1` | Linjen `Sampling: scan-task hvert 1 ms (skifteregister-indgang …)` | |
| 3.2 | Dashboard → Digital I/O: tryk DI1–DI8 én ad gangen | Hver indgang skifter korrekt (74HC165 læses nu med 2 µs i stedet for 10 µs) | |
| 3.3 | Pulser på DI8 med kendt antal (fx 100 tryk, eller signalgenerator 100 Hz i 10 s = 1000) med `debounce:off` | Tælleren rammer antallet præcist | |
| 3.4 | Signalgenerator: 200, 300, 400 Hz i 10 s | Præcist (forventet grænse ca. 400–450 Hz). Notér hvor den begynder at tabe | |
| 3.5 | `set counter 1 control counter-reg-reset-on-read:on` → `save` | Status viser `counter-reg-reset-on-read: ENABLED` | |
| 3.6 | Tæl lidt, så `read h-reg 100 2` (CLI-læsning går gennem samme hook som FC03) | Værdien vises, og tælleren står derefter på startværdien | |
| 3.7 | Genstart → `show config counter` | Indeholder `control counter-reg-reset-on-read:on` | |
| 3.8 | `set counter 1 control counter-reg-reset-on-read:off` → `save` | Læsning nulstiller ikke længere | |

## 4. Statusside (FEAT-434, FEAT-435, FEAT-441)

| # | Trin | Forventet | ✓/✗ |
|---|---|---|---|
| 4.1 | Monitor → **Statusside** | 17 kort, forhåndsvisning af `/` til højre | |
| 4.2 | Vælg System, Watchdog, Expansion Boards, Trend Recorder; flyt Watchdog øverst; Gem | Forhåndsvisningen viser kortene i den rækkefølge | |
| 4.3 | Åbn `/` i et privat vindue (uden login) | Samme kort; Watchdog uden fejltekst; Expansion uden IP | |
| 4.4 | Trend Recorderen optager (fx HR100) | Kurve + seneste værdi på `/` | |
| 4.5 | Fravælg alle → Gem → `/` | Ingen kort; `/api/public-dashboard/trend` giver 404 | |
| 4.6 | **Save** (øverst) → genstart → `/` | Valget er bevaret | |

## 5. Sikkerhed (FEAT-440, SSE-dokumentation)

| # | Trin | Forventet | ✓/✗ |
|---|---|---|---|
| 5.1 | Log ind med en bruger, der stadig har `modbus123` (opret evt. en testbruger) | Rødt banner øverst på alle sider | |
| 5.2 | Telnet-login med samme bruger | Advarsel efter "Welcome" | |
| 5.3 | `show status` | Starter med advarslen | |
| 5.4 | Skift adgangskoden → log ind igen | Intet banner, ingen advarsel | |
| 5.5 | System → SSE-kortet | Gul advarsel om ukrypteret SSE | |

## 6. Expansion Board (FEAT-442 — HTTPClient erstattet)

Kun hvis et board er tilsluttet.

| # | Trin | Forventet | ✓/✗ |
|---|---|---|---|
| 6.1 | Monitor → Modbus → Expansion Boards | *Online — fw x.y · n kanaler* (status-kaldet via den nye klient) | |
| 6.2 | I/O-siden → board-kanaler: hent og gem en kanal-config | Virker som før | |
| 6.3 | `mbx 1 ota status` | Svar fra boardet | |
| 6.4 | Sluk boardet → Monitor | *Offline* efter ca. 2 tjek, ingen hængende side | |

## 7. Ethernet med LTO (BUG-423b) — hvis W5500 er ombundet til GPIO25/26

| # | Trin | Forventet | ✓/✗ |
|---|---|---|---|
| 7.1 | `set ethernet enable` → `save` → genstart | Booter uden crash, `show ethernet` viser link + IP | |
| 7.2 | `show status` 10 gange med 1 min mellemrum | Ingen crash | |
| 7.3 | Web-UI via Ethernet-IP'en | Virker | |

---

**Efter testen:** send ✗-linjerne. Det, der består, markeres som hardware-testet i `BUGS_INDEX.md`.
