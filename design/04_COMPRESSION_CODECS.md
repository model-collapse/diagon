# Compression Codecs Design
## Based on ClickHouse Compression System

Source references:
- `ClickHouse/src/Compression/ICompressionCodec.h`
- `ClickHouse/src/Compression/CompressionCodecLZ4.cpp`
- `ClickHouse/src/Compression/CompressionCodecZSTD.cpp`
- `ClickHouse/src/Compression/CompressionCodecDelta.cpp`
- `ClickHouse/src/Compression/CompressionCodecGorilla.cpp`
- `ClickHouse/src/Compression/CompressionFactory.h`
- `ClickHouse/src/Compression/CompressionInfo.h`

## Overview

ClickHouse's compression system provides:
- **Generic codecs**: LZ4, ZSTD work on any data
- **Type-specific codecs**: Delta, Gorilla, T64 optimize for specific data patterns
- **Codec chaining**: Multiple codecs in sequence (e.g., Delta + LZ4)
- **Compressed block format**: 16-byte header + compressed data
- **Factory registration**: Plugin-style codec discovery

## ICompressionCodec Interface

```cpp
/**
 * Base interface for compression codecs.
 *
 * Each codec has a method byte for identification.
 * Codecs can be chained using CompressionCodecMultiple.
 *
 * Based on: ClickHouse ICompressionCodec
 */
class ICompressionCodec {
public:
    virtual ~ICompressionCodec() = default;

    // ==================== Identification ====================

    /**
     * Method byte for codec identification
     * Used in compressed block header
     */
    virtual uint8_t getMethodByte() const = 0;

    /**
     * Codec description (e.g., "LZ4", "CODEC(Delta, ZSTD)")
     */
    virtual std::string getCodecDesc() const = 0;

    // ==================== Compression ====================

    /**
     * Compress data
     * @param source Source data
     * @param source_size Size of source in bytes
     * @param dest Destination buffer (must have enough space)
     * @return Number of compressed bytes written
     */
    virtual uint32_t doCompressData(const char* source, uint32_t source_size,
                                    char* dest) const = 0;

    /**
     * Decompress data
     * @param source Compressed data
     * @param source_size Size of compressed data
     * @param dest Destination buffer
     * @param uncompressed_size Expected uncompressed size
     * @return Number of decompressed bytes (should equal uncompressed_size)
     */
    virtual void doDecompressData(const char* source, uint32_t source_size,
                                  char* dest, uint32_t uncompressed_size) const = 0;

    // ==================== Properties ====================

    /**
     * Is this a compression codec (vs encryption, etc.)?
     */
    virtual bool isCompression() const = 0;

    /**
     * Is this a generic compression (vs type-specific)?
     * Generic codecs work on any data type.
     */
    virtual bool isGenericCompression() const = 0;

    /**
     * Is this an encryption codec?
     */
    virtual bool isEncryption() const {
        return false;
    }

    /**
     * Is this a delta compression codec?
     */
    virtual bool isDeltaCompression() const {
        return false;
    }

    // ==================== Utilities ====================

    /**
     * Compress with header
     * Writes 16-byte CompressedBlockHeader + compressed data
     */
    uint32_t compress(const char* source, uint32_t source_size,
                     char* dest) const;

    /**
     * Decompress with header validation
     * Reads and validates CompressedBlockHeader, then decompresses
     */
    void decompress(const char* source, uint32_t source_size,
                   char* dest, uint32_t uncompressed_size) const;

    /**
     * Get compressed size bound (worst case)
     */
    virtual uint32_t getMaxCompressedDataSize(uint32_t uncompressed_size) const;

    /**
     * Get compression level (if applicable)
     */
    virtual uint8_t getCompressionLevel() const {
        return 0;
    }

protected:
    /**
     * Read compressed block header and validate
     */
    static void validateCompressedBlockHeader(const char* source,
                                              uint32_t source_size,
                                              uint32_t uncompressed_size);
};

using CompressionCodecPtr = std::shared_ptr<ICompressionCodec>;
```

## Compression Method Bytes

```cpp
/**
 * Method byte identifies codec in compressed data.
 *
 * Based on: ClickHouse CompressionMethodByte
 */
enum class CompressionMethodByte : uint8_t {
    NONE            = 0x02,  // No compression
    LZ4             = 0x82,  // LZ4 (default, fast)
    ZSTD            = 0x90,  // ZSTD (better ratio)
    Multiple        = 0x91,  // Codec chain
    Delta           = 0x92,  // Delta encoding
    T64             = 0x93,  // Floating-point transposition
    DoubleDelta     = 0x94,  // Double delta (time series)
    Gorilla         = 0x95,  // Gorilla (float time series)
    AES_128_GCM_SIV = 0x96,  // Encryption
    AES_256_GCM_SIV = 0x97,  // Encryption
    FPC             = 0x98,  // Floating-point compression
    GCD             = 0x9a   // GCD compression
};
```

## Compressed Block Format

```cpp
/**
 * Compressed block header (25 bytes total)
 *
 * Format:
 * [16 bytes] CityHash128 checksum of (method + compressed_size + uncompressed_size + compressed_data)
 * [1 byte]   Compression method byte
 * [4 bytes]  Compressed size (little endian)
 * [4 bytes]  Uncompressed size (little endian)
 * [N bytes]  Compressed data
 */
struct CompressedBlockHeader {
    uint8_t checksum[16];           // CityHash128
    uint8_t method;                 // CompressionMethodByte
    uint32_t compressed_size;       // LE
    uint32_t uncompressed_size;     // LE

    static constexpr size_t HEADER_SIZE = 25;

    void write(WriteBuffer& out) const;
    static CompressedBlockHeader read(ReadBuffer& in);
    void validate(const char* compressed_data, uint32_t data_size) const;
};
```

## Generic Compression Codecs

### LZ4 Codec

```cpp
/**
 * LZ4 compression codec (default)
 *
 * Fast compression with good ratio.
 * Default codec in ClickHouse.
 *
 * Based on: CompressionCodecLZ4
 */
class CompressionCodecLZ4 : public ICompressionCodec {
public:
    CompressionCodecLZ4() = default;

    explicit CompressionCodecLZ4(int level) : level_(level) {}

    uint8_t getMethodByte() const override {
        return static_cast<uint8_t>(CompressionMethodByte::LZ4);
    }

    std::string getCodecDesc() const override {
        return "LZ4";
    }

    uint32_t doCompressData(const char* source, uint32_t source_size,
                           char* dest) const override {
        return LZ4_compress_default(source, dest, source_size,
                                    LZ4_compressBound(source_size));
    }

    void doDecompressData(const char* source, uint32_t source_size,
                         char* dest, uint32_t uncompressed_size) const override {
        int res = LZ4_decompress_safe(source, dest, source_size, uncompressed_size);
        if (res < 0) {
            throw CompressionException("LZ4 decompression failed");
        }
    }

    bool isCompression() const override {
        return true;
    }

    bool isGenericCompression() const override {
        return true;
    }

    uint32_t getMaxCompressedDataSize(uint32_t uncompressed_size) const override {
        return LZ4_compressBound(uncompressed_size);
    }

private:
    int level_{0};  // 0 = default, 1-9 for LZ4HC
};
```

### ZSTD Codec

```cpp
/**
 * ZSTD compression codec
 *
 * Better compression ratio than LZ4, slower compression.
 * Fast decompression (similar to LZ4).
 *
 * Based on: CompressionCodecZSTD
 */
class CompressionCodecZSTD : public ICompressionCodec {
public:
    explicit CompressionCodecZSTD(int level = 3) : level_(level) {}

    uint8_t getMethodByte() const override {
        return static_cast<uint8_t>(CompressionMethodByte::ZSTD);
    }

    std::string getCodecDesc() const override {
        return "ZSTD(" + std::to_string(level_) + ")";
    }

    uint32_t doCompressData(const char* source, uint32_t source_size,
                           char* dest) const override {
        size_t compressed = ZSTD_compress(dest, ZSTD_compressBound(source_size),
                                         source, source_size, level_);
        if (ZSTD_isError(compressed)) {
            throw CompressionException("ZSTD compression failed: " +
                                      std::string(ZSTD_getErrorName(compressed)));
        }
        return compressed;
    }

    void doDecompressData(const char* source, uint32_t source_size,
                         char* dest, uint32_t uncompressed_size) const override {
        size_t res = ZSTD_decompress(dest, uncompressed_size, source, source_size);
        if (ZSTD_isError(res)) {
            throw CompressionException("ZSTD decompression failed: " +
                                      std::string(ZSTD_getErrorName(res)));
        }
    }

    bool isCompression() const override {
        return true;
    }

    bool isGenericCompression() const override {
        return true;
    }

    uint32_t getMaxCompressedDataSize(uint32_t uncompressed_size) const override {
        return ZSTD_compressBound(uncompressed_size);
    }

    uint8_t getCompressionLevel() const override {
        return level_;
    }

private:
    int level_;  // 1-22, default 3
};
```

## Type-Specific Codecs

### Delta Codec

```cpp
/**
 * Delta encoding codec
 *
 * Stores differences between consecutive values.
 * Effective for sorted or slowly changing data.
 *
 * Works with: integer types (int8 to int64, uint8 to uint64)
 *
 * Based on: CompressionCodecDelta
 */
class CompressionCodecDelta : public ICompressionCodec {
public:
    explicit CompressionCodecDelta(size_t data_bytes_size)
        : data_bytes_size_(data_bytes_size) {}

    uint8_t getMethodByte() const override {
        return static_cast<uint8_t>(CompressionMethodByte::Delta);
    }

    std::string getCodecDesc() const override {
        return "Delta(" + std::to_string(data_bytes_size_) + ")";
    }

    uint32_t doCompressData(const char* source, uint32_t source_size,
                           char* dest) const override {
        // Write delta byte size
        dest[0] = data_bytes_size_;

        // Compute deltas
        switch (data_bytes_size_) {
            case 1: return compressDataForType<uint8_t>(source, source_size, dest + 1);
            case 2: return compressDataForType<uint16_t>(source, source_size, dest + 1);
            case 4: return compressDataForType<uint32_t>(source, source_size, dest + 1);
            case 8: return compressDataForType<uint64_t>(source, source_size, dest + 1);
            default:
                throw CompressionException("Invalid delta size: " +
                                          std::to_string(data_bytes_size_));
        }
    }

    void doDecompressData(const char* source, uint32_t source_size,
                         char* dest, uint32_t uncompressed_size) const override {
        uint8_t delta_bytes = source[0];

        switch (delta_bytes) {
            case 1: decompressDataForType<uint8_t>(source + 1, source_size - 1,
                                                   dest, uncompressed_size);
                    break;
            case 2: decompressDataForType<uint16_t>(source + 1, source_size - 1,
                                                    dest, uncompressed_size);
                    break;
            case 4: decompressDataForType<uint32_t>(source + 1, source_size - 1,
                                                    dest, uncompressed_size);
                    break;
            case 8: decompressDataForType<uint64_t>(source + 1, source_size - 1,
                                                    dest, uncompressed_size);
                    break;
            default:
                throw CompressionException("Invalid delta size");
        }
    }

    bool isCompression() const override {
        return true;
    }

    bool isGenericCompression() const override {
        return false;  // Type-specific
    }

    bool isDeltaCompression() const override {
        return true;
    }

private:
    size_t data_bytes_size_;  // 1, 2, 4, or 8

    template <typename T>
    uint32_t compressDataForType(const char* source, uint32_t source_size,
                                 char* dest) const {
        const T* src_typed = reinterpret_cast<const T*>(source);
        T* dest_typed = reinterpret_cast<T*>(dest);
        size_t count = source_size / sizeof(T);

        if (count == 0) return 0;

        // First value unchanged
        dest_typed[0] = src_typed[0];

        // Store deltas
        for (size_t i = 1; i < count; ++i) {
            dest_typed[i] = src_typed[i] - src_typed[i - 1];
        }

        return source_size;  // Same size (will be compressed further)
    }

    template <typename T>
    void decompressDataForType(const char* source, uint32_t source_size,
                              char* dest, uint32_t uncompressed_size) const {
        const T* src_typed = reinterpret_cast<const T*>(source);
        T* dest_typed = reinterpret_cast<T*>(dest);
        size_t count = uncompressed_size / sizeof(T);

        if (count == 0) return;

        // First value unchanged
        dest_typed[0] = src_typed[0];

        // Reconstruct from deltas
        for (size_t i = 1; i < count; ++i) {
            dest_typed[i] = dest_typed[i - 1] + src_typed[i];
        }
    }
};
```

### Gorilla Codec

```cpp
/**
 * Gorilla compression codec
 *
 * Optimized for time-series floating-point data.
 * Uses XOR encoding with leading/trailing zero compression.
 *
 * Reference: "Gorilla: A Fast, Scalable, In-Memory Time Series Database"
 *
 * Based on: CompressionCodecGorilla
 */
class CompressionCodecGorilla : public ICompressionCodec {
public:
    explicit CompressionCodecGorilla(size_t data_bytes_size)
        : data_bytes_size_(data_bytes_size) {}

    uint8_t getMethodByte() const override {
        return static_cast<uint8_t>(CompressionMethodByte::Gorilla);
    }

    std::string getCodecDesc() const override {
        return "Gorilla";
    }

    uint32_t doCompressData(const char* source, uint32_t source_size,
                           char* dest) const override {
        dest[0] = data_bytes_size_;

        if (data_bytes_size_ == 4) {
            return compressDataForType<float>(source, source_size, dest + 1) + 1;
        } else if (data_bytes_size_ == 8) {
            return compressDataForType<double>(source, source_size, dest + 1) + 1;
        }

        throw CompressionException("Gorilla only supports float32/float64");
    }

    void doDecompressData(const char* source, uint32_t source_size,
                         char* dest, uint32_t uncompressed_size) const override {
        uint8_t bytes = source[0];

        if (bytes == 4) {
            decompressDataForType<float>(source + 1, source_size - 1,
                                        dest, uncompressed_size);
        } else if (bytes == 8) {
            decompressDataForType<double>(source + 1, source_size - 1,
                                         dest, uncompressed_size);
        } else {
            throw CompressionException("Invalid Gorilla data size");
        }
    }

    bool isCompression() const override {
        return true;
    }

    bool isGenericCompression() const override {
        return false;  // Float-specific
    }

private:
    size_t data_bytes_size_;

    template <typename T>
    uint32_t compressDataForType(const char* source, uint32_t source_size,
                                 char* dest) const {
        using UInt = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;

        const T* src = reinterpret_cast<const T*>(source);
        size_t count = source_size / sizeof(T);

        BitWriter writer(dest);

        if (count == 0) return 0;

        // Write first value as-is
        UInt prev_bits = std::bit_cast<UInt>(src[0]);
        writer.writeBits(prev_bits, sizeof(T) * 8);

        // Gorilla encoding for subsequent values
        uint8_t prev_leading_zeros = 0;
        uint8_t prev_trailing_zeros = 0;

        for (size_t i = 1; i < count; ++i) {
            UInt curr_bits = std::bit_cast<UInt>(src[i]);
            UInt xor_value = prev_bits ^ curr_bits;

            if (xor_value == 0) {
                // Same value: write 0 bit
                writer.writeBit(0);
            } else {
                writer.writeBit(1);

                uint8_t leading = countLeadingZeros(xor_value);
                uint8_t trailing = countTrailingZeros(xor_value);

                if (leading >= prev_leading_zeros &&
                    trailing >= prev_trailing_zeros) {
                    // Use previous block
                    writer.writeBit(0);
                    uint8_t significant_bits = sizeof(T) * 8 - prev_leading_zeros -
                                              prev_trailing_zeros;
                    UInt significant = xor_value >> prev_trailing_zeros;
                    writer.writeBits(significant, significant_bits);
                } else {
                    // New block
                    writer.writeBit(1);
                    writer.writeBits(leading, 5);  // 5 bits for leading zeros
                    uint8_t significant_bits = sizeof(T) * 8 - leading - trailing;
                    writer.writeBits(significant_bits, 6);  // 6 bits for length
                    UInt significant = xor_value >> trailing;
                    writer.writeBits(significant, significant_bits);

                    prev_leading_zeros = leading;
                    prev_trailing_zeros = trailing;
                }
            }

            prev_bits = curr_bits;
        }

        return writer.finish();
    }

    template <typename T>
    void decompressDataForType(const char* source, uint32_t source_size,
                              char* dest, uint32_t uncompressed_size) const {
        using UInt = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;

        T* dest_typed = reinterpret_cast<T*>(dest);
        size_t count = uncompressed_size / sizeof(T);

        BitReader reader(source);

        if (count == 0) return;

        // Read first value
        UInt prev_bits = reader.readBits(sizeof(T) * 8);
        dest_typed[0] = std::bit_cast<T>(prev_bits);

        uint8_t prev_leading_zeros = 0;
        uint8_t prev_trailing_zeros = 0;

        for (size_t i = 1; i < count; ++i) {
            if (reader.readBit() == 0) {
                // Same value
                dest_typed[i] = dest_typed[i - 1];
            } else {
                UInt xor_value;

                if (reader.readBit() == 0) {
                    // Use previous block
                    uint8_t significant_bits = sizeof(T) * 8 - prev_leading_zeros -
                                              prev_trailing_zeros;
                    UInt significant = reader.readBits(significant_bits);
                    xor_value = significant << prev_trailing_zeros;
                } else {
                    // New block
                    uint8_t leading = reader.readBits(5);
                    uint8_t significant_bits = reader.readBits(6);
                    uint8_t trailing = sizeof(T) * 8 - leading - significant_bits;
                    UInt significant = reader.readBits(significant_bits);
                    xor_value = significant << trailing;

                    prev_leading_zeros = leading;
                    prev_trailing_zeros = trailing;
                }

                UInt curr_bits = prev_bits ^ xor_value;
                dest_typed[i] = std::bit_cast<T>(curr_bits);
                prev_bits = curr_bits;
            }
        }
    }
};
```

### DoubleDelta Codec

```cpp
/**
 * Double delta codec
 *
 * Stores second-order differences: delta of deltas.
 * Effective for monotonic sequences (timestamps, counters).
 *
 * Based on: CompressionCodecDoubleDelta
 */
class CompressionCodecDoubleDelta : public ICompressionCodec {
public:
    explicit CompressionCodecDoubleDelta(size_t data_bytes_size)
        : data_bytes_size_(data_bytes_size) {}

    uint8_t getMethodByte() const override {
        return static_cast<uint8_t>(CompressionMethodByte::DoubleDelta);
    }

    std::string getCodecDesc() const override {
        return "DoubleDelta";
    }

    // Implementation similar to Delta but stores delta-of-deltas
    // ...
};
```

## Codec Chaining (Multiple)

```cpp
/**
 * Codec chain: apply multiple codecs in sequence
 *
 * Example: CODEC(Delta, LZ4)
 * - First apply Delta encoding
 * - Then compress with LZ4
 *
 * Based on: CompressionCodecMultiple
 */
class CompressionCodecMultiple : public ICompressionCodec {
public:
    explicit CompressionCodecMultiple(std::vector<CompressionCodecPtr> codecs)
        : codecs_(std::move(codecs)) {}

    uint8_t getMethodByte() const override {
        return static_cast<uint8_t>(CompressionMethodByte::Multiple);
    }

    std::string getCodecDesc() const override {
        std::string desc = "CODEC(";
        for (size_t i = 0; i < codecs_.size(); ++i) {
            if (i > 0) desc += ", ";
            desc += codecs_[i]->getCodecDesc();
        }
        desc += ")";
        return desc;
    }

    uint32_t doCompressData(const char* source, uint32_t source_size,
                           char* dest) const override {
        // Write number of codecs
        dest[0] = static_cast<uint8_t>(codecs_.size());

        // Apply codecs in sequence
        std::vector<char> buffer1(source, source + source_size);
        std::vector<char> buffer2;

        const char* input = buffer1.data();
        uint32_t input_size = source_size;

        for (const auto& codec : codecs_) {
            // Write codec method byte
            // Compress
            buffer2.resize(codec->getMaxCompressedDataSize(input_size));
            uint32_t compressed = codec->doCompressData(input, input_size,
                                                        buffer2.data());
            buffer2.resize(compressed);
            std::swap(buffer1, buffer2);
            input = buffer1.data();
            input_size = buffer1.size();
        }

        // Copy final result
        std::memcpy(dest + 1, input, input_size);
        return input_size + 1;
    }

    void doDecompressData(const char* source, uint32_t source_size,
                         char* dest, uint32_t uncompressed_size) const override {
        uint8_t num_codecs = source[0];

        // Apply codecs in reverse order
        std::vector<char> buffer1(source + 1, source + source_size);
        std::vector<char> buffer2;

        for (auto it = codecs_.rbegin(); it != codecs_.rend(); ++it) {
            buffer2.resize(uncompressed_size);  // Worst case
            (*it)->doDecompressData(buffer1.data(), buffer1.size(),
                                   buffer2.data(), uncompressed_size);
            std::swap(buffer1, buffer2);
        }

        std::memcpy(dest, buffer1.data(), uncompressed_size);
    }

    bool isCompression() const override {
        return true;
    }

    bool isGenericCompression() const override {
        return std::all_of(codecs_.begin(), codecs_.end(),
            [](const auto& c) { return c->isGenericCompression(); });
    }

private:
    std::vector<CompressionCodecPtr> codecs_;
};
```

## CompressionFactory

```cpp
/**
 * Factory for creating compression codecs
 *
 * Based on: ClickHouse CompressionFactory
 */
class CompressionFactory {
public:
    using Creator = std::function<CompressionCodecPtr(const ASTPtr&)>;
    using CreatorWithType = std::function<CompressionCodecPtr(const ASTPtr&,
                                                               const IDataType*)>;

    static CompressionFactory& instance() {
        static CompressionFactory factory;
        return factory;
    }

    /**
     * Get default codec (LZ4)
     */
    CompressionCodecPtr getDefaultCodec() const {
        return std::make_shared<CompressionCodecLZ4>();
    }

    /**
     * Get codec from AST
     * Example: CODEC(Delta, LZ4) or CODEC(ZSTD(3))
     */
    CompressionCodecPtr get(const ASTPtr& ast,
                           const IDataType* column_type = nullptr,
                           bool only_generic = false) const {
        // Parse AST and create codec(s)
        // ...
    }

    /**
     * Get codec by method byte
     */
    CompressionCodecPtr get(uint8_t method_byte) const {
        auto it = codecs_by_byte_.find(method_byte);
        if (it == codecs_by_byte_.end()) {
            throw Exception("Unknown compression method: " +
                          std::to_string(method_byte));
        }
        return it->second();
    }

    /**
     * Register codec
     */
    void registerCompressionCodec(const std::string& family_name,
                                  CreatorWithType creator) {
        creators_[family_name] = creator;
    }

private:
    std::unordered_map<std::string, CreatorWithType> creators_;
    std::unordered_map<uint8_t, std::function<CompressionCodecPtr()>> codecs_by_byte_;

    CompressionFactory() {
        // Register built-in codecs
        registerBuiltinCodecs();
    }

    void registerBuiltinCodecs() {
        // LZ4
        registerCompressionCodec("LZ4", [](const ASTPtr&, const IDataType*) {
            return std::make_shared<CompressionCodecLZ4>();
        });

        // ZSTD
        registerCompressionCodec("ZSTD", [](const ASTPtr& ast, const IDataType*) {
            int level = 3;  // Default
            // Parse level from AST if present
            return std::make_shared<CompressionCodecZSTD>(level);
        });

        // Delta
        registerCompressionCodec("Delta", [](const ASTPtr&, const IDataType* type) {
            if (!type) {
                throw Exception("Delta codec requires type information");
            }
            size_t bytes = type->getSizeOfValueInMemory();
            return std::make_shared<CompressionCodecDelta>(bytes);
        });

        // Gorilla
        registerCompressionCodec("Gorilla", [](const ASTPtr&, const IDataType* type) {
            if (!type || !type->isValueRepresentedByNumber()) {
                throw Exception("Gorilla codec requires numeric type");
            }
            size_t bytes = type->getSizeOfValueInMemory();
            return std::make_shared<CompressionCodecGorilla>(bytes);
        });

        // DoubleDelta
        registerCompressionCodec("DoubleDelta", [](const ASTPtr&, const IDataType* type) {
            size_t bytes = type->getSizeOfValueInMemory();
            return std::make_shared<CompressionCodecDoubleDelta>(bytes);
        });

        // Register by method byte
        codecs_by_byte_[0x82] = []() {
            return std::make_shared<CompressionCodecLZ4>();
        };
        codecs_by_byte_[0x90] = []() {
            return std::make_shared<CompressionCodecZSTD>();
        };
        // ... register all method bytes
    }
};
```

## Usage Examples

```cpp
// Get default codec (LZ4)
auto codec = CompressionFactory::instance().getDefaultCodec();

// Compress data
std::vector<char> source_data = {...};
std::vector<char> compressed(codec->getMaxCompressedDataSize(source_data.size()));

uint32_t compressed_size = codec->compress(
    source_data.data(), source_data.size(),
    compressed.data()
);

// Decompress
std::vector<char> decompressed(source_data.size());
codec->decompress(
    compressed.data(), compressed_size,
    decompressed.data(), source_data.size()
);

// Use type-specific codec chain
auto delta_codec = std::make_shared<CompressionCodecDelta>(4);  // int32
auto lz4_codec = std::make_shared<CompressionCodecLZ4>();
auto chained = std::make_shared<CompressionCodecMultiple>(
    std::vector{delta_codec, lz4_codec}
);

// Codec selection based on data type
DataTypeInt32 int_type;
auto int_codec = CompressionFactory::instance().get(
    parse("CODEC(Delta, LZ4)"),
    &int_type
);

DataTypeFloat64 float_type;
auto float_codec = CompressionFactory::instance().get(
    parse("CODEC(Gorilla, ZSTD(3))"),
    &float_type
);
```

## Integration with Column Storage

```cpp
// When writing columns, select codec based on type
void writeColumn(const IColumn& column, const IDataType& type,
                WriteBuffer& out) {
    // Get appropriate codec
    auto codec = CompressionFactory::instance().get(
        column_codec_ast,
        &type,
        only_generic_codecs
    );

    // Serialize column to buffer
    std::vector<char> uncompressed;
    WriteBufferFromVector uncompressed_buf(uncompressed);
    type.getDefaultSerialization()->serializeBinaryBulk(
        column, uncompressed_buf, 0, column.size()
    );

    // Compress and write
    std::vector<char> compressed(codec->getMaxCompressedDataSize(uncompressed.size()));
    uint32_t size = codec->compress(
        uncompressed.data(), uncompressed.size(),
        compressed.data()
    );

    out.write(compressed.data(), size);
}
```
