# CLAUDE.md Index - Quick Navigation

**Purpose:** Fast navigation to Claude Code project documentation. Start here!

## 🚀 Quick Links by Task

| Need | Read This | Time | Tokens |
|------|-----------|------|--------|
| **First time setup** | [`CLAUDE_SETUP.md`](CLAUDE_SETUP.md) | 5 min | ~200 |
| **Before ANY code change** | [`CLAUDE_WORKFLOW.md`](CLAUDE_WORKFLOW.md) | 10 min | ~400 |
| **Architecture deep dive** | [`CLAUDE_ARCH.md`](CLAUDE_ARCH.md) | 30 min | ~1000 |
| **Project overview** | See below | 2 min | ~100 |
| **Bug tracking** | [`BUGS_INDEX.md`](BUGS_INDEX.md) | 10 sec | ~500 |

---

## ⚡ Critical Rules (5 seconds)

✅ **LANGUAGE:** Always respond in Danish
✅ **WORKING DIR:** Only `C:\Projekter\Modbus_server_slave_ESP32\*.*`
✅ **BUGS FIRST:** Check [`BUGS_INDEX.md`](BUGS_INDEX.md) before ANY code change
✅ **GIT SAFETY:** Warn before destructive git operations
✅ **SEE DETAILS:** Read [`CLAUDE_SETUP.md`](CLAUDE_SETUP.md) for full rules

---

## 📋 Project at a Glance

**ESP32 Modbus RTU Server** - Refactored from Arduino Mega2560

| Aspect | Details |
|--------|---------|
| **Microcontroller** | ESP32-WROOM-32 (240MHz dual-core) |
| **Interface** | RS-485 Modbus RTU (UART0: Slave, UART1: Master) |
| **Architecture** | 50+ modular .cpp/.h files |
| **Version** | v6.1.0 (Build #1350) |
| **Main Components** | Modbus Master/Slave, Counters, Timers, ST Logic, CLI, Ethernet (W5500) |

**Key improvement:** Monolithic code → Modular architecture with hardware abstraction layers

---

## 📁 File Organization

```
CLAUDE.md-related documentation (THIS SECTION):
├─ CLAUDE_INDEX.md (THIS FILE) - Navigation hub
├─ CLAUDE_SETUP.md - Security & workspace rules (~200 tokens)
├─ CLAUDE_WORKFLOW.md - Code modification workflow (~400 tokens)
└─ CLAUDE_ARCH.md - Architecture & file reference (~1000 tokens)

Bug tracking:
├─ BUGS_INDEX.md - Quick bug reference (~500 tokens)
└─ BUGS.md - Full detailed bugs (5000+ tokens)

Test documentation:
└─ tests/ - Strukturerede testplaner
   ├─ TEST_INDEX.md - Test navigation hub
   ├─ API_TEST_PLAN.md - HTTP REST API tests (41 tests)
   ├─ ST_TEST_OPERATORS.md - Operator tests (20 tests)
   ├─ ST_TEST_BUILTINS.md - Builtin function tests (23 tests)
   ├─ ST_TEST_TIMERS.md - TON/TOF/TP tests (6 tests)
   ├─ ST_TEST_GPIO.md - GPIO & Hardware tests (4 tests)
   ├─ ST_TEST_CONTROL.md - Control structure tests (6 tests)
   ├─ ST_TEST_TYPES.md - Type system tests (12 tests)
   └─ ST_TEST_COMBINED.md - Combined tests (10 tests)

Project files:
├─ src/ - All C++ implementation files (30+)
├─ include/ - All headers (.h files)
├─ docs/ - Additional documentation
└─ platformio.ini - Build configuration
```

---

## 🔧 Common Tasks

### "I need to modify a function"
1. Check [`BUGS_INDEX.md`](BUGS_INDEX.md) for relevant bugs
2. Read [`CLAUDE_WORKFLOW.md`](CLAUDE_WORKFLOW.md) for workflow
3. If needed, review [`CLAUDE_ARCH.md`](CLAUDE_ARCH.md) for context
4. Make changes
5. Update [`BUGS_INDEX.md`](BUGS_INDEX.md) if you fixed something

### "I need to understand the architecture"
1. Read [`CLAUDE_ARCH.md`](CLAUDE_ARCH.md) for overview
2. Check file reference table in same document
3. Use `grep` to find specific functions

### "I'm setting up for first time"
1. Read [`CLAUDE_SETUP.md`](CLAUDE_SETUP.md)
2. Understand working directory restrictions
3. Read [`CLAUDE_WORKFLOW.md`](CLAUDE_WORKFLOW.md)

### "Something is broken"
1. Check [`BUGS_INDEX.md`](BUGS_INDEX.md) - might already be known
2. Read [`BUGS.md`](BUGS.md) for details on that bug
3. If new bug, document it in BUGS.md

### "I need to run tests"
1. Open [`tests/TEST_INDEX.md`](tests/TEST_INDEX.md) for overview
2. Choose relevant test file (API, ST operators, etc.)
3. Follow test procedures with copy/paste CLI commands

---

## 📚 Reference Cards

### Version Numbers
- **Current:** v6.1.0
- **Format:** vMAJOR.MINOR.PATCH
- **File:** See `include/constants.h`
- **Changelog:** See `CHANGELOG.md`

### Build System
- **Compiler:** PlatformIO (pio)
- **Board:** ESP32 DOIT DevKit V1
- **Build:** `pio run`
- **Upload:** `pio run -t upload`
- **Monitor:** `pio device monitor -b 115200`

### Key Technologies
- **Language:** C/C++ (Arduino framework)
- **Protocol:** Modbus RTU (Serial)
- **Config:** NVS (non-volatile storage)
- **ST Logic:** Custom bytecode compiler & VM

---

## ⏱️ Token Usage Guide

**Use this file when:**
- You're new to the project (entry point)
- You need to know what file to read
- You want quick project overview
- Time: 1-2 minutes, Tokens: ~100-150

**Don't read CLAUDE.md directly anymore:**
- It's now split into focused modules
- Use the INDEX to find what you need
- Saves ~80% tokens compared to reading full CLAUDE.md

---

## 🎯 Files to Read Before Coding

**Mandatory (every time):**
1. [`BUGS_INDEX.md`](BUGS_INDEX.md) - 10 seconds, ~500 tokens

**Recommended (first-time or complex changes):**
2. [`CLAUDE_WORKFLOW.md`](CLAUDE_WORKFLOW.md) - 5-10 minutes, ~400 tokens
3. [`CLAUDE_ARCH.md`](CLAUDE_ARCH.md) - 20-30 minutes, ~1000 tokens (for deep context)

**Optional:**
- [`CLAUDE_SETUP.md`](CLAUDE_SETUP.md) - Only if changing working directory or security rules

---

## 🔐 Security Boundary (30 seconds)

```
✅ ALLOWED:
  - Read/write files in: C:\Projekter\Modbus_server_slave_ESP32\*.*
  - Execute commands there
  - Create/delete files and folders
  - Git operations (with warnings for destructive ops)

❌ NOT ALLOWED:
  - Read/write outside that folder
  - Execute commands outside that folder
  - Force push to main
  - Hard reset git history (without warning)

EXCEPTION:
  - Only modify CLAUDE_SETUP.md to change these rules
```

**Full details:** See [`CLAUDE_SETUP.md`](CLAUDE_SETUP.md)

---

## 📞 Getting Help

- **"How do I...?"** → Check this INDEX first
- **"What's the workflow?"** → [`CLAUDE_WORKFLOW.md`](CLAUDE_WORKFLOW.md)
- **"Where's the code for X?"** → [`CLAUDE_ARCH.md`](CLAUDE_ARCH.md) has file reference
- **"Is this a known bug?"** → [`BUGS_INDEX.md`](BUGS_INDEX.md)
- **"Tell me about that bug..."** → [`BUGS.md`](BUGS.md) and search for BUG-ID

---

## ✅ Recommended Reading Order

### First Time
1. This file (CLAUDE_INDEX.md) - ~2 minutes
2. [`CLAUDE_SETUP.md`](CLAUDE_SETUP.md) - ~5 minutes
3. [`CLAUDE_WORKFLOW.md`](CLAUDE_WORKFLOW.md) - ~10 minutes
4. [`CLAUDE_ARCH.md`](CLAUDE_ARCH.md) - ~30 minutes (at your own pace)

### Before Each Code Change
1. [`BUGS_INDEX.md`](BUGS_INDEX.md) - ~10 seconds (MANDATORY)
2. Skim relevant section in [`CLAUDE_WORKFLOW.md`](CLAUDE_WORKFLOW.md) - ~2 minutes
3. Code!

### When Stuck
1. Check [`BUGS_INDEX.md`](BUGS_INDEX.md) for known issues
2. Read [`BUGS.md`](BUGS.md) for that bug
3. Check [`CLAUDE_ARCH.md`](CLAUDE_ARCH.md) file reference for code location

---

**Last Updated:** 2026-02-25
**Version:** v6.1.0
**Build:** #1350
**Status:** ✅ Active & Maintained
