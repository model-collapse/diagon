// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/search/WANDScorer.h"
#include "diagon/codecs/lucene104/Lucene104PostingsReader.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace diagon {
namespace search {

WANDScorer::WANDScorer(std::vector<std::unique_ptr<Scorer>>& scorers,
                       const BM25Similarity& similarity, int minShouldMatch)
    : similarity_(similarity)
    , minShouldMatch_(minShouldMatch)
    , minCompetitiveScore_(0.0f)
    , lead_(nullptr)
    , doc_(-1)
    , leadScore_(0.0f)
    , freq_(0)
    , tailMaxScore_(0.0f)
    , tailSize_(0)
    , cost_(0)
    , upTo_(-1) {

    if (minShouldMatch >= static_cast<int>(scorers.size())) {
        throw std::invalid_argument("minShouldMatch should be < number of scorers");
    }

    // Take ownership of scorers
    allScorers_ = std::move(scorers);

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

    // Reserve space for heaps
    head_.reserve(allScorers_.size());
    tail_.reserve(allScorers_.size());
}

WANDScorer::~WANDScorer() = default;

int WANDScorer::nextDoc() {
    return advance(doc_ + 1);
}

int WANDScorer::advance(int target) {
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

    // Set doc to head top
    doc_ = headTop->doc;

    // Move scorers on doc from head to lead
    moveToNextCandidate();

    // Check if this doc matches constraints
    while (!matches()) {
        // Can't possibly match
        if (leadScore_ + tailMaxScore_ < minCompetitiveScore_ ||
            freq_ + tailSize_ < minShouldMatch_) {
            // Move to next doc
            headTop = advanceHead(doc_ + 1);
            if (headTop == nullptr) {
                return doc_ = index::PostingsEnum::NO_MORE_DOCS;
            }
            doc_ = headTop->doc;
            moveToNextCandidate();
        } else {
            // Advance a tail scorer
            advanceTail();
        }
    }

    return doc_;
}

float WANDScorer::score() const {
    // Score is already computed in leadScore_
    return leadScore_;
}

void WANDScorer::setMinCompetitiveScore(float minScore) {
    minCompetitiveScore_ = minScore;
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
            std::push_heap(head_.begin(), head_.end(),
                          [](ScorerWrapper* a, ScorerWrapper* b) {
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
                     [](ScorerWrapper* a, ScorerWrapper* b) {
                         return a->doc > b->doc;
                     });
        ScorerWrapper* wrapper = head_.back();
        head_.pop_back();

        // Try to insert into tail
        ScorerWrapper* evicted = insertTailWithOverFlow(wrapper);
        if (evicted != nullptr) {
            // Advance evicted and re-insert into head
            evicted->doc = evicted->scorer->advance(target);
            head_.push_back(evicted);
            std::push_heap(head_.begin(), head_.end(),
                          [](ScorerWrapper* a, ScorerWrapper* b) {
                              return a->doc > b->doc;
                          });
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
                      [](ScorerWrapper* a, ScorerWrapper* b) {
                          return a->doc > b->doc;
                      });
    }
}

void WANDScorer::moveToNextCandidate() {
    // Pop all scorers from head that match current doc
    while (!head_.empty() && head_[0]->doc == doc_) {
        std::pop_heap(head_.begin(), head_.end(),
                     [](ScorerWrapper* a, ScorerWrapper* b) {
                         return a->doc > b->doc;
                     });
        ScorerWrapper* wrapper = head_.back();
        head_.pop_back();

        addLead(wrapper);
    }
}

bool WANDScorer::matches() {
    // Check minShouldMatch constraint
    if (freq_ < minShouldMatch_) {
        return false;
    }

    // Check score constraint
    if (leadScore_ < minCompetitiveScore_) {
        return false;
    }

    return true;
}

void WANDScorer::updateMaxScores(int target) {
    upTo_ = index::PostingsEnum::NO_MORE_DOCS;

    // Find minimum block boundary from head scorers
    for (ScorerWrapper* wrapper : head_) {
        if (wrapper->doc <= upTo_ && wrapper->cost <= 1000000) {  // Cost threshold
            // TODO: Call advanceShallow() on impacts enum
            // For now, use conservative estimate
            wrapper->maxScore = 100.0f;  // Placeholder
        }
    }

    // Update tail max scores
    tailMaxScore_ = 0.0f;
    for (int i = 0; i < tailSize_; ++i) {
        // TODO: Update max score from impacts
        tail_[i]->maxScore = 100.0f;  // Placeholder
        tailMaxScore_ += tail_[i]->maxScore;
    }

    // Remove tail scorers that can match on their own
    while (tailSize_ > 0 && tailMaxScore_ >= minCompetitiveScore_) {
        ScorerWrapper* wrapper = popTail();
        wrapper->doc = wrapper->scorer->advance(target);
        head_.push_back(wrapper);
        std::push_heap(head_.begin(), head_.end(),
                      [](ScorerWrapper* a, ScorerWrapper* b) {
                          return a->doc > b->doc;
                      });
    }
}

WANDScorer::ScorerWrapper* WANDScorer::insertTailWithOverFlow(ScorerWrapper* wrapper) {
    // If tail has space, just insert
    if (tailSize_ < static_cast<int>(allScorers_.size()) - 1) {
        tail_.push_back(wrapper);
        tailSize_++;
        tailMaxScore_ += wrapper->maxScore;

        // Heapify upward
        upHeapMaxScore(tailSize_ - 1);

        return nullptr;
    }

    // Tail is full, check if this wrapper has higher max score than min
    if (wrapper->maxScore <= tail_[0]->maxScore) {
        return wrapper;  // This wrapper should be evicted
    }

    // Evict min max score from tail
    ScorerWrapper* evicted = tail_[0];
    tail_[0] = wrapper;
    tailMaxScore_ = tailMaxScore_ - evicted->maxScore + wrapper->maxScore;
    downHeapMaxScore(0);

    return evicted;
}

WANDScorer::ScorerWrapper* WANDScorer::popTail() {
    if (tailSize_ == 0) {
        return nullptr;
    }

    ScorerWrapper* top = tail_[0];
    tailMaxScore_ -= top->maxScore;
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
    float maxScore = wrapper->maxScore;

    while (index > 0) {
        int parent = (index - 1) / 2;
        if (tail_[parent]->maxScore >= maxScore) {
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
    float maxScore = wrapper->maxScore;

    int half = tailSize_ / 2;
    while (index < half) {
        int child = 2 * index + 1;

        // Choose larger child
        if (child + 1 < tailSize_ && tail_[child + 1]->maxScore > tail_[child]->maxScore) {
            child++;
        }

        if (maxScore >= tail_[child]->maxScore) {
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
