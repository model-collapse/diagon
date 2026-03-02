// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>

namespace diagon {
namespace util {

/**
 * PFOR-Delta encoder/decoder for 128-element blocks.
 *
 * Encodes N uint32 values using patched frame-of-reference delta encoding:
 * - Chooses a base bit width covering most values (≤7 exceptions allowed)
 * - Exception values store overflow bits as (index, highBits) pairs
 *
 * Format:
 *   Token byte: (numExceptions << 5) | patchedBitsRequired
 *     - Low 5 bits: base bit width (0-31)
 *     - High 3 bits: number of exceptions (0-7)
 *
 *   If token == 0 (bpv=0, numEx=0):
 *     - All values are equal
 *     - Followed by VInt(value)
 *
 *   Otherwise:
 *     - Packed data: ceil(count * bpv / 8) bytes
 *     - Exception pairs: numExceptions * 2 bytes, each (index, highBits)
 *
 * Based on: org.apache.lucene.util.packed.PForUtil (Lucene's PFOR encoding)
 */
class BitPacking {
public:
    /// Standard block size for postings
    static constexpr int BLOCK_SIZE = 128;

    /// Maximum exceptions in a PFOR block
    static constexpr int MAX_EXCEPTIONS = 7;

    /**
     * Encode `count` uint32 values using PFOR-Delta.
     *
     * NOTE: This function MAY modify the values array (masking exception bits).
     *
     * @param values Input values to encode (may be modified)
     * @param count Number of values (typically 128)
     * @param output Output buffer (must be at least maxBytesPerBlock() bytes)
     * @return Number of bytes written to output
     */
    static int encode(uint32_t* values, int count, uint8_t* output);

    /**
     * Decode `count` values from PFOR-Delta packed input.
     *
     * @param input Packed input data (starts with token byte)
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
     * = 1 (token) + ceil(count * 31 / 8) + 14 (max exceptions)
     */
    static constexpr int maxBytesPerBlock(int count) {
        return 1 + (count * 31 + 7) / 8 + MAX_EXCEPTIONS * 2;
    }
};

}  // namespace util
}  // namespace diagon
