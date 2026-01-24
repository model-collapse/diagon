// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <memory>

namespace diagon {
namespace search {

// Forward declaration
class Query;

/**
 * Boolean clause relationship
 *
 * Extended from Lucene 8.x+ with FILTER support
 */
enum class Occur : uint8_t {
    /**
     * Required clause - MUST match and participates in scoring
     */
    MUST = 0,

    /**
     * Optional clause - MAY match and participates in scoring
     */
    SHOULD = 1,

    /**
     * Prohibited clause - MUST NOT match, no scoring
     */
    MUST_NOT = 2,

    /**
     * Required clause - MUST match but does NOT participate in scoring
     *
     * Use for:
     * - Range filters (price, date)
     * - Category filters
     * - Status filters (in_stock, published)
     *
     * Benefits:
     * - No score computation overhead
     * - Eligible for caching
     * - Works with skip indexes
     */
    FILTER = 3
};

/**
 * Boolean clause (query + occurrence relationship)
 *
 * Based on: org.apache.lucene.search.BooleanClause
 *
 * NOTE: Stub implementation - provides structure only.
 * BooleanQuery not yet implemented.
 */
struct BooleanClause {
    std::shared_ptr<Query> query;
    Occur occur;

    BooleanClause(std::shared_ptr<Query> q, Occur o)
        : query(std::move(q))
        , occur(o) {}

    bool isScoring() const { return occur == Occur::MUST || occur == Occur::SHOULD; }

    bool isProhibited() const { return occur == Occur::MUST_NOT; }

    bool isRequired() const { return occur == Occur::MUST || occur == Occur::FILTER; }

    bool isFilter() const { return occur == Occur::FILTER; }
};

}  // namespace search
}  // namespace diagon
