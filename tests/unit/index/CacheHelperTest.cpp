// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/CacheHelper.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/index/SegmentReader.h"
#include "diagon/store/FSDirectory.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <unordered_map>

using namespace diagon;
using namespace diagon::index;
using namespace diagon::store;
using namespace diagon::document;

namespace fs = std::filesystem;

class CacheHelperTest : public ::testing::Test {
protected:
    void SetUp() override {
        testDir_ = fs::temp_directory_path() / "diagon_cache_helper_test";
        fs::remove_all(testDir_);
        fs::create_directories(testDir_);

        directory_ = FSDirectory::open(testDir_.string());
    }

    void TearDown() override {
        directory_.reset();
        fs::remove_all(testDir_);
    }

    void addDocument(IndexWriter& writer, const std::string& id, const std::string& content) {
        Document doc;
        doc.add(std::make_unique<StringField>("id", id, StringField::TYPE_STORED));
        doc.add(std::make_unique<TextField>("content", content, TextField::TYPE_STORED));
        writer.addDocument(doc);
    }

    fs::path testDir_;
    std::unique_ptr<FSDirectory> directory_;
};

// ==================== Test 1: CacheKey Uniqueness ====================

TEST_F(CacheHelperTest, CacheKeyUniqueness) {
    // Create two cache helpers - should have different keys
    CacheHelper helper1;
    CacheHelper helper2;

    CacheKey* key1 = helper1.getKey();
    CacheKey* key2 = helper2.getKey();

    ASSERT_NE(nullptr, key1);
    ASSERT_NE(nullptr, key2);

    // Keys should be different (pointer comparison)
    EXPECT_NE(key1, key2);
    EXPECT_TRUE(*key1 != *key2);
}

// ==================== Test 2: CacheKey Stability ====================

TEST_F(CacheHelperTest, CacheKeyStability) {
    CacheHelper helper;

    // Get key multiple times - should return same pointer
    CacheKey* key1 = helper.getKey();
    CacheKey* key2 = helper.getKey();
    CacheKey* key3 = helper.getKey();

    EXPECT_EQ(key1, key2);
    EXPECT_EQ(key2, key3);
}

// ==================== Test 3: CacheKey Hash ====================

TEST_F(CacheHelperTest, CacheKeyHash) {
    CacheHelper helper1;
    CacheHelper helper2;

    CacheKey* key1 = helper1.getKey();
    CacheKey* key2 = helper2.getKey();

    // Hash codes should be different (based on pointer)
    size_t hash1 = key1->hashCode();
    size_t hash2 = key2->hashCode();

    EXPECT_NE(hash1, hash2);

    // Hash should be stable
    EXPECT_EQ(hash1, key1->hashCode());
    EXPECT_EQ(hash2, key2->hashCode());
}

// ==================== Test 4: SegmentReader Cache Helpers ====================

TEST_F(CacheHelperTest, SegmentReaderCacheHelpers) {
    // Create index with one segment
    IndexWriterConfig config;
    IndexWriter writer(*directory_, config);
    addDocument(writer, "1", "hello world");
    writer.commit();

    // Open reader
    auto dirReader = DirectoryReader::open(*directory_);
    ASSERT_TRUE(dirReader);

    auto leaves = dirReader->leaves();
    ASSERT_EQ(1, leaves.size());

    LeafReader* leafReader = leaves[0].reader;
    ASSERT_NE(nullptr, leafReader);

    // Get cache helpers
    CacheHelper* coreHelper = leafReader->getCoreCacheHelper();
    CacheHelper* readerHelper = leafReader->getReaderCacheHelper();

    ASSERT_NE(nullptr, coreHelper);
    ASSERT_NE(nullptr, readerHelper);

    // Should have different keys (core vs reader)
    EXPECT_NE(coreHelper->getKey(), readerHelper->getKey());
}

// ==================== Test 5: DirectoryReader Cache Helper ====================

TEST_F(CacheHelperTest, DirectoryReaderCacheHelper) {
    // Create index
    IndexWriterConfig config;
    IndexWriter writer(*directory_, config);
    addDocument(writer, "1", "test");
    writer.commit();

    // Open reader
    auto reader1 = DirectoryReader::open(*directory_);
    ASSERT_TRUE(reader1);

    // Get cache helper
    CacheHelper* helper1 = reader1->getReaderCacheHelper();
    ASSERT_NE(nullptr, helper1);

    CacheKey* key1 = helper1->getKey();
    ASSERT_NE(nullptr, key1);

    // Reopen reader - should get different key
    addDocument(writer, "2", "test2");
    writer.commit();

    auto reader2 = DirectoryReader::openIfChanged(reader1);
    ASSERT_TRUE(reader2);

    CacheHelper* helper2 = reader2->getReaderCacheHelper();
    ASSERT_NE(nullptr, helper2);

    CacheKey* key2 = helper2->getKey();
    ASSERT_NE(nullptr, key2);

    // Different readers should have different keys
    EXPECT_NE(key1, key2);
}

// ==================== Test 6: Cache Simulation ====================

TEST_F(CacheHelperTest, CacheSimulation) {
    // Simulate using CacheHelper for caching
    std::unordered_map<CacheKey*, std::string> cache;

    // Create index
    IndexWriterConfig config;
    IndexWriter writer(*directory_, config);
    addDocument(writer, "1", "test");
    writer.commit();

    // Open reader and cache some data
    auto reader1 = DirectoryReader::open(*directory_);
    CacheHelper* helper1 = reader1->getReaderCacheHelper();
    CacheKey* key1 = helper1->getKey();

    cache[key1] = "cached_data_1";

    EXPECT_EQ(1, cache.size());
    EXPECT_EQ("cached_data_1", cache[key1]);

    // Reopen reader
    addDocument(writer, "2", "test2");
    writer.commit();

    auto reader2 = DirectoryReader::openIfChanged(reader1);
    ASSERT_TRUE(reader2);

    CacheHelper* helper2 = reader2->getReaderCacheHelper();
    CacheKey* key2 = helper2->getKey();

    // New reader should not find cached data (different key)
    EXPECT_EQ(cache.end(), cache.find(key2));

    // Cache new data for new reader
    cache[key2] = "cached_data_2";
    EXPECT_EQ(2, cache.size());

    // Both entries should exist
    EXPECT_EQ("cached_data_1", cache[key1]);
    EXPECT_EQ("cached_data_2", cache[key2]);
}

// ==================== Test 7: Core vs Reader Cache ====================

TEST_F(CacheHelperTest, CoreVsReaderCache) {
    // Create index
    IndexWriterConfig config;
    IndexWriter writer(*directory_, config);
    addDocument(writer, "1", "test");
    writer.commit();

    // Open reader
    auto dirReader = DirectoryReader::open(*directory_);
    auto leaves = dirReader->leaves();
    ASSERT_EQ(1, leaves.size());

    LeafReader* leafReader = leaves[0].reader;

    // Get both helpers
    CacheHelper* coreHelper = leafReader->getCoreCacheHelper();
    CacheHelper* readerHelper = leafReader->getReaderCacheHelper();

    CacheKey* coreKey = coreHelper->getKey();
    CacheKey* readerKey = readerHelper->getKey();

    // Core cache: safe to cache term dictionaries, doc values (immutable)
    // Reader cache: includes deletions (may change)

    // Keys should be different
    EXPECT_NE(coreKey, readerKey);

    // Both should be valid throughout reader lifetime
    EXPECT_NE(nullptr, coreKey);
    EXPECT_NE(nullptr, readerKey);
}

// ==================== Test 8: Multiple Segments ====================

TEST_F(CacheHelperTest, MultipleSegments) {
    // Create index with multiple segments
    IndexWriterConfig config;
    IndexWriter writer(*directory_, config);

    addDocument(writer, "1", "doc1");
    writer.commit();

    addDocument(writer, "2", "doc2");
    writer.commit();

    addDocument(writer, "3", "doc3");
    writer.commit();

    // Open reader
    auto dirReader = DirectoryReader::open(*directory_);
    auto leaves = dirReader->leaves();
    ASSERT_GE(leaves.size(), 1);

    // Each segment should have unique cache keys
    std::unordered_map<CacheKey*, int> coreKeys;
    std::unordered_map<CacheKey*, int> readerKeys;

    for (const auto& leaf : leaves) {
        LeafReader* reader = leaf.reader;

        CacheHelper* coreHelper = reader->getCoreCacheHelper();
        CacheHelper* readerHelper = reader->getReaderCacheHelper();

        ASSERT_NE(nullptr, coreHelper);
        ASSERT_NE(nullptr, readerHelper);

        CacheKey* coreKey = coreHelper->getKey();
        CacheKey* readerKey = readerHelper->getKey();

        // Track unique keys
        coreKeys[coreKey]++;
        readerKeys[readerKey]++;

        // Core and reader keys should differ
        EXPECT_NE(coreKey, readerKey);
    }

    // Each segment should have unique keys
    EXPECT_EQ(leaves.size(), coreKeys.size());
    EXPECT_EQ(leaves.size(), readerKeys.size());

    // No duplicates
    for (const auto& pair : coreKeys) {
        EXPECT_EQ(1, pair.second);
    }
    for (const auto& pair : readerKeys) {
        EXPECT_EQ(1, pair.second);
    }
}
