// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/BKDConfig.h"
#include "diagon/index/PointValues.h"
#include "diagon/store/IndexInput.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace diagon {
namespace codecs {

/**
 * BKDReader - Reads BKD tree from .kdm/.kdi/.kdd files
 *
 * Based on: org.apache.lucene.util.bkd.BKDReader
 *
 * Loads inner node index to RAM from .kdi for fast traversal.
 * Leaf data is read from .kdd on demand (via IndexInput random access).
 *
 * Thread safety: Not thread-safe. Each search thread should use its own clone.
 */
class BKDReader : public index::PointValues {
public:
    /**
     * Per-field BKD tree data
     */
    struct FieldTree {
        std::string fieldName;
        int32_t fieldNumber;
        index::BKDConfig config;
        int numPoints;
        int docCount;
        std::vector<uint8_t> minPackedValue;
        std::vector<uint8_t> maxPackedValue;

        // Offsets into .kdi and .kdd
        int64_t kdiStartFP;
        int64_t kdiEndFP;
        int64_t kddStartFP;
        int64_t kddEndFP;
    };

    /**
     * Load all field trees from metadata (.kdm) file.
     * Returns a map of field name -> BKDReader.
     */
    static std::unordered_map<std::string, std::unique_ptr<BKDReader>>
    loadFields(store::IndexInput& kdmIn, store::IndexInput& kdiIn, store::IndexInput& kddIn);

    /**
     * Construct a BKDReader for one field
     */
    BKDReader(FieldTree tree, std::unique_ptr<store::IndexInput> kdiSlice,
              std::unique_ptr<store::IndexInput> kddSlice);

    // ==================== PointValues interface ====================

    void intersect(IntersectVisitor& visitor) const override;
    int getNumDimensions() const override { return tree_.config.numDims; }
    int getNumIndexDimensions() const override { return tree_.config.numIndexDims; }
    int getBytesPerDimension() const override { return tree_.config.bytesPerDim; }
    int size() const override { return tree_.numPoints; }
    int getDocCount() const override { return tree_.docCount; }
    const uint8_t* getMinPackedValue() const override { return tree_.minPackedValue.data(); }
    const uint8_t* getMaxPackedValue() const override { return tree_.maxPackedValue.data(); }

private:
    /**
     * Recursive tree traversal for intersection
     */
    void intersectNode(int64_t nodeFP, bool isLeaf, const uint8_t* cellMin, const uint8_t* cellMax,
                       IntersectVisitor& visitor) const;

    /**
     * Visit all points in a leaf block
     */
    void visitLeafBlock(int64_t leafFP, IntersectVisitor& visitor, bool allInside) const;

    FieldTree tree_;
    std::unique_ptr<store::IndexInput> kdiInput_;  // Inner node data
    std::unique_ptr<store::IndexInput> kddInput_;  // Leaf data
};

}  // namespace codecs
}  // namespace diagon
