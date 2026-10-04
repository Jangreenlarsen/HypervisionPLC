# Test Plan Index

**Version:** v6.0.0
**Build:** #1108+
**Formål:** Organiseret testdokumentation for ESP32 Modbus Server

---

## Testplan Struktur

Testplanerne er opdelt i fokuserede moduler for lettere vedligeholdelse:

| Fil | Indhold | Antal Tests |
|-----|---------|-------------|
| [ST_TEST_OPERATORS.md](ST_TEST_OPERATORS.md) | Aritmetiske, logiske, bit-shift, sammenligning | 20 |
| [ST_TEST_BUILTINS.md](ST_TEST_BUILTINS.md) | Matematiske, clamping, trig, type conversion, persistence | 23 |
| [ST_TEST_TIMERS.md](ST_TEST_TIMERS.md) | TON/TOF/TP timer funktioner | 6 |
| [ST_TEST_GPIO.md](ST_TEST_GPIO.md) | GPIO & Hardware tests | 4 |
| [ST_TEST_CONTROL.md](ST_TEST_CONTROL.md) | IF, CASE, FOR, WHILE, REPEAT | 5 |
| [ST_TEST_TYPES.md](ST_TEST_TYPES.md) | Type system, EXPORT, TIME literals | 12 |
| [ST_TEST_COMBINED.md](ST_TEST_COMBINED.md) | Fase 2: Kombinerede tests | 10 |
| [ST_TEST_FUNCTIONS.md](ST_TEST_FUNCTIONS.md) | **NY:** FUNCTION/FUNCTION_BLOCK (FEAT-003) | 14 |
| [API_TEST_PLAN.md](API_TEST_PLAN.md) | HTTP REST API tests | 25+ |
| **Total** | | **119+** |

---

## Quick Start

### For ST Logic Tests
1. Forbind til ESP32 via CLI (Serial eller Telnet)
2. Vælg relevant testfil baseret på funktionalitet
3. Kør tests sekventielt inden for hver kategori

### For API Tests
1. Sørg for WiFi er forbundet: `show wifi`
2. Aktivér HTTP server: `set http enabled on`
3. Brug curl, Postman, eller Python til at køre API tests

---

## Test Konventioner

| Symbol | Betydning |
|--------|-----------|
| ✅ | Test bestået |
| ❌ | Test fejlet |
| ⚠️ | Advarsel/bemærkning |
| 🔄 | Test skal gentages efter fix |
| ⬜ | Test ikke udført |

---

## Hardware Setup

Se [ST_TEST_GPIO.md](ST_TEST_GPIO.md) for komplet hardware konfiguration.

**Opsummering:**
- GPIO17-21: LED outputs (Coils)
- GPIO32-34: Switch inputs (Discrete Inputs)
- GPIO25: 1kHz test signal (Counter/Timer tests)

---

## Register Allocation for Tests

### Safe Register Ranges

| Range | Brug | Undgår Konflikt Med |
|-------|------|---------------------|
| **HR 20-29** | Basis INT test variables | Counter range (HR 100-179) |
| **HR 40-55** | DINT/DWORD tests (32-bit) | Counter range |
| **HR 60-69** | REAL tests (float) | Counter range |
| **HR 70-89** | Kombinerede tests | Counter range |
| **Coils 0-20** | Test outputs | - |
| **DI 0-20** | Test inputs | - |

### System Reserved Ranges

| Range | Reserveret Til |
|-------|----------------|
| HR 100-179 | Counter registers (4 counters x 20 regs) |
| IR/HR 200-293 | ST Logic system registers |

---

## Test Execution Workflow

### Før Test Session
```bash
# 1. Verificer firmware version
show system

# 2. Check WiFi (for API tests)
show wifi

# 3. Check HTTP status (for API tests)
show http

# 4. Clear eksisterende logic
set logic 1 delete
set logic 2 delete
set logic 3 delete
set logic 4 delete
```

### Under Test Session
1. Upload ST program
2. Konfigurer bindings
3. Aktivér program
4. Kør test cases
5. Verificer resultater
6. Dokumentér i testfil

### Efter Test Session
```bash
# Gem eventuelle ændringer
save

# Optionelt: Genstart for clean state
reboot
```

---

## Fejlhåndtering

### Compilation Errors
- Check syntax i ST program
- Verificer variable typer matcher
- Check for manglende semicolons

### Runtime Errors
- Check `show logic` for error info
- Verificer bindings er korrekte
- Check register ranges

### API Errors
- Check HTTP status: `show http`
- Verificer WiFi forbindelse
- Check authentication settings

---

## Related Documentation

- [REST_API.md](../docs/REST_API.md) - API dokumentation
- [MODBUS_REGISTER_MAP.md](../docs/MODBUS_REGISTER_MAP.md) - Register oversigt
- [ST_SYNTAX_REFERENCE.md](../docs/ST_SYNTAX_REFERENCE.md) - ST syntax guide

---

**Last Updated:** 2026-02-07
**Maintained By:** Development Team
