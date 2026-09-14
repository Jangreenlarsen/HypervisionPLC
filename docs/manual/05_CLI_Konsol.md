# 5. CLI / Konsol

[← 4. Web Dashboard](04_Web_Dashboard_og_Monitor.md) · [Indeks](00_INDEKS.md) · Næste: [6. Modbus-interface →](06_Modbus_Interface.md)

---

## 5.1 Tre adgangsveje, ét kommandosæt

CLI'en er identisk uanset hvordan man tilgår den:

| Adgang | Kræver | Kryptering | Egnet til |
|--------|--------|------------|-----------|
| **Seriel (USB)** | Fysisk adgang | N/A (fysisk kabel) | Første opsætning, ES32D26-boot-arbitrering (§3.3) |
| **Telnet** (port 23) | Netværksadgang + login | Ingen (klartekst) | Hurtig fjernadgang, scripting |
| **Web-CLI** (`/cli`) | Netværksadgang + login | Ja, hvis HTTPS aktiveret | Fjernadgang der skal krypteres |

> Overvej at foretrække web-CLI over telnet i produktion, hvis netværket ikke er tillidsfuldt — se [kapitel 10](10_Sikkerhed_og_Adgangsstyring.md).

## 5.2 Kommandomønster

To hovedfamilier af kommandoer, plus enkeltstående kommandoer:

```
show <emne> [parametre]     — vis status/konfiguration (skrivebeskyttet)
set <emne> <parameter> <værdi>  — ændr konfiguration
```

**Aliaser:** `show` kan forkortes `sh` eller `s`. Fx `sh modbus`, `s wifi` — alt virker identisk med `show modbus`/`show wifi`.

**Indbygget hjælp:** langt de fleste kommandogrupper har en `?`-variant, fx:
```
set wifi ?
set http ?
show ?
mb ?
```

Kommandoer er **ikke versalfølsomme** — `SET WIFI DHCP ON` og `set wifi dhcp on` er ækvivalente.

## 5.3 Eksempler på almindelige opgaver

```
show status                    Runtime-status: oppetid, heap, GPIO-tilstand
show config                    Fuld persisteret konfiguration
show config wifi               ...filtreret til kun Wi-Fi-sektionen
show modbus                    Modbus Slave + Master, samlet
show telnet                    Telnet-serverens status
show logic                     Alle 4 ST-programmers status
show logic 1 code              Kildekode for program 1
show registers 100 10          Holding-registre 100-109
show version                   Firmwareversion, build, target-hardware
```

```
set modbus-master enabled on
set logic 1 enabled on
set wifi ssid MitNetvaerk
```

## 5.4 `mb` — Modbus Master fra kommandolinjen

En separat, direkte kommandofamilie til at afprøve Modbus Master-kommunikation manuelt, uden at skrive et ST-program:

```
mb read holding 1 0            Læs holding-register 0 fra slave-ID 1
mb read holding 1 0 4          ...4 registre fra adresse 0
mb write holding 1 0 1234      Skriv værdien 1234 til register 0
mb write coil 1 0 on
mb scan 1 20                   Scan slave-ID 1-20 for svar
mb reset stats                 Nulstil statistiktællere
```

Disse kommandoer går gennem samme asynkrone kø/cache som ST Logic's Modbus-kald, og optræder derfor også i Modbus Aktivitetsloggen ([§4.2](04_Web_Dashboard_og_Monitor.md#4-2-dashboard--layout-og-faner)) med kilde `cli`.

Findes der et Modbus Expansion Board på netværket ([§6.7](06_Modbus_Interface.md#67-modbus-expansion-boards-feat-409)), er der en tilsvarende `mbx <board> <kanal> read|write ...`-kommandofamilie til at afprøve DETS kanaler manuelt — se [Appendiks A](A_CLI_Kommando_Reference.md#modbus-expansion-board-feat-409).

## 5.5 Fuld kommandoreference

Se [**Appendiks A: CLI-kommandoreference**](A_CLI_Kommando_Reference.md) for en komplet, systematisk liste over alle `show`-, `set`- og `mb`-kommandoer med syntaks og beskrivelse.

---

[← 4. Web Dashboard](04_Web_Dashboard_og_Monitor.md) · [Indeks](00_INDEKS.md) · Næste: [6. Modbus-interface →](06_Modbus_Interface.md)
