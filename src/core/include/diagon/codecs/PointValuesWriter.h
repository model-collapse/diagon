// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/BKDConfig.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/store/IndexOutput.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace diagon {
namespace codecs {

/**
 * PointValuesWriter - In-memory buffer for point values during indexing
 *
 * Based on: org.apache.lucene.codecs.PointsWriter
 *
 * Follows the NumericDocValuesWriter pattern: per-field buffer holding
 * (docID, packedValue) pairs. Flush writes BKD tree to .kdm/.kdi/.kdd files.
 */
class PointValuesWriter {
public:
    explicit PointValuesWriter(const std::string& segmentName, int maxDoc);

    /**
     * Add a point value for a field
     */
    void addPoint(const index::FieldInfo& fieldInfo, int docID, const uint8_t* packedValue);

    /**
     * Flush all buffered points to BKD tree files
     */
    void flush(store::IndexOutput& kdmOut, store::IndexOutput& kdiOut, store::IndexOutput& kddOut);

    /**
     * Approximate RAM bytes used
     */
    int64_t ramBytesUsed() const;

    /**
     * Check if any points have been buffered
     */
    bool hasPoints() const { return !fieldBuffers_.empty(); }

private:
    struct FieldBuffer {
        std::string fieldName;
        int32_t fieldNumber;
        index::BKDConfig config;
        std::vector<int32_t> docIDs;
        std::vector<uint8_t> packedValues;  // flat: docIDs.size() * config.packedBytesLength
    };

    std::string segmentName_;
    std::unordered_map<int32_t, std::unique_ptr<FieldBuffer>> fieldBuffers_;
};

}  // namespace codecs
}  // namespace diagon
