// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/BKDWriter.h"

#include "diagon/util/NumericUtils.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>

namespace diagon {
namespace codecs {

BKDWriter::BKDWriter(const index::BKDConfig& config)
    : config_(config) {}

void BKDWriter::writeField(const std::string& fieldName, int32_t fieldNumber,
                           std::vector<int32_t>& docIDs, std::vector<uint8_t>& packedValues,
                           store::IndexOutput& kdmOut, store::IndexOutput& kdiOut,
                           store::IndexOutput& kddOut) {
    int numPoints = static_cast<int>(docIDs.size());
    if (numPoints == 0) {
        return;
    }

    int packedLen = config_.packedBytesLength;

    // Sort by packed value
    sortPoints(0, numPoints, docIDs.data(), packedValues.data());

    // Compute min/max packed values
    std::vector<uint8_t> minPacked(packedLen);
    std::vector<uint8_t> maxPacked(packedLen);
    std::memcpy(minPacked.data(), packedValues.data(), packedLen);
    std::memcpy(maxPacked.data(), packedValues.data() + (numPoints - 1) * packedLen, packedLen);

    // Count unique doc IDs
    std::set<int32_t> uniqueDocs(docIDs.begin(), docIDs.end());
    int docCount = static_cast<int>(uniqueDocs.size());

    // Record starting offsets
    int64_t kdiStartFP = kdiOut.getFilePointer();
    int64_t kddStartFP = kddOut.getFilePointer();

    // Build tree recursively
    buildTree(0, numPoints, docIDs.data(), packedValues.data(), minPacked.data(), maxPacked.data(),
              kdiOut, kddOut);

    // Write metadata to .kdm
    kdmOut.writeString(fieldName);
    kdmOut.writeVInt(fieldNumber);
    kdmOut.writeVInt(config_.numDims);
    kdmOut.writeVInt(config_.numIndexDims);
    kdmOut.writeVInt(config_.bytesPerDim);
    kdmOut.writeVInt(config_.maxPointsPerLeaf);
    kdmOut.writeVInt(numPoints);
    kdmOut.writeVInt(docCount);
    kdmOut.writeBytes(minPacked.data(), packedLen);
    kdmOut.writeBytes(maxPacked.data(), packedLen);
    kdmOut.writeLong(kdiStartFP);
    kdmOut.writeLong(kddStartFP);
    // Write end offsets so reader knows how much data to read
    kdmOut.writeLong(kdiOut.getFilePointer());
    kdmOut.writeLong(kddOut.getFilePointer());
}

int64_t BKDWriter::buildTree(int from, int to, int32_t* docIDs, uint8_t* packedValues,
                             const uint8_t* minPacked, const uint8_t* maxPacked,
                             store::IndexOutput& kdiOut, store::IndexOutput& kddOut) {
    int count = to - from;

    if (count <= config_.maxPointsPerLeaf) {
        // Leaf node — write to .kdd
        return writeLeafBlock(from, to, docIDs, packedValues, kddOut);
    }

    // Inner node — median split
    int mid = from + count / 2;
    int packedLen = config_.packedBytesLength;

    // The split value is the packed value at the median
    const uint8_t* splitPacked = packedValues + mid * packedLen;

    // Compute min/max for children
    // Left child: [from, mid) has min=minPacked, max=splitPacked
    // Right child: [mid, to) has min=splitPacked, max=maxPacked

    // Recurse left
    int64_t leftFP = buildTree(from, mid, docIDs, packedValues, minPacked, splitPacked, kdiOut,
                               kddOut);
    bool leftIsLeaf = (mid - from) <= config_.maxPointsPerLeaf;

    // Recurse right
    int64_t rightFP = buildTree(mid, to, docIDs, packedValues, splitPacked, maxPacked, kdiOut,
                                kddOut);
    bool rightIsLeaf = (to - mid) <= config_.maxPointsPerLeaf;

    // Write inner node to .kdi
    int64_t nodeFP = kdiOut.getFilePointer();

    // Write split value
    kdiOut.writeBytes(splitPacked, config_.bytesPerDim);

    // Write child info: flags byte + file pointers
    uint8_t flags = 0;
    if (leftIsLeaf)
        flags |= 0x01;
    if (rightIsLeaf)
        flags |= 0x02;
    kdiOut.writeByte(flags);
    kdiOut.writeLong(leftFP);
    kdiOut.writeLong(rightFP);

    return nodeFP;
}

int64_t BKDWriter::writeLeafBlock(int from, int to, const int32_t* docIDs,
                                  const uint8_t* packedValues, store::IndexOutput& kddOut) {
    int64_t leafFP = kddOut.getFilePointer();
    int count = to - from;
    int packedLen = config_.packedBytesLength;

    // Write count
    kddOut.writeVInt(count);

    // Write doc IDs (delta-encoded)
    int prevDocID = 0;
    for (int i = from; i < to; i++) {
        int delta = docIDs[i] - prevDocID;
        kddOut.writeVInt(delta);
        prevDocID = docIDs[i];
    }

    // Write packed values
    // For 1D: delta-encode the packed values (as int64 big-endian)
    if (config_.numDims == 1 && config_.bytesPerDim == 8) {
        int64_t prevVal = 0;
        for (int i = from; i < to; i++) {
            int64_t val = util::NumericUtils::bytesToLongBE(packedValues + i * packedLen);
            int64_t delta = val - prevVal;
            kddOut.writeVLong(delta);
            prevVal = val;
        }
    } else {
        // General case: write raw packed values
        for (int i = from; i < to; i++) {
            kddOut.writeBytes(packedValues + i * packedLen, packedLen);
        }
    }

    return leafFP;
}

void BKDWriter::sortPoints(int from, int to, int32_t* docIDs, uint8_t* packedValues) {
    int count = to - from;
    int packedLen = config_.packedBytesLength;

    // Create index array and sort by packed value
    std::vector<int> indices(count);
    for (int i = 0; i < count; i++) {
        indices[i] = from + i;
    }

    std::sort(indices.begin(), indices.end(), [&](int a, int b) {
        int cmp = comparePackedValues(packedValues + a * packedLen, packedValues + b * packedLen,
                                      config_.bytesPerDim);
        if (cmp != 0)
            return cmp < 0;
        return docIDs[a] < docIDs[b];  // Tie-break by docID
    });

    // Apply permutation to both arrays
    std::vector<int32_t> tmpDocIDs(count);
    std::vector<uint8_t> tmpValues(count * packedLen);

    for (int i = 0; i < count; i++) {
        tmpDocIDs[i] = docIDs[indices[i]];
        std::memcpy(tmpValues.data() + i * packedLen, packedValues + indices[i] * packedLen,
                    packedLen);
    }

    std::memcpy(docIDs + from, tmpDocIDs.data(), count * sizeof(int32_t));
    std::memcpy(packedValues + from * packedLen, tmpValues.data(), count * packedLen);
}

int BKDWriter::comparePackedValues(const uint8_t* a, const uint8_t* b, int bytesPerDim) {
    // Big-endian unsigned byte comparison (matching sortable long encoding)
    return std::memcmp(a, b, bytesPerDim);
}

}  // namespace codecs
}  // namespace diagon
