# Compression API Reference

Diagon provides pluggable compression codecs for reducing index size while maintaining fast access times.

## Table of Contents

- [Overview](#overview)
- [CompressionCodec Interface](#compressioncodec-interface)
- [LZ4 Codec](#lz4-codec)
- [ZSTD Codec](#zstd-codec)
- [Choosing a Codec](#choosing-a-codec)
- [Custom Codecs](#custom-codecs)

---

## Overview

Compression in Diagon is designed for:
- **High throughput**: Fast compression/decompression for query performance
- **Adaptive selection**: Different codecs for different data types
- **Pluggable architecture**: Easy to add custom compression schemes

### Compression Architecture

```
┌────────────────────────────────────────────┐
│         CompressionCodec                    │
│         (abstract interface)                │
└────────────────┬───────────────────────────┘
                 │
        ┌────────┴────────┬─────────────┐
        │                 │             │
┌───────▼────────┐ ┌─────▼──────┐ ┌───▼──────────┐
│   LZ4Codec     │ │ ZSTDCodec  │ │ CustomCodec  │
│   (fast)       │ │ (high      │ │ (user-      │
│                │ │  ratio)    │ │  defined)   │
└────────────────┘ └────────────┘ └──────────────┘
```

### Available Codecs

| Codec | Compression Ratio | Speed | Best For |
|-------|------------------|-------|----------|
| **LZ4** | Moderate (2-3×) | Very Fast | Hot data, high QPS queries |
| **ZSTD** | High (3-10×) | Fast | Cold data, analytical workloads |
| **None** | 1× | Fastest | Already compressed data |

---

## CompressionCodec Interface

Base interface for all compression codecs.

### Header

```cpp
#include <diagon/compression/CompressionCodec.h>
```

### Interface

```cpp
namespace diagon::compression {

class CompressionCodec {
public:
    virtual ~CompressionCodec() = default;

    // Get codec name
    virtual std::string name() const = 0;

    // Maximum compressed size
    virtual size_t maxCompressedSize(size_t source_size) const = 0;

    // Compress data
    virtual size_t compress(
        const char* source,
        size_t source_size,
        char* dest,
        size_t dest_capacity
    ) const = 0;

    // Decompress data
    virtual size_t decompress(
        const char* source,
        size_t source_size,
        char* dest,
        size_t dest_capacity
    ) const = 0;
};

}  // namespace diagon::compression
```

### Basic Usage

```cpp
#include <diagon/compression/CompressionCodecs.h>

using namespace diagon::compression;

// Create codec
auto codec = CompressionCodecs::lz4();

// Prepare data
std::string input = "Hello, World! This is test data...";
size_t max_compressed = codec->maxCompressedSize(input.size());
std::vector<char> compressed(max_compressed);

// Compress
size_t compressed_size = codec->compress(
    input.data(), input.size(),
    compressed.data(), compressed.size()
);
compressed.resize(compressed_size);

std::cout << "Compressed from " << input.size()
          << " to " << compressed_size << " bytes ("
          << (100.0 * compressed_size / input.size()) << "%)\n";

// Decompress
std::vector<char> decompressed(input.size());
size_t decompressed_size = codec->decompress(
    compressed.data(), compressed.size(),
    decompressed.data(), decompressed.size()
);

// Verify
assert(decompressed_size == input.size());
assert(std::memcmp(decompressed.data(), input.data(), input.size()) == 0);
```

---

## LZ4 Codec

Fast compression codec optimized for speed over ratio.

### Header

```cpp
#include <diagon/compression/CompressionCodecs.h>
```

### Creating LZ4 Codec

```cpp
// Default LZ4
auto codec = CompressionCodecs::lz4();

// Check availability
#ifdef HAVE_LZ4
    // LZ4 is available
#else
    // LZ4 not compiled in
#endif
```

### Characteristics

**Advantages**:
- ✅ Very fast compression (500+ MB/s)
- ✅ Extremely fast decompression (2-3 GB/s)
- ✅ Low CPU overhead
- ✅ Deterministic compression

**Trade-offs**:
- ❌ Moderate compression ratio (2-3×)
- ❌ Not as space-efficient as ZSTD

### Use Cases

```cpp
// Hot data tier (frequently queried)
auto hot_codec = CompressionCodecs::lz4();

// Posting lists (need fast decompression)
auto postings_codec = CompressionCodecs::lz4();

// Near-real-time indexes (low latency requirement)
auto nrt_codec = CompressionCodecs::lz4();
```

### Performance Example

```cpp
#include <chrono>

auto codec = CompressionCodecs::lz4();

// Generate test data (10 MB)
std::vector<char> data(10 * 1024 * 1024);
std::fill(data.begin(), data.end(), 'A');  // Highly compressible

// Prepare output buffer
size_t max_size = codec->maxCompressedSize(data.size());
std::vector<char> compressed(max_size);

// Benchmark compression
auto start = std::chrono::high_resolution_clock::now();

size_t compressed_size = codec->compress(
    data.data(), data.size(),
    compressed.data(), compressed.size()
);

auto end = std::chrono::high_resolution_clock::now();
auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
    end - start);

double throughput = (data.size() / 1024.0 / 1024.0) /
                   (duration.count() / 1000000.0);

std::cout << "LZ4 Compression:\n";
std::cout << "  Original: " << data.size() << " bytes\n";
std::cout << "  Compressed: " << compressed_size << " bytes\n";
std::cout << "  Ratio: " << (100.0 * compressed_size / data.size()) << "%\n";
std::cout << "  Throughput: " << throughput << " MB/s\n";
```

Expected output:
```
LZ4 Compression:
  Original: 10485760 bytes
  Compressed: 52 bytes
  Ratio: 0.0005%
  Throughput: 520 MB/s
```

---

## ZSTD Codec

High-ratio compression codec with tunable compression levels.

### Header

```cpp
#include <diagon/compression/CompressionCodecs.h>
```

### Creating ZSTD Codec

```cpp
// Default ZSTD (level 3)
auto codec = CompressionCodecs::zstd();

// Custom compression level (1-22)
auto fast_codec = CompressionCodecs::zstd(1);    // Fastest
auto balanced_codec = CompressionCodecs::zstd(3);  // Default
auto high_codec = CompressionCodecs::zstd(10);   // High ratio
auto max_codec = CompressionCodecs::zstd(22);    // Maximum ratio

// Check availability
#ifdef HAVE_ZSTD
    // ZSTD is available
#else
    // ZSTD not compiled in
#endif
```

### Compression Levels

| Level | Speed | Ratio | Use Case |
|-------|-------|-------|----------|
| 1 | Fastest | ~3× | Hot data, low latency |
| 3 | Fast | ~4× | Balanced (default) |
| 5 | Medium | ~5× | Warm data |
| 10 | Slow | ~7× | Cold data |
| 15 | Very Slow | ~8× | Archive |
| 22 | Slowest | ~10× | Long-term storage |

### Characteristics

**Advantages**:
- ✅ Excellent compression ratio (3-10×)
- ✅ Tunable compression/speed trade-off
- ✅ Good decompression speed (~500 MB/s)
- ✅ Dictionary support (future work)

**Trade-offs**:
- ❌ Slower compression than LZ4
- ❌ Higher CPU usage
- ❌ More memory for high levels

### Use Cases

```cpp
// Cold data tier (rarely queried)
auto cold_codec = CompressionCodecs::zstd(10);

// Columnar data (high compression ratio)
auto column_codec = CompressionCodecs::zstd(5);

// Archive tier (maximum space savings)
auto archive_codec = CompressionCodecs::zstd(15);

// Balanced performance
auto balanced_codec = CompressionCodecs::zstd(3);
```

### Level Comparison

```cpp
void compareZSTDLevels() {
    std::vector<char> data = generateTestData(1024 * 1024);  // 1 MB

    for (int level : {1, 3, 5, 10, 15, 22}) {
        auto codec = CompressionCodecs::zstd(level);

        size_t max_size = codec->maxCompressedSize(data.size());
        std::vector<char> compressed(max_size);

        auto start = std::chrono::high_resolution_clock::now();
        size_t compressed_size = codec->compress(
            data.data(), data.size(),
            compressed.data(), compressed.size()
        );
        auto end = std::chrono::high_resolution_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end - start);

        std::cout << "Level " << level << ": "
                  << compressed_size << " bytes ("
                  << (100.0 * compressed_size / data.size()) << "%), "
                  << duration.count() << " ms\n";
    }
}
```

Expected output:
```
Level 1: 52000 bytes (5.0%), 10 ms
Level 3: 42000 bytes (4.0%), 15 ms
Level 5: 35000 bytes (3.4%), 25 ms
Level 10: 28000 bytes (2.7%), 50 ms
Level 15: 24000 bytes (2.3%), 100 ms
Level 22: 20000 bytes (1.9%), 300 ms
```

---

## Choosing a Codec

### Decision Matrix

```cpp
std::unique_ptr<CompressionCodec> selectCodec(
    StorageTier tier,
    DataType type,
    size_t data_size)
{
    // Hot tier - prioritize speed
    if (tier == StorageTier::HOT) {
        return CompressionCodecs::lz4();
    }

    // Warm tier - balanced
    if (tier == StorageTier::WARM) {
        return CompressionCodecs::zstd(3);
    }

    // Cold tier - prioritize space
    if (tier == StorageTier::COLD) {
        return CompressionCodecs::zstd(10);
    }

    // Frozen tier - maximum compression
    if (tier == StorageTier::FROZEN) {
        return CompressionCodecs::zstd(15);
    }

    // Default
    return CompressionCodecs::lz4();
}
```

### Per-Data-Type Recommendations

```cpp
// Posting lists (need fast random access)
auto postings_codec = CompressionCodecs::lz4();

// Columnar data (read large blocks)
auto column_codec = CompressionCodecs::zstd(5);

// Field data (small random reads)
auto field_codec = CompressionCodecs::lz4();

// Doc values (sequential scans)
auto docvalues_codec = CompressionCodecs::zstd(3);

// Stored fields (rare access)
auto stored_codec = CompressionCodecs::zstd(10);
```

### Query Pattern Considerations

```cpp
// High QPS (queries per second) - use LZ4
if (qps > 1000) {
    codec = CompressionCodecs::lz4();
}

// Low QPS, high data volume - use ZSTD
else if (data_size > 100_GB) {
    codec = CompressionCodecs::zstd(10);
}

// Balanced workload
else {
    codec = CompressionCodecs::zstd(3);
}
```

---

## Custom Codecs

You can implement custom compression codecs by extending `CompressionCodec`.

### Example: Delta Encoding Codec

```cpp
#include <diagon/compression/CompressionCodec.h>

namespace myapp {

class DeltaCodec : public diagon::compression::CompressionCodec {
public:
    std::string name() const override {
        return "Delta";
    }

    size_t maxCompressedSize(size_t source_size) const override {
        // Delta encoding doesn't reduce size without additional compression
        return source_size;
    }

    size_t compress(
        const char* source, size_t source_size,
        char* dest, size_t dest_capacity) const override
    {
        if (source_size == 0) return 0;

        // Assume int32 data
        const int32_t* values = reinterpret_cast<const int32_t*>(source);
        int32_t* deltas = reinterpret_cast<int32_t*>(dest);
        size_t count = source_size / sizeof(int32_t);

        // First value unchanged
        deltas[0] = values[0];

        // Compute deltas
        for (size_t i = 1; i < count; i++) {
            deltas[i] = values[i] - values[i-1];
        }

        return source_size;
    }

    size_t decompress(
        const char* source, size_t source_size,
        char* dest, size_t dest_capacity) const override
    {
        if (source_size == 0) return 0;

        const int32_t* deltas = reinterpret_cast<const int32_t*>(source);
        int32_t* values = reinterpret_cast<int32_t*>(dest);
        size_t count = source_size / sizeof(int32_t);

        // First value unchanged
        values[0] = deltas[0];

        // Reconstruct values
        for (size_t i = 1; i < count; i++) {
            values[i] = values[i-1] + deltas[i];
        }

        return source_size;
    }
};

}  // namespace myapp
```

### Example: Chained Compression

Combine multiple codecs for better compression:

```cpp
class ChainedCodec : public CompressionCodec {
private:
    std::unique_ptr<CompressionCodec> first_;
    std::unique_ptr<CompressionCodec> second_;

public:
    ChainedCodec(
        std::unique_ptr<CompressionCodec> first,
        std::unique_ptr<CompressionCodec> second)
        : first_(std::move(first))
        , second_(std::move(second)) {}

    std::string name() const override {
        return first_->name() + "+" + second_->name();
    }

    size_t compress(
        const char* source, size_t source_size,
        char* dest, size_t dest_capacity) const override
    {
        // First compression
        std::vector<char> temp(first_->maxCompressedSize(source_size));
        size_t temp_size = first_->compress(
            source, source_size, temp.data(), temp.size());

        // Second compression
        return second_->compress(
            temp.data(), temp_size, dest, dest_capacity);
    }

    // Similar for decompress...
};

// Usage: Delta + ZSTD for sorted integers
auto codec = std::make_unique<ChainedCodec>(
    std::make_unique<DeltaCodec>(),
    CompressionCodecs::zstd(10)
);
```

---

## Advanced Usage

### Compression Context Reuse

For better performance when compressing many small blocks, reuse compression contexts:

```cpp
// Future API (not yet implemented)
class ZSTDContext {
    ZSTD_CCtx* cctx_;
    ZSTD_DCtx* dctx_;

public:
    ZSTDContext(int level) {
        cctx_ = ZSTD_createCCtx();
        ZSTD_CCtx_setParameter(cctx_, ZSTD_c_compressionLevel, level);
        dctx_ = ZSTD_createDCtx();
    }

    size_t compress(const char* src, size_t src_size,
                   char* dst, size_t dst_capacity) {
        return ZSTD_compress2(cctx_, dst, dst_capacity, src, src_size);
    }

    ~ZSTDContext() {
        ZSTD_freeCCtx(cctx_);
        ZSTD_freeDCtx(dctx_);
    }
};
```

### Dictionary Training

Train dictionaries on sample data for better compression (future work):

```cpp
// Future API
class ZSTDDictionaryCodec : public CompressionCodec {
    std::vector<char> dictionary_;

public:
    static std::vector<char> trainDictionary(
        const std::vector<std::vector<char>>& samples,
        size_t dict_size)
    {
        // Train ZSTD dictionary from samples
        // ...
    }

    ZSTDDictionaryCodec(std::vector<char> dictionary, int level)
        : dictionary_(std::move(dictionary)) {}

    // Use dictionary for compression/decompression
    // ...
};
```

---

## Benchmarking

### Compression Benchmark

```cpp
void benchmarkCodec(const CompressionCodec* codec,
                   const std::vector<char>& data)
{
    const int ITERATIONS = 100;

    // Compression benchmark
    std::vector<char> compressed(codec->maxCompressedSize(data.size()));
    auto comp_start = std::chrono::high_resolution_clock::now();

    size_t compressed_size = 0;
    for (int i = 0; i < ITERATIONS; i++) {
        compressed_size = codec->compress(
            data.data(), data.size(),
            compressed.data(), compressed.size()
        );
    }

    auto comp_end = std::chrono::high_resolution_clock::now();
    auto comp_duration = std::chrono::duration_cast<std::chrono::microseconds>(
        comp_end - comp_start);

    // Decompression benchmark
    std::vector<char> decompressed(data.size());
    auto decomp_start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < ITERATIONS; i++) {
        codec->decompress(
            compressed.data(), compressed_size,
            decompressed.data(), decompressed.size()
        );
    }

    auto decomp_end = std::chrono::high_resolution_clock::now();
    auto decomp_duration = std::chrono::duration_cast<std::chrono::microseconds>(
        decomp_end - decomp_start);

    // Calculate throughput
    double data_mb = data.size() / 1024.0 / 1024.0;
    double comp_throughput = (data_mb * ITERATIONS) /
                            (comp_duration.count() / 1000000.0);
    double decomp_throughput = (data_mb * ITERATIONS) /
                              (decomp_duration.count() / 1000000.0);

    std::cout << codec->name() << " Results:\n";
    std::cout << "  Compression ratio: "
              << (100.0 * compressed_size / data.size()) << "%\n";
    std::cout << "  Compression throughput: "
              << comp_throughput << " MB/s\n";
    std::cout << "  Decompression throughput: "
              << decomp_throughput << " MB/s\n";
}
```

---

## See Also

- [Core API Reference](core.md)
- [Column Storage API](columns.md)
- [Performance Guide](../guides/performance.md)
- [Compression Design Document](../../design/04_COMPRESSION_CODECS.md)
