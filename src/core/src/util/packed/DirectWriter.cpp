// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/packed/DirectWriter.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace diagon {
namespace util {
namespace packed {

// ==================== DirectWriter ====================

int DirectWriter::rawUnsignedBitsRequired(uint64_t value) {
    if (value == 0) return 0;
    return 64 - __builtin_clzll(value);
}

int DirectWriter::roundBits(int bitsRequired) {
    // Binary search in SUPPORTED_BITS_PER_VALUE
    auto it =
        std::lower_bound(SUPPORTED_BITS_PER_VALUE, SUPPORTED_BITS_PER_VALUE + NUM_SUPPORTED_BPV,
                         bitsRequired);
    if (it == SUPPORTED_BITS_PER_VALUE + NUM_SUPPORTED_BPV) {
        return 64;  // Maximum
    }
    return *it;
}

int DirectWriter::bitsRequired(int64_t maxValue) {
    if (maxValue < 0) {
        throw std::invalid_argument("maxValue must be >= 0");
    }
    int raw = rawUnsignedBitsRequired(static_cast<uint64_t>(maxValue));
    if (raw == 0) raw = 1;  // At least 1 bit for signed
    return roundBits(raw);
}

int DirectWriter::unsignedBitsRequired(uint64_t maxValue) {
    int raw = rawUnsignedBitsRequired(maxValue);
    return roundBits(raw);
}

int DirectWriter::paddingBytesNeeded(int bitsPerValue) {
    int paddingBitsNeeded;
    if (bitsPerValue > 32) {
        paddingBitsNeeded = 64 - bitsPerValue;
    } else if (bitsPerValue > 16) {
        paddingBitsNeeded = 32 - bitsPerValue;
    } else if (bitsPerValue > 8) {
        paddingBitsNeeded = 16 - bitsPerValue;
    } else {
        paddingBitsNeeded = 0;
    }
    return (paddingBitsNeeded + 7) / 8;
}

int64_t DirectWriter::bytesRequired(int64_t numValues, int bitsPerValue) {
    int64_t bytes = (numValues * bitsPerValue + 7) / 8;
    return bytes + paddingBytesNeeded(bitsPerValue);
}

DirectWriter::DirectWriter(store::IndexOutput* output, int64_t numValues, int bitsPerValue)
    : output_(output)
    , numValues_(numValues)
    , bitsPerValue_(bitsPerValue)
    , count_(0)
    , finished_(false)
    , off_(0) {
    if (!output_) {
        throw std::invalid_argument("Output cannot be null");
    }
    if (numValues < 0) {
        throw std::invalid_argument("numValues must be >= 0");
    }

    // Validate bitsPerValue is supported
    bool valid = false;
    for (int i = 0; i < NUM_SUPPORTED_BPV; i++) {
        if (SUPPORTED_BITS_PER_VALUE[i] == bitsPerValue) {
            valid = true;
            break;
        }
    }
    if (!valid && bitsPerValue != 0) {
        throw std::invalid_argument("Unsupported bitsPerValue: " + std::to_string(bitsPerValue));
    }

    // Buffer size: match Lucene's DEFAULT_BUFFER_SIZE strategy
    // Budget = 16384 bytes * 8 bits = 131072 bits
    // bufferSize = budget / (64 + bpv), rounded up to next multiple of 64
    if (bitsPerValue_ > 0) {
        int memoryBudgetInBits = 8 * 16384;
        int bufferSize = memoryBudgetInBits / (64 + bitsPerValue_);
        if (bufferSize <= 0) bufferSize = 1;
        bufferSize = (bufferSize + 63) & ~63;  // Round to next multiple of 64

        nextValues_.resize(bufferSize, 0);
        // +7 bytes for LE over-write safety
        nextBlocks_.resize(static_cast<size_t>(bufferSize) * bitsPerValue_ / 8 + 7, 0);
    }
}

void DirectWriter::add(int64_t value) {
    if (finished_) {
        throw std::runtime_error("Writer already finished");
    }
    if (count_ >= numValues_) {
        throw std::runtime_error("Writing past end of stream");
    }

    if (bitsPerValue_ == 0) {
        // Zero bpv — nothing to write
        count_++;
        return;
    }

    nextValues_[off_++] = value;
    if (off_ == static_cast<int>(nextValues_.size())) {
        flush();
    }
    count_++;
}

void DirectWriter::flush() {
    if (off_ == 0) return;

    // Zero-fill unused slots to prevent data leakage
    std::fill(nextValues_.begin() + off_, nextValues_.end(), 0LL);

    // Encode values into packed bytes (LE)
    encode(nextValues_.data(), off_, nextBlocks_.data(), bitsPerValue_);

    // Calculate exact byte count: ceil(off * bpv / 8)
    int blockCount = (off_ * bitsPerValue_ + 7) / 8;

    // Write encoded bytes
    output_->writeBytes(nextBlocks_.data(), blockCount);

    off_ = 0;
}

void DirectWriter::encode(const int64_t* values, int upTo, uint8_t* blocks, int bpv) {
    if ((bpv & 7) == 0) {
        // Byte-aligned: 8, 16, 24, 32, 40, 48, 56, 64
        const int bytesPerValue = bpv / 8;
        for (int i = 0, o = 0; i < upTo; i++, o += bytesPerValue) {
            uint64_t v = static_cast<uint64_t>(values[i]);
            if (bpv > 32) {
                writeLE64(&blocks[o], v);
            } else if (bpv > 16) {
                writeLE32(&blocks[o], static_cast<uint32_t>(v));
            } else if (bpv > 8) {
                writeLE16(&blocks[o], static_cast<uint16_t>(v));
            } else {
                blocks[o] = static_cast<uint8_t>(v);
            }
        }
    } else if (bpv < 8) {
        // Sub-byte: 1, 2, 4 — pack multiple values into each 64-bit LE long
        const int valuesPerLong = 64 / bpv;
        for (int i = 0, o = 0; i < upTo; i += valuesPerLong, o += 8) {
            uint64_t v = 0;
            for (int j = 0; j < valuesPerLong; j++) {
                v |= static_cast<uint64_t>(values[i + j]) << (bpv * j);
            }
            writeLE64(&blocks[o], v);
        }
    } else {
        // Non-aligned: 12, 20, 28 — encode value pairs
        const int numBytesFor2Values = bpv * 2 / 8;
        for (int i = 0, o = 0; i < upTo; i += 2, o += numBytesFor2Values) {
            uint64_t l1 = static_cast<uint64_t>(values[i]);
            uint64_t l2 = static_cast<uint64_t>(values[i + 1]);
            uint64_t merged = l1 | (l2 << bpv);
            if (bpv <= 16) {
                // 12 bpv: merged is 24 bits → write as LE int32
                writeLE32(&blocks[o], static_cast<uint32_t>(merged));
            } else {
                // 20, 28 bpv: merged is 40 or 56 bits → write as LE int64
                writeLE64(&blocks[o], merged);
            }
        }
    }
}

void DirectWriter::finish() {
    if (count_ != numValues_) {
        throw std::runtime_error("Wrong number of values added, expected: " +
                                 std::to_string(numValues_) + ", got: " + std::to_string(count_));
    }
    if (finished_) {
        throw std::runtime_error("Already finished");
    }

    flush();

    // Add padding bytes for fast I/O (reader can over-read without bounds checking)
    int padding = paddingBytesNeeded(bitsPerValue_);
    for (int i = 0; i < padding; i++) {
        output_->writeByte(0);
    }
    finished_ = true;
}

// ==================== DirectReader ====================

uint64_t DirectReader::readLE64(store::IndexInput* input) {
    uint8_t buf[8];
    input->readBytes(buf, 8);
    uint64_t v;
    std::memcpy(&v, buf, 8);
    return v;
}

uint32_t DirectReader::readLE32(store::IndexInput* input) {
    uint8_t buf[4];
    input->readBytes(buf, 4);
    uint32_t v;
    std::memcpy(&v, buf, 4);
    return v;
}

uint16_t DirectReader::readLE16(store::IndexInput* input) {
    uint8_t buf[2];
    input->readBytes(buf, 2);
    uint16_t v;
    std::memcpy(&v, buf, 2);
    return v;
}

std::vector<int64_t> DirectReader::read(store::IndexInput* input, int bitsPerValue, int64_t count) {
    if (count == 0 || bitsPerValue == 0) {
        return std::vector<int64_t>(count, 0);
    }

    std::vector<int64_t> result(count);

    if ((bitsPerValue & 7) == 0) {
        // Byte-aligned path
        for (int64_t i = 0; i < count; i++) {
            uint64_t v = 0;
            if (bitsPerValue > 32) {
                v = readLE64(input);
            } else if (bitsPerValue > 16) {
                v = readLE32(input);
            } else if (bitsPerValue > 8) {
                v = readLE16(input);
            } else {
                v = input->readByte();
            }
            // Mask to exact bits
            if (bitsPerValue < 64) {
                v &= (1ULL << bitsPerValue) - 1;
            }
            result[i] = static_cast<int64_t>(v);
        }
    } else if (bitsPerValue < 8) {
        // Sub-byte: decode from LE longs
        // Writer stores ceil(count * bpv / 8) bytes, but values are packed in LE longs.
        // Read exact bytes needed and reconstruct longs.
        int64_t totalBytes = (count * bitsPerValue + 7) / 8;
        std::vector<uint8_t> buf(totalBytes + 7, 0);  // +7 for safe LE64 reads
        input->readBytes(buf.data(), totalBytes);

        const int valuesPerLong = 64 / bitsPerValue;
        const uint64_t mask = (1ULL << bitsPerValue) - 1;
        int64_t idx = 0;
        int byteOff = 0;
        while (idx < count) {
            uint64_t v;
            std::memcpy(&v, buf.data() + byteOff, 8);
            int valsThisLong = std::min(static_cast<int64_t>(valuesPerLong), count - idx);
            for (int j = 0; j < valsThisLong; j++) {
                result[idx + j] = static_cast<int64_t>((v >> (bitsPerValue * j)) & mask);
            }
            idx += valuesPerLong;
            byteOff += 8;
        }
    } else {
        // Non-aligned: 12, 20, 28 — decode pairs
        const uint64_t mask = (1ULL << bitsPerValue) - 1;
        int64_t idx = 0;
        while (idx < count) {
            uint64_t merged;
            if (bitsPerValue <= 16) {
                merged = readLE32(input);
            } else {
                merged = readLE64(input);
            }
            result[idx] = static_cast<int64_t>(merged & mask);
            if (idx + 1 < count) {
                result[idx + 1] = static_cast<int64_t>((merged >> bitsPerValue) & mask);
            }
            idx += 2;
        }
    }

    return result;
}

int64_t DirectReader::get(store::IndexInput* input, int bitsPerValue, int64_t baseOffset,
                          int64_t index) {
    if (bitsPerValue == 0) return 0;

    if ((bitsPerValue & 7) == 0) {
        // Byte-aligned: direct seek
        int bytesPerValue = bitsPerValue / 8;
        input->seek(baseOffset + index * bytesPerValue);
        uint64_t v = 0;
        if (bitsPerValue > 32) {
            v = readLE64(input);
        } else if (bitsPerValue > 16) {
            v = readLE32(input);
        } else if (bitsPerValue > 8) {
            v = readLE16(input);
        } else {
            v = input->readByte();
        }
        if (bitsPerValue < 64) v &= (1ULL << bitsPerValue) - 1;
        return static_cast<int64_t>(v);
    } else if (bitsPerValue < 8) {
        // Sub-byte: find the containing long
        int valuesPerLong = 64 / bitsPerValue;
        int64_t longIndex = index / valuesPerLong;
        int offsetInLong = static_cast<int>(index % valuesPerLong);
        input->seek(baseOffset + longIndex * 8);
        // Read available bytes safely (writer may have written fewer than 8)
        int64_t remaining = input->length() - input->getFilePointer();
        uint8_t buf[8] = {0};
        int toRead = static_cast<int>(std::min(remaining, int64_t(8)));
        if (toRead > 0) input->readBytes(buf, toRead);
        uint64_t v;
        std::memcpy(&v, buf, 8);
        uint64_t mask = (1ULL << bitsPerValue) - 1;
        return static_cast<int64_t>((v >> (bitsPerValue * offsetInLong)) & mask);
    } else {
        // Non-aligned: find the containing pair
        int64_t pairIndex = index / 2;
        int offsetInPair = static_cast<int>(index % 2);
        int numBytesFor2Values = bitsPerValue * 2 / 8;
        input->seek(baseOffset + pairIndex * numBytesFor2Values);
        uint64_t merged;
        if (bitsPerValue <= 16) {
            merged = readLE32(input);
        } else {
            merged = readLE64(input);
        }
        uint64_t mask = (1ULL << bitsPerValue) - 1;
        if (offsetInPair == 0) {
            return static_cast<int64_t>(merged & mask);
        } else {
            return static_cast<int64_t>((merged >> bitsPerValue) & mask);
        }
    }
}

}  // namespace packed
}  // namespace util
}  // namespace diagon
