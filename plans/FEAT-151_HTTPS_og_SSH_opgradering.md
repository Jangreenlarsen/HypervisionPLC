# FEAT-151: Opgradering til HTTPS på web-frontend + SSH på konsol

**Status:** Undersøgelse (ikke implementeret)
**Dato:** 2026-09-02
**Version undersøgt:** v7.9.8.3 (build #1996), ES32D26 / ESP32-WROVER, Arduino-ESP32 2.0.17 (ESP-IDF 4.4.x)

---

## Kort konklusion

| Mål | Vurdering | Hvorfor |
|-----|-----------|---------|
| **HTTPS på web-frontend** | ✅ **Realistisk nu** — koden findes allerede | `esp_https_server` er allerede linket ind i binæren; det koster ~0 ekstra flash at slå til. Kræver 3 fixes først (se blokkere). |
| **Telnet → ægte SSH** | ⚠️ **Svært / frarådes lige nu** | Ingen SSH i ESP-IDF. wolfSSH kræver 2. krypto-stack + ESP-IDF-komponent i et Arduino-build, og der er kun **185 KB fri flash**. |
| **Alternativ til SSH** | ✅ **Anbefales** | Web CLI (`/cli`) over HTTPS giver krypteret, autentificeret konsol med **nul ny kode** — sluk derefter telnet helt. |

---

## Del 1: HTTPS på web-frontend

### Nuværende tilstand — bedre end forventet

HTTPS er **allerede fuldt implementeret**, bare slået fra:

- `src/https_wrapper.c` bruger ESP-IDF's officielle `esp_https_server` (`httpd_ssl_start()`) — ikke hjemmestrikket TLS.
- Certifikater embeddes ved build via `board_build.embed_txtfiles` (`certs/servercert.pem` + `certs/prvtkey.pem`).
- Styres af runtime-flaget `g_persist_config.network.http.tls_enabled` (default 0).
- `src/http_server.cpp:869` vælger HTTPS- eller HTTP-sti ved opstart.

**Vigtigst:** `libesp_https_server.a` er **allerede linket ind** i den nuværende binær (verificeret i `firmware.map`), fordi kaldet til `httpd_ssl_start()` sker bag en runtime-`if`, ikke en `#ifdef`. Linkeren kan derfor ikke strippe det væk.

➡️ **At slå TLS til koster stort set ingen ekstra flash.** Det er afgørende, da der kun er 185 KB fri (90.0% brugt).

### Blokkere der SKAL rettes før TLS slås til

#### 🔴 1. `max_uri_handlers` er for lav i HTTPS-stien (kritisk)

```c
// http_server.cpp:874 — HTTPS-stien
https_wrapper_start(&http_state.server, config->port,
                    64,       // ← max URI handlers
                    10240, prio, 1);

// http_server.cpp:887 — plain HTTP-stien
httpd_config.max_uri_handlers = 96;
```

Der registreres **92 URI-handlers**. I HTTPS-mode ville de sidste ~28 fejle med `ESP_ERR_HTTPD_HANDLERS_FULL` — og returværdien fra `httpd_register_uri_handler()` tjekkes **ikke** nogen steder, så det sker helt tavst. Cirka en tredjedel af REST-API'et og web-siderne ville forsvinde uden fejlbesked.

**Fix:** hæv til mindst 96 (helst 112 for hovedrum), og tilføj return-check + tælling, så en fuld handler-tabel logges tydeligt.

#### 🟡 2. SSE-serveren får aldrig TLS

`src/sse_events.cpp` kører en selvstændig rå TCP-server på egen port (`sse_port`, default hovedport+1). Den er uafhængig af `tls_enabled`. Konsekvens ved HTTPS: dashboardet er krypteret, men dets live-datakanal er stadig klartekst — og SSE-engangstokenet sendes som `?token=` i URL'en. (Svarer til åbent punkt **#13** i `SECURITY_INDEX.md`.)

**Muligheder:**
- **A)** Flyt SSE ind på hoved-HTTPS-serveren (ESP-IDF httpd understøtter chunked/streaming responses) — pænest, men kræver omskrivning af SSE-transporten.
- **B)** Wrap SSE-socket i `esp_tls` — genbruger allerede-linket mbedTLS, men dobbelt TLS-kontekst koster heap.
- **C)** Deaktivér SSE automatisk når `tls_enabled=1`, og lad dashboardet falde tilbage til polling (som det allerede kan). Billigst, dårligst UX.

#### 🟡 3. Delt, selvsigneret certifikat og privatnøgle

Alle enheder flashet med samme build deler samme private nøgle (`certs/prvtkey.pem` embeddes i binæren). Én dumpet binær kompromitterer TLS på alle enheder. (Åbent punkt **#8** i `SECURITY_INDEX.md`.) Certifikatet er selvsigneret → browseradvarsel hver gang, hvilket træner brugere i at klikke advarsler væk.

**Muligheder (stigende ambition):**
- **A)** Generér selvsigneret cert **per enhed ved første boot** (mbedTLS kan generere ECDSA P-256 on-device) og gem i NVS. Løser nøgledeling; browseradvarsel består.
- **B)** Upload cert+nøgle via autentificeret API/CLI, så kunden kan lægge sit eget ind.
- **C)** Intern CA: udsted per-enhed-certifikater, rul CA-roden ud til klienterne. Løser også browseradvarslen. Mest arbejde.

### Øvrige ting at teste (ikke blokkere)

- **Heap:** TLS-handshake koster typisk 30-45 KB heap pr. forbindelse; HTTPS-stien har allerede `max_open_sockets = 3`. WROVER'ens 4 MB PSRAM hjælper ikke automatisk — mbedTLS-buffere lander i intern heap medmindre `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC` aktiveres. **Mål det** før produktionssætning.
- **OTA over TLS:** OTA-upload (1.7 MB) gennem TLS presser heap markant mere end normale requests. Test specifikt.
- **Port-adfærd:** HTTPS erstatter HTTP på **samme** port — der er ingen redirect fra 80. Overvej at holde en minimal lytter på 80 der svarer 301 → `https://<ip>`, ellers får brugerne "connection reset" på den gamle URL.
- **Rækkefølge:** `auth_enabled` er nu default 1 (BUG-328). TLS + Basic Auth hører sammen — Basic Auth over klartekst-HTTP sender base64-credentials i hver request.

### Anbefalet implementeringsrækkefølge (HTTPS)

1. Ret `max_uri_handlers` 64 → 112 + tilføj return-check på registreringer. *(lille, isoleret)*
2. Vælg SSE-strategi (start evt. med **C** for at komme i gang, planlæg **A**).
3. Per-enhed cert-generering ved første boot + NVS-lagring.
4. Slå `tls_enabled` til på testenhed, mål heap under load + OTA.
5. Overvej 80→443 redirect.

---

## Del 2: Telnet → SSH på konsollen

### Hvorfor ægte SSH er dyrt her

ESP-IDF har **ingen** indbygget SSH-server. Den realistiske kandidat er **wolfSSH** (wolfSSL), som faktisk har officielle Espressif-eksempler med præcis dette formål — en SSH→UART-konsolbro på ESP32, understøttet på ESP-IDF 4.x og 5.x.

Men i netop dette projekt støder det på fire problemer:

| Problem | Detalje |
|---------|---------|
| **Framework-mismatch** | wolfSSH leveres som ESP-IDF-komponent. Dette projekt bygger på **Arduino-framework** (Arduino-ESP32 2.0.17). Kræver PlatformIO IDF+Arduino-hybrid eller vendoring af kilderne. |
| **Dobbelt krypto-stack** | wolfSSH kræver wolfCrypt/wolfSSL — oveni den mbedTLS der allerede er linket ind til HTTPS. To komplette krypto-biblioteker i samme binær. |
| **Flash** | Kun **185 KB fri**. wolfSSL+wolfSSH lander typisk i lav-hundredvis af KB. Meget sandsynligt at det ikke kan være der uden at skrue ned for features eller repartitionere (SPIFFS er allerede nede på 256 KB; OTA-partitionerne er 1.812 MB hver). |
| **Licens** | wolfSSL er GPLv2 eller kommerciel licens — relevant hvis firmwaren distribueres til kunder. |

### Billigere alternativer med samme sikkerhedsgevinst

#### ✅ Alternativ 1 (anbefalet): Web CLI over HTTPS — nul ny kode

Der findes **allerede** en browser-baseret konsol: `/cli` (side) + `/api/cli` (kommando-endpoint), begge bag auth. Slår man TLS til, er det en krypteret, autentificeret fjernkonsol med det samme. Derefter kan telnet slukkes helt — hvilket samtidig lukker to åbne sikkerhedspunkter:
- **#9** (telnet DoS: én uautentificeret TCP-forbindelse blokerer al admin-adgang)
- **#10** (stack buffer overflow i `set coil DYNAMIC`-parsing, kun nåelig via CLI-skriveadgang)

#### 🔸 Alternativ 2: Telnet over TLS ("telnets", port 992)

Wrap den eksisterende `tcp_server`/`telnet_server`-socket i `esp_tls` — genbruger den allerede linkede mbedTLS, koster formentlig 10-20 KB flash. Ulempe: **klientsupport er dårlig** — PuTTY taler det ikke direkte, man skal bruge `stunnel`/`socat` som mellemled. Fint til scriptet adgang, akavet for mennesker.

#### 🔸 Alternativ 3: Kun seriel + HTTPS web CLI

Simpleste hærdning: sluk netværkskonsollen helt, brug seriel til lokal privilegeret adgang og web CLI til fjernadgang. Fjerner angrebsfladen frem for at kryptere den.

### Hvis ægte SSH alligevel er et krav

Så bør det behandles som et selvstændigt projekt, ikke en tilføjelse:
1. Frigør flash først (drop `ETHERNET_W5500_ENABLED` hvis ikke brugt, skær SPIFFS ned, evt. 8 MB flash-modul).
2. Skift til PlatformIO IDF+Arduino-hybrid build.
3. Integrér wolfSSH + wolfSSL, konfigurér wolfCrypt minimalt (kun ECDSA/AES-GCM/SHA-256, ingen RSA/SFTP/SCP).
4. Host-nøglegenerering ved første boot + NVS-lagring.
5. Bro SSH-kanal til den eksisterende `console`-abstraktion (samme lag som `console_telnet.cpp`).
6. Afklar licens.

**Realistisk estimat:** dage-til-uger, ikke timer — og med reel risiko for at det ikke kan være i flash.

---

## Samlet anbefaling

1. **Gør HTTPS-fixene nu** (max_uri_handlers, SSE-strategi, per-enhed cert) og slå TLS til. Lav indsats, stor gevinst, ingen flash-omkostning.
2. **Retirér telnet** til fordel for web CLI over HTTPS. Lukker samtidig SECURITY_INDEX #9 og #10.
3. **Udskyd ægte SSH** indtil der enten er et hårdt krav eller mere flash til rådighed.

---

## Referencer

- [wolfSSH ESP32 SSH-server eksempel (wolfSSL/wolfssh-examples)](https://github.com/wolfSSL/wolfssh-examples/blob/main/Espressif/ESP32/ESP32-SSH-Server/README.md)
- [wolfSSL Espressif-support](https://www.wolfssl.com/docs/espressif/)
- [wolfSSH produktside (licens/features)](https://www.wolfssl.com/products/wolfssh/)
- [wolfSSL: PSRAM til heap-operationer på ESP32](https://www.wolfssl.com/utilizing-psram-for-wolfssl-heap-operations-for-the-espressif-esp32/)
- Interne: `SECURITY_INDEX.md` (#8 delt cert, #9 telnet DoS, #10 CLI overflow, #13 SSE uden TLS)
