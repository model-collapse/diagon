// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/search/BulkScorer.h"
#include "diagon/search/Collector.h"
#include "diagon/search/Scorer.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace diagon {
namespace search {

/**
 * Window-based bulk scorer for pure disjunctions (OR queries).
 *
 * Processes documents in 4096-doc inner windows within dynamically-sized
 * outer windows bounded by impact block boundaries. Uses essential/non-essential
 * clause partitioning to minimize per-document work.
 *
 * Key differences from doc-at-a-time WANDScorer:
 * - Priority queue ops only at window boundaries (~32x fewer)
 * - Bitset + score array for batch collection (no per-doc heap ops)
 * - Essential/non-essential split reduces per-doc scoring by ~50%
 * - Three dispatch paths optimize for common patterns
 *
 * Based on: org.apache.lucene.search.MaxScoreBulkScorer
 */
class MaxScoreBulkScorer : public BulkScorer {
public:
    /**
     * Construct from a list of term scorers.
     *
     * @param maxDoc Maximum document ID in the segment
     * @param scorers Clause scorers (ownership transferred)
     */
    MaxScoreBulkScorer(int maxDoc, std::vector<std::unique_ptr<Scorer>> scorers);

    int score(LeafCollector* collector, int min, int max) override;

    int64_t cost() const override { return cost_; }

private:
    static constexpr int INNER_WINDOW_SIZE = 1 << 12;  // 4096

    /**
     * Wrapper for a clause scorer within the bulk scorer.
     * Tracks current doc and window-level max score.
     */
    struct DisiWrapper {
        Scorer* scorer;         // Non-owning
        int doc;                // Current doc ID
        int64_t cost;           // Estimated cost
        float maxWindowScore;   // Max score in current outer window
        float efficiencyRatio;  // maxWindowScore / max(1, cost), updated per outer window

        DisiWrapper()
            : scorer(nullptr)
            , doc(-1)
            , cost(0)
            , maxWindowScore(0.0f)
            , efficiencyRatio(0.0f) {}
        explicit DisiWrapper(Scorer* s)
            : scorer(s)
            , doc(s->docID())
            , cost(s->cost())
            , maxWindowScore(0.0f)
            , efficiencyRatio(0.0f) {}
    };

    /**
     * Scorable adapter passed to collector.
     * Stores pre-computed score; collector calls setMinCompetitiveScore() on it.
     */
    class BulkScorable : public Scorable {
    public:
        float score_ = 0.0f;
        int docID_ = -1;
        float minCompetitiveScore = 0.0f;

        float score() override { return score_; }
        int docID() override { return docID_; }
        void setMinCompetitiveScore(float minScore) override { minCompetitiveScore = minScore; }
    };

    // Document/score buffer for batch passing to non-essential scoring + collection
    struct DocScoreBuffer {
        std::vector<int> docs;
        std::vector<float> scores;
        int size = 0;

        void clear() { size = 0; }
        void ensureCapacity(int n) {
            if (static_cast<int>(docs.size()) < n) {
                docs.resize(n);
                scores.resize(n);
            }
        }
        void add(int doc, float score) {
            if (size >= static_cast<int>(docs.size())) {
                docs.resize(size + 256);
                scores.resize(size + 256);
            }
            docs[size] = doc;
            scores[size] = score;
            size++;
        }
    };

    // Owned scorers
    std::vector<std::unique_ptr<Scorer>> ownedScorers_;

    // All wrappers, partitioned: [non-essential | essential]
    // allScorers_[0..firstEssentialScorer_-1] = non-essential (sorted by ascending maxScore/cost)
    // allScorers_[firstEssentialScorer_..n-1] = essential
    std::vector<DisiWrapper> allScorers_;

    // Prefix sums of maxWindowScores for non-essential scorers
    // maxScoreSums_[i] = sum of maxWindowScore for allScorers_[0..i]
    std::vector<double> maxScoreSums_;

    // Scratch arrays for partitioning (avoids per-call allocation)
    std::vector<DisiWrapper*> scratch_;
    std::vector<DisiWrapper*> nonEssentialScratch_;

    // Essential scorers priority queue (min-heap by doc ID)
    std::vector<DisiWrapper*> essentialQueue_;
    int essentialQueueSize_ = 0;

    // Inner window state
    std::array<uint64_t, INNER_WINDOW_SIZE / 64> windowMatches_{};
    std::array<float, INNER_WINDOW_SIZE> windowScores_{};

    // Batch scoring buffers (reused across inner window calls)
    static constexpr int BATCH_SIZE = Scorer::SCORER_BATCH_SIZE;
    int batchDocs_[BATCH_SIZE];
    float batchScores_[BATCH_SIZE];

    // Buffer for collecting docs+scores before non-essential scoring
    DocScoreBuffer buffer_;

    // Partition state
    int firstEssentialScorer_ = 0;
    int firstRequiredScorer_ = 0;
    float nextMinCompetitiveScore_ = 0.0f;

    // Scorable for collector interface
    BulkScorable scorable_;

    // Adaptive window sizing
    int64_t numCandidates_ = 0;
    int numOuterWindows_ = 0;
    int minWindowSize_ = 1;

    int maxDoc_;
    int64_t cost_;

    // ==================== Core Algorithm ====================

    /**
     * Compute outer window max from impact block boundaries.
     * Returns the first block boundary after windowMin across all essential scorers.
     */
    int computeOuterWindowMax(int windowMin);

    /**
     * Update max scores for all scorers within [windowMin, windowMax).
     */
    void updateMaxWindowScores(int windowMin, int windowMax);

    /**
     * Partition scorers into essential and non-essential.
     * Essential scorers are those needed to reach minCompetitiveScore.
     * @return true if at least one essential scorer exists
     */
    bool partitionScorers();

    // ==================== Inner Window Scoring ====================

    /**
     * Dispatch to appropriate inner window scoring path.
     */
    void scoreInnerWindow(LeafCollector* collector, int max);

    /**
     * Fast path: single essential scorer iterates directly.
     */
    void scoreInnerWindowSingleEssential(LeafCollector* collector, int upTo);

    /**
     * Bitset path: multiple essential scorers use windowMatches_ + windowScores_.
     */
    void scoreInnerWindowMultipleEssentials(LeafCollector* collector, int max);

    // ==================== Non-Essential Scoring ====================

    /**
     * Score non-essential clauses on buffer, then collect.
     */
    void scoreNonEssentialClauses(LeafCollector* collector, int numNonEssentialClauses);

    /**
     * Filter out docs from buffer where accumulated score + maxRemainingScore <
     * minCompetitiveScore.
     */
    void filterCompetitiveHits(float maxRemainingScore);

    /**
     * Add scores from an optional (non-essential) clause to matching docs in buffer.
     */
    void applyOptionalClause(DisiWrapper& w);

    /**
     * Intersect buffer with a required clause (single essential case).
     */
    void applyRequiredClause(DisiWrapper& w);

    // ==================== Priority Queue Operations ====================

    void essentialQueueClear();
    void essentialQueuePush(DisiWrapper* w);

    inline DisiWrapper* essentialQueueTop() {
        if (essentialQueueSize_ == 0)
            return nullptr;
        return essentialQueue_[0];
    }

    DisiWrapper* essentialQueueTop2();  // Second-smallest by doc

    inline void essentialQueueUpdateTop() {
        if (essentialQueueSize_ > 0) {
            essentialQueueSiftDown(0);
        }
    }

    void essentialQueueSiftDown(int i);
    void essentialQueueSiftUp(int i);

    // ==================== Bitset Helpers (inlined for hot path) ====================

    inline void windowSetBit(int index) { windowMatches_[index >> 6] |= (1ULL << (index & 63)); }

    inline void windowClearAll(int size) {
        int words = (size + 63) >> 6;
        for (int i = 0; i < words; i++) {
            windowMatches_[i] = 0;
        }
    }

    inline int windowNextSetBit(int from, int limit) const {
        int wordIndex = from >> 6;
        int maxWord = (limit + 63) >> 6;

        if (wordIndex >= maxWord)
            return limit;

        uint64_t word = windowMatches_[wordIndex] >> (from & 63);
        if (word != 0) {
            return from + __builtin_ctzll(word);
        }

        wordIndex++;
        while (wordIndex < maxWord) {
            word = windowMatches_[wordIndex];
            if (word != 0) {
                int bit = (wordIndex << 6) + __builtin_ctzll(word);
                return bit < limit ? bit : limit;
            }
            wordIndex++;
        }
        return limit;
    }
};

}  // namespace search
}  // namespace diagon
