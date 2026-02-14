// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/StreamVByte.h"

#include "diagon/util/StreamVByteTables.h"  // Precomputed lookup tables

#include <algorithm>
#include <cstring>

// SIMD intrinsics are included via SIMDUtils.h (included by StreamVByte.h)
// No need to re-include or re-define macros here

namespace diagon {
namespace util {

// ==================== Encoding ====================

int StreamVByte::encode(const uint32_t* values, int count, uint8_t* output) {
    if (count <= 0 || count > 4) {
        return 0;  // Invalid count
    }

    // Determine lengths
    int lengths[4] = {0, 0, 0, 0};
    for (int i = 0; i < count; ++i) {
        lengths[i] = encodedSize(values[i]);
    }

    // Build control byte
    uint8_t control;
    if (count == 4) {
        control = buildControl(lengths[0], lengths[1], lengths[2], lengths[3]);
    } else {
        // Pad with 1-byte lengths for remainder
        control = buildControl(count > 0 ? lengths[0] : 1, count > 1 ? lengths[1] : 1,
                               count > 2 ? lengths[2] : 1, count > 3 ? lengths[3] : 1);
    }

    output[0] = control;
    int offset = 1;

    // Write data bytes
    for (int i = 0; i < count; ++i) {
        uint32_t value = values[i];
        for (int j = 0; j < lengths[i]; ++j) {
            output[offset++] = static_cast<uint8_t>(value & 0xFF);
            value >>= 8;
        }
    }

    return offset;
}

int StreamVByte::encodedSizeArray(const uint32_t* values, int count) {
    int total = 0;
    int i = 0;

    // Process groups of 4
    while (i + 4 <= count) {
        total += 1;  // Control byte
        for (int j = 0; j < 4; ++j) {
            total += encodedSize(values[i + j]);
        }
        i += 4;
    }

    // Handle remainder
    if (i < count) {
        total += 1;  // Control byte
        while (i < count) {
            total += encodedSize(values[i]);
            ++i;
        }
    }

    return total;
}

// ==================== Scalar Decode (Fallback) ====================

int StreamVByte::decode4_scalar(const uint8_t* input, uint32_t* output) {
    uint8_t control = input[0];
    int offset = 1;

    // Decode 4 integers using control byte
    for (int i = 0; i < 4; ++i) {
        int length = getLength(control, i);
        uint32_t value = 0;

        // Read bytes in little-endian order
        for (int j = 0; j < length; ++j) {
            value |= static_cast<uint32_t>(input[offset + j]) << (j * 8);
        }

        output[i] = value;
        offset += length;
    }

    return offset;
}

// ==================== SSE4.2 Decode ====================

#if defined(__SSE4_2__) || defined(__AVX2__)

int StreamVByte::decode4_SSE(const uint8_t* input, uint32_t* output) {
    uint8_t control = input[0];
    const uint8_t* data = input + 1;

    // ✅ FAST: Load precomputed shuffle mask (1 cycle, not 20-30 cycles!)
    __m128i mask_vec = _mm_load_si128(
        reinterpret_cast<const __m128i*>(StreamVByteTables::SSE_MASKS[control]));

    // Load data bytes (up to 16 bytes)
    __m128i data_vec = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data));

    // Shuffle bytes to extract 4 integers (PSHUFB: 1 cycle)
    __m128i result = _mm_shuffle_epi8(data_vec, mask_vec);

    // Store result
    _mm_storeu_si128(reinterpret_cast<__m128i*>(output), result);

    // ✅ FAST: Lookup data length (1 cycle, not loop!)
    return 1 + StreamVByteTables::DATA_LENGTHS[control];
}

#endif

// ==================== AVX2 Decode ====================

#if defined(__AVX2__)

int StreamVByte::decode4_AVX2(const uint8_t* input, uint32_t* output) {
    // For 4 integers, SSE is sufficient and actually faster (smaller register)
    // AVX2 shines when decoding 8+ integers (see decode8_AVX2)
    return decode4_SSE(input, output);
}

int StreamVByte::decode8_AVX2(const uint8_t* input, uint32_t* output) {
    // Decode two groups of 4 integers using AVX2 (true 8-wide decode)
    // This processes 2 control bytes and decodes 8 integers in parallel

    uint8_t control0 = input[0];
    int dataLen0 = StreamVByteTables::DATA_LENGTHS[control0];

    uint8_t control1 = input[1 + dataLen0];
    int dataLen1 = StreamVByteTables::DATA_LENGTHS[control1];

    // Decode first group of 4 (lower 128 bits)
    __m128i mask0_vec = _mm_load_si128(
        reinterpret_cast<const __m128i*>(StreamVByteTables::SSE_MASKS[control0]));
    __m128i data0_vec = _mm_loadu_si128(reinterpret_cast<const __m128i*>(input + 1));
    __m128i result0 = _mm_shuffle_epi8(data0_vec, mask0_vec);

    // Decode second group of 4 (upper 128 bits)
    __m128i mask1_vec = _mm_load_si128(
        reinterpret_cast<const __m128i*>(StreamVByteTables::SSE_MASKS[control1]));
    __m128i data1_vec = _mm_loadu_si128(reinterpret_cast<const __m128i*>(input + 1 + dataLen0 + 1));
    __m128i result1 = _mm_shuffle_epi8(data1_vec, mask1_vec);

    // Combine into single AVX2 register and store
    __m256i result = _mm256_set_m128i(result1, result0);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(output), result);

    return 2 + dataLen0 + dataLen1;  // Two control bytes + data
}

#endif

// ==================== ARM NEON Decode ====================

#if defined(__ARM_NEON)

int StreamVByte::decode4_NEON(const uint8_t* input, uint32_t* output) {
    uint8_t control = input[0];
    const uint8_t* data = input + 1;

    // NEON doesn't have variable byte shuffle like PSHUFB
    // Use table lookup (vtbl) with 4 separate lookups
    // This is less efficient than x86 PSHUFB, but still faster than scalar

    int lengths[4];
    int offsets[4];
    int offset = 0;
    for (int i = 0; i < 4; ++i) {
        lengths[i] = getLength(control, i);
        offsets[i] = offset;
        offset += lengths[i];
    }

    // Decode each integer (NEON doesn't have good variable-length load)
    for (int i = 0; i < 4; ++i) {
        uint32_t value = 0;
        for (int j = 0; j < lengths[i]; ++j) {
            value |= static_cast<uint32_t>(data[offsets[i] + j]) << (j * 8);
        }
        output[i] = value;
    }

    return 1 + offset;
}

#endif

// ==================== Dispatch to Best Implementation ====================

int StreamVByte::decode4(const uint8_t* input, uint32_t* output) {
#if defined(__AVX2__)
    return decode4_AVX2(input, output);
#elif defined(__SSE4_2__)
    return decode4_SSE(input, output);
#elif defined(__ARM_NEON)
    return decode4_NEON(input, output);
#else
    return decode4_scalar(input, output);
#endif
}

// ==================== Bulk Decode ====================

int StreamVByte::decodeBulk(const uint8_t* input, int count, uint32_t* output) {
    if (count % 4 != 0) {
        // Count must be multiple of 4 for bulk decode
        return -1;
    }

    const uint8_t* ptr = input;
    for (int i = 0; i < count; i += 4) {
        int bytes = decode4(ptr, output + i);
        ptr += bytes;
    }

    return static_cast<int>(ptr - input);
}

// ==================== Flexible Decode ====================

int StreamVByte::decode(const uint8_t* input, int count, uint32_t* output) {
    const uint8_t* ptr = input;
    int i = 0;

    // Process groups of 4 using SIMD
    while (i + 4 <= count) {
        int bytes = decode4(ptr, output + i);
        ptr += bytes;
        i += 4;
    }

    // Handle remainder with scalar decode
    while (i < count) {
        uint8_t control = ptr[0];
        int offset = 1;

        // Decode remaining integers (1-3)
        int remaining = std::min(4, count - i);
        for (int j = 0; j < remaining; ++j) {
            int length = getLength(control, j);
            uint32_t value = 0;
            for (int k = 0; k < length; ++k) {
                value |= static_cast<uint32_t>(ptr[offset + k]) << (k * 8);
            }
            output[i + j] = value;
            offset += length;
        }

        ptr += offset;
        i += remaining;
    }

    return static_cast<int>(ptr - input);
}

}  // namespace util
}  // namespace diagon
