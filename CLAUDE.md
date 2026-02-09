# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Tenets
- **Be Self-disciplined** Each efficiency observation should be based on a correctly built artifact, following the build SOP. There is no trade-off in experiment/test, there is no 'guessed/predicted' finding based on incorrectly built artifact/artifact with obvious bugs/approximate without testing. **Always use `/build_diagon target=benchmarks` before benchmarking.** All benchmarks should be conducted after a full clean build using the build skills.
- **Be Humble and Straight** The positioning of this product is already clearly defined and known by the user. It is forbbiden to 'bury' the lags/drawbacks/inalignment/concerns in a long passage of boast. In each report, just mention the real benchmark data, straight and objective comparison, insights for improvement. No others.
- **Be Honest** DO NOT emphasize the advantage signal based on prediction data. All the reported comparison should be annotated with "predicted" and "experimented". Don't try to disguise the unreliable data (even fake data) with confident narrative.
- **Be Rational** Each step you take (decide) to optmize / fix should be 100% rational based on the former obsevation, deep dive before every proposal. It is discouraged to enumerate massive clueless possibilties and let me choose. Experiment, verify and narrow down the root cause scope as much as possible.
- **Insist Highest Standard** The design target to succeed lucene and click-house from all the apects. There is no "Although we lag behind XXX, but we can save sth. by our design". There is NO EXCUSE falling behind them. Each time the benchmark show we are slower, you should be ashamed. Keep efficiency first in mind.

## MANDATORY: Skills-First Policy

**CRITICAL RULE**: This project has production-ready Claude Code skills that MUST be used instead of writing custom code.

### Build Operations - ALWAYS Use Skills

When building is needed:
- ✅ **REQUIRED**: Use `/build_diagon` skill
- ❌ **FORBIDDEN**: Manual cmake/make commands
- ❌ **FORBIDDEN**: Writing custom build scripts

**Example (correct)**:
```
/build_diagon target=benchmarks
```

**If spawning agent**: Agent MUST use the Skill tool with `/build_diagon`

### Benchmark Operations - ALWAYS Use Skills

When benchmarking is needed:
- ✅ **REQUIRED**: Use one of the 4 benchmark skills
- ❌ **FORBIDDEN**: Writing custom benchmark code (*.cpp, *.java)
- ❌ **FORBIDDEN**: Direct execution of benchmark binaries
- ❌ **FORBIDDEN**: Creating new benchmark implementations

**Available benchmark skills**:
1. `/benchmark_diagon` - Pure Diagon performance
2. `/benchmark_reuters_lucene` - vs Lucene comparison
3. `/benchmark_diagon_multiterm` - Multi-term query focus
4. `/benchmark_lucene_multiterm` - Multi-term competitive analysis

**Example (correct)**:
```
/benchmark_lucene_multiterm
```

**If spawning agent**: Agent MUST use the Skill tool:
```python
Skill(skill="benchmark_lucene_multiterm")
# NOT: Create new benchmark code
# NOT: Write custom C++ benchmark
```

### When Skills Don't Exist

**Only if no skill exists** for your specific need:
1. Ask user if a new skill should be created
2. If user approves, create the skill following `.claude/skills/` patterns
3. Do NOT write one-off custom code as workaround

### Enforcement

**For Claude (me)**:
- Before ANY build: Check if I used `/build_diagon` skill
- Before ANY benchmark: Check if I used appropriate benchmark skill
- If I find myself writing cmake/make/benchmark code: STOP and use skill instead

**For Agents**:
- When spawning general-purpose agent for benchmarking: Explicitly instruct to use Skill tool
- Verify agent used skills by checking for skill invocation in output
- If agent writes custom code: Recognize this as deviation from project policy

### Why This Rule Exists

**Consistency**: Skills ensure same procedure every time
**Quality**: Skills follow BUILD_SOP.md and report templates
**Efficiency**: Skills are tested and optimized
**Documentation**: Skills generate professional reports
**NO EXCEPTIONS**: This is a hard requirement, not a suggestion

## Repository Purpose

This is a **design and implementation workspace** for **DIAGON** (**D**iverse **I**ndex **A**rchitecture for **G**ranular **O**LAP **a**nd **N**atural language search), a C++ search engine library combining:
- **Apache Lucene**: Inverted index architecture for full-text search
- **ClickHouse**: Columnar storage with granule-based indexing for OLAP workloads

The goal is to create a production-grade hybrid search engine with both text search and analytical query capabilities.

DIAGON provides diverse indexing capabilities through specialized index architectures: structured analytics (Granular OLAP) for fast aggregations and unstructured exploration (Natural language search) for full-text queries.

**Legal Notice**: DIAGON is an independent open source project. It is not affiliated with, endorsed by, or connected to Warner Bros Entertainment Inc., J.K. Rowling, or the Harry Potter franchise.

## Project Structure

```
/home/ubuntu/opensearch_warmroom/lucene-pp/
├── design/                              # Detailed design specifications
│   ├── README.md                        # Design documentation index
│   ├── 00_ARCHITECTURE_OVERVIEW.md      # System architecture
│   ├── 01_INDEX_READER_WRITER.md        # IndexReader/Writer interfaces
│   ├── 02_CODEC_ARCHITECTURE.md         # Pluggable codec system
│   ├── 03_COLUMN_STORAGE.md             # IColumn, IDataType, ISerialization
│   └── [04-12 to be created]            # Additional modules
├── research/                            # Research documents
│   ├── clickhouse_granules_explained.md
│   └── storage_comparison_report.md
└── CLAUDE.md                            # This file
```

## Targets
- A state-of-the-art C++ search engine index library with highest efficiency, compatibility, reliability
- Fully optimized algorithm, RAM layout and code implementation
- **3-10x search speed compared with Lucene** (verified via `/benchmark_reuters_lucene`)
- **3.5-6x speedup for multi-term OR queries** (WAND advantage, verified via `/benchmark_lucene_multiterm`)
- 2x analytics processing speed compared with ClickHouse

### Performance Targets

**General Queries:**
- Single-term: <1ms P99 latency
- Boolean AND (2-term): <2ms P99 latency
- Boolean OR (2-term): <3ms P99 latency

**Multi-Term Queries:**
- 2-term AND: <2ms, OR: <3ms
- 5-term AND: <5ms, OR: <8ms
- 10-term AND: <10ms, OR: <15ms

**Indexing:**
- Throughput: ≥5,000 docs/sec (Reuters-21578)
- Index size: 250-700 bytes/doc

**Competitive Targets:**
- 3-10x faster than Lucene (general queries)
- 3.5-6x faster than Lucene (multi-term OR queries with WAND) 

## Build Standard Operating Procedure (SOP)

**CRITICAL**: Always use the build skills to ensure consistent, error-free builds.

**Full Documentation**: See `BUILD_SOP.md` and `.claude/skills/README.md` for complete details.

### Build Methods

**Primary Method: Use Build Skills (Required)**
```
/build_diagon                          # Build core library only
/build_diagon target=benchmarks        # Build core + benchmarks (for benchmarking)
/build_diagon target=tests             # Build core + tests
/build_diagon target=all               # Build everything
/build_diagon target=benchmarks jobs=16  # Use 16 parallel jobs

# Alternative name (identical functionality):
/build_lucene                          # Same as /build_diagon
```

**Why use skills:**
- ✅ Follows BUILD_SOP.md exactly
- ✅ Automatic ICU linking verification
- ✅ Handles clean builds correctly
- ✅ Prevents common build errors
- ✅ Consistent across all environments

**Alternative: Helper Script** (if skills unavailable)
```bash
./scripts/build_lucene.sh              # Build core library
./scripts/build_lucene.sh benchmarks   # Build core + benchmarks
./scripts/build_lucene.sh all true 16  # Build all, clean, 16 jobs
```

**Manual Build** (only for troubleshooting)
```bash
# 1. ALWAYS start clean
cd /home/ubuntu/diagon
rm -rf build && mkdir build && cd build

# 2. Configure (Release mode WITHOUT LTO)
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS="-O3 -march=native" \
      -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
      -DDIAGON_BUILD_BENCHMARKS=ON \
      -DDIAGON_BUILD_TESTS=ON \
      ..

# 3. Build core library first
make diagon_core -j8

# 4. Verify ICU is linked (CRITICAL CHECK)
ldd src/core/libdiagon_core.so | grep icu
# Must show: libicuuc.so and libicui18n.so

```

See `.claude/skills/README.md` for complete skill documentation.

### Key Rules

1. **ALWAYS disable LTO**: Use `-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF`
   - LTO causes `undefined reference to icu_73::...` errors
   - Performance difference negligible (~2-5%)

2. **ALWAYS start with clean build directory**: `rm -rf build`
   - Stale CMake cache causes random failures
   - Don't trust pre-compiled binaries

3. **ALWAYS verify ICU linking**: `ldd libdiagon_core.so | grep icu`
   - If ICU not shown, benchmarks will fail to link

4. **NEVER use Debug mode for benchmarks**: 10-100x slower
   - Use Release mode without LTO

5. **Build diagon_core first**: Catches compilation errors early

### Common Errors and Fixes

| Error | Cause | Fix |
|-------|-------|-----|
| `undefined reference to icu_73::...` | Conda ICU 73 vs System ICU 74 mismatch | Use pre-compiled binaries OR rebuild with system-only PATH |
| `use of deleted function` | C++ code error | Fix code (don't copy unique_ptr, implement virtuals) |
| `multiple definition of ...` | Duplicate symbols | Make one static or remove duplicate |
| Pre-compiled binary works but build fails | Stale binaries | `rm -rf build` and rebuild |
| `ZSTD target not found` | System libs missing | Install: `sudo apt install libzstd-dev` |

See `BUILD_SOP.md` for troubleshooting guide and detailed explanations.

## Benchmarking

**CRITICAL**: Always use benchmark skills for consistent, reproducible results.

**Full Documentation**: See `.claude/skills/BENCHMARK_DIAGON_GUIDE.md`, `.claude/skills/BENCHMARK_REUTERS_GUIDE.md`, and `.claude/skills/BENCHMARK_MULTITERM_GUIDE.md`.

### Available Benchmark Skills

#### General Performance Benchmarks

**1. Pure Diagon Performance** (daily tracking, regression detection)
```
/benchmark_diagon                      # Standard benchmark
/benchmark_diagon benchmark=wand       # WAND optimization test
/benchmark_diagon benchmark=both       # All benchmarks
/benchmark_diagon compare_baseline=false  # No baseline comparison
```

**2. Diagon vs Lucene Comparison** (milestone validation)
```
/benchmark_reuters_lucene              # Standard comparison
/benchmark_reuters_lucene benchmark=wand  # WAND comparison
/benchmark_reuters_lucene benchmark=both  # Full comparison
```

#### Multi-Term Query Benchmarks

**3. Multi-Term Query Performance** (query optimization)
```
/benchmark_diagon_multiterm            # Test all multi-term queries
/benchmark_diagon_multiterm query_type=or  # OR queries (WAND focus)
/benchmark_diagon_multiterm term_counts=large  # 6-10 term queries
```

**4. Multi-Term Competitive Analysis** (competitive validation)
```
/benchmark_lucene_multiterm            # Compare with Lucene
/benchmark_lucene_multiterm query_type=or  # OR advantage (3.5-6x target)
/benchmark_lucene_multiterm target_speedup=5.0  # Raise target
```

### Benchmark Workflow

**Daily Development:**
```bash
# 1. Build with benchmarks
/build_diagon target=benchmarks

# 2. Run pure Diagon benchmark (trend tracking)
/benchmark_diagon

# 3. Check for regressions
# Review report in benchmark_results/
```

**After Query Optimization:**
```bash
# 1. Build
/build_diagon target=benchmarks

# 2. Test multi-term queries
/benchmark_diagon_multiterm

# 3. Validate improvement
# Compare with baseline in report
```

**Pre-Release Validation:**
```bash
# 1. Build
/build_diagon target=benchmarks

# 2. Run comprehensive benchmarks
/benchmark_diagon benchmark=both
/benchmark_reuters_lucene benchmark=both
/benchmark_diagon_multiterm
/benchmark_lucene_multiterm

# 3. Verify all targets met
# Review all reports in benchmark_results/
```

### Benchmark Reports

All benchmarks generate comprehensive reports following `.claude/BENCHMARK_REPORT_TEMPLATE_V2.md`:

**Report Location:**
```
benchmark_results/diagon_[YYYYMMDD_HHMMSS].md          # Pure Diagon
benchmark_results/reuters_lucene_[YYYYMMDD_HHMMSS].md  # vs Lucene
benchmark_results/multiterm_[YYYYMMDD_HHMMSS].md       # Multi-term Diagon
benchmark_results/lucene_multiterm_[YYYYMMDD_HHMMSS].md # Multi-term comparison
```

**Report Sections:**
1. Executive Summary
2. Test Environment
3. Indexing Performance
4. Query Performance (with multi-term analysis for specialized benchmarks)
5. Performance Analysis
6. Detailed Comparison
7. Issues and Concerns
8. Recommendations
9. Raw Data
10. Reproducibility

**Why use benchmark skills:**
- ✅ Automatic build verification
- ✅ Consistent test procedure
- ✅ Baseline tracking and regression detection
- ✅ Comprehensive report generation
- ✅ Reproducible results
- ✅ Professional formatting

### Manual Benchmarking (Discouraged)

Only use manual commands for debugging specific issues:
```bash
cd /home/ubuntu/diagon/build/benchmarks
./ReutersBenchmark
./ReutersWANDBenchmark
```

## Design Methodology

**Critical**: This project follows **production codebase study**, not theoretical design.

### Reference Codebases

1. **Apache Lucene** (Java): `/home/ubuntu/opensearch_warmroom/lucene/`
   - Study: `lucene/core/src/java/org/apache/lucene/`
   - Key modules: index, search, codecs, store, util

2. **ClickHouse** (C++): `/home/ubuntu/opensearch_warmroom/ClickHouse/`
   - Study: `src/Storages/MergeTree/`, `src/Columns/`, `src/Compression/`
   - Key concepts: MergeTree engine, granules, marks, type system

### Design Process

1. **Read actual source code**: Don't guess interfaces, study the real implementations
2. **Copy successful patterns**: Sealed hierarchies, Producer/Consumer, COW semantics
3. **Document trade-offs**: Explain why Lucene/ClickHouse made specific choices
4. **Align APIs**: Keep Lucene-compatible interfaces where possible
5. **Hybrid design**: Combine best of both systems

## Key Design Principles

### From Lucene

- **Sealed reader hierarchy**: IndexReader → LeafReader/CompositeReader → DirectoryReader
- **Producer/Consumer codecs**: FieldsProducer/Consumer for read/write separation
- **Immutable segments**: Never modify after flush, background merge for compaction
- **Iterator-based access**: TermsEnum, PostingsEnum for memory efficiency
- **Three-level queries**: Query → Weight → Scorer for reusability

### From ClickHouse

- **COW columns**: IColumn with copy-on-write semantics for efficient sharing
- **Granule-based I/O**: 8192-row chunks with marks for random access
- **Type-specific codecs**: IDataType + ISerialization + ICompressionCodec per type
- **Wide vs Compact**: Format selection based on size thresholds
- **Sparse primary index**: Index only granule boundaries (1/8192 rows)

## Working with Designs

### Reading Designs

Start with `design/README.md` for the complete index.

**Recommended order**:
1. `00_ARCHITECTURE_OVERVIEW.md` - Understand overall system
2. `01_INDEX_READER_WRITER.md` - Core indexing interfaces
3. `02_CODEC_ARCHITECTURE.md` - Pluggable format system
4. `03_COLUMN_STORAGE.md` - Column-oriented storage

### Creating New Designs

When adding new design documents:

1. **Study codebase first**:
   ```bash
   # Explore Lucene
   cd /home/ubuntu/opensearch_warmroom/lucene
   # Read relevant Java files

   # Explore ClickHouse
   cd /home/ubuntu/opensearch_warmroom/ClickHouse
   # Read relevant C++ files
   ```

2. **Reference source files**: Include paths like:
   - `org.apache.lucene.index.IndexReader`
   - `ClickHouse/src/Columns/IColumn.h`

3. **Use actual interfaces**: Copy signatures and adapt to C++

4. **Document design decisions**: Explain trade-offs and alternatives

5. **Update design/README.md**: Add to index with status

### Design Document Template

```markdown
# Module Name Design
## Based on [Lucene/ClickHouse] [Component]

Source references:
- [Path to actual source file]
- [Related files]

## Overview
[Brief description]

## Interface Design
[Actual C++ interfaces based on source]

## Key Design Decisions
[Trade-offs, alternatives, rationale]

## Usage Examples
[Code examples]
```

## Implementation Guidelines (Future)

When implementation begins:

### Build System
- Use CMake (3.20+)
- C++20 standard
- Support GCC 11+, Clang 14+

### Dependencies
- Compression: LZ4, ZSTD
- Hashing: CityHash (for ClickHouse compatibility)
- Testing: Google Test
- Benchmarking: Google Benchmark

### Code Style
- Follow Lucene naming conventions for inverted index code
- Follow ClickHouse style for column storage code
- Use clang-format for consistency

### Code Quality Standards
- **Zero-warning policy**: Build uses `-Werror` to treat all warnings as errors
- All code must compile cleanly without warnings
- External dependencies are marked as SYSTEM headers to suppress their warnings
- Member initialization order must match declaration order
- Use explicit casts for format strings (`%p` requires `void*`)

### Testing Strategy
- Unit tests per module
- Integration tests for end-to-end scenarios
- Performance benchmarks vs Lucene and ClickHouse
- Correctness tests (compare results with Lucene)

## Comparison with Related Systems

| Feature | DIAGON | Apache Lucene | ClickHouse |
|---------|----------|---------------|------------|
| Language | C++ | Java | C++ |
| Inverted Index | ✓ | ✓ | ✗ |
| Column Storage | ✓ | ✗ | ✓ |
| Hybrid Queries | ✓ | Limited | Limited |
| Granule-Based I/O | ✓ | ✗ | ✓ |
| Storage Tiers | ✓ | ✗ (via ILM) | Limited |
| Memory Mode | ✓ (mmap) | ✓ | ✗ |
| Type Partitioning | ✓ | ✗ | ✓ |
| FST Term Dict | ✓ | ✓ | ✗ |

## Claude Code Skills

This project provides production-ready Claude Code skills for build and benchmark operations.

### Available Skills (6 Total)

**Build Skills:**
- `/build_diagon` - Primary build skill (recommended)
- `/build_lucene` - Alternative name (identical functionality)

**General Benchmark Skills:**
- `/benchmark_diagon` - Pure Diagon performance (trend tracking, regression detection)
- `/benchmark_reuters_lucene` - Diagon vs Lucene comparison (milestone validation)

**Multi-Term Benchmark Skills:**
- `/benchmark_diagon_multiterm` - Multi-term query performance (query optimization)
- `/benchmark_lucene_multiterm` - Multi-term competitive analysis (3-6x speedup target)

### Skill Documentation

**Quick Reference:**
- `.claude/skills/README.md` - All skills overview
- `.claude/skills/SKILLS_OVERVIEW.md` - Comprehensive guide with workflows

**Build Documentation:**
- `.claude/skills/BUILD_SKILL_SETUP.md` - Build skills setup
- `BUILD_SOP.md` - Build standard operating procedure

**Benchmark Documentation:**
- `.claude/skills/BENCHMARK_DIAGON_GUIDE.md` - Pure Diagon benchmarks
- `.claude/skills/BENCHMARK_REUTERS_GUIDE.md` - Lucene comparison benchmarks
- `.claude/skills/BENCHMARK_MULTITERM_GUIDE.md` - Multi-term query benchmarks
- `.claude/BENCHMARK_REPORT_TEMPLATE_V2.md` - Comprehensive report template

### When to Use Each Skill

| Skill | When to Use | Frequency |
|-------|-------------|-----------|
| `/build_diagon` | Before any benchmarking or testing | Always |
| `/benchmark_diagon` | Daily performance tracking | Daily/per-commit |
| `/benchmark_reuters_lucene` | Validate against Lucene | Weekly/per-milestone |
| `/benchmark_diagon_multiterm` | After query optimization | After query changes |
| `/benchmark_lucene_multiterm` | Competitive validation | Before releases |

## Common Tasks

### Building

```bash
# Standard build (always use this)
/build_diagon target=benchmarks

# Quick core-only build
/build_diagon

# Full build with tests
/build_diagon target=all
```

### Benchmarking

```bash
# Daily check
/benchmark_diagon

# Weekly Lucene comparison
/benchmark_reuters_lucene

# After query optimization
/benchmark_diagon_multiterm

# Pre-release validation
/benchmark_diagon benchmark=both
/benchmark_reuters_lucene benchmark=both
/benchmark_diagon_multiterm
/benchmark_lucene_multiterm
```

### Studying Reference Code

```bash
# Study Lucene IndexReader
cat /home/ubuntu/opensearch_warmroom/lucene/lucene/core/src/java/org/apache/lucene/index/IndexReader.java

# Study ClickHouse IColumn
cat /home/ubuntu/opensearch_warmroom/ClickHouse/src/Columns/IColumn.h

# Find all codec implementations in Lucene
find /home/ubuntu/opensearch_warmroom/lucene -name "*Codec.java" -type f

# Find ClickHouse compression codecs
find /home/ubuntu/opensearch_warmroom/ClickHouse/src/Compression -name "*.cpp" -type f
```

### Design Exploration

Use the Task tool to explore codebases:
```
Ask Claude to: "Explore the Lucene IndexWriter implementation at
/home/ubuntu/opensearch_warmroom/lucene/lucene/core/src/java/org/apache/lucene/index/IndexWriter.java
and explain the flush mechanism"
```

### Creating Designs

1. Identify missing module from `design/README.md`
2. Study relevant Lucene/ClickHouse code
3. Draft interface in C++ based on source
4. Document design decisions
5. Update `design/README.md`

## References

### Lucene Documentation
- API Docs: https://lucene.apache.org/core/9_11_0/
- Index Format: https://lucene.apache.org/core/9_11_0/core/org/apache/lucene/codecs/lucene90/package-summary.html

### ClickHouse Documentation
- Architecture: https://clickhouse.com/docs/en/development/architecture/
- MergeTree: https://clickhouse.com/docs/en/engines/table-engines/mergetree-family/mergetree

### Papers
- FST: "Direct Construction of Minimal Acyclic Subsequential Transducers"
- WAND: "Using Block-Max Indexes for Score-At-A-Time WAND Processing"
- Gorilla: "Gorilla: A Fast, Scalable, In-Memory Time Series Database"

## Notes for Future Work

When extending the design:

1. **Maintain production alignment**: Always reference actual Lucene/ClickHouse code
2. **Don't invent**: Copy proven patterns from production systems
3. **Hybrid carefully**: When combining Lucene + ClickHouse concepts, document why
4. **Performance first**: Both source systems are highly optimized, learn from them
5. **Test against source**: DIAGON should match or exceed source system performance

## Skills-Based Development Workflow

This project uses Claude Code skills for all build and benchmark operations. **Always use skills instead of manual commands.**

### Standard Development Cycle

1. **Make code changes**
   ```bash
   vim src/core/search/BooleanQuery.cpp
   ```

2. **Build using skill**
   ```
   /build_diagon target=benchmarks
   ```

3. **Benchmark using skill**
   ```
   /benchmark_diagon                    # Daily tracking
   # OR
   /benchmark_diagon_multiterm          # After query changes
   ```

4. **Review report**
   ```bash
   cat benchmark_results/diagon_*.md | grep "Overall Result"
   ```

5. **Commit if no regressions**
   ```bash
   git add .
   git commit -m "Optimize BooleanQuery"
   ```

### Pre-Release Checklist

- [ ] `/build_diagon target=all` - Full clean build
- [ ] `/benchmark_diagon benchmark=both` - Pure Diagon performance
- [ ] `/benchmark_reuters_lucene benchmark=both` - vs Lucene general
- [ ] `/benchmark_diagon_multiterm` - Multi-term performance
- [ ] `/benchmark_lucene_multiterm` - Multi-term competitive
- [ ] Review all reports for regressions
- [ ] Verify all performance targets met
- [ ] Document performance improvements in release notes

### Why Skills?

**Consistency:**
- Same procedure every time
- No forgotten steps
- No manual errors

**Quality:**
- Automatic ICU verification
- Clean builds by default
- Comprehensive reporting

**Efficiency:**
- One command instead of many
- Baseline tracking automatic
- Regression detection built-in

**Documentation:**
- Professional reports
- Reproducible procedures
- Historical tracking

---

**Last Updated:** 2026-02-09
**Skills:** 6 production-ready skills available
**Status:** ✅ All workflows use skills for build and benchmark operations
