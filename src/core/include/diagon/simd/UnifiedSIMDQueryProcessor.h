// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/simd/SIMDScorers.h"
#include "diagon/simd/UnifiedColumnFormat.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace diagon {
namespace simd {

/**
 * Scoring mode for unified processor
 */
enum class ScoringMode {
    BM25,           // Dynamic BM25 computation
    RANK_FEATURES,  // Static precomputed weights
    TF_IDF          // Classic TF-IDF
};

/**
 * Top scoring document
 */
struct ScoreDoc {
    int doc;
    float score;

    ScoreDoc(int doc_ = 0, float score_ = 0.0f)
        : doc(doc_)
        , score(score_) {}
};

/**
 * Top documents result
 */
struct TopDocs {
    int totalHits{0};
    std::vector<ScoreDoc> scoreDocs;

    TopDocs() = default;
    explicit TopDocs(int totalHits_)
        : totalHits(totalHits_) {}
};

/**
 * Filter interface (forward declaration)
 */
class Filter {
public:
    virtual ~Filter() = default;
    virtual std::string getFieldName() const = 0;
};

using FilterPtr = std::shared_ptr<Filter>;

/**
 * Unified column reader interface (stub)
 */
class UnifiedColumnReader {
public:
    virtual ~UnifiedColumnReader() = default;

    /**
     * Get sparse column (posting list)
     */
    template<typename ValueType>
    ColumnWindow<ValueType>* getSparseColumn(const std::string& term) const {
        // Stub: would load from index
        return nullptr;
    }

    /**
     * Get dense column (doc values)
     */
    template<typename ValueType>
    ColumnWindow<ValueType>* getDenseColumn(const std::string& fieldName) const {
        // Stub: would load from index
        return nullptr;
    }
};

/**
 * Unified SIMD query processor
 *
 * Supports:
 * - BM25 scoring (dynamic computation)
 * - rank_features scoring (static weights)
 * - TF-IDF scoring
 * - Filters (SIMD range checks)
 *
 * Based on: SINDI paper + unified storage architecture
 *
 * NOTE: Stub implementation - SIMD operations and actual query
 * execution not fully implemented.
 */
class UnifiedSIMDQueryProcessor {
public:
    explicit UnifiedSIMDQueryProcessor(const UnifiedColumnReader& reader,
                                       ScoringMode mode = ScoringMode::BM25)
        : reader_(reader)
        , mode_(mode)
        , bm25Scorer_(1.2f, 0.75f, 100.0f)
        , rankFeaturesScorer_()
        , tfIdfScorer_() {}

    // ==================== Configuration ====================

    /**
     * Get scoring mode
     */
    ScoringMode getScoringMode() const { return mode_; }

    /**
     * Set scoring mode
     */
    void setScoringMode(ScoringMode mode) { mode_ = mode; }

    /**
     * Get BM25 scorer (for parameter tuning)
     */
    SIMDBm25Scorer& getBm25Scorer() { return bm25Scorer_; }

    // ==================== Query Execution ====================

    /**
     * Execute OR query with appropriate scoring
     *
     * @param queryTerms Terms with weights (IDF for BM25, query weights for rank_features)
     * @param filter Optional filter to apply
     * @param topK Number of top results to return
     *
     * NOTE: Stub - actual SIMD execution not implemented
     */
    TopDocs searchOr(const std::vector<std::pair<std::string, float>>& queryTerms, FilterPtr filter,
                     int topK) {
        TopDocs result;

        if (mode_ == ScoringMode::BM25) {
            // Stub: would load tf columns and doc_length column
            // Then execute SIMD BM25 scoring
            std::map<std::string, ColumnWindow<int>*> tfColumns;
            // ... load columns ...

            // auto results = bm25Scorer_.scoreOrQuery(...);

        } else if (mode_ == ScoringMode::RANK_FEATURES) {
            // Stub: would load feature columns with precomputed weights
            // Then execute SIMD scatter-add
            std::map<std::string, ColumnWindow<float>*> featureColumns;
            // ... load columns ...

            // auto results = rankFeaturesScorer_.scoreOrQuery(...);

        } else if (mode_ == ScoringMode::TF_IDF) {
            // Stub: would load tf columns
            // Then execute SIMD TF-IDF scoring
            std::map<std::string, ColumnWindow<int>*> tfColumns;
            // ... load columns ...

            // auto results = tfIdfScorer_.scoreOrQuery(...);
        }

        // Apply filter if provided (also SIMD-accelerated)
        // if (filter) {
        //     result = applyFilter(result, filter);
        // }

        return result;
    }

    /**
     * Execute AND query
     *
     * NOTE: Stub implementation
     */
    TopDocs searchAnd(const std::vector<std::pair<std::string, float>>& queryTerms,
                      FilterPtr filter, int topK) {
        // Stub: would intersect posting lists then score
        return TopDocs{};
    }

    /**
     * Execute phrase query
     *
     * NOTE: Stub implementation
     */
    TopDocs searchPhrase(const std::vector<std::string>& terms, FilterPtr filter, int topK) {
        // Stub: would verify position constraints
        return TopDocs{};
    }

private:
    const UnifiedColumnReader& reader_;
    ScoringMode mode_;

    SIMDBm25Scorer bm25Scorer_;
    RankFeaturesScorer rankFeaturesScorer_;
    SIMDTfIdfScorer tfIdfScorer_;
};

}  // namespace simd
}  // namespace diagon
