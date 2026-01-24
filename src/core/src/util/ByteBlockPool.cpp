// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/ByteBlockPool.h"

#include <cstring>
#include <stdexcept>

namespace diagon {
namespace util {

ByteBlockPool::ByteBlockPool() {
    // Allocate first block
    nextBuffer();
}

int64_t ByteBlockPool::append(const uint8_t* bytes, int length) {
    if (length <= 0) {
        return size();
    }

    int64_t startOffset = size();
    int remaining = length;
    const uint8_t* src = bytes;

    while (remaining > 0) {
        // Space available in current buffer
        int available = BYTE_BLOCK_SIZE - byteUpto_;

        if (available == 0) {
            nextBuffer();
            available = BYTE_BLOCK_SIZE;
        }

        // Copy what fits
        int toCopy = std::min(remaining, available);
        std::memcpy(buffer_ + byteUpto_, src, toCopy);

        byteUpto_ += toCopy;
        src += toCopy;
        remaining -= toCopy;
    }

    return startOffset;
}

int64_t ByteBlockPool::append(const std::string& str) {
    // Append string with null terminator
    int64_t offset = append(reinterpret_cast<const uint8_t*>(str.c_str()), str.length());
    append(reinterpret_cast<const uint8_t*>("\0"), 1);
    return offset;
}

uint8_t* ByteBlockPool::allocate(int size) {
    if (size <= 0) {
        throw std::invalid_argument("Size must be > 0");
    }

    if (size > BYTE_BLOCK_SIZE) {
        throw std::invalid_argument("Size exceeds block size");
    }

    // Check if current buffer has space
    int available = BYTE_BLOCK_SIZE - byteUpto_;
    if (available < size) {
        // Not enough space, allocate new buffer
        nextBuffer();
    }

    uint8_t* ptr = buffer_ + byteUpto_;
    byteUpto_ += size;

    return ptr;
}

uint8_t ByteBlockPool::getByte(int64_t offset) const {
    if (offset < 0 || offset >= size()) {
        throw std::out_of_range("Offset out of range");
    }

    int blockIndex = offset / BYTE_BLOCK_SIZE;
    int blockOffset = offset % BYTE_BLOCK_SIZE;

    return buffers_[blockIndex][blockOffset];
}

void ByteBlockPool::readBytes(int64_t offset, uint8_t* dest, int length) const {
    if (offset < 0 || offset + length > size()) {
        throw std::out_of_range("Read out of range");
    }

    int64_t currentOffset = offset;
    int remaining = length;
    uint8_t* destPtr = dest;

    while (remaining > 0) {
        int blockIndex = currentOffset / BYTE_BLOCK_SIZE;
        int blockOffset = currentOffset % BYTE_BLOCK_SIZE;
        int available = BYTE_BLOCK_SIZE - blockOffset;
        int toCopy = std::min(remaining, available);

        std::memcpy(destPtr, buffers_[blockIndex].get() + blockOffset, toCopy);

        destPtr += toCopy;
        currentOffset += toCopy;
        remaining -= toCopy;
    }
}

std::string ByteBlockPool::readString(int64_t offset) const {
    std::string result;
    int64_t currentOffset = offset;

    while (currentOffset < size()) {
        uint8_t byte = getByte(currentOffset);
        if (byte == 0) {
            break;  // Null terminator
        }
        result.push_back(static_cast<char>(byte));
        currentOffset++;
    }

    return result;
}

void ByteBlockPool::reset() {
    // Keep allocated buffers, just reset position
    bufferUpto_ = 0;
    byteUpto_ = 0;
    if (!buffers_.empty()) {
        buffer_ = buffers_[0].get();
    }
}

void ByteBlockPool::clear() {
    // Free all buffers
    buffers_.clear();
    bufferUpto_ = 0;
    byteUpto_ = 0;
    buffer_ = nullptr;
}

void ByteBlockPool::nextBuffer() {
    // Allocate new block
    auto newBuffer = std::make_unique<uint8_t[]>(BYTE_BLOCK_SIZE);
    buffer_ = newBuffer.get();
    buffers_.push_back(std::move(newBuffer));

    bufferUpto_ = buffers_.size() - 1;
    byteUpto_ = 0;
}

}  // namespace util
}  // namespace diagon
