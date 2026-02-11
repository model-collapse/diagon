# StandardTokenizer Investigation - Tokenization is NOT the Problem

**Date**: 2026-02-09
**Status**: ✅ **RESOLVED** - Tokenization is correct, problem is elsewhere

---

## Executive Summary

**Initial Hypothesis**: Tokenization mismatch causing 2.85x hit count difference
**Actual Finding**: **StandardTokenizer IS working correctly and matches Lucene exactly**

**Key Discovery**: Lucene's StandardAnalyzer **KEEPS possessives** like "market's" as a single token, just like our StandardTokenizer.

**Root Cause**: The 2.85x hit count mismatch (2,871 vs 1,007) is **NOT due to tokenization**. Problem lies elsewhere (document count, field indexing, or search logic).

---

## Investigation Process

### Step 1: Hypothesis Formation

Based on hit count mismatch (2,871 vs 1,007 for "market"), suspected tokenization differences:
- Theory: Diagon keeps "market's", Lucene splits to "market"
- Expected: This would cause more hits in Diagon
- **REJECTED**: Testing proved this wrong

### Step 2: Direct Tokenization Comparison

**Created Test**: `/tmp/test_tokenizer.cpp` and `/tmp/LuceneTokenizerTest.java`

**Test Input**: `"IBM's market-leading products"`

**Results**:
```
Lucene StandardAnalyzer:  ["ibm's", "market", "leading", "products"]
Diagon StandardTokenizer: ["ibm's", "market", "leading", "products"]
```

**Conclusion**: **IDENTICAL**

### Step 3: Comprehensive Test Cases

| Input | Lucene | Diagon | Match? |
|-------|--------|--------|--------|
| `"IBM's market-leading"` | `["ibm's", "market", "leading"]` | `["ibm's", "market", "leading"]` | ✅ |
| `"market's"` | `["market's"]` | `["market's"]` | ✅ |
| `"don't won't can't"` | `["don't", "won't", "can't"]` | `["don't", "won't", "can't"]` | ✅ |
| `"e-mail co-operate"` | `["e", "mail", "co", "operate"]` | `["e", "mail", "co", "operate"]` | ✅ |
| `"U.S.A. vs U.K."` | `["u.s.a", "vs", "u.k"]` | `["u.s.a", "vs", "u.k"]` | ✅ |

**Conclusion**: **Perfect match in all test cases**

---

## How Tokenization Works

### Lucene's StandardAnalyzer Pipeline

```
Text → StandardTokenizer → LowerCaseFilter → StopFilter (empty) → Tokens
```

**Key Points**:
1. **StandardTokenizer**: Implements Unicode UAX#29 Word Break rules
2. **LowerCaseFilter**: Converts to lowercase
3. **StopFilter**: Filters stop words (default: empty set, so no filtering)

### Diagon's StandardTokenizer

```cpp
// src/core/include/diagon/util/StandardTokenizer.h
class StandardTokenizer {
    static std::vector<std::string> tokenize(const std::string& text) {
        // Uses ICU BreakIterator (UAX#29 implementation)
        icu::BreakIterator::createWordInstance(...)
        // Filters whitespace and punctuation-only tokens
        // Lowercases all tokens
        // Returns vector of lowercase tokens
    }
};
```

**Implementation**: Header-only, uses ICU's BreakIterator

**Correctness**: Matches Lucene's behavior exactly

---

## Why Possessives Are Kept

**UAX#29 Rule**: Unicode Word Break algorithm treats apostrophes **inside** words as part of the word.

**Examples**:
- `"IBM's"` → Word boundary: `[IBM's]` (one word)
- `"don't"` → Word boundary: `[don't]` (one word)
- `"market's"` → Word boundary: `[market's]` (one word)

**This is correct behavior** according to Unicode Standard Annex #29.

**Lucene does NOT remove possessives** - this is a common misconception. The StandardAnalyzer preserves them.

---

## Where Is The Real Problem?

Since tokenization is correct, the 2.85x hit count difference must be due to:

### Hypothesis 1: Document Count Difference

**Evidence**:
- Files in dataset: **21,578**
- Lucene indexed: **19,043 documents** (88.2%)
- Diagon indexed: **20,841 documents** (96.6%)
- Difference: **1,798 more documents** (9.4% more)

**Impact**:
- If 9.4% more docs: Expected ~1,102 hits (1,007 × 1.094)
- Actual: 2,871 hits
- **Gap unexplained**: 1,769 extra hits (60%)

### Hypothesis 2: Field Indexing Differences

**Possible cause**: Diagon might be indexing multiple fields (title + body), Lucene only indexes body

**Test needed**: Check if query `body:market` is somehow matching title field as well

### Hypothesis 3: Search Logic Bug

**Possible cause**: Bug in how Diagon counts hits or matches documents

**Test needed**: Dump actual matched document IDs and compare with Lucene

---

## StandardTokenizer Code Location

**Header**: `/home/ubuntu/diagon/src/core/include/diagon/util/StandardTokenizer.h`
- Line 53-115: Main `tokenize()` method
- Uses ICU BreakIterator (UAX#29)
- Filters whitespace and punctuation-only tokens
- Lowercases all tokens

**Integration**: `/home/ubuntu/diagon/src/core/include/diagon/document/Field.h`
- Line 104: `return util::StandardTokenizer::tokenize(*val);`
- Used by TextField for tokenizing text fields

**Status**: ✅ **Working correctly** - Verified to match Lucene exactly

---

## Next Steps

### Priority 1: Investigate Document Count Difference

**Why are we indexing 1,798 more documents than Lucene?**

Possible causes:
1. Diagon indexes empty documents (title="" body="")
2. Diagon doesn't filter out duplicate documents
3. Lucene filters certain document types

**Action**: Add document filtering to ReutersDatasetAdapter to skip empty docs

### Priority 2: Investigate Field Indexing

**Are we indexing both title and body for "market"?**

**Test**:
```cpp
// Check term statistics per field
auto bodyTerms = reader->terms("body");
auto titleTerms = reader->terms("title");

// Count "market" in each field
int bodyMarketCount = getDocFreq(bodyTerms, "market");
int titleMarketCount = getDocFreq(titleTerms, "market");

// Total should be bodyMarketCount only (query is body:market)
```

**Expected**: Query `body:market` should only match body field

### Priority 3: Investigate Search Logic

**Is there a bug in hit counting?**

**Test**:
```cpp
// Dump matched document IDs
auto results = searcher.search(query, 10000); // Get all hits
for (auto& scoreDoc : results.scoreDocs) {
    std::cout << "DocID: " << scoreDoc.doc << "\n";
}

// Compare with Lucene's matched document IDs
// Should have exact same IDs
```

---

## Lessons Learned

1. **Verify assumptions with direct tests**: Don't assume how a library works - test it!

2. **Lucene keeps possessives**: Common misconception that StandardAnalyzer removes 's

3. **ICU BreakIterator is correct**: Follows UAX#29 standard, same as Lucene

4. **Tokenization is not always the problem**: Hit count mismatches can have many causes

---

## Related Documents

- `docs/REUTERS_CORRECTNESS_ISSUE.md` - Initial bug report
- `docs/BM25_PERF_GUARD_PROFILING.md` - Profiling analysis
- `src/core/include/diagon/util/StandardTokenizer.h` - Implementation

---

**Conclusion**: StandardTokenizer implementation is **correct and matches Lucene**. The hit count mismatch is due to other factors (document count, field indexing, or search logic), not tokenization.
