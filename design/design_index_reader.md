# Index Reader - Low-Level Design

## Overview

The `IndexReader` provides read access to the index, executing queries across multiple segments and storage tiers. It coordinates search, column scanning, and aggregation operations.

## Class Structure

```cpp
class IndexReader {
public:
    static std::unique_ptr<IndexReader> open(const std::string& index_path);
    static std::unique_ptr<IndexReader> open(
        const std::string& index_path,
        const IndexReaderConfig& config);

    ~IndexReader();

    // Search operations
    TopDocs search(const Query& query, uint32_t top_k) const;
    TopDocs search(const Query& query, uint32_t top_k, const Sort& sort) const;

    // Document retrieval
    Document document(uint32_t doc_id) const;
    std::vector<Document> documents(const std::vector<uint32_t>& doc_ids) const;

    // Column access (analytical queries)
    std::unique_ptr<ColumnReader> column(const std::string& field_name) const;
    AggregationResult aggregate(const AggregationQuery& query) const;

    // Statistics
    uint64_t num_docs() const;
    uint64_t num_deleted_docs() const;
    uint32_t num_segments() const;
    std::vector<SegmentInfo> segments() const;

    // Term statistics
    uint64_t doc_freq(const Term& term) const;
    uint64_t total_term_freq(const Term& term) const;

    // Refresh (reload segments)
    void refresh();

    // Lifecycle
    void close();

private:
    std::unique_ptr<IndexReaderImpl> impl_;
};

class IndexReaderImpl {
public:
    // Segment list (atomic for lock-free reads)
    std::shared_ptr<SegmentList> segments_;

    // Segment manager
    std::unique_ptr<SegmentManager> segment_manager_;

    // Tier manager
    std::unique_ptr<TierManager> tier_manager_;

    // Query execution
    std::unique_ptr<QueryExecutor> query_executor_;

    // Document lookup
    std::unique_ptr<DocumentLoader> doc_loader_;

    // Configuration
    IndexReaderConfig config_;
    std::string index_path_;

    // Refresh tracking
    std::atomic<uint64_t> generation_{0};
    std::mutex refresh_mutex_;
};

struct IndexReaderConfig {
    // Memory settings
    size_t cache_size = 512 * 1024 * 1024;  // 512MB
    bool use_mmap = true;

    // Query settings
    uint32_t max_clause_count = 1024;
    uint32_t default_top_k = 10;
    bool track_total_hits = false;  // Expensive for large result sets

    // Thread settings
    uint32_t search_threads = std::thread::hardware_concurrency();

    // Tier settings
    bool search_all_tiers = true;
};
```

## Segment List Management

```cpp
// Immutable segment list for lock-free reads
class SegmentList {
public:
    explicit SegmentList(std::vector<std::shared_ptr<Segment>> segments)
        : segments_(std::move(segments)) {}

    const std::vector<std::shared_ptr<Segment>>& segments() const {
        return segments_;
    }

    size_t size() const { return segments_.size(); }

    // Create new list with added segment
    std::shared_ptr<SegmentList> with_segment(
        std::shared_ptr<Segment> segment) const {
        auto new_segments = segments_;
        new_segments.push_back(std::move(segment));
        return std::make_shared<SegmentList>(std::move(new_segments));
    }

    // Create new list with removed segment
    std::shared_ptr<SegmentList> without_segment(
        const std::string& segment_name) const {
        std::vector<std::shared_ptr<Segment>> new_segments;
        for (const auto& seg : segments_) {
            if (seg->name() != segment_name) {
                new_segments.push_back(seg);
            }
        }
        return std::make_shared<SegmentList>(std::move(new_segments));
    }

private:
    std::vector<std::shared_ptr<Segment>> segments_;
};

// Atomic segment list updates
class IndexReaderImpl {
private:
    std::shared_ptr<SegmentList> load_segments() {
        return std::atomic_load(&segments_);
    }

    void update_segments(std::shared_ptr<SegmentList> new_segments) {
        std::atomic_store(&segments_, new_segments);
        generation_.fetch_add(1);
    }
};
```

## Search Implementation

### Basic Search

```cpp
TopDocs IndexReader::search(const Query& query, uint32_t top_k) const {
    auto segments = impl_->load_segments();

    // Search each segment in parallel
    std::vector<std::future<TopDocs>> futures;

    for (const auto& segment : segments->segments()) {
        futures.push_back(
            impl_->thread_pool_->submit([&]() {
                return search_segment(segment.get(), query, top_k);
            })
        );
    }

    // Collect results
    std::vector<TopDocs> segment_results;
    for (auto& future : futures) {
        segment_results.push_back(future.get());
    }

    // Merge top-k from all segments
    return merge_top_docs(segment_results, top_k);
}

TopDocs IndexReaderImpl::search_segment(
    const Segment* segment,
    const Query& query,
    uint32_t top_k) const {

    // Create per-segment scorer
    auto scorer = query.create_scorer(segment);

    // Priority queue for top-k
    TopKQueue<ScoredDoc> top_docs(top_k);

    // Iterate over matching documents
    while (scorer->has_next()) {
        auto [doc_id, score] = scorer->next();

        // Check if deleted
        if (!segment->is_deleted(doc_id)) {
            top_docs.insert({doc_id, score});
        }
    }

    return top_docs.to_top_docs(segment->base_doc_id());
}
```

### Sorted Search

```cpp
TopDocs IndexReader::search(
    const Query& query,
    uint32_t top_k,
    const Sort& sort) const {

    if (sort.is_score_sort()) {
        return search(query, top_k);  // Use regular search
    }

    // Column-based sorting
    auto segments = impl_->load_segments();

    // Collect candidates from all segments
    std::vector<ScoredDoc> all_docs;

    for (const auto& segment : segments->segments()) {
        auto segment_docs = search_segment(segment.get(), query, UINT32_MAX);
        all_docs.insert(
            all_docs.end(),
            segment_docs.hits.begin(),
            segment_docs.hits.end()
        );
    }

    // Sort by field value
    const std::string& sort_field = sort.field();
    auto column_reader = column(sort_field);

    std::sort(all_docs.begin(), all_docs.end(),
        [&](const ScoredDoc& a, const ScoredDoc& b) {
            auto val_a = column_reader->get_value(a.doc_id);
            auto val_b = column_reader->get_value(b.doc_id);
            return sort.is_ascending()
                ? val_a < val_b
                : val_a > val_b;
        }
    );

    // Return top-k
    TopDocs result;
    result.total_hits = all_docs.size();
    result.hits.assign(
        all_docs.begin(),
        all_docs.begin() + std::min(static_cast<size_t>(top_k), all_docs.size())
    );

    return result;
}
```

## Document Loading

```cpp
class DocumentLoader {
public:
    Document load_document(uint32_t global_doc_id) const {
        // Find segment containing this doc
        auto [segment, local_doc_id] = find_segment(global_doc_id);

        Document doc;
        doc.doc_id = global_doc_id;

        // Load stored fields
        for (const auto& field_info : segment->field_infos()) {
            if (field_info.stored) {
                doc.fields[field_info.name] = load_field(
                    segment,
                    local_doc_id,
                    field_info
                );
            }
        }

        return doc;
    }

private:
    std::pair<const Segment*, uint32_t> find_segment(
        uint32_t global_doc_id) const {

        for (const auto& segment : segments_) {
            uint32_t base = segment->base_doc_id();
            uint32_t max = base + segment->num_docs();

            if (global_doc_id >= base && global_doc_id < max) {
                return {segment.get(), global_doc_id - base};
            }
        }

        throw DocumentNotFoundException(global_doc_id);
    }

    Field load_field(
        const Segment* segment,
        uint32_t local_doc_id,
        const FieldInfo& field_info) const {

        // Load from doc values or column storage
        if (field_info.has_doc_values) {
            auto dv_reader = segment->doc_values_reader(field_info.name);
            return dv_reader->get_value(local_doc_id);
        }

        if (field_info.has_column_storage) {
            auto col_reader = segment->column_reader(field_info.name);
            return col_reader->get_value(local_doc_id);
        }

        // Load from stored fields file
        auto stored_reader = segment->stored_fields_reader();
        return stored_reader->get_field(local_doc_id, field_info.name);
    }
};
```

## Column Access

```cpp
std::unique_ptr<ColumnReader> IndexReader::column(
    const std::string& field_name) const {

    auto segments = impl_->load_segments();

    // Create multi-segment column reader
    return std::make_unique<MultiSegmentColumnReader>(
        field_name,
        segments->segments()
    );
}

class MultiSegmentColumnReader : public ColumnReader {
public:
    MultiSegmentColumnReader(
        const std::string& field_name,
        const std::vector<std::shared_ptr<Segment>>& segments)
        : field_name_(field_name) {

        // Create reader for each segment
        for (const auto& segment : segments) {
            if (segment->has_column(field_name)) {
                segment_readers_.push_back(
                    segment->column_reader(field_name)
                );
            }
        }
    }

    FieldValue get_value(uint32_t global_doc_id) const override {
        // Find segment
        for (const auto& reader : segment_readers_) {
            if (reader->contains_doc(global_doc_id)) {
                return reader->get_value(global_doc_id);
            }
        }
        throw DocumentNotFoundException(global_doc_id);
    }

    std::unique_ptr<ColumnIterator> iterator() const override {
        return std::make_unique<MultiSegmentColumnIterator>(
            segment_readers_
        );
    }

private:
    std::string field_name_;
    std::vector<std::unique_ptr<ColumnReader>> segment_readers_;
};
```

## Aggregation

```cpp
AggregationResult IndexReader::aggregate(
    const AggregationQuery& query) const {

    auto segments = impl_->load_segments();

    // Parallel aggregation across segments
    std::vector<std::future<AggregationResult>> futures;

    for (const auto& segment : segments->segments()) {
        futures.push_back(
            impl_->thread_pool_->submit([&]() {
                return aggregate_segment(segment.get(), query);
            })
        );
    }

    // Combine results
    std::vector<AggregationResult> segment_results;
    for (auto& future : futures) {
        segment_results.push_back(future.get());
    }

    return combine_aggregations(segment_results, query);
}

AggregationResult IndexReaderImpl::aggregate_segment(
    const Segment* segment,
    const AggregationQuery& query) const {

    AggregationResult result;

    // For each metric
    for (const auto& metric : query.metrics()) {
        auto col_reader = segment->column_reader(metric.field);
        auto scanner = col_reader->create_scanner(query.filter());

        switch (metric.type) {
            case AggregationType::SUM: {
                double sum = 0.0;
                while (scanner->has_next()) {
                    auto [doc_id, value] = scanner->next();
                    sum += value.as_double();
                }
                result.add_metric(metric.name, sum);
                break;
            }

            case AggregationType::AVG: {
                double sum = 0.0;
                uint64_t count = 0;
                while (scanner->has_next()) {
                    auto [doc_id, value] = scanner->next();
                    sum += value.as_double();
                    count++;
                }
                result.add_metric(metric.name, sum / count);
                break;
            }

            case AggregationType::MIN: {
                double min_val = std::numeric_limits<double>::max();
                while (scanner->has_next()) {
                    auto [doc_id, value] = scanner->next();
                    min_val = std::min(min_val, value.as_double());
                }
                result.add_metric(metric.name, min_val);
                break;
            }

            case AggregationType::MAX: {
                double max_val = std::numeric_limits<double>::lowest();
                while (scanner->has_next()) {
                    auto [doc_id, value] = scanner->next();
                    max_val = std::max(max_val, value.as_double());
                }
                result.add_metric(metric.name, max_val);
                break;
            }

            case AggregationType::COUNT: {
                uint64_t count = 0;
                while (scanner->has_next()) {
                    scanner->next();
                    count++;
                }
                result.add_metric(metric.name, count);
                break;
            }
        }
    }

    return result;
}
```

## Refresh

```cpp
void IndexReader::refresh() {
    std::lock_guard<std::mutex> lock(impl_->refresh_mutex_);

    // Discover new segments
    auto new_segment_names = impl_->segment_manager_->list_segments();
    auto current_segments = impl_->load_segments();

    // Find added segments
    std::unordered_set<std::string> current_names;
    for (const auto& seg : current_segments->segments()) {
        current_names.insert(seg->name());
    }

    std::vector<std::shared_ptr<Segment>> added_segments;
    for (const auto& name : new_segment_names) {
        if (current_names.find(name) == current_names.end()) {
            // Load new segment
            auto segment = impl_->segment_manager_->load_segment(name);
            added_segments.push_back(std::move(segment));
        }
    }

    // Create new segment list
    auto new_segments = current_segments;
    for (auto& seg : added_segments) {
        new_segments = new_segments->with_segment(std::move(seg));
    }

    // Atomic update
    impl_->update_segments(new_segments);
}
```

## Caching

```cpp
class IndexReaderCache {
public:
    explicit IndexReaderCache(size_t capacity_bytes);

    // Query result cache
    std::optional<TopDocs> get_cached_query(const Query& query) const;
    void cache_query_result(const Query& query, const TopDocs& result);

    // Document cache
    std::optional<Document> get_cached_document(uint32_t doc_id) const;
    void cache_document(uint32_t doc_id, const Document& doc);

    // Term statistics cache
    std::optional<uint64_t> get_cached_doc_freq(const Term& term) const;
    void cache_doc_freq(const Term& term, uint64_t doc_freq);

private:
    size_t capacity_bytes_;

    // LRU caches
    LRUCache<QueryHash, TopDocs> query_cache_;
    LRUCache<uint32_t, Document> doc_cache_;
    LRUCache<TermHash, uint64_t> term_stats_cache_;
};
```

## Concurrent Access

```cpp
class IndexReaderPool {
public:
    explicit IndexReaderPool(const std::string& index_path, size_t pool_size);

    // Acquire reader (blocks if all in use)
    std::shared_ptr<IndexReader> acquire();

    // Release reader back to pool
    void release(std::shared_ptr<IndexReader> reader);

    // Refresh all readers
    void refresh_all();

private:
    std::string index_path_;
    std::vector<std::shared_ptr<IndexReader>> readers_;
    std::queue<std::shared_ptr<IndexReader>> available_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

// RAII wrapper
class ScopedReader {
public:
    ScopedReader(IndexReaderPool& pool)
        : pool_(pool), reader_(pool.acquire()) {}

    ~ScopedReader() {
        pool_.release(std::move(reader_));
    }

    IndexReader& operator*() { return *reader_; }
    IndexReader* operator->() { return reader_.get(); }

private:
    IndexReaderPool& pool_;
    std::shared_ptr<IndexReader> reader_;
};
```

## Statistics

```cpp
struct SearchStatistics {
    uint64_t num_queries;
    uint64_t total_hits;
    std::chrono::milliseconds total_query_time;

    // Per-segment stats
    std::unordered_map<std::string, SegmentSearchStats> segment_stats;

    // Timing breakdown
    std::chrono::microseconds time_query_parsing;
    std::chrono::microseconds time_posting_list_intersection;
    std::chrono::microseconds time_scoring;
    std::chrono::microseconds time_top_k_merge;

    float avg_query_latency_ms() const {
        return total_query_time.count() / static_cast<float>(num_queries);
    }
};

class SearchStatisticsCollector {
public:
    void record_query(const Query& query, const TopDocs& result,
                     std::chrono::milliseconds latency);

    SearchStatistics get_statistics() const;
    void reset();

private:
    mutable std::shared_mutex mutex_;
    SearchStatistics stats_;
};
```

## Testing

```cpp
class IndexReaderTest {
    void test_concurrent_search() {
        auto reader = IndexReader::open("/tmp/test_index");

        // Launch multiple search threads
        std::vector<std::thread> threads;
        for (int i = 0; i < 10; i++) {
            threads.emplace_back([&]() {
                for (int j = 0; j < 100; j++) {
                    TermQuery query("field", "term");
                    auto results = reader->search(query, 10);
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        // No crashes = success
    }

    void test_refresh_during_search() {
        auto reader = IndexReader::open("/tmp/test_index");

        // Search thread
        std::thread search_thread([&]() {
            for (int i = 0; i < 1000; i++) {
                reader->search(query, 10);
            }
        });

        // Refresh thread
        std::thread refresh_thread([&]() {
            for (int i = 0; i < 10; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                reader->refresh();
            }
        });

        search_thread.join();
        refresh_thread.join();
    }
};
```
