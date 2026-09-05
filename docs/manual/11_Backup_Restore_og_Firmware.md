# 11. Backup, Restore & Firmware

[← 10. Sikkerhed](10_Sikkerhed_og_Adgangsstyring.md) · [Indeks](00_INDEKS.md) · Næste: [12. Netværkskonfiguration →](12_Netvaerkskonfiguration.md)

---

## 11.1 Konfigurationsbackup

Hele den persisterede konfiguration — netværk, Modbus-opsætning, RBAC-brugere, tæller-/timer-konfiguration, persist-grupper, ST Logic-programmer og bindings — kan eksporteres som én JSON-fil:

```bash
curl -u admin:modbus123 http://192.168.1.100/api/system/backup -o backup_2026-09-03.json
```

Eller fra CLI: `show backup` (viser URL'en, download foretages fra en browser/HTTP-klient).

> **Følsomt indhold:** backup-filen indeholder Wi-Fi-adgangskode, HTTP- og telnet-credentials samt alle RBAC-brugeres adgangskoder **i klartekst**. Opbevar backup-filer med samme forsigtighed som selve credentials — ikke i et delt, uautoriseret tilgængeligt sted. Endpointet kræver skriverettighed at hente (ikke blot læse-adgang), netop fordi indholdet er så følsomt.

## 11.2 Gendannelse (Restore)

```bash
curl -u admin:modbus123 -X POST http://192.168.1.100/api/system/restore \
     -H "Content-Type: application/json" \
     --data-binary @backup_2026-09-03.json
```

Restore overskriver den gemte konfiguration og genindlæser den. Systemet genstarter typisk selv (eller kræver en manuel genstart, afhængig af hvad der er ændret) for at alle ændringer slår fuldt igennem.

**Anbefalet rutine:** tag et backup **før** enhver større konfigurationsændring eller firmwareopdatering — restore er den hurtigste vej tilbage, hvis noget går galt.

## 11.3 OTA-firmwareopdatering

Firmware kan opdateres over netværket uden at skulle koble USB til igen:

1. Log ind på `/ota`-siden i webgrænsefladen (eller `POST /api/system/ota` direkte med `.bin`-filen som request-body)
2. Upload den nye `firmware.bin`
3. Enheden skriver til den inaktive OTA-partition og genstarter automatisk ind i den nye firmware
4. Går noget galt (fx firmwaren fejler ved boot), understøtter systemet **automatisk rollback** til den forrige, fungerende firmware (dual-bank OTA-partitionering)

**Størrelsesgrænse:** maks. ca. **1,8 MB** (1.900.544 bytes, svarende til én OTA-partition). Upload afvises med en tydelig fejlbesked hvis filen er for stor.

> **Adgangskontrol:** OTA-upload/rollback kræver eksplicit skriverettighed (rettet i BUG-355) — en bruger med kun læse-adgang kan ikke flashe firmware. **Stadig ingen kryptografisk firmware-signaturverifikation** — se [`../../SECURITY_INDEX.md`](../../SECURITY_INDEX.md) #2.

### 11.3a Opdatering direkte fra GitHub (FEAT-169)

Som alternativ til manuel `.bin`-upload kan `/system`-sidens OTA-kort hente og installere den nyeste offentliggjorte version direkte fra projektets GitHub Releases:

1. Klik **"Tjek for opdatering"** — enheden slår op mod `api.github.com` og viser nuværende vs. seneste udgivne version.
2. Er der en nyere version, vises en **"Installér"**-knap med filstørrelsen. Klik for at bekræfte, hente og flashe — samme automatiske rollback-beskyttelse som ved manuel upload gælder uændret.

**Bevidst kun manuelt** — enheden slår aldrig selv op i baggrunden uopfordret; det er en aktiv handling hver gang, samme filosofi som resten af sikkerhedsmodellen i dette kapitel. Kræver at enheden reelt har internetadgang (`api.github.com` + `objects.githubusercontent.com`) — irrelevant/virker ikke på en fuldstændig LAN-isoleret installation, hvor manuel upload forbliver vejen frem.

TLS-forbindelsen bruger bundlede, ægte rod-CA-certifikater (ikke en usikker/uverificeret forbindelse) — se [`../../SECURITY_INDEX.md`](../../SECURITY_INDEX.md) #19 for den fulde afvejning omkring cert-pinning. Se [`../RELEASE_PROCEDURE.md`](../RELEASE_PROCEDURE.md) for hvordan nye versioner bliver gjort tilgængelige som en GitHub Release i første omgang.

**Status og fremgang under upload:**
```bash
curl -u admin:modbus123 http://192.168.1.100/api/system/ota/status
```

## 11.4 Manuel rollback

Ønskes en tilbagerulning uden at afvente en fejlet boot:
```bash
curl -u admin:modbus123 -X POST http://192.168.1.100/api/system/ota/rollback
```

## 11.5 Firmwareversion

```
show version
```
viser aktuel firmwareversion, build-nummer, git-commit og target-hardware — inkludér altid dette når I rapporterer et problem eller stiller spørgsmål til supportkanalen.

---

[← 10. Sikkerhed](10_Sikkerhed_og_Adgangsstyring.md) · [Indeks](00_INDEKS.md) · Næste: [12. Netværkskonfiguration →](12_Netvaerkskonfiguration.md)
