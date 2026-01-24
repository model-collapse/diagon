// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/storage/StorageTier.h"

#include "diagon/storage/LifecyclePolicy.h"
#include "diagon/storage/TierManager.h"
#include "diagon/storage/TierMigrationService.h"

#include <gtest/gtest.h>

#include <thread>

using namespace diagon::storage;

// ==================== StorageTier Tests ====================

TEST(StorageTierTest, EnumValues) {
    EXPECT_EQ(0, static_cast<uint8_t>(StorageTier::HOT));
    EXPECT_EQ(1, static_cast<uint8_t>(StorageTier::WARM));
    EXPECT_EQ(2, static_cast<uint8_t>(StorageTier::COLD));
    EXPECT_EQ(3, static_cast<uint8_t>(StorageTier::FROZEN));
}

TEST(StorageTierTest, ToString) {
    EXPECT_STREQ("hot", toString(StorageTier::HOT));
    EXPECT_STREQ("warm", toString(StorageTier::WARM));
    EXPECT_STREQ("cold", toString(StorageTier::COLD));
    EXPECT_STREQ("frozen", toString(StorageTier::FROZEN));
}

// ==================== TierConfig Tests ====================

TEST(TierConfigTest, Construction) {
    TierConfig config{.tier = StorageTier::HOT,
                      .directory_type = "MMapDirectory",
                      .base_path = "/mnt/nvme",
                      .max_cache_bytes = 16ULL * 1024 * 1024 * 1024,  // 16GB
                      .use_mmap = true,
                      .read_ahead_bytes = 1024 * 1024,
                      .searchable = true,
                      .use_skip_indexes = true,
                      .max_concurrent_queries = 100,
                      .writable = true,
                      .compress_on_migrate = false,
                      .compression_codec = "LZ4"};

    EXPECT_EQ(StorageTier::HOT, config.tier);
    EXPECT_EQ("MMapDirectory", config.directory_type);
    EXPECT_EQ("/mnt/nvme", config.base_path);
    EXPECT_EQ(16ULL * 1024 * 1024 * 1024, config.max_cache_bytes);
    EXPECT_TRUE(config.use_mmap);
    EXPECT_EQ(1024 * 1024, config.read_ahead_bytes);
    EXPECT_TRUE(config.searchable);
    EXPECT_TRUE(config.use_skip_indexes);
    EXPECT_EQ(100, config.max_concurrent_queries);
    EXPECT_TRUE(config.writable);
    EXPECT_FALSE(config.compress_on_migrate);
    EXPECT_EQ("LZ4", config.compression_codec);
}

// ==================== LifecyclePolicy Tests ====================

TEST(LifecyclePolicyTest, DefaultValues) {
    LifecyclePolicy policy;

    // Hot phase
    EXPECT_EQ(7 * 24 * 3600, policy.hot.max_age_seconds);
    EXPECT_EQ(50LL * 1024 * 1024 * 1024, policy.hot.max_size_bytes);
    EXPECT_TRUE(policy.hot.force_merge);
    EXPECT_EQ(1, policy.hot.merge_max_segments);

    // Warm phase
    EXPECT_EQ(30 * 24 * 3600, policy.warm.max_age_seconds);
    EXPECT_EQ(10, policy.warm.min_access_count);
    EXPECT_TRUE(policy.warm.recompress);
    EXPECT_TRUE(policy.warm.delete_after_migrate);

    // Cold phase
    EXPECT_EQ(365 * 24 * 3600, policy.cold.max_age_seconds);
    EXPECT_TRUE(policy.cold.readonly_mode);
    EXPECT_TRUE(policy.cold.retained_columns.empty());

    // Frozen phase
    EXPECT_EQ(-1, policy.frozen.max_age_seconds);
}

TEST(LifecyclePolicyTest, EvaluateSegmentHotToWarmByAge) {
    LifecyclePolicy policy;
    policy.hot.max_age_seconds = 7 * 24 * 3600;  // 7 days

    // Segment older than 7 days should move to WARM
    auto result = policy.evaluateSegment(StorageTier::HOT,
                                         8 * 24 * 3600,  // 8 days old
                                         1024,           // 1KB size
                                         100             // 100 accesses
    );

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(StorageTier::WARM, *result);
}

TEST(LifecyclePolicyTest, EvaluateSegmentHotToWarmBySize) {
    LifecyclePolicy policy;
    policy.hot.max_size_bytes = 50LL * 1024 * 1024 * 1024;  // 50GB

    // Segment larger than 50GB should move to WARM
    auto result = policy.evaluateSegment(StorageTier::HOT,
                                         3600,                       // 1 hour old
                                         60LL * 1024 * 1024 * 1024,  // 60GB size
                                         100                         // 100 accesses
    );

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(StorageTier::WARM, *result);
}

TEST(LifecyclePolicyTest, EvaluateSegmentHotNoTransition) {
    LifecyclePolicy policy;

    // Young and small segment should stay in HOT
    auto result = policy.evaluateSegment(StorageTier::HOT,
                                         3600,  // 1 hour old
                                         1024,  // 1KB size
                                         100    // 100 accesses
    );

    EXPECT_FALSE(result.has_value());
}

TEST(LifecyclePolicyTest, EvaluateSegmentWarmToColdByAge) {
    LifecyclePolicy policy;
    policy.warm.max_age_seconds = 30 * 24 * 3600;  // 30 days

    // Segment older than 30 days should move to COLD
    auto result = policy.evaluateSegment(StorageTier::WARM,
                                         35 * 24 * 3600,  // 35 days old
                                         1024,            // 1KB size
                                         50               // 50 accesses
    );

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(StorageTier::COLD, *result);
}

TEST(LifecyclePolicyTest, EvaluateSegmentWarmToColdByAccessCount) {
    LifecyclePolicy policy;
    policy.warm.min_access_count = 10;

    // Segment with < 10 accesses should move to COLD
    auto result = policy.evaluateSegment(StorageTier::WARM,
                                         10 * 24 * 3600,  // 10 days old
                                         1024,            // 1KB size
                                         5                // 5 accesses (below threshold)
    );

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(StorageTier::COLD, *result);
}

TEST(LifecyclePolicyTest, EvaluateSegmentWarmNoTransition) {
    LifecyclePolicy policy;

    // Recent segment with good access count should stay in WARM
    auto result = policy.evaluateSegment(StorageTier::WARM,
                                         10 * 24 * 3600,  // 10 days old
                                         1024,            // 1KB size
                                         50               // 50 accesses
    );

    EXPECT_FALSE(result.has_value());
}

TEST(LifecyclePolicyTest, EvaluateSegmentColdToFrozen) {
    LifecyclePolicy policy;
    policy.cold.max_age_seconds = 365 * 24 * 3600;  // 365 days

    // Segment older than 365 days should move to FROZEN
    auto result = policy.evaluateSegment(StorageTier::COLD,
                                         400 * 24 * 3600,  // 400 days old
                                         1024,             // 1KB size
                                         0                 // 0 accesses
    );

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(StorageTier::FROZEN, *result);
}

TEST(LifecyclePolicyTest, EvaluateSegmentColdNoTransition) {
    LifecyclePolicy policy;
    policy.cold.max_age_seconds = 365 * 24 * 3600;  // 365 days

    // Segment less than 365 days old should stay in COLD
    auto result = policy.evaluateSegment(StorageTier::COLD,
                                         100 * 24 * 3600,  // 100 days old
                                         1024,             // 1KB size
                                         0                 // 0 accesses
    );

    EXPECT_FALSE(result.has_value());
}

TEST(LifecyclePolicyTest, EvaluateSegmentFrozenTerminal) {
    LifecyclePolicy policy;

    // FROZEN is terminal - no further transitions
    auto result = policy.evaluateSegment(StorageTier::FROZEN,
                                         1000 * 24 * 3600,  // 1000 days old
                                         1024,              // 1KB size
                                         0                  // 0 accesses
    );

    EXPECT_FALSE(result.has_value());
}

// ==================== TierManager Tests ====================

TEST(TierManagerTest, Construction) {
    std::map<StorageTier, TierConfig> configs;
    configs[StorageTier::HOT] = TierConfig{.tier = StorageTier::HOT,
                                           .directory_type = "MMapDirectory",
                                           .base_path = "/mnt/nvme",
                                           .max_cache_bytes = 16ULL * 1024 * 1024 * 1024,
                                           .use_mmap = true,
                                           .read_ahead_bytes = 1024 * 1024,
                                           .searchable = true,
                                           .use_skip_indexes = true,
                                           .max_concurrent_queries = 100,
                                           .writable = true,
                                           .compress_on_migrate = false,
                                           .compression_codec = "LZ4"};

    LifecyclePolicy policy;

    TierManager manager(configs, policy);

    // Should not throw
    EXPECT_NO_THROW(manager.getConfig(StorageTier::HOT));
}

TEST(TierManagerTest, RegisterSegment) {
    std::map<StorageTier, TierConfig> configs;
    configs[StorageTier::HOT] = TierConfig{.tier = StorageTier::HOT,
                                           .directory_type = "MMapDirectory",
                                           .base_path = "/mnt/nvme",
                                           .max_cache_bytes = 16ULL * 1024 * 1024 * 1024,
                                           .use_mmap = true,
                                           .read_ahead_bytes = 1024 * 1024,
                                           .searchable = true,
                                           .use_skip_indexes = true,
                                           .max_concurrent_queries = 100,
                                           .writable = true,
                                           .compress_on_migrate = false,
                                           .compression_codec = "LZ4"};

    LifecyclePolicy policy;
    TierManager manager(configs, policy);

    manager.registerSegment("segment_001", 1024 * 1024);

    EXPECT_EQ(StorageTier::HOT, manager.getSegmentTier("segment_001"));
    EXPECT_EQ(0, manager.getAccessCount("segment_001"));
}

TEST(TierManagerTest, GetSegmentTierUnknown) {
    std::map<StorageTier, TierConfig> configs;
    configs[StorageTier::HOT] = TierConfig{.tier = StorageTier::HOT,
                                           .directory_type = "MMapDirectory",
                                           .base_path = "/mnt/nvme",
                                           .max_cache_bytes = 16ULL * 1024 * 1024 * 1024,
                                           .use_mmap = true,
                                           .read_ahead_bytes = 1024 * 1024,
                                           .searchable = true,
                                           .use_skip_indexes = true,
                                           .max_concurrent_queries = 100,
                                           .writable = true,
                                           .compress_on_migrate = false,
                                           .compression_codec = "LZ4"};

    LifecyclePolicy policy;
    TierManager manager(configs, policy);

    EXPECT_THROW(manager.getSegmentTier("unknown_segment"), std::invalid_argument);
}

TEST(TierManagerTest, RecordAccess) {
    std::map<StorageTier, TierConfig> configs;
    configs[StorageTier::HOT] = TierConfig{.tier = StorageTier::HOT,
                                           .directory_type = "MMapDirectory",
                                           .base_path = "/mnt/nvme",
                                           .max_cache_bytes = 16ULL * 1024 * 1024 * 1024,
                                           .use_mmap = true,
                                           .read_ahead_bytes = 1024 * 1024,
                                           .searchable = true,
                                           .use_skip_indexes = true,
                                           .max_concurrent_queries = 100,
                                           .writable = true,
                                           .compress_on_migrate = false,
                                           .compression_codec = "LZ4"};

    LifecyclePolicy policy;
    TierManager manager(configs, policy);

    manager.registerSegment("segment_001", 1024 * 1024);

    EXPECT_EQ(0, manager.getAccessCount("segment_001"));

    manager.recordAccess("segment_001");
    EXPECT_EQ(1, manager.getAccessCount("segment_001"));

    manager.recordAccess("segment_001");
    manager.recordAccess("segment_001");
    EXPECT_EQ(3, manager.getAccessCount("segment_001"));
}

TEST(TierManagerTest, GetConfig) {
    std::map<StorageTier, TierConfig> configs;
    configs[StorageTier::HOT] = TierConfig{.tier = StorageTier::HOT,
                                           .directory_type = "MMapDirectory",
                                           .base_path = "/mnt/nvme",
                                           .max_cache_bytes = 16ULL * 1024 * 1024 * 1024,
                                           .use_mmap = true,
                                           .read_ahead_bytes = 1024 * 1024,
                                           .searchable = true,
                                           .use_skip_indexes = true,
                                           .max_concurrent_queries = 100,
                                           .writable = true,
                                           .compress_on_migrate = false,
                                           .compression_codec = "LZ4"};

    LifecyclePolicy policy;
    TierManager manager(configs, policy);

    const auto& config = manager.getConfig(StorageTier::HOT);

    EXPECT_EQ(StorageTier::HOT, config.tier);
    EXPECT_EQ("MMapDirectory", config.directory_type);
}

TEST(TierManagerTest, GetConfigUnconfiguredTier) {
    std::map<StorageTier, TierConfig> configs;
    configs[StorageTier::HOT] = TierConfig{.tier = StorageTier::HOT,
                                           .directory_type = "MMapDirectory",
                                           .base_path = "/mnt/nvme",
                                           .max_cache_bytes = 16ULL * 1024 * 1024 * 1024,
                                           .use_mmap = true,
                                           .read_ahead_bytes = 1024 * 1024,
                                           .searchable = true,
                                           .use_skip_indexes = true,
                                           .max_concurrent_queries = 100,
                                           .writable = true,
                                           .compress_on_migrate = false,
                                           .compression_codec = "LZ4"};

    LifecyclePolicy policy;
    TierManager manager(configs, policy);

    EXPECT_THROW(manager.getConfig(StorageTier::COLD), std::invalid_argument);
}

TEST(TierManagerTest, MigrateSegment) {
    std::map<StorageTier, TierConfig> configs;
    configs[StorageTier::HOT] = TierConfig{.tier = StorageTier::HOT,
                                           .directory_type = "MMapDirectory",
                                           .base_path = "/mnt/nvme",
                                           .max_cache_bytes = 16ULL * 1024 * 1024 * 1024,
                                           .use_mmap = true,
                                           .read_ahead_bytes = 1024 * 1024,
                                           .searchable = true,
                                           .use_skip_indexes = true,
                                           .max_concurrent_queries = 100,
                                           .writable = true,
                                           .compress_on_migrate = false,
                                           .compression_codec = "LZ4"};
    configs[StorageTier::WARM] = TierConfig{.tier = StorageTier::WARM,
                                            .directory_type = "FSDirectory",
                                            .base_path = "/mnt/ssd",
                                            .max_cache_bytes = 4ULL * 1024 * 1024 * 1024,
                                            .use_mmap = false,
                                            .read_ahead_bytes = 256 * 1024,
                                            .searchable = true,
                                            .use_skip_indexes = true,
                                            .max_concurrent_queries = 50,
                                            .writable = false,
                                            .compress_on_migrate = true,
                                            .compression_codec = "ZSTD"};

    LifecyclePolicy policy;
    TierManager manager(configs, policy);

    manager.registerSegment("segment_001", 1024 * 1024);
    EXPECT_EQ(StorageTier::HOT, manager.getSegmentTier("segment_001"));

    manager.migrateSegment("segment_001", StorageTier::WARM);
    EXPECT_EQ(StorageTier::WARM, manager.getSegmentTier("segment_001"));
}

TEST(TierManagerTest, MigrateSegmentSameTier) {
    std::map<StorageTier, TierConfig> configs;
    configs[StorageTier::HOT] = TierConfig{.tier = StorageTier::HOT,
                                           .directory_type = "MMapDirectory",
                                           .base_path = "/mnt/nvme",
                                           .max_cache_bytes = 16ULL * 1024 * 1024 * 1024,
                                           .use_mmap = true,
                                           .read_ahead_bytes = 1024 * 1024,
                                           .searchable = true,
                                           .use_skip_indexes = true,
                                           .max_concurrent_queries = 100,
                                           .writable = true,
                                           .compress_on_migrate = false,
                                           .compression_codec = "LZ4"};

    LifecyclePolicy policy;
    TierManager manager(configs, policy);

    manager.registerSegment("segment_001", 1024 * 1024);

    // Migrating to same tier should be no-op
    EXPECT_NO_THROW(manager.migrateSegment("segment_001", StorageTier::HOT));
    EXPECT_EQ(StorageTier::HOT, manager.getSegmentTier("segment_001"));
}

TEST(TierManagerTest, GetSearchableTiers) {
    std::map<StorageTier, TierConfig> configs;
    configs[StorageTier::HOT] = TierConfig{.tier = StorageTier::HOT,
                                           .directory_type = "MMapDirectory",
                                           .base_path = "/mnt/nvme",
                                           .max_cache_bytes = 16ULL * 1024 * 1024 * 1024,
                                           .use_mmap = true,
                                           .read_ahead_bytes = 1024 * 1024,
                                           .searchable = true,
                                           .use_skip_indexes = true,
                                           .max_concurrent_queries = 100,
                                           .writable = true,
                                           .compress_on_migrate = false,
                                           .compression_codec = "LZ4"};
    configs[StorageTier::WARM] = TierConfig{.tier = StorageTier::WARM,
                                            .directory_type = "FSDirectory",
                                            .base_path = "/mnt/ssd",
                                            .max_cache_bytes = 4ULL * 1024 * 1024 * 1024,
                                            .use_mmap = false,
                                            .read_ahead_bytes = 256 * 1024,
                                            .searchable = true,
                                            .use_skip_indexes = true,
                                            .max_concurrent_queries = 50,
                                            .writable = false,
                                            .compress_on_migrate = true,
                                            .compression_codec = "ZSTD"};
    configs[StorageTier::COLD] = TierConfig{.tier = StorageTier::COLD,
                                            .directory_type = "S3Directory",
                                            .base_path = "s3://bucket",
                                            .max_cache_bytes = 512ULL * 1024 * 1024,
                                            .use_mmap = false,
                                            .read_ahead_bytes = 64 * 1024,
                                            .searchable = false,  // Not searchable by default
                                            .use_skip_indexes = true,
                                            .max_concurrent_queries = 10,
                                            .writable = false,
                                            .compress_on_migrate = true,
                                            .compression_codec = "ZSTD"};

    LifecyclePolicy policy;
    TierManager manager(configs, policy);

    auto searchable_tiers = manager.getSearchableTiers();

    EXPECT_EQ(2, searchable_tiers.size());
    EXPECT_NE(searchable_tiers.end(),
              std::find(searchable_tiers.begin(), searchable_tiers.end(), StorageTier::HOT));
    EXPECT_NE(searchable_tiers.end(),
              std::find(searchable_tiers.begin(), searchable_tiers.end(), StorageTier::WARM));
    EXPECT_EQ(searchable_tiers.end(),
              std::find(searchable_tiers.begin(), searchable_tiers.end(), StorageTier::COLD));
}

TEST(TierManagerTest, GetSegmentsInTiers) {
    std::map<StorageTier, TierConfig> configs;
    configs[StorageTier::HOT] = TierConfig{.tier = StorageTier::HOT,
                                           .directory_type = "MMapDirectory",
                                           .base_path = "/mnt/nvme",
                                           .max_cache_bytes = 16ULL * 1024 * 1024 * 1024,
                                           .use_mmap = true,
                                           .read_ahead_bytes = 1024 * 1024,
                                           .searchable = true,
                                           .use_skip_indexes = true,
                                           .max_concurrent_queries = 100,
                                           .writable = true,
                                           .compress_on_migrate = false,
                                           .compression_codec = "LZ4"};
    configs[StorageTier::WARM] = TierConfig{.tier = StorageTier::WARM,
                                            .directory_type = "FSDirectory",
                                            .base_path = "/mnt/ssd",
                                            .max_cache_bytes = 4ULL * 1024 * 1024 * 1024,
                                            .use_mmap = false,
                                            .read_ahead_bytes = 256 * 1024,
                                            .searchable = true,
                                            .use_skip_indexes = true,
                                            .max_concurrent_queries = 50,
                                            .writable = false,
                                            .compress_on_migrate = true,
                                            .compression_codec = "ZSTD"};

    LifecyclePolicy policy;
    TierManager manager(configs, policy);

    manager.registerSegment("segment_001", 1024 * 1024);
    manager.registerSegment("segment_002", 1024 * 1024);
    manager.registerSegment("segment_003", 1024 * 1024);

    // Migrate some segments to WARM
    manager.migrateSegment("segment_002", StorageTier::WARM);

    auto hot_segments = manager.getSegmentsInTiers({StorageTier::HOT});
    EXPECT_EQ(2, hot_segments.size());

    auto warm_segments = manager.getSegmentsInTiers({StorageTier::WARM});
    EXPECT_EQ(1, warm_segments.size());

    auto all_segments = manager.getSegmentsInTiers({StorageTier::HOT, StorageTier::WARM});
    EXPECT_EQ(3, all_segments.size());
}

TEST(TierManagerTest, EvaluateMigrations) {
    std::map<StorageTier, TierConfig> configs;
    configs[StorageTier::HOT] = TierConfig{.tier = StorageTier::HOT,
                                           .directory_type = "MMapDirectory",
                                           .base_path = "/mnt/nvme",
                                           .max_cache_bytes = 16ULL * 1024 * 1024 * 1024,
                                           .use_mmap = true,
                                           .read_ahead_bytes = 1024 * 1024,
                                           .searchable = true,
                                           .use_skip_indexes = true,
                                           .max_concurrent_queries = 100,
                                           .writable = true,
                                           .compress_on_migrate = false,
                                           .compression_codec = "LZ4"};

    LifecyclePolicy policy;
    policy.hot.max_age_seconds = 1;  // 1 second

    TierManager manager(configs, policy);

    manager.registerSegment("segment_001", 1024 * 1024);

    // Wait for segment to age
    std::this_thread::sleep_for(std::chrono::seconds(2));

    auto migrations = manager.evaluateMigrations();

    EXPECT_EQ(1, migrations.size());
    EXPECT_EQ("segment_001", migrations[0].first);
    EXPECT_EQ(StorageTier::WARM, migrations[0].second);
}

// ==================== TierMigrationService Tests ====================

TEST(TierMigrationServiceTest, Construction) {
    std::map<StorageTier, TierConfig> configs;
    configs[StorageTier::HOT] = TierConfig{.tier = StorageTier::HOT,
                                           .directory_type = "MMapDirectory",
                                           .base_path = "/mnt/nvme",
                                           .max_cache_bytes = 16ULL * 1024 * 1024 * 1024,
                                           .use_mmap = true,
                                           .read_ahead_bytes = 1024 * 1024,
                                           .searchable = true,
                                           .use_skip_indexes = true,
                                           .max_concurrent_queries = 100,
                                           .writable = true,
                                           .compress_on_migrate = false,
                                           .compression_codec = "LZ4"};

    LifecyclePolicy policy;
    TierManager manager(configs, policy);

    TierMigrationService service(manager, std::chrono::seconds(10));

    EXPECT_FALSE(service.isRunning());
    EXPECT_EQ(std::chrono::seconds(10), service.getCheckInterval());
}

TEST(TierMigrationServiceTest, StartStop) {
    std::map<StorageTier, TierConfig> configs;
    configs[StorageTier::HOT] = TierConfig{.tier = StorageTier::HOT,
                                           .directory_type = "MMapDirectory",
                                           .base_path = "/mnt/nvme",
                                           .max_cache_bytes = 16ULL * 1024 * 1024 * 1024,
                                           .use_mmap = true,
                                           .read_ahead_bytes = 1024 * 1024,
                                           .searchable = true,
                                           .use_skip_indexes = true,
                                           .max_concurrent_queries = 100,
                                           .writable = true,
                                           .compress_on_migrate = false,
                                           .compression_codec = "LZ4"};

    LifecyclePolicy policy;
    TierManager manager(configs, policy);

    TierMigrationService service(manager, std::chrono::seconds(3600));

    EXPECT_FALSE(service.isRunning());

    service.start();
    EXPECT_TRUE(service.isRunning());

    service.stop();
    EXPECT_FALSE(service.isRunning());
}

TEST(TierMigrationServiceTest, SetCheckInterval) {
    std::map<StorageTier, TierConfig> configs;
    configs[StorageTier::HOT] = TierConfig{.tier = StorageTier::HOT,
                                           .directory_type = "MMapDirectory",
                                           .base_path = "/mnt/nvme",
                                           .max_cache_bytes = 16ULL * 1024 * 1024 * 1024,
                                           .use_mmap = true,
                                           .read_ahead_bytes = 1024 * 1024,
                                           .searchable = true,
                                           .use_skip_indexes = true,
                                           .max_concurrent_queries = 100,
                                           .writable = true,
                                           .compress_on_migrate = false,
                                           .compression_codec = "LZ4"};

    LifecyclePolicy policy;
    TierManager manager(configs, policy);

    TierMigrationService service(manager);

    EXPECT_EQ(std::chrono::hours(1), service.getCheckInterval());

    service.setCheckInterval(std::chrono::seconds(30));
    EXPECT_EQ(std::chrono::seconds(30), service.getCheckInterval());
}

TEST(TierMigrationServiceTest, AutomaticMigration) {
    std::map<StorageTier, TierConfig> configs;
    configs[StorageTier::HOT] = TierConfig{.tier = StorageTier::HOT,
                                           .directory_type = "MMapDirectory",
                                           .base_path = "/mnt/nvme",
                                           .max_cache_bytes = 16ULL * 1024 * 1024 * 1024,
                                           .use_mmap = true,
                                           .read_ahead_bytes = 1024 * 1024,
                                           .searchable = true,
                                           .use_skip_indexes = true,
                                           .max_concurrent_queries = 100,
                                           .writable = true,
                                           .compress_on_migrate = false,
                                           .compression_codec = "LZ4"};

    LifecyclePolicy policy;
    policy.hot.max_age_seconds = 1;      // 1 second
    policy.warm.min_access_count = 0;    // Don't migrate to COLD based on access count
    policy.warm.max_age_seconds = 3600;  // 1 hour

    TierManager manager(configs, policy);

    manager.registerSegment("segment_001", 1024 * 1024);

    TierMigrationService service(manager, std::chrono::seconds(2));
    service.start();

    // Wait for migration to occur
    std::this_thread::sleep_for(std::chrono::seconds(5));

    service.stop();

    // Segment should have been migrated to WARM
    EXPECT_EQ(StorageTier::WARM, manager.getSegmentTier("segment_001"));
}
