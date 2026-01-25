# Multi-Valued (Array) Fields Design
## Based on Lucene SORTED_SET/SORTED_NUMERIC + ClickHouse Array Types

**Design Document #15**
**Status**: Draft
**Author**: System Design
**Date**: 2026-01-25

---

## Table of Contents
1. [Overview](#overview)
2. [Design Principles](#design-principles)
3. [Reference Implementations](#reference-implementations)
4. [Architecture](#architecture)
5. [Schema Definition](#schema-definition)
6. [Field Type API](#field-type-api)
7. [Storage Format](#storage-format)
8. [Indexing Behavior](#indexing-behavior)
9. [Query API](#query-api)
10. [Implementation Details](#implementation-details)
11. [Examples](#examples)
12. [Trade-offs](#trade-offs)

---

## Overview

This design addresses multi-valued (array) fields in Diagon with **explicit schema declaration**, combining:

- **ClickHouse's explicit typing**: Users must declare `Array(T)` in index mapping
- **Lucene's efficient storage**: Flat stream + offset table for inverted index
- **Hybrid optimization**: Different strategies for inverted index vs column storage

### Key Goals

1. **Explicit Schema**: Users must specify if field contains arrays in mapping
2. **Type Safety**: Array element types are validated at indexing time
3. **Efficient Storage**: Optimize for both single-valued and multi-valued cases
4. **Query Support**: Full support for text search, exact match, and range queries
5. **Backward Compatible**: Existing single-valued fields continue to work

---

## Design Principles

### From ClickHouse

✅ **Explicit Array Types**: `Array(Int64)`, `Array(String)` declared in schema
✅ **Parametric Types**: Element type specified at mapping time
✅ **Two-Component Storage**: Data column + offsets column
✅ **Type Validation**: Element types validated during indexing

### From Lucene

✅ **Sorted Values**: Values sorted within each document for efficiency
✅ **Deduplication**: SORTED_SET deduplicates repeated values
✅ **Flat Storage**: All values stored in flat stream with offset table
✅ **DocValues Types**: Special types for multi-valued fields

### Diagon-Specific

✅ **Schema Enforcement**: FieldInfo tracks multi-valued flag
✅ **Hybrid Storage**: Different strategies for inverted index vs columns
✅ **API Simplicity**: Intuitive API for adding array fields

---

## Reference Implementations

### Lucene Multi-Valued Fields

**Files**:
- `lucene/core/src/java/org/apache/lucene/index/DocValuesType.java`
- `lucene/core/src/java/org/apache/lucene/document/SortedSetDocValuesField.java`
- `lucene/core/src/java/org/apache/lucene/document/SortedNumericDocValuesField.java`
- `lucene/core/src/java/org/apache/lucene/codecs/lucene90/Lucene90DocValuesConsumer.java`

**Key Insights**:
- `SORTED_SET`: For String/BytesRef arrays (deduplicated, sorted)
- `SORTED_NUMERIC`: For numeric arrays (sorted within document)
- Storage format: flat value stream + offset table + count stream
- Values sorted for efficient merging and querying

### ClickHouse Array Type

**Files**:
- `ClickHouse/src/DataTypes/DataTypeArray.h`
- `ClickHouse/src/Columns/ColumnArray.h`
- `ClickHouse/src/DataTypes/Serializations/SerializationArray.cpp`

**Key Insights**:
- Explicit type declaration: `Array(Int32)`, `Array(String)`
- Storage: flattened data column + cumulative offsets
- Multi-stream serialization: separate streams for sizes and elements
- Supports nested arrays: `Array(Array(T))`

---

## Architecture

### Schema Declaration

```
Index Mapping (Schema)
├─ Field: "categories"
│  ├─ Type: Array(StringField)
│  ├─ Element Type: String (exact match)
│  └─ Storage: Inverted Index + Column
│
├─ Field: "tags"
│  ├─ Type: Array(TextField)
│  ├─ Element Type: Text (tokenized)
│  └─ Storage: Inverted Index
│
└─ Field: "ratings"
   ├─ Type: Array(NumericDocValues)
   ├─ Element Type: Int64
   └─ Storage: Column Only
```

### Storage Architecture

```
┌─────────────────────────────────────────────────┐
│            Multi-Valued Field                    │
├─────────────────────────────────────────────────┤
│                                                  │
│  ┌────────────────┐      ┌─────────────────┐   │
│  │ Inverted Index │      │ Column Storage  │   │
│  │ (Lucene-style) │      │ (ClickHouse)    │   │
│  ├────────────────┤      ├─────────────────┤   │
│  │ • Term Dict    │      │ • Data Column   │   │
│  │ • Postings     │      │ • Offsets Col   │   │
│  │ • Positions*   │      │ • Granule-based │   │
│  │ • Value Dedup  │      │ • COW Semantics │   │
│  └────────────────┘      └─────────────────┘   │
│         │                          │            │
│         └──────────┬───────────────┘            │
│                    │                            │
│          ┌─────────▼──────────┐                 │
│          │ Unified API        │                 │
│          ├────────────────────┤                 │
│          │ • ArrayField<T>    │                 │
│          │ • Schema Enforce   │                 │
│          │ • Type Validation  │                 │
│          └────────────────────┘                 │
│                                                  │
└─────────────────────────────────────────────────┘

* Positions only for TextField arrays
```

---

## Schema Definition

### FieldInfo Extension

Extend `FieldInfo` to track multi-valued fields:

```cpp
// src/core/include/diagon/index/FieldInfo.h

struct FieldInfo {
    std::string name;
    int number;

    // Existing fields
    IndexOptions indexOptions;
    DocValuesType docValuesType;
    bool stored;
    bool omitNorms;

    // NEW: Multi-valued support
    bool multiValued;           // True if field is an array
    FieldType elementType;      // Type of array elements (for validation)

    // Validation: multi-valued fields use special DocValues types
    bool isMultiValued() const { return multiValued; }

    // Factory methods
    static FieldInfo createSingleValued(const std::string& name, FieldType type);
    static FieldInfo createMultiValued(const std::string& name, FieldType elementType);
};
```

### Index Mapping API

```cpp
// src/core/include/diagon/index/IndexMapping.h

class IndexMapping {
public:
    // Single-valued field
    void addField(const std::string& name, FieldType type, bool stored = true);

    // Multi-valued field (EXPLICIT DECLARATION)
    void addArrayField(const std::string& name, FieldType elementType, bool stored = true);

    // Query mapping
    bool isMultiValued(const std::string& name) const;
    FieldType getElementType(const std::string& name) const;
    const FieldInfo& getFieldInfo(const std::string& name) const;

private:
    std::unordered_map<std::string, FieldInfo> fields_;
};
```

### Schema Creation Example

```cpp
// Create index mapping
IndexMapping mapping;

// Single-valued fields
mapping.addField("title", FieldType::TEXT, true);
mapping.addField("price", FieldType::NUMERIC, true);

// Multi-valued fields (explicit array declaration)
mapping.addArrayField("categories", FieldType::STRING, true);  // Array(String)
mapping.addArrayField("tags", FieldType::TEXT, false);         // Array(Text)
mapping.addArrayField("ratings", FieldType::NUMERIC, true);    // Array(Int64)

// Create writer with mapping
IndexWriterConfig config;
config.setIndexMapping(mapping);
auto writer = IndexWriter::create(dir.get(), config);
```

---

## Field Type API

### Array Field Classes

Three new field classes for multi-valued fields:

```cpp
// src/core/include/diagon/document/ArrayField.h

namespace diagon::document {

// ==================== ArrayTextField ====================

/**
 * Multi-valued text field (tokenized, full-text searchable)
 *
 * Each value is tokenized separately and all terms are indexed.
 * Positions are preserved for phrase queries across values.
 */
class ArrayTextField : public IndexableField {
public:
    ArrayTextField(const std::string& name,
                   const std::vector<std::string>& values,
                   bool stored);

    // Add value to array
    void addValue(const std::string& value);

    // Access
    const std::vector<std::string>& getValues() const { return values_; }
    size_t getValueCount() const { return values_.size(); }

    // IndexableField interface
    std::string name() const override { return name_; }
    FieldType fieldType() const override;

private:
    std::string name_;
    std::vector<std::string> values_;
    bool stored_;
};

// ==================== ArrayStringField ====================

/**
 * Multi-valued string field (exact match, not tokenized)
 *
 * Each value treated as single term for exact matching.
 * Values are deduplicated and sorted within document.
 */
class ArrayStringField : public IndexableField {
public:
    ArrayStringField(const std::string& name,
                     const std::vector<std::string>& values,
                     bool stored);

    void addValue(const std::string& value);

    const std::vector<std::string>& getValues() const { return values_; }
    size_t getValueCount() const { return values_.size(); }

    std::string name() const override { return name_; }
    FieldType fieldType() const override;

private:
    std::string name_;
    std::vector<std::string> values_;
    bool stored_;
};

// ==================== ArrayNumericField ====================

/**
 * Multi-valued numeric field (range queries, sorting)
 *
 * Stored in column format for efficient filtering.
 * Values are sorted within document.
 */
class ArrayNumericField : public IndexableField {
public:
    ArrayNumericField(const std::string& name,
                      const std::vector<int64_t>& values);

    void addValue(int64_t value);

    const std::vector<int64_t>& getValues() const { return values_; }
    size_t getValueCount() const { return values_.size(); }

    std::string name() const override { return name_; }
    FieldType fieldType() const override;

private:
    std::string name_;
    std::vector<int64_t> values_;
};

} // namespace diagon::document
```

### Usage Example

```cpp
using namespace diagon::document;

Document doc;

// Single-valued fields
doc.addField(std::make_unique<TextField>("title", "Product Title", true));
doc.addField(std::make_unique<NumericDocValuesField>("price", 9999));

// Multi-valued fields
doc.addField(std::make_unique<ArrayStringField>(
    "categories",
    std::vector<std::string>{"electronics", "computers", "laptops"},
    true
));

doc.addField(std::make_unique<ArrayTextField>(
    "tags",
    std::vector<std::string>{"gaming", "portable", "high-performance"},
    false
));

doc.addField(std::make_unique<ArrayNumericField>(
    "ratings",
    std::vector<int64_t>{5, 4, 5, 3, 4}  // User ratings
));

writer->addDocument(doc);
```

---

## Storage Format

### Inverted Index Storage (Text/String Arrays)

Following Lucene's SORTED_SET/SORTED_NUMERIC format:

```
┌─────────────────────────────────────────────────┐
│           ArrayStringField Storage               │
├─────────────────────────────────────────────────┤
│                                                  │
│  1. Term Dictionary (FST)                       │
│     ├─ "computers" → term_id: 1                 │
│     ├─ "electronics" → term_id: 2               │
│     └─ "laptops" → term_id: 3                   │
│                                                  │
│  2. Ordinal Stream (sorted, deduplicated)       │
│     Doc 0: [1, 2, 3]  (computers, electronics, laptops)
│     Doc 1: [2]        (electronics)             │
│     Doc 2: [1, 3]     (computers, laptops)      │
│     → Flat: [1, 2, 3, 2, 1, 3]                  │
│                                                  │
│  3. Offset Table (cumulative positions)         │
│     [0, 3, 4, 6]                                │
│     Doc 0: offset[0]=0, count=3-0=3             │
│     Doc 1: offset[1]=3, count=4-3=1             │
│     Doc 2: offset[2]=4, count=6-4=2             │
│                                                  │
│  4. Postings (inverted index)                   │
│     Term "electronics" → [Doc 0, Doc 1]         │
│     Term "computers" → [Doc 0, Doc 2]           │
│     Term "laptops" → [Doc 0, Doc 2]             │
│                                                  │
└─────────────────────────────────────────────────┘
```

**Key Points**:
- Values deduplicated within document
- Values sorted by term ordinal
- Efficient for set operations (intersection, union)

### Column Storage (Numeric Arrays)

Following ClickHouse's Array column format:

```
┌─────────────────────────────────────────────────┐
│        ArrayNumericField Storage                 │
├─────────────────────────────────────────────────┤
│                                                  │
│  1. Data Column (flattened values)              │
│     Doc 0: [5, 4, 5]                            │
│     Doc 1: [3]                                  │
│     Doc 2: [4, 4, 5]                            │
│     → Flat: [5, 4, 5, 3, 4, 4, 5]               │
│                                                  │
│  2. Offsets Column (cumulative positions)       │
│     [3, 4, 7]                                   │
│     Doc 0: offsets[0]=3, size=3-0=3             │
│     Doc 1: offsets[1]=4, size=4-3=1             │
│     Doc 2: offsets[2]=7, size=7-4=3             │
│                                                  │
│  3. Compression (optional)                      │
│     Data: Delta + LZ4                           │
│     Offsets: Delta + Varint                     │
│                                                  │
│  4. Granule Organization (8192 rows)            │
│     Granule 0: Docs 0-8191                      │
│     ├─ data.bin: compressed data values         │
│     ├─ offsets.bin: compressed offsets          │
│     └─ marks: granule boundaries                │
│                                                  │
└─────────────────────────────────────────────────┘
```

**Key Points**:
- Two-component storage (data + offsets)
- Granule-based for efficient I/O
- Compression applied to both streams
- Supports range queries and aggregations

### TextField Array Storage

```
┌─────────────────────────────────────────────────┐
│          ArrayTextField Storage                  │
├─────────────────────────────────────────────────┤
│                                                  │
│  Doc: tags=["high performance", "portable"]     │
│                                                  │
│  1. Tokenization (per value)                    │
│     Value 0: "high performance"                 │
│              → tokens: ["high", "performance"]  │
│     Value 1: "portable"                         │
│              → tokens: ["portable"]             │
│                                                  │
│  2. Position Assignment (continuous)            │
│     Token "high":        position=0             │
│     Token "performance": position=1             │
│     Token "portable":    position=2             │
│                                                  │
│  3. Postings (with positions)                   │
│     Term "high":        [Doc 0, pos=0]          │
│     Term "performance": [Doc 0, pos=1]          │
│     Term "portable":    [Doc 0, pos=2]          │
│                                                  │
│  4. Phrase Queries (across values)              │
│     Query: "high performance"                   │
│     Match: YES (positions 0-1 are consecutive)  │
│                                                  │
│     Query: "performance portable"               │
│     Match: YES (positions 1-2 are consecutive)  │
│                                                  │
└─────────────────────────────────────────────────┘
```

**Key Points**:
- Each array value tokenized separately
- Positions are continuous across values
- Phrase queries work across array boundaries
- Useful for: tags, keywords, multi-field descriptions

---

## Indexing Behavior

### Validation at Index Time

```cpp
// src/core/index/IndexWriter.cpp

void IndexWriter::addDocument(const Document& doc) {
    for (const auto& field : doc.fields()) {
        const FieldInfo& info = mapping_.getFieldInfo(field->name());

        // Validate multi-valued declaration
        bool isArrayField = dynamic_cast<const ArrayField*>(field.get()) != nullptr;

        if (isArrayField && !info.isMultiValued()) {
            throw IllegalArgumentException(
                "Field '" + field->name() + "' is not declared as array in mapping. " +
                "Use mapping.addArrayField() to declare array fields.");
        }

        if (!isArrayField && info.isMultiValued()) {
            throw IllegalArgumentException(
                "Field '" + field->name() + "' is declared as array but single value provided. " +
                "Use ArrayField classes for multi-valued fields.");
        }

        // Validate element types
        if (isArrayField) {
            validateElementType(field, info.elementType);
        }
    }

    // Process document...
}
```

### Sorting and Deduplication

```cpp
// src/core/codecs/DocValuesConsumer.cpp

void DocValuesConsumer::addSortedSetField(const FieldInfo& field,
                                         const std::vector<BytesRef>& values) {
    // 1. Sort values
    std::vector<BytesRef> sorted = values;
    std::sort(sorted.begin(), sorted.end());

    // 2. Deduplicate consecutive values
    auto last = std::unique(sorted.begin(), sorted.end());
    sorted.erase(last, sorted.end());

    // 3. Convert to ordinals and write
    for (const auto& value : sorted) {
        int64_t ord = termDict_.getOrAdd(value);
        ordinalsWriter_.add(ord);
    }

    // 4. Record count for this document
    countsWriter_.add(sorted.size());
}

void DocValuesConsumer::addSortedNumericField(const FieldInfo& field,
                                             const std::vector<int64_t>& values) {
    // 1. Sort values (do NOT deduplicate for numeric)
    std::vector<int64_t> sorted = values;
    std::sort(sorted.begin(), sorted.end());

    // 2. Write sorted values
    for (int64_t value : sorted) {
        valuesWriter_.add(value);
    }

    // 3. Record count
    countsWriter_.add(sorted.size());
}
```

**Deduplication Rules**:
- **ArrayStringField**: Values deduplicated (set semantics)
- **ArrayTextField**: Values NOT deduplicated (bag semantics)
- **ArrayNumericField**: Values NOT deduplicated (allows duplicates)

---

## Query API

### Query Types for Array Fields

```cpp
// src/core/include/diagon/search/Query.h

namespace diagon::search {

// ==================== TermQuery (works with arrays) ====================

// Matches documents where array contains the term
auto query = TermQuery::create("categories", "electronics");
// Matches: categories=["electronics", "computers"]
// Matches: categories=["electronics"]
// No match: categories=["computers"]

// ==================== PhraseQuery (for TextField arrays) ====================

auto query = PhraseQuery::builder("tags")
    .add("high")
    .add("performance")
    .build();
// Matches: tags=["high performance gaming"]
// Matches: tags=["high", "performance"] (consecutive positions)

// ==================== RangeQuery (for numeric arrays) ====================

auto query = RangeQuery::create("ratings", 4, 5);  // 4 <= rating <= 5
// Matches if ANY value in array satisfies range
// Matches: ratings=[1, 2, 5] (contains 5)
// Matches: ratings=[4, 4, 4] (contains 4)
// No match: ratings=[1, 2, 3]

// ==================== ArrayContainsAllQuery (NEW) ====================

auto query = ArrayContainsAllQuery::create("categories",
    std::vector<std::string>{"electronics", "laptops"});
// Matches only if ALL terms present
// Matches: categories=["electronics", "computers", "laptops"]
// No match: categories=["electronics", "computers"]

// ==================== ArrayContainsAnyQuery (NEW) ====================

auto query = ArrayContainsAnyQuery::create("tags",
    std::vector<std::string>{"gaming", "work"});
// Matches if ANY term present
// Matches: tags=["gaming", "portable"] (has gaming)
// Matches: tags=["work", "productivity"] (has work)

// ==================== ArraySizeQuery (NEW) ====================

auto query = ArraySizeQuery::create("ratings", 5);  // exactly 5 ratings
auto query = ArraySizeQuery::createMin("ratings", 3);  // at least 3 ratings
auto query = ArraySizeQuery::createMax("ratings", 10);  // at most 10 ratings

} // namespace diagon::search
```

### Retrieval API

```cpp
// src/core/include/diagon/search/IndexSearcher.h

class IndexSearcher {
public:
    // Search returns StoredDocument with array fields
    std::vector<StoredDocument> search(Query* query, int topN);
};

// Usage
auto results = searcher.search(query.get(), 10);
for (const auto& doc : results) {
    // Get single-valued field
    std::string title = doc.get("title");

    // Get array field
    std::vector<std::string> categories = doc.getArray("categories");
    std::vector<int64_t> ratings = doc.getNumericArray("ratings");

    std::cout << "Title: " << title << "\n";
    std::cout << "Categories: ";
    for (const auto& cat : categories) {
        std::cout << cat << " ";
    }
    std::cout << "\nRatings: ";
    for (int64_t rating : ratings) {
        std::cout << rating << " ";
    }
}
```

---

## Implementation Details

### DocValuesType Extension

```cpp
// src/core/include/diagon/index/DocValuesType.h

enum class DocValuesType {
    NONE,                  // No doc values
    NUMERIC,               // Single numeric value
    BINARY,                // Single binary value
    SORTED,                // Single sorted string
    SORTED_SET,            // Multi-valued sorted strings (NEW: for ArrayStringField)
    SORTED_NUMERIC,        // Multi-valued sorted numerics (NEW: for ArrayNumericField)
    SORTED_SET_TEXT        // Multi-valued text (NEW: for ArrayTextField)
};
```

### FieldType Configuration

```cpp
// Predefined types for array fields

// ArrayTextField::TYPE
FieldType {
    .indexOptions = IndexOptions::DOCS_AND_FREQS_AND_POSITIONS,
    .docValuesType = DocValuesType::SORTED_SET_TEXT,
    .stored = true/false,
    .tokenized = true,
    .omitNorms = false,
    .multiValued = true
}

// ArrayStringField::TYPE
FieldType {
    .indexOptions = IndexOptions::DOCS,
    .docValuesType = DocValuesType::SORTED_SET,
    .stored = true/false,
    .tokenized = false,
    .omitNorms = true,
    .multiValued = true
}

// ArrayNumericField::TYPE
FieldType {
    .indexOptions = IndexOptions::NONE,
    .docValuesType = DocValuesType::SORTED_NUMERIC,
    .stored = false,  // Always stored in doc values
    .tokenized = false,
    .omitNorms = true,
    .multiValued = true
}
```

### Writer Classes

```cpp
// src/core/index/SortedSetDocValuesWriter.h

class SortedSetDocValuesWriter {
public:
    void addValue(int doc, const BytesRef& value);
    void finish(int maxDoc);
    void flush(SegmentWriteState& state, DocValuesConsumer* consumer);

private:
    BytesRefHash terms_;                    // Deduplicated terms
    PackedLongValues::Builder ordinals_;    // Term ordinals per doc
    PackedLongValues::Builder counts_;      // Value counts per doc
    int currentDoc_ = -1;
    int currentUpto_ = 0;
};

// src/core/index/SortedNumericDocValuesWriter.h

class SortedNumericDocValuesWriter {
public:
    void addValue(int doc, int64_t value);
    void finish(int maxDoc);
    void flush(SegmentWriteState& state, DocValuesConsumer* consumer);

private:
    PackedLongValues::Builder values_;      // All numeric values
    PackedLongValues::Builder counts_;      // Value counts per doc
    int currentDoc_ = -1;
    std::vector<int64_t> currentValues_;    // Buffer for current doc
};
```

---

## Examples

### E-commerce Product with Arrays

```cpp
Document createProduct(const Product& p) {
    Document doc;

    // Single-valued fields
    doc.addField(std::make_unique<TextField>("title", p.title, true));
    doc.addField(std::make_unique<TextField>("description", p.description, false));
    doc.addField(std::make_unique<StringField>("brand", p.brand, true));
    doc.addField(std::make_unique<NumericDocValuesField>("price", p.price * 100));

    // Multi-valued fields
    doc.addField(std::make_unique<ArrayStringField>(
        "categories", p.categories, true));

    doc.addField(std::make_unique<ArrayTextField>(
        "tags", p.tags, false));

    doc.addField(std::make_unique<ArrayNumericField>(
        "ratings", p.ratings));

    doc.addField(std::make_unique<ArrayStringField>(
        "colors", p.availableColors, true));

    return doc;
}

// Index mapping
IndexMapping mapping;
mapping.addField("title", FieldType::TEXT, true);
mapping.addField("description", FieldType::TEXT, false);
mapping.addField("brand", FieldType::STRING, true);
mapping.addField("price", FieldType::NUMERIC, true);
mapping.addArrayField("categories", FieldType::STRING, true);
mapping.addArrayField("tags", FieldType::TEXT, false);
mapping.addArrayField("ratings", FieldType::NUMERIC, true);
mapping.addArrayField("colors", FieldType::STRING, true);
```

### Query Examples

```cpp
// 1. Find products in specific category
auto query1 = TermQuery::create("categories", "laptops");

// 2. Find products with ALL specified categories
auto query2 = ArrayContainsAllQuery::create("categories",
    {"electronics", "computers", "laptops"});

// 3. Find products with tag containing phrase
auto query3 = PhraseQuery::builder("tags")
    .add("high").add("performance")
    .build();

// 4. Find products with at least 3 ratings
auto query4 = ArraySizeQuery::createMin("ratings", 3);

// 5. Find products with rating >= 4
auto query5 = RangeQuery::create("ratings", 4, INT64_MAX);

// 6. Combined query: laptops with high ratings
auto boolQuery = BooleanQuery::builder()
    .add(TermQuery::create("categories", "laptops"), BooleanClause::MUST)
    .add(RangeQuery::create("ratings", 4, 5), BooleanClause::MUST)
    .add(ArraySizeQuery::createMin("ratings", 5), BooleanClause::MUST)
    .build();
```

### Log Entry with Arrays

```cpp
Document createLog(const LogEntry& log) {
    Document doc;

    doc.addField(std::make_unique<TextField>("message", log.message, true));
    doc.addField(std::make_unique<StringField>("level", log.level, true));
    doc.addField(std::make_unique<NumericDocValuesField>("timestamp", log.timestamp));

    // Multi-valued fields
    doc.addField(std::make_unique<ArrayStringField>(
        "tags", log.tags, false));  // ["error", "database", "timeout"]

    doc.addField(std::make_unique<ArrayStringField>(
        "hosts", log.affectedHosts, true));  // ["server1", "server2"]

    if (!log.errorCodes.empty()) {
        doc.addField(std::make_unique<ArrayNumericField>(
            "error_codes", log.errorCodes));  // [500, 503]
    }

    return doc;
}
```

---

## Trade-offs

### Design Decisions

| Decision | Rationale | Alternative Considered |
|----------|-----------|------------------------|
| **Explicit schema declaration** | Type safety, better validation, optimized storage | Schema-less (Lucene-style) - more flexible but error-prone |
| **Sorted values within document** | Efficient merging, deduplication, range queries | Preserve insertion order - simpler but less efficient |
| **Deduplicate StringField arrays** | Set semantics, smaller index | Keep duplicates - allows counting |
| **Keep duplicates in numeric arrays** | Support aggregations (sum, avg) | Deduplicate - matches StringField |
| **Continuous positions in TextField arrays** | Phrase queries across values | Separate positions per value - prevents cross-value phrases |
| **Two-component column storage** | ClickHouse-compatible, efficient I/O | Single flattened column - simpler but less flexible |

### Performance Characteristics

| Operation | Single-Valued | Multi-Valued (Array) | Notes |
|-----------|---------------|----------------------|-------|
| **Index write** | Baseline | +20-30% overhead | Sorting + offset tracking |
| **Query (TermQuery)** | Baseline | ~Same | Same postings traversal |
| **Query (RangeQuery)** | Baseline | +10-20% | Check multiple values per doc |
| **Stored field retrieval** | Baseline | +N × 5% | Decompress N values |
| **Memory (indexing)** | Baseline | +N × 100% | Buffer all values per doc |
| **Disk space (inverted)** | Baseline | +N × 80% | Deduplication saves space |
| **Disk space (column)** | Baseline | +N × 95% | Offsets add overhead |

**N** = average array size

### Storage Efficiency

**Example**: 1M documents with categories

```
Single-valued (1 category per doc):
  - Terms: 100 unique categories
  - Postings: 1M doc IDs
  - Storage: ~4MB

Multi-valued (avg 3 categories per doc):
  - Terms: 100 unique categories
  - Postings: ~3M doc IDs (deduplicated: ~2.5M)
  - Ordinals: 2.5M ordinals
  - Offsets: 1M offsets
  - Storage: ~12MB (3× single-valued)
```

### Limitations

1. **No nested arrays**: `Array(Array(T))` not supported in v1
2. **No array aggregations in v1**: Sum, avg, etc. require custom implementation
3. **Position limit**: Max 2^31 positions per document (TextField arrays)
4. **Memory during indexing**: All values buffered until doc finishes
5. **No partial array updates**: Must reindex entire document

### Future Enhancements

**Phase 2**:
- Nested arrays: `Array(Array(Int64))`
- Array aggregation functions
- Sparse array encoding (for mostly-empty arrays)
- Array-specific skip indexes

**Phase 3**:
- JSON array support: `Array(JSON)`
- Array-specific compression codecs
- Streaming array indexing (reduce memory)
- Array similarity queries

---

## Summary

This design provides:

1. ✅ **Explicit Schema**: Users declare arrays in mapping
2. ✅ **Type Safety**: Element types validated at index time
3. ✅ **Efficient Storage**: Optimized for both single and multi-valued
4. ✅ **Rich Queries**: Term, phrase, range, contains, size queries
5. ✅ **Hybrid Architecture**: Lucene inverted index + ClickHouse columns
6. ✅ **Backward Compatible**: Existing single-valued fields unchanged

**Next Steps**:
1. Implement `ArrayField` classes
2. Extend `FieldInfo` with multi-valued tracking
3. Implement `SortedSetDocValuesWriter` and `SortedNumericDocValuesWriter`
4. Add array-specific query classes
5. Update codecs for multi-valued storage format
6. Add comprehensive tests and benchmarks

---

**References**:
- Lucene SORTED_SET: `lucene/core/src/java/org/apache/lucene/document/SortedSetDocValuesField.java`
- Lucene SORTED_NUMERIC: `lucene/core/src/java/org/apache/lucene/document/SortedNumericDocValuesField.java`
- ClickHouse Array: `ClickHouse/src/DataTypes/DataTypeArray.h`
- ClickHouse ColumnArray: `ClickHouse/src/Columns/ColumnArray.h`
