// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/packed/DirectWriter.h"

#include <stdexcept>
#include <algorithm>

namespace diagon {
namespace util {
namespace packed {

// ==================== DirectWriter ====================

int DirectWriter::bitsRequired(int64_t value) {
    if (value < 0) {
        return 64;  // Need all bits for negative values
    }
    return unsignedBitsRequired(static_cast<uint64_t>(value));
}

int DirectWriter::unsignedBitsRequired(uint64_t value) {
    if (value == 0) {
        return 0;
    }
    // Count leading zeros and subtract from 64
    int bits = 64 - __builtin_clzll(value);
    return bits;
}

DirectWriter::DirectWriter(store::IndexOutput* output, int64_t numValues, int bitsPerValue)
    : output_(output)
    , numValues_(numValues)
    , bitsPerValue_(bitsPerValue)
    , count_(0)
    , buffer_(0)
    , bufferSize_(0)
    , byteAligned_(false) {

    if (!output_) {
        throw std::invalid_argument("Output cannot be null");
    }
    if (bitsPerValue < 0 || bitsPerValue > 64) {
        throw std::invalid_argument("bitsPerValue must be 0-64");
    }
    if (numValues < 0) {
        throw std::invalid_argument("numValues must be >= 0");
    }

    // Check if byte-aligned (8, 16, 24, 32, 40, 48, 56, 64)
    byteAligned_ = (bitsPerValue_ > 0 && bitsPerValue_ % 8 == 0);

    if (byteAligned_) {
        int bytesPerValue = bitsPerValue_ / 8;
        byteBuffer_.resize(bytesPerValue);
    }
}

void DirectWriter::add(int64_t value) {
    if (count_ >= numValues_) {
        throw std::runtime_error("Already wrote all values");
    }

    if (bitsPerValue_ == 0) {
        // All zeros case - nothing to write
        count_++;
        return;
    }

    // Byte-aligned fast path
    if (byteAligned_) {
        writeByteFastPath(value);
        count_++;
        return;
    }

    // General bitpacking case
    // Add value to buffer, MSB first
    buffer_ = (buffer_ << bitsPerValue_) | (value & ((1ULL << bitsPerValue_) - 1));
    bufferSize_ += bitsPerValue_;

    // Write complete bytes
    while (bufferSize_ >= 8) {
        bufferSize_ -= 8;
        uint8_t byte = (buffer_ >> bufferSize_) & 0xFF;
        output_->writeByte(byte);
    }

    count_++;
}

void DirectWriter::finish() {
    if (count_ != numValues_) {
        throw std::runtime_error("Must write exactly numValues");
    }

    // Flush any remaining bits in buffer
    if (bufferSize_ > 0) {
        // Pad with zeros and write final byte
        uint8_t byte = (buffer_ << (8 - bufferSize_)) & 0xFF;
        output_->writeByte(byte);
        buffer_ = 0;
        bufferSize_ = 0;
    }
}

void DirectWriter::flushBuffer() {
    if (bufferSize_ > 0) {
        uint8_t byte = (buffer_ << (8 - bufferSize_)) & 0xFF;
        output_->writeByte(byte);
        buffer_ = 0;
        bufferSize_ = 0;
    }
}

void DirectWriter::writeByteFastPath(int64_t value) {
    int bytesPerValue = bitsPerValue_ / 8;

    // Write bytes big-endian
    for (int i = bytesPerValue - 1; i >= 0; i--) {
        byteBuffer_[i] = static_cast<uint8_t>(value & 0xFF);
        value >>= 8;
    }

    output_->writeBytes(byteBuffer_.data(), bytesPerValue);
}

// ==================== DirectReader ====================

std::vector<int64_t> DirectReader::read(
    store::IndexInput* input,
    int bitsPerValue,
    int64_t count) {

    if (count == 0 || bitsPerValue == 0) {
        return std::vector<int64_t>(count, 0);
    }

    std::vector<int64_t> result;
    result.reserve(count);

    // Byte-aligned fast path
    if (bitsPerValue % 8 == 0) {
        int bytesPerValue = bitsPerValue / 8;
        std::vector<uint8_t> buffer(bytesPerValue);

        for (int64_t i = 0; i < count; i++) {
            input->readBytes(buffer.data(), bytesPerValue);

            int64_t value = 0;
            for (int j = 0; j < bytesPerValue; j++) {
                value = (value << 8) | buffer[j];
            }
            result.push_back(value);
        }
        return result;
    }

    // General bitpacking case
    int64_t bitPosition = 0;
    for (int64_t i = 0; i < count; i++) {
        int64_t value = readValue(input, bitsPerValue, bitPosition);
        result.push_back(value);
        bitPosition += bitsPerValue;
    }

    return result;
}

int64_t DirectReader::getInstance(
    store::IndexInput* input,
    int bitsPerValue,
    int64_t index) {

    if (bitsPerValue == 0) {
        return 0;
    }

    int64_t bitPosition = index * bitsPerValue;
    return readValue(input, bitsPerValue, bitPosition);
}

int64_t DirectReader::readValue(
    store::IndexInput* input,
    int bitsPerValue,
    int64_t bitPosition) {

    // Calculate byte position and bit offset
    int64_t bytePosition = bitPosition / 8;
    int bitOffset = bitPosition % 8;

    // Seek to byte position
    input->seek(bytePosition);

    // Read enough bytes to cover the value
    int bytesNeeded = (bitOffset + bitsPerValue + 7) / 8;
    std::vector<uint8_t> buffer(bytesNeeded);
    input->readBytes(buffer.data(), bytesNeeded);

    // Reconstruct value from bytes
    uint64_t accumulator = 0;
    for (int i = 0; i < bytesNeeded; i++) {
        accumulator = (accumulator << 8) | buffer[i];
    }

    // Shift to align value and mask
    int bitsInLastByte = (bitOffset + bitsPerValue) % 8;
    if (bitsInLastByte == 0) {
        bitsInLastByte = 8;
    }
    int shiftRight = 8 - bitsInLastByte;

    accumulator >>= shiftRight;

    // Mask to extract only bitsPerValue bits
    uint64_t mask = (1ULL << bitsPerValue) - 1;
    int64_t value = accumulator & mask;

    return value;
}

}  // namespace packed
}  // namespace util
}  // namespace diagon
