// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/util/SIMDUtils.h"

#include <cstdint>
#include <cstring>

namespace diagon {
namespace util {

/**
 * StreamVByte: SIMD-accelerated variable-byte integer encoding
 *
 * Based on the StreamVByte algorithm by Daniel Lemire et al.
 * https://arxiv.org/abs/1709.08990
 *
 * Key differences from standard VByte:
 * - Control byte: Stores lengths of 4 integers (2 bits each)
 * - Data bytes: Packed data follows control byte
 * - SIMD decode: Uses shuffle to decode 4 integers in parallel
 *
 * Performance: 2-3× faster than scalar VByte for bulk decoding
 *
 * Format:
 *   [control_byte] [data_bytes...] [control_byte] [data_bytes...] ...
 *
 * Control byte layout (2 bits per integer length):
 *   Bits [1:0] = length-1 of integer 0  (0=1 byte, 1=2 bytes, 2=3 bytes, 3=4 bytes)
 *   Bits [3:2] = length-1 of integer 1
 *   Bits [5:4] = length-1 of integer 2
 *   Bits [7:6] = length-1 of integer 3
 */
class StreamVByte {
public:
    /**
     * Encode up to 4 unsigned 32-bit integers using StreamVByte format
     *
     * @param values Array of values to encode (must have at least count elements)
     * @param count Number of values to encode (1-4)
     * @param output Buffer to write to (must have at least 17 bytes: 1 control + 16 data)
     * @return Number of bytes written
     */
    static int encode(const uint32_t* values, int count, uint8_t* output);

    /**
     * Decode 4 unsigned 32-bit integers using SIMD (AVX2/SSE/NEON)
     *
     * Fastest path: decodes 4 integers in parallel using SIMD shuffle.
     *
     * @param input Buffer to read from (must have control byte + data)
     * @param output Array to write 4 decoded values
     * @return Number of bytes consumed from input
     */
    static int decode4(const uint8_t* input, uint32_t* output);

    /**
     * Decode N integers (multiple of 4) using SIMD bulk decode
     *
     * Processes 4 integers at a time for maximum performance.
     *
     * @param input Buffer to read from
     * @param count Number of integers to decode (should be multiple of 4)
     * @param output Array to write decoded values (must have space for count integers)
     * @return Number of bytes consumed from input
     */
    static int decodeBulk(const uint8_t* input, int count, uint32_t* output);

    /**
     * Decode variable number of integers (handles remainder)
     *
     * Uses SIMD for groups of 4, scalar for remainder.
     *
     * @param input Buffer to read from
     * @param count Number of integers to decode
     * @param output Array to write decoded values
     * @return Number of bytes consumed from input
     */
    static int decode(const uint8_t* input, int count, uint32_t* output);

    /**
     * Get encoded size for a single value
     *
     * @param value Value to encode
     * @return Number of bytes needed (1-4)
     */
    static inline int encodedSize(uint32_t value) {
        if (value < (1U << 8)) return 1;
        if (value < (1U << 16)) return 2;
        if (value < (1U << 24)) return 3;
        return 4;
    }

    /**
     * Get total encoded size for array of values
     *
     * @param values Array of values
     * @param count Number of values
     * @return Total bytes needed (control bytes + data bytes)
     */
    static int encodedSizeArray(const uint32_t* values, int count);

private:
    // Platform-specific decode implementations
    static int decode4_AVX2(const uint8_t* input, uint32_t* output);
    static int decode4_SSE(const uint8_t* input, uint32_t* output);
    static int decode4_NEON(const uint8_t* input, uint32_t* output);
    static int decode4_scalar(const uint8_t* input, uint32_t* output);

    // Helper: Extract length from control byte
    static inline int getLength(uint8_t control, int index) {
        return ((control >> (index * 2)) & 0x3) + 1;
    }

    // Helper: Build control byte from lengths
    static inline uint8_t buildControl(int len0, int len1, int len2, int len3) {
        return static_cast<uint8_t>(
            ((len0 - 1) << 0) |
            ((len1 - 1) << 2) |
            ((len2 - 1) << 4) |
            ((len3 - 1) << 6)
        );
    }

    // Shuffle masks for SIMD decode (indexed by control byte)
    // These are precomputed lookup tables for fast decoding
    static const uint8_t SHUFFLE_MASKS_AVX2[256][32];
    static const uint8_t SHUFFLE_MASKS_SSE[256][16];
};

}  // namespace util
}  // namespace diagon
