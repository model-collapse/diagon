// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/store/IndexOutput.h"

#include <zlib.h>

#include <cstdint>
#include <string>
#include <vector>

namespace diagon::store {

/**
 * IndexOutput implementation that writes to in-memory byte buffers.
 *
 * Useful for testing and temporary buffering before writing to disk.
 *
 * Based on: org.apache.lucene.store.ByteBuffersDataOutput
 */
class ByteBuffersIndexOutput : public IndexOutput {
public:
    /**
     * Constructor
     * @param name File name for diagnostic purposes
     */
    explicit ByteBuffersIndexOutput(const std::string& name)
        : name_(name)
        , buffer_()
        , position_(0)
        , crc32_(static_cast<uint32_t>(::crc32(0L, Z_NULL, 0))) {
        buffer_.reserve(1024);  // Start with 1KB
    }

    // ==================== Basic Writing ====================

    void writeByte(uint8_t b) override {
        buffer_.push_back(b);
        position_++;
        crc32_ = static_cast<uint32_t>(::crc32(crc32_, &b, 1));
    }

    void writeBytes(const uint8_t* buf, size_t length) override {
        buffer_.insert(buffer_.end(), buf, buf + length);
        position_ += length;
        crc32_ = static_cast<uint32_t>(::crc32(crc32_, buf, static_cast<uInt>(length)));
    }

    // ==================== Positioning ====================

    int64_t getFilePointer() const override { return position_; }

    int64_t getChecksum() const override {
        return static_cast<int64_t>(crc32_) & 0xFFFFFFFFL;
    }

    std::string getName() const override { return name_; }

    // ==================== Finalization ====================

    void close() override {
        // No-op for in-memory buffer
    }

    // ==================== Buffer Access ====================

    /**
     * Get the bytes written so far.
     * @return Vector of bytes
     */
    const std::vector<uint8_t>& toArrayCopy() const { return buffer_; }

    /**
     * Get pointer to buffer data.
     * @return Pointer to internal buffer
     */
    const uint8_t* data() const { return buffer_.data(); }

    /**
     * Get size of buffer.
     * @return Number of bytes written
     */
    size_t size() const { return buffer_.size(); }

    /**
     * Reset buffer to empty state.
     */
    void reset() {
        buffer_.clear();
        position_ = 0;
        crc32_ = static_cast<uint32_t>(::crc32(0L, Z_NULL, 0));
    }

private:
    std::string name_;
    std::vector<uint8_t> buffer_;
    int64_t position_;
    uint32_t crc32_;
};

}  // namespace diagon::store
