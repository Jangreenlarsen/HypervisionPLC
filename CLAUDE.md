# CLAUDE.md - ESP32 Modbus RTU Server Documentation

**This file is now a navigation hub.** Detailed documentation has been split into focused modules to reduce token usage and improve maintainability.

---

## 🚀 START HERE

**Your first visit?** 👉 Read [`CLAUDE_INDEX.md`](CLAUDE_INDEX.md) (2 minutes, ~150 tokens)

**Need to make a code change?** 👉 Check [`BUGS_INDEX.md`](BUGS_INDEX.md) first (10 seconds, ~500 tokens)

---

## 📚 Documentation Modules

### Quick Navigation
- **[`CLAUDE_INDEX.md`](CLAUDE_INDEX.md)** - Main entry point, quick links by task (~300 tokens)
- **[`BUGS_INDEX.md`](BUGS_INDEX.md)** - Bug tracking index, all 26 bugs at a glance (~500 tokens)

### Setup & Rules
- **[`CLAUDE_SETUP.md`](CLAUDE_SETUP.md)** - Security rules, working directory, git safety (~200 tokens)

### Development Workflow
- **[`CLAUDE_WORKFLOW.md`](CLAUDE_WORKFLOW.md)** - Code modification guidelines, commit workflow (~400 tokens)

### Architecture & Reference
- **[`CLAUDE_ARCH.md`](CLAUDE_ARCH.md)** - Layered architecture, file reference, data flows (~1000 tokens)

### Bug Tracking
- **[`BUGS.md`](BUGS.md)** - Full detailed bug analysis (5000+ tokens, use sparingly)

---

## ⏱️ Reading Guide by Role

### 👨‍💻 Developer (making code changes)
1. **First time only:** Read [`CLAUDE_SETUP.md`](CLAUDE_SETUP.md) (understand rules)
2. **Before EVERY change:** Check [`BUGS_INDEX.md`](BUGS_INDEX.md) (10 seconds)
3. **Then read:** [`CLAUDE_WORKFLOW.md`](CLAUDE_WORKFLOW.md) (refresh memory)
4. **Make changes** following guidelines
5. **Commit** with clear message

**Total time:** First: 15 minutes | Subsequent: 2 minutes per change

### 🏗️ Architecture Review (understanding structure)
1. Read [`CLAUDE_INDEX.md`](CLAUDE_INDEX.md) (quick overview)
2. Read [`CLAUDE_ARCH.md`](CLAUDE_ARCH.md) (detailed architecture)
3. Use grep/search to find specific code locations

**Total time:** 30-40 minutes

### 🐛 Bug Fix (implement known bug fix)
1. Find bug ID in [`BUGS_INDEX.md`](BUGS_INDEX.md)
2. Read details in [`BUGS.md`](BUGS.md) (section for that bug)
3. Follow [`CLAUDE_WORKFLOW.md`](CLAUDE_WORKFLOW.md) (commit guidelines)
4. Update [`BUGS_INDEX.md`](BUGS_INDEX.md) status

**Total time:** 20-60 minutes (depending on complexity)

---

## 🎯 Key Principles

### 1. LANGUAGE: Always respond in Danish
Regardless of user input, Claude Code uses Danish for this project.

### 2. SECURITY: Working directory boundary
Only work within: `C:\Projekter\Modbus_server_slave_ESP32\*.*`

See [`CLAUDE_SETUP.md`](CLAUDE_SETUP.md) for full rules.

### 3. BUGS FIRST: Check before any code change
Always read [`BUGS_INDEX.md`](BUGS_INDEX.md) to identify relevant bugs.

Takes 10 seconds, saves hours of debugging.

### 4. MODULAR ARCHITECTURE
30+ focused files, each with single responsibility. No circular dependencies.

See [`CLAUDE_ARCH.md`](CLAUDE_ARCH.md) for details.

---

## 📊 Token Usage Summary

| Action | Traditional | New System | Savings |
|--------|-----------|-----------|---------|
| Check for relevant bugs | 5000+ | 500 | **90%** ↓ |
| Understand architecture | 5000+ | 1000 | **80%** ↓ |
| Commit workflow | 2000+ | 400 | **80%** ↓ |
| Setup/rules review | 1000+ | 200 | **80%** ↓ |

**Total for routine change:** Old: 8000+ tokens → New: 1000 tokens (87.5% savings!)

---

## 🔄 Document Structure

```
CLAUDE.md (THIS FILE)
├── Navigation hub
├── Quick principles
└── Links to detailed modules

├─ CLAUDE_INDEX.md (START HERE)
│  └─ Quick links, project overview, common tasks
│
├─ CLAUDE_SETUP.md (SECURITY)
│  └─ Working directory, git safety, environment checks
│
├─ CLAUDE_WORKFLOW.md (DEVELOPMENT)
│  └─ Code modification, commit workflow, guidelines
│
├─ CLAUDE_ARCH.md (ARCHITECTURE)
│  └─ Layer breakdown, file reference, data flows
│
└─ BUGS_* (BUG TRACKING)
   ├─ BUGS_INDEX.md (ALWAYS READ FIRST)
   └─ BUGS.md (detailed analysis, read as needed)
```

---

## ⚡ 30-Second Summary

**What is this project?**
ESP32 Modbus RTU server with counters, timers, and ST Logic programs.

**What should I do first?**
Read [`CLAUDE_INDEX.md`](CLAUDE_INDEX.md).

**Before I code?**
Check [`BUGS_INDEX.md`](BUGS_INDEX.md) (10 seconds).

**When I get stuck?**
1. Check [`CLAUDE_ARCH.md`](CLAUDE_ARCH.md) for file locations
2. Check [`BUGS_INDEX.md`](BUGS_INDEX.md) for known issues
3. Read [`BUGS.md`](BUGS.md) for bug details

**When I commit?**
Follow [`CLAUDE_WORKFLOW.md`](CLAUDE_WORKFLOW.md) guidelines.

---

## 📁 Quick File Reference

**Most used:**
- `src/cli_commands.cpp` - CLI set commands
- `src/counter_engine.cpp` - Counter orchestration
- `src/timer_engine.cpp` - Timer state machines
- `src/st_logic_engine.cpp` - ST Logic scheduler

**Configuration:**
- `include/constants.h` - ALL constants
- `include/types.h` - ALL struct definitions
- `platformio.ini` - Build configuration

**Storage:**
- `src/config_load.cpp` - Load from NVS
- `src/config_save.cpp` - Save to NVS
- `src/config_apply.cpp` - Apply to running system

See [`CLAUDE_ARCH.md`](CLAUDE_ARCH.md) for complete file reference.

---

## 🔐 Critical Rules (Read [`CLAUDE_SETUP.md`](CLAUDE_SETUP.md) for details)

```
✅ ALLOWED:
  - Read/write in: C:\Projekter\Modbus_server_slave_ESP32\*.*
  - Git operations (with warnings for destructive ops)
  - Create/delete files and folders

❌ NOT ALLOWED:
  - Read/write outside that folder
  - Execute outside that folder
  - Force push to main without warning
```

---

## 📞 Common Questions

**Q: Where is the code for X?**
A: Check file reference in [`CLAUDE_ARCH.md`](CLAUDE_ARCH.md)

**Q: How do I add a new feature?**
A: Read [`CLAUDE_WORKFLOW.md`](CLAUDE_WORKFLOW.md) → Adding a New Feature section

**Q: Is this a known bug?**
A: Check [`BUGS_INDEX.md`](BUGS_INDEX.md)

**Q: How do I commit safely?**
A: Follow [`CLAUDE_WORKFLOW.md`](CLAUDE_WORKFLOW.md) → Commit Workflow

**Q: I need to understand the architecture**
A: Read [`CLAUDE_ARCH.md`](CLAUDE_ARCH.md)

---

## 🎓 Learning Path

### Path 1: Quick Start (First-time contributor)
1. [`CLAUDE_INDEX.md`](CLAUDE_INDEX.md) - 2 min
2. [`CLAUDE_SETUP.md`](CLAUDE_SETUP.md) - 5 min
3. [`CLAUDE_WORKFLOW.md`](CLAUDE_WORKFLOW.md) - 10 min
4. Ready to code!

### Path 2: Deep Architecture (First-time reviewer)
1. [`CLAUDE_INDEX.md`](CLAUDE_INDEX.md) - 2 min
2. [`CLAUDE_ARCH.md`](CLAUDE_ARCH.md) - 30 min
3. Understand structure & dependencies
4. Read specific files as needed

### Path 3: Bug Fix (Routine task)
1. [`BUGS_INDEX.md`](BUGS_INDEX.md) - 10 sec
2. Find BUG-ID
3. Read [`BUGS.md`](BUGS.md) § BUG-ID - 5 min
4. Implement fix following [`CLAUDE_WORKFLOW.md`](CLAUDE_WORKFLOW.md)
5. Update [`BUGS_INDEX.md`](BUGS_INDEX.md)

---

## 📈 Project At a Glance

| Aspect | Details |
|--------|---------|
| **Target** | ESP32-WROOM-32 (240MHz dual-core) |
| **Protocol** | Modbus RTU (RS-485) |
| **Architecture** | 30+ modular .cpp/.h files |
| **Version** | v6.1.0 (Build #1350) |
| **Components** | Counters, Timers, ST Logic, CLI, Ethernet (W5500) |
| **Key Feature** | IEC 61131-3 Type System (INT/DINT/REAL) |

---

## ✅ Navigation Checklist

- [ ] I read [`CLAUDE_INDEX.md`](CLAUDE_INDEX.md)
- [ ] I understand the working directory boundary (see [`CLAUDE_SETUP.md`](CLAUDE_SETUP.md))
- [ ] I know to check [`BUGS_INDEX.md`](BUGS_INDEX.md) before coding
- [ ] I can find files using [`CLAUDE_ARCH.md`](CLAUDE_ARCH.md)
- [ ] I understand the commit workflow from [`CLAUDE_WORKFLOW.md`](CLAUDE_WORKFLOW.md)

All checked? Ready to code! 🚀

---

**Last Updated:** 2026-02-25
**Version:** v6.1.0
**Build:** #1350
**Status:** ✅ Navigation Hub (Modular Documentation)

**Remember:** The value of documentation is not in its size, but in its usability. Start with [`CLAUDE_INDEX.md`](CLAUDE_INDEX.md) →
