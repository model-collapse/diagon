# Diagon Skills Overview

This document provides an overview of all available Claude Code skills for the Diagon project.

## Available Skills

### Build Skills

### Primary: `/build_diagon`
**File:** `build_diagon.json`

The main build skill for the Diagon search engine. Follows BUILD_SOP.md exactly.

**Quick usage:**
```
/build_diagon                   # Build core library
/build_diagon target=benchmarks # Build with benchmarks
/build_diagon target=all       # Build everything
```

**When to use:**
- Default choice for building Diagon
- Clear, project-aligned naming
- All standard build scenarios

### Alternative: `/build_lucene`
**File:** `build_lucene.json`

Identical to `/build_diagon` but emphasizes the Lucene-inspired architecture.

**Quick usage:**
```
/build_lucene                   # Same as /build_diagon
/build_lucene target=benchmarks # Same functionality
```

**When to use:**
- When emphasizing Lucene architecture aspects
- Personal preference for this naming
- Functionally identical to `/build_diagon`

## Skills Comparison

### Build Skills

| Feature | `/build_diagon` | `/build_lucene` |
|---------|----------------|-----------------|
| Build procedure | BUILD_SOP.md | BUILD_SOP.md |
| ICU verification | ✅ Yes | ✅ Yes |
| Targets | core/benchmarks/tests/all | core/benchmarks/tests/all |
| Clean builds | ✅ Default | ✅ Default |
| Parallel jobs | Configurable | Configurable |
| **Recommendation** | **Primary** | Alternative |

Both build skills are **functionally identical** - choose based on naming preference.

### Benchmark Skills

| Feature | `/benchmark_diagon` | `/benchmark_reuters_lucene` | `/benchmark_diagon_multiterm` | `/benchmark_lucene_multiterm` |
|---------|---------------------|------------------------------|-------------------------------|-------------------------------|
| Dataset | Reuters-21578 | Reuters-21578 | Reuters-21578 | Reuters-21578 |
| Focus | Pure Diagon | vs Lucene | Multi-term queries | Multi-term vs Lucene |
| Query types | All standard | All standard | AND, OR, mixed | AND, OR, mixed |
| Term counts | Standard | Standard | 2, 3, 5, 10 terms | 2, 3, 5, 10 terms |
| Comparison | Previous runs | Apache Lucene | Scalability | Speedup ratios |
| Regression detection | ✅ Yes | ❌ No | ⚠️ Performance only | ⚠️ vs target |
| Baseline tracking | ✅ Yes | ❌ No | ❌ No | ❌ No |
| WAND analysis | ✅ Yes | ✅ Yes | ✅ Deep dive | ✅ Advantage |
| Speedup target | N/A | 3-10x | N/A | 3-6x (configurable) |
| **Use for** | Daily tracking | Milestone validation | Query optimization | Competitive analysis |
| **Frequency** | Daily/per-commit | Weekly/per-milestone | After query changes | Before releases |

### Benchmark Skills

### Primary: `/benchmark_diagon`
**File:** `benchmark_diagon.json`

Runs pure Diagon performance benchmark on Reuters-21578 dataset, focusing on absolute performance and trend tracking.

**Quick usage:**
```
/benchmark_diagon                   # Pure Diagon performance
/benchmark_diagon benchmark=wand    # WAND optimization
/benchmark_diagon benchmark=both    # All benchmarks
```

**When to use:**
- Daily performance tracking
- Pre-commit validation
- Regression detection
- Performance optimization measurement
- Trend analysis over time

**Key features:**
- Pure Diagon metrics (not vs Lucene)
- Baseline comparison and trend tracking
- Regression detection
- Automatic baseline updates
- Same report template as Lucene comparison

### Alternative: `/benchmark_reuters_lucene`
**File:** `benchmark_reuters_lucene.json`

Runs the Reuters-21578 dataset benchmark for comparison with Apache Lucene.

**Quick usage:**
```
/benchmark_reuters_lucene                   # Standard benchmark
/benchmark_reuters_lucene benchmark=wand    # WAND optimization test
/benchmark_reuters_lucene benchmark=both    # All benchmarks
```

**When to use:**
- Performance testing and validation
- Comparing with Lucene baseline
- Verifying optimization improvements
- Generating benchmark reports
- CI/CD performance regression testing

**Key features:**
- Standard IR benchmark (Reuters-21578)
- 21,578 news articles from 1987
- Tests indexing and query performance
- Direct comparison with Lucene
- Automatic result saving

See `.claude/skills/BENCHMARK_REUTERS_GUIDE.md` for complete documentation.

### Multi-Term Specialization: `/benchmark_diagon_multiterm`
**File:** `benchmark_diagon_multiterm.json`

Deep-dive benchmark focusing exclusively on multi-term query performance.

**Quick usage:**
```
/benchmark_diagon_multiterm                      # All queries
/benchmark_diagon_multiterm query_type=or        # OR queries only (WAND test)
/benchmark_diagon_multiterm term_counts=large    # 6-10 term queries
/benchmark_diagon_multiterm iterations=1000      # High precision
```

**When to use:**
- Testing multi-term query optimizations
- Validating WAND implementation
- Analyzing Boolean operator efficiency
- Measuring query scalability

**Key features:**
- Focused on Boolean AND, OR, and mixed queries
- Tests 2, 3, 5, and 10 term combinations
- Scalability analysis (sub-linear target)
- WAND effectiveness measurement
- Frequency-based optimization validation
- Performance targets by term count

**Query types tested:**
- **AND queries**: Intersection operations, early termination
- **OR queries**: Union operations, WAND early termination critical
- **Mixed queries**: Nested Boolean combinations

**Performance targets:**
- 2-term AND: <2ms P99
- 5-term AND: <5ms P99
- 2-term OR: <3ms P99
- 5-term OR: <8ms P99
- 10-term OR: <15ms P99

### Multi-Term Comparison: `/benchmark_lucene_multiterm`
**File:** `benchmark_lucene_multiterm.json`

Compares Diagon vs Lucene specifically for multi-term queries.

**Quick usage:**
```
/benchmark_lucene_multiterm                         # Full comparison
/benchmark_lucene_multiterm query_type=or           # OR focus (max advantage)
/benchmark_lucene_multiterm target_speedup=5.0      # Higher target
/benchmark_lucene_multiterm term_counts=large       # Complex queries
```

**When to use:**
- Validating speedup targets for multi-term queries
- Demonstrating WAND performance advantage
- Competitive benchmarking
- External reporting and marketing
- Release validation

**Key features:**
- Direct Diagon vs Lucene comparison
- Speedup calculation per query type
- Target validation (default 3x minimum)
- WAND advantage quantification
- Speedup trend analysis with term count
- Competitive performance metrics

**Speedup targets:**
- AND queries: ≥3x faster than Lucene
- OR queries: ≥3.5-6x faster (WAND advantage)
- Mixed queries: ≥3-4x faster
- Scaling: Speedup increases with term count

**Why multi-term specialization?**
- **Most common**: Real queries often have 2-5 terms
- **Most complex**: Require intersection/union algorithms
- **Performance-critical**: Can be 10-100x slower than single-term
- **WAND showcase**: Early termination most valuable for OR
- **Optimization target**: Where most gains can be achieved

## Common Usage Patterns

### Development Workflow
```
# Initial build
/build_diagon

# After code changes
/build_diagon clean=false        # Quick rebuild

# Before benchmarking
/build_diagon target=benchmarks  # Full clean build
```

### Testing Workflow
```
# Build tests
/build_diagon target=tests

# Build everything
/build_diagon target=all
```

### Performance Tuning
```
# Fast parallel build
/build_diagon target=all jobs=16

# Core only for quick testing
/build_diagon target=core
```

### Benchmarking Workflow
```
# Build benchmarks
/build_diagon target=benchmarks

# Daily performance check (pure Diagon)
/benchmark_diagon

# Pre-commit validation
/benchmark_diagon

# Weekly Lucene comparison
/benchmark_reuters_lucene

# Full evaluation (both benchmarks)
/benchmark_diagon benchmark=both
/benchmark_reuters_lucene benchmark=both

# After query optimization work
/benchmark_diagon_multiterm                    # Test query improvements
/benchmark_lucene_multiterm                    # Validate speedup target

# Before release (comprehensive)
/benchmark_diagon benchmark=both               # Pure Diagon baseline
/benchmark_reuters_lucene benchmark=both       # vs Lucene general
/benchmark_diagon_multiterm                    # Multi-term deep dive
/benchmark_lucene_multiterm                    # Multi-term competitive
```

## Skill Arguments Reference

### target (string, default: "core")
- `core` - Build diagon_core library only (fastest, ~1 min)
- `benchmarks` - Build core + benchmark executables (~2-3 min)
- `tests` - Build core + test executables (~2-3 min)
- `all` - Build everything (~3-5 min)

### clean (boolean, default: true)
- `true` - Delete and recreate build directory (recommended)
- `false` - Incremental build (faster but can cause issues)

**Use `clean=true` when:**
- First build
- After CMakeLists.txt changes
- After pulling updates
- Build errors occur
- Before benchmarking

**Use `clean=false` when:**
- Quick code-only changes
- Iterating on single file
- Confident no CMake changes

### verify (boolean, default: true)
- `true` - Check ICU linking with `ldd` (recommended)
- `false` - Skip verification (faster but risky)

**Always use `verify=true` unless:**
- You've already verified in this session
- Building multiple times in a row
- Absolutely certain ICU is linked

### jobs (number, default: 8)
- Number of parallel make jobs
- Recommended: # of CPU cores or cores-1
- Common values: 4, 8, 16, 32

**Guidelines:**
- Development: 4-8 jobs (leaves CPU for other work)
- CI/CD: max cores (fastest build)
- Low memory: fewer jobs (prevents OOM)

## Error Messages

All skills provide helpful error messages:

### ICU Not Linked
```
❌ ERROR: ICU not linked!
See BUILD_SOP.md for troubleshooting.
```
→ Check BUILD_SOP.md "Error: undefined reference to icu_73"

### Build Failure
```
❌ Build failed
[compiler output]
```
→ Fix code errors, then rebuild with `clean=true`

### ZSTD Issues
```
ZSTD target not found
```
→ Should be auto-fixed in CMake, check dependencies

## Future Skills (Planned)

Possible additions:
- `/benchmark_custom` - Run custom benchmark configurations
- `/test` - Run test suites with filtering
- `/clean` - Clean build artifacts and caches
- `/verify` - Verify build environment
- `/profile` - Build with profiling enabled
- `/analyze` - Static analysis with clang-tidy
- `/compare_lucene` - Direct Lucene vs Diagon comparison
- `/benchmark_suite` - Run full benchmark suite

## Adding Custom Skills

To create your own skill:

1. Create `.claude/skills/your-skill.json`
2. Follow the format in existing skills
3. Add documentation here
4. Test thoroughly

## Best Practices

1. **Use skills over manual builds**: More reliable and consistent
2. **Default to clean builds**: Prevents 95% of build issues
3. **Verify ICU on first build**: Catches linking problems early
4. **Use appropriate targets**: Don't build tests if only benchmarking
5. **Leverage parallel builds**: Use more jobs on powerful machines

## Documentation Links

- **Skill usage**: This file
- **Build procedure**: `BUILD_SOP.md`
- **Quick start**: `QUICKSTART.md`
- **Project guide**: `CLAUDE.md`
- **Design docs**: `design/README.md`

---

**Last Updated:** 2026-02-09
**Skills Count:** 6
  - Build: `build_diagon`, `build_lucene`
  - Benchmark (general): `benchmark_diagon`, `benchmark_reuters_lucene`
  - Benchmark (multi-term): `benchmark_diagon_multiterm`, `benchmark_lucene_multiterm`
**Status:** ✅ Production ready
