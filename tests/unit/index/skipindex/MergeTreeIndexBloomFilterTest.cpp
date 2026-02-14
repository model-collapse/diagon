// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/skipindex/MergeTreeIndexBloomFilter.h"

#include "diagon/store/ByteBuffersIndexInput.h"
#include "diagon/store/ByteBuffersIndexOutput.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace diagon::index::skipindex;
using namespace diagon::store;

// ==================== Index Factory Tests ====================

TEST(MergeTreeIndexBloomFilterTest, Construction) {
    MergeTreeIndexBloomFilter index("test_idx", {"col1", "col2"},
                                    1,   // granularity
                                    8,   // bits_per_row
                                    3);  // hash_functions

    EXPECT_EQ("skp_idx_test_idx", index.getFileName());
    EXPECT_EQ(".idx", index.getFileExtension());
    EXPECT_EQ(1, index.getGranularity());
    EXPECT_EQ(8, index.bitsPerRow());
    EXPECT_EQ(3, index.hashFunctions());
    EXPECT_EQ(2, index.columns().size());
}

TEST(MergeTreeIndexBloomFilterTest, ConstructionValidation) {
    // Empty columns
    EXPECT_THROW(MergeTreeIndexBloomFilter("test", {}, 1, 8, 3), std::invalid_argument);

    // Zero bits per row
    EXPECT_THROW(MergeTreeIndexBloomFilter("test", {"col1"}, 1, 0, 3), std::invalid_argument);

    // Zero hash functions
    EXPECT_THROW(MergeTreeIndexBloomFilter("test", {"col1"}, 1, 8, 0), std::invalid_argument);
}

TEST(MergeTreeIndexBloomFilterTest, CreateGranule) {
    MergeTreeIndexBloomFilter index("test", {"col1"}, 1, 8, 3);

    auto granule = index.createIndexGranule();
    ASSERT_NE(nullptr, granule);
    EXPECT_TRUE(granule->empty());
}

TEST(MergeTreeIndexBloomFilterTest, CreateAggregator) {
    MergeTreeIndexBloomFilter index("test", {"col1", "col2"}, 1, 8, 3);

    auto aggregator = index.createIndexAggregator();
    ASSERT_NE(nullptr, aggregator);
    EXPECT_TRUE(aggregator->empty());
}

TEST(MergeTreeIndexBloomFilterTest, CreateCondition) {
    MergeTreeIndexBloomFilter index("test", {"col1"}, 1, 8, 3);

    auto condition = index.createIndexCondition();
    ASSERT_NE(nullptr, condition);
    EXPECT_TRUE(condition->alwaysUnknownOrTrue());  // No predicates yet
}

// ==================== Aggregator Tests ====================

TEST(MergeTreeIndexBloomFilterAggregatorTest, AddRow) {
    MergeTreeIndexAggregatorBloomFilter agg(8, 3, {"col1", "col2"});

    EXPECT_TRUE(agg.empty());

    // Add single row
    std::vector<uint64_t> row1 = {0x1111111111111111ULL, 0x2222222222222222ULL};
    agg.addRow(row1);

    EXPECT_FALSE(agg.empty());
}

TEST(MergeTreeIndexBloomFilterAggregatorTest, AddRowValidation) {
    MergeTreeIndexAggregatorBloomFilter agg(8, 3, {"col1", "col2"});

    // Wrong number of columns
    std::vector<uint64_t> row = {0x1111111111111111ULL};
    EXPECT_THROW(agg.addRow(row), std::invalid_argument);
}

TEST(MergeTreeIndexBloomFilterAggregatorTest, UpdateMultipleRows) {
    MergeTreeIndexAggregatorBloomFilter agg(8, 3, {"col1", "col2"});

    // Add multiple rows
    std::vector<std::vector<uint64_t>> column_hashes = {
        {0x1111111111111111ULL, 0x3333333333333333ULL, 0x5555555555555555ULL},  // col1
        {0x2222222222222222ULL, 0x4444444444444444ULL, 0x6666666666666666ULL}   // col2
    };

    agg.update(column_hashes);
    EXPECT_FALSE(agg.empty());
}

TEST(MergeTreeIndexBloomFilterAggregatorTest, GetGranuleAndReset) {
    MergeTreeIndexAggregatorBloomFilter agg(8, 3, {"col1"});

    // Add some rows
    agg.addRow({0x1111111111111111ULL});
    agg.addRow({0x2222222222222222ULL});
    agg.addRow({0x3333333333333333ULL});

    // Get granule
    auto granule_ptr = agg.getGranuleAndReset();
    ASSERT_NE(nullptr, granule_ptr);

    auto* granule = dynamic_cast<MergeTreeIndexGranuleBloomFilter*>(granule_ptr.get());
    ASSERT_NE(nullptr, granule);
    EXPECT_FALSE(granule->empty());
    EXPECT_EQ(3, granule->totalRows());

    // Aggregator should be reset
    EXPECT_TRUE(agg.empty());
}

TEST(MergeTreeIndexBloomFilterAggregatorTest, MultipleGranules) {
    MergeTreeIndexAggregatorBloomFilter agg(8, 3, {"col1"});

    // First granule
    agg.addRow({0x1111111111111111ULL});
    auto granule1 = agg.getGranuleAndReset();
    EXPECT_FALSE(granule1->empty());

    // Second granule
    agg.addRow({0x2222222222222222ULL});
    auto granule2 = agg.getGranuleAndReset();
    EXPECT_FALSE(granule2->empty());

    // Granules should be independent
    EXPECT_NE(granule1.get(), granule2.get());
}

// ==================== Granule Serialization Tests ====================

TEST(MergeTreeIndexGranuleBloomFilterTest, SerializeEmpty) {
    MergeTreeIndexGranuleBloomFilter granule(8, 3, 2);

    // Serialize
    ByteBuffersIndexOutput output("test");
    granule.serialize(&output);

    // Deserialize
    ByteBuffersIndexInput input("test", output.toArrayCopy());
    MergeTreeIndexGranuleBloomFilter loaded(8, 3, 2);
    loaded.deserialize(&input, BLOOM_FILTER_VERSION_V1);

    EXPECT_TRUE(loaded.empty());
    EXPECT_EQ(0, loaded.totalRows());
}

TEST(MergeTreeIndexGranuleBloomFilterTest, SerializeNonEmpty) {
    // Create and populate aggregator
    MergeTreeIndexAggregatorBloomFilter agg(8, 3, {"col1", "col2"});
    agg.addRow({0x1111111111111111ULL, 0x2222222222222222ULL});
    agg.addRow({0x3333333333333333ULL, 0x4444444444444444ULL});

    // Get granule
    auto granule_ptr = agg.getGranuleAndReset();
    auto* granule = dynamic_cast<MergeTreeIndexGranuleBloomFilter*>(granule_ptr.get());
    ASSERT_NE(nullptr, granule);

    // Serialize
    ByteBuffersIndexOutput output("test");
    granule->serialize(&output);

    // Deserialize
    ByteBuffersIndexInput input("test", output.toArrayCopy());
    MergeTreeIndexGranuleBloomFilter loaded(8, 3, 2);
    loaded.deserialize(&input, BLOOM_FILTER_VERSION_V1);

    EXPECT_FALSE(loaded.empty());
    EXPECT_EQ(2, loaded.totalRows());
    EXPECT_EQ(2, loaded.getFilters().size());
}

TEST(MergeTreeIndexGranuleBloomFilterTest, SerializePreservesData) {
    // Create and populate
    MergeTreeIndexAggregatorBloomFilter agg(16, 5, {"col1"});

    uint64_t test_hashes[] = {0x1111111111111111ULL, 0x2222222222222222ULL, 0x3333333333333333ULL};

    for (uint64_t hash : test_hashes) {
        agg.addRow({hash});
    }

    auto granule_ptr = agg.getGranuleAndReset();
    auto* granule = dynamic_cast<MergeTreeIndexGranuleBloomFilter*>(granule_ptr.get());

    // Serialize and deserialize
    ByteBuffersIndexOutput output("test");
    granule->serialize(&output);

    ByteBuffersIndexInput input("test", output.toArrayCopy());
    MergeTreeIndexGranuleBloomFilter loaded(16, 5, 1);
    loaded.deserialize(&input, BLOOM_FILTER_VERSION_V1);

    // Check that bloom filter contains the hashes
    const auto& filters = loaded.getFilters();
    ASSERT_EQ(1, filters.size());
    ASSERT_NE(nullptr, filters[0]);

    for (uint64_t hash : test_hashes) {
        EXPECT_TRUE(filters[0]->containsHash(hash))
            << "Hash " << std::hex << hash << " should be in filter";
    }
}

TEST(MergeTreeIndexGranuleBloomFilterTest, MemoryUsage) {
    MergeTreeIndexAggregatorBloomFilter agg(8, 3, {"col1", "col2"});

    // Empty granule
    auto empty_granule_ptr = agg.getGranuleAndReset();
    auto* empty_granule = dynamic_cast<MergeTreeIndexGranuleBloomFilter*>(empty_granule_ptr.get());
    EXPECT_EQ(0, empty_granule->memoryUsageBytes());

    // Non-empty granule
    for (int i = 0; i < 100; ++i) {
        agg.addRow({static_cast<uint64_t>(i), static_cast<uint64_t>(i * 2)});
    }
    auto granule_ptr = agg.getGranuleAndReset();
    auto* granule = dynamic_cast<MergeTreeIndexGranuleBloomFilter*>(granule_ptr.get());

    size_t memory = granule->memoryUsageBytes();
    EXPECT_GT(memory, 0);
    // Should be approximately: bits_per_row * total_rows * num_columns / 8
    // = 8 * 100 * 2 / 8 = 200 bytes (plus overhead)
    EXPECT_GE(memory, 150);
    EXPECT_LE(memory, 300);
}

// ==================== Condition Tests ====================

TEST(MergeTreeIndexConditionBloomFilterTest, NoPredicates) {
    MergeTreeIndexConditionBloomFilter cond({"col1"}, 3);

    EXPECT_TRUE(cond.alwaysUnknownOrTrue());
}

TEST(MergeTreeIndexConditionBloomFilterTest, EqualsPredicate) {
    MergeTreeIndexConditionBloomFilter cond({"col1"}, 3);

    cond.addEqualsPredicate("col1", 0x1111111111111111ULL);
    EXPECT_FALSE(cond.alwaysUnknownOrTrue());
}

TEST(MergeTreeIndexConditionBloomFilterTest, InPredicate) {
    MergeTreeIndexConditionBloomFilter cond({"col1"}, 3);

    std::vector<uint64_t> values = {0x1111111111111111ULL, 0x2222222222222222ULL,
                                    0x3333333333333333ULL};
    cond.addInPredicate("col1", values);

    EXPECT_FALSE(cond.alwaysUnknownOrTrue());
}

TEST(MergeTreeIndexConditionBloomFilterTest, UnindexedColumn) {
    MergeTreeIndexConditionBloomFilter cond({"col1"}, 3);

    // Add predicate on column that's not in index
    cond.addEqualsPredicate("col2", 0x1111111111111111ULL);

    // Should still be "always unknown" because predicate was ignored
    EXPECT_TRUE(cond.alwaysUnknownOrTrue());
}

TEST(MergeTreeIndexConditionBloomFilterTest, FilteringEquals) {
    // Create granule with specific hash
    MergeTreeIndexAggregatorBloomFilter agg(16, 5, {"col1"});
    agg.addRow({0x1111111111111111ULL});
    auto granule = agg.getGranuleAndReset();

    // Create condition
    MergeTreeIndexConditionBloomFilter cond({"col1"}, 5);

    // Query for value that IS in granule
    cond.addEqualsPredicate("col1", 0x1111111111111111ULL);
    EXPECT_TRUE(cond.mayBeTrueOnGranule(granule));  // Should NOT skip

    // Query for value that is NOT in granule
    MergeTreeIndexConditionBloomFilter cond2({"col1"}, 5);
    cond2.addEqualsPredicate("col1", 0xFFFFFFFFFFFFFFFFULL);
    // Might be false positive, but usually should return false
    // (We can't guarantee due to bloom filter nature, so just check it doesn't crash)
    cond2.mayBeTrueOnGranule(granule);
}

TEST(MergeTreeIndexConditionBloomFilterTest, FilteringIn) {
    // Create granule
    MergeTreeIndexAggregatorBloomFilter agg(16, 5, {"col1"});
    agg.addRow({0x1111111111111111ULL});
    agg.addRow({0x2222222222222222ULL});
    auto granule = agg.getGranuleAndReset();

    // Create condition with IN predicate including values in granule
    MergeTreeIndexConditionBloomFilter cond({"col1"}, 5);
    std::vector<uint64_t> values = {
        0x1111111111111111ULL,  // In granule
        0x9999999999999999ULL   // Not in granule
    };
    cond.addInPredicate("col1", values);

    // Should NOT skip because at least one value is in granule
    EXPECT_TRUE(cond.mayBeTrueOnGranule(granule));
}

TEST(MergeTreeIndexConditionBloomFilterTest, EmptyGranule) {
    MergeTreeIndexGranuleBloomFilter granule(8, 3, 1);

    MergeTreeIndexConditionBloomFilter cond({"col1"}, 3);
    cond.addEqualsPredicate("col1", 0x1111111111111111ULL);

    // Empty granule - cannot skip (conservative)
    EXPECT_TRUE(cond.mayBeTrueOnGranule(std::static_pointer_cast<IMergeTreeIndexGranule>(
        std::make_shared<MergeTreeIndexGranuleBloomFilter>(granule))));
}

// ==================== Integration Test ====================

TEST(MergeTreeIndexBloomFilterTest, EndToEndWorkflow) {
    // 1. Create index
    MergeTreeIndexBloomFilter index("status_idx", {"status_code"}, 1, 8, 3);

    // 2. Create aggregator and accumulate data
    auto aggregator = index.createIndexAggregator();
    auto* agg = dynamic_cast<MergeTreeIndexAggregatorBloomFilter*>(aggregator.get());
    ASSERT_NE(nullptr, agg);

    // Simulate indexing rows with status codes: 200, 404, 500
    agg->addRow({200});
    agg->addRow({404});
    agg->addRow({500});

    // 3. Create granule
    auto granule = agg->getGranuleAndReset();
    ASSERT_FALSE(granule->empty());

    // 4. Serialize granule
    ByteBuffersIndexOutput output("test");
    auto* bf_granule = dynamic_cast<MergeTreeIndexGranuleBloomFilter*>(granule.get());
    bf_granule->serialize(&output);

    // 5. Deserialize granule (simulating read from disk)
    ByteBuffersIndexInput input("test", output.toArrayCopy());
    auto loaded_granule = index.createIndexGranule();
    auto* loaded_bf = dynamic_cast<MergeTreeIndexGranuleBloomFilter*>(loaded_granule.get());
    loaded_bf->deserialize(&input, BLOOM_FILTER_VERSION_V1);

    // 6. Create query condition: WHERE status_code = 404
    auto condition = index.createIndexCondition();
    auto* cond = dynamic_cast<MergeTreeIndexConditionBloomFilter*>(condition.get());
    cond->addEqualsPredicate("status_code", 404);

    // 7. Check if granule may contain matching rows
    EXPECT_TRUE(cond->mayBeTrueOnGranule(loaded_granule));  // Should NOT skip

    // 8. Query for non-existent value
    auto condition2 = index.createIndexCondition();
    auto* cond2 = dynamic_cast<MergeTreeIndexConditionBloomFilter*>(condition2.get());
    cond2->addEqualsPredicate("status_code", 999);

    // Should potentially skip (but might be false positive)
    cond2->mayBeTrueOnGranule(loaded_granule);
}
