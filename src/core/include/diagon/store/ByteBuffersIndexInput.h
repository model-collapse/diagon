// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/store/IndexInput.h"
#include "diagon/util/Exceptions.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace diagon::store {

/**
 * IndexInput implementation that reads from in-memory byte buffers.
 *
 * Useful for testing and reading data previously written to ByteBuffersIndexOutput.
 *
 * Based on: org.apache.lucene.store.ByteBuffersDataInput
 */
class ByteBuffersIndexInput : public IndexInput {
public:
    /**
     * Constructor from vector of bytes
     * @param name File name for diagnostic purposes
     * @param buffer Data to read from
     */
    ByteBuffersIndexInput(const std::string& name, const std::vector<uint8_t>& buffer)
        : name_(name)
        , buffer_(buffer)
        , position_(0) {}

    /**
     * Constructor from pointer and size
     * @param name File name for diagnostic purposes
     * @param data Pointer to data
     * @param length Length of data in bytes
     */
    ByteBuffersIndexInput(const std::string& name, const uint8_t* data, size_t length)
        : name_(name)
        , buffer_(data, data + length)
        , position_(0) {}

    // ==================== Basic Reading ====================

    uint8_t readByte() override {
        if (position_ >= buffer_.size()) {
            throw EOFException("Attempt to read past end of input");
        }
        return buffer_[position_++];
    }

    void readBytes(uint8_t* buf, size_t length) override {
        if (position_ + length > buffer_.size()) {
            throw EOFException("Attempt to read past end of input");
        }
        std::copy(buffer_.begin() + position_, buffer_.begin() + position_ + length, buf);
        position_ += length;
    }

    // ==================== Positioning ====================

    int64_t getFilePointer() const override { return position_; }

    void seek(int64_t pos) override {
        if (pos < 0 || pos > static_cast<int64_t>(buffer_.size())) {
            throw std::invalid_argument("Invalid seek position: " + std::to_string(pos));
        }
        position_ = pos;
    }

    int64_t length() const override { return buffer_.size(); }

    std::string toString() const override { return name_; }

    // ==================== Cloning ====================

    std::unique_ptr<IndexInput> clone() const override {
        auto cloned = std::make_unique<ByteBuffersIndexInput>(name_, buffer_);
        cloned->position_ = position_;
        return cloned;
    }

    std::unique_ptr<IndexInput> slice(const std::string& sliceDescription, int64_t offset,
                                      int64_t length) const override {
        if (offset < 0 || length < 0 || offset + length > static_cast<int64_t>(buffer_.size())) {
            throw std::invalid_argument("Invalid slice parameters");
        }

        std::vector<uint8_t> sliceData(buffer_.begin() + offset, buffer_.begin() + offset + length);
        return std::make_unique<ByteBuffersIndexInput>(name_ + " [slice=" + sliceDescription + "]",
                                                       sliceData);
    }

private:
    std::string name_;
    std::vector<uint8_t> buffer_;
    size_t position_;
};

}  // namespace diagon::store
