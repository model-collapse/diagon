# Diagon Test Suite

**Test Framework**: Google Test + Google Benchmark
**Design References**: TESTING_STRATEGY.md

## Overview

Comprehensive test suite covering unit tests, integration tests, stress tests, and performance benchmarks for all Diagon modules.

## Test Structure

### Unit Tests (`unit/`)
**Module-specific isolated tests**

Tests are organized by module:
- `store/`: Directory abstraction, IndexInput/Output, Lock
- `util/`: BytesRef, BitSet, NumericUtils
- `index/`: FieldInfo, IndexReader, IndexWriter
- `codecs/`: Codec system, PostingsFormat, DocValuesFormat
- `search/`: Query types, IndexSearcher, Collectors, Filters
- `columns/`: IColumn, MergeTree data parts, Granularity, Skip indexes
- `compression/`: Compression codecs (LZ4, ZSTD, Delta, Gorilla)
- `simd/`: SIMD operations, BM25, Filters, Window storage
- `merge/`: MergePolicy, MergeScheduler

**Characteristics**:
- Fast execution (<5ms per test)
- No external dependencies
- Isolated from filesystem
- Deterministic results

### Integration Tests (`integration/`)
**End-to-end scenario tests**

- `EndToEndTest`: Full indexing + search workflow
- `IndexingSearchTest`: Index documents, verify search results
- `ConcurrencyTest`: Multi-threaded indexing and search
- `CrashRecoveryTest`: WAL recovery after crashes
- `MergeTest`: Segment merging correctness
- `SkipIndexIntegrationTest`: Skip index effectiveness
- `SIMDIntegrationTest`: SIMD unified storage queries

**Characteristics**:
- Medium execution time (100-500ms per test)
- Uses temporary directories
- May spawn threads
- Tests component interactions

### Benchmarks (`benchmark/`)
**Performance measurement**

- `IndexingBenchmark`: Indexing throughput (docs/sec)
- `SearchBenchmark`: Query latency (ms/query)
- `CompressionBenchmark`: Codec throughput and ratio
- `SIMDBenchmark`: SIMD vs scalar speedup
- `FilterBenchmark`: Filter evaluation performance
- `MergeBenchmark`: Merge throughput

**Characteristics**:
- Long execution time (seconds to minutes)
- Reports throughput, latency, ratios
- Compares SIMD vs scalar
- Measures memory usage

## Running Tests

### All Tests
```bash
cd build
ctest --output-on-failure
```

### Unit Tests Only
```bash
./diagon_unit_tests
```

### Integration Tests Only
```bash
./diagon_integration_tests
```

### Specific Test Suite
```bash
./diagon_unit_tests --gtest_filter="IndexWriterTest.*"
```

### Benchmarks
```bash
./diagon_benchmarks --benchmark_filter="BM25"
```

## Test Coverage

### Core Module
- [x] Directory: create, open, delete, list
- [ ] IndexReader: open, reopen, numDocs, maxDoc
- [ ] IndexWriter: addDocument, commit, flush, close
- [ ] Codec: registration, format selection
- [ ] Query: TermQuery, BooleanQuery, PhraseQuery
- [ ] Filter: RangeFilter, TermFilter, cache

### Columns Module
- [ ] IColumn: insert, COW, mutations
- [ ] ColumnVector: numeric operations
- [ ] ColumnString: variable-length strings
- [ ] MergeTree: Wide/Compact format I/O
- [ ] Granularity: constant and adaptive
- [ ] Skip indexes: MinMax, Set, BloomFilter

### Compression Module
- [ ] LZ4: compress/decompress correctness
- [ ] ZSTD: compression levels
- [ ] Delta: monotonic sequences
- [ ] Gorilla: float time series
- [ ] Chaining: Delta + LZ4

### SIMD Module
- [ ] WindowStorage: build and access
- [ ] SIMDBM25: scoring correctness
- [ ] SIMDFilter: range and equality
- [ ] FilterStrategy: adaptive selection

## Test Data

### Synthetic Datasets
Located in `tests/data/`:
- `small.json`: 1K documents
- `medium.json`: 100K documents
- `large.json`: 1M documents (optional, for benchmarks)

### Real-World Datasets
Optional downloads:
- Wikipedia abstracts (5M documents)
- Stack Overflow posts (50M documents)
- MS MARCO passages (8.8M documents)

## Performance Baselines

### Target Metrics
Measured on AWS c5.2xlarge (8 vCPU, 16GB RAM):

| Operation | Target | Actual |
|-----------|--------|--------|
| Indexing throughput | >10K docs/sec | TBD |
| TermQuery latency | <1ms | TBD |
| BooleanQuery latency | <5ms | TBD |
| PhraseQuery latency | <10ms | TBD |
| Filter query latency | <5ms | TBD |
| Merge throughput | >100 MB/sec | TBD |
| SIMD BM25 speedup | >4× vs scalar | TBD |
| SIMD filter speedup | >2× vs scalar | TBD |

## Stress Tests

### Concurrency Stress
```bash
./diagon_integration_tests --gtest_filter="ConcurrencyTest.HighLoad"
```
- 10 indexing threads
- 20 search threads
- 1 merge thread
- Duration: 60 seconds

### Memory Stress
```bash
./diagon_integration_tests --gtest_filter="*MemoryPressure"
```
- Index 10M documents
- Monitor memory usage
- Verify no OOM

### Crash Recovery Stress
```bash
./diagon_integration_tests --gtest_filter="CrashRecoveryTest.RandomCrashes"
```
- Simulate random crashes during indexing
- Verify WAL recovery
- Check data integrity

## Continuous Integration

### GitHub Actions Workflow
`.github/workflows/ci.yml`:
- Run on push and pull request
- Test on Linux, macOS, Windows
- GCC, Clang, MSVC
- Debug and Release builds
- Upload coverage reports

### Pre-Commit Checks
- Unit tests must pass
- No warnings
- clang-format check
- clang-tidy check

## Test Utilities

### Test Fixtures
```cpp
class IndexTestBase : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir_ = createTempDirectory();
        dir_ = FSDirectory::open(temp_dir_);
    }

    void TearDown() override {
        dir_.reset();
        removeDirectory(temp_dir_);
    }

    std::unique_ptr<Directory> dir_;
    std::string temp_dir_;
};
```

### Mock Objects
- MockDirectory: In-memory directory
- MockCodec: Configurable codec
- MockMergePolicy: Deterministic merges

### Assertions
```cpp
// Lucene-style assertions
ASSERT_DOC_COUNT(reader, 100);
ASSERT_TERM_EXISTS(reader, "field", "term");
ASSERT_SCORE_NEAR(expected, actual, epsilon);

// Column assertions
ASSERT_COLUMN_SIZE(column, 100);
ASSERT_COLUMN_VALUE(column, index, expected);

// SIMD assertions
ASSERT_SIMD_EQUALS_SCALAR(simd_result, scalar_result);
```

## Debugging Tests

### Verbose Output
```bash
./diagon_unit_tests --gtest_filter="*" --gtest_verbose
```

### Breakpoint on Failure
```bash
gdb --args ./diagon_unit_tests --gtest_filter="IndexWriterTest.Crash"
(gdb) catch throw
(gdb) run
```

### Sanitizers
```bash
# Address Sanitizer
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address"

# Thread Sanitizer
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=thread"
```

## Contributing Tests

### Test Naming
- Use `CamelCase` for test names
- Format: `ModuleTest.Scenario`
- Example: `IndexWriterTest.ConcurrentIndexing`

### Test Organization
- One test file per module
- Group related tests in test suite
- Use fixtures for common setup

### Documentation
- Comment complex test scenarios
- Explain expected behavior
- Reference design documents

---

**Last Updated**: 2026-01-24
**Status**: Test infrastructure created, tests to be implemented
