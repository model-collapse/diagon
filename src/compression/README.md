# Diagon Compression Library

**Module**: Compression codec system
**Based on**: ClickHouse compression framework
**Design References**: Module 04

## Overview

The compression library implements ClickHouse's compression codec system, supporting both generic codecs (LZ4, ZSTD) and type-specific codecs (Delta, Gorilla, DoubleDelta). Codecs can be chained for optimal compression ratios.

## Module Structure

### ICompressionCodec Interface
**Abstract compression interface**

```cpp
class ICompressionCodec {
public:
    virtual ~ICompressionCodec() = default;

    // Compression
    virtual uint32_t compress(const char* source, uint32_t source_size,
                             char* dest) const = 0;

    // Decompression
    virtual uint32_t decompress(const char* source, uint32_t source_size,
                               char* dest, uint32_t uncompressed_size) const = 0;

    // Metadata
    virtual uint8_t getMethodByte() const = 0;
    virtual std::string getCodecDesc() const = 0;

    // Properties
    virtual bool isGenericCompression() const = 0;
    virtual bool isExperimental() const = 0;
};
```

### Generic Codecs
**General-purpose compression**

#### CompressionCodecNone
- **Purpose**: No compression (passthrough)
- **Use case**: Already compressed data, debugging
- **Overhead**: 0 bytes

#### CompressionCodecLZ4
- **Purpose**: Fast compression with decent ratio
- **Algorithm**: LZ4 (Yann Collet)
- **Compression ratio**: 2-3×
- **Speed**: ~500 MB/s compression, ~2000 MB/s decompression
- **Use case**: Default for most data types

#### CompressionCodecZSTD
- **Purpose**: High compression ratio
- **Algorithm**: ZSTD (Facebook)
- **Compression ratio**: 3-5× (level 3), up to 10× (level 22)
- **Speed**: ~200 MB/s compression, ~600 MB/s decompression
- **Use case**: Cold storage, large datasets
- **Configurable levels**: 1 (fast) to 22 (max compression)

### Type-Specific Codecs
**Optimized for specific data patterns**

#### CompressionCodecDelta
- **Purpose**: Encode differences between consecutive values
- **Algorithm**: Store delta values instead of absolute values
- **Compression ratio**: 2-5× for monotonic sequences
- **Use case**: Timestamps, monotonic counters
- **Chaining**: Often followed by LZ4/ZSTD

**Example**:
```
Input:  [100, 101, 102, 103, 104]
Delta:  [100, 1, 1, 1, 1]  // Much more compressible
```

#### CompressionCodecDoubleDelta
- **Purpose**: Encode delta of deltas
- **Algorithm**: Store second-order differences
- **Compression ratio**: 5-10× for linear sequences
- **Use case**: Uniformly spaced timestamps, linear series
- **Chaining**: Often followed by LZ4

**Example**:
```
Input:        [100, 110, 120, 130, 140]
Delta:        [100, 10, 10, 10, 10]
DoubleDelta:  [100, 10, 0, 0, 0]  // Highly compressible
```

#### CompressionCodecGorilla
- **Purpose**: XOR-based compression for floats
- **Algorithm**: Gorilla (Facebook time-series DB)
- **Compression ratio**: 10-20× for smooth series
- **Use case**: Sensor data, stock prices, metrics
- **Reference**: "Gorilla: A Fast, Scalable, In-Memory Time Series Database"

**Algorithm**:
1. XOR consecutive values
2. Count leading/trailing zero bits
3. Encode only changed bits

**Example**:
```
Value 1: 42.5     = 0x4244000000000000
Value 2: 42.5001  = 0x4244000346DC5D64
XOR:              = 0x0000000346DC5D64
Encoding: Store only 40 bits (leading zeros: 24, trailing zeros: 0)
```

#### CompressionCodecT64
- **Purpose**: Transpose bits for better LZ4/ZSTD compression
- **Algorithm**: 64-bit transpose + LZ4/ZSTD
- **Compression ratio**: 1.2-2× improvement over plain LZ4
- **Use case**: Wide bit patterns, sparse integers

### Codec Chaining
**Combine multiple codecs**

```cpp
// Create chained codec: Delta → LZ4
auto codec = CompressionCodecMultiple::create({
    CompressionCodecDelta::create(),
    CompressionCodecLZ4::create()
});

// Compression: Apply codecs in order
uint32_t size = codec->compress(source, source_size, dest);

// Decompression: Apply codecs in reverse order
codec->decompress(source, size, dest, source_size);
```

**Common Chains**:
- `Delta + LZ4`: Timestamps, counters
- `DoubleDelta + LZ4`: Linear sequences
- `Gorilla`: Float time series (no chaining needed)
- `T64 + ZSTD`: Sparse integers

### Compressed Block Format
**16-byte header + compressed data**

```cpp
struct CompressedBlockHeader {
    uint8_t  method;              // Codec method byte
    uint32_t compressed_size;     // Size of compressed data
    uint32_t uncompressed_size;   // Original size
    uint64_t checksum;            // CityHash64 checksum
};
```

**Total block format**:
```
[Header: 16 bytes][Compressed data: compressed_size bytes]
```

### CompressionFactory
**Codec registration and creation**

```cpp
class CompressionFactory {
public:
    // Register codec
    static void registerCodec(
        uint8_t method_byte,
        std::function<std::unique_ptr<ICompressionCodec>()> factory
    );

    // Create codec
    static std::unique_ptr<ICompressionCodec> create(uint8_t method_byte);
    static std::unique_ptr<ICompressionCodec> createFromString(const std::string& desc);

    // Default codec
    static std::unique_ptr<ICompressionCodec> getDefault();  // LZ4
};
```

### Compressed I/O
**Buffered reading and writing**

#### CompressedReadBuffer
```cpp
class CompressedReadBuffer {
    // Read compressed block
    bool nextBlock();

    // Access decompressed data
    const char* position() const;
    size_t available() const;
};
```

#### CompressedWriteBuffer
```cpp
class CompressedWriteBuffer {
    // Write data (auto-compresses when buffer full)
    void write(const char* data, size_t size);

    // Flush current block
    void flush();
};
```

## Implementation Status

### Completed
- [ ] ICompressionCodec interface
- [ ] CompressionFactory

### In Progress
- [ ] LZ4 codec
- [ ] ZSTD codec
- [ ] Delta codec

### TODO
- [ ] DoubleDelta codec
- [ ] Gorilla codec
- [ ] T64 codec
- [ ] Codec chaining (CompressionCodecMultiple)
- [ ] Compressed I/O buffers

## Codec Selection Guidelines

### Data Type Recommendations

| Data Type | Recommended Codec | Rationale |
|-----------|------------------|-----------|
| Timestamps | Delta + LZ4 | Monotonic, small deltas |
| Counters | Delta + LZ4 | Monotonic, incremental |
| IDs (sequential) | Delta + LZ4 | Monotonic sequence |
| IDs (random) | LZ4 | No pattern |
| Floats (smooth) | Gorilla | XOR-based, high ratio |
| Floats (random) | ZSTD | Generic compression |
| Strings | LZ4 or ZSTD | Dictionary compression |
| Booleans | LZ4 | RLE patterns |
| Enum/Categories | LZ4 | Repetitive values |

### Performance vs Ratio Trade-offs

| Codec | Compression Speed | Decompression Speed | Compression Ratio |
|-------|------------------|---------------------|-------------------|
| None | N/A | N/A | 1.0× |
| LZ4 | 500 MB/s | 2000 MB/s | 2-3× |
| ZSTD (level 3) | 200 MB/s | 600 MB/s | 3-5× |
| ZSTD (level 10) | 50 MB/s | 600 MB/s | 5-8× |
| Delta + LZ4 | 300 MB/s | 1500 MB/s | 3-6× |
| Gorilla | 400 MB/s | 800 MB/s | 10-20× |

## Testing

### Unit Tests
- `CompressionCodecTest`: All codec correctness
- `DeltaCodecTest`: Delta encoding edge cases
- `GorillaCodecTest`: Float compression accuracy
- `CodecChainingTest`: Multiple codec composition

### Benchmarks
- Compression/decompression throughput
- Compression ratios by data type
- Memory usage during compression

## References

### Design Documents
- `design/04_COMPRESSION_CODECS.md`: Codec architecture

### ClickHouse Source Code
- `ClickHouse/src/Compression/ICompressionCodec.h`
- `ClickHouse/src/Compression/CompressionCodecDelta.cpp`
- `ClickHouse/src/Compression/CompressionCodecGorilla.cpp`

### Papers
- Gorilla: "Gorilla: A Fast, Scalable, In-Memory Time Series Database" (Facebook, 2015)
- LZ4: https://github.com/lz4/lz4
- ZSTD: https://github.com/facebook/zstd

---

**Last Updated**: 2026-01-24
**Status**: Initial structure created, implementation in progress
