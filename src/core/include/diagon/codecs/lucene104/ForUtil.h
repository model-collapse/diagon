// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

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
 * Lucene 104-compatible ForUtil — bit-packing encoder/decoder for 256 ints.
 *
 * Matches Lucene's exact encoding layout:
 * - bitsPerValue <= 8: pack 4 ints per int32 word (collapse8/expand8)
 * - bitsPerValue <= 16: pack 2 ints per int32 word (collapse16/expand16)
 * - bitsPerValue > 16: scalar encoding
 *
 * Based on: org.apache.lucene.codecs.lucene104.ForUtil
 */
class ForUtil {
public:
    static constexpr int BLOCK_SIZE = 256;
    static constexpr int BLOCK_SIZE_LOG2 = 8;

    /** Number of bytes required to encode 256 integers at given bitsPerValue. */
    static int numBytes(int bitsPerValue) {
        return bitsPerValue << (BLOCK_SIZE_LOG2 - 3);  // bitsPerValue * 256 / 8
    }

    /** Encode 256 ints into output. ints[] may be modified (for collapse). */
    void encode(int32_t* ints, int bitsPerValue, store::IndexOutput& out) {
        int nextPrimitive;
        if (bitsPerValue <= 8) {
            nextPrimitive = 8;
            collapse8(ints);
        } else if (bitsPerValue <= 16) {
            nextPrimitive = 16;
            collapse16(ints);
        } else {
            nextPrimitive = 32;
        }
        encodeInner(ints, bitsPerValue, nextPrimitive, out, tmp_);
    }

    /** Decode 256 ints from input. */
    void decode(int bitsPerValue, store::IndexInput& in, int32_t* ints) {
        if (bitsPerValue <= 8) {
            decodeGeneric(bitsPerValue, 8, in, ints);
            expand8(ints);
        } else if (bitsPerValue <= 16) {
            decodeGeneric(bitsPerValue, 16, in, ints);
            expand16(ints);
        } else {
            decodeGeneric(bitsPerValue, 32, in, ints);
        }
    }

private:
    int32_t tmp_[BLOCK_SIZE];

    // ==================== Mask Tables ====================

    static int32_t mask32(int bits) { return (1 << bits) - 1; }

    static int32_t expandMask16(int mask16) { return mask16 | (mask16 << 16); }
    static int32_t expandMask8(int mask8) { return expandMask16(mask8 | (mask8 << 8)); }

    static int32_t mask16val(int bits) { return expandMask16((1 << bits) - 1); }
    static int32_t mask8val(int bits) { return expandMask8((1 << bits) - 1); }

    static int32_t getMask(int bits, int primitiveSize) {
        if (primitiveSize == 8) return mask8val(bits);
        if (primitiveSize == 16) return mask16val(bits);
        return mask32(bits);
    }

    // ==================== Collapse/Expand ====================

    /** Pack 4 bytes per int32 word — Lucene's collapse8. */
    static void collapse8(int32_t* arr) {
        for (int i = 0; i < 64; ++i) {
            arr[i] = (arr[i] << 24) | (arr[64 + i] << 16) | (arr[128 + i] << 8) | arr[192 + i];
        }
    }

    /** Unpack 4 bytes per int32 word — Lucene's expand8. */
    static void expand8(int32_t* arr) {
        for (int i = 63; i >= 0; --i) {
            int32_t l = arr[i];
            arr[i] = (l >> 24) & 0xFF;
            arr[64 + i] = (l >> 16) & 0xFF;
            arr[128 + i] = (l >> 8) & 0xFF;
            arr[192 + i] = l & 0xFF;
        }
    }

    /** Pack 2 shorts per int32 word — Lucene's collapse16. */
    static void collapse16(int32_t* arr) {
        for (int i = 0; i < 128; ++i) {
            arr[i] = (arr[i] << 16) | arr[128 + i];
        }
    }

    /** Unpack 2 shorts per int32 word — Lucene's expand16. */
    static void expand16(int32_t* arr) {
        for (int i = 127; i >= 0; --i) {
            int32_t l = arr[i];
            arr[i] = (l >> 16) & 0xFFFF;
            arr[128 + i] = l & 0xFFFF;
        }
    }

    // ==================== Encode Inner ====================

    static void encodeInner(int32_t* ints, int bitsPerValue, int primitiveSize,
                            store::IndexOutput& out, int32_t* tmp) {
        const int numInts = BLOCK_SIZE * primitiveSize / 32;
        const int numIntsPerShift = bitsPerValue * 8;

        int idx = 0;
        int shift = primitiveSize - bitsPerValue;
        for (int i = 0; i < numIntsPerShift; ++i) {
            tmp[i] = ints[idx++] << shift;
        }
        for (shift = shift - bitsPerValue; shift >= 0; shift -= bitsPerValue) {
            for (int i = 0; i < numIntsPerShift; ++i) {
                tmp[i] |= ints[idx++] << shift;
            }
        }

        const int remainingBitsPerInt = shift + bitsPerValue;
        const int32_t maskRemainingBitsPerInt = getMask(remainingBitsPerInt, primitiveSize);

        int tmpIdx = 0;
        int remainingBitsPerValue = bitsPerValue;
        while (idx < numInts) {
            if (remainingBitsPerValue >= remainingBitsPerInt) {
                remainingBitsPerValue -= remainingBitsPerInt;
                tmp[tmpIdx++] |= (ints[idx] >> remainingBitsPerValue) & maskRemainingBitsPerInt;
                if (remainingBitsPerValue == 0) {
                    idx++;
                    remainingBitsPerValue = bitsPerValue;
                }
            } else {
                const int32_t mask1 = getMask(remainingBitsPerValue, primitiveSize);
                const int32_t mask2 = getMask(remainingBitsPerInt - remainingBitsPerValue, primitiveSize);
                tmp[tmpIdx] |= (ints[idx++] & mask1) << (remainingBitsPerInt - remainingBitsPerValue);
                remainingBitsPerValue = bitsPerValue - remainingBitsPerInt + remainingBitsPerValue;
                tmp[tmpIdx++] |= (ints[idx] >> remainingBitsPerValue) & mask2;
            }
        }

        // Write as big-endian int32s (Lucene's writeInt is big-endian)
        for (int i = 0; i < numIntsPerShift; ++i) {
            out.writeInt(tmp[i]);
        }
    }

    // ==================== Decode ====================

    /**
     * Generic decode matching Lucene's splitInts approach.
     *
     * Algorithm:
     * 1. Read numIntsPerShift packed int32 words into tmp_
     * 2. Extract values at multiple shift levels (like Lucene's splitInts)
     * 3. If bpv doesn't divide primitiveSize evenly, reassemble remainder
     *    values from leftover bits (like Lucene's decodeSlow remainder loop)
     */
    void decodeGeneric(int bitsPerValue, int primitiveSize, store::IndexInput& in, int32_t* ints) {
        const int numIntsPerShift = bitsPerValue * 8;
        const int numIntsTotal = BLOCK_SIZE * primitiveSize / 32;

        // Read packed int32 words into tmp_
        for (int i = 0; i < numIntsPerShift; ++i) {
            tmp_[i] = in.readInt();
        }

        // Special case: bpv == primitiveSize (e.g., bpv=8/prim=8 or bpv=16/prim=16)
        if (bitsPerValue == primitiveSize) {
            for (int i = 0; i < numIntsPerShift; ++i) {
                ints[i] = tmp_[i];
            }
            return;
        }

        const int32_t mask = getMask(bitsPerValue, primitiveSize);
        const int bShift = primitiveSize - bitsPerValue;

        // Extract values at each shift level (Lucene's splitInts pattern)
        const int maxIter = (bShift - 1) / bitsPerValue;
        for (int j = 0; j <= maxIter; ++j) {
            const int shift = bShift - j * bitsPerValue;
            const int bOffset = numIntsPerShift * j;
            for (int i = 0; i < numIntsPerShift; ++i) {
                ints[bOffset + i] = (static_cast<uint32_t>(tmp_[i]) >> shift) & mask;
            }
        }

        int outIdx = numIntsPerShift * (maxIter + 1);

        // Calculate remaining bits per packed word after all shift extractions
        const int remainingBitsPerInt = bShift - maxIter * bitsPerValue;

        if (remainingBitsPerInt == 0 || outIdx >= numIntsTotal) {
            return;  // Perfect division, all values extracted
        }

        // Apply cMask to keep only remaining low bits in tmp_
        const int32_t cMask = getMask(remainingBitsPerInt, primitiveSize);
        for (int i = 0; i < numIntsPerShift; ++i) {
            tmp_[i] &= cMask;
        }

        // Reassemble remaining values from leftover bits (decodeSlow remainder pattern)
        int tmpIdx = 0;
        int bitsLeft = remainingBitsPerInt;
        for (int intsIdx = outIdx; intsIdx < numIntsTotal; ++intsIdx) {
            int b = bitsPerValue - bitsLeft;
            int32_t l = (tmp_[tmpIdx++] & getMask(bitsLeft, primitiveSize)) << b;
            while (b >= remainingBitsPerInt) {
                b -= remainingBitsPerInt;
                l |= (tmp_[tmpIdx++] & cMask) << b;
            }
            if (b > 0) {
                l |= (static_cast<uint32_t>(tmp_[tmpIdx]) >> (remainingBitsPerInt - b))
                     & getMask(b, primitiveSize);
                bitsLeft = remainingBitsPerInt - b;
            } else {
                bitsLeft = remainingBitsPerInt;
            }
            ints[intsIdx] = l;
        }
    }
};

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
