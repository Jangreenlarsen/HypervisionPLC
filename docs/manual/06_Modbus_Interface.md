# 6. Modbus-interface

[← 5. CLI/Konsol](05_CLI_Konsol.md) · [Indeks](00_INDEKS.md) · Næste: [7. REST API →](07_REST_API.md)

---

## 6.1 To roller, samme register-lager

Systemet kan samtidig (eller på ES32D26: skiftevis, se [§2.4](02_Hardware_og_Moduler.md#24-modbus-transceiver-delt-vs-dedikeret-uart)) spille to Modbus-roller:

- **Slave** — svarer på forespørgsler fra et overordnet system (SCADA/HMI). Systemet er passivt: det venter på og besvarer requests.
- **Master** — sender selv forespørgsler ud til andre enheder på RS-485-bussen (VFD'er, sensorer, målere) og gemmer deres svar i en intern cache.

Begge roller læser og skriver til det **samme interne register-/coil-lager**, som også er det ST Logic og REST API'et arbejder på. Det er derfor Slave-siden kan eksponere data som Master-siden selv har indsamlet fra andre enheder — systemet fungerer som en Modbus-gateway/aggregator uden ekstra kode.

## 6.2 Understøttede function codes

| FC | Navn | Slave (svarer på) | Master (kan sende) |
|----|------|:---:|:---:|
| 0x01 | Read Coils | ✅ | ✅ |
| 0x02 | Read Discrete Inputs | ✅ | ✅ |
| 0x03 | Read Holding Registers | ✅ | ✅ (inkl. multi-register) |
| 0x04 | Read Input Registers | ✅ | ✅ |
| 0x05 | Write Single Coil | ✅ | ✅ |
| 0x06 | Write Single Register | ✅ | ✅ |
| 0x0F | Write Multiple Coils | ✅ | — |
| 0x10 | Write Multiple Registers | ✅ | ✅ |

Protokol: **Modbus RTU** over RS-485 (framing, CRC16) for systemets egen Slave/Master-rolle beskrevet i dette afsnit. Systemet er derudover også en **Modbus TCP-klient** (MBAP-framing) mod eksterne Modbus Expansion Boards — se [§6.7](#67-modbus-expansion-boards-feat-409).

**Adgangskontrol på RTU-bussen (bevidst fravalg, ikke en overset mangel):** Modbus RTU har pr. protokol-design ingen adgangskontrol på function-code- eller register-niveau — enhver enhed der fysisk er koblet på RS-485-bussen og kender (eller gætter) et slave-ID kan sende læse-/skrive-forespørgsler, inklusive til systemets egne kontrol-registre (fx `ST_LOGIC_CONTROL_REG_BASE`). Dette er en egenskab ved selve RTU som fysisk lag — samme begrænsning gælder ethvert Modbus RTU-slave-udstyr, ikke kun dette system — og løses i praksis ved **fysisk adgangskontrol til bussen** (hvem har adgang til RS-485-kablingen), ikke i software. Samme ræsonnement gælder ST Logic-programmers Modbus Master-kald (`MB_READ_*`/`MB_WRITE_*`): de kan i dag adressere enhver slave 1-247 på bussen uden en indbygget allowlist — men et ST-program kræver i sig selv allerede skriverettighed til at blive uploadet, og en bruger med den rettighed har allerede tilsvarende bus-adgang via `mb write` i CLI'en, så en allowlist ville ikke reelt begrænse en angriber med den adgang.

## 6.3 Register-kapacitet

| Type | Antal | Adresser |
|------|-------|----------|
| Holding Registers | 256 | 0-255 |
| Input Registers | 256 | 0-255 |
| Coils | 256 bits (32 bytes) | 0-255 |
| Discrete Inputs | 256 bits (32 bytes) | 0-255 |

For den **komplette, adresse-for-adresse** oversigt over hvad hvert register betyder (systemregistre, ST Logic-eksporterede variabler, tæller-/timer-mapping, m.m.), se [**`../../MODBUS_REGISTER_MAP.md`**](../../MODBUS_REGISTER_MAP.md) — den autoritative, løbende vedligeholdte register-reference. Dette kapitel giver konceptet; den fil giver hver eneste adresse.

Kort orienteringsguide til de vigtigste blokke (se register-map-filen for præcise grænser og eventuelle ændringer):
- **IR 220-251** — ST Logic EXPORT-variabler, synlige som Input Registers for eksterne SCADA-systemer
- **HR 0-17** (ES32D26) — Analog I/O: Vi1-4/Ii1-4 raw+kalibreret værdi, AO1-2 setpoint (se [kapitel 4](04_Web_Dashboard_og_Monitor.md) og `show analog`)
- Dynamisk allokerede registre til tællere, timere og ST Logic-bindings, styret af en intern register-allokator der forhindrer utilsigtet overlap mellem subsystemer

## 6.4 Modbus Slave — konfiguration

```
set modbus-slave enabled on
set modbus-slave slave-id 1        (gyldigt: 1-247, eller 0 for broadcast-modtagelse)
set modbus-slave baudrate 9600
set modbus-slave parity none       (none | even | odd)
set modbus-slave stop-bits 1

show modbus-slave                  (status + løbende statistik)
```

## 6.5 Modbus Master — konfiguration

Master-rollen kører som en **asynkron baggrundstask** med prioritetskø og cache — den blokerer aldrig ST Logic eller andre subsystemer, selv ved langsomme eller udeblevne svar fra eksterne enheder.

```
set modbus-master enabled on
set modbus-master baudrate 9600
set modbus-master timeout 1000            (ms, svartimeout pr. forespørgsel)
set modbus-master max-requests 5          (MB_*-kald per ST-program per scan-cyklus)
set modbus-master cache-ttl 0             (0 = værdier udløber aldrig af sig selv)
set modbus-master cache-size 32           (max unikke (slave,adresse)-kombinationer)
set modbus-master queue-size 16           (max ventende requests)

show modbus-master                        (status + kø-/cache-statistik)
```

**Prioritering:** skrivninger (Write) prioriteres altid over læsninger; blandt læsninger prioriteres "første læsning" (intet cachet endnu) over "opdatering af allerede kendt værdi". Er køen fuld, fortrænges den laveste-prioritets ventende request — se [§13.4](13_Fejlfinding.md#modbus-master-holder-op-med-at-opdatere-en-bestemt-adresse) hvis en bestemt adresse holder op med at opdatere.

**Manuelt afprøve Master:** se [`mb read`/`mb write`](05_CLI_Konsol.md#54-mb--modbus-master-fra-kommandolinjen) i CLI-kapitlet, eller den manuelle Read/Write-formular i dashboardets Modbus Master-kort ([§4.2](04_Web_Dashboard_og_Monitor.md)).

**Fra ST Logic:** se [kapitel 8](08_ST_Logic_Programmering.md#modbus-master-fra-st-logic) for `MB_READ_HOLDING`/`MB_WRITE_HOLDING` m.fl.

**Via web-GUI (FEAT-166):** slave-id/baudrate/paritet/stopbits/inter-frame-delay (Slave) og enabled/baudrate/paritet/stopbits/timeout/inter-frame-delay/max-requests/cache-ttl (Master) kan nu også sættes fra `/system`-siden — samme felter som ovenfor, blot uden CLI. `cache-size`/`queue-size` er ikke inkluderet dér, da de er faste compile-time-kapaciteter (`MB_CACHE_MAX`/`MB_QUEUE_MAX`), ikke runtime-konfigurerbare via REST i dag.

## 6.6 Overvågning af faktisk bustrafik

Både Slave- og Master-siden logges live i **Modbus Aktivitetsloggen** i dashboardet ([§4.2](04_Web_Dashboard_og_Monitor.md)) — et wire-level-vindue der viser hver transaktion uanset hvor den stammer fra (ST Logic, CLI, dashboard, eller en ekstern master der taler til jeres slave). Uvurderligt værktøj når noget "burde virke, men gør ikke" — se [kapitel 13](13_Fejlfinding.md).

## 6.7 Modbus Expansion Boards (FEAT-409)

Et "HypervisionPLC Extension Board" er et separat, fysisk board med sine egne RS485/RS232-kanaler (kanal A + B), som PLC'en administrerer over netværket — al konfiguration og diagnose sker herfra, boardet har ingen egen driftsbrugerflade (kun en engangs seriel opsætning ved installation).

**System-siden → "Modbus Expansion Boards"-kortet:**

- **Tilføj et board**: et fast **board nr (1-8)** du selv vælger (anbefaling: match fysisk mærkning i skabet/panelet — nummeret kan ikke ændres bagefter, kun fjernes og tilføjes igen), en **type** (i dag kun "Modbus Expansion, 2× RS485/RS232" — flere board-typer, fx rene digitale ind-/udgangs-expansion-boards, forventes tilføjet efterhånden som de findes som fysisk hardware), navn (frit valgt label), IP-adresse, og det Bearer-token boardets serielle opsætnings-CLI viser (kommandoen `status` på boardet selv). Tokenet vises aldrig igen af PLC'en efter det er gemt — hav det klar fra boardets egen skærm.
- **Test forbindelse**: henter boardets live status (firmware-version, oppetid, antal kanaler) — bekræfter at IP og token er korrekte.
- **Kanaler**: viser og redigerer kanal A/B's konfiguration (mode RS485/RS232, baudrate, paritet, stopbits, timeout) direkte fra boardet — ændringer gemmes atomisk (alle felter i ét kald, aldrig delvist). Statistik (antal forespørgsler/fejl) vises live.
- **Test funktions-register**: et diagnostisk panel til at afprøve en enkelt Modbus-transaktion (læs/skriv holding-register, coil, osv.) mod en given slave på den valgte kanal — til opsætning/fejlsøgning, ikke til løbende drift.

![System-siden — Modbus Expansion Boards-kortet, med et tilsluttet board](assets/screenshots/system_modbus.png)

**Kontinuerlig datatrafik i ST Logic (FEAT-410):** den løbende, høj-frekvente Modbus-trafik mod feltbusserne bag et expansion-board går via Modbus TCP direkte til boardet (ikke gennem PLC'ens web-UI), og kan læses/skrives fra ST Logic-programmer med `MBX_*`-funktionsfamilien — samme non-blocking cache/kø-mønster som den lokale RS485-bus' `MB_*`-funktioner (se [Appendiks D.5.9b](D_ST_Logic_Funktionsreference.md#d59b-modbus-expansion-board-mbx_-feat-410)), blot med et ekstra `board`- og `kanal`-argument foran. **v7.9.68.0:** multi-register/coil WRITE tilføjet (`MBX_WRITE_HOLDINGS`/FC16, `MBX_WRITE_COILS`/FC15) — multi-**read** (`MBX_READ_HOLDINGS`) findes stadig ikke. Kø/cache-diagnostik: `show modbus-expansion queue` (se [Appendiks A](A_CLI_Kommando_Reference.md#modbus-expansion-board-feat-409)).

**Dashboard-badge (Monitor):** Dashboardet ([kapitel 4](04_Web_Dashboard_og_Monitor.md)) har et tilsvarende "Modbus Expansion Boards"-kort med et online/offline-badge pr. board (grøn = online + fw-version/kanaltal, rød = offline, grå = endnu ikke tjekket), plus et samlet "N/M online"-badge i kort-overskriften. Boards tjekkes automatisk hvert 30. sekund, så længe dashboardet er åbent i en browser — luk fanen, og pollingen stopper (enheden selv ringer aldrig ud af sig selv uopfordret).

**CLI:** se [Appendiks A](A_CLI_Kommando_Reference.md#modbus-expansion-board-feat-409) for `set modbus-expansion`/`show modbus-expansion`/`mbx`-kommandoerne.
**REST API:** se [Appendiks B](B_REST_API_Reference.md#modbus-expansion-board-feat-409).

---

[← 5. CLI/Konsol](05_CLI_Konsol.md) · [Indeks](00_INDEKS.md) · Næste: [7. REST API →](07_REST_API.md)
