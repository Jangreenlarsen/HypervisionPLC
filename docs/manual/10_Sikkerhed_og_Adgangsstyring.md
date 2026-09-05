# 10. Sikkerhed & Adgangsstyring

[← 9. Tællere & Timere](09_Taellere_og_Timere.md) · [Indeks](00_INDEKS.md) · Næste: [11. Backup/Restore & Firmware →](11_Backup_Restore_og_Firmware.md)

---

> Dette kapitel dækker sikkerheds*funktioner* i systemet. For løbende status over kendte, endnu-uafhjulpne sikkerhedspunkter, se den separat vedligeholdte [`../../SECURITY_INDEX.md`](../../SECURITY_INDEX.md) — den bør konsulteres før enhver produktionsudrulning.

## 10.1 To adgangsmodeller

Systemet har to adgangs-modeller, valgt globalt:

- **Legacy-mode** (RBAC deaktiveret) — ét fælles brugernavn/adgangskode pr. grænseflade (HTTP, telnet). Simpelt, men ingen differentiering mellem brugere.
- **RBAC-mode** (Role-Based Access Control) — op til 8 navngivne brugerkonti, hver med sin egen kombination af **roller** og **privilegie**.

Aktivér RBAC:
```
set rbac enable
set user tekniker password <kodeord> roles api,cli,monitor privilege read/write
set user overvaagning password <kodeord> roles monitor privilege read
```

> **Fald-i-baglås-advarsel:** aktivér ikke RBAC før mindst én bruger med tilstrækkelige rettigheder er oprettet — CLI'en advarer om dette, men dobbelttjek altid før I logger ud af den session der aktiverer det.

**Brugerstyring via web-GUI (FEAT-166):** samme funktionalitet findes nu også på `/system`-siden under "Brugerstyring" — til/fra for RBAC, tabel over eksisterende brugere, samt en formular til at oprette/redigere/slette brugere (roller som afkrydsningsfelter, privilegium som dropdown). Adgangskode skal genindtastes ved enhver redigering, også hvis kun roller/privilegium ændres — samme betingelse som CLI'ens `set user`, der heller ikke tilbyder en "kun rediger roller"-mulighed. De nye REST-endpoints (`GET/POST /api/rbac`, `POST /api/rbac/users`, `DELETE /api/rbac/users/{navn}`) kræver skriverettighed, bevidst samme grænse som CLI'en allerede har (se [`../../SECURITY_INDEX.md`](../../SECURITY_INDEX.md) #18) — ikke en strengere model, blot samme adgang et andet sted fra.

## 10.2 Roller og privilegier

| Rolle | Giver adgang til |
|-------|-------------------|
| `api` | REST API-endpoints (`/api/*`) |
| `cli` | CLI-kommandoer (seriel, telnet, web-CLI) |
| `editor` | ST Logic-editoren (`/editor`) |
| `monitor` | Dashboard/monitor (`/`) |

| Privilegie | Betydning |
|------------|-----------|
| `read` | Kan se/hente data (GET, `show`-kommandoer) |
| `write` | Kan ændre noget (POST/DELETE, `set`-kommandoer) |
| `read/write` (`rw`) | Begge dele |

En bruger med kun `monitor`+`read` kan altså se dashboardet, men hverken skrive konfiguration eller bruge CLI'en.

## 10.3 Standard-credentials — SKAL ændres

| Grænseflade | Standard-bruger | Standard-kodeord |
|-------------|------------------|---------------------|
| HTTP/dashboard | `admin` | `modbus123` |
| Telnet | `admin` | `telnet123` |

Disse er fabriksstandarder, dokumenteret i selve kildekoden — enhver med adgang til firmwaren (eller til denne manual) kender dem. **Skift dem ved installation**, se [§3.6](03_Installation_og_Foerste_Opstart.md#haerdning-efter-installation) — eller nu også via `/system`-sidens "HTTP Legacy Auth"- og "Telnet"-kort (FEAT-166), som alternativ til CLI'ens `set http password`/`set telnet password`.

**Password-lagring (BUG-352, fra v7.9.10.9):** HTTP/dashboard- og RBAC-brugerpasswords gemmes **hashet** (SHA-256 + et unikt, tilfældigt 16-byte salt pr. konto) — ikke længere i klartekst i NVS. En NVS-backup-eksport eller fysisk flash-dump afslører derfor ikke længere passwords direkte. **Undtagelser, bevidst uændrede:** WiFi-passwordet skal forblive klartekst (kræves af WPA2-håndtrykket mod radioen) og Telnet-passwordet er et separat, selvstændigt credential-system der endnu ikke er omfattet — begge er stadig synlige i en backup-eksport. Ændringen er transparent for eksisterende brugere: et allerede sat password bliver automatisk hashet ved første opstart efter opdateringen, uden at skulle sættes igen.

**Session-tokens (BUG-353, fra v7.9.10.10):** Web-UI'et (dashboard, editor, system, web-CLI, OTA-siden) gensender ikke længere brugernavn/kodeord på hvert request. Login (`POST /api/login`) verificerer én gang og udsteder et kortlivet token, som browseren derefter bruger (`Authorization: Bearer <token>`) — med et **glidende 30-minutters inaktivitets-timeout** (en aktiv fane logges ikke ud, en glemt fane gør efter 30 min uden aktivitet). Log ud (`POST /api/logout`) invaliderer token'et med det samme på enheden. **Almindelig HTTP Basic Auth virker fortsat uændret og for evigt** — dette er en tilføjelse, ikke en erstatning: scripts, curl og Node-RED-integrationer der taler direkte med REST API'et behøver ikke ændres.

## 10.4 Transportkryptering (HTTPS/TLS)

Systemet understøtter HTTPS via ESP-IDF's indbyggede TLS-server:
```
set http tls on             (kræver reboot)
set http https-port 443     (valgfrit — 443 er default)
```
Eller i web-GUI'en: `/system` → kortet "HTTPS / TLS".

**Vigtigt (BUG-350, fra v7.9.10.8): HTTPS lytter på sin egen, dedikerede port — som standard 443, adskilt fra HTTP's port (som standard 80).** De to porte er uafhængige af hinanden, styret af hhv. `set http port` og `set http https-port`. Før v7.9.10.8 delte HTTPS samme portnummer som HTTP, hvilket betød at aktivering af TLS gjorde HTTP-porten om til en TLS-only-lytter uden varsel — enhver klient der stadig sendte almindelig `http://` mod den port (browser-bogmarker, Node-RED, åbne dashboard-faner) fik en `mbedtls_ssl_handshake returned -0x7900` ("bad ClientHello")-fejlflod, og `https://` uden eksplicit portnummer ramte slet ikke serveren (browsere antager port 443 for https-skemaet, ikke 80). **Efter aktivering skal du derfor besøge `https://<enhedens-ip>:443/`** (eller den port du selv har sat) — ikke bare skifte `http://` til `https://` på samme URL som før.

**Vigtige forbehold, læs før aktivering:**
- Alle enheder bygget fra samme firmware **deler samme selvsignerede certifikat og private nøgle** (embeddet ved kompilering) — det giver kryptering på ledningen, men *ikke* enheds-unik identitet. Kompromitteres én enheds firmware, er nøglen kendt for alle enheder på samme build.
- Browseren vil vise en sikkerhedsadvarsel (selvsigneret certifikat) ved hvert besøg, medmindre certifikatet eksplicit tilføjes som tillid i browseren/OS'et.
- **SSE-strømmen (real-time push til dashboardet) krypteres ikke af `http tls`** — den kører på sin egen, separate raw-TCP-forbindelse. Med TLS aktiveret er dashboardets sider krypteret, men live-datastrømmen er det ikke. Se [`../../SECURITY_INDEX.md`](../../SECURITY_INDEX.md) for status.

## 10.5 Telnet vs. Web-CLI

Telnet (port 23) er ukrypteret klartekst — credentials og kommandoer kan aflyttes af enhver med adgang til det samme netværkssegment. Hvis fjernadgang til CLI er nødvendig, og netværket ikke er fysisk isoleret/tillidsfuldt, **foretræk web-CLI'en over HTTPS** (`/cli`, se [§4.4](04_Web_Dashboard_og_Monitor.md#44-web-cli-cli)) i stedet.

Telnet har en indbygget brute-force-lockout (3 fejlede loginforsøg → 30 sekunders spærring), der er bevaret på tværs af reconnect fra og med v7.9.7.7.

## 10.6 Rate limiting

Alle REST API-kald er rate-limited pr. klient-IP (se [§7.3](07_REST_API.md#73-rate-limiting)) — beskytter mod utilsigtet eller ondsindet overbelastning af API'et, men er ikke i sig selv en erstatning for autentificering.

## 10.7 Hærdningstjekliste

Gennemgå denne liste før produktionsudrulning:

- [ ] Skift standard HTTP-credentials (`set http username`/`set http password`)
- [ ] Skift standard telnet-credentials (`set telnet user`/`set telnet pass`), eller deaktivér telnet helt (`set telnet disable`) hvis ikke nødvendigt
- [ ] Aktivér HTTP Basic Auth hvis den ikke allerede er slået til (`set http auth on`)
- [ ] Overvej RBAC med separate, navngivne konti frem for delt admin-login, hvis flere personer/systemer skal have adgang
- [ ] Overvej HTTPS (`set http tls on`) — med forbehold i [§10.4](#104-transportkryptering-httpstls) i baghovedet
- [ ] Gennemgå [`../../SECURITY_INDEX.md`](../../SECURITY_INDEX.md) for endnu-åbne punkter relevante for jeres installation (fx OTA-adgangskontrol, som pr. skrivende stund kun kræver gyldigt login, ikke skriverettighed)
- [ ] Begræns netværksadgang til enheden til det nødvendige (firewall/VLAN), særligt hvis telnet eller ukrypteret HTTP forbliver aktiveret
- [ ] Tag et konfigurationsbackup **efter** hærdning ([kapitel 11](11_Backup_Restore_og_Firmware.md)) — bemærk at backup-filen indeholder credentials i klartekst; opbevar den derfor sikkert

---

[← 9. Tællere & Timere](09_Taellere_og_Timere.md) · [Indeks](00_INDEKS.md) · Næste: [11. Backup/Restore & Firmware →](11_Backup_Restore_og_Firmware.md)
