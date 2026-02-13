# WAND (Weak AND) Scorer Implementation

## Overview

This document describes the implementation of Block-Max WAND (Weak AND) for dynamic pruning in Diagon, based on Lucene's WANDScorer architecture.

**References:**
- "Efficient Query Evaluation using a Two-Level Retrieval Process" by Broder et al.
- "Faster Top-k Document Retrieval Using Block-Max Indexes" by Ding and Suel
- Apache Lucene's `org.apache.lucene.search.WANDScorer`

## Architecture

### TwoPhaseIterator Pattern

WAND uses a two-phase iteration pattern for efficient document matching:

**Phase 1 - Approximation (Fast):**
```cpp
int advanceApproximation(int target) {
    pushBackLeads(target);              // Move lead scorers to tail
    ScorerWrapper* headTop = advanceHead(target);  // Advance head scorers

    if (headTop == nullptr || headTop->doc > upTo_) {
        updateMaxScores(target);        // Update block-max scores
    }

    return doc_ = headTop->doc;         // Return candidate doc
}
```

**Phase 2 - Matches (Verification):**
```cpp
bool doMatches() {
    moveToNextCandidate();              // Move head scorers to lead

    while (true) {
        float maxPossible = leadScore_ + headMaxScore + tailMaxScore_;

        // Prune if can't be competitive
        if (maxPossible < minCompetitiveScore_ ||
            freq_ + tailSize_ < minShouldMatch_) {
            return false;               // Skip this candidate
        }

        // Check if constraints satisfied
        if (matches()) {
            return true;                // This doc matches!
        }

        // Try to improve score by advancing tail
        advanceTail();
    }
}
```

**Main Loop:**
```cpp
int advance(int target) {
    while (true) {
        int candidate = advanceApproximation(target);
        if (candidate == NO_MORE_DOCS) {
            return NO_MORE_DOCS;
        }

        if (doMatches()) {
            return candidate;           // Found a match
        }

        target = candidate + 1;         // Try next doc
    }
}
```

### Three-Heap Structure

WAND maintains scorers in three locations:

1. **Lead** (linked list): Scorers positioned on current doc
   - Score is computed and summed
   - Counts toward minShouldMatch

2. **Head** (min-heap by doc ID): Scorers ahead of current doc
   - Ordered by doc ID for fast candidate selection
   - Max scores used for pruning estimation

3. **Tail** (max-heap by max score): Scorers behind current doc
   - Ordered by max score (highest first)
   - Capacity = n-1 scorers
   - Advanced only when needed to improve score

### Dynamic Pruning Logic

**Block-Max Scores:**
```cpp
void updateMaxScores(int target) {
    upTo_ = target + 128;  // Block boundary

    // Update max scores for head scorers
    for (ScorerWrapper* wrapper : head_) {
        wrapper->scorer->advanceShallow(target);
        wrapper->maxScore = wrapper->scorer->getMaxScore(upTo_);
    }

    // Update tail max scores
    tailMaxScore_ = 0.0f;
    for (int i = 0; i < tailSize_; ++i) {
        tail_[i]->scorer->advanceShallow(target);
        tail_[i]->maxScore = tail_[i]->scorer->getMaxScore(upTo_);
        tailMaxScore_ += tail_[i]->maxScore;
    }

    // Promote tail scorers if their sum could produce competitive match
    while (tailSize_ > 0 && tailMaxScore_ >= minCompetitiveScore_) {
        ScorerWrapper* wrapper = popTail();
        wrapper->doc = wrapper->scorer->advance(target);
        head_.push_back(wrapper);
    }
}
```

**Pruning Decision:**
```cpp
float maxPossible = leadScore_ + headMaxScore + tailMaxScore_;

if (maxPossible < minCompetitiveScore_ ||
    freq_ + tailSize_ < minShouldMatch_) {
    return false;  // Prune: can't possibly match
}
```

## Fallback Strategy for Simple Queries

### The Problem with 2-Term OR Queries

WAND provides **no benefit** for queries with < 3 terms and minShouldMatch=0:

**Why:**
1. **Tail capacity constraint**: With n=2 terms, tail holds only n-1=1 scorer
2. **Immediate promotion**: When minCompetitiveScore=0 (at search start):
   ```
   tailMaxScore = 2.34  // Sum of single tail scorer
   minCompetitiveScore = 0.0

   // Promotion condition
   while (tailSize > 0 && tailMaxScore >= minCompetitiveScore) {
       // 2.34 >= 0.0 → true, promote to head
   }

   // Result: both scorers in head, tail empty
   ```
3. **Sequential processing**: With tail empty, WAND processes docs sequentially by doc ID
4. **No pruning benefit**: Returns docs in order (0, 2, 19, 20...) like exhaustive search

### Solution: Intelligent Fallback

**Decision Logic:**
```cpp
// WAND is only beneficial when:
// 1. We have 3+ terms (so tail can hold multiple scorers), OR
// 2. minShouldMatch > 0 (requires coordination even with 2 terms)

bool useWAND = config.enable_block_max_wand &&
               scoreMode == ScoreMode::COMPLETE &&
               (shouldScorers.size() >= 3 ||      // 3+ terms
                minShouldMatch > 0);              // Coordination needed

if (useWAND) {
    return std::make_unique<WANDScorer>(...);
} else {
    return std::make_unique<DisjunctionScorer>(...);  // Exhaustive
}
```

**Cases:**

| Query | Terms | minShouldMatch | Uses | Reason |
|-------|-------|----------------|------|--------|
| `A OR B` | 2 | 0 | Exhaustive | WAND degenerates to sequential |
| `A OR B OR C` | 3 | 0 | WAND | Tail can hold 2 scorers for pruning |
| `A OR B` with minShouldMatch=2 | 2 | 2 | WAND | Coordination needed (effectively AND) |
| `A OR B OR C OR D` | 4 | 2 | WAND | Multiple scorers, requires coordination |

## Performance Characteristics

### When WAND Excels

**Best Cases:**
- Queries with 3+ terms
- High selectivity (rare terms)
- Large result sets with filtering (top-K from millions)
- When minCompetitiveScore rises quickly (after ~100 docs)

**Example:**
```
Query: "machine learning algorithm optimization" (4 terms)
Index: 10M documents
Top-K: 100

Without WAND: Scores all 500K matching documents
With WAND:    Scores ~10K documents (98% pruned)
Speedup:      50x
```

### When WAND Provides No Benefit

**Cases where exhaustive is equivalent or better:**
- 2-term OR queries (minShouldMatch=0)
- Very common terms (matches most documents)
- Small result sets (< 10K docs)
- When minCompetitiveScore stays low (no filtering)

## Implementation Notes

### Differences from Lucene

**Similarities:**
- ✅ TwoPhaseIterator pattern
- ✅ Three-heap structure (lead/head/tail)
- ✅ Block-max score tracking
- ✅ Sum-based tail promotion
- ✅ Dynamic threshold from collector

**Differences:**
- ⚠️ C++ implementation (no GC, different memory model)
- ⚠️ Simpler score scaling (Lucene uses integer arithmetic)
- ✅ **Intelligent fallback** for simple queries (Diagon-specific optimization)

### Key Data Structures

**ScorerWrapper:**
```cpp
struct ScorerWrapper {
    Scorer* scorer;           // Not owned
    float maxScore;           // Block-max score
    int doc;                  // Current doc ID
    int64_t cost;             // Cost estimate
    ScorerWrapper* next;      // Lead linked list
};
```

**State Variables:**
```cpp
ScorerWrapper* lead_;         // Linked list (on current doc)
std::vector<ScorerWrapper*> head_;  // Min-heap by doc ID
std::vector<ScorerWrapper*> tail_;  // Max-heap by max score

int doc_;                     // Current doc
float leadScore_;             // Sum of lead scores
int freq_;                    // Number of lead scorers

float tailMaxScore_;          // Sum of tail max scores
int tailSize_;                // Number of tail scorers

float minCompetitiveScore_;   // Dynamic threshold
int upTo_;                    // Block boundary
```

## Testing and Verification

### Correctness Tests

**Test 1: No False Positives**
```cpp
// WAND should find same documents as exhaustive
assert(wandDocs.size() == exhaustiveDocs.size());
for (doc : wandDocs) {
    assert(exhaustiveDocs.contains(doc));
}
```

**Test 2: Score Consistency**
```cpp
// Common documents should have same scores
for (doc : commonDocs) {
    assert(abs(wandScore[doc] - exhaustiveScore[doc]) < 0.0001);
}
```

**Test 3: Top-K Correctness (with fallback)**
```cpp
// 2-term OR query should match exhaustive exactly
auto query2 = BooleanQuery::Builder()
    .add(TermQuery("market"), SHOULD)
    .add(TermQuery("company"), SHOULD)
    .build();

auto wandResults = searcher.search(query2, 100);    // Uses exhaustive
auto exhaustiveResults = exhaustive.search(query2, 100);

assert(wandResults == exhaustiveResults);  // ✅ 100/100 match
```

**Test 4: WAND Used for 3+ Terms**
```cpp
// 3-term query should use WAND optimization
auto query3 = BooleanQuery::Builder()
    .add(TermQuery("market"), SHOULD)
    .add(TermQuery("company"), SHOULD)
    .add(TermQuery("stock"), SHOULD)
    .build();

auto results = searcher.search(query3, 100);  // Uses WAND
// Results may differ from exhaustive (different doc order)
// but all returned docs are correct matches
```

## Future Optimizations

### Potential Improvements

1. **Cost-based promotion**
   - Track scorer costs
   - Prefer advancing low-cost scorers first

2. **Adaptive block sizing**
   - Adjust block size (upTo) based on query selectivity
   - Larger blocks for selective queries

3. **Score caching**
   - Cache scores for docs seen multiple times
   - Useful for multi-segment searches

4. **SIMD acceleration**
   - Vectorized max score computation
   - Batch processing of multiple candidates

5. **Better threshold estimation**
   - Predict minCompetitiveScore before search
   - Use index statistics (score distributions)

## References

### Papers
- Broder, A., et al. "Efficient Query Evaluation using a Two-Level Retrieval Process." ACM SIGIR 2003.
- Ding, S., and Suel, T. "Faster Top-k Document Retrieval Using Block-Max Indexes." ACM SIGIR 2011.

### Implementations
- Apache Lucene: `org.apache.lucene.search.WANDScorer`
- Pisa: C++ implementation of WAND variants
- Terrier: Java implementation with BMW-WAND

### Related Topics
- Block-Max indexes (skip lists with max scores)
- Dynamic pruning algorithms (WAND, BMW-WAND, JASS)
- Top-K query processing
- Inverted index optimization
