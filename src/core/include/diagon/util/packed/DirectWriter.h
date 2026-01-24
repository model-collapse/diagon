// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/store/IndexInput.h"
#include "diagon/store/IndexOutput.h"

#include <cstdint>
#include <vector>

namespace diagon {
namespace util {
namespace packed {

/**
 * Bit-packing utility for writing fixed bit-width integers.
 *
 * Based on: org.apache.lucene.util.packed.DirectWriter
 *
 * Encodes a sequence of integers where each value uses the same
 * number of bits (bitsPerValue). Values are packed sequentially
 * into the output stream.
 *
 * Example: 5 values, 3 bits each
 * Values: [3, 7, 1, 5, 2]
 * Binary: 011 111 001 101 010
 * Output: 01111100 11010100 (2 bytes, padded)
 */
class DirectWriter {
public:
    /**
     * Calculate bits required to represent a value.
     *
     * @param value Maximum value to encode
     * @return Bits required (1-64)
     */
    static int bitsRequired(int64_t value);

    /**
     * Calculate bits required for unsigned values.
     *
     * @param value Maximum unsigned value
     * @return Bits required (0-64)
     */
    static int unsignedBitsRequired(uint64_t value);

    /**
     * Create a DirectWriter.
     *
     * @param output Output stream to write to
     * @param numValues Number of values to write
     * @param bitsPerValue Bits per value (1-64)
     */
    DirectWriter(store::IndexOutput* output, int64_t numValues, int bitsPerValue);

    /**
     * Write a value.
     * Must be called exactly numValues times.
     *
     * @param value Value to write (must fit in bitsPerValue bits)
     */
    void add(int64_t value);

    /**
     * Finish writing and flush any pending bits.
     */
    void finish();

private:
    store::IndexOutput* output_;
    int64_t numValues_;
    int bitsPerValue_;
    int64_t count_;

    // Buffer for accumulating bits
    uint64_t buffer_;
    int bufferSize_;  // Bits currently in buffer

    // For byte-aligned fast path
    bool byteAligned_;
    std::vector<uint8_t> byteBuffer_;

    void flushBuffer();
    void writeByteFastPath(int64_t value);
};

/**
 * Bit-unpacking utility for reading fixed bit-width integers.
 *
 * Based on: org.apache.lucene.util.packed.DirectReader
 */
class DirectReader {
public:
    /**
     * Read values from input.
     *
     * @param input Input stream to read from
     * @param bitsPerValue Bits per value (1-64)
     * @param count Number of values to read
     * @return Vector of decoded values
     */
    static std::vector<int64_t> read(store::IndexInput* input, int bitsPerValue, int64_t count);

    /**
     * Read a single value at a given index without reading preceding values.
     *
     * @param input Input stream
     * @param bitsPerValue Bits per value
     * @param index Index of value to read
     * @return Decoded value
     */
    static int64_t getInstance(store::IndexInput* input, int bitsPerValue, int64_t index);

private:
    static int64_t readValue(store::IndexInput* input, int bitsPerValue, int64_t bitPosition);
};

}  // namespace packed
}  // namespace util
}  // namespace diagon
