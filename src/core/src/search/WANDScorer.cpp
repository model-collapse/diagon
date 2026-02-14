// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/search/WANDScorer.h"

#include "diagon/codecs/lucene104/Lucene104PostingsReader.h"
#include "diagon/util/ScalingUtils.h"
#include "diagon/util/SearchProfiler.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace diagon {
namespace search {

WANDScorer::WANDScorer(std::vector<std::unique_ptr<Scorer>>& scorers,
                       const BM25Similarity& similarity, int minShouldMatch)
    : similarity_(similarity)
    , minShouldMatch_(minShouldMatch)
    , scalingFactor_(0)        // Will be computed below
    , minCompetitiveScore_(0)  // Scaled integer, starts at 0
    , leadCost_(0)
    , lead_(nullptr)
    , doc_(-1)
    , leadScore_(0.0f)
    , freq_(0)
    , tailMaxScore_(0)  // Scaled integer
    , tailSize_(0)
    , cost_(0)
    , upTo_(-1)
    , docsScored_(0)  // Instrumentation
    , tailPromotions_(0)
    , maxScoreUpdates_(0)
    , matchingDocs_(0)
    , blockBoundaryHits_(0)     // Phase 2 instrumentation
    , blockBoundaryMisses_(0)   // Phase 2 instrumentation
    , blocksSkipped_(0)         // Phase 3 instrumentation
    , moveToNextBlockCalls_(0)  // Phase 3 instrumentation
    , debugPrint_(false) {
    if (minShouldMatch >= static_cast<int>(scorers.size())) {
        throw std::invalid_argument("minShouldMatch should be < number of scorers");
    }

    // Take ownership of scorers
    allScorers_ = std::move(scorers);

    // Compute scaling factor from first scorer's max score
    // This determines the precision of integer comparisons
    if (!allScorers_.empty()) {
        float maxPossibleScore = allScorers_[0]->getMaxScore(index::PostingsEnum::NO_MORE_DOCS);
        scalingFactor_ = util::ScalingUtils::scalingFactor(maxPossibleScore);
    }

    // Create wrappers for each scorer
    wrappers_.reserve(allScorers_.size());
    for (auto& scorer : allScorers_) {
        // Get cost from scorer (Scorer extends DocIdSetIterator)
        int64_t scorerCost = scorer->cost();

        // Create wrapper
        wrappers_.emplace_back(scorer.get(), scorerCost);

        // Add to lead as unpositioned (-1)
        ScorerWrapper* wrapper = &wrappers_.back();
        wrapper->doc = -1;
        wrapper->next = lead_;
        lead_ = wrapper;
        freq_++;

        cost_ += scorerCost;
    }

    // Initialize leadCost_ (will be updated during iteration)
    leadCost_ = cost_;

    // Reserve space for heaps
    head_.reserve(allScorers_.size());
    tail_.reserve(allScorers_.size());
}

WANDScorer::~WANDScorer() {
    if (debugPrint_) {
        std::cerr << "[WAND Stats] "
                  << "Docs scored: " << docsScored_ << ", Matching: " << matchingDocs_
                  << ", Tail promotions: " << tailPromotions_
                  << ", Max score updates: " << maxScoreUpdates_
                  << ", moveToNextBlock calls: " << moveToNextBlockCalls_
                  << ", Blocks skipped: " << blocksSkipped_ << std::endl;
    }
}

int WANDScorer::nextDoc() {
    return advance(doc_ + 1);
}

int WANDScorer::advanceApproximation(int target) {
    // Phase 1: Return next candidate without checking if it matches

    // Move lead scorers back to tail
    pushBackLeads(target);

    // Advance head scorers to target
    ScorerWrapper* headTop = advanceHead(target);

    // Update max scores if we moved to a new block
    if (headTop == nullptr || headTop->doc > upTo_) {
        updateMaxScores(target);
        headTop = (head_.empty()) ? nullptr : head_[0];
    }

    // No more docs
    if (headTop == nullptr) {
        return doc_ = index::PostingsEnum::NO_MORE_DOCS;
    }

    // Set doc to head top (candidate)
    doc_ = headTop->doc;
    return doc_;
}

bool WANDScorer::doMatches() {
    // Phase 2: Check if current candidate satisfies constraints

    // Move scorers on doc from head to lead
    moveToNextCandidate();

    // Try to satisfy constraints by advancing tail scorers
    while (true) {
        // Check if document cannot meet minShouldMatch constraint (truly doesn't match)
        if (freq_ + tailSize_ < minShouldMatch_) {
            return false;  // Can never meet minShouldMatch, not a match
        }

        // Check if we meet minShouldMatch
        if (matches()) {
            // This document matches the query!
            matchingDocs_++;  // Count for totalHits (ALL matches)

            // Compute max possible score to check competitiveness
            int64_t headMaxScore = 0;
            for (ScorerWrapper* wrapper : head_) {
                headMaxScore += wrapper->scaledMaxScore;
            }
            int64_t scaledLeadScore = util::ScalingUtils::scaleMaxScore(leadScore_, scalingFactor_);
            int64_t maxPossible = scaledLeadScore + headMaxScore + tailMaxScore_;

            // Now check if it's competitive for top-K
            if (maxPossible < minCompetitiveScore_) {
                // Document matches but isn't competitive, skip it
                return false;
            }

            // Document matches AND is competitive
            return true;
        }

        // We don't meet minShouldMatch yet
        // Try advancing a tail scorer to get more term matches
        if (tailSize_ > 0) {
            advanceTail();
        } else {
            // No more tail scorers to try, but we don't meet minShouldMatch
            return false;
        }
    }
}

int WANDScorer::advance(int target) {
    // Two-phase iteration: repeatedly call approximation then matches until we find a match
    while (true) {
        // Phase 1: Get next candidate
        int candidate = advanceApproximation(target);

        if (candidate == index::PostingsEnum::NO_MORE_DOCS) {
            return candidate;
        }

        // Instrumentation: Count every candidate we examine
        docsScored_++;

        // Phase 2: Check if candidate matches
        bool isMatch = doMatches();

        // Count ALL matching documents for totalHits, even if not competitive
        // This is done inside doMatches() when matches() returns true

        if (isMatch) {
            return candidate;
        }

        // Candidate didn't match, try next doc
        target = candidate + 1;
    }
}

float WANDScorer::score() const {
    // Score is already computed in leadScore_
    return leadScore_;
}

void WANDScorer::setMinCompetitiveScore(float minScore) {
    // Phase 1: Scale min score to integer (round DOWN to be conservative)
    minCompetitiveScore_ = util::ScalingUtils::scaleMinScore(minScore, scalingFactor_);
}

void WANDScorer::addLead(ScorerWrapper* wrapper) {
    wrapper->next = lead_;
    lead_ = wrapper;
    freq_++;

    // Compute score for this doc
    float termScore = wrapper->scorer->score();
    leadScore_ += termScore;
}

void WANDScorer::pushBackLeads(int target) {
    while (lead_ != nullptr) {
        ScorerWrapper* wrapper = lead_;
        lead_ = wrapper->next;
        wrapper->next = nullptr;
        freq_--;

        // Insert into tail or advance to head
        ScorerWrapper* evicted = insertTailWithOverFlow(wrapper);
        if (evicted != nullptr) {
            // Advance evicted scorer and add to head
            evicted->doc = evicted->scorer->advance(target);

            // Insert into head heap
            head_.push_back(evicted);
            std::push_heap(head_.begin(), head_.end(), [](ScorerWrapper* a, ScorerWrapper* b) {
                return a->doc > b->doc;  // Min heap by doc ID
            });
        }
    }

    leadScore_ = 0.0f;
}

WANDScorer::ScorerWrapper* WANDScorer::advanceHead(int target) {
    while (!head_.empty() && head_[0]->doc < target) {
        // Pop from head
        std::pop_heap(head_.begin(), head_.end(),
                      [](ScorerWrapper* a, ScorerWrapper* b) { return a->doc > b->doc; });
        ScorerWrapper* wrapper = head_.back();
        head_.pop_back();

        // Try to insert into tail
        ScorerWrapper* evicted = insertTailWithOverFlow(wrapper);
        if (evicted != nullptr) {
            // Advance evicted and re-insert into head
            evicted->doc = evicted->scorer->advance(target);
            head_.push_back(evicted);
            std::push_heap(head_.begin(), head_.end(),
                           [](ScorerWrapper* a, ScorerWrapper* b) { return a->doc > b->doc; });
        }
    }

    return head_.empty() ? nullptr : head_[0];
}

void WANDScorer::advanceTail() {
    if (tailSize_ == 0) {
        return;
    }

    // Pop wrapper with highest max score
    ScorerWrapper* wrapper = popTail();

    // Advance to current doc
    wrapper->doc = wrapper->scorer->advance(doc_);

    if (wrapper->doc == doc_) {
        // Matches current doc, add to lead
        addLead(wrapper);
    } else {
        // Beyond current doc, add to head
        head_.push_back(wrapper);
        std::push_heap(head_.begin(), head_.end(),
                       [](ScorerWrapper* a, ScorerWrapper* b) { return a->doc > b->doc; });
    }
}

void WANDScorer::moveToNextCandidate() {
    // Pop all scorers from head that match current doc
    while (!head_.empty() && head_[0]->doc == doc_) {
        std::pop_heap(head_.begin(), head_.end(),
                      [](ScorerWrapper* a, ScorerWrapper* b) { return a->doc > b->doc; });
        ScorerWrapper* wrapper = head_.back();
        head_.pop_back();

        addLead(wrapper);
    }
}

bool WANDScorer::matches() {
    // Only check minShouldMatch constraint
    // The collector will filter by score for top-K
    if (freq_ < minShouldMatch_) {
        return false;
    }

    return true;
}

void WANDScorer::moveToNextBlock(int target) {
    moveToNextBlockCalls_++;  // Instrumentation

    // Phase 3: Skip blocks that cannot produce competitive scores
    //
    // Based on Lucene WANDScorer.java lines 400-430:
    // "We need to find the next block where the sum of max scores
    // could potentially produce a competitive hit."
    //
    // Algorithm:
    // 1. Compute max scores for current window [target, upTo_]
    // 2. Check: sum(head maxScores) + sum(tail maxScores) >= minCompetitiveScore?
    // 3. If YES: found competitive block, return
    // 4. If NO: skip this block (advance upTo_), repeat from step 1
    //
    // This reduces documents scored by skipping entire non-competitive blocks.

    while (upTo_ < index::PostingsEnum::NO_MORE_DOCS) {
        // Update max scores for all scorers in range [target, upTo_]
        // (This is a subset of what updateMaxScores() does)

        // Compute head max scores
        int64_t headMaxScore = 0;
        for (ScorerWrapper* wrapper : head_) {
            wrapper->scorer->advanceShallow(target);
            float maxScore = wrapper->scorer->getMaxScore(upTo_);
            int64_t scaledMaxScore = util::ScalingUtils::scaleMaxScore(maxScore, scalingFactor_);
            headMaxScore += scaledMaxScore;
        }

        // Compute tail max scores
        int64_t tailMaxScore = 0;
        for (int i = 0; i < tailSize_; ++i) {
            tail_[i]->scorer->advanceShallow(target);
            float maxScore = tail_[i]->scorer->getMaxScore(upTo_);
            int64_t scaledMaxScore = util::ScalingUtils::scaleMaxScore(maxScore, scalingFactor_);
            tailMaxScore += scaledMaxScore;
        }

        // Check if this block can produce competitive scores
        int64_t totalMaxScore = headMaxScore + tailMaxScore;
        if (totalMaxScore >= minCompetitiveScore_) {
            // Found a potentially competitive block
            return;
        }

        // This block cannot produce competitive scores, skip it
        blocksSkipped_++;  // Instrumentation

        // Advance to next block (128 docs ahead, or NO_MORE_DOCS)
        if (upTo_ >= index::PostingsEnum::NO_MORE_DOCS - 128) {
            upTo_ = index::PostingsEnum::NO_MORE_DOCS;
            return;
        }

        target = upTo_ + 1;    // Start search from next doc
        upTo_ = target + 128;  // Next window
    }
}

void WANDScorer::updateMaxScores(int target) {
    // Instrumentation: Count max score updates
    maxScoreUpdates_++;

    // Phase 1: Fixed 128-doc window
    upTo_ = (target < index::PostingsEnum::NO_MORE_DOCS - 128) ? target + 128
                                                               : index::PostingsEnum::NO_MORE_DOCS;

    // Phase 3: Skip non-competitive blocks
    // This may advance upTo_ to find a block where sum(maxScores) >= minCompetitiveScore
    if (minCompetitiveScore_ > 0) {
        moveToNextBlock(target);
    }

    // Update max scores for head scorers
    for (ScorerWrapper* wrapper : head_) {
        // Call advanceShallow to prepare for getMaxScore
        wrapper->scorer->advanceShallow(target);
        // Get maximum possible score in range [target, upTo_]
        float maxScore = wrapper->scorer->getMaxScore(upTo_);
        // Phase 1: Scale to integer (round UP to avoid missing matches)
        wrapper->scaledMaxScore = util::ScalingUtils::scaleMaxScore(maxScore, scalingFactor_);
    }

    // Update tail max scores
    tailMaxScore_ = 0;  // Scaled integer
    for (int i = 0; i < tailSize_; ++i) {
        // Call advanceShallow to prepare for getMaxScore
        tail_[i]->scorer->advanceShallow(target);
        // Get maximum possible score in range [target, upTo_]
        float maxScore = tail_[i]->scorer->getMaxScore(upTo_);
        // Phase 1: Scale to integer (round UP to avoid missing matches)
        tail_[i]->scaledMaxScore = util::ScalingUtils::scaleMaxScore(maxScore, scalingFactor_);
        tailMaxScore_ += tail_[i]->scaledMaxScore;  // Integer addition
    }

    // Promote tail scorers when their combined max scores could produce a competitive match
    //
    // Lucene's logic (line 481): "We need to make sure that entries in 'tail' alone cannot match
    // a competitive hit."
    //
    // If tailMaxScore >= minCompetitiveScore, then tail scorers alone could match without
    // any head scorers, so we must advance them to check for actual matches.
    //
    // This is sum-based promotion: if SUM(tail max scores) >= threshold, promote highest scorer.
    // Phase 1: Integer comparison (exact, no precision errors)
    while (tailSize_ > 0 && tailMaxScore_ >= minCompetitiveScore_) {
        tailPromotions_++;  // Instrumentation: Count tail promotions
        ScorerWrapper* wrapper = popTail();
        wrapper->doc = wrapper->scorer->advance(target);
        head_.push_back(wrapper);
        std::push_heap(head_.begin(), head_.end(),
                       [](ScorerWrapper* a, ScorerWrapper* b) { return a->doc > b->doc; });
    }
}

WANDScorer::ScorerWrapper* WANDScorer::insertTailWithOverFlow(ScorerWrapper* wrapper) {
    // If tail has space, just insert
    if (tailSize_ < static_cast<int>(allScorers_.size()) - 1) {
        tail_.push_back(wrapper);
        tailSize_++;
        tailMaxScore_ += wrapper->scaledMaxScore;  // Phase 1: Use scaled integer

        // Heapify upward
        upHeapMaxScore(tailSize_ - 1);

        return nullptr;
    }

    // Tail is full, check if this wrapper has higher max score than min
    // Phase 1: Use scaled integers
    if (wrapper->scaledMaxScore <= tail_[0]->scaledMaxScore) {
        return wrapper;  // This wrapper should be evicted
    }

    // Evict min max score from tail
    ScorerWrapper* evicted = tail_[0];
    tail_[0] = wrapper;
    tailMaxScore_ = tailMaxScore_ - evicted->scaledMaxScore +
                    wrapper->scaledMaxScore;  // Integer arithmetic
    downHeapMaxScore(0);

    return evicted;
}

WANDScorer::ScorerWrapper* WANDScorer::popTail() {
    if (tailSize_ == 0) {
        return nullptr;
    }

    ScorerWrapper* top = tail_[0];
    tailMaxScore_ -= top->scaledMaxScore;  // Phase 1: Use scaled integer
    tailSize_--;

    if (tailSize_ > 0) {
        tail_[0] = tail_[tailSize_];
        tail_.pop_back();
        downHeapMaxScore(0);
    } else {
        tail_.pop_back();
    }

    return top;
}

void WANDScorer::upHeapMaxScore(int index) {
    ScorerWrapper* wrapper = tail_[index];
    int64_t scaledMaxScore = wrapper->scaledMaxScore;  // Phase 1: Use scaled integer

    while (index > 0) {
        int parent = (index - 1) / 2;
        if (tail_[parent]->scaledMaxScore >= scaledMaxScore) {  // Phase 1: Use scaled integer
            break;
        }
        tail_[index] = tail_[parent];
        index = parent;
    }

    tail_[index] = wrapper;
}

void WANDScorer::downHeapMaxScore(int index) {
    if (tailSize_ == 0) {
        return;
    }

    ScorerWrapper* wrapper = tail_[index];
    int64_t scaledMaxScore = wrapper->scaledMaxScore;  // Phase 1: Use scaled integer

    int half = tailSize_ / 2;
    while (index < half) {
        int child = 2 * index + 1;

        // Choose larger child
        // Phase 1: Use scaled integers
        if (child + 1 < tailSize_ &&
            tail_[child + 1]->scaledMaxScore > tail_[child]->scaledMaxScore) {
            child++;
        }

        if (scaledMaxScore >= tail_[child]->scaledMaxScore) {
            break;
        }

        tail_[index] = tail_[child];
        index = child;
    }

    tail_[index] = wrapper;
}

void WANDScorer::upHeapDocID(int index) {
    // Not needed for now (using std::push_heap)
}

void WANDScorer::downHeapDocID(int index) {
    // Not needed for now (using std::pop_heap)
}

}  // namespace search
}  // namespace diagon
