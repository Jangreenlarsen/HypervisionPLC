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

Disse kommandoer går gennem samme asynkrone kø/cache som ST Logic's Modbus-kald, og optræder derfor også i Modbus Aktivitetsloggen ([§4.2](04_Web_Dashboard_og_Monitor.md#42-dashboard--layout-og-faner)) med kilde `cli`.

### Live-debug af Modbus-trafikken (`debug modbus`)

Samme funktion som på Expansion Boardet: al Modbus Master-trafik på RS485-bussen — fra ST Logic, dashboardet og `mb`-kommandoerne — vises live i konsollen, én linje pr. trin med tidsstempel (oppetid `D:HH:MM:SS.mmm`):

```
debug modbus level 1           Slå til (1-8, højere = flere detaljer)
no debug modbus                Slå fra
```

```
DEBUG [0:00:12:03.410] mb_master >TX> start: FC: 03, Slave: 15, Len: 8, Kilde: ST
DEBUG [0:00:12:03.410] mb_master >TX> decode: ID: 0F (15), FC03 Read Holding Registers, Addr: 0, Qty: 1, CRC: 84 E4
DEBUG [0:00:12:03.431] mb_master <RX< decode: ID: 0F (15), FC03 Read Holding Registers, Bytes: 2, Regs: 244, CRC: ..., Status: MB_OK
DEBUG [0:00:12:03.431] mb_master <RX< result: MB_OK (modtog 7 byte(s), 21ms)
```

| Level | Viser |
|---|---|
| 1 | start, decode af request/svar, resultat (inkl. TIMEOUT/CRC/EXCEPTION) |
| 2 | + støj-bytes tømt fra bussen før afsendelse |
| 3 | + DE/RE-retningsskift |
| 4–5 | + ventetid pr. modtaget byte |
| 6 | + CRC-tjek (modtaget/beregnet) |
| 7 | + rå TX-hex |
| 8 | + rå RX-hex |

- **Kun via Telnet.** På ES32D26 deler USB-konsollen forbindelse (GPIO1/3) med RS485, så kommandoen afvises fra seriel-konsollen. Web-CLI'en kan ikke vise live output og afviser også.
- Output går til den Telnet-session, der slog debug til. Debug slås automatisk fra, når sessionen lukkes, og efter reboot (gemmes ikke).
- Påvirker ikke bussens timing: trafikken opsamles som rå bytes og formateres først i hovedløkken. Ved meget trafik på høje levels kan linjer blive sprunget over (vises som `dropped`).
- `show debug` viser om det er slået til. Kræver skriverettighed (RBAC).

Findes der et Modbus Expansion Board på netværket ([§6.7](06_Modbus_Interface.md#67-modbus-expansion-boards-feat-409)), er der en tilsvarende `mbx <board> <kanal> read|write ...`-kommandofamilie til at afprøve DETS kanaler manuelt — se [Appendiks A](A_CLI_Kommando_Reference.md#modbus-expansion-board-feat-409).

## 5.5 Fuld kommandoreference

Se [**Appendiks A: CLI-kommandoreference**](A_CLI_Kommando_Reference.md) for en komplet, systematisk liste over alle `show`-, `set`- og `mb`-kommandoer med syntaks og beskrivelse.

---

[← 4. Web Dashboard](04_Web_Dashboard_og_Monitor.md) · [Indeks](00_INDEKS.md) · Næste: [6. Modbus-interface →](06_Modbus_Interface.md)
