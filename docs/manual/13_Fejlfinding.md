# 13. Fejlfinding

[← 12. Netværkskonfiguration](12_Netvaerkskonfiguration.md) · [Indeks](00_INDEKS.md) · Næste: [Appendiks A: CLI-reference →](A_CLI_Kommando_Reference.md)

---

## 13.1 Diagnostisk værktøjskasse

Start altid her, uanset problem — de fem kommandoer/visninger der giver mest information hurtigst:

| Værktøj | Kommando/sted | Viser |
|---------|----------------|-------|
| Systemstatus | `show status` | Oppetid, heap fri/min/fragmentering, GPIO-tilstand |
| Firmwareversion | `show version` | Version, build, git-commit — hav altid dette klar ved support-henvendelser |
| Modbus-status | `show modbus` | Slave + Master status, statistik, kø/cache |
| Modbus Aktivitetslog | Dashboard, [§4.2](04_Web_Dashboard_og_Monitor.md) | Faktisk wire-trafik, live, med kilde-attribution |
| Alarm Historik | Dashboard, [§4.2](04_Web_Dashboard_og_Monitor.md) | Systemhændelser med tidsstempel |

Ved ST Logic-relaterede problemer, tilføj:
```
show logic <id>              (statistik: udførelser, fejl, sidste fejlbesked)
show logic <id> bytecode      (antal kompilerede instruktioner — 0 betyder ikke-kompileret)
```
...og brug **Runtime Monitor** i editoren ([§4.3](04_Web_Dashboard_og_Monitor.md#43-st-logic-editor)) til at se variabler live.

## 13.2 Generel fremgangsmåde

1. **Skelnen: kører logikken, eller er den stoppet?** `show logic <id>` — stiger `Udførelser`? Hvis nej, er problemet i om programmet overhovedet eksekveres (deaktiveret? kompileret?). Hvis ja, fortsæt til punkt 2.
2. **Stiger fejltælleren?** Hvis ja: `Last error` fortæller præcis hvad der går galt — typisk en simpel rettelse i selve ST-koden.
3. **Udførelser stiger, fejl gør ikke, men intet opdateres:** se [§13.3](#13-3-st-program-ser-ud-til-at-koere-men-intet-opdateres) — dette er ikke et VM-problem.
4. **Involverer det Modbus?** Åbn Modbus Aktivitetsloggen — ser I overhovedet transaktioner mod den relevante adresse? Ingen transaktioner = problemet er *før* bussen (konfiguration, adgangskontrol). Transaktioner med fejl-status = problemet er *på* bussen (kabling, slave-ID, baudrate, ekstern enhed nede).
5. **Involverer det netværk/REST API?** Bekræft først med `curl` direkte mod enheden, uden om jeres integration — udelukker om fejlen er i klienten eller i PLC'en.

## 13.3 "ST-program ser ud til at køre, men intet opdateres"

**Symptom:** `Udførelser` i Runtime Monitor stiger jævnt, `Fejl` forbliver 0, men ingen variabler ændrer værdi — programmet virker "levende", men gør ingenting. `Stop`+`Start` eller `Reinit` hjælper ikke.

Dette er *ikke* en fejl i selve ST-programmet. Det betyder programmet kører præcis som skrevet — og typisk **venter på noget der aldrig sker**. To kendte, bekræftede årsager:

**A) Modbus Master er ikke faktisk aktiv, selvom konfigurationen siger "on"**

På ES32D26 kan RS-485-aktivering ved boot blive afbrudt (se [§3.3](03_Installation_og_Foerste_Opstart.md#33-opstart-p%C3%A5-es32d26--rs-485-vs-usb-konsol)). Symptomet er præcis dette: ST-logikken kører videre, men ethvert `MB_READ_*`/`MB_WRITE_*`-kald returnerer straks uden effekt.

**Tjek:** `show modbus-master` — står der en advarsel om at RS-485 ikke blev aktiveret ved boot, selvom `Status` burde være `ENABLED`? Ret det uden reboot:
```
set modbus-master enabled on
```

**B) En bestemt Modbus-adresse "hænger" i intern ventetilstand**

Hvis Master-kommunikation generelt virker, men ét bestemt program/én bestemt adresse konsekvent ikke opdateres, mens andre gør: se [§13.4](#13-4-modbus-master-holder-op-med-at-opdatere-en-bestemt-adresse).

**Generel fremgangsmåde til at skelne A fra B og alt andet:**
1. `show modbus-master` — er Status reelt `ENABLED`, og stiger `Total requests`/`Async requests`?
2. Åbn Modbus Aktivitetsloggen — kommer der *overhovedet* transaktioner igennem for den pågældende adresse, med kilde `st_logic`?
3. Prøv samme læsning manuelt: `mb read holding <slave> <adresse>` — virker det isoleret fra CLI, men ikke fra ST-programmet, er fejlen i selve ST-kaldet (forkert slave-ID/adresse i koden), ikke i Modbus-laget.

## 13.4 "Modbus Master holder op med at opdatere en bestemt adresse"

**Symptom:** Generel Modbus Master-kommunikation virker fint, men én bestemt (slave, adresse)-kombination holder pludselig op med at blive opdateret, mens resten fortsætter normalt.

**Tjek `Priority drops` i `show modbus-master`.** Er den > 0, har systemets prioritetskø måttet fortrænge en ventende forespørgsel for at give plads til en vigtigere (skrivninger går altid foran læsninger). Fra og med v7.9.8.5 rydder systemet automatisk op efter denne situation (både øjeblikkeligt ved selve fortrængningen, og som sikkerhedsnet via en periodisk oprydning af forespørgsler der har hængt unormalt længe) — er I på en ældre firmware, opdatér.

**Midlertidig afhjælpning uden opdatering:** reducér belastningen på køen — sæt `cache-ttl` til en værdi forskellig fra 0 (`set modbus-master cache-ttl 5000`), eller reducér antallet af samtidige `MB_*`-kald pr. scan-cyklus (`set modbus-master max-requests`).

## 13.5 "Enheden svarer slet ikke"

1. **Fysisk:** lyser en status-LED (hvis boardet har én, se [§2.5](02_Hardware_og_Moduler.md#25-status-led-og-fysiske-indikatorer))? Er der strøm?
2. **Netværk:** `ping <enhedens-ip>` — svarer den slet ikke, er den enten offline, eller IP-adressen er forkert (tjek DHCP-leasetabel, eller genopfrisk kendskabet til fallback-IP i [§3.4](03_Installation_og_Foerste_Opstart.md)).
3. **Seriel som sidste udvej:** USB-konsollen kræver ikke netværk — tilslut direkte og kør `show status`/`show wifi` for at diagnosticere netværkslaget indefra.
4. **Watchdog:** systemet har en indbygget watchdog (30 sekunders timeout) der automatisk genstarter enheden hvis hovedløkken låser sig fast. En enhed der reelt "hænger" burde derfor sjældent forblive utilgængelig i lang tid — vedvarende utilgængelighed peger typisk på et netværks- eller strømproblem, ikke et software-hang.

## 13.6 Telnet/CLI svarer ikke, selvom netværket virker

Telnet understøtter kun **én samtidig forbindelse**. Er en tidligere session ikke lukket ordentligt (fx en klient der crashede uden at sende disconnect), kan porten fremstå optaget. Brug web-CLI (`/cli`) som alternativ adgang, eller genstart enheden hvis ingen anden adgang er mulig.

## 13.7 Hvor finder jeg mere?

| Ressource | Indhold |
|-----------|---------|
| [`../../BUGS_INDEX.md`](../../BUGS_INDEX.md) | Komplet historik over kendte og rettede fejl — søg efter symptomer eller BUG-ID |
| [`../../SECURITY_INDEX.md`](../../SECURITY_INDEX.md) | Kendte, endnu-åbne sikkerhedspunkter |
| [`../../MODBUS_REGISTER_MAP.md`](../../MODBUS_REGISTER_MAP.md) | Præcis register-for-register-reference |
| GitHub Issues (projektets repo) | Rapportér nye fejl — vedlæg altid `show version`-output og relevante `show`-kommandoers output |

---

[← 12. Netværkskonfiguration](12_Netvaerkskonfiguration.md) · [Indeks](00_INDEKS.md) · Næste: [Appendiks A: CLI-reference →](A_CLI_Kommando_Reference.md)
