// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/BloomFilter.h"

#include <gtest/gtest.h>

#include <cstring>
#include <random>

using namespace diagon::util;

// ==================== Basic Operations ====================

TEST(BloomFilterTest, Construction) {
    // Valid construction
    BloomFilter bf(1024, 3, 42);
    EXPECT_EQ(1024, bf.sizeBytes());
    EXPECT_EQ(3, bf.numHashes());
    EXPECT_EQ(42, bf.seed());
    EXPECT_TRUE(bf.empty());
}

TEST(BloomFilterTest, ConstructionInvalidSize) {
    EXPECT_THROW(BloomFilter(0, 3, 42), std::invalid_argument);
}

TEST(BloomFilterTest, ConstructionInvalidHashes) {
    EXPECT_THROW(BloomFilter(1024, 0, 42), std::invalid_argument);
}

TEST(BloomFilterTest, AddAndContains) {
    BloomFilter bf(1024, 3, 0);

    // Empty filter
    EXPECT_TRUE(bf.empty());
    EXPECT_FALSE(bf.contains("hello", 5));

    // Add element
    bf.add("hello", 5);
    EXPECT_FALSE(bf.empty());
    EXPECT_TRUE(bf.contains("hello", 5));

    // Different element should not be found
    EXPECT_FALSE(bf.contains("world", 5));

    // Add another element
    bf.add("world", 5);
    EXPECT_TRUE(bf.contains("world", 5));
    EXPECT_TRUE(bf.contains("hello", 5));  // First still there
}

TEST(BloomFilterTest, AddHash) {
    BloomFilter bf(1024, 3, 0);

    uint64_t hash1 = 0x123456789abcdef0ULL;
    uint64_t hash2 = 0xfedcba9876543210ULL;

    bf.addHash(hash1);
    EXPECT_TRUE(bf.containsHash(hash1));
    EXPECT_FALSE(bf.containsHash(hash2));

    bf.addHash(hash2);
    EXPECT_TRUE(bf.containsHash(hash2));
    EXPECT_TRUE(bf.containsHash(hash1));
}

TEST(BloomFilterTest, Clear) {
    BloomFilter bf(1024, 3, 0);

    bf.add("element1", 8);
    bf.add("element2", 8);
    EXPECT_FALSE(bf.empty());
    EXPECT_TRUE(bf.contains("element1", 8));

    bf.clear();
    EXPECT_TRUE(bf.empty());
    EXPECT_FALSE(bf.contains("element1", 8));
    EXPECT_FALSE(bf.contains("element2", 8));
}

// ==================== Set Operations ====================

TEST(BloomFilterTest, ContainsAll) {
    BloomFilter bf1(1024, 3, 0);
    BloomFilter bf2(1024, 3, 0);

    bf1.add("a", 1);
    bf1.add("b", 1);
    bf1.add("c", 1);

    bf2.add("a", 1);
    bf2.add("b", 1);

    // bf1 contains all of bf2
    EXPECT_TRUE(bf1.containsAll(bf2));

    // bf2 does not contain all of bf1 (missing "c")
    EXPECT_FALSE(bf2.containsAll(bf1));

    // Identity: bf1 contains all of bf1
    EXPECT_TRUE(bf1.containsAll(bf1));
}

TEST(BloomFilterTest, ContainsAllRequiresSameSize) {
    BloomFilter bf1(1024, 3, 0);
    BloomFilter bf2(2048, 3, 0);  // Different size

    EXPECT_THROW(bf1.containsAll(bf2), std::invalid_argument);
}

TEST(BloomFilterTest, ContainsAllRequiresSameSeed) {
    BloomFilter bf1(1024, 3, 0);
    BloomFilter bf2(1024, 3, 42);  // Different seed

    EXPECT_THROW(bf1.containsAll(bf2), std::invalid_argument);
}

TEST(BloomFilterTest, Merge) {
    BloomFilter bf1(1024, 3, 0);
    BloomFilter bf2(1024, 3, 0);

    bf1.add("a", 1);
    bf1.add("b", 1);

    bf2.add("c", 1);
    bf2.add("d", 1);

    // Merge bf2 into bf1
    bf1.merge(bf2);

    // bf1 should contain all elements from both filters
    EXPECT_TRUE(bf1.contains("a", 1));
    EXPECT_TRUE(bf1.contains("b", 1));
    EXPECT_TRUE(bf1.contains("c", 1));
    EXPECT_TRUE(bf1.contains("d", 1));

    // bf2 should be unchanged
    EXPECT_TRUE(bf2.contains("c", 1));
    EXPECT_TRUE(bf2.contains("d", 1));
    EXPECT_FALSE(bf2.contains("a", 1));
    EXPECT_FALSE(bf2.contains("b", 1));
}

TEST(BloomFilterTest, MergeRequiresSameParameters) {
    BloomFilter bf1(1024, 3, 0);
    BloomFilter bf2(2048, 3, 0);  // Different size

    EXPECT_THROW(bf1.merge(bf2), std::invalid_argument);
}

// ==================== Statistics ====================

TEST(BloomFilterTest, Popcount) {
    BloomFilter bf(1024, 1, 0);  // 1 hash for predictable popcount

    EXPECT_EQ(0, bf.popcount());

    // Add a few elements
    bf.add("a", 1);
    size_t count1 = bf.popcount();
    EXPECT_GT(count1, 0);

    bf.add("b", 1);
    size_t count2 = bf.popcount();
    EXPECT_GE(count2, count1);  // Should increase or stay same (collision)

    // Total bits
    size_t total_bits = bf.sizeBytes() * 8;
    EXPECT_LT(bf.popcount(), total_bits);
}

TEST(BloomFilterTest, EstimateFalsePositiveRate) {
    BloomFilter bf(1024, 3, 0);

    // Empty filter: FPR should be ~0
    double fpr_empty = bf.estimateFalsePositiveRate();
    EXPECT_NEAR(0.0, fpr_empty, 0.01);

    // Add many elements
    for (int i = 0; i < 100; ++i) {
        std::string elem = "element_" + std::to_string(i);
        bf.add(elem.c_str(), elem.size());
    }

    // FPR should increase
    double fpr_full = bf.estimateFalsePositiveRate();
    EXPECT_GT(fpr_full, fpr_empty);
    EXPECT_LT(fpr_full, 1.0);
}

// ==================== Comparison ====================

TEST(BloomFilterTest, Equality) {
    BloomFilter bf1(1024, 3, 0);
    BloomFilter bf2(1024, 3, 0);

    // Empty filters should be equal
    EXPECT_EQ(bf1, bf2);

    // Add same elements in same order
    bf1.add("a", 1);
    bf1.add("b", 1);
    bf2.add("a", 1);
    bf2.add("b", 1);

    EXPECT_EQ(bf1, bf2);

    // Add different element
    bf1.add("c", 1);
    EXPECT_NE(bf1, bf2);

    // Different parameters
    BloomFilter bf3(1024, 4, 0);  // Different num_hashes
    EXPECT_NE(bf1, bf3);

    BloomFilter bf4(2048, 3, 0);  // Different size
    EXPECT_NE(bf1, bf4);

    BloomFilter bf5(1024, 3, 42);  // Different seed
    EXPECT_NE(bf1, bf5);
}

// ==================== Properties ====================

TEST(BloomFilterTest, MemoryUsage) {
    BloomFilter bf(1024, 3, 0);

    size_t memory = bf.memoryUsageBytes();
    EXPECT_GE(memory, 1024);  // At least the size of the filter
}

TEST(BloomFilterTest, DataAccess) {
    BloomFilter bf(1024, 3, 0);

    // Get const data
    const auto& data = bf.data();
    EXPECT_FALSE(data.empty());

    // Get mutable data (for deserialization)
    auto& mutable_data = bf.data();
    EXPECT_FALSE(mutable_data.empty());

    // Modify directly
    size_t original_popcount = bf.popcount();
    mutable_data[0] |= 1;  // Set first bit
    EXPECT_GT(bf.popcount(), original_popcount);
}

// ==================== False Positive Test ====================

TEST(BloomFilterTest, FalsePositiveRate) {
    // Large filter with good parameters
    BloomFilter bf(4096, 5, 0);  // ~10 bits per element, 5 hashes

    std::mt19937_64 rng(12345);
    std::uniform_int_distribution<uint64_t> dist;

    // Add 1000 elements
    std::vector<uint64_t> added_elements;
    for (int i = 0; i < 1000; ++i) {
        uint64_t val = dist(rng);
        added_elements.push_back(val);
        bf.add(reinterpret_cast<const char*>(&val), sizeof(val));
    }

    // Check that all added elements are found
    for (uint64_t val : added_elements) {
        EXPECT_TRUE(bf.contains(reinterpret_cast<const char*>(&val), sizeof(val)));
    }

    // Check false positive rate on elements NOT added
    int false_positives = 0;
    int test_count = 10000;
    for (int i = 0; i < test_count; ++i) {
        uint64_t val = dist(rng);
        // Skip if we happened to test an added element
        if (std::find(added_elements.begin(), added_elements.end(), val) != added_elements.end()) {
            continue;
        }
        if (bf.contains(reinterpret_cast<const char*>(&val), sizeof(val))) {
            false_positives++;
        }
    }

    double fpr = static_cast<double>(false_positives) / test_count;

    // With ~10 bits per element and 5 hashes, FPR should be < 1%
    EXPECT_LT(fpr, 0.02);  // Less than 2% FPR

    // Estimated FPR should be in reasonable range
    double estimated_fpr = bf.estimateFalsePositiveRate();
    EXPECT_LT(estimated_fpr, 0.1);
}

// ==================== Stress Test ====================

TEST(BloomFilterTest, LargeNumberOfElements) {
    // Stress test with many elements
    BloomFilter bf(16384, 3, 0);  // 16KB filter

    // Add 10000 elements
    for (int i = 0; i < 10000; ++i) {
        std::string elem = "element_" + std::to_string(i);
        bf.add(elem.c_str(), elem.size());
    }

    // Verify some elements
    EXPECT_TRUE(bf.contains("element_0", std::strlen("element_0")));
    EXPECT_TRUE(bf.contains("element_5000", std::strlen("element_5000")));
    EXPECT_TRUE(bf.contains("element_9999", std::strlen("element_9999")));

    // Should not be empty
    EXPECT_FALSE(bf.empty());

    // Popcount should be significant
    EXPECT_GT(bf.popcount(), 1000);
}

// ==================== Different Data Types ====================

TEST(BloomFilterTest, IntegerValues) {
    BloomFilter bf(1024, 3, 0);

    int values[] = {42, 123, -456, 0, 999999};

    for (int val : values) {
        bf.add(reinterpret_cast<const char*>(&val), sizeof(val));
    }

    for (int val : values) {
        EXPECT_TRUE(bf.contains(reinterpret_cast<const char*>(&val), sizeof(val)));
    }

    // Test value not added
    int not_added = 777;
    EXPECT_FALSE(bf.contains(reinterpret_cast<const char*>(&not_added), sizeof(not_added)));
}

TEST(BloomFilterTest, StringValues) {
    BloomFilter bf(1024, 3, 0);

    const char* strings[] = {"hello", "world", "bloom", "filter", "test"};

    for (const char* str : strings) {
        bf.add(str, std::strlen(str));
    }

    for (const char* str : strings) {
        EXPECT_TRUE(bf.contains(str, std::strlen(str)));
    }

    EXPECT_FALSE(bf.contains("notadded", 8));
}

// ==================== Edge Cases ====================

TEST(BloomFilterTest, EmptyString) {
    BloomFilter bf(1024, 3, 0);

    bf.add("", 0);
    EXPECT_TRUE(bf.contains("", 0));
    EXPECT_FALSE(bf.empty());
}

TEST(BloomFilterTest, VeryLongString) {
    BloomFilter bf(1024, 3, 0);

    std::string long_str(10000, 'x');
    bf.add(long_str.c_str(), long_str.size());
    EXPECT_TRUE(bf.contains(long_str.c_str(), long_str.size()));
}

TEST(BloomFilterTest, ManyHashes) {
    // Filter with many hash functions
    BloomFilter bf(2048, 20, 0);

    bf.add("test", 4);
    EXPECT_TRUE(bf.contains("test", 4));
}
