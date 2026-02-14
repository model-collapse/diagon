// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/search/MaxScoreBulkScorer.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>

#ifdef __AVX2__
#    include <immintrin.h>
#endif

namespace diagon {
namespace search {

// ==================== Constructor ====================

MaxScoreBulkScorer::MaxScoreBulkScorer(int maxDoc, std::vector<std::unique_ptr<Scorer>> scorers)
    : maxDoc_(maxDoc)
    , cost_(0) {
    ownedScorers_ = std::move(scorers);

    int n = static_cast<int>(ownedScorers_.size());
    allScorers_.resize(n);
    maxScoreSums_.resize(n, 0.0);
    scratch_.resize(n);
    nonEssentialScratch_.resize(n);

    for (int i = 0; i < n; i++) {
        allScorers_[i] = DisiWrapper(ownedScorers_[i].get());
        cost_ += allScorers_[i].cost;
    }

    essentialQueue_.resize(n);
    essentialQueueSize_ = 0;

    // Pre-fill window arrays with zeros
    windowMatches_.fill(0);
    windowScores_.fill(0.0f);

    // Reserve buffer capacity
    buffer_.docs.reserve(INNER_WINDOW_SIZE);
    buffer_.scores.reserve(INNER_WINDOW_SIZE);
}

// ==================== Main score() Method ====================

int MaxScoreBulkScorer::score(LeafCollector* collector, int min, int max) {
    // Pass scorable to collector so it can call setMinCompetitiveScore
    scorable_.minCompetitiveScore = 0.0f;
    collector->setScorer(&scorable_);

    int outerWindowMin = min;
    numOuterWindows_ = 0;
    numCandidates_ = 0;
    minWindowSize_ = 1;

    while (outerWindowMin < max) {
        int outerWindowMax = computeOuterWindowMax(outerWindowMin);
        outerWindowMax = std::min(outerWindowMax, max);

        // Convergence loop: recompute partition until window stabilizes
        while (true) {
            updateMaxWindowScores(outerWindowMin, outerWindowMax);

            if (!partitionScorers()) {
                // No matches possible in this window
                break;
            }

            // Check if window needs to shrink after partitioning
            int newMax = computeOuterWindowMax(outerWindowMin);
            newMax = std::min(newMax, max);
            if (newMax >= outerWindowMax) {
                break;  // Converged
            }
            outerWindowMax = newMax;
        }

        // Advance essential scorers to outerWindowMin
        if (essentialQueueSize_ > 0) {
            DisiWrapper* top = essentialQueueTop();

            while (top != nullptr && top->doc < outerWindowMin) {
                top->doc = top->scorer->advance(outerWindowMin);
                essentialQueueUpdateTop();
                top = essentialQueueTop();
            }

            // Score inner windows within this outer window
            while (top != nullptr && top->doc < outerWindowMax) {
                scoreInnerWindow(collector, outerWindowMax);
                top = essentialQueueTop();

                // Check if minCompetitiveScore improved enough to re-partition
                if (scorable_.minCompetitiveScore >= nextMinCompetitiveScore_) {
                    break;
                }
            }

            // Next outer window starts at top or outerWindowMax
            if (top != nullptr && top->doc < outerWindowMax) {
                outerWindowMin = top->doc;
            } else {
                outerWindowMin = outerWindowMax;
            }
        } else {
            outerWindowMin = outerWindowMax;
        }

        numOuterWindows_++;
    }

    return NO_MORE_DOCS;
}

// ==================== Outer Window Computation ====================

int MaxScoreBulkScorer::computeOuterWindowMax(int windowMin) {
    int windowMax = std::numeric_limits<int>::max();

    // Use impact block boundaries from essential scorers
    int firstLead = std::min(firstEssentialScorer_, static_cast<int>(allScorers_.size()) - 1);
    if (firstLead < 0)
        firstLead = 0;

    for (int i = firstLead; i < static_cast<int>(allScorers_.size()); i++) {
        DisiWrapper& w = allScorers_[i];
        int target = std::max(w.doc, windowMin);
        if (target >= NO_MORE_DOCS)
            continue;

        // advanceShallow returns the end of the current impact block
        int upTo = w.scorer->advanceShallow(target);
        // upTo is inclusive, so block boundary is upTo + 1
        if (upTo < std::numeric_limits<int>::max() - 1) {
            windowMax = std::min(windowMax, upTo + 1);
        }
    }

    // Adaptive minimum window size: target ~32 candidates per clause per window
    int64_t threshold = static_cast<int64_t>(numOuterWindows_ + 1) * 32L *
                        static_cast<int64_t>(allScorers_.size());
    if (numCandidates_ < threshold) {
        minWindowSize_ = std::min(minWindowSize_ << 1, INNER_WINDOW_SIZE);
    } else {
        minWindowSize_ = 1;
    }

    // Enforce minimum window size
    int64_t minWindowMax = static_cast<int64_t>(windowMin) + minWindowSize_;
    if (minWindowMax > std::numeric_limits<int>::max()) {
        minWindowMax = std::numeric_limits<int>::max();
    }
    windowMax = std::max(windowMax, static_cast<int>(minWindowMax));

    return windowMax;
}

// ==================== Max Score Update ====================

void MaxScoreBulkScorer::updateMaxWindowScores(int windowMin, int windowMax) {
    for (auto& w : allScorers_) {
        int target = std::max(w.doc, windowMin);
        if (target < NO_MORE_DOCS) {
            w.scorer->advanceShallow(target);
        }
        // getMaxScore(upTo) returns max score for docs in [current, upTo]
        // windowMax-1 because outer window is [min, max)
        w.maxWindowScore = w.scorer->getMaxScore(windowMax - 1);
        w.efficiencyRatio = static_cast<float>(static_cast<double>(w.maxWindowScore) /
                                               static_cast<double>(std::max(w.cost, int64_t(1))));
    }
}

// ==================== Partitioning ====================

bool MaxScoreBulkScorer::partitionScorers() {
    int n = static_cast<int>(allScorers_.size());

    // Fast path: when minCompetitiveScore is 0 (no results yet), all scorers
    // are essential — skip sorting entirely.
    if (scorable_.minCompetitiveScore <= 0.0f) {
        firstEssentialScorer_ = 0;
        nextMinCompetitiveScore_ = std::numeric_limits<float>::max();
        firstRequiredScorer_ = n;

        essentialQueueClear();
        for (int i = 0; i < n; i++) {
            essentialQueuePush(&allScorers_[i]);
        }
        return true;
    }

    // Build scratch pointers
    for (int i = 0; i < n; i++) {
        scratch_[i] = &allScorers_[i];
    }

    // Sort by pre-computed efficiency ratio (ascending)
    // Low-ratio scorers are cheap to evaluate, high-ratio are expensive
    std::sort(scratch_.begin(), scratch_.begin() + n,
              [](const DisiWrapper* a, const DisiWrapper* b) {
                  return a->efficiencyRatio < b->efficiencyRatio;
              });

    // Greedy accumulation: non-essential scorers are those whose cumulative
    // max score is below minCompetitiveScore.
    // Essential scorers are those needed to potentially reach minCompetitiveScore.
    double maxScoreSum = 0.0;
    firstEssentialScorer_ = 0;
    nextMinCompetitiveScore_ = std::numeric_limits<float>::max();

    // Reuse pre-allocated scratch (no heap allocation per call)
    int nonEssentialCount = 0;

    for (int i = 0; i < n; i++) {
        DisiWrapper* w = scratch_[i];
        double newSum = maxScoreSum + w->maxWindowScore;
        float sumFloat = static_cast<float>(newSum);

        if (sumFloat < scorable_.minCompetitiveScore) {
            // This scorer is non-essential: its cumulative max score
            // isn't enough to reach minCompetitiveScore
            maxScoreSum = newSum;
            allScorers_[firstEssentialScorer_] = *w;
            maxScoreSums_[firstEssentialScorer_] = maxScoreSum;
            firstEssentialScorer_++;
        } else {
            // This scorer is essential
            nonEssentialScratch_[nonEssentialCount++] = w;
            nextMinCompetitiveScore_ = std::min(nextMinCompetitiveScore_, sumFloat);
        }
    }

    // Place essential scorers at the end of allScorers_
    int essentialIdx = firstEssentialScorer_;
    for (int i = 0; i < nonEssentialCount; i++) {
        allScorers_[essentialIdx++] = *nonEssentialScratch_[i];
    }

    // Check if any essential scorers exist
    if (firstEssentialScorer_ == n) {
        // No essential scorers = no matches possible
        essentialQueueClear();
        return false;
    }

    // Determine required scorers for single-essential-scorer optimization
    firstRequiredScorer_ = n;
    if (firstEssentialScorer_ == n - 1) {
        // Single essential scorer case: determine which non-essential scorers
        // become required (must match for doc to be competitive)
        firstRequiredScorer_ = n - 1;
        double maxRequiredScore = allScorers_[firstEssentialScorer_].maxWindowScore;

        while (firstRequiredScorer_ > 0) {
            double maxWithoutPrev = maxRequiredScore;
            if (firstRequiredScorer_ > 1) {
                maxWithoutPrev += maxScoreSums_[firstRequiredScorer_ - 2];
            }
            if (static_cast<float>(maxWithoutPrev) >= scorable_.minCompetitiveScore) {
                break;
            }
            firstRequiredScorer_--;
            maxRequiredScore += allScorers_[firstRequiredScorer_].maxWindowScore;
        }
    }

    // Rebuild essential queue with essential scorers
    essentialQueueClear();
    for (int i = firstEssentialScorer_; i < n; i++) {
        essentialQueuePush(&allScorers_[i]);
    }

    return true;
}

// ==================== Inner Window Dispatch ====================

void MaxScoreBulkScorer::scoreInnerWindow(LeafCollector* collector, int max) {
    DisiWrapper* top = essentialQueueTop();
    if (top == nullptr)
        return;

    DisiWrapper* top2 = essentialQueueTop2();

    if (top2 == nullptr) {
        // Path 1: Single essential scorer
        scoreInnerWindowSingleEssential(collector, max);
    } else if (top2->doc - INNER_WINDOW_SIZE / 2 >= top->doc) {
        // Path 2: Gap detected - single scorer dominates first half
        scoreInnerWindowSingleEssential(collector, std::min(max, top2->doc));
    } else {
        // Path 3: Multiple essential scorers
        scoreInnerWindowMultipleEssentials(collector, max);
    }
}

// ==================== Path 1/2: Single Essential Scorer ====================

void MaxScoreBulkScorer::scoreInnerWindowSingleEssential(LeafCollector* collector, int upTo) {
    DisiWrapper* top = essentialQueueTop();
    if (top == nullptr)
        return;

    buffer_.clear();
    Scorer* scorer = top->scorer;

    // Batch scoring: process docs in chunks of BATCH_SIZE
    while (true) {
        int count = scorer->scoreBatch(upTo, batchDocs_, batchScores_, BATCH_SIZE);
        if (count == 0)
            break;

        for (int j = 0; j < count; j++) {
            buffer_.add(batchDocs_[j], batchScores_[j]);
        }
    }

    // Update wrapper state
    top->doc = scorer->docID();
    essentialQueueUpdateTop();

    // Score non-essential clauses and collect
    scoreNonEssentialClauses(collector, firstEssentialScorer_);
}

// ==================== Path 3: Multiple Essential Scorers ====================

void MaxScoreBulkScorer::scoreInnerWindowMultipleEssentials(LeafCollector* collector, int max) {
    DisiWrapper* top = essentialQueueTop();
    if (top == nullptr)
        return;

    int innerWindowMin = top->doc;
    int innerWindowMax = std::min(max, innerWindowMin + INNER_WINDOW_SIZE);
    int innerWindowSize = innerWindowMax - innerWindowMin;

    // Collect all essential scorer matches into bitset + score array
    while (top != nullptr && top->doc < innerWindowMax) {
        Scorer* scorer = top->scorer;

        // Batch scoring: process docs in chunks of BATCH_SIZE
        while (true) {
            int count = scorer->scoreBatch(innerWindowMax, batchDocs_, batchScores_, BATCH_SIZE);
            if (count == 0)
                break;

            for (int j = 0; j < count; j++) {
                int i = batchDocs_[j] - innerWindowMin;
                windowSetBit(i);
                windowScores_[i] += batchScores_[j];
            }
        }

        top->doc = scorer->docID();
        essentialQueueUpdateTop();
        top = essentialQueueTop();
    }

    // Extract matched docs from bitset into buffer
    buffer_.clear();
    for (int bit = windowNextSetBit(0, innerWindowSize); bit < innerWindowSize;
         bit = windowNextSetBit(bit + 1, innerWindowSize)) {
        buffer_.add(innerWindowMin + bit, windowScores_[bit]);
        windowScores_[bit] = 0.0f;  // Reset for next window
    }

    // Clear bitset
    windowClearAll(innerWindowSize);

    // Score non-essential clauses and collect
    scoreNonEssentialClauses(collector, firstEssentialScorer_);
}

// ==================== Non-Essential Scoring + Collection ====================

void MaxScoreBulkScorer::scoreNonEssentialClauses(LeafCollector* collector,
                                                  int numNonEssentialClauses) {
    numCandidates_ += buffer_.size;

    // Process non-essential scorers in reverse order (highest max score first)
    for (int i = numNonEssentialClauses - 1; i >= 0; --i) {
        DisiWrapper& w = allScorers_[i];

        if (scorable_.minCompetitiveScore > 0.0f) {
            // Filter out docs that cannot be competitive even with remaining scorers
            filterCompetitiveHits(maxScoreSums_[i]);

            if (buffer_.size == 0)
                return;  // All filtered out
        }

        if (i >= firstRequiredScorer_) {
            // Required clause: only keep docs that match
            applyRequiredClause(w);
        } else {
            // Optional clause: add scores if match, keep all docs
            applyOptionalClause(w);
        }

        w.doc = w.scorer->docID();
    }

    // Collect all remaining docs
    for (int i = 0; i < buffer_.size; i++) {
        scorable_.score_ = buffer_.scores[i];
        scorable_.docID_ = buffer_.docs[i];
        collector->collect(buffer_.docs[i]);
    }
}

void MaxScoreBulkScorer::filterCompetitiveHits(float maxRemainingScore) {
    float minRequired = scorable_.minCompetitiveScore - maxRemainingScore;
    if (minRequired <= 0.0f)
        return;

    int newSize = 0;
    int i = 0;

#ifdef __AVX2__
    // AVX2: process 8 floats at a time
    __m256 vMin = _mm256_set1_ps(minRequired);
    for (; i + 8 <= buffer_.size; i += 8) {
        __m256 vScores = _mm256_loadu_ps(&buffer_.scores[i]);
        __m256 cmp = _mm256_cmp_ps(vScores, vMin, _CMP_GE_OQ);
        int mask = _mm256_movemask_ps(cmp);

        // Compact surviving elements
        while (mask != 0) {
            int bit = __builtin_ctz(mask);
            buffer_.docs[newSize] = buffer_.docs[i + bit];
            buffer_.scores[newSize] = buffer_.scores[i + bit];
            newSize++;
            mask &= mask - 1;  // Clear lowest set bit
        }
    }
#endif

    // Scalar tail
    for (; i < buffer_.size; i++) {
        if (buffer_.scores[i] >= minRequired) {
            buffer_.docs[newSize] = buffer_.docs[i];
            buffer_.scores[newSize] = buffer_.scores[i];
            newSize++;
        }
    }
    buffer_.size = newSize;
}

void MaxScoreBulkScorer::applyOptionalClause(DisiWrapper& w) {
    Scorer* scorer = w.scorer;
    int curDoc = w.doc;

    for (int i = 0; i < buffer_.size; i++) {
        int targetDoc = buffer_.docs[i];
        if (curDoc < targetDoc) {
            curDoc = scorer->advance(targetDoc);
        }
        if (curDoc == targetDoc) {
            buffer_.scores[i] += scorer->score();
        }
    }
    w.doc = curDoc;
}

void MaxScoreBulkScorer::applyRequiredClause(DisiWrapper& w) {
    Scorer* scorer = w.scorer;
    int curDoc = w.doc;

    int newSize = 0;
    for (int i = 0; i < buffer_.size; i++) {
        int targetDoc = buffer_.docs[i];
        if (curDoc < targetDoc) {
            curDoc = scorer->advance(targetDoc);
        }
        if (curDoc == targetDoc) {
            buffer_.docs[newSize] = targetDoc;
            buffer_.scores[newSize] = buffer_.scores[i] + scorer->score();
            newSize++;
        }
    }
    buffer_.size = newSize;
    w.doc = curDoc;
}

// ==================== Priority Queue Operations ====================
// Min-heap by doc ID for essential scorers

void MaxScoreBulkScorer::essentialQueueClear() {
    essentialQueueSize_ = 0;
}

void MaxScoreBulkScorer::essentialQueuePush(DisiWrapper* w) {
    essentialQueue_[essentialQueueSize_] = w;
    essentialQueueSiftUp(essentialQueueSize_);
    essentialQueueSize_++;
}

MaxScoreBulkScorer::DisiWrapper* MaxScoreBulkScorer::essentialQueueTop2() {
    // Second-smallest element in min-heap is one of the children of root
    if (essentialQueueSize_ < 2)
        return nullptr;
    if (essentialQueueSize_ == 2)
        return essentialQueue_[1];

    // Return child with smaller doc
    if (essentialQueue_[1]->doc <= essentialQueue_[2]->doc) {
        return essentialQueue_[1];
    }
    return essentialQueue_[2];
}

void MaxScoreBulkScorer::essentialQueueSiftDown(int i) {
    DisiWrapper* node = essentialQueue_[i];
    int half = essentialQueueSize_ / 2;

    while (i < half) {
        int child = 2 * i + 1;
        DisiWrapper* childNode = essentialQueue_[child];

        int right = child + 1;
        if (right < essentialQueueSize_ && essentialQueue_[right]->doc < childNode->doc) {
            child = right;
            childNode = essentialQueue_[right];
        }

        if (node->doc <= childNode->doc)
            break;

        essentialQueue_[i] = childNode;
        i = child;
    }

    essentialQueue_[i] = node;
}

void MaxScoreBulkScorer::essentialQueueSiftUp(int i) {
    DisiWrapper* node = essentialQueue_[i];

    while (i > 0) {
        int parent = (i - 1) / 2;
        if (essentialQueue_[parent]->doc <= node->doc)
            break;

        essentialQueue_[i] = essentialQueue_[parent];
        i = parent;
    }

    essentialQueue_[i] = node;
}

// Bitset helpers are inlined in MaxScoreBulkScorer.h

}  // namespace search
}  // namespace diagon
