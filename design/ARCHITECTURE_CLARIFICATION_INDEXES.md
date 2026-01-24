# Architecture Clarification: Inverted Index vs Forward Index
## Critical Design Decision for Implementation Phase

**Created**: 2026-01-23
**Status**: REQUIRED READING before implementing storage layer
**Related Modules**: 01 (Reader/Writer), 02 (Codecs), 03 (Columns), 14 (Unified SIMD)

---

## Problem Statement

During design discussions, a critical architectural question emerged:

> **"If a field is configured as column-based storage, shall it serve as inverted index or forward index?"**

This question highlights a **fundamental difference** between ClickHouse (our column storage reference) and Lucene (our inverted index reference):

- **ClickHouse**: Pure column storage, NO inverted index (except experimental features)
- **Lucene**: Inverted index primary, column storage (doc values) optional

**Risk**: Without clear documentation, the implementation phase may introduce conflicts where:
- Text fields lack inverted indexes (breaking text search)
- Numeric fields build unnecessary inverted indexes (wasting space)
- APIs assume wrong index type for field operations

**This document provides definitive guidance.**

---

## Core Concepts

### Inverted Index (Term → Documents)

**Definition**: A mapping from terms to the documents containing them.

**Structure**:
```
Term Dictionary:
  "wireless" → PostingsList
  "headphones" → PostingsList
  "bluetooth" → PostingsList

PostingsList for "wireless":
  [doc5, doc12, doc23, doc45, ...]
  [tf:2,  tf:1,  tf:3,  tf:1, ...]  (term frequencies)
  [positions...]                     (for phrase queries)
```

**Direction**: `Term → [Documents]`

**Operations Supported**:
- ✅ Find documents containing term ("wireless")
- ✅ Boolean queries (AND, OR, NOT)
- ✅ Phrase queries ("wireless headphones")
- ✅ BM25 scoring (uses term frequencies)
- ❌ Get field value for document (wrong direction!)
- ❌ Sort by field (inefficient, requires scan)
- ❌ Aggregate (sum, avg, max) across documents

**Storage Characteristics**:
- Sparse (only docs with term are stored)
- Term-centric (organized by terms)
- Optimized for lookup by term

**In Unified Design**: Implemented as **sparse columns**
```cpp
ColumnWindow<int> tfColumn("wireless", SPARSE);
// indices: [5, 12, 23, 45, ...]  (doc IDs)
// values:  [2,  1,  3,  1, ...]  (term frequencies)
```

---

### Forward Index / Doc Values / Column Storage (Document → Value)

**Definition**: A mapping from documents to field values.

**Structure**:
```
Document → Value:
  doc0 → "wireless headphones"
  doc1 → "bluetooth speaker"
  doc2 → "wired earbuds"
  doc3 → "wireless mouse"
  ...
```

For numeric fields:
```
Document → Value:
  doc0 → price: 99.99
  doc1 → price: 129.99
  doc2 → price: 79.99
  doc3 → price: 149.99
  ...
```

**Direction**: `Document → Value`

**Operations Supported**:
- ✅ Get field value for document
- ✅ Sort by field value
- ✅ Aggregate (sum, avg, max, min, count)
- ✅ Group by field value
- ❌ Find documents by term (wrong direction!)
- ❌ Text search (inefficient, requires full scan)

**Storage Characteristics**:
- Dense (all documents stored, including missing values)
- Document-centric (organized by doc ID)
- Optimized for sequential access by doc ID

**In Unified Design**: Implemented as **dense columns**
```cpp
ColumnWindow<float> priceColumn("price", DENSE);
// denseValues: [99.99, 129.99, 79.99, 149.99, ...]  (all docs)
// nullBitmap:  [1, 1, 1, 1, ...]                    (1 = present)
```

---

### ClickHouse Has NO Inverted Index

**Important**: ClickHouse does NOT build inverted indexes (with rare exceptions).

**How ClickHouse handles filtering**:

```sql
SELECT * FROM products WHERE category = 'electronics' AND price < 200
```

1. **Skip Index Pruning** (granule-level):
   ```
   Granule 0: Set(category) = {electronics, books} → CHECK
   Granule 1: Set(category) = {toys, games} → SKIP
   Granule 2: MinMax(price) = [50, 300] → CHECK
   Granule 3: MinMax(price) = [500, 1000] → SKIP
   ```

2. **Column Scan** (row-level within surviving granules):
   ```cpp
   // Read columns for surviving granules
   auto* categoryColumn = readColumn("category", {0, 2});
   auto* priceColumn = readColumn("price", {0, 2});

   // Sequential scan within granules
   for (int row : granule_rows) {
       if (categoryColumn[row] == "electronics" && priceColumn[row] < 200) {
           results.push_back(row);
       }
   }
   ```

**Key Point**: Everything is column-based. No `term → documents` mapping.

**Why ClickHouse doesn't need inverted index**:
- Designed for OLAP (aggregations, not text search)
- Skip indexes provide efficient pruning
- Queries typically have high selectivity after skip index pruning
- No BM25 scoring required

---

### Lucene Requires Inverted Index

**Why Lucene NEEDS inverted index**:

1. **Text Search**: Core use case
   ```java
   // Query: "wireless headphones"
   // Without inverted index: Scan ALL documents' description field (O(N))
   // With inverted index: Lookup posting lists (O(1) per term)
   ```

2. **BM25 Scoring**: Requires term frequencies
   ```java
   // BM25 needs: tf (term frequency), df (doc frequency), doc_length
   // All stored in inverted index efficiently
   ```

3. **Boolean Queries**: Efficient list intersection
   ```java
   // Query: "wireless" AND "bluetooth"
   // Intersect posting lists: O(|docs_with_wireless| + |docs_with_bluetooth|)
   // Without inverted index: O(N × string_compare)
   ```

4. **Phrase Queries**: Requires positions
   ```java
   // Query: "wireless headphones" (exact phrase)
   // Needs positions from inverted index
   ```

**Lucene's Doc Values (Forward Index)**: Optional, used for sorting/aggregation
```java
// Added in Lucene 4.0 to support sorting without fieldCache
NumericDocValues prices = reader.getNumericDocValues("price");
```

---

## Field Configuration Decision Matrix

**Rule**: A field may have inverted index, forward index, or BOTH, depending on use case.

### Text Fields (description, title, content)

**Primary Use Case**: Full-text search with BM25 scoring

**Configuration**:
```cpp
FieldInfo descriptionField = FieldInfo::builder("description")
    .setIndexOptions(IndexOptions::DOCS_AND_FREQS_AND_POSITIONS)  // ✅ Inverted index
    .setDocValuesType(DocValuesType::NONE)                         // ❌ No forward index
    .build();
```

**Storage**:
- ✅ Inverted index: Posting lists (term → docs with positions and frequencies)
- ❌ Forward index: Not built (rarely needed for text)

**Supported Operations**:
- ✅ Text search: `description:"wireless"`
- ✅ Boolean queries: `description:("wireless" AND "bluetooth")`
- ✅ Phrase queries: `description:"wireless headphones"`
- ✅ BM25 scoring
- ❌ Sort by description (not supported - doesn't make sense)
- ❌ Aggregate on description (not supported)

**Implementation Note**: Use sparse columns (Module 14) for posting lists.

---

### Numeric Fields (price, quantity, rating)

**Primary Use Case**: Range filtering, sorting, aggregations

**Configuration**:
```cpp
FieldInfo priceField = FieldInfo::builder("price")
    .setIndexOptions(IndexOptions::NONE)                           // ❌ No inverted index
    .setDocValuesType(DocValuesType::NUMERIC)                      // ✅ Forward index
    .setSkipIndexTypes({SkipIndexType::MIN_MAX})                   // ✅ Skip index for ranges
    .build();
```

**Storage**:
- ❌ Inverted index: Not built (inefficient for numerics)
- ✅ Forward index: Dense column (doc → value)
- ✅ Skip index: MinMax granules for range pruning

**Supported Operations**:
- ✅ Range filter: `price:[0 TO 200]`
- ✅ Sort by price: ascending/descending
- ✅ Aggregations: `avg(price)`, `sum(quantity)`, `max(rating)`
- ❌ Text search (not applicable)
- ❌ Exact term match: `price:99.99` (supported but inefficient without inverted index)

**Implementation Note**: Use dense columns (Module 14) + MinMax skip index (Module 11).

---

### Keyword Fields (category, brand, status)

**Primary Use Case**: Term filtering + sorting/aggregation

**Configuration Option 1: Inverted Index Only** (filtering, no sorting)
```cpp
FieldInfo categoryField = FieldInfo::builder("category")
    .setIndexOptions(IndexOptions::DOCS)                           // ✅ Inverted index
    .setDocValuesType(DocValuesType::NONE)                         // ❌ No forward index
    .setSkipIndexTypes({SkipIndexType::SET, SkipIndexType::BLOOM}) // ✅ Skip indexes
    .build();
```

**Configuration Option 2: Both** (filtering + sorting/aggregation)
```cpp
FieldInfo categoryField = FieldInfo::builder("category")
    .setIndexOptions(IndexOptions::DOCS)                           // ✅ Inverted index
    .setDocValuesType(DocValuesType::SORTED)                       // ✅ Forward index
    .setSkipIndexTypes({SkipIndexType::SET, SkipIndexType::BLOOM}) // ✅ Skip indexes
    .build();
```

**Storage (Option 1 - Inverted Only)**:
- ✅ Inverted index: Posting lists (term → docs)
- ❌ Forward index: Not built
- ✅ Skip index: Set granules for term pruning

**Storage (Option 2 - Both)**:
- ✅ Inverted index: Posting lists (term → docs)
- ✅ Forward index: Dense column (doc → value)
- ✅ Skip index: Set granules for term pruning

**Supported Operations (Option 1)**:
- ✅ Term filter: `category:"electronics"`
- ✅ Boolean queries: `category:("electronics" OR "books")`
- ❌ Sort by category (not supported)
- ❌ Aggregate: `count(distinct category)` (not supported)

**Supported Operations (Option 2)**:
- ✅ Term filter: `category:"electronics"`
- ✅ Boolean queries: `category:("electronics" OR "books")`
- ✅ Sort by category: ascending/descending
- ✅ Aggregate: `count(distinct category)`, group by category

**Trade-off**: Option 2 uses ~2× storage (both indexes) but supports all operations.

**Implementation Note**:
- Inverted index: Sparse columns (Module 14)
- Forward index: Dense columns (Module 14)
- Skip index: Set/Bloom filter (Module 11)

---

### Timestamp Fields (created_at, updated_at)

**Primary Use Case**: Range filtering, sorting

**Configuration**:
```cpp
FieldInfo timestampField = FieldInfo::builder("created_at")
    .setIndexOptions(IndexOptions::NONE)                           // ❌ No inverted index
    .setDocValuesType(DocValuesType::NUMERIC)                      // ✅ Forward index
    .setSkipIndexTypes({SkipIndexType::MIN_MAX})                   // ✅ Skip index
    .build();
```

**Storage**:
- ❌ Inverted index: Not built
- ✅ Forward index: Dense column (doc → timestamp)
- ✅ Skip index: MinMax granules for range pruning

**Supported Operations**:
- ✅ Range filter: `created_at:[2024-01-01 TO 2024-12-31]`
- ✅ Sort by timestamp: ascending/descending
- ✅ Aggregations: `min(created_at)`, `max(created_at)`

**Implementation Note**: Same as numeric fields (dense columns + MinMax skip index).

---

## Storage Decision Table

| Field Type | Inverted Index | Forward Index | Skip Index | Example Fields |
|------------|----------------|---------------|------------|----------------|
| **Text** | ✅ Required | ❌ No | ❌ No | description, title, content |
| **Numeric** | ❌ No | ✅ Required | ✅ MinMax | price, quantity, rating, age |
| **Keyword (filter only)** | ✅ Required | ❌ No | ✅ Set/Bloom | category, brand, status |
| **Keyword (filter + agg)** | ✅ Required | ✅ Required | ✅ Set/Bloom | category, brand, status |
| **Timestamp** | ❌ No | ✅ Required | ✅ MinMax | created_at, updated_at |
| **Boolean** | ✅ Optional | ✅ Required | ✅ Set | is_active, is_deleted |

**Default Rule**:
- Text → Inverted index only
- Numeric/Timestamp → Forward index + skip index only
- Keyword → Inverted index only (add forward index if sorting/aggregation needed)

---

## Query Execution Examples

### Example 1: E-Commerce Text Search

**Query**: "wireless headphones" + price filter [0, 200] + avg price of top 100

**Field Configuration**:
```cpp
// description: Inverted index only
FieldInfo("description", IndexOptions::DOCS_AND_FREQS, DocValuesType::NONE)

// price: Forward index + MinMax skip index only
FieldInfo("price", IndexOptions::NONE, DocValuesType::NUMERIC, {MinMax})
```

**Execution Plan**:
```cpp
// Step 1: Text search using INVERTED INDEX
auto* wirelessPostings = reader->getPostings("description", "wireless");
auto* headphonesPostings = reader->getPostings("description", "headphones");

// SIMD BM25 scoring on posting lists (sparse columns)
TopDocs textResults = bm25Scorer.scoreOr(
    {wirelessPostings, headphonesPostings}, 1000);
// Results: [doc5: 8.3, doc12: 7.1, ...]

// Step 2: Price range filter using SKIP INDEX + FORWARD INDEX
auto* priceSkipIndex = reader->getMinMaxSkipIndex("price");
std::vector<int> candidateGranules = priceSkipIndex->filterGranules(
    RangeFilter("price", 0, 200));
// Pruned: 8 out of 10 granules (80% skipped)

// Fine-grained filtering using forward index (dense column)
auto* priceColumn = reader->getNumericColumn("price");  // FORWARD INDEX
std::vector<int> filteredDocs;
for (int doc : textResults.scoreDocs) {
    float price = priceColumn->get(doc).value_or(-1);
    if (price >= 0 && price <= 200) {
        filteredDocs.push_back(doc);
    }
}

// Step 3: Aggregation using FORWARD INDEX
float totalPrice = 0;
int count = 0;
for (int doc : filteredDocs) {
    if (count >= 100) break;
    totalPrice += priceColumn->get(doc).value();
    count++;
}
float avgPrice = totalPrice / count;
```

**Indexes Used**:
- ✅ Inverted index: Text search on "description"
- ✅ Forward index: Get price values for matched docs, aggregation
- ✅ Skip index: Prune granules by price range

---

### Example 2: Category Filter + Sort by Price

**Query**: category:"electronics" + sort by price descending

**Field Configuration**:
```cpp
// category: Inverted index only (no sorting on category)
FieldInfo("category", IndexOptions::DOCS, DocValuesType::NONE, {Set})

// price: Forward index only (for sorting)
FieldInfo("price", IndexOptions::NONE, DocValuesType::NUMERIC, {MinMax})
```

**Execution Plan**:
```cpp
// Step 1: Category filter using INVERTED INDEX
auto* electronicsPostings = reader->getPostings("category", "electronics");
std::vector<int> matchingDocs = electronicsPostings->getAllDocs();
// Results: [doc3, doc5, doc8, doc12, ...]

// Skip index optimization (coarse pruning)
auto* categorySkipIndex = reader->getSetSkipIndex("category");
std::vector<int> candidateGranules = categorySkipIndex->filterGranules(
    TermFilter("category", "electronics"));
// Only read posting lists for granules containing "electronics"

// Step 2: Sort by price using FORWARD INDEX
auto* priceColumn = reader->getNumericColumn("price");  // FORWARD INDEX
std::vector<std::pair<int, float>> docPrices;
for (int doc : matchingDocs) {
    docPrices.push_back({doc, priceColumn->get(doc).value()});
}
std::sort(docPrices.begin(), docPrices.end(),
          [](auto& a, auto& b) { return a.second > b.second; });  // Descending
```

**Indexes Used**:
- ✅ Inverted index: Category filtering
- ✅ Forward index: Price sorting
- ✅ Skip index: Granule pruning by category

---

### Example 3: Pure Aggregation (No Text Search)

**Query**: SELECT avg(price), count(*) FROM products WHERE created_at >= '2024-01-01'

**Field Configuration**:
```cpp
// created_at: Forward index + MinMax skip index only
FieldInfo("created_at", IndexOptions::NONE, DocValuesType::NUMERIC, {MinMax})

// price: Forward index only
FieldInfo("price", IndexOptions::NONE, DocValuesType::NUMERIC, {MinMax})
```

**Execution Plan** (ClickHouse-style):
```cpp
// Step 1: Skip index pruning by timestamp
auto* timestampSkipIndex = reader->getMinMaxSkipIndex("created_at");
std::vector<int> candidateGranules = timestampSkipIndex->filterGranules(
    RangeFilter("created_at", parseDate("2024-01-01"), MAX_DATE));
// Pruned: 7 out of 10 granules (70% skipped)

// Step 2: Column scan within surviving granules
auto* timestampColumn = reader->getNumericColumn("created_at");
auto* priceColumn = reader->getNumericColumn("price");

float totalPrice = 0;
int count = 0;
for (int granuleId : candidateGranules) {
    // Read columns for this granule (8192 rows)
    auto timestampGranule = timestampColumn->readGranule(granuleId);
    auto priceGranule = priceColumn->readGranule(granuleId);

    for (int i = 0; i < 8192; ++i) {
        if (timestampGranule[i] >= parseDate("2024-01-01")) {
            totalPrice += priceGranule[i];
            count++;
        }
    }
}
float avgPrice = totalPrice / count;
```

**Indexes Used**:
- ❌ Inverted index: Not needed (no text search)
- ✅ Forward index: Timestamp filtering + price aggregation
- ✅ Skip index: Granule pruning by timestamp

**Note**: This query follows pure ClickHouse approach (no inverted index).

---

## Implementation Conflicts to Avoid

### Conflict 1: Building Inverted Index for Numeric Fields

**Bad**:
```cpp
// User configures price field with column_storage=true
FieldInfo priceField("price", DocValuesType::NUMERIC);

// Implementation mistakenly ALSO builds inverted index
// (Lucene legacy behavior)
PostingsFormat::write(priceField, ...);  // ❌ WRONG!
```

**Why Bad**:
- Wastes storage (inverted index for numerics is inefficient)
- Slows down indexing (unnecessary posting list building)
- Provides no benefit (range queries don't use term-based inverted index)

**Correct**:
```cpp
// Check if inverted index should be built
if (fieldInfo.getIndexOptions() != IndexOptions::NONE) {
    // Only build if explicitly enabled
    PostingsFormat::write(fieldInfo, ...);  // ✅ Conditional
}

// Always build doc values if configured
if (fieldInfo.getDocValuesType() != DocValuesType::NONE) {
    DocValuesFormat::write(fieldInfo, ...);  // ✅ Correct
}
```

---

### Conflict 2: Missing Inverted Index for Text Fields

**Bad**:
```cpp
// User configures text field
FieldInfo descriptionField("description", IndexOptions::DOCS_AND_FREQS);

// Implementation only builds column storage (thinking it's sufficient)
ColumnFormat::write(descriptionField, ...);  // ❌ INCOMPLETE!
```

**Why Bad**:
- Text search will fail or be extremely slow (full scan)
- BM25 scoring impossible (no term frequencies)
- Phrase queries impossible (no positions)

**Correct**:
```cpp
// Build inverted index for text fields
if (fieldInfo.getIndexOptions() != IndexOptions::NONE) {
    PostingsFormat::write(fieldInfo, ...);  // ✅ Required for text search
}

// Doc values optional for text (usually not needed)
if (fieldInfo.getDocValuesType() != DocValuesType::NONE) {
    DocValuesFormat::write(fieldInfo, ...);  // Optional
}
```

---

### Conflict 3: Wrong Index Type for Operations

**Bad**:
```cpp
// User wants to sort by price
// But field only has inverted index, no doc values
FieldInfo priceField("price", IndexOptions::DOCS, DocValuesType::NONE);

// Implementation tries to sort using inverted index
// This requires scanning ALL posting lists (O(N × terms))
for (auto& [term, postings] : invertedIndex["price"]) {
    // Reconstruct doc→value mapping ❌ INEFFICIENT!
}
```

**Why Bad**:
- Extremely inefficient (O(N × terms) instead of O(N))
- High memory usage (need to build doc→value map in memory)
- Doesn't scale

**Correct**:
```cpp
// At configuration time, validate requirements
if (requiresSorting("price")) {
    if (fieldInfo.getDocValuesType() == DocValuesType::NONE) {
        throw ConfigurationException(
            "Field 'price' requires doc values for sorting. "
            "Set docValuesType=NUMERIC.");
    }
}

// At query time, use doc values for sorting
auto* priceColumn = reader->getNumericColumn("price");
sort(docs, [&](int a, int b) {
    return priceColumn->get(a) < priceColumn->get(b);
});
```

---

### Conflict 4: Assuming Column Storage Means No Inverted Index

**Bad**:
```cpp
// User configures keyword field with BOTH
FieldInfo categoryField("category",
    IndexOptions::DOCS,          // Inverted index
    DocValuesType::SORTED);      // Doc values

// Implementation sees "column_storage=true" and skips inverted index
if (fieldInfo.getDocValuesType() != DocValuesType::NONE) {
    // Only build column storage ❌ WRONG!
    ColumnFormat::write(fieldInfo, ...);
}
```

**Why Bad**:
- Term filtering becomes inefficient (requires full column scan)
- Boolean queries slow (no posting list intersection)
- Lost benefits of inverted index for filtering

**Correct**:
```cpp
// Build BOTH indexes if configured
if (fieldInfo.getIndexOptions() != IndexOptions::NONE) {
    PostingsFormat::write(fieldInfo, ...);  // ✅ Inverted index
}

if (fieldInfo.getDocValuesType() != DocValuesType::NONE) {
    DocValuesFormat::write(fieldInfo, ...);  // ✅ Doc values
}

// Storage: Both exist independently
```

---

## API Contracts

### IndexWriter API

```cpp
class IndexWriter {
public:
    /**
     * Add a document to the index.
     *
     * For each field:
     * - If IndexOptions != NONE: Build inverted index (posting lists)
     * - If DocValuesType != NONE: Build forward index (doc values/columns)
     * - If SkipIndexTypes not empty: Build skip indexes
     *
     * A field may have none, one, or multiple index types.
     */
    void addDocument(const Document& doc);
};
```

### IndexReader API

```cpp
class LeafReader {
public:
    /**
     * Get posting lists (inverted index) for a term.
     *
     * Returns nullptr if:
     * - Field not indexed (IndexOptions == NONE)
     * - Term doesn't exist in field
     *
     * Use for: Text search, term filtering, BM25 scoring
     */
    std::unique_ptr<PostingsEnum> postings(
        const std::string& field,
        const BytesRef& term) const;

    /**
     * Get numeric doc values (forward index) for a field.
     *
     * Returns nullptr if:
     * - Field has no doc values (DocValuesType == NONE)
     *
     * Use for: Sorting, aggregations, retrieving field values
     */
    std::unique_ptr<NumericDocValues> getNumericDocValues(
        const std::string& field) const;

    /**
     * Get sorted doc values (forward index) for a field.
     *
     * Returns nullptr if:
     * - Field has no doc values (DocValuesType != SORTED)
     *
     * Use for: Sorting by string field, group by, cardinality
     */
    std::unique_ptr<SortedDocValues> getSortedDocValues(
        const std::string& field) const;

    /**
     * Get skip index for a field (Module 11).
     *
     * Returns nullptr if:
     * - Field has no skip index configured
     *
     * Use for: Granule-level filtering before fine-grained evaluation
     */
    std::shared_ptr<IMergeTreeIndex> getSkipIndex(
        const std::string& field,
        SkipIndexType type) const;
};
```

### UnifiedColumnReader API (Module 14)

```cpp
class UnifiedColumnReader {
public:
    /**
     * Get sparse column (inverted index representation).
     *
     * Returns nullptr if:
     * - Field not indexed (IndexOptions == NONE)
     * - Term doesn't exist
     *
     * Use for: SIMD scatter-add on posting lists
     */
    ColumnWindow<int>* getSparseColumn(
        const std::string& field,
        const std::string& term) const;

    /**
     * Get dense column (forward index representation).
     *
     * Returns nullptr if:
     * - Field has no doc values (DocValuesType == NONE)
     *
     * Use for: SIMD batch operations on doc values
     */
    template<typename T>
    ColumnWindow<T>* getDenseColumn(
        const std::string& field) const;
};
```

---

## Validation Rules

### At Configuration Time

```cpp
class FieldInfoValidator {
public:
    static void validate(const FieldInfo& fieldInfo) {
        // Rule 1: Text fields should have inverted index
        if (fieldInfo.getFieldType() == FieldType::TEXT &&
            fieldInfo.getIndexOptions() == IndexOptions::NONE) {
            LOG_WARN << "Text field '" << fieldInfo.name()
                     << "' has no inverted index. Text search will not work.";
        }

        // Rule 2: If sorting required, need doc values
        if (fieldInfo.requiresSorting() &&
            fieldInfo.getDocValuesType() == DocValuesType::NONE) {
            throw ConfigurationException(
                "Field '" + fieldInfo.name() + "' requires doc values for sorting.");
        }

        // Rule 3: If aggregation required, need doc values
        if (fieldInfo.requiresAggregation() &&
            fieldInfo.getDocValuesType() == DocValuesType::NONE) {
            throw ConfigurationException(
                "Field '" + fieldInfo.name() + "' requires doc values for aggregation.");
        }

        // Rule 4: Numeric fields rarely need inverted index
        if ((fieldInfo.getFieldType() == FieldType::INT ||
             fieldInfo.getFieldType() == FieldType::FLOAT ||
             fieldInfo.getFieldType() == FieldType::LONG ||
             fieldInfo.getFieldType() == FieldType::DOUBLE) &&
            fieldInfo.getIndexOptions() != IndexOptions::NONE) {
            LOG_WARN << "Numeric field '" << fieldInfo.name()
                     << "' has inverted index. Consider using doc values + skip index instead.";
        }

        // Rule 5: Positions only make sense with freqs
        if (fieldInfo.getIndexOptions() == IndexOptions::DOCS_AND_POSITIONS &&
            !fieldInfo.hasFreqs()) {
            throw ConfigurationException(
                "Field '" + fieldInfo.name() + "' has positions but no frequencies.");
        }
    }
};
```

### At Query Time

```cpp
class QueryValidator {
public:
    static void validate(const Query& query, const IndexReader& reader) {
        if (auto* termQuery = dynamic_cast<const TermQuery*>(&query)) {
            std::string field = termQuery->getField();
            FieldInfo fieldInfo = reader.getFieldInfo(field);

            // Check if inverted index exists
            if (fieldInfo.getIndexOptions() == IndexOptions::NONE) {
                throw QueryException(
                    "Cannot execute term query on field '" + field +
                    "': field is not indexed.");
            }
        }

        if (auto* sortField = query.getSort()) {
            std::string field = sortField->getField();
            FieldInfo fieldInfo = reader.getFieldInfo(field);

            // Check if doc values exist
            if (fieldInfo.getDocValuesType() == DocValuesType::NONE) {
                throw QueryException(
                    "Cannot sort by field '" + field +
                    "': field has no doc values.");
            }
        }
    }
};
```

---

## Testing Checklist

### Unit Tests

- [ ] **Text field without inverted index**: Verify exception thrown
- [ ] **Numeric field with only doc values**: Verify term query fails gracefully
- [ ] **Keyword field with both indexes**: Verify both filtering and sorting work
- [ ] **Sort on field without doc values**: Verify exception thrown
- [ ] **Aggregation without doc values**: Verify exception thrown

### Integration Tests

- [ ] **E-commerce query**: Text search + numeric filter + aggregation
- [ ] **Pure aggregation**: No text search, only column operations
- [ ] **Mixed keyword operations**: Filter by category + sort by category
- [ ] **Storage validation**: Verify only configured indexes are built

### Performance Tests

- [ ] **Text search performance**: Inverted index vs full scan
- [ ] **Numeric filter performance**: Skip index + column vs inverted index
- [ ] **Sort performance**: Doc values vs reconstructing from inverted index
- [ ] **Storage overhead**: Measure size with different index combinations

---

## Migration Notes

### From Pure Lucene

**Change**: Numeric fields no longer build inverted index by default.

**Migration**:
```java
// Lucene (old)
Field priceField = new IntField("price", 99, Field.Store.NO);
// Automatically builds inverted index

// Lucene++ (new)
FieldInfo priceField = FieldInfo::builder("price")
    .setIndexOptions(IndexOptions::NONE)        // No inverted index
    .setDocValuesType(DocValuesType::NUMERIC)   // Doc values only
    .setSkipIndexTypes({SkipIndexType::MIN_MAX})
    .build();
```

**Impact**: Faster indexing, smaller storage, same query performance for ranges.

### From Pure ClickHouse

**Change**: Text fields now have inverted index (new capability).

**Migration**:
```sql
-- ClickHouse (old)
CREATE TABLE products (
    description String
) ENGINE = MergeTree();
-- Only column storage, no inverted index

-- Lucene++ (new)
FieldInfo descriptionField = FieldInfo::builder("description")
    .setIndexOptions(IndexOptions::DOCS_AND_FREQS_AND_POSITIONS)  // NEW!
    .build();
```

**Impact**: Text search now possible with BM25 scoring.

---

## Summary

### Key Principles

1. **Inverted Index ≠ Column Storage**: They serve different purposes
   - Inverted: Term → Documents (for search)
   - Forward: Document → Value (for sorting/aggregation)

2. **ClickHouse has NO inverted index**: Pure column storage + skip indexes

3. **Lucene requires inverted index**: For text search and BM25 scoring

4. **Lucene++ is hybrid**: Choose per field based on use case

5. **Configuration determines storage**: `IndexOptions` and `DocValuesType` are independent

6. **Some fields need BOTH**: Keyword fields often have inverted index + doc values

### Decision Matrix

| Use Case | Index Type | Example |
|----------|------------|---------|
| Text search | Inverted index | description:"wireless" |
| Numeric range filter | Skip index + forward index | price:[0 TO 200] |
| Sort by field | Forward index | ORDER BY price DESC |
| Aggregate (sum, avg) | Forward index | avg(price) |
| Term filter | Inverted index | category:"electronics" |
| Term filter + sort | Both indexes | category:"electronics" ORDER BY category |

### Implementation Checklist

- [ ] Validate field configuration at startup
- [ ] Build only configured index types
- [ ] Check index existence at query time
- [ ] Provide clear error messages
- [ ] Document field configuration guidelines
- [ ] Add integration tests for all combinations

---

**CRITICAL**: Read this document before implementing:
- Module 01 (IndexReader/Writer)
- Module 02 (Codec Architecture)
- Module 03 (Column Storage)
- Module 14 (Unified SIMD Storage)

**Questions?** Refer back to this document to resolve conflicts.
