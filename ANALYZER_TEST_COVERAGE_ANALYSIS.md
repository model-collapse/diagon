# Analyzer/Metrics Test Coverage Analysis

**Date**: 2026-01-27
**Analysis**: Comparison of QBlock's IndexAnalyzer vs Diagon's QueryStats

---

## Executive Summary

**Finding**: Neither QBlock's `IndexAnalyzer` nor Diagon's `QueryStats` have dedicated unit tests.

**Status**:
- ✅ **Diagon's approach is simpler and more testable** (struct vs singleton)
- ⚠️ **QBlock's IndexAnalyzer has NO unit tests**
- ❌ **Diagon's QueryStats has NO unit tests**

**Recommendation**: Add unit tests for Diagon's QueryStats to ensure correctness of timing and metrics collection.

---

## QBlock's IndexAnalyzer

### Location
- **Header**: `/home/ubuntu/cpp-sparse-ann/cpp/src/IndexAnalyzer.h`
- **Implementation**: `/home/ubuntu/cpp-sparse-ann/cpp/src/IndexAnalyzer.cpp`
- **Tests**: ❌ **None found**

### Design Pattern

**Singleton** with global state:
```cpp
class IndexAnalyzer {
public:
    static IndexAnalyzer* GetInstance() {
        static IndexAnalyzer* instance = new IndexAnalyzer();
        return instance;
    }

private:
    std::atomic<uint64_t> cluster_examined;
    std::atomic<uint64_t> dp;
    std::atomic<uint64_t> counter;
    std::atomic<uint64_t> bitq_touched_docs;
    std::atomic<uint64_t> bitq_score_ops;
    std::atomic<uint64_t> bitq_selected_blocks;
};
```

### Functionality

1. **AnalyzeInvertedIndex()**: Prints index statistics (terms, avg docs per term)
2. **AnalyzeClusters()**: Prints cluster statistics
3. **RecordQuery()**: Records query metrics (clusters examined, dot products)
4. **RecordBitQMetrics()**: Records BitQ-specific metrics
5. **AnalyzeBitQQueryWithWLatency()**: Prints detailed timing breakdown

### Issues

❌ **Not testable**:
- Global singleton state
- Side effects (std::cout)
- No way to inject dependencies
- Atomic variables make assertions difficult

❌ **No unit tests**:
```bash
$ grep -r "IndexAnalyzer" /home/ubuntu/cpp-sparse-ann/cpp/test/
# No results
```

❌ **Design problems**:
- Violates Single Responsibility Principle (collects AND prints)
- Hard-coded debug flag (`ANALYSIS_DEBUG`)
- Memory leak (singleton never deleted)

---

## Diagon's QueryStats

### Location
- **Header**: `/home/ubuntu/diagon/src/core/include/diagon/index/BlockMaxQuantizedIndex.h` (lines 40-50)
- **Usage**: Passed as pointer to `query()` function
- **Tests**: ❌ **None found**

### Design Pattern

**Plain struct** (value semantics):
```cpp
struct QueryStats {
    size_t total_blocks = 0;
    size_t selected_blocks = 0;
    size_t score_operations = 0;
    double block_selection_ms = 0.0;
    double scatter_add_ms = 0.0;
    double scatter_add_part1_ms = 0.0;  // Score accumulation phase
    double scatter_add_part2_ms = 0.0;  // TopK processing phase
    double reranking_ms = 0.0;
    double total_ms = 0.0;
};
```

### Functionality

**Passive data collection** - no logic, just storage:
```cpp
// Usage in query()
QueryStats stats;
auto result = index.query(query, params, &stats);

// Access metrics
std::cout << "QPS: " << (1000.0 / stats.total_ms) << std::endl;
std::cout << "Blocks selected: " << stats.selected_blocks << std::endl;
```

### Advantages

✅ **Testable**:
- No global state
- No side effects
- Easy to construct and inspect
- Can be passed to functions

✅ **Separation of concerns**:
- Data collection separate from presentation
- Benchmark code handles printing
- No hard-coded debug flags

✅ **Thread-safe**:
- Each query gets its own instance
- No shared state

### Current Test Status

❌ **No unit tests**:
```bash
$ find /home/ubuntu/diagon/tests -name "*BlockMax*" -o -name "*QueryStats*"
# No results
```

---

## Comparison

| Aspect | QBlock IndexAnalyzer | Diagon QueryStats | Winner |
|--------|---------------------|-------------------|--------|
| **Design** | Singleton | Struct | ✅ Diagon |
| **Testability** | Hard (global state) | Easy (value type) | ✅ Diagon |
| **Thread Safety** | Atomic counters | Per-query instance | ✅ Diagon |
| **Separation of Concerns** | Mixed (collect + print) | Clean (data only) | ✅ Diagon |
| **Memory Management** | Leaks | Stack-allocated | ✅ Diagon |
| **Unit Tests** | ❌ None | ❌ None | ⚠️ Neither |
| **Functionality** | Rich (analysis + printing) | Focused (metrics only) | Tie |

---

## Test Coverage Gap Analysis

### What Should Be Tested

#### Diagon's QueryStats (High Priority)

1. **Correctness of timing measurements**
   - Part 1 + Part 2 should sum close to scatter_add_ms
   - block_selection_ms + scatter_add_ms + reranking_ms should sum close to total_ms
   - All timing values should be non-negative

2. **Counter accuracy**
   - total_blocks should be > 0 for non-empty queries
   - selected_blocks should be <= total_blocks
   - score_operations should match actual operations

3. **Edge cases**
   - Empty query (should have 0 blocks, 0 operations)
   - Single-term query
   - Multi-term query with different alpha values

#### QBlock's IndexAnalyzer (Medium Priority)

1. **Metric accumulation**
   - RecordBitQMetrics() accumulates correctly
   - Reset() clears all counters
   - Thread-safe accumulation

2. **Analysis functions**
   - AnalyzeInvertedIndex() handles empty index
   - AnalyzeClusters() handles empty clusters
   - No crashes on edge cases

**Problem**: Hard to test due to singleton + side effects. Would need refactoring first.

---

## Recommendations

### Immediate Actions

#### 1. Add Unit Tests for Diagon's QueryStats ✅ HIGH PRIORITY

Create `/home/ubuntu/diagon/tests/unit/index/QueryStatsTest.cpp`:

```cpp
#include <gtest/gtest.h>
#include "diagon/index/BlockMaxQuantizedIndex.h"

using namespace diagon::index;

TEST(QueryStatsTest, DefaultConstruction) {
    QueryStats stats;

    EXPECT_EQ(stats.total_blocks, 0);
    EXPECT_EQ(stats.selected_blocks, 0);
    EXPECT_EQ(stats.score_operations, 0);
    EXPECT_EQ(stats.block_selection_ms, 0.0);
    EXPECT_EQ(stats.scatter_add_ms, 0.0);
    EXPECT_EQ(stats.scatter_add_part1_ms, 0.0);
    EXPECT_EQ(stats.scatter_add_part2_ms, 0.0);
    EXPECT_EQ(stats.reranking_ms, 0.0);
    EXPECT_EQ(stats.total_ms, 0.0);
}

TEST(QueryStatsTest, TimingConsistency) {
    QueryStats stats;

    // Simulate timing measurements
    stats.block_selection_ms = 0.05;
    stats.scatter_add_part1_ms = 0.20;
    stats.scatter_add_part2_ms = 0.15;
    stats.scatter_add_ms = stats.scatter_add_part1_ms + stats.scatter_add_part2_ms;
    stats.reranking_ms = 0.30;
    stats.total_ms = stats.block_selection_ms + stats.scatter_add_ms + stats.reranking_ms;

    // Verify consistency
    EXPECT_NEAR(stats.scatter_add_ms, 0.35, 0.001);
    EXPECT_NEAR(stats.total_ms, 0.70, 0.001);

    // Verify phase breakdown
    double phase_sum = stats.scatter_add_part1_ms + stats.scatter_add_part2_ms;
    EXPECT_NEAR(phase_sum, stats.scatter_add_ms, 0.001);
}

TEST(QueryStatsTest, CounterConstraints) {
    QueryStats stats;

    stats.total_blocks = 100;
    stats.selected_blocks = 25;
    stats.score_operations = 50000;

    // Invariants
    EXPECT_LE(stats.selected_blocks, stats.total_blocks);
    EXPECT_GT(stats.score_operations, 0);
}

TEST(QueryStatsTest, NonNegativeTiming) {
    QueryStats stats;

    // All timing values should be non-negative
    stats.block_selection_ms = -1.0;  // Invalid!

    // In production code, we should validate this
    // For now, just document the invariant
    EXPECT_GE(stats.block_selection_ms, 0.0) << "Timing must be non-negative";
}
```

#### 2. Add Integration Tests for BlockMaxQuantizedIndex ✅ HIGH PRIORITY

Create `/home/ubuntu/diagon/tests/unit/index/BlockMaxQuantizedIndexTest.cpp`:

```cpp
#include <gtest/gtest.h>
#include "diagon/index/BlockMaxQuantizedIndex.h"

using namespace diagon::index;

class BlockMaxQuantizedIndexTest : public ::testing::Test {
protected:
    void SetUp() override {
        config.num_quantization_bins = 256;
        config.window_size = 500000;
        config.window_group_size = 15;
    }

    BlockMaxQuantizedIndex::Config config;
};

TEST_F(BlockMaxQuantizedIndexTest, EmptyQueryReturnsEmptyResults) {
    BlockMaxQuantizedIndex index(config);

    // Build with some documents
    std::vector<SparseDoc> docs(100);
    for (size_t i = 0; i < docs.size(); ++i) {
        docs[i].push_back({0, 1.0f});  // All have term 0
    }
    index.build(docs);

    // Query with empty doc
    SparseDoc empty_query;
    BlockMaxQuantizedIndex::QueryParams params;
    QueryStats stats;

    auto results = index.query(empty_query, params, &stats);

    EXPECT_TRUE(results.empty());
    EXPECT_EQ(stats.total_blocks, 0);
    EXPECT_EQ(stats.selected_blocks, 0);
    EXPECT_EQ(stats.score_operations, 0);
}

TEST_F(BlockMaxQuantizedIndexTest, QueryStatsArePopulated) {
    BlockMaxQuantizedIndex index(config);

    // Build with documents
    std::vector<SparseDoc> docs(1000);
    for (size_t i = 0; i < docs.size(); ++i) {
        docs[i].push_back({i % 10, 1.0f});
    }
    index.build(docs);

    // Query
    SparseDoc query{{0, 1.0f}, {1, 1.0f}};
    BlockMaxQuantizedIndex::QueryParams params;
    QueryStats stats;

    auto results = index.query(query, params, &stats);

    // Verify stats are populated
    EXPECT_GT(stats.total_blocks, 0);
    EXPECT_GT(stats.selected_blocks, 0);
    EXPECT_LE(stats.selected_blocks, stats.total_blocks);
    EXPECT_GT(stats.score_operations, 0);

    // Verify timing is reasonable
    EXPECT_GT(stats.total_ms, 0.0);
    EXPECT_GT(stats.block_selection_ms, 0.0);
    EXPECT_GT(stats.scatter_add_ms, 0.0);
    EXPECT_GT(stats.reranking_ms, 0.0);

    // Verify timing consistency (with tolerance for measurement overhead)
    double phase_sum = stats.block_selection_ms + stats.scatter_add_ms + stats.reranking_ms;
    EXPECT_NEAR(phase_sum, stats.total_ms, stats.total_ms * 0.1)
        << "Phases should sum close to total (within 10%)";
}

TEST_F(BlockMaxQuantizedIndexTest, AlphaAffectsBlockSelection) {
    BlockMaxQuantizedIndex index(config);

    // Build with documents
    std::vector<SparseDoc> docs(1000);
    for (size_t i = 0; i < docs.size(); ++i) {
        docs[i].push_back({i % 10, 1.0f});
    }
    index.build(docs);

    // Query with different alpha values
    SparseDoc query{{0, 1.0f}, {1, 1.0f}, {2, 1.0f}};

    BlockMaxQuantizedIndex::QueryParams params_low;
    params_low.alpha = 0.3f;
    QueryStats stats_low;
    auto results_low = index.query(query, params_low, &stats_low);

    BlockMaxQuantizedIndex::QueryParams params_high;
    params_high.alpha = 0.7f;
    QueryStats stats_high;
    auto results_high = index.query(query, params_high, &stats_high);

    // Higher alpha should select more blocks
    EXPECT_GT(stats_high.selected_blocks, stats_low.selected_blocks);

    // Higher alpha should have higher recall (more results or same)
    EXPECT_GE(results_high.size(), results_low.size());
}
```

#### 3. Consider Refactoring QBlock's IndexAnalyzer ⚠️ LOW PRIORITY

If QBlock development continues, consider:

1. **Extract interface**: Separate data collection from printing
2. **Dependency injection**: Make cout injectable for testing
3. **Remove singleton**: Use instance passed to functions
4. **Add unit tests**: After refactoring

**Example refactored design**:
```cpp
// Testable design
class MetricsCollector {
public:
    void RecordBitQMetrics(long touched_docs, long score_ops, long blocks) {
        touched_docs_ += touched_docs;
        score_ops_ += score_ops;
        selected_blocks_ += blocks;
    }

    long GetTouchedDocs() const { return touched_docs_; }
    long GetScoreOps() const { return score_ops_; }
    long GetSelectedBlocks() const { return selected_blocks_; }

    void Reset() {
        touched_docs_ = 0;
        score_ops_ = 0;
        selected_blocks_ = 0;
    }

private:
    std::atomic<long> touched_docs_{0};
    std::atomic<long> score_ops_{0};
    std::atomic<long> selected_blocks_{0};
};

// Separate printer (dependency injection)
class MetricsPrinter {
public:
    explicit MetricsPrinter(std::ostream& out) : out_(out) {}

    void Print(const MetricsCollector& metrics, int num_queries) {
        out_ << "Average touched docs: "
             << (double)metrics.GetTouchedDocs() / num_queries << "\n";
        // ...
    }

private:
    std::ostream& out_;
};
```

---

## Test Infrastructure

### Diagon's Test Setup

Diagon uses **Google Test** framework:

```cmake
# tests/CMakeLists.txt (excerpt)
find_package(GTest REQUIRED)

add_executable(BlockMaxQuantizedIndexTest
    unit/index/BlockMaxQuantizedIndexTest.cpp
)

target_link_libraries(BlockMaxQuantizedIndexTest
    PRIVATE
        diagon_core
        GTest::gtest
        GTest::gtest_main
)

gtest_discover_tests(BlockMaxQuantizedIndexTest)
```

### Running Tests

```bash
cd /home/ubuntu/diagon/build
ctest --output-on-failure
```

Or run specific test:
```bash
./tests/unit/index/BlockMaxQuantizedIndexTest
```

---

## Test Coverage Goals

### Target Coverage

| Component | Current | Target | Priority |
|-----------|---------|--------|----------|
| **QueryStats** | 0% | 90%+ | ✅ HIGH |
| **BlockMaxQuantizedIndex** | 0% | 70%+ | ✅ HIGH |
| **query() method** | 0% | 80%+ | ✅ HIGH |
| **scatterAdd()** | 0% | 60%+ | MEDIUM |
| **rerank()** | 0% | 60%+ | MEDIUM |

### Metrics to Track

1. **Line coverage**: Percentage of lines executed
2. **Branch coverage**: Percentage of branches taken
3. **Function coverage**: Percentage of functions called
4. **Integration coverage**: End-to-end scenarios

---

## Summary

### Current Status

❌ **QBlock's IndexAnalyzer**:
- No unit tests
- Hard to test (singleton, side effects)
- Needs refactoring before testing

✅ **Diagon's QueryStats**:
- Well-designed (simple struct)
- Easy to test
- **BUT: No tests yet!**

### Action Items

1. ✅ **HIGH**: Add QueryStats unit tests
2. ✅ **HIGH**: Add BlockMaxQuantizedIndex integration tests
3. ⚠️ **MEDIUM**: Add tests for edge cases (empty query, single term, etc.)
4. ⚠️ **LOW**: Consider refactoring QBlock's IndexAnalyzer (if needed)

### Expected Outcome

After implementing recommended tests:
- **QueryStats**: 90%+ coverage
- **BlockMaxQuantizedIndex**: 70%+ coverage
- **Confidence**: High (tested metrics collection)
- **Regression prevention**: Tests catch timing calculation bugs

---

## Conclusion

**Diagon's design is superior** to QBlock's for testability. The simple struct approach makes it easy to add comprehensive unit tests.

**Recommendation**: Prioritize adding tests for Diagon's QueryStats and BlockMaxQuantizedIndex before adding more features. This ensures the optimization work we've done is protected by tests.

The test files can be created at:
- `/home/ubuntu/diagon/tests/unit/index/QueryStatsTest.cpp`
- `/home/ubuntu/diagon/tests/unit/index/BlockMaxQuantizedIndexTest.cpp`

Both QBlock and Diagon lack test coverage for their metrics/analyzer components, but Diagon's design makes it much easier to add tests now.

---

**Prepared By**: Claude Sonnet 4.5
**Date**: 2026-01-27
**Status**: Analysis complete, test recommendations provided
