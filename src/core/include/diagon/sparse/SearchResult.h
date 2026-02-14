// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>

namespace diagon {
namespace sparse {

/**
 * Search result for sparse vector queries
 *
 * Common result structure used by both SINDI and QBlock indexes.
 */
struct SearchResult {
    uint32_t doc_id;  // Document ID
    float score;      // Similarity score

    SearchResult()
        : doc_id(0)
        , score(0.0f) {}  // Default constructor
    SearchResult(uint32_t id, float s)
        : doc_id(id)
        , score(s) {}

    // Sort by score descending (higher score first)
    bool operator<(const SearchResult& other) const { return score > other.score; }
};

}  // namespace sparse
}  // namespace diagon
