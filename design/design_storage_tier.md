# Storage Tier Management - Low-Level Design

## Overview

Storage tiers enable efficient data lifecycle management by moving segments across hot/warm/cold tiers based on age and access patterns. Inspired by OpenSearch Index Lifecycle Management (ILM).

## Tier Definitions

```cpp
enum class StorageTier {
    HOT,    // In-memory or SSD, 0-7 days, <10ms latency
    WARM,   // SSD, 7-90 days, <100ms latency
    COLD,   // HDD or S3, >90 days, <1s latency
    FROZEN  // Glacier/tape, rarely accessed, no SLA
};

struct TierConfig {
    StorageTier tier;
    std::filesystem::path storage_path;

    // Memory management
    size_t memory_budget;           // Max RAM for this tier
    MemoryMode memory_mode;         // IN_MEMORY, MMAP, DIRECT_IO

    // Age thresholds
    std::chrono::duration<int64_t> min_age;
    std::chrono::duration<int64_t> max_age;

    // Performance characteristics
    uint32_t expected_latency_ms;
    uint32_t max_concurrent_queries;

    // Compression
    CompressionType compression;
    int compression_level;

    // Merge policy
    std::unique_ptr<MergePolicy> merge_policy;
};
```

## Tier Manager

```cpp
class TierManager {
public:
    explicit TierManager(const std::vector<TierConfig>& configs);

    // Segment operations
    void add_segment(std::unique_ptr<Segment> segment, StorageTier tier);
    void migrate_segment(const std::string& segment_name, StorageTier target_tier);

    // Automatic lifecycle
    void check_lifecycle_policies();
    void force_tier_transition(const std::string& segment_name);

    // Query routing
    std::vector<Segment*> get_segments_for_query(const Query& query) const;

    // Statistics
    TierStatistics get_tier_stats(StorageTier tier) const;
    std::vector<SegmentInfo> list_segments_in_tier(StorageTier tier) const;

private:
    std::unordered_map<StorageTier, TierConfig> tier_configs_;
    std::unordered_map<StorageTier, std::vector<std::unique_ptr<Segment>>> tier_segments_;

    // Lifecycle tracking
    std::unordered_map<std::string, SegmentLifecycle> segment_lifecycles_;

    // Background thread for lifecycle management
    std::thread lifecycle_thread_;
    std::atomic<bool> running_{true};

    // Metrics
    std::unique_ptr<TierMetrics> metrics_;
};

struct SegmentLifecycle {
    std::string segment_name;
    StorageTier current_tier;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point last_accessed;
    uint64_t access_count;
    size_t size_bytes;
};
```

## Lifecycle Policies

```cpp
class LifecyclePolicy {
public:
    virtual ~LifecyclePolicy() = default;

    // Determine if segment should transition
    virtual bool should_transition(
        const SegmentLifecycle& lifecycle,
        StorageTier current_tier,
        StorageTier target_tier) const = 0;

    // Get target tier for segment
    virtual StorageTier get_target_tier(
        const SegmentLifecycle& lifecycle) const = 0;
};

// Age-based policy
class AgeBasedPolicy : public LifecyclePolicy {
public:
    AgeBasedPolicy(
        std::chrono::duration<int64_t> hot_ttl,
        std::chrono::duration<int64_t> warm_ttl)
        : hot_ttl_(hot_ttl), warm_ttl_(warm_ttl) {}

    StorageTier get_target_tier(const SegmentLifecycle& lifecycle) const override {
        auto age = std::chrono::system_clock::now() - lifecycle.created_at;

        if (age < hot_ttl_) {
            return StorageTier::HOT;
        } else if (age < warm_ttl_) {
            return StorageTier::WARM;
        } else {
            return StorageTier::COLD;
        }
    }

    bool should_transition(
        const SegmentLifecycle& lifecycle,
        StorageTier current_tier,
        StorageTier target_tier) const override {

        return current_tier != target_tier;
    }

private:
    std::chrono::duration<int64_t> hot_ttl_;
    std::chrono::duration<int64_t> warm_ttl_;
};

// Access-frequency policy
class AccessFrequencyPolicy : public LifecyclePolicy {
public:
    AccessFrequencyPolicy(
        uint64_t hot_access_threshold,
        uint64_t warm_access_threshold)
        : hot_access_threshold_(hot_access_threshold)
        , warm_access_threshold_(warm_access_threshold) {}

    StorageTier get_target_tier(const SegmentLifecycle& lifecycle) const override {
        // Calculate accesses per day
        auto age = std::chrono::system_clock::now() - lifecycle.created_at;
        auto days = std::chrono::duration_cast<std::chrono::hours>(age).count() / 24.0;

        if (days == 0) days = 1;

        double accesses_per_day = lifecycle.access_count / days;

        if (accesses_per_day >= hot_access_threshold_) {
            return StorageTier::HOT;
        } else if (accesses_per_day >= warm_access_threshold_) {
            return StorageTier::WARM;
        } else {
            return StorageTier::COLD;
        }
    }

    bool should_transition(
        const SegmentLifecycle& lifecycle,
        StorageTier current_tier,
        StorageTier target_tier) const override {

        // Avoid thrashing (only move if significantly different)
        if (current_tier == StorageTier::HOT && target_tier == StorageTier::WARM) {
            return lifecycle.access_count < warm_access_threshold_ / 2;
        }

        return current_tier != target_tier;
    }

private:
    uint64_t hot_access_threshold_;
    uint64_t warm_access_threshold_;
};

// Composite policy (age + access frequency)
class CompositePolicy : public LifecyclePolicy {
public:
    CompositePolicy(
        std::unique_ptr<LifecyclePolicy> primary,
        std::unique_ptr<LifecyclePolicy> secondary)
        : primary_(std::move(primary))
        , secondary_(std::move(secondary)) {}

    StorageTier get_target_tier(const SegmentLifecycle& lifecycle) const override {
        StorageTier tier1 = primary_->get_target_tier(lifecycle);
        StorageTier tier2 = secondary_->get_target_tier(lifecycle);

        // Take the warmer tier (prefer keeping data accessible)
        return std::min(tier1, tier2);
    }

private:
    std::unique_ptr<LifecyclePolicy> primary_;
    std::unique_ptr<LifecyclePolicy> secondary_;
};
```

## Segment Migration

```cpp
class SegmentMigrator {
public:
    // Migrate segment to different tier
    void migrate(
        Segment* segment,
        StorageTier source_tier,
        StorageTier target_tier,
        const TierConfig& target_config);

private:
    // Migration steps
    void copy_segment_files(
        const std::filesystem::path& source,
        const std::filesystem::path& target);

    void recompress_if_needed(
        const std::filesystem::path& segment_dir,
        CompressionType target_compression);

    void rebuild_memory_structures(
        Segment* segment,
        MemoryMode target_mode);

    void verify_migration(
        const Segment* source,
        const Segment* target);
};

void SegmentMigrator::migrate(
    Segment* segment,
    StorageTier source_tier,
    StorageTier target_tier,
    const TierConfig& target_config) {

    // 1. Create target directory
    auto target_dir = target_config.storage_path / segment->name();
    std::filesystem::create_directories(target_dir);

    // 2. Copy segment files
    auto source_dir = segment->directory();
    copy_segment_files(source_dir, target_dir);

    // 3. Recompress if compression differs
    if (target_config.compression != segment->compression_type()) {
        recompress_if_needed(target_dir, target_config.compression);
    }

    // 4. Rebuild memory structures for target mode
    rebuild_memory_structures(segment, target_config.memory_mode);

    // 5. Verify migration
    verify_migration(segment, target_segment.get());

    // 6. Atomic swap
    segment_manager_->replace_segment(
        segment->name(),
        std::move(target_segment)
    );

    // 7. Delete source files (after successful swap)
    std::filesystem::remove_all(source_dir);
}
```

## Memory Management per Tier

```cpp
class TierMemoryManager {
public:
    explicit TierMemoryManager(const TierConfig& config);

    // Allocate memory for segment
    bool try_allocate(size_t bytes);
    void deallocate(size_t bytes);

    // Memory pressure handling
    bool is_under_pressure() const;
    void evict_lru_segments(size_t target_bytes);

    // Statistics
    size_t allocated_bytes() const { return allocated_; }
    size_t available_bytes() const { return budget_ - allocated_; }
    float utilization() const { return allocated_ / static_cast<float>(budget_); }

private:
    size_t budget_;
    std::atomic<size_t> allocated_{0};

    // LRU tracking
    struct SegmentEntry {
        std::string segment_name;
        size_t bytes;
        std::chrono::system_clock::time_point last_access;
    };
    std::list<SegmentEntry> lru_list_;
    std::unordered_map<std::string, std::list<SegmentEntry>::iterator> lru_map_;

    mutable std::mutex mutex_;
};

bool TierMemoryManager::try_allocate(size_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (allocated_ + bytes > budget_) {
        // Try to evict
        if (is_under_pressure()) {
            evict_lru_segments(bytes);
        }

        if (allocated_ + bytes > budget_) {
            return false;  // Cannot allocate
        }
    }

    allocated_ += bytes;
    return true;
}

void TierMemoryManager::evict_lru_segments(size_t target_bytes) {
    size_t freed = 0;

    while (!lru_list_.empty() && freed < target_bytes) {
        auto& entry = lru_list_.back();

        // Evict segment from memory (unload mmap, clear caches)
        segment_manager_->evict_segment(entry.segment_name);

        freed += entry.bytes;
        deallocate(entry.bytes);

        lru_map_.erase(entry.segment_name);
        lru_list_.pop_back();
    }
}
```

## Query Routing

```cpp
class TierAwareQueryExecutor {
public:
    TopDocs execute_query(const Query& query, uint32_t top_k) {
        // 1. Determine which tiers to search
        auto tiers = select_tiers(query);

        // 2. Search tiers in parallel (hot first for early termination)
        std::vector<std::future<TopDocs>> futures;
        for (auto tier : tiers) {
            futures.push_back(
                thread_pool_->submit([&]() {
                    return search_tier(query, tier, top_k);
                })
            );
        }

        // 3. Merge results
        std::vector<TopDocs> tier_results;
        for (auto& future : futures) {
            tier_results.push_back(future.get());
        }

        return merge_top_docs(tier_results, top_k);
    }

private:
    std::vector<StorageTier> select_tiers(const Query& query) const {
        // If query has time range, only search relevant tiers
        if (auto* range_query = dynamic_cast<const RangeQuery*>(&query)) {
            if (range_query->field() == "timestamp") {
                return tiers_for_time_range(
                    range_query->min_value(),
                    range_query->max_value()
                );
            }
        }

        // Default: search all tiers
        return {StorageTier::HOT, StorageTier::WARM, StorageTier::COLD};
    }

    TopDocs search_tier(
        const Query& query,
        StorageTier tier,
        uint32_t top_k) const {

        auto segments = tier_manager_->get_segments_in_tier(tier);

        // Search segments in tier
        std::vector<TopDocs> segment_results;
        for (auto* segment : segments) {
            segment_results.push_back(
                segment->search(query, top_k)
            );
        }

        return merge_top_docs(segment_results, top_k);
    }
};
```

## Tier Statistics

```cpp
struct TierStatistics {
    StorageTier tier;

    // Segment counts
    uint32_t num_segments;
    uint32_t num_docs;
    uint32_t num_deleted_docs;

    // Storage
    size_t total_bytes;
    size_t compressed_bytes;
    size_t index_bytes;
    size_t column_bytes;

    // Memory
    size_t memory_allocated;
    size_t memory_budget;
    float memory_utilization;

    // Performance
    uint64_t query_count;
    uint64_t query_time_ms;
    float avg_query_latency_ms;

    // Lifecycle
    uint32_t migrations_in;
    uint32_t migrations_out;
    std::chrono::system_clock::time_point oldest_segment;
    std::chrono::system_clock::time_point newest_segment;
};

class TierMetrics {
public:
    void record_query(StorageTier tier, std::chrono::milliseconds latency);
    void record_migration(StorageTier from, StorageTier to, size_t bytes);
    void record_memory_usage(StorageTier tier, size_t bytes);

    TierStatistics get_statistics(StorageTier tier) const;

    // Export to monitoring systems
    void export_prometheus_metrics(std::ostream& out) const;

private:
    std::unordered_map<StorageTier, TierStatistics> stats_;
    mutable std::shared_mutex mutex_;
};
```

## Configuration Examples

```cpp
// Example 1: Standard three-tier setup
std::vector<TierConfig> create_standard_tiers() {
    std::vector<TierConfig> configs;

    // Hot tier: Recent data, fast access
    TierConfig hot;
    hot.tier = StorageTier::HOT;
    hot.storage_path = "/mnt/nvme/index/hot";
    hot.memory_budget = 16ULL * 1024 * 1024 * 1024;  // 16GB
    hot.memory_mode = MemoryMode::MMAP;
    hot.min_age = std::chrono::hours(0);
    hot.max_age = std::chrono::days(7);
    hot.compression = CompressionType::LZ4;
    hot.compression_level = 1;
    configs.push_back(hot);

    // Warm tier: Older searchable data
    TierConfig warm;
    warm.tier = StorageTier::WARM;
    warm.storage_path = "/mnt/ssd/index/warm";
    warm.memory_budget = 4ULL * 1024 * 1024 * 1024;  // 4GB
    warm.memory_mode = MemoryMode::MMAP;
    warm.min_age = std::chrono::days(7);
    warm.max_age = std::chrono::days(90);
    warm.compression = CompressionType::ZSTD;
    warm.compression_level = 3;
    configs.push_back(warm);

    // Cold tier: Archived data
    TierConfig cold;
    cold.tier = StorageTier::COLD;
    cold.storage_path = "/mnt/hdd/index/cold";
    cold.memory_budget = 1ULL * 1024 * 1024 * 1024;  // 1GB
    cold.memory_mode = MemoryMode::DIRECT_IO;
    cold.min_age = std::chrono::days(90);
    cold.max_age = std::chrono::days(36500);  // ~100 years
    cold.compression = CompressionType::ZSTD;
    cold.compression_level = 9;
    configs.push_back(cold);

    return configs;
}

// Example 2: All in-memory for low-latency
std::vector<TierConfig> create_inmemory_tier() {
    TierConfig config;
    config.tier = StorageTier::HOT;
    config.storage_path = "/tmp/index";
    config.memory_budget = 128ULL * 1024 * 1024 * 1024;  // 128GB
    config.memory_mode = MemoryMode::IN_MEMORY;
    config.compression = CompressionType::NONE;

    return {config};
}
```

## Testing

```cpp
class TierManagerTest {
    void test_age_based_migration() {
        TierManager manager(create_standard_tiers());

        // Add segment to hot tier
        auto segment = create_test_segment();
        manager.add_segment(std::move(segment), StorageTier::HOT);

        // Simulate 8 days passing
        advance_time(std::chrono::days(8));

        // Check lifecycle
        manager.check_lifecycle_policies();

        // Segment should be in warm tier
        auto stats = manager.get_tier_stats(StorageTier::WARM);
        ASSERT_EQ(stats.num_segments, 1);
    }

    void test_memory_pressure_eviction() {
        TierConfig config;
        config.memory_budget = 1024 * 1024;  // 1MB

        TierMemoryManager memory_mgr(config);

        // Allocate until pressure
        bool allocated = memory_mgr.try_allocate(900 * 1024);
        ASSERT_TRUE(allocated);

        // This should trigger eviction
        allocated = memory_mgr.try_allocate(200 * 1024);
        ASSERT_TRUE(allocated);  // Should succeed after eviction
    }
};
```
