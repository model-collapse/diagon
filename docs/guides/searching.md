# Searching Guide

Comprehensive guide to querying and searching in Diagon.

## Table of Contents

- [Getting Started](#getting-started)
- [Opening a Reader](#opening-a-reader)
- [IndexSearcher Basics](#indexsearcher-basics)
- [Query Types](#query-types)
- [Filters](#filters)
- [Boolean Queries](#boolean-queries)
- [Phrase Queries](#phrase-queries)
- [Scoring and Ranking](#scoring-and-ranking)
- [Pagination](#pagination)
- [Result Processing](#result-processing)
- [Best Practices](#best-practices)

---

## Getting Started

### Basic Search Flow

```cpp
#include <diagon/search/IndexSearcher.h>
#include <diagon/index/DirectoryReader.h>
#include <diagon/store/FSDirectory.h>

using namespace diagon;

// 1. Open directory
auto dir = store::FSDirectory::open("/path/to/index");

// 2. Open reader
auto reader = index::DirectoryReader::open(dir.get());

// 3. Create searcher
search::IndexSearcher searcher(reader.get());

// 4. Create query
auto query = search::TermQuery::create("title", "search");

// 5. Execute search
auto results = searcher.search(query.get(), 10);

// 6. Process results
for (const auto& hit : results.scoreDocs) {
    auto doc = searcher.doc(hit.doc);
    std::cout << doc->get("title") << " (score: "
              << hit.score << ")\n";
}

// 7. Close reader
reader->close();
```

---

## Opening a Reader

### Standard Open

```cpp
auto dir = store::FSDirectory::open("/path/to/index");
auto reader = index::DirectoryReader::open(dir.get());
```

### Near-Real-Time (NRT) Reader

Open reader that sees uncommitted changes:

```cpp
// Open NRT reader from writer
auto reader = index::DirectoryReader::open(writer.get());

// After more indexing, reopen to see new docs
auto newReader = index::DirectoryReader::openIfChanged(reader.get());
if (newReader) {
    reader->close();
    reader = std::move(newReader);
}
```

**Use case**: Search while indexing, minimal latency

### Reader Reuse

Keep readers open and refresh periodically:

```cpp
class ReaderManager {
    std::unique_ptr<index::DirectoryReader> reader_;
    std::chrono::steady_clock::time_point last_refresh_;
    std::chrono::seconds refresh_interval_;

public:
    ReaderManager(store::Directory* dir, std::chrono::seconds interval = 60s)
        : refresh_interval_(interval)
    {
        reader_ = index::DirectoryReader::open(dir);
        last_refresh_ = std::chrono::steady_clock::now();
    }

    index::DirectoryReader* getReader() {
        auto now = std::chrono::steady_clock::now();
        if (now - last_refresh_ >= refresh_interval_) {
            maybeRefresh();
        }
        return reader_.get();
    }

    void maybeRefresh() {
        auto newReader = index::DirectoryReader::openIfChanged(reader_.get());
        if (newReader) {
            reader_->close();
            reader_ = std::move(newReader);
            last_refresh_ = std::chrono::steady_clock::now();
        }
    }
};
```

---

## IndexSearcher Basics

### Creating a Searcher

```cpp
auto reader = index::DirectoryReader::open(dir.get());
search::IndexSearcher searcher(reader.get());
```

### With Thread Pool (Parallel Search)

```cpp
auto executor = std::make_shared<ThreadPoolExecutor>(8);
search::IndexSearcher searcher(reader.get(), executor);
```

### Basic Search

```cpp
// Search for top 10 results
auto results = searcher.search(query.get(), 10);

// Search for top 100 results
auto results = searcher.search(query.get(), 100);
```

### Search with Filter

```cpp
auto query = search::TermQuery::create("title", "search");
auto filter = search::RangeFilter::create("price", 10.0, 100.0);

auto results = searcher.search(query.get(), filter.get(), 10);
```

---

## Query Types

### TermQuery

Exact term match (after tokenization).

```cpp
// Find documents with "search" in title
auto query = search::TermQuery::create("title", "search");

// Case-sensitive keyword match
auto query = search::TermQuery::create("category", "Electronics");
```

**Use cases**:
- Exact term matching
- Keyword field queries
- ID lookups (after tokenization)

### RangeQuery

Numeric or keyword range matching.

```cpp
// Numeric range (inclusive)
auto query = search::RangeQuery::create("price", 10.0, 100.0);

// Date range (unix timestamp)
auto query = search::RangeQuery::create("timestamp",
    start_timestamp, end_timestamp);

// Open-ended ranges
auto query = search::RangeQuery::createMin("price", 50.0);  // price >= 50
auto query = search::RangeQuery::createMax("price", 100.0); // price <= 100
```

**Use cases**:
- Price ranges
- Date/time ranges
- Numeric filters
- Ranges on sorted fields

### MatchAllQuery

Match all documents (useful with filters).

```cpp
// Get all documents
auto query = search::MatchAllQuery::create();

// Get all documents in price range
auto query = search::MatchAllQuery::create();
auto filter = search::RangeFilter::create("price", 10.0, 100.0);
auto results = searcher.search(query.get(), filter.get(), 10);
```

### WildcardQuery (Future Work)

Pattern matching with wildcards.

```cpp
// Future API
auto query = search::WildcardQuery::create("title", "search*");
auto query = search::WildcardQuery::create("email", "*@example.com");
```

### FuzzyQuery (Future Work)

Fuzzy matching (edit distance).

```cpp
// Future API
auto query = search::FuzzyQuery::create("title", "serch", 2);  // Max 2 edits
```

---

## Filters

Filters are non-scoring constraints that prune documents before scoring.

### TermFilter

Exact term matching.

```cpp
auto filter = search::TermFilter::create("category", "electronics");
auto filter = search::TermFilter::create("status", "active");
```

### RangeFilter

Numeric or keyword ranges.

```cpp
// Price range
auto filter = search::RangeFilter::create("price", 10.0, 100.0);

// Date range
auto filter = search::RangeFilter::create("year", 2020, 2024);

// Open-ended
auto filter = search::RangeFilter::createMin("rating", 4.0);
```

### BooleanFilter

Combine multiple filters.

```cpp
// AND: Must satisfy all
auto filter1 = search::TermFilter::create("category", "books");
auto filter2 = search::RangeFilter::create("price", 0.0, 50.0);
auto combined = search::BooleanFilter::create({filter1.get(), filter2.get()},
                                             search::BooleanClause::MUST);

// OR: Must satisfy at least one
auto combined = search::BooleanFilter::create({filter1.get(), filter2.get()},
                                             search::BooleanClause::SHOULD);

// NOT: Must not satisfy
auto combined = search::BooleanFilter::create({filter1.get()},
                                             search::BooleanClause::MUST_NOT);
```

### When to Use Filters vs Queries

**Use Filters**:
- ✅ High selectivity (filters out many docs)
- ✅ Non-scoring constraints (price, date, category)
- ✅ Cacheable conditions
- ✅ Frequently reused

```cpp
// Good: Filter on category, search on text
auto query = search::TermQuery::create("title", "laptop");
auto filter = search::TermFilter::create("category", "electronics");
results = searcher.search(query.get(), filter.get(), 10);
```

**Use Boolean Queries**:
- ✅ Scoring matters for all clauses
- ✅ Low selectivity (keeps most docs)
- ✅ Complex relevance logic

```cpp
// Good: All clauses contribute to score
auto boolQuery = search::BooleanQuery::Builder()
    .add(search::TermQuery::create("title", "laptop"), MUST)
    .add(search::TermQuery::create("title", "gaming"), SHOULD)  // Boost
    .add(search::TermQuery::create("description", "portable"), SHOULD)
    .build();
```

---

## Boolean Queries

Combine multiple queries with boolean logic.

### Boolean Clauses

| Clause | Meaning | Scoring |
|--------|---------|---------|
| `MUST` | Must match | ✅ Contributes to score |
| `SHOULD` | Optional, boosts score | ✅ Contributes to score |
| `MUST_NOT` | Must not match | ❌ No scoring |
| `FILTER` | Must match (no scoring) | ❌ No scoring |

### Basic Boolean Query

```cpp
using namespace search;

// title must contain "laptop", category must be "electronics"
auto query = BooleanQuery::Builder()
    .add(TermQuery::create("title", "laptop"), BooleanClause::MUST)
    .add(TermQuery::create("category", "electronics"), BooleanClause::MUST)
    .build();
```

### SHOULD Clauses (Boosting)

```cpp
// Must match "laptop", prefer "gaming" or "ultrabook"
auto query = BooleanQuery::Builder()
    .add(TermQuery::create("title", "laptop"), BooleanClause::MUST)
    .add(TermQuery::create("title", "gaming"), BooleanClause::SHOULD)
    .add(TermQuery::create("title", "ultrabook"), BooleanClause::SHOULD)
    .build();

// Documents with "gaming" or "ultrabook" score higher
```

### MUST_NOT (Exclusion)

```cpp
// Laptops but not refurbished
auto query = BooleanQuery::Builder()
    .add(TermQuery::create("title", "laptop"), BooleanClause::MUST)
    .add(TermQuery::create("condition", "refurbished"), BooleanClause::MUST_NOT)
    .build();
```

### Minimum Should Match

```cpp
// At least 2 of the SHOULD clauses must match
auto query = BooleanQuery::Builder()
    .add(TermQuery::create("title", "laptop"), BooleanClause::SHOULD)
    .add(TermQuery::create("title", "gaming"), BooleanClause::SHOULD)
    .add(TermQuery::create("title", "ultrabook"), BooleanClause::SHOULD)
    .add(TermQuery::create("title", "portable"), BooleanClause::SHOULD)
    .setMinimumNumberShouldMatch(2)
    .build();
```

### Nested Boolean Queries

```cpp
// (title:laptop OR title:notebook) AND price:[0 TO 1000]
auto textQuery = BooleanQuery::Builder()
    .add(TermQuery::create("title", "laptop"), BooleanClause::SHOULD)
    .add(TermQuery::create("title", "notebook"), BooleanClause::SHOULD)
    .build();

auto finalQuery = BooleanQuery::Builder()
    .add(std::move(textQuery), BooleanClause::MUST)
    .add(RangeQuery::create("price", 0.0, 1000.0), BooleanClause::MUST)
    .build();
```

### Complex Example

```cpp
// Search for:
// - Active electronics OR computers
// - Price between $100-$1000
// - NOT clearance items
// - Prefer "gaming" or "professional"

auto categoryQuery = BooleanQuery::Builder()
    .add(TermQuery::create("category", "electronics"), BooleanClause::SHOULD)
    .add(TermQuery::create("category", "computers"), BooleanClause::SHOULD)
    .build();

auto mainQuery = BooleanQuery::Builder()
    .add(std::move(categoryQuery), BooleanClause::MUST)
    .add(TermQuery::create("status", "active"), BooleanClause::MUST)
    .add(RangeQuery::create("price", 100.0, 1000.0), BooleanClause::MUST)
    .add(TermQuery::create("tags", "clearance"), BooleanClause::MUST_NOT)
    .add(TermQuery::create("title", "gaming"), BooleanClause::SHOULD)
    .add(TermQuery::create("title", "professional"), BooleanClause::SHOULD)
    .build();
```

---

## Phrase Queries

Match exact phrases or terms within proximity.

### Exact Phrase

```cpp
// Find exact phrase "search engine"
auto query = search::PhraseQuery::create("content",
    {"search", "engine"});
```

### With Slop (Proximity)

```cpp
// Allow up to 2 words between "search" and "engine"
// Matches: "search engine", "search and engine", "search the best engine"
auto query = search::PhraseQuery::create("content",
    {"search", "engine"}, 2);
```

### Multi-Term Phrases

```cpp
// "distributed search engine system"
auto query = search::PhraseQuery::create("content",
    {"distributed", "search", "engine", "system"});
```

### Phrase Query Examples

```cpp
// Product names (exact match)
auto query = search::PhraseQuery::create("name",
    {"macbook", "pro", "16"});

// Addresses (with slop)
auto query = search::PhraseQuery::create("address",
    {"123", "main", "street"}, 1);

// Technical terms
auto query = search::PhraseQuery::create("content",
    {"machine", "learning"});
```

---

## Scoring and Ranking

### BM25 Scoring (Default)

Diagon uses BM25 by default:

```cpp
// Default BM25 (k1=1.2, b=0.75)
auto similarity = std::make_unique<search::BM25Similarity>();
searcher.setSimilarity(std::move(similarity));
```

### Custom BM25 Parameters

```cpp
// k1: Term frequency saturation (0.0 - 3.0)
// b:  Length normalization (0.0 - 1.0)

// Short documents (tweets, titles)
auto similarity = std::make_unique<search::BM25Similarity>(
    1.2f,  // k1
    0.5f   // b (less length normalization)
);

// Long documents (articles, books)
auto similarity = std::make_unique<search::BM25Similarity>(
    1.2f,  // k1
    0.9f   // b (more length normalization)
);

searcher.setSimilarity(std::move(similarity));
```

### Per-Field Similarity

```cpp
auto perField = std::make_unique<search::PerFieldSimilarity>();

// Title: high importance, standard params
perField->set("title",
    std::make_unique<search::BM25Similarity>(2.0f, 0.75f));

// Content: standard params
perField->set("content",
    std::make_unique<search::BM25Similarity>(1.2f, 0.75f));

// Tags: presence matters more than frequency
perField->set("tags",
    std::make_unique<search::BM25Similarity>(0.8f, 0.0f));

searcher.setSimilarity(std::move(perField));
```

### Query Boosting

```cpp
// Boost title matches
auto titleQuery = search::TermQuery::create("title", "laptop");
titleQuery->setBoost(2.0f);  // 2× importance

// Normal content match
auto contentQuery = search::TermQuery::create("content", "laptop");

auto query = search::BooleanQuery::Builder()
    .add(std::move(titleQuery), BooleanClause::SHOULD)
    .add(std::move(contentQuery), BooleanClause::SHOULD)
    .build();
```

### Explain Scores

Understand why a document matched with a specific score:

```cpp
auto results = searcher.search(query.get(), 10);

for (const auto& hit : results.scoreDocs) {
    auto explanation = searcher.explain(query.get(), hit.doc);
    std::cout << "Doc " << hit.doc << ":\n";
    std::cout << explanation->toString() << "\n\n";
}
```

Example output:
```
Doc 42:
1.234 = score(doc=42, freq=2), computed from:
  0.567 = idf, computed as log(1 + (N - n + 0.5) / (n + 0.5))
  2.176 = tf, computed as freq / (freq + k1 * (1 - b + b * dl / avgdl))
    2 = freq
    1.2 = k1
    0.75 = b
    100 = dl (field length)
    150 = avgdl (average field length)
```

---

## Pagination

### Basic Pagination

```cpp
// Page 1 (first 20 results)
auto results = searcher.search(query.get(), 20);
displayResults(results, 1);

// Page 2 (next 20 results)
auto results = searcher.search(query.get(), 40);  // Get 40 total
displayResults(results, 2);  // Show results 21-40
```

**Problem**: Inefficient for large offsets

### SearchAfter (Recommended)

```cpp
// Page 1
auto results = searcher.search(query.get(), 20);
displayResults(results);

// Page 2 - continue from last result
if (!results.scoreDocs.empty()) {
    auto lastDoc = results.scoreDocs.back();
    auto nextResults = searcher.searchAfter(lastDoc, query.get(), 20);
    displayResults(nextResults);
}
```

**Benefits**:
- ✅ Constant time per page
- ✅ Efficient for deep pagination
- ✅ Correct even if index changes

### Pagination Helper

```cpp
class SearchPaginator {
    search::IndexSearcher& searcher_;
    std::unique_ptr<search::Query> query_;
    size_t page_size_;
    search::ScoreDoc last_doc_;
    bool has_more_;

public:
    SearchPaginator(search::IndexSearcher& searcher,
                   std::unique_ptr<search::Query> query,
                   size_t page_size)
        : searcher_(searcher)
        , query_(std::move(query))
        , page_size_(page_size)
        , last_doc_{-1, 0.0f}
        , has_more_(true) {}

    search::TopDocs nextPage() {
        if (!has_more_) {
            return search::TopDocs{};
        }

        search::TopDocs results;
        if (last_doc_.doc == -1) {
            // First page
            results = searcher_.search(query_.get(), page_size_);
        } else {
            // Subsequent pages
            results = searcher_.searchAfter(last_doc_, query_.get(), page_size_);
        }

        if (!results.scoreDocs.empty()) {
            last_doc_ = results.scoreDocs.back();
            has_more_ = results.scoreDocs.size() == page_size_;
        } else {
            has_more_ = false;
        }

        return results;
    }

    bool hasMore() const { return has_more_; }
};

// Usage
SearchPaginator paginator(searcher, std::move(query), 20);

while (paginator.hasMore()) {
    auto results = paginator.nextPage();
    displayResults(results);

    if (!promptContinue()) break;
}
```

---

## Result Processing

### Accessing Documents

```cpp
auto results = searcher.search(query.get(), 10);

for (const auto& hit : results.scoreDocs) {
    // Full document
    auto doc = searcher.doc(hit.doc);

    std::string title = doc->get("title");
    double price = doc->getNumeric("price");
    std::string category = doc->get("category");

    std::cout << "Doc " << hit.doc << " (score: " << hit.score << ")\n";
    std::cout << "  Title: " << title << "\n";
    std::cout << "  Price: $" << price << "\n";
    std::cout << "  Category: " << category << "\n";
}
```

### Selective Field Loading

```cpp
// Only load specific fields (faster)
std::set<std::string> fields = {"title", "price"};

for (const auto& hit : results.scoreDocs) {
    auto doc = searcher.doc(hit.doc, fields);
    // Only title and price are loaded
}
```

### Highlighting (Future Work)

```cpp
// Future API
auto highlighter = search::Highlighter::create(query.get());

for (const auto& hit : results.scoreDocs) {
    auto doc = searcher.doc(hit.doc);
    std::string content = doc->get("content");

    // Highlight matching terms
    std::string highlighted = highlighter->highlight("content", content);
    std::cout << highlighted << "\n";
}
```

---

## Best Practices

### 1. Reuse Readers

```cpp
// ✅ Good: Reuse reader
auto reader = index::DirectoryReader::open(dir.get());
for (int i = 0; i < 1000; i++) {
    search::IndexSearcher searcher(reader.get());
    searcher.search(query.get(), 10);
}
reader->close();

// ❌ Bad: Open/close for each query
for (int i = 0; i < 1000; i++) {
    auto reader = index::DirectoryReader::open(dir.get());
    search::IndexSearcher searcher(reader.get());
    searcher.search(query.get(), 10);
    reader->close();
}
```

### 2. Use Filters for Constraints

```cpp
// ✅ Good: Filter on non-scoring constraints
auto query = search::TermQuery::create("title", "laptop");
auto filter = search::RangeFilter::create("price", 100.0, 1000.0);
searcher.search(query.get(), filter.get(), 10);

// ❌ Bad: Boolean query for everything
auto query = BooleanQuery::Builder()
    .add(TermQuery::create("title", "laptop"), MUST)
    .add(RangeQuery::create("price", 100.0, 1000.0), MUST)  // Scores unnecessarily
    .build();
```

### 3. Limit Result Set Size

```cpp
// ✅ Good: Get only what you need
searcher.search(query.get(), 20);

// ❌ Bad: Get all results
searcher.search(query.get(), 1000000);
```

### 4. Use SearchAfter for Pagination

```cpp
// ✅ Good: SearchAfter
auto results = searcher.searchAfter(lastDoc, query.get(), 20);

// ❌ Bad: Large offset
auto results = searcher.search(query.get(), 10000);  // Get 10K to show page 500
```

### 5. Cache Query Objects

```cpp
// ✅ Good: Create once, reuse
std::unordered_map<std::string, std::unique_ptr<search::Query>> query_cache;

auto it = query_cache.find("laptop");
if (it == query_cache.end()) {
    it = query_cache.emplace("laptop",
        search::TermQuery::create("title", "laptop")).first;
}

searcher.search(it->second.get(), 10);
```

### 6. Tune BM25 for Your Data

```cpp
// ✅ Good: Tune for your corpus
auto similarity = std::make_unique<search::BM25Similarity>(1.5f, 0.8f);
searcher.setSimilarity(std::move(similarity));

// Test different parameters and measure relevance
```

### 7. Use Explain for Debugging

```cpp
// ✅ Good: Understand scoring
auto explanation = searcher.explain(query.get(), docID);
std::cout << explanation->toString() << "\n";
```

---

## See Also

- [Quick Start Guide](quick-start.md)
- [Indexing Guide](indexing.md)
- [Performance Guide](performance.md)
- [Search API Reference](../api/search.md)
- [Core API Reference](../api/core.md)
