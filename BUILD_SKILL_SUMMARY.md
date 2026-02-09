# Build Skill Setup - Summary

## ✅ Setup Complete

The Diagon build process has been organized as a reusable Claude Code skill, following the BUILD_SOP.md standard operating procedure.

## What You Can Do Now

### 1. Use the `/build_lucene` Skill (Recommended)

In Claude Code CLI, simply type:

```
/build_lucene
```

**Common usage patterns:**
```bash
/build_lucene                          # Build core library only (fastest)
/build_lucene target=benchmarks        # Build core + benchmarks
/build_lucene target=all              # Build everything
/build_lucene target=benchmarks jobs=16  # Use 16 parallel jobs
/build_lucene clean=false             # Incremental build (use cautiously)
```

### 2. Use the Helper Script

For direct shell usage:
```bash
./scripts/build_lucene.sh              # Build core
./scripts/build_lucene.sh benchmarks   # Build core + benchmarks
./scripts/build_lucene.sh all true 16  # Build all, clean, 16 jobs
```

### 3. Manual Build (If Needed)

Follow BUILD_SOP.md for manual builds.

## Files Created

```
.claude/skills/
├── build_lucene.json       # Skill definition
├── README.md               # Usage documentation
└── BUILD_SKILL_SETUP.md    # Setup details

scripts/
└── build.sh                # Bash helper script (executable)

QUICKSTART.md               # Quick start guide
BUILD_SKILL_SUMMARY.md      # This file
```

**Also updated:**
- `CLAUDE.md` - Added skill reference in build section

## Why This Helps

| Before | After |
|--------|-------|
| Remember all CMake flags | Just `/build_lucene` |
| Manually verify ICU | Automatic verification |
| Look up BUILD_SOP.md every time | Built-in error hints |
| Risk of stale cache | Automatic clean builds |
| Inconsistent builds | Always SOP-compliant |

## Key Features

✅ **SOP-Compliant**: Follows BUILD_SOP.md exactly
✅ **Automatic ICU Verification**: Catches linking errors early
✅ **Configurable**: Choose target, jobs, clean mode
✅ **Error Handling**: Helpful troubleshooting messages
✅ **Safe**: Always disables LTO, uses Release mode
✅ **Fast**: Parallel builds with configurable job count

## Quick Test

Try it now:
```bash
# In Claude Code:
/build_lucene

# Or in shell:
./scripts/build_lucene.sh
```

Expected output:
```
[1/5] Cleaning build directory...
[2/5] Configuring CMake (Release, no LTO)...
[3/5] Building diagon_core...
[4/5] Verifying ICU linking...
✅ ICU linked successfully
[5/5] Building target: core...
✅ Build completed successfully!
```

## Next Steps

1. **Try the skill**: `/build_lucene` in Claude Code
2. **Build benchmarks**: `/build_lucene target=benchmarks`
3. **Run benchmarks**: `cd build && ./benchmarks/SearchBenchmark`
4. **Read docs**: `.claude/skills/README.md` for full documentation

## Documentation

- **Skill usage**: `.claude/skills/README.md`
- **Build SOP**: `BUILD_SOP.md`
- **Quick start**: `QUICKSTART.md`
- **Setup details**: `.claude/skills/BUILD_SKILL_SETUP.md`
- **Project guide**: `CLAUDE.md`

## Adding More Skills

To create additional skills (e.g., `/benchmark`, `/test`):

1. Create `.claude/skills/your-skill.json`
2. Follow the format in `build.json`
3. Document in `.claude/skills/README.md`
4. Test the skill

Examples of future skills:
- `/benchmark` - Run benchmarks with presets
- `/test` - Run test suites
- `/clean` - Clean build artifacts
- `/profile` - Build with profiling
- `/compare` - Compare with Lucene

---

**Status**: ✅ Ready to use
**Version**: 1.0.0
**Date**: 2026-02-09

Try it now: `/build_lucene`
