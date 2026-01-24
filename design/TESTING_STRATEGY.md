# Testing Strategy
## Comprehensive Testing Approach for Lucene++

**Status**: Infrastructure Design
**Purpose**: Define testing strategy, test types, and quality assurance

---

## Overview

Lucene++ employs a multi-layered testing strategy to ensure correctness, performance, and reliability:
- **Unit tests**: Test individual components in isolation
- **Integration tests**: Test component interactions
- **Stress tests**: Test under high load and concurrency
- **Correctness tests**: Validate against Apache Lucene golden datasets
- **Performance benchmarks**: Track performance regressions

**Quality Goals**:
- 90%+ code coverage for core modules
- Zero crashes under stress testing
- 100% correctness on golden datasets
- No performance regressions >5%

---

## Test Categories

### 1. Unit Tests

**Purpose**: Test individual classes and functions in isolation.

**Framework**: Google Test (gtest)

**Location**: `tests/unit/`

**Structure**:
```
tests/unit/
├── index/
│   ├── IndexWriterTest.cpp
│   ├── IndexReaderTest.cpp
│   ├── SegmentReaderTest.cpp
│   └── DirectoryReaderTest.cpp
├── search/
│   ├── TermQueryTest.cpp
│   ├── BooleanQueryTest.cpp
│   ├── PhraseQueryTest.cpp
│   └── IndexSearcherTest.cpp
├── codecs/
│   ├── PostingsFormatTest.cpp
│   ├── DocValuesFormatTest.cpp
│   └── Lucene104CodecTest.cpp
├── store/
│   ├── DirectoryTest.cpp
│   ├── FSDirectoryTest.cpp
│   └── MMapDirectoryTest.cpp
└── util/
    ├── BitSetTest.cpp
    └── BytesRefTest.cpp
```

**Example Unit Test**:
```cpp
#include <gtest/gtest.h>
#include "lucenepp/index/IndexWriter.h"
#include "lucenepp/store/ByteBuffersDirectory.h"

TEST(IndexWriterTest, AddDocument) {
    // Setup
    auto dir = std::make_unique<ByteBuffersDirectory>();
    IndexWriterConfig config;
    IndexWriter writer(*dir, config);

    // Create document
    Document doc;
    doc.add(Field("title", "lucene", Field::Store::YES));
    doc.add(Field("body", "search engine", Field::Store::YES));

    // Execute
    writer.addDocument(doc);
    writer.commit();

    // Verify
    auto reader = DirectoryReader::open(*dir);
    ASSERT_EQ(1, reader->numDocs());

    Document retrievedDoc = reader->document(0);
    EXPECT_EQ("lucene", retrievedDoc.get("title"));
    EXPECT_EQ("search engine", retrievedDoc.get("body"));
}

TEST(IndexWriterTest, DeleteDocuments) {
    auto dir = std::make_unique<ByteBuffersDirectory>();
    IndexWriterConfig config;
    IndexWriter writer(*dir, config);

    // Add documents
    for (int i = 0; i < 10; ++i) {
        Document doc;
        doc.add(Field("id", std::to_string(i)));
        writer.addDocument(doc);
    }
    writer.commit();

    // Delete documents
    writer.deleteDocuments(Term("id", "5"));
    writer.commit();

    // Verify
    auto reader = DirectoryReader::open(*dir);
    ASSERT_EQ(9, reader->numDocs());  // 10 - 1 deleted
}

TEST(IndexWriterTest, CrashRecovery) {
    auto dir = std::make_unique<FSDirectory>("/tmp/test_index");

    // Write some documents
    {
        IndexWriterConfig config;
        IndexWriter writer(*dir, config);
        for (int i = 0; i < 100; ++i) {
            Document doc;
            doc.add(Field("id", std::to_string(i)));
            writer.addDocument(doc);
        }
        writer.commit();
        // writer goes out of scope without explicit close (simulates crash)
    }

    // Reopen and verify
    {
        auto reader = DirectoryReader::open(*dir);
        ASSERT_EQ(100, reader->numDocs());
    }
}
```

**Coverage Target**: 90% for core modules (index, search, codecs, store)

---

### 2. Integration Tests

**Purpose**: Test component interactions and end-to-end workflows.

**Location**: `tests/integration/`

**Structure**:
```
tests/integration/
├── EndToEndTest.cpp           # Full indexing → searching workflow
├── ConcurrencyTest.cpp        # Multi-threaded indexing/searching
├── CrashRecoveryTest.cpp      # Crash scenarios
├── MergeTest.cpp              # Merge policy and scheduler
└── QueryExecutionTest.cpp     # Complex query scenarios
```

**Example Integration Test**:
```cpp
TEST(EndToEnd, IndexAndSearchWorkflow) {
    auto dir = std::make_unique<FSDirectory>("/tmp/integration_test");

    // Phase 1: Index documents
    {
        IndexWriterConfig config;
        config.setRAMBufferSizeMB(16);
        IndexWriter writer(*dir, config);

        // Index 10,000 documents
        for (int i = 0; i < 10000; ++i) {
            Document doc;
            doc.add(Field("id", std::to_string(i)));
            doc.add(Field("text", "lucene search engine " + std::to_string(i)));
            writer.addDocument(doc);
        }

        writer.commit();
        writer.close();
    }

    // Phase 2: Search documents
    {
        auto reader = DirectoryReader::open(*dir);
        IndexSearcher searcher(*reader);

        // Term query
        TermQuery query("text", "lucene");
        TopDocs results = searcher.search(query, 10);
        ASSERT_EQ(10000, results.totalHits);

        // Boolean query
        BooleanQuery::Builder builder;
        builder.add(TermQuery("text", "lucene"), BooleanClause::MUST);
        builder.add(TermQuery("text", "search"), BooleanClause::MUST);
        TopDocs results2 = searcher.search(*builder.build(), 10);
        ASSERT_EQ(10000, results2.totalHits);
    }
}

TEST(Concurrency, ParallelIndexing) {
    auto dir = std::make_unique<FSDirectory>("/tmp/concurrency_test");
    IndexWriterConfig config;
    IndexWriter writer(*dir, config);

    // Spawn 10 threads, each indexing 1000 documents
    std::vector<std::thread> threads;
    std::atomic<int> errors{0};

    for (int t = 0; t < 10; ++t) {
        threads.emplace_back([&, t]() {
            try {
                for (int i = 0; i < 1000; ++i) {
                    Document doc;
                    doc.add(Field("id", std::to_string(t * 1000 + i)));
                    writer.addDocument(doc);
                }
            } catch (const std::exception& e) {
                errors++;
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    writer.commit();
    writer.close();

    // Verify
    ASSERT_EQ(0, errors.load());

    auto reader = DirectoryReader::open(*dir);
    ASSERT_EQ(10000, reader->numDocs());
}
```

---

### 3. Stress Tests

**Purpose**: Test system under extreme conditions.

**Location**: `tests/stress/`

**Scenarios**:

#### Stress Test 1: High Concurrency

```cpp
TEST(Stress, HighConcurrencyReadWrite) {
    auto dir = std::make_unique<FSDirectory>("/tmp/stress_test");
    IndexWriterConfig config;
    IndexWriter writer(*dir, config);

    // 50 writer threads + 50 reader threads
    std::atomic<bool> stop{false};
    std::vector<std::thread> threads;

    // Writer threads
    for (int i = 0; i < 50; ++i) {
        threads.emplace_back([&]() {
            while (!stop) {
                Document doc;
                doc.add(Field("id", generateRandomID()));
                writer.addDocument(doc);
            }
        });
    }

    // Reader threads
    for (int i = 0; i < 50; ++i) {
        threads.emplace_back([&]() {
            while (!stop) {
                auto reader = DirectoryReader::open(*dir);
                IndexSearcher searcher(*reader);
                TermQuery query("id", generateRandomID());
                searcher.search(query, 10);
            }
        });
    }

    // Run for 60 seconds
    std::this_thread::sleep_for(std::chrono::seconds(60));
    stop = true;

    for (auto& thread : threads) {
        thread.join();
    }

    // Verify no crashes
    writer.commit();
    auto reader = DirectoryReader::open(*dir);
    ASSERT_GT(reader->numDocs(), 0);
}
```

#### Stress Test 2: Memory Pressure

```cpp
TEST(Stress, MemoryPressure) {
    auto dir = std::make_unique<FSDirectory>("/tmp/memory_stress");
    IndexWriterConfig config;
    config.setRAMBufferSizeMB(8);  // Small buffer to trigger frequent flushes
    IndexWriter writer(*dir, config);

    // Index large documents until OOM or completion
    size_t docsIndexed = 0;
    try {
        for (int i = 0; i < 1000000; ++i) {
            Document doc;
            // 1MB document
            std::string largeText(1024 * 1024, 'x');
            doc.add(Field("text", largeText));
            writer.addDocument(doc);
            docsIndexed++;

            if (i % 100 == 0) {
                writer.commit();  // Periodic commits to free memory
            }
        }
    } catch (const MemoryLimitExceededException& e) {
        // Expected under memory pressure
    }

    writer.commit();
    writer.close();

    // Verify index integrity
    auto reader = DirectoryReader::open(*dir);
    ASSERT_GT(reader->numDocs(), 0);
    ASSERT_LE(reader->numDocs(), docsIndexed);
}
```

#### Stress Test 3: Crash Scenarios

```cpp
TEST(Stress, RandomCrashes) {
    for (int iteration = 0; iteration < 100; ++iteration) {
        auto dir = std::make_unique<FSDirectory>("/tmp/crash_test_" + std::to_string(iteration));
        IndexWriterConfig config;
        IndexWriter writer(*dir, config);

        // Index random number of documents
        int numDocs = rand() % 1000 + 1;
        for (int i = 0; i < numDocs; ++i) {
            Document doc;
            doc.add(Field("id", std::to_string(i)));
            writer.addDocument(doc);
        }

        // Random crash (50% chance to commit first)
        if (rand() % 2 == 0) {
            writer.commit();
        }
        // Simulate crash by NOT calling close()

        // Reopen and verify
        auto reader = DirectoryReader::open(*dir);
        // After crash, should see committed docs only
        ASSERT_LE(reader->numDocs(), numDocs);
    }
}
```

**Duration**: Stress tests run for 1-10 minutes per scenario.

---

### 4. Correctness Validation

**Purpose**: Validate against Apache Lucene golden datasets.

**Location**: `tests/correctness/`

**Approach**:
1. Index same dataset with Apache Lucene and Lucene++
2. Run identical queries on both
3. Compare results (document IDs, scores, order)

**Golden Dataset**: Wikipedia articles (1GB subset)

**Example**:
```cpp
TEST(Correctness, LuceneCompatibility) {
    // Load golden dataset created by Apache Lucene
    auto luceneIndex = loadGoldenDataset("testdata/lucene_golden");
    auto luceneppIndex = loadGoldenDataset("testdata/lucenepp_indexed");

    // Run test queries
    std::vector<std::string> testQueries = {
        "search engine",
        "information retrieval",
        "database system",
        // ... 1000 test queries
    };

    int matches = 0;
    int total = 0;

    for (const auto& queryStr : testQueries) {
        // Query Lucene index
        auto luceneResults = queryLuceneIndex(luceneIndex, queryStr);

        // Query Lucene++ index
        auto luceneppResults = queryLucenePPIndex(luceneppIndex, queryStr);

        // Compare top 100 results
        total++;
        if (compareResults(luceneResults, luceneppResults, /*top=*/100)) {
            matches++;
        }
    }

    // Require 99%+ match rate
    double matchRate = static_cast<double>(matches) / total;
    ASSERT_GE(matchRate, 0.99);
}
```

**Tolerance**: Allow ±0.001 score differences due to floating-point precision.

---

### 5. Performance Benchmarks

**Purpose**: Track performance and detect regressions.

**Framework**: Google Benchmark

**Location**: `tests/benchmark/`

**Benchmarks**:

#### Benchmark 1: Indexing Throughput

```cpp
static void BM_IndexingThroughput(benchmark::State& state) {
    auto dir = std::make_unique<ByteBuffersDirectory>();
    IndexWriterConfig config;
    IndexWriter writer(*dir, config);

    Document doc;
    doc.add(Field("text", "the quick brown fox jumps over the lazy dog"));

    for (auto _ : state) {
        writer.addDocument(doc);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_IndexingThroughput);
```

#### Benchmark 2: Query Latency

```cpp
static void BM_TermQueryLatency(benchmark::State& state) {
    // Pre-index documents
    auto dir = createPreIndexedData(/*numDocs=*/100000);
    auto reader = DirectoryReader::open(*dir);
    IndexSearcher searcher(*reader);

    TermQuery query("text", "search");

    for (auto _ : state) {
        TopDocs results = searcher.search(query, 10);
        benchmark::DoNotOptimize(results);
    }
}
BENCHMARK(BM_TermQueryLatency);
```

#### Benchmark 3: SIMD Performance

```cpp
static void BM_SIMDScoring(benchmark::State& state) {
    // Benchmark SIMD vs scalar scoring
    std::vector<float> scores(1024);
    std::vector<int> docIDs(1024);

    for (auto _ : state) {
        computeBM25Scores_SIMD(docIDs.data(), scores.data(), 1024);
        benchmark::DoNotOptimize(scores);
    }

    state.SetItemsProcessed(state.iterations() * 1024);
}
BENCHMARK(BM_SIMDScoring);
```

**Metrics Tracked**:
- Indexing throughput (docs/sec)
- Query latency (p50, p95, p99)
- Merge throughput (MB/sec)
- Memory usage (peak RSS)

**Regression Threshold**: Alert if performance degrades >5%

---

## Test Execution

### Local Development

```bash
# Build with tests
cmake -B build -S . -DLUCENEPP_BUILD_TESTS=ON
cmake --build build

# Run all tests
cd build && ctest --output-on-failure

# Run specific test suite
./lucenepp_unit_tests --gtest_filter="IndexWriter*"

# Run with verbose output
./lucenepp_unit_tests --gtest_verbose

# Run benchmarks
./lucenepp_benchmarks --benchmark_filter="BM_Indexing*"
```

### Continuous Integration

**GitHub Actions** (see BUILD_SYSTEM.md):
- Run on every PR and push to main
- Linux (GCC, Clang), macOS, Windows
- Debug and Release builds
- Code coverage report (Codecov)

**Test Matrix**:
| OS | Compiler | Build Type | Tests |
|----|----------|------------|-------|
| Ubuntu 22.04 | GCC 11 | Debug | Unit + Integration |
| Ubuntu 22.04 | GCC 11 | Release | Unit + Integration + Stress |
| Ubuntu 22.04 | Clang 14 | Debug | Unit + Integration |
| macOS 13 | AppleClang | Release | Unit + Integration |
| Windows 2022 | MSVC 2022 | Release | Unit |

---

## Code Coverage

**Tool**: gcov + lcov (Linux), llvm-cov (macOS/Clang)

**Generate Coverage Report**:
```bash
# Configure with coverage flags
cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="--coverage" \
    -DLUCENEPP_BUILD_TESTS=ON

# Build and run tests
cmake --build build
cd build && ctest

# Generate coverage report
lcov --capture --directory . --output-file coverage.info
lcov --remove coverage.info '/usr/*' '*/tests/*' --output-file coverage.info
genhtml coverage.info --output-directory coverage_html
```

**Upload to Codecov**:
```yaml
- name: Upload coverage
  uses: codecov/codecov-action@v3
  with:
    files: ./build/coverage.info
```

**Coverage Targets**:
- Core modules: >90%
- Utilities: >80%
- Overall: >85%

---

## Fuzzing

**Purpose**: Discover edge cases and crashes.

**Tool**: libFuzzer (Clang)

**Fuzz Targets**:

```cpp
// Fuzz IndexWriter::addDocument
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 10) return 0;

    auto dir = std::make_unique<ByteBuffersDirectory>();
    IndexWriterConfig config;
    IndexWriter writer(*dir, config);

    try {
        // Parse fuzzer input as document fields
        Document doc = parseFuzzInput(data, size);
        writer.addDocument(doc);
        writer.commit();
    } catch (const std::exception& e) {
        // Catch expected exceptions, crash on unexpected ones
    }

    return 0;
}
```

**Build Fuzz Target**:
```bash
clang++ -fsanitize=fuzzer,address -g \
    fuzz_indexwriter.cpp -o fuzz_indexwriter \
    -llucenepp_core

# Run fuzzer
./fuzz_indexwriter -max_total_time=3600  # 1 hour
```

---

## Performance Regression Testing

**Tool**: Continuous Benchmarking

**Process**:
1. Run benchmarks on every commit
2. Compare against baseline (main branch)
3. Alert if >5% regression

**Benchmark Storage**: Store results in time-series database (InfluxDB)

**Visualization**: Grafana dashboard showing performance trends

**Example Alert**:
```
⚠️ Performance Regression Detected

Benchmark: BM_TermQueryLatency
Baseline:  125 μs/op
Current:   145 μs/op
Change:    +16% (exceeds 5% threshold)

Commit: abc1234
```

---

## Test Data

### Unit Test Data

**Location**: `tests/testdata/`

**Contents**:
- Small indexes (10-100 docs)
- Edge case documents (empty, very large, unicode)
- Malformed data for error handling tests

### Integration Test Data

**Dataset**: Wikipedia articles (1GB subset)

**Download**:
```bash
wget https://dumps.wikimedia.org/enwiki/latest/enwiki-latest-pages-articles1.xml.bz2
python scripts/extract_test_data.py --input enwiki.xml.bz2 --output testdata/
```

### Stress Test Data

**Generated dynamically** during test execution to avoid large checked-in files.

---

## Test Maintenance

**Guidelines**:
- Update tests when changing APIs
- Add regression tests for every bug fix
- Keep tests fast (<1 second per unit test)
- Avoid flaky tests (use deterministic seeds)
- Document complex test scenarios

**Code Review**:
- Every PR must include tests
- Code coverage should not decrease
- New features require integration tests

---

## Summary

**Test Pyramid**:
```
         /\
        /  \  Unit Tests (1000+ tests, <5 min)
       /____\
      /      \  Integration Tests (100+ tests, ~15 min)
     /________\
    /          \  Stress Tests (10+ scenarios, ~1 hour)
   /____________\
  /              \  Correctness Tests (golden datasets, ~30 min)
 /________________\
```

**Quality Metrics**:
- **Code coverage**: >90% for core modules
- **Test count**: 1000+ unit tests, 100+ integration tests
- **Execution time**: <20 minutes for full test suite (excluding stress tests)
- **Correctness**: 99%+ match with Apache Lucene golden datasets
- **Zero crashes**: Under stress testing

**Continuous Improvement**:
- Add tests for every bug report
- Increase coverage for under-tested modules
- Expand golden dataset with more query types
- Profile and optimize slow tests

---

**Design Status**: Complete ✅
**Next Document**: OBSERVABILITY.md
