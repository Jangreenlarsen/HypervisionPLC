<div align="center">

<!-- Læg et logo her når det findes: docs/manual/assets/logo.png -->
<!-- ![Hypervision PLC](assets/logo.png) -->

# HYPERVISION PLC
### Brugermanual

*Letvægts, netværkstilkoblet PLC med Modbus RTU/TCP, ST Logic (IEC 61131-3) og REST API*

---

**Version:** v7.9.64.0 · **Platform:** ESP32-WROOM-32 / ESP32-WROVER / ESP32-S3

</div>

---

## Om denne manual

Hypervision PLC er en ESP32-baseret PLC (Programmable Logic Controller), der kombinerer klassisk Modbus RTU-kommunikation med et moderne netværkslag — REST API, webbaseret dashboard og fjernstyret ST Logic-programmering. Denne manual dækker installation, daglig drift, programmering og integration.

Manualen er skrevet som en **modulær samling af selvstændige kapitler**, så den er nem at vedligeholde og udvide efterhånden som systemet udvikler sig. Hvert kapitel kan læses for sig, men den anbefalede rækkefølge for en ny bruger er kapitel 1 → 5 i rækkefølge, derefter de øvrige kapitler efter behov.

> **Vedligeholdelse:** Denne manual dokumenterer funktionalitet — ikke bugs eller intern arkitektur. For kendte fejl og status, se [`../../BUGS_INDEX.md`](../../BUGS_INDEX.md). For sikkerhedsfund, se [`../../SECURITY_INDEX.md`](../../SECURITY_INDEX.md). For arkitektur/filreference (udviklerdokumentation), se [`../../CLAUDE_ARCH.md`](../../CLAUDE_ARCH.md). Når en funktion ændres i koden, opdatér det tilsvarende kapitel her i samme commit.

---

## Indholdsfortegnelse

### Kom i gang

| # | Kapitel | Indhold |
|---|---------|---------|
| 1 | [Systembeskrivelse](01_Systembeskrivelse.md) | Hvad er Hypervision PLC, hvilket problem løser den, målgruppe, nøglefunktioner |
| 2 | [Hardware & Moduler](02_Hardware_og_Moduler.md) | Board-varianter, IO (DI/DO/AI/AO), RS-485, Ethernet/Wi-Fi, pin-mapping |
| 3 | [Installation & Første Opstart](03_Installation_og_Foerste_Opstart.md) | Fra kasse til drift: seriel opsætning, IP-konfiguration, GUI-login, hærdning |
| 4 | [Web Dashboard & Monitor](04_Web_Dashboard_og_Monitor.md) | Rundvisning i webgrænsefladen: dashboard, editor, CLI, alarm- og aktivitetslog |
| 5 | [CLI / Konsol](05_CLI_Konsol.md) | Seriel, telnet og web-CLI — kommandomønster og eksempler |

### Kernefunktioner

| # | Kapitel | Indhold |
|---|---------|---------|
| 6 | [Modbus-interface](06_Modbus_Interface.md) | RTU Slave + Master, register-map, function codes, adressering, Modbus Expansion Boards (Modbus TCP) |
| 7 | [REST API](07_REST_API.md) | Autentificering, endpoints, integrationseksempler (curl/Python/Node-RED) |
| 8 | [ST Logic-programmering](08_ST_Logic_Programmering.md) | IEC 61131-3-sproget, VM'en, builtins, eksempelprogrammer, fjernovervågning via REST |
| 9 | [Tællere & Timere](09_Taellere_og_Timere.md) | Dedikerede hardware-tællere (SW/HW/ISR) og timer-funktionsblokke |

### Drift & sikkerhed

| # | Kapitel | Indhold |
|---|---------|---------|
| 10 | [Sikkerhed & Adgangsstyring](10_Sikkerhed_og_Adgangsstyring.md) | Brugere/roller (RBAC), autentificering, TLS/HTTPS, hærdningstjekliste |
| 11 | [Backup, Restore & Firmware](11_Backup_Restore_og_Firmware.md) | Konfigurationsbackup, gendannelse, OTA-firmwareopdatering |
| 12 | [Netværkskonfiguration](12_Netvaerkskonfiguration.md) | Wi-Fi, Ethernet (W5500), statisk IP/DHCP, NTP-tidssynkronisering |
| 13 | [Fejlfinding](13_Fejlfinding.md) | Diagnostiske kommandoer, typiske faldgruber, hvornår kontakt support |

### Referencer

| # | Appendiks | Indhold |
|---|-----------|---------|
| A | [CLI-kommandoreference](A_CLI_Kommando_Reference.md) | Komplet liste over alle `show`/`set`/`mb`-kommandoer |
| B | [REST API-reference](B_REST_API_Reference.md) | Komplet liste over alle endpoints, metoder og auth-krav |
| C | [Ordliste](C_Ordliste.md) | Forklaring af fagtermer (Modbus, IEC 61131-3, netværk) |
| D | [ST Logic Funktionsreference](D_ST_Logic_Funktionsreference.md) | Komplet reference: alle datatyper, kontrolstrukturer, operatorer og indbyggede funktioner i ST-sproget |

---

## Hurtig-links til de mest almindelige opgaver

- **Første opstart / glemt IP-adresse** → [Kapitel 3: Installation](03_Installation_og_Foerste_Opstart.md#tilslutning-via-seriel-konsol)
- **Skifte standard-adgangskoder** → [Kapitel 3: Hærdning efter installation](03_Installation_og_Foerste_Opstart.md#haerdning-efter-installation)
- **Skrive dit første ST Logic-program** → [Kapitel 8: Kom i gang](08_ST_Logic_Programmering.md#kom-i-gang-et-foerste-program)
- **Læse/skrive et register via REST API** → [Kapitel 7: Register- og coil-endpoints](07_REST_API.md#registre-og-coils)
- **Opsætte Modbus Master mod eksterne enheder** → [Kapitel 6: Master-rollen](06_Modbus_Interface.md#modbus-master)
- **Tilslutte og styre et Modbus Expansion Board (MBX_*)** → [Kapitel 6.7: Modbus Expansion Boards](06_Modbus_Interface.md#67-modbus-expansion-boards-feat-409)
- **Bruge en GPIO-indgang (inkl. de multiplexede skifteregister-kanaler) fra ST Logic** → [Kapitel 8: GPIO-bindinger](08_ST_Logic_Programmering.md#813-gpio-indgange-i-st-logic-bindings-mode)
- **Tillade/blokere adgang fra bestemte IP'er/subnet (IP ACL, permit/deny, kladde-tilstand)** → [Kapitel 10: IP Access Control List](10_Sikkerhed_og_Adgangsstyring.md#107-ip-access-control-list-feat-399401402)
- **Opdatere firmware (OTA)** → [Kapitel 11: OTA-opdatering](11_Backup_Restore_og_Firmware.md#ota-firmwareopdatering)
- **Enheden svarer ikke / mistænkelig opførsel** → [Kapitel 13: Fejlfinding](13_Fejlfinding.md)

---

**Sprog:** Manualen er skrevet på dansk, i tråd med resten af projektets dokumentation (se [`../../CLAUDE.md`](../../CLAUDE.md)). Kodeeksempler, kommandoer og feltnavne følger engelsk konvention, som i selve systemet.
