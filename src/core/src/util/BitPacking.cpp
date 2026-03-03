// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/BitPacking.h"

#include <algorithm>
#include <cstring>

namespace diagon {
namespace util {

int BitPacking::bitsRequired(uint32_t maxValue) {
    if (maxValue == 0)
        return 0;
    // Count leading zeros, subtract from 32
    return 32 - __builtin_clz(maxValue);
}

// Internal: write a uint32 as VInt into output buffer, return bytes written
static inline int writeVInt(uint32_t v, uint8_t* output) {
    int pos = 0;
    while (v >= 0x80) {
        output[pos++] = static_cast<uint8_t>((v & 0x7F) | 0x80);
        v >>= 7;
    }
    output[pos++] = static_cast<uint8_t>(v);
    return pos;
}

// Internal: read a uint32 VInt from input buffer, advance pos
static inline uint32_t readVInt(const uint8_t* input, int& pos) {
    uint8_t b = input[pos++];
    uint32_t v = b & 0x7F;
    if ((b & 0x80) == 0)
        return v;
    b = input[pos++];
    v |= static_cast<uint32_t>(b & 0x7F) << 7;
    if ((b & 0x80) == 0)
        return v;
    b = input[pos++];
    v |= static_cast<uint32_t>(b & 0x7F) << 14;
    if ((b & 0x80) == 0)
        return v;
    b = input[pos++];
    v |= static_cast<uint32_t>(b & 0x7F) << 21;
    if ((b & 0x80) == 0)
        return v;
    b = input[pos++];
    v |= static_cast<uint32_t>(b & 0x0F) << 28;
    return v;
}

// Internal: pack count values at bpv bits each into output, return bytes written
static inline int packBits(const uint32_t* values, int count, int bpv, uint8_t* output) {
    uint64_t bitBuffer = 0;
    int bitsInBuffer = 0;
    int outPos = 0;

    for (int i = 0; i < count; i++) {
        bitBuffer |= static_cast<uint64_t>(values[i]) << bitsInBuffer;
        bitsInBuffer += bpv;

        while (bitsInBuffer >= 8) {
            output[outPos++] = static_cast<uint8_t>(bitBuffer & 0xFF);
            bitBuffer >>= 8;
            bitsInBuffer -= 8;
        }
    }

    if (bitsInBuffer > 0) {
        output[outPos++] = static_cast<uint8_t>(bitBuffer & 0xFF);
    }

    return outPos;
}

// Internal: unpack count values at bpv bits each from input, return bytes consumed
static inline int unpackBits(const uint8_t* input, int count, int bpv, uint32_t* values) {
    const uint32_t mask = (bpv == 32) ? 0xFFFFFFFFu : ((1u << bpv) - 1);

    uint64_t bitBuffer = 0;
    int bitsInBuffer = 0;
    int inPos = 0;

    for (int i = 0; i < count; i++) {
        while (bitsInBuffer < bpv) {
            bitBuffer |= static_cast<uint64_t>(input[inPos++]) << bitsInBuffer;
            bitsInBuffer += 8;
        }

        values[i] = static_cast<uint32_t>(bitBuffer) & mask;
        bitBuffer >>= bpv;
        bitsInBuffer -= bpv;
    }

    int dataBits = count * bpv;
    return (dataBits + 7) / 8;
}

int BitPacking::encode(uint32_t* values, int count, uint8_t* output) {
    // --- Special case: all values identical ---
    bool allEqual = true;
    for (int i = 1; i < count; i++) {
        if (values[i] != values[0]) {
            allEqual = false;
            break;
        }
    }

    if (allEqual) {
        // Token: 0x00 (bpv=0, numEx=0), followed by VInt(value)
        output[0] = 0;
        return 1 + writeVInt(values[0], output + 1);
    }

    // --- Build histogram of bit widths ---
    int histogram[33] = {};  // histogram[b] = count of values needing b bits
    int maxBitsRequired = 0;
    for (int i = 0; i < count; i++) {
        int bits = bitsRequired(values[i]);
        histogram[bits]++;
        maxBitsRequired = std::max(maxBitsRequired, bits);
    }

    // --- Find optimal base bit width with ≤7 exceptions ---
    // Exception high bits must fit in 1 byte, so can't reduce by more than 8
    int minBits = std::max(0, maxBitsRequired - 8);
    int cumulativeExceptions = 0;
    int patchedBitsRequired = maxBitsRequired;
    int numExceptions = 0;

    for (int b = maxBitsRequired; b >= minBits; b--) {
        if (cumulativeExceptions > MAX_EXCEPTIONS)
            break;
        patchedBitsRequired = b;
        numExceptions = cumulativeExceptions;
        cumulativeExceptions += histogram[b];
    }

    // --- Extract exceptions and mask values ---
    uint32_t maxUnpatched = (patchedBitsRequired == 0) ? 0 : ((1u << patchedBitsRequired) - 1);
    uint8_t exceptions[MAX_EXCEPTIONS * 2];
    int exCount = 0;

    for (int i = 0; i < count && exCount < numExceptions; i++) {
        if (values[i] > maxUnpatched) {
            exceptions[exCount * 2] = static_cast<uint8_t>(i);
            exceptions[exCount * 2 + 1] = static_cast<uint8_t>(values[i] >> patchedBitsRequired);
            values[i] &= maxUnpatched;
            exCount++;
        }
    }

    // --- Write token byte ---
    int pos = 0;
    output[pos++] = static_cast<uint8_t>((numExceptions << 5) | patchedBitsRequired);

    // --- Write packed data ---
    if (patchedBitsRequired > 0) {
        pos += packBits(values, count, patchedBitsRequired, output + pos);
    }

    // --- Write exception pairs ---
    for (int i = 0; i < numExceptions * 2; i++) {
        output[pos++] = exceptions[i];
    }

    return pos;
}

int BitPacking::decode(const uint8_t* input, int count, uint32_t* values) {
    int pos = 0;
    uint8_t token = input[pos++];
    int bpv = token & 0x1F;
    int numExceptions = token >> 5;

    if (bpv == 0 && numExceptions == 0) {
        // All-equal case: read VInt value
        uint32_t value = readVInt(input, pos);
        for (int i = 0; i < count; i++) {
            values[i] = value;
        }
        return pos;
    }

    // --- Unpack base values ---
    if (bpv > 0) {
        pos += unpackBits(input + pos, count, bpv, values);
    } else {
        // bpv == 0 but has exceptions: all base values are 0
        std::memset(values, 0, count * sizeof(uint32_t));
    }

    // --- Apply exceptions ---
    for (int i = 0; i < numExceptions; i++) {
        int idx = input[pos++];
        uint32_t highBits = input[pos++];
        values[idx] |= highBits << bpv;
    }

    return pos;
}

}  // namespace util
}  // namespace diagon
