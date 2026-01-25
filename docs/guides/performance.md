# Performance Tuning Guide

This guide covers performance optimization techniques for Diagon search engine.

## Table of Contents

- [Quick Wins](#quick-wins)
- [Indexing Performance](#indexing-performance)
- [Query Performance](#query-performance)
- [Memory Management](#memory-management)
- [Storage Optimization](#storage-optimization)
- [SIMD Acceleration](#simd-acceleration)
- [Monitoring and Profiling](#monitoring-and-profiling)

---

## Quick Wins

These changes provide immediate performance improvements with minimal effort.

### 1. Enable Compiler Optimizations

```bash
# Build with optimizations
cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-O3 -march=native -DNDEBUG"

cmake --build build -j$(nproc)
```

**Impact**: 2-3× faster queries, 2× faster indexing

### 2. Enable SIMD Acceleration

```bash
cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DDIAGON_ENABLE_SIMD=ON \
    -DCMAKE_CXX_FLAGS="-march=native"
```

**Impact**: 4-8× faster BM25 scoring on AVX2 CPUs

### 3. Increase RAM Buffer Size

```cpp
index::IndexWriterConfig config;
config.setRAMBufferSizeMB(1024);  // 1GB instead of default 16MB
```

**Impact**: 10-20× faster indexing, fewer segment flushes

### 4. Use MMap Directory

```cpp
// Instead of FSDirectory
auto dir = store::MMapDirectory::open("/path/to/index");
```

**Impact**: 30-50% faster queries on large indexes

### 5. Batch Document Inserts

```cpp
// Instead of adding documents one by one
std::vector<index::Document> batch;
batch.reserve(10000);

for (/* ... */) {
    batch.push_back(createDocument());
}

writer->addDocuments(batch);  // Batch insert
```

**Impact**: 5-10× faster indexing

---

## Indexing Performance

### RAM Buffer Configuration

The RAM buffer controls how much memory IndexWriter uses before flushing to disk.

```cpp
index::IndexWriterConfig config;

// Small dataset (<1M docs)
config.setRAMBufferSizeMB(256);

// Medium dataset (1-10M docs)
config.setRAMBufferSizeMB(512);

// Large dataset (>10M docs)
config.setRAMBufferSizeMB(2048);

// Or limit by document count
config.setMaxBufferedDocs(50000);
```

**Trade-offs**:
- ✅ Larger buffer = faster indexing, fewer segments
- ❌ Larger buffer = more memory, longer flush times

### Thread Configuration

```cpp
// Use all available cores
config.setMaxThreadStates(std::thread::hardware_concurrency());

// Or set explicitly
config.setMaxThreadStates(8);  // 8 concurrent indexing threads
```

**Best practice**: Set to number of physical cores (not hyperthreads).

### Merge Policy

```cpp
// Tiered merge (default, balanced)
config.setMergePolicy(MergePolicy::TIERED);

// Log-byte-size merge (predictable I/O)
config.setMergePolicy(MergePolicy::LOG_BYTE_SIZE);

// No-merge (fastest indexing, slower queries)
config.setMergePolicy(MergePolicy::NO_MERGE);
```

**Tiered merge parameters**:

```cpp
auto tiered = std::make_unique<TieredMergePolicy>();
tiered->setMaxMergedSegmentMB(5000);     // Max segment size
tiered->setSegmentsPerTier(10);          // Merge factor
tiered->setMaxMergeAtOnce(10);           // Concurrent merges
config.setMergePolicy(std::move(tiered));
```

### Compression Selection

```cpp
// Fast indexing - use LZ4
config.setCompressionCodec(CompressionCodecs::lz4());

// Balanced - use ZSTD level 1-3
config.setCompressionCodec(CompressionCodecs::zstd(3));

// Space-constrained - use ZSTD level 10+
config.setCompressionCodec(CompressionCodecs::zstd(10));
```

### Compound File Format

```cpp
// Disable compound files for faster indexing
config.setUseCompoundFile(false);

// Enable for fewer file handles (default)
config.setUseCompoundFile(true);
```

**Trade-offs**:
- ✅ Disabled = faster indexing, faster merging
- ❌ Disabled = more file handles, slower open

### Indexing Benchmark

```cpp
void benchmarkIndexing() {
    auto dir = store::MMapDirectory::open("/tmp/bench_index");

    index::IndexWriterConfig config;
    config.setRAMBufferSizeMB(1024);
    config.setMaxThreadStates(8);
    config.setUseCompoundFile(false);

    auto writer = index::IndexWriter::create(dir.get(), config);

    const int NUM_DOCS = 1000000;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < NUM_DOCS; i++) {
        index::Document doc;
        doc.addField("id", i, index::FieldType::NUMERIC);
        doc.addField("text", generateText(), index::FieldType::TEXT);
        doc.addField("price", randomPrice(), index::FieldType::NUMERIC);
        writer->addDocument(doc);

        if (i % 10000 == 0) {
            std::cout << "Indexed " << i << " docs\n";
        }
    }

    writer->commit();
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        end - start);
    double throughput = NUM_DOCS / static_cast<double>(duration.count());

    std::cout << "Indexing throughput: " << throughput << " docs/sec\n";
}
```

**Target**: >10,000 docs/sec on modern hardware

---

## Query Performance

### Query Optimization Techniques

#### 1. Use Filters Instead of Boolean Queries

```cpp
// Slow: Boolean query
auto boolQuery = BooleanQuery::Builder()
    .add(TermQuery::create("text", "search"), BooleanClause::MUST)
    .add(RangeQuery::create("price", 10, 100), BooleanClause::MUST)
    .build();

// Fast: Query + Filter
auto query = TermQuery::create("text", "search");
auto filter = RangeFilter::create("price", 10, 100);
searcher.search(query.get(), filter.get(), 10);
```

**Why**: Filters can skip entire segments, queries cannot.

#### 2. Use Term Filters for High Selectivity

```cpp
// High selectivity (filters most docs)
auto filter = TermFilter::create("category", "rare_category");

// Low selectivity (keeps most docs) - don't filter
// Just use boolean query instead
```

#### 3. Reuse Query Objects

```cpp
// Create query once
auto query = TermQuery::create("title", "search");

// Reuse for multiple searches
for (int i = 0; i < 1000; i++) {
    searcher.search(query.get(), 10);
}
```

#### 4. Limit Result Set Size

```cpp
// Only get what you need
searcher.search(query.get(), 20);  // Not 10000

// For pagination, use searchAfter
auto results = searcher.search(query.get(), 20);
// ... show page 1 ...

// Get next page
auto nextResults = searcher.searchAfter(
    results.scoreDocs.back(), query.get(), 20);
```

### BM25 Parameter Tuning

```cpp
// Default BM25 (k1=1.2, b=0.75)
auto similarity = std::make_unique<BM25Similarity>();

// Tune for your data:

// Short documents (tweets, titles) - reduce b
auto short_sim = std::make_unique<BM25Similarity>(1.2f, 0.5f);

// Long documents (articles, books) - increase b
auto long_sim = std::make_unique<BM25Similarity>(1.2f, 0.85f);

// Repeated terms matter more - increase k1
auto freq_sim = std::make_unique<BM25Similarity>(2.0f, 0.75f);

// Repeated terms matter less - decrease k1
auto presence_sim = std::make_unique<BM25Similarity>(0.8f, 0.75f);

searcher.setSimilarity(std::move(similarity));
```

### Query Caching

```cpp
class QueryCache {
    std::unordered_map<std::string, TopDocs> cache_;
    std::mutex mutex_;

public:
    TopDocs search(const std::string& query_str,
                   search::IndexSearcher& searcher,
                   search::Query* query,
                   int topN) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = cache_.find(query_str);
        if (it != cache_.end()) {
            return it->second;  // Cache hit
        }

        auto results = searcher.search(query, topN);
        cache_[query_str] = results;
        return results;
    }
};
```

### Parallel Search

```cpp
// Create thread pool executor
auto executor = std::make_shared<ThreadPoolExecutor>(8);

// Create searcher with executor
search::IndexSearcher searcher(reader.get(), executor);

// Searches will run in parallel across segments
auto results = searcher.search(query.get(), 1000);
```

**Impact**: 2-4× faster on multi-segment indexes

---

## Memory Management

### Reader Pooling

Keep readers open to avoid reopening cost:

```cpp
class ReaderPool {
    std::shared_ptr<index::DirectoryReader> reader_;
    std::mutex mutex_;
    std::chrono::steady_clock::time_point last_refresh_;

public:
    std::shared_ptr<index::DirectoryReader> getReader(
        store::Directory* dir)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!reader_) {
            reader_ = index::DirectoryReader::open(dir);
            last_refresh_ = std::chrono::steady_clock::now();
            return reader_;
        }

        // Refresh every 60 seconds
        auto now = std::chrono::steady_clock::now();
        if (now - last_refresh_ > std::chrono::seconds(60)) {
            auto new_reader = index::DirectoryReader::openIfChanged(
                reader_.get());
            if (new_reader) {
                reader_ = std::move(new_reader);
                last_refresh_ = now;
            }
        }

        return reader_;
    }
};
```

### Memory-Mapped I/O

```cpp
// For large indexes, use MMapDirectory
auto dir = store::MMapDirectory::open("/path/to/index");

// Let OS manage memory
// Benefits:
// - No application memory used for index data
// - OS page cache handles recently used data
// - Better performance for large indexes
```

### Segment Warming

Warm up segments after opening:

```cpp
void warmSegments(index::DirectoryReader* reader) {
    for (const auto& leaf : reader->leaves()) {
        // Read terms dictionary
        auto terms = leaf.reader()->terms("text");

        // Read postings for common terms
        for (const auto& term : getCommonTerms()) {
            auto postings = leaf.reader()->postings(term);
            while (postings->nextDoc() != NO_MORE_DOCS) {
                // Trigger loading
            }
        }
    }
}
```

---

## Storage Optimization

### Segment Size Management

```cpp
// Target segment size (affects merge I/O)
auto tiered = std::make_unique<TieredMergePolicy>();
tiered->setMaxMergedSegmentMB(5000);  // 5GB max segment

// Too small: frequent merges, many segments
// Too large: expensive merges, long recovery
// Sweet spot: 1-5 GB per segment
```

### Force Merge for Read-Only Indexes

```cpp
// After bulk indexing, merge to single segment
writer->forceMerge(1);

// Benefits:
// - Faster queries (single segment)
// - Smaller index (merged deletes)
// - Better compression

// Warning: Expensive operation!
// Only do this for read-only indexes
```

### Storage Tier Optimization

```cpp
// Hot tier - fast storage, light compression
auto hot_config = index::IndexWriterConfig();
hot_config.setCompressionCodec(CompressionCodecs::lz4());
hot_config.setStorageTier(StorageTier::HOT);

// Cold tier - slow storage, high compression
auto cold_config = index::IndexWriterConfig();
cold_config.setCompressionCodec(CompressionCodecs::zstd(10));
cold_config.setStorageTier(StorageTier::COLD);
```

### Compound File Trade-offs

```cpp
// Many small segments - use compound files
config.setUseCompoundFile(true);
// Benefits: Fewer file handles
// Cost: Slightly slower I/O

// Few large segments - don't use compound files
config.setUseCompoundFile(false);
// Benefits: Faster I/O
// Cost: More file handles
```

---

## SIMD Acceleration

### Enabling SIMD

```bash
# Build with SIMD support
cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DDIAGON_ENABLE_SIMD=ON \
    -DCMAKE_CXX_FLAGS="-march=native -O3"
```

### Using SIMD Scorers

```cpp
#include <diagon/search/BM25ScorerSIMD.h>
#include <diagon/util/CPUInfo.h>

// Check CPU support
if (util::CPUInfo::hasAVX2()) {
    std::cout << "AVX2 supported - SIMD enabled\n";

    // Use SIMD scorer
    auto scorer = std::make_unique<BM25ScorerSIMD>(
        weight, std::move(postings), idf, k1, b);
} else {
    // Fallback to scalar
    auto scorer = std::make_unique<BM25Scorer>(
        weight, std::move(postings), idf, k1, b);
}
```

### SIMD Best Practices

1. **Process in batches of 8**:
```cpp
const int BATCH_SIZE = 8;
alignas(32) int freqs[BATCH_SIZE];
alignas(32) long norms[BATCH_SIZE];
alignas(32) float scores[BATCH_SIZE];

scorer->scoreBatch(freqs, norms, scores);
```

2. **Use aligned memory**:
```cpp
// Aligned allocation
alignas(32) int data[8];

// Or dynamic
int* data = static_cast<int*>(aligned_alloc(32, 8 * sizeof(int)));
```

3. **Enable compiler optimizations**:
```bash
-O3           # Maximum optimization
-march=native # Use all CPU features
-ffast-math   # Fast floating-point (if precision loss OK)
```

---

## Monitoring and Profiling

### Index Statistics

```cpp
void printIndexStats(index::IndexWriter* writer) {
    std::cout << "Documents: " << writer->numDocs() << "\n";
    std::cout << "Deleted: " << writer->numDeletedDocs() << "\n";
    std::cout << "RAM used: " << (writer->ramBytesUsed() / 1024 / 1024)
              << " MB\n";
    std::cout << "Pending changes: "
              << (writer->hasUncommittedChanges() ? "yes" : "no") << "\n";
}
```

### Query Profiling

```cpp
class QueryProfiler {
public:
    struct Profile {
        std::chrono::microseconds duration;
        size_t docs_examined;
        size_t results_returned;
    };

    Profile profile(search::IndexSearcher& searcher,
                   search::Query* query,
                   int topN) {
        auto start = std::chrono::high_resolution_clock::now();

        auto results = searcher.search(query, topN);

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            end - start);

        return Profile{
            duration,
            /* docs_examined */ results.totalHits,
            /* results_returned */ results.scoreDocs.size()
        };
    }
};

// Usage
QueryProfiler profiler;
auto profile = profiler.profile(searcher, query.get(), 10);

std::cout << "Query took " << profile.duration.count() << " μs\n";
std::cout << "Examined " << profile.docs_examined << " docs\n";
std::cout << "Returned " << profile.results_returned << " results\n";
```

### System-Level Profiling

```bash
# CPU profiling with perf
perf record -g ./my_search_app
perf report

# Memory profiling with valgrind
valgrind --tool=massif ./my_search_app
ms_print massif.out.*

# I/O profiling with iostat
iostat -x 1

# CPU usage
top -p $(pgrep my_search_app)
```

---

## Performance Checklist

### Indexing

- [ ] RAM buffer ≥ 256MB
- [ ] Compression codec selected (LZ4 for speed, ZSTD for space)
- [ ] Batch document inserts (≥1000 docs per batch)
- [ ] Merge policy configured
- [ ] Built with `-O3 -march=native`

### Querying

- [ ] Filters used instead of boolean MUST clauses
- [ ] Result set limited to what's needed
- [ ] Query objects reused
- [ ] Reader pooling implemented
- [ ] SIMD enabled for BM25 scoring
- [ ] Parallel search enabled

### Storage

- [ ] MMapDirectory for large indexes
- [ ] Appropriate segment size (1-5GB)
- [ ] Compound files enabled for many small segments
- [ ] Storage tier matches access pattern
- [ ] Force merge done for read-only indexes

### System

- [ ] Sufficient RAM for OS page cache
- [ ] Fast storage (NVMe SSD)
- [ ] Multiple cores available
- [ ] No CPU throttling
- [ ] No I/O throttling

---

## Common Performance Issues

### Issue: Slow Indexing

**Symptoms**: <1000 docs/sec

**Solutions**:
1. Increase RAM buffer size
2. Disable compound files
3. Reduce merge frequency
4. Use faster compression (LZ4)
5. Batch document inserts

### Issue: Slow Queries

**Symptoms**: >100ms for simple queries

**Solutions**:
1. Enable SIMD
2. Use filters instead of boolean queries
3. Reduce result set size
4. Warm up segments
5. Use MMapDirectory
6. Check for excessive segments (>100)

### Issue: High Memory Usage

**Symptoms**: OOM errors, swapping

**Solutions**:
1. Reduce RAM buffer size
2. Close unused readers
3. Use MMapDirectory (OS manages memory)
4. Reduce batch size
5. Enable compound files

### Issue: High Disk Usage

**Symptoms**: Large index size

**Solutions**:
1. Use ZSTD compression
2. Force merge to remove deletes
3. Remove unused fields
4. Disable stored fields if not needed

---

## See Also

- [Quick Start Guide](quick-start.md)
- [SIMD API Reference](../reference/simd.md)
- [Compression API Reference](../reference/compression.md)
- [Architecture Overview](../../design/00_ARCHITECTURE_OVERVIEW.md)
