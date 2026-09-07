# 4. Web Dashboard & Monitor

[← 3. Installation](03_Installation_og_Foerste_Opstart.md) · [Indeks](00_INDEKS.md) · Næste: [5. CLI/Konsol →](05_CLI_Konsol.md)

---

## 4.1 Sideoversigt

Webgrænsefladen består af fem sider, alle bag samme login:

| Side | URL | Formål |
|------|-----|--------|
| **Dashboard** | `/` | Live-monitor: system, netværk, Modbus, alarmer, IO, tællere/timere |
| **ST Logic Editor** | `/editor` | Skriv, kompilér, upload og debug ST-programmer med runtime-monitor |
| **System** | `/system` | Systemindstillinger (uden for det almindelige `set`-CLI-flow) |
| **OTA-opdatering** | `/ota` | Upload ny firmware |
| **Web-CLI** | `/cli` | Fuld CLI-konsol i browseren — samme kommandoer som seriel/telnet |

Login sker via HTTP Basic Auth; browseren gemmer credentials i `sessionStorage` for resten af sessionen (går tabt ved lukning af fanen).

## 4.2 Dashboard — layout og faner

Dashboardet er organiseret i **kort** (cards), grupperet under faner:

- **Alle** — alle kort samlet
- **Overblik** — System, Netværk, Alarm Historik, Hændelseslog
- **Modbus** — Modbus Slave, Modbus Master (+ manuel Read/Write), Modbus Aktivitetslog, RTU-trafik
- **Forbindelser** — HTTP API/SSE, TCP-forbindelsesmonitor, NTP
- **Applikation** — Tællere, Timere, ST Logic, Digital I/O
- **Custom (FEAT-167)** — et uafhængigt, brugervalgt kort-sæt, se nedenfor

**Fri kort-placering (FEAT-160):** hvert kort kan flyttes vilkårligt (træk i ☰-håndtaget i kortets titel, eller ved at venstreklikke og holde et vilkårligt sted på kortet — undtagen på knapper/felter/links, FEAT-162) og størrelsesændres frit (træk i hjørne-håndtaget nederst til højre) i et 12-kolonners grid — ikke bare et bred/normal-valg, men fuld 2D-placering ligesom et dashboard-builder-værktøj (Grafana e.l.). Lander man et kort oven i et andet, bytter de plads; overlapper en ny størrelse et nabokort, skubbes det ned. Alle periodiske dataopdateringer sættes på pause, mens et kort aktivt flyttes/resizes (BUG-359), så kortet ikke "snapper" tilbage midt i trækket.

**Layoutet gemmes separat pr. skærmstørrelse OG pr. fane (FEAT-167):** at flytte "Modbus Slave"-kortet mens du kigger på Modbus-fanen ændrer IKKE dets position på Alle-fanen eller Overblik-fanen — hver fane har sit eget, uafhængige layout. Et "Nulstil layout"-knap findes på Indstillinger-siden (pr. skærmstørrelse — nulstiller dog kun den aktuelt viste fane) — samme funktion findes også direkte i Metrics-visningen som "⊞ Auto-organisér" yderst til højre i fane-linjen, så man ikke behøver skifte side for at få et pænt standard-layout at starte fra (fikset i BUG-360 til korrekt kun at gøre kort lige akkurat så store som deres indhold reelt kræver, ikke mere). På mobil (≤768px) er fri placering slået fra — kortene stables i én kolonne, uden træk-/resize-håndtag, da det ikke giver mening på en smal touch-skærm. Kort-*synlighed* og *fane-tildeling* (Indstillinger-siden) gemmes derimod fælles for hele enheden (på selve ESP32'en), uafhængigt af skærmstørrelse — kun selve placeringen/størrelsen er lokal pr. browser.

**Custom-fanen (FEAT-167):** på Indstillinger-siden kan hvert kort — uafhængigt af dets normale fane-tildeling — også markeres til at vise sig på en ny "Custom"-fane. Et kort her **forsvinder ikke** fra sin normale fane (fx et Modbus-kort markeret til Custom ses stadig under både "Modbus" og "Custom") — Custom er en ekstra, brugerdefineret samling ved siden af, ikke en omplacering. Custom-fanens layout er uafhængigt af de øvrige faners, ligesom enhver anden fane.

**Mindstestørrelse (BUG-356):** kort med en indbygget rulleflade (Modbus Aktivitetslog, Alarm Historik, Hændelseslog, Modbus manuel Read/Write-resultatet) kan ikke resizes mindre end det de reelt kræver for at vise deres fulde indhold — rammen bliver rød og resize stopper, hvis du prøver at trække under den grænse. Standard-layoutet regner automatisk med denne mindstestørrelse fra starten, så en tabel der endnu ikke har hentet data, ikke låser kortet for lille til når data ankommer.

**Pause under flyt/resize (BUG-359):** mens et kort aktivt flyttes eller resizes (venstreklik holdt nede), sættes ALLE periodiske dataopdateringer på pause — ellers ville kortet man er ved at placere kunne "snappe" tilbage til sin gamle position midt i trækket, og andre kort kunne hoppe i indhold/størrelse imens. Opdateringerne genoptages automatisk, så snart museknappen slippes.

### Nøglekort

**System** — firmwareversion, oppetid, heap fri/min/fragmentering, PSRAM, aktive FreeRTOS-tasks.

**Netværk** — Wi-Fi/Ethernet-status, IP/gateway/DNS, Telnet-status, Wi-Fi-genforbindelser.

**Modbus Slave / Modbus Master** — konfiguration og løbende statistik (requests, success rate, fejltyper). Master-kortet indeholder desuden cache-/kø-statistik (hit rate, kø-dybde, prioritets-drops) og en **manuel Read/Write-formular** til hurtige ad-hoc-forespørgsler uden at skulle bruge CLI.

**Modbus Aktivitetslog** — et wire-level-vindue ind i hvad der rent faktisk sker på Modbus-interfacet lige nu: hver transaktion (Master *og* Slave-rolle) med rolle, kilde (ST Logic/CLI/dashboard/ekstern master), slave-ID, function code, adresse, værdi og status. RAM-only (nulstilles ved reboot), fungerer som et levende diagnoseværktøj — se [kapitel 13](13_Fejlfinding.md) for hvordan den bruges til fejlsøgning.

Loggen rummer **500 transaktioner** (ringbuffer i PSRAM) og har tre knapper:

| Knap | Funktion |
|------|----------|
| **Stop logning** / **Start logning** | Fryser opsamlingen uden at rydde indholdet — nyttigt når man vil nå at læse eller eksportere et øjebliksbillede før det ruller videre. Tilstanden nulstilles til "kører" ved reboot. |
| **Eksporter CSV** | Henter **hele** loggen (ikke kun det viste udsnit) og gemmer den som semikolon-separeret CSV med både klokkeslæt, uptime og Unix-tid. |
| **Ryd log** | Sletter alle poster. Rydder man mens logningen er stoppet, forbliver den stoppet. |

Antalsfeltet styrer hvor mange linjer der **hentes og vises** (standard 100). Dashboardet henter kun det viste udsnit ved hver opdatering — hele loggen trækkes udelukkende ved eksport.

**Tidsstempler:** er NTP synkroniseret, vises rigtigt klokkeslæt (se [kapitel 12](12_Netvaerkskonfiguration.md)); ellers vises tiden siden opstart. Begge dele følger altid med i CSV-eksporten.

**Alarm Historik** — systemhændelser (heap-advarsler, kommunikationsfejl, auth-fejl, m.fl.), med filtrering på sværhedsgrad og kvitteringsstatus.

**Hændelseslog** — audit-spor over *hvem* der har lavet *hvad* og *hvornår*: config gemt, reboot (REST/CLI/OTA), login-fejl og boot ("Hændelser"), samt registerændringer skrevet via REST API eller af en ekstern Modbus-master ("Registerændringer" — gammel/ny værdi, adresse, og for REST-kald hvilken bruger + klient-IP). Ringbuffer på **200 poster** i PSRAM, samme start/stop-, CSV-eksport- og filtreringsmønster som Modbus Aktivitetslog ovenfor. **Dækker bevidst ikke** CLI-skrivninger eller ST Logics periodiske output-binding — se [Appendiks B.16a](B_REST_API_Reference.md) for begrundelsen. **FEAT-172:** samme Hændelseslog findes nu også som sin egen fane på **`/logs`**-siden (link i topnavigationen), i fuld sidebredde ved siden af API Audit Log-fanen — brug dashboard-kortet til hurtigt overblik undervejs, og `/logs`-siden når du har brug for mere skærmplads til at grave i en lang log.

**Digital I/O** — live-visning og manuel styring af digitale ind-/udgange.

**Analog I/O** (kun ES32D26) — live-visning af de 4 spændingsindgange (Vi1-4, 0-10V), 4 strømindgange (Ii1-4, 4-20mA) og 2 analoge udgange (AO1-2, DAC). Kortet skjules automatisk hvis ingen kanaler er aktiveret. Sæt en ny AO-værdi direkte fra dashboardet (felt + "Sæt"-knap) — virker med det samme, ligesom Digital I/O's toggle-knapper.

Kanaler aktiveres og kalibreres via CLI (`set analog <vi1-4|ii1-4|ao1-2> enabled on|off`, `set analog <kanal> scale|offset <tal>`, `show analog`), REST (`GET`/`POST /api/analog`, se [Appendiks B](B_REST_API_Reference.md)) eller nu også direkte fra `/system`-sidens "Analog I/O Kalibrering"-kort (FEAT-166) — samme scale/offset-felter, med en "Gem"-knap pr. kanal. Firmwaren kender ikke boardets præcise delerforhold/shunt-værdier — standardkalibreringen antager fuld ADC-skala svarer til fuldt måleområde (10,00V hhv. 20,00mA), som bør finjusteres mod en kendt referencekilde (multimeter) efter installation, ligesom en tællers `scale-factor`. Vi1 (GPIO14) og Vi3 (GPIO27) deler ADC2 med WiFi-radioen — de kan ikke læses mens WiFi er tilsluttet, og viser i så fald deres sidste gyldige værdi.

**Tællere / Timere / ST Logic** — status og seneste værdier for hver af de 4 tællere, 4 timere og 4 ST-programmer.

## 4.3 ST Logic Editor

Se [kapitel 8](08_ST_Logic_Programmering.md) for selve sproget. Dette afsnit dækker værktøjet.

Editoren har 4 uafhængige program-faner (Logic1-4), vist øverst i deres egen række sammen med **Editor**-knappen (som skifter tilbage til kildekode-visningen — placeret her og ikke i værktøjslinjen, da den hører logisk sammen med program-valget, ikke med selve handlingerne på det valgte program). Værktøjslinjen:

| Knap | Funktion |
|------|----------|
| **Kompilér** | Oversætter kildekoden til bytecode. Fejl markeres direkte i editoren med linjenummer. |
| **Gem Config** | Gemmer program + bindings persistent. |
| **Stop** | Deaktiverer programmet (stopper eksekvering, bevarer variabeltilstand). |
| **Reinit** | "Cold restart" — nulstiller alle variabler til deres initialværdier, stateful storage (timere/tællere/edge-detektion) og statistik. |
| **Slet** | Fjerner programmet helt. |
| **Download / Upload** | Hent/gem kildekode som `.st`-fil. |
| **Find** | Søg/erstat i kildekoden (Ctrl+F/Ctrl+H). |
| **Bindings / Monitor / Settings** | Skift mellem variabel-bindings-konfiguration, runtime-monitor og globale motor-indstillinger. |

### Runtime Monitor

Live-visning af alle programvariabler mens programmet kører, med:
- **Udførelser / Tid (ms) / Min-Max / Fejl / Overruns** — performance- og sundhedsstatistik
- **Trend-kurver** pr. variabel, valgbar historik-længde og opdateringshastighed
- **Debug-kontroller:** Pause Execute, Single Step, Single Cycle, Normal Execute — fuld single-step-debugger med PC-visning og breakpoints

> **Fejlfindingstip:** stiger "Udførelser" støt, men ingen variabler ændrer sig og "Fejl" forbliver 0, er programmet ikke crashet — det venter typisk på noget der aldrig sker (f.eks. et Modbus-svar). Se [kapitel 13, afsnit "ST-program ser ud til at køre, men intet opdateres"](13_Fejlfinding.md#st-program-ser-ud-til-at-koere-men-intet-opdateres) for en systematisk fremgangsmåde.

### Settings (FEAT-164)

Globale motor-indstillinger — gælder alle 4 programmer samtidig, ikke kun det valgte slot:

- **ST Logic-motor aktiveret** — global til/fra-kontakt for hele motoren (alle 4 programmer stoppes/genoptages samlet). Svarer til modul-flaget `st_logic` under `/api/modules` / CLI'ens modul-styring.
- **Eksekveringsinterval** — hvor ofte alle programmer kører (2/5/10/20/25/50/75/100 ms, standard 10 ms). Svarer til CLI'ens `set logic interval:X`.

Begge dele var tidligere kun tilgængelige via CLI. Ændringer aktiveres med det samme; brug "Gem Config" for at overleve reboot. Motorens interne trace-debug (`set logic debug:true|false`, bytecode-udskrift til seriel/telnet) er bevidst ikke medtaget — den har ingen synlig effekt i web-GUI'et.

## 4.4 Web-CLI (`/cli`)

En fuld terminal-emulering i browseren, med samme kommandosæt som seriel/telnet-konsollen (se [kapitel 5](05_CLI_Konsol.md)). Praktisk når man er logget ind via HTTPS og ikke ønsker at åbne en separat telnet-session — og det eneste af de tre konsol-adgange der kan beskyttes af TLS.

## 4.5 Real-time opdatering (SSE)

Dashboardet bruger **Server-Sent Events** til at modtage register-, tæller- og timer-ændringer i realtid uden konstant polling. Falder SSE-forbindelsen væk, falder UI'et automatisk tilbage til almindelig polling — funktionaliteten er uændret, blot med lidt højere opdateringsforsinkelse. Se [`../SSE_USER_GUIDE.md`](../SSE_USER_GUIDE.md) for den tekniske protokol, hvis I bygger jeres eget klient-integration mod samme SSE-strøm.

---

[← 3. Installation](03_Installation_og_Foerste_Opstart.md) · [Indeks](00_INDEKS.md) · Næste: [5. CLI/Konsol →](05_CLI_Konsol.md)
