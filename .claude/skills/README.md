# Diagon Claude Code Skills

## ⚠️ Important: Actual Skills Location

**Skills are located in the GLOBAL directory**: `~/.claude/skills/`

**NOT in this project-local directory**: `/home/ubuntu/diagon/.claude/skills/`

This directory contains **documentation only**. The actual skills are registered globally.

## Skill Format (IMPORTANT)

Skills use **SKILL.md format** (markdown with YAML frontmatter):

```
~/.claude/skills/skill_name/
└── SKILL.md  ← YAML frontmatter + markdown instructions
```

Example SKILL.md:
```markdown
---
name: skill_name
description: Brief description
allowed-tools: Bash, Read, Write
user-invocable: true
argument-hint: [arg1] [arg2=default]
---

# Skill instructions...
```

This directory contains reusable skill documentation for working with the Diagon codebase.

## Available Skills

### `/build_diagon` - Build Diagon search engine (Recommended)

Builds the Diagon search engine following the standard operating procedure defined in BUILD_SOP.md. This is the primary build skill.

**Usage:**
```
/build_diagon
/build_diagon target=benchmarks
/build_diagon target=all clean=true
/build_diagon target=core verify=true jobs=16
```

**Arguments:**
- `target` (string, default: "core")
  - `core` - Build only the core library
  - `benchmarks` - Build core + benchmarks
  - `tests` - Build core + tests
  - `all` - Build everything

- `clean` (boolean, default: true)
  - `true` - Clean build directory before building (recommended)
  - `false` - Incremental build (use with caution)

- `verify` (boolean, default: true)
  - `true` - Verify ICU linking after build
  - `false` - Skip verification

- `jobs` (number, default: 8)
  - Number of parallel build jobs (e.g., 4, 8, 16)

**Examples:**

Build core library only (fastest):
```
/build_diagon
```

Build everything with clean build:
```
/build_diagon target=all
```

Quick incremental rebuild (use cautiously):
```
/build_diagon clean=false
```

Build benchmarks with 16 parallel jobs:
```
/build_diagon target=benchmarks jobs=16
```

**What it does:**
1. Cleans build directory (if clean=true)
2. Configures CMake with Release mode (no LTO)
3. Builds diagon_core library
4. Verifies ICU linking (if verify=true)
5. Builds specified target
6. Reports results and next steps

**Important Notes:**
- Always uses Release mode without LTO (safe, fast)
- Verifies ICU linking to catch common errors
- Follows BUILD_SOP.md procedure exactly
- Reports any errors with troubleshooting hints

### `/build_lucene` - Alternative build command

Alias for `/build_diagon` that emphasizes the Lucene-inspired architecture. Functionally identical.

Usage: Same as `/build_diagon` above.

Both skills use identical build procedures - choose whichever name you prefer.

### `/benchmark_diagon` - Run pure Diagon performance benchmark

Runs Diagon performance benchmark on Reuters-21578 dataset, focusing on absolute performance and trend tracking.

**Quick usage:**
```
/benchmark_diagon                    # Run standard benchmark
/benchmark_diagon benchmark=wand     # Run WAND optimization benchmark
/benchmark_diagon benchmark=both     # Run all benchmarks
/benchmark_diagon compare_baseline=false  # Don't compare with baseline
```

**Arguments:**
- `benchmark` (string, default: "reuters")
  - `reuters` - Standard Reuters benchmark
  - `wand` - WAND optimization benchmark
  - `both` - Run all benchmarks

- `build` (boolean, default: true)
  - `true` - Build benchmark before running
  - `false` - Use existing built benchmark

- `clean_index` (boolean, default: true)
  - `true` - Remove old index before benchmarking
  - `false` - Reuse existing index (not recommended)

- `save_results` (boolean, default: true)
  - `true` - Save results to timestamped file
  - `false` - Display only (no file saved)

- `compare_baseline` (boolean, default: true)
  - `true` - Compare with previous baseline
  - `false` - Pure performance measurement

**What it does:**
1. Builds benchmark executables (if build=true)
2. Loads previous baseline (if compare_baseline=true)
3. Cleans old index (if clean_index=true)
4. Runs selected benchmark(s)
5. Compares with baseline and detects regressions
6. Reports absolute performance metrics
7. Saves results and updates baseline

**Focus:**
- Pure Diagon performance (not vs Lucene comparison)
- Trend tracking over time
- Regression detection
- Absolute performance targets

**Use Cases:**
- Daily performance tracking
- Pre-commit validation
- Release validation
- Performance optimization measurement

### `/benchmark_reuters_lucene` - Run Reuters-21578 benchmark vs Lucene

Runs the standard Reuters-21578 dataset benchmark to compare Diagon performance with Apache Lucene.

**Quick usage:**
```
/benchmark_reuters_lucene                    # Run standard benchmark
/benchmark_reuters_lucene benchmark=wand     # Run WAND optimization benchmark
/benchmark_reuters_lucene benchmark=both     # Run all benchmarks
/benchmark_reuters_lucene build=false        # Skip build, run existing
```

**Arguments:**
- `benchmark` (string, default: "reuters")
  - `reuters` - Standard Reuters benchmark (indexing + queries)
  - `wand` - WAND (Weak AND) optimization benchmark
  - `both` - Run all benchmarks sequentially

- `build` (boolean, default: true)
  - `true` - Build benchmark before running
  - `false` - Use existing built benchmark

- `clean_index` (boolean, default: true)
  - `true` - Remove old index before benchmarking
  - `false` - Reuse existing index (not recommended)

- `save_results` (boolean, default: true)
  - `true` - Save results to timestamped file
  - `false` - Display only (no file saved)

**What it does:**
1. Builds benchmark executables (if build=true)
2. Verifies Reuters dataset exists (21,578 documents)
3. Cleans old index (if clean_index=true)
4. Runs selected benchmark(s)
5. Reports performance metrics
6. Saves results to file (if save_results=true)

**Dataset:**
- Location: `/home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark/work/reuters-out/`
- Documents: 21,578 news articles from 1987
- Standard benchmark used by Apache Lucene

**Performance Context:**
- Target: 3-10x faster than Lucene
- Reports: indexing throughput, query latency, index size
- Comparison: Direct comparison with Lucene's published results

### `/benchmark_diagon_multiterm` - Deep dive into multi-term query performance

Benchmarks Diagon's multi-term query performance (Boolean AND, OR, mixed) with various term counts.

**Quick usage:**
```
/benchmark_diagon_multiterm                           # Test all query types and term counts
/benchmark_diagon_multiterm query_type=and            # Test only AND queries
/benchmark_diagon_multiterm query_type=or             # Test only OR queries
/benchmark_diagon_multiterm term_counts=small         # Test only 2-3 term queries
/benchmark_diagon_multiterm iterations=1000           # More iterations for stability
```

**Arguments:**
- `query_type` (string, default: "all")
  - `and` - Boolean AND queries only
  - `or` - Boolean OR queries only
  - `mixed` - Mixed AND+OR queries only
  - `all` - Test all query types

- `term_counts` (string, default: "all")
  - `small` - 2-3 terms
  - `medium` - 4-5 terms
  - `large` - 6-10 terms
  - `all` - Test all term counts

- `build` (boolean, default: true)
- `iterations` (number, default: 100)
- `save_results` (boolean, default: true)

**What it does:**
1. Tests multi-term queries with 2, 3, 5, and 10 terms
2. Measures P50/P95/P99 latencies for each query
3. Analyzes scalability (sub-linear scaling target)
4. Validates WAND effectiveness for OR queries
5. Reports frequency-based optimization impact
6. Generates detailed multi-term analysis report

**Focus:**
- Multi-term query performance specialization
- Boolean operator efficiency (AND, OR, mixed)
- Scalability with term count
- WAND early termination validation

**Use Cases:**
- Validate multi-term query optimizations
- Test WAND implementation effectiveness
- Analyze query complexity impact
- Benchmark Boolean operator performance

### `/benchmark_lucene_multiterm` - Compare Diagon vs Lucene for multi-term queries

Compares Diagon and Lucene specifically for multi-term query performance.

**Quick usage:**
```
/benchmark_lucene_multiterm                           # Compare all query types
/benchmark_lucene_multiterm query_type=or             # Focus on OR queries (WAND advantage)
/benchmark_lucene_multiterm target_speedup=5.0        # Raise speedup target to 5x
/benchmark_lucene_multiterm term_counts=large         # Test only 6-10 term queries
```

**Arguments:**
- `query_type` (string, default: "all")
- `term_counts` (string, default: "all")
- `build` (boolean, default: true)
- `iterations` (number, default: 100)
- `save_results` (boolean, default: true)
- `target_speedup` (number, default: 3.0)
  - Minimum speedup target vs Lucene

**What it does:**
1. Runs same multi-term queries on both Diagon and Lucene
2. Calculates speedup ratio for each query
3. Validates against target speedup (default 3x)
4. Analyzes WAND advantage for OR queries
5. Reports speedup trends with term count
6. Generates comparative analysis report

**Focus:**
- Direct Diagon vs Lucene comparison
- Speedup validation vs target
- WAND advantage quantification
- Competitive performance analysis

**Performance Targets:**
- AND queries: ≥3x speedup
- OR queries: ≥3.5-6x speedup (WAND advantage)
- Mixed queries: ≥3-4x speedup
- Scalability: Speedup should increase with term count

**Use Cases:**
- Validate 3-10x faster target for multi-term queries
- Demonstrate WAND performance advantage
- Competitive benchmarking
- Marketing/external reporting

## Creating New Skills

To create a new skill:

1. Create a JSON file: `.claude/skills/your-skill.json`
2. Define the skill metadata:
   ```json
   {
     "name": "your-skill",
     "description": "What your skill does",
     "version": "1.0.0",
     "arguments": [...],
     "prompt": "Instructions for Claude..."
   }
   ```
3. Document it in this README

## Skill Design Guidelines

Good skills should:
- Follow existing project SOPs and conventions
- Have clear, focused purposes
- Include error handling and verification
- Provide helpful output and next steps
- Be composable with other skills

See `build_diagon.json` or `build_lucene.json` for complete examples.
