# Reuters Correctness Issue - Hit Count Mismatch

**Date**: 2026-02-09
**Priority**: **P0 - CRITICAL**
**Status**: **OPEN - Blocking performance optimization**

---

## Executive Summary

**Critical Bug Discovered**: Diagon returns **2.85x more hits** than Lucene for the same query on Reuters dataset.

| Query | Lucene Hits | Diagon Hits | Ratio | Status |
|-------|-------------|-------------|-------|--------|
| `body:market` | 1,007 (5.3%) | 2,871 (13.8%) | **2.85x** | ❌ **INCORRECT** |

**Impact**:
- **Correctness**: Diagon returns incorrect search results
- **Performance**: 10.3x slowdown partly due to 2.85x more documents being scored
- **Comparison**: Cannot fairly compare performance until correctness is fixed

**Root Cause Hypothesis**: Tokenization mismatch between Diagon and Lucene StandardAnalyzer

---

## Problem Details

### Test Setup

**Dataset**: Reuters-21578
**Index**: Created with ReutersDatasetAdapter reading extracted .txt files
**Query**: Single-term query `body:market`
**Expected Behavior**: Match Lucene's 1,007 hits

### Observed Results

```
=== Single-Term Query: 'body:market' ===
Lucene:  1,007 hits out of 19,043 documents (5.3%)
Diagon:  2,871 hits out of 20,841 documents (13.8%)
```

**Analysis**:
- Diagon indexed **20,841 documents** (vs Lucene's 19,043)
  - 1,798 more documents (9.4% more)
  - Possible cause: Including empty documents or different filtering

- Diagon found **2,871 hits** (vs Lucene's 1,007)
  - 1,864 extra hits (2.85x more)
  - **Cannot be explained by document count alone**
  - Even if all 1,798 extra docs contained "market": 1,007 + 1,798 = 2,805 (still less than 2,871)

### Performance Impact

**Current Performance** (with incorrect results):
- P50: 483 µs
- Slowdown: 10.3x vs Lucene (47 µs)

**Expected Performance** (if correctness were fixed):
- Postings to decode: 1,007 instead of 2,871 (2.85x reduction)
- Expected P50: ~170 µs (483 / 2.85)
- **Still 3.6x slower than Lucene**, but much better than current 10.3x

---

## Root Cause Analysis

### Hypothesis 1: Tokenization Mismatch (MOST LIKELY)

**Diagon**: Uses simple whitespace tokenization + lowercase
**Lucene**: Uses StandardAnalyzer (complex tokenization)

**StandardAnalyzer Behavior**:
- Splits on whitespace AND punctuation
- Removes possessives ('s)
- Handles hyphenated words
- Normalizes accents
- Filters out stop words (optional)

**Example**: Text `"market's price-fixing scheme"`

| Tokenizer | Tokens |
|-----------|--------|
| **Lucene StandardAnalyzer** | `[market, price, fixing, scheme]` |
| **Diagon Whitespace** | `[market's, price-fixing, scheme]` |

**Impact**: Term "market" vs "market's"
- Lucene: Indexes as "market" (possessive removed)
- Diagon: Indexes as "market's" (possessive kept)
- Query `body:market` matches different term sets

**Verification**:
```bash
# Check if Lucene's index has term "market's"
# (Probably NO - filtered by StandardAnalyzer)

# Check if Diagon's index has term "market's"
# (Probably YES - kept by simple tokenization)
```

### Hypothesis 2: Document Filtering Differences

**Diagon**: ReutersDatasetAdapter indexes ALL .txt files, even empty ones
**Lucene**: May filter out empty documents or duplicates

**Evidence**:
- Diagon: 20,841 documents
- Lucene: 19,043 documents
- Difference: 1,798 documents (9.4% more)

**Partial Explanation**:
- If 1,798 extra docs ALL contain "market": 1,007 + 1,798 = 2,805 hits
- Actual: 2,871 hits
- Still 66 hits unaccounted for

**Conclusion**: Document filtering is a **minor factor**, but tokenization is the **major factor**.

### Hypothesis 3: Case Sensitivity

**Diagon**: Converts to lowercase during tokenization
**Lucene**: StandardAnalyzer also lowercases

**Unlikely**: Both systems lowercase, so this shouldn't cause mismatch.

### Hypothesis 4: Multiple Field Tokenization

**Diagon**: Tokenizes both "title" and "body" fields
**Lucene**: Same

**Unlikely**: Query explicitly targets `body:market`, not `title:market`.

---

## Validation Steps

### Step 1: Verify Tokenization Difference

**Create test program** to dump terms from Diagon index:

```cpp
// Check if "market" and "market's" are separate terms
auto terms = reader->terms("body");
auto termsEnum = terms->iterator();

BytesRef term;
while (termsEnum->next(&term)) {
    std::string termStr(term.bytes, term.length);
    if (termStr.find("market") != std::string::npos) {
        std::cout << "Term: " << termStr
                  << ", DocFreq: " << termsEnum->docFreq()
                  << "\n";
    }
}
```

**Expected Output**:
```
Term: market, DocFreq: ???
Term: market's, DocFreq: ???  ← Should NOT exist if using StandardAnalyzer
Term: marketing, DocFreq: ???
```

### Step 2: Compare Lucene and Diagon Tokenization

**Test Input**: `"IBM's market-leading products"`

| System | Tokens |
|--------|--------|
| **Lucene StandardAnalyzer** | ? |
| **Diagon Current** | ? |

### Step 3: Check Document Count Mismatch

```bash
# Count .txt files in Reuters dataset
ls /home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark/work/reuters-out/*.txt | wc -l

# Compare with Diagon's 20,841 documents
```

---

## Fix Strategy

### Fix 1: Implement StandardAnalyzer-Compatible Tokenization (P0)

**Goal**: Match Lucene's StandardAnalyzer tokenization exactly

**Implementation**:
1. Read Lucene's `StandardTokenizer.java` and `StandardAnalyzer.java`
2. Implement Unicode-aware tokenization:
   - Split on whitespace AND punctuation
   - Remove possessives ('s, 's)
   - Handle hyphenated words correctly
   - Normalize Unicode (NFD/NFC)
3. Add lowercase filter (already have this)
4. Optional: Add stop word filter (if Lucene uses it)

**Files to Create**:
- `src/core/include/diagon/util/StandardTokenizer.h`
- `src/core/src/util/StandardTokenizer.cpp`
- `tests/unit/util/StandardTokenizerTest.cpp` (compare with Lucene)

**Complexity**: ~500-800 lines of code

**Reference**:
- Lucene: `org.apache.lucene.analysis.standard.StandardTokenizer`
- ICU support: Already have ICU for Unicode handling

### Fix 2: Filter Empty Documents (P1)

**Goal**: Match Lucene's document filtering

**Implementation**:
```cpp
// In ReutersDatasetAdapter::parseDocument()
if (title.empty() && body.empty()) {
    return false; // Skip empty documents
}
```

**Expected Impact**: Reduce document count from 20,841 to ~19,043

### Fix 3: Validate Term Statistics

**Goal**: Ensure term frequencies match Lucene exactly

**Implementation**:
- Create test that compares top 100 terms by document frequency
- Ensure Diagon matches Lucene for each term

---

## Testing Plan

### Test 1: Tokenization Unit Tests

```cpp
TEST(StandardTokenizerTest, HandlesCommonCases) {
    // Test: "IBM's market-leading products cost $1,000"
    // Expected: [ibm, market, leading, products, cost, 1000]
    // (lowercase, possessive removed, hyphen split, $ removed)
}

TEST(StandardTokenizerTest, MatchesLucene) {
    // Compare with Lucene's output for same text
    // Use Lucene's test data from StandardTokenizerTest.java
}
```

### Test 2: Correctness Validation

```cpp
TEST(ReutersCorrectnessTest, MarketQueryHitCount) {
    // Query: body:market
    // Expected: 1,007 hits (matching Lucene)
    ASSERT_EQ(results.totalHits.value, 1007);
}

TEST(ReutersCorrectnessTest, DocumentCount) {
    // Expected: 19,043 documents (matching Lucene)
    ASSERT_EQ(reader->maxDoc(), 19043);
}
```

### Test 3: Performance Re-Validation

After fixing correctness:
1. Re-run BM25PerformanceGuard tests
2. Expected P50: ~170 µs (down from 483 µs)
3. Still 3.6x slower than Lucene, but much better

---

## Timeline

| Phase | Task | Effort | Status |
|-------|------|--------|--------|
| **Phase 1** | Validate tokenization difference | 2 hours | ⏭️ Next |
| **Phase 2** | Implement StandardTokenizer | 1-2 days | ⏭️ Pending |
| **Phase 3** | Fix document filtering | 1 hour | ⏭️ Pending |
| **Phase 4** | Validate correctness | 2 hours | ⏭️ Pending |
| **Phase 5** | Re-measure performance | 1 hour | ⏭️ Pending |
| **Total** | | **2-3 days** | |

---

## Impact on Performance Optimization

**Current Status**: **BLOCKED**

**Reason**: Cannot fairly optimize performance while correctness is broken.

**Why**:
- Decoding 2.85x more postings than necessary
- Scoring 2.85x more documents than necessary
- Profiling will show wrong bottlenecks
- Optimizations may target symptoms, not root causes

**Action**: **Fix correctness first, THEN optimize performance**

**Expected Performance After Fix**:
- Single-term: ~170 µs (3.6x slower than Lucene)
- OR-5: ~720 µs (6.6x slower than Lucene)
- AND-2: ~170 µs (3.9x slower than Lucene)

**Then**: Profile and optimize to close remaining 3-7x gap

---

## Comparison with Previous Work

### Phase 0 (Synthetic Data)

**Problem**: Synthetic data had "market" in 99% of documents
**Result**: 10x slower due to unrealistic term distribution
**Solution**: Switch to real Reuters data ✅

### Phase 1 (Real Reuters, Current)

**Problem**: Hit count mismatch (2.85x more hits)
**Result**: 10.3x slower due to incorrect results + tokenization overhead
**Solution**: Fix StandardAnalyzer tokenization ⏭️

### Phase 2 (After Correctness Fix, Expected)

**Problem**: Still 3-7x slower than Lucene
**Result**: Need to profile and optimize
**Solution**: Profile → Optimize → Validate ⏭️

---

## Related Documents

- `docs/BM25_PERF_GUARD_STATUS.md` - Performance guard implementation status
- `docs/BM25_PERF_GUARD_PROFILING.md` - Synthetic data profiling analysis
- `docs/LUCENE_BM25_PERFORMANCE_BASELINE.md` - Lucene baseline data

---

## Action Items

1. ✅ **DONE**: Identify hit count mismatch
2. ⏭️ **NEXT**: Create term dumper to validate tokenization hypothesis
3. ⏭️ Read Lucene's StandardTokenizer implementation
4. ⏭️ Implement StandardTokenizer in Diagon
5. ⏭️ Fix document filtering
6. ⏭️ Validate correctness with tests
7. ⏭️ Re-run performance guards
8. ⏭️ Profile to find remaining bottlenecks
9. ⏭️ Optimize

---

**Conclusion**: Performance optimization is **blocked** until correctness is fixed. The 10.3x slowdown is **partially artificial** due to processing 2.85x more documents than necessary. Fix tokenization first, then re-evaluate performance.
