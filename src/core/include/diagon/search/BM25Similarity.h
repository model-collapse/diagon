// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/util/BytesRef.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace diagon {
namespace search {

/**
 * Collection statistics for a field
 */
struct CollectionStatistics {
    std::string field;
    int64_t maxDoc;            // Number of documents
    int64_t docCount;          // Documents with this field
    int64_t sumTotalTermFreq;  // Total term occurrences
    int64_t sumDocFreq;        // Sum of document frequencies

    CollectionStatistics(const std::string& field_, int64_t maxDoc_, int64_t docCount_,
                         int64_t sumTotalTermFreq_, int64_t sumDocFreq_)
        : field(field_)
        , maxDoc(maxDoc_)
        , docCount(docCount_)
        , sumTotalTermFreq(sumTotalTermFreq_)
        , sumDocFreq(sumDocFreq_) {}
};

/**
 * Term statistics
 */
struct TermStatistics {
    util::BytesRef term;
    int64_t docFreq;        // Documents containing this term
    int64_t totalTermFreq;  // Total occurrences of this term

    TermStatistics(const util::BytesRef& term_, int64_t docFreq_, int64_t totalTermFreq_)
        : term(term_)
        , docFreq(docFreq_)
        , totalTermFreq(totalTermFreq_) {}
};

/**
 * BM25 similarity scoring
 *
 * BM25 formula (Lucene 8.x+ simplified):
 * score = IDF * freq / (freq + k1 * (1 - b + b * fieldLength / avgFieldLength))
 *
 * where:
 * - IDF = ln(1 + (N - df + 0.5) / (df + 0.5))
 * - k1 = term frequency saturation parameter (default 1.2)
 * - b = length normalization parameter (default 0.75)
 *
 * Note: The classic BM25 formula includes (k1 + 1) in the numerator,
 * but Lucene 8+ removed this constant multiplier since it doesn't affect ranking.
 *
 * Based on: org.apache.lucene.search.similarities.BM25Similarity
 */
class BM25Similarity {
public:
    /**
     * Constructor with default parameters
     */
    BM25Similarity()
        : k1_(1.2f)
        , b_(0.75f) {}

    /**
     * Constructor with custom parameters
     * @param k1 Term frequency saturation (default 1.2)
     * @param b Length normalization (default 0.75)
     */
    BM25Similarity(float k1, float b)
        : k1_(k1)
        , b_(b) {}

    /**
     * Compute IDF (Inverse Document Frequency)
     * @param docFreq Number of documents containing term
     * @param docCount Total number of documents with field
     */
    float idf(int64_t docFreq, int64_t docCount) const {
        // Lucene's BM25+ formula
        return std::log(1.0f + (docCount - docFreq + 0.5f) / (docFreq + 0.5f));
    }

    /**
     * Compute BM25 score
     * @param freq Term frequency in document
     * @param norm Encoded document length (Lucene norm encoding)
     */
    float score(float freq, long norm) const {
        // Decode norm to get field length
        float fieldLength = decodeNorm(norm);

        // avgFieldLength is encoded in the norm for efficiency
        // For Phase 4, we'll use a simplified approach
        float avgFieldLength = fieldLength;  // Simplified

        // BM25 formula (Lucene 8+ simplified)
        float k = k1_ * (1.0f - b_ + b_ * fieldLength / avgFieldLength);
        return freq / (freq + k);
    }

    /**
     * Create scorer for a term
     */
    class SimScorer {
    public:
        SimScorer(float idf, float k1, float b, float avgFieldLength)
            : idf_(idf)
            , k1_(k1)
            , b_(b)
            , inv_avgFieldLength_(1.0f / avgFieldLength)  // Precompute reciprocal (opt #2)
        {}

        /**
         * Score a document (optimized version)
         * @param freq Term frequency
         * @param norm Document norm (encoded length)
         *
         * Optimizations applied:
         * - Inlined decodeNorm() to eliminate function call overhead
         * - Precomputed 1/avgFieldLength for multiplication instead of division
         * - Removed freq==0 branch (branchless: 0*anything=0 anyway)
         * - Added branch hints for rare norm values
         */
        __attribute__((always_inline))
        inline float score(float freq, long norm) const {
            // Optimization #3: Remove freq==0 branch
            // If freq==0, the result will be 0 anyway (0 * idf_ = 0)
            // No need for explicit check - let it compute naturally

            // Optimization #1: Inline decodeNorm with branch hints
            // Decode norm to field length inline (avoid function call)
            float fieldLength;
            if (__builtin_expect(norm == 0 || norm == 127, 0)) {
                // Rare case: branch hint tells CPU this is unlikely
                fieldLength = 1.0f;
            } else {
                // Common case: normal decoding
                float normFloat = static_cast<float>(norm);
                float invNorm = 127.0f / normFloat;
                fieldLength = invNorm * invNorm;
            }

            // Optimization #2: Use precomputed reciprocal (multiplication is 5× faster than division)
            // Before: b_ * fieldLength / avgFieldLength
            // After:  b_ * fieldLength * inv_avgFieldLength_
            float k = k1_ * (1.0f - b_ + b_ * fieldLength * inv_avgFieldLength_);
            // BM25 formula (Lucene 8+ simplified without (k1+1) term)
            return idf_ * freq / (freq + k);
        }

        /**
         * Get IDF value (for batch scoring)
         */
        float getIDF() const { return idf_; }

        /**
         * Get k1 parameter (for batch scoring)
         */
        float getK1() const { return k1_; }

        /**
         * Get b parameter (for batch scoring)
         */
        float getB() const { return b_; }

        /**
         * Get avgFieldLength (for WAND scorer initialization)
         */
        float getAvgFieldLength() const { return 1.0f / inv_avgFieldLength_; }

    private:
        float idf_;
        float k1_;
        float b_;
        float inv_avgFieldLength_;  // Precomputed 1/avgFieldLength for fast multiplication

        // Note: decodeNorm() removed - now inlined in score() for better performance
    };

    /**
     * Create scorer for term
     */
    SimScorer scorer(float boost, const CollectionStatistics& collectionStats,
                     const TermStatistics& termStats) const {
        float idfValue = idf(termStats.docFreq, collectionStats.docCount);

        // Compute actual average field length from index statistics
        // avgFieldLength = total term occurrences / documents with field
        float avgFieldLength = 50.0f;  // Default fallback
        if (collectionStats.docCount > 0 && collectionStats.sumTotalTermFreq > 0) {
            avgFieldLength = static_cast<float>(collectionStats.sumTotalTermFreq) /
                           static_cast<float>(collectionStats.docCount);
        }

        return SimScorer(idfValue * boost, k1_, b_, avgFieldLength);
    }

private:
    float k1_;  // Term frequency saturation
    float b_;   // Length normalization

    /**
     * Decode Lucene norm to field length
     *
     * Encoding: norm = 127 / sqrt(length)
     * Decoding: length = (127 / norm)^2
     */
    static float decodeNorm(long norm) {
        if (norm == 0) {
            return 1.0f;  // Default for deleted/missing docs
        }
        if (norm == 127) {
            return 1.0f;  // Single term document
        }

        float normFloat = static_cast<float>(norm);
        float length = 127.0f / normFloat;
        return length * length;  // Return length (not sqrt(length))
    }
};

}  // namespace search
}  // namespace diagon
