# Counter Compare Feature - Quick Start Guide

<!-- DOC-STATUS: historisk -->
> ⚠️ **Historisk dokument — vedligeholdes ikke længere.** Skrevet til en ældre firmwareversion og kan være forældet (CLI-syntaks, pins, endpoints, standardværdier). Den aktuelle, vedligeholdte dokumentation er [brugermanualen](manual/00_INDEKS.md) (CLI: appendiks A, REST: appendiks B, ST: appendiks D). Markeret ved dokumentationsgennemgang 2026-09-28.


**Version:** 2.3+
**Status:** Fully implemented (Phase 1-3 complete)

## Hvad Er Counter Compare?

Counter Compare tillader dig at **automatisk opdage når en tæller når en bestemt værdi** og signalere det via et status-bit i Modbus input-registre.

### Praktisk Eksempel:

Du har en tæller der måler `impulser fra en sensor`. Du vil gerne vide når den når `1000 impulser`.

**Uden compare:** Du skal læse tælleren hele tiden og selv tjekke hvis den er >= 1000.

**Med compare:** Systemet tjekker automatisk hver løbetid, og når 1000 nås → sætter et status-bit til `1` i et Modbus input-register. Du læser registeret, og når du gør → bit ryddes automatisk til næste detektion.

---

## Quick Start: 5 Minutters Opsætning

### Step 1: Aktivér Counter med Compare

```bash
set counter 1 \
  enabled:1 \
  mode:1 \
  hw-mode:sw \
  index-reg:0 \
  raw-reg:1 \
  freq-reg:2 \
  overload-reg:3 \
  ctrl-reg:4 \
  prescaler:1 \
  compare:on \
  compare-value:1000 \
  compare-mode:0 \
  compare-status-reg:100 \
  compare-bit:5 \
  reset-on-read:on
```

### Step 2: Verificér Konfigurationen

```bash
show counter 1
```

Du skal se:

```
=== COUNTER COMPARE FEATURE ===
Counter 1:
  Mode: ≥ (greater-or-equal)
  Compare Value: 1000
  Status Register: input_reg[100], Bit 5
  Reset-on-Read: ENABLED
  Current Status Bit: CLEAR (0)
===============================
```

### Step 3: Test Fra Modbus Master

1. **Læs input register 100 (FC04)** → Status bit 5 = 0 (ikke nået tærskel endnu)
2. **Når tæller når 1000** → Status bit 5 bliver 1 (automatisk af systemet)
3. **Læs input register 100 igen** → Du får 1, så bliver bit ryddet til 0 automatisk

---

## Samlet Kommando-Syntax

```bash
set counter <ID> compare:<on|off> compare-value:<threshold> compare-mode:<0|1|2> \
  compare-status-reg:<reg> compare-bit:<bit> reset-on-read:<on|off>
```

### Parametre:

| Parameter | Værdi | Beskrivelse |
|-----------|-------|-----------|
| `compare` | `on` / `off` | Aktivér/deaktivér compare-funktionen |
| `compare-value` | 0-18446744073709551615 | Tærskelsværdi (64-bit) |
| `compare-mode` | `0` / `1` / `2` | Se tabel nedenfor |
| `compare-status-reg` | 0-255 | Modbus input register for status-bit |
| `compare-bit` | 0-15 | Bit-position (0=LSB, 15=MSB) |
| `reset-on-read` | `on` / `off` | Auto-ryd bit når Modbus læser? |

### Compare-Modes:

| Mode | Betingelse | Brugskasus |
|------|-----------|-----------|
| `0` | `counter >= compare-value` | **Mest brugt** - Alert når værdi nået |
| `1` | `counter > compare-value` | Streng større-end (ikke større-eller-lig) |
| `2` | `counter === compare-value` (transition) | Exact match kun når værdi **krydses** |

---

## Praktiske Eksempler

### Eksempel 1: Måle Produkter på Bånd

**Konfiguration:**
```bash
set counter 1 \
  enabled:1 mode:1 hw-mode:sw \
  index-reg:0 raw-reg:1 freq-reg:2 overload-reg:3 ctrl-reg:4 \
  prescaler:1 \
  compare:on compare-value:100 compare-mode:0 \
  compare-status-reg:100 compare-bit:0 reset-on-read:on
```

**Resultat:**
- Hver gang 100 produkter tælles → input_reg[100] bit 0 = 1
- PLC/Master læser register 100 → får "1" og videre proces, bit ryddes automatisk

### Eksempel 2: Høj Frekvens Alert

**Konfiguration:**
```bash
set counter 2 \
  enabled:1 mode:1 hw-mode:pcnt \
  hw-gpio:19 \
  compare:on compare-value:5000 compare-mode:0 \
  compare-status-reg:101 compare-bit:3 reset-on-read:on
```

**Resultat:**
- Hvis frekvens bliver for høj (5000+ pulser) → input_reg[101] bit 3 blinker
- PLC kan detektere og reagere på anomali

### Eksempel 3: Eksakt Værdi Detektion

**Konfiguration:**
```bash
set counter 3 \
  enabled:1 mode:1 hw-mode:sw \
  compare:on compare-value:999 compare-mode:2 \
  compare-status-reg:102 compare-bit:7 reset-on-read:off
```

**Resultat:**
- Alert _kun_ når tæller krydser fra < 999 til >= 999
- Hvis du springer værdien over → ingen alert
- `reset-on-read:off` betyder bit **forbliver SET** til du manuelt resetter

---

## Fejlfinding

### Status-Bit Bliver Ikke SET

**Årsag 1:** Counter ikke aktiveret
```bash
show counter 1
# Se hvis "enabled: 0"
```
**Fix:** `set counter 1 enabled:1`

**Årsag 2:** Compare værdi er for høj
```bash
# Check aktuel tæller-værdi
show counter 1
# Se "Current Value"
```
**Fix:** Sæt `compare-value` tæt på aktuel værdi

**Årsag 3:** Status register ugyldigt
```bash
# Input registers skal være < 256
show counter 1 | grep "Status Register"
```
**Fix:** Brug register 0-255 for input registers

### Status-Bit Bliver Ikke Ryddet

**Årsag 1:** `reset-on-read` er `off`
```bash
show counter 1 | grep "Reset-on-Read"
```
**Fix:** `set counter 1 reset-on-read:on`

**Årsag 2:** Master læser fra forkert register
```bash
# Verificér which register hold status bit
show counter 1 | grep "Status Register"
```
**Fix:** Sørg for at læse FC04 fra den samme register

---

## Modbus Protokol Integration

### FC04 (Read Input Registers)

Når du læser input register (f.eks. register 100):

1. ESP32 sender dig register-værdi (med bit 5 = 1 hvis triggered)
2. ESP32 **automatisk** rydder bit 5 efter svar (hvis `reset-on-read:on`)
3. Du kan straks gentage læsning til næste trigger

**Eksempel med Modbus Frame:**
```
Request:  FC04 | Start: 100 | Qty: 1
Response: FC04 | Value: 0xFFDF (bit 5 = 0 efter reset)
```

---

## Lagring og Genstart

Counter Compare-indstillinger **gemmes automatisk** i NVS (non-volatile storage) når du udfører `set counter` kommando.

**Bekræftelse:**
```bash
save config
```

**Verificér efter genstart:**
```bash
show counter 1
# Alle compare-settings vil være intakte
```

---

## Næste Trin

- **Læs:** [COUNTER_COMPARE_REFERENCE.md](COUNTER_COMPARE_REFERENCE.md) for detaljeret teknisk dokumentation
- **Se:** [FEATURE_GUIDE.md](FEATURE_GUIDE.md) for andre tæller-funktioner
- **Test:** Brug CLI `show counter` til at verificere alle indstillinger

---

## Support & Fejlrapporter

Hvis du finder problemer:

1. Gem hele `show counter` output
2. Noter hvilken ESP32 hardware du bruger
3. Beskriv hvad du forventede vs. hvad der skete

Eksempel:
```bash
show counter 1
show counter 2
show config
```

Send output og beskrivelse til projekt-support.
