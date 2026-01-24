# Segment Management - Low-Level Design

## Overview

Segment management handles the lifecycle of immutable segments: creation, merging, deletion, and visibility. It implements Lucene's segment-based architecture with background merging to maintain query performance.

## Segment Structure

```cpp
class Segment {
public:
    // Metadata
    std::string name() const { return name_; }
    uint32_t num_docs() const { return num_docs_; }
    uint32_t num_deleted_docs() const { return num_deleted_docs_; }
    uint32_t base_doc_id() const { return base_doc_id_; }
    StorageTier tier() const { return tier_; }

    // Inverted index access
    std::unique_ptr<TermDictionary> term_dictionary(const std::string& field) const;
    std::unique_ptr<PostingList> posting_list(const std::string& field, uint64_t offset) const;

    // Column storage access
    std::unique_ptr<ColumnReader> column_reader(const std::string& field) const;
    bool has_column(const std::string& field) const;

    // Doc values access
    std::unique_ptr<DocValuesReader> doc_values_reader(const std::string& field) const;

    // Stored fields access
    std::unique_ptr<StoredFieldsReader> stored_fields_reader() const;

    // Deletion tracking
    bool is_deleted(uint32_t local_doc_id) const;
    void mark_deleted(uint32_t local_doc_id);

    // Field metadata
    const FieldInfos& field_infos() const { return *field_infos_; }

    // Statistics
    size_t bytes_on_disk() const;
    CompressionType compression_type() const;

    // Filesystem
    std::filesystem::path directory() const { return directory_; }

private:
    std::string name_;                    // e.g., "_0", "_1", ...
    uint32_t num_docs_;
    uint32_t num_deleted_docs_;
    uint32_t base_doc_id_;                // Global doc ID offset
    StorageTier tier_;

    std::filesystem::path directory_;

    // Metadata
    std::unique_ptr<FieldInfos> field_infos_;
    std::unique_ptr<SegmentInfo> segment_info_;

    // Index structures (lazy-loaded)
    mutable std::unordered_map<std::string, std::unique_ptr<TermDictionary>> term_dicts_;
    mutable std::unordered_map<std::string, std::unique_ptr<ColumnReader>> column_readers_;

    // Deleted docs
    std::unique_ptr<Bitmap> deleted_docs_;

    // Memory mode
    MemoryMode memory_mode_;
};

struct SegmentInfo {
    std::string name;
    uint32_t num_docs;
    std::chrono::system_clock::time_point created_at;

    // File checksums (for integrity verification)
    std::unordered_map<std::string, uint64_t> file_checksums;

    // Codec version
    std::string codec_version;

    // Statistics
    std::unordered_map<std::string, uint64_t> index_stats;  // Field -> term count
    std::unordered_map<std::string, uint64_t> column_stats;  // Field -> row count
};
```

## Segment Manager

```cpp
class SegmentManager {
public:
    explicit SegmentManager(const std::string& index_path);

    // Segment operations
    void add_segment(std::unique_ptr<Segment> segment);
    void remove_segment(const std::string& segment_name);
    std::shared_ptr<Segment> load_segment(const std::string& segment_name);

    // Segment discovery
    std::vector<std::string> list_segments() const;
    std::vector<SegmentInfo> list_segment_infos() const;

    // Generation management
    uint64_t next_generation() { return generation_.fetch_add(1); }
    uint64_t current_generation() const { return generation_.load(); }

    // Commit
    void commit(const std::vector<std::string>& segment_names);
    void rollback();

    // File management
    void delete_unused_files();

private:
    std::string index_path_;
    std::atomic<uint64_t> generation_{0};

    // Segments file (tracks active segments)
    void write_segments_file(const std::vector<std::string>& segment_names);
    std::vector<std::string> read_segments_file() const;

    // Lock file for multi-process coordination
    std::unique_ptr<FileLock> index_lock_;
};
```

## Segments File Format

```
segments_N format:

[Header]
- Magic: "SEGS" (4 bytes)
- Version: uint32_t
- Generation: uint64_t
- Timestamp: uint64_t

[Segment List]
- Num Segments: uint32_t
- For each segment:
  - Segment Name: string
  - Num Docs: uint32_t
  - Num Deleted Docs: uint32_t
  - Tier: uint8_t
  - Checksum: uint64_t

[Metadata]
- User Data: map<string, string>

[Checksum]
- File Checksum: uint64_t (CRC32)
```

## Merge Policy

```cpp
class MergePolicy {
public:
    virtual ~MergePolicy() = default;

    // Determine which segments to merge
    virtual std::vector<MergeSpec> find_merges(
        const std::vector<SegmentInfo>& segments) const = 0;

    // Check if merge should be triggered
    virtual bool should_merge(
        const std::vector<SegmentInfo>& segments) const = 0;
};

struct MergeSpec {
    std::vector<std::string> source_segments;  // Segments to merge
    std::string target_segment;                // Output segment name
    StorageTier target_tier;                   // Tier for merged segment
    uint64_t estimated_bytes;                  // Estimated output size
};

// Tiered merge policy (like Lucene's TieredMergePolicy)
class TieredMergePolicy : public MergePolicy {
public:
    TieredMergePolicy();

    std::vector<MergeSpec> find_merges(
        const std::vector<SegmentInfo>& segments) const override;

    bool should_merge(
        const std::vector<SegmentInfo>& segments) const override;

    // Configuration
    void set_max_merged_segment_mb(uint32_t mb) { max_merged_segment_mb_ = mb; }
    void set_segments_per_tier(uint32_t count) { segments_per_tier_ = count; }
    void set_max_merge_at_once(uint32_t count) { max_merge_at_once_ = count; }

private:
    uint32_t max_merged_segment_mb_{5000};      // Max 5GB per segment
    uint32_t segments_per_tier_{10};            // Aim for 10 segments per tier
    uint32_t max_merge_at_once_{10};            // Merge at most 10 segments

    std::vector<MergeSpec> find_merges_for_tier(
        const std::vector<SegmentInfo>& segments,
        StorageTier tier) const;

    double compute_merge_score(
        const std::vector<SegmentInfo>& candidates) const;
};

std::vector<MergeSpec> TieredMergePolicy::find_merges(
    const std::vector<SegmentInfo>& segments) const {

    std::vector<MergeSpec> merges;

    // Group segments by tier
    std::unordered_map<StorageTier, std::vector<SegmentInfo>> tier_segments;
    for (const auto& seg : segments) {
        tier_segments[seg.tier].push_back(seg);
    }

    // Find merges per tier
    for (auto& [tier, tier_segs] : tier_segments) {
        auto tier_merges = find_merges_for_tier(tier_segs, tier);
        merges.insert(merges.end(), tier_merges.begin(), tier_merges.end());
    }

    return merges;
}

std::vector<MergeSpec> TieredMergePolicy::find_merges_for_tier(
    const std::vector<SegmentInfo>& segments,
    StorageTier tier) const {

    if (segments.size() <= segments_per_tier_) {
        return {};  // No merge needed
    }

    // Sort by size
    auto sorted = segments;
    std::sort(sorted.begin(), sorted.end(),
        [](const SegmentInfo& a, const SegmentInfo& b) {
            return a.num_docs < b.num_docs;
        }
    );

    std::vector<MergeSpec> merges;

    // Merge smallest segments
    size_t i = 0;
    while (i + 1 < sorted.size()) {
        std::vector<std::string> to_merge;
        uint64_t total_docs = 0;

        // Collect up to max_merge_at_once segments
        for (size_t j = 0; j < max_merge_at_once_ && i < sorted.size(); j++, i++) {
            to_merge.push_back(sorted[i].name);
            total_docs += sorted[i].num_docs;

            // Stop if merged size exceeds limit
            if (total_docs * sizeof_doc_ > max_merged_segment_mb_ * 1024 * 1024) {
                break;
            }
        }

        if (to_merge.size() >= 2) {
            MergeSpec spec;
            spec.source_segments = std::move(to_merge);
            spec.target_segment = generate_segment_name();
            spec.target_tier = tier;
            spec.estimated_bytes = total_docs * sizeof_doc_;
            merges.push_back(spec);
        }
    }

    return merges;
}
```

## Merge Scheduler

```cpp
class MergeScheduler {
public:
    explicit MergeScheduler(SegmentManager* segment_manager,
                           std::unique_ptr<MergePolicy> merge_policy);

    ~MergeScheduler();

    // Check for merges
    void maybe_merge();
    void force_merge(int max_segments = 1);

    // Control
    void pause();
    void resume();

    // Statistics
    MergeStatistics get_statistics() const;

private:
    SegmentManager* segment_manager_;
    std::unique_ptr<MergePolicy> merge_policy_;

    // Background merge thread
    std::thread merge_thread_;
    std::atomic<bool> running_{true};
    std::atomic<bool> paused_{false};

    // Merge queue
    std::queue<MergeSpec> pending_merges_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;

    // Active merges
    std::vector<std::unique_ptr<MergeTask>> active_merges_;
    size_t max_concurrent_merges_{2};

    // Background merge loop
    void merge_loop();

    // Execute single merge
    void execute_merge(const MergeSpec& spec);
};

void MergeScheduler::merge_loop() {
    while (running_) {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        // Wait for merge or shutdown
        queue_cv_.wait(lock, [this]() {
            return !running_ || (!pending_merges_.empty() && !paused_);
        });

        if (!running_) break;
        if (paused_) continue;

        // Get next merge
        if (pending_merges_.empty()) continue;

        MergeSpec spec = pending_merges_.front();
        pending_merges_.pop();

        lock.unlock();

        // Execute merge (may take a while)
        try {
            execute_merge(spec);
        } catch (const std::exception& e) {
            // Log error but continue
            std::cerr << "Merge failed: " << e.what() << std::endl;
        }
    }
}

void MergeScheduler::execute_merge(const MergeSpec& spec) {
    auto start = std::chrono::steady_clock::now();

    // Load source segments
    std::vector<std::shared_ptr<Segment>> source_segments;
    for (const auto& name : spec.source_segments) {
        source_segments.push_back(segment_manager_->load_segment(name));
    }

    // Create merger
    SegmentMerger merger(source_segments, spec.target_segment);

    // Perform merge
    auto merged_segment = merger.merge();

    // Add merged segment atomically
    segment_manager_->add_segment(std::move(merged_segment));

    // Remove source segments
    for (const auto& name : spec.source_segments) {
        segment_manager_->remove_segment(name);
    }

    // Commit changes
    segment_manager_->commit();

    auto elapsed = std::chrono::steady_clock::now() - start;

    // Update statistics
    record_merge_stats(spec, elapsed);
}
```

## Segment Merger

```cpp
class SegmentMerger {
public:
    SegmentMerger(
        const std::vector<std::shared_ptr<Segment>>& source_segments,
        const std::string& target_name);

    std::unique_ptr<Segment> merge();

private:
    std::vector<std::shared_ptr<Segment>> source_segments_;
    std::string target_name_;

    // Merge components
    void merge_inverted_indexes(Segment* target);
    void merge_column_storage(Segment* target);
    void merge_doc_values(Segment* target);
    void merge_stored_fields(Segment* target);

    // Doc ID remapping
    std::unordered_map<uint32_t, uint32_t> build_doc_id_map();
};

std::unique_ptr<Segment> SegmentMerger::merge() {
    auto target = std::make_unique<Segment>(target_name_);

    // Build doc ID mapping (skip deleted docs)
    auto doc_id_map = build_doc_id_map();

    // Merge inverted indexes
    merge_inverted_indexes(target.get());

    // Merge column storage
    merge_column_storage(target.get());

    // Merge doc values
    merge_doc_values(target.get());

    // Merge stored fields
    merge_stored_fields(target.get());

    return target;
}

void SegmentMerger::merge_inverted_indexes(Segment* target) {
    // Collect all fields
    std::unordered_set<std::string> fields;
    for (const auto& segment : source_segments_) {
        for (const auto& field_info : segment->field_infos()) {
            if (field_info.has_inverted_index) {
                fields.insert(field_info.name);
            }
        }
    }

    // Merge each field
    for (const auto& field : fields) {
        merge_field_inverted_index(target, field);
    }
}

void SegmentMerger::merge_field_inverted_index(
    Segment* target,
    const std::string& field) {

    // Collect all unique terms
    std::map<std::string, std::vector<PostingList*>> term_postings;

    for (const auto& segment : source_segments_) {
        auto term_dict = segment->term_dictionary(field);
        auto it = term_dict->iterator();

        while (it->has_next()) {
            auto [term, term_info] = it->next();

            auto posting_list = segment->posting_list(
                field,
                term_info.posting_list_offset
            );

            term_postings[term].push_back(posting_list.get());
        }
    }

    // Merge posting lists for each term
    for (auto& [term, postings] : term_postings) {
        auto merged_posting = merge_posting_lists(postings);

        // Write to target segment
        target->write_posting_list(field, term, merged_posting.get());
    }
}

std::unique_ptr<PostingList> SegmentMerger::merge_posting_lists(
    const std::vector<PostingList*>& postings) {

    // Collect all doc IDs (with remapping)
    std::vector<Posting> merged_postings;

    for (auto* posting_list : postings) {
        auto it = posting_list->iterator();

        while (it->has_next()) {
            auto posting = it->next();

            // Remap doc ID
            uint32_t new_doc_id = doc_id_map_[posting.doc_id];

            if (new_doc_id != DELETED_DOC_ID) {
                posting.doc_id = new_doc_id;
                merged_postings.push_back(posting);
            }
        }
    }

    // Sort by doc ID
    std::sort(merged_postings.begin(), merged_postings.end(),
        [](const Posting& a, const Posting& b) {
            return a.doc_id < b.doc_id;
        }
    );

    // Create merged posting list
    return std::make_unique<StandardPostingList>(merged_postings);
}

void SegmentMerger::merge_column_storage(Segment* target) {
    // Collect all column fields
    std::unordered_set<std::string> columns;
    for (const auto& segment : source_segments_) {
        for (const auto& field_info : segment->field_infos()) {
            if (field_info.has_column_storage) {
                columns.insert(field_info.name);
            }
        }
    }

    // Merge each column
    for (const auto& column : columns) {
        merge_column(target, column);
    }
}

void SegmentMerger::merge_column(Segment* target, const std::string& field) {
    // Read all column values in doc ID order
    std::vector<FieldValue> all_values;

    for (const auto& segment : source_segments_) {
        auto col_reader = segment->column_reader(field);

        for (uint32_t doc_id = 0; doc_id < segment->num_docs(); doc_id++) {
            if (!segment->is_deleted(doc_id)) {
                all_values.push_back(col_reader->get_value(doc_id));
            }
        }
    }

    // Write merged column
    auto col_writer = target->column_writer(field);
    for (size_t i = 0; i < all_values.size(); i++) {
        col_writer->add_value(i, all_values[i]);
    }
    col_writer->flush();
}
```

## Commit Point

```cpp
class CommitPoint {
public:
    explicit CommitPoint(uint64_t generation);

    // Segments in this commit
    void add_segment(const std::string& segment_name);
    const std::vector<std::string>& segments() const { return segments_; }

    // Metadata
    uint64_t generation() const { return generation_; }
    std::chrono::system_clock::time_point timestamp() const { return timestamp_; }

    // User data (custom metadata)
    void set_user_data(const std::string& key, const std::string& value);
    std::string get_user_data(const std::string& key) const;

    // Serialization
    void write_to_file(const std::string& path) const;
    static CommitPoint read_from_file(const std::string& path);

private:
    uint64_t generation_;
    std::vector<std::string> segments_;
    std::chrono::system_clock::time_point timestamp_;
    std::unordered_map<std::string, std::string> user_data_;
};

// Find latest commit
std::optional<CommitPoint> find_latest_commit(const std::string& index_path) {
    // List all segments_N files
    std::vector<uint64_t> generations;

    for (const auto& entry : std::filesystem::directory_iterator(index_path)) {
        if (entry.path().filename().string().starts_with("segments_")) {
            auto gen_str = entry.path().filename().string().substr(9);
            generations.push_back(std::stoull(gen_str));
        }
    }

    if (generations.empty()) {
        return std::nullopt;
    }

    // Find highest generation
    uint64_t max_gen = *std::max_element(generations.begin(), generations.end());

    // Read commit point
    std::string commit_file = index_path + "/segments_" + std::to_string(max_gen);
    return CommitPoint::read_from_file(commit_file);
}
```

## File Management

```cpp
class FileManager {
public:
    explicit FileManager(const std::string& index_path);

    // Track file usage
    void add_file_ref(const std::string& file_name);
    void remove_file_ref(const std::string& file_name);

    // Delete unused files
    void delete_unused_files();

    // Filesystem utilities
    std::vector<std::string> list_index_files() const;
    uint64_t get_file_size(const std::string& file_name) const;
    void fsync_directory();

private:
    std::string index_path_;

    // Reference counting
    std::unordered_map<std::string, uint32_t> file_refs_;
    std::mutex refs_mutex_;

    // Protected files (current commit)
    std::unordered_set<std::string> protected_files_;
};

void FileManager::delete_unused_files() {
    std::lock_guard<std::mutex> lock(refs_mutex_);

    auto all_files = list_index_files();

    for (const auto& file : all_files) {
        // Skip if referenced
        if (file_refs_[file] > 0) continue;

        // Skip if protected (in current commit)
        if (protected_files_.find(file) != protected_files_.end()) continue;

        // Skip if it's a segments file (keep for recovery)
        if (file.starts_with("segments_")) continue;

        // Delete file
        std::filesystem::remove(index_path_ / file);
    }
}
```

## Testing

```cpp
class SegmentManagementTest {
    void test_merge_correctness() {
        // Create two segments
        auto seg1 = create_segment_with_docs({"doc1", "doc2", "doc3"});
        auto seg2 = create_segment_with_docs({"doc4", "doc5"});

        // Merge
        SegmentMerger merger({seg1, seg2}, "merged");
        auto merged = merger.merge();

        // Verify all docs present
        ASSERT_EQ(merged->num_docs(), 5);

        // Verify search works
        TermQuery query("text", "doc1");
        auto results = merged->search(query, 10);
        ASSERT_EQ(results.total_hits, 1);
    }

    void test_deleted_docs_compaction() {
        auto segment = create_test_segment();

        // Delete half the docs
        for (uint32_t i = 0; i < segment->num_docs() / 2; i++) {
            segment->mark_deleted(i);
        }

        // Merge to compact
        SegmentMerger merger({segment}, "compacted");
        auto compacted = merger.merge();

        // Verify deleted docs removed
        ASSERT_EQ(compacted->num_docs(), segment->num_docs() / 2);
        ASSERT_EQ(compacted->num_deleted_docs(), 0);
    }

    void test_concurrent_merge() {
        // Add segments while merging
        std::thread writer_thread([&]() {
            for (int i = 0; i < 100; i++) {
                writer->add_document(create_doc());
                writer->commit();
            }
        });

        std::thread merge_thread([&]() {
            for (int i = 0; i < 10; i++) {
                scheduler->maybe_merge();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });

        writer_thread.join();
        merge_thread.join();

        // Verify index consistency
        reader->refresh();
        ASSERT_GT(reader->num_docs(), 0);
    }
};
```
