// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/PostingsEnum.h"
#include "diagon/search/Scorer.h"
#include "diagon/search/BM25Similarity.h"

#include <memory>
#include <vector>
#include <cstdint>

namespace diagon {
namespace search {

/**
 * WAND (Weak AND) Scorer with Block-Max optimization.
 *
 * Based on:
 * - "Efficient Query Evaluation using a Two-Level Retrieval Process"
 *   by Broder, Carmel, Herscovici, Soffer and Zien
 * - "Faster Top-k Document Retrieval Using Block-Max Indexes"
 *   by Ding and Suel
 *
 * Implementation:
 * - Three-heap structure: tail (behind), lead (on doc), head (ahead)
 * - Dynamic threshold tracking from collector
 * - Skip logic: if sum(max_scores) < threshold, skip block
 *
 * Phase 2 (P0 Task #39): Block-Max WAND for early termination
 */
class WANDScorer : public Scorer {
public:
    /**
     * Wrapper for a term scorer with impact information.
     */
    struct ScorerWrapper {
        Scorer* scorer;                    // Not owned (owned by parent)
        float maxScore;                    // Maximum score for current block
        int doc;                           // Current doc ID
        int64_t cost;                      // Cost estimate
        ScorerWrapper* next;               // Linked list pointer for lead

        ScorerWrapper(Scorer* s, int64_t c)
            : scorer(s), maxScore(0.0f), doc(-1), cost(c), next(nullptr) {}
    };

    /**
     * Constructor
     * @param scorers List of term scorers
     * @param similarity BM25 similarity for score computation
     * @param minShouldMatch Minimum number of terms that must match
     */
    WANDScorer(std::vector<std::unique_ptr<Scorer>>& scorers,
               const BM25Similarity& similarity,
               int minShouldMatch = 0);

    ~WANDScorer() override;

    // ==================== DocIdSetIterator ====================

    int docID() const override { return doc_; }

    int nextDoc() override;

    int advance(int target) override;

    int64_t cost() const override { return cost_; }

    // ==================== Scorer ====================

    float score() const override;

    const Weight& getWeight() const override {
        // Return first scorer's weight (all should have same query weight)
        return allScorers_[0]->getWeight();
    }

    /**
     * Set minimum competitive score for early termination.
     * Called by collector when threshold changes.
     *
     * @param minScore New minimum competitive score
     */
    void setMinCompetitiveScore(float minScore);

private:
    // Configuration
    const BM25Similarity& similarity_;
    int minShouldMatch_;

    // Minimum competitive score (scaled for integer arithmetic)
    float minCompetitiveScore_;

    // All scorers (owned)
    std::vector<std::unique_ptr<Scorer>> allScorers_;
    std::vector<ScorerWrapper> wrappers_;  // One wrapper per scorer

    // Three heaps
    ScorerWrapper* lead_;     // Linked list of scorers on current doc
    std::vector<ScorerWrapper*> head_;     // Heap of scorers ahead (ordered by doc ID)
    std::vector<ScorerWrapper*> tail_;     // Heap of scorers behind (ordered by max score)

    int doc_;                 // Current doc ID
    float leadScore_;         // Sum of scores from lead scorers
    int freq_;                // Number of lead scorers (matching terms)

    float tailMaxScore_;      // Sum of max scores in tail
    int tailSize_;

    int64_t cost_;            // Total cost
    int upTo_;                // Upper bound for current block max scores

    /**
     * Add a scorer to the lead list (scorers positioned on current doc).
     */
    void addLead(ScorerWrapper* wrapper);

    /**
     * Move all lead scorers back to tail heap.
     */
    void pushBackLeads(int target);

    /**
     * Advance head to ensure all scorers are on or after target.
     */
    ScorerWrapper* advanceHead(int target);

    /**
     * Pop scorer with highest max score from tail and advance it.
     */
    void advanceTail();

    /**
     * Move scorers from head to lead if they match current doc.
     */
    void moveToNextCandidate();

    /**
     * Check if current lead scorers satisfy constraints.
     * Returns true if match, false if we should skip.
     */
    bool matches();

    /**
     * Update max scores for all scorers up to target.
     * Called when entering new block.
     */
    void updateMaxScores(int target);

    /**
     * Insert wrapper into tail heap, maintaining max-heap property.
     * Returns evicted wrapper if tail is full, nullptr otherwise.
     */
    ScorerWrapper* insertTailWithOverFlow(ScorerWrapper* wrapper);

    /**
     * Pop wrapper with highest max score from tail.
     */
    ScorerWrapper* popTail();

    /**
     * Heapify tail upward (used after updating max score).
     */
    void upHeapMaxScore(int index);

    /**
     * Heapify tail downward (used after pop).
     */
    void downHeapMaxScore(int index);

    /**
     * Heapify head upward by doc ID.
     */
    void upHeapDocID(int index);

    /**
     * Heapify head downward by doc ID.
     */
    void downHeapDocID(int index);
};

}  // namespace search
}  // namespace diagon
