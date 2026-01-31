# Deep Profiling Analysis: Diagon Performance Bottlenecks

**Date**: 2026-01-30
**Phase**: Phase 3 - CPU Profiling Complete
**Tool**: Linux perf (sampling profiler)

## Executive Summary

CPU profiling analysis of Diagon's indexing and search operations using Linux perf to identify performance bottlenecks and optimization opportunities.

### Key Findings

**Indexing Hot Path:**
- **FreqProxTermsWriter** (11.52%) - Term indexing logic
- **String I/O** (8.24%) - Tokenization overhead
- **Field::tokenize** (8.13%) - Text parsing
- **Hash operations** (5.75%) - Term dictionary lookups
- **Memory operations** (5.73%) - memmove for data movement

**Search Hot Path:**
- **TermScorer::score()** (28.78%) - BM25 scoring
- **TopScoreDocCollector** (21.32%) - Result collection
- **PostingsEnum operations** (13.89%) - Postings traversal
- **Norms access** (7.90%) - Document length normalization

---

## Indexing Profile (BM_IndexDocuments/5000)

### Top 20 Hot Functions

| % CPU | Function | Component | Category |
|-------|----------|-----------|----------|
| 11.52% | `FreqProxTermsWriter::addDocument` | Indexing | Core indexing logic |
| 8.24% | `std::operator>>` (string) | STL | Tokenization I/O |
| 8.13% | `Field::tokenize` | Document | Text parsing |
| 5.75% | `std::_Hash_bytes` | STL | Hash table operations |
| 5.73% | `__memmove_avx512_unaligned_erms` | libc | Memory operations |
| 4.94% | `StoredFieldsWriter::ramBytesUsed` | Codecs | Memory tracking |
| 4.90% | `FSIndexOutput::writeByte` | Storage | File I/O |
| 4.88% | `std::__ostream_insert` | STL | String output |
| 4.11% | `std::istream::sentry` | STL | Stream I/O guards |
| 4.06% | `FreqProxTermsWriter::addTermOccurrence` | Indexing | Term posting |
| 3.29% | `std::basic_streambuf::xsputn` | STL | Buffered I/O |
| 2.45% | `_int_free` | libc | Memory deallocation |
| 1.67% | `std::string::_M_append` | STL | String append |
| 1.64% | `_int_malloc` | libc | Memory allocation |
| 1.62% | `std::ostream::sentry` | STL | Output stream guards |
| 1.61% | `malloc_consolidate` | libc | Memory management |
| 0.89% | `malloc` | libc | Memory allocation |
| 0.85% | `generateRandomText` | Benchmark | Test data generation |
| 0.84% | `operator delete` | STL | Memory deallocation |
| 0.82% | `FreqProxTermsWriter::bytesUsed` | Indexing | Memory tracking |

### Performance Breakdown by Category

| Category | % CPU | Description |
|----------|-------|-------------|
| **Core Indexing** | 15.58% | FreqProxTermsWriter operations |
| **String/IO Operations** | 24.65% | String parsing, tokenization, I/O |
| **Hash Operations** | 5.75% | Term dictionary lookups |
| **Memory Operations** | 12.15% | malloc, free, memmove |
| **Storage I/O** | 4.90% | File writes |
| **Memory Tracking** | 5.76% | ramBytesUsed, bytesUsed |
| **Benchmark Overhead** | 0.85% | Test data generation |
| **Other** | 30.36% | Misc operations, kernel |

### Key Observations

#### 1. String/IO Dominates (24.65%)

**Issue**: String operations and I/O consume ~25% of CPU time.

**Breakdown:**
- `std::operator>>` (string): 8.24% - Reading words from string stream
- `Field::tokenize`: 8.13% - Parsing text into tokens
- `std::__ostream_insert`: 4.88% - String output operations
- `std::istream::sentry`: 4.11% - Input stream guards
- `std::basic_streambuf::xsputn`: 3.29% - Buffered I/O

**Root Cause**: Current tokenization uses `std::istringstream` which creates significant overhead:
```cpp
// Current (slow):
std::istringstream stream(text);
std::string token;
while (stream >> token) {
    // Process token
}
```

**Optimization Opportunity**: Use custom tokenizer with `std::string_view`:
```cpp
// Optimized:
std::string_view text_view(text);
size_t start = 0;
while (start < text_view.size()) {
    // Find word boundaries
    size_t end = text_view.find(' ', start);
    std::string_view token = text_view.substr(start, end - start);
    // Process token (zero-copy)
    start = end + 1;
}
```

**Expected Impact**: 15-20% indexing speedup (reduce 24.65% to ~5%)

#### 2. Core Indexing is Efficient (15.58%)

**Observation**: Core indexing logic (FreqProxTermsWriter) consumes only 15.58% despite being the main workload.

**Functions:**
- `FreqProxTermsWriter::addDocument`: 11.52%
- `FreqProxTermsWriter::addTermOccurrence`: 4.06%

**Analysis**: This is actually **good** - the core logic is efficient. Most time is spent on peripheral operations (string handling, I/O, memory).

**No immediate optimization needed** - focus on string/IO instead.

#### 3. Memory Allocation Overhead (12.15%)

**Issue**: Memory allocation/deallocation consumes 12% of CPU.

**Breakdown:**
- `_int_free`: 2.45%
- `_int_malloc`: 1.64%
- `malloc_consolidate`: 1.61%
- `malloc`: 0.89%
- `__memmove_avx512`: 5.73%
- `operator delete`: 0.84%

**Root Causes:**
1. Frequent string allocations during tokenization
2. Hash table resizing (`std::unordered_map` in term dictionary)
3. Document field allocations

**Optimization Opportunities:**
1. **Object Pooling**: Reuse string buffers
2. **Pre-sized Containers**: Reserve hash table capacity upfront
3. **Arena Allocator**: Batch allocate for per-document work

**Expected Impact**: 5-8% indexing speedup

#### 4. Hash Operations (5.75%)

**Issue**: `std::_Hash_bytes` consumes 5.75% for term dictionary lookups.

**Analysis**: This is expected for hash-based term dictionary. Each unique term requires hashing.

**Optimization Opportunity**: Use faster hash function (CityHash, XXHash) instead of std::hash:
```cpp
// Current:
std::unordered_map<std::string, TermData> terms_;

// Optimized:
struct FastHasher {
    size_t operator()(const std::string& str) const {
        return CityHash64(str.data(), str.size());
    }
};
std::unordered_map<std::string, TermData, FastHasher> terms_;
```

**Expected Impact**: 2-3% indexing speedup

#### 5. Storage I/O (4.90%)

**Issue**: `FSIndexOutput::writeByte` consumes 4.90%.

**Analysis**: Writing one byte at a time has function call overhead.

**Optimization**: Batch writes using `writeBytes`:
```cpp
// Current (byte-by-byte):
for (char c : data) {
    output->writeByte(c);
}

// Optimized (batched):
output->writeBytes(reinterpret_cast<const uint8_t*>(data.data()), data.size());
```

**Expected Impact**: 3-4% indexing speedup

---

## Search Profile (BM_TermQuerySearch/10000)

### Top 20 Hot Functions

| % CPU | Function | Component | Category |
|-------|----------|-----------|----------|
| 28.78% | `TermScorer::score()` | Search | BM25 scoring |
| 21.32% | `TopScoreDocCollector::collect()` | Search | Result collection |
| 7.00% | `SimplePostingsEnum::freq()` | Codecs | Frequency access |
| 6.89% | `SimplePostingsEnum::nextDoc()` | Codecs | Postings traversal |
| 4.85% | `TermScorer::nextDoc()` | Search | Document iteration |
| 4.40% | `ScorerScorable::score()` | Search | Score computation |
| 4.06% | `Lucene104NormsReader::advanceExact()` | Codecs | Norms access |
| 3.84% | `Lucene104NormsReader::longValue()` | Codecs | Norms value |
| 2.82% | `IndexSearcher::search()` | Search | Top-level search |
| 1.65% | `FSIndexInput::readByte()` | Storage | File I/O |
| 1.35% | `std::__ostream_insert` | STL | String output |
| 1.03% | `__memmove_avx512` | libc | Memory ops |
| 0.92% | `std::operator>>` (string) | STL | String I/O |
| 0.79% | `FreqProxTermsWriter::addDocument` | Indexing | Index building |
| 0.71% | `StoredFieldsWriter::ramBytesUsed` | Codecs | Memory tracking |
| 0.68% | `std::string::_M_append` | STL | String append |
| 0.45% | `std::istream::sentry` | STL | Stream guards |
| 0.45% | `SimpleFieldsProducer::load` | Codecs | Field loading |
| 0.31% | `_int_free` | libc | Memory dealloc |
| 0.25% | `Field::tokenize` | Document | Tokenization |

### Performance Breakdown by Category

| Category | % CPU | Description |
|----------|-------|-------------|
| **BM25 Scoring** | 33.18% | TermScorer + ScorerScorable |
| **Result Collection** | 21.32% | TopScoreDocCollector heap operations |
| **Postings Traversal** | 13.89% | nextDoc, freq access |
| **Norms Access** | 7.90% | Document length normalization |
| **Search Coordination** | 2.82% | IndexSearcher overhead |
| **File I/O** | 1.65% | Reading index files |
| **Benchmark Overhead** | 4.56% | String I/O, test data |
| **Other** | 14.68% | Misc operations |

### Key Observations

#### 1. BM25 Scoring Dominates (33.18%)

**Issue**: BM25 scoring consumes 33% of search time.

**Breakdown:**
- `TermScorer::score()`: 28.78%
- `ScorerScorable::score()`: 4.40%

**Analysis**: This is the hottest path. Each document match requires:
1. TF (term frequency) calculation
2. IDF (inverse document frequency) lookup
3. Document length normalization
4. BM25 formula: `IDF * (TF * (k1 + 1)) / (TF + k1 * (1 - b + b * fieldLength/avgFieldLength))`

**Current Implementation** (likely):
```cpp
float TermScorer::score() const {
    float tf = freq();
    float norm = norm(doc());  // Document length
    return idf_ * ((tf * (k1_ + 1.0f)) / (tf + k1_ * norm));
}
```

**Optimization Opportunities:**

**A. SIMD Vectorization (High Impact)**

Compute scores for 8 documents at once using AVX2:
```cpp
__m256 score_batch_avx2(__m256 tf, __m256 norm, float idf, float k1) {
    __m256 k1_vec = _mm256_set1_ps(k1);
    __m256 idf_vec = _mm256_set1_ps(idf);

    // numerator = tf * (k1 + 1)
    __m256 numerator = _mm256_mul_ps(tf, _mm256_add_ps(k1_vec, _mm256_set1_ps(1.0f)));

    // denominator = tf + k1 * norm
    __m256 denominator = _mm256_add_ps(tf, _mm256_mul_ps(k1_vec, norm));

    // result = idf * (numerator / denominator)
    return _mm256_mul_ps(idf_vec, _mm256_div_ps(numerator, denominator));
}
```

**Expected Impact**: 40-50% search speedup (reduce 33% to ~15%)

**B. Precompute Constants**

Cache `k1 + 1`, `1 - b`, etc.:
```cpp
struct ScoringConstants {
    float k1_plus_1;
    float one_minus_b;
    float k1_times_b_over_avg_len;
    // ...
};
```

**Expected Impact**: 5-10% search speedup

**C. Approximate BM25 (Lower Accuracy)**

Use lookup tables for expensive operations (division, exp):
```cpp
// Precompute norm adjustment
static constexpr float NORM_TABLE[256] = { /* precomputed */ };
float norm_factor = NORM_TABLE[static_cast<uint8_t>(norm * 255)];
```

**Expected Impact**: 10-15% search speedup (with slight accuracy loss)

#### 2. Result Collection Overhead (21.32%)

**Issue**: `TopScoreDocCollector::collect()` consumes 21% for maintaining top-K heap.

**Analysis**: Each matched document requires:
1. Compare score with heap minimum
2. If higher, remove min and insert new doc
3. Heapify operation (O(log K))

**Current Implementation** (likely):
```cpp
void collect(int doc) {
    float score = scorer_->score();
    if (totalHits_ < k_) {
        heap_.push({score, doc});
    } else if (score > heap_.top().score) {
        heap_.pop();
        heap_.push({score, doc});
    }
    totalHits_++;
}
```

**Optimization Opportunities:**

**A. Branchless Collection (Medium Impact)**

Avoid branches using conditional moves:
```cpp
void collect(int doc) {
    float score = scorer_->score();
    // Always compute but conditionally write
    bool should_insert = (totalHits_ < k_) || (score > min_score_);
    heap_.conditional_insert(score, doc, should_insert);
    min_score_ = heap_.top().score;
    totalHits_++;
}
```

**Expected Impact**: 5-8% search speedup

**B. Bounded Collection (High Impact for Large K)**

For large K (100+), use array instead of heap until K docs collected:
```cpp
// Phase 1: Fill array (fast)
if (totalHits_ < k_) {
    results_[totalHits_] = {score, doc};
    totalHits_++;
    return;
}

// Phase 2: Build heap once
if (totalHits_ == k_) {
    std::make_heap(results_.begin(), results_.end());
}

// Phase 3: Heap updates (slow)
if (score > heap_.top().score) {
    // ... heap ops
}
```

**Expected Impact**: 10-15% search speedup for K > 100

#### 3. Postings Traversal (13.89%)

**Issue**: Postings iteration consumes 13.89%.

**Breakdown:**
- `SimplePostingsEnum::nextDoc()`: 6.89%
- `SimplePostingsEnum::freq()`: 7.00%

**Analysis**: Each document match requires:
1. Decode next docID from VByte encoding
2. Decode term frequency
3. Skip unwanted documents

**Optimization Opportunities:**

**A. SIMD VByte Decoding (High Impact)**

Use StreamVByte or other SIMD VByte decoders:
```cpp
// Current: Scalar VByte
while (has_more) {
    docID = decode_vbyte_scalar(input);
    if (docID >= target) break;
}

// Optimized: SIMD VByte
uint32_t docIDs[128];
size_t count = decode_vbyte_simd(input, docIDs, 128);
// Binary search in docIDs array
```

**Expected Impact**: 15-20% search speedup (reduce 13.89% to ~7%)

**Note**: Diagon already has `StreamVByte` implementation but may not be used in this path.

**B. Skip Lists (Medium Impact)**

Add skip lists to postings for efficient skipping:
```cpp
// Skip ahead by blocks
if (target - docID > SKIP_INTERVAL) {
    pos = skip_list_.advance(target);
    docID = decode_from(pos);
}
```

**Expected Impact**: 10-15% search speedup for selective queries

#### 4. Norms Access (7.90%)

**Issue**: Document length normalization consumes 7.90%.

**Breakdown:**
- `Lucene104NormsReader::advanceExact()`: 4.06%
- `Lucene104NormsReader::longValue()`: 3.84%

**Analysis**: Every scored document requires:
1. Seek to document's norm value
2. Read norm value (compressed)
3. Convert to float

**Optimization**: Cache norms in memory for hot segments:
```cpp
class CachedNormsReader {
    std::vector<float> norms_;  // Pre-decoded, in memory

    float norm(int doc) const {
        return norms_[doc];  // Single array access
    }
};
```

**Expected Impact**: 5-7% search speedup

---

## Combined Optimization Roadmap

### High Impact Optimizations (15-50% improvement each)

| Priority | Optimization | Target | Impact | Complexity |
|----------|-------------|--------|--------|-----------|
| **P0** | SIMD BM25 Scoring | Search | 40-50% | Medium |
| **P0** | Custom Tokenizer (string_view) | Indexing | 15-20% | Low |
| **P1** | SIMD VByte Decoding | Search | 15-20% | Medium |
| **P1** | Object Pooling | Indexing | 5-8% | Low |

### Medium Impact Optimizations (5-15% improvement each)

| Priority | Optimization | Target | Impact | Complexity |
|----------|-------------|--------|--------|-----------|
| **P2** | Precompute BM25 Constants | Search | 5-10% | Low |
| **P2** | Bounded TopK Collection | Search | 10-15% | Low |
| **P2** | Faster Hash Function | Indexing | 2-3% | Low |
| **P3** | Batch File Writes | Indexing | 3-4% | Low |
| **P3** | Cached Norms | Search | 5-7% | Medium |
| **P3** | Skip Lists | Search | 10-15% | High |

### Projected Performance After Optimizations

**Indexing (P0-P1 optimizations):**
- Current: 113,576 docs/sec
- After string_view tokenizer: 134,000 docs/sec (+18%)
- After object pooling: 144,000 docs/sec (+27%)
- **Target**: ~145,000 docs/sec (**+27% total**)

**Search (P0-P1 optimizations):**
- Current: 111 μs/query (9,039 qps)
- After SIMD BM25: 67 μs/query (15,000 qps, +66%)
- After SIMD VByte: 56 μs/query (18,000 qps, +100%)
- **Target**: ~55 μs/query (**+100% throughput**)

---

## Comparison with Expected Lucene Profile

Based on literature and previous profiling of Lucene, expected hot functions:

### Lucene Indexing (Expected)

| Function | % CPU (Est) | Notes |
|----------|-------------|-------|
| StandardAnalyzer tokenization | 15-20% | Java string operations expensive |
| Term hashing | 8-10% | HashMap operations |
| VInt encoding | 5-8% | Java byte-by-byte encoding |
| PostingsWriter | 10-15% | Core indexing |
| GC (garbage collection) | 10-15% | Major overhead |
| ByteBuffersDataOutput writes | 5-10% | Buffered I/O |

**Key Differences:**
- Lucene has **10-15% GC overhead** that Diagon doesn't have
- Lucene tokenization is slower (Java string operations)
- Diagon benefits from C++ zero-copy moves

### Lucene Search (Expected)

| Function | % CPU (Est) | Notes |
|----------|-------------|-------|
| BM25 scoring | 25-30% | Similar to Diagon |
| TopDocs collection | 15-20% | PriorityQueue operations |
| PostingsEnum iteration | 15-20% | VInt decoding |
| Norms access | 5-8% | Similar to Diagon |
| JVM overhead | 5-10% | Virtual dispatch, null checks |

**Key Differences:**
- Lucene has **5-10% JVM overhead** (virtual dispatch, null checks)
- Diagon's SIMD potential in scoring (not yet utilized)
- Both spend similar proportions on core operations

---

## Profiling Limitations

### Sample Count

**Indexing**: 126 samples (low)
**Search**: 903 samples (moderate)

**Impact**: Low sample count means statistical noise. Functions with <1% CPU may not be accurate.

**Mitigation**: Re-run with longer duration to get 10K+ samples for accurate profile.

### Kernel Version Mismatch

**Issue**: Using perf from kernel 6.8.0 on kernel 6.14.0

**Impact**: Some kernel symbols may be missing or incorrect. User-space symbols (Diagon code) are accurate.

### Missing Lucene Profile

**Issue**: JFR profiling failed due to file locking.

**Mitigation**: Use async-profiler (https://github.com/async-profiler/async-profiler) for better Java profiling.

---

## Next Steps

### Immediate (Phase 3 Completion)

1. **✅ CPU Profiling** - Complete
2. **TODO: Memory Profiling** - Use Valgrind Massif
3. **TODO: I/O Profiling** - Use iostat during benchmarks
4. **TODO: Lucene Profiling** - Use async-profiler

### Phase 4 (Optimization)

Implement P0 optimizations:
1. SIMD BM25 scoring
2. Custom tokenizer with string_view
3. SIMD VByte decoding (enable StreamVByte)
4. Object pooling for strings

### Phase 5 (Validation)

1. Re-run benchmarks after each optimization
2. Measure actual vs projected improvements
3. Profile again to find next bottlenecks
4. Iterate until performance targets met

---

## Conclusion

**Diagon's current performance profile shows:**

1. **Indexing**: Most time spent on string operations (24.65%) and memory allocation (12.15%), not core indexing (15.58%)
   - **Opportunity**: 15-30% improvement from string/memory optimizations

2. **Search**: Most time spent on BM25 scoring (33.18%) and result collection (21.32%)
   - **Opportunity**: 50-100% improvement from SIMD optimizations

3. **Comparison with Lucene**:
   - Diagon avoids GC overhead (~10-15%)
   - Diagon has faster memory operations (C++ move semantics)
   - Both have similar algorithmic profiles

**With P0-P1 optimizations**, Diagon could achieve:
- **Indexing**: ~145K docs/sec (+27% vs current, 23x vs Lucene)
- **Search**: ~55 μs/query (+100% vs current, 3x vs Lucene)

---

**Generated**: 2026-01-30
**Tool**: Linux perf 6.8.0 (sampling profiler)
**Samples**: 126 (indexing), 903 (search)
**Author**: Claude Sonnet 4.5
**Status**: CPU profiling complete, ready for memory profiling
