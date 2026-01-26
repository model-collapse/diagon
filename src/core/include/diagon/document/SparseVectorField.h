// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/document/IndexableField.h"
#include "diagon/sparse/SparseVector.h"

#include <memory>
#include <string>

namespace diagon {
namespace document {

/**
 * SparseVectorField - Field containing a sparse vector
 *
 * Sparse vectors are high-dimensional vectors with mostly zero values,
 * represented as (index, value) pairs. Used for:
 * - Learned sparse retrieval (SPLADE)
 * - BM25 expansions
 * - Sparse neural embeddings
 *
 * Example:
 * ```cpp
 * sparse::SparseVector vec;
 * vec.add(10, 0.8f);
 * vec.add(25, 1.2f);
 * vec.add(100, 0.5f);
 *
 * Document doc;
 * doc.addField(std::make_unique<SparseVectorField>("embedding", vec, true));
 * ```
 *
 * Based on Lucene's KnnVectorField pattern, adapted for sparse vectors.
 */
class SparseVectorField : public IndexableField {
public:
    /**
     * Predefined field type for sparse vectors
     *
     * Sparse vectors are:
     * - Not tokenized (already in term space)
     * - Not stored by default (can be large)
     * - Indexed with special sparse vector index
     * - No norms needed
     */
    static FieldType TYPE_NOT_STORED;
    static FieldType TYPE_STORED;

    /**
     * Create a sparse vector field
     *
     * @param name Field name
     * @param vector Sparse vector data
     * @param stored Whether to store the original vector (default: false)
     */
    SparseVectorField(std::string name,
                      const sparse::SparseVector& vector,
                      bool stored = false);

    /**
     * Create a sparse vector field with custom field type
     *
     * @param name Field name
     * @param vector Sparse vector data
     * @param type Custom field type configuration
     */
    SparseVectorField(std::string name,
                      const sparse::SparseVector& vector,
                      FieldType type);

    // IndexableField implementation
    std::string name() const override { return name_; }
    const FieldType& fieldType() const override { return type_; }

    /**
     * Sparse vectors don't have string representation
     */
    std::optional<std::string> stringValue() const override { return std::nullopt; }

    /**
     * Sparse vectors don't have numeric representation
     */
    std::optional<int64_t> numericValue() const override { return std::nullopt; }

    /**
     * Binary representation of sparse vector (for storage)
     * Serialized as: [num_elements, (index, value), ...]
     */
    std::optional<util::BytesRef> binaryValue() const override;

    /**
     * Sparse vectors are not tokenized (already in term space)
     */
    std::vector<std::string> tokenize() const override { return {}; }

    /**
     * Get the sparse vector
     */
    const sparse::SparseVector& sparseVector() const { return vector_; }

    /**
     * Get maximum dimension of the vector
     */
    uint32_t maxDimension() const { return vector_.maxDimension(); }

    /**
     * Get number of non-zero elements
     */
    size_t size() const { return vector_.size(); }

private:
    std::string name_;
    FieldType type_;
    sparse::SparseVector vector_;
};

}  // namespace document
}  // namespace diagon
