# 12. Netværkskonfiguration

[← 11. Backup/Restore & Firmware](11_Backup_Restore_og_Firmware.md) · [Indeks](00_INDEKS.md) · Næste: [13. Fejlfinding →](13_Fejlfinding.md)

---

## 12.1 Wi-Fi og Ethernet — uafhængige af hinanden

Systemet kan have **Wi-Fi og kablet Ethernet aktiveret samtidig** — de konfigureres og fungerer uafhængigt af hinanden, hver med egen IP-konfiguration. Praktisk til at have en fast, pålidelig kablet forbindelse til produktionssystemet og Wi-Fi som sekundær adgang til fejlsøgning, eller omvendt.

Grundlæggende Wi-Fi-opsætning er dækket i [§3.4](03_Installation_og_Foerste_Opstart.md#34-netværkskonfiguration-fra-seriel-konsol). Dette kapitel dækker Ethernet og NTP.

## 12.2 Ethernet (W5500)

Kræver enten et eksternt W5500-SPI-modul (standard ESP32-varianter) eller er indbygget (Waveshare S3-ETH) — se [kapitel 2](02_Hardware_og_Moduler.md). Firmwaren skal desuden være bygget med `-DETHERNET_W5500_ENABLED` (standard i de fleste build-profiler).

```
set ethernet enable
set ethernet dhcp on
show ethernet
```

**Statisk IP i stedet for DHCP:**
```
set ethernet dhcp off
set ethernet ip 192.168.1.60
set ethernet gateway 192.168.1.1
set ethernet netmask 255.255.255.0
```

Standard fallback-IP hvis DHCP fejler: **192.168.1.101** (se [§3.4](03_Installation_og_Foerste_Opstart.md)).

## 12.3 NTP-tidssynkronisering

Bruges til at give systemet en korrekt reel dato/tid (ellers vises kun oppetid siden boot) — relevant for tidsstempler i alarmhistorik og logfiler.

```
set ntp enable
set ntp server pool.ntp.org
set ntp timezone "CET-1CEST,M3.5.0,M10.5.0/3"
show ntp
```

Standard er **deaktiveret** ved fabriksnulstilling, med `pool.ntp.org` og dansk/central-europæisk tidszone (inkl. automatisk sommertid) som forudfyldte værdier. Tidszonen angives i POSIX TZ-format — se [`../NTP_TIMEZONE_GUIDE.md`](../NTP_TIMEZONE_GUIDE.md) for eksempler på andre tidszoner.

## 12.4 Diagnosticér netværksproblemer

```
show status         (Wi-Fi/Ethernet-status, IP-adresser, samlet overblik)
show wifi            (detaljeret Wi-Fi-status, signalstyrke, genforbindelser)
show ethernet         (detaljeret Ethernet-status, link-status)
```

Dashboardets **Netværk**-kort ([§4.2](04_Web_Dashboard_og_Monitor.md)) viser samme information visuelt, inkl. antal Wi-Fi-genforbindelser — en stigende tæller her indikerer et ustabilt trådløst signal.

## 12.5 Konfiguration via web-GUI (FEAT-166)

Alt det ovenstående — Wi-Fi (SSID/adgangskode/DHCP/statisk IP/power-save), Ethernet (DHCP/statisk IP/hostname) og NTP (server/tidszone/sync-interval) — kan nu også indstilles direkte fra `/system`-siden, uden CLI. Enhedens eget hostname (mDNS) har sit eget felt der (adskilt fra Ethernet-modulets DHCP-hostname). Ændringer af IP/DHCP/Wi-Fi-credentials anvendes i RAM med det samme, men kræver "Save" + genstart for at overleve en reboot — samme betingelse som CLI'ens `save`-kommando. NTP-ændringer (server/tidszone/interval) anvendes derimod straks uden reboot.

---

[← 11. Backup/Restore & Firmware](11_Backup_Restore_og_Firmware.md) · [Indeks](00_INDEKS.md) · Næste: [13. Fejlfinding →](13_Fejlfinding.md)
