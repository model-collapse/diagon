// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/sparse/SparseVector.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace diagon::sparse;

// ==================== Construction Tests ====================

TEST(SparseVectorTest, DefaultConstruction) {
    SparseVector vec;
    EXPECT_EQ(0, vec.size());
    EXPECT_TRUE(vec.empty());
    EXPECT_EQ(0, vec.maxDimension());
}

TEST(SparseVectorTest, ConstructionFromArrays) {
    std::vector<uint32_t> indices = {10, 25, 100};
    std::vector<float> values = {0.8f, 1.2f, 0.5f};

    SparseVector vec(indices, values);

    EXPECT_EQ(3, vec.size());
    EXPECT_EQ(101, vec.maxDimension());  // max index + 1
    EXPECT_FLOAT_EQ(0.8f, vec.get(10));
    EXPECT_FLOAT_EQ(1.2f, vec.get(25));
    EXPECT_FLOAT_EQ(0.5f, vec.get(100));
}

TEST(SparseVectorTest, ConstructionFiltersZeros) {
    std::vector<uint32_t> indices = {10, 25, 50, 100};
    std::vector<float> values = {0.8f, 0.0f, 1.2f, 0.0f};

    SparseVector vec(indices, values);

    EXPECT_EQ(2, vec.size());  // Only 2 non-zero values
    EXPECT_FLOAT_EQ(0.8f, vec.get(10));
    EXPECT_FLOAT_EQ(0.0f, vec.get(25));  // Filtered out
    EXPECT_FLOAT_EQ(1.2f, vec.get(50));
    EXPECT_FLOAT_EQ(0.0f, vec.get(100));  // Filtered out
}

TEST(SparseVectorTest, ConstructionSortsByIndex) {
    std::vector<uint32_t> indices = {100, 10, 50, 25};
    std::vector<float> values = {0.5f, 0.8f, 1.2f, 1.5f};

    SparseVector vec(indices, values);

    EXPECT_EQ(4, vec.size());
    // Verify sorted order
    EXPECT_EQ(10, vec[0].index);
    EXPECT_EQ(25, vec[1].index);
    EXPECT_EQ(50, vec[2].index);
    EXPECT_EQ(100, vec[3].index);
}

TEST(SparseVectorTest, ConstructionThrowsOnMismatchedSizes) {
    std::vector<uint32_t> indices = {10, 25};
    std::vector<float> values = {0.8f, 1.2f, 0.5f};

    EXPECT_THROW(SparseVector(indices, values), std::invalid_argument);
}

// ==================== Modification Tests ====================

TEST(SparseVectorTest, AddNewElement) {
    SparseVector vec;
    vec.add(10, 0.8f);

    EXPECT_EQ(1, vec.size());
    EXPECT_FLOAT_EQ(0.8f, vec.get(10));
}

TEST(SparseVectorTest, AddToExistingElement) {
    SparseVector vec;
    vec.add(10, 0.8f);
    vec.add(10, 0.4f);

    EXPECT_EQ(1, vec.size());
    EXPECT_FLOAT_EQ(1.2f, vec.get(10));
}

TEST(SparseVectorTest, AddZeroDoesNothing) {
    SparseVector vec;
    vec.add(10, 0.0f);

    EXPECT_EQ(0, vec.size());
}

TEST(SparseVectorTest, AddToZeroRemovesElement) {
    SparseVector vec;
    vec.add(10, 0.8f);
    vec.add(10, -0.8f);  // Sum to zero

    EXPECT_EQ(0, vec.size());
}

TEST(SparseVectorTest, SetNewElement) {
    SparseVector vec;
    vec.set(10, 0.8f);

    EXPECT_EQ(1, vec.size());
    EXPECT_FLOAT_EQ(0.8f, vec.get(10));
}

TEST(SparseVectorTest, SetExistingElement) {
    SparseVector vec;
    vec.add(10, 0.8f);
    vec.set(10, 1.2f);

    EXPECT_EQ(1, vec.size());
    EXPECT_FLOAT_EQ(1.2f, vec.get(10));
}

TEST(SparseVectorTest, SetZeroRemovesElement) {
    SparseVector vec;
    vec.add(10, 0.8f);
    vec.set(10, 0.0f);

    EXPECT_EQ(0, vec.size());
}

// ==================== Access Tests ====================

TEST(SparseVectorTest, GetNonExistentReturnsZero) {
    SparseVector vec;
    vec.add(10, 0.8f);

    EXPECT_FLOAT_EQ(0.0f, vec.get(5));
    EXPECT_FLOAT_EQ(0.0f, vec.get(15));
}

TEST(SparseVectorTest, Contains) {
    SparseVector vec;
    vec.add(10, 0.8f);
    vec.add(25, 1.2f);

    EXPECT_TRUE(vec.contains(10));
    EXPECT_TRUE(vec.contains(25));
    EXPECT_FALSE(vec.contains(5));
    EXPECT_FALSE(vec.contains(15));
    EXPECT_FALSE(vec.contains(50));
}

// ==================== Vector Operations Tests ====================

TEST(SparseVectorTest, DotProduct) {
    SparseVector vec1;
    vec1.add(10, 0.8f);
    vec1.add(25, 1.2f);
    vec1.add(100, 0.5f);

    SparseVector vec2;
    vec2.add(10, 0.5f);   // Match
    vec2.add(50, 1.0f);   // No match
    vec2.add(100, 2.0f);  // Match

    float dot = vec1.dot(vec2);
    // Expected: 0.8*0.5 + 0.5*2.0 = 0.4 + 1.0 = 1.4
    EXPECT_FLOAT_EQ(1.4f, dot);
}

TEST(SparseVectorTest, DotProductDisjoint) {
    SparseVector vec1;
    vec1.add(10, 0.8f);
    vec1.add(25, 1.2f);

    SparseVector vec2;
    vec2.add(50, 1.0f);
    vec2.add(100, 2.0f);

    float dot = vec1.dot(vec2);
    EXPECT_FLOAT_EQ(0.0f, dot);
}

TEST(SparseVectorTest, Norm) {
    SparseVector vec;
    vec.add(10, 3.0f);
    vec.add(25, 4.0f);

    float norm = vec.norm();
    // Expected: sqrt(3^2 + 4^2) = sqrt(9 + 16) = sqrt(25) = 5.0
    EXPECT_FLOAT_EQ(5.0f, norm);
}

TEST(SparseVectorTest, Norm1) {
    SparseVector vec;
    vec.add(10, 3.0f);
    vec.add(25, -4.0f);

    float norm1 = vec.norm1();
    // Expected: |3.0| + |-4.0| = 3.0 + 4.0 = 7.0
    EXPECT_FLOAT_EQ(7.0f, norm1);
}

TEST(SparseVectorTest, Sum) {
    SparseVector vec;
    vec.add(10, 3.0f);
    vec.add(25, -4.0f);
    vec.add(50, 2.0f);

    float sum = vec.sum();
    // Expected: 3.0 + (-4.0) + 2.0 = 1.0
    EXPECT_FLOAT_EQ(1.0f, sum);
}

TEST(SparseVectorTest, CosineSimilarity) {
    SparseVector vec1;
    vec1.add(10, 3.0f);
    vec1.add(25, 4.0f);

    SparseVector vec2;
    vec2.add(10, 6.0f);
    vec2.add(25, 8.0f);

    float cosine = vec1.cosineSimilarity(vec2);
    // Both vectors are in same direction, should be 1.0
    EXPECT_FLOAT_EQ(1.0f, cosine);
}

TEST(SparseVectorTest, CosineSimilarityOrthogonal) {
    SparseVector vec1;
    vec1.add(10, 1.0f);

    SparseVector vec2;
    vec2.add(25, 1.0f);

    float cosine = vec1.cosineSimilarity(vec2);
    // Orthogonal vectors, should be 0.0
    EXPECT_FLOAT_EQ(0.0f, cosine);
}

// ==================== Pruning Tests ====================

TEST(SparseVectorTest, PruneTopK) {
    SparseVector vec;
    vec.add(10, 0.5f);
    vec.add(25, 1.2f);
    vec.add(50, 0.8f);
    vec.add(75, 0.3f);
    vec.add(100, 1.0f);

    vec.pruneTopK(3);

    EXPECT_EQ(3, vec.size());
    // Should keep 25 (1.2), 100 (1.0), 50 (0.8)
    EXPECT_TRUE(vec.contains(25));
    EXPECT_TRUE(vec.contains(100));
    EXPECT_TRUE(vec.contains(50));
    EXPECT_FALSE(vec.contains(10));
    EXPECT_FALSE(vec.contains(75));
}

TEST(SparseVectorTest, PruneByMass) {
    SparseVector vec;
    vec.add(10, 1.0f);  // 25% of mass
    vec.add(25, 2.0f);  // 50% of mass
    vec.add(50, 1.0f);  // 25% of mass
    // Total mass: 4.0

    vec.pruneByMass(0.75f);  // Keep 75% of mass (3.0)

    // Should keep elements covering at least 75% of mass
    // Since we have 2.0 + 1.0 + 1.0, and threshold is set to 1.0,
    // all three elements are kept (all have value >= 1.0)
    EXPECT_TRUE(vec.contains(25));  // Largest, must be kept

    // Verify total mass is at least 75% of original
    float total_mass = 0.0f;
    for (const auto& elem : vec) {
        total_mass += std::abs(elem.value);
    }
    EXPECT_GE(total_mass, 3.0f);  // At least 75% of 4.0
}

TEST(SparseVectorTest, PruneByThreshold) {
    SparseVector vec;
    vec.add(10, 0.5f);
    vec.add(25, 1.2f);
    vec.add(50, 0.8f);
    vec.add(75, 0.3f);

    vec.pruneByThreshold(0.6f);

    EXPECT_EQ(2, vec.size());
    EXPECT_TRUE(vec.contains(25));   // 1.2 >= 0.6
    EXPECT_TRUE(vec.contains(50));   // 0.8 >= 0.6
    EXPECT_FALSE(vec.contains(10));  // 0.5 < 0.6
    EXPECT_FALSE(vec.contains(75));  // 0.3 < 0.6
}

// ==================== Normalization Tests ====================

TEST(SparseVectorTest, Normalize) {
    SparseVector vec;
    vec.add(10, 3.0f);
    vec.add(25, 4.0f);

    vec.normalize();

    float norm = vec.norm();
    EXPECT_FLOAT_EQ(1.0f, norm);

    // Values should be scaled by 1/5
    EXPECT_FLOAT_EQ(0.6f, vec.get(10));  // 3/5
    EXPECT_FLOAT_EQ(0.8f, vec.get(25));  // 4/5
}

TEST(SparseVectorTest, Scale) {
    SparseVector vec;
    vec.add(10, 3.0f);
    vec.add(25, 4.0f);

    vec.scale(2.0f);

    EXPECT_FLOAT_EQ(6.0f, vec.get(10));
    EXPECT_FLOAT_EQ(8.0f, vec.get(25));
}

// ==================== Conversion Tests ====================

TEST(SparseVectorTest, ToDense) {
    SparseVector vec;
    vec.add(1, 0.5f);
    vec.add(3, 1.2f);
    vec.add(5, 0.8f);

    auto dense = vec.toDense(8);

    EXPECT_EQ(8, dense.size());
    EXPECT_FLOAT_EQ(0.0f, dense[0]);
    EXPECT_FLOAT_EQ(0.5f, dense[1]);
    EXPECT_FLOAT_EQ(0.0f, dense[2]);
    EXPECT_FLOAT_EQ(1.2f, dense[3]);
    EXPECT_FLOAT_EQ(0.0f, dense[4]);
    EXPECT_FLOAT_EQ(0.8f, dense[5]);
    EXPECT_FLOAT_EQ(0.0f, dense[6]);
    EXPECT_FLOAT_EQ(0.0f, dense[7]);
}

TEST(SparseVectorTest, ToDenseAutoSize) {
    SparseVector vec;
    vec.add(1, 0.5f);
    vec.add(5, 0.8f);

    auto dense = vec.toDense();  // Auto-detect size

    EXPECT_EQ(6, dense.size());  // max index + 1
}

TEST(SparseVectorTest, FromDense) {
    std::vector<float> dense = {0.0f, 0.5f, 0.0f, 1.2f, 0.0f, 0.8f};

    auto vec = SparseVector::fromDense(dense);

    EXPECT_EQ(3, vec.size());
    EXPECT_FLOAT_EQ(0.5f, vec.get(1));
    EXPECT_FLOAT_EQ(1.2f, vec.get(3));
    EXPECT_FLOAT_EQ(0.8f, vec.get(5));
}

TEST(SparseVectorTest, FromDenseWithThreshold) {
    std::vector<float> dense = {0.1f, 0.5f, 0.2f, 1.2f, 0.3f, 0.8f};

    auto vec = SparseVector::fromDense(dense, 0.4f);

    EXPECT_EQ(3, vec.size());  // Only values > 0.4
    EXPECT_FLOAT_EQ(0.5f, vec.get(1));
    EXPECT_FLOAT_EQ(1.2f, vec.get(3));
    EXPECT_FLOAT_EQ(0.8f, vec.get(5));
}

// ==================== Iteration Tests ====================

TEST(SparseVectorTest, Iteration) {
    SparseVector vec;
    vec.add(10, 0.8f);
    vec.add(25, 1.2f);
    vec.add(50, 0.5f);

    size_t count = 0;
    for (const auto& elem : vec) {
        EXPECT_GT(elem.value, 0.0f);
        count++;
    }

    EXPECT_EQ(3, count);
}
