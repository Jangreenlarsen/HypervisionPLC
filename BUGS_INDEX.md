# BUGS Index - Quick Reference

**Purpose:** Ultra-compact bug tracking for fast lookup. For detailed analysis, see BUGS.md sections.

## Bug Status Summary

| Bug ID | Title | Status | Priority | Version | Impact |
|--------|-------|--------|----------|---------|--------|
| BUG-001 | IR 220-251 ikke opdateret med ST Logic values | ✅ FIXED | 🔴 CRITICAL | v4.0.2 | ST Logic vars ikke synlig i Modbus |
| BUG-002 | Manglende type checking på ST variable bindings | ✅ FIXED | 🔴 CRITICAL | v4.0.2 | Data corruption ved type mismatch |
| BUG-003 | Manglende bounds checking på var index | ✅ FIXED | 🟡 HIGH | v4.0.2 | Buffer overflow risk |
| BUG-004 | Control register reset bit cleares ikke | ✅ FIXED | 🟡 HIGH | v4.0.2 | Error state persists incorrectly |
| BUG-005 | Inefficient variable binding count lookup | ✅ FIXED | 🟠 MEDIUM | v4.0.2 | Performance issue (O(n) lookup) |
| BUG-006 | Execution/error counters truncated til 16-bit | ✅ FIXED | 🔵 LOW | v4.0.2 | Counter wraps at 65535 |
| BUG-007 | Ingen timeout protection på program execution | ✅ FIXED | 🟠 MEDIUM | v4.0.2 | Runaway program can hang system |
| BUG-008 | IR 220-251 opdateres 1 iteration senere (latency) | ✅ FIXED | 🟠 MEDIUM | v4.1.0 | Stale data in Modbus registers |
| BUG-009 | Inkonsistent type handling (IR 220-251 vs gpio_mapping) | ✅ FIXED | 🔵 LOW | v4.1.0 | Confusing behavior, low priority |
| BUG-010 | Forkert bounds check for INPUT bindings | ✅ FIXED | 🔵 LOW | v4.1.0 | Cosmetic validation issue |
| BUG-011 | Variabelnavn `coil_reg` bruges til HR også (confusing) | ❌ OPEN | 🔵 LOW | v4.1.0 | Code clarity issue |
| BUG-012 | "both" mode binding skaber dobbelt output i 'show logic' | ✅ FIXED | 🟡 HIGH | v4.1.0 | Confusing UI display |
| BUG-013 | Binding visnings-rækkefølge matcher ikke var index | ✔️ NOT A BUG | 🔵 LOW | v4.1.0 | Design choice, not a bug |
| BUG-014 | execution_interval_ms bliver ikke gemt persistent | ✅ FIXED | 🟡 HIGH | v4.1.0 | Settings lost on reboot |
| BUG-015 | HW Counter PCNT ikke initialiseret uden hw_gpio | ✅ FIXED | 🔴 CRITICAL | v4.2.0 | HW counter doesn't work |
| BUG-016 | Running bit (bit 7) ignoreres | ✅ FIXED | 🔴 CRITICAL | v4.2.0 | Counter control broken |
| BUG-017 | Auto-start ikke implementeret | ✅ FIXED | 🔴 CRITICAL | v4.2.0 | Startup behavior inconsistent |
| BUG-018 | Show counters display respects bit-width | ✅ FIXED | 🟡 HIGH | v4.2.0 | Display truncation |
| BUG-019 | Show counters race condition (atomic reading) | ✅ FIXED | 🟡 HIGH | v4.2.0 | Intermittent display corruption |
| BUG-020 | Manual register configuration allowed (should be disabled) | ✅ FIXED | 🔴 CRITICAL | v4.2.0 | User can break system with bad config |
| BUG-021 | Delete counter command missing | ✅ FIXED | 🟡 HIGH | v4.2.0 | Can't reconfigure counters |
| BUG-022 | Auto-enable counter on running:on not working | ✅ FIXED | 🟡 HIGH | v4.2.0 | Counter state inconsistent |
| BUG-023 | Compare doesn't work when disabled | ✅ FIXED | 🟡 HIGH | v4.2.0 | Output stuck after disable |
| BUG-024 | PCNT counter truncated to 16-bit (raw reg limited to 2000) | ✅ FIXED | 🔴 CRITICAL | v4.2.0 | Counter value overflow |
| BUG-025 | Global register overlap not checked (Counter/Timer/ST overlap) | ✅ FIXED | 🔴 CRITICAL | v4.2.0 | Register conflicts possible |
| BUG-026 | ST Logic binding register allocator not freed on change | ✅ FIXED | 🔴 CRITICAL | v4.2.3 | Stale allocation persists across reboot (NOW FIXED) |
| BUG-027 | Counter display overflow - values above bit_width show incorrectly | ✅ FIXED | 🟠 MEDIUM | v4.2.3 | CLI display shows unclamped values |
| BUG-028 | Register spacing too small for 64-bit counters | ✅ FIXED | 🔴 CRITICAL | v4.2.3 | Counter 1 overlaps Counter 2 registers |
| BUG-029 | Compare modes use continuous check instead of edge detection | ✅ FIXED | 🔴 CRITICAL | v4.2.4 | Reset-on-read doesn't work, bit4 always set |
| BUG-030 | Compare value not accessible via Modbus | ✅ FIXED | 🔴 CRITICAL | v4.2.4 | Threshold only settable via CLI, not SCADA |
| BUG-031 | Counter write lock ikke brugt af Modbus FC handlers | ✅ FIXED | 🔴 CRITICAL | v4.2.5 | 64-bit counter read kan give korrupt data |
| BUG-032 | Buffer overflow i ST parser (strcpy uden bounds) | ✅ FIXED | 🔴 CRITICAL | v4.2.5 | Stack corruption ved lange variabelnavne |
| BUG-033 | Variable declaration bounds check efter increment | ✅ FIXED | 🔴 CRITICAL | v4.2.5 | Buffer overflow på 33. variable |
| BUG-034 | ISR state læsning uden volatile cast | ✅ FIXED | 🟡 HIGH | v4.2.6 | Sporadisk manglende pulser ved høj frekvens |
| BUG-035 | Overflow flag aldrig clearet automatisk | ✅ FIXED | 🟡 HIGH | v4.2.6 | Sticky overflow kræver manuel reset |
| BUG-036 | SW-ISR underflow wrapper ikke (inkonsistent med SW) | ✅ FIXED | 🟠 MEDIUM | v4.2.5 | DOWN mode stopper ved 0 i ISR mode |
| BUG-037 | Jump patch grænse 512 i stedet for 1024 | ✅ FIXED | 🟠 MEDIUM | v4.2.5 | Store CASE statements kan fejle |
| BUG-038 | ST Logic variable memcpy uden synchronization | ✅ FIXED | 🟡 HIGH | v4.2.6 | Race condition mellem execute og I/O |
| BUG-039 | CLI compare-enabled parameter ikke genkendt | ✅ FIXED | 🟠 MEDIUM | v4.2.7 | Kun "compare:1" virker, ikke "compare-enabled:1" |
| BUG-040 | Compare bruger rå counter værdi i stedet for prescaled | ✅ FIXED | 🟡 HIGH | v4.2.8 | Compare ignorerer prescaler/scale, ukonfigurérbar |
| BUG-041 | Reset-on-read parameter placering og navngivning forvirrende | ✅ FIXED | 🟠 MEDIUM | v4.2.9 | Samme parameter navn for counter og compare reset |
| BUG-042 | normalize_alias() håndterer ikke "auto-load" | ✅ FIXED | 🟡 HIGH | v4.3.0 | "set persist auto-load" ikke genkendt af parser |
| BUG-043 | "set persist enable on" case sensitivity bug | ✅ FIXED | 🟡 HIGH | v4.3.0 | enabled blev altid false → printer "DISABLED" |
| BUG-044 | cli_cmd_set_persist_auto_load() case sensitive strcmp | ✅ FIXED | 🟠 MEDIUM | v4.3.0 | "ENABLE" eller "Enable" ville ikke virke |
| BUG-045 | Upload mode ignorerer brugerens echo setting | ✅ FIXED | 🟡 HIGH | v4.3.0 | "set echo on" har ingen effekt i ST upload mode |
| BUG-046 | ST datatype keywords (INT, REAL) kolliderer med literals | ✅ FIXED | 🔴 CRITICAL | v4.3.1 | REAL/INT variable declarations fejler med "Unknown variable" |
| BUG-047 | Register allocator ikke frigivet ved program delete | ✅ FIXED | 🔴 CRITICAL | v4.3.2 | "Register already allocated" efter delete/recreate |
| BUG-048 | Bind direction parameter ignoreret | ✅ FIXED | 🟡 HIGH | v4.3.3 | "input" parameter ikke brugt, defaults altid til "output" |
| BUG-049 | ST Logic kan ikke læse fra Coils | ✅ FIXED | 🔴 CRITICAL | v4.3.3 | "coil:20 input" læser fra discrete input i stedet for coil |
| BUG-050 | VM aritmetiske operatorer understøtter ikke REAL | ✅ FIXED | 🔴 CRITICAL | v4.3.4 | MUL/ADD/SUB bruger altid int_val, REAL arithmetic giver 0 |
| BUG-051 | Expression chaining fejler for REAL | ✅ FIXED | 🟡 HIGH | v4.3.5 | "a := b * c / d" fejler, men separate statements virker |
| BUG-052 | VM operators mangler type tracking | ✅ FIXED | 🔴 CRITICAL | v4.3.6 | Comparison/logical/bitwise operators bruger st_vm_push() i stedet for st_vm_push_typed() |
| BUG-053 | SHL/SHR operators virker ikke | ✅ FIXED | 🔴 CRITICAL | v4.3.7 | Parser precedence chain mangler SHL/SHR tokens |
| BUG-054 | FOR loop body aldrig eksekveret | ✅ FIXED | 🔴 CRITICAL | v4.3.8 | Compiler bruger GT i stedet for LT i loop condition check |
| BUG-055 | Modbus Master CLI commands ikke virker | ✅ FIXED | 🔴 CRITICAL | v4.4.0 | normalize_alias() mangler parameter entries |
| BUG-056 | Buffer overflow i compiler symbol table | ✅ FIXED | 🔴 CRITICAL | v4.4.3 | strcpy uden bounds check i st_compiler_add_symbol() |
| BUG-057 | Buffer overflow i parser program name | ✅ FIXED | 🟠 MEDIUM | v4.4.3 | strcpy hardcoded string (low risk) |
| BUG-058 | Buffer overflow i compiler bytecode name | ✅ FIXED | 🟠 MEDIUM | v4.4.3 | strcpy program name til bytecode uden bounds check |
| BUG-059 | Comparison operators ignorerer REAL type | ✅ FIXED | 🔴 CRITICAL | v4.4.3 | EQ/NE/LT/GT/LE/GE bruger kun int_val, REAL comparison fejler |
| BUG-060 | NEG operator ignorerer REAL type | ✅ FIXED | 🟠 MEDIUM | v4.4.3 | Unary minus bruger kun int_val, -1.5 bliver til -1 |
| BUG-063 | Function argument overflow validation | ✅ FIXED | 🟡 HIGH | v4.4.3 | Parser bruger break i stedet for return NULL (compilation fejler ikke) |
| BUG-065 | SQRT mangler negative input validation | ✅ FIXED | 🟠 MEDIUM | v4.4.4 | sqrtf(negative) returnerer NaN, crasher beregninger |
| BUG-067 | Lexer strcpy buffer overflow risiko | ✅ FIXED | 🔴 CRITICAL | v4.4.4 | 12× strcpy uden bounds check (token value 256 bytes) |
| BUG-068 | String parsing mangler null terminator | ✅ FIXED | 🟡 HIGH | v4.4.4 | Loop limit 250 men buffer 256, strcpy kan fejle |
| BUG-072 | DUP operator mister type information | ✅ FIXED | 🟠 MEDIUM | v4.4.4 | REAL værdier duplikeres som INT → forkerte beregninger |
| BUG-073 | SHL/SHR mangler shift amount validation | ✅ FIXED | 🟠 MEDIUM | v4.4.4 | Shift >= 32 er undefined behavior på ESP32 |
| BUG-074 | Jump patch silent failure | ✅ FIXED | 🟠 MEDIUM | v4.4.4 | Bounds check returnerer uden fejlmelding → bytecode korruption |
| BUG-069 | INT literal parsing overflow | ✅ FIXED | 🟠 MEDIUM | v4.4.5 | strtol kan overflow uden errno check → undefined values |
| BUG-070 | REAL literal parsing overflow | ✅ FIXED | 🟠 MEDIUM | v4.4.5 | strtof kan overflow uden errno check → undefined values |
| BUG-083 | Modulo INT_MIN overflow | ✅ FIXED | 🔵 LOW | v4.4.5 | INT_MIN % -1 er undefined behavior i C/C++ |
| BUG-084 | Modbus slave_id mangler validation | ✅ FIXED | 🟠 MEDIUM | v4.4.5 | Kan sende requests til invalid slave (0, 248-255) |
| BUG-085 | Modbus address mangler validation | ✅ FIXED | 🟠 MEDIUM | v4.4.5 | Kan sende requests med negative addresser |
| BUG-066 | AST malloc fejl ikke håndteret | ✅ FIXED | 🟡 HIGH | v4.4.5 | 19× ast_node_alloc() uden NULL check → segfault på OOM |
| BUG-087 | NEG operator INT_MIN overflow | ✅ FIXED | 🔵 LOW | v4.4.5 | -INT_MIN er undefined behavior i C/C++ |
| BUG-081 | Memory leak ved parser error | ✅ FIXED | 🟠 MEDIUM | v4.4.5 | Expression parsing chain lækker AST ved fejl |
| BUG-077 | Function return type validation | ✅ FIXED | 🟠 MEDIUM | v4.4.5 | SEL/LIMIT polymorfiske funktioner bruger forkert type |
| BUG-088 | ABS funktion INT_MIN overflow | ✅ FIXED | 🔴 CRITICAL | v4.4.5 | ABS(-2147483648) returnerer -2147483648 (ikke positiv) |
| BUG-089 | ADD/SUB/MUL integer overflow | ✅ FIXED | 🔴 CRITICAL | v4.4.5 | Ingen overflow checks på arithmetic → silent overflow |
| BUG-104 | Function argument NULL pointer | ✅ FIXED | 🟠 MEDIUM | v4.4.5 | parser_parse_expression() NULL ikke håndteret |
| BUG-105 | INT type skal være 16-bit, ikke 32-bit (IEC 61131-3) | ✅ FIXED | 🔴 CRITICAL | v5.0.0 | INT overflow ikke korrekt, mangler DINT/multi-register |
| BUG-106 | Division by zero gemmer gamle værdier | ✅ FIXED | 🔴 CRITICAL | v5.0.0 | Variabler kopieres tilbage fra VM også ved runtime error |
| BUG-107 | CLI bind display viser "HR#X" for coil input | ✅ FIXED | 🔵 LOW | v5.0.0 | Forvirrende CLI output, men funktionalitet virker |
| BUG-108 | CLI mangler `write h-reg value real` kommando | ✅ FIXED | 🔴 CRITICAL | v5.0.0 | Kan ikke skrive REAL værdier korrekt via CLI |
| BUG-109 | Multi-register bindings ikke frigivet korrekt ved delete | ✅ FIXED | 🔴 CRITICAL | v5.0.0 | DINT/REAL bindings frigiver kun 1 register ved sletning |
| BUG-110 | SUM funktion ikke type-aware (returnerer kun første parameter) | ✅ FIXED | 🔴 CRITICAL | v5.0.0 | SUM(5,3) returnerer 5 i stedet for 8 |
| BUG-116 | Modbus Master funktioner ikke registreret i compiler | ✅ FIXED | 🔴 CRITICAL | v5.0.0 | MB_READ_COIL, MB_WRITE_HOLDING osv. kan ikke kompileres |
| BUG-117 | MIN/MAX funktioner ikke type-aware | ✅ FIXED | 🔴 CRITICAL | v5.0.0 | MIN/MAX med REAL værdier giver forkerte resultater |
| BUG-118 | ABS funktion kun INT type | ✅ FIXED | 🟡 HIGH | v5.0.0 | ABS(-1.5) returnerer 1 i stedet for 1.5 |
| BUG-119 | LIMIT funktion ikke type-aware | ✅ FIXED | 🟡 HIGH | v5.0.0 | LIMIT med REAL værdier clampes forkert |
| BUG-120 | SEL return type mangler DINT håndtering | ✅ FIXED | 🟠 MEDIUM | v5.0.0 | SEL(cond, DINT1, DINT2) returnerer INT type |
| BUG-121 | LIMIT return type mangler DINT håndtering | ✅ FIXED | 🟠 MEDIUM | v5.0.0 | LIMIT(DINT_min, val, DINT_max) returnerer INT type |
| BUG-122 | CLI show logic timing og reset logic stats ikke tilgængelige | ✅ FIXED | 🟠 MEDIUM | v5.0.0 | Funktioner implementeret men ikke eksponeret i parser/header |
| BUG-123 | Parser accepterer syntax fejl (reserved keywords i statement position) | ✅ FIXED | 🔴 CRITICAL | v5.0.0 | "THEN THEN", "END_IF x := 5" accepteres uden fejl |
| BUG-124 | Counter 32/64-bit DINT/DWORD register byte order | ✅ FIXED | 🔴 CRITICAL | v5.0.0 | CLI read/write brugte MSW først, counter skriver LSW først |
| BUG-125 | ST Logic multi-register byte order (DINT/DWORD/REAL INPUT/OUTPUT) | ✅ FIXED | 🔴 CRITICAL | v5.0.0 | Variable bindings brugte MSW først, skulle bruge LSW først |
| BUG-126 | st_count redeclaration i cli_show.cpp | ✅ FIXED | 🔵 LOW | v4.4.0 | Variable declared twice in same function, compile error |
| BUG-127 | st_state declaration order (used before declared) | ✅ FIXED | 🔵 LOW | v4.4.0 | Variable used on line 382 but declared on line 415 |
| BUG-128 | normalize_alias() mangler BYTECODE/TIMING keywords | ✅ FIXED | 🟠 MEDIUM | v4.4.0 | `show logic <id> bytecode/timing` kommandoer virker ikke |
| BUG-129 | normalize_alias() returnerer ST-STATS i stedet for STATS | ✅ FIXED | 🟡 HIGH | v4.4.0 | `show logic stats` og `reset logic stats` virker ikke |
| BUG-130 | NVS partition for lille til PersistConfig med ST bindings | ✅ FIXED | 🔴 CRITICAL | v4.5.0 | ESP_ERR_NVS_NOT_ENOUGH_SPACE (4357) ved bind kommandoer |
| BUG-131 | CLI `set id` kommando virker ikke (SLAVE-ID vs ID mismatch) | ✅ FIXED | 🟡 HIGH | v4.5.0 | normalize_alias() returnerer "SLAVE-ID" men parser tjekker "ID" |
| BUG-132 | CLI `set baud` kommando virker ikke (BAUDRATE vs BAUD mismatch) | ✅ FIXED | 🟡 HIGH | v4.5.0 | normalize_alias() returnerer "BAUDRATE" men parser tjekker "BAUD" |
| BUG-133 | Modbus Master request counter reset mangler | ✅ FIXED | 🔴 CRITICAL | v4.5.2 | g_mb_request_count aldrig resettet → system blokerer efter 10 requests. FIX v2 (v7.9.0.2): Reset per-slot i stedet for per-cycle — alle 4 ST slots får nu egen quota |
| BUG-134 | MB_WRITE DINT arguments sender garbage data | ✅ FIXED | 🔴 CRITICAL | v4.6.1 | DINT slave_id/address bruger int_val i stedet for dint_val → garbage validering (Build #919) |
| BUG-135 | MB_WRITE_HOLDING mangler value type validering | ✅ FIXED | 🔴 CRITICAL | v4.6.1 | REAL/DWORD værdier bruger int_val → garbage sendt til remote register (Build #919) |
| BUG-136 | MB_WRITE_COIL mangler value type validering | ✅ FIXED | 🔴 CRITICAL | v4.6.1 | INT værdier bruger bool_val i stedet for konvertering → random coil state (Build #919) |
| BUG-137 | CLI `read h-reg <count> real/dint/dword` ignorerer count parameter | ✅ FIXED | 🟠 MEDIUM | v4.7.1 | Kan ikke læse arrays af multi-register værdier (Build #937) |
| BUG-138 | ST Logic upload error message generisk og ikke informativ | ✅ FIXED | 🔵 LOW | v4.7.1 | Viser kun "Failed to upload" uden detaljer (Build #940) |
| BUG-139 | `show logic stats` skjuler disabled programs med source code | ✅ FIXED | 🟠 MEDIUM | v4.7.1 | Pool total matcher ikke per-program sum (Build #948) |
| BUG-140 | Persistence group_count=255 buffer overflow i show config | ✅ FIXED | 🔴 CRITICAL | v4.7.1 | Out-of-bounds array access → garbage display + crash risk (Build #951 + recovery cmd #953) |
| BUG-141 | Save/load viser var_map_count i stedet for aktive mappings | ✅ FIXED | 🟠 MEDIUM | v4.7.1 | Viser "32 mappings" selvom alle er unused (Build #960) |
| BUG-142 | `set reg STATIC` blokerer HR238-255 fejlagtigt | ✅ FIXED | 🟠 MEDIUM | v4.7.3 | Validation blokerede HR200-299, nu korrigeret til HR200-237 (Build #995) |
| BUG-143 | ST Logic IR variable mapping begrænset til 8 per program | ✅ FIXED | 🟠 MEDIUM | v5.1.0 | Fixed via EXPORT keyword + dynamic IR pool allocation (Build #1032) |
| BUG-144 | Forvirrende CLI: "read reg" læser HR, men ST vars er i IR | ✅ FIXED | 🔵 LOW | v4.7.2 | Brugere forventer "read h-reg 220" viser ST vars, men skal bruge "read i-reg 220" (Build #973-974) |
| BUG-145 | CLI help message mangler "read input-reg" option | ✅ FIXED | 🔵 LOW | v4.7.2 | "read" uden argumenter viste ikke "input-reg" option selvom funktionen findes (Build #973) |
| BUG-146 | Use-after-free i config_save.cpp | ✅ FIXED | 🔴 CRITICAL | v4.7.3 | Memory corruption - debug print brugte frigivet pointer (config_save.cpp:175) (Build #995) |
| BUG-147 | Buffer underflow i modbus_frame.cpp | ✅ FIXED | 🔴 CRITICAL | v4.7.3 | Integer underflow i memcpy size → buffer overflow (modbus_frame.cpp:84,100) (Build #995) |
| BUG-148 | Printf format mismatch i cli_config_regs.cpp | ✅ FIXED | 🟡 HIGH | v4.7.3 | %ld format med int32_t argument - portability issue (cli_config_regs.cpp:398) (Build #995) |
| BUG-149 | Identical condition i modbus_master.cpp | ✅ FIXED | 🟠 MEDIUM | v4.7.3 | Redundant indre if-check altid sand (modbus_master.cpp:181) (Build #995) |
| BUG-150 | CTUD ikke implementeret i VM | ✅ FIXED | 🔴 CRITICAL | v4.8.1 | VM execution handler tilføjet for 5-arg CTUD counter (st_vm.cpp:1047-1059) (Build #1016) |
| BUG-151 | Type stack corruption i AND/OR/XOR/NOT operationer | ✅ FIXED | 🔴 HIGH | v4.8.0 | Legacy st_vm_pop() korrupterer type_stack → downstream type inference fejler (st_vm.cpp:483-527) (Build #1010) |
| BUG-152 | Signal processing antager REAL type uden validering | ✅ FIXED | 🟡 MEDIUM | v4.8.0 | SCALE/HYSTERESIS/BLINK/FILTER bruger direkte .real_val uden type check (st_vm.cpp:1070-1213) (Build #1011) |
| BUG-153 | FILTER hardcoded cycle time (10ms) | ✅ FIXED | 🟠 MEDIUM | v4.8.1 | Cycle time læses nu fra stateful->cycle_time_ms (st_logic_engine.cpp:51-55, st_builtin_signal.cpp:163-169) (Build #1015) |
| BUG-154 | Jump target validation manglede | ✅ FIXED | 🟠 MEDIUM | v4.8.0 | JMP/JMP_IF_FALSE/JMP_IF_TRUE validerer ikke target < instr_count (st_vm.cpp:1275-1329) (Build #1012) |
| BUG-155 | Buffer overflow i st_token_t.value | ✅ FIXED | 🔴 CRITICAL | v4.8.2 | Token buffer kun 256 bytes, lexer kan skrive mere → stack corruption (FIXED Build #1020) |
| BUG-156 | Manglende validation af function argument count | ✅ FIXED | 🔴 CRITICAL | v4.8.2 | Compiler validerer ikke antal argumenter → stack corruption (st_compiler.cpp:335-344) (Build #1018) |
| BUG-157 | Stack overflow risk i parser recursion | ✅ FIXED | 🔴 CRITICAL | v4.8.2 | Rekursiv descent uden depth limit → ESP32 crash (st_parser.h:27, st_parser.cpp:28,353-374) (Build #1018) |
| BUG-158 | NULL pointer dereference i st_vm_exec_call_builtin | ✅ FIXED | 🔴 CRITICAL | v4.8.2 | Stateful check EFTER brug → NULL deref (st_vm.cpp:1000-1272) (Build #1018) |
| BUG-159 | Integer overflow i FOR loop | ✅ MITIGATED | 🟡 HIGH | v4.8.2 | FOR loop overflow beskyttet af max_steps=10000 limit (st_logic_engine.cpp:62) |
| BUG-160 | Missing NaN/INF validation i arithmetic | ✅ FIXED | 🟡 HIGH | v4.8.2 | REAL arithmetik validerer ikke NaN/INF → propagering (st_vm.cpp:284-422) (Build #1018) |
| BUG-161 | Division by zero i SCALE function | ✅ FIXED | 🟡 HIGH | v4.8.2 | Returnerer arbitrær værdi uden error (st_builtin_signal.cpp:28-32) (Build #1018) |
| BUG-162 | Manglende bounds check på bytecode array | ✅ FIXED | 🟡 HIGH | v4.8.2 | target_addr ikke valideret → VM crash (st_compiler.cpp:150-156) (Build #1018) |
| BUG-163 | Memory leak i parser error paths | ✅ VERIFIED | 🟡 HIGH | v4.8.2 | st_ast_node_free rekursivt frigiver alle args (st_parser.cpp:140-145) |
| BUG-164 | Inefficient linear search i symbol lookup | ✅ ACCEPTABLE | 🟡 HIGH | v4.8.2 | O(n) acceptable for max 32 vars (st_compiler.cpp:73-80) |
| BUG-165 | Missing input validation i BLINK function | ✅ FIXED | 🟠 MEDIUM | v4.8.2 | Negative time → huge unsigned (st_builtin_signal.cpp:98-99) (Build #1019) |
| BUG-166 | Race condition i stateful storage access | ✔️ NOT A BUG | 🟠 MEDIUM | v4.8.2 | FALSE POSITIVE - alt kører single-threaded i Arduino loop() (st_logic_engine.cpp:54, st_vm.cpp:1222) |
| BUG-167 | No timeout i lexer comment parsing | ✅ FIXED | 🟠 MEDIUM | v4.8.2 | Unterminated comment scanner til EOF (st_lexer.cpp:50-63) (Build #1019) |
| BUG-168 | Missing validation af CASE branch count | ✅ FIXED | 🟠 MEDIUM | v4.8.2 | Max 16 branches ikke valideret → memory overwrite (st_compiler.cpp:475-574) (Build #1019) |
| BUG-169 | Inefficient memory usage i AST nodes | ✅ ACCEPTABLE | 🔵 LOW | v4.8.2 | Union ~600 bytes per node - acceptable for temporary compilation RAM (st_types.h:270-291) |
| BUG-170 | Missing overflow check i millis() wraparound | ✅ NOT A BUG | 🔵 LOW | v5.1.1 | Unsigned arithmetic handles wraparound correctly - documented (st_builtin_signal.cpp:122-128) (Build #1040) |
| BUG-171 | Suboptimal error messages i compiler | ✅ FIXED | 🔵 LOW | v5.1.1 | Compiler nu inkluderer line number i error messages (st_compiler.h:52, st_compiler.cpp:170-180, 893, 253) (Build #1040) |
| BUG-172 | Missing overflow detection i integer arithmetic | ✅ DOCUMENTED | 🟠 MEDIUM | v5.1.1 | Design choice dokumenteret: wrapping for performance (st_vm.cpp:280-287) (Build #1040) |
| BUG-173 | MOD operation med negative operands | ✅ DOCUMENTED | 🔵 LOW | v5.1.1 | C remainder semantics dokumenteret vs matematik modulo (st_vm.cpp:470-473) (Build #1040) |
| BUG-174 | Missing type validation i binary operations | ✅ FIXED | 🟠 MEDIUM | v5.1.1 | BOOL + BOOL nu valideret - giver type error (st_vm.cpp:273-277, 322-326, 371-375, 420-424, 463-467) (Build #1038) |
| BUG-175 | FILTER function med zero cycle time | ✅ FIXED | 🔵 LOW | v5.1.1 | Fallback dokumenteret med forklaring (st_builtin_signal.cpp:184-189) (Build #1040) |
| BUG-176 | HYSTERESIS function med inverterede thresholds | ✅ FIXED | 🔵 LOW | v4.8.2 | Ingen validation af high > low (st_builtin_signal.cpp:69-76) (Build #1019) |
| BUG-177 | strcpy uden bounds check i lexer operators | ✅ FIXED | 🔵 LOW | v5.1.1 | strcpy → strncpy for 2-char operators (:=, <>, <=, >=, **) (st_lexer.cpp:412-445) (Build #1038) |
| BUG-178 | EXPORT variables ikke skrevet til IR 220-251 | ✅ FIXED | 🔴 CRITICAL | v5.1.1 | EXPORT keyword allokerede pool men skrev aldrig værdier til IR → Modbus read viste altid 0 (ir_pool_manager.cpp:166-236, st_logic_engine.cpp:108-110) (Build #1044) |
| BUG-179 | CLI read i-reg mangler type parameter support | ✅ FIXED | 🟠 MEDIUM | v5.1.2 | "read i-reg 220 int/dint/dword/real" fejlede med "antal skal være større end 0" fordi type blev parset som count → atoi("int")=0 (cli_show.cpp:2687-2960) (Build #1048) |
| BUG-180 | Counter overflow mister ekstra counts ved wraparound | ✅ FIXED | 🟡 HIGH | v5.1.3 | Ved overflow fra 65535 → 0, mistedes ekstra counts. Nu bevares overflow: start_value + (pcnt_value - max_val - 1) (counter_hw.cpp:118-123, counter_sw.cpp:156-161, counter_sw_isr.cpp:190-195) (Build #1052) |
| BUG-181 | DOWN mode underflow wrapper til max_val i stedet for start_value | ✅ FIXED | 🔴 CRITICAL | v5.1.4 | DOWN counting wrapper fejlagtigt til 65535 i stedet for start_value ved underflow (0-1). UP mode: wrap til start_value ✓. DOWN mode: burde wrappe til start_value også! Fixet i alle 3 modes: SW/SW_ISR/HW (counter_sw.cpp:114-122, counter_sw_isr.cpp:39+54-60+77-84+101-107+124-130+236-237, counter_hw.cpp:93-105) (Build #1063) |
| BUG-182 | PCNT signed overflow ved 32768 + atol/atoll signed parsing | ✅ FIXED | 🔴 CRITICAL | v5.1.5 | DOBBELT BUG: (1) PCNT hardware er signed 16-bit (-32768 til 32767), men vi vil have unsigned (0-65535). Hardware events disabled. (2) atol()/atoll() parser signed værdier → 32-bit counter med start_value > 2.1B får negativt tal! Fix: strtoul/strtoull for unsigned parsing (cli_commands.cpp:124-127+171-174, pcnt_driver.cpp:69-99) (Build #1069) |
| BUG-183 | start_value kun uint16_t - begrænser 32/64-bit counters | ✅ FIXED | 🔴 CRITICAL | v5.1.6 | CounterConfig.start_value var uint16_t (0-65535), men counters kan være 32-bit eller 64-bit. CLI brugte strtoul (32-bit). CLI display brugte (unsigned int) cast → trunkering. FIX: start_value → uint64_t, strtoul → strtoull, debug_print_uint → debug_print_ulong (types.h:82, cli_commands.cpp:124-127, cli_show.cpp:172+935+1485-1494+1506) (Build #1077) |
| BUG-184 | Frequency measurement giver forkerte resultater for DOWN counting | ✅ FIXED | 🟡 HIGH | v5.1.7 | Frequency algoritme antog altid UP counting. For DOWN: current_value < last_count er normalt, men koden gik i wrap-around branch og beregnede kæmpe forkert delta. FIX: Direction-aware delta beregning (counter_frequency.cpp:92-138) (Build #1074) |
| BUG-185 | Timer Mode 2 trigger_level parameter ikke brugt | ✔️ DESIGN | 🔵 LOW | v5.1.7 | trigger_level er legacy parameter - Mode 2 (Monostable) triggeres via Modbus ctrl_reg, ikke input-niveau. Dokumenteret som design choice |
| BUG-186 | Timer Mode 1 duration=0 kører hele sekvensen på én iteration | ✔️ DESIGN | 🔵 LOW | v5.1.7 | Hvis alle 3 phase durationer er 0, springer timer igennem alle faser på én loop iteration. Intentional "instant sequence" feature |
| BUG-187 | Timer ctrl_reg ikke initialiseret i defaults | ✅ FIXED | 🟠 MEDIUM | v5.1.7 | timer_config_defaults() satte ikke ctrl_reg, default var 0 → overlap med andre subsystemer. FIX: Smart defaults Timer 1→HR180, Timer 2→HR185, etc. (timer_config.cpp:64-67) (Build #1074) |
| BUG-188 | ISR underflow wrapper vs HW mode inkonsistens | ✔️ DESIGN | 🔵 LOW | v5.1.7 | SW/SW_ISR er edge-triggered (delta=1 altid), HW kan have delta>1. Simpel wrap er korrekt for ISR, kompleks wrap med overflow_amt er korrekt for HW |
| BUG-189 | Timer Mode 4 læser fra COIL i stedet for Discrete Input | ✔️ DESIGN | 🔵 LOW | v5.1.7 | Parameter hedder input_dis men koden læser registers_get_coil(). Bevidst design: tillader Modbus-triggered timer control. Dokumenteret |
| BUG-190 | ST Debug: total_steps_debugged tæller i OFF mode | ✅ FIXED | 🔵 LOW | v5.3.0 | FEAT-008 bugfix: Counter incrementeredes for alle steps, ikke kun debug mode. FIX: Kun tæl når mode != ST_DEBUG_OFF (st_logic_engine.cpp:100-103) (Build #1083) |
| BUG-191 | ST Debug: Ingen snapshot ved halt/error | ✅ FIXED | 🟠 MEDIUM | v5.3.0 | FEAT-008 bugfix: Når program haltede/fejlede under debugging blev ingen snapshot gemt → bruger kunne ikke se final state. FIX: Gem snapshot med REASON_HALT/REASON_ERROR (st_logic_engine.cpp:111-120) (Build #1083) |
| BUG-192 | Dobbelt-close af socket i https_close_fn | ✅ FIXED | 🔴 CRITICAL | v6.0.4 | else-grenen kaldte close(sockfd) men httpd lukker også → heap corruption. FIX: Fjernet close() i else-gren (https_wrapper.c:166) (Build #1126) |
| BUG-193 | Manglende null-terminering i upload buffer ved fuld kapacitet | ✅ FIXED | 🔴 CRITICAL | v6.0.4 | Når cli_upload_buffer_pos >= CLI_UPLOAD_BUFFER_SIZE → strlen() læser ud over buffer. FIX: else-gren null-terminerer ved [SIZE-1] (cli_shell.cpp:232,486) (Build #1126) |
| BUG-194 | URI routing med strstr("/source") giver falsk positiv | ✅ FIXED | 🟡 HIGH | v6.0.4 | strstr matchede /source_backup, /sources etc. FIX: Erstattet med suffix-check strcmp (api_handlers.cpp:708) (Build #1126) |
| BUG-195 | GPIO write API endpoint mangler pin-validering | ✅ FIXED | 🟡 HIGH | v6.0.4 | Kunne skrive til vilkårlige GPIO pins inkl. flash-pins (6-11). FIX: Validerer mod var_maps output-konfiguration (api_handlers.cpp:1240) (Build #1126) |
| BUG-196 | Hardkodede registeradresser i show logic stats | ✅ FIXED | 🟡 HIGH | v6.0.4 | Literal 252/260/268/276/284-292 antog 4 programmer. FIX: Bruger nu ST_LOGIC_*_REG_BASE konstanter (cli_show.cpp:2094-2122) (Build #1126) |
| BUG-197 | wifi_power_save config har ingen effekt | ✅ FIXED | 🟡 HIGH | v6.0.4 | esp_wifi_set_ps() blev aldrig kaldt med config-værdi. FIX: Tilføjet apply i config_apply.cpp (Build #1126) |
| BUG-198 | Manglende defaults for api_enabled og priority | ✅ FIXED | 🟠 MEDIUM | v6.0.4 | Ved første boot/migration var api_enabled=0 (disabled). FIX: Defaults api_enabled=1, priority=1 i config_struct/config_load/network_config (Build #1126) |
| BUG-199 | show config mangler sektionsfiltrering | ✅ FIXED | 🟠 MEDIUM | v6.0.4 | "show config wifi" virkede ikke - ingen section-parameter support. FIX: cli_cmd_show_config(section) med filter for WIFI/MODBUS/COUNTER/etc (cli_show.cpp, cli_parser.cpp) (Build #1126) |
| BUG-200 | Privat TLS-nøgle ikke beskyttet i .gitignore | ✅ FIXED | 🔴 CRITICAL | v6.0.4 | certs/prvtkey.pem kunne committes ved uheld. FIX: Tilføjet certs/prvtkey.pem og certs/*.key til .gitignore (Build #1126) |
| BUG-201 | ESP-IDF middle-wildcard URI routing matcher aldrig | ✅ FIXED | 🔴 CRITICAL | v6.0.5 | `httpd_uri_match_wildcard` understøtter kun `*` i ENDEN af URI. `/api/logic/*/source` matchede aldrig. FIX: Fjernet 8 middle-wildcard URIs, intern suffix-routing i wildcard handlers (http_server.cpp, api_handlers.cpp) (Build #1162) |
| BUG-202 | Source pool entries ikke null-termineret - strlen læser naboer | ✅ FIXED | 🔴 CRITICAL | v6.0.5 | `st_logic_get_source_code()` returnerer pointer i shared pool UDEN null-terminator. `strlen()` læste ind i efterfølgende programmer. FIX: Brug `prog->source_size`, opret null-termineret kopi (api_handlers.cpp) (Build #1162) |
| BUG-203 | /api/config returnerer ufuldstændig konfiguration | ✅ FIXED | 🟡 HIGH | v6.0.5 | Manglede modbus master, counter detaljer, timer detaljer, GPIO, ST Logic, modules, persistence. FIX: Komplet rewrite med alle sektioner matchende `show config` (api_handlers.cpp) (Build #1162) |
| BUG-204 | WWW-Authenticate header tabt pga. httpd response rækkefølge | ✅ FIXED | 🟡 HIGH | v6.0.5 | Header sat FØR `set_type`/`set_status` blev overskrevet. FIX: Flyttet til `api_send_error()` EFTER set_type/set_status men FØR sendstr (api_handlers.cpp) (Build #1162) |
| BUG-205 | API responses cached af browser - manglende Cache-Control | ✅ FIXED | 🟡 HIGH | v6.0.5 | Uden `Cache-Control` header cachede browsere API responses aggressivt. Efter reflash viste browser gammel data. FIX: `Cache-Control: no-store, no-cache, must-revalidate` på alle API svar (api_handlers.cpp) (Build #1162) |
| BUG-206 | /api/ trailing slash returnerer 404 | ✅ FIXED | 🔵 LOW | v6.0.5 | Kun `/api` var registreret, `/api/` gav 404. FIX: Tilføjet separat URI registration for `/api/` (http_server.cpp) (Build #1162) |
| BUG-207 | HTTP server stack_size 4096 for lille til API handlers | ✅ FIXED | 🔴 CRITICAL | v6.0.6 | Plain HTTP task stack kun 4096 bytes → POST `/api/logic/{id}/source` og `/bind` crasher ESP32 (stack overflow). HTTPS brugte 10240. FIX: Øget til 8192 + flyttet handler response buffere fra stack til heap (http_server.cpp:463, api_handlers.cpp) (Build #1196) |
| BUG-208 | GET /api/logic/{id}/stats stack buffer overflow | ✅ FIXED | 🟡 HIGH | v6.0.6 | `char buf[HTTP_JSON_DOC_SIZE]` (1024 bytes) på stack + JsonDocument (~700 bytes) overskrider 4096 stack. FIX: Flyttet buf til heap med malloc (api_handlers.cpp:1589) (Build #1196) |
| BUG-209 | GET /api/logic/{id}/source timeout - delvis data | ✅ FIXED | 🟡 HIGH | v6.0.6 | Content-Length sendes men kun delvis data modtages. Forårsaget af stack overflow (BUG-207). VERIFICERET FIXED efter stack_size øgning til 8192 (Build #1196) |
| BUG-210 | API source upload kompilerer ikke automatisk | ✅ FIXED | 🟡 HIGH | v6.0.6 | POST `/api/logic/{id}/source` kaldte kun `st_logic_upload()` men IKKE `st_logic_compile()`. FIX: Tilføjet `st_logic_compile()` kald efter upload (api_handlers.cpp:1084) (Build #1197) |
| BUG-211 | Parser håndterer ikke FUNCTION/FUNCTION_BLOCK definitioner i PROGRAM body | ✅ FIXED | 🔴 CRITICAL | v6.0.6 | `st_parser_parse_statement()` havde ingen case for FUNCTION/FUNCTION_BLOCK. FIX: (1) `st_parser_parse_program()` parser FUNCTION/FB mellem VAR og BEGIN, (2) valgfri BEGIN i funktionskrop, (3) funktionskald-som-statement support. Alle 11 FEAT-003 tests bestået (st_parser.cpp:1668-1704, 697-747) (Build #1224) |
| BUG-212 | Source pool null-terminering mangler i compiler path | ✅ FIXED | 🔴 CRITICAL | v6.0.6 | `st_logic_get_source_code()` returnerer pointer ind i shared pool UDEN null-terminator. Lexeren bruger '\0' til EOF-detektion → parser læser forbi kildekoden ind i tilstødende pool-data. Symptom: parse fejl med variabelnavne fra andre programmer. FIX: null-termineret kopi i `st_logic_compile()` + memory leak fix i `st_logic_delete()` (st_logic_config.cpp:240-260, 376-380) (Build #1224) |
| BUG-213 | GPIO hardware pins statisk - ignorerer modbus slave/master config | ✅ FIXED | 🟠 MEDIUM | v6.0.7 | `show config`, `show gpio` og `show gpio <pin>` viste hardkodede pins uanset enabled status. Counter pins (GPIO 19/25/27/33) vist selvom counters disabled. FIX: Alle 3 kommandoer bruger nu dynamisk visning baseret på enabled flags (modbus slave/master + counter) + faktisk hw_gpio config (cli_show.cpp:373-411, 2291-2333, 2399-2434) |
| BUG-214 | Backup: ST Logic source korruption pga. manglende null-terminering | ✅ FIXED | 🔴 CRITICAL | v6.0.7 | `api_handler_system_backup()` brugte `st_logic_get_source_code()` pointer direkte i ArduinoJson. Source pool entries er IKKE null-terminerede (BUG-212). ArduinoJson læste forbi program boundary → Program 0 (646 bytes) serialiseret som 5158 bytes (sammenklæbet med program 1-3). Ved restore: pool overflow → program 0+2 source tabt, program 1+3 korrupt. FIX: malloc null-termineret kopi med source_size (api_handlers.cpp:3371) (Build #1241) |
| BUG-215 | Restore: var_maps tabt pga. st_logic_delete() side-effect | ✅ FIXED | 🔴 CRITICAL | v6.0.7 | Under restore blev var_maps restored FØR logic_programs. `st_logic_delete()` (st_logic_config.cpp:411-415) clearer ALLE var_map entries med matchende st_program_id som side-effect. Da logic_programs restore kalder delete+upload, blev alle netop restored var_maps slettet. FIX: Flyttet var_maps restore sektion til EFTER logic_programs restore (api_handlers.cpp) (Build #1241) |
| BUG-216 | Backup: IP-adresser som rå uint32_t i stedet for dotted strings | ✅ FIXED | 🟠 MEDIUM | v6.0.7 | Network felter (static_ip, static_gateway, static_netmask, static_dns) blev serialiseret som rå uint32_t little-endian integers (fx 1677830336 = 192.168.1.100). Ikke menneskelæseligt, platform-afhængigt. FIX: Serialiseres nu som dotted strings + backward-kompatibel `parse_ip_field()` helper der håndterer begge formater ved restore (api_handlers.cpp:3188) (Build #1241) |
| BUG-217 | Backup: Boolean felter inkonsistente (int vs true/false) | ✅ FIXED | 🔵 LOW | v6.0.7 | `wifi_power_save`, `remote_echo`, `gpio2_user_mode` blev serialiseret som integers (0/1) i stedet for JSON boolean (true/false). FIX: Ternary operators for konsistent boolean output + `.as<bool>()` ved restore (api_handlers.cpp) (Build #1241) |
| BUG-218 | W5500 Ethernet boot-loop ved flash overflow | ✅ FIXED | 🔴 CRITICAL | v6.0.8 | `-DETHERNET_W5500_ENABLED` tilføjer ~38 KB → flash 97.1% overfyldt → boot-loop/crash. FIX: Custom partition table (app0 1.5MB vs 1.25MB) + ArduinoJson size flags. Flash nu 80.9% uden Ethernet, 83.3% med (platformio.ini, partitions.csv) |
| BUG-219 | Flash forbrug 97%+ forhindrer nye features | ✅ FIXED | 🔴 CRITICAL | v6.0.8 | Default partition (1.25 MB app) kun 37 KB fri → ingen plads til Ethernet eller andre features. FIX: Aktiveret custom partitions.csv (app0=1.5MB, +262 KB). Kræver `pio run -t erase` ved første upload (platformio.ini) |
| BUG-220 | Ethernet init afhængig af WiFi enabled | ✅ FIXED | 🟡 HIGH | v6.0.9 | `ethernet_driver_init()` kun kaldt fra `network_manager_connect()` som kræver WiFi enabled. Ethernet aldrig initialiseret når WiFi disabled. FIX: Ny `network_manager_start_ethernet()` + `network_manager_start_services()` funktioner, kaldt uafhængigt fra main.cpp. Services (HTTP/Telnet) startes FØR interfaces. W5500 MAC sat fra ESP32 eFuse (network_manager.cpp, main.cpp, ethernet_driver.cpp) |
| BUG-221 | `set wifi disable` blokeret af argc < 2 check | ✅ FIXED | 🟡 HIGH | v6.0.9 | `cli_cmd_set_wifi()` krævede `argc < 2` men `enable`/`disable` er standalone options uden value-parameter. FIX: Ændret til `argc < 1`, value er nu optional med fallback til tom string (cli_commands.cpp) |
| BUG-222 | `set logic interval 100` fejler med "Invalid program ID 0" | ✅ FIXED | 🟡 HIGH | v6.0.9 | Parseren kun understøttede kolon-syntax `set logic interval:100`. Mellemrum-syntax `set logic interval 100` (som `show config` genererer) faldt igennem til program-ID parser → atoi("interval")=0. FIX: Tilføjet mellemrum-variant + `set logic <id> enabled/disabled` + INTERVAL/DISABLED i normalize_alias (cli_parser.cpp) |
| BUG-223 | W5500 Ethernet ~1000ms ping latency | ✅ FIXED | 🔴 CRITICAL | v6.0.10 | ESP-IDF 4.4 W5500 driver RX task (`w5500_tsk`) bruger 1000ms timeout på `ulTaskNotifyTake()`. GPIO34 falling-edge ISR fyrer ikke pålideligt → RX task kun vågner ved timeout (~1s). Ping: 300-1000ms. FIX: `ethernet_driver_loop()` poller GPIO34 hvert loop-cycle, sender `xTaskNotifyGive()` direkte til `w5500_tsk` task når INT er LOW. Ping nu 2-5ms. SPI clock sænket fra 20→8 MHz for signalintegritet (ethernet_driver.cpp, tcp_server.cpp) |
| BUG-224 | Telnet character echo langsom over Ethernet | ✅ FIXED | 🟡 HIGH | v6.0.10 | TCP Nagle algorithm bufferede 1-byte telnet echo pakker i op til 200ms. FIX: `TCP_NODELAY` socket option sat på client accept (tcp_server.cpp:168) |
| BUG-225 | `sh config \| s telnet` viser ingen SET commands | ✅ FIXED | 🟠 MEDIUM | v6.0.10 | Sektionsfilter i `cli_cmd_show_config()` genkendte "NETWORK" og "WIFI" men IKKE "TELNET". Telnet SET commands nested under `show_network` → usynlige ved telnet-filtrering. FIX: Tilføjet `show_section_match(section, "TELNET")` til show_network filter (cli_show.cpp:69) |
| BUG-226 | Telnet config nested under WiFi — usynlig ved WiFi disabled | ✅ FIXED | 🟡 HIGH | v6.1.0 | Telnet SET commands og status var nested under `show_network`/WiFi enabled check. Når WiFi disabled → telnet config helt usynlig i `sh config`. FIX: Standalone `[TELNET]` sektion + `set telnet` kommandoer uafhængig af WiFi (cli_show.cpp, cli_commands.cpp, cli_parser.cpp) |
| BUG-227 | normalize_alias() mangler TELNET keyword | ✅ FIXED | 🟡 HIGH | v6.1.0 | `set telnet pass` → "SET: unknown argument" fordi "telnet" ikke normaliseres til "TELNET". FIX: Tilføjet `if (str_eq_i(s, "TELNET")) return "TELNET"` i normalize_alias() (cli_parser.cpp) |
| BUG-228 | Telnet banner viser "Telnet Server (v3.0)" i stedet for hostname | ✅ FIXED | 🔵 LOW | v6.1.0 | Banner viste hardkodet "=== Telnet Server (v3.0) ===" i stedet for hostname. FIX: Bruger nu `g_persist_config.hostname` + "Telnet Srv" (telnet_server.cpp) |
| BUG-229 | Telnet login bruger startup-kopi af credentials | ✅ FIXED | 🟡 HIGH | v6.1.0 | `telnet_server.cpp` brugte `server->network_config` (kopi fra startup). Credentials ændret via CLI blev aldrig brugt. FIX: Bruger nu `g_persist_config.network` direkte for live credentials (telnet_server.cpp) |
| BUG-230 | `sh config` over telnet trunkeret — kun [SYSTEM] vises | ✅ FIXED | 🔴 CRITICAL | v6.1.0 | Non-blocking TCP socket returnerer EAGAIN ved fuld send-buffer. Data silently droppet → kun første ~200 bytes (SYSTEM sektion) nåede frem. FIX: Retry-loop i `tcp_server_send()` med EAGAIN håndtering (tcp_server.cpp) |
| BUG-231 | TCP send retry blokerer main loop → 1s output bursts | ✅ FIXED | 🔴 CRITICAL | v6.1.0 | `delay(10)` i retry-loop blokerede main loop → `ethernet_driver_loop()` kørte ikke → W5500 RX task kun vågnet på 1000ms timeout → TCP ACKs forsinkede → output i 1-sekunds ryk. FIX: `vTaskDelay(1)` + direkte `xTaskNotifyGive()` til W5500 RX task under retries (tcp_server.cpp) |
| BUG-232 | `sh ethernet` viser "NOT CONNECTED" selvom fysisk link er oppe | ✅ FIXED | 🟡 HIGH | v6.2.0 | `ETHERNET_EVENT_CONNECTED` satte ikke `eth_state.state` → forblev IDLE. State kun sat til CONNECTED i `IP_EVENT_ETH_GOT_IP` (DHCP). FIX: Ny `link_up` flag + `ethernet_driver_has_link()` + `sh ethernet` viser nu Link og IP separat (ethernet_driver.cpp, ethernet_driver.h, cli_show.cpp) |
| BUG-233 | Statisk IP markerer aldrig state CONNECTED | ✅ FIXED | 🔴 CRITICAL | v6.2.0 | `ethernet_driver_set_static_ip()` satte `local_ip` men aldrig `state = CONNECTED`. State kun sat via DHCP `IP_EVENT_ETH_GOT_IP` event. Med DHCP OFF: permanent "Disconnected" trods gyldig statisk IP og link. FIX: Sæt `CONNECTED` i `set_static_ip()` når link er oppe + i `ETHERNET_EVENT_CONNECTED` når statisk IP er konfigureret (ethernet_driver.cpp) |
| BUG-234 | Netmask validering fejler pga. byte order (ntohl mangler) | ✅ FIXED | 🟡 HIGH | v6.2.0 | `network_config_is_valid_netmask()` lavede bit-contiguity check på network byte order (big-endian fra `inet_aton()`). På little-endian ESP32: `255.255.255.0` = `0x00FFFFFF` → `~mask+1` ikke power-of-2 → alle gyldige netmasks afvist. FIX: `ntohl()` konvertering før bit-check (network_config.cpp) |
| BUG-235 | Ethernet statisk IP genaktiveres ikke efter link-flap | ✅ FIXED | 🟡 HIGH | v6.2.0 | `ETHERNET_EVENT_DISCONNECTED` nulstillede `local_ip=0`. `ETHERNET_EVENT_CONNECTED` checkede `local_ip != 0` for statisk IP → aldrig gensat efter disconnect. FIX: Checker `static_ip != 0` og re-applyer fra static config (ethernet_driver.cpp) |
| BUG-236 | Heartbeat POST fanges af GPIO wildcard handler | ✅ FIXED | 🟠 MEDIUM | v6.3.0 | `/api/gpio/2/heartbeat` POST matchede `/api/gpio/*` wildcard fordi gpio_write registreredes FØR heartbeat. FIX: Heartbeat handlers registreres FØR GPIO wildcard i http_server.cpp (Build #1384) |
| BUG-237 | WiFi statisk IP virker ikke — kan ikke ping/API fra remote | ✅ FIXED | 🔴 CRITICAL | v7.1.1 | `wifi_driver_apply_static_ip()` blev aldrig kaldt. `set_static_ip()` gemte kun config, men DHCP blev aldrig stoppet og IP aldrig sat. FIX: Kald `apply_static_ip()` i `WIFI_EVENT_STA_CONNECTED` handler (wifi_driver.cpp) |
| BUG-238 | 74HC165 digital inputs altid HIGH på ES32D26 | ✅ FIXED | 🔴 CRITICAL | v7.1.1 | Pin-mapping i constants.h havde DATA (QH, pin 9) og LOAD (SH/LD, pin 1) byttet om: GPIO0 var defineret som DATA men er LOAD, GPIO15 var defineret som LOAD men er DATA. Koden læste LOAD-pinnen (altid HIGH pga pullup) som data. FIX: Rettet pin-mapping — `PIN_SR_IN_LOAD=0`, `PIN_SR_IN_DATA=15` (constants.h) |
| BUG-239 | Modbus Master runtime config ikke synkroniseret fra NVS ved boot | ✅ FIXED | 🔴 CRITICAL | v7.2.0 | `g_modbus_master_config` initialiseres med `.enabled=false` ved kompilering. `g_persist_config.modbus_master` (NVS) kopieres ALDRIG til runtime config ved boot. `modbus_master_send_request()` returnerer altid `MB_NOT_ENABLED` trods `modbus_mode=master` i NVS. Opdaget via DM56A04 display test: 10704 requests, 0 success, 0 timeout, 0 CRC → alle MB_NOT_ENABLED. FIX: Sync runtime fra persist i `modbus_master_init()` (modbus_master.cpp:48-55) (Build #1540) |
| BUG-240 | ST compiler heap-udmattelse pga oversized AST-noder + fragmentation | ✅ FIXED | 🟡 HIGH | v7.2.0 |
| BUG-241 | ST compile fejler via HTTP API pga heap-fragmentering fra keep-alive | ✅ FIXED | 🔴 CRITICAL | v7.3.0 | HTTP keep-alive connections (3 samtidige fra browser) fragmenterer heap → AST pool `malloc(120KB)` fejler trods 170KB fri. FIX: (1) `heap_caps_get_largest_free_block()` i stedet for `esp_get_free_heap_size()`, (2) try-decreasing allocation (512→256→128→64→32 nodes), (3) `lru_purge_enable=true` for auto-close idle connections, (4) JSON scope-blok i source_post handler frigør ArduinoJson FØR compile (st_parser.cpp, http_server.cpp, api_handlers.cpp) |
| BUG-242 | Tom parse error besked ved AST pool allokerings-fejl | ✅ FIXED | 🟡 HIGH | v7.3.0 | `st_parser_parse_program()` returnerede `NULL` uden at sætte `error_msg` når `ast_pool_init()` fejlede → CLI/API viste "Parse error: " uden forklaring. FIX: Tilføjet `parser_error()` kald ved pool init og program struct malloc fejl (st_parser.cpp:1704-1710) |
| BUG-243 | API /api/system/save gemmer ikke ST Logic programmer | ✅ FIXED | 🔴 CRITICAL | v7.3.0 | `POST /api/system/save` kaldte kun `config_save_to_nvs()` men manglede `st_logic_save_to_persist_config()` og `config_calculate_crc16()`. CLI `save` havde begge. ST programmer uploadet via API/web editor blev IKKE persisteret til NVS. FIX: Tilføjet begge kald i `api_handler_system_save()` (api_handlers.cpp:1195-1198) |
| BUG-244 | API /api/logic response mangler source_size felt | ✅ FIXED | 🟠 MEDIUM | v7.3.0 | `GET /api/logic` returnerede ikke `source_size` per program → web editor kunne ikke afgøre om et slot havde kildekode. FIX: Tilføjet `source_size` felt i logic list response (api_handlers.cpp:863) |
| BUG-245 | normalize_alias() mangler STATUS keyword | ✅ FIXED | 🟡 HIGH | v7.3.0 | `show status` virkede kun med uppercase `sh STATUS`. FIX: Tilføjet `STATUS`/`STAT` i normalize_alias() (cli_parser.cpp) | ST-programmer >~1300 bytes fejlede med "Memory allocation failed" trods 110 KB fri heap. Årsag: (1) `st_ast_node_t` var ~1920 bytes/node (function_def i union), (2) mange små malloc-kald fragmenterede heap, (3) compiler+bytecode duplikerede 18.5 KB. FIX (5 optimeringer): AST node 1920→140 bytes (function_def til pointer), AST pool-allokator (eliminerer fragmentation), direct bytecode output (sparer 10.5 KB malloc), compiler bytecode pointer (sparer 8 KB), parser early-free. Resultat: max 21→34 IF-blokke (+62%), min heap 47→80 KB (+70%). Filer: st_types.h, st_parser.cpp, st_compiler.h, st_compiler.cpp, st_logic_config.cpp (Build #1580) |
| BUG-246 | Web editor login hardcoded "api_user" default | ✅ FIXED | 🟠 MEDIUM | v7.3.1 | Login-formularen i `/editor` havde `value="api_user"` hardcoded — matchede ikke nødvendigvis konfigureret brugernavn. FIX: Tomt felt med placeholder, ingen fejlmeddelelse ved forkert login. Rettet til korrekt auth-test mod `/api/status` før modal lukkes (web_editor.cpp) |
| BUG-247 | Web editor API endpoints ikke registreret i HTTP server | ✅ FIXED | 🔴 CRITICAL | v7.3.1 | `/api/cli`, `/api/bindings`, `/api/bindings/*` var i v1_routes tabel men manglede `httpd_register_uri_handler()` i http_server.cpp → 405 Method Not Allowed. FIX: Tilføjet URI handler structs + registrering (http_server.cpp) |
| BUG-248 | CLI modbus master/slave kommandoer bruger Serial.print direkte | ✅ FIXED | 🟡 HIGH | v7.3.1 | `cli_commands_modbus_master.cpp` (24×) og `cli_commands_modbus_slave.cpp` (36×) brugte `Serial.printf()`/`Serial.println()` i stedet for `debug_printf()`/`debug_println()` → output vistes kun på hardware serial, ikke i web CLI eller telnet. FIX: Alle Serial.print → debug_printf/debug_println |
| BUG-249 | Binding delete bruger forkert index (var_maps vs filtreret liste) | ✅ FIXED | 🟡 HIGH | v7.3.1 | `GET /api/bindings` filtrerede non-ST bindings med `continue` men returnerede ikke globalt `var_maps` index → frontend sendte forkert index til `DELETE /api/bindings/{idx}` → "Not an ST variable binding" fejl. FIX: Tilføjet `index` felt i GET response med globalt var_maps index, fjernet restriktiv source_type check i DELETE (api_handlers.cpp, web_editor.cpp) |
| BUG-253 | SSE events forsinket/tabt pga. manglende TCP_NODELAY | ✅ FIXED | 🔴 CRITICAL | v7.5.0 | Nagle-algoritmen buffedreer små SSE-events (64-384 bytes) og ventede på TCP ACK før afsendelse → events forsinket eller tabt under load fra v7.3+ web features (dashboard, OTA, system). Node-RED viste "connected" men modtog ingen opdateringer. FIX: `TCP_NODELAY` sat på SSE klient-socket (sse_events.cpp:549) (Build #1700) |
| BUG-254 | SSE send timeout for aggressiv (5s) lukker forbindelse under load | ✅ FIXED | 🟡 HIGH | v7.5.0 | `SO_SNDTIMEO=5s` var for kort under belastning fra v7.3+ features (dashboard polling, OTA upload, web system). TCP send-buffer kunne ikke drænes i tide → `send()` returnerede -1 → SSE-forbindelse lukket silently. FIX: Øget SO_SNDTIMEO fra 5s til 30s (sse_events.cpp:553) (Build #1700) |
| BUG-250 | SSE subscribe=all overvåger kun HR 0-15, ignorerer coils/IR/DI | ✅ FIXED | 🔴 CRITICAL | v7.6.1 | `subscribe=all` uden eksplicitte adresse-params (hr/ir/coils/di) defaultede til kun HR 0-15. Coils, IR og DI blev aldrig scannet → ingen register-events for coils/DI. FIX: Ny `watch_all` mode i SseWatchList — allokerer SseWatchAllState (~1.5 KB) og scanner alle 256 HR + 256 IR + 256 coils + 256 DI (sse_events.cpp) |
| BUG-255 | SSE klient-task stack overflow risiko | ✅ FIXED | 🟡 HIGH | v7.5.0 | `init_buf[320]` på stack + `sse_send_event_fd` `buf[384]` = 704 bytes permanent stack-forbrug ved 5120 bytes total stack. Marginalt med dybere callstacks i v7.3+. FIX: `init_buf` flyttet til heap (malloc/free), task stack øget 5120→6144 bytes (sse_events.cpp:370,690) (Build #1700) |
| BUG-256 | SSE sse_sock_send behandler EAGAIN som fatal fejl | ✅ FIXED | 🟠 MEDIUM | v7.5.0 | Når TCP send-buffer var fuld returnerede `send()` -1 med `EAGAIN`, men koden behandlede det som fatal fejl → SSE-forbindelse lukket unødigt. FIX: Retry-loop med max 5 forsøg á 10ms delay ved EAGAIN/EWOULDBLOCK (sse_events.cpp:260-272) (Build #1700) |

## Feature Requests / Enhancements

| Feature ID | Title | Status | Priority | Target Version | Description |
|-----------|-------|--------|----------|----------------|-------------|
| FEAT-001 | `set reg STATIC` multi-register type support | ✅ DONE | 🟠 MEDIUM | v4.7.1 | Add DINT/DWORD/REAL support til persistent register configuration (Build #966) |
| FEAT-002 | ST Logic dynamisk pool allokering (8KB shared) | ✅ DONE | 🟡 HIGH | v4.7.1 | Erstat fixed 4KB arrays med 8KB shared pool - flexibel allokering (Build #944) |
| FEAT-003 | ST Logic custom FUNCTION/FUNCTION_BLOCK support | ✅ DONE | 🟠 MEDIUM | v6.0.6 | IEC 61131-3 FUNCTION/FUNCTION_BLOCK med VAR_INPUT, VAR_OUTPUT, VAR, lokale variable, nested calls, stateful FB instances. Parser, compiler (2-pass), VM (CALL_USER/RETURN/LOAD_PARAM/STORE_LOCAL) fuldt implementeret. 11/11 tests bestået (Build #1224) |
| FEAT-004 | ST Logic ARRAY type support | ✅ DONE | 🟡 HIGH | v7.9.0 | IEC 61131-3 ARRAY: `VAR arr: ARRAY[0..7] OF INT; END_VAR` med `arr[i]` read/write. Lexer ARRAY+`..` tokens, parser array decl+indexing, compiler LOAD_ARRAY/STORE_ARRAY opcodes, VM bounds-checked runtime. Arrays flattened til consecutive variable slots (max 24 elem). (st_types.h, st_lexer.cpp, st_parser.cpp, st_compiler.cpp, st_vm.cpp) |
| FEAT-005 | ST Logic STRING type support | ❌ OPEN | 🟠 MEDIUM | v6.0.0 | IEC 61131-3 STRING type med LEN(), CONCAT(), LEFT(), RIGHT(), MID() funktioner. Kræver: heap allocation, garbage collection overvejelser. Nyttigt til logging, protokol parsing |
| FEAT-006 | ST Logic TIME literal support | ✅ DONE | 🟠 MEDIUM | v5.2.0 | Native TIME literals: `T#5s`, `T#100ms`, `T#1h30m`. Lexer parser, gemmes som DINT millisekunder. (st_types.h, st_lexer.cpp, st_parser.cpp) |
| FEAT-007 | ST Logic inter-program variable sharing | ❌ OPEN | 🟡 HIGH | v5.2.0 | Deling af variable mellem Logic1-4 programmer. Implementering via shared memory pool eller GLOBAL_VAR deklarationer. Tillader modulær programmering |
| FEAT-008 | ST Logic debugging/single-step mode | ✅ DONE | 🔵 LOW | v5.3.0 | CLI: `set logic <id> debug pause/step/continue`, breakpoints, variable inspection. Build #1082, bugfixes Build #1083 (BUG-190, BUG-191). (st_debug.h, st_debug.cpp) |
| FEAT-009 | ST Logic STRUCT type support | ❌ OPEN | 🔵 LOW | v6.0.0 | Brugerdefinerede strukturer: `TYPE MyStruct: STRUCT x: INT; y: REAL; END_STRUCT END_TYPE`. Avanceret - lav prioritet |
| FEAT-010 | ST Logic program prioriteter/scheduling | ❌ OPEN | 🔵 LOW | v6.0.0 | Differenteret execution interval per program, interrupt-drevet high-priority execution. Nyttigt til real-time krav |
| FEAT-011 | HTTP REST API v6.0.0 | ✅ DONE | 🟡 HIGH | v6.0.0 | REST API med 20+ endpoints for counters, timers, logic, registers, GPIO, system control. (api_handlers.cpp) (Build #1108) |
| FEAT-012 | HTTPS/TLS support | ✅ DONE | 🟠 MEDIUM | v6.0.4 | esp_https_server component (httpd_ssl_start) med ECDSA P-256 certifikat embedded i flash. (https_wrapper.c) (Build #1126) |
| FEAT-013 | Dynamisk parser/compiler RAM-allokering | ✅ DONE | 🟡 HIGH | v6.0.4 | Parser/compiler malloc'd under kompilering, frigivet efter. Sparer ~12KB permanent RAM. Upload-buffer også dynamisk (~5KB). (st_logic_config.cpp, cli_shell.cpp) (Build #1126) |
| FEAT-014 | ST_LOGIC_MAX_PROGRAMS refactoring | ✅ DONE | 🟠 MEDIUM | v6.0.4 | Alle hardkodede 4-værdier erstattet med konstant. Module enable/disable flags. (constants.h, 10+ filer) (Build #1126) |
| FEAT-015 | Telnet IAC negotiation + ANSI-kompatibel history | ✅ DONE | 🟠 MEDIUM | v6.0.4 | Korrekt IAC WILL ECHO/SUPPRESS-GO-AHEAD ved connection. ANSI-fri line clearing for alle terminaler. (telnet_server.cpp) (Build #1126) |
| FEAT-016 | Show config sektionsfiltrering | ✅ DONE | 🟠 MEDIUM | v6.0.4 | "show config wifi/modbus/counters/http/..." viser kun relevant sektion. (cli_show.cpp, cli_parser.cpp) (Build #1126) |
| FEAT-017 | Config Backup/Restore via HTTP API + CLI | ✅ DONE | 🟡 HIGH | v6.0.7 | GET /api/system/backup download fuld JSON config inkl. passwords + ST Logic source. POST /api/system/restore upload JSON for fuld restore. CLI: show backup. (api_handlers.cpp, http_server.cpp, cli_show.cpp, cli_parser.cpp) |
| FEAT-018 | CLI ping kommando | ✅ DONE | 🟠 MEDIUM | v6.2.0 | `ping <ip> [count]` kommando i CLI. esp_ping session-baseret async med semaphore. Viser RTT per ping + statistik (sent/received/loss/min/avg/max). Virker over WiFi og Ethernet (wifi_driver.cpp, cli_commands.cpp, cli_parser.cpp) |
| FEAT-019 | Telnet Configuration API endpoint | ✅ DONE+TESTET | 🟡 HIGH | v6.3.0 | `GET/POST /api/telnet` — Telnet konfiguration via REST API. Testet 4/4 PASS (Build #1384) |
| FEAT-020 | ST Logic Debug API endpoints | ✅ DONE+TESTET | 🟡 HIGH | v6.3.0 | `POST /api/logic/{id}/debug/pause\|continue\|step\|breakpoint\|stop` + `GET .../debug/state` — Fuld debug kontrol via API. Testet 8/8 PASS (Build #1384) |
| FEAT-021 | Bulk Register Operations API | ✅ DONE+TESTET | 🟡 HIGH | v6.3.0 | `GET /api/registers/hr?start=0&count=100` + `POST /api/registers/hr/bulk` + IR/coils/DI bulk read/write. Testet 12/12 PASS (Build #1384) |
| FEAT-022 | Persistence Group Management API | ✅ DONE | 🟠 MEDIUM | v7.1.0 | `GET/POST/DELETE /api/persist/groups/{name}` + `/api/persist/save` + `/api/persist/restore` — fuld REST API for persistence groups |
| FEAT-023 | SSE Real-Time Events | ✅ DONE+TESTET | 🟠 MEDIUM | v7.0.3 | Raw TCP multi-klient SSE med CLI management. `set sse`/`show sse` config+status. Klient-registry med IP-tracking, `disconnect all\|<slot>`. Konfigurerbar watch HR/IR/Coils/DI. NVS-persisteret config (schema 12) |
| FEAT-024 | Hostname API endpoint | ✅ DONE+TESTET | 🟠 MEDIUM | v6.3.0 | `GET/POST /api/hostname` — Hostname ændring via REST API. Testet 3/3 PASS (Build #1384) |
| FEAT-025 | Watchdog Status API endpoint | ✅ DONE+TESTET | 🟠 MEDIUM | v6.3.0 | `GET /api/system/watchdog` — Reboot count, reset reason, heap stats, uptime. Testet 2/2 PASS (Build #1384) |
| FEAT-026 | GPIO2 Heartbeat Control API | ✅ DONE+TESTET | 🔵 LOW | v6.3.0 | `GET/POST /api/gpio/2/heartbeat` — Enable/disable heartbeat LED. BUG-236 fixed. Testet 3/3 PASS (Build #1384) |
| FEAT-027 | CORS Headers support | ✅ DONE+TESTET | 🟠 MEDIUM | v6.3.0 | `Access-Control-Allow-Origin: *` på alle API responses + OPTIONS preflight. Testet 3/3 PASS (Build #1384) |
| FEAT-028 | Request Rate Limiting | ✅ DONE | 🟠 MEDIUM | v7.1.0 | Token bucket rate limiter per klient IP (30 burst, 10/sec refill). Returnerer 429 Too Many Requests ved overbelastning |
| FEAT-029 | OpenAPI/Swagger Schema endpoint | ❌ OPEN | 🔵 LOW | v7.0.0 | `GET /api/schema` returnerer maskinlæsbar OpenAPI 3.0 spec — muliggør automatisk klient-kodegenerering (Python, JS, C#). Stort JSON dokument, kan kræve chunked response |
| FEAT-030 | API Versioning | ✅ DONE+TESTET | 🔵 LOW | v7.0.0 | `GET /api/version` + `/api/v1/*` dispatcher med URI rewriting. Alle eksisterende endpoints tilgængelige via v1 prefix. Backward-kompatibelt. Testet 32/32 PASS (Build #1389) |
| FEAT-031 | Firmware OTA via API | ✅ DONE | 🟡 HIGH | v7.5.0 | `POST /api/system/ota` chunked firmware upload, `GET /api/system/ota/status` progress polling, `POST /api/system/ota/rollback` rollback. Dual OTA partitions (ota_0/ota_1 á 1.625MB), boot validation via `esp_ota_mark_app_valid_cancel_rollback()`. Web UI på `/ota` med drag-drop upload, progress bar, rollback knap. Auth + rate limit beskyttet (Build #1699) |
| FEAT-032 | Prometheus Metrics endpoint | ✅ DONE | 🔵 LOW | v7.1.0 | `GET /api/metrics` i Prometheus text format — eksponerer: uptime, heap, HTTP stats, Modbus slave/master stats, SSE clients, WiFi/Ethernet, counters, timers, watchdog, firmware info |
| FEAT-033 | Request Audit Log | ❌ OPEN | 🔵 LOW | v7.0.0 | `GET /api/system/logs` — ringbuffer med sidste 50-100 API requests (timestamp, path, method, status, IP). Vigtig for fejlfinding og sikkerhedsovervågning. RAM-begrænset på ESP32 |
| FEAT-034 | ES32D26 Analog spændings-input (0-10V) | ❌ OPEN | 🟡 HIGH | v7.2.0 | 4x 0-10V analog input via onboard signal conditioning. Vi1=GPIO14 (ADC2), Vi2=GPIO33 (ADC1), Vi3=GPIO27 (ADC2), Vi4=GPIO32 (ADC1). Kræver: ADC kalibrering, skalering til engineering units, Modbus register mapping. OBS: ADC2 (Vi1/Vi3) virker IKKE med WiFi aktiv — kun ADC1 (Vi2/Vi4) er pålidelige |
| FEAT-035 | ES32D26 Analog strøm-input (4-20mA) | ❌ OPEN | 🟡 HIGH | v7.2.0 | 4x 4-20mA strøm-input via onboard shunt-modstand. Ii1=GPIO34, Ii2=GPIO39, Ii3=GPIO36, Ii4=GPIO35. Alle ADC1 — virker med WiFi. Kræver: ADC kalibrering, nul-punkt offset, skalering 4-20mA → engineering units, Modbus register mapping |
| FEAT-036 | ES32D26 Analog output (DAC 0-10V) | ❌ OPEN | 🟠 MEDIUM | v7.2.0 | 2x analog output via ESP32 DAC. AO1=GPIO25 (DAC1), AO2=GPIO26 (DAC2). Onboard signal conditioning 0-10V. Kræver: DAC driver, skalering, Modbus holding register mapping for output setpoint |
| FEAT-037 | ES32D26 Analog Modbus register mapping | ❌ OPEN | 🟡 HIGH | v7.2.0 | Mapping af analog I/O til Modbus registre: AI → Input Registers (skaleret INT/REAL), AO → Holding Registers. CLI: `set analog`, `show analog`. Konfigurerbar skalering, filtrering, polling interval |
| FEAT-038 | Modbus mode refaktor (single-transceiver) | ✅ DONE | 🟡 HIGH | v7.2.0 | ES32D26 deler én RS485 transceiver (GPIO1/3/21) mellem slave og master. `set modbus mode slave\|master\|off`. Bidirektionel sync med enabled flags. Conditional compilation via `MODBUS_SINGLE_TRANSCEIVER`. Master bruger `uart1_*` abstraktion på ES32D26 |
| FEAT-039 | AO mode config (DIP switch SW1) | ✅ DONE | 🟠 MEDIUM | v7.2.0 | ES32D26 AO1/AO2 hardware DIP switch mode konfiguration. `set ao1\|ao2 mode voltage\|current`. Config-infrastruktur for fremtidig DAC driver (FEAT-036) |
| FEAT-040 | Runtime UART selection | ✅ DONE | 🟡 HIGH | v7.2.0 | `set modbus slave\|master uart uart0\|uart1\|uart2` — runtime-konfigurerbar UART perifer for Modbus slave/master. Board-afhængige defaults (ES32D26: UART2, andre: UART1) |
| FEAT-041 | Hardware modul config (RS485 pins + Ethernet) | ✅ DONE | 🟡 HIGH | v7.2.0 | `set modul rs485 uart1\|uart2 tx <pin> rx <pin> dir <pin>` — konfigurerbare UART pins via ESP32 GPIO matrix. `set modul ethernet enable\|disable`. Schema 14 med pin config felter. Backup/restore support |
| FEAT-042 | Web-baseret ST Logic Editor | ✅ DONE | 🟡 HIGH | v7.3.0 | Embedded HTML/CSS/JS editor served at `/editor`. Program slot selector (1-4), code editor med line numbers, Tab/Ctrl+S, pool usage meter, ST keyword reference, compile error feedback, enable/disable/delete/save. Basic Auth login. Zero runtime RAM (PROGMEM). Bruger eksisterende `/api/logic/*` endpoints |
| FEAT-043 | Web CLI Console | ✅ DONE | 🟡 HIGH | v7.3.1 | Web-baseret CLI terminal i `/editor` (CLI panel). `POST /api/cli` — fanger CLI output i buffer-console og returnerer som JSON. Kommandohistorik (pil op/ned), monospace output, blokerer farlige kommandoer (reboot/defaults). Samme funktionalitet som telnet |
| FEAT-044 | Web Bindings Panel | ✅ DONE | 🟡 HIGH | v7.3.1 | Bindings-panel i `/editor` til oprettelse/sletning af ST variabel→Modbus register bindinger. Dropdown med kompilerede variabler, retning (input/output), registertype (HR/Coil/DI), adresse. `GET /api/bindings` + `DELETE /api/bindings/{idx}` endpoints |
| FEAT-045 | Web Runtime Monitor | ✅ DONE | 🟡 HIGH | v7.3.1 | Runtime monitor-panel i `/editor` med 1.5s auto-refresh. Viser execution count, timing (last/min/max µs), fejl, overruns + live variabel-tabel med aktuelle værdier. Auto-stop polling ved panel-skift |
| FEAT-046 | BIT_SET/BIT_CLR/BIT_TST ST Logic funktioner | ✅ DONE | 🟠 MEDIUM | v7.3.1 | Bit-manipulation builtins: `BIT_SET(value, bit)` → INT (sæt bit N), `BIT_CLR(value, bit)` → INT (clear bit N), `BIT_TST(value, bit)` → BOOL (test bit N). Bit position 0-15 for INT. Registreret i compiler, VM dispatcher og web editor sidebar |
| FEAT-047 | Web Dashboard med live Prometheus metrics | ✅ DONE | 🟡 HIGH | v7.3.1 | Dashboard forside served at `/` uden login. Parser `/api/metrics` Prometheus text format. 8 kort: System (uptime/heap/PSRAM), Netværk (WiFi/Ethernet/Telnet), Modbus Slave/Master stats, HTTP API stats, Tællere, Timere, ST Logic programmer. Auto-refresh 3s. Catppuccin Mocha dark theme. Navigation til `/editor` |
| FEAT-048 | Bindings redigering i Web Editor | ✅ DONE | 🟠 MEDIUM | v7.3.1 | Redigér-knap på hver binding i Bindings-panelet. Pre-udfylder formularen med eksisterende data (variabel, retning, register type, adresse). "Tilføj" skifter til "Opdatér" mode. Implementeret som delete+re-create via eksisterende API |
| FEAT-049 | Syntax highlighting i ST Editor | ✅ DONE | 🟠 MEDIUM | v7.3.1 | Farvelægning af ST keywords, kommentarer, strenge, tal og typer i code editoren via transparent textarea + farvet overlay |
| FEAT-050 | Auto-complete/IntelliSense i ST Editor | ✅ DONE | 🟠 MEDIUM | v7.3.1 | Popup med ST keywords, builtin funktioner og program-variabler mens man skriver. Tab/Enter til at indsætte, Esc til at lukke |
| FEAT-051 | Sparkline grafer i Runtime Monitor | ✅ DONE | 🟠 MEDIUM | v7.3.1 | Mini SVG-grafer der viser variabel-historik over tid i monitor-panelet. Ringbuffer med seneste 60 datapunkter per variabel |
| FEAT-052 | Single-step execution i ST Logic | ✅ DONE | 🟡 HIGH | v7.3.1 | Pause/Step/Resume kontrol i monitor-panelet. `POST /api/logic/{id}/step` endpoint. Kør ét cycle ad gangen til debugging |
| FEAT-053 | Register viewer i Dashboard | ✅ DONE | 🟡 HIGH | v7.3.1 | Grid-visning af alle Modbus holding registers og coils med live opdatering. Viser allokerings-ejer (counter/timer/ST/manual) |
| FEAT-054 | Historik-grafer i Dashboard | ✅ DONE | 🟠 MEDIUM | v7.3.1 | SVG trend-grafer med ringbuffer for heap, uptime, Modbus requests over seneste 5/15/60 min |
| FEAT-055 | Alarm/notifikationer i Dashboard | ✅ DONE | 🟠 MEDIUM | v7.3.1 | Konfigurerbare tærskler (heap < 30KB, CRC errors stigende) med visuel alarm-bar. Blinkende indikator ved overskridelse |
| FEAT-056 | Modbus register map i Dashboard | ✅ DONE | 🟡 HIGH | v7.3.1 | Visuel oversigt over alle allokerede Modbus registre — hvem ejer dem (counter/timer/ST/manual), address range og konflikter |
| FEAT-057 | Program backup/download | ✅ DONE | 🔵 LOW | v7.3.1 | Download ST kildekode som .st fil direkte fra browseren. Knap i editor toolbar |
| FEAT-058 | PLC Ladder diagram visning | ✅ DONE | 🟠 MEDIUM | v7.3.1 | Read-only ladder diagram genereret fra ST kildekode. Viser kontakter, coils, assignments og IF/THEN som visuelle ladder rungs |
| FEAT-059 | RBAC Multi-User System | ✅ DONE | 🟡 HIGH | v7.6.2 | Role-Based Access Control med op til 8 brugere. Roller: api, cli, editor, monitor (bitmask). Privilegier: read, write, read/write. Auth for HTTP API, SSE, CLI og Web UI. Schema migration v14→v15 med legacy single-user auto-migration. CLI: `set user`, `show users`, `delete user`, `show config rbac`. Backup/restore inkluderer RBAC data. Virtual admin (idx 99) for backward-kompatibilitet |
| FEAT-060 | Standalone Web CLI side | ✅ DONE | 🟠 MEDIUM | v7.6.1.1 | CLI udskilt fra ST Editor til selvstændig side på `/cli`. Fuld terminal med login, kommandohistorik (ArrowUp/Down), statusbar. Konsistent topnav: Monitor \| ST Editor \| CLI \| System |
| FEAT-061 | SSE subscribe=all watch_all mode | ✅ DONE | 🟠 MEDIUM | v7.6.1 | `subscribe=all` scanner nu alle 256 HR + 256 IR + 256 coils + 256 DI. Heap-allokeret SseWatchAllState (~1.5 KB pr. klient). BUG-260 fix |
| FEAT-062 | SSE klient monitor udvidelse | ✅ DONE | 🔵 LOW | v7.6.2 | `show sse` viser nu username, IP, subscribed topics og formateret uptime per klient. SseClientInfo udvidet med username[24] og topics felter |
| BUG-260 | SSE subscribe=all overvågede kun HR 0-15 | ✅ FIXED | 🟡 HIGH | v7.6.1 | `subscribe=all` uden eksplicitte adresser defaultede til HR 0-15 — coils, IR og DI blev ignoreret. FIX: watch_all mode med fuld-range scanning |
| BUG-261 | show config RBAC viste tom sektion | ✅ FIXED | 🔵 LOW | v7.6.2 | `show config rbac` matchede ikke noget filter — RBAC sektionen brugte `show_http` guard. FIX: Selvstændig `show_rbac` filter med match på "RBAC" og "USER" |
| BUG-262 | set RBAC kommando ikke genkendt | ✅ FIXED | 🔵 LOW | v7.6.2 | `set rbac ?` returnerede "unknown argument". FIX: Tilføjet RBAC alias til normalize_alias() → mapper til USER |
| FEAT-063 | Web User Badge + Login/Logout | ✅ DONE | 🟠 MEDIUM | v7.6.2.2 | User badge i topnav højre hjørne på alle 4 web-sider. Viser brugernavn, grøn/rød status-dot, dropdown med roller, privilegier, auth mode. Log ind/ud funktionalitet. Dashboard: login modal (skjult default) med "Log ind" knap. CLI/Editor/System: updateUserBadge() efter login. `/api/user/me` endpoint + `show user` CLI kommando |
| FEAT-064 | /api/user/me API endpoint | ✅ DONE | 🟠 MEDIUM | v7.6.2.1 | REST endpoint returnerer authenticated status, username, roles, privilege, mode (legacy/rbac). Bruges af web user badge til at vise brugerinfo |
| FEAT-065 | show user CLI kommando | ✅ DONE | 🔵 LOW | v7.6.2.1 | `show user` viser aktuel session info: interface, RBAC status, antal brugere, auth mode |
| FEAT-066 | SSE klient management i web System | ✅ DONE | 🟠 MEDIUM | v7.6.2.3 | SSE sektion på `/system` side: tabel med slot, IP, bruger, topics, uptime. Afbryd individuelle klienter eller alle. API: `GET /api/events/clients`, `POST /api/events/disconnect` med `{slot: N}` eller `{slot: -1}` for alle |
| FEAT-069 | SSE konfiguration i backup/restore | ✅ DONE | 🟠 MEDIUM | v7.6.2.7 | Backup eksporterer SSE indstillinger (enabled, port, max_clients, check_interval_ms, heartbeat_ms) som selvstændig `sse` JSON sektion. Restore gendanner SSE config fra backup fil |
| FEAT-070 | Async Modbus Master (non-blocking) | ✅ DONE | 🔴 CRITICAL | v7.7.0 | Dedikeret FreeRTOS task (Core 0, 4KB stack) til Modbus UART I/O. ST builtins er nu non-blocking: reads returnerer cached værdi og køer refresh, writes køes i baggrunden. Nye builtins: `MB_SUCCESS()`, `MB_BUSY()`, `MB_ERROR()`. 32-entry cache, 16-deep queue, request deduplication. Eliminerer overruns ved Modbus-operationer i ST Logic |
| FEAT-072 | Web Monitor: Modbus RTU trafikmonitor | ✅ DONE | 🟡 HIGH | v7.8.0 | Live req/3s rate, success rate, CRC fejl, timeouts, exceptions, sparkline grafer for slave+master |
| FEAT-073 | Web Monitor: Modbus Master status | ✅ DONE | 🟡 HIGH | v7.8.0 | Async cache stats (hits/misses/entries/hit rate), per-slave cache status tabel, queue full count |
| FEAT-074 | Web Monitor: Ethernet/WiFi signalkvalitet | ❌ OPEN | 🟠 MEDIUM | v8.0.0 | RSSI over tid (WiFi), link speed, pakketab, reconnect-historik |
| FEAT-075 | Web Monitor: TCP forbindelsesmonitor | ✅ DONE | 🔵 LOW | v7.9.6.7 | Dashboard kort: SSE klient-tabel (IP/bruger/topics/uptime), Telnet IP+bruger+uptime via Prometheus, HTTP aggregate stats. TcpClient.connected_ms tilføjet |
| FEAT-076 | Web Monitor: Bandwidth monitor | ❌ OPEN | 🔵 LOW | v8.0.0 | Bytes sendt/modtaget over tid (Modbus + HTTP + SSE) |
| FEAT-077 | Web Monitor: Heap fragmentation graf | ❌ OPEN | 🟡 HIGH | v8.0.0 | Largest free block vs. total free — viser fragmentering over tid, fanger memory leaks tidligt |
| FEAT-078 | Web Monitor: FreeRTOS task monitor | ✅ DONE | 🟠 MEDIUM | v7.8.0 | Task count, heap fragmentation (largest block vs free), stack HWM for kendte tasks (loopTask, mb_async, IDLE0/1) |
| FEAT-079 | Web Monitor: FreeRTOS task monitor | ❌ OPEN | 🟠 MEDIUM | v8.0.0 | FreeRTOS task liste med stack high-water mark, prioritet, state |
| FEAT-080 | Web Monitor: Boot-historik | ❌ OPEN | 🟡 HIGH | v8.0.0 | Antal reboots, reset-årsager (watchdog, brownout, panic, OTA), sidste 10 boot timestamps |
| FEAT-081 | Web Monitor: NVS brug | ❌ OPEN | 🟠 MEDIUM | v8.0.0 | Brugt/ledig plads i NVS, antal entries, write-count (flash wear indikator) |
| FEAT-082 | Web Monitor: SPIFFS/LittleFS brug | ❌ OPEN | 🔵 LOW | v8.0.0 | Filsystem kapacitet, brugt plads, filantal |
| FEAT-083 | Web Monitor: ESP32 intern temperatur | ❌ OPEN | 🔵 LOW | v8.0.0 | Chip junction temperatur via intern sensor |
| FEAT-084 | Web Monitor: Spændingsmonitor | ❌ OPEN | 🔵 LOW | v8.0.0 | VCC/3.3V rail via ADC, brownout detection events |
| FEAT-085 | Web Monitor: Alarm-historik/log | ✅ DONE | 🟡 HIGH | v7.8.0 | 32-entry ringbuffer med auto-detection (heap, CRC, timeout, auth, overrun). API: GET /api/alarms + POST /api/alarms/ack. Dashboard kort med severity-farver og kvittér-knap |
| FEAT-086 | Web Monitor: Event log viewer | ❌ OPEN | 🟠 MEDIUM | v8.0.0 | Systemhændelser: config ændringer, reboot, OTA, login-forsøg |
| FEAT-087 | Web Monitor: Watchdog-statistik | ❌ OPEN | 🟠 MEDIUM | v8.0.0 | Antal watchdog resets, hvilken task triggered, tid siden sidste feed |
| FEAT-088 | Web Monitor: Threshold-alarmer | ❌ OPEN | 🟠 MEDIUM | v8.0.0 | Konfigurerbare grænseværdier for vilkårlige registre med visuel alarm |
| FEAT-089 | Web Monitor: Register change-log | ❌ OPEN | 🟠 MEDIUM | v8.0.0 | Historik over registerændringer med tidsstempler (hvem ændrede hvad) |
| FEAT-090 | Web Monitor: Modbus request log / sniffer | ❌ OPEN | 🟡 HIGH | v8.0.0 | Live visning af indkomne Modbus requests (funktion, adresse, data) |
| FEAT-091 | Web Monitor: ST Logic profiler | ❌ OPEN | 🟠 MEDIUM | v8.0.0 | Cycle time histogram, worst-case execution, variable trace over tid |
| FEAT-092 | Web Monitor: GPIO pulse monitor | ❌ OPEN | 🔵 LOW | v8.0.0 | Pulsbredde, frekvens, duty cycle for digitale inputs |
| FEAT-093 | Web Monitor: Uptime-graf | ❌ OPEN | 🔵 LOW | v8.0.0 | Visuelt overblik over oppetid med markering af nedetid/reboot |
| FEAT-094 | Web Monitor: Analog input trending | ❌ OPEN | 🟡 HIGH | v8.0.0 | 0-10V og 4-20mA inputs med trendgrafer og min/max/avg (afhænger af FEAT-034/035) |
| FEAT-095 | Web Monitor: Digital I/O dashboard | ✅ DONE | 🟠 MEDIUM | v7.8.0 | Visuelle LED-indikatorer for IN1-IN8 (input) og CH1-CH8 (output) med klik-toggle på outputs. Auto-skjules uden shift register HW |
| FEAT-096 | Web Monitor: RS-485 bus health | ❌ OPEN | 🟠 MEDIUM | v8.0.0 | Bus-fejlrate, collision detection, bus idle/busy ratio |
| FEAT-097 | Web Monitor: Customizable dashboard | ❌ OPEN | 🔵 LOW | v8.0.0 | Brugeren kan vælge/arrangere widgets (drag & drop), gem layout i NVS |
| FEAT-098 | Web Monitor: Register-map visualizer | ❌ OPEN | 🟠 MEDIUM | v8.0.0 | Grafisk oversigt over hele register-mappen med farvekodning efter type/binding |
| FEAT-099 | Web Monitor: Trend recorder med CSV-eksport | ❌ OPEN | 🟡 HIGH | v8.0.0 | Optag vilkårlige registre over tid, eksportér som CSV til commissioning/debugging |
| FEAT-100 | Web Monitor: Mini-SCADA view | ❌ OPEN | 🔵 LOW | v8.0.0 | Simpel process-visualisering med symboler for pumper, ventiler, motorer |
| FEAT-101 | NTP tidssynkronisering | ✅ DONE | 🟡 HIGH | v7.8.1 | ESP-IDF SNTP klient med konfigurerbar server, POSIX tidszone, sync interval. CLI: `set ntp`, `show ntp`/`show time`. API: `GET/POST /api/ntp`. Prometheus metrics: ntp_synced, ntp_epoch, ntp_sync_count. Dashboard NTP kort med live tid. Alarm-log med real-time tidsstempler. Schema 15→16 |
| BUG-263 | Web user badge viste altid "Ikke logget ind" | ✅ FIXED | 🟡 HIGH | v7.6.2.3 | Tre separate fejl: (1) `/api/user/me` manglede direkte httpd URI handler — var kun i v1_routes, så `fetch('/api/user/me')` gav 404 (kun `/api/v1/user/me` virkede). (2) Dashboard manglede login modal. (3) `updateUserBadge()` blev ikke kaldt efter login. FIX: Tilføjet `uri_user_me` httpd registrering, login modal på dashboard, updateUserBadge() kald efter login i alle 4 sider |
| BUG-264 | SSE afviste brugere med API rolle (403) | ✅ FIXED | 🟡 HIGH | v7.6.2.4 | SSE rolle-check krævede kun `ROLE_MONITOR` (0x08). Brugere med `api` rolle (0x01) fik 403 "MONITOR role required" selvom de var autentificeret korrekt. FIX: Ændret check til `ROLE_MONITOR \| ROLE_API` — begge roller giver SSE-adgang. Tilføjet RBAC Authentication sektion i `show sse` output |
| BUG-265 | API write endpoints tjekker ikke privilege | ✅ FIXED | 🔴 CRITICAL | v7.6.2.4 | Alle 42 POST/DELETE endpoints brugte `CHECK_AUTH` som kun tjekker authentication — ikke privilege. Read-only brugere (`privilege read`) kunne lave skrive-operationer (reboot, save, delete, config-ændringer). FIX: 42 write-handlers ændret til `CHECK_AUTH_WRITE` som returnerer 403 "Write privilege required" |
| BUG-266 | rbac_parse_privilege("write") returnerede PRIV_RW | ✅ FIXED | 🟠 MEDIUM | v7.6.2.4 |
| BUG-267 | Web system write-knapper viste ikke fejl ved 403 | ✅ FIXED | 🟠 MEDIUM | v7.6.2.5 |
| BUG-268 | Confirm dialog callback blev aldrig kaldt | ✅ FIXED | 🔴 CRITICAL | v7.6.2.6 | `confirmAction()` kaldte `closeConfirm()` som satte `pendingConfirmFn=null` FØR callback-tjekket. Alle handlinger bag bekræftelsesdialog (Fabriksindstillinger, Genstart uden gem, Afbryd alle SSE, Restore config) viste dialogen men gjorde ingenting ved "Bekræft". FIX: Gem fn i lokal variabel før closeConfirm() | `doReboot()` brugte `.catch(()=>{})` — slugte fejl silently. Brugeren så "Genstarter..." men intet skete ved manglende privilege. Alle write-funktioner (reboot, save, load, defaults) viser nu 403 "Ingen skriveadgang" fejl korrekt | `strcasestr(str, "write")` matchede i `rbac_parse_privilege()` før "read/write" blev tjekket — så `privilege write` blev til `PRIV_RW` (read+write). FIX: Tjek "read/write" og "rw" først, derefter "write" alene → `PRIV_WRITE` |
| BUG-250 | PSRAM detection mangler i CLI og metrics | ✅ FIXED | 🔵 LOW | v7.3.1 | `show version` og `show status` viste ikke PSRAM info. `/api/metrics` manglede `esp32_psram_total_bytes` og `esp32_psram_free_bytes`. FIX: Tilføjet PSRAM detection med `ESP.getPsramSize()`/`ESP.getFreePsram()` til begge CLI kommandoer og Prometheus endpoint |
| BUG-251 | `/api/metrics` kræver unødvendig auth | ✅ FIXED | 🟠 MEDIUM | v7.3.1 | Metrics endpoint krævede Basic Auth → dashboard og Prometheus scrapers kunne ikke hente data uden credentials. FIX: Fjernet `CHECK_AUTH()`, beholdt `CHECK_API_ENABLED()` og rate limit. Read-only data, ingen sikkerhedsrisiko |
| BUG-252 | Web editor/system login tabt ved navigation mellem moduler | ✅ FIXED | 🟡 HIGH | v7.3.2 | `AUTH` variabel (in-memory) nulstillet ved hvert page load → brugeren skulle logge ind igen ved skift mellem `/editor` og `/system`. FIX: Auth token gemt i `sessionStorage` (`hfplc_auth` key), auto-verificeret ved page load. Ryddes ved 401 eller tab-lukning (web_editor.cpp, web_system.cpp) |
| BUG-290 | VAR initial values ignoreret af compiler | ✅ FIXED | 🔴 CRITICAL | v7.7.1.1 | `INPUTTIMER : INT := 120` blev altid 0. Compiler satte `variables[i].int_val = 0` hardkodet. FIX: Symbol table bærer nu `initial_value` fra parser → compiler → bytecode. (st_compiler.h, st_compiler.cpp) |
| BUG-291 | VAR initial values tabt ved boot (bytecode cache) | ✅ FIXED | 🔴 CRITICAL | v7.7.1.1 | Cached bytecode (`.bc` filer) gemte ikke initial values. Ved boot satte `st_bytecode_load()` alle variabler til 0. FIX: Bytecode persist format v2 — gemmer/loader `var_initial[]` per variabel. Version mismatch trigger automatisk recompile. (st_bytecode_persist.cpp, st_types.h) |
| BUG-292 | Telnet auth bruger ikke RBAC | ✅ FIXED | 🟡 HIGH | v7.7.1.1 | Telnet brugte kun lokale telnet credentials, ikke RBAC brugerdatabase. FIX: Når `rbac.enabled=true` bruger telnet `rbac_authenticate()`. Fallback til lokal user/pass når RBAC disabled. `show config telnet` viser auth mode. (telnet_server.cpp, cli_show.cpp) |
| BUG-293 | mb_async_init() crash på ES32D26 i SLAVE mode | ✅ FIXED | 🔴 CRITICAL | v7.7.1.1 | `g_modbus_master_config.enabled` var true i NVS selvom master ikke initialiseret (SLAVE mode). Guard checkede forkert flag → crash ved "Sub". FIX: `#if MODBUS_SINGLE_TRANSCEIVER` checker nu `mb_mode == MODBUS_MODE_MASTER` direkte. (main.cpp) |
| BUG-294 | Web editor manglede BEGIN/END/EXPORT keywords | ✅ FIXED | 🟠 MEDIUM | v7.7.1.1 | Autocomplete kendte ikke BEGIN, END, EXPORT. Modbus Master funktioner ufuldstændige (manglede MB_READ_COIL, MB_READ_INPUT_REG, MB_SUCCESS, MB_BUSY, MB_ERROR). FIX: Opdateret ST_KW og ST_FN lister + tilføjet Modbus Master hjælpe-sektion. (web_editor.cpp) |
| BUG-295 | Version+build ikke vist i web System/Administration | ✅ FIXED | 🟠 MEDIUM | v7.7.1.1 | `/api/status` manglede `firmware` felt. Web UI overskrev firmware med bare version. OTA status brugte ESP-IDF app version. FIX: Tilføjet `firmware: "v7.7.1.1.XXXX"` i API + konsistent visning i web UI og OTA. (api_handlers.cpp, web_system.cpp, ota_handler.cpp) |
| BUG-296 | ST parser max 4 function arguments — CTUD/SCALE/CNT fejler | ✅ FIXED | 🔴 CRITICAL | v7.7.2 | AST struct `args[4]` og parser `< 4` check begrænsede alle funktionskald til max 4 args. CTUD(5 args), SCALE(5 args) og nye CNT_SETUP(6 args) kunne aldrig kompilere. FIX: `args[4]→args[8]`, parser limit 4→8 (st_types.h:229, st_parser.cpp:367+385+761+768) |
| BUG-297 | Web dashboard register map viser forkerte counter-adresser | ✅ FIXED | 🟡 HIGH | v7.7.2 | Register allokering brugte `(id-1)*4` fra HR0 (Counter 1→HR0-3). Korrekt er base=100+(id-1)*20 stride 20. Counter 1 er HR100-114, ikke HR0-3. FIX: Korrekt base+offset for value/raw/freq/ctrl/compare (web_dashboard.cpp:569-580) |
| FEAT-102 | Web Monitor: Udvidet alarm historik | ✅ DONE | 🟡 HIGH | v7.8.3.1 | Alarm-kort spænder 2 kolonner, 25 entries (op fra 15), severity-kolonne (KRIT/ADV/INFO), beriget Kilde/Detaljer med farvekodede IP/brugernavn (auth) og Modbus kommunikationsfejl med slave ID + register adresse. Scrollbar max 320px. Alarm-beskeder: Master `TX -> ID:X @Y`, Slave `RX <- ID:X`. Backend tracker `last_error_slave_id`/`last_error_address` per fejlet request (types.h, modbus_master.cpp, api_handlers.cpp, web_dashboard.cpp) |
| FEAT-103 | Web Monitor: Modbus Master/Slave interface info | ✅ DONE | 🟡 HIGH | v7.8.3.1 | Interface-række i card body: "ID:1 Baud:115K Data:8 Parity:None Stop:1" (slave) / "Baud:115K Data:8 Parity:None Stop:1" (master). Badge viser Deaktiveret/Aktiv/Idle. Nye Prometheus metrics: modbus_slave_config_enabled/id/baudrate/parity/stopbits, modbus_master_config_enabled/baudrate/parity/stopbits (api_handlers.cpp, web_dashboard.cpp) |
| FEAT-104 | Web Monitor: NTP tid følger ESP32 tidszone | ✅ DONE | 🟠 MEDIUM | v7.8.3.1 | Dashboard NTP-kort bruger nu `local_time` fra ESP32 `/api/ntp` i stedet for browserens tidszone via `new Date()`. Tiden følger NTP timezone-konfigurationen (fx CEST). (web_dashboard.cpp) |
| FEAT-105 | NTP Tidszone dokumentation | ✅ DONE | 🔵 LOW | v7.8.3.1 | `docs/NTP_TIMEZONE_GUIDE.md` — POSIX TZ-format forklaring, kopiér-og-indsæt tidszoner for 30+ lande, NTP servere, fejlfinding, CLI+API eksempler |
| BUG-298 | Alarm historik auth failures viste 0.0.0.0 som klient-IP | ✅ FIXED | 🟡 HIGH | v7.8.3.2 | `getpeername()` blev kaldt EFTER `httpd_resp_sendstr()` — socket kunne være lukket/genbrugt → 0.0.0.0. Derudover brugte koden `sockaddr_in` men ESP-IDF lwIP kan bruge IPv6-kompatible sockets (IPv4-mapped `::ffff:x.x.x.x`). FIX: IP+username opslag flyttet til FØR response sendes + håndtering af både AF_INET og AF_INET6 (api_handlers.cpp:282-326) |
| BUG-299 | Alarm historik opdaterings-interval for langsomt (10s) | ✅ FIXED | 🔵 LOW | v7.8.3.2 | `fetchAlarms()` kørte hvert 10. sekund mens metrics opdateredes hvert 3. sekund → alarmer virkede forsinkede. FIX: Sænket interval fra 10s til 5s (web_dashboard.cpp) |
| FEAT-106 | Web Monitor: Udvidet Netværk kort med IP/DNS/gateway | ✅ DONE | 🟡 HIGH | v7.8.4.1 | Netværk-kort viser nu hostname, WiFi IP/mask/gateway/DNS/SSID, Ethernet IP/mask/gateway/DNS/MAC/speed. Henter data fra `/api/hostname`, `/api/wifi`, `/api/ethernet` hvert 10. sekund. SSE info flyttet til HTTP API kort (web_dashboard.cpp) |
| FEAT-107 | Web Monitor: SSE info sammenlagt med HTTP API kort | ✅ DONE | 🟠 MEDIUM | v7.8.4.1 | SSE server status (aktiv/deaktiveret, port, klienter, max klienter, heartbeat) flyttet fra Netværk-kort til HTTP API kort som separat sektion. Henter data fra `/api/events/status` (web_dashboard.cpp) |
| FEAT-108 | Web Monitor: Drag-and-drop kort-sortering med NVS persistens | ✅ DONE | 🟠 MEDIUM | v7.8.4.2 | Cards i monitor dashboard kan trækkes og omarrangeres via drag-and-drop. Rækkefølge gemmes på ESP32 via `POST /api/dashboard/layout` og hentes ved load via `GET /api/dashboard/layout`. Persisteres i NVS som del af PersistConfig (schema 17). Hvert card har drag-håndtag (≡) i overskriften (types.h, config_load.cpp, api_handlers.cpp, web_dashboard.cpp) |
| FEAT-109 | Global Save-knap i topnav på alle sider | ✅ DONE | 🟡 HIGH | v7.8.4.2 | Grøn "💾 Save"-knap i top-navigation på ALLE 4 sider (Monitor, ST Editor, CLI, System). Kalder `POST /api/system/save` for at gemme al konfiguration til NVS inkl. dashboard kort-rækkefølge. Visuelt feedback: grøn→orange (gemmer)→checkmark (gemt)/rød (fejl). (web_dashboard.cpp, web_system.cpp, web_editor.cpp, web_cli.cpp) |
| FEAT-110 | SSE server mættet alarm + Write denied alarm | ✅ DONE | 🟠 MEDIUM | v7.8.4.2 | Alarm (ADV) når SSE server mættet + alarm (ADV) ved 403 "Write privilege denied" med klient-IP/brugernavn. `docs/ALARM_SYSTEM_GUIDE.md` dokumentation (api_handlers.cpp) |
| FEAT-111 | Register Map: Sticky allokerings-reference | ✅ DONE | 🟡 HIGH | v7.8.4.2 | Register Allokering + Allokerings-tabel slået sammen i sticky header der følger med ved scroll i register-vinduer. Legende + tabel altid synlig som reference (web_dashboard.cpp) |
| BUG-300 | Dashboard kort-rækkefølge tabt ved reboot | ✅ FIXED | 🟡 HIGH | v7.8.4.2 | `GET /api/dashboard/layout` krævede auth (CHECK_AUTH) men restoreCardOrder() sendte ingen auth ved page load. FIX: Ændret til CHECK_API_ENABLED (ingen auth krævet for UI-præference) + robust tom-streng håndtering i JS (api_handlers.cpp, web_dashboard.cpp) |
| BUG-301 | Telnet RBAC: Mangler ROLE_CLI check ved login | ✅ FIXED | 🔴 CRITICAL | v7.8.4.4 | Bruger uden CLI-rolle kunne logge ind via Telnet. FIX: `rbac_has_role(uid, ROLE_CLI)` check efter autentificering, afviser med "User does not have CLI role" (telnet_server.cpp) |
| BUG-302 | Telnet RBAC: Mangler rbac_cli_allowed() ved kommando | ✅ FIXED | 🔴 CRITICAL | v7.8.4.4 | Read-only Telnet-bruger kunne køre alle kommandoer (set, reboot, etc.). FIX: `rbac_cli_allowed()` check før `cli_shell_execute_command()` — read-only brugere begrænset til show/help/ping/who (telnet_server.cpp) |
| BUG-303 | Web CLI: Mangler ROLE_CLI check | ✅ FIXED | 🟡 HIGH | v7.8.4.4 | `POST /api/cli` brugte kun `CHECK_AUTH_WRITE` — bruger med api+write men uden cli-rolle kunne køre CLI-kommandoer. FIX: Ændret til `CHECK_AUTH_ROLE(req, ROLE_CLI)` + separat write-privilege check (api_handlers.cpp) |
| BUG-304 | show config: Inkonsistent RBAC-visning på tværs af interfaces | ✅ FIXED | 🟠 MEDIUM | v7.8.4.4 | HTTP viste "RBAC active" men Telnet viste altid legacy credentials og SSE manglede auth-info helt. FIX: Ensrettet alle 3 sektioner i både table- og script-format med samme stil: `auth: RBAC (N users)` + `auth-mode: RBAC (multi-user)` (cli_show.cpp) |
| FEAT-112 | ST Logic Cold Restart (Reinit) | ✅ DONE | 🟡 HIGH | v7.8.4.5 | IEC 61131-3 cold restart: nulstiller program-variabler til kompilerede VAR-initialværdier, resetter stateful builtins (TON/TOF, R_TRIG/F_TRIG, CTU/CTD), FB-instanser og statistik. CLI: `set logic <id> reinit`, API: `POST /api/logic/{1-4}/reinit`, Web: orange "Reinit"-knap i ST Editor toolbar. Bruger eksisterende `var_initial[32]` array fra bytecoden. (st_logic_config.cpp, cli_commands_logic.cpp, cli_parser.cpp, api_handlers.cpp, web_editor.cpp) |
| FEAT-113 | Modbus baudrate 2400/4800 support | ✅ DONE | 🟠 MEDIUM | v7.8.4.5 | Tilføjet 2400 og 4800 baud til både master og slave baudrate-validering. Før: kun 9600-115200. Nu: 2400, 4800, 9600, 19200, 38400, 57600, 115200. t3.5 inter-frame delay beregnes korrekt for lave baudrater. (cli_commands_modbus_master.cpp, cli_commands_modbus_slave.cpp) |
| FEAT-114 | Modbus Master cache age human-readable | ✅ DONE | 🔵 LOW | v7.8.4.5 | Cache entry alder vises nu som `60ms`, `23.0s`, `1.5m` i stedet for rå millisekunder. CLI: Age-kolonne i `show modbus master`. Web dashboard: ny "Age" kolonne i cache-tabel via `age_ms` label i Prometheus-metrik. (cli_commands_modbus_master.cpp, api_handlers.cpp, web_dashboard.cpp) |
| BUG-305 | ARRAY base-variabel vises som `CH` i stedet for `CH[0]` | ✅ FIXED | 🟠 MEDIUM | v7.9.0 | FEAT-004 array base-symbol beholdt rå navn (`CH`) i bytecode var_names display. Monitor viste `CH` for element 0 men `CH[1]`..`CH[7]` for resten. FIX: Compiler omdøber base-symbol til `CH[0]` format i bytecode var_names copy-loop med `snprintf("%s[%d]", name, lower_bound)`. Lookup i symbol table bruger stadig `CH`. (st_compiler.cpp) |
| BUG-306 | ES32D26 Master mode: USB serial dør ved boot | ✅ FIXED | 🔴 CRITICAL | v7.9.1.0 | `modbus_master_init()` kaldte `modbus_master_reconfigure()` som overtog GPIO1/3 (USB serial) til RS485 **FØR** WiFi/Telnet var startet → ingen konsol tilgængelig. FIX: Udskudt UART-overtagelse til efter netværksservices er startet + 5 sek interaktiv boot-prompt (tryk MELLEMRUM for at beholde USB console). Ny `modbus_master_activate_uart()` funktion. (modbus_master.cpp, modbus_master.h, main.cpp) |
| BUG-307 | `esp_wifi_set_ps()` kaldt før WiFi-stak initialiseret | ✅ FIXED | 🟡 HIGH | v7.9.1.0 | `config_apply()` kører på linje 147 i main.cpp, men WiFi init er først linje 173. Kaldte `esp_wifi_set_ps()` på uinitialiseret WiFi-stak → potentiel crash/ESP_ERR_WIFI_NOT_INIT. FIX: Guard med `cfg->network.enabled` check + `esp_err_to_name()` fejlhåndtering, springer over hvis WiFi er disabled. (config_apply.cpp) |
| BUG-133 | Modbus Master: Kun slot 0 kører MB operationer | ✅ FIXED (v2) | 🔴 CRITICAL | v7.9.1.0 | `g_mb_request_count` blev nulstillet én gang per cycle (før for-loop), så slot 0 brugte alle requests. FIX v2: Reset per slot + `g_mb_cache_enabled = true` reset per slot. (st_logic_engine.cpp) |
| FEAT-115 | MB_CACHE(BOOL) builtin til per-program cache kontrol | ✅ DONE | 🟠 MEDIUM | v7.9.1.0 | ST builtin `MB_CACHE(FALSE/TRUE)` styrer om MB_READ/MB_WRITE bruger cache-dedup. Returnerer forrige tilstand. Reset til TRUE per slot. (st_builtin_modbus.cpp, st_builtins.cpp, st_compiler.cpp, st_builtins.h, st_builtin_modbus.h) |
| FEAT-116 | Modbus Master Cache TTL konfiguration | ✅ DONE | 🟠 MEDIUM | v7.9.1.0 | `set modbus-master cache-ttl <ms>` — 0=aldrig expire, >0=ms. Expired entries markeret [EXP] i show. cache_entry_expired() helper i alle 4 read builtins. NVS persistent. CLI + API + backup/restore. (cli_commands_modbus_master.cpp, cli_parser.cpp, api_handlers.cpp, types.h, config_load.cpp, modbus_master.cpp) |
| FEAT-117 | Cache hits/misses tracking | ✅ DONE | 🔵 LOW | v7.9.1.0 | `cache_hits` og `cache_misses` felter i `mb_async_state_t` blev aldrig inkrementeret. FIX: Tæller nu i `mb_cache_get_or_create()`. (mb_async.cpp) |
| BUG-308 | Web editor: syntax-highlight usynlig efter scroll | ✅ FIXED | 🟠 MEDIUM | v7.9.1.1 | `#highlight` div havde `height:100%` + `overflow:hidden`, så content udover container-højden blev klippet bort — transform `translate(0, -scrollTop)` kunne aldrig afsløre det. Tekst var kun synlig ved markering (via textarea). FIX: `min-height:100%` + `overflow:visible` (parent `.editor-container` klipper stadig). (web_editor.cpp) |
| BUG-309 | Modbus Master cache list: alle ops vises som FC3 | ✅ FIXED | 🟠 MEDIUM | v7.9.1.1 | Cache nøgles på read-FC (FC01-FC04); writes (FC05/FC06) merger ind i read-cache entry via `WRITE_HOLDING→READ_HOLDING`-mapping, så `key.req_type` altid viste FC3 for holding registers. FIX: Nyt `last_fc` felt på `mb_cache_entry_t` der opdateres med faktisk request-type ved hver completion. CLI `show modbus master` + web dashboard + Prometheus-metrik viser nu reel FC (FC01-FC06). Writes til aldrig-læste adresser opretter nu også cache-entry så de er synlige. (mb_async.h, mb_async.cpp, api_handlers.cpp, cli_commands_modbus_master.cpp) |
| BUG-310 | `set modbus-master cache-ttl` giver "unknown parameter" | ✅ FIXED | 🟠 MEDIUM | v7.9.1.1 | `normalize_alias()` manglede `cache-ttl` mapping, så user input forblev lowercase og matchede aldrig `strcmp(param, "CACHE-TTL")`. Parameteren var også udeladt fra help-teksten. FIX: Tilføjet alias `CACHE-TTL`/`CACHETTL`/`CACHE_TTL`/`TTL` i `normalize_alias()` + dokumenteret i `print_modbus_master_help()` og kort usage-liste. (cli_parser.cpp) |
| FEAT-118 | `show modbus-master` viser konfig-kommandoer | ✅ DONE | 🔵 LOW | v7.9.1.2 | Tilføjet "Configuration commands" sektion i bunden af `show modbus-master` output med komplet liste over alle `set modbus-master` parametre inkl. `cache-ttl`. Brugere kan nu se tilgængelige kommandoer direkte i status-output. (cli_commands_modbus_master.cpp) |
| FEAT-119 | Web dashboard: Cache TTL + [EXP] markør | ✅ DONE | 🟠 MEDIUM | v7.9.1.2 | Web dashboard Modbus Master-kort viser nu: (1) ny "Cache TTL" række (`0 (aldrig expire)` eller `N ms`), (2) `[EXP]` markør i status-kolonnen når `age_ms >= cache_ttl_ms` (expired entries vises i orange val-warn). Ny Prometheus-metric `modbus_master_cache_ttl_ms` eksponerer TTL-værdien. (api_handlers.cpp, web_dashboard.cpp) |
| FEAT-120 | ST Logic Multi-Register Modbus Master (FC03/FC16) med ARRAY | ✅ DONE | 🟡 HIGH | v7.9.2.0 | `regs := MB_READ_HOLDINGS(slave, addr, count)` læser op til 16 reg via FC03 direkte ind i ARRAY OF INT. `MB_WRITE_HOLDINGS(slave, addr, count) := regs` skriver fra ARRAY via FC16. Konsistent assignment-syntax med eksisterende single-register ops. Compiler resolver array-variabel til base_index ved compile-time, VM kopierer til/fra variable slots. Cache opdateres per-register. Protokol: `modbus_master_read_holdings()`+`write_holdings()`. (modbus_master.h/cpp, mb_async.h/cpp, st_builtin_modbus.h/cpp, st_builtins.h/cpp, st_compiler.cpp, st_vm.cpp, st_parser.cpp, st_types.h) |
| OPT-001 | ST Compiler heap optimering | ✅ DONE | 🟡 HIGH | v7.9.3.0 | AST node 156->84 bytes (46% reduktion): char[64]->char[32] i union + CASE branches heap-allokeret. var_names[32][32]->[32][16] (-2KB RAM). Dynamisk bytecode buffer: start 256, realloc i 256-blokke, max 2048 (var fast 1024=8KB). Function registry functions[64]->[32]. Bytecode persist format v3. Resultat: peak heap 39KB->22KB, largest_block 41KB->49KB. (st_types.h, st_parser.cpp, st_compiler.cpp, st_compiler.h, st_bytecode_persist.h/cpp, constants.h) |
| FEAT-127 | Realtime compiler ressource-info i web editor | ✅ DONE | 🟠 MEDIUM | v7.9.3.1 | `/api/logic` returnerer `resources{}` med heap_free, largest_block, min_free, pool_total/used/free, ast_node_size, max_ast_nodes. Web editor viser Heap-bar ved siden af Pool-bar med storste blok i KB + max AST nodes. Auto-poll 5s. Farvekoder: gron >35KB, orange 20-35KB, rod <20KB. Tooltip med detaljeret info. (api_handlers.cpp, web_editor.cpp) |
| OPT-002 | Modbus Master cache + request optimering | ✅ DONE | 🟡 HIGH | v7.9.3.2 | mb_async_request_t 41→10 bytes (ring-buffer pool for FC16 values). LRU eviction når cache fuld (ældste non-PENDING entry). Write dedup: skipper queue hvis cache allerede har samme værdi. Queue memory: 656→160 bytes (-75%). (mb_async.h/cpp) |
| OPT-003 | Modbus FC16 response timeout fix | ✅ DONE | 🔴 CRITICAL | v7.9.3.2 | FC16 (0x10) manglede i response parser switch — alle FC16 writes ventede fuld timeout (500ms) selvom 8-byte response var modtaget. Tilføjet `function_code == 0x10` case med `bytes_received >= 8` check. Dual-phase timeout: inter-character timeout beregnet fra baudrate (T3.5), fuld timeout kun for første byte. (modbus_master.cpp) |
| FEAT-128 | Intelligent per-slave adaptive backoff | ✅ DONE | 🟡 HIGH | v7.9.3.2 | Exponential backoff ved timeouts (50ms→2000ms cap), lineær decay ved success (-100ms). Op til 8 slaves tracked. Forhindrer bus-flooding mod offline slaves. Synlig i CLI `show modbus-master` + web dashboard + Prometheus metric `modbus_master_slave_backoff`. (mb_async.h/cpp, cli_commands_modbus_master.cpp, web_dashboard.cpp, api_handlers.cpp) |
| FEAT-129 | Modbus Master stats reset med tidspunkt | ✅ DONE | 🟠 MEDIUM | v7.9.3.2 | `POST /api/modbus/master/reset-stats` nulstiller alle master+cache statistik-tællere og adaptive backoff. Web dashboard viser "Statistik siden: Xt Ym siden" med `modbus_master_stats_age_ms` metric. Nulstil-knap i Modbus Master kort. (mb_async.h/cpp, types.h, api_handlers.cpp, web_dashboard.cpp, modbus_master.cpp) |
| FEAT-121 | ST Logic TIME datatype | ✅ DONE | 🟡 HIGH | v7.9.4.0 | IEC 61131-3 TIME datatype: `T#5s`, `T#100ms`, `T#1m30s`, `T#2h`. ST_TYPE_TIME enum, TIME keyword i VAR declarations, T# literals som native TIME type. 32-bit millisekunder via DWORD push. (st_types.h, st_lexer.cpp, st_parser.cpp, st_compiler.cpp, st_vm.cpp) |
| FEAT-122 | ST Logic TON timer (On-delay) | ✅ DONE | 🟡 HIGH | v7.9.4.0 | IEC 61131-3 TON function block med named-parameter syntax: `TON(IN := trigger, PT := T#5s, Q => output, ET => elapsed)`. Output bindings via `=>` operator og ny ST_OP_LOAD_FB_FIELD opcode. Backward-kompatibel med positionel syntax. (st_parser.cpp, st_compiler.cpp, st_vm.cpp) |
| FEAT-123 | ST Logic TOF timer (Off-delay) | ✅ DONE | 🟡 HIGH | v7.9.4.0 | IEC 61131-3 TOF function block med named-parameter syntax: `TOF(IN := trigger, PT := T#3s, Q => output, ET => elapsed)`. Deler parser/compiler infrastruktur med TON. |
| FEAT-124 | ST Logic CTU counter (Count Up) | ✅ DONE | 🟡 HIGH | v7.9.4.0 | IEC 61131-3 CTU function block med named-parameter syntax: `CTU(CU := pulse, RESET := rst, PV := 100, Q => done, CV => count)`. Edge-triggered, output bindings via LOAD_FB_FIELD. |
| FEAT-125 | ST Logic CTD counter (Count Down) | ✅ DONE | 🟡 HIGH | v7.9.4.0 | IEC 61131-3 CTD function block med named-parameter syntax: `CTD(CD := pulse, LOAD := ld, PV := 100, Q => done, CV => count)`. Edge-triggered, deler counter infrastruktur med CTU. |
| FEAT-126 | ST Logic CTUD counter (Up/Down) | ✅ DONE | 🟡 HIGH | v7.9.4.0 | IEC 61131-3 CTUD function block med named-parameter syntax: `CTUD(CU := up, CD := down, RESET := rst, LOAD := ld, PV := 100, QU => at_max, QD => at_zero, CV => count)`. 3 output bindings (QU, QD, CV). |
| BUG-311 | Function call statement ikke kompileret | ✅ FIXED | 🔴 CRITICAL | v7.9.4.1 | `ST_AST_FUNCTION_CALL` manglede i compiler statement-switch → `default: break` ignorerede hele FB-kaldet. TON/TOF/CTU etc. som statement (ikke assignment) genererede 0 instrukser. FIX: Tilføjet case med compile_expr + POP (st_compiler.cpp) |
| BUG-312 | TIME variabel vist som int16 i REST API | ✅ FIXED | 🟡 HIGH | v7.9.4.1 | `ST_TYPE_TIME` manglede i API handler value-switch → faldt til `default: val.int_val` (16-bit). T#60s=60000 vistes som -5536. FIX: Tilføjet TIME case med dint_val (api_handlers.cpp) |
| BUG-313 | DINT literal >32767 crasher ESP32 | ✅ FIXED | 🔴 CRITICAL | v7.9.4.1 | DINT brugte PUSH_INT (16-bit int_arg). Værdier >32767 overfyldte int16 og crashede VM'en. FIX: DINT literals bruger nu PUSH_DWORD (32-bit) ligesom TIME (st_compiler.cpp) |
| BUG-314 | Integer literal auto-promote til DINT | ✅ FIXED | 🟡 HIGH | v7.9.4.1 | Parser afviste alle integers >32767 med "INT overflow" fejl. DINT variabler kunne ikke initialiseres med store tal. FIX: Auto-promote literal til ST_TYPE_DINT når værdi > INT16_MAX eller < INT16_MIN (st_parser.cpp) |
| BUG-315 | Modbus Master parity/stop-bits ignoreret på ES32D26 | ✅ FIXED | 🔴 CRITICAL | v7.9.5.1 | `modbus_master_reconfigure()` kaldte `uart1_init(baudrate)` i ES32D26 (shared transceiver) grenen, men `uart1_init()` hardcoded SERIAL_8N1. Master parity/stop-bits config blev silent droppet → frames blev sendt med forkert framing, slaver kasserede dem. FIX: Ny `uart1_init_ex(baudrate, config)` + build korrekt UART config fra `g_modbus_master_config.parity`/`stop_bits` (uart_driver.cpp, uart_driver.h, modbus_master.cpp) |
| BUG-316 | Modbus Master post-TX DE-release for hurtig → sidste byte afskåret | ✅ FIXED | 🔴 CRITICAL | v7.9.5.1 | `modbus_master_send_request()` ventede kun 50µs efter `flush()` før DE blev sat LOW. Ved 9600 baud er 1 byte ~1040µs — hvis `HardwareSerial::flush()` ikke venter på shift-register empty (version-afhængig semantik i Arduino ESP32 core), blev sidste byte (typisk CRC hi) afskåret midt i transmissionen. Slave modtog korrupt frame, ignorerede den, ingen svar → timeout. Symptom: pulser synlige på oscilloskop men ingen slave svarede. FIX: Delay nu beregnet fra baudrate som 1 char-tid (11 bit) + 100µs margin — fx 1246µs @ 9600 baud (modbus_master.cpp) |
| BUG-323 | Login modal flash ved sideskift i web GUI | ✅ FIXED | 🟡 HIGH | v7.9.6.5 | Editor/CLI/System sider startede med login modal synlig (`class="show"`), så brugeren blev promptet for login ved hvert sideskift selvom `sessionStorage` havde gyldigt auth token. FIX: Modal starter nu skjult, vises kun hvis auth mangler eller validering fejler (web_editor.cpp, web_cli.cpp, web_system.cpp) |
| FEAT-130 | SSE-drevet dashboard + editor monitor | ✅ DONE | 🟡 HIGH | v7.9.6.0 | Web dashboard (/) og editor monitor (/editor) abonnerer nu på `/api/events?subscribe=all\|registers` via EventSource i stedet for konstant polling. Dashboard polling slowed 3s→5s ved SSE connect, editor monTimer 1500→2500ms + debounced refresh (500ms) ved register-events. SSE indicator (●) viser live-status, auto-reconnect efter 5s ved fejl. Register events opdaterer regHR/regmapHR/regmapIR/regCoils/regmapCoils/regmapDI grids instant. visibilitychange handler pauser polling når tab er hidden. (web_dashboard.cpp, web_editor.cpp) |
| FEAT-131 | Inline kompileringsfejl i web editor | ✅ DONE | 🟡 HIGH | v7.9.6.0 | Parser `compile_error` response med regex `/(?:at\s+line\|line)\s+(\d+)/i` for at udtrække linjenummer. Rød baggrund (`.line-nums .errln`) markerer fejl-linjen i line-numbers gutter, auto-scroll via `scrollToLine()` centrerer textarea på fejl-linjen. Ryddes ved slot-skift eller successful compile. (web_editor.cpp) |
| FEAT-132 | Upload .st-fil i web editor | ✅ DONE | 🟢 MEDIUM | v7.9.6.0 | Upload-knap + skjult `<input type=file accept=".st,.txt">` + FileReader text-read. 16KB max størrelse-check, indsætter indhold i textarea og markerer slot som modified. (web_editor.cpp) |
| FEAT-133 | Find/Replace i web editor (Ctrl+F/Ctrl+H) | ✅ DONE | 🟢 MEDIUM | v7.9.6.0 | Find-bar panel med find/replace inputs, count display, Next/Prev/Replace/ReplaceAll knapper. Global Ctrl+F / Ctrl+H keydown handler åbner panel og fokuserer inputs. Case-sensitive string search på textarea value. (web_editor.cpp) |
| FEAT-134 | Alarm historik filter | ✅ DONE | 🟢 MEDIUM | v7.9.6.0 | Client-side filter bar på alarm-card: severity (info/warn/error/critical), ack status (all/acked/unacked), search text (message contains), limit input, reset-knap. `fetchAlarms()` cacher rå liste, `renderAlarms()` applicerer filter ved hvert render + ved filter-change. (web_dashboard.cpp) |
| FEAT-135 | Modbus Master mb read/write mini-form i dashboard | ✅ DONE | 🟢 MEDIUM | v7.9.6.0 | Live widget i Master-card: dropdown (read hr/ir/coil/di, write hr/coil), slave/addr/val inputs, Udfør-knap. Poster til `/api/cli` med `mb <op> <slave> <addr> <val>` og viser response i `<pre>`. Ingen CLI-session nødvendig. (web_dashboard.cpp) |
| BUG-317 | Dashboard DIO toggle brugte forkerte JSON-felter | ✅ FIXED | 🟢 MEDIUM | v7.9.6.0 | `toggleDO(pin)` læste `d.level` og postede `{level: newVal}` — men `/api/gpio/{pin}` returnerer og forventer `{value: 0/1}`. Toggle var halvt implementeret (CSS hover hint fandtes, men kald failede silent). FIX: Brug `d.value` + `{value: newVal}` body + eksplicit error-alert ved HTTP fejl. (web_dashboard.cpp) |
| FEAT-136 | CLI `set rbac enable/disable` | ✅ DONE | 🟡 HIGH | v7.9.6.1 | RBAC-flaget kunne kun sættes via REST API (`/api/rbac`) eller migration — CLI manglede helt. Nu: `set rbac <enable\|disable>` + aliases (on/off/1/0/true/false). Viser current status når kaldt uden argument. Advarer hvis RBAC enables før der er oprettet brugere (lock-out risk). Fjernet `RBAC→USER` alias som shadow-mappede kommandoen til user-add. (cli_parser.cpp) |
| BUG-318 | SSE fejlede med 401 fra web monitor/editor når auth aktiv | ✅ FIXED | 🔴 CRITICAL | v7.9.6.2 | `EventSource` API tillader ikke custom headers, og browsere deler ikke Basic Auth credentials cross-port (dashboard på :80, SSE på :81 = forskellige origins). Resultat: alle SSE-forbindelser fra web UI fik 401, `show sse` viste 0 clients, dashboard og editor fallback'ede til ren polling. FIX: Query-string token auth — `api_handler_sse_status` udsteder et 5-min reusable token bundet til authenticated caller, dashboard+editor sender det som `?token=XXX` i EventSource URL. SSE accept-loop validerer token før fallback til Authorization header. Token-tabel med 8 slots, `portMUX_TYPE` lås, evict-oldest ved overflow. (sse_events.cpp, web_dashboard.cpp, web_editor.cpp) |
| BUG-319 | mb mini-form i dashboard brugte forkerte type-navne | ✅ FIXED | 🟡 HIGH | v7.9.6.2 | Dropdown brugte `hr`, `ir`, `di` som CLI aldrig har accepteret — CLI'en forventer `holding`, `input-reg`, `input` (og `coil`). Desuden sendte formen altid en value-arg, selv for read coil/input/input-reg som CLI afviser. Og `write coil` fik tal-input men CLI forventer `0/1/on/off`. FIX: Opdateret dropdown med korrekte CLI-navne + FC-numre som label, `updateMbForm()` disabler value-feltet hvor det ikke bruges, `doMbCmd()` bygger nu kommandoen præcist som CLI-hjælpen viser. (web_dashboard.cpp) |
| BUG-320 | SSE-events opdaterede kun register-grids, resten af dashboardet ventede på 5s polling | ✅ FIXED | 🟡 HIGH | v7.9.6.3 | `sseApplyCounterEvent`/`sseApplyTimerEvent` var no-ops (satte kun `sseLastEventMs`). DIO LEDs, counter-widgets, timer-widgets, master-stats og system-info ventede stadig på `fetchMetrics()` som kører hver 5 sekunder når SSE er aktiv. Resultat: bruger så ingen forskel i opdaterings-hastighed. FIX: Ny `sseKickMetrics()` debounced til max 1 fetch per 150ms — trigges ved counter/timer/coil/di events så hele UI reagerer inden for ~150ms efter aktivitet. Register-events opdaterer stadig grid-celler direkte uden round-trip. (web_dashboard.cpp) |
| BUG-321 | Editor monitor SSE brugte 500ms debounce + kun register-topic | ✅ FIXED | 🟡 HIGH | v7.9.6.3 | `editorSseConnect` abonnerede kun på `subscribe=registers` og debouncede refreshMonitor 500ms. Counter/timer ændringer fra ST-kode viste sig langsomt, og første event kom først efter 500ms delay. FIX: Ændret til `subscribe=all`, leading-edge dispatch (første event triggrer refreshMonitor med det samme hvis >100ms siden sidste kick), subsequent events coalesces til én refresh per 100ms vindue. Abonnerer nu på register, counter OG timer events. (web_editor.cpp) |
| FEAT-137 | Monitor trend-kurver forbedret med oscilloskop-gitter | ✅ DONE | 🟢 MEDIUM | v7.9.6.4 | Trend SVG 320×40 med oscilloskop-stil gitterlinjer (4 horisontale + 8 vertikale), mørk baggrund (#11111b). Min/max labels til venstre for kurve, tid til højre. Valgbar kurvefarve (blå/grøn/gul/rød/lilla/orange/cyan/hvid) på kontrollinjen. (web_editor.cpp) |
| FEAT-138 | Monitor hastigheds- og historik-kontrol | ✅ DONE | 🟢 MEDIUM | v7.9.6.4 | Opdateringshastighed dropdown (500ms–10s) styrer polling-interval korrekt. Historiklængde (30/60/120/240) trimmer eksisterende data-buffer. SSE events udløser ikke længere direkte refresh — kun polling-timeren styrer rate for jævn opdatering. Overlap-guard (monBusy) forhindrer stacked API-kald. (web_editor.cpp) |
| FEAT-139 | Debug-knapper omdøbt til klarere funktionsbeskrivelser | ✅ DONE | 🟢 MEDIUM | v7.9.6.4 | Pause→"Pause Execute", Step→"Single Step", Continue→"Single Cycle", Stop Debug→"Normal Execute". Tooltips tilføjet. Vertikal separator mellem knapper og statusbadge. PC-info integreret i badge-tekst ("Paused \| PC=5"). Kompakt tabel-padding for flere synlige variabler. (web_editor.cpp) |
| FEAT-140 | Modbus Master mini-form labels i dashboard | ✅ DONE | 🟢 MEDIUM | v7.9.6.5 | Tilføjet statiske labels (FC Type, Slave ID, Adresse, Antal/Værdi) over hvert input-felt i Read/Write sektionen. Label for felt 4 skifter dynamisk mellem "Antal" (read) og "Værdi" (write). Slave ID felt gjort bredere (55px, samme som Adresse/Værdi). (web_dashboard.cpp) |
| BUG-322 | Monitor opdatering uregelmæssig — SSE bypass'ede polling-interval | ✅ FIXED | 🟡 HIGH | v7.9.6.4 | SSE `kickMonitor()` kaldte `refreshMonitor()` direkte med kun 100ms throttle, uanset brugerens valgte opdateringshastighed (500ms–10s). Resulterede i uregelmæssige ryk i kurver og ignorering af hastighedsvalg. FIX: SSE events trigger ikke længere refresh direkte — kun `setInterval` polling-timeren styrer opdateringsraten. `monBusy` flag forhindrer overlappende API-kald. (web_editor.cpp) |
| FEAT-141 | Masonry dashboard layout med dynamisk kort-stacking | ✅ DONE | 🟢 MEDIUM | v7.9.6.6 | Dashboard grid brugte `grid-auto-rows: auto` → alle kort i samme række fik højden fra det største kort. Små kort (NTP, Counters) spildte plads ved siden af store kort (Modbus Master). FIX: Masonry layout med `grid-auto-rows: 8px` + JS `autoSizeCards()` der måler `scrollHeight` og sætter `grid-row: span N`. Bred/normal toggle (⇔ knap) per kort med `localStorage` persistence. Mobile fallback til enkelt-kolonne. Auto-resize ved metrics-fetch, alarm-render og window resize. Scroll-position bevares via `minHeight` freeze under resize. (web_dashboard.cpp) |
| FEAT-142 | Modbus Master mini-form bruger async kø + cache | ✅ DONE | 🟡 HIGH | v7.9.6.6 | Mini-form brugte `/api/cli` med synkron `mb read` → blokerede HTTP-tråd + UART-konflikt med async-task → hyppige timeouts. Nyt `/api/modbus/master/rw` endpoint der bruger `mb_cache_find()` for reads (returnerer cached værdi instant) og `mb_async_queue_write()` for writes. Polling (max 3s) for pending reads. Output er nu selekterbart (`user-select:text`). (api_handlers.cpp, web_dashboard.cpp) |

## Quick Lookup by Category

### 🌐 API Roadmap (v6.3.0 — v7.0.0)

**v6.3.0 — API Gap Coverage (CLI parity) — ✅ 7/8 DONE:**
- **FEAT-019:** ✅ Telnet Configuration API — 4/4 testet PASS
- **FEAT-020:** ✅ ST Logic Debug API — 8/8 testet PASS
- **FEAT-021:** ✅ Bulk Register Operations — 12/12 testet PASS
- **FEAT-022:** ✅ Persistence Group Management API (v7.1.0)
- **FEAT-024:** ✅ Hostname API — 3/3 testet PASS
- **FEAT-025:** ✅ Watchdog Status API — 2/2 testet PASS
- **FEAT-026:** ✅ Heartbeat Control API — 3/3 testet PASS (BUG-236 fixed)
- **FEAT-027:** ✅ CORS Headers — 3/3 testet PASS
- **Test:** 34/34 PASS (100%) — se `tests/API_V630_TEST_RESULTS.md`

**v7.1.0 — Prometheus Metrics + Persist API + Rate Limiting (2026-03-18):**
- **FEAT-032:** `GET /api/metrics` — Prometheus text format med system, HTTP, Modbus, SSE, network, counter, timer, watchdog metrics
- **FEAT-022:** Persistence Group Management API — `GET/POST/DELETE /api/persist/groups/{name}` + save/restore endpoints
- **FEAT-028:** Token bucket rate limiter per klient IP (30 burst, 10/sec) — returnerer 429 Too Many Requests
- **CLI:** `show rate-limit`, `set rate-limit enable|disable`, `show metrics` + sektioner i `show config` og `show http`
- **FIX:** API v1 routing-tabel manglede 7 endpoints (bulk read, heartbeat, SSE status, version)
- **FIX:** max_uri_handlers øget fra 64 til 80 (72 registrerede handlers)

**v7.0.3 — SSE CLI Management (2026-03-18):**
- **FEAT:** `set sse` / `show sse` CLI sektioner med fuld konfiguration
- **FEAT:** Klient-registry med IP-tracking, `set sse disconnect all|<slot>`
- **FEAT:** `show config` inkluderer [API SSE] + # API SSE sektioner
- **FEAT:** SSE parametre konfigurerbare via NVS (schema 12)

**v7.0.2 — SSE Stabilitet (2026-03-18):**
- **FIX:** SSE multi-klient reconnect-beskyttelse (heap-check, cooldown, defensiv decrement)
- Testet: 2 samtidige klienter bekræftet stabil

**v7.0.1 — Next Generation API (2026-03-17):**
- **FEAT-023:** ✅ SSE Real-Time Events — konfigurerbar watch af HR/IR/Coils/DI via query params
- **FEAT-030:** ✅ API Versioning — 32/32 testet PASS (/api/v1/* + /api/version)
- **Test:** 40/40 PASS (100%) — se `tests/FEAT023_FEAT030_TEST_RESULTS.md`

**v7.x.0 — Planned:**
- **FEAT-028:** ✅ Request Rate Limiting (v7.1.0)
- **FEAT-029:** OpenAPI/Swagger Schema endpoint 🔵 LOW
- **FEAT-031:** ✅ Firmware OTA via API (v7.5.0)
- **FEAT-032:** ✅ Prometheus Metrics endpoint (v7.1.0)
- **FEAT-033:** Request Audit Log 🔵 LOW

**v7.2.0 — ES32D26 Modbus Refaktor + Hardware Modul Config (2026-03-23):**
- **FEAT-038:** ✅ Modbus mode refaktor — single-transceiver support for ES32D26 (slave/master/off)
- **FEAT-039:** ✅ AO mode config — DIP switch SW1 voltage/current konfiguration
- **FEAT-040:** ✅ Runtime UART selection — `set modbus slave|master uart uart0|uart1|uart2`
- **FEAT-041:** ✅ Hardware modul config — `set modul rs485 uart<N> tx rx dir` + `set modul ethernet enable|disable`
- Schema migration 12→13→14 (modbus_mode, ao_mode, UART pin config)

**v7.3.1 — Web Editor Udvidelse: CLI + Bindings + Monitor (2026-03-26):**
- **FEAT-043:** ✅ Web CLI Console — `POST /api/cli` med buffer-console capture, kommandohistorik, farlige kommandoer blokeret
- **FEAT-044:** ✅ Web Bindings Panel — variabel→register binding oprettelse/sletning via GUI. `GET /api/bindings` + `DELETE /api/bindings/{idx}`
- **FEAT-045:** ✅ Web Runtime Monitor — live variabel-værdier + execution stats med 1.5s auto-refresh
- **UI:** 4 view-modes i toolbar (Editor/Bindings/Monitor/CLI), fixed layout (toolbar+statusbar fastlåst), global output-log
- **BUG-246:** ✅ Login hardcoded default username → tomme felter med placeholder
- **BUG-247:** ✅ API endpoints manglede URI handler registrering (405 fejl)
- **BUG-248:** ✅ Modbus CLI kommandoer brugte Serial.print → debug_printf (web CLI + telnet output)
- **BUG-249:** ✅ Binding delete index mismatch (filtreret vs globalt index)
- **BUG-250:** ✅ PSRAM detection tilføjet til `show version`, `show status` og `/api/metrics`
- **BUG-251:** ✅ `/api/metrics` auth fjernet — åben for dashboard og Prometheus scrapers
- **BUG-252:** ✅ Web editor/system login tabt ved navigation — `sessionStorage` persistering
- **FEAT-046:** ✅ BIT_SET/BIT_CLR/BIT_TST bit-manipulation funktioner i ST Logic
- **FEAT-047:** ✅ Web Dashboard med live Prometheus metrics på `/` (ingen login)
- **FEAT-048:** ✅ Bindings redigering — Redigér-knap med pre-udfyldt formular
- **FEAT-049:** ✅ Syntax highlighting i ST Editor
- **FEAT-050:** ✅ Auto-complete/IntelliSense i ST Editor
- **FEAT-051:** ✅ Sparkline grafer i Runtime Monitor
- **FEAT-052:** ✅ Single-step execution i ST Logic
- **FEAT-053:** ✅ Register viewer i Dashboard
- **FEAT-054:** ✅ Historik-grafer i Dashboard
- **FEAT-055:** ✅ Alarm/notifikationer i Dashboard
- **FEAT-056:** ✅ Modbus register map i Dashboard
- **FEAT-057:** ✅ Program backup/download fra editor
- **FEAT-058:** ✅ PLC Ladder diagram visning

**v7.6.2 — RBAC Multi-User System + SSE Monitor Udvidelse (2026-03-30):**
- **FEAT-059:** ✅ RBAC med op til 8 brugere, roller (api/cli/editor/monitor), privilegier (read/write/rw)
- **FEAT-060:** ✅ Standalone Web CLI side på `/cli` (udskilt fra ST Editor)
- **FEAT-061:** ✅ SSE subscribe=all watch_all mode (fuld-range scanning)
- **FEAT-062:** ✅ SSE klient monitor — username, IP, topics, uptime i `show sse`
- **BUG-260:** ✅ SSE subscribe=all overvågede kun HR 0-15
- **BUG-261:** ✅ `show config rbac` viste tom sektion
- **BUG-262:** ✅ `set rbac` kommando ikke genkendt
- **CLI:** `set user`, `show users`, `delete user`, `show config rbac`
- **Sikkerhed:** SSE kræver MONITOR rolle, schema migration v14→v15
- **DOC:** [docs/SECURITY.md](docs/SECURITY.md) sikkerhedsguide, [docs/SSE_USER_GUIDE.md](docs/SSE_USER_GUIDE.md) opdateret

**v7.6.2.2 — Web User Badge + /api/user/me (2026-03-30):**
- **FEAT-063:** ✅ Web User Badge i topnav — brugernavn, status-dot, dropdown med roller/privilegier/auth mode, login/logout
- **FEAT-064:** ✅ `/api/user/me` API endpoint — returnerer brugerinfo (username, roles, privilege, mode)
- **FEAT-065:** ✅ `show user` CLI kommando — viser aktuel session info
- **FEAT-066:** ✅ SSE klient management i web System — tabel med disconnect-knapper
- **BUG-263:** ✅ User badge viste altid "Ikke logget ind" — `/api/user/me` manglede httpd URI handler, login modal på dashboard, updateUserBadge() kald efter login
- **BUG-264:** ✅ SSE afviste brugere med API rolle — rolle-check udvidet til MONITOR|API, RBAC sektion i `show sse`

**v7.6.2.4 — RBAC Privilege Enforcement (2026-03-31):**
- **BUG-265:** ✅ 42 write-endpoints ændret til CHECK_AUTH_WRITE — read-only brugere blokeres nu
- **BUG-266:** ✅ rbac_parse_privilege("write") returnerede PRIV_RW — rettet parse-rækkefølge
- **BUG-264:** ✅ SSE rolle-check udvidet til MONITOR|API
- `show sse` viser RBAC Authentication sektion
- `show users roles` viser SSE-krav

**v7.6.2.5 — Web System fejlhåndtering (2026-03-31):**
- **BUG-267:** ✅ Write-knapper (reboot, save, load, defaults) viser nu 403 fejl i stedet for at slugte dem silently

**v7.7.0 — Async Modbus Master (2026-03-31):**
- **FEAT-070:** ✅ Modbus Master operationer er nu asynkrone via dedikeret FreeRTOS baggrundstask
- Non-blocking ST builtins: reads returnerer cached værdi, writes køes
- Nye builtins: `MB_SUCCESS()`, `MB_BUSY()`, `MB_ERROR()`
- 32-entry cache, 16-deep request queue, request deduplication
- `show modbus-master` viser async cache statistik + entries
- Backward-kompatibel: eksisterende ST-programmer virker uændret

**v7.8.0 — Web Monitor Udvidelse (2026-04-01):**
- **FEAT-072:** ✅ Modbus RTU trafikmonitor — live req/3s rate, success rate, CRC/timeout/exception tæller, sparkline grafer
- **FEAT-073:** ✅ Modbus Master cache status — async cache hits/misses/entries, per-slave status tabel
- **FEAT-078:** ✅ FreeRTOS task monitor — task count, heap fragmentation, stack HWM for kendte tasks
- **FEAT-085:** ✅ Alarm-historik — 32-entry ringbuffer, auto-detection (heap/CRC/timeout/auth/overrun), API + kvittér
- **FEAT-095:** ✅ Digital I/O dashboard — LED-indikatorer for IN1-IN8 + CH1-CH8 med klik-toggle
- **Backend:** Nye Prometheus metrics: heap largest block, master exceptions, master cache stats, FreeRTOS tasks, watchdog reset reason, alarm count
- **API:** `GET /api/alarms` (alarm log JSON), `POST /api/alarms/ack` (kvittér alle)
- **Metrics buffer:** 8KB → 12KB for udvidede metrics

**v7.8.1 — NTP Tidssynkronisering (2026-04-01):**
- **FEAT-101:** ✅ NTP tidssynkronisering — ESP-IDF SNTP klient, konfigurerbar server + POSIX tidszone
- **CLI:** `set ntp enable|server|timezone|interval`, `show ntp`, `show time`
- **API:** `GET /api/ntp` (status + config), `POST /api/ntp` (konfiguration)
- **Dashboard:** NTP kort med live klokkeslæt, sync status, server info
- **Metrics:** ntp_enabled, ntp_synced, ntp_sync_count, ntp_epoch_seconds, ntp_last_sync_age_ms
- **Alarm-log:** Real-time tidsstempler fra NTP (fallback til uptime)
- **Schema:** 15 → 16 (NtpConfig: enabled, server, timezone, sync_interval_min)

**v8.0.0 — Web Monitor Udvidelse (Roadmap):**
- **FEAT-074:** Ethernet/WiFi signalkvalitet (RSSI, link speed, pakketab) 🟠 MEDIUM
- **FEAT-075:** ✅ TCP forbindelsesmonitor (v7.9.6.7)
- **FEAT-076:** Bandwidth monitor (bytes sendt/modtaget over tid) 🔵 LOW
- **FEAT-077:** Heap fragmentation graf (largest free block vs total) 🟡 HIGH
- **FEAT-079:** FreeRTOS task monitor udvidet (prioritet, CPU%) 🟠 MEDIUM
- **FEAT-080:** Boot-historik (reset-årsager, timestamps) 🟡 HIGH
- **FEAT-081:** NVS brug (plads, entries, flash wear) 🟠 MEDIUM
- **FEAT-082:** SPIFFS/LittleFS brug 🔵 LOW
- **FEAT-083:** ESP32 intern temperatur 🔵 LOW
- **FEAT-084:** Spændingsmonitor (VCC, brownout events) 🔵 LOW
- **FEAT-086:** Event log viewer (config, reboot, OTA, login) 🟠 MEDIUM
- **FEAT-087:** Watchdog-statistik 🟠 MEDIUM
- **FEAT-088:** Threshold-alarmer for registre 🟠 MEDIUM
- **FEAT-089:** Register change-log med tidsstempler 🟠 MEDIUM
- **FEAT-090:** Modbus request log / sniffer 🟡 HIGH
- **FEAT-091:** ST Logic profiler (cycle time, worst-case) 🟠 MEDIUM
- **FEAT-092:** GPIO pulse monitor (pulsbredde, frekvens, duty cycle) 🔵 LOW
- **FEAT-093:** Uptime-graf med nedetidsmarkering 🔵 LOW
- **FEAT-094:** Analog input trending (0-10V, 4-20mA) 🟡 HIGH
- **FEAT-096:** RS-485 bus health 🟠 MEDIUM
- **FEAT-097:** Customizable dashboard (drag & drop widgets) 🔵 LOW
- **FEAT-098:** Register-map visualizer (farvekodning) 🟠 MEDIUM
- **FEAT-099:** Trend recorder med CSV-eksport 🟡 HIGH
- **FEAT-100:** Mini-SCADA view 🔵 LOW

**v7.7.2 — FEAT: HW Counter Access fra ST Logic (2026-03-31):**
- **FEAT-071:** 9 nye ST builtins: CNT_SETUP, CNT_SETUP_ADV, CNT_SETUP_CMP, CNT_ENABLE, CNT_CTRL, CNT_VALUE, CNT_RAW, CNT_FREQ, CNT_STATUS
- **FEAT-071:** Konfigurer, styr og aflæs ESP32 HW counters direkte fra ST-programmer
- **FEAT-071:** Config fra ST afspejles i CLI (`show counter`) og NVS persistens
- **FEAT-071:** VM udvidet til max 6 argumenter (CNT_SETUP)
- **FEAT-071:** Web editor: autocomplete + hjælpepanel for CNT funktioner
- **BUG-296:** ✅ ST parser max 4 function arguments — CTUD/SCALE/CNT_SETUP fejlede (args[4]→args[8])
- **BUG-297:** ✅ Web dashboard register map viste Counter 1 ved HR0-3 i stedet for HR100-114
- **FEAT-071:** Register map viser nu ST Logic variable bindings (HR/Coil/DI) fra /api/bindings

**v7.7.1.1 — VAR Initializers + Telnet RBAC + Boot Fix (2026-03-31):**
- **BUG-290:** ✅ VAR initial values (`INT := 120`) virker nu i compiler
- **BUG-291:** ✅ VAR initial values bevares i bytecode cache (nyt v2 format)
- **BUG-292:** ✅ Telnet bruger RBAC auth når enabled, ellers lokal user/pass
- **BUG-293:** ✅ mb_async_init() crash på ES32D26 i SLAVE mode fixet
- **BUG-294:** ✅ Web editor autocomplete: BEGIN/END/EXPORT + alle Modbus Master funktioner
- **BUG-295:** ✅ Version+build konsistent i web UI (System, OTA, footer)

**v7.6.2.7 — SSE backup/restore (2026-03-31):**
- **FEAT-069:** ✅ SSE konfiguration inkluderet i backup/restore — enabled, port, max_clients, check_interval_ms, heartbeat_ms

**v7.6.2.6 — Confirm dialog fix (2026-03-31):**
- **BUG-268:** ✅ confirmAction() nulstillede callback før den blev kaldt — alle confirm-handlinger virkede ikke

**v7.4.0 — Web System Administration + Dashboard Refactor (2026-03-28):**
- **FEAT:** Web-baseret system administration side på `/system` — backup/restore, save/load, factory defaults, reboot, persist groups
- **FEAT:** Web Dashboard refaktoreret til standalone `web_dashboard.cpp` (v7.4.0)
- **FEAT:** Web Editor refaktoreret til standalone `web_editor.cpp` (v7.4.0)
- **FEAT:** Fælles navigation mellem Dashboard (`/`), Editor (`/editor`), System (`/system`), OTA (`/ota`)

**v7.5.0 — Firmware OTA via HTTP API + SSE bugfixes (2026-03-28):**
- **FEAT-031:** ✅ Firmware OTA via API — chunked upload, progress polling, rollback
- **Endpoints:** `POST /api/system/ota` (upload .bin), `GET /api/system/ota/status` (progress), `POST /api/system/ota/rollback`
- **Web UI:** Dedikeret OTA side på `/ota` med drag-drop upload, progress bar, firmware info, rollback knap
- **Partitions:** Dual OTA layout (ota_0 + ota_1 á 1.625MB, NVS 64KB, SPIFFS 640KB)
- **Boot validation:** `esp_ota_mark_app_valid_cancel_rollback()` i setup() bekræfter ny firmware
- **Sikkerhed:** Basic Auth + rate limiting på OTA endpoints, `CHECK_AUTH_OTA()` macro
- **BUG-253:** ✅ SSE manglende TCP_NODELAY → events forsinket/tabt (Nagle buffering)
- **BUG-254:** ✅ SSE SO_SNDTIMEO 5s→30s (for aggressiv timeout under load)
- **BUG-255:** ✅ SSE klient-task stack overflow risiko (init_buf→heap, stack 5120→6144)
- **BUG-256:** ✅ SSE EAGAIN behandlet som fatal fejl (retry-loop tilføjet)
- **Build:** #1700, schema uændret

---

### 📊 ST Logic Heap & RAM Analyse (v7.5.0 — post-optimering)

**ESP32-WROOM-32: 520 KB SRAM total, ~320 KB disponibelt efter WiFi/BT**

#### Permanent Heap (altid allokeret)

| Komponent | Per stk | Antal | Total |
|-----------|---------|-------|-------|
| `source_pool[8000]` | 8,000 B | 1 shared | **8.0 KB** |
| `st_logic_program_config_t` (bytecode+stats+meta) | ~2.5 KB | 4 | **10.0 KB** |
| ↳ `*instructions` (dynamisk, exact-size) | 8 B/instr | variabel | *se nedenfor* |
| ↳ `variables[32]` á 8 bytes | 256 B | (inline) | |
| ↳ `var_names[32][16]` (v7.9.3: var [32][32]) | 512 B | (inline) | ~~1,024 B~~ |
| ↳ `var_types[32]` + `var_export_flags[32]` | 64 B | (inline) | |
| ↳ `name[32]` + `last_error[64]` + stats/meta | ~236 B | (inline) | ~~392 B~~ |
| Dynamisk instructions (per compiled prog) | 8 B/instr | 0-2048 | **variabel** |
| ↳ Typisk 100-instruktion program | 800 B | per prog | |
| ↳ Stort 500-instruktion program | 4,000 B | per prog | |
| `st_stateful_storage_t` (malloc'd per compiled prog) | ~540 B | 0-4 | **0–2.2 KB** |
| `st_function_registry_t` (kun ved user functions) | ~3.9 KB | 0-4 | **0–15.6 KB** |
| ↳ `functions[32]` á ~52 bytes (v7.9.3: var [64]) | 1,664 B | (inline) | ~~3,328 B~~ |
| ↳ `fb_instances[16]` á ~145 bytes | 2,320 B | (inline) | |
| Engine global state (enabled, interval, cycle stats) | ~60 B | 1 | **0.06 KB** |

**Permanent total:**
```
Minimum (0 programmer compiled):     18.1 KB  (pool + bytecode structs + engine)
Typisk (2 prog á 100 instr, ingen UF): 20.9 KB  (+ 2× instructions + 2× stateful)
Maximum (4 prog á 500 instr + UF):   50.4 KB  (+ 4× instructions + 4× stateful + 4× registry)

FØR optimeringer:  51.7 – 84.6 KB
EFTER optimeringer: 18.1 – 50.4 KB  → BESPARELSE: 33.6 – 34.2 KB (65% ↓)
```

#### Temporær Heap (under kompilering — frigives efter)

| Komponent | Størrelse | Bemærkning |
|-----------|-----------|------------|
| AST node pool | **21–40 KB** | 256–475 nodes × 84 bytes/node (v7.9.3 optimering) |
| ↳ `st_ast_node_t`: 84 bytes | | v7.9.3: char[64]→[32] + CASE branches→heap (var 156 B) |
| ↳ Try-decreasing: 512→256→128→64→32 | | BUG-241: tilpasser sig fragmenteret heap |
| Parser `st_parser_t` (malloc'd) | **~1.3 KB** | lexer + tokens + error_msg[256] |
| Compiler `st_compiler_t` (malloc'd) | **~3.0 KB** | symbol_table[32]×74 + patches + stacks |
| Temp bytecode buffer (dynamisk) | **2–16 KB** | v7.9.3: start 256 instr (2KB), realloc i 256-blokke, max 2048 |
| Source code kopi (null-terminated) | **0.1–2 KB** | BUG-212 fix: malloc(source_size+1) |
| CASE branches (per CASE stmt) | ~128 B | v7.9.3: heap-allokeret 16×8 bytes, free efter parse |
| CASE jump arrays (per CASE stmt) | ~64 B | malloc/free per CASE branch |

**Peak kompilering (v7.9.3):**
```
Lille program (100 linjer):    ~22 KB peak  (128 AST nodes×84B + parser + compiler + 2KB bytecode)
Mellem program (500 linjer):   ~32 KB peak  (256 AST nodes×84B + 4KB bytecode)
Stort program (1000+ linjer):  ~55 KB peak  (475 AST nodes×84B + 8KB bytecode + func registry)

Alt frigives efter kompilering ✓ (recovery til permanent level)
FØR v7.9.3: peak var 36–94 KB — NU: 22–55 KB (40% reduktion)
```

#### Runtime VM (stack-allokeret — per execution cycle)

| Komponent | Størrelse | Placering |
|-----------|-----------|-----------|
| `st_vm_state_t` | **~660 B** | Stack (lokalt i execute) |
| ↳ `stack[64]` á 8 bytes | 512 B | Execution value stack |
| ↳ `error_msg[128]` | 128 B | |
| ↳ pc + halted + error + pointer | 20 B | |
| Lokale variabler i VM dispatch | ~200 B | Stack |
| **Per program per cycle** | **~860 B** | **Frigives efter cycle** |
| **4 programmer sekventielt** | **~860 B** | Genbruges (ikke concurrent) |

**Nøglepointe:** VM kører programmer sekventielt i `loop()` — kun ét sæt VM-state ad gangen.
Ingen malloc under execution — kun stack. Zero heap-påvirkning under drift.

#### Heap Tidslinje (typisk boot → drift)

```
BOOT:     [██████░░░░░░░░░░░░░░░░░░░░░░░] 18.1 KB permanent (structs, ingen compiled)
           ↓ config_load restorer + recompiler fra SPIFFS
COMPILE:  [████████████████████░░░░░░░░░░] 18.1 + 53 KB = 71.1 KB PEAK (temp)
           ↓ AST + parser + compiler + temp bytecode frigives
LOADED:   [████████░░░░░░░░░░░░░░░░░░░░░] 20.9 KB (+ dynamiske instructions + stateful)
           ↓ VM cycle (stack, ikke heap)
CYCLE:    [████████░░░░░░░░░░░░░░░░░░░░░] 20.9 KB + 860 B stack (released)
```

#### Optimeringer (samlet oversigt)

| Optimering | Version | Før | Efter | Besparelse |
|-----------|---------|-----|-------|------------|
| AST node størrelse | v7.2.1 | 1,920 B | 161 B | **93% ↓** |
| AST pool (256 nodes) | v7.2.1 | 491 KB | 41 KB | **450 KB ↓** |
| Parser/compiler allokering | v7.2.1 | Permanent | Temporær | **4.3 KB ↓** idle |
| Try-decreasing pool | v7.2.1 | Fast 512 | 512→32 adaptive | **Fragmentation-robust** |
| `instructions[1024]` → dynamisk | v7.5.0 | 8,192 B/prog | ~800 B/prog typ | **~29.6 KB ↓** (4 prog) |
| `var_names[32][64]` → `[32][32]` | v7.5.0 | 2,048 B/prog | 1,024 B/prog | **4.0 KB ↓** (4 prog) |
| `name[64]` → `[32]` (bytecode) | v7.5.0 | 64 B | 32 B | **128 B ↓** (4 prog) |
| `func_name[64]` → `[32]` (func_def/entry) | v7.5.0 | 64 B each | 32 B each | **~2 KB ↓** (registry) |
| `last_error[128]` → `[64]` | v7.5.0 | 128 B/prog | 64 B/prog | **256 B ↓** (4 prog) |
| **v7.5.0 subtotal** | | | | **~34 KB ↓ permanent** |

---

**v7.2.0 — ES32D26 Analog I/O (Planned):**
- **FEAT-034:** 4x 0-10V spændings-input (Vi1-Vi4) — ADC kalibrering + skalering 🟡 HIGH
- **FEAT-035:** 4x 4-20mA strøm-input (Ii1-Ii4) — ADC kalibrering + offset 🟡 HIGH
- **FEAT-036:** 2x analog output DAC (AO1-AO2) — 0-10V via DAC1/DAC2 🟠 MEDIUM
- **FEAT-037:** Analog Modbus register mapping + CLI (`set analog`/`show analog`) 🟡 HIGH

### ⚠️ CRITICAL Bugs (MUST FIX)
- **BUG-001:** ST Logic vars not visible in Modbus (IR 220-251)
- **BUG-002:** Type checking on variable bindings
- **BUG-015:** HW Counter initialization
- **BUG-016:** Running bit control
- **BUG-017:** Auto-start feature
- **BUG-020:** Manual register config disabled
- **BUG-024:** Counter truncation fix
- **BUG-025:** Register overlap checking
- **BUG-026:** Binding allocator cleanup
- **BUG-028:** Register spacing for 64-bit counters
- **BUG-029:** Compare edge detection
- **BUG-030:** Compare value Modbus access
- **BUG-031:** Counter write lock i Modbus handlers (FIXED v4.2.5)
- **BUG-032:** ST parser buffer overflow (FIXED v4.2.5)
- **BUG-033:** Variable declaration bounds (FIXED v4.2.5)
- **BUG-046:** ST datatype keywords collision (FIXED v4.3.1 Build #676)
- **BUG-047:** Register allocator not freed on delete (FIXED v4.3.2 Build #691)
- **BUG-049:** ST Logic cannot read from Coils (FIXED v4.3.3 Build #703)
- **BUG-050:** VM arithmetic operators don't support REAL (FIXED v4.3.4 Build #708)
- **BUG-052:** VM operators mangler type tracking (FIXED v4.3.6 Build #714)
- **BUG-053:** SHL/SHR operators virker ikke (FIXED v4.3.7 Build #717)
- **BUG-054:** FOR loop body aldrig eksekveret (FIXED v4.3.8 Build #720)
- **BUG-055:** Modbus Master CLI commands ikke virker (FIXED v4.4.0 Build #744)
- **BUG-056:** Buffer overflow i compiler symbol table (FIXED v4.4.3)
- **BUG-059:** Comparison operators ignorerer REAL type (FIXED v4.4.3)
- **BUG-067:** Lexer strcpy buffer overflow (FIXED v4.4.4)
- **BUG-105:** INT type skal være 16-bit, ikke 32-bit (FIXED v5.0.0 Build #822)
- **BUG-124:** Counter 32/64-bit DINT/DWORD register byte order (FIXED v5.0.0 Build #834)
- **BUG-125:** ST Logic multi-register byte order DINT/DWORD/REAL (FIXED v5.0.0 Build #860)
- **BUG-133:** Modbus Master request counter reset mangler (FIXED v4.5.2 Build #917)
- **BUG-134:** MB_WRITE DINT arguments sender garbage data (FIXED v4.6.1 Build #919)
- **BUG-135:** MB_WRITE_HOLDING mangler value type validering (FIXED v4.6.1 Build #919)
- **BUG-136:** MB_WRITE_COIL mangler value type validering (FIXED v4.6.1 Build #919)
- **BUG-146:** Use-after-free i config_save.cpp (FIXED v4.7.3 Build #995)
- **BUG-147:** Buffer underflow i modbus_frame.cpp (FIXED v4.7.3 Build #995)
- **BUG-155:** Buffer overflow i st_token_t.value (FIXED Build #1020)
- **BUG-156:** Manglende validation af function argument count (FIXED Build #1018)
- **BUG-157:** Stack overflow risk i parser recursion (FIXED Build #1018)
- **BUG-158:** NULL pointer dereference i st_vm_exec_call_builtin (FIXED Build #1018)
- **BUG-183:** start_value kun uint16_t - begrænser 32/64-bit counters (FIXED Build #1077)
- **BUG-201:** ESP-IDF middle-wildcard URI routing matcher aldrig (FIXED Build #1162)
- **BUG-202:** Source pool entries ikke null-termineret (FIXED Build #1162)
- **BUG-207:** HTTP server stack_size 4096 for lille til API handlers (FIXED Build #1196)
- **BUG-214:** Backup ST Logic source korruption pga. manglende null-terminering (FIXED Build #1241)
- **BUG-215:** Restore var_maps tabt pga. st_logic_delete() side-effect (FIXED Build #1241)
- **BUG-218:** W5500 Ethernet boot-loop ved flash overflow (FIXED v6.0.8)
- **BUG-219:** Flash forbrug 97%+ forhindrer nye features (FIXED v6.0.8)
- **BUG-230:** `sh config` over telnet trunkeret — kun [SYSTEM] vises (FIXED v6.1.0)
- **BUG-231:** TCP send retry blokerer main loop → 1s output bursts (FIXED v6.1.0)

### 🟡 HIGH Priority (SHOULD FIX)
- **BUG-003:** Bounds checking on var index
- **BUG-004:** Reset bit in control register
- **BUG-014:** Persistent interval save
- **BUG-018:** Show counters bit-width
- **BUG-019:** Race condition in display
- **BUG-021:** Delete counter command
- **BUG-022:** Auto-enable counter
- **BUG-023:** Compare after disable
- **BUG-034:** ISR state volatile cast (FIXED v4.2.6)
- **BUG-035:** Overflow flag auto-clear (FIXED v4.2.6)
- **BUG-038:** ST Logic variable race condition (FIXED v4.2.6)
- **BUG-042:** normalize_alias() missing "auto-load" (FIXED v4.3.0)
- **BUG-043:** "set persist enable on" case sensitivity (FIXED v4.3.0)
- **BUG-045:** Upload mode echo setting (FIXED v4.3.0)
- **BUG-048:** Bind direction parameter ignored (FIXED v4.3.3 Build #698)
- **BUG-051:** Expression chaining fejler for REAL (FIXED v4.3.5 Build #712)
- **BUG-063:** Function argument overflow validation (FIXED v4.4.3)
- **BUG-068:** String parsing null terminator (FIXED v4.4.4)
- **BUG-129:** normalize_alias() returnerer ST-STATS (FIXED v4.4.0 Build #880)
- **BUG-130:** NVS partition for lille til PersistConfig (FIXED v4.5.0 Build #904)
- **BUG-131:** CLI `set id` kommando virker ikke (FIXED v4.5.0 Build #910)
- **BUG-132:** CLI `set baud` kommando virker ikke (FIXED v4.5.0 Build #910)
- **BUG-133:** Modbus Master request counter reset mangler (FIXED v4.5.2 Build #911)
- **BUG-148:** Printf format mismatch i cli_config_regs.cpp (FIXED v4.7.3 Build #995)
- **BUG-159:** Integer overflow i FOR loop (OPEN v4.8.2 - kompleks fix)
- **BUG-160:** Missing NaN/INF validation i arithmetic (FIXED Build #1018)
- **BUG-161:** Division by zero i SCALE function (FIXED Build #1018)
- **BUG-162:** Manglende bounds check på bytecode array (FIXED Build #1018)
- **BUG-163:** Memory leak i parser error paths (OPEN v4.8.2 - behøver refactoring)
- **BUG-164:** Inefficient linear search i symbol lookup (OPEN v4.8.2 - optimization, ikke bug)
- **BUG-CLI-1:** Parameter keyword clarification
- **BUG-CLI-2:** GPIO validation

### 🟠 MEDIUM Priority (NICE TO HAVE)
- **BUG-005:** Binding count lookup optimization
- **BUG-007:** Execution timeout protection
- **BUG-008:** IR update latency
- **BUG-027:** Counter display overflow clamping
- **BUG-039:** CLI compare-enabled parameter (FIXED v4.2.7)
- **BUG-040:** Compare source configurability (FIXED v4.2.8)
- **BUG-041:** Reset-on-read parameter structure (FIXED v4.2.9)
- **BUG-044:** cli_cmd_set_persist_auto_load() case sensitive (FIXED v4.3.0)
- **BUG-057:** Buffer overflow i parser program name (FIXED v4.4.3)
- **BUG-058:** Buffer overflow i compiler bytecode name (FIXED v4.4.3)
- **BUG-060:** NEG operator ignorerer REAL type (FIXED v4.4.3)
- **BUG-065:** SQRT negative validation (FIXED v4.4.4)
- **BUG-072:** DUP type stack sync (FIXED v4.4.4)
- **BUG-073:** SHL/SHR shift overflow (FIXED v4.4.4)
- **BUG-074:** Jump patch silent fail (FIXED v4.4.4)
- **BUG-128:** normalize_alias() mangler BYTECODE/TIMING (FIXED v4.4.0 Build #875)
- **BUG-137:** CLI `read reg` count parameter ignoreres for REAL/DINT/DWORD (FIXED v4.7.1 Build #937)
- **BUG-142:** `set reg STATIC` blokerer HR238-255 fejlagtigt (FIXED v4.7.3 Build #995)
- **BUG-149:** Identical condition i modbus_master.cpp (FIXED v4.7.3 Build #995)
- **BUG-165:** Missing input validation i BLINK function (FIXED Build #1019)
- **BUG-167:** No timeout i lexer comment parsing (FIXED Build #1019)
- **BUG-168:** Missing validation af CASE branch count (FIXED Build #1019)
- **BUG-179:** CLI read i-reg mangler type parameter support (FIXED Build #1048)

### 🔴 CRITICAL Priority (MUST FIX)
- **BUG-181:** DOWN mode underflow wrapper til max_val i stedet for start_value (FIXED Build #1063)
- **BUG-182:** PCNT signed overflow ved 32768 + atol/atoll signed parsing (FIXED Build #1069)
- **BUG-315:** Modbus Master parity/stop-bits ignoreret på ES32D26 — uart1_init hardcodet SERIAL_8N1 (FIXED v7.9.5.1 Build #1895)
- **BUG-316:** Modbus Master post-TX DE-release for hurtig — sidste byte afskåret ved lav baudrate (FIXED v7.9.5.1 Build #1895)
- **BUG-323:** Login modal flash ved sideskift i web GUI — editor/CLI/system startede med show class (FIXED v7.9.6.5 Build #1909)

### 🟡 HIGH Priority (SHOULD FIX)
- **BUG-180:** Counter overflow mister ekstra counts ved wraparound (FIXED Build #1052)
- **BUG-184:** Frequency measurement giver forkerte resultater for DOWN counting (FIXED Build #1074)
- **BUG-203:** /api/config returnerer ufuldstændig konfiguration (FIXED Build #1162)
- **BUG-204:** WWW-Authenticate header tabt pga. httpd response rækkefølge (FIXED Build #1162)
- **BUG-205:** API responses cached af browser - manglende Cache-Control (FIXED Build #1162)
- **BUG-208:** GET /api/logic/{id}/stats stack buffer overflow (FIXED Build #1196)
- **BUG-209:** GET /api/logic/{id}/source timeout - delvis data (FIXED Build #1196)
- **BUG-210:** API source upload kompilerer ikke automatisk (FIXED Build #1197)
- **BUG-226:** Telnet config nested under WiFi — usynlig ved WiFi disabled (FIXED v6.1.0)
- **BUG-227:** normalize_alias() mangler TELNET keyword (FIXED v6.1.0)
- **BUG-229:** Telnet login bruger startup-kopi af credentials (FIXED v6.1.0)

### 🟠 MEDIUM Priority (NICE TO HAVE)
- **BUG-187:** Timer ctrl_reg ikke initialiseret i defaults (FIXED Build #1074)
- **BUG-216:** Backup IP-adresser som rå uint32_t i stedet for dotted strings (FIXED Build #1241)

### 🔵 LOW Priority (COSMETIC)
- **BUG-006:** Counter wrapping at 65535
- **BUG-011:** Variable naming (`coil_reg`)
- **BUG-126:** st_count redeclaration in cli_show.cpp (FIXED v4.4.0 Build #869)
- **BUG-127:** st_state declaration order (FIXED v4.4.0 Build #869)
- **BUG-138:** ST Logic upload error message generisk (FIXED v4.7.1 Build #940)
- **BUG-169:** Inefficient memory usage i AST nodes (ACCEPTABLE - temporary compilation RAM)
- **BUG-171:** Suboptimal error messages i compiler (FIXED Build #1040)
- **BUG-172:** Missing overflow detection i integer arithmetic (DOCUMENTED Build #1040)
- **BUG-173:** MOD operation med negative operands (DOCUMENTED Build #1040)
- **BUG-175:** FILTER function med zero cycle time (FIXED Build #1040)
- **BUG-176:** HYSTERESIS function med inverterede thresholds (FIXED Build #1019)
- **BUG-177:** strcpy uden bounds check i lexer (FIXED Build #1038)
- **BUG-206:** /api/ trailing slash returnerer 404 (FIXED Build #1162)
- **BUG-217:** Backup boolean felter inkonsistente int vs true/false (FIXED Build #1241)
- **BUG-228:** Telnet banner viser "Telnet Server (v3.0)" i stedet for hostname (FIXED v6.1.0)

### ✔️ NOT BUGS (DESIGN CHOICES)
- **BUG-013:** Binding display order (intentional)
- **BUG-166:** Race condition i stateful storage (FALSE POSITIVE - single-threaded)
- **BUG-170:** millis() wraparound (unsigned arithmetic handles it correctly)
- **BUG-185:** Timer Mode 2 trigger_level (legacy parameter, Modbus-triggered design)
- **BUG-186:** Timer Mode 1 duration=0 (intentional "instant sequence" feature)
- **BUG-188:** ISR vs HW underflow inkonsistens (korrekt for edge-triggered vs delta-based)
- **BUG-189:** Timer Mode 4 læser COIL (bevidst design for Modbus control)

## Status Legend

| Symbol | Meaning |
|--------|---------|
| ✅ FIXED | Bug implemented and verified |
| ❌ OPEN | Bug identified but not fixed |
| ✔️ NOT A BUG | Determined to be design choice |
| ✔️ DESIGN | Intentional behavior, documented |
| 🔴 CRITICAL | System breaks without fix |
| 🟡 HIGH | Significant impact, should fix soon |
| 🟠 MEDIUM | Noticeable impact, nice to fix |
| 🔵 LOW | Minor issue, cosmetic only |

## How Claude Should Use This

**When working on code changes:**
1. Check BUGS_INDEX.md first (this file) - ~10 seconds to skim
2. Note which bugs might be affected by your changes
3. If you need details, go to BUGS.md and search for specific BUG-ID
4. Before committing: Update BUGS_INDEX.md status if you fixed a bug
5. Update BUGS.md detailed section only if implementing significant fix

**Example workflow:**
```
Task: Modify ST Logic binding logic
1. Skim BUGS_INDEX.md → See BUG-002, BUG-005, BUG-012, BUG-026
2. Check if your changes impact these areas
3. If modifying binding code → might affect BUG-026
4. Read BUGS.md § BUG-026 for implementation details
5. After fix → Update BUGS_INDEX.md row for BUG-026
```

## File Organization

- **BUGS_INDEX.md** (THIS FILE): Compact 1-liner summary of all bugs (tokens: ~500)
- **BUGS.md**: Full detailed analysis with root causes, test results, code references (tokens: ~5000+)

Use BUGS_INDEX.md for quick navigation, BUGS.md for deep dives.
