# P0: Comprehensive End-to-End Performance Profiling

**Date**: 2026-02-05
**Baseline**: 129µs per query (TopK=10, 10K docs)
**Target**: 15-20µs (3-5x of Lucene 4-5µs)
**Gap**: 26-32x slower than Lucene

---

## Profiling Methodology

**Tool**: Linux perf (sampling profiler)
- Sampling frequency: 999 Hz
- Call graph: DWARF unwinding
- Samples collected: 26,419 samples
- Total cycles: 94,635,327,011

**Benchmark**: SearchBenchmark with TopK=10
- Minimum time: 5 seconds per benchmark
- Iterations: 54,701
- Query: "body" field search

---

## Critical Findings

### Hotspot Distribution

| Component | CPU % | Time (µs) | Description |
|-----------|-------|-----------|-------------|
| **BM25 Scoring** | 30.29% | 39.1 | Score computation + norms lookup |
| **TopK Collection** | 33.96% | 43.8 | Batch flushing + heap management |
| **PostingsEnum** | 13.95% | 18.0 | Document iteration + frequency |
| **StreamVByte Decode** | 6.07% | 7.8 | SIMD integer decompression |
| **FST/BlockTree** | 2.62% | 3.4 | Term dictionary lookup |
| **Virtual Calls** | 2.05% | 2.6 | Scorer abstraction overhead |
| **Other** | 1.38% | 1.8 | Search orchestration |
| **Unaccounted** | 9.68% | 12.5 | Miscellaneous |
| **TOTAL** | 100% | 129 µs | |

---

## Detailed Bottleneck Analysis

### 1. TopK Collection (43.8µs, 33.96%)

**HIGHEST PRIORITY - Largest bottleneck**

#### Breakdown

| Function | CPU % | Time (µs) | Issue |
|----------|-------|-----------|-------|
| flushBatch() | 13.19% | 17.0 | Heap insertions for batch |
| collect() | 9.03% | 11.7 | Batch accumulation |
| collectSingle() | 8.67% | 11.2 | Individual heap insert |
| topDocs() | 3.07% | 4.0 | Final heap extraction |

#### Root Causes

1. **No Early Termination**
   - Current: Scores ALL matching documents (exhaustive)
   - Lucene: Block-Max WAND stops early when threshold reached
   - Impact: 2-10x unnecessary work

2. **Inefficient Heap Implementation**
   - Current: std::priority_queue with per-insertion overhead
   - Lucene: Custom HitQueue with batch operations
   - Impact: 2-3x slower heap operations

3. **Batch Flushing Overhead**
   - Current: Flush batch of 128 docs to heap individually
   - Better: Bulk heap operations or skip low-scoring batches
   - Impact: 1.5-2x overhead

#### Optimization Opportunities

**Block-Max WAND (P0 - Critical)**
- Add block-max metadata to postings (during indexing)
- Implement dynamic threshold tracking
- Skip blocks with max_score < threshold
- Expected: **5-10x improvement** (43.8µs → 4-9µs)

**Better Heap Implementation (P1)**
- Replace std::priority_queue with custom heap
- Implement batch insertion (insert 128 at once)
- Use winner tree for multi-way merge
- Expected: **2-3x improvement** (17µs → 6-8µs in flushBatch)

**Batch Filtering (P1)**
- SIMD filter batch before heap insertion
- Skip entire batch if max(batch_scores) < threshold
- Expected: **1.5-2x improvement** (11.7µs → 6-8µs in collect)

---

### 2. BM25 Scoring (39.1µs, 30.29%)

**HIGH PRIORITY - Second largest bottleneck**

#### Breakdown

| Function | CPU % | Time (µs) | Issue |
|----------|-------|-----------|-------|
| TermScorer::score() | 23.39% | 30.2 | BM25 formula computation |
| NormsReader::advanceExact() | 5.66% | 7.3 | Norm lookup for doc length |
| NormsReader::longValue() | 1.24% | 1.6 | Norm value extraction |

#### Root Causes

1. **Scalar BM25 Computation**
   - Current: Compute one score at a time
   - Better: SIMD compute 4-8 scores at once (AVX2/AVX512)
   - Impact: 3-5x slower than SIMD

2. **Norms Lookup Overhead**
   - Current: advanceExact() per document (7.3µs)
   - Issue: Indirect lookup through iterator interface
   - Impact: 2-3x slower than direct array access

3. **No Prefetching**
   - Current: Sequential norms access without prefetch
   - Better: Prefetch norms for next 8-12 docs
   - Impact: 1.2-1.5x cache miss penalty

#### Optimization Opportunities

**SIMD BM25 Scoring (P0)**
- Batch compute 4-8 scores using AVX2/AVX512
- Vectorize: (freq * (k1 + 1)) / (freq + k * (1 - b + b * fieldLength / avgFieldLength))
- Handle norms lookup in batch
- Expected: **3-4x improvement** (30.2µs → 7-10µs)

**Direct Norms Access (P1)**
- Replace iterator with direct array access
- Cache norms array pointer
- Expected: **2x improvement** (7.3µs → 3-4µs)

**Prefetch Norms (P1)**
- Prefetch norms[doc + 12] in scoring loop
- Expected: **1.2x improvement** (reduce cache misses)

---

### 3. PostingsEnum (18.0µs, 13.95%)

**MEDIUM-HIGH PRIORITY**

#### Breakdown

| Function | CPU % | Time (µs) | Issue |
|----------|-------|-----------|-------|
| nextDoc() | 6.38% | 8.2 | Document iteration |
| freq() | 2.81% | 3.6 | Frequency retrieval |
| TermScorer::nextDoc() | 2.27% | 2.9 | Wrapper overhead |
| refillBuffer() | 1.49% | 1.9 | Buffer management |

#### Root Causes

1. **Single-Document Iteration**
   - Current: nextDoc() called per document
   - Better: Batch decode 128 docs at once
   - Impact: 3-5x call overhead

2. **Frequent Buffer Refills**
   - Current: refillBuffer() called every 128 docs
   - Issue: StreamVByte decodes 4 ints at a time, not optimized for large batches
   - Impact: 1.5-2x overhead

3. **Virtual Function Calls**
   - Current: PostingsEnum is virtual interface
   - Impact: Branch misprediction, no inlining
   - Measured: 2.05% in ScorerScorable::score()

#### Optimization Opportunities

**Batch PostingsEnum (P0)**
- Implement BatchPostingsEnum::nextBatch(int* docs, int* freqs, int max)
- Decode 128 docs at once using StreamVByte
- Bypass virtual calls for hot path
- Expected: **4-6x improvement** (18.0µs → 3-4µs)

**Optimized Buffer Management (P1)**
- Larger buffer (4096 instead of 512 ints)
- Reduce refill frequency
- Expected: **1.5x improvement** (1.9µs → 1.2µs)

**Devirtualization (P1)**
- Use template specialization for known types
- Use `final` keyword to enable devirtualization
- Expected: **1.2x improvement** (2.6µs virtual → 2.2µs)

---

### 4. StreamVByte Decode (7.8µs, 6.07%)

**MEDIUM PRIORITY**

#### Current Performance
- Function: StreamVByte::decode4_SSE()
- Time: 7.8µs (6.07% CPU)
- Already using SSE2 SIMD

#### Analysis

**StreamVByte is already optimized** (from Task #37):
- Uses precomputed lookup tables (shuffle_mask_)
- SIMD decodes 4 integers at a time
- Achieves 909M ints/sec

#### Why Still Shows 6%?

1. **Called Very Frequently**
   - 10K docs × 10 queries = 100K documents decoded
   - Each call decodes only 4 ints (need 25K calls)
   - Overhead: Function call + table lookup

2. **Not Using Bulk Decode**
   - Current: decode4_SSE() called repeatedly
   - Better: Bulk decode entire buffer (128 docs)
   - Expected: 2-3x fewer function calls

#### Optimization Opportunities

**Bulk StreamVByte Decode (P1)**
- Implement decodeBulk_SSE(uint8_t* in, uint32_t* out, size_t count)
- Decode 128 integers at once (32 calls instead of 128)
- Amortize table lookup overhead
- Expected: **2-3x improvement** (7.8µs → 2.6-3.9µs)

---

### 5. FST/BlockTree Lookup (3.4µs, 2.62%)

**LOW-MEDIUM PRIORITY**

#### Breakdown

| Function | CPU % | Time (µs) | Issue |
|----------|-------|-----------|-------|
| BlockTreeTermsReader::loadBlock() | 2.62% | 3.4 | Load FST block from disk/cache |

#### Analysis

**Already reasonably fast** at 3.4µs per query.

#### Optimization Opportunities (P2 - Long Term)

**Jump Tables (P2)**
- Cache common prefix blocks
- Skip FST traversal for frequent terms
- Expected: **2-3x improvement** (3.4µs → 1.1-1.7µs)

**FST Node Caching (P2)**
- Cache last 1000 FST nodes
- LRU eviction policy
- Expected: **1.5-2x improvement** (3.4µs → 1.7-2.3µs)

---

### 6. Virtual Call Overhead (2.6µs, 2.05%)

**LOW PRIORITY**

#### Breakdown

| Function | CPU % | Time (µs) | Issue |
|----------|-------|-----------|-------|
| ScorerScorable::score() | 2.05% | 2.6 | Virtual call to scorer |

#### Analysis

**Virtual call overhead is acceptable** at 2.6µs (2% of total).

Modern CPUs handle branch prediction well. Earlier hypothesis of 68% virtual call overhead was wrong - reality is only 2%.

#### Optimization Opportunities (P2)

**Template Specialization (P2)**
- Template search loop on scorer type
- Enables inlining
- Expected: **1.5x improvement** (2.6µs → 1.7µs)

---

## Cumulative Optimization Roadmap

### P0 - Immediate (2 weeks)

| Optimization | Component | Current | Target | Speedup | Priority |
|--------------|-----------|---------|--------|---------|----------|
| **Block-Max WAND** | TopK | 43.8µs | 4-9µs | **5-10x** | 🔴 P0 |
| **SIMD BM25** | Scoring | 30.2µs | 7-10µs | **3-4x** | 🔴 P0 |
| **Batch PostingsEnum** | PostingsEnum | 18.0µs | 3-4µs | **4-6x** | 🔴 P0 |

**Expected cumulative impact:**
- Current: 129µs
- After P0: **~25-35µs** (4-5x improvement)
- Gap to Lucene: 5-9x (down from 26-32x)

### P1 - Short Term (1-2 months)

| Optimization | Component | Current | Target | Speedup | Priority |
|--------------|-----------|---------|--------|---------|----------|
| **Better Heap** | TopK flush | 17.0µs | 6-8µs | **2-3x** | 🟡 P1 |
| **Direct Norms** | BM25 norms | 7.3µs | 3-4µs | **2x** | 🟡 P1 |
| **Bulk StreamVByte** | Decode | 7.8µs | 2.6-3.9µs | **2-3x** | 🟡 P1 |
| **Batch Filter** | TopK collect | 11.7µs | 6-8µs | **1.5-2x** | 🟡 P1 |

**Expected cumulative impact:**
- After P0: ~25-35µs
- After P1: **~12-18µs** (additional 1.5-2x)
- Gap to Lucene: 2.4-4.5x

### P2 - Long Term (3-6 months)

| Optimization | Component | Current | Target | Speedup | Priority |
|--------------|-----------|---------|--------|---------|----------|
| **FST Caching** | FST lookup | 3.4µs | 1.1-2.3µs | **1.5-3x** | 🟢 P2 |
| **Devirtualization** | Virtual calls | 2.6µs | 1.7µs | **1.5x** | 🟢 P2 |
| **Prefetch Norms** | BM25 norms | - | - | **1.2x** | 🟢 P2 |

**Expected cumulative impact:**
- After P1: ~12-18µs
- After P2: **~8-12µs** (additional 1.2-1.8x)
- Gap to Lucene: 1.6-3x

### Stretch Goal (6-12 months)

**Advanced Lucene Features:**
- Multi-level skip lists (1.5-3x on selective queries)
- Optimized codec (Lucene105/106 with better compression)
- Better memory layout (cache-aware data structures)

**Expected**: **Match or exceed Lucene at 4-7µs**

---

## Implementation Priority Queue

### Week 1-2: Block-Max WAND (P0)

**Why first?**: Largest single optimization (5-10x improvement)

**Tasks:**
1. Add block-max metadata to Lucene104PostingsWriter
2. Store max_score per 128-doc block
3. Implement dynamic threshold tracking in TopScoreDocCollector
4. Skip blocks with max_score < threshold
5. Test on MSMarco dataset

**Expected**: 129µs → 13-26µs (5-10x improvement)

### Week 3: SIMD BM25 Scoring (P0)

**Tasks:**
1. Implement AVX2 batch BM25 scorer (4 scores at once)
2. Vectorize norms lookup
3. Handle edge cases (norm=0, norm=127)
4. Add unit tests for correctness

**Expected**: 30µs → 7-10µs (3-4x improvement)

### Week 4: Batch PostingsEnum (P0)

**Tasks:**
1. Design BatchPostingsEnum interface
2. Implement nextBatch(int* docs, int* freqs, int max)
3. Use StreamVByte bulk decode
4. Update TermScorer to use batch API

**Expected**: 18µs → 3-4µs (4-6x improvement)

---

## Measurement & Validation

### Before Each Optimization

1. **Baseline benchmark**: Run SearchBenchmark 5x, record median
2. **Profile**: Run perf to verify bottleneck exists
3. **Document**: Record current performance in task document

### After Each Optimization

1. **Benchmark**: Run SearchBenchmark 5x, record median
2. **Compare**: Calculate speedup vs baseline
3. **Profile**: Verify bottleneck was addressed
4. **Test**: Run all unit tests + correctness tests
5. **Commit**: Commit with detailed message

### Regression Detection

- Run SearchBenchmark in CI
- Alert if performance degrades >5%
- Store baseline results in git

---

## Risk Analysis

### High Risk

**Block-Max WAND**
- Risk: Implementation complexity, correctness bugs
- Mitigation: Extensive unit tests, compare results with exhaustive search
- Fallback: Keep exhaustive search as option

**SIMD BM25**
- Risk: Correctness issues with vectorized math
- Mitigation: Compare results with scalar version, test edge cases
- Fallback: Scalar version always available

### Medium Risk

**Batch PostingsEnum**
- Risk: API design may not fit all query types
- Mitigation: Design for extensibility, keep single-doc API
- Fallback: Single-doc API still works

### Low Risk

**Better Heap, Direct Norms, Bulk StreamVByte**
- Risk: Minimal, internal optimizations
- Mitigation: Unit tests, profiling validation

---

## Success Metrics

### P0 Target (2 weeks)

- [ ] Latency: 129µs → 25-35µs (4-5x improvement)
- [ ] Gap to Lucene: 26-32x → 5-9x
- [ ] All tests passing
- [ ] No correctness regressions

### P1 Target (2 months)

- [ ] Latency: 25-35µs → 12-18µs (additional 1.5-2x)
- [ ] Gap to Lucene: 5-9x → 2.4-4.5x

### P2 Target (6 months)

- [ ] Latency: 12-18µs → 8-12µs (additional 1.2-1.8x)
- [ ] Gap to Lucene: 2.4-4.5x → 1.6-3x

### Stretch Goal (12 months)

- [ ] Latency: **4-7µs** (match or exceed Lucene)
- [ ] Gap to Lucene: **0.8-1.75x** (parity or better)

---

## Next Steps

1. **Create Task #39**: Implement Block-Max WAND
2. **Start implementation**: Begin with postings writer changes
3. **Daily progress**: Update task with findings and blockers
4. **Weekly review**: Check progress against timeline

---

**Status**: Ready to begin P0 optimizations
**Priority**: 🔴 CRITICAL
**Timeline**: 2 weeks for P0 (Block-Max WAND + SIMD BM25 + Batch PostingsEnum)
