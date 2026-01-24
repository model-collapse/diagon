// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/search/Query.h"
#include "diagon/search/Weight.h"
#include "diagon/util/BytesRef.h"

#include <memory>
#include <string>

namespace diagon {
namespace search {

/**
 * Term - represents field + term bytes
 *
 * Based on: org.apache.lucene.index.Term
 */
class Term {
public:
    Term(const std::string& field, const std::string& text)
        : field_(field), bytes_(text) {}

    Term(const std::string& field, const util::BytesRef& bytes)
        : field_(field), bytes_(bytes) {}

    const std::string& field() const { return field_; }
    const util::BytesRef& bytes() const { return bytes_; }
    std::string text() const { return bytes_.toString(); }

    bool equals(const Term& other) const {
        return field_ == other.field_ && bytes_.equals(other.bytes_);
    }

    size_t hashCode() const {
        size_t h = std::hash<std::string>{}(field_);
        h ^= bytes_.hashCode() + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }

private:
    std::string field_;
    util::BytesRef bytes_;
};

/**
 * TermQuery - Query matching documents containing a specific term
 *
 * Phase 4 implementation:
 * - Basic BM25 scoring
 * - No term state caching
 * - No score upper bounds (WAND)
 * - No two-phase iteration
 *
 * Based on: org.apache.lucene.search.TermQuery
 */
class TermQuery : public Query {
public:
    /**
     * Constructor
     * @param term Term to search for
     */
    explicit TermQuery(const Term& term);

    /**
     * Get the term
     */
    const Term& getTerm() const { return term_; }

    // ==================== Query Interface ====================

    std::unique_ptr<Weight> createWeight(
        IndexSearcher& searcher,
        ScoreMode scoreMode,
        float boost) const override;

    std::string toString(const std::string& field) const override;

    bool equals(const Query& other) const override;

    size_t hashCode() const override;

    std::unique_ptr<Query> clone() const override;

private:
    Term term_;
};

}  // namespace search
}  // namespace diagon
