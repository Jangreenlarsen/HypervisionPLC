# Relay Board-4 — Modbus RTU 4-kanals relæmodul (RS485)

Opsummering af producentens manual: [`Relay Board-4 MODBUS RTU 4CH Relay Command.pdf`](Relay%20Board-4%20MODBUS%20RTU%204CH%20Relay%20Command.pdf). Den er hentet fra forhandleren Robu og hedder "MODBUS RTU 4CH RELAY 12V RS485 MODULE – 4 INPUT OPTOCOUPLER ISOLATION".

> **Status:** ikke testet mod PLC'en endnu. Afsnittet "Faldgruber" beskriver de ting, der skal afprøves først.

## Hardware

| | |
|---|---|
| Silketryk | "Relay Board-4" (set som V1.06 og V1.2) |
| MCU | STM8S103F3 (SWIM-programmeringsstik J13) |
| Relæer | 4 × Songle SRD-12VDC-SL-C, 10 A 250 VAC / 10 A 30 VDC, NO/COM/NC pr. kanal |
| Indgange | IN1–IN4, optoisolerede, terminal IN1-IN4 + GND. De kan **kun aflæses** via Modbus og styrer ikke selv relæerne |
| Forsyning | 12 V DC (jackstik eller skrueterminal VCC/GND) |
| Bus | RS485 **A+ / B−** (også TTL TX/RX på stiftrækken) |
| Øvrigt | Reset-knap S1, kørsels-LED D5, LED pr. relæ, strøm-LED |

## Kommunikation

**Modbus RTU, 9600 baud, 8N1. Standardadresse 1** (1–255, kan ændres med kommando).

| Handling | Ramme (hex, adresse 1) | Funktion |
|---|---|---|
| Relæ 1 til | `01 05 00 00 FF 00 8C 3A` | FC05, coil **0** (manualen siger coil 1 — forkert, se faldgrube 2) |
| Relæ 1 fra | `01 05 00 00 00 00 CD CA` | |
| Relæ 2/3/4 | coil `00 01` / `00 02` / `00 03` | samme mønster |
| Alle til | `01 05 00 FF FF FF FC 4A` | coil 0x00FF = alle |
| Alle fra | `01 05 00 FF 00 00 FD FA` | |
| Læs relæstatus | `01 01 00 00 00 04 3D C9` | FC01 fra coil 0, 4 stk. |
| Læs indgange | `01 02 00 00 00 00 78 0A` | FC02 (antal 0 i manualen) |
| Sæt adresse til 2 | `00 06 40 00 00 02 1C 1A` | FC06 broadcast (adr. 0) til register **0x4000** |
| Læs adresse | `00 03 40 00 00 01 90 1B` | FC03 broadcast, register 0x4000 |
| Softwareversion | `00 03 00 04 00 01 C4 1A` | broadcast (måned; år på 0x0008, dag på 0x0010) |

Broadcast-kommandoerne (adresse 0) må kun bruges, når modulet er **alene på bussen**.

## Faldgruber i forhold til PLC'en

1. **"Til"-værdien er ikke standard.** Manualen bruger `0x0100` som "til" i FC05. Standard Modbus, og dermed PLC'ens `MB_WRITE_COIL` og `MBX_WRITE_COIL`, sender `0xFF00`.
   - **Testet:** modulet accepterer `0xFF00` (PLC'ens standard), afprøvet med FC05 fra I/O-siden.
2. **Coil-adresserne starter ved 0 (afprøvet):** relæ 1 = coil 0 … relæ 4 = coil 3 — modsat manualen, der siger 1-4. En skrivning til coil 4 findes ikke på modulet; det svarer med et forkert ekko, som expansion-boardet (v0.34.3+) melder som `MB_RESPONSE_MISMATCH`.
3. **Adressekollision:** Standardadressen 1 er den samme som DM56A04-displayet på den lokale RS485-bus. Giv modulet en ny adresse, mens det sidder alene på bussen, før det kobles sammen med displayet.
4. **Antal 0 ved læsning af indgange (FC02):** Manualens eksempel er usædvanligt. Ved læsning fra ST (`MB_READ_INPUT(slave, addr)`) skal det afprøves, hvilken adresse (0 eller 1) der giver IN1.

## Fra ST Logic

Syntaksen er compile-testet på PLC'en (v7.9.68.113).

```
(* lokal RS485-bus, modul paa adresse 2 *)
MB_WRITE_COIL(2, 0) := TRUE;      (* relae 1 til (ogsaa: ok := MB_WRITE_COIL(2, 0, TRUE);) *)
MB_WRITE_COIL(2, 0) := FALSE;     (* relae 1 fra  *)
in1 := MB_READ_INPUT(2, 0);       (* IN1 - adresse 0 eller 1, se faldgrube 4 *)

(* via expansion board 1, kanal C - MBX_WRITE_COIL kraever kaldsformen,
   og kanalbogstavet skal citeres ('C'), ellers laeses det som en variabel *)
ok := MBX_WRITE_COIL(1, 'C', 2, 0, TRUE);  (* relae 1 *)
```
