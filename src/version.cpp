// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file version.cpp
 * @brief Version information and changelog implementation
 */

#include "version.h"
#include "debug.h"

void version_print_changelog(void) {
  debug_println("\n=== Changelog ===\n");
  debug_println("v2.2.0 - ST Logic Modbus Integration & Extended Registers");
  debug_println("  - Modbus registers expanded: 0-255 (from 0-159)");
  debug_println("  - Dedicated ST Logic registers: 200-251");
  debug_println("  - INPUT registers 200-219: ST program status & execution stats");
  debug_println("  - INPUT registers 220-251: ST variable output values");
  debug_println("  - HOLDING registers 200-203: ST program control (enable/disable/reset)");
  debug_println("  - HOLDING registers 204-235: ST variable input values");
  debug_println("  - Remote control: Enable/disable ST programs via Modbus");
  debug_println("  - Remote monitoring: Read status, execution count, error count");
  debug_println("  - GPIO mapping limit increased: 32 (from 8)");
  debug_println("  - Enhanced documentation with Modbus integration guide");
  debug_println("\nv2.1.0 - ST Logic Variable Binding & Persistence");
  debug_println("  - Intuitive variable-name based bind syntax");
  debug_println("  - Unified variable mapping system (GPIO + ST)");
  debug_println("  - Persistent variable bindings via NVS");
  debug_println("  - Multi-line upload mode for easy program entry");
  debug_println("  - Error diagnostics with statistics tracking");
  debug_println("\nv2.0.0 - Structured Text Logic Mode (MAJOR)");
  debug_println("  - IEC 61131-3 Structured Text (ST) language support");
  debug_println("  - ST Lexer (tokenization of all IEC 61131-3 keywords/operators)");
  debug_println("  - ST Parser (recursive descent, AST construction)");
  debug_println("  - Support: IF/THEN/ELSE, FOR, WHILE, REPEAT, CASE statements");
  debug_println("  - Data types: BOOL, INT, DWORD, REAL");
  debug_println("  - Variable declarations: VAR, VAR_INPUT, VAR_OUTPUT, CONST");
  debug_println("  - Full operator support (arithmetic, logical, relational, bitwise)");
  debug_println("  - Bytecode compiler & VM (Phase 2 design)");
  debug_println("  - 4 independent logic programs with Modbus integration");
  debug_println("  - IEC 61131-3 ST-Light profile compliance");
  debug_println("\nv1.0.0 - Initial release");
  debug_println("  - Counter Engine (SW, SW-ISR, HW-PCNT modes)");
  debug_println("  - Timer Engine (One-shot, Monostable, Astable, Input-triggered)");
  debug_println("  - CLI interface (show/set commands)");
  debug_println("  - GPIO driver abstraction");
  debug_println("  - PCNT hardware counter support");
  debug_println("  - Configuration persistence (NVS)");
  debug_println("\nv0.1.0 - Mega2560 port from v3.6.5");
  debug_println("  - Baseline architecture porting complete\n");
}
