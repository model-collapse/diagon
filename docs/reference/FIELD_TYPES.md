# Field Types Reference

Complete reference for supported field types in Diagon.

## Overview

Diagon provides three main field classes for different use cases:
- **TextField**: Full-text searchable fields (tokenized)
- **StringField**: Exact-match keyword fields (not tokenized)
- **NumericDocValuesField**: Numeric values for filtering and sorting

All fields are based on `IndexableField` with configurable `FieldType` properties.

---

## TextField

Full-text searchable fields that are tokenized for text search.

### Usage

```cpp
#include <diagon/document/Field.h>

// Stored TextField (value retrievable after indexing)
auto titleField = std::make_unique<diagon::document::TextField>(
    "title", "Search Engine Design", true);  // stored=true

// Not-stored TextField (only searchable, not retrievable)
auto contentField = std::make_unique<diagon::document::TextField>(
    "content", "Full text content...", false);  // stored=false
```

### Characteristics

| Property | Value |
|----------|-------|
| **Tokenized** | ✅ Yes - split into individual terms |
| **Indexed** | ✅ Yes - searchable with text queries |
| **Scored** | ✅ Yes - BM25 scoring for relevance |
| **Phrase Queries** | ✅ Yes - position information stored |
| **Range Queries** | ❌ No - use NumericDocValuesField |
| **Exact Match** | ❌ No - use StringField |
| **Filtering** | ⚠️ Not recommended - use StringField or NumericDocValuesField |
| **Sorting** | ❌ No - use NumericDocValuesField |

### Index Options

TextField uses `IndexOptions::DOCS_AND_FREQS_AND_POSITIONS` which stores:
- Document IDs containing the term
- Term frequencies within each document
- Term positions (for phrase queries)

### Storage Options

```cpp
// Predefined types
TextField::TYPE_STORED      // indexed=true, stored=true
TextField::TYPE_NOT_STORED  // indexed=true, stored=false

// Stored text (can retrieve original value)
TextField field("title", "My Title", true);

// Not stored (search only, cannot retrieve)
TextField field("content", "Long content...", false);
```

### Use Cases

- Article titles and content
- Product descriptions
- Comments and reviews
- Email bodies
- Log messages
- Any text that needs full-text search

### Example

```cpp
using namespace diagon::document;

// Create document
Document doc;

// Add stored title (searchable + retrievable)
auto title = std::make_unique<TextField>("title", "Lucene Search", true);
doc.addField(std::move(title));

// Add not-stored content (searchable only)
auto content = std::make_unique<TextField>("content",
    "Apache Lucene is a search library...", false);
doc.addField(std::move(content));

// Query
auto query = TermQuery::create("title", "lucene");
auto results = searcher.search(query.get(), 10);
```

---

## StringField

Exact-match keyword fields that are NOT tokenized.

### Usage

```cpp
#include <diagon/document/Field.h>

// Stored keyword (exact match + retrievable)
auto categoryField = std::make_unique<diagon::document::StringField>(
    "category", "electronics", true);  // stored=true

// Not-stored keyword (exact match only)
auto idField = std::make_unique<diagon::document::StringField>(
    "id", "SKU-12345", false);  // stored=false
```

### Characteristics

| Property | Value |
|----------|-------|
| **Tokenized** | ❌ No - stored as single term |
| **Indexed** | ✅ Yes - searchable with exact match |
| **Scored** | ❌ No - binary match/no-match |
| **Exact Match** | ✅ Yes - entire value must match |
| **Range Queries** | ❌ No - use NumericDocValuesField |
| **Filtering** | ✅ Yes - fast exact-match filtering |
| **Sorting** | ⚠️ Limited - not ideal for sorting |
| **Aggregations** | ✅ Yes - faceting, grouping |

### Index Options

StringField uses `IndexOptions::DOCS` which stores:
- Document IDs containing the term
- No frequencies (not scored)
- No positions (no phrase queries)

### Storage Options

```cpp
// Predefined types
StringField::TYPE_STORED      // indexed=true, stored=true
StringField::TYPE_NOT_STORED  // indexed=true, stored=false

// Stored (exact match + retrievable)
StringField field("category", "electronics", true);

// Not stored (exact match only)
StringField field("status", "active", false);
```

### Use Cases

- Categories, tags
- Product IDs, SKUs
- UUIDs, unique identifiers
- Email addresses
- URLs
- Status flags ("active", "inactive")
- Country codes ("US", "UK")
- Filenames, paths

### Example

```cpp
using namespace diagon::document;

// Create product document
Document doc;

// Category (stored keyword)
auto category = std::make_unique<StringField>("category", "electronics", true);
doc.addField(std::move(category));

// Brand (stored keyword)
auto brand = std::make_unique<StringField>("brand", "Acme Corp", true);
doc.addField(std::move(brand));

// SKU (not stored - just for filtering)
auto sku = std::make_unique<StringField>("sku", "SKU-12345", false);
doc.addField(std::move(sku));

// Exact match query
auto query = TermQuery::create("category", "electronics");
auto results = searcher.search(query.get(), 10);
```

---

## NumericDocValuesField

Numeric values for range queries, filtering, and sorting.

### Usage

```cpp
#include <diagon/document/Field.h>

// Integer values
auto priceField = std::make_unique<diagon::document::NumericDocValuesField>(
    "price", 9999);  // stored as int64_t (in cents)

auto quantityField = std::make_unique<diagon::document::NumericDocValuesField>(
    "quantity", 42);

auto timestampField = std::make_unique<diagon::document::NumericDocValuesField>(
    "timestamp", 1234567890L);
```

### Characteristics

| Property | Value |
|----------|-------|
| **Tokenized** | ❌ No - stored as numeric value |
| **Indexed** | ⚠️ Not in inverted index |
| **Scored** | ❌ No - not used for text scoring |
| **Range Queries** | ✅ Yes - price >= 10 AND price <= 100 |
| **Exact Match** | ✅ Yes - quantity = 42 |
| **Filtering** | ✅ Yes - fast numeric filtering |
| **Sorting** | ✅ Yes - optimized for sorting |
| **Aggregations** | ✅ Yes - sum, avg, min, max |

### Data Type

- Stored as `int64_t` (64-bit signed integer)
- For floating-point values, scale by factor:
  - Prices: multiply by 100 or 10000 (cents/fractional cents)
  - Ratings: multiply by 10 or 100
- For very large integers, use as-is

### Storage

- Always stored in columnar format (not in inverted index)
- Column-oriented storage optimized for fast filtering
- Uses DocValues (always retrievable)

### Use Cases

- Prices, costs
- Quantities, counts
- Ratings, scores
- Timestamps, dates (as Unix epoch)
- IDs (numeric identifiers)
- Geographic coordinates (scaled to integers)
- File sizes

### Example

```cpp
using namespace diagon::document;

// Create product document
Document doc;

// Text fields
auto title = std::make_unique<TextField>("title", "Widget", true);
doc.addField(std::move(title));

// Numeric fields (scaled for precision)
auto price = std::make_unique<NumericDocValuesField>("price", 9999);  // $99.99
auto rating = std::make_unique<NumericDocValuesField>("rating", 45);  // 4.5 stars
auto stock = std::make_unique<NumericDocValuesField>("stock", 42);
auto timestamp = std::make_unique<NumericDocValuesField>("timestamp",
    std::time(nullptr));

doc.addField(std::move(price));
doc.addField(std::move(rating));
doc.addField(std::move(stock));
doc.addField(std::move(timestamp));

// Range query: price between $50 and $150
auto filter = RangeFilter::create("price", 5000, 15000);
auto results = searcher.search(query.get(), filter.get(), 10);

// Sort by price
auto sort = Sort::byField("price", Sort::DESCENDING);
auto results = searcher.search(query.get(), 10, sort);
```

---

## FieldType Configuration

All field classes use `FieldType` struct for configuration:

```cpp
struct FieldType {
    index::IndexOptions indexOptions;  // What to index
    index::DocValuesType docValuesType;  // Column storage
    bool stored;      // Store original value
    bool tokenized;   // Apply tokenization
    bool omitNorms;   // Omit length normalization
};
```

### IndexOptions

Controls what information is stored in the inverted index:

```cpp
enum class IndexOptions {
    NONE,                         // Not indexed
    DOCS,                         // Doc IDs only (StringField)
    DOCS_AND_FREQS,              // Doc IDs + term frequencies
    DOCS_AND_FREQS_AND_POSITIONS // Doc IDs + freqs + positions (TextField)
};
```

### DocValuesType

Controls column storage format:

```cpp
enum class DocValuesType {
    NONE,      // No column storage
    NUMERIC,   // Numeric column (NumericDocValuesField)
    BINARY,    // Binary column (not yet supported)
    SORTED,    // Sorted strings (not yet supported)
    SORTED_SET // Multi-valued sorted strings (not yet supported)
};
```

### Custom FieldType

```cpp
// Custom field type
FieldType customType;
customType.indexOptions = IndexOptions::DOCS_AND_FREQS;
customType.docValuesType = DocValuesType::NONE;
customType.stored = true;
customType.tokenized = true;
customType.omitNorms = true;  // Skip length normalization

TextField customField("field", "value", customType);
```

---

## Multi-Valued Fields

All field types support multiple values:

```cpp
Document doc;

// Multiple categories
doc.addField(std::make_unique<StringField>("category", "electronics", false));
doc.addField(std::make_unique<StringField>("category", "computers", false));
doc.addField(std::make_unique<StringField>("category", "laptops", false));

// Multiple authors
doc.addField(std::make_unique<TextField>("author", "John Doe", true));
doc.addField(std::make_unique<TextField>("author", "Jane Smith", true));

// Multiple ratings (not recommended - use single avg/sum)
doc.addField(std::make_unique<NumericDocValuesField>("ratings", 5));
doc.addField(std::make_unique<NumericDocValuesField>("ratings", 4));
```

---

## Field Type Selection Guide

| Use Case | Field Type | Example |
|----------|------------|---------|
| Full-text search | TextField | Article content, descriptions |
| Exact match | StringField | Categories, tags, IDs |
| Range queries | NumericDocValuesField | Prices, timestamps |
| Filtering | StringField or NumericDocValuesField | Status, category, price range |
| Sorting | NumericDocValuesField | Price, date, rating |
| Aggregations | StringField or NumericDocValuesField | Category counts, price stats |
| IDs (string) | StringField | UUIDs, SKUs |
| IDs (numeric) | NumericDocValuesField | Auto-increment IDs |
| Timestamps | NumericDocValuesField | Created date, modified date |
| Geographic coordinates | NumericDocValuesField | Latitude, longitude (scaled) |

---

## Comparison Table

| Feature | TextField | StringField | NumericDocValuesField |
|---------|-----------|-------------|------------------------|
| **Tokenized** | ✅ Yes | ❌ No | ❌ No |
| **Full-text search** | ✅ Yes | ❌ No | ❌ No |
| **Exact match** | ❌ No | ✅ Yes | ✅ Yes |
| **Range queries** | ❌ No | ❌ No | ✅ Yes |
| **Scoring (BM25)** | ✅ Yes | ❌ No | ❌ No |
| **Filtering** | ⚠️ Not recommended | ✅ Yes | ✅ Yes |
| **Sorting** | ❌ No | ⚠️ Limited | ✅ Yes |
| **Aggregation** | ❌ No | ✅ Yes | ✅ Yes |
| **Phrase queries** | ✅ Yes | ❌ No | ❌ No |
| **Stored option** | ✅ Yes | ✅ Yes | ✅ Always (DocValues) |

---

## Common Patterns

### E-commerce Product

```cpp
Document createProduct(const Product& p) {
    Document doc;

    // Text search
    doc.addField(std::make_unique<TextField>("title", p.title, true));
    doc.addField(std::make_unique<TextField>("description", p.desc, false));

    // Exact match / filtering
    doc.addField(std::make_unique<StringField>("category", p.category, true));
    doc.addField(std::make_unique<StringField>("brand", p.brand, true));
    doc.addField(std::make_unique<StringField>("sku", p.sku, false));

    // Numeric filtering / sorting
    doc.addField(std::make_unique<NumericDocValuesField>("price", p.price * 100));
    doc.addField(std::make_unique<NumericDocValuesField>("rating", p.rating * 10));
    doc.addField(std::make_unique<NumericDocValuesField>("stock", p.stock));

    return doc;
}
```

### Log Entry

```cpp
Document createLog(const LogEntry& log) {
    Document doc;

    // Text search
    doc.addField(std::make_unique<TextField>("message", log.message, true));
    if (!log.exception.empty()) {
        doc.addField(std::make_unique<TextField>("exception", log.exception, true));
    }

    // Exact match / filtering
    doc.addField(std::make_unique<StringField>("level", log.level, true));
    doc.addField(std::make_unique<StringField>("logger", log.logger, false));
    doc.addField(std::make_unique<StringField>("thread", log.thread, false));

    // Numeric filtering / sorting
    doc.addField(std::make_unique<NumericDocValuesField>("timestamp", log.timestamp));

    return doc;
}
```

### Blog Post

```cpp
Document createBlogPost(const BlogPost& post) {
    Document doc;

    // Text search
    doc.addField(std::make_unique<TextField>("title", post.title, true));
    doc.addField(std::make_unique<TextField>("content", post.content, false));
    doc.addField(std::make_unique<TextField>("author", post.author, true));

    // Tags (multi-valued)
    for (const auto& tag : post.tags) {
        doc.addField(std::make_unique<StringField>("tags", tag, false));
    }

    // Exact match / filtering
    doc.addField(std::make_unique<StringField>("category", post.category, true));
    doc.addField(std::make_unique<StringField>("status", post.status, false));

    // Numeric filtering / sorting
    doc.addField(std::make_unique<NumericDocValuesField>("timestamp", post.timestamp));
    doc.addField(std::make_unique<NumericDocValuesField>("views", post.views));
    doc.addField(std::make_unique<NumericDocValuesField>("likes", post.likes));

    return doc;
}
```

---

## See Also

- [Indexing Guide](../guides/indexing.md) - Complete indexing workflow
- [Searching Guide](../guides/searching.md) - Querying different field types
- [Core API Reference](../api/core.md) - Complete API documentation
- [Performance Guide](../guides/performance.md) - Field type performance characteristics
