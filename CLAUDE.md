# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

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

## Build Standard Operating Procedure (SOP)

**CRITICAL**: Always follow this procedure to avoid compilation/linking errors.

**Full Documentation**: See `BUILD_SOP.md` for complete details.

### Quick Build Procedure

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

# 5. Build benchmarks
make SearchBenchmark -j8  # or make benchmarks -j8

# 6. Run benchmark
./benchmarks/SearchBenchmark
```

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

## Common Tasks

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
