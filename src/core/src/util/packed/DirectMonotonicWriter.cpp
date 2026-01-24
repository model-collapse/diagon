// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/packed/DirectMonotonicWriter.h"
#include "diagon/util/packed/DirectWriter.h"

#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace diagon {
namespace util {
namespace packed {

// ==================== DirectMonotonicWriter ====================

DirectMonotonicWriter::DirectMonotonicWriter(
    store::IndexOutput* meta,
    store::IndexOutput* data,
    int64_t numValues,
    int blockShift)
    : meta_(meta)
    , data_(data)
    , numValues_(numValues)
    , blockShift_(blockShift)
    , blockSize_(1 << blockShift)
    , count_(0)
    , lastValue_(0) {

    if (!meta_ || !data_) {
        throw std::invalid_argument("Outputs cannot be null");
    }
    if (numValues < 0) {
        throw std::invalid_argument("numValues must be >= 0");
    }
    if (blockShift < 0 || blockShift > 30) {
        throw std::invalid_argument("blockShift must be 0-30");
    }

    buffer_.reserve(blockSize_);
}

void DirectMonotonicWriter::add(int64_t value) {
    if (count_ >= numValues_) {
        throw std::runtime_error("Already wrote all values");
    }

    // Check monotonicity
    if (count_ > 0 && value < lastValue_) {
        throw std::invalid_argument("Values must be monotonically increasing");
    }

    buffer_.push_back(value);
    lastValue_ = value;
    count_++;

    // Flush block when full
    if (static_cast<int>(buffer_.size()) >= blockSize_) {
        flushBlock();
    }
}

DirectMonotonicWriter::Meta DirectMonotonicWriter::finish() {
    if (count_ != numValues_) {
        throw std::runtime_error("Must write exactly numValues");
    }

    // Flush remaining values
    if (!buffer_.empty()) {
        flushBlock();
    }

    // Write all block metadata
    for (const auto& block : blocks_) {
        writeMeta(block);
    }

    Meta meta;
    meta.numValues = numValues_;
    meta.blockShift = blockShift_;
    meta.min = blocks_.empty() ? 0 : blocks_.front().min;
    meta.max = blocks_.empty() ? 0 : blocks_.back().max;
    meta.metaFP = 0;  // Caller should set this
    meta.dataFP = 0;  // Caller should set this

    return meta;
}

void DirectMonotonicWriter::flushBlock() {
    if (buffer_.empty()) {
        return;
    }

    Block block;
    int size = buffer_.size();

    block.min = buffer_.front();
    block.max = buffer_.back();

    // Compute average slope
    if (size > 1) {
        block.avgSlope = static_cast<float>(block.max - block.min) / (size - 1);
    } else {
        block.avgSlope = 0;
    }

    // Compute deviations from expected values
    std::vector<int64_t> deviations(size);
    int64_t minDeviation = INT64_MAX;
    int64_t maxDeviation = INT64_MIN;

    for (int i = 0; i < size; i++) {
        int64_t expected = block.min + static_cast<int64_t>(block.avgSlope * i);
        int64_t deviation = buffer_[i] - expected;
        deviations[i] = deviation;
        minDeviation = std::min(minDeviation, deviation);
        maxDeviation = std::max(maxDeviation, deviation);
    }

    // Normalize deviations to non-negative
    for (int i = 0; i < size; i++) {
        deviations[i] -= minDeviation;
    }

    // Calculate bits required
    uint64_t maxNormalized = maxDeviation - minDeviation;
    block.bitsPerValue = DirectWriter::unsignedBitsRequired(maxNormalized);

    // Store block metadata (written in finish())
    block.minDeviation = minDeviation;
    block.dataOffset = data_->getFilePointer();
    blocks_.push_back(block);

    // Write packed deviations
    if (block.bitsPerValue > 0) {
        DirectWriter writer(data_, size, block.bitsPerValue);
        for (int i = 0; i < size; i++) {
            writer.add(deviations[i]);
        }
        writer.finish();
    }

    // Clear buffer for next block
    buffer_.clear();
}

void DirectMonotonicWriter::writeMeta(const Block& block) {
    // Write block metadata (called during finish())
    meta_->writeLong(block.min);

    // Write avgSlope as float bits
    float slope = block.avgSlope;
    uint32_t bits;
    std::memcpy(&bits, &slope, sizeof(bits));
    meta_->writeInt(static_cast<int>(bits));

    meta_->writeLong(block.minDeviation);
    meta_->writeLong(block.dataOffset);
    meta_->writeByte(static_cast<uint8_t>(block.bitsPerValue));
}

// ==================== DirectMonotonicReader ====================

int64_t DirectMonotonicReader::get(
    const DirectMonotonicWriter::Meta& meta,
    store::IndexInput* metaIn,
    store::IndexInput* dataIn,
    int64_t index) {

    if (index < 0 || index >= meta.numValues) {
        throw std::invalid_argument("Index out of range");
    }

    int blockSize = 1 << meta.blockShift;
    int64_t blockIndex = index / blockSize;
    int offsetInBlock = index % blockSize;

    // Read block metadata
    Block block = readBlockMeta(meta, metaIn, blockIndex);

    // Calculate expected value
    int64_t expected = block.min + static_cast<int64_t>(block.avgSlope * offsetInBlock);

    // Read deviation if needed
    int64_t deviation = 0;
    if (block.bitsPerValue > 0) {
        // Calculate absolute bit position: block start + offset within block
        int64_t absoluteBitPosition = block.dataOffset * 8 + offsetInBlock * block.bitsPerValue;
        int64_t bytePosition = absoluteBitPosition / 8;
        dataIn->seek(bytePosition);

        // Read the deviation value
        int bitOffset = absoluteBitPosition % 8;
        int bytesNeeded = (bitOffset + block.bitsPerValue + 7) / 8;
        std::vector<uint8_t> buffer(bytesNeeded);
        dataIn->readBytes(buffer.data(), bytesNeeded);

        // Reconstruct value from bytes
        uint64_t accumulator = 0;
        for (int i = 0; i < bytesNeeded; i++) {
            accumulator = (accumulator << 8) | buffer[i];
        }

        // Shift to align value and mask
        int bitsInLastByte = (bitOffset + block.bitsPerValue) % 8;
        if (bitsInLastByte == 0) {
            bitsInLastByte = 8;
        }
        int shiftRight = 8 - bitsInLastByte;
        accumulator >>= shiftRight;

        // Mask to extract only bitsPerValue bits
        uint64_t mask = (1ULL << block.bitsPerValue) - 1;
        deviation = accumulator & mask;
    }

    return expected + deviation + block.minDeviation;
}

std::vector<int64_t> DirectMonotonicReader::readAll(
    const DirectMonotonicWriter::Meta& meta,
    store::IndexInput* metaIn,
    store::IndexInput* dataIn) {

    std::vector<int64_t> result;
    result.reserve(meta.numValues);

    int blockSize = 1 << meta.blockShift;
    int64_t numBlocks = (meta.numValues + blockSize - 1) / blockSize;

    for (int64_t blockIndex = 0; blockIndex < numBlocks; blockIndex++) {
        Block block = readBlockMeta(meta, metaIn, blockIndex);

        // Calculate number of values in this block
        int64_t valuesInBlock = std::min<int64_t>(
            blockSize,
            meta.numValues - blockIndex * blockSize);

        // Read deviations for this block
        std::vector<int64_t> deviations(valuesInBlock);
        if (block.bitsPerValue > 0) {
            // Read each deviation using absolute positioning
            for (int64_t i = 0; i < valuesInBlock; i++) {
                int64_t absoluteBitPosition = block.dataOffset * 8 + i * block.bitsPerValue;
                int64_t bytePosition = absoluteBitPosition / 8;
                dataIn->seek(bytePosition);

                int bitOffset = absoluteBitPosition % 8;
                int bytesNeeded = (bitOffset + block.bitsPerValue + 7) / 8;
                std::vector<uint8_t> buffer(bytesNeeded);
                dataIn->readBytes(buffer.data(), bytesNeeded);

                uint64_t accumulator = 0;
                for (int j = 0; j < bytesNeeded; j++) {
                    accumulator = (accumulator << 8) | buffer[j];
                }

                int bitsInLastByte = (bitOffset + block.bitsPerValue) % 8;
                if (bitsInLastByte == 0) {
                    bitsInLastByte = 8;
                }
                int shiftRight = 8 - bitsInLastByte;
                accumulator >>= shiftRight;

                uint64_t mask = (1ULL << block.bitsPerValue) - 1;
                deviations[i] = accumulator & mask;
            }
        } else {
            deviations.resize(valuesInBlock, 0);
        }

        // Reconstruct values
        for (int i = 0; i < valuesInBlock; i++) {
            int64_t expected = block.min + static_cast<int64_t>(block.avgSlope * i);
            result.push_back(expected + deviations[i] + block.minDeviation);
        }
    }

    return result;
}

DirectMonotonicReader::Block DirectMonotonicReader::readBlockMeta(
    const DirectMonotonicWriter::Meta& meta,
    store::IndexInput* metaIn,
    int64_t blockIndex) {

    // Seek to block metadata
    // Each block metadata is: 8 (min) + 4 (slope) + 8 (minDeviation) + 8 (offset) + 1 (bits) = 29 bytes
    int64_t metaOffset = meta.metaFP + blockIndex * 29;
    metaIn->seek(metaOffset);

    Block block;
    block.min = metaIn->readLong();

    // Read avgSlope as float from int bits
    uint32_t bits = static_cast<uint32_t>(metaIn->readInt());
    std::memcpy(&block.avgSlope, &bits, sizeof(block.avgSlope));

    block.minDeviation = metaIn->readLong();
    block.dataOffset = metaIn->readLong();
    block.bitsPerValue = metaIn->readByte();

    return block;
}

}  // namespace packed
}  // namespace util
}  // namespace diagon
