// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace diagon {
namespace compression {

/**
 * ICompressionCodec provides compression/decompression interface.
 *
 * Based on: ClickHouse ICompressionCodec
 *
 * Supported codecs:
 * - LZ4: Fast compression (default)
 * - ZSTD: High compression ratio
 * - None: No compression
 */
class ICompressionCodec {
public:
    virtual ~ICompressionCodec() = default;

    /**
     * Codec name (e.g., "LZ4", "ZSTD")
     */
    virtual std::string getName() const = 0;

    /**
     * Codec ID byte (for file headers)
     */
    virtual uint8_t getCodecId() const = 0;

    /**
     * Compress data
     * @param source Source buffer
     * @param source_size Source size in bytes
     * @param dest Destination buffer (must be pre-allocated)
     * @param dest_capacity Destination capacity
     * @return Compressed size in bytes
     */
    virtual size_t compress(
        const char* source,
        size_t source_size,
        char* dest,
        size_t dest_capacity) const = 0;

    /**
     * Decompress data
     * @param source Compressed buffer
     * @param source_size Compressed size
     * @param dest Destination buffer (must be pre-allocated)
     * @param dest_capacity Destination capacity
     * @return Decompressed size in bytes
     */
    virtual size_t decompress(
        const char* source,
        size_t source_size,
        char* dest,
        size_t dest_capacity) const = 0;

    /**
     * Get maximum compressed size for given input
     * Used to allocate compression buffer
     */
    virtual size_t getMaxCompressedSize(size_t source_size) const = 0;

    /**
     * Get compression level (1-9, where higher = better compression)
     * Returns 0 if not applicable
     */
    virtual int getLevel() const {
        return 0;
    }
};

using CompressionCodecPtr = std::shared_ptr<const ICompressionCodec>;

/**
 * Codec IDs (for file format)
 */
enum class CodecId : uint8_t {
    None = 0x00,
    LZ4 = 0x01,
    ZSTD = 0x02,
    LZ4HC = 0x03,
};

}  // namespace compression
}  // namespace diagon
