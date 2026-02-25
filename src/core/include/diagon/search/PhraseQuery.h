// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/search/Query.h"
#include "diagon/search/TermQuery.h"
#include "diagon/search/Weight.h"

#include <memory>
#include <string>
#include <vector>

namespace diagon {
namespace search {

/**
 * PhraseQuery - Query matching documents containing terms at consecutive positions.
 *
 * Exact phrase matching (slop=0): all terms must appear at consecutive positions
 * in the specified order. For example, PhraseQuery("quick", "brown", "fox")
 * matches documents containing "quick brown fox" as a phrase.
 *
 * Single-term phrase queries are rewritten to TermQuery for efficiency.
 *
 * Based on: org.apache.lucene.search.PhraseQuery
 */
class PhraseQuery : public Query {
public:
    /**
     * Builder for constructing PhraseQuery instances.
     */
    class Builder {
    public:
        /**
         * Create builder for the given field.
         * @param field Field name for all terms
         */
        explicit Builder(const std::string& field)
            : field_(field)
            , slop_(0) {}

        /**
         * Add a term at the next sequential position.
         * @param text Term text
         * @return this builder
         */
        Builder& add(const std::string& text) {
            int pos = positions_.empty() ? 0 : (positions_.back() + 1);
            terms_.push_back(Term(field_, text));
            positions_.push_back(pos);
            return *this;
        }

        /**
         * Add a term at a specific position.
         * @param text Term text
         * @param position Explicit position
         * @return this builder
         */
        Builder& add(const std::string& text, int position) {
            terms_.push_back(Term(field_, text));
            positions_.push_back(position);
            return *this;
        }

        /**
         * Set slop (distance tolerance between terms).
         * 0 = exact phrase match (default).
         * @param slop Maximum edit distance for positions
         * @return this builder
         */
        Builder& setSlop(int slop) {
            slop_ = slop;
            return *this;
        }

        /**
         * Build the PhraseQuery.
         * @return Constructed PhraseQuery
         */
        std::unique_ptr<PhraseQuery> build() {
            return std::make_unique<PhraseQuery>(field_, std::move(terms_), std::move(positions_),
                                                 slop_);
        }

    private:
        std::string field_;
        std::vector<Term> terms_;
        std::vector<int> positions_;
        int slop_;
    };

    /**
     * Constructor (use Builder for cleaner construction).
     */
    PhraseQuery(const std::string& field, std::vector<Term> terms, std::vector<int> positions,
                int slop = 0);

    // ==================== Accessors ====================

    const std::string& getField() const { return field_; }
    const std::vector<Term>& getTerms() const { return terms_; }
    const std::vector<int>& getPositions() const { return positions_; }
    int getSlop() const { return slop_; }

    // ==================== Query Interface ====================

    std::unique_ptr<Weight> createWeight(IndexSearcher& searcher, ScoreMode scoreMode,
                                         float boost) const override;

    std::unique_ptr<Query> rewrite(index::IndexReader& reader) const override;

    std::string toString(const std::string& field) const override;

    bool equals(const Query& other) const override;

    size_t hashCode() const override;

    std::unique_ptr<Query> clone() const override;

private:
    std::string field_;
    std::vector<Term> terms_;
    std::vector<int> positions_;
    int slop_;
};

}  // namespace search
}  // namespace diagon
