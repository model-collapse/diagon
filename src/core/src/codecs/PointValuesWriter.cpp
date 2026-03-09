// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/PointValuesWriter.h"

#include "diagon/codecs/BKDWriter.h"

#include <cstring>
#include <stdexcept>

namespace diagon {
namespace codecs {

PointValuesWriter::PointValuesWriter(const std::string& segmentName, int /*maxDoc*/)
    : segmentName_(segmentName) {}

void PointValuesWriter::addPoint(const index::FieldInfo& fieldInfo, int docID,
                                 const uint8_t* packedValue) {
    int32_t fieldNum = fieldInfo.number;

    auto it = fieldBuffers_.find(fieldNum);
    if (it == fieldBuffers_.end()) {
        // Create new field buffer
        auto buf = std::make_unique<FieldBuffer>();
        buf->fieldName = fieldInfo.name;
        buf->fieldNumber = fieldNum;
        buf->config = index::BKDConfig(fieldInfo.pointDimensionCount,
                                       fieldInfo.pointIndexDimensionCount, fieldInfo.pointNumBytes);
        it = fieldBuffers_.emplace(fieldNum, std::move(buf)).first;
    }

    FieldBuffer& fb = *it->second;
    int packedLen = fb.config.packedBytesLength;

    fb.docIDs.push_back(docID);
    size_t oldSize = fb.packedValues.size();
    fb.packedValues.resize(oldSize + packedLen);
    std::memcpy(fb.packedValues.data() + oldSize, packedValue, packedLen);
}

void PointValuesWriter::flush(store::IndexOutput& kdmOut, store::IndexOutput& kdiOut,
                              store::IndexOutput& kddOut) {
    // Write number of fields
    kdmOut.writeVInt(static_cast<int32_t>(fieldBuffers_.size()));

    for (auto& [fieldNum, buf] : fieldBuffers_) {
        BKDWriter writer(buf->config);
        writer.writeField(buf->fieldName, buf->fieldNumber, buf->docIDs, buf->packedValues, kdmOut,
                          kdiOut, kddOut);
    }
}

int64_t PointValuesWriter::ramBytesUsed() const {
    int64_t total = 0;
    for (const auto& [fieldNum, buf] : fieldBuffers_) {
        total += static_cast<int64_t>(buf->docIDs.capacity() * sizeof(int32_t));
        total += static_cast<int64_t>(buf->packedValues.capacity());
    }
    return total;
}

}  // namespace codecs
}  // namespace diagon
