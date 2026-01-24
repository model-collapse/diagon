// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/compression/ICompressionCodec.h"

#include <cstring>
#include <limits>
#include <stdexcept>

#ifdef HAVE_LZ4
#    include <lz4.h>
#endif

#ifdef HAVE_ZSTD
#    include <zstd.h>
#endif

namespace diagon {
namespace compression {

/**
 * No compression codec (identity)
 */
class NoneCodec : public ICompressionCodec {
public:
    std::string getName() const override { return "None"; }

    uint8_t getCodecId() const override { return static_cast<uint8_t>(CodecId::None); }

    size_t compress(const char* source, size_t source_size, char* dest,
                    size_t dest_capacity) const override {
        if (dest_capacity < source_size) {
            throw std::runtime_error("NoneCodec: destination buffer too small");
        }
        std::memcpy(dest, source, source_size);
        return source_size;
    }

    size_t decompress(const char* source, size_t source_size, char* dest,
                      size_t dest_capacity) const override {
        if (dest_capacity < source_size) {
            throw std::runtime_error("NoneCodec: destination buffer too small");
        }
        std::memcpy(dest, source, source_size);
        return source_size;
    }

    size_t getMaxCompressedSize(size_t source_size) const override { return source_size; }

    static CompressionCodecPtr create() { return std::make_shared<NoneCodec>(); }
};

/**
 * LZ4 compression codec
 *
 * Fast compression with decent compression ratio.
 * Based on: ClickHouse LZ4 codec
 */
class LZ4Codec : public ICompressionCodec {
public:
    std::string getName() const override { return "LZ4"; }

    uint8_t getCodecId() const override { return static_cast<uint8_t>(CodecId::LZ4); }

    size_t compress(const char* source, size_t source_size, char* dest,
                    size_t dest_capacity) const override {
#ifdef HAVE_LZ4
        if (source_size == 0) {
            return 0;
        }

        // Cast size_t to int for LZ4 API
        if (source_size > static_cast<size_t>(std::numeric_limits<int>::max())) {
            throw std::runtime_error("LZ4Codec: source size too large");
        }

        int compressed_size = LZ4_compress_default(source, dest, static_cast<int>(source_size),
                                                   static_cast<int>(dest_capacity));

        if (compressed_size <= 0) {
            throw std::runtime_error("LZ4Codec: compression failed");
        }

        return static_cast<size_t>(compressed_size);
#else
        throw std::runtime_error("LZ4Codec: LZ4 library not available");
#endif
    }

    size_t decompress(const char* source, size_t source_size, char* dest,
                      size_t dest_capacity) const override {
#ifdef HAVE_LZ4
        if (source_size == 0) {
            return 0;
        }

        // Cast size_t to int for LZ4 API
        if (source_size > static_cast<size_t>(std::numeric_limits<int>::max()) ||
            dest_capacity > static_cast<size_t>(std::numeric_limits<int>::max())) {
            throw std::runtime_error("LZ4Codec: buffer size too large");
        }

        int decompressed_size = LZ4_decompress_safe(source, dest, static_cast<int>(source_size),
                                                    static_cast<int>(dest_capacity));

        if (decompressed_size < 0) {
            throw std::runtime_error("LZ4Codec: decompression failed");
        }

        return static_cast<size_t>(decompressed_size);
#else
        throw std::runtime_error("LZ4Codec: LZ4 library not available");
#endif
    }

    size_t getMaxCompressedSize(size_t source_size) const override {
#ifdef HAVE_LZ4
        if (source_size > static_cast<size_t>(std::numeric_limits<int>::max())) {
            throw std::runtime_error("LZ4Codec: source size too large");
        }
        return static_cast<size_t>(LZ4_compressBound(static_cast<int>(source_size)));
#else
        // Conservative estimate if LZ4 not available
        return source_size + (source_size / 255) + 16;
#endif
    }

    static CompressionCodecPtr create() {
        return std::make_shared<LZ4Codec>();
    }
};

/**
 * ZSTD compression codec
 *
 * High compression ratio with adjustable compression level.
 * Based on: ClickHouse ZSTD codec
 */
class ZSTDCodec : public ICompressionCodec {
public:
    explicit ZSTDCodec(int level = 3)
        : level_(level) {
        if (level < 1 || level > 22) {
            throw std::runtime_error("ZSTDCodec: invalid compression level (must be 1-22)");
        }
    }

    std::string getName() const override { return "ZSTD"; }

    uint8_t getCodecId() const override { return static_cast<uint8_t>(CodecId::ZSTD); }

    size_t compress(const char* source, size_t source_size, char* dest,
                    size_t dest_capacity) const override {
#ifdef HAVE_ZSTD
        if (source_size == 0) {
            return 0;
        }

        size_t compressed_size = ZSTD_compress(dest, dest_capacity, source, source_size, level_);

        if (ZSTD_isError(compressed_size)) {
            throw std::runtime_error(std::string("ZSTDCodec: compression failed: ") +
                                     ZSTD_getErrorName(compressed_size));
        }

        return compressed_size;
#else
        throw std::runtime_error("ZSTDCodec: ZSTD library not available");
#endif
    }

    size_t decompress(const char* source, size_t source_size, char* dest,
                      size_t dest_capacity) const override {
#ifdef HAVE_ZSTD
        if (source_size == 0) {
            return 0;
        }

        size_t decompressed_size = ZSTD_decompress(dest, dest_capacity, source, source_size);

        if (ZSTD_isError(decompressed_size)) {
            throw std::runtime_error(std::string("ZSTDCodec: decompression failed: ") +
                                     ZSTD_getErrorName(decompressed_size));
        }

        return decompressed_size;
#else
        throw std::runtime_error("ZSTDCodec: ZSTD library not available");
#endif
    }

    size_t getMaxCompressedSize(size_t source_size) const override {
#ifdef HAVE_ZSTD
        return ZSTD_compressBound(source_size);
#else
        // Conservative estimate if ZSTD not available
        return source_size + (source_size / 255) + 16;
#endif
    }

    int getLevel() const override {
        return level_;
    }

    static CompressionCodecPtr create(int level = 3) {
        return std::make_shared<ZSTDCodec>(level);
    }

private:
    int level_;
};

/**
 * Codec factory
 */
class CompressionCodecFactory {
public:
    static CompressionCodecPtr getCodec(const std::string& name) {
        if (name == "None") {
            return NoneCodec::create();
        } else if (name == "LZ4") {
            return LZ4Codec::create();
        } else if (name == "ZSTD") {
            return ZSTDCodec::create();
        } else {
            throw std::runtime_error("Unknown compression codec: " + name);
        }
    }

    static CompressionCodecPtr getCodecById(uint8_t codec_id) {
        switch (static_cast<CodecId>(codec_id)) {
            case CodecId::None:
                return NoneCodec::create();
            case CodecId::LZ4:
                return LZ4Codec::create();
            case CodecId::ZSTD:
                return ZSTDCodec::create();
            default:
                throw std::runtime_error("Unknown compression codec ID: " +
                                         std::to_string(codec_id));
        }
    }

    static CompressionCodecPtr getDefault() {
        return LZ4Codec::create();  // LZ4 is default
    }
};

}  // namespace compression
}  // namespace diagon
