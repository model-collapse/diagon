// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/BitPacking.h"

#include <algorithm>
#include <cstring>

namespace diagon {
namespace util {

int BitPacking::bitsRequired(uint32_t maxValue) {
    if (maxValue == 0) return 0;
    // Count leading zeros, subtract from 32
    return 32 - __builtin_clz(maxValue);
}

int BitPacking::encode(const uint32_t* values, int count, uint8_t* output) {
    // Find max value to determine bitsPerValue
    uint32_t maxVal = 0;
    for (int i = 0; i < count; i++) {
        maxVal |= values[i];  // OR is faster than max for finding max bit width
    }
    int bpv = bitsRequired(maxVal);

    // Write header byte
    output[0] = static_cast<uint8_t>(bpv);

    if (bpv == 0) {
        // All zeros — just the header byte
        return 1;
    }

    // Pack values into output starting at byte 1
    // Use a bit buffer to pack values contiguously
    uint64_t bitBuffer = 0;
    int bitsInBuffer = 0;
    int outPos = 1;

    for (int i = 0; i < count; i++) {
        bitBuffer |= static_cast<uint64_t>(values[i]) << bitsInBuffer;
        bitsInBuffer += bpv;

        // Flush complete bytes
        while (bitsInBuffer >= 8) {
            output[outPos++] = static_cast<uint8_t>(bitBuffer & 0xFF);
            bitBuffer >>= 8;
            bitsInBuffer -= 8;
        }
    }

    // Flush remaining bits
    if (bitsInBuffer > 0) {
        output[outPos++] = static_cast<uint8_t>(bitBuffer & 0xFF);
    }

    return outPos;
}

int BitPacking::decode(const uint8_t* input, int count, uint32_t* values) {
    int bpv = input[0];

    if (bpv == 0) {
        // All zeros
        std::memset(values, 0, count * sizeof(uint32_t));
        return 1;
    }

    const uint32_t mask = (bpv == 32) ? 0xFFFFFFFFu : ((1u << bpv) - 1);

    // Unpack values from input starting at byte 1
    uint64_t bitBuffer = 0;
    int bitsInBuffer = 0;
    int inPos = 1;

    for (int i = 0; i < count; i++) {
        // Ensure we have enough bits
        while (bitsInBuffer < bpv) {
            bitBuffer |= static_cast<uint64_t>(input[inPos++]) << bitsInBuffer;
            bitsInBuffer += 8;
        }

        values[i] = static_cast<uint32_t>(bitBuffer) & mask;
        bitBuffer >>= bpv;
        bitsInBuffer -= bpv;
    }

    // Calculate total bytes consumed: 1 header + ceil(count * bpv / 8)
    int dataBits = count * bpv;
    int dataBytes = (dataBits + 7) / 8;
    return 1 + dataBytes;
}

}  // namespace util
}  // namespace diagon
