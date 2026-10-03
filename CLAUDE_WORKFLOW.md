# CLAUDE Workflow - Code Modification Guidelines

**Read before:** Any code change, bug fix, or refactoring.

---

## ⚡ 30-Second Quick Start

```
Before ANY code change:
1. Check BUGS_INDEX.md for relevant bugs (10 seconds)
2. Make your change (following guidelines below)
3. Update BUGS_INDEX.md if you fixed a bug
4. Git commit with clear message
5. Done!
```

---

## 🐛 Bug Checking Workflow

### MANDATORY: Check bugs FIRST

**Every time before code change:**
1. Open [`BUGS_INDEX.md`](BUGS_INDEX.md)
2. Skim the table (takes 10 seconds)
3. Note any BUG-IDs that might affect your change
4. If found relevant bug:
   - Read [`BUGS.md`](BUGS.md) specific section
   - Follow implementation details
   - Don't reintroduce same bug

**Example:**
```
Task: Modify ST Logic binding logic

1. Skim BUGS_INDEX.md:
   → See BUG-001, BUG-002, BUG-005, BUG-012, BUG-026

2. BUG-026 affects binding! ✓ Must know about it

3. Read BUGS.md § BUG-026 for details

4. Implement fix aware of register allocator

5. Update BUGS_INDEX.md: BUG-026 ❌ OPEN → ✅ FIXED
```

---

## 📝 Code Modification Workflow

### Step 1: Preparation (5 minutes)

- [ ] Check [`BUGS_INDEX.md`](BUGS_INDEX.md) for related bugs
- [ ] Read relevant section in [`CLAUDE_ARCH.md`](CLAUDE_ARCH.md) if needed
- [ ] Understand the code you're about to change
- [ ] Check git status: `git status` (working tree should be clean)

### Step 2: Make Changes

**While coding:**
- Follow existing code patterns (same style, structure)
- Add comments only if logic isn't self-evident
- Don't over-engineer (KISS principle)
- Don't add features beyond the task
- Test locally if possible

**Avoid:**
- ❌ Major refactoring in same commit as bug fix
- ❌ Changing unrelated code
- ❌ Adding "improvements" not requested
- ❌ Modifying code without understanding it first

### Step 3: Testing

```bash
# Always build before committing:
pio clean && pio run

# Check for:
✓ No compilation errors
✓ No new warnings
✓ RAM/Flash usage acceptable
✓ Build numbers incremented
```

**Changed ST compiler/VM/builtins (`src/st_*.cpp`)?** Run the PC test — it compiles the
ST core with g++ and runs real ST programs through it (no hardware needed):

```bash
bash test/st_host/run.sh      # must end with "ALLE TESTS OK"
```

**Changed web pages?** Syntax-check the `<script>` blocks (`node`), and show any text
that comes from the device with `escHtml()` or `textContent` — never raw in `innerHTML`
(BUG-435, SECURITY_INDEX #24).

**Web pages are minified at build time** (`scripts/gzip_web_assets.py`, FEAT-431): indentation,
empty lines, whole `//` comment lines and `<!-- -->` comments are stripped before gzip. Two rules
keep that safe: no multi-line template literals (backticks) and no multi-line content in
`<pre>`/`<textarea>`. Comments AFTER code on a line are left alone.

**Hardware test:** a test PLC can be used when the user provides address/credentials
(OTA via `POST /api/system/ota`, ST tests in a free Logic slot). Never store the
credentials in the repo or in memory, and clean up test configuration afterwards —
note that `set counter` auto-saves the WHOLE config to NVS.

### Step 4: Commit

```bash
# Commit format (use imperative):
git add <changed files>
git commit -m "VERB: Brief description

Optional longer description if complex change.

Generated with Claude Code
Co-Authored-By: Claude Haiku 4.5"
```

**Verb guidelines:**
- `FIX:` - Bug fix
- `FEAT:` - New feature
- `REFACTOR:` - Code restructuring (no logic change)
- `DOCS:` - Documentation only
- `TEST:` - Test additions/fixes
- `CHORE:` - Build, deps, cleanup

### Step 5: Update Bug Tracking

**If you fixed a bug:**
1. Open [`BUGS_INDEX.md`](BUGS_INDEX.md)
2. Find the BUG-ID row
3. Change status: `❌ OPEN` → `✅ FIXED`
4. Update version number if bumped
5. Commit: `git add BUGS_INDEX.md && git commit`

**If it was complex fix:**
1. Also update [`BUGS.md`](BUGS.md) detailed section
2. Add test results
3. Document root cause

---

## 🎯 Common Scenarios

### Scenario 1: Simple Bug Fix

```
Task: Fix typo in CLI output

1. skim BUGS_INDEX.md (1 minute)
2. Edit src/cli_shell.cpp (1 minute)
3. Build: pio run (8 seconds)
4. Commit: git commit -m "FIX: Typo in login message"
5. Done!

Total time: 5 minutes
Total tokens used: ~200 (for bug check + build output)
```

### Scenario 2: Fix Known Bug

```
Task: Implement BUG-026 fix

1. Read BUGS_INDEX.md (10 seconds) → See BUG-026
2. Read BUGS.md § BUG-026 (5 minutes)
3. Edit src/cli_commands_logic.cpp (10 minutes)
4. Build: pio run (8 seconds)
5. Update BUGS_INDEX.md: BUG-026 ❌ OPEN → ✅ FIXED
6. Commit all changes
7. Done!

Total time: 25 minutes
```

### Scenario 3: New Feature

```
Task: Add new counter mode

1. Read BUGS_INDEX.md (quick scan)
2. Read CLAUDE_ARCH.md § Counters (5 minutes)
3. Understand current modes (read counter_*.cpp files)
4. Create new src/counter_new_mode.cpp (20 minutes)
5. Update src/counter_engine.cpp (10 minutes)
6. Build: pio run (8 seconds)
7. Git add & commit: "FEAT: New counter mode"
8. Done!

Total time: 45 minutes
```

---

## 📋 Checklist Before Commit

**Every commit should have:**

- [ ] Code compiles without errors: `pio run` ✓
- [ ] No new warnings introduced
- [ ] Bug tracker (BUGS_INDEX.md) updated if applicable
- [ ] Git commit message is clear and follows format
- [ ] No debug code left in (console.log, printf, etc)
- [ ] Related code still makes sense
- [ ] Not modifying unrelated files

**Before force operations:**

- [ ] I understand what will happen
- [ ] User has been warned
- [ ] User has explicitly approved
- [ ] Backup/stash exists if possible

---

## 🔄 Git Commit Workflow

### Normal Workflow

```bash
# 1. Make changes
vim src/some_file.cpp

# 2. Review what changed
git status
git diff src/some_file.cpp

# 3. Stage & commit
git add src/some_file.cpp
git commit -m "FIX: Description of fix"

# 4. Verify
git log -1

# 5. Done (or push if applicable)
```

### If You Made Mistakes

**Small typo in commit message:**
```bash
# Only if NOT yet pushed:
git commit --amend -m "Fixed message"
```

**Forgot to add a file:**
```bash
# Only if NOT yet pushed:
git add forgotten_file.cpp
git commit --amend -m "Original message"
```

**Major mistake:**
```bash
# Revert last commit (safer option):
git revert HEAD
# Creates NEW commit that undoes the old one
```

---

## ⚠️ Things to Avoid

### 1. Don't commit debug code

```cpp
❌ printf("DEBUG: value = %d\n", val);  // Left in by mistake
❌ debug_println("temp logging");       // Should be removed

✅ // Keep only if actually useful for debugging when needed
✅ Remove debug output before final commit
```

### 2. Don't mix concerns

```
❌ One commit: Fix bug + refactor unrelated code + add feature
✅ Multiple commits: One per concern
```

### 3. Don't modify unnecessarily

```cpp
❌ Change brace style while fixing bug
❌ Reorder includes while fixing bug
❌ Rename variables while fixing bug

✅ Make minimal change to fix bug only
✅ Style/naming changes in separate commit
```

### 4. Don't skip BUGS_INDEX.md

```
❌ Fix bug without updating BUGS_INDEX.md
✅ Update BUGS_INDEX.md in same commit
```

---

## 💡 Code Style Guidelines

**Follow existing patterns:**

### Naming
- Functions: `snake_case` (C style)
- Variables: `snake_case`
- Constants: `UPPER_CASE`
- Types: `CamelCase` (structs)

### Comments
```cpp
// Use this for brief, obvious explanations
// Keep comments close to code they describe

/* Use this for complex logic explanations
   that need multiple lines */

// Avoid over-commenting obvious code
int x = 5;  // ✅ Don't comment unless needed
```

### Line Length
- Keep lines under 100 characters
- Break long lines at logical points
- Indentation: 2 spaces (C style)

### Function Structure
```cpp
// 1. Parameter validation
if (invalid_param) return -1;

// 2. Main logic
do_something();

// 3. Return/cleanup
return result;
```

---

## 🚀 Version Bumping Workflow

**When to bump version:**

- **PATCH** (3.0.0 → 3.0.1): Bug fix only
- **MINOR** (3.0.0 → 3.1.0): New feature
- **MAJOR** (3.0.0 → 4.0.0): Breaking change

**To bump version:**

1. Edit `include/constants.h`:
   ```c
   #define PROJECT_VERSION "3.0.1"  // ← Change here
   ```

2. Edit `CHANGELOG.md`:
   ```markdown
   ## [3.0.1] - 2025-12-16
   ### Fixed
   - Brief description of fix
   ```

3. Commit:
   ```bash
   git add include/constants.h CHANGELOG.md
   git commit -m "VERSION: v3.0.1 - Bug fix description"
   git tag -a v3.0.1 -m "Release v3.0.1: Bug fix"
   ```

---

## 📊 Pre-Commit Mental Checklist

Before you git commit, ask yourself:

1. **Did I check BUGS_INDEX.md?** → ✓ Yes
2. **Does the code work?** → ✓ Yes (pio run succeeded)
3. **Did I accidentally break anything?** → ✓ No
4. **Is the commit message clear?** → ✓ Yes
5. **Should BUGS_INDEX.md be updated?** → ✓ Yes/No (appropriate action taken)
6. **Would another developer understand this change?** → ✓ Yes
7. **Did I leave debug code?** → ✓ No
8. **Is this the minimal change needed?** → ✓ Yes

If all ✓ → **Safe to commit!**

---

## 🆘 Troubleshooting

### Build fails with errors

```bash
# 1. Clean and rebuild
pio clean && pio run

# 2. Check for obvious errors (syntax, includes)
# 3. If compilation error, fix the code
# 4. Try again
```

### Git says "working tree not clean"

```bash
# Option 1: Stash changes (keep them)
git stash
# ... do other work ...
git stash pop

# Option 2: Discard changes (dangerous!)
git checkout -- <file>
```

### "Already up to date" on pull

```bash
# Your local is already latest version
# Nothing to do, continue working
```

### Compilation works but behavior wrong

```bash
# 1. Check git log to see recent changes
git log --oneline -10

# 2. Check if BUGS_INDEX.md has clues
# 3. Compare with working version
# 4. Use git bisect if needed
```

---

## 📞 When Uncertain

If unsure:
1. Ask the user first
2. Reference this document
3. Check [`CLAUDE_ARCH.md`](CLAUDE_ARCH.md) for context
4. Check [`BUGS_INDEX.md`](BUGS_INDEX.md) for known issues
5. Err on the side of caution

---

**Last Updated:** 2026-01-20
**Version:** v5.3.0
**Build:** #1084
**Status:** ✅ Active & Required Reading
