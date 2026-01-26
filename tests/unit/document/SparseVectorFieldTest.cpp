// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/document/SparseVectorField.h"
#include "diagon/sparse/SparseVector.h"

#include <gtest/gtest.h>

using namespace diagon::document;
using namespace diagon::sparse;

// ==================== Construction Tests ====================

TEST(SparseVectorFieldTest, Construction) {
    SparseVector vec;
    vec.add(10, 0.8f);
    vec.add(25, 1.2f);
    vec.add(100, 0.5f);

    SparseVectorField field("embedding", vec);

    EXPECT_EQ("embedding", field.name());
    EXPECT_EQ(3, field.size());
    EXPECT_EQ(101, field.maxDimension());
}

TEST(SparseVectorFieldTest, ConstructionStored) {
    SparseVector vec;
    vec.add(10, 0.8f);

    SparseVectorField field("embedding", vec, true);

    EXPECT_EQ("embedding", field.name());
    EXPECT_TRUE(field.fieldType().stored);
}

TEST(SparseVectorFieldTest, ConstructionNotStored) {
    SparseVector vec;
    vec.add(10, 0.8f);

    SparseVectorField field("embedding", vec, false);

    EXPECT_EQ("embedding", field.name());
    EXPECT_FALSE(field.fieldType().stored);
}

// ==================== Field Type Tests ====================

TEST(SparseVectorFieldTest, FieldTypeNotIndexed) {
    SparseVector vec;
    vec.add(10, 0.8f);

    SparseVectorField field("embedding", vec);

    const auto& type = field.fieldType();
    EXPECT_EQ(diagon::index::IndexOptions::NONE, type.indexOptions);
    EXPECT_FALSE(type.tokenized);
    EXPECT_TRUE(type.omitNorms);
}

// ==================== Value Access Tests ====================

TEST(SparseVectorFieldTest, NoStringValue) {
    SparseVector vec;
    vec.add(10, 0.8f);

    SparseVectorField field("embedding", vec);

    EXPECT_FALSE(field.stringValue().has_value());
}

TEST(SparseVectorFieldTest, NoNumericValue) {
    SparseVector vec;
    vec.add(10, 0.8f);

    SparseVectorField field("embedding", vec);

    EXPECT_FALSE(field.numericValue().has_value());
}

TEST(SparseVectorFieldTest, NoTokenization) {
    SparseVector vec;
    vec.add(10, 0.8f);

    SparseVectorField field("embedding", vec);

    auto tokens = field.tokenize();
    EXPECT_EQ(0, tokens.size());
}

// ==================== Sparse Vector Access Tests ====================

TEST(SparseVectorFieldTest, SparseVectorAccess) {
    SparseVector vec;
    vec.add(10, 0.8f);
    vec.add(25, 1.2f);

    SparseVectorField field("embedding", vec);

    const auto& retrieved = field.sparseVector();
    EXPECT_EQ(2, retrieved.size());
    EXPECT_FLOAT_EQ(0.8f, retrieved.get(10));
    EXPECT_FLOAT_EQ(1.2f, retrieved.get(25));
}

// ==================== Binary Serialization Tests ====================

TEST(SparseVectorFieldTest, BinaryValueNotStored) {
    SparseVector vec;
    vec.add(10, 0.8f);

    SparseVectorField field("embedding", vec, false);

    EXPECT_FALSE(field.binaryValue().has_value());
}

TEST(SparseVectorFieldTest, BinaryValueStored) {
    SparseVector vec;
    vec.add(10, 0.8f);
    vec.add(25, 1.2f);

    SparseVectorField field("embedding", vec, true);

    auto binary = field.binaryValue();
    EXPECT_TRUE(binary.has_value());

    // Verify format: [num_elements:4] [index:4, value:4] ...
    // Expected size: 4 + 2*(4+4) = 4 + 16 = 20 bytes
    EXPECT_EQ(20, binary->length());
}

// ==================== Empty Vector Tests ====================

TEST(SparseVectorFieldTest, EmptyVector) {
    SparseVector vec;  // Empty

    SparseVectorField field("embedding", vec);

    EXPECT_EQ(0, field.size());
    EXPECT_EQ(0, field.maxDimension());

    const auto& retrieved = field.sparseVector();
    EXPECT_TRUE(retrieved.empty());
}

// ==================== Large Vector Tests ====================

TEST(SparseVectorFieldTest, LargeVector) {
    SparseVector vec;

    // Create sparse vector with 100 elements (skip 0 to avoid filtering)
    for (uint32_t i = 1; i <= 100; ++i) {
        vec.add(i * 10, static_cast<float>(i) * 0.1f);
    }

    SparseVectorField field("embedding", vec);

    EXPECT_EQ(100, field.size());
    EXPECT_EQ(1001, field.maxDimension());  // 100*10 + 1

    // Verify some values
    EXPECT_FLOAT_EQ(0.1f, field.sparseVector().get(10));
    EXPECT_FLOAT_EQ(5.0f, field.sparseVector().get(500));
    EXPECT_FLOAT_EQ(10.0f, field.sparseVector().get(1000));
}
