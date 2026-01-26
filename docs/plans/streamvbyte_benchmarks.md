# StreamVByte Posting List Benchmarks

## Test Environment
- CPU: 64-core @ 2.6 GHz (AWS m5d.16xlarge)
- Build: Release mode with LTO and AVX2 enabled
- Benchmark Framework: Google Benchmark

## Results Summary

### Raw Decode Performance (No Reader Overhead)

| Encoding | Throughput | Speedup |
|----------|-----------|---------|
| StreamVByte | **348 M items/s** | **1.72×** baseline |
| VInt (baseline) | 202 M items/s | 1.0× |

**✅ Result**: StreamVByte raw decode is **1.72× faster** than VInt (as expected)

### Posting List Decode (With Lucene104PostingsReader)

| Implementation | Throughput | Speedup |
|----------------|-----------|---------|
| VInt (baseline) | **104 M items/s** | **1.51×** StreamVByte |
| StreamVByte (current) | 69 M items/s | 1.0× |

**❌ Result**: VInt is **1.51× faster** than StreamVByte (unexpected!)

### Encode Performance

| Encoding | Throughput |
|----------|-----------|
| StreamVByte | 118 M items/s |
| VInt | 121 M items/s |

**Result**: About the same (~2.5% difference)

## Analysis

### Why is Raw StreamVByte Faster?

StreamVByte achieves the expected 1.7× speedup through:
1. **SIMD parallelism**: Decodes 4 integers simultaneously using AVX2/SSE4.1
2. **Fewer branches**: Single decode operation for 4 values
3. **Better CPU utilization**: ~5 cycles per 4 integers vs ~80 cycles for scalar VInt

### Why is the Reader Slower?

The Lucene104PostingsReader with StreamVByte is actually **slower** despite faster raw decoding due to:

1. **Buffer refill overhead**: Every 4 docs, the reader must:
   - Read control byte
   - Calculate data bytes needed (loop over 4 values)
   - Read data bytes
   - Call StreamVByte::decode4()
   - Handle buffer state (bufferPos_, bufferLimit_)

2. **Frequent refills**: With only 4-doc buffers, refilling happens 25× per 100 docs, 250× per 1000 docs

3. **Sequential access pattern**: Posting list iteration is inherently sequential, so:
   - SIMD parallelism benefit is limited
   - The reader serves one doc at a time anyway
   - Buffer management overhead dominates

4. **State management**: StreamVByte reader has more state:
   ```cpp
   // StreamVByte reader state
   uint32_t docDeltaBuffer_[4];
   uint32_t freqBuffer_[4];
   int bufferPos_;
   int bufferLimit_;
   ```
   vs VInt reader (no buffers needed)

### Performance Breakdown

Estimated time per doc (1000 docs benchmark):

**VInt**:
- Decode: ~9.6 µs / 1000 = 9.6 ns/doc
- Total: 9.6 ns/doc

**StreamVByte**:
- Raw decode: 2.87 µs / 1000 = 2.87 ns/doc (for groups of 4)
- Reader overhead: (14.3 - 2.87) = 11.43 µs / 1000 = 11.43 ns/doc
- Total: 14.3 ns/doc

**Conclusion**: Reader overhead (11.43 ns) is **4× larger** than the raw decode time (2.87 ns)!

## Recommendations

### Option 1: Increase Buffer Size
Buffer 16 or 32 docs instead of 4 to amortize refill overhead:
- Fewer refills: 62× fewer for 1000 docs (16 vs 250)
- Better memory locality
- More SIMD utilization

**Expected improvement**: 30-50% throughput increase

### Option 2: Hybrid Approach
Use StreamVByte for large posting lists (>100 docs), VInt for small lists:
- Small lists: VInt overhead is minimal
- Large lists: Amortized StreamVByte benefits
- Break-even around 50-100 docs

**Expected improvement**: 10-20% average throughput

### Option 3: Inline Buffering
Remove refillBuffer() indirection, inline the decode logic:
- Reduce function call overhead
- Better compiler optimization
- Direct buffer access

**Expected improvement**: 15-25% throughput increase

### Option 4: Keep Current Implementation
Current implementation works correctly but is slower. Acceptable if:
- Code simplicity preferred over performance
- Future optimizations planned (e.g., skip lists)
- Other bottlenecks dominate (disk I/O, query processing)

## Detailed Benchmark Data

### Decode Throughput by Posting List Size

| Size | StreamVByte | VInt | Ratio |
|------|-------------|------|-------|
| 100 docs | 65.7 M/s | 102.4 M/s | 0.64× |
| 1K docs | 70.0 M/s | 104.2 M/s | 0.67× |
| 10K docs | 69.0 M/s | 104.1 M/s | 0.66× |
| 100K docs | 69.0 M/s | 104.0 M/s | 0.66× |

**Observation**: Performance ratio is consistent across sizes (65-67% of VInt throughput)

### Raw Decode Throughput (No Reader)

| Size | StreamVByte | VInt | Ratio |
|------|-------------|------|-------|
| 100 ints | 347.9 M/s | 197.3 M/s | 1.76× |
| 1K ints | 348.2 M/s | 202.3 M/s | 1.72× |
| 10K ints | 348.4 M/s | 202.9 M/s | 1.72× |
| 100K ints | 348.4 M/s | 202.5 M/s | 1.72× |

**Observation**: Raw StreamVByte maintains 1.7× speedup consistently

## Conclusions

1. **StreamVByte SIMD decoding works**: 1.7× faster than VInt in isolation
2. **Current reader implementation is slower**: 1.5× slower than VInt due to overhead
3. **Buffer size is too small**: 4-doc buffers create too many refills
4. **Optimization opportunities exist**: Larger buffers or hybrid approaches could recover performance

## Next Steps

### P1: Optimize Reader (Recommended)
1. Increase buffer size to 16 or 32 docs
2. Inline decode logic to reduce function call overhead
3. Re-benchmark and target parity or better than VInt

### P2: Profile Reader Overhead
1. Use `perf` to identify hot spots
2. Measure impact of buffer management
3. Optimize critical paths

### P3: Real-World Testing
1. Benchmark with actual IndexWriter/IndexReader
2. Measure end-to-end query performance
3. Compare with Apache Lucene Java benchmarks

## References

- StreamVByte paper: "Stream VByte: Faster Byte-Oriented Integer Compression" (Lemire et al., 2017)
- Apache Lucene posting list format: https://lucene.apache.org/core/9_11_0/core/org/apache/lucene/codecs/lucene90/package-summary.html
- Implementation: `/home/ubuntu/diagon/src/core/src/codecs/lucene104/`
