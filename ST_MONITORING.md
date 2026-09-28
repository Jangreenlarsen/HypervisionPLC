# ST Logic Monitoring & Performance Tuning Guide

<!-- DOC-STATUS: historisk -->
> ⚠️ **Historisk dokument — vedligeholdes ikke længere.** Skrevet til en ældre firmwareversion og kan være forældet (CLI-syntaks, pins, endpoints, standardværdier). Den aktuelle, vedligeholdte dokumentation er [brugermanualen](docs/manual/00_INDEKS.md) (CLI: appendiks A, REST: appendiks B, ST: appendiks D). Markeret ved dokumentationsgennemgang 2026-09-28.


**Version:** v4.1.1
**Dato:** 2025-12-13

---

## 📊 Overview

ST Logic i v4.1.1 inkluderer omfattende performance monitoring værktøjer til at analysere og tune dine Structured Text programmer. Denne guide viser hvordan du bruger disse værktøjer til at optimere performance.

**Ny funktionalitet:**
- ✅ CLI kommandoer til performance statistik
- ✅ **Modbus register tilgang til alle statistikker** (Input Registers 252-293)
- ✅ **Dynamisk interval justering via CLI og Modbus** (2, 5, 10, 20, 25, 50, 75, 100 ms) - **NYT i v4.1.1: 2ms og 5ms support tilføjet**
- ✅ **Persistent interval storage** - Interval nulstilles ikke efter reboot (v4.1.1)

---

## 🎯 Nye CLI Kommandoer

### 1. `show logic stats`
Vis performance statistik for alle ST Logic programmer.

**Output:**
```
======== ST Logic Performance Statistics ========

Global Cycle Stats:
  Total cycles:    1523
  Cycle min:       2ms
  Cycle max:       15ms
  Cycle target:    10ms
  Overruns:        0 (0.0%)

Logic1 (Logic1):
  Status:        ENABLED, compiled
  Executions:    1523
  Min time:      0.245ms
  Max time:      1.125ms
  Avg time:      0.487ms
  Last time:     0.512ms

Logic2 (Logic2):
  Status:        ENABLED, compiled
  Executions:    1523
  Min time:      0.089ms
  Max time:      0.453ms
  Avg time:      0.124ms
  Last time:     0.098ms
  Overruns:      0 (0.0%)

Use 'show logic X timing' for detailed timing analysis
Use 'set logic stats reset' to clear statistics
=================================================
```

**Hvad betyder det:**
- **Total cycles:** Antal gange alle programmer er kørt
- **Cycle min/max:** Hurtigste/langsomste total cycle time
- **Overruns:** Antal gange cycle time > target interval
- **Per-program stats:** Min/Max/Avg execution time i millisekunder

---

### 2. `show logic X timing`
Detaljeret timing analyse for et specifikt program.

**Eksempel:** `show logic 1 timing`

**Output:**
```
======== Logic1 Timing Analysis ========

Program: Logic1
Status:  ENABLED (compiled)

Execution Statistics:
  Total executions:  1523
  Successful:        1523
  Failed:            0

Timing Performance:
  Min:               245µs
  Max:               1125µs
  Avg:               487µs
  Last:              512µs

Performance Analysis:
  Target interval:   10ms
  Rating:            ✓ EXCELLENT (< 25% of target)
  Overruns:          0 (0.0%) ✓

==========================================
```

**Performance Ratings:**
- ✓ **EXCELLENT:** Avg < 25% af target (< 2.5ms for 10ms target)
- ✓ **GOOD:** Avg < 50% af target (< 5ms for 10ms target)
- ⚠️ **ACCEPTABLE:** Avg < 100% af target (< 10ms for 10ms target)
- ❌ **POOR:** Avg > 100% af target - REFACTOR NEEDED!

**Med Overruns:**
```
Performance Analysis:
  Target interval:   10ms
  Rating:            ❌ POOR (> 100% of target) - REFACTOR NEEDED!
  Overruns:          245 (16.1%) ⚠️
                     Program exceeded target interval!

⚠️  RECOMMENDATIONS:
  - Simplify program logic (reduce loop iterations)
  - Increase execution interval (set logic interval:20)
  - Split program into smaller sub-programs
  - Consider moving heavy computation outside ST Logic
```

---

### 3. `set logic stats reset [target]`
Nulstil performance statistik.

**Targets:**
- `all` - Nulstil ALLE statistikker (programmer + global)
- `cycle` - Nulstil kun global cycle statistik
- `1-4` - Nulstil kun Logic1, Logic2, Logic3 eller Logic4

**Eksempler:**
```bash
set logic stats reset all      # Nulstil alt
set logic stats reset cycle    # Nulstil global cycle stats
set logic stats reset 1        # Nulstil kun Logic1
```

---

### 4. `set logic interval:X`
Juster global execution interval dynamisk.

**Tilladte værdier:** 2, 5, 10, 20, 25, 50, 75, 100 ms **(2ms og 5ms NYT i v4.1.1)**

**Eksempler:**
```bash
set logic interval:2    # Ekstrem hurtig (2ms) - VARNING: kan påvirke andre operationer!
set logic interval:5    # Meget hurtig (5ms) - VARNING: monitor performance
set logic interval:10   # Standard hurtig (10ms) - ANBEFALET som default
set logic interval:50   # Medium hastighed (50ms mellem cycles)
set logic interval:100  # Langsomste (100ms mellem cycles)
```

**Output:**
```
[OK] ST Logic execution interval set to 50ms
Note: Use 'save' command to persist to NVS
```

**Output for hurtige intervaller (< 10ms):**
```
⚠️  WARNING: Very fast interval (2ms) may impact other system operations
[OK] ST Logic execution interval set to 2ms
Note: Use 'save' command to persist to NVS
```

**Vigtigt:**
- ✅ Ændringer træder i kraft ØJEBLIKKELIGT
- ✅ Skal gemmes med `save` kommando for at overleve reboot **(NYT i v4.1.1: persistent)**
- ✅ Kan også kontrolleres via Modbus (HR 236-237)
- ⚠️ **Intervaller < 10ms:** Monitor system performance! Kan påvirke Modbus RTU, Wi-Fi, og andre funktioner

**Hvornår skal du ændre interval?**
- **2-5ms:** Kun for kritiske real-time applikationer (PID control, sikkerhedssystemer)
  - Monitor med `show logic stats` - check for overruns
  - Test grundigt for bivirkninger på Modbus latency
- **10ms (default):** Balanced mellem performance og system stability
- **20-50ms:** Når programmer er komplekse og har høj execution time
- **75-100ms:** Når du har mange programmer eller kan tolerere længere delays

---

## 📡 Modbus Register Adgang (v4.1.1)

Alle performance statistikker er tilgængelige via Modbus Input Registers (read-only) og Holding Registers (control). Execution interval kan også styres via Modbus (HR 236-237), og understøtter alle værdier 2-100ms.

### Input Registers (Read-Only Status)

**Per-Program Statistikker (32-bit værdier, 2 registers hver):**

| Register | Beskrivelse | Type | Enhed |
|----------|-------------|------|-------|
| **252-253** | Logic1 Min Execution Time | 32-bit | µs (mikrosekunder) |
| **254-255** | Logic2 Min Execution Time | 32-bit | µs |
| **256-257** | Logic3 Min Execution Time | 32-bit | µs |
| **258-259** | Logic4 Min Execution Time | 32-bit | µs |
| **260-261** | Logic1 Max Execution Time | 32-bit | µs |
| **262-263** | Logic2 Max Execution Time | 32-bit | µs |
| **264-265** | Logic3 Max Execution Time | 32-bit | µs |
| **266-267** | Logic4 Max Execution Time | 32-bit | µs |
| **268-269** | Logic1 Avg Execution Time | 32-bit | µs (beregnet) |
| **270-271** | Logic2 Avg Execution Time | 32-bit | µs |
| **272-273** | Logic3 Avg Execution Time | 32-bit | µs |
| **274-275** | Logic4 Avg Execution Time | 32-bit | µs |
| **276-277** | Logic1 Overrun Count | 32-bit | antal |
| **278-279** | Logic2 Overrun Count | 32-bit | antal |
| **280-281** | Logic3 Overrun Count | 32-bit | antal |
| **282-283** | Logic4 Overrun Count | 32-bit | antal |

**Global Cycle Statistikker (32-bit værdier, 2 registers hver):**

| Register | Beskrivelse | Type | Enhed |
|----------|-------------|------|-------|
| **284-285** | Cycle Min Time | 32-bit | ms |
| **286-287** | Cycle Max Time | 32-bit | ms |
| **288-289** | Cycle Overrun Count | 32-bit | antal |
| **290-291** | Total Cycles Executed | 32-bit | antal |
| **292-293** | Execution Interval (read-only) | 32-bit | ms |

### Holding Registers (Control)

| Register | Beskrivelse | Type | Adgang |
|----------|-------------|------|--------|
| **236-237** | Execution Interval Control | 32-bit | Read-Write |

**Interval Control (HR 236-237):** (NYT i v4.1.1: Persistent)
- Skriv ny interval værdi (2, 5, 10, 20, 25, 50, 75, 100) - **2ms og 5ms NYT i v4.1.1**
- Validering sker automatisk
- Ugyldige værdier afvises (register nulstilles til nuværende værdi)
- **Ændring gemmes automatisk til NVS ved `save` command** (v4.1.1)

**Eksempel (Python med pymodbus):**
```python
# Læs Logic1 average execution time (IR 268-269)
high = client.read_input_registers(268, 1).registers[0]
low = client.read_input_registers(269, 1).registers[0]
avg_us = (high << 16) | low
print(f"Logic1 avg execution time: {avg_us} µs")

# Sæt execution interval til 50ms (HR 236-237)
client.write_register(236, 0)      # High word = 0
client.write_register(237, 50)     # Low word = 50
print("Interval set to 50ms")

# Læs nuværende interval (IR 292-293)
high = client.read_input_registers(292, 1).registers[0]
low = client.read_input_registers(293, 1).registers[0]
interval = (high << 16) | low
print(f"Current interval: {interval} ms")
```

**32-bit Register Format:**
- Modbus registre er 16-bit
- 32-bit værdier opdeles i HIGH og LOW word
- HIGH word (bits 31-16) i første register
- LOW word (bits 15-0) i andet register
- Eksempel: 1234 µs = 0x000004D2 → HR[N]=0x0000, HR[N+1]=0x04D2

---

## 📈 Performance Metrics Forklaring

### Per-Program Metrics

| Metric | Beskrivelse | Hvad betyder det? |
|--------|-------------|-------------------|
| **Min time** | Hurtigste execution | Best-case performance |
| **Max time** | Langsomste execution | Worst-case performance |
| **Avg time** | Gennemsnitlig execution | Typisk performance |
| **Last time** | Sidste execution | Current performance |
| **Overruns** | Antal gange > interval | Stability indicator |
| **Executions** | Total antal kørsler | Sample size |
| **Errors** | Antal fejl | Reliability |

### Global Cycle Metrics

| Metric | Beskrivelse |
|--------|-------------|
| **Total cycles** | Antal gange ST Logic engine er kørt |
| **Cycle min/max** | Hurtigste/langsomste total cycle (alle 4 programmer) |
| **Cycle target** | Target interval (default 10ms) |
| **Overruns** | Antal gange total cycle time > target |

---

## 🔧 Performance Tuning Workflow

### Step 1: Baseline Measurement

1. **Enable dine programmer:**
   ```
   set logic 1 enabled:true
   set logic 2 enabled:true
   ```

2. **Nulstil statistik:**
   ```
   set logic stats reset all
   ```

3. **Lad det køre i 1-5 minutter**

4. **Check overall stats:**
   ```
   show logic stats
   ```

### Step 2: Identificer Problemer

**🔴 KRITISK hvis:**
- Overruns > 5% (cycle eller program)
- Avg time > 50% af target interval
- Max time > target interval
- Rating: POOR eller ACCEPTABLE

**🟡 MODERAT hvis:**
- Overruns < 5%
- Avg time 25-50% af target
- Rating: GOOD

**🟢 GOD hvis:**
- Overruns < 1%
- Avg time < 25% af target
- Rating: EXCELLENT

### Step 3: Detaljeret Analyse

For hvert problem-program:

```bash
show logic X timing
```

Observer:
- **Min vs Max spread:** Stor forskel = inconsistent performance
- **Overrun rate:** > 0% = programmet er for stort/komplekst
- **Recommendations:** Følg automatiske forslag

### Step 4: Optimization Strategies

#### Strategi 1: Simplificer Program

**Problem:** Max time > target, store loops

**Løsning:**
```
BEFORE (SLOW):
FOR i := 1 TO 1000 DO
  sum := sum + array[i];
END_FOR

AFTER (FAST):
FOR i := 1 TO 100 DO
  sum := sum + array[i];
END_FOR
```

Reducer loop iterations, split arbejde over flere cycles.

#### Strategi 2: Øg Execution Interval

**Problem:** Avg time > target, men acceptabel performance

**Løsning:**
```bash
# ✅ IMPLEMENTERET i v4.1.0
set logic interval:20   # 20ms i stedet for 10ms
set logic interval:50   # 50ms for meget langsomme programmer
save                    # Gem til NVS
```

Dobbelt interval = halvér load.

**Via Modbus:**
```python
# Sæt interval til 20ms via HR 236-237
client.write_register(236, 0)   # High word
client.write_register(237, 20)  # Low word = 20ms
```

#### Strategi 3: Split Program

**Problem:** Ét stort program med mange funktioner

**Løsning:**
- Split Logic1 i Logic1 + Logic2
- Separer kritiske og ikke-kritiske paths
- Brug variable bindings til at kommunikere

#### Strategi 4: Conditional Execution

**Problem:** Nogle funktioner behøver ikke køre hver cycle

**Løsning:**
```
VAR counter: INT; END_VAR

counter := counter + 1;

IF counter >= 10 THEN
  (* Run slow operation only every 10th cycle *)
  counter := 0;
  slow_computation();
END_IF
```

---

## 🚨 Common Issues & Solutions

### Issue 1: Occasional Overruns

**Symptoms:**
- Overruns 1-5%
- Max time >> Avg time
- Inconsistent performance

**Diagnose:**
```bash
show logic 1 timing
```

**Mulige årsager:**
- Conditional paths (IF/CASE) med forskellig kompleksitet
- Modbus trafik interrupt
- Wi-Fi/Telnet aktivitet

**Løsninger:**
1. Identificer conditional paths:
   ```
   set logic debug:true
   ```
   Monitor output for spikes

2. Simplificer worst-case path

3. Accepter <5% overruns hvis ikke kritisk

### Issue 2: Consistent Overruns

**Symptoms:**
- Overruns > 10%
- Avg time > target
- Rating: POOR

**Diagnose:**
```bash
show logic stats        # Check all programs
show logic 1 timing     # Detailed analysis
```

**Løsninger:**
1. **KRITISK:** Simplificer program STRAKS
2. Alternativt: Øg interval (når feature tilgængelig)
3. Split program i flere mindre

### Issue 3: High Cycle Time

**Symptoms:**
- Global cycle overruns
- Cycle max > target
- Multiple programs enabled

**Diagnose:**
```bash
show logic stats
```
Observer: Sum af alle program avg times.

**Løsning:**
```
Logic1: 4ms
Logic2: 3ms
Logic3: 2ms
Logic4: 1ms
--------
TOTAL:  10ms  <-- På grænsen!
```

Hvis total > target:
- Disable ikke-kritiske programmer
- Simplificer alle programmer
- Overvej parallel execution (v4.2.0)

---

## 📊 Example Tuning Session

### Initial State
```
show logic stats

Logic1: Avg 12.5ms, Overruns: 892 (58.5%) ❌
```

**Problem:** Program er ALT for langsomt!

### Investigation
```
show logic 1 timing

Performance Analysis:
  Target interval:   10ms
  Rating:            ❌ POOR (> 100% of target) - REFACTOR NEEDED!
  Overruns:          892 (58.5%) ⚠️

⚠️  RECOMMENDATIONS:
  - Simplify program logic (reduce loop iterations)
  - Increase execution interval (set logic interval:20)
  - Split program into smaller sub-programs
```

### Analysis: Check Program

```
show logic 1

Source:
VAR i, sum: INT; END_VAR
sum := 0;
FOR i := 1 TO 5000 DO   ← PROBLEM: 5000 iterations!
  sum := sum + i;
END_FOR
```

### Fix: Reduce Loop Iterations

```
set logic 1 upload "VAR i, sum: INT; END_VAR sum := 0; FOR i := 1 TO 500 DO sum := sum + i; END_FOR"
set logic stats reset 1
```

Wait 1 minute...

### Verify Fix
```
show logic 1 timing

Timing Performance:
  Avg:               1.2ms (was 12.5ms) ✓
  Overruns:          0 (0.0%) ✓

Performance Analysis:
  Rating:            ✓ EXCELLENT (< 25% of target)
```

**SUCCESS!** 10× performance improvement.

---

## 🎓 Best Practices

### 1. Always Measure Before Optimizing
```bash
set logic stats reset all
# Let it run for 1-5 minutes
show logic stats
```

### 2. Profile Individual Programs
```bash
show logic 1 timing  # Detailed analysis
show logic 2 timing
show logic 3 timing
show logic 4 timing
```

### 3. Monitor During Development
```bash
set logic debug:true  # Enable real-time timing output
```

Output:
```
[ST_TIMING] Cycle time: 3ms / 10ms (OK)
[ST_TIMING] Cycle time: 4ms / 10ms (OK)
[ST_TIMING] WARNING: Cycle time 15ms > target 10ms (overrun!)
```

### 4. Set Performance Goals

| Application | Target Avg | Max Overruns |
|-------------|------------|--------------|
| **Critical (PID control)** | < 10% of interval | 0% |
| **Important (monitoring)** | < 50% of interval | < 1% |
| **Non-critical (logging)** | < 75% of interval | < 5% |

### 5. Regular Performance Audits

**Weekly:**
```bash
show logic stats
```

Check for:
- Overrun trend (increasing over time = memory leak?)
- Avg time drift (increasing = code bloat?)

**After Code Changes:**
```bash
set logic stats reset all
# Deploy new code
# Wait 5 minutes
show logic stats
```

Compare before/after.

---

## 🔍 Troubleshooting

### Q: Why are overruns happening randomly?

**A:** Likely external factors:
- Modbus master traffic (Modbus FC03/FC16 can block)
- Wi-Fi reconnects (can take 50-500ms)
- Telnet connections

**Solution:** Profile with isolated system (no Modbus/Telnet).

### Q: Min time = Max time, no variation?

**A:** Program is very simple (< 100µs execution):
- This is GOOD! Deterministic performance.
- No optimization needed.

### Q: Overruns but Rating = GOOD?

**A:** Individual program is fast, but total cycle > target:
```
Logic1: 2ms (GOOD)
Logic2: 3ms (GOOD)
Logic3: 4ms (GOOD)
Logic4: 2ms (GOOD)
--------
TOTAL: 11ms > 10ms target! ← Cycle overrun
```

**Solution:** Reduce total load or increase interval.

### Q: Statistics not updating?

**A:** Check:
```
show logic stats
Total cycles: 0  ← Not executing!
```

Verify:
```
show logic 1
Status: disabled  ← Enable it!
```

---

## 📚 Related Documentation

- `TIMING_ANALYSIS.md` - Deep dive into timing architecture
- `BUGS.md` - Known bugs and fixes (BUG-007: timeout monitoring)
- `CLAUDE.md` - ST Logic architecture (Layer 5)

---

## 🚀 Future Features (v4.2.0+)

**Implemented in v4.1.0:**
- ✅ `set logic interval:X` - Dynamisk interval ændring
- ✅ Modbus register adgang til alle statistikker (IR 252-293)
- ✅ Interval control via Modbus (HR 236-237)

**Planned for v4.2.0:**
- `show logic monitor` - Real-time monitoring (opdateres hvert sekund)
- Per-program interval (forskellige rates for hver Logic1-4)
- Execution history buffer (sidste 10-100 executions)
- Latency histogram display

---

## 💡 Pro Tips

1. **Use microsecond precision:** Nye stats bruger µs i stedet for ms
   ```
   Min: 487µs (not 0ms)
   ```

2. **Check immediately after upload:**
   ```
   set logic 1 upload "..."
   set logic stats reset 1
   show logic 1 timing   # After 30 seconds
   ```

3. **Compare before/after optimization:**
   ```
   show logic 1 timing > before.txt
   # ... optimize ...
   show logic 1 timing > after.txt
   diff before.txt after.txt
   ```

4. **Monitor production systems:**
   - Schedule periodic `show logic stats` via Modbus/Telnet
   - Log output for trend analysis
   - Alert on overrun rate > threshold

---

**Version:** v4.1.0
**Author:** Claude Code
**Last Updated:** 2025-12-12
