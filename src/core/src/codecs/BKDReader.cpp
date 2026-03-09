// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/BKDReader.h"

#include "diagon/util/NumericUtils.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace diagon {
namespace codecs {

std::unordered_map<std::string, std::unique_ptr<BKDReader>>
BKDReader::loadFields(store::IndexInput& kdmIn, store::IndexInput& kdiIn,
                      store::IndexInput& kddIn) {
    std::unordered_map<std::string, std::unique_ptr<BKDReader>> result;

    int numFields = kdmIn.readVInt();

    for (int i = 0; i < numFields; i++) {
        FieldTree tree;
        tree.fieldName = kdmIn.readString();
        tree.fieldNumber = kdmIn.readVInt();
        int numDims = kdmIn.readVInt();
        int numIndexDims = kdmIn.readVInt();
        int bytesPerDim = kdmIn.readVInt();
        int maxPointsPerLeaf = kdmIn.readVInt();
        tree.config = index::BKDConfig(numDims, numIndexDims, bytesPerDim, maxPointsPerLeaf);

        tree.numPoints = kdmIn.readVInt();
        tree.docCount = kdmIn.readVInt();

        int packedLen = tree.config.packedBytesLength;
        tree.minPackedValue.resize(packedLen);
        tree.maxPackedValue.resize(packedLen);
        kdmIn.readBytes(tree.minPackedValue.data(), packedLen);
        kdmIn.readBytes(tree.maxPackedValue.data(), packedLen);

        tree.kdiStartFP = kdmIn.readLong();
        tree.kddStartFP = kdmIn.readLong();
        tree.kdiEndFP = kdmIn.readLong();
        tree.kddEndFP = kdmIn.readLong();

        // Create slices for this field's data
        int64_t kdiLen = tree.kdiEndFP - tree.kdiStartFP;
        int64_t kddLen = tree.kddEndFP - tree.kddStartFP;

        auto kdiSlice = kdiIn.slice("BKDReader.kdi." + tree.fieldName, tree.kdiStartFP, kdiLen);
        auto kddSlice = kddIn.slice("BKDReader.kdd." + tree.fieldName, tree.kddStartFP, kddLen);

        std::string fieldName = tree.fieldName;
        auto reader =
            std::make_unique<BKDReader>(std::move(tree), std::move(kdiSlice), std::move(kddSlice));
        result.emplace(fieldName, std::move(reader));
    }

    return result;
}

BKDReader::BKDReader(FieldTree tree, std::unique_ptr<store::IndexInput> kdiSlice,
                     std::unique_ptr<store::IndexInput> kddSlice)
    : tree_(std::move(tree))
    , kdiInput_(std::move(kdiSlice))
    , kddInput_(std::move(kddSlice)) {}

void BKDReader::intersect(IntersectVisitor& visitor) const {
    if (tree_.numPoints == 0) {
        return;
    }

    int packedLen = tree_.config.packedBytesLength;

    // Check if the entire tree is inside/outside the query
    auto rel = visitor.compare(tree_.minPackedValue.data(), tree_.maxPackedValue.data());
    if (rel == PointValues::Relation::CELL_OUTSIDE_QUERY) {
        return;  // Nothing matches
    }

    // If entire tree fits in one leaf
    if (tree_.numPoints <= tree_.config.maxPointsPerLeaf) {
        // Root is a leaf
        visitLeafBlock(0, visitor, rel == PointValues::Relation::CELL_INSIDE_QUERY);
        return;
    }

    // Root is an inner node — read it from .kdi
    auto kdiClone = kdiInput_->clone();
    int64_t kdiLen = tree_.kdiEndFP - tree_.kdiStartFP;

    // The root is the LAST inner node written (it was written after its children)
    // We need to find the root. In our format, buildTree returns the root FP.
    // The root FP is the last node written to .kdi.
    // We stored kdiStartFP and kdiEndFP, so the root is at (kdiEndFP - kdiStartFP - nodeSize).
    // But we don't know exact nodeSize. Instead, let's read the .kdi sequentially from where
    // the root was written.

    // Actually, our buildTree returns the FP of the root node. But we didn't store it directly.
    // We stored kdiStartFP as the start of .kdi data for this field.
    // The root is the LAST node written since we do post-order (left, right, then self).
    // So the root node is at the END of the .kdi section.

    // Let's find root by reading backwards. The root inner node was the last one written.
    // Node format: bytesPerDim (split value) + 1 (flags) + 8 (leftFP) + 8 (rightFP)
    int nodeSize = tree_.config.bytesPerDim + 1 + 8 + 8;
    int64_t rootFP = kdiLen - nodeSize;

    // Read root node
    kdiClone->seek(rootFP);

    // Read split value
    std::vector<uint8_t> splitValue(tree_.config.bytesPerDim);
    kdiClone->readBytes(splitValue.data(), tree_.config.bytesPerDim);

    uint8_t flags = kdiClone->readByte();
    bool leftIsLeaf = (flags & 0x01) != 0;
    bool rightIsLeaf = (flags & 0x02) != 0;
    int64_t leftFP = kdiClone->readLong();
    int64_t rightFP = kdiClone->readLong();

    // For 1D: left child has values <= split, right has values > split
    // Left cell: [treeMin, splitValue]
    // Right cell: [splitValue, treeMax]
    std::vector<uint8_t> leftMax(packedLen);
    std::vector<uint8_t> rightMin(packedLen);
    std::memcpy(leftMax.data(), splitValue.data(), tree_.config.bytesPerDim);
    std::memcpy(rightMin.data(), splitValue.data(), tree_.config.bytesPerDim);

    // Check and recurse left
    auto leftRel = visitor.compare(tree_.minPackedValue.data(), leftMax.data());
    if (leftRel != PointValues::Relation::CELL_OUTSIDE_QUERY) {
        if (leftRel == PointValues::Relation::CELL_INSIDE_QUERY) {
            // Collect all docs in left subtree
            intersectNode(leftFP, leftIsLeaf, tree_.minPackedValue.data(), leftMax.data(), visitor);
        } else {
            intersectNode(leftFP, leftIsLeaf, tree_.minPackedValue.data(), leftMax.data(), visitor);
        }
    }

    // Check and recurse right
    auto rightRel = visitor.compare(rightMin.data(), tree_.maxPackedValue.data());
    if (rightRel != PointValues::Relation::CELL_OUTSIDE_QUERY) {
        intersectNode(rightFP, rightIsLeaf, rightMin.data(), tree_.maxPackedValue.data(), visitor);
    }
}

void BKDReader::intersectNode(int64_t nodeFP, bool isLeaf, const uint8_t* cellMin,
                               const uint8_t* cellMax, IntersectVisitor& visitor) const {
    // First check cell against query
    auto rel = visitor.compare(cellMin, cellMax);
    if (rel == PointValues::Relation::CELL_OUTSIDE_QUERY) {
        return;
    }

    if (isLeaf) {
        visitLeafBlock(nodeFP, visitor, rel == PointValues::Relation::CELL_INSIDE_QUERY);
        return;
    }

    // Inner node — read from .kdi
    auto kdiClone = kdiInput_->clone();
    kdiClone->seek(nodeFP);

    std::vector<uint8_t> splitValue(tree_.config.bytesPerDim);
    kdiClone->readBytes(splitValue.data(), tree_.config.bytesPerDim);

    uint8_t flags = kdiClone->readByte();
    bool leftIsLeaf = (flags & 0x01) != 0;
    bool rightIsLeaf = (flags & 0x02) != 0;
    int64_t leftFP = kdiClone->readLong();
    int64_t rightFP = kdiClone->readLong();

    int packedLen = tree_.config.packedBytesLength;

    // Left cell: [cellMin, splitValue]
    std::vector<uint8_t> leftMax(packedLen);
    std::memcpy(leftMax.data(), splitValue.data(), tree_.config.bytesPerDim);

    // Right cell: [splitValue, cellMax]
    std::vector<uint8_t> rightMin(packedLen);
    std::memcpy(rightMin.data(), splitValue.data(), tree_.config.bytesPerDim);

    if (rel == PointValues::Relation::CELL_INSIDE_QUERY) {
        // Both children are fully inside — collect all
        intersectNode(leftFP, leftIsLeaf, cellMin, leftMax.data(), visitor);
        intersectNode(rightFP, rightIsLeaf, rightMin.data(), cellMax, visitor);
    } else {
        // CELL_CROSSES_QUERY — check each child
        auto leftRel = visitor.compare(cellMin, leftMax.data());
        if (leftRel != PointValues::Relation::CELL_OUTSIDE_QUERY) {
            intersectNode(leftFP, leftIsLeaf, cellMin, leftMax.data(), visitor);
        }

        auto rightRel = visitor.compare(rightMin.data(), cellMax);
        if (rightRel != PointValues::Relation::CELL_OUTSIDE_QUERY) {
            intersectNode(rightFP, rightIsLeaf, rightMin.data(), cellMax, visitor);
        }
    }
}

void BKDReader::visitLeafBlock(int64_t leafFP, IntersectVisitor& visitor, bool allInside) const {
    auto kddClone = kddInput_->clone();
    kddClone->seek(leafFP);

    int count = kddClone->readVInt();

    // Read doc IDs (delta-encoded)
    std::vector<int32_t> docIDs(count);
    int prevDocID = 0;
    for (int i = 0; i < count; i++) {
        int delta = kddClone->readVInt();
        prevDocID += delta;
        docIDs[i] = prevDocID;
    }

    int packedLen = tree_.config.packedBytesLength;

    if (tree_.config.numDims == 1 && tree_.config.bytesPerDim == 8) {
        // 1D delta-encoded int64 values
        std::vector<uint8_t> packed(packedLen);
        int64_t prevVal = 0;

        for (int i = 0; i < count; i++) {
            int64_t delta = kddClone->readVLong();
            prevVal += delta;

            if (allInside) {
                visitor.visit(docIDs[i]);
            } else {
                util::NumericUtils::longToBytesBE(prevVal, packed.data());
                visitor.visit(docIDs[i], packed.data());
            }
        }
    } else {
        // General case: raw packed values
        std::vector<uint8_t> packed(packedLen);

        for (int i = 0; i < count; i++) {
            if (allInside) {
                kddClone->skipBytes(packedLen);
                visitor.visit(docIDs[i]);
            } else {
                kddClone->readBytes(packed.data(), packedLen);
                visitor.visit(docIDs[i], packed.data());
            }
        }
    }
}

}  // namespace codecs
}  // namespace diagon
