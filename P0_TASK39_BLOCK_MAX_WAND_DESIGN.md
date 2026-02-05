# Task #39: Block-Max WAND Implementation Design

**Status**: In Progress
**Priority**: P0 (Highest)
**Expected Impact**: 5-10x improvement (43.8µs → 4-9µs)
**Target**: 129µs → 13-26µs overall query latency

---

## Overview

Implement Block-Max Weak-AND (WAND) algorithm to enable early termination in top-k queries. This is the **highest priority optimization** as it addresses the largest bottleneck (TopK collection: 43.8µs, 33.96% CPU).

---

## Algorithm Overview

### WAND Algorithm (Weak AND)

**Core Idea**: Skip document blocks that cannot contribute to top-k results based on their maximum possible score.

**Key Components**:
1. **Impacts**: (freq, norm) pairs representing maximum scoring potential per block
2. **Dynamic Threshold**: Minimum score needed to enter top-k heap
3. **Three Heaps**:
   - **Tail**: Scorers behind current doc (ordered by max score)
   - **Lead**: Scorers on current doc (linked list)
   - **Head**: Scorers ahead of current doc (ordered by doc ID)
4. **Skip Logic**: `if (sum(max_scores) < threshold) skip_to_next_block()`

### Algorithm Flow

```
1. Set target_doc = min(head)
2. Advance tail scorers to target_doc
3. While (sum(tail.max_scores) < threshold):
    a. Pop highest max_score scorer from tail
    b. Advance it to next block
    c. Insert into head or lead
4. Collect and score all lead scorers
5. If score >= threshold: add to top-k heap
6. Update threshold from heap
7. Goto step 1
```

---

## Lucene Implementation Study

### Key Classes

1. **`WANDScorer.java`**: Core WAND algorithm implementation
   - Three-heap management (tail/lead/head)
   - Dynamic threshold tracking
   - Skip logic based on sum(max_scores)

2. **`Impacts.java`**: Abstract class for impact information
   - `numLevels()`: Number of impact levels (typically 2-3)
   - `getDocIdUpTo(level)`: Max doc ID for impact level
   - `getImpacts(level)`: List of (freq, norm) pairs for level

3. **`ImpactsEnum.java`**: Extends PostingsEnum with impact info
   - `advanceShallow(target)`: Fast-forward to target without decoding
   - `getImpacts()`: Get impacts for current position

4. **`Lucene912PostingsWriter.java`**: Stores impacts during indexing
   - Tracks max_freq and max_norm per skip block (128 docs)
   - Writes impacts to disk alongside skip data

### Lucene's Skip Structure

**Lucene uses multi-level skip lists**:
- Level 0: Every 128 docs (block size)
- Level 1: Every 128 * 8 = 1024 docs
- Level 2: Every 128 * 8 * 8 = 8192 docs

**Each skip entry stores**:
- Doc ID
- File pointer to next block
- **Max frequency** in block
- **Max norm** in block

---

## Diagon Implementation Plan

### Phase 1: Add Impacts to Postings Writer (Week 1)

#### 1.1 Extend Lucene104PostingsWriter

**File**: `src/core/src/codecs/lucene104/Lucene104PostingsWriter.cpp`

**Changes**:
- Track `max_freq` and `max_norm` for current block (128 docs)
- Write impacts alongside skip data

**New structure** (per skip entry):
```cpp
struct SkipEntry {
    int32_t doc;           // Doc ID at start of block
    int64_t block_fp;      // File pointer to block start
    int32_t max_freq;      // Maximum frequency in block
    int8_t max_norm;       // Maximum norm in block (0-127)
};
```

**Implementation**:
```cpp
class Lucene104PostingsWriter {
private:
    // Track block-level statistics
    int32_t block_max_freq_ = 0;
    int8_t block_max_norm_ = 0;
    int32_t docs_since_last_skip_ = 0;

    std::vector<SkipEntry> skip_entries_;

public:
    void addPosition(...) override {
        // ... existing code ...

        // Track max frequency
        if (freq > block_max_freq_) {
            block_max_freq_ = freq;
        }

        // Track max norm (from norms writer)
        int8_t norm = getNorm(doc);
        if (norm > block_max_norm_) {
            block_max_norm_ = norm;
        }

        docs_since_last_skip_++;

        // Flush skip entry every 128 docs
        if (docs_since_last_skip_ >= 128) {
            flushSkipEntry();
        }
    }

    void flushSkipEntry() {
        SkipEntry entry;
        entry.doc = current_doc_;
        entry.block_fp = block_fp_;
        entry.max_freq = block_max_freq_;
        entry.max_norm = block_max_norm_;

        skip_entries_.push_back(entry);

        // Reset for next block
        block_max_freq_ = 0;
        block_max_norm_ = 0;
        docs_since_last_skip_ = 0;
    }
};
```

**File Format Changes**:
```
// Old format (per skip entry):
VInt doc_delta
VInt block_fp_delta

// New format (per skip entry):
VInt doc_delta
VInt block_fp_delta
VInt max_freq           // NEW: Maximum frequency in block
Byte max_norm           // NEW: Maximum norm in block
```

**Backward Compatibility**:
- Bump codec version: Lucene104 → Lucene105
- Old indices can still be read (without impacts, fall back to exhaustive)

#### 1.2 Extend Lucene104PostingsReader

**File**: `src/core/src/codecs/lucene104/Lucene104PostingsReader.cpp`

**Changes**:
- Read impact metadata from skip entries
- Store in `ImpactsData` structure

**Implementation**:
```cpp
struct ImpactsData {
    int32_t max_freq;
    int8_t max_norm;
    int32_t valid_until_doc;  // Impact valid until this doc ID
};

class Lucene104PostingsEnumWithImpacts : public Lucene104PostingsEnumOptimized {
private:
    std::vector<ImpactsData> impacts_;
    size_t current_impact_index_ = 0;

public:
    // Load impacts from skip data
    void loadImpacts() {
        for (const auto& skip_entry : skip_entries_) {
            ImpactsData impact;
            impact.max_freq = skip_entry.max_freq;
            impact.max_norm = skip_entry.max_norm;
            impact.valid_until_doc = skip_entry.doc + 127;  // Next 128 docs
            impacts_.push_back(impact);
        }
    }

    // Get current impact
    ImpactsData getCurrentImpact() const {
        if (current_impact_index_ < impacts_.size()) {
            return impacts_[current_impact_index_];
        }
        // Fallback: maximum possible values
        return {INT32_MAX, 127, INT32_MAX};
    }

    // Advance to next impact block
    void advanceImpact(int32_t target_doc) {
        while (current_impact_index_ < impacts_.size() &&
               impacts_[current_impact_index_].valid_until_doc < target_doc) {
            current_impact_index_++;
        }
    }
};
```

---

### Phase 2: Implement WAND Scorer (Week 1-2)

#### 2.1 Create WANDScorer Class

**File**: `src/core/include/diagon/search/WANDScorer.h`

**Design**:
```cpp
namespace diagon::search {

// Wrapper for scorer with max score caching
struct ScorerWrapper {
    std::unique_ptr<Scorer> scorer;
    int32_t doc;
    float max_score;          // Maximum score for current block
    int32_t max_score_valid_until;  // Max score valid until this doc
    ScorerWrapper* next;      // For linked list
};

class WANDScorer : public Scorer {
private:
    // Three heaps/lists
    std::vector<ScorerWrapper*> tail_;  // Heap ordered by max_score (descending)
    ScorerWrapper* lead_;                // Linked list
    std::priority_queue<ScorerWrapper*, std::vector<ScorerWrapper*>, DocIDComparator> head_;

    // Current state
    int32_t doc_;
    float min_competitive_score_;
    float lead_score_;
    int32_t up_to_;  // Max score valid until

    // Configuration
    const int top_k_;

public:
    WANDScorer(std::vector<std::unique_ptr<Scorer>> scorers, int top_k)
        : scorers_(std::move(scorers)), top_k_(top_k) {
        initializeHeaps();
    }

    // Core WAND algorithm
    int32_t nextDoc() override {
        while (true) {
            // 1. Get next candidate from head
            if (!moveToNextCandidate()) {
                return NO_MORE_DOCS;
            }

            // 2. Check if we can skip this candidate
            if (canSkipCandidate()) {
                continue;  // Skip to next candidate
            }

            // 3. Advance tail to current doc
            advanceTail();

            // 4. Check if sum(max_scores) >= threshold
            if (getTailMaxScore() + lead_score_ >= min_competitive_score_) {
                return doc_;  // This is a potential match
            }
        }
    }

    float score() override {
        // Score all lead scorers
        advanceAllTail();  // Move all tail to lead

        float total_score = lead_score_;
        for (ScorerWrapper* w = lead_; w != nullptr; w = w->next) {
            total_score += w->scorer->score();
        }
        return total_score;
    }

    void setMinCompetitiveScore(float min_score) override {
        min_competitive_score_ = min_score;
    }

    float getMaxScore(int32_t up_to) override {
        float max_score_sum = 0.0f;
        for (const auto& wrapper : all_scorers_) {
            if (wrapper->doc <= up_to) {
                max_score_sum += wrapper->scorer->getMaxScore(up_to);
            }
        }
        return max_score_sum;
    }

private:
    bool moveToNextCandidate() {
        if (head_.empty()) {
            return false;
        }

        // Pop top of head (scorer with lowest doc ID)
        ScorerWrapper* top = head_.top();
        head_.pop();

        doc_ = top->doc;
        lead_ = top;
        lead_->next = nullptr;
        lead_score_ = top->scorer->score();

        // Pop all scorers on same doc ID
        while (!head_.empty() && head_.top()->doc == doc_) {
            addLead(head_.top());
            head_.pop();
        }

        return true;
    }

    bool canSkipCandidate() {
        // Check if max possible score is competitive
        float max_possible_score = getTailMaxScore() + lead_score_;

        for (ScorerWrapper* w = lead_; w != nullptr; w = w->next) {
            max_possible_score += w->max_score;
        }

        return max_possible_score < min_competitive_score_;
    }

    void advanceTail() {
        // Advance highest max_score scorer from tail
        while (!tail_.empty()) {
            ScorerWrapper* top = tail_[0];

            // Check if advancing this scorer would help
            if (getTailMaxScore() >= min_competitive_score_) {
                break;  // Enough max score in tail
            }

            // Pop from tail and advance
            popTail();

            int32_t next_doc = top->scorer->advance(doc_);
            if (next_doc == NO_MORE_DOCS) {
                continue;  // Scorer exhausted
            }

            top->doc = next_doc;
            updateMaxScore(top);

            // Insert into appropriate heap
            if (next_doc == doc_) {
                addLead(top);
            } else {
                head_.push(top);
            }
        }
    }

    void updateMaxScore(ScorerWrapper* wrapper) {
        // Get max score for next block
        wrapper->max_score = wrapper->scorer->getMaxScore(wrapper->doc + 127);
        wrapper->max_score_valid_until = wrapper->doc + 127;
    }

    float getTailMaxScore() const {
        float sum = 0.0f;
        for (const auto* wrapper : tail_) {
            sum += wrapper->max_score;
        }
        return sum;
    }

    void addLead(ScorerWrapper* wrapper) {
        wrapper->next = lead_;
        lead_ = wrapper;
        lead_score_ += wrapper->scorer->score();
    }

    // Tail heap management (max-heap by max_score)
    void popTail() {
        tail_[0] = tail_.back();
        tail_.pop_back();
        if (!tail_.empty()) {
            heapifyDown(0);
        }
    }

    void pushTail(ScorerWrapper* wrapper) {
        tail_.push_back(wrapper);
        heapifyUp(tail_.size() - 1);
    }
};

} // namespace
```

#### 2.2 Integrate with TopScoreDocCollector

**File**: `src/core/src/search/TopScoreDocCollector.cpp`

**Changes**:
- Pass `minCompetitiveScore` back to scorer via callback
- Update scorer when heap threshold changes

**Implementation**:
```cpp
class TopScoreDocCollector : public Collector {
private:
    Scorer* scorer_ = nullptr;  // Reference to scorer
    float min_competitive_score_ = 0.0f;

public:
    void setScorer(Scorable* scorer) override {
        scorer_ = dynamic_cast<WANDScorer*>(scorer);
    }

    void collect(int doc) override {
        float score = scorer_->score();

        // Try to insert into heap
        if (heap_.size() < top_k_) {
            heap_.push({doc, score});
        } else if (score > heap_.top().score) {
            heap_.pop();
            heap_.push({doc, score});

            // Update minimum competitive score
            float new_threshold = heap_.top().score;
            if (new_threshold > min_competitive_score_) {
                min_competitive_score_ = new_threshold;
                if (scorer_) {
                    scorer_->setMinCompetitiveScore(new_threshold);
                }
            }
        }
    }
};
```

---

### Phase 3: Testing & Validation (Week 2)

#### 3.1 Unit Tests

**File**: `tests/unit/search/WANDScorerTest.cpp`

**Test cases**:
1. **Correctness**: WAND produces same results as exhaustive search
2. **Early Termination**: WAND visits fewer docs than exhaustive
3. **Dynamic Threshold**: Threshold updates correctly
4. **Edge Cases**: Empty results, single doc, all docs match

**Example test**:
```cpp
TEST(WANDScorerTest, MatchesExhaustiveSearch) {
    // Setup: Index with 10K docs
    auto index = createTestIndex(10000);

    // Query: "body"
    auto query = std::make_unique<TermQuery>("body", "test");

    // Search with exhaustive scorer
    auto exhaustive_scorer = query->createScorer(context);
    auto exhaustive_results = searchExhaustive(exhaustive_scorer.get(), 10);

    // Search with WAND scorer
    auto wand_scorer = std::make_unique<WANDScorer>(
        std::vector{std::move(exhaustive_scorer)}, 10);
    auto wand_results = searchWAND(wand_scorer.get(), 10);

    // Assert: Same doc IDs and scores
    ASSERT_EQ(exhaustive_results.size(), wand_results.size());
    for (size_t i = 0; i < exhaustive_results.size(); i++) {
        EXPECT_EQ(exhaustive_results[i].doc, wand_results[i].doc);
        EXPECT_FLOAT_EQ(exhaustive_results[i].score, wand_results[i].score);
    }
}

TEST(WANDScorerTest, SkipsDocuments) {
    auto index = createTestIndex(10000);
    auto query = std::make_unique<TermQuery>("body", "rare");

    // Track visited docs
    int wand_visited = 0;
    int exhaustive_visited = 0;

    auto wand_scorer = createWANDScorer(query.get());
    while (wand_scorer->nextDoc() != NO_MORE_DOCS) {
        wand_visited++;
    }

    auto exhaustive_scorer = createExhaustiveScorer(query.get());
    while (exhaustive_scorer->nextDoc() != NO_MORE_DOCS) {
        exhaustive_visited++;
    }

    // Assert: WAND visits significantly fewer docs
    EXPECT_LT(wand_visited, exhaustive_visited / 2);
}
```

#### 3.2 Benchmark Validation

**Goal**: Verify 5-10x improvement in TopK collection

**Before**:
```
BM_SearchWithDifferentTopK/10          129 µs
```

**Expected After**:
```
BM_SearchWithDifferentTopK/10          13-26 µs  (5-10x improvement)
```

**Validation**:
- Run SearchBenchmark 5x
- Median latency: 13-26µs
- Speedup: 5-10x
- Correctness: All tests pass

---

## Risk Analysis

### High Risk

**Correctness Bugs**
- Risk: WAND may skip documents that should be in top-k
- Mitigation: Extensive unit tests comparing with exhaustive search
- Validation: Run on MSMarco dataset, compare with Lucene results

**Implementation Complexity**
- Risk: Three-heap management is error-prone
- Mitigation: Follow Lucene's proven implementation closely
- Validation: Assertions in debug build, profiling to verify behavior

### Medium Risk

**Backward Compatibility**
- Risk: Old indices without impacts won't benefit
- Mitigation: Fallback to exhaustive search if no impacts
- Validation: Test with both old and new indices

**Performance Regression**
- Risk: WAND overhead may hurt if threshold never updates
- Mitigation: Disable WAND for exhaustive queries (no top-k limit)
- Validation: Benchmark both top-k and exhaustive queries

---

## Success Metrics

### P0 Completion (Week 2)

- [ ] Impacts added to Lucene105 codec
- [ ] WANDScorer implemented and tested
- [ ] Correctness tests passing (WAND == exhaustive)
- [ ] Benchmark shows 5-10x improvement
- [ ] Profile shows TopK collection < 10% CPU (down from 33.96%)
- [ ] Overall latency: 129µs → 13-26µs

### Follow-up (Week 3+)

- [ ] Integrate with BooleanQuery (multi-term WAND)
- [ ] Add multi-level skip lists (128, 1024, 8192)
- [ ] Optimize heap operations further

---

## References

1. **Lucene Source Code**:
   - `WANDScorer.java` - Core WAND implementation
   - `Impacts.java` - Impact data structure
   - `ImpactsEnum.java` - PostingsEnum with impacts
   - `Lucene912PostingsWriter.java` - Impact storage

2. **Papers**:
   - "Efficient Query Evaluation using a Two-Level Retrieval Process" (Broder et al., 2003)
   - "Faster Top-k Document Retrieval Using Block-Max Indexes" (Ding & Suel, 2011)

3. **Diagon Files**:
   - `P0_COMPREHENSIVE_PROFILE_ANALYSIS.md` - Bottleneck analysis
   - `LUCENE_COMPARISON_FINAL.md` - Lucene vs Diagon comparison

---

**Status**: Ready to implement
**Next**: Start Phase 1 - Add impacts to Lucene104PostingsWriter
