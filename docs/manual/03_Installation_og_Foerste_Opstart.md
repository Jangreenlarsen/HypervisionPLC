# 3. Installation & Første Opstart

[← 2. Hardware & Moduler](02_Hardware_og_Moduler.md) · [Indeks](00_INDEKS.md) · Næste: [4. Web Dashboard →](04_Web_Dashboard_og_Monitor.md)

---

## 3.1 Oversigt over opsætningsforløbet

```
1. Tilslut strøm + USB (seriel konsol)
2. Log ind på konsollen (standard-credentials)
3. (Kun ES32D26 i Master-mode) Håndtér RS-485/USB-konsol-arbitrering
4. Konfigurér netværk (Wi-Fi og/eller Ethernet)
5. Find enhedens IP-adresse
6. Log ind på webdashboardet
7. HÆRD installationen — skift standard-adgangskoder (se §3.6)
```

## 3.2 Tilslutning via seriel konsol

Alle varianter har en USB-seriel-konsol tilgængelig med det samme efter tilslutning — ingen netværk nødvendigt for første kontakt.

- **Baudrate:** 115200, 8N1
- **Værktøj:** PlatformIO's `pio device monitor`, eller ethvert terminalprogram (PuTTY, Tera Term, `screen`/`minicom` på Linux/Mac)
- **Login:** Seriel konsol kræver som udgangspunkt intet login (fysisk adgang til enheden = tillid), medmindre RBAC er aktiveret globalt — se [kapitel 10](10_Sikkerhed_og_Adgangsstyring.md)

Ved boot printes en kort diagnostik-linje (bogstav-for-bogstav for hvert subsystem der initialiseres), efterfulgt af CLI-prompten `>`.

## 3.3 Opstart på ES32D26 — RS-485 vs. USB-konsol

Gælder kun ES32D26 (delt UART, se [§2.4](02_Hardware_og_Moduler.md#24-modbus-transceiver-delt-vs-dedikeret-uart)). Når enheden bootes i **Master**-mode, viser den:

```
>> RS485 Master mode: USB console vil blive overtaget.
>> Tryk MELLEMRUM 3 gange inden 5 sek for at forblive i USB console...
```

- **Gør ingenting:** efter 5 sekunder overtager RS-485 pinsne, og Modbus Master aktiveres. USB-konsollen er nu utilgængelig for kommandoer — brug **telnet** eller **web-CLI** i stedet (se [kapitel 5](05_CLI_Konsol.md)).
- **Tryk mellemrum 3 gange hurtigt** hvis I har brug for at blive på USB-konsollen (f.eks. for at ændre konfiguration før RS-485 aktiveres). Kravet om 3 tryk (i stedet for blot ét) er bevidst — ét enkelt mellemrums-tegn kan i sjældne tilfælde optræde i normal Modbus-bustrafik og ville ellers utilsigtet afbryde aktiveringen.

Hvis I ved en fejl afbryder aktiveringen, behøver I **ikke genstarte** for at rette det — kør `set modbus-master enabled on` fra USB-konsollen, og RS-485 aktiveres med det samme.

## 3.4 Netværkskonfiguration fra seriel konsol

Fabriksstandard: **DHCP aktiveret** på både Wi-Fi og Ethernet, intet Wi-Fi SSID sat. Enheden får altså ingen netværksforbindelse før I fortæller den hvilket Wi-Fi-netværk den skal bruge (eller sætter statisk IP), eller tilslutter kablet Ethernet med DHCP-server til stede.

**Opsæt Wi-Fi:**
```
set wifi ssid MitNetvaerk
set wifi password MitKodeord
set wifi dhcp on
```

**Eller statisk IP i stedet for DHCP:**
```
set wifi dhcp off
set wifi ip 192.168.1.50
set wifi gateway 192.168.1.1
set wifi netmask 255.255.255.0
set wifi dns 192.168.1.1
```

**Bekræft forbindelsen:**
```
show wifi
```

Se hele kommandosættet i [Appendiks A](A_CLI_Kommando_Reference.md#netv%C3%A6rk) eller kør `set wifi ?` / `set ethernet ?` for indbygget hjælp.

> Falder DHCP væk (ingen DHCP-server, kabel trukket ud), falder enheden automatisk tilbage til en fast standard-IP: **192.168.1.100** (Wi-Fi) / **192.168.1.101** (Ethernet), gateway 192.168.1.1 — nyttigt hvis I har brug for at nå enheden uden fungerende DHCP.

## 3.5 Første login på webdashboardet

Når enheden har en IP-adresse: åbn `http://<enhedens-ip>/` i en browser.

**Fabriksstandard-credentials:**

| Grænseflade | Brugernavn | Adgangskode |
|-------------|------------|-------------|
| HTTP/dashboard (Basic Auth) | `admin` | `modbus123` |
| Telnet | `admin` | `telnet123` |

> **Vigtigt:** som standard er HTTP Basic Auth **slået fra** (`auth_enabled=0` i ældre firmware), eller **slået til** fra og med v7.9.7.7 (se [kapitel 10](10_Sikkerhed_og_Adgangsstyring.md)). Tjek jeres firmwareversion og bekræft status med `show http`.

## 3.6 Hærdning efter installation

**Gør dette FØR enheden går i produktion — det tager to minutter:**

```
set http username <nyt-brugernavn>
set http password <nyt-stærkt-kodeord>
set telnet user <nyt-brugernavn>
set telnet pass <nyt-stærkt-kodeord>
```

Overvej desuden:
- Aktivér HTTP Basic Auth hvis den ikke allerede er slået til: `set http auth on`
- Aktivér HTTPS/TLS hvis I har brug for kryptering på ledningen: `set http tls on` (kræver reboot) — se [kapitel 10](10_Sikkerhed_og_Adgangsstyring.md) for forudsætninger
- Overvej at deaktivere telnet helt til fordel for web-CLI, hvis fjernadgang via netværk ikke er nødvendig: `set telnet disable`
- Gennemgå [`../../SECURITY_INDEX.md`](../../SECURITY_INDEX.md) for kendte, endnu-ikke-lukkede sikkerhedspunkter der er relevante for jeres installation (f.eks. OTA-adgangskontrol)

Se den fulde tjekliste i [kapitel 10](10_Sikkerhed_og_Adgangsstyring.md#haerdningstjekliste).

## 3.7 Konfigurér Modbus-rollerne

Afhængig af board og brug, sæt hvilken rolle systemet skal spille:

```
show modbus                       (viser nuværende Slave + Master konfiguration)
set modbus mode master            (kun relevant på ES32D26, delt UART)
set modbus-slave enabled on
set modbus-slave slave-id 1
set modbus-master enabled on
```

Se [kapitel 6](06_Modbus_Interface.md) for fuld gennemgang af Slave- og Master-konfiguration.

## 3.8 Gem konfigurationen

Konfigurationsændringer via `set`-kommandoer gemmes automatisk til NVS (non-volatile storage) og overlever genstart — der er intet separat "gem"-trin for netværks-/systemkonfiguration. (ST Logic-programmer skal derimod eksplicit kompileres/gemmes — se [kapitel 8](08_ST_Logic_Programmering.md).)

Tag et backup af konfigurationen når opsætningen er færdig — se [kapitel 11](11_Backup_Restore_og_Firmware.md#konfigurationsbackup).

---

[← 2. Hardware & Moduler](02_Hardware_og_Moduler.md) · [Indeks](00_INDEKS.md) · Næste: [4. Web Dashboard →](04_Web_Dashboard_og_Monitor.md)
