# Phase 5: Optimization & Production Features

**Date**: 2026-01-24
**Status**: PLANNING
**Prerequisites**: Phase 4 Complete ✅

---

## Overview

Phase 5 transforms Diagon from a working prototype into a production-ready search engine. This phase focuses on:
1. **Performance optimization** - SIMD, compression, efficient codecs
2. **Production infrastructure** - CI/CD, configuration, tooling
3. **Advanced queries** - Boolean, Phrase, Wildcard, Range
4. **Scalability features** - FST term dictionary, skip lists

After Phase 5, Diagon will be ready for real-world workloads with production-grade performance and features.

---

## Phase 5 Structure

Phase 5 is divided into three sub-phases:

### Phase 5a: Optimization & Infrastructure (Tasks #11-17)
**Focus**: Performance optimization and development infrastructure
**Duration**: ~1-2 weeks
**Goal**: Establish baseline optimizations and CI/CD

### Phase 5b: Advanced Queries (Tasks #18-23)
**Focus**: Multi-term queries and query complexity
**Duration**: ~2-3 weeks
**Goal**: Support Boolean, Phrase, Wildcard, Range queries

### Phase 5c: Production Codec (Tasks #24-29)
**Focus**: Space-efficient storage with FST, compression, skip lists
**Duration**: ~2-3 weeks
**Goal**: Reduce memory usage by 10-100x, improve query speed by 10x

---

## Phase 5a: Optimization & Infrastructure

### Task 11: AVX2 SIMD BM25 Scorer ⏱️ 4-5 hours
**Priority**: HIGH - Direct performance improvement

**Goal**: Vectorize BM25 scoring for 4-8x throughput improvement.

**What to Implement**:
- AVX2 BM25Scorer with 8-way parallelism
- Vectorized IDF multiplication
- Vectorized TF component calculation
- Fallback to scalar implementation on non-AVX2 CPUs

**Current Baseline** (from Phase 4 benchmarks):
- 13.9k queries/sec for 10K doc index
- 70-72μs per query

**Target**: 55-100k queries/sec (4-8x improvement)

**Implementation**:
```cpp
// BM25ScorerSIMD.h
#ifdef DIAGON_HAVE_AVX2
class BM25ScorerSIMD : public Scorer {
public:
    // Process 8 documents at once
    void scoreDocuments(
        const int* docIDs,
        const int* freqs,
        int count,
        float* scores
    );

private:
    __m256 idf_;
    __m256 k1_;
    __m256 b_;
    __m256 avgFieldLength_;

    // Vectorized BM25 formula
    __m256 computeBM25Vector(
        __m256 freqs,
        __m256 fieldLengths
    );
};
#endif
```

**Tests**:
- Verify correctness vs scalar implementation
- Benchmark speedup on AVX2 CPU
- Fallback to scalar on non-AVX2
- Edge cases: freq=0, freq=1, high freq

**Success Criteria**:
- ✅ Correctness: scores match scalar ±0.001
- ✅ Performance: 4-8x speedup on AVX2 hardware
- ✅ Portability: Falls back gracefully
- ✅ Benchmark: Update SearchBenchmark with SIMD results

---

### Task 12: LZ4 Compression Codec ⏱️ 3-4 hours
**Priority**: MEDIUM - Storage efficiency

**Goal**: Add LZ4 compression for postings and stored fields.

**What to Implement**:
- LZ4CompressionCodec wrapper around liblz4
- Compressed block format with header
- Integration with IndexInput/IndexOutput
- Block-level compression (e.g., 8KB blocks)

**Expected Compression**:
- Postings: 30-50% space savings
- Stored fields: 40-60% space savings
- Speed: ~500 MB/s compression, ~2 GB/s decompression

**Implementation**:
```cpp
// LZ4Codec.h
class LZ4Codec : public CompressionCodec {
public:
    static constexpr int BLOCK_SIZE = 8192;

    // Compress data
    std::vector<uint8_t> compress(
        const uint8_t* data,
        size_t size
    ) override;

    // Decompress data
    std::vector<uint8_t> decompress(
        const uint8_t* compressed,
        size_t compressedSize,
        size_t originalSize
    ) override;
};
```

**Tests**:
- Round-trip: compress → decompress → verify
- Various data sizes: 1KB, 8KB, 100KB, 1MB
- Random data, repetitive data, text data
- Benchmark compression ratio and speed

**Success Criteria**:
- ✅ Correctness: round-trip verified
- ✅ Compression ratio: 30-60%
- ✅ Performance: >500 MB/s compression
- ✅ Integration: works with existing codecs

---

### Task 13: ZSTD Compression Codec ⏱️ 3-4 hours
**Priority**: LOW - Better compression, slower speed

**Goal**: Add ZSTD compression for high compression ratio scenarios.

**What to Implement**:
- ZSTDCompressionCodec wrapper around libzstd
- Configurable compression level (1-22)
- Dictionary support (optional)
- Block-level compression

**Expected Compression**:
- Postings: 50-70% space savings (vs 30-50% for LZ4)
- Stored fields: 60-80% space savings
- Speed: ~200 MB/s compression, ~800 MB/s decompression

**Implementation**:
```cpp
// ZSTDCodec.h
class ZSTDCodec : public CompressionCodec {
public:
    explicit ZSTDCodec(int compressionLevel = 3);

    std::vector<uint8_t> compress(
        const uint8_t* data,
        size_t size
    ) override;

    std::vector<uint8_t> decompress(
        const uint8_t* compressed,
        size_t compressedSize,
        size_t originalSize
    ) override;

private:
    int compressionLevel_;
};
```

**Tests**:
- Same as LZ4Codec tests
- Test different compression levels
- Compare compression ratio vs LZ4
- Benchmark speed vs LZ4

**Success Criteria**:
- ✅ Higher compression than LZ4
- ✅ Acceptable decompression speed
- ✅ Configurable compression levels
- ✅ Integration with codec framework

---

### Task 16: CI/CD with GitHub Actions ⏱️ 4-6 hours
**Priority**: HIGH - Development infrastructure

**Goal**: Automated testing and build validation on every commit.

**What to Implement**:
- `.github/workflows/ci.yml` - Main CI pipeline
- `.github/workflows/benchmarks.yml` - Benchmark tracking
- Matrix builds: GCC/Clang, Ubuntu/macOS
- Test coverage reporting (optional)

**CI Workflow**:
```yaml
name: CI

on: [push, pull_request]

jobs:
  build-and-test:
    strategy:
      matrix:
        os: [ubuntu-latest, macos-latest]
        compiler: [gcc, clang]
        build_type: [Debug, Release]

    steps:
      - uses: actions/checkout@v3

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake liblz4-dev libzstd-dev libbenchmark-dev

      - name: Configure
        run: |
          cmake -B build -DCMAKE_BUILD_TYPE=${{ matrix.build_type }}

      - name: Build
        run: cmake --build build --parallel

      - name: Test
        run: cd build && ctest --output-on-failure

      - name: Benchmarks
        if: matrix.build_type == 'Release'
        run: |
          ./build/benchmarks/IndexingBenchmark
          ./build/benchmarks/SearchBenchmark
```

**Success Criteria**:
- ✅ CI runs on every commit
- ✅ Tests pass on multiple platforms
- ✅ Build failures caught early
- ✅ Benchmark results tracked

---

### Task 17: Configuration Files ⏱️ 1-2 hours
**Priority**: LOW - Code hygiene

**Goal**: Standard repository configuration files.

**What to Create**:
1. **`.gitignore`** - Ignore build artifacts, IDE files
2. **`.clang-format`** - Consistent code formatting
3. **`.editorconfig`** - Editor settings
4. **`CONTRIBUTING.md`** - Contribution guidelines
5. **`CODE_OF_CONDUCT.md`** - Community guidelines

**.gitignore**:
```gitignore
# Build artifacts
build/
CMakeCache.txt
CMakeFiles/
*.so
*.a

# Test artifacts
Testing/
*.test

# Benchmark executables
benchmarks/IndexingBenchmark
benchmarks/SearchBenchmark

# IDE
.vscode/
.idea/
*.swp
```

**.clang-format**:
```yaml
BasedOnStyle: LLVM
IndentWidth: 4
ColumnLimit: 100
AllowShortFunctionsOnASingleLine: Empty
```

**Success Criteria**:
- ✅ Clean repository (no build artifacts)
- ✅ Consistent formatting
- ✅ CI enforces formatting
- ✅ Documentation for contributors

---

## Phase 5b: Advanced Queries

### Task 18: BooleanQuery (AND/OR/NOT) ⏱️ 8-10 hours
**Priority**: HIGH - Core search feature

**Goal**: Multi-clause queries with Boolean logic.

**What to Implement**:
- BooleanQuery with MUST/SHOULD/MUST_NOT clauses
- BooleanWeight with clause coordination
- BooleanScorer with score aggregation
- Optimization: skip MUST_NOT postings

**Example**:
```cpp
BooleanQuery query;
query.add(TermQuery("field", "search"), Occur::MUST);
query.add(TermQuery("field", "engine"), Occur::SHOULD);
query.add(TermQuery("field", "spam"), Occur::MUST_NOT);
```

**Tests**:
- Single MUST clause
- Multiple MUST clauses (AND)
- Multiple SHOULD clauses (OR)
- MUST + SHOULD + MUST_NOT
- Empty query, no matches

---

### Task 19: PhraseQuery ⏱️ 10-12 hours
**Priority**: MEDIUM - Important for precision

**Goal**: Exact phrase matching using positions.

**What to Implement**:
- PhraseQuery with term sequence
- Position-based matching
- Slop tolerance (optional)
- PhraseScorer with position validation

**Example**:
```cpp
PhraseQuery query("field");
query.add("search");
query.add("engine");
// Matches: "search engine" but not "engine search"
```

---

### Task 20: WildcardQuery ⏱️ 6-8 hours
**Priority**: LOW - Convenience feature

**Goal**: Pattern-based term matching.

**What to Implement**:
- Wildcard pattern parsing (*, ?)
- Automaton-based matching (optional: use FST)
- WildcardQuery expansion to multiple TermQueries

---

### Task 21: RangeQuery ⏱️ 6-8 hours
**Priority**: MEDIUM - Filtering feature

**Goal**: Numeric and term range filtering.

**What to Implement**:
- RangeQuery for numeric fields
- RangeQuery for term fields (lexicographic)
- Open/closed bounds

---

## Phase 5c: Production Codec

### Task 22: FST Term Dictionary ⏱️ 12-15 hours
**Priority**: HIGH - Massive memory savings

**Goal**: Replace in-memory term dictionary with FST.

**Current Memory Usage**:
- 100K unique terms ≈ 10-50 MB in memory
- FST: 1-5 MB (10-50x reduction)

**What to Implement**:
- FST-based TermsWriter
- FST-based TermsReader
- Integration with existing TermsEnum
- Binary format for FST serialization

---

### Task 23: VByte Postings Compression ⏱️ 8-10 hours
**Priority**: MEDIUM - Storage efficiency

**Goal**: Variable-byte encoding for postings.

**Current Format**: 4 bytes per integer
**VByte Format**: 1-5 bytes per integer (avg 1.5-2 bytes)

**Space Savings**: 50-70% for postings lists

---

### Task 24: Skip Lists ⏱️ 10-12 hours
**Priority**: MEDIUM - Query optimization

**Goal**: Skip expensive postings during query execution.

**Current**: Sequential scan of all postings
**With Skip Lists**: Jump to relevant documents

**Expected Speedup**: 5-10x for long postings lists

---

## Success Criteria

### Phase 5a: Optimization & Infrastructure
✅ SIMD: 4-8x query throughput improvement
✅ LZ4: 30-50% storage reduction
✅ ZSTD: 50-70% storage reduction
✅ CI/CD: automated testing on every commit
✅ Config: clean repository with formatting enforced

### Phase 5b: Advanced Queries
✅ BooleanQuery: MUST/SHOULD/MUST_NOT working
✅ PhraseQuery: exact phrase matching
✅ WildcardQuery: pattern matching
✅ RangeQuery: numeric/term filtering

### Phase 5c: Production Codec
✅ FST: 10-50x memory reduction for term dictionary
✅ VByte: 50-70% postings compression
✅ Skip lists: 5-10x speedup for long postings

---

## Performance Targets

### Phase 5a Targets
| Metric | Phase 4 Baseline | Phase 5a Target | Improvement |
|--------|------------------|-----------------|-------------|
| Query throughput (10K docs) | 13.9k qps | 55-100k qps | 4-8x |
| Index size (100K docs) | 100 MB | 40-60 MB | 40-60% |
| Compression speed | N/A | 500 MB/s | N/A |

### Phase 5c Targets
| Metric | Phase 5a | Phase 5c Target | Improvement |
|--------|----------|-----------------|-------------|
| Memory (100K terms) | 50 MB | 1-5 MB | 10-50x |
| Long postings query | 1ms | 0.1ms | 10x |
| Postings size | 40 MB | 12-20 MB | 50-70% |

---

## Timeline Estimate

### Phase 5a: Optimization & Infrastructure
- Task 11 (SIMD): 4-5 hours
- Task 12 (LZ4): 3-4 hours
- Task 13 (ZSTD): 3-4 hours
- Task 16 (CI/CD): 4-6 hours
- Task 17 (Config): 1-2 hours
- **Subtotal**: 15-21 hours (~2-3 days)

### Phase 5b: Advanced Queries
- Task 18 (BooleanQuery): 8-10 hours
- Task 19 (PhraseQuery): 10-12 hours
- Task 20 (WildcardQuery): 6-8 hours
- Task 21 (RangeQuery): 6-8 hours
- **Subtotal**: 30-38 hours (~5-6 days)

### Phase 5c: Production Codec
- Task 22 (FST): 12-15 hours
- Task 23 (VByte): 8-10 hours
- Task 24 (Skip Lists): 10-12 hours
- **Subtotal**: 30-37 hours (~5-6 days)

**Total Phase 5**: 75-96 hours (~12-16 days)

---

## Dependencies

### Phase 5 Requires (All Complete ✅)
- ✅ Phase 4: Complete read/write/search pipeline
- ✅ Benchmark infrastructure
- ✅ 166 passing tests

### Phase 5 Enables
- ➡️ Phase 6: Merge policy (needs efficient codec)
- ➡️ Phase 7: Delete support (needs skip lists)
- ➡️ Phase 8: Concurrent DWPT (needs production performance)

---

## Risk Mitigation

### Risk 1: SIMD Complexity
**Mitigation**: Start with scalar implementation, add SIMD incrementally, test thoroughly.

### Risk 2: Compression Overhead
**Mitigation**: Benchmark carefully, make compression optional per field.

### Risk 3: FST Complexity
**Mitigation**: Use existing FST implementation, focus on integration not algorithms.

### Risk 4: Query Complexity
**Mitigation**: Implement BooleanQuery first (foundation), then add other query types incrementally.

---

## Recommended Order

### Week 1-2: Phase 5a (Foundation)
1. Task 17: Config files (quick win)
2. Task 16: CI/CD (infrastructure)
3. Task 12: LZ4 compression
4. Task 13: ZSTD compression
5. Task 11: SIMD BM25

### Week 3-4: Phase 5b (Queries)
6. Task 18: BooleanQuery (MUST/SHOULD/MUST_NOT)
7. Task 19: PhraseQuery
8. Task 21: RangeQuery
9. Task 20: WildcardQuery

### Week 5-6: Phase 5c (Codec)
10. Task 23: VByte compression
11. Task 24: Skip lists
12. Task 22: FST term dictionary

---

## Next Steps

With Phase 4 complete, we're ready to begin Phase 5a. The recommended starting point is:

**Start with Task 17 (Config files)** - Quick 1-2 hour task to clean up repository, then move to Task 16 (CI/CD) to establish automated testing before optimization work.

Alternatively, **start with Task 11 (SIMD)** for immediate performance wins if CI/CD is not a priority.

---

## Conclusion

Phase 5 transforms Diagon from prototype to production-ready:
- **4-8x query performance** improvement with SIMD
- **40-70% storage reduction** with compression
- **Advanced query support** (Boolean, Phrase, Wildcard, Range)
- **10-50x memory reduction** with FST term dictionary
- **Production infrastructure** with CI/CD and testing

After Phase 5, Diagon will be ready for real-world workloads and large-scale deployments.

**Ready to begin Phase 5a: Optimization & Infrastructure** 🚀

---

**Phase 5 Plan Created: 2026-01-24**
