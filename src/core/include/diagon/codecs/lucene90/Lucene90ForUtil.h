// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/store/IndexInput.h"

#include <cassert>
#include <cstdint>
#include <cstring>

namespace diagon {
namespace codecs {
namespace lucene90 {

/**
 * Lucene 90-compatible ForUtil — bit-packing decoder for 128 integers.
 *
 * Decodes packed integer blocks stored as big-endian 64-bit longs.
 * Uses SIMD-friendly shift-and-mask pattern with configurable primitive size:
 *   - bpv 1-8:   8-bit primitives (8 values packed per long)
 *   - bpv 9-16: 16-bit primitives (4 values packed per long)
 *   - bpv 17-31: 32-bit primitives (2 values packed per long)
 *
 * Decode-only (for reading OpenSearch 2.11 / Lucene 9.x indices).
 *
 * Based on: org.apache.lucene.backward_codecs.lucene90.ForUtil
 */
class ForUtil {
public:
    static constexpr int BLOCK_SIZE = 128;
    static constexpr int BLOCK_SIZE_LOG2 = 7;

    /** Number of bytes required to encode 128 integers at given bitsPerValue. */
    static int numBytes(int bitsPerValue) {
        return bitsPerValue << (BLOCK_SIZE_LOG2 - 3);  // bpv * 16
    }

    /** Decode 128 integers into longs[128]. */
    void decode(int bitsPerValue, store::IndexInput& in, int64_t* longs) {
        if (bitsPerValue <= 8) {
            decodeGeneric(bitsPerValue, 8, in, longs);
            expand8(longs);
        } else if (bitsPerValue <= 16) {
            decodeGeneric(bitsPerValue, 16, in, longs);
            expand16(longs);
        } else {
            decodeGeneric(bitsPerValue, 32, in, longs);
            expand32(longs);
        }
    }

    /**
     * Decode 128 integers into 64 longs, each containing two 32-bit values.
     * Values [0..63] in high 32 bits, [64..127] in low 32 bits.
     * Used by PForUtil for efficient dual-packed prefix sum.
     */
    void decodeTo32(int bitsPerValue, store::IndexInput& in, int64_t* longs) {
        if (bitsPerValue <= 8) {
            decodeGeneric(bitsPerValue, 8, in, longs);
            expand8To32(longs);
        } else if (bitsPerValue <= 16) {
            decodeGeneric(bitsPerValue, 16, in, longs);
            expand16To32(longs);
        } else {
            decodeGeneric(bitsPerValue, 32, in, longs);
            // Already in 2-per-long format, no expand needed
        }
    }

private:
    int64_t tmp_[BLOCK_SIZE / 2];  // 64 longs scratch buffer

    // ==================== Mask Tables ====================
    // Masks with bits replicated at each primitive-sized position within a 64-bit long.

    static constexpr int64_t expandMask32(int64_t mask32) {
        return mask32 | (mask32 << 32);
    }

    static constexpr int64_t expandMask16(int64_t mask16) {
        return expandMask32(mask16 | (mask16 << 16));
    }

    static constexpr int64_t expandMask8(int64_t mask8) {
        return expandMask16(mask8 | (mask8 << 8));
    }

    static int64_t mask32(int bits) {
        return bits == 0 ? 0 : expandMask32((1LL << bits) - 1);
    }

    static int64_t mask16(int bits) {
        return bits == 0 ? 0 : expandMask16((1LL << bits) - 1);
    }

    static int64_t mask8(int bits) {
        return bits == 0 ? 0 : expandMask8((1LL << bits) - 1);
    }

    static int64_t getMask(int bits, int primSize) {
        if (primSize == 8) return mask8(bits);
        if (primSize == 16) return mask16(bits);
        return mask32(bits);
    }

    // ==================== Expand Functions ====================
    // Convert from packed-primitive format to individual values.

    /** Expand 16 packed longs (8 values each) to 128 individual values. */
    static void expand8(int64_t* arr) {
        for (int i = 0; i < 16; ++i) {
            uint64_t l = static_cast<uint64_t>(arr[i]);
            arr[i]       = static_cast<int64_t>((l >> 56) & 0xFFULL);
            arr[16 + i]  = static_cast<int64_t>((l >> 48) & 0xFFULL);
            arr[32 + i]  = static_cast<int64_t>((l >> 40) & 0xFFULL);
            arr[48 + i]  = static_cast<int64_t>((l >> 32) & 0xFFULL);
            arr[64 + i]  = static_cast<int64_t>((l >> 24) & 0xFFULL);
            arr[80 + i]  = static_cast<int64_t>((l >> 16) & 0xFFULL);
            arr[96 + i]  = static_cast<int64_t>((l >> 8) & 0xFFULL);
            arr[112 + i] = static_cast<int64_t>(l & 0xFFULL);
        }
    }

    /** Expand 32 packed longs (4 values each) to 128 individual values. */
    static void expand16(int64_t* arr) {
        for (int i = 0; i < 32; ++i) {
            uint64_t l = static_cast<uint64_t>(arr[i]);
            arr[i]       = static_cast<int64_t>((l >> 48) & 0xFFFFULL);
            arr[32 + i]  = static_cast<int64_t>((l >> 32) & 0xFFFFULL);
            arr[64 + i]  = static_cast<int64_t>((l >> 16) & 0xFFFFULL);
            arr[96 + i]  = static_cast<int64_t>(l & 0xFFFFULL);
        }
    }

    /** Expand 64 packed longs (2 values each) to 128 individual values. */
    static void expand32(int64_t* arr) {
        for (int i = 0; i < 64; ++i) {
            uint64_t l = static_cast<uint64_t>(arr[i]);
            arr[i]       = static_cast<int64_t>(l >> 32);
            arr[64 + i]  = static_cast<int64_t>(l & 0xFFFFFFFFULL);
        }
    }

    /** Expand 16 packed longs (8 values each) to 64 dual-packed longs (32-bit each). */
    static void expand8To32(int64_t* arr) {
        for (int i = 0; i < 16; ++i) {
            uint64_t l = static_cast<uint64_t>(arr[i]);
            arr[i]       = static_cast<int64_t>((l >> 24) & 0x000000FF000000FFULL);
            arr[16 + i]  = static_cast<int64_t>((l >> 16) & 0x000000FF000000FFULL);
            arr[32 + i]  = static_cast<int64_t>((l >> 8) & 0x000000FF000000FFULL);
            arr[48 + i]  = static_cast<int64_t>(l & 0x000000FF000000FFULL);
        }
    }

    /** Expand 32 packed longs (4 values each) to 64 dual-packed longs (32-bit each). */
    static void expand16To32(int64_t* arr) {
        for (int i = 0; i < 32; ++i) {
            uint64_t l = static_cast<uint64_t>(arr[i]);
            arr[i]       = static_cast<int64_t>((l >> 16) & 0x0000FFFF0000FFFFULL);
            arr[32 + i]  = static_cast<int64_t>(l & 0x0000FFFF0000FFFFULL);
        }
    }

    // ==================== Shift Helper ====================

    /**
     * SIMD-friendly shift pattern.
     * The C++ compiler can auto-vectorize this loop for SIMD execution.
     * Matches Java's shiftLongs() which is recognized by C2 JIT.
     */
    static void shiftLongs(const int64_t* a, int count,
                           int64_t* b, int bi, int shift, int64_t mask) {
        for (int i = 0; i < count; ++i) {
            b[bi + i] = static_cast<int64_t>(
                (static_cast<uint64_t>(a[i]) >> shift) & static_cast<uint64_t>(mask));
        }
    }

    // ==================== Read Helper ====================

    static void readLongs(store::IndexInput& in, int64_t* dst, int count) {
        for (int i = 0; i < count; ++i) {
            dst[i] = in.readLong();
        }
    }

    // ==================== Generic Decode ====================

    /**
     * Generic decode for any bpv and primitive size.
     *
     * Algorithm (matches Java ForUtil decode1-24 + decodeSlow):
     * 1. Read bpv*2 longs from input
     * 2. Extract values at each shift level using SIMD-friendly mask pattern
     * 3. Handle remainder bits via cross-word reassembly
     *
     * Output: packed longs in the primitive-sized format:
     *   primSize=8:  16 longs (8 values each)
     *   primSize=16: 32 longs (4 values each)
     *   primSize=32: 64 longs (2 values each)
     */
    void decodeGeneric(int bitsPerValue, int primSize,
                       store::IndexInput& in, int64_t* longs) {
        const int numLongs = bitsPerValue * 2;
        readLongs(in, tmp_, numLongs);

        const int numOutputLongs = BLOCK_SIZE * primSize / 64;

        // Special case: bpv == primitive size (e.g., bpv=8, bpv=16)
        // Just copy the raw longs — no shifting needed.
        if (bitsPerValue == primSize) {
            std::memcpy(longs, tmp_, numOutputLongs * sizeof(int64_t));
            return;
        }

        const int64_t mask = getMask(bitsPerValue, primSize);
        int outIdx = 0;
        int shift = primSize - bitsPerValue;

        // Extract values at each shift level (SIMD-friendly pattern)
        for (; shift >= 0; shift -= bitsPerValue) {
            shiftLongs(tmp_, numLongs, longs, outIdx, shift, mask);
            outIdx += numLongs;
        }

        if (outIdx >= numOutputLongs) return;

        // Handle remainder bits via cross-word reassembly
        const int remainingBitsPerLong = shift + bitsPerValue;
        const int64_t maskRemaining = getMask(remainingBitsPerLong, primSize);
        int tmpIdx = 0;
        int remainingBits = remainingBitsPerLong;

        for (; outIdx < numOutputLongs; ++outIdx) {
            int b = bitsPerValue - remainingBits;
            int64_t l = (tmp_[tmpIdx++] & getMask(remainingBits, primSize)) << b;

            while (b >= remainingBitsPerLong) {
                b -= remainingBitsPerLong;
                l |= (tmp_[tmpIdx++] & maskRemaining) << b;
            }

            if (b > 0) {
                l |= static_cast<int64_t>(
                    (static_cast<uint64_t>(tmp_[tmpIdx]) >> (remainingBitsPerLong - b))
                    & static_cast<uint64_t>(getMask(b, primSize)));
                remainingBits = remainingBitsPerLong - b;
            } else {
                remainingBits = remainingBitsPerLong;
            }
            longs[outIdx] = l;
        }
    }
};

}  // namespace lucene90
}  // namespace codecs
}  // namespace diagon
