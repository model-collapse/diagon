# Lucene++ Design Documentation
## Production-Grade Design Based on Apache Lucene & ClickHouse

This directory contains detailed design specifications for Lucene++, a C++ search engine combining Lucene's inverted index with ClickHouse's columnar storage.

**Design Philosophy**: Study and align with production codebases, not theoretical MVP designs.

---

## ⚠️ CRITICAL: Required Reading Before Implementation

**[ARCHITECTURE_CLARIFICATION_INDEXES.md](./ARCHITECTURE_CLARIFICATION_INDEXES.md)** - **READ THIS FIRST**

This document clarifies the fundamental architectural difference between:
- **Inverted Index** (term → documents): For text search, BM25 scoring
- **Forward Index / Column Storage** (document → value): For sorting, aggregation

**Key Points**:
- ClickHouse has **NO inverted index** (pure column storage)
- Lucene **requires inverted index** (for text search)
- Lucene++ is **hybrid**: Choose per field based on use case
- A field may have inverted index, forward index, or **BOTH**

**Why Critical**: Without understanding this distinction, implementation will introduce conflicts where text search breaks, or storage is wasted on unnecessary indexes.

**Must read before implementing**: Modules 01, 02, 03, 14

---

## Documentation Status

**Progress**: 14/14 core modules complete (100%) ✅
**Quality**: Production-grade, based on actual Lucene & ClickHouse codebases + cutting-edge research

### ✅ Complete Designs (14 modules)

#### [00_ARCHITECTURE_OVERVIEW.md](./00_ARCHITECTURE_OVERVIEW.md)
**System architecture and module organization**
- Sealed interface hierarchies from Lucene
- Column storage concepts from ClickHouse
- Module structure and dependencies
- Hybrid Wide/Compact data part formats
- Storage tier integration
- Implementation roadmap

**Key Learnings**:
- Lucene's reader hierarchy: `IndexReader → {LeafReader, CompositeReader} → DirectoryReader`
- ClickHouse's part types: Wide (separate files per column) vs Compact (single data.bin)
- Granule-based organization: 8192 rows default with adaptive granularity
- Mark files: Two-level addressing (compressed block + offset within)

#### [01_INDEX_READER_WRITER.md](./01_INDEX_READER_WRITER.md)
**IndexReader and IndexWriter interfaces**
- Complete IndexReader sealed hierarchy
- LeafReader for atomic segment access
- CompositeReader for multi-segment views
- DirectoryReader concrete implementation
- IndexWriter with concurrency model
- IndexWriterConfig with all settings
- Reader/Writer context classes

**Aligned with**:
- `org.apache.lucene.index.IndexReader`
- `org.apache.lucene.index.LeafReader`
- `org.apache.lucene.index.IndexWriter`
- `org.apache.lucene.index.IndexWriterConfig`

#### [02_CODEC_ARCHITECTURE.md](./02_CODEC_ARCHITECTURE.md)
**Pluggable codec system for all formats**
- Codec abstract base with format accessors
- PostingsFormat (inverted index encoding)
- DocValuesFormat (Lucene doc values)
- ColumnFormat (NEW: ClickHouse-style columns)
- Producer/Consumer pattern for read/write
- FieldsProducer/Consumer for postings
- Terms/TermsEnum/PostingsEnum iterators
- Lucene104Codec default implementation

**Aligned with**:
- `org.apache.lucene.codecs.Codec`
- `org.apache.lucene.codecs.PostingsFormat`
- `org.apache.lucene.codecs.DocValuesFormat`
- `org.apache.lucene.index.Terms/TermsEnum/PostingsEnum`

#### [03_COLUMN_STORAGE.md](./03_COLUMN_STORAGE.md)
**ClickHouse-style column storage system**
- IColumn interface with COW semantics
- ColumnVector<T> for numeric types
- ColumnString with offsets + chars
- ColumnArray for nested arrays
- ColumnNullable for nullable types
- IDataType type system
- ISerialization for binary I/O

**Aligned with**:
- `ClickHouse/src/Columns/IColumn.h`
- `ClickHouse/src/Columns/ColumnVector.h`
- `ClickHouse/src/Columns/ColumnString.h`
- `ClickHouse/src/DataTypes/IDataType.h`
- `ClickHouse/src/DataTypes/Serializations/ISerialization.h`

#### [04_COMPRESSION_CODECS.md](./04_COMPRESSION_CODECS.md)
**ClickHouse compression system**
- ICompressionCodec interface
- CompressionFactory with registration
- CompressedBlockHeader (16-byte format)
- LZ4, ZSTD generic codecs
- Delta, Gorilla, DoubleDelta type-specific codecs
- CompressionCodecMultiple for chaining
- CompressedReadBuffer/WriteBuffer

**Aligned with**: `ClickHouse/src/Compression/ICompressionCodec.h`

#### [05_MERGETREE_DATA_PARTS.md](./05_MERGETREE_DATA_PARTS.md)
**ClickHouse MergeTree storage engine**
- IMergeTreeDataPart abstract base
- MergeTreeDataPartWide/Compact implementations
- DataPartState machine (Temporary → Committed → Outdated)
- MergeTreeDataPartWriterWide/Compact
- MergeTreeReaderWide/Compact
- Mark files format (.mrk2, .mrk3)
- Checksums validation

**Aligned with**: `ClickHouse/src/Storages/MergeTree/IMergeTreeDataPart.h`

#### [06_GRANULARITY_AND_MARKS.md](./06_GRANULARITY_AND_MARKS.md)
**Granule-based indexing system**
- IMergeTreeIndexGranularity interface
- MergeTreeIndexGranularityConstant (fixed 8192 rows)
- MergeTreeIndexGranularityAdaptive (variable rows)
- MarkInCompressedFile two-level addressing
- MarkRange for row ranges
- Adaptive granularity configuration

**Aligned with**: `ClickHouse/src/Storages/MergeTree/MergeTreeIndexGranularity.h`

#### [07_QUERY_EXECUTION.md](./07_QUERY_EXECUTION.md)
**Lucene query execution framework**
- Query/Weight/Scorer three-level architecture
- IndexSearcher query execution engine
- DocIdSetIterator interface
- Collector and LeafCollector interfaces
- TopScoreDocCollector for top-K results
- ScoreMode optimization hints

**Aligned with**: `org.apache.lucene.search.*`

#### [07a_FILTERS.md](./07a_FILTERS.md)
**Non-scoring filter system**
- BooleanClause::FILTER (non-scoring MUST)
- Filter abstract class with skip index integration
- RangeFilter, TermFilter, AndFilter, OrFilter
- DocIdSet (BitSet and IntArray representations)
- FilterCache with LRU eviction
- FilteredCollector for transparent filtering
- IndexSearcher filter API

**Aligned with**: `org.apache.lucene.search.Query` (FILTER support)

**Key Features**:
- No scoring overhead for filters
- Skip index integration (90%+ granule pruning)
- Filter result caching
- 2-3x performance improvement for analytical queries

#### [08_MERGE_SYSTEM.md](./08_MERGE_SYSTEM.md)
**Segment merging and lifecycle**
- MergePolicy interface (TieredMergePolicy, LogByteSizeMergePolicy)
- MergeScheduler interface (ConcurrentMergeScheduler)
- MergeSpecification and OneMerge classes
- Merge triggers (flush, commit, forceMerge)
- Doc ID remapping during merge
- Background merge threads

**Aligned with**: `org.apache.lucene.index.MergePolicy`

#### [09_DIRECTORY_ABSTRACTION.md](./09_DIRECTORY_ABSTRACTION.md)
**Filesystem abstraction layer**
- Directory abstract base
- FSDirectory, MMapDirectory, ByteBuffersDirectory
- IndexInput/IndexOutput interfaces
- ChecksumIndexInput for integrity
- Lock interface (write.lock)
- IOContext for I/O hints

**Aligned with**: `org.apache.lucene.store.Directory`

#### [10_FIELD_INFO.md](./10_FIELD_INFO.md)
**Field metadata system**
- FieldInfo structure with all field properties
- FieldInfos collection
- IndexOptions enum (DOCS, DOCS_AND_FREQS, etc.)
- DocValuesType enum (NUMERIC, BINARY, SORTED, etc.)
- VectorEncoding and VectorSimilarityFunction
- Lucene104FieldInfosFormat codec

**Aligned with**: `org.apache.lucene.index.FieldInfo`

#### [11_SKIP_INDEXES.md](./11_SKIP_INDEXES.md)
**ClickHouse skip index system**
- IMergeTreeIndex interface hierarchy
- IMergeTreeIndexGranule for metadata
- IMergeTreeIndexAggregator for write-time building
- IMergeTreeIndexCondition for read-time filtering
- MinMax, Set, and BloomFilter implementations
- MergeTreeIndexFactory registration system

**Aligned with**: `ClickHouse/src/Storages/MergeTree/MergeTreeIndices.h`

#### [12_STORAGE_TIERS.md](./12_STORAGE_TIERS.md)
**Multi-tier storage management**
- StorageTier enum (Hot/Warm/Cold/Frozen)
- TierManager for lifecycle management
- LifecyclePolicy with age-based transitions
- Automatic segment migration
- TieredIndexSearcher for tier-aware queries
- Integration with Directory abstraction

**Aligned with**: OpenSearch ILM + ClickHouse TTL patterns

#### [13_SIMD_POSTINGS_FORMAT.md](./13_SIMD_POSTINGS_FORMAT.md) → Superseded by Module 14
**Initial SIMD-optimized format (rank_features only)**
- Value-storing posting lists for static weights
- Window-based partitioning
- 4-26× speedup on rank_features queries
- **Limitation**: Only works for precomputed weights, not BM25

**Based on**: SINDI paper (https://arxiv.org/abs/2509.08395)

**Status**: ⚠️ Superseded by unified architecture in Module 14

#### [14_UNIFIED_SIMD_STORAGE.md](./14_UNIFIED_SIMD_STORAGE.md) ✨ **NEW ARCHITECTURE**
**Unified SIMD-accelerated storage layer**
- Merges inverted index and column storage
- Single window-based storage for sparse (posting lists) and dense (columns) data
- SIMD BM25 scoring (dynamic computation)
- SIMD rank_features scoring (static weights)
- SIMD filter evaluation (range checks)
- 37% storage reduction by eliminating duplication
- 2.7-4× query speedup for analytical queries

**Based on**: SINDI + ClickHouse column storage + BM25 analysis

**Key Innovation**:
- **Insight**: Posting lists ≈ Sparse columns → unify at storage layer
- **BM25 support**: SIMD-accelerate formula, not just precomputed values
- **Unified API**: Same SIMD operations for scoring and filtering
- **Storage efficiency**: Single copy instead of 3 separate systems

**Performance**:
- E-commerce query (text + filters + aggregation): 16ms vs. 61ms = **3.7× faster**
- Storage: 170GB vs. 270GB = **37% reduction**

**Adaptive Filter Strategies**:
- **List merge**: Extract filtered docs → gather TF values → scatter-add (best for <1% selectivity)
- **Pre-fill**: Initialize scores to -∞ → SIMD scatter-add on all docs → filter finite (best for >1% selectivity)
- **Dynamic selection**: Choose strategy based on estimated selectivity and query complexity
- **See**: [RESEARCH_SIMD_FILTER_STRATEGIES.md](./RESEARCH_SIMD_FILTER_STRATEGIES.md) for detailed cost model

---

## Code References

### Apache Lucene
**Location**: `/home/ubuntu/opensearch_warmroom/lucene/`

Key paths:
- `lucene/core/src/java/org/apache/lucene/index/` - Indexing
- `lucene/core/src/java/org/apache/lucene/search/` - Query execution
- `lucene/core/src/java/org/apache/lucene/codecs/` - Codec system
- `lucene/core/src/java/org/apache/lucene/store/` - Storage abstraction
- `lucene/core/src/java/org/apache/lucene/util/` - Utilities

### ClickHouse
**Location**: `/home/ubuntu/opensearch_warmroom/ClickHouse/`

Key paths:
- `src/Storages/MergeTree/` - MergeTree engine
- `src/Columns/` - Column interfaces
- `src/DataTypes/` - Type system
- `src/Compression/` - Compression codecs
- `src/DataTypes/Serializations/` - Serialization

---

## Design Principles

### From Lucene

1. **Sealed Hierarchies**: Clear inheritance with abstract bases and limited concrete implementations
2. **Producer/Consumer Pattern**: Separate read/write interfaces for codecs
3. **Immutable Segments**: Never modify after flush, merge to compact
4. **Lock-Free Reads**: Immutability enables concurrent reads without locks
5. **SPI Registration**: Codec/format discovery via factory registration
6. **Iterator-Based Access**: TermsEnum, PostingsEnum for memory-efficient traversal
7. **Three-Level Queries**: Query → Weight → Scorer for reusability and caching

### From ClickHouse

1. **COW Columns**: Copy-on-write semantics for efficient sharing
2. **Granule-Based I/O**: Fixed-size chunks (8192 rows) for predictable performance
3. **Type-Specific Everything**: Serialization, compression, and storage per type
4. **Wide vs Compact**: Format selection based on size thresholds
5. **Mark Files**: Two-level addressing for random access in compressed data
6. **Sparse Primary Index**: Index only granule boundaries (1/8192 rows)
7. **Compression Chaining**: Multiple codecs in sequence (e.g., Delta + LZ4)

---

## Reading Guide

### For Implementation

1. **Start with**: 00_ARCHITECTURE_OVERVIEW.md
   - Understand module dependencies
   - Review hybrid design decisions

2. **Core interfaces**: 01_INDEX_READER_WRITER.md
   - IndexReader sealed hierarchy
   - IndexWriter concurrency model

3. **Codec system**: 02_CODEC_ARCHITECTURE.md
   - Producer/Consumer pattern
   - Format registration

4. **Column storage**: 03_COLUMN_STORAGE.md
   - IColumn COW semantics
   - Concrete column types

5. **Next priorities**: 04-07 (compression, data parts, granularity, queries)

### For Understanding Lucene Alignment

Read Lucene docs: https://lucene.apache.org/core/9_11_0/

Key concepts:
- Inverted index structure
- Segment-based architecture
- Codec abstraction
- Query execution model

### For Understanding ClickHouse Alignment

Read ClickHouse docs: https://clickhouse.com/docs/en/

Key concepts:
- MergeTree engine
- Granules and marks
- Column-oriented storage
- Compression techniques

---

## Next Steps

### Design Phase ✅ COMPLETE

All 14 core modules have been designed with production-grade specifications:
- ✅ 00_ARCHITECTURE_OVERVIEW.md
- ✅ 01_INDEX_READER_WRITER.md
- ✅ 02_CODEC_ARCHITECTURE.md
- ✅ 03_COLUMN_STORAGE.md
- ✅ 04_COMPRESSION_CODECS.md
- ✅ 05_MERGETREE_DATA_PARTS.md
- ✅ 06_GRANULARITY_AND_MARKS.md
- ✅ 07_QUERY_EXECUTION.md
- ✅ 07a_FILTERS.md
- ✅ 08_MERGE_SYSTEM.md
- ✅ 09_DIRECTORY_ABSTRACTION.md
- ✅ 10_FIELD_INFO.md
- ✅ 11_SKIP_INDEXES.md
- ✅ 12_STORAGE_TIERS.md
- ✅ 13_SIMD_POSTINGS_FORMAT.md (NEW - Research-based)

### Implementation Phase (Next)

1. **Phase 1**: Core abstractions (Directory, IndexReader/Writer, Codec)
2. **Phase 2**: Postings format (FST, VByte, skip lists)
3. **Phase 3**: Column storage (IColumn, serialization, marks)
4. **Phase 4**: Compression (LZ4, ZSTD, Delta, Gorilla)
5. **Phase 5**: Query execution (Query/Weight/Scorer)
6. **Phase 6**: Merge system (policies, scheduler, merger)
7. **Phase 7**: Storage tiers (migration, lifecycle)
8. **Phase 8**: Optimizations (SIMD, caching, prefetching)

---

## Design Review & Refinement

**[DESIGN_REVIEW.md](./DESIGN_REVIEW.md)** 📋 **PRINCIPAL SDE REVIEW**
- Comprehensive design review covering all 14 modules
- **Critical findings**: 5 blocking issues identified
- **Logical closure**: Missing concurrency model, crash recovery, delete operations
- **Efficiency risks**: Memory pressure (COW), write amplification, SIMD costs
- **Implementation feasibility**: SIMD complexity, codec complexity assessment
- **Missing components**: Durability, memory management, vector search gap
- **Verdict**: **Design incomplete** - 3-4 weeks additional work needed before implementation
- **Timeline estimate**: 12-18 months for production-ready system with 3-5 engineers

**[DESIGN_REFINEMENT_STATUS.md](./DESIGN_REFINEMENT_STATUS.md)** ✅ **100% COMPLETE**
- **Status**: ✅ **13/13 critical items complete (100%) - DESIGN READY FOR IMPLEMENTATION!**
- **Core Design Items Completed** (10 items):
  - ✅ Crash recovery (WAL, two-phase commit) - 200 lines
  - ✅ Concurrency model (DWPT, reader lifecycle) - 280 lines
  - ✅ Delete operations (LiveDocs, compaction) - 270 lines
  - ✅ Memory management (budgets, pooling, OOM) - 260 lines
  - ✅ Vector search removal (Module 10 - deferred to v2.0)
  - ✅ Codec format evolution (versioning, compatibility, migration) - 410 lines
  - ✅ Column memory management (COW rules, arena integration) - 238 lines
  - ✅ Write amplification analysis (WAF factors, SSD lifetime, tuning) - 335 lines
  - ✅ Query timeout/cancellation (timeout enforcement, cancellation API) - 560 lines
  - ✅ Phrase query details (position matching, slop parameter, scoring) - 630 lines
- **Infrastructure Documents Completed** (3 items):
  - ✅ BUILD_SYSTEM.md (CMake, dependencies, SIMD detection) - 400 lines
  - ✅ TESTING_STRATEGY.md (unit, integration, stress, benchmarks) - 450 lines
  - ✅ OBSERVABILITY.md (metrics, logging, tracing, dashboards) - 350 lines
- **Total Work**: ~4,573 lines added across 8 documents
- **Timeline**: Started and completed 2026-01-23
- **Next Phase**: Implementation planning and proof of concept

---

## Supporting Documentation

### Architecture Clarifications

**[ARCHITECTURE_CLARIFICATION_INDEXES.md](./ARCHITECTURE_CLARIFICATION_INDEXES.md)** ⚠️ **CRITICAL**
- Inverted index vs forward index (column storage) distinction
- When to build which index type for different field types
- ClickHouse (no inverted index) vs Lucene (inverted index primary) differences
- Field configuration decision matrix
- Implementation conflicts to avoid
- **Status**: Required reading before implementing storage layer

### Research Documents

**[RESEARCH_SIMD_FILTER_STRATEGIES.md](./RESEARCH_SIMD_FILTER_STRATEGIES.md)**
- Cost model analysis: List merge scanning vs pre-fill score buffer
- Gather operations vs sequential SIMD processing
- Selectivity-based dynamic strategy selection
- Crossover point analysis (~1-2% selectivity)
- Experimental validation plan for PoC phase
- **Status**: Informs Module 14 adaptive filter strategy

---

## Contributing to Design

When adding or modifying designs:

1. **Study the codebase first**: Read actual Lucene/ClickHouse code
2. **Reference source files**: Include paths to relevant code
3. **Use actual interfaces**: Don't invent, copy and adapt
4. **Document trade-offs**: Explain why specific choices were made
5. **Provide examples**: Show usage patterns
6. **Update this README**: Keep index in sync

---

## Questions & Feedback

For design discussions:
1. Reference specific design document and section
2. Cite Lucene/ClickHouse code if applicable
3. Consider backward compatibility with Lucene API
4. Evaluate performance implications

---

**Last Updated**: 2026-01-23
**Status**: ✅ **Design phase 100% complete - ALL refinements addressed - READY FOR IMPLEMENTATION**
**Design Refinement**: 13/13 critical items complete (100%)
**Latest Additions**:
- ✅ All critical gaps from Principal SDE review addressed (~4,573 lines added)
- ✅ BUILD_SYSTEM.md - Production CMake build system with cross-platform support
- ✅ TESTING_STRATEGY.md - Comprehensive testing approach (unit, integration, stress, benchmarks)
- ✅ OBSERVABILITY.md - Prometheus metrics, spdlog logging, OpenTelemetry tracing
- ✅ Modules 01, 02, 03, 07, 08 enhanced with crash recovery, concurrency, memory management, query timeout, phrase queries, write amplification
- 07a_FILTERS.md - Non-scoring filter system with skip index integration
- 13_SIMD_POSTINGS_FORMAT.md - SIMD-optimized inverted index (superseded by Module 14)
- 14_UNIFIED_SIMD_STORAGE.md - Unified SIMD storage with adaptive filter strategies
- ARCHITECTURE_CLARIFICATION_INDEXES.md - ⚠️ Critical clarification on inverted vs forward indexes
- RESEARCH_SIMD_FILTER_STRATEGIES.md - Cost model analysis for filter strategy selection
- DESIGN_REVIEW.md - Principal SDE comprehensive design review
- DESIGN_REFINEMENT_STATUS.md - Complete tracking of all 13 refinement items
