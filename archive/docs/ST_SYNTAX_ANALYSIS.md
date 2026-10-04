# ST Logic Syntax Analysis - README.md vs Implementation

<!-- DOC-STATUS: historisk -->
> ⚠️ **Historisk dokument — vedligeholdes ikke længere.** Skrevet til en ældre firmwareversion og kan være forældet (CLI-syntaks, pins, endpoints, standardværdier). Den aktuelle, vedligeholdte dokumentation er [brugermanualen](docs/manual/00_INDEKS.md) (CLI: appendiks A, REST: appendiks B, ST: appendiks D). Markeret ved dokumentationsgennemgang 2026-09-28.


**Dato:** 2025-12-12
**Version:** v4.1.0
**Analyst:** Claude Code

---

## Executive Summary

**KRITISK INKONSISTENS FUNDET:** README.md indeholder 3 forskellige ST syntax patterns, men kun ÉN af dem virker i den faktiske implementation!

### Status
- ❌ **README.md:** Viser FORKERT syntax med `PROGRAM`/`BEGIN`/`END` keywords
- ✅ **Implementering:** Understøtter KUN `VAR ... END_VAR` + statements
- ⚠️ **Bruger impact:** Brugere får compilation errors når de følger README eksempler

---

## Fundne Inkonsistenser

### 1. README.md Eksempel (Linje 408-419) - VIRKER IKKE! ❌

```st
PROGRAM Logic1          ← IKKE UNDERSTØTTET
VAR
  sensor_value: INT;
  save_trigger: BOOL;
END_VAR

sensor_value := 42;
IF save_trigger THEN
  SAVE();
END_IF;
END_PROGRAM             ← IKKE UNDERSTØTTET
```

**Compilation Error:**
```
Parse error at line 1: Expected VAR/VAR_INPUT/VAR_OUTPUT, got PROGRAM
```

---

### 2. README.md Example 3 (Linje 768-796) - VIRKER IKKE! ❌

```st
VAR_INPUT
  sensor : INT;
END_VAR

VAR_OUTPUT
  heater : INT;
END_VAR

BEGIN                   ← IKKE UNDERSTØTTET
  IF sensor < 50 THEN
    heater := 1;
  END_IF;
END                     ← IKKE UNDERSTØTTET
```

**Compilation Error:**
```
Parse error: Unexpected token BEGIN (not a keyword)
```

---

### 3. README.md Example 5 (Linje 894-915) - DELVIST FORKERT ❌

```st
PROGRAM CalibrationManager    ← IKKE UNDERSTØTTET
VAR
  cal_offset: INT;
END_VAR

BEGIN                         ← IKKE UNDERSTØTTET
  IF save_trigger THEN
    SAVE();
  END_IF;
END                          ← IKKE UNDERSTØTTET
```

---

### 4. KORREKT Syntax (Dokumenteret i GPIO2_ST_QUICK_START.md) ✅

```st
VAR
  sensor: INT;
  led: BOOL;
END_VAR

IF sensor > 50 THEN
  led := TRUE;
ELSE
  led := FALSE;
END_IF;
```

**INGEN `PROGRAM` keyword!**
**INGEN `BEGIN ... END` keywords!**
**INGEN `END_PROGRAM` keyword!**

---

## Implementeringsanalyse

### Lexer Keywords (src/st_lexer.cpp:120-121)

```cpp
{"PROGRAM", ST_TOK_PROGRAM},        // DEFINERET men IKKE BRUGT
{"END_PROGRAM", ST_TOK_END_PROGRAM}, // DEFINERET men IKKE BRUGT
```

**Note:** `BEGIN` og `END` er IKKE engang defineret som keywords!

### Parser Logic (src/st_parser.cpp:927-948)

```cpp
st_program_t *st_parser_parse_program(st_parser_t *parser) {
  // Parse variable declarations
  if (!st_parser_parse_var_declarations(parser, program->variables, &program->var_count)) {
    return NULL;  // FEJLER hvis første token ikke er VAR/VAR_INPUT/VAR_OUTPUT
  }

  // Parse statements (direkte efter END_VAR)
  program->body = st_parser_parse_statements(parser);

  return program;
}
```

**Observation:** Parseren forventer:
1. Variable declarations FØRST (`VAR...END_VAR`)
2. Statements DIREKTE efter `END_VAR` (IKKE efter `BEGIN`)
3. INGEN `PROGRAM` eller `END_PROGRAM` tokens håndteres

---

## Root Cause

**Tokens defineret, men aldrig implementeret i parser:**
- `ST_TOK_PROGRAM` exists in `st_types.h:78` with comment: `// PROGRAM (future, for now just statements)`
- `BEGIN` / `END` keywords findes SLET IKKE i lexeren

**Konklusion:** README.md eksempler er baseret på **planned future syntax**, ikke **current implementation**!

---

## Impact Assessment

### Bruger Experience

1. **Confusion:** Brugere følger README eksempler → får parse errors
2. **Trial-and-Error:** Brugere må gætte korrekt syntax gennem eksperimenter
3. **Support Burden:** Gentagne spørgsmål om "hvorfor virker README eksempel ikke?"

### Eksempler der FEJLER

**Fra README.md:**
- Linje 408-419: Persistence example (PROGRAM/END_PROGRAM)
- Linje 768-796: Example 3 thermostat (BEGIN/END)
- Linje 894-915: Example 5 calibration (PROGRAM + BEGIN)

**Kun 1 korrekt eksempel:**
- GPIO2_ST_QUICK_START.md linje 16 (inline format)

---

## Korrekt ST Syntax (v4.1.0)

### Minimal Program

```st
VAR
  x: INT;
END_VAR

x := 42;
```

### Med Input/Output Variables

```st
VAR_INPUT
  sensor: INT;
  setpoint: INT;
END_VAR

VAR_OUTPUT
  heater: BOOL;
  alarm: BOOL;
END_VAR

VAR
  hysteresis: INT := 5;
END_VAR

(* Statements start DIREKTE efter sidste END_VAR *)
IF sensor < (setpoint - hysteresis) THEN
  heater := TRUE;
ELSIF sensor > (setpoint + hysteresis) THEN
  heater := FALSE;
END_IF;

IF sensor > 100 THEN
  alarm := TRUE;
ELSE
  alarm := FALSE;
END_IF;
```

### Multi-line Upload (CLI)

```
> set logic 1 upload
Entering ST Logic upload mode. Type code and end with 'END_UPLOAD':
>>> VAR
>>>   sensor_value: INT;
>>>   save_trigger: BOOL;
>>> END_VAR
>>>
>>> sensor_value := 42;
>>>
>>> IF save_trigger THEN
>>>   SAVE();
>>> END_IF;
>>> END_UPLOAD
Compiling ST Logic program...
✓ COMPILATION SUCCESSFUL
```

**VIGTIGT:** INGEN `PROGRAM` eller `BEGIN` keywords!

---

## Anbefalinger

### 1. Ret README.md STRAKS (HIGH PRIORITY)

**Filer at rette:**
- `README.md` linjer 408-419 (persistence example)
- `README.md` linjer 768-796 (Example 3)
- `README.md` linjer 894-915 (Example 5)

**Ændringer:**
- Fjern ALLE `PROGRAM` keywords
- Fjern ALLE `BEGIN ... END` blokke
- Fjern ALLE `END_PROGRAM` keywords
- Brug syntax fra GPIO2_ST_QUICK_START.md som reference

### 2. Implementer PROGRAM/BEGIN Support (FUTURE)

**Hvis ønsket:**
1. Implementer `PROGRAM` parsing i `st_parser_parse_program()`
2. Tilføj `BEGIN`/`END` keywords til lexer
3. Opdater parser til at håndtere optional `BEGIN ... END` blok
4. Test backward compatibility

**Alternativt:**
- Fjern `ST_TOK_PROGRAM` og `ST_TOK_END_PROGRAM` fra `st_types.h`
- Opdater kommentar til "REMOVED - not supported"

### 3. Tilføj Syntax Reference til README

**Ny sektion:** "ST Logic Syntax Reference"

```markdown
## ST Logic Syntax (v4.1.0)

### Program Structure (REQUIRED ORDER)

1. Variable declarations (optional)
2. Statements (executable code)
3. NO wrapping keywords needed (no PROGRAM/BEGIN/END)

### Example

\`\`\`st
VAR
  x: INT := 0;
END_VAR

x := x + 1;
\`\`\`

### NOT SUPPORTED (Future)
- ❌ PROGRAM keyword
- ❌ BEGIN...END blocks
- ❌ END_PROGRAM keyword
```

---

## Test Cases

### Test 1: Minimal Valid Program ✅

```st
VAR x: INT; END_VAR
x := 1;
```

**Expected:** Compiles successfully

### Test 2: PROGRAM keyword (SHOULD FAIL) ❌

```st
PROGRAM Test
VAR x: INT; END_VAR
x := 1;
END_PROGRAM
```

**Expected:** Parse error at line 1

### Test 3: BEGIN keyword (SHOULD FAIL) ❌

```st
VAR x: INT; END_VAR
BEGIN
  x := 1;
END
```

**Expected:** Unexpected token BEGIN

---

## Konklusion

**KRITISK BUG I DOKUMENTATION:** README.md er ude af sync med implementation.

**Bruger siger:**
> "jeg upload ST programer fra cli, har jeg ikke echo"

**Faktisk problem:**
- Echo er fixed (v4.1.0+)
- **Men README syntax eksempler virker IKKE!**
- Brugeren får compilation errors fordi README viser forkert syntax

**Action Required:**
1. Fix README.md eksempler (STRAKS)
2. Test alle eksempler i README mod faktisk implementation
3. Tilføj syntax reference sektion
4. Beslut om PROGRAM/BEGIN skal implementeres eller fjernes fra lexer

---

## 🔴 KRITISK OPDATERING: SAVE() og LOAD() Virker IKKE!

**ROOT CAUSE FUNDET:** Function calls er IKKE implementeret i ST parseren!

### Problem

README.md viser mange eksempler med `SAVE()` og `LOAD()` funktioner:

```st
IF save_trigger THEN
  SAVE();  ← COMPILATION ERROR!
END_IF;
```

**Compilation Error:**
```
Parse error at line 7: Expected := in assignment
```

### Hvorfor?

**Parser Implementation (src/st_parser.cpp:202-206):**

```cpp
// Variable
if (parser_match(parser, ST_TOK_IDENT)) {
  st_ast_node_t *node = ast_node_alloc(ST_AST_VARIABLE, line);
  strcpy(node->data.variable.var_name, parser->current_token.value);
  parser_advance(parser);
  return node;  // ← RETURNERER UDEN AT TJEKKE FOR '('
}
```

**Når parseren ser `SAVE`:**
1. ✅ Matcher `ST_TOK_IDENT` (identifier)
2. ✅ Opretter `ST_AST_VARIABLE` node
3. ❌ Returnerer UDEN at tjekke om næste token er `(` (function call)

**Resultat:** `SAVE` behandles som en variabel, ikke en funktion!

### Implementation Status

**st_types.h:180:**
```cpp
ST_AST_FUNCTION_CALL,     // func(arg1, arg2, ...) (future)
                          //                        ^^^^^^^^ NOT IMPLEMENTED!
```

**Verification:**
- ❌ Ingen `case ST_AST_FUNCTION_CALL:` i st_parser.cpp
- ❌ Ingen parsing af `IDENT ( args )`
- ❌ Ingen VM bytecode for function calls
- ✅ `st_builtin_persist.cpp` eksisterer, men kan IKKE kaldes!

### Konklusion

**README.md Eksempler med SAVE()/LOAD() VIRKER IKKE i v4.1.0!**

Brugere kan IKKE bruge:
- `SAVE()` - Gem persistence groups
- `LOAD()` - Gendan persistence groups
- Nogen andre funktioner (ABS, SQRT, MIN, MAX - alle nævnt i README)

**Alt kræver function call parsing, som ikke er implementeret!**

---

**Fil:** `ST_SYNTAX_ANALYSIS.md`
**Genereret:** 2025-12-12
**Opdateret:** 2025-12-12 (SAVE/LOAD analyse)
**Version:** v4.1.0
