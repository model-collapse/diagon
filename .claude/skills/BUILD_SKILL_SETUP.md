# Build Skill Setup Complete

## What Was Created

### 1. Build Skill (`.claude/skills/build_lucene_lucene.json`)
A Claude Code skill that automates the SOP-compliant build process.

**Features:**
- Follows BUILD_SOP.md exactly
- Configurable targets: core, benchmarks, tests, all
- Automatic ICU verification
- Clean build option
- Parallel build jobs control
- Error handling with troubleshooting hints

### 2. Helper Script (`scripts/build_lucene.sh`)
A bash script that mirrors the skill functionality for CLI usage.

**Usage:**
```bash
./scripts/build_lucene.sh [target] [clean] [jobs]
# Examples:
./scripts/build_lucene.sh core true 8
./scripts/build_lucene.sh benchmarks true 16
./scripts/build_lucene.sh all false 8
```

### 3. Documentation Files
- `.claude/skills/README.md` - Skill usage documentation
- `QUICKSTART.md` - Quick start guide for new users
- Updated `CLAUDE.md` - Added skill references

## How to Use the Build Skill

### In Claude Code CLI:

```bash
# Basic builds
/build_lucene                          # Build core library only
/build_lucene target=benchmarks        # Build core + benchmarks
/build_lucene target=tests            # Build core + tests
/build_lucene target=all              # Build everything

# With options
/build_lucene target=benchmarks clean=true verify=true jobs=16
/build_lucene target=core clean=false  # Incremental build (use cautiously)
```

### In Shell:

```bash
# Using helper script
./scripts/build_lucene.sh              # Core library
./scripts/build_lucene.sh benchmarks   # Core + benchmarks
./scripts/build_lucene.sh all true 16  # Everything, clean, 16 jobs

# Manual build
rm -rf build && mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
      -DDIAGON_BUILD_BENCHMARKS=ON ..
make diagon_core -j8
```

## What the Skill Does

### Step-by-Step Process:

1. **Clean** (if clean=true)
   - Removes stale build directory
   - Creates fresh build directory
   - Ensures no CMake cache issues

2. **Configure CMake**
   - Sets Release mode
   - Disables LTO (prevents ICU errors)
   - Enables benchmarks and tests
   - Uses `-O3 -march=native` for performance

3. **Build Core**
   - Compiles diagon_core library first
   - Catches compilation errors early
   - Foundation for all other targets

4. **Verify ICU** (if verify=true)
   - Checks `ldd` output for ICU libraries
   - Prevents benchmark linking errors
   - Reports issues immediately

5. **Build Target**
   - Builds requested target (benchmarks/tests/all)
   - Uses parallel jobs for speed
   - Reports build status

6. **Report Results**
   - Build success/failure status
   - ICU verification result
   - Location of artifacts
   - Suggested next steps

## Advantages Over Manual Builds

| Aspect | Manual Build | Build Skill |
|--------|--------------|-------------|
| Remember flags | You must remember | Automatic |
| ICU verification | Manual check | Automatic |
| Error handling | Read BUILD_SOP | Built-in hints |
| Consistency | Varies | Always SOP-compliant |
| Documentation | Separate | Integrated |
| Speed | Same | Same |

## Build Modes Comparison

| Mode | LTO | Speed | Reliability | Use Case |
|------|-----|-------|-------------|----------|
| **Skill/Script** | Off | Fast | ✅ High | **Recommended** |
| Manual (correct) | Off | Fast | ✅ High | OK if careful |
| Manual (LTO) | On | Fast | ❌ Breaks | **Avoid** |
| Debug | Off | Slow | ✅ High | Development only |

## Troubleshooting

### Skill Not Found
```bash
# Verify skill exists
ls -la /home/ubuntu/diagon/.claude/skills/build_lucene.json

# Restart Claude Code or reload configuration
```

### Build Fails
The skill will:
1. Report exact error from compiler
2. Suggest troubleshooting steps
3. Reference BUILD_SOP.md section

Common errors handled:
- ICU version mismatch → Link to BUILD_SOP.md fix
- Compilation errors → Show exact error location
- ZSTD issues → Should be auto-fixed in CMake

### ICU Verification Fails
If `ldd` doesn't show ICU:
1. Check if conda environment is active
2. See BUILD_SOP.md "Error: undefined reference to icu_73"
3. Try system-only build (set PATH)

## Testing the Setup

### Quick Test:

```bash
# Test helper script
./scripts/build_lucene.sh core true 8

# Or in Claude Code:
/build_lucene
```

Expected output:
```
[1/5] Cleaning build directory...
[2/5] Configuring CMake...
[3/5] Building diagon_core...
[4/5] Verifying ICU linking...
✅ ICU linked successfully
[5/5] Building target: core...
✅ Build completed successfully!
```

### Full Test:

```bash
# Build everything
./scripts/build_lucene.sh all

# Run a benchmark
cd build
./benchmarks/SearchBenchmark --benchmark_filter=BM_TermQuerySearch/1000

# Run tests
ctest
```

## Next Steps

1. **Try the skill:** `/build_lucene` in Claude Code
2. **Read docs:** `.claude/skills/README.md`
3. **Run benchmarks:** After building with target=benchmarks
4. **Customize:** Add more skills to `.claude/skills/`

## File Locations

```
/home/ubuntu/diagon/
├── .claude/
│   ├── skills/
│   │   ├── build_lucene.json   # The skill definition
│   │   ├── README.md           # Skill documentation
│   │   └── BUILD_SKILL_SETUP.md  # This file
│   └── settings.local.json     # Permissions (already has make/cmake)
├── scripts/
│   └── build.sh                # Helper script
├── BUILD_SOP.md                # Detailed build procedure
├── QUICKSTART.md               # Quick start guide
└── CLAUDE.md                   # Project guidelines
```

## Future Enhancements

Possible additional skills:
- `/benchmark` - Run benchmarks with common configurations
- `/test` - Run specific test suites
- `/clean` - Clean build artifacts
- `/verify` - Verify build environment
- `/profile` - Build with profiling enabled

To add these, create new JSON files in `.claude/skills/`.

---

**Status:** ✅ Setup Complete
**Version:** 1.0.0
**Date:** 2026-02-09
