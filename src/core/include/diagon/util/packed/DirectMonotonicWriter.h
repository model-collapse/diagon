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
 * Lucene-compatible format — metadata per block (21 bytes):
 *   Long  min           — minimum delta value
 *   Int   avgInc        — float average increment as IEEE 754 int bits
 *   Long  dataOffset    — relative offset from baseDataPointer
 *   Byte  bitsRequired  — 0 if all deltas equal, else DirectWriter bpv
 *
 * Data is packed residuals after subtracting linear prediction (slope + min).
 *
 * Algorithm per block:
 * 1. avgInc = (last - first) / max(1, count-1)
 * 2. expected[i] = (long)(avgInc * i)
 * 3. buffer[i] -= expected[i]
 * 4. min = min(buffer[i])
 * 5. buffer[i] -= min (normalize to non-negative)
 * 6. maxDelta = max(buffer[i])
 * 7. Write residuals with DirectWriter at unsignedBitsRequired(maxDelta)
 */
class DirectMonotonicWriter {
public:
    static constexpr int MIN_BLOCK_SHIFT = 2;
    static constexpr int MAX_BLOCK_SHIFT = 22;

    /**
     * Create a writer.
     *
     * @param metaOut Metadata output (per-block: min, avgInc, dataOffset, bitsRequired)
     * @param dataOut Data output (packed residuals via DirectWriter)
     * @param numValues Total number of values to encode
     * @param blockShift Block size = 1 << blockShift
     */
    DirectMonotonicWriter(store::IndexOutput* metaOut, store::IndexOutput* dataOut,
                          int64_t numValues, int blockShift);

    /**
     * Add a value. Must be monotonically increasing.
     */
    void add(int64_t value);

    /**
     * Finish encoding and flush all pending blocks.
     * Must be called after adding exactly numValues values.
     */
    void finish();

private:
    store::IndexOutput* meta_;
    store::IndexOutput* data_;
    int64_t numValues_;
    int64_t baseDataPointer_;
    std::vector<int64_t> buffer_;
    int bufferSize_;
    int64_t count_;
    int64_t previous_;
    bool finished_;

    void flush();
};

/**
 * Reads monotonically increasing sequences encoded with DirectMonotonicWriter.
 *
 * Based on: org.apache.lucene.util.packed.DirectMonotonicReader
 *
 * Reads Lucene-compatible metadata format (21 bytes per block).
 */
class DirectMonotonicReader {
public:
    /**
     * Per-block metadata read from the meta stream.
     */
    struct BlockMeta {
        int64_t min;
        float avgInc;
        int64_t dataOffset;  // Relative to baseDataPointer
        int bitsRequired;
    };

    /**
     * Read a single value by index.
     *
     * @param metaIn Input for block metadata
     * @param dataIn Input for packed data
     * @param baseDataPointer File pointer where packed data starts in dataIn
     * @param blockShift Block size = 1 << blockShift
     * @param numValues Total number of values
     * @param index Index of value to read
     * @param metaStartFP File pointer where first block metadata starts in metaIn (default 0)
     */
    static int64_t get(store::IndexInput* metaIn, store::IndexInput* dataIn,
                       int64_t baseDataPointer, int blockShift, int64_t numValues, int64_t index,
                       int64_t metaStartFP = 0);

    /**
     * Read all values into a vector.
     *
     * @param metaStartFP File pointer where first block metadata starts in metaIn (default 0)
     */
    static std::vector<int64_t> readAll(store::IndexInput* metaIn, store::IndexInput* dataIn,
                                        int64_t baseDataPointer, int blockShift,
                                        int64_t numValues, int64_t metaStartFP = 0);

    /**
     * Read block metadata at a given block index.
     * Each block metadata is 21 bytes: Long(8) + Int(4) + Long(8) + Byte(1)
     */
    static BlockMeta readBlockMeta(store::IndexInput* metaIn, int64_t metaStartFP,
                                   int64_t blockIndex);
};

}  // namespace packed
}  // namespace util
}  // namespace diagon
