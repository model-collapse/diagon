// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/InMemoryNormsProducer.h"

#include <cmath>
#include <algorithm>

namespace diagon {
namespace index {

void InMemoryNormsProducer::setNorm(const std::string& field, int docID, int8_t norm) {
    auto& fieldNorms = norms_[field];

    // Expand vector if needed
    if (docID >= static_cast<int>(fieldNorms.size())) {
        fieldNorms.resize(docID + 1, 0);
    }

    fieldNorms[docID] = norm;
}

void InMemoryNormsProducer::setNormFromLength(const std::string& field, int docID, int fieldLength) {
    int8_t norm = encodeNorm(fieldLength);
    setNorm(field, docID, norm);
}

std::unique_ptr<NumericDocValues> InMemoryNormsProducer::getNorms(const FieldInfo& field) {
    auto it = norms_.find(field.name);
    if (it == norms_.end()) {
        return nullptr;
    }

    return std::make_unique<InMemoryNormValues>(it->second);
}

int8_t InMemoryNormsProducer::encodeNorm(int fieldLength) {
    if (fieldLength == 0) {
        return 0;
    }

    // Lucene norm encoding: norm = 1 / sqrt(fieldLength)
    // Encoded as byte: (int)(256 * norm)
    // Range: 0 (long docs) to 255 (short docs)

    float lengthNorm = 1.0f / std::sqrt(static_cast<float>(fieldLength));

    // Scale to 0-255 range
    int encoded = static_cast<int>(256.0f * lengthNorm);

    // Clamp to valid byte range
    encoded = std::max(0, std::min(255, encoded));

    return static_cast<int8_t>(encoded);
}

}  // namespace index
}  // namespace diagon
