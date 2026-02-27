# Diagon vs Lucene: Ingestion Speed Comparison

**Date**: 2026-02-27
**Context**: Post Issue #6 optimizations (round 3: direct-to-posting tokenization, jemalloc, fair timing methodology)

## Test Environment

| Parameter | Diagon | Lucene |
|-----------|--------|--------|
| Version | 1.0.0 | 11.0.0-SNAPSHOT |
| Language | C++ (GCC 13.3, -O3 -march=native) | Java 25.0.2 (G1GC, 4GB heap) |
| Tokenizer | ASCII fast-path + ICU BreakIterator (fallback) | StandardAnalyzer (JFlex) |
| Allocator | jemalloc | JVM (G1GC) |
| Directory | FSDirectory | FSDirectory |
| Platform | Linux 6.14.0-1015-aws, 64 vCPU | same |

## Results Summary

| Benchmark | Diagon | Lucene | Ratio | Status |
|-----------|--------|--------|-------|--------|
| Reuters single-field (stored=false) | **43,019 docs/sec** | 35,958 docs/sec | **1.20x FASTER** | Target exceeded |
| Multi-field 25x20 (stored=true) | **13,871 docs/sec** | 8,997 docs/sec | **1.54x FASTER** | Target exceeded |
| Synthetic 1x50 (stored=true) | **174,461 docs/sec** | 74,861 docs/sec | **2.33x FASTER** | Target exceeded |

**Diagon beats Lucene across ALL benchmarks.**

## Optimization History

| Round | Reuters | Multi-field | Synthetic | Key Change |
|-------|---------|------------|-----------|------------|
| Baseline (pre-Issue #6) | ~8,900 | ~8,900 (CGO) | — | No optimization |
| Round 1 (cached BreakIterator, single-pass DWPT, batch API, flat fieldLengths) | 10,277 | 2,761 | 51,326 | Cached ICU + per-field posting maps |
| Round 2a (O(n²) ramBytesUsed fix) | 53,190 | 4,732 | — | StoredFieldsWriter incremental tracking |
| Round 2b (ASCII fast-path tokenizer) | 19,043† | 9,532 | 117,000 | Bypass ICU for ASCII text |
| Round 2c (fair timing + jemalloc) | 33,764 | 10,735 | 134,730 | Pre-load docs + jemalloc allocator |
| **Round 2d (direct-to-posting)** | **43,019** | **13,871** | **174,461** | Eliminate termPositionsCache double-hashing |

†Round 2b Reuters included file I/O in timed section (unfair methodology); corrected in Round 2c.

## Detailed Results

### Benchmark 1: Reuters-21578 (single body field, stored=false)

Real-world dataset. Documents pre-loaded into memory before timing (matches Lucene methodology).

| Metric | Diagon | Lucene |
|--------|--------|--------|
| Documents | 19,043 | 21,578 |
| Best time | 0.441s | 0.600s |
| **Throughput** | **43,019 docs/sec** | **35,958 docs/sec** |
| Index size | 12 MB (685 B/doc) | 5.9 MB (287 B/doc) |

**Diagon is 1.20x FASTER than Lucene.** Reuters gap completely closed and reversed.

### Benchmark 2: Multi-field (25 fields x 20 words, stored=true)

Matches Issue #6 CGO workload (25-field documents). Both store all fields.

| Metric | Diagon | Lucene |
|--------|--------|--------|
| Documents | 5,000 | 5,000 |
| Fields/doc | 25 | 25 |
| Words/field | 20 | 20 |
| **Throughput** | **13,871 docs/sec** | **8,997 docs/sec** |

**Diagon is 1.54x FASTER than Lucene.** The Issue #6 target workload is now dominated by Diagon.

### Benchmark 3: Synthetic single-field (1 body x 50 words, stored=true)

Minimal overhead per document — isolates tokenizer + posting list construction.

| Metric | Diagon | Lucene |
|--------|--------|--------|
| Documents | 5,000 | 5,000 |
| Fields/doc | 1 | 1 |
| Words/field | 50 | 50 |
| **Throughput** | **174,461 docs/sec** | **74,861 docs/sec** |

**Diagon is 2.33x FASTER than Lucene.**

## Key Optimizations (Issue #6)

### Round 2d: Direct-to-posting tokenization (+27% Reuters, +29% multi-field)

Eliminated `termPositionsCache_` intermediate hash map. Previously each token was hashed TWICE: once into a temporary position-grouping map, then again into `fieldPostings_`. Now tokens write directly to `fieldPostings_` with in-place freq tracking via `pendingFreqIndex`.

**What changed**:
- Removed `unordered_map<string, vector<int>> termPositionsCache_`
- Added `pendingFreqIndex` to `PostingData` for in-place freq updates
- Each token: one `try_emplace` into `fieldPostings_` (was: one `operator[]` into cache + one `emplace` into postings)
- Unique terms are copied into postings map only once (was: copied into cache, then copied again into postings)

**Impact**: Reuters 33,764 → 43,019 (+27%). Multi-field 10,735 → 13,871 (+29%). Synthetic 134,730 → 174,461 (+29%).

### Round 2c: Fair timing methodology + jemalloc (+33% + 30% Reuters)

**Fair timing**: Pre-load all Reuters documents into memory before timed section (matches Lucene's `LuceneIngestionBenchmark` methodology). Previously file I/O was included in Diagon's timing.

**jemalloc**: Linked jemalloc allocator. Profile showed 17.5% of CPU in glibc malloc/free. jemalloc's thread-local caches and size-class bins reduce allocation overhead significantly.

**Impact**: Reuters 19,043 → 25,425 (fair timing) → 33,764 (jemalloc).

### Round 2b: ASCII Fast-Path Tokenizer (+100% multi-field, +120% single-field)

Added `tokenizeAscii()` that splits on non-alphanumeric characters and lowercases with simple byte operations, bypassing all ICU overhead (BreakIterator, UnicodeString, toLower, char32At). Falls back to ICU for non-ASCII text.

**Impact**: Multi-field 4,732 → 9,532 (+101%). Single-field 53K → 117K (+121%).

### Round 2a: O(n²) StoredFieldsWriter::ramBytesUsed() (+64% multi-field)

`StoredFieldsWriter::ramBytesUsed()` iterated over ALL stored fields of ALL buffered documents on EVERY `addDocument()` call (via `needsFlush()`). Fixed with incremental `bytesUsed_` counter.

**Impact**: Multi-field 2,884 → 4,732 (+64%).

### Round 1: Per-field posting maps, cached BreakIterator, single-pass DWPT

- Per-field posting maps (eliminated pair-key hashing)
- `thread_local` cached ICU BreakIterator
- Merged 4 field iteration passes into 1 in DWPT
- Flat vector fieldLengths (O(1) access)

## Remaining Opportunities

1. **Index size 2.4x larger** (685 vs 287 bytes/doc) — Diagon writes more per-document metadata. Compressing stored fields or optimizing segment format could further improve commit I/O.
2. **Concurrent segment flush** — Lucene's IndexWriter uses concurrent flush threads. Diagon is single-threaded.
3. **String interning** — Frequently occurring terms could be interned to reduce hash map key overhead.

## Reproducibility

```bash
# Diagon Reuters
/build_diagon target=benchmarks
rm -rf /tmp/diagon_reuters_index
cd /home/ubuntu/diagon/build/benchmarks && ./ReutersBenchmark

# Diagon synthetic
./IndexingBenchmark --benchmark_filter="BM_IndexDocuments/5000|BM_IndexMultiFieldDocuments/5000"

# Lucene
cd /home/ubuntu/diagon/benchmarks/java
JAVA_HOME="/usr/lib/jvm/jdk-25.0.2" PATH="$JAVA_HOME/bin:$PATH" \
LUCENE_DIR="/home/ubuntu/opensearch_warmroom/lucene" \
CORE_JAR=$(find $LUCENE_DIR/lucene/core/build/libs -name "lucene-core-*.jar" | head -1) \
ANALYSIS_JAR=$(find $LUCENE_DIR/lucene/analysis/common/build/libs -name "lucene-*.jar" | head -1) \
javac -cp "$CORE_JAR:$ANALYSIS_JAR" LuceneIngestionBenchmark.java && \
java -cp "$CORE_JAR:$ANALYSIS_JAR:." -Xmx4g -Xms4g LuceneIngestionBenchmark
```
