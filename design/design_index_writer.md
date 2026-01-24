# Index Writer - Low-Level Design

## Overview

The `IndexWriter` is responsible for ingesting documents, building in-memory segments, and flushing them to disk. It manages field analysis, routing fields to appropriate storage (inverted index vs column storage), and coordinates merge operations.

## Class Structure

```cpp
class IndexWriter {
public:
    IndexWriter(const std::string& index_path, const IndexWriterConfig& config);
    ~IndexWriter();

    // Document operations
    void add_document(const Document& doc);
    void update_document(const Term& term, const Document& doc);
    void delete_document(const Term& term);
    void delete_documents(const Query& query);

    // Commit operations
    void commit();  // Flush and make visible
    void flush();   // Force flush without commit
    void rollback();

    // Index operations
    void force_merge(int max_segments = 1);
    void add_index(const IndexReader& reader);

    // Statistics
    int64_t num_docs() const;
    int64_t num_ram_docs() const;

private:
    std::unique_ptr<IndexWriterImpl> impl_;
};

class IndexWriterImpl {
public:
    // Core components
    std::unique_ptr<DocumentsWriter> docs_writer_;
    std::unique_ptr<SegmentManager> segment_manager_;
    std::unique_ptr<MergeScheduler> merge_scheduler_;
    std::unique_ptr<FieldInfos> field_infos_;

    // Configuration
    IndexWriterConfig config_;
    std::string index_path_;

    // State
    std::atomic<bool> closed_{false};
    std::mutex writer_mutex_;  // Protects add/update/delete operations
    SequenceNumber next_seq_num_{0};

    // Buffer management
    std::unique_ptr<MemoryArena> arena_;
    size_t current_ram_bytes_{0};

    // Tier management
    std::unique_ptr<TierManager> tier_manager_;
};
```

## Components

### 1. DocumentsWriter

Manages in-memory buffering of documents before flushing to segments.

```cpp
class DocumentsWriter {
public:
    void add_document(const Document& doc);
    void update_document(const Term& term, const Document& doc);
    void delete_document(const Term& term);

    // Flush to segment
    std::unique_ptr<SegmentCommitInfo> flush();

    // RAM usage
    size_t ram_bytes_used() const { return ram_bytes_; }
    bool should_flush(size_t ram_buffer_size) const;

private:
    // In-memory structures
    struct BufferedDocument {
        uint32_t doc_id;
        std::unordered_map<std::string, FieldData> fields;
    };

    std::vector<BufferedDocument> buffered_docs_;

    // Inverted index buffers
    std::unordered_map<std::string, InMemoryTermDictionary> term_dicts_;
    std::unordered_map<std::string, InMemoryPostingLists> posting_buffers_;

    // Column storage buffers
    std::unordered_map<std::string, ColumnBuilder> column_builders_;

    // Delete buffers
    std::vector<Term> pending_deletes_;
    std::unique_ptr<Bitmap> deleted_docs_;

    size_t ram_bytes_{0};
    uint32_t next_doc_id_{0};
};
```

### 2. FieldRouter

Routes fields to appropriate storage based on field type and configuration.

```cpp
class FieldRouter {
public:
    struct FieldTarget {
        bool inverted_index;   // Build inverted index
        bool column_storage;   // Build column storage
        bool doc_values;       // Build doc values (fast random access)
        bool term_vectors;     // Store term positions
    };

    FieldTarget route_field(const Field& field, const FieldInfo& info) const {
        FieldTarget target;

        switch (field.type) {
            case FieldType::TEXT:
                target.inverted_index = true;
                target.doc_values = info.need_doc_values;
                target.term_vectors = info.store_term_vectors;
                break;

            case FieldType::KEYWORD:
                target.inverted_index = true;
                target.doc_values = true;
                break;

            case FieldType::INT32:
            case FieldType::INT64:
            case FieldType::FLOAT:
            case FieldType::DOUBLE:
                target.column_storage = true;
                target.doc_values = true;
                // Optional inverted index for exact-match queries
                target.inverted_index = info.indexed;
                break;

            case FieldType::ARRAY:
            case FieldType::NESTED:
                target.column_storage = true;
                break;
        }

        return target;
    }
};
```

### 3. InMemoryTermDictionary

Temporary term dictionary during indexing.

```cpp
class InMemoryTermDictionary {
public:
    // Add term and get term ID
    uint32_t add_term(const std::string_view& term);

    // Get sorted terms for flushing
    std::vector<std::pair<std::string, uint32_t>> sorted_terms() const;

    size_t ram_bytes_used() const;

private:
    // Two-way mapping
    std::unordered_map<std::string, uint32_t> term_to_id_;
    std::vector<std::string> id_to_term_;
    uint32_t next_term_id_{0};
};
```

### 4. InMemoryPostingLists

Accumulates posting lists during indexing.

```cpp
class InMemoryPostingLists {
public:
    struct Posting {
        uint32_t doc_id;
        uint32_t term_freq;
        std::vector<uint32_t> positions;  // Optional
    };

    void add_posting(uint32_t term_id, uint32_t doc_id,
                     uint32_t position = 0);

    // Get posting list for a term
    const std::vector<Posting>& get_postings(uint32_t term_id) const;

    // Flush to disk format
    void flush_to_file(FileWriter& writer, const CompressionConfig& config);

    size_t ram_bytes_used() const;

private:
    std::unordered_map<uint32_t, std::vector<Posting>> postings_;
};
```

### 5. ColumnBuilder

Builds column storage during indexing.

```cpp
class ColumnBuilder {
public:
    explicit ColumnBuilder(const std::string& field_name, FieldType type);

    // Add value
    void add_value(uint32_t doc_id, const FieldValue& value);
    void add_null(uint32_t doc_id);

    // Flush to column files
    void flush_to_files(const std::string& output_dir,
                       const ColumnConfig& config);

    size_t ram_bytes_used() const;

private:
    std::string field_name_;
    FieldType type_;

    // Type-specific builders
    std::unique_ptr<TypedColumnBuilder> typed_builder_;

    // Granule tracking
    uint32_t current_granule_start_{0};
    uint32_t granule_size_{8192};

    // Null tracking
    std::unique_ptr<BitmapBuilder> null_bitmap_;
};

// Type-specific builders
template<typename T>
class TypedColumnBuilder {
public:
    void add_value(T value);
    void flush_granule(FileWriter& writer);

private:
    std::vector<T> current_granule_;
    std::unique_ptr<Compressor> compressor_;
    std::vector<GranuleMark> marks_;
};
```

## Indexing Flow

### Add Document Flow

```
1. IndexWriter.add_document(doc)
   ↓
2. Acquire writer_mutex (single writer)
   ↓
3. Analyze document fields
   ↓
4. For each field:
   ├─→ Route to storage (FieldRouter)
   │
   ├─→ If INVERTED_INDEX:
   │   ├─→ Tokenize text
   │   ├─→ Add terms to InMemoryTermDictionary
   │   └─→ Add postings to InMemoryPostingLists
   │
   ├─→ If COLUMN_STORAGE:
   │   └─→ Add value to ColumnBuilder
   │
   └─→ If DOC_VALUES:
       └─→ Add to DocValuesBuilder
   ↓
5. Increment doc_id
   ↓
6. Update RAM usage counter
   ↓
7. Check flush trigger:
   - RAM bytes > threshold
   - Doc count > threshold
   - Time since last flush > interval
   ↓
8. If should flush:
   └─→ DocumentsWriter.flush()
```

### Flush Flow

```
1. DocumentsWriter.flush()
   ↓
2. Create new segment name (e.g., _0, _1, ...)
   ↓
3. Flush inverted index:
   ├─→ Sort terms in term dictionary
   ├─→ Write term dictionary to file (_term_dict.trie/.hash)
   ├─→ For each term (sorted):
   │   ├─→ Get posting list
   │   ├─→ Compress posting list (VByte, PForDelta)
   │   ├─→ Build skip list (every 128 docs)
   │   └─→ Write to _postings.dat
   └─→ Write skip list data to _postings.skip
   ↓
4. Flush column storage:
   ├─→ For each column:
   │   ├─→ Partition by type
   │   ├─→ For each type partition:
   │   │   ├─→ Compress granules
   │   │   ├─→ Write data to field.type/data.bin
   │   │   ├─→ Write marks to field.type/marks.idx
   │   │   └─→ Build sparse index on first row of each granule
   │   └─→ Write sparse index to field.type/primary.idx
   └─→ Write null bitmaps
   ↓
5. Flush doc values:
   └─→ Write random-access structures to field.dv
   ↓
6. Write segment metadata:
   ├─→ Field infos (schema)
   ├─→ Segment info (doc count, codec version)
   └─→ Deleted docs bitmap
   ↓
7. Commit segment:
   ├─→ Fsync all files
   └─→ Add to SegmentManager (atomic visibility)
   ↓
8. Clear in-memory buffers
   ↓
9. Reset RAM counter
```

### Update Document Flow

```
1. IndexWriter.update_document(term, new_doc)
   ↓
2. Add delete for term (mark old doc as deleted)
   ↓
3. Add new document
   ↓
4. Note: Old doc remains in segment but marked deleted
        Actual removal happens during merge
```

### Delete Document Flow

```
1. IndexWriter.delete_document(term)
   ↓
2. Add to pending deletes buffer
   ↓
3. On flush:
   ├─→ Resolve term to doc IDs (query existing segments)
   ├─→ Mark doc IDs as deleted in bitmap
   └─→ Write deleted_docs.bm to segment
```

## Memory Management

### RAM Buffer Size Calculation

```cpp
class RAMUsageEstimator {
public:
    static size_t estimate_document(const Document& doc) {
        size_t bytes = sizeof(Document);
        for (const auto& field : doc.fields) {
            bytes += estimate_field(field);
        }
        return bytes;
    }

    static size_t estimate_field(const Field& field) {
        size_t bytes = sizeof(Field) + field.name.size();

        // Inverted index overhead
        if (field.storage & FieldStorage::INVERTED_INDEX) {
            // Term dictionary entry: ~32 bytes per unique term
            // Posting: ~12 bytes per doc
            bytes += estimated_unique_terms(field) * 32;
            bytes += 12;  // One posting per doc
        }

        // Column storage overhead
        if (field.storage & FieldStorage::COLUMN_STORAGE) {
            bytes += estimate_column_value(field.value);
        }

        return bytes;
    }
};
```

### Flush Triggers

```cpp
bool DocumentsWriter::should_flush(size_t ram_buffer_size) const {
    // 1. RAM threshold
    if (ram_bytes_ >= ram_buffer_size) {
        return true;
    }

    // 2. Document count threshold
    if (buffered_docs_.size() >= MAX_BUFFERED_DOCS) {
        return true;
    }

    // 3. Time threshold (avoid stale data)
    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - last_flush_time_;
    if (elapsed > MAX_FLUSH_INTERVAL) {
        return true;
    }

    return false;
}
```

## Concurrency Model

### Single Writer, Multiple Readers

```cpp
// Writer side (IndexWriter)
{
    std::unique_lock<std::mutex> lock(writer_mutex_);
    // Add/update/delete operations
    // Only one thread can write at a time
}

// Reader side (IndexReader)
// No locks needed - segments are immutable
// New segments become visible atomically via SegmentManager
```

### Flush Coordination

```cpp
void IndexWriter::flush() {
    // 1. Stop accepting new documents
    writer_mutex_.lock();

    // 2. Flush current buffer to segment
    auto segment = docs_writer_->flush();

    // 3. Add segment to manager (atomic)
    segment_manager_->add_segment(std::move(segment));

    // 4. Resume accepting documents
    writer_mutex_.unlock();

    // 5. Trigger merge check (async)
    merge_scheduler_->maybe_merge();
}
```

## Circuit Breaker Integration

```cpp
class IndexWriter {
private:
    std::unique_ptr<CircuitBreaker> circuit_breaker_;

    void check_circuit_breaker() {
        if (circuit_breaker_) {
            // Memory limit check
            circuit_breaker_->check_memory_limit(current_ram_bytes_);

            // Indexing rate check
            circuit_breaker_->check_indexing_rate(docs_indexed_per_sec_);
        }
    }

public:
    void add_document(const Document& doc) {
        check_circuit_breaker();
        // ... rest of implementation
    }
};
```

## Configuration

```cpp
struct IndexWriterConfig {
    // Memory settings
    size_t ram_buffer_size = 128 * 1024 * 1024;  // 128MB
    size_t max_buffered_docs = 10000;

    // Storage mode
    MemoryMode memory_mode = MemoryMode::HYBRID;

    // Merge policy
    std::unique_ptr<MergePolicy> merge_policy;

    // Tier settings
    std::chrono::duration<int64_t> hot_tier_ttl = std::chrono::days(7);
    std::chrono::duration<int64_t> warm_tier_ttl = std::chrono::days(90);

    // Codec
    std::unique_ptr<Codec> codec;

    // Term dictionary type
    TermDictionaryType term_dict_type = TermDictionaryType::TRIE;

    // Column settings
    uint32_t column_granule_size = 8192;
    CompressionType column_compression = CompressionType::LZ4;

    // Circuit breaker
    std::unique_ptr<CircuitBreaker> circuit_breaker;
};
```

## Error Handling

```cpp
class IndexWriterException : public std::exception {
public:
    enum class ErrorCode {
        DISK_FULL,
        CORRUPTION,
        LOCK_TIMEOUT,
        MEMORY_LIMIT_EXCEEDED,
        CIRCUIT_BREAKER_TRIPPED
    };

    ErrorCode code() const { return code_; }
    const char* what() const noexcept override { return message_.c_str(); }

private:
    ErrorCode code_;
    std::string message_;
};

// Usage
void IndexWriter::add_document(const Document& doc) {
    try {
        // ... indexing logic
    } catch (const std::bad_alloc& e) {
        throw IndexWriterException(ErrorCode::MEMORY_LIMIT_EXCEEDED,
                                   "Out of memory during indexing");
    } catch (const std::filesystem::filesystem_error& e) {
        if (e.code() == std::errc::no_space_on_device) {
            throw IndexWriterException(ErrorCode::DISK_FULL,
                                       "Disk full while writing segment");
        }
        throw;
    }
}
```

## Performance Optimizations

### 1. Arena Allocation

```cpp
class MemoryArena {
public:
    void* allocate(size_t size) {
        if (current_offset_ + size > block_size_) {
            allocate_new_block();
        }
        void* ptr = current_block_ + current_offset_;
        current_offset_ += align(size);
        return ptr;
    }

    void reset() {
        // Bulk free - just reset offset
        current_offset_ = 0;
    }
};

// Usage in DocumentsWriter
arena_->reset();  // Fast bulk free after flush
```

### 2. Batch Processing

```cpp
// Instead of add_document() one at a time
void IndexWriter::add_documents(const std::vector<Document>& docs) {
    std::unique_lock<std::mutex> lock(writer_mutex_);

    for (const auto& doc : docs) {
        docs_writer_->add_document_unsafe(doc);  // No lock per doc
    }

    if (docs_writer_->should_flush(config_.ram_buffer_size)) {
        flush_internal();  // Already have lock
    }
}
```

### 3. Direct Field Access

```cpp
// Avoid copying field values
void DocumentsWriter::add_document(const Document& doc) {
    for (const auto& [field_name, field] : doc.fields) {
        // Use string_view to avoid copying
        std::string_view field_view(field.name);

        // Move values when possible
        column_builders_[field_name]->add_value(
            buffered_docs_.size(),
            std::move(field.value)  // Move instead of copy
        );
    }
}
```

## Testing Considerations

```cpp
class IndexWriterTest {
    void test_flush_on_ram_limit() {
        IndexWriterConfig config;
        config.ram_buffer_size = 1024;  // 1KB for testing

        IndexWriter writer("/tmp/test", config);

        // Add documents until flush
        while (writer.num_ram_docs() > 0) {
            writer.add_document(create_large_doc());
        }

        // Verify segment created
        ASSERT_GT(writer.num_docs(), 0);
    }

    void test_concurrent_reads_during_flush() {
        // Add documents in writer thread
        // Read from reader threads
        // Verify no corruption
    }
};
```
