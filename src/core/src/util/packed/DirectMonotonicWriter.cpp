// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/packed/DirectMonotonicWriter.h"

#include "diagon/util/packed/DirectWriter.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace diagon {
namespace util {
namespace packed {

// ==================== DirectMonotonicWriter ====================

DirectMonotonicWriter::DirectMonotonicWriter(store::IndexOutput* metaOut,
                                             store::IndexOutput* dataOut, int64_t numValues,
                                             int blockShift)
    : meta_(metaOut)
    , data_(dataOut)
    , numValues_(numValues)
    , baseDataPointer_(dataOut->getFilePointer())
    , bufferSize_(0)
    , count_(0)
    , previous_(std::numeric_limits<int64_t>::min())
    , finished_(false) {
    if (!metaOut || !dataOut) {
        throw std::invalid_argument("Outputs cannot be null");
    }
    if (blockShift < MIN_BLOCK_SHIFT || blockShift > MAX_BLOCK_SHIFT) {
        throw std::invalid_argument("blockShift must be in [" + std::to_string(MIN_BLOCK_SHIFT) +
                                    "-" + std::to_string(MAX_BLOCK_SHIFT) + "]");
    }
    if (numValues < 0) {
        throw std::invalid_argument("numValues can't be negative");
    }

    int blockSize = 1 << blockShift;
    buffer_.resize(std::min(numValues, static_cast<int64_t>(blockSize)));
}

void DirectMonotonicWriter::add(int64_t value) {
    if (value < previous_) {
        throw std::invalid_argument("Values do not come in order: " + std::to_string(previous_) +
                                    ", " + std::to_string(value));
    }
    if (bufferSize_ == static_cast<int>(buffer_.size())) {
        flush();
    }
    buffer_[bufferSize_++] = value;
    previous_ = value;
    count_++;
}

void DirectMonotonicWriter::flush() {
    if (bufferSize_ == 0) return;

    // Step 1: Compute average increment (slope)
    const float avgInc = static_cast<float>(
        static_cast<double>(buffer_[bufferSize_ - 1] - buffer_[0]) /
        std::max(1, bufferSize_ - 1));

    // Step 2: Subtract expected values (linear prediction)
    int64_t min = std::numeric_limits<int64_t>::max();
    for (int i = 0; i < bufferSize_; i++) {
        int64_t expected = static_cast<int64_t>(avgInc * static_cast<int64_t>(i));
        buffer_[i] -= expected;
        min = std::min(buffer_[i], min);
    }

    // Step 3: Normalize to non-negative by subtracting min
    int64_t maxDelta = 0;
    for (int i = 0; i < bufferSize_; i++) {
        buffer_[i] -= min;
        maxDelta |= buffer_[i];  // OR to find max bits needed
    }

    // Step 4: Write metadata (Lucene format: 21 bytes per block)
    meta_->writeLong(min);

    // Write avgInc as IEEE 754 float bits (big-endian int)
    uint32_t floatBits;
    std::memcpy(&floatBits, &avgInc, sizeof(floatBits));
    meta_->writeInt(static_cast<int32_t>(floatBits));

    // Write data offset relative to baseDataPointer
    meta_->writeLong(data_->getFilePointer() - baseDataPointer_);

    // Step 5: Write packed residuals (or just metadata if all equal)
    if (maxDelta == 0) {
        meta_->writeByte(0);  // No data needed
    } else {
        int bitsRequired = DirectWriter::unsignedBitsRequired(static_cast<uint64_t>(maxDelta));
        DirectWriter writer(data_, bufferSize_, bitsRequired);
        for (int i = 0; i < bufferSize_; i++) {
            writer.add(buffer_[i]);
        }
        writer.finish();
        meta_->writeByte(static_cast<uint8_t>(bitsRequired));
    }

    bufferSize_ = 0;
}

void DirectMonotonicWriter::finish() {
    if (count_ != numValues_) {
        throw std::runtime_error("Wrong number of values added, expected: " +
                                 std::to_string(numValues_) + ", got: " + std::to_string(count_));
    }
    if (finished_) {
        throw std::runtime_error("#finish has been called already");
    }
    if (bufferSize_ > 0) {
        flush();
    }
    finished_ = true;
}

// ==================== DirectMonotonicReader ====================

DirectMonotonicReader::BlockMeta
DirectMonotonicReader::readBlockMeta(store::IndexInput* metaIn, int64_t metaStartFP,
                                     int64_t blockIndex) {
    // Each block metadata: Long(8) + Int(4) + Long(8) + Byte(1) = 21 bytes
    metaIn->seek(metaStartFP + blockIndex * 21);

    BlockMeta block;
    block.min = metaIn->readLong();

    // Read float from int bits (big-endian int from stream)
    uint32_t floatBits = static_cast<uint32_t>(metaIn->readInt());
    std::memcpy(&block.avgInc, &floatBits, sizeof(block.avgInc));

    block.dataOffset = metaIn->readLong();
    block.bitsRequired = metaIn->readByte();

    return block;
}

int64_t DirectMonotonicReader::get(store::IndexInput* metaIn, store::IndexInput* dataIn,
                                   int64_t baseDataPointer, int blockShift, int64_t numValues,
                                   int64_t index, int64_t metaStartFP) {
    if (index < 0 || index >= numValues) {
        throw std::invalid_argument("Index out of range: " + std::to_string(index));
    }

    int blockSize = 1 << blockShift;
    int64_t blockIndex = index >> blockShift;
    int offsetInBlock = static_cast<int>(index & (blockSize - 1));

    // Read block metadata from known start position
    BlockMeta block = readBlockMeta(metaIn, metaStartFP, blockIndex);

    // Compute expected value from linear prediction
    int64_t expected = static_cast<int64_t>(block.avgInc * static_cast<int64_t>(offsetInBlock));

    // Read residual
    int64_t residual = 0;
    if (block.bitsRequired > 0) {
        int64_t dataOffset = baseDataPointer + block.dataOffset;
        residual =
            DirectReader::get(dataIn, block.bitsRequired, dataOffset, offsetInBlock);
    }

    return block.min + expected + residual;
}

std::vector<int64_t> DirectMonotonicReader::readAll(store::IndexInput* metaIn,
                                                     store::IndexInput* dataIn,
                                                     int64_t baseDataPointer, int blockShift,
                                                     int64_t numValues, int64_t metaStartFP) {
    std::vector<int64_t> result;
    result.reserve(numValues);

    int blockSize = 1 << blockShift;
    int64_t numBlocks = numValues == 0 ? 0 : ((numValues - 1) >> blockShift) + 1;

    for (int64_t blockIdx = 0; blockIdx < numBlocks; blockIdx++) {
        BlockMeta block = readBlockMeta(metaIn, metaStartFP, blockIdx);

        int64_t valuesInBlock =
            std::min(static_cast<int64_t>(blockSize), numValues - blockIdx * blockSize);

        if (block.bitsRequired > 0) {
            // Read all residuals for this block
            int64_t dataOffset = baseDataPointer + block.dataOffset;
            dataIn->seek(dataOffset);
            auto residuals = DirectReader::read(dataIn, block.bitsRequired, valuesInBlock);

            for (int64_t i = 0; i < valuesInBlock; i++) {
                int64_t expected =
                    static_cast<int64_t>(block.avgInc * static_cast<int64_t>(i));
                result.push_back(block.min + expected + residuals[i]);
            }
        } else {
            for (int64_t i = 0; i < valuesInBlock; i++) {
                int64_t expected =
                    static_cast<int64_t>(block.avgInc * static_cast<int64_t>(i));
                result.push_back(block.min + expected);
            }
        }
    }

    return result;
}

}  // namespace packed
}  // namespace util
}  // namespace diagon
