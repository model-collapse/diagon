// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/document/SparseVectorField.h"

#include <cstring>
#include <vector>

namespace diagon {
namespace document {

// ==================== Static Field Types ====================

FieldType SparseVectorField::TYPE_NOT_STORED = []() {
    FieldType ft;
    ft.indexOptions = index::IndexOptions::NONE;  // Not in inverted index
    ft.docValuesType = index::DocValuesType::NONE;  // Special sparse vector index
    ft.stored = false;
    ft.tokenized = false;  // Already in term space
    ft.omitNorms = true;   // No norms needed
    return ft;
}();

FieldType SparseVectorField::TYPE_STORED = []() {
    FieldType ft;
    ft.indexOptions = index::IndexOptions::NONE;
    ft.docValuesType = index::DocValuesType::NONE;
    ft.stored = true;      // Store for retrieval
    ft.tokenized = false;
    ft.omitNorms = true;
    return ft;
}();

// ==================== Construction ====================

SparseVectorField::SparseVectorField(std::string name,
                                     const sparse::SparseVector& vector,
                                     bool stored)
    : name_(std::move(name))
    , type_(stored ? TYPE_STORED : TYPE_NOT_STORED)
    , vector_(vector) {}

SparseVectorField::SparseVectorField(std::string name,
                                     const sparse::SparseVector& vector,
                                     FieldType type)
    : name_(std::move(name))
    , type_(type)
    , vector_(vector) {}

// ==================== Binary Serialization ====================

std::optional<util::BytesRef> SparseVectorField::binaryValue() const {
    if (!type_.stored) {
        return std::nullopt;
    }

    // Serialize sparse vector as binary data
    // Format: [num_elements:4] [index:4, value:4] ...
    //
    // This is a simple format for storage. For production, consider:
    // - VInt encoding for indices
    // - Quantization for values
    // - Compression (LZ4/ZSTD)

    size_t num_elements = vector_.size();
    size_t data_size = sizeof(uint32_t) +  // num_elements
                       num_elements * (sizeof(uint32_t) + sizeof(float));  // (index, value) pairs

    // Allocate buffer
    std::vector<uint8_t> buffer(data_size);
    uint8_t* ptr = buffer.data();

    // Write num_elements
    uint32_t num = static_cast<uint32_t>(num_elements);
    std::memcpy(ptr, &num, sizeof(uint32_t));
    ptr += sizeof(uint32_t);

    // Write (index, value) pairs
    for (size_t i = 0; i < num_elements; ++i) {
        const auto& elem = vector_[i];

        // Write index
        std::memcpy(ptr, &elem.index, sizeof(uint32_t));
        ptr += sizeof(uint32_t);

        // Write value
        std::memcpy(ptr, &elem.value, sizeof(float));
        ptr += sizeof(float);
    }

    // Create BytesRef (assumes caller manages lifetime)
    // Note: In production, this should use a memory pool or allocator
    return util::BytesRef(buffer.data(), data_size);
}

}  // namespace document
}  // namespace diagon
