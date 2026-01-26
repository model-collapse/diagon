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
 * BM25 formula:
 * score = IDF * (freq * (k1 + 1)) / (freq + k1 * (1 - b + b * fieldLength / avgFieldLength))
 *
 * where:
 * - IDF = ln(1 + (N - df + 0.5) / (df + 0.5))
 * - k1 = term frequency saturation parameter (default 1.2)
 * - b = length normalization parameter (default 0.75)
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

        // BM25 formula
        float k = k1_ * (1.0f - b_ + b_ * fieldLength / avgFieldLength);
        return freq * (k1_ + 1.0f) / (freq + k);
    }

    /**
     * Create scorer for a term
     */
    class SimScorer {
    public:
        SimScorer(float idf, float k1, float b)
            : idf_(idf)
            , k1_(k1)
            , b_(b) {}

        /**
         * Score a document
         * @param freq Term frequency
         * @param norm Document norm (encoded length)
         */
        float score(float freq, long norm) const {
            if (freq == 0.0f)
                return 0.0f;

            // Decode norm to field length
            float fieldLength = decodeNorm(norm);

            // Use a reasonable default average field length
            // Phase 5 will compute this from collection statistics
            float avgFieldLength = 50.0f;  // Typical document has ~50 terms

            // BM25 formula with length normalization
            float k = k1_ * (1.0f - b_ + b_ * fieldLength / avgFieldLength);
            return idf_ * freq * (k1_ + 1.0f) / (freq + k);
        }

    private:
        float idf_;
        float k1_;
        float b_;

        float decodeNorm(long norm) const {
            // Decode norm to field length
            // Encoding: norm = 127 / sqrt(length)
            // Decoding: length = (127 / norm)^2

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

    /**
     * Create scorer for term
     */
    SimScorer scorer(float boost, const CollectionStatistics& collectionStats,
                     const TermStatistics& termStats) const {
        float idfValue = idf(termStats.docFreq, collectionStats.docCount);
        return SimScorer(idfValue * boost, k1_, b_);
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
