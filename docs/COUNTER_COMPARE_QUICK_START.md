# Tæller-compare — kom hurtigt i gang

**Gælder:** firmware v7.9.68.50+ · **Opdateret:** 2026-10-04 · Fuld reference: [COUNTER_COMPARE_REFERENCE.md](COUNTER_COMPARE_REFERENCE.md) · Tællere generelt: [manualens kapitel 9](manual/09_Taellere_og_Timere.md).

## Hvad gør compare?

Tælleren holder selv øje med en tærskel. Når værdien **krydser** tærsklen (nedefra og op), sætter den **bit 4 i tællerens kontrolregister** (HR110 for tæller 1). En Modbus-master, et ST-program eller en DYNAMIC-coil kan så reagere på bitten — uden selv at sammenligne tal hele tiden.

## Opsætning (tæller 1, tærskel 1000)

Compare-nøglerne står på **samme linje** som resten af tællerens opsætning (én `set counter … mode 1`-linje = hele konfigurationen):

```bash
set counter 1 mode 1 hw-mode:sw input-dis:7 edge:falling bit-width:32 compare-enabled:on compare-value:1000 compare-mode:0 compare-source:0 reset-on-read:on
set counter 1 control auto-start:on running:on
save
show counter 1
```

| Nøgle | Værdier | Betydning |
|---|---|---|
| `compare-enabled` | `on`/`off` | Slå compare til |
| `compare-value` | tal | Tærsklen. Skrives også i HR111-114 og kan ændres dér løbende |
| `compare-mode` | 0, 1, 2 | 0 = krydser til ≥ tærsklen, 1 = krydser til > tærsklen, 2 = som 0 (se reference) |
| `compare-source` | 0, 1, 2 | Hvad der sammenlignes: 0 = tælleværdien (pulser), 1 = prescaled (HR104), 2 = scaled (HR100) |
| `reset-on-read` | `on`/`off` | Slet bit 4, når en Modbus-master læser kontrolregistret |

## Aflæs status

| Tæller | Kontrolregister (bit 4) | Tærskel |
|---|---|---|
| 1 | HR110 | HR111-114 |
| 2 | HR130 | HR131-134 |
| 3 | HR150 | HR151-154 |
| 4 | HR170 | HR171-174 |

- **Modbus-master:** læs HR110 med FC03. Bit 4 = 1 → tærsklen er krydset. Med `reset-on-read:on` slettes bitten ved selve læsningen.
- **Uden reset-on-read:** skriv bitten tilbage til 0 (fx `HR110 = HR110 AND NOT 16`). `reset counter` sletter *ikke* bit 4.
- **ST Logic:** `BIT_TST(CNT_STATUS(1), 2)` er TRUE efter et compare-hit (`CNT_STATUS` returnerer bit 0 = kører, bit 1 = overflow, bit 2 = compare).
- **Spejl til en coil:** ikke muligt direkte for bit 4 (DYNAMIC-coil understøtter `counter<id>:overflow`); brug et ST-program eller læs HR110.

## Eksempler

```bash
# Batch på 500 stk.: bit 4 ved 500, masteren nulstiller tælleren efter aflæsning (skriv 1 = reset i HR110)
set counter 1 mode 1 hw-mode:sw input-dis:7 edge:falling bit-width:32 compare-enabled:on compare-value:500 compare-mode:0 reset-on-read:on

# 10 liter fra en flowmåler med 100 pulser/l: sammenlign med den prescalerede værdi (HR104)
set counter 2 mode 1 hw-mode:sw input-dis:6 edge:falling bit-width:32 prescaler:100 compare-enabled:on compare-value:10 compare-source:1
```

## Faldgruber

- **Bitten sættes kun ved krydsning.** Står tælleren allerede over tærsklen, når compare slås til, sker der intet, før værdien har været under igen.
- **Nedtælling udløser ikke compare** (der tjekkes kun for krydsning opad).
- Del ikke opsætningen over flere `set counter … mode 1`-linjer — den sidste nulstiller de andre nøgler.
- `compare-value` i konfigurationen er startværdien for HR111-114; ændres registrene af en master, gælder registrenes værdi.
