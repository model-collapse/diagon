// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/compression/ICompressionCodec.h"

#include <cstring>
#include <stdexcept>

namespace diagon {
namespace compression {

/**
 * No compression codec (identity)
 */
class NoneCodec : public ICompressionCodec {
public:
    std::string getName() const override {
        return "None";
    }

    uint8_t getCodecId() const override {
        return static_cast<uint8_t>(CodecId::None);
    }

    size_t compress(
        const char* source,
        size_t source_size,
        char* dest,
        size_t dest_capacity) const override {
        if (dest_capacity < source_size) {
            throw std::runtime_error("NoneCodec: destination buffer too small");
        }
        std::memcpy(dest, source, source_size);
        return source_size;
    }

    size_t decompress(
        const char* source,
        size_t source_size,
        char* dest,
        size_t dest_capacity) const override {
        if (dest_capacity < source_size) {
            throw std::runtime_error("NoneCodec: destination buffer too small");
        }
        std::memcpy(dest, source, source_size);
        return source_size;
    }

    size_t getMaxCompressedSize(size_t source_size) const override {
        return source_size;
    }

    static CompressionCodecPtr create() {
        return std::make_shared<NoneCodec>();
    }
};

/**
 * LZ4 compression codec (stub - requires lz4 library)
 *
 * NOTE: This is a stub implementation. Full implementation requires:
 * - Link against lz4 library
 * - Include lz4.h
 * - Implement actual compression calls
 */
class LZ4Codec : public ICompressionCodec {
public:
    std::string getName() const override {
        return "LZ4";
    }

    uint8_t getCodecId() const override {
        return static_cast<uint8_t>(CodecId::LZ4);
    }

    size_t compress(
        const char* source,
        size_t source_size,
        char* dest,
        size_t dest_capacity) const override {
        // TODO: Implement with LZ4_compress_default()
        // For now, just copy (stub)
        if (dest_capacity < source_size) {
            throw std::runtime_error("LZ4Codec: destination buffer too small (stub)");
        }
        std::memcpy(dest, source, source_size);
        return source_size;
    }

    size_t decompress(
        const char* source,
        size_t source_size,
        char* dest,
        size_t dest_capacity) const override {
        // TODO: Implement with LZ4_decompress_safe()
        // For now, just copy (stub)
        if (dest_capacity < source_size) {
            throw std::runtime_error("LZ4Codec: destination buffer too small (stub)");
        }
        std::memcpy(dest, source, source_size);
        return source_size;
    }

    size_t getMaxCompressedSize(size_t source_size) const override {
        // TODO: Use LZ4_compressBound()
        // For now, worst case is uncompressed + small header
        return source_size + (source_size / 255) + 16;
    }

    static CompressionCodecPtr create() {
        return std::make_shared<LZ4Codec>();
    }
};

/**
 * ZSTD compression codec (stub - requires zstd library)
 *
 * NOTE: This is a stub implementation. Full implementation requires:
 * - Link against zstd library
 * - Include zstd.h
 * - Implement actual compression calls
 */
class ZSTDCodec : public ICompressionCodec {
public:
    explicit ZSTDCodec(int level = 3) : level_(level) {}

    std::string getName() const override {
        return "ZSTD";
    }

    uint8_t getCodecId() const override {
        return static_cast<uint8_t>(CodecId::ZSTD);
    }

    size_t compress(
        const char* source,
        size_t source_size,
        char* dest,
        size_t dest_capacity) const override {
        // TODO: Implement with ZSTD_compress()
        // For now, just copy (stub)
        if (dest_capacity < source_size) {
            throw std::runtime_error("ZSTDCodec: destination buffer too small (stub)");
        }
        std::memcpy(dest, source, source_size);
        return source_size;
    }

    size_t decompress(
        const char* source,
        size_t source_size,
        char* dest,
        size_t dest_capacity) const override {
        // TODO: Implement with ZSTD_decompress()
        // For now, just copy (stub)
        if (dest_capacity < source_size) {
            throw std::runtime_error("ZSTDCodec: destination buffer too small (stub)");
        }
        std::memcpy(dest, source, source_size);
        return source_size;
    }

    size_t getMaxCompressedSize(size_t source_size) const override {
        // TODO: Use ZSTD_compressBound()
        // For now, worst case is uncompressed + small header
        return source_size + (source_size / 255) + 16;
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
