# GPIO 2 Control via ST Logic - Quick Start Card

<!-- DOC-STATUS: historisk -->
> ⚠️ **Historisk dokument — vedligeholdes ikke længere.** Skrevet til en ældre firmwareversion og kan være forældet (CLI-syntaks, pins, endpoints, standardværdier). Den aktuelle, vedligeholdte dokumentation er [brugermanualen](manual/00_INDEKS.md) (CLI: appendiks A, REST: appendiks B, ST: appendiks D). Markeret ved dokumentationsgennemgang 2026-09-28.


## 30-Sekunders Setup

### ⚡ Copy-Paste These Commands

```bash
# Step 1: Enable GPIO 2 for user control
set gpio 2 enable
save

# Step 2: Map GPIO2 to Coil #0
set gpio 2 static map coil:0

# Step 3: Upload ST program (LED control based on sensor)
set logic 1 upload "VAR sensor: INT; led: BOOL; END_VAR IF sensor > 50 THEN led := TRUE; ELSE led := FALSE; END_IF;"

# Step 4: Bind sensor input and LED output using variable names
set logic 1 bind sensor reg:100
set logic 1 bind led coil:0

# Step 5: Start the program
set logic 1 enabled:true

# Step 6: Verify it works
show logic 1
```

---

## Test It

### Turn LED ON
```bash
write_holding_register(100, 75)  # Sensor value > 50
read_holding_register(101)        # Should return: 1 (ON)
```

### Turn LED OFF
```bash
write_holding_register(100, 30)  # Sensor value < 50
read_holding_register(101)        # Should return: 0 (OFF)
```

---

## What's Happening

```
Input (HR#100): 75
    ↓
ST Logic: IF 75 > 50 THEN led := TRUE
    ↓
Output (HR#101): 1
    ↓
GPIO 2: HIGH
    ↓
LED: ON ✅
```

---

## Other Example Programs

### Example 2: Temperature Alarm
```bash
set logic 1 upload "VAR temp: INT; alarm: BOOL; END_VAR IF temp > 600 THEN alarm := TRUE; ELSIF temp < 550 THEN alarm := FALSE; END_IF;"
set logic 1 bind 0 100 input
set logic 1 bind 1 101 output
set logic 1 enabled:true
```

### Example 3: Counter-based Blinker
```bash
set logic 1 upload "VAR count: INT; led: BOOL; END_VAR count := count + 1; IF count > 10 THEN led := NOT led; count := 0; END_IF;"
set logic 1 bind 0 100 both
set logic 1 bind 1 101 output
set logic 1 enabled:true
```

### Example 4: Dual Output (AND logic)
```bash
set logic 1 upload "VAR input1: INT; input2: INT; output: BOOL; END_VAR output := (input1 > 25) AND (input2 > 25);"
set logic 1 bind 0 100 input
set logic 1 bind 1 101 input
set logic 1 bind 2 102 output
set logic 1 enabled:true
```

---

## Status Commands

```bash
# See program details
show logic 1

# See all programs
show logic all

# See execution stats
show logic stats

# Monitor in real-time
show logic stats   # Run repeatedly
```

---

## Common Issues & Fixes

| Issue | Fix |
|-------|-----|
| "GPIO2 user mode not enabled" | Run: `set gpio 2 enable` then `save` |
| "Program not compiled" | Make sure ST syntax is correct (check `docs/ST_USAGE_GUIDE.md`) |
| LED doesn't change | Check coil binding: `show logic 1` and verify `set gpio 2 static map coil:0` was set |
| LED not responding to coil changes | Ensure GPIO2 is enabled with `set gpio 2 enable` and mapped with `set gpio 2 static map coil:0` |

---

## Supported ST Features

✅ Variables (INT, BOOL, DWORD, REAL)
✅ IF/ELSIF/ELSE
✅ FOR loops
✅ WHILE loops
✅ Operators: +, -, *, /, AND, OR, NOT, <, >, >=, <=, =, <>
✅ Built-in functions: ABS, MIN, MAX, SQRT, etc.

❌ Function blocks (not supported)
❌ Recursion (not supported)

---

## Performance

- ST program executes every ~10ms
- Total latency: ~5ms (write to GPIO change)
- Max 4 logic programs running simultaneously
- Each program: up to 10,000 execution steps max (safety limit)

---

**Full documentation:** See `docs/ST_USAGE_GUIDE.md` and `docs/TEST_GPIO2_ST_LOGIC.md`
