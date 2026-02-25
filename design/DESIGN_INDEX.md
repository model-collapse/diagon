# DIAGON Design Documentation Index

This directory contains comprehensive design documentation for the DIAGON search engine library.

## Design Documents

### Numbered Design Modules (00-15)

All design modules are complete and production-ready.

| # | File | Description |
|---|------|-------------|
| 00 | [00_ARCHITECTURE_OVERVIEW.md](00_ARCHITECTURE_OVERVIEW.md) | System architecture, module organization, hybrid design |
| 01 | [01_INDEX_READER_WRITER.md](01_INDEX_READER_WRITER.md) | IndexReader/Writer interfaces, concurrency, crash recovery |
| 02 | [02_CODEC_ARCHITECTURE.md](02_CODEC_ARCHITECTURE.md) | Pluggable codec system, Producer/Consumer pattern |
| 03 | [03_COLUMN_STORAGE.md](03_COLUMN_STORAGE.md) | IColumn with COW semantics, type system |
| 04 | [04_COMPRESSION_CODECS.md](04_COMPRESSION_CODECS.md) | LZ4, ZSTD, Delta, Gorilla compression codecs |
| 05 | [05_MERGETREE_DATA_PARTS.md](05_MERGETREE_DATA_PARTS.md) | MergeTree Wide/Compact data parts |
| 06 | [06_GRANULARITY_AND_MARKS.md](06_GRANULARITY_AND_MARKS.md) | Granule-based indexing, mark files |
| 07 | [07_QUERY_EXECUTION.md](07_QUERY_EXECUTION.md) | Query/Weight/Scorer framework, timeout, phrase queries |
| 07a | [07a_FILTERS.md](07a_FILTERS.md) | Non-scoring filter system with skip index integration |
| 08 | [08_MERGE_SYSTEM.md](08_MERGE_SYSTEM.md) | Merge policies, scheduler, write amplification |
| 09 | [09_DIRECTORY_ABSTRACTION.md](09_DIRECTORY_ABSTRACTION.md) | FSDirectory, MMapDirectory, IndexInput/Output |
| 10 | [10_FIELD_INFO.md](10_FIELD_INFO.md) | Field metadata, IndexOptions, DocValuesType |
| 11 | [11_SKIP_INDEXES.md](11_SKIP_INDEXES.md) | MinMax, Set, BloomFilter skip indexes |
| 12 | [12_STORAGE_TIERS.md](12_STORAGE_TIERS.md) | Hot/Warm/Cold/Frozen tier management |
| 13 | [13_SIMD_POSTINGS_FORMAT.md](13_SIMD_POSTINGS_FORMAT.md) | SIMD-optimized format (superseded by 14) |
| 14 | [14_UNIFIED_SIMD_STORAGE.md](14_UNIFIED_SIMD_STORAGE.md) | Unified SIMD storage layer |
| 15 | [15_MULTI_VALUED_FIELDS.md](15_MULTI_VALUED_FIELDS.md) | Multi-valued array field support |

### Supporting Documents

| File | Description |
|------|-------------|
| [README.md](README.md) | Design documentation overview and reading guide |
| [DESIGN_SUMMARY.md](DESIGN_SUMMARY.md) | Complete design summary with all 15 modules |
| [DESIGN_REVIEW.md](DESIGN_REVIEW.md) | Principal SDE design review |
| [DESIGN_REFINEMENT_STATUS.md](DESIGN_REFINEMENT_STATUS.md) | Refinement tracking (13/13 complete) |
| [REFINEMENT_SUMMARY.md](REFINEMENT_SUMMARY.md) | Refinement summary |
| [ARCHITECTURE_CLARIFICATION_INDEXES.md](ARCHITECTURE_CLARIFICATION_INDEXES.md) | Inverted vs forward index clarification |
| [RESEARCH_SIMD_FILTER_STRATEGIES.md](RESEARCH_SIMD_FILTER_STRATEGIES.md) | SIMD filter strategy cost model |

### Infrastructure Documents

| File | Description |
|------|-------------|
| [BUILD_SYSTEM.md](BUILD_SYSTEM.md) | CMake build system design |
| [TESTING_STRATEGY.md](TESTING_STRATEGY.md) | Testing approach (unit, integration, stress) |
| [OBSERVABILITY.md](OBSERVABILITY.md) | Metrics, logging, tracing design |

## Reading Guide

### For Implementation

1. **Start with**: [00_ARCHITECTURE_OVERVIEW.md](00_ARCHITECTURE_OVERVIEW.md) — system architecture and module dependencies
2. **Core interfaces**: [01_INDEX_READER_WRITER.md](01_INDEX_READER_WRITER.md) — IndexReader/Writer
3. **Codec system**: [02_CODEC_ARCHITECTURE.md](02_CODEC_ARCHITECTURE.md) — pluggable formats
4. **Column storage**: [03_COLUMN_STORAGE.md](03_COLUMN_STORAGE.md) — IColumn COW semantics
5. **Next priorities**: 04-07 (compression, data parts, granularity, queries)

### For Specific Use Cases

- **Inverted index features**: 02, 07, 13/14
- **Column storage features**: 03, 05, 06
- **Query processing**: 07, 07a, 14
- **Performance optimization**: 04, 13, 14
- **Operations**: 08, 09, 12

## Key Design Principles

1. **Immutable Segments**: Never modify after creation; background merge for compaction
2. **Hybrid Storage**: Inverted index for text, column storage for analytics, configurable per field
3. **Type-Aware Architecture**: Type-specific compression, partitioning, and codecs
4. **Pluggable Components**: Codecs, merge policies, compression, scoring algorithms
5. **Performance First**: Zero-copy reads, memory-mapped files, SIMD optimizations

## References

- **Apache Lucene**: https://lucene.apache.org/
- **ClickHouse Architecture**: https://clickhouse.com/docs/en/development/architecture/

---

**Last Updated**: 2026-02-25
**Status**: All 15 design modules complete (100%)
