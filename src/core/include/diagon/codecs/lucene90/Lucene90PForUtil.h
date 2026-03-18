// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/lucene90/Lucene90ForUtil.h"
#include "diagon/store/IndexInput.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>

namespace diagon {
namespace codecs {
namespace lucene90 {

/**
 * Lucene 90-compatible PForUtil — patched frame-of-reference decoder for 128 integers.
 *
 * Wraps ForUtil with:
 *   - Token byte format: [numExceptions:3 | bitsPerValue:5]
 *   - Exception patching (up to 7 exceptions per block)
 *   - Prefix sum computation (for delta-encoded doc IDs)
 *   - Block skipping
 *
 * Decode-only (for reading OpenSearch 2.11 / Lucene 9.x indices).
 *
 * Based on: org.apache.lucene.backward_codecs.lucene90.PForUtil
 */
class PForUtil {
public:
    static constexpr int MAX_EXCEPTIONS = 7;
    static constexpr int HALF_BLOCK_SIZE = ForUtil::BLOCK_SIZE / 2;

    explicit PForUtil(ForUtil& forUtil) : forUtil_(forUtil) {}

    /** Decode 128 integers into longs[128]. */
    void decode(store::IndexInput& in, int64_t* longs) {
        const int token = static_cast<int>(in.readByte());
        const int bitsPerValue = token & 0x1f;
        const int numExceptions = token >> 5;

        if (bitsPerValue == 0) {
            // All-equal block: fill with single value
            const int64_t val = in.readVLong();
            std::fill(longs, longs + ForUtil::BLOCK_SIZE, val);
        } else {
            forUtil_.decode(bitsPerValue, in, longs);
        }

        // Apply exception patches: each exception is (position, high-order bits)
        for (int i = 0; i < numExceptions; ++i) {
            const int pos = static_cast<int>(in.readByte());
            const int64_t highBits = static_cast<int64_t>(in.readByte());
            longs[pos] |= highBits << bitsPerValue;
        }
    }

    /** Decode deltas, compute prefix sum, and add base to all decoded longs. */
    void decodeAndPrefixSum(store::IndexInput& in, int64_t base, int64_t* longs) {
        const int token = static_cast<int>(in.readByte());
        const int bitsPerValue = token & 0x1f;
        const int numExceptions = token >> 5;

        if (numExceptions == 0) {
            // No exceptions: can use optimized decode paths
            if (bitsPerValue == 0) {
                const int64_t val = in.readVLong();
                if (val == 1) {
                    prefixSumOfOnes(longs, base);
                } else {
                    prefixSumOf(longs, base, val);
                }
            } else {
                forUtil_.decodeTo32(bitsPerValue, in, longs);
                prefixSum32(longs, base);
            }
        } else {
            // Has exceptions: decode into dual-packed format, patch, then prefix sum
            if (bitsPerValue == 0) {
                fillSameValue32(longs, in.readVLong());
            } else {
                forUtil_.decodeTo32(bitsPerValue, in, longs);
            }
            applyExceptions32(bitsPerValue, numExceptions, in, longs);
            prefixSum32(longs, base);
        }
    }

    /** Skip one block of 128 integers without decoding. */
    void skip(store::IndexInput& in) {
        const int token = static_cast<int>(in.readByte());
        const int bitsPerValue = token & 0x1f;
        const int numExceptions = token >> 5;

        if (bitsPerValue == 0) {
            in.readVLong();
            in.skipBytes(numExceptions * 2);
        } else {
            in.skipBytes(ForUtil::numBytes(bitsPerValue) + numExceptions * 2);
        }
    }

private:
    ForUtil& forUtil_;
    uint8_t exceptionBuff_[MAX_EXCEPTIONS * 2];

    // ==================== Prefix Sum Helpers ====================

    /** Fill longs with base+1, base+2, ..., base+128 (all deltas = 1). */
    static void prefixSumOfOnes(int64_t* longs, int64_t base) {
        for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
            longs[i] = base + i + 1;
        }
    }

    /** Fill longs with base+val, base+2*val, ..., base+128*val (all deltas = val). */
    static void prefixSumOf(int64_t* longs, int64_t base, int64_t val) {
        for (int i = 0; i < ForUtil::BLOCK_SIZE; ++i) {
            longs[i] = static_cast<int64_t>(i + 1) * val + base;
        }
    }

    /** Fill 64 dual-packed longs with the same value in both 32-bit halves. */
    static void fillSameValue32(int64_t* longs, int64_t val) {
        const int64_t packed = (val << 32) | val;
        std::fill(longs, longs + HALF_BLOCK_SIZE, packed);
    }

    // ==================== Exception Patching (Dual-Packed) ====================

    /**
     * Apply exception patches on dual-packed format.
     *
     * Position [0..63] patches the high 32 bits (shift includes +32).
     * Position [64..127] patches the low 32 bits (shift is just bpv).
     */
    void applyExceptions32(int bitsPerValue, int numExceptions,
                           store::IndexInput& in, int64_t* longs) {
        in.readBytes(exceptionBuff_, numExceptions * 2);
        for (int i = 0; i < numExceptions; ++i) {
            const int exceptionPos = static_cast<int>(exceptionBuff_[i * 2]);
            const int64_t exception = static_cast<int64_t>(exceptionBuff_[i * 2 + 1]);
            // Index into the 64-long dual-packed array
            const int idx = exceptionPos & 0x3f;  // mod 64
            // Shift: bpv + 32 for positions [0..63], bpv + 0 for positions [64..127]
            const int shift = bitsPerValue + ((1 ^ (exceptionPos >> 6)) << 5);
            longs[idx] |= exception << shift;
        }
    }

    // ==================== Dual-Packed Prefix Sum ====================

    /**
     * Compute prefix sum on dual-packed format, then expand to 128 values.
     *
     * The dual-packed format stores two parallel streams:
     *   high 32 bits: values [0..63]
     *   low 32 bits:  values [64..127]
     *
     * Adding the base to the high half of longs[0], then running prefix sum
     * on the full 64-bit values computes both streams simultaneously.
     */
    static void prefixSum32(int64_t* longs, int64_t base) {
        // Add base to the high 32-bit half of the first element
        longs[0] += base << 32;

        // Prefix sum on 64 dual-packed longs (both halves simultaneously)
        innerPrefixSum32(longs);

        // Expand dual-packed to 128 individual values
        expand32(longs);

        // The low half (positions [64..127]) needs the 64th value added
        // because the prefix sum only accumulated within each half
        const int64_t carry = longs[HALF_BLOCK_SIZE - 1];
        for (int i = HALF_BLOCK_SIZE; i < ForUtil::BLOCK_SIZE; ++i) {
            longs[i] += carry;
        }
    }

    /** Expand 64 dual-packed longs to 128 individual values. */
    static void expand32(int64_t* longs) {
        for (int i = 0; i < 64; ++i) {
            const uint64_t l = static_cast<uint64_t>(longs[i]);
            longs[i] = static_cast<int64_t>(l >> 32);
            longs[64 + i] = static_cast<int64_t>(l & 0xFFFFFFFFULL);
        }
    }

    /** Prefix sum on 64 dual-packed longs. */
    static void innerPrefixSum32(int64_t* longs) {
        for (int i = 1; i < HALF_BLOCK_SIZE; ++i) {
            longs[i] += longs[i - 1];
        }
    }
};

}  // namespace lucene90
}  // namespace codecs
}  // namespace diagon
