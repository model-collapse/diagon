// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/IntBlockPool.h"

#include <stdexcept>

namespace diagon {
namespace util {

IntBlockPool::IntBlockPool() {
    // Allocate first block
    nextBuffer();
}

int IntBlockPool::append(int value) {
    int offset = size();

    // Check if current buffer is full
    if (intUpto_ >= INT_BLOCK_SIZE) {
        nextBuffer();
    }

    buffer_[intUpto_++] = value;
    return offset;
}

int IntBlockPool::allocate(int count) {
    if (count <= 0) {
        throw std::invalid_argument("Count must be > 0");
    }

    if (count > INT_BLOCK_SIZE) {
        throw std::invalid_argument("Count exceeds block size");
    }

    // Check if current buffer has space
    int available = INT_BLOCK_SIZE - intUpto_;
    if (available < count) {
        // Not enough space, allocate new buffer
        nextBuffer();
    }

    int offset = size();
    intUpto_ += count;

    return offset;
}

void IntBlockPool::writeInt(int offset, int value) {
    if (offset < 0 || offset >= size()) {
        throw std::out_of_range("Offset out of range");
    }

    int blockIndex = offset / INT_BLOCK_SIZE;
    int blockOffset = offset % INT_BLOCK_SIZE;

    buffers_[blockIndex][blockOffset] = value;
}

int IntBlockPool::readInt(int offset) const {
    if (offset < 0 || offset >= size()) {
        throw std::out_of_range("Offset out of range");
    }

    int blockIndex = offset / INT_BLOCK_SIZE;
    int blockOffset = offset % INT_BLOCK_SIZE;

    return buffers_[blockIndex][blockOffset];
}

int* IntBlockPool::allocateSlice(int count) {
    if (count <= 0) {
        throw std::invalid_argument("Count must be > 0");
    }

    if (count > INT_BLOCK_SIZE) {
        throw std::invalid_argument("Count exceeds block size");
    }

    // Check if current buffer has space
    int available = INT_BLOCK_SIZE - intUpto_;
    if (available < count) {
        // Not enough space, allocate new buffer
        nextBuffer();
    }

    int* ptr = buffer_ + intUpto_;
    intUpto_ += count;

    return ptr;
}

void IntBlockPool::reset() {
    // Keep allocated buffers, just reset position
    bufferUpto_ = 0;
    intUpto_ = 0;
    if (!buffers_.empty()) {
        buffer_ = buffers_[0].get();
    }
}

void IntBlockPool::clear() {
    // Free all buffers
    buffers_.clear();
    bufferUpto_ = 0;
    intUpto_ = 0;
    buffer_ = nullptr;
}

void IntBlockPool::nextBuffer() {
    // Allocate new block
    auto newBuffer = std::make_unique<int[]>(INT_BLOCK_SIZE);
    buffer_ = newBuffer.get();
    buffers_.push_back(std::move(newBuffer));

    bufferUpto_ = buffers_.size() - 1;
    intUpto_ = 0;
}

}  // namespace util
}  // namespace diagon
