# SIMD and Prefetch Optimization Analysis

**Date**: 2026-01-26
**Status**: Analysis for optimization opportunities

## Executive Summary

This document analyzes DIAGON codebase for SIMD and prefetch optimization opportunities. Current status:
- **SIMD Usage**: Limited - Only BM25ScorerSIMD has AVX2 implementation
- **Prefetch Usage**: None - No software prefetch instructions used
- **Optimization Potential**: High - Many hot paths could benefit from SIMD and prefetch

## 1. Current SIMD Usage

### ✅ Implemented: BM25ScorerSIMD

**File**: `src/core/src/search/BM25ScorerSIMD.cpp`

**SIMD Operations**:
- AVX2 intrinsics for batch scoring (8 documents at once)
- FMA (Fused Multiply-Add) support when available
- Vectorized BM25 formula computation

**Performance**: ~4-8× speedup vs scalar for batch operations

**Status**: ✅ Production-ready with proper fallback to scalar

### ⚠️ Limitations

1. **Only used for scoring** - Not used for decoding, copying, or other operations
2. **No ARM NEON support** - x86-only (AVX2)
3. **Batch-only** - Single-document scoring still uses scalar path
4. **No SIMD VByte decoding** - Posting list decoding is scalar

## 2. Missing SIMD Opportunities

### 🔴 Critical: VByte Decoding (Posting Lists)

**Location**: `src/core/include/diagon/util/VByte.h`

**Current Implementation**: Scalar loop
```cpp
static uint32_t decodeUInt32(const uint8_t* input, int* bytesRead) {
    uint32_t value = 0;
    int shift = 0;
    int bytes = 0;
    uint8_t byte;
    do {
        byte = input[bytes++];
        value |= static_cast<uint32_t>(byte & 0x7F) << shift;
        shift += 7;
    } while ((byte & 0x80) != 0);
    *bytesRead = bytes;
    return value;
}
```

**SIMD Opportunity**:
- **StreamVByte algorithm** (Lemire et al.) - Decode 4-8 integers at once using SIMD
- Expected speedup: **2-3× for posting list decoding**
- Impact: High - posting list decoding is on critical query path

**References**:
- "Decoding billions of integers per second through vectorization" (Lemire et al., 2015)
- https://github.com/lemire/streamvbyte

**Implementation Priority**: 🔴 **HIGH** - Major query performance bottleneck

### 🟡 Important: Memory Copy Operations

**Location**: Multiple files use `std::memcpy`

**Current Hot Paths**:
1. **MMapIndexInput::readBytes()** (`src/core/src/store/MMapIndexInput.cpp:158`)
   ```cpp
   std::memcpy(buffer + buffer_offset, chunk.data + chunk_offset, to_copy);
   ```

2. **ByteBlockPool operations** (`src/core/src/util/ByteBlockPool.cpp`)
3. **Column data copying** (`src/core/include/diagon/columns/ColumnVector.h`)

**SIMD Opportunity**:
- Replace small memcpy with SIMD instructions for aligned data
- Use `_mm256_loadu_si256` + `_mm256_storeu_si256` for 32-byte chunks
- Expected speedup: **1.5-2× for small copies (<256 bytes)**

**Implementation Priority**: 🟡 **MEDIUM** - Moderate impact

### 🟡 Important: Column Operations

**Location**: `src/core/include/diagon/columns/ColumnVector.h`

**Current Implementation**: Scalar operations for filter evaluation

**SIMD Opportunity**:
- Vectorized range checks (e.g., `value >= min && value <= max`)
- SIMD comparisons: `_mm256_cmpgt_epi32`, `_mm256_and_si256`
- Bitmask generation for filtered documents
- Expected speedup: **2-4× for filter evaluation**

**Implementation Priority**: 🟡 **MEDIUM** - Important for analytical queries

### 🟢 Nice-to-have: Batch Document Processing

**Location**: Various document processing paths

**SIMD Opportunity**:
- Process 4-8 documents simultaneously during indexing
- Vectorized field extraction
- Batch norm computation

**Implementation Priority**: 🟢 **LOW** - Write path, less critical

## 3. Missing Prefetch Opportunities

### 🔴 Critical: MMapIndexInput Sequential Access

**Location**: `src/core/src/store/MMapIndexInput.cpp`

**Current Implementation**: No prefetch
```cpp
void MMapIndexInput::readBytes(uint8_t* buffer, size_t length) {
    while (remaining > 0) {
        // ... calculate chunk and offset ...
        std::memcpy(buffer + buffer_offset, chunk.data + chunk_offset, to_copy);
        // No prefetch for next chunk!
    }
}
```

**Prefetch Opportunity**:
```cpp
void MMapIndexInput::readBytes(uint8_t* buffer, size_t length) {
    while (remaining > 0) {
        // Prefetch next chunk if crossing boundary
        if (chunk_offset + to_copy >= chunk.length && chunk_idx + 1 < num_chunks_) {
            __builtin_prefetch(chunks_[chunk_idx + 1].data, 0, 3);
        }

        std::memcpy(buffer + buffer_offset, chunk.data + chunk_offset, to_copy);
    }
}
```

**Expected Impact**:
- Reduce cache miss penalty for chunk boundary crossings
- Expected improvement: **5-15% for sequential reads crossing chunks**

**Implementation Priority**: 🔴 **HIGH** - Simple addition, measurable impact

### 🔴 Critical: Posting List Iteration

**Location**: Posting list readers (not yet implemented)

**Prefetch Opportunity**:
- Prefetch next block of doc IDs when nearing end of current block
- Prefetch term frequencies when accessing doc IDs
- **Software prefetch pattern**:
  ```cpp
  // When processing posting list entry i
  if (i % PREFETCH_DISTANCE == 0) {
      __builtin_prefetch(&postings[i + PREFETCH_DISTANCE], 0, 3);
  }
  ```

**Expected Impact**: **10-20% improvement for posting list traversal**

**Implementation Priority**: 🔴 **HIGH** - Critical query path

### 🟡 Important: Skip List Traversal

**Location**: Future skip list implementation

**Prefetch Opportunity**:
- Prefetch next skip level when jumping
- Prefetch target block when skip pointer followed

**Expected Impact**: **5-10% improvement for large posting lists**

**Implementation Priority**: 🟡 **MEDIUM** - Depends on skip list implementation

### 🟢 Nice-to-have: Column Scanning

**Location**: Column storage reads

**Prefetch Opportunity**:
- Prefetch next granule during sequential scan
- Streaming prefetch pattern for large scans

**Expected Impact**: **5-10% improvement for column scans**

**Implementation Priority**: 🟢 **LOW** - Already fast with OS prefetch

## 4. Platform-Specific Considerations

### x86-64 (Intel/AMD)

**Available Instructions**:
- ✅ SSE2 (baseline - universally available)
- ✅ SSE4.2 (POPCNT, CRC32)
- ✅ AVX2 (256-bit vectors, 8×32-bit or 4×64-bit)
- ⚠️ AVX-512 (512-bit vectors) - Limited availability, not recommended yet

**Prefetch**:
- `_mm_prefetch(addr, _MM_HINT_T0)` - L1 cache
- `_mm_prefetch(addr, _MM_HINT_T1)` - L2 cache
- `_mm_prefetch(addr, _MM_HINT_T2)` - L3 cache
- `_mm_prefetch(addr, _MM_HINT_NTA)` - Non-temporal (bypass cache)

### ARM64 (Apple Silicon, AWS Graviton)

**Available Instructions**:
- ✅ NEON (128-bit vectors, 4×32-bit or 2×64-bit)
- ⚠️ SVE (Scalable Vector Extension) - Limited availability

**Prefetch**:
- `__builtin_prefetch(addr, 0, 3)` - Read, high temporal locality
- `__builtin_prefetch(addr, 1, 3)` - Write prefetch

**Current Status**: ⚠️ **No ARM NEON implementation** - BM25ScorerSIMD is x86-only

## 5. Recommended Implementation Plan

### Phase 1: High-Impact, Low-Effort (1-2 weeks)

**Goal**: Quick wins with measurable impact

1. **Add prefetch to MMapIndexInput** (2 days)
   - Add `__builtin_prefetch` for chunk boundary crossings
   - Add compile-time detection: `#ifdef __GNUC__`
   - Benchmark: Expect 5-15% improvement

2. **Add prefetch to posting list iteration** (3 days)
   - Wait for posting list implementation
   - Add software prefetch with tunable distance
   - Benchmark: Expect 10-20% improvement

3. **Optimize small memcpy with SIMD** (3 days)
   - Replace memcpy in hot paths for sizes 16-256 bytes
   - Use AVX2 load/store for aligned data
   - Benchmark: Expect 1.5-2× for small copies

**Expected Total Impact**: **15-30% query performance improvement**

### Phase 2: Major SIMD Work (4-6 weeks)

**Goal**: Vectorize critical decoding paths

1. **Implement StreamVByte for VInt decoding** (2 weeks)
   - Port StreamVByte algorithm (Lemire)
   - Integrate with posting list reader
   - Add scalar fallback for unaligned/tail data
   - Benchmark: Expect 2-3× decoding speedup

2. **Add ARM NEON support to BM25ScorerSIMD** (1 week)
   - Port AVX2 code to NEON intrinsics
   - Add runtime detection via `#ifdef __ARM_NEON`
   - Test on Apple Silicon/AWS Graviton

3. **Vectorize column filter operations** (1-2 weeks)
   - SIMD range checks
   - Bitmask generation
   - Benchmark: Expect 2-4× filter evaluation speedup

**Expected Total Impact**: **2-3× query performance for text+filter workloads**

### Phase 3: Advanced Optimizations (ongoing)

**Goal**: Fine-tune SIMD usage based on profiling

1. **Auto-vectorization analysis**
   - Use compiler reports: `-fopt-info-vec-optimized`
   - Identify missed optimization opportunities
   - Add pragmas/hints where needed

2. **Cache-aware prefetching**
   - Tune prefetch distance based on cache sizes
   - Add adaptive prefetch based on access patterns
   - Measure using perf counters: `perf stat -e cache-misses`

3. **Batch processing**
   - Vectorized document processing during indexing
   - SIMD norm computation
   - Parallel field extraction

## 6. SIMD Abstraction Layer Design

### Motivation

Current BM25ScorerSIMD has platform-specific code with `#ifdef` blocks. As we add more SIMD operations, need abstraction layer.

### Proposed Design

```cpp
// src/core/include/diagon/util/SIMDTraits.h
namespace diagon::util::simd {

#if defined(__AVX2__)
    using Vec256i = __m256i;
    using Vec256f = __m256;
    constexpr int VEC_WIDTH = 8;  // 8 floats or 8 int32s

    inline Vec256i load_i32(const int32_t* addr) {
        return _mm256_loadu_si256(reinterpret_cast<const __m256i*>(addr));
    }

    inline void store_i32(int32_t* addr, Vec256i vec) {
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(addr), vec);
    }

    inline void prefetch_read(const void* addr) {
        _mm_prefetch(static_cast<const char*>(addr), _MM_HINT_T0);
    }

#elif defined(__ARM_NEON)
    using Vec128i = int32x4_t;
    using Vec128f = float32x4_t;
    constexpr int VEC_WIDTH = 4;  // 4 floats or 4 int32s

    inline Vec128i load_i32(const int32_t* addr) {
        return vld1q_s32(addr);
    }

    inline void store_i32(int32_t* addr, Vec128i vec) {
        vst1q_s32(addr, vec);
    }

    inline void prefetch_read(const void* addr) {
        __builtin_prefetch(addr, 0, 3);
    }

#else
    // Scalar fallback
    constexpr int VEC_WIDTH = 1;

    inline void prefetch_read(const void* addr) {
        (void)addr;  // No-op on platforms without prefetch
    }
#endif

}  // namespace diagon::util::simd
```

**Benefits**:
- Single source code works across platforms
- Easy to add new platforms (e.g., RISC-V Vector Extension)
- Compiler can optimize away abstraction layer (zero-cost)
- Easier testing and maintenance

## 7. Benchmarking Strategy

### Micro-benchmarks

**VByte Decoding**:
```cpp
// Benchmark scalar vs SIMD VByte decoding
void BM_VByteDecodeScalar(benchmark::State& state) {
    std::vector<uint32_t> values = generateTestData();
    std::vector<uint8_t> encoded = encodeVByte(values);

    for (auto _ : state) {
        for (size_t i = 0; i < encoded.size(); ) {
            int bytesRead;
            uint32_t decoded = VByte::decodeUInt32(&encoded[i], &bytesRead);
            benchmark::DoNotOptimize(decoded);
            i += bytesRead;
        }
    }
    state.SetBytesProcessed(state.iterations() * encoded.size());
}

void BM_VByteDecodeSIMD(benchmark::State& state) {
    // Same but with StreamVByte
}
```

**Prefetch Impact**:
```cpp
// Benchmark MMapIndexInput with/without prefetch
void BM_MMapSequentialRead_NoPrefetch(benchmark::State& state) {
    auto input = createLargeFile();
    uint8_t buffer[1024];

    for (auto _ : state) {
        input->seek(0);
        while (input->getFilePointer() < input->length()) {
            size_t to_read = std::min(sizeof(buffer),
                                     input->length() - input->getFilePointer());
            input->readBytes(buffer, to_read);
        }
    }
}

void BM_MMapSequentialRead_WithPrefetch(benchmark::State& state) {
    // Same but with prefetch-enabled MMapIndexInput
}
```

### End-to-End Benchmarks

**Query Performance**:
- Single-term query (measure VByte decoding impact)
- Boolean query (measure SIMD BM25 impact)
- Filter + text query (measure column SIMD impact)

**Metrics**:
- Throughput (queries/second)
- Latency (P50, P95, P99)
- CPU utilization
- Cache miss rate (`perf stat -e cache-misses,cache-references`)

## 8. Implementation Checklist

### SIMD Opportunities

Priority | Feature | Location | Expected Speedup | Effort
---------|---------|----------|-----------------|-------
🔴 HIGH | StreamVByte decoding | VByte.h | 2-3× | 2 weeks
🔴 HIGH | ARM NEON BM25 | BM25ScorerSIMD | Same perf on ARM | 1 week
🟡 MEDIUM | Column filters | ColumnVector.h | 2-4× | 1-2 weeks
🟡 MEDIUM | SIMD memcpy | MMapIndexInput | 1.5-2× | 3 days
🟢 LOW | Batch processing | IndexWriter | 1.2-1.5× | 2 weeks

### Prefetch Opportunities

Priority | Feature | Location | Expected Speedup | Effort
---------|---------|----------|-----------------|-------
🔴 HIGH | MMap chunk prefetch | MMapIndexInput.cpp | 5-15% | 2 days
🔴 HIGH | Posting list prefetch | PostingsReader | 10-20% | 3 days
🟡 MEDIUM | Skip list prefetch | SkipListReader | 5-10% | 1 week
🟢 LOW | Column scan prefetch | ColumnReader | 5-10% | 1 week

### Infrastructure

- [ ] SIMD abstraction layer (SIMDTraits.h)
- [ ] Runtime CPU feature detection
- [ ] Micro-benchmark suite
- [ ] Profiling infrastructure (perf integration)
- [ ] ARM CI/CD pipeline (Apple Silicon or AWS Graviton)

## 9. Risk Analysis

### Performance Risks

1. **Code bloat**: SIMD code can be 3-5× larger than scalar
   - Mitigation: Use abstraction layer, share common patterns

2. **Maintenance burden**: Multiple platform codepaths
   - Mitigation: Good test coverage, CI on multiple platforms

3. **Diminishing returns**: Some operations already memory-bound
   - Mitigation: Profile first, optimize hot paths only

### Compatibility Risks

1. **AVX-512 compatibility**: Not universal on x86-64
   - Mitigation: Stick to AVX2 for now, add AVX-512 as optional later

2. **ARM compatibility**: NEON widely available, SVE is not
   - Mitigation: Use NEON (universal), defer SVE

3. **Compiler support**: Older compilers may not support all intrinsics
   - Mitigation: Require GCC 11+ / Clang 14+ (already documented)

## 10. Measurement and Validation

### Before/After Metrics

**Query Performance**:
- Run benchmark suite (single-term, boolean, filter+text)
- Measure QPS improvement for each query type
- Profile with `perf` to verify cache miss reduction

**CPU Efficiency**:
- Measure instructions per cycle (IPC) via `perf stat`
- Verify SIMD instructions are actually used: `perf record -e cycles:u`
- Check for auto-vectorization: `gcc -fopt-info-vec-all`

**Memory Bandwidth**:
- Measure with `perf stat -e cpu/event=0xb7,umask=0x1/` (memory loads)
- Verify prefetch reduces stalls: `perf stat -e mem_load_uops_retired.l3_miss`

### Regression Testing

- [ ] Run full test suite with SIMD code (ensure correctness)
- [ ] Compare SIMD results against scalar (bit-exact where possible)
- [ ] Test on multiple CPU models (Intel, AMD, Apple, AWS Graviton)
- [ ] Verify graceful fallback when SIMD unavailable

## 11. Conclusion and Recommendations

### Current State

DIAGON has **limited SIMD usage** (only BM25 scoring) and **no prefetch**. Significant optimization opportunities exist.

### Immediate Actions (Next Sprint)

1. ✅ **Add prefetch to MMapIndexInput** - Low effort, high impact
2. ✅ **Design SIMD abstraction layer** - Foundation for future work
3. ✅ **Set up micro-benchmark suite** - Measure baseline performance

### Medium-Term Goals (Next Quarter)

1. ✅ **Implement StreamVByte** - 2-3× decoding speedup
2. ✅ **Add ARM NEON support** - Maintain performance on ARM
3. ✅ **Vectorize column filters** - 2-4× filter speedup

### Expected Overall Impact

Conservative estimates based on similar systems (Lucene, ClickHouse, Pisa):
- **Query throughput**: +50-100% (2-3× faster posting list processing)
- **Query latency**: -30-50% (P95 latency improvement)
- **CPU efficiency**: +20-40% (better IPC, fewer cache misses)

**Recommendation**: Prioritize Phase 1 (prefetch) and Phase 2 (StreamVByte + ARM NEON) for maximum impact with reasonable effort.
