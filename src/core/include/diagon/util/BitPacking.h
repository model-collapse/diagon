// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>

namespace diagon {
namespace util {

/**
 * Bit-packing encoder/decoder for 128-element blocks.
 *
 * Encodes N uint32 values using the minimum bits-per-value needed
 * to represent the maximum value in the block.
 *
 * Format: [1 byte: bitsPerValue] [packed data: ceil(count * bitsPerValue / 8) bytes]
 *
 * Special case: bitsPerValue == 0 means all values are 0 (1 byte total).
 *
 * Based on: org.apache.lucene.util.packed.PackedInts (Lucene's ForUtil)
 */
class BitPacking {
public:
    /// Standard block size for postings
    static constexpr int BLOCK_SIZE = 128;

    /**
     * Encode `count` uint32 values using minimum bits-per-value.
     *
     * @param values Input values to encode
     * @param count Number of values (typically 128)
     * @param output Output buffer (must be at least maxBytesPerBlock() bytes)
     * @return Number of bytes written to output
     */
    static int encode(const uint32_t* values, int count, uint8_t* output);

    /**
     * Decode `count` values from packed input.
     *
     * @param input Packed input data (starts with bitsPerValue byte)
     * @param count Number of values to decode
     * @param values Output buffer for decoded values
     * @return Number of bytes consumed from input
     */
    static int decode(const uint8_t* input, int count, uint32_t* values);

    /**
     * Calculate minimum bits needed to represent a value.
     *
     * @param maxValue Maximum value to represent
     * @return Bits per value (0 for maxValue==0, 1-32 otherwise)
     */
    static int bitsRequired(uint32_t maxValue);

    /**
     * Maximum bytes a single encode() call can produce for given count.
     * = 1 (header) + ceil(count * 32 / 8) = 1 + count * 4
     */
    static constexpr int maxBytesPerBlock(int count) { return 1 + count * 4; }
};

}  // namespace util
}  // namespace diagon
