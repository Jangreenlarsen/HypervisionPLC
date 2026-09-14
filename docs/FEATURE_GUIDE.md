# ESP32 Modbus RTU Server - Komplet Funktionsguide

**Version:** 2.0.0 | **Dato:** 2025-11-30 | **Platform:** ESP32-WROOM-32

**Indholdsfortegnelse:**
1. [Structured Text Logic Mode](#structured-text-logic-mode) **[NYT I V2.0.0]**
2. [Timere (4 modes)](#timere)
3. [Tællere (3 modes)](#tællere)
4. [CLI Kommandoer](#cli-kommandoer)
5. [GPIO Features](#gpio-features)
6. [Registers & Coils](#registers--coils)
7. [Persistence](#persistence)
8. [Eksempler](#eksempler)

---

## STRUCTURED TEXT LOGIC MODE

**[NYT I V2.0.0]** - Fuldstændig IEC 61131-3 Standard Text (ST) language support for avanceret automationslogik.

ESP32 serveren understøtter nu **4 uafhængige Structured Text programmer** med fuld IEC 61131-3 compliance. Dette tillader kompleks automationslogik uden at skulle tegne diagrammer.

### Kernefunktioner:

- **Structured Text sprog**: IEC 61131-3 "ST-Light" profil
- **4 uafhængige programmer**: Kører parallelt uden at påvirke hinanden
- **Variable-binding**: Automatisk mapping mellem ST-variabler og Modbus-registre
- **16 built-in funktioner**: ABS, MIN, MAX, SQRT, type-konverteringer, etc.
- **Kontrol flow**: IF/ELSIF/ELSE, CASE, FOR, WHILE, REPEAT
- **Non-blocking**: Integreret i Modbus server-loop, blokerer ikke RTU-service

### Eksempel - Temperatur-kontrol:

```
set logic 1 upload "VAR \
  temp_in: INT; \
  heating: BOOL; \
  cooling: BOOL; \
END_VAR \
IF temp_in < 18 THEN \
  heating := TRUE; \
  cooling := FALSE; \
ELSIF temp_in > 25 THEN \
  heating := FALSE; \
  cooling := TRUE; \
ELSE \
  heating := FALSE; \
  cooling := FALSE; \
END_IF;"

set logic 1 bind 0 100 input      # Læs temperatur fra HR#100
set logic 1 bind 1 101 output     # Skriv heating til HR#101
set logic 1 bind 2 102 output     # Skriv cooling til HR#102
set logic 1 enabled:true
```

### CLI Kommandoer:

```bash
# Upload ST program
set logic <id> upload "<st_code>"

# Bind variabler til Modbus registers
set logic <id> bind <var_idx> <register> [input|output|both]

# Aktivering
set logic <id> enabled:true|false

# Sletning
set logic <id> delete

# Visning
show logic <id>         # Detaljer om et program
show logic all          # Alle programmer
show logic stats        # Statistik
```

### Dokumentation:

Se **ST_USAGE_GUIDE.md** for detaljeret bruger-guide med:
- Quick-start guide (4 trin)
- Syntaks-eksempler
- Variable-binding forklaring
- Built-in function-reference
- Fejlfinding

Se **ST_IEC61131_COMPLIANCE.md** for teknisk specifikation og IEC-compliance.

---

## TIMERE

ESP32 serveren har **4 uafhængige timere** (Timer 1-4), hver med sit eget operationsmodus.

### TIMER MODE 1: ONE-SHOT (3-fase sekvens)

Afspil en forudbestemt sekvens på 3 faser med individuelle timinger.

**Sekvens:**
```
Start → P1 (duration_ms) → P2 (duration_ms) → P3 (duration_ms) → Stop
```

**Parametre:**
```
set timer <id> mode 1 \
  p1-duration:<ms> p1-output:<0|1> \
  p2-duration:<ms> p2-output:<0|1> \
  p3-duration:<ms> p3-output:<0|1> \
  output-coil:<addr>
```

**Eksempel - 3-sekunders alarm:**
```
set timer 1 mode 1 \
  p1-duration:1000 p1-output:1 \
  p2-duration:1000 p2-output:0 \
  p3-duration:1000 p3-output:1 \
  output-coil:200

write coil 200 value 1  # Start sekvenssl
```

**Adfærd:**
- Aktiveres når output-coil skrivesdet
- Hver fase varer sin egen duration
- Når fase 3 færdig → output slettes, timer slutter
- Kan ikke retriggers under kørsel

**Defaults:**
```
p1-duration: 0
p1-output: 0
p2-duration: 0
p2-output: 1
p3-duration: 0
p3-output: 0
```

---

### TIMER MODE 2: MONOSTABLE (retriggerable puls)

Generér enkelt puls af defineret længde. Kan retriggeres under kørsel.

**Sekvens:**
```
Hvile (P1) → Trigger → Puls (P2, pulse_ms) → Hvile (P1)
```

**Parametre:**
```
set timer <id> mode 2 \
  pulse-ms:<ms> \
  p1-output:<hvile_niveau> \
  p2-output:<puls_niveau> \
  output-coil:<addr>
```

**Eksempel - Pulsgenerator:**
```
set timer 2 mode 2 \
  pulse-ms:500 \
  p1-output:0 \
  p2-output:1 \
  output-coil:201

write coil 201 value 1  # Trigger → 500ms puls
write coil 201 value 1  # Retrigger under pulsen (forlænger med 500ms)
```

**Adfærd:**
- Hviler på P1-niveau indtil trigger
- Trigger = skrivning til output-coil
- Pulsen varer `pulse_ms` millisekunder
- Under pulsen: hvis der trigges igen → timer nulstilles (forlænges)
- Efter puls → tilbage til P1-niveau

**Defaults:**
```
pulse-ms: 0
p1-output: 0
p2-output: 1
```

---

### TIMER MODE 3: ASTABLE (oscillator/blinker)

Kontinuerlig toggle mellem to niveauer - perfekt til blinking.

**Sekvens:**
```
P1 (on_ms) → P2 (off_ms) → P1 (on_ms) → ... gentag
```

**Parametre:**
```
set timer <id> mode 3 \
  on-ms:<ms> \
  off-ms:<ms> \
  p1-output:<ON_niveau> \
  p2-output:<OFF_niveau> \
  output-coil:<addr> \
  enabled:<0|1>
```

**Eksempel - 1 Hz blink:**
```
set timer 3 mode 3 \
  on-ms:1000 \
  off-ms:1000 \
  p1-output:1 \
  p2-output:0 \
  output-coil:202 \
  enabled:1
```

**Adfærd:**
- Starter AUTOMATISK når `enabled=1`
- Skifter niveau hver `on_ms` / `off_ms`
- Kører kontinuerligt - kan ikke retriggers
- Kan stoppes med `set timer X mode 3 enabled:0`
- Output svinger permanent mellem P1 og P2

**Defaults:**
```
on-ms: 0
off-ms: 0
p1-output: 1
p2-output: 0
enabled: 0
```

**Test resultat:** Mode 3 fungerer perfekt (verificeret med 1000ms ON/OFF)

---

### TIMER MODE 4: INPUT-TRIGGERED ✨ **NY**

Svar på kantdetektioner på input med valgfrit delay før output.

**Sekvens:**
```
Overvåg input → Detektér kant → Vent delay → Sæt output
```

**Parametre:**
```
set timer <id> mode 4 \
  input-dis:<coil_addr> \
  trigger-edge:<0|1> \
  delay-ms:<ms> \
  output-coil:<addr>
```

**Triggers:**
- `trigger-edge:1` = Stigende kant (0→1)
- `trigger-edge:0` = Faldende kant (1→0)

**Eksempel - Rising edge detector:**
```
set timer 2 mode 4 \
  input-dis:30 \
  trigger-edge:1 \
  delay-ms:0 \
  output-coil:250

# Test: Skriv til input og se output gå HIGH
write coil 30 value 1    # 0→1 transition → Trigger!
read coil 250 1          # Coil 250 = 1 (HIGH)

write coil 30 value 0    # 1→0 transition (ikke matching)
read coil 250 1          # Coil 250 stadig 1

write coil 30 value 1    # 0→1 transition → Trigger igen!
read coil 250 1          # Coil 250 = 1 (HIGH)
```

**Eksempel - Falling edge detector:**
```
set timer 3 mode 4 \
  input-dis:31 \
  trigger-edge:0 \
  delay-ms:0 \
  output-coil:251

write coil 31 value 0    # Hvis var 1, nu 1→0 → Trigger!
read coil 251 1          # Coil 251 = 1 (HIGH)

write coil 31 value 1    # 0→1 (ikke matching)
read coil 251 1          # Coil 251 stadig 1

write coil 31 value 0    # 1→0 → Trigger igen!
read coil 251 1          # Coil 251 = 1 (HIGH)
```

**Adfærd:**
- Kontinuerlig overvågning af input
- Detekterer HVER gang den matchende kant forekommer
- Output sættes og FORBLIVER sat (latch-adfærd)
- `delay-ms`: Vent denne tid før at sætte output
- Virker med COIL-baserede inputs (simulation) eller diskrete inputs

**Defaults:**
```
input-dis: 0
trigger-edge: 1 (rising)
delay-ms: 0
p1-output: 1  # Output HIGH når triggered
```

**Test resultat:** Mode 4 [PASS] - Både rising og falling edge detection fungerer

---

### Timer Control Register (ctrl_reg)

Kontroller alle timer-modes (1-4) via Modbus holding register uden CLI.

**Funktionalitet:**
- **Bit 0 (0x0001):** START command - Starter timer execution
- **Bit 1 (0x0002):** STOP command - Stopper timer, slukker output
- **Bit 2 (0x0004):** RESET command - Nulstiller timer state helt
- Bits auto-cleares efter eksekvering (host læser altid 0)

**Konfiguration:**
```
set timer <id> mode <1-4> ... ctrl-reg:<holding_register> ...
```

**Eksempler:**

Mode 1 (One-shot) med ctrl_reg:
```
set timer 1 mode 1 \
  p1-dur:500 p1-out:1 \
  p2-dur:300 p2-out:0 \
  p3-dur:200 p3-out:0 \
  output-coil:100 \
  ctrl-reg:50

# Start timer via register
write h-reg 50 value 1   # Bit 0 = START
# Timer 1 executes 3-phase sequence

# Stop timer mid-sequence
write h-reg 50 value 2   # Bit 1 = STOP

# Reset timer
write h-reg 50 value 4   # Bit 2 = RESET
```

Mode 3 (Astable) med ctrl_reg:
```
set timer 3 mode 3 \
  on-ms:500 off-ms:500 \
  output-coil:150 \
  ctrl-reg:60 \
  enabled:0   # Don't auto-start

# Start via ctrl_reg instead of auto-start
write h-reg 60 value 1   # START

# Stop blinking
write h-reg 60 value 2   # STOP

# Reset state
write h-reg 60 value 4   # RESET
```

**Use Cases:**
- Start/stop timers fra Modbus master uden CLI adgang
- Synchronisere timer-execution med Modbus polling
- Implementere timer-sekvenser fra SCADA-systemer
- Trigger timers baseret på eksterne events (læst fra input-registre)

---

## TÆLLERE

ESP32 serveren har **4 uafhængige tællere** (Counter 1-4), hver med valgfrit operationsmodus.

### COUNTER MODE 1: SW (Software Polling)

Tæl pulser ved polling af diskret input-niveau hver loop.

**Specifikation:**
- **Input:** Diskret input (0-255)
- **Maksfrekvens:** ~10 kHz
- **Debounce:** 10ms (justerbar)
- **Precision:** Afhængig af polling-frekvens

**Kommando:**
```
set counter <id> mode 1 \
  input-dis:<idx> \
  debounce-ms:<10> \
  prescaler:<1|4|8|16|64|256|1024> \
  bit-width:<8|16|32|64> \
  scale:<float> \
  [direction:<up|down>] \
  [start-value:<0-65535>]
```

**Eksempel:**
```
set counter 1 mode 1 \
  input-dis:50 \
  debounce-ms:10 \
  prescaler:1 \
  bit-width:16 \
  scale:1.0

show registers 10 5  # Læs counter-registre
```

---

### COUNTER MODE 2: SW-ISR (Software med Interrupt)

Tæl pulser via GPIO-interrupt for højere frekvens.

**Specifikation:**
- **Input:** GPIO-pin (0-39, undtagen reserverede)
- **Maksfrekvens:** ~100 kHz
- **Debounce:** Supporteret
- **Precision:** GPIO ISR-baseret

**Kommando:**
```
set counter <id> mode 2 \
  hw-gpio:<gpio_pin> \
  debounce-ms:<10> \
  prescaler:<1|4|8|16|64|256|1024> \
  bit-width:<8|16|32|64> \
  scale:<float> \
  [direction:<up|down>] \
  [start-value:<0-65535>]
```

**Eksempel:**
```
set counter 2 mode 2 \
  hw-gpio:13 \
  debounce-ms:10 \
  prescaler:4 \
  bit-width:32 \
  scale:2.5  # Hver puls = 2.5 enheder
```

---

### COUNTER MODE 3: HW (Hardware PCNT)

Brug ESP32's hardware pulse counter for ultra-høj frekvens.

**Specifikation:**
- **Input:** GPIO-pin (0-39, undtagen reserverede)
- **Maksfrekvens:** >1 MHz
- **Hardware:** PCNT unit (auto-allokeret)
- **Overflow handling:** 16-bit HW → 64-bit akkumulator

**Kommando:**
```
set counter <id> mode 3 \
  hw-gpio:<gpio_pin> \
  prescaler:<1|4|8|16|64|256|1024> \
  bit-width:<8|16|32|64> \
  scale:<float> \
  [direction:<up|down>] \
  [start-value:<0-65535>]
```

**Eksempel:**
```
set counter 3 mode 3 \
  hw-gpio:19 \
  prescaler:1 \
  bit-width:64 \
  scale:1.0
```

---

### FÆLLES COUNTER-FEATURES

#### Prescaler (Alle modes)

Divide tæller-værdi uden at miste præcision.

**Strategi:**
- Tæl ALLE pulser internt
- Divider kun når værdi skrives til register

**Supporterede værdier:** 1, 4, 8, 16, 64, 256, 1024

**Formler:**
```
raw-register = counterValue / prescaler
value-register = counterValue × scale_factor
freq-register = pulses per second (Hz)
```

**Eksempel:**
```
Counter tæller 16000 pulser, prescaler=4
raw-register = 16000 / 4 = 4000
```

#### Bit-Width Clamping

Begrænser register-værdierne:

| Bit-Width | Max værdi | Type | Overflow |
|-----------|-----------|------|----------|
| 8 | 255 | uint8_t | Ja, wraps |
| 16 | 65535 | uint16_t | Ja, wraps |
| 32 | 4.29B | uint32_t | Ja, wraps |
| 64 | 18.4E18 | uint64_t | Nej, praktisk infinit |

#### Scale Factor (Multiplikator)

Konverter pulser til enheder.

**Eksempel:**
```
Fysisk måling: Pulstæller × 2.5 = Liter
set counter 1 mode 3 hw-gpio:19 scale:2.5

Hvis counter = 1000 pulser
value-register = 1000 × 2.5 = 2500 (liter)
```

#### Frequency Measurement

Automatisk måling af signal-frekvens.

**Specifikation:**
- **Vindue:** 1 sekund
- **Output:** Hz (0-20000)
- **Register:** freq-register
- **Uafhængig:** Af prescaler

**Læsning:**
```
read h-reg 12 1  # Læs freq-register for counter 1
```

#### Direction Control

Tæl op eller ned.

```
set counter 1 mode 1 ... direction:up    # Tæl op (default)
set counter 1 mode 1 ... direction:down  # Tæl ned
```

#### Control Register

Start/stop/reset tæller fra Modbus.

**Bits:**
```
Bit 0: reset-on-read (0=normal, 1=nulstil ved hver læsning)
Bit 1: start (write: start tæller)
Bit 2: stop (write: stop tæller)
Bit 7: running (read: status)
```

**Kommando:**
```
set counter 1 control auto-start:on running:on reset-on-read:off
```

#### Per-Counter Registers

Hver tæller bruger **4 basis holding-registre** (plus multi-word for 32/64-bit):

| Offset | Navn | Indhold | Multi-Word |
|--------|------|---------|------------|
| +0 | value-register | Skaleret værdi (counter × scale) | 1-4 words (bit_width) |
| +4 | raw-register | Rå værdi (counter / prescaler) | 1-4 words (bit_width) |
| +8 | freq-register | Frekvens i Hz | 1 word |
| +10 | ctrl-register | Control/status bitfield | 1 word |

**ctrl-register bit layout:**
- Bit 0: Reset command (W, auto-clears)
- Bit 1: Start command (W, auto-clears)
- Bit 2: Running status (R, 1=active)
- Bit 3: Overflow flag (R, 1=overflow)
- Bit 4: Compare match (R, 1=threshold reached)
- Bit 7: Direction (R, 0=up, 1=down)

**Eksempel for Counter 1:**
```
Register 10: index
Register 11: raw
Register 12: freq
Register 13: ctrl
Register 14: overflow
```

---

## CLI KOMMANDOER

### SHOW KOMMANDOER

```
show config          # Hele konfigurationen (pretty-print)
show counters        # Counter-tabel (ID, mode, input, prescaler, value, freq)
show timers          # Timer-status (placeholder)
show registers [s] [c]  # Holding-registers (start=0, count=10)
show coils           # Coil-tilstande (0-255)
show inputs          # Diskrete inputs (0-255)
show version         # Firmware-version + build-info
show gpio            # GPIO-mappings
show echo            # Remote echo-status
show reg             # Register-konfigurationer (STATIC+DYNAMIC)
show coil            # Coil-konfigurationer (STATIC+DYNAMIC)
```

### COUNTER KOMMANDOER

```
set counter <id> mode 1 [parameters]
set counter <id> mode 2 [parameters]
set counter <id> mode 3 [parameters]

set counter <id> control [bits]
set counter <id> start
set counter <id> stop
set counter <id> reset

reset counter <id>
clear counters
```

**Eksempel:**
```
set counter 1 mode 3 hw-gpio:19 prescaler:1 bit-width:32 scale:1.0
set counter 1 control auto-start:on
set counter 1 start
read h-reg 10 5
```

### TIMER KOMMANDOER

```
set timer <id> mode 1 [parameters]  # Mode 1: One-shot
set timer <id> mode 2 [parameters]  # Mode 2: Monostable
set timer <id> mode 3 [parameters]  # Mode 3: Astable (auto-start)
set timer <id> mode 4 [parameters]  # Mode 4: Input-triggered ✨

set timer <id> <mode> enabled:0     # Stop timer
```

**Eksempel:**
```
set timer 1 mode 3 on-ms:1000 off-ms:1000 output-coil:200 enabled:1
write coil 200 value 1
read coil 200 1
```

### GPIO KOMMANDOER

```
set gpio <pin> static map input:<idx>      # GPIO → Diskret input
set gpio <pin> static map coil:<idx>       # Coil → GPIO (output)
no set gpio <pin>                          # Fjern kortlægning

set gpio 2 enable                          # Frigiv GPIO2 (disable heartbeat)
set gpio 2 disable                         # Reservér GPIO2 (enable heartbeat)
```

**Eksempel:**
```
set gpio 16 static map input:45        # GPIO16 → Diskret input 45
set gpio 23 static map coil:112        # Coil 112 → GPIO23
no set gpio 16                         # Fjern GPIO16 kortlægning
```

### REGISTER/COIL KOMMANDOER

```
set reg STATIC <addr> Value <value>              # Fast register
set reg DYNAMIC <addr> counter<id>:<func>        # Counter-bundet
set reg DYNAMIC <addr> timer<id>:<func>          # Timer-bundet

set coil STATIC <addr> Value <ON|OFF>            # Fast coil
set coil DYNAMIC <addr> counter<id>:<func>       # Counter-bundet
set coil DYNAMIC <addr> timer<id>:<func>         # Timer-bundet

read h-reg <addr> [count]                # Læs registre
read coil <addr> [count]               # Læs coils
read input <addr> [count]              # Læs diskrete inputs

write h-reg <addr> value <value>         # Skriv register
write coil <addr> value <ON|OFF>       # Skriv coil
```

**Counter-funktioner:** index, raw, freq, overflow, ctrl
**Timer-funktioner:** output

**Eksempel:**
```
set reg DYNAMIC 20 counter1:index      # Register 20 = Counter 1's skalerede værdi
set coil DYNAMIC 100 timer1:output     # Coil 100 = Timer 1's output

set reg STATIC 30 Value 12345          # Fast værdi i register 30
set coil STATIC 50 Value ON            # Fast ON i coil 50

read h-reg 20 1
read coil 100 1
```

### SYSTEM KOMMANDOER

```
set hostname <name>         # Systemnavn (placeholder)
set baud <rate>             # Baudrate (300-115200, kræver reboot)
set id <slave_id>           # Modbus slave-ID (0-247)
set echo <on|off>           # Remote echo

save                        # Gem til NVS
load                        # Indlæs fra NVS
defaults                    # Nulstil til factory-defaults
reboot                      # Genstarte ESP32

help                        # Kommando-hjælp
?                          # Alias for help
```

---

## GPIO FEATURES

### GPIO Zones

| Type | Range | Brug | Link |
|------|-------|------|------|
| Fysiske GPIO | 0-39 | Direkte ESP32-pins | Hvis kortlagt |
| Virtuelle GPIO | 100-255 | Læser/skriver coils | Til diskret input |
| Reserverede | 0,2,4,5,15,19-21,33 | System | N/A |

### UNIFIED VARIABLE MAPPING (GPIO + ST Logic)

**[NYT I V2.1.0]** - Generaliseret mapping-system til både GPIO-pins og ST Logic variabler!

Det nye unified VariableMapping system betyder at GPIO-mapping og ST variable-binding bruger samme underliggende engine. Dette giver:

✅ **Konsistent interface** - Samme mapping-mekanisme for alle variable-typer
✅ **Ingen kodeduplikering** - Single I/O engine i `gpio_mapping_update()`
✅ **Letere vedligeholdelse** - Alle bindings på ét sted
✅ **Fremtidssikring** - Nemt at tilføje nye variable-kilder (USB, netværk, etc.)

**Arkitektur: 3-fase execution per loop:**
```
Phase 1: gpio_mapping_update()      → Læs INPUTs (GPIO + ST)
Phase 2: st_logic_engine_loop()     → Eksekvér ST programmer
Phase 3: gpio_mapping_update()      → Skriv OUTPUTs (GPIO + ST)
```

---

### STATIC GPIO MAPPING (GPIO PINS)

Kortlægning mellem GPIO pins og Modbus (persistent).

**Input-mode (GPIO → Diskret input):**
```
set gpio 16 static map input:45

Hver loop (Phase 1 & 3):
  1. Læs GPIO16
  2. Skriv til diskret input 45
```

**Output-mode (Coil → GPIO):**
```
set gpio 23 static map coil:112

Hver loop (Phase 1 & 3):
  1. Læs coil 112
  2. Sæt/clear GPIO23
```

---

### ST LOGIC VARIABLE BINDING (UNIFIED MAPPING)

Kortlægning mellem ST Logic variabler og Modbus holding-registers.

**Input-mode (Register → ST variable):**
```
set logic 1 bind 0 100 input

Hver loop (Phase 1):
  1. Læs HR#100
  2. Skriv til ST variabel[0]
```

**Output-mode (ST variable → Register):**
```
set logic 1 bind 2 101 output

Hver loop (Phase 3):
  1. Læs ST variabel[2]
  2. Skriv til HR#101
```

**Praktisk eksempel:**
```
# Opload ST temperatur-kontrol program
set logic 1 upload "VAR temp_in: INT; heating: BOOL; END_VAR ..."

# Bind indgang (læs temperatur fra Modbus)
set logic 1 bind 0 100 input      # temp_in ← HR#100

# Bind udgang (skriv kontrol til Modbus)
set logic 1 bind 1 101 output     # heating → HR#101

# Aktivér
set logic 1 enabled:true

# Nu: Hver 10ms opdateres HR#100→temp_in, program kører, heating→HR#101
```

---

### GPIO2 HEARTBEAT

Built-in "system alive" indikator.

**Default:** Blinker hver 500ms

**Enable heartbeat (standard):**
```
set gpio 2 disable  # Reserve GPIO2 til heartbeat
```

**Disable heartbeat (frigivelse til bruger):**
```
set gpio 2 enable   # GPIO2 til brugerkode
```

---

## REGISTERS & COILS

### Holding Registers (0-159)

Lagring for 16-bit værdier.

**Typer:**
1. **STATIC:** Faste værdier (persistent)
2. **DYNAMIC:** Auto-updated fra counter/timer
3. **Counter-bindet:** Læses fra counter-registre
4. **Timer-bundet:** Læses fra timer-output

**Lim itations:**
```
Max 16 STATIC registers
Max 16 DYNAMIC registers
Total: 32 af 160 kan være mapped
```

### Coils (0-255)

Bitbuffer for 1-bit værdier (ON/OFF).

**Typer:**
1. **STATIC:** Faste ON/OFF (persistent)
2. **DYNAMIC:** Auto-updated fra counter/timer
3. **Timer-output:** Direct timer output
4. **Counter-overflow:** Overflow-flag fra tæller

**Limi tations:**
```
Max 16 STATIC coils
Max 16 DYNAMIC coils
Total: 32 af 256 kan være mapped
```

---

## PERSISTENCE

### Configuration Storage

Hele konfigurationen gemmes i **NVS** (Non-Volatile Storage).

**Hvad gemmes:**
- Alle counter-konfigurationer
- Alle timer-konfigurationer
- Alle variable-mappings (GPIO + ST Logic)
  - GPIO mappings (STATIC GPIO ↔ Modbus)
  - ST variable bindings (ST Logic ↔ Holding Registers)
- STATIC register-værdier
- STATIC coil-værdier
- Modbus slave-ID
- Baudrate
- GPIO2 heartbeat-state

**Kommandoer:**
```
save        # Gem alt til NVS
load        # Indlæs fra NVS
defaults    # Nulstil til factory-defaults
reboot      # Genstarte (bruges ved baud-ændring)
```

### Schema Versioning

Version 2 med migration support.

**Auto-migration** når versioner stammer fra:
- v1 Mega2560 EEPROM format
- v2 ESP32 NVS format

---

## EKSEMPLER

### Eksempel 1: 1 kHz Blink (Timer Mode 3)

```bash
# Konfigurér Timer 1 til 1 Hz (1000ms ON, 1000ms OFF)
set timer 1 mode 3 on-ms:1000 off-ms:1000 p1-output:1 p2-output:0 output-coil:200 enabled:1

# Læs output (burde være 1)
read coil 200 1

# Vent 1 sekund
# Læs igen (burde være 0)
read coil 200 1

# Gem konfiguration
save
```

### Eksempel 2: Input-Triggered Alarm (Timer Mode 4)

```bash
# Konfigurér Timer 2 til at triggered på rising edge
set timer 2 mode 4 input-dis:30 trigger-edge:1 delay-ms:0 output-coil:250

# Simulér input
write coil 30 value 0        # Sæt input LOW
write coil 30 value 1        # Rising edge → Trigger!
read coil 250 1              # Output = 1

write coil 30 value 0        # Falling edge (ignoreret)
read coil 250 1              # Output stadig = 1

write coil 30 value 1        # Rising edge igen → Trigger igen!
read coil 250 1              # Output = 1
```

### Eksempel 3: Hardware Counter (Mode 3 + PCNT)

```bash
# Konfigurér Counter 1 til hardware HW med 1 kHz GPIO input
set counter 1 mode 3 hw-gpio:19 prescaler:1 bit-width:32 scale:1.0

# Start tæller
set counter 1 control auto-start:on

# Læs værdier efter 10 sekunder (burde være ~10000)
read h-reg 10 5

# Register 10 = index (skaleret)
# Register 11 = raw (fra prescaler)
# Register 12 = freq (Hz)
# Register 13 = ctrl
# Register 14 = overflow

# Nulstil tæller
set counter 1 reset
read h-reg 10 5  # Burde være ~0 nu
```

### Eksempel 4: GPIO Kortlægning

```bash
# Kortlæg GPIO16 til diskret input 45
set gpio 16 static map input:45

# Kortlæg coil 112 til GPIO23 (output)
set gpio 23 static map coil:112

# Hver gang GPIO16 goes HIGH → diskret input 45 = 1
# Hver gang coil 112 sættes → GPIO23 goes HIGH

# Gem
save
```

### Eksempel 5: 3-fase Timer Sekvens

```bash
# Timer 1: 1s HIGH, 500ms LOW, 500ms HIGH
set timer 1 mode 1 \
  p1-duration:1000 p1-output:1 \
  p2-duration:500 p2-output:0 \
  p3-duration:500 p3-output:1 \
  output-coil:200

# Trigger sekvensen
write coil 200 value 1

# Sekvens afspilles automatisk
# T+0..1000ms: HIGH
# T+1000..1500ms: LOW
# T+1500..2000ms: HIGH
# T+2000ms: Stop

read coil 200 1  # Check status
```

---

## CONSTRAINTS & LIMITS

| Parameter | Min | Max | Default |
|-----------|-----|-----|---------|
| **Timers** | 1 | 4 | N/A |
| **Counters** | 1 | 4 | N/A |
| **Registers** | 0 | 159 | N/A |
| **Coils** | 0 | 255 | N/A |
| **GPIO (fysisk)** | 0 | 39 | N/A |
| **GPIO (virtuel)** | 100 | 255 | N/A |
| **Prescaler** | 1 | 1024 | 1 |
| **Slave ID** | 0 | 247 | 1 |
| **Baudrate** | 300 | 115200 | 115200 |
| **Frequency window** | - | 1 sec | - |
| **Frequency max** | - | 20000 Hz | - |

---

## TROUBLESHOOTING

### Timer Mode 3 test viser [0,0,0,0,0]
**Problem:** test_suite_extended rapporterer failures
**Årsag:** Async race condition i test suite, ikke firmware
**Løsning:** Kør `test_timer_exact.py` eller `test_coil_raw.py` for manuel verifikation
**Status:** Mode 3 virker korrekt ✅

### Counter viser ikke værdier
**Problem:** Register 10+ er alle 0
**Årsag:**
1. Contador ikke startet (`set counter X control auto-start:on`)
2. Ingen signal på GPIO
3. GPIO ikke konfigureret som input
**Løsning:** `show counters` for at se status

### GPIO mapping virker ikke
**Problem:** GPIO ændrer sig ikke når coil skrives
**Årsag:** GPIO ikke kartla get eller GPIO er reserveret
**Løsning:** `show gpio` for at se aktuelle mappings

---

## VERSION HISTORY

### v1.0.0 (2025-11-29)
- ✅ Mode 1: One-shot (3-phase)
- ✅ Mode 2: Monostable (retriggerable pulse)
- ✅ Mode 3: Astable (oscillator/blink)
- ✅ Mode 4: Input-triggered ✨ **NY**
- ✅ Counter: SW, SW-ISR, HW (PCNT)
- ✅ Persistence: NVS + schema migration
- ✅ GPIO: Physical + Virtual
- ✅ 70+ CLI commands
- ✅ Modbus RTU (FC01-10)

---

## Support & Resources

**Dokumentation:**
- `docs/ESP32_Module_Architecture.md` - Arkitektur-oversigt
- `docs/SETUP_GUIDE.md` - Hardware setup
- `CLAUDE.md` - Development guide

**Test:**
- `test_mode4_comprehensive.py` - Timer Mode 4 test
- `test_suite_extended.py` - Full feature test
- `test_coil_raw.py` - Timer output timing

**GitHub:** https://github.com/Jangreenlarsen/HypervisionPLC
