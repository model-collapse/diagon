# Indexing Guide

Comprehensive guide to indexing documents in Diagon.

## Table of Contents

- [Getting Started](#getting-started)
- [Field Types](#field-types)
- [Document Structure](#document-structure)
- [IndexWriter Configuration](#indexwriter-configuration)
- [Batch Indexing](#batch-indexing)
- [Updating Documents](#updating-documents)
- [Deleting Documents](#deleting-documents)
- [Commit Strategies](#commit-strategies)
- [Segment Management](#segment-management)
- [Error Handling](#error-handling)
- [Best Practices](#best-practices)

---

## Getting Started

### Basic Indexing Flow

```cpp
#include <diagon/index/IndexWriter.h>
#include <diagon/store/FSDirectory.h>

using namespace diagon;

// 1. Create/open directory
auto dir = store::FSDirectory::open("/path/to/index");

// 2. Configure writer
index::IndexWriterConfig config;
config.setRAMBufferSizeMB(256);

// 3. Create writer
auto writer = index::IndexWriter::create(dir.get(), config);

// 4. Add documents
index::Document doc;
doc.addField("title", "My Document", index::FieldType::TEXT);
doc.addField("price", 99.99, index::FieldType::NUMERIC);
writer->addDocument(doc);

// 5. Commit
writer->commit();

// 6. Close
writer->close();
```

---

## Field Types

Diagon supports three field types, each optimized for different use cases.

### TEXT Fields

Full-text searchable fields that are tokenized.

```cpp
doc.addField("title", "Search Engine Design", index::FieldType::TEXT);
doc.addField("content", "Full text content...", index::FieldType::TEXT);
doc.addField("description", "Product description", index::FieldType::TEXT);
```

**Characteristics**:
- ✅ Tokenized (split into terms)
- ✅ Searchable with text queries
- ✅ BM25 scored
- ✅ Supports phrase queries
- ❌ Not for exact matching

**Use cases**:
- Article content
- Product descriptions
- Comments and reviews
- Email bodies
- Log messages

### NUMERIC Fields

Numeric values for range queries and filters.

```cpp
// Integer types
doc.addField("quantity", 42, index::FieldType::NUMERIC);
doc.addField("year", 2024, index::FieldType::NUMERIC);

// Floating-point types
doc.addField("price", 99.99, index::FieldType::NUMERIC);
doc.addField("rating", 4.5, index::FieldType::NUMERIC);

// Long integers
doc.addField("timestamp", 1234567890L, index::FieldType::NUMERIC);
doc.addField("file_size", 1073741824L, index::FieldType::NUMERIC);
```

**Characteristics**:
- ✅ Range queries (price >= 10 AND price <= 100)
- ✅ Fast filtering
- ✅ Sorting support
- ✅ Aggregations
- ❌ Not tokenized

**Use cases**:
- Prices, quantities
- Ratings, scores
- Timestamps, dates
- IDs, counts
- Geographic coordinates

### KEYWORD Fields

Exact-match string fields (not tokenized).

```cpp
doc.addField("category", "electronics", index::FieldType::KEYWORD);
doc.addField("brand", "Acme Corp", index::FieldType::KEYWORD);
doc.addField("isbn", "978-0-123456-78-9", index::FieldType::KEYWORD);
doc.addField("status", "active", index::FieldType::KEYWORD);
```

**Characteristics**:
- ✅ Exact matching only
- ✅ Fast filtering
- ✅ Aggregations (faceting)
- ✅ Sorting
- ❌ Not tokenized
- ❌ Not scored

**Use cases**:
- Categories, tags
- IDs, UUIDs
- Email addresses
- URLs
- Status flags
- Country codes

### Field Type Comparison

| Feature | TEXT | NUMERIC | KEYWORD |
|---------|------|---------|---------|
| Tokenized | ✅ | ❌ | ❌ |
| Full-text search | ✅ | ❌ | ❌ |
| Exact match | ❌ | ✅ | ✅ |
| Range queries | ❌ | ✅ | ❌ |
| Scoring (BM25) | ✅ | ❌ | ❌ |
| Filtering | ❌ | ✅ | ✅ |
| Sorting | ❌ | ✅ | ✅ |
| Aggregation | ❌ | ✅ | ✅ |

---

## Document Structure

### Creating Documents

```cpp
index::Document doc;

// Add single field
doc.addField("title", "My Title", index::FieldType::TEXT);

// Add multiple values for same field
doc.addField("tags", "cpp", index::FieldType::KEYWORD);
doc.addField("tags", "search", index::FieldType::KEYWORD);
doc.addField("tags", "lucene", index::FieldType::KEYWORD);

// Mix field types
doc.addField("name", "Product Name", index::FieldType::TEXT);
doc.addField("price", 99.99, index::FieldType::NUMERIC);
doc.addField("category", "electronics", index::FieldType::KEYWORD);
```

### Multi-Valued Fields

Fields can have multiple values:

```cpp
// Tags (keywords)
for (const auto& tag : product.tags) {
    doc.addField("tags", tag, index::FieldType::KEYWORD);
}

// Authors (text)
for (const auto& author : book.authors) {
    doc.addField("author", author, index::FieldType::TEXT);
}

// Categories (keywords)
for (const auto& cat : item.categories) {
    doc.addField("category", cat, index::FieldType::KEYWORD);
}
```

### Document Examples

**E-commerce Product**:

```cpp
index::Document createProductDoc(const Product& product) {
    index::Document doc;

    // Text fields
    doc.addField("title", product.title, index::FieldType::TEXT);
    doc.addField("description", product.description, index::FieldType::TEXT);

    // Numeric fields
    doc.addField("price", product.price, index::FieldType::NUMERIC);
    doc.addField("rating", product.rating, index::FieldType::NUMERIC);
    doc.addField("stock", product.stock, index::FieldType::NUMERIC);

    // Keyword fields
    doc.addField("category", product.category, index::FieldType::KEYWORD);
    doc.addField("brand", product.brand, index::FieldType::KEYWORD);
    doc.addField("sku", product.sku, index::FieldType::KEYWORD);

    // Multi-valued
    for (const auto& tag : product.tags) {
        doc.addField("tags", tag, index::FieldType::KEYWORD);
    }

    return doc;
}
```

**Blog Post**:

```cpp
index::Document createBlogPostDoc(const BlogPost& post) {
    index::Document doc;

    doc.addField("title", post.title, index::FieldType::TEXT);
    doc.addField("content", post.content, index::FieldType::TEXT);
    doc.addField("author", post.author, index::FieldType::TEXT);

    doc.addField("timestamp", post.timestamp, index::FieldType::NUMERIC);
    doc.addField("views", post.views, index::FieldType::NUMERIC);
    doc.addField("likes", post.likes, index::FieldType::NUMERIC);

    doc.addField("category", post.category, index::FieldType::KEYWORD);
    doc.addField("status", post.status, index::FieldType::KEYWORD);

    for (const auto& tag : post.tags) {
        doc.addField("tags", tag, index::FieldType::KEYWORD);
    }

    return doc;
}
```

**Log Entry**:

```cpp
index::Document createLogDoc(const LogEntry& log) {
    index::Document doc;

    doc.addField("message", log.message, index::FieldType::TEXT);
    doc.addField("logger", log.logger, index::FieldType::KEYWORD);

    doc.addField("timestamp", log.timestamp, index::FieldType::NUMERIC);
    doc.addField("level", log.level, index::FieldType::KEYWORD);
    doc.addField("thread", log.thread, index::FieldType::KEYWORD);

    if (!log.exception.empty()) {
        doc.addField("exception", log.exception, index::FieldType::TEXT);
    }

    return doc;
}
```

---

## IndexWriter Configuration

### RAM Buffer Size

Controls memory usage before flushing to disk.

```cpp
config.setRAMBufferSizeMB(256);   // 256 MB
config.setRAMBufferSizeMB(512);   // 512 MB
config.setRAMBufferSizeMB(1024);  // 1 GB
config.setRAMBufferSizeMB(2048);  // 2 GB
```

**Guidelines**:
- Small datasets (<100K docs): 128-256 MB
- Medium datasets (100K-1M docs): 512 MB
- Large datasets (>1M docs): 1-2 GB
- Available memory / 4 is a good rule of thumb

### Max Buffered Documents

Alternative to RAM buffer - flush after N documents.

```cpp
config.setMaxBufferedDocs(10000);  // Flush every 10K docs
```

**Use when**:
- Documents vary greatly in size
- Want predictable flush frequency
- Memory is constrained

### Merge Policy

Controls how segments are merged.

```cpp
// Tiered merge (default) - balanced
config.setMergePolicy(MergePolicy::TIERED);

// Log-byte-size merge - predictable I/O
config.setMergePolicy(MergePolicy::LOG_BYTE_SIZE);

// No merge - fastest indexing
config.setMergePolicy(MergePolicy::NO_MERGE);
```

**Tiered merge configuration**:

```cpp
auto tiered = std::make_unique<TieredMergePolicy>();
tiered->setMaxMergedSegmentMB(5000);     // Max segment: 5GB
tiered->setSegmentsPerTier(10);          // Merge factor
tiered->setMaxMergeAtOnce(10);           // Concurrent merges
tiered->setMaxMergeAtOnceExplicit(30);   // For forceMerge
config.setMergePolicy(std::move(tiered));
```

### Compression Codec

```cpp
// Fast compression (LZ4)
config.setCompressionCodec(CompressionCodecs::lz4());

// Balanced (ZSTD level 3)
config.setCompressionCodec(CompressionCodecs::zstd(3));

// High compression (ZSTD level 10)
config.setCompressionCodec(CompressionCodecs::zstd(10));
```

### Open Mode

```cpp
// Create new index (fails if exists)
config.setOpenMode(OpenMode::CREATE);

// Append to existing (fails if doesn't exist)
config.setOpenMode(OpenMode::APPEND);

// Create or append (default)
config.setOpenMode(OpenMode::CREATE_OR_APPEND);
```

### Complete Configuration Example

```cpp
index::IndexWriterConfig config;

// Memory settings
config.setRAMBufferSizeMB(1024);
config.setMaxBufferedDocs(50000);

// Merge settings
auto tiered = std::make_unique<TieredMergePolicy>();
tiered->setMaxMergedSegmentMB(5000);
tiered->setSegmentsPerTier(10);
config.setMergePolicy(std::move(tiered));

// Compression
config.setCompressionCodec(CompressionCodecs::zstd(3));

// File format
config.setUseCompoundFile(true);

// Mode
config.setOpenMode(OpenMode::CREATE_OR_APPEND);

// Thread settings
config.setMaxThreadStates(std::thread::hardware_concurrency());
```

---

## Batch Indexing

### Single Document at a Time

```cpp
// Slow - context switches for each document
for (const auto& product : products) {
    auto doc = createProductDoc(product);
    writer->addDocument(doc);
}
```

### Batch Inserts (Recommended)

```cpp
// Fast - batch processing
const size_t BATCH_SIZE = 10000;
std::vector<index::Document> batch;
batch.reserve(BATCH_SIZE);

for (const auto& product : products) {
    batch.push_back(createProductDoc(product));

    if (batch.size() >= BATCH_SIZE) {
        writer->addDocuments(batch);
        batch.clear();
    }
}

// Don't forget remaining documents
if (!batch.empty()) {
    writer->addDocuments(batch);
}
```

**Performance**: 5-10× faster than single inserts

### Parallel Indexing

```cpp
#include <thread>
#include <queue>

void indexWorker(
    index::IndexWriter* writer,
    std::queue<std::vector<Product>>& workQueue,
    std::mutex& queueMutex)
{
    while (true) {
        std::vector<Product> batch;

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (workQueue.empty()) break;
            batch = workQueue.front();
            workQueue.pop();
        }

        std::vector<index::Document> docs;
        docs.reserve(batch.size());

        for (const auto& product : batch) {
            docs.push_back(createProductDoc(product));
        }

        writer->addDocuments(docs);
    }
}

// Use multiple threads
std::queue<std::vector<Product>> workQueue;
std::mutex queueMutex;

// Fill work queue with batches
const size_t BATCH_SIZE = 10000;
for (size_t i = 0; i < products.size(); i += BATCH_SIZE) {
    size_t end = std::min(i + BATCH_SIZE, products.size());
    workQueue.push(std::vector<Product>(
        products.begin() + i, products.begin() + end));
}

// Launch worker threads
std::vector<std::thread> threads;
for (int i = 0; i < 8; i++) {
    threads.emplace_back(indexWorker, writer.get(),
                        std::ref(workQueue), std::ref(queueMutex));
}

for (auto& thread : threads) {
    thread.join();
}
```

---

## Updating Documents

### Update = Delete + Add

Updating replaces the entire document:

```cpp
// Define query to identify document
auto query = search::TermQuery::create("id", "123");

// Create new document
index::Document newDoc;
newDoc.addField("id", 123, index::FieldType::NUMERIC);
newDoc.addField("title", "Updated Title", index::FieldType::TEXT);
newDoc.addField("price", 79.99, index::FieldType::NUMERIC);

// Update (deletes old, adds new)
writer->updateDocument(query.get(), newDoc);
```

### Batch Updates

```cpp
std::vector<std::pair<std::unique_ptr<search::Query>,
                     index::Document>> updates;

for (const auto& [id, product] : productsToUpdate) {
    auto query = search::TermQuery::create("id", std::to_string(id));
    auto doc = createProductDoc(product);
    updates.emplace_back(std::move(query), std::move(doc));
}

writer->updateDocuments(updates);
```

### Partial Updates (Not Supported)

Diagon does NOT support partial updates. You must:

1. Retrieve existing document
2. Modify it
3. Replace entire document

```cpp
// 1. Get existing document
auto reader = index::DirectoryReader::open(dir.get());
search::IndexSearcher searcher(reader.get());
auto query = search::TermQuery::create("id", "123");
auto results = searcher.search(query.get(), 1);

if (results.totalHits > 0) {
    int docID = results.scoreDocs[0].doc;
    auto oldDoc = searcher.doc(docID);

    // 2. Create new document with changes
    index::Document newDoc;
    // Copy unchanged fields
    newDoc.addField("id", oldDoc->getNumeric("id"),
                   index::FieldType::NUMERIC);
    newDoc.addField("title", oldDoc->get("title"),
                   index::FieldType::TEXT);

    // Update price
    newDoc.addField("price", 79.99, index::FieldType::NUMERIC);

    // 3. Replace document
    writer->updateDocument(query.get(), newDoc);
}
```

---

## Deleting Documents

### Delete by Term

```cpp
// Delete single document
writer->deleteDocuments(index::Term("id", "123"));

// Delete multiple documents with same term
writer->deleteDocuments(index::Term("category", "discontinued"));
```

### Delete by Query

```cpp
// Delete by text query
auto query = search::TermQuery::create("status", "deleted");
writer->deleteDocuments(query.get());

// Delete by range
auto rangeQuery = search::RangeQuery::create("price", 0.0, 10.0);
writer->deleteDocuments(rangeQuery.get());

// Delete by boolean query
auto boolQuery = search::BooleanQuery::Builder()
    .add(search::TermQuery::create("status", "inactive"),
         search::BooleanClause::MUST)
    .add(search::RangeQuery::create("last_updated", 0L, old_timestamp),
         search::BooleanClause::MUST)
    .build();
writer->deleteDocuments(boolQuery.get());
```

### Delete All Documents

```cpp
writer->deleteAll();
```

**Warning**: This is expensive! Consider creating a new index instead.

---

## Commit Strategies

### Explicit Commits

```cpp
// Add documents
for (int i = 0; i < 10000; i++) {
    writer->addDocument(doc);
}

// Commit when done
writer->commit();
```

### Periodic Commits

```cpp
const size_t COMMIT_INTERVAL = 100000;

for (size_t i = 0; i < products.size(); i++) {
    writer->addDocument(createProductDoc(products[i]));

    if (i % COMMIT_INTERVAL == 0) {
        writer->commit();
        std::cout << "Committed " << i << " documents\n";
    }
}

// Final commit
writer->commit();
```

### Commit with Metadata

```cpp
std::map<std::string, std::string> metadata;
metadata["timestamp"] = getCurrentTimestamp();
metadata["source"] = "daily_import";
metadata["record_count"] = std::to_string(record_count);

writer->commit(metadata);
```

### Rollback

```cpp
try {
    // Add documents
    for (const auto& doc : documents) {
        writer->addDocument(doc);
    }

    // Commit
    writer->commit();
} catch (const std::exception& e) {
    // Rollback on error
    writer->rollback();
    std::cerr << "Indexing failed: " << e.what() << "\n";
}
```

---

## Segment Management

### Segment Information

```cpp
// Get segment count
size_t segmentCount = writer->getSegmentCount();

// Get segment info
for (const auto& segment : writer->getSegments()) {
    std::cout << "Segment: " << segment.name << "\n";
    std::cout << "  Docs: " << segment.numDocs << "\n";
    std::cout << "  Deletes: " << segment.numDeletes << "\n";
    std::cout << "  Size: " << segment.sizeInBytes << " bytes\n";
}
```

### Force Merge

```cpp
// Merge to N segments
writer->forceMerge(1);  // Single segment

// With max segment size
writer->forceMerge(5, 1024 * 1024 * 1024);  // Max 5 segments, 1GB each
```

**Use cases**:
- Read-only indexes (after bulk load)
- Before backup/snapshot
- Reclaim space after many deletes

**Warning**: Expensive operation! Don't use for active indexes.

### Merge Monitoring

```cpp
class MergeMonitor : public MergeScheduler::Listener {
public:
    void onMergeStart(const MergeInfo& info) override {
        std::cout << "Merge started: " << info.segments.size()
                  << " segments\n";
    }

    void onMergeComplete(const MergeInfo& info) override {
        std::cout << "Merge complete: " << info.totalBytes
                  << " bytes merged\n";
    }

    void onMergeError(const MergeInfo& info,
                     const std::exception& e) override {
        std::cerr << "Merge failed: " << e.what() << "\n";
    }
};

// Register listener
config.setMergeSchedulerListener(std::make_unique<MergeMonitor>());
```

---

## Error Handling

### Common Errors

**Out of memory**:

```cpp
try {
    writer->addDocument(doc);
} catch (const std::bad_alloc& e) {
    // Reduce RAM buffer size
    config.setRAMBufferSizeMB(128);
    // Or commit and retry
    writer->commit();
    writer->addDocument(doc);
}
```

**Disk full**:

```cpp
try {
    writer->commit();
} catch (const std::runtime_error& e) {
    if (std::string(e.what()).find("No space left") != std::string::npos) {
        // Free up disk space
        // Delete old indexes
        // Compress existing data
    }
}
```

**Lock timeout**:

```cpp
try {
    auto writer = index::IndexWriter::create(dir.get(), config);
} catch (const std::runtime_error& e) {
    if (std::string(e.what()).find("Lock") != std::string::npos) {
        // Another writer is active
        // Wait and retry, or fail
    }
}
```

### Graceful Shutdown

```cpp
std::unique_ptr<index::IndexWriter> writer;

void signalHandler(int signal) {
    if (writer) {
        std::cout << "Shutting down, committing changes...\n";
        writer->commit();
        writer->close();
    }
    exit(0);
}

int main() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Create writer
    auto dir = store::FSDirectory::open("/path/to/index");
    index::IndexWriterConfig config;
    writer = index::IndexWriter::create(dir.get(), config);

    // Index documents...

    return 0;
}
```

---

## Best Practices

### 1. Use Appropriate Field Types

```cpp
// ✅ Good
doc.addField("price", 99.99, index::FieldType::NUMERIC);
doc.addField("category", "electronics", index::FieldType::KEYWORD);
doc.addField("description", "Full text...", index::FieldType::TEXT);

// ❌ Bad
doc.addField("price", "99.99", index::FieldType::TEXT);  // Can't range query
doc.addField("category", "electronics", index::FieldType::TEXT);  // Tokenized
```

### 2. Batch Inserts

```cpp
// ✅ Good
std::vector<index::Document> batch(10000);
// Fill batch
writer->addDocuments(batch);

// ❌ Bad
for (const auto& doc : docs) {
    writer->addDocument(doc);  // Individual inserts
}
```

### 3. Configure RAM Buffer

```cpp
// ✅ Good
config.setRAMBufferSizeMB(1024);  // Based on available memory

// ❌ Bad
config.setRAMBufferSizeMB(16);  // Too small, frequent flushes
```

### 4. Use Unique ID Field

```cpp
// ✅ Good
doc.addField("id", product.id, index::FieldType::NUMERIC);
doc.addField("uuid", product.uuid, index::FieldType::KEYWORD);

// Enables efficient updates
auto query = search::TermQuery::create("id", std::to_string(product.id));
writer->updateDocument(query.get(), newDoc);
```

### 5. Commit Periodically

```cpp
// ✅ Good
for (size_t i = 0; i < docs.size(); i++) {
    writer->addDocument(docs[i]);
    if (i % 100000 == 0) {
        writer->commit();  // Periodic commits
    }
}

// ❌ Bad
for (const auto& doc : docs) {
    writer->addDocument(doc);
    writer->commit();  // Too frequent
}
```

### 6. Handle Errors

```cpp
// ✅ Good
try {
    writer->addDocument(doc);
    writer->commit();
} catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    writer->rollback();
}

// ❌ Bad
writer->addDocument(doc);  // No error handling
writer->commit();
```

### 7. Close Writer

```cpp
// ✅ Good
{
    auto writer = index::IndexWriter::create(dir.get(), config);
    // Use writer
    writer->commit();
    writer->close();  // Explicit close
}

// ✅ Also good (RAII)
{
    auto writer = index::IndexWriter::create(dir.get(), config);
    // Use writer
    writer->commit();
}  // Automatic close via destructor
```

---

## See Also

- [Quick Start Guide](quick-start.md)
- [Searching Guide](searching.md)
- [Performance Guide](performance.md)
- [Core API Reference](../api/core.md)
