// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/BKDConfig.h"
#include "diagon/store/IndexOutput.h"

#include <cstdint>
#include <vector>

namespace diagon {
namespace codecs {

/**
 * BKDWriter - Builds BKD tree from point values and serializes to disk
 *
 * Based on: org.apache.lucene.util.bkd.BKDWriter
 *
 * Algorithm (1D only):
 * 1. Sort (docID, packedValue) pairs by packedValue
 * 2. Recursive median split to build balanced binary tree
 * 3. Leaf blocks hold up to maxPointsPerLeaf points
 * 4. Serialize:
 *    - .kdd: leaf data (sorted docIDs + packed values per leaf)
 *    - .kdi: inner node index (split values + child pointers)
 *    - .kdm: metadata (per-field: dims, counts, min/max, offsets)
 */
class BKDWriter {
public:
    explicit BKDWriter(const index::BKDConfig& config);

    /**
     * Build BKD tree from buffered points and write to output streams.
     *
     * @param fieldName Field name (for metadata)
     * @param fieldNumber Field number
     * @param docIDs Document IDs (will be reordered in-place)
     * @param packedValues Packed point values (will be reordered in-place)
     * @param kdmOut Metadata output (.kdm)
     * @param kdiOut Inner node index output (.kdi)
     * @param kddOut Leaf data output (.kdd)
     */
    void writeField(const std::string& fieldName, int32_t fieldNumber,
                    std::vector<int32_t>& docIDs, std::vector<uint8_t>& packedValues,
                    store::IndexOutput& kdmOut, store::IndexOutput& kdiOut,
                    store::IndexOutput& kddOut);

private:
    // Internal tree node for building
    struct InnerNode {
        int64_t splitValue;  // For 1D: the split value as int64
        int64_t leftChildFP;   // File pointer in .kdi for left child (or .kdd for leaf)
        int64_t rightChildFP;  // File pointer in .kdi for right child (or .kdd for leaf)
        bool leftIsLeaf;
        bool rightIsLeaf;
    };

    /**
     * Recursively build the BKD tree.
     * @param from Start index (inclusive) in sorted arrays
     * @param to End index (exclusive) in sorted arrays
     * @param docIDs Document ID array
     * @param packedValues Packed values array
     * @param minPacked Minimum packed value in range
     * @param maxPacked Maximum packed value in range
     * @param kdiOut Inner node output
     * @param kddOut Leaf data output
     * @return File pointer where this node was written
     */
    int64_t buildTree(int from, int to,
                      int32_t* docIDs, uint8_t* packedValues,
                      const uint8_t* minPacked, const uint8_t* maxPacked,
                      store::IndexOutput& kdiOut, store::IndexOutput& kddOut);

    /**
     * Write a leaf block to .kdd
     * @return File pointer where the leaf was written
     */
    int64_t writeLeafBlock(int from, int to,
                           const int32_t* docIDs, const uint8_t* packedValues,
                           store::IndexOutput& kddOut);

    /**
     * Sort points by packed value (1D: simple int64 comparison)
     */
    void sortPoints(int from, int to,
                    int32_t* docIDs, uint8_t* packedValues);

    /**
     * Compare two packed values (1D: big-endian byte comparison = numeric order)
     */
    static int comparePackedValues(const uint8_t* a, const uint8_t* b, int bytesPerDim);

    index::BKDConfig config_;
};

}  // namespace codecs
}  // namespace diagon
