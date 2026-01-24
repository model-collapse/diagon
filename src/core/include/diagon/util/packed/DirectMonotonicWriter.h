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
 * Encodes monotonically increasing sequences efficiently.
 *
 * Based on: org.apache.lucene.util.packed.DirectMonotonicWriter
 *
 * Algorithm:
 * 1. Split sequence into blocks (typically 16 values per block)
 * 2. For each block, compute average slope and encode deviations
 * 3. Use DirectWriter to bitpack deviations
 *
 * This achieves O(1) random access and excellent compression for
 * monotonic sequences like address lists.
 *
 * Example: [100, 120, 135, 160] with blockShift=2 (4 values per block)
 * - avgSlope = (160-100)/3 = 20.0
 * - expected = [100, 120, 140, 160]
 * - deviations = [0, 0, -5, 0]
 * - min = -5, encode [5, 5, 0, 5] with DirectWriter
 */
class DirectMonotonicWriter {
public:
    /**
     * Metadata for reading a DirectMonotonic sequence.
     */
    struct Meta {
        /** Number of values */
        int64_t numValues;

        /** Block shift (block size = 1 << blockShift) */
        int blockShift;

        /** Minimum value across all blocks */
        int64_t min;

        /** Maximum value across all blocks */
        int64_t max;

        /** File pointer where block metadata starts */
        int64_t metaFP;

        /** File pointer where packed data starts */
        int64_t dataFP;

        Meta()
            : numValues(0)
            , blockShift(0)
            , min(0)
            , max(0)
            , metaFP(0)
            , dataFP(0) {}
    };

    /**
     * Create a writer.
     *
     * @param meta Metadata output (records block structure)
     * @param data Data output (packed deviations)
     * @param numValues Number of values to encode
     * @param blockShift Block size = 1 << blockShift (typically 4 for 16 values)
     */
    DirectMonotonicWriter(store::IndexOutput* meta, store::IndexOutput* data, int64_t numValues,
                          int blockShift);

    /**
     * Add a value to the sequence.
     * Values must be monotonically increasing.
     *
     * @param value Value to add (must be >= previous value)
     */
    void add(int64_t value);

    /**
     * Finish encoding and flush all blocks.
     *
     * @return Metadata for reading this sequence
     */
    Meta finish();

private:
    struct Block {
        int64_t min;           // Minimum value in block
        int64_t max;           // Maximum value in block
        float avgSlope;        // Average delta per value
        int64_t minDeviation;  // Minimum deviation (for reconstruction)
        int64_t dataOffset;    // File pointer to packed data
        int bitsPerValue;      // Bits required for deviations

        Block()
            : min(0)
            , max(0)
            , avgSlope(0)
            , minDeviation(0)
            , dataOffset(0)
            , bitsPerValue(0) {}
    };

    store::IndexOutput* meta_;
    store::IndexOutput* data_;
    int64_t numValues_;
    int blockShift_;
    int blockSize_;

    int64_t count_;
    int64_t lastValue_;
    std::vector<int64_t> buffer_;  // Current block being accumulated
    std::vector<Block> blocks_;    // Completed block metadata

    void flushBlock();
    void writeMeta(const Block& block);
};

/**
 * Reads monotonically increasing sequences encoded with DirectMonotonicWriter.
 *
 * Based on: org.apache.lucene.util.packed.DirectMonotonicReader
 */
class DirectMonotonicReader {
public:
    /**
     * Get a value at a specific index.
     *
     * @param meta Metadata from writer
     * @param metaIn Input for block metadata
     * @param dataIn Input for packed data
     * @param index Index of value to retrieve
     * @return Value at index
     */
    static int64_t get(const DirectMonotonicWriter::Meta& meta, store::IndexInput* metaIn,
                       store::IndexInput* dataIn, int64_t index);

    /**
     * Read all values into a vector.
     *
     * @param meta Metadata from writer
     * @param metaIn Input for block metadata
     * @param dataIn Input for packed data
     * @return Vector of all values
     */
    static std::vector<int64_t> readAll(const DirectMonotonicWriter::Meta& meta,
                                        store::IndexInput* metaIn, store::IndexInput* dataIn);

private:
    struct Block {
        int64_t min;
        float avgSlope;
        int64_t minDeviation;  // Minimum deviation (for reconstruction)
        int64_t dataOffset;
        int bitsPerValue;
    };

    static Block readBlockMeta(const DirectMonotonicWriter::Meta& meta, store::IndexInput* metaIn,
                               int64_t blockIndex);
};

}  // namespace packed
}  // namespace util
}  // namespace diagon
