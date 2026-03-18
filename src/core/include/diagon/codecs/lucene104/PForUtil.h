// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/lucene104/ForUtil.h"
#include "diagon/store/IndexInput.h"
#include "diagon/store/IndexOutput.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>

namespace diagon {
namespace codecs {
namespace lucene104 {

/**
 * Lucene 104-compatible PForUtil — PFOR (Patched Frame of Reference) encoding.
 *
 * Token byte format: (numExceptions << 5) | bitsPerValue
 *   - Low 5 bits: base bit width (0-31)
 *   - High 3 bits: number of exceptions (0-7)
 *
 * Special case: if bitsPerValue=0 AND all values are equal (maxBits<=8),
 *   write token + VInt(value) + exception bytes.
 *
 * Exception format: numExceptions pairs of (index_byte, highBits_byte).
 *
 * Based on: org.apache.lucene.codecs.lucene104.PForUtil
 */
class PForUtil {
public:
    static constexpr int MAX_EXCEPTIONS = 7;

    explicit PForUtil(ForUtil& forUtil) : forUtil_(forUtil) {}

    /**
     * Encode 256 ints using PFOR. May modify ints[].
     */
    void encode(int32_t* ints, store::IndexOutput& out) {
        // Build histogram of bit widths
        int histogram[33] = {};
        int maxBitsRequired = 0;
        for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
            int bits = bitsRequired(static_cast<uint32_t>(ints[i]));
            histogram[bits]++;
            maxBitsRequired = std::max(maxBitsRequired, bits);
        }

        // Find optimal bit width: allow up to MAX_EXCEPTIONS values to overflow
        // We store patch on a byte, so can't decrease bits by more than 8
        const int minBits = std::max(0, maxBitsRequired - 8);
        int cumulativeExceptions = 0;
        int patchedBitsRequired = maxBitsRequired;
        int numExceptions = 0;

        for (int b = maxBitsRequired; b >= minBits; --b) {
            if (cumulativeExceptions > MAX_EXCEPTIONS) {
                break;
            }
            patchedBitsRequired = b;
            numExceptions = cumulativeExceptions;
            cumulativeExceptions += histogram[b];
        }

        const int32_t maxUnpatchedValue = (1 << patchedBitsRequired) - 1;

        // Collect exceptions: (index, highBits) pairs
        uint8_t exceptions[MAX_EXCEPTIONS * 2] = {};
        if (numExceptions > 0) {
            int exceptionCount = 0;
            for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
                if (ints[i] > maxUnpatchedValue) {
                    exceptions[exceptionCount * 2] = static_cast<uint8_t>(i);
                    exceptions[exceptionCount * 2 + 1] =
                        static_cast<uint8_t>(static_cast<uint32_t>(ints[i]) >> patchedBitsRequired);
                    ints[i] &= maxUnpatchedValue;
                    exceptionCount++;
                }
            }
            assert(exceptionCount == numExceptions);
        }

        // Check all-equal special case
        if (allEqual(ints) && maxBitsRequired <= 8) {
            // Shift exception high bits for the all-equal case
            for (int i = 0; i < numExceptions; ++i) {
                exceptions[2 * i + 1] = static_cast<uint8_t>(
                    static_cast<uint8_t>(exceptions[2 * i + 1]) << patchedBitsRequired);
            }
            out.writeByte(static_cast<uint8_t>(numExceptions << 5));
            out.writeVInt(ints[0]);
        } else {
            const uint8_t token = static_cast<uint8_t>((numExceptions << 5) | patchedBitsRequired);
            out.writeByte(token);
            forUtil_.encode(ints, patchedBitsRequired, out);
        }

        // Write exception pairs
        if (numExceptions > 0) {
            out.writeBytes(exceptions, numExceptions * 2);
        }
    }

    /**
     * Decode 256 ints from input.
     */
    void decode(store::IndexInput& in, int32_t* ints) {
        const int token = in.readByte() & 0xFF;
        const int bitsPerValue = token & 0x1F;

        if (bitsPerValue == 0) {
            // All-equal: fill with VInt value
            int32_t val = in.readVInt();
            for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
                ints[i] = val;
            }
        } else {
            forUtil_.decode(bitsPerValue, in, ints);
        }

        // Apply exceptions
        const int numExceptions = token >> 5;
        for (int i = 0; i < numExceptions; ++i) {
            int idx = in.readByte() & 0xFF;
            int highBits = in.readByte() & 0xFF;
            ints[idx] |= highBits << bitsPerValue;
        }
    }

    /**
     * Skip 256 ints without decoding.
     */
    static void skip(store::IndexInput& in) {
        const int token = in.readByte() & 0xFF;
        const int bitsPerValue = token & 0x1F;
        const int numExceptions = token >> 5;

        if (bitsPerValue == 0) {
            in.readVLong();  // skip the VInt value
            in.skipBytes(numExceptions * 2);
        } else {
            in.skipBytes(ForUtil::numBytes(bitsPerValue) + numExceptions * 2);
        }
    }

private:
    ForUtil& forUtil_;

    static bool allEqual(const int32_t* arr) {
        for (int i = 1; i < ForUtil::BLOCK_SIZE; ++i) {
            if (arr[i] != arr[0]) return false;
        }
        return true;
    }

    static int bitsRequired(uint32_t value) {
        if (value == 0) return 0;
        return 32 - __builtin_clz(value);
    }
};

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
