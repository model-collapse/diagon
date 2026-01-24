// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <limits>
#include <vector>

namespace diagon {
namespace search {

/**
 * Represents a single hit in search results.
 *
 * Based on: org.apache.lucene.search.ScoreDoc
 */
struct ScoreDoc {
    /**
     * Document ID
     */
    int doc;

    /**
     * Score (higher is better)
     */
    float score;

    /**
     * Shard index (for distributed search, -1 if not used)
     */
    int shardIndex;

    /**
     * Constructor
     */
    ScoreDoc(int d = -1, float s = 0.0f, int shard = -1)
        : doc(d)
        , score(s)
        , shardIndex(shard) {}

    /**
     * Comparison for sorting (by score descending, then doc ascending)
     */
    bool operator<(const ScoreDoc& other) const {
        // Higher score comes first
        if (score != other.score) {
            return score > other.score;
        }
        // Tie-break by doc ID ascending
        return doc < other.doc;
    }

    /**
     * Greater-than comparison (needed for std::greater in priority_queue)
     */
    bool operator>(const ScoreDoc& other) const { return other < *this; }
};

/**
 * Total hits information with relation.
 *
 * Based on: org.apache.lucene.search.TotalHits
 */
struct TotalHits {
    enum class Relation {
        /**
         * The total hit count is equal to the value.
         */
        EQUAL_TO = 0,

        /**
         * The total hit count is greater than or equal to the value.
         */
        GREATER_THAN_OR_EQUAL_TO = 1
    };

    /**
     * Total hit count (or lower bound)
     */
    int64_t value;

    /**
     * Relation of value to actual count
     */
    Relation relation;

    /**
     * Constructor
     */
    TotalHits(int64_t val = 0, Relation rel = Relation::EQUAL_TO)
        : value(val)
        , relation(rel) {}
};

/**
 * Top scoring documents.
 *
 * Based on: org.apache.lucene.search.TopDocs
 */
struct TopDocs {
    /**
     * Total hits information
     */
    TotalHits totalHits;

    /**
     * Top documents, sorted by score descending
     */
    std::vector<ScoreDoc> scoreDocs;

    /**
     * Maximum score in this result set (or NaN if no scores)
     */
    float maxScore;

    /**
     * Constructor
     */
    TopDocs(const TotalHits& hits, const std::vector<ScoreDoc>& docs)
        : totalHits(hits)
        , scoreDocs(docs)
        , maxScore(computeMaxScore(docs)) {}

    /**
     * Default constructor
     */
    TopDocs()
        : totalHits(0, TotalHits::Relation::EQUAL_TO)
        , scoreDocs()
        , maxScore(std::numeric_limits<float>::quiet_NaN()) {}

private:
    static float computeMaxScore(const std::vector<ScoreDoc>& docs) {
        if (docs.empty()) {
            return std::numeric_limits<float>::quiet_NaN();
        }
        float max = docs[0].score;
        for (size_t i = 1; i < docs.size(); i++) {
            if (docs[i].score > max) {
                max = docs[i].score;
            }
        }
        return max;
    }
};

}  // namespace search
}  // namespace diagon
