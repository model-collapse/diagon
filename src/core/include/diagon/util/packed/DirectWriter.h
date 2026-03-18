// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/store/IndexInput.h"
#include "diagon/store/IndexOutput.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace diagon {
namespace util {
namespace packed {

/**
 * Bit-packing utility for writing fixed bit-width integers.
 *
 * Based on: org.apache.lucene.util.packed.DirectWriter
 *
 * Lucene-compatible encoding:
 * - Little-endian byte order for all packed data
 * - Only supported bpv values: {1,2,4,8,12,16,20,24,28,32,40,48,56,64}
 * - Three encoding paths: byte-aligned (bpv%8==0), sub-byte (<8), pair-packed (12/20/28)
 * - Padding bytes at finish() for fast reader I/O (up to 3 bytes)
 * - Buffered: accumulates values, encodes in blocks, writes to output
 */
class DirectWriter {
public:
    /** Supported bits-per-value values (must use one of these). */
    static constexpr int SUPPORTED_BITS_PER_VALUE[] = {
        1, 2, 4, 8, 12, 16, 20, 24, 28, 32, 40, 48, 56, 64};
    static constexpr int NUM_SUPPORTED_BPV = 14;

    /**
     * Calculate bits required to represent a signed value, rounded up to supported bpv.
     * @param maxValue Maximum value to encode (must be >= 0)
     * @return Supported bpv that can hold the value
     */
    static int bitsRequired(int64_t maxValue);

    /**
     * Calculate bits required for unsigned values, rounded up to supported bpv.
     * @param maxValue Maximum unsigned value
     * @return Supported bpv that can hold the value
     */
    static int unsignedBitsRequired(uint64_t maxValue);

    /**
     * Calculate total bytes written for encoding numValues at bitsPerValue.
     * Includes padding bytes for fast I/O.
     */
    static int64_t bytesRequired(int64_t numValues, int bitsPerValue);

    /**
     * Create a DirectWriter.
     * @param output Output stream to write to
     * @param numValues Number of values to write
     * @param bitsPerValue Must be one of SUPPORTED_BITS_PER_VALUE
     */
    DirectWriter(store::IndexOutput* output, int64_t numValues, int bitsPerValue);

    /** Write a value. Must be called exactly numValues times. */
    void add(int64_t value);

    /** Finish writing: flush pending values and write padding bytes. */
    void finish();

private:
    store::IndexOutput* output_;
    int64_t numValues_;
    int bitsPerValue_;
    int64_t count_;
    bool finished_;

    int off_;                          // Current position in nextValues_
    std::vector<int64_t> nextValues_;  // Value accumulation buffer
    std::vector<uint8_t> nextBlocks_;  // Encoded byte buffer

    void flush();

    /**
     * Encode values into packed bytes (little-endian).
     * Three paths: byte-aligned, sub-byte, non-aligned pairs.
     */
    static void encode(const int64_t* values, int upTo, uint8_t* blocks, int bitsPerValue);

    /** Padding bytes needed for fast I/O at finish(). */
    static int paddingBytesNeeded(int bitsPerValue);

    /** Round raw bits to next supported bpv. */
    static int roundBits(int bitsRequired);

    /** Raw unsigned bits needed (not rounded to supported). */
    static int rawUnsignedBitsRequired(uint64_t value);

    // Little-endian helpers (native on x86/ARM)
    static void writeLE64(uint8_t* buf, uint64_t v) { std::memcpy(buf, &v, 8); }
    static void writeLE32(uint8_t* buf, uint32_t v) { std::memcpy(buf, &v, 4); }
    static void writeLE16(uint8_t* buf, uint16_t v) { std::memcpy(buf, &v, 2); }
};

/**
 * Bit-unpacking utility for reading fixed bit-width integers.
 *
 * Based on: org.apache.lucene.util.packed.DirectReader
 *
 * Reads little-endian packed data produced by DirectWriter.
 */
class DirectReader {
public:
    /**
     * Read all values sequentially.
     * @param input Input stream positioned at packed data start
     * @param bitsPerValue Must be one of SUPPORTED_BITS_PER_VALUE
     * @param count Number of values to read
     * @return Vector of decoded values
     */
    static std::vector<int64_t> read(store::IndexInput* input, int bitsPerValue, int64_t count);

    /**
     * Read a single value at a given index (random access).
     * @param input Input stream positioned at packed data start
     * @param bitsPerValue Must be one of SUPPORTED_BITS_PER_VALUE
     * @param baseOffset Byte offset of packed data start in file
     * @param index Index of value to read
     * @return Decoded value
     */
    static int64_t get(store::IndexInput* input, int bitsPerValue, int64_t baseOffset,
                       int64_t index);

private:
    static uint64_t readLE64(store::IndexInput* input);
    static uint32_t readLE32(store::IndexInput* input);
    static uint16_t readLE16(store::IndexInput* input);
};

}  // namespace packed
}  // namespace util
}  // namespace diagon
