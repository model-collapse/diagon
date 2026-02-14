// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/PostingsEnum.h"
#include "diagon/search/BM25Similarity.h"
#include "diagon/search/Scorer.h"

#include <cstdint>
#include <memory>
#include <vector>

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
        Scorer* scorer;          // Not owned (owned by parent)
        int64_t scaledMaxScore;  // Maximum score for current block (scaled to integer)
        int doc;                 // Current doc ID
        int64_t cost;            // Cost estimate
        ScorerWrapper* next;     // Linked list pointer for lead

        ScorerWrapper(Scorer* s, int64_t c)
            : scorer(s)
            , scaledMaxScore(0)
            , doc(-1)
            , cost(c)
            , next(nullptr) {}
    };

    /**
     * Constructor
     * @param scorers List of term scorers
     * @param similarity BM25 similarity for score computation
     * @param minShouldMatch Minimum number of terms that must match
     */
    WANDScorer(std::vector<std::unique_ptr<Scorer>>& scorers, const BM25Similarity& similarity,
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

    /**
     * Get total number of documents that matched the query.
     * This includes ALL matching documents, not just those collected.
     *
     * @return Total matching document count
     */
    int getTotalMatches() const override { return matchingDocs_; }

    // ==================== Instrumentation (Diagnostic) ====================

    /**
     * Get number of documents actually scored (advanced through).
     * This counts every call to nextDoc/advance that returns a valid doc.
     */
    int getDocsScored() const { return docsScored_; }

    /**
     * Get number of tail promotions (scorers moved from tail to head).
     * High count indicates WAND is working hard but not skipping effectively.
     */
    int getTailPromotions() const { return tailPromotions_; }

    /**
     * Get number of times updateMaxScores was called.
     * High count indicates frequent block boundary crossings.
     */
    int getMaxScoreUpdates() const { return maxScoreUpdates_; }

    /**
     * Get number of documents that matched minShouldMatch constraint.
     * This is the actual "matching" documents for the query.
     */
    int getMatchingDocs() const { return matchingDocs_; }

    /**
     * Get number of blocks skipped by moveToNextBlock.
     * This indicates WAND's effectiveness at skipping non-competitive blocks.
     */
    int getBlocksSkipped() const { return blocksSkipped_; }

    /**
     * Get number of times moveToNextBlock was called.
     * Compared with blocksSkipped to measure skip effectiveness.
     */
    int getMoveToNextBlockCalls() const { return moveToNextBlockCalls_; }

    /**
     * Enable/disable debug printing of statistics on destruction.
     * Useful for profiling without modifying collector code.
     */
    void setDebugPrint(bool enable) { debugPrint_ = enable; }

private:
    // Configuration
    const BM25Similarity& similarity_;
    int minShouldMatch_;

    // Integer scaling for exact threshold comparisons (Phase 1)
    int scalingFactor_;            // Scaling factor to bring scores into [2^23, 2^24)
    int64_t minCompetitiveScore_;  // Minimum competitive score (scaled to integer)
    int64_t leadCost_;             // Cost of lead scorer (for cost-based filtering)

    // All scorers (owned)
    std::vector<std::unique_ptr<Scorer>> allScorers_;
    std::vector<ScorerWrapper> wrappers_;  // One wrapper per scorer

    // Three heaps
    ScorerWrapper* lead_;               // Linked list of scorers on current doc
    std::vector<ScorerWrapper*> head_;  // Heap of scorers ahead (ordered by doc ID)
    std::vector<ScorerWrapper*> tail_;  // Heap of scorers behind (ordered by max score)

    int doc_;          // Current doc ID
    float leadScore_;  // Sum of scores from lead scorers
    int freq_;         // Number of lead scorers (matching terms)

    int64_t tailMaxScore_;  // Sum of scaled max scores in tail (integer)
    int tailSize_;

    int64_t cost_;  // Total cost
    int upTo_;      // Upper bound for current block max scores

    // Instrumentation counters (diagnostic)
    mutable int docsScored_;            // Documents actually scored (advanced through)
    mutable int tailPromotions_;        // Tail scorers promoted to head
    mutable int maxScoreUpdates_;       // Times updateMaxScores was called
    mutable int matchingDocs_;          // Documents that matched constraints
    mutable int blockBoundaryHits_;     // Times upTo aligned with block boundary (Phase 2)
    mutable int blockBoundaryMisses_;   // Times upTo fell back to fixed window (Phase 2)
    mutable int blocksSkipped_;         // Blocks skipped by moveToNextBlock (Phase 3)
    mutable int moveToNextBlockCalls_;  // Times moveToNextBlock was called (Phase 3)
    bool debugPrint_;                   // Print statistics on destruction

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
     * Two-Phase Iterator Pattern:
     * Phase 1 (approximation): Return next candidate document from head.
     * This is fast and returns potential matches without verification.
     *
     * @param target Minimum doc ID to consider
     * @return Next candidate doc ID, or NO_MORE_DOCS if exhausted
     */
    int advanceApproximation(int target);

    /**
     * Two-Phase Iterator Pattern:
     * Phase 2 (matches): Check if current candidate satisfies constraints.
     * This verifies the candidate by:
     * - Checking if leadScore + tailMaxScore >= minCompetitiveScore
     * - Checking if freq + tailSize >= minShouldMatch
     * - Advancing tail scorers if needed to improve score
     *
     * @return true if doc matches, false if it should be skipped
     */
    bool doMatches();

    /**
     * Legacy method - check if current lead scorers satisfy minShouldMatch.
     * Used for simple validation.
     * Returns true if match, false if we should skip.
     */
    bool matches();

    /**
     * Update max scores for all scorers up to target.
     * Called when entering new block.
     */
    void updateMaxScores(int target);

    /**
     * Phase 3: Skip to next potentially competitive block.
     *
     * Advances upTo_ to skip blocks where sum(maxScores) < minCompetitiveScore.
     * This avoids scoring documents that cannot possibly be competitive.
     *
     * Based on Lucene WANDScorer.java lines 400-430.
     *
     * @param target Minimum doc ID to consider
     */
    void moveToNextBlock(int target);

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
