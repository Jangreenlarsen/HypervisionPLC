# Tæller-compare — reference

**Gælder:** firmware v7.9.68.50+ · **Opdateret:** 2026-10-04 · Kom i gang: [COUNTER_COMPARE_QUICK_START.md](COUNTER_COMPARE_QUICK_START.md) · Tællere generelt: [manualens kapitel 9](manual/09_Taellere_og_Timere.md) (diagram i §9.1.1).

> Denne reference erstatter en ældre beskrivelse af et design med frit valgt statusregister/-bit i input-registrene (`compare-status-reg`, `compare-bit`, FC04). Det design findes ikke i firmwaren — status ligger i bit 4 i tællerens kontrolregister.

## 1. Opførsel

Compare kører for hver tæller i hovedløkken (`counter_engine_check_compare()`, `src/counter_engine.cpp`):

1. **Tærsklen** læses fra compare-registrene (HR111-114 for tæller 1, antal ord efter `bit-width`, lavt ord først). `compare-value` i konfigurationen er kun startværdien for registrene — skriver en master en ny værdi, gælder den med det samme.
2. **Sammenligningsværdien** vælges med `compare-source`:

   | `compare-source` | Værdi | Svarer til |
   |---|---|---|
   | 0 | Tælleværdien i pulser | CLI "Raw Value" |
   | 1 | Tælleværdien ÷ prescaler (heltal) | HR104, `CNT_RAW` |
   | 2 | Tælleværdien × scale (afrundet) | HR100, `CNT_VALUE` |

3. **Krydsning:** bit 4 i kontrolregistret sættes kun, når værdien *krydser* tærsklen i forhold til sidste gennemløb:

   | `compare-mode` | Udløses når |
   |---|---|
   | 0 | forrige < tærskel **og** nu ≥ tærskel |
   | 1 | forrige ≤ tærskel **og** nu > tærskel |
   | 2 | som mode 0 (kaldes "præcis", men implementeret som krydsning til ≥ — en værdi der springer over tærsklen udløser også) |

4. **Bitten bliver stående**, til den slettes (afsnit 3). Den sættes ikke igen, før værdien har været under tærsklen og krydser på ny.

**Begrænsninger**

- **Kun opadgående krydsning.** Med `direction:down` udløses compare ikke.
- **Allerede over tærsklen:** slås compare til, mens værdien er over tærsklen, sker der intet før en ny krydsning.
- **Efter `reset counter`** starter krydsningsdetektionen fra `start-value`.
- Opløsningen er én hovedløkke (typisk 1-3 ms); compare ser værdien efter hver tælling.

## 2. Konfiguration

**CLI** — på samme linje som resten af tællerens opsætning (én `set counter … mode 1`-linje = hele konfigurationen):

```bash
set counter 1 mode 1 hw-mode:sw input-dis:7 edge:falling bit-width:32 compare-enabled:on compare-value:1000 compare-mode:0 compare-source:0 reset-on-read:on
set counter 1 control auto-start:on running:on
save
```

`compare`, `compare:enable` og `compare-enabled:on` er ens.

**REST** — `POST /api/counters/{id}` med `compare_enabled`, `compare_value`, `compare_mode`, `compare_source`, `reset_on_read` (se [appendiks B](manual/B_REST_API_Reference.md)). `GET` returnerer de samme felter.

**ST Logic** — `CNT_SETUP_CMP(id, cmp_mode, cmp_value, cmp_source, reset_on_read)` → BOOL (se [appendiks D](manual/D_ST_Logic_Funktionsreference.md)).

**Web** — I/O-siden, tællerens compare-felter.

## 3. Status og nulstilling af bit 4

| Tæller | Kontrolregister | Compare-værdi |
|---|---|---|
| 1 | HR110 | HR111-114 |
| 2 | HR130 | HR131-134 |
| 3 | HR150 | HR151-154 |
| 4 | HR170 | HR171-174 |

Bit 4 slettes på én af disse måder:

- **`reset-on-read:on`:** når en Modbus-master læser kontrolregistret med **FC03** (holding registers) — også hvis registret blot indgår i et større læseområde — eller når det læses med CLI'ens `read`-kommando. Læsning via REST eller dashboardet sletter den ikke.
- **Skriv** kontrolregistret med bit 4 = 0.
- `reset counter <id>` nulstiller tælleren; bitten skal stadig slettes på en af måderne ovenfor.

**Aflæsning**

- Modbus-master: FC03 på HR110 → bit 4.
- ST Logic: `BIT_TST(CNT_STATUS(1), 2)` — `CNT_STATUS` returnerer et omkodet bitfelt: bit 0 = kører, bit 1 = overflow, bit 2 = compare.
- REST: `GET /api/counters/{id}` → `compare_triggered`.

## 4. Samspil med prescaler, skala og overløb

- **Prescaler** påvirker kun compare med `compare-source:1`. Eksempel: `prescaler:100 compare-source:1 compare-value:10` udløser ved 1000 pulser.
- **Skala** påvirker kun compare med `compare-source:2`.
- **Overløb** (tælleren går forfra fra `start-value` ved `bit-width`-loftet) er ikke en krydsning opad; næste krydsning udløser igen.

## 5. Kontrolregistrets bits (samlet)

| Bit | Betydning | Adgang |
|---|---|---|
| 0 | Reset-kommando (slettes automatisk) | Skriv |
| 1 | Start-kommando (slettes automatisk) | Skriv |
| 2 | Stop-kommando (slettes automatisk) | Skriv |
| 3 | Overflow/underflow | Læs |
| 4 | **Compare-match** | Læs (slet ved FC03-læsning med `reset-on-read:on`, eller skriv 0) |
| 7 | Kører (vedvarende) | Læs/skriv |

> ⚠️ `control counter-reg-reset-on-read:on` (nulstil *tælleren* ved læsning af værdiregistrene) bruger også bit 0 og virker derfor ikke vedvarende — den nulstiller tælleren én gang (BUG-447, åben). Den har intet at gøre med compare-`reset-on-read`.

## 6. Fejlfinding

| Symptom | Årsag | Løsning |
|---|---|---|
| Bit 4 sættes aldrig | Tælleren kører ikke | `set counter 1 control running:on` |
| Bit 4 sættes aldrig | Værdien var allerede over tærsklen, eller der tælles ned | Nulstil tælleren / tæl op |
| Bit 4 sættes aldrig | Forkert `compare-source` ift. prescaler/skala | Se afsnit 4 |
| Bit 4 forsvinder straks | Masteren læser HR110 med FC03, og `reset-on-read:on` | Forventet; eller slå `reset-on-read` fra |
| Bit 4 forsvinder aldrig | `reset-on-read:off`, eller der læses via REST | Skriv bit 4 = 0 eller brug FC03 |
| Compare-indstillinger forsvinder | Sat på en separat `set counter … mode 1`-linje | Saml alt på én linje |
