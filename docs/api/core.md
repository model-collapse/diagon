# Core API Reference

This document covers the core APIs for indexing and searching in Diagon Lib.

## Table of Contents

- [IndexWriter](#indexwriter)
- [IndexReader](#indexreader)
- [DirectoryReader](#directoryreader)
- [IndexSearcher](#indexsearcher)
- [Document](#document)
- [Directory](#directory)

---

## IndexWriter

`IndexWriter` is the main class for creating and updating indexes.

### Header

```cpp
#include <diagon/index/IndexWriter.h>
```

### Creating an IndexWriter

```cpp
#include <diagon/store/FSDirectory.h>
#include <diagon/index/IndexWriter.h>

using namespace diagon;

// Open directory
auto dir = store::FSDirectory::open("/path/to/index");

// Configure writer
index::IndexWriterConfig config;
config.setRAMBufferSizeMB(256);           // RAM buffer size
config.setMaxBufferedDocs(10000);          // Max docs before flush
config.setMergePolicy(MergePolicy::TIERED); // Merge strategy

// Create writer
auto writer = index::IndexWriter::create(dir.get(), config);
```

### IndexWriterConfig

Configuration options for `IndexWriter`:

| Method | Description | Default |
|--------|-------------|---------|
| `setRAMBufferSizeMB(size_t mb)` | RAM buffer size before flush | 16 MB |
| `setMaxBufferedDocs(size_t n)` | Max documents before flush | 10,000 |
| `setMergePolicy(MergePolicy)` | Segment merge strategy | TIERED |
| `setCodec(std::unique_ptr<Codec>)` | Pluggable codec | Lucene99Codec |
| `setUseCompoundFile(bool)` | Use compound file format | true |
| `setOpenMode(OpenMode)` | CREATE, APPEND, CREATE_OR_APPEND | CREATE_OR_APPEND |

### Adding Documents

```cpp
// Single document
index::Document doc;
doc.addField("title", "My Document", index::FieldType::TEXT);
doc.addField("price", 99.99, index::FieldType::NUMERIC);
doc.addField("category", "electronics", index::FieldType::KEYWORD);
writer->addDocument(doc);

// Batch of documents
std::vector<index::Document> docs;
for (int i = 0; i < 1000; i++) {
    index::Document doc;
    doc.addField("id", i, index::FieldType::NUMERIC);
    doc.addField("text", "Document " + std::to_string(i), index::FieldType::TEXT);
    docs.push_back(std::move(doc));
}
writer->addDocuments(docs);
```

### Committing Changes

```cpp
// Commit makes documents searchable
writer->commit();

// With metadata
writer->commit({{"timestamp", "2024-01-24"}, {"author", "system"}});
```

### Updating Documents

```cpp
// Delete by term
writer->deleteDocuments(index::Term("id", "123"));

// Delete by query
auto query = search::TermQuery::create("status", "deleted");
writer->deleteDocuments(query.get());

// Update document (delete + add)
auto updateQuery = search::TermQuery::create("id", "123");
index::Document newDoc;
newDoc.addField("id", 123, index::FieldType::NUMERIC);
newDoc.addField("title", "Updated Title", index::FieldType::TEXT);
writer->updateDocument(updateQuery.get(), newDoc);
```

### Merging Segments

```cpp
// Force merge to N segments
writer->forceMerge(1);  // Merge to single segment

// Force merge with max segment size
writer->forceMerge(5, 1024 * 1024 * 1024);  // Max 5 segments, 1GB each
```

### Getting Statistics

```cpp
// Number of documents
size_t numDocs = writer->numDocs();

// Number of deleted documents
size_t numDeletes = writer->numDeletedDocs();

// RAM usage
size_t ramUsed = writer->ramBytesUsed();

// Check if pending changes
bool hasPendingChanges = writer->hasUncommittedChanges();
```

### Closing

```cpp
// Close writer (commits automatically if changes exist)
writer->close();

// Rollback uncommitted changes
writer->rollback();
```

---

## IndexReader

Abstract base class for reading indexes. Use `DirectoryReader` for concrete implementation.

### Header

```cpp
#include <diagon/index/IndexReader.h>
#include <diagon/index/DirectoryReader.h>
```

### Reader Hierarchy

```
IndexReader (abstract)
    ├── CompositeReader (abstract)
    │   └── DirectoryReader
    └── LeafReader (abstract)
        ├── SegmentReader
        └── FilterLeafReader
```

### Opening a Reader

```cpp
// Open from directory
auto reader = index::DirectoryReader::open(dir.get());

// Open from writer (near-real-time)
auto reader = index::DirectoryReader::open(writer.get());

// Open from previous reader (incremental reopen)
auto newReader = index::DirectoryReader::openIfChanged(reader.get());
if (newReader) {
    reader = std::move(newReader);  // Use new reader
}
```

### Reader Methods

```cpp
// Document count
size_t numDocs = reader->numDocs();
size_t maxDoc = reader->maxDoc();  // Including deleted docs
size_t numDeletedDocs = reader->numDeletedDocs();

// Check if document is deleted
bool isDeleted = reader->isDeleted(docID);

// Access segments (for CompositeReader)
const auto& leaves = reader->leaves();
for (const auto& leaf : leaves) {
    index::LeafReader* leafReader = leaf.reader();
    std::cout << "Segment: " << leafReader->numDocs() << " docs\n";
}

// Close reader
reader->close();
```

---

## DirectoryReader

Concrete implementation of `CompositeReader` for reading directory-based indexes.

### Opening Options

```cpp
// Standard open
auto reader = index::DirectoryReader::open(dir.get());

// Open with specific commit
auto commit = index::IndexCommit::getLatestCommit(dir.get());
auto reader = index::DirectoryReader::open(commit.get());

// Open from writer (NRT)
auto reader = index::DirectoryReader::open(writer.get());
```

### Near-Real-Time (NRT) Search

```cpp
// Open NRT reader
auto reader = index::DirectoryReader::open(writer.get());
auto searcher = std::make_unique<search::IndexSearcher>(reader.get());

// ... later, after more indexing ...

// Reopen to see new documents
auto newReader = index::DirectoryReader::openIfChanged(reader.get());
if (newReader) {
    reader = std::move(newReader);
    searcher = std::make_unique<search::IndexSearcher>(reader.get());
}
```

### Index Statistics

```cpp
// Version (changes with each commit)
int64_t version = reader->getVersion();

// Check if current
bool isCurrent = reader->isCurrent();

// Get commit data
auto commitData = reader->getIndexCommit()->getUserData();
for (const auto& [key, value] : commitData) {
    std::cout << key << " = " << value << "\n";
}
```

---

## IndexSearcher

Main class for executing searches against an index.

### Header

```cpp
#include <diagon/search/IndexSearcher.h>
```

### Creating a Searcher

```cpp
auto reader = index::DirectoryReader::open(dir.get());
search::IndexSearcher searcher(reader.get());

// With custom executor for parallel search
auto executor = std::make_shared<ThreadPoolExecutor>(8);
search::IndexSearcher searcher(reader.get(), executor);
```

### Basic Search

```cpp
// Create query
auto query = search::TermQuery::create("title", "search");

// Search with top-N results
auto results = searcher.search(query.get(), 10);

std::cout << "Total hits: " << results.totalHits << "\n";
for (const auto& hit : results.scoreDocs) {
    std::cout << "Doc " << hit.doc << " score=" << hit.score << "\n";
}
```

### Search with Filters

```cpp
// Numeric range filter
auto filter = search::RangeFilter::create("price", 10.0, 100.0);
auto results = searcher.search(query.get(), filter.get(), 10);

// Multiple filters (AND)
auto filter1 = search::TermFilter::create("category", "books");
auto filter2 = search::RangeFilter::create("year", 2020, 2024);
auto combined = search::BooleanFilter::create({filter1.get(), filter2.get()});
auto results = searcher.search(query.get(), combined.get(), 10);
```

### Boolean Queries

```cpp
using namespace search;

// Create boolean query
auto boolQuery = BooleanQuery::Builder()
    .add(TermQuery::create("title", "search"), BooleanClause::MUST)
    .add(TermQuery::create("title", "engine"), BooleanClause::SHOULD)
    .add(TermQuery::create("category", "spam"), BooleanClause::MUST_NOT)
    .build();

auto results = searcher.search(boolQuery.get(), 10);
```

### Phrase Queries

```cpp
// Exact phrase
auto phrase = search::PhraseQuery::create("content", {"search", "engine"});

// With slop (allow gaps)
auto phraseWithSlop = search::PhraseQuery::create(
    "content", {"search", "engine"}, 2  // Allow up to 2 words between
);

auto results = searcher.search(phrase.get(), 10);
```

### Retrieving Documents

```cpp
// Get full document
auto doc = searcher.doc(docID);
std::string title = doc->get("title");
double price = doc->getNumeric("price");

// Get specific fields only
std::set<std::string> fields = {"title", "price"};
auto doc = searcher.doc(docID, fields);
```

### Explain Scores

```cpp
// Explain why a document matched with specific score
auto explanation = searcher.explain(query.get(), docID);
std::cout << explanation->toString() << "\n";
```

### Custom Scoring

```cpp
// Use custom similarity
searcher.setSimilarity(std::make_unique<BM25Similarity>(1.5f, 0.8f));

// Or per-field similarities
auto perFieldSim = std::make_unique<PerFieldSimilarity>();
perFieldSim->set("title", std::make_unique<BM25Similarity>(2.0f, 0.75f));
perFieldSim->set("content", std::make_unique<BM25Similarity>(1.2f, 0.75f));
searcher.setSimilarity(std::move(perFieldSim));
```

---

## Document

Represents a document with multiple fields.

### Header

```cpp
#include <diagon/index/Document.h>
```

### Creating Documents

```cpp
index::Document doc;

// Add text field (tokenized, searchable)
doc.addField("title", "Search Engine Design", index::FieldType::TEXT);
doc.addField("content", "Full text content...", index::FieldType::TEXT);

// Add numeric fields
doc.addField("price", 99.99, index::FieldType::NUMERIC);
doc.addField("quantity", 42, index::FieldType::NUMERIC);
doc.addField("timestamp", 1234567890L, index::FieldType::NUMERIC);

// Add keyword field (not tokenized, exact match)
doc.addField("category", "books", index::FieldType::KEYWORD);
doc.addField("isbn", "978-0-123456-78-9", index::FieldType::KEYWORD);
```

### Field Types

```cpp
enum class FieldType {
    TEXT,      // Tokenized, searchable text
    NUMERIC,   // Numeric value (int, long, float, double)
    KEYWORD    // Exact-match string (not tokenized)
};
```

### Retrieving Field Values

```cpp
// Get string field
std::string title = doc->get("title");

// Get numeric field
double price = doc->getNumeric("price");
int quantity = doc->getNumericInt("quantity");
int64_t timestamp = doc->getNumericLong("timestamp");

// Get all values for a field
std::vector<std::string> categories = doc->getValues("category");

// Check if field exists
if (doc->hasField("price")) {
    // ...
}
```

### Multi-Valued Fields

```cpp
// Add multiple values for same field
doc.addField("tags", "cpp", index::FieldType::KEYWORD);
doc.addField("tags", "search", index::FieldType::KEYWORD);
doc.addField("tags", "lucene", index::FieldType::KEYWORD);

// Retrieve all values
std::vector<std::string> tags = doc->getValues("tags");
```

---

## Directory

Abstract storage layer for index files.

### Header

```cpp
#include <diagon/store/Directory.h>
#include <diagon/store/FSDirectory.h>
#include <diagon/store/MMapDirectory.h>
#include <diagon/store/RAMDirectory.h>
```

### Directory Implementations

```cpp
// File system directory (standard I/O)
auto dir = store::FSDirectory::open("/path/to/index");

// Memory-mapped directory (better for large files)
auto dir = store::MMapDirectory::open("/path/to/index");

// In-memory directory (for testing)
auto dir = std::make_unique<store::RAMDirectory>();
```

### Directory Operations

```cpp
// List files
std::vector<std::string> files = dir->listAll();

// File size
size_t size = dir->fileLength("segments_1");

// Delete file
dir->deleteFile("old_segment.del");

// Rename file
dir->rename("temp.tmp", "final.dat");

// Check if file exists
if (dir->fileExists("segments_N")) {
    // ...
}

// Sync files to disk
dir->sync({file1, file2, file3});
```

### Creating Index Input/Output

```cpp
// Read from file
auto input = dir->openInput("segments_1");
int32_t value = input->readInt();
std::string str = input->readString();

// Write to file
auto output = dir->createOutput("new_file.dat");
output->writeInt(42);
output->writeString("hello");
output->close();
```

---

## Complete Example

Here's a complete example combining all core APIs:

```cpp
#include <diagon/index/IndexWriter.h>
#include <diagon/index/DirectoryReader.h>
#include <diagon/search/IndexSearcher.h>
#include <diagon/store/FSDirectory.h>
#include <iostream>

using namespace diagon;

int main() {
    try {
        // Create directory
        auto dir = store::FSDirectory::open("/tmp/demo_index");

        // Configure and create writer
        index::IndexWriterConfig config;
        config.setRAMBufferSizeMB(128);
        auto writer = index::IndexWriter::create(dir.get(), config);

        // Add 1000 documents
        for (int i = 0; i < 1000; i++) {
            index::Document doc;
            doc.addField("id", i, index::FieldType::NUMERIC);
            doc.addField("title", "Document " + std::to_string(i),
                        index::FieldType::TEXT);
            doc.addField("price", 10.0 + (i % 100),
                        index::FieldType::NUMERIC);
            doc.addField("category",
                        (i % 3 == 0) ? "books" : "electronics",
                        index::FieldType::KEYWORD);
            writer->addDocument(doc);
        }

        writer->commit();
        std::cout << "Indexed " << writer->numDocs() << " documents\n";

        // Open NRT reader
        auto reader = index::DirectoryReader::open(writer.get());
        search::IndexSearcher searcher(reader.get());

        // Search with filter
        auto query = search::TermQuery::create("category", "books");
        auto filter = search::RangeFilter::create("price", 50.0, 80.0);
        auto results = searcher.search(query.get(), filter.get(), 10);

        std::cout << "Found " << results.totalHits << " results\n";
        for (const auto& hit : results.scoreDocs) {
            auto doc = searcher.doc(hit.doc);
            std::cout << "  " << doc->get("title")
                     << " ($" << doc->getNumeric("price") << ")\n";
        }

        // Update a document
        auto updateQuery = search::TermQuery::create("id", "42");
        index::Document newDoc;
        newDoc.addField("id", 42, index::FieldType::NUMERIC);
        newDoc.addField("title", "Updated Document 42",
                       index::FieldType::TEXT);
        newDoc.addField("price", 99.99, index::FieldType::NUMERIC);
        newDoc.addField("category", "books", index::FieldType::KEYWORD);
        writer->updateDocument(updateQuery.get(), newDoc);
        writer->commit();

        // Reopen reader to see update
        auto newReader = index::DirectoryReader::openIfChanged(reader.get());
        if (newReader) {
            reader = std::move(newReader);
            std::cout << "Reader reopened with updates\n";
        }

        // Clean up
        reader->close();
        writer->close();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
```

## See Also

- [Search API Reference](search.md)
- [Query Types Guide](../guides/searching.md)
- [Performance Tuning](../guides/performance.md)
- [Architecture Overview](../../design/00_ARCHITECTURE_OVERVIEW.md)
