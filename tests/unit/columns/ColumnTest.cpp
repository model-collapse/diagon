// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/columns/ColumnVector.h"
#include "diagon/columns/ColumnString.h"
#include "diagon/columns/Field.h"

#include <gtest/gtest.h>
#include <memory>

using namespace diagon::columns;

// ==================== Field Tests ====================

TEST(FieldTest, NullConstruction) {
    Field f;
    EXPECT_TRUE(f.isNull());
    EXPECT_FALSE(f.isInt());
    EXPECT_FALSE(f.isString());
}

TEST(FieldTest, IntegerConstruction) {
    Field f1(42);
    EXPECT_FALSE(f1.isNull());
    EXPECT_TRUE(f1.isInt());
    EXPECT_EQ(42, f1.get<int64_t>());

    Field f2(uint32_t(100));
    EXPECT_TRUE(f2.isUInt());
    EXPECT_EQ(100, f2.get<uint64_t>());
}

TEST(FieldTest, FloatConstruction) {
    Field f1(3.14f);
    EXPECT_TRUE(f1.isFloat());
    EXPECT_FLOAT_EQ(3.14f, f1.get<float>());

    Field f2(2.71);
    EXPECT_TRUE(f2.isDouble());
    EXPECT_DOUBLE_EQ(2.71, f2.get<double>());
}

TEST(FieldTest, StringConstruction) {
    Field f1("hello");
    EXPECT_TRUE(f1.isString());
    EXPECT_EQ("hello", f1.get<std::string>());

    std::string s = "world";
    Field f2(s);
    EXPECT_TRUE(f2.isString());
    EXPECT_EQ("world", f2.get<std::string>());
}

TEST(FieldTest, Comparison) {
    Field f1(42);
    Field f2(42);
    Field f3(100);

    EXPECT_EQ(f1, f2);
    EXPECT_NE(f1, f3);
    EXPECT_LT(f1, f3);
}

// ==================== ColumnVector Tests ====================

TEST(ColumnVectorTest, Construction) {
    auto col = ColumnInt32::create();
    EXPECT_EQ(0, col->size());
    EXPECT_TRUE(col->empty());
    EXPECT_EQ("Int32", col->getName());
    EXPECT_EQ(TypeIndex::Int32, col->getDataType());
    EXPECT_TRUE(col->isNumeric());
}

TEST(ColumnVectorTest, InsertAndAccess) {
    auto col = ColumnInt32::create();

    col->insert(Field(int32_t(42)));
    col->insert(Field(int32_t(100)));
    col->insert(Field(int32_t(-50)));

    EXPECT_EQ(3, col->size());
    EXPECT_EQ(42, (*col)[0].get<int64_t>());
    EXPECT_EQ(100, (*col)[1].get<int64_t>());
    EXPECT_EQ(-50, (*col)[2].get<int64_t>());
}

TEST(ColumnVectorTest, InsertDefault) {
    auto col = ColumnInt32::create();

    col->insertDefault();
    col->insertManyDefaults(3);

    EXPECT_EQ(4, col->size());
    EXPECT_EQ(0, (*col)[0].get<int64_t>());
    EXPECT_EQ(0, (*col)[3].get<int64_t>());
}

TEST(ColumnVectorTest, InsertFrom) {
    auto col1 = ColumnInt32::create();
    col1->insert(Field(int32_t(42)));
    col1->insert(Field(int32_t(100)));

    auto col2 = ColumnInt32::create();
    col2->insertFrom(*col1, 0);
    col2->insertFrom(*col1, 1);

    EXPECT_EQ(2, col2->size());
    EXPECT_EQ(42, (*col2)[0].get<int64_t>());
    EXPECT_EQ(100, (*col2)[1].get<int64_t>());
}

TEST(ColumnVectorTest, InsertRangeFrom) {
    auto col1 = ColumnInt32::create();
    col1->insert(Field(int32_t(10)));
    col1->insert(Field(int32_t(20)));
    col1->insert(Field(int32_t(30)));
    col1->insert(Field(int32_t(40)));

    auto col2 = ColumnInt32::create();
    col2->insertRangeFrom(*col1, 1, 2);  // Insert [20, 30]

    EXPECT_EQ(2, col2->size());
    EXPECT_EQ(20, (*col2)[0].get<int64_t>());
    EXPECT_EQ(30, (*col2)[1].get<int64_t>());
}

TEST(ColumnVectorTest, PopBack) {
    auto col = ColumnInt32::create();
    col->insert(Field(int32_t(10)));
    col->insert(Field(int32_t(20)));
    col->insert(Field(int32_t(30)));

    col->popBack(1);
    EXPECT_EQ(2, col->size());
    EXPECT_EQ(20, (*col)[1].get<int64_t>());
}

TEST(ColumnVectorTest, Filter) {
    auto col = ColumnInt32::create();
    col->insert(Field(int32_t(10)));
    col->insert(Field(int32_t(20)));
    col->insert(Field(int32_t(30)));
    col->insert(Field(int32_t(40)));

    Filter filt = {1, 0, 1, 0};  // Keep rows 0 and 2
    auto filtered = col->filter(filt, 2);

    EXPECT_EQ(2, filtered->size());
    EXPECT_EQ(10, (*filtered)[0].get<int64_t>());
    EXPECT_EQ(30, (*filtered)[1].get<int64_t>());
}

TEST(ColumnVectorTest, Cut) {
    auto col = ColumnInt32::create();
    col->insert(Field(int32_t(10)));
    col->insert(Field(int32_t(20)));
    col->insert(Field(int32_t(30)));
    col->insert(Field(int32_t(40)));

    auto cut = col->cut(1, 2);  // Extract [20, 30]

    EXPECT_EQ(2, cut->size());
    EXPECT_EQ(20, (*cut)[0].get<int64_t>());
    EXPECT_EQ(30, (*cut)[1].get<int64_t>());
}

TEST(ColumnVectorTest, CompareAt) {
    auto col1 = ColumnInt32::create();
    col1->insert(Field(int32_t(10)));
    col1->insert(Field(int32_t(30)));

    auto col2 = ColumnInt32::create();
    col2->insert(Field(int32_t(20)));

    EXPECT_LT(col1->compareAt(0, 0, *col2, 0), 0);  // 10 < 20
    EXPECT_GT(col1->compareAt(1, 0, *col2, 0), 0);  // 30 > 20
}

TEST(ColumnVectorTest, Clone) {
    auto col = ColumnInt32::create();
    col->insert(Field(int32_t(42)));
    col->insert(Field(int32_t(100)));

    auto cloned = col->clone();
    EXPECT_EQ(2, cloned->size());
    EXPECT_EQ(42, (*cloned)[0].get<int64_t>());
    EXPECT_EQ(100, (*cloned)[1].get<int64_t>());

    // Original unchanged
    EXPECT_EQ(2, col->size());
}

TEST(ColumnVectorTest, CloneResized) {
    auto col = ColumnInt32::create();
    col->insert(Field(int32_t(10)));
    col->insert(Field(int32_t(20)));
    col->insert(Field(int32_t(30)));

    // Clone with larger size
    auto cloned1 = col->cloneResized(5);
    EXPECT_EQ(5, cloned1->size());
    EXPECT_EQ(10, (*cloned1)[0].get<int64_t>());
    EXPECT_EQ(20, (*cloned1)[1].get<int64_t>());
    EXPECT_EQ(30, (*cloned1)[2].get<int64_t>());
    EXPECT_EQ(0, (*cloned1)[3].get<int64_t>());  // Zero-filled

    // Clone with smaller size
    auto cloned2 = col->cloneResized(2);
    EXPECT_EQ(2, cloned2->size());
    EXPECT_EQ(10, (*cloned2)[0].get<int64_t>());
    EXPECT_EQ(20, (*cloned2)[1].get<int64_t>());
}

TEST(ColumnVectorTest, CloneEmpty) {
    auto col = ColumnInt32::create();
    col->insert(Field(int32_t(42)));

    auto empty = col->cloneEmpty();
    EXPECT_EQ(0, empty->size());
    EXPECT_EQ("Int32", empty->getName());
}

TEST(ColumnVectorTest, COWSemantics) {
    auto col1 = ColumnInt32::create();
    col1->insert(Field(int32_t(42)));

    // Shallow copy (shared)
    ColumnPtr col2 = col1;
    EXPECT_EQ(2, col1.use_count());

    // Mutate creates deep copy if shared
    auto col3 = col2->mutate();
    col3->insert(Field(int32_t(100)));

    // col1 and col2 unchanged
    EXPECT_EQ(1, col1->size());
    EXPECT_EQ(1, col2->size());

    // col3 modified
    EXPECT_EQ(2, col3->size());
}

TEST(ColumnVectorTest, FloatingPointNaN) {
    auto col = ColumnFloat64::create();
    col->insert(Field(1.0));
    col->insert(Field(std::nan("")));
    col->insert(Field(2.0));

    // NaN handling in comparison
    EXPECT_EQ(0, col->compareAt(1, 1, *col, 1));  // NaN == NaN
    EXPECT_GT(0, col->compareAt(0, 1, *col, 1));   // 1.0 < NaN (with hint 1)
}

// ==================== ColumnString Tests ====================

TEST(ColumnStringTest, Construction) {
    auto col = ColumnString::create();
    EXPECT_EQ(0, col->size());
    EXPECT_TRUE(col->empty());
    EXPECT_EQ("String", col->getName());
    EXPECT_EQ(TypeIndex::String, col->getDataType());
    EXPECT_FALSE(col->isNumeric());
}

TEST(ColumnStringTest, InsertAndAccess) {
    auto col = ColumnString::create();

    col->insert(Field("hello"));
    col->insert(Field("world"));
    col->insert(Field("test"));

    EXPECT_EQ(3, col->size());
    EXPECT_EQ("hello", (*col)[0].get<std::string>());
    EXPECT_EQ("world", (*col)[1].get<std::string>());
    EXPECT_EQ("test", (*col)[2].get<std::string>());
}

TEST(ColumnStringTest, GetDataAt) {
    auto col = ColumnString::create();
    col->insert(Field("hello"));
    col->insert(Field("world"));

    EXPECT_EQ("hello", col->getDataAt(0));
    EXPECT_EQ("world", col->getDataAt(1));
}

TEST(ColumnStringTest, InsertData) {
    auto col = ColumnString::create();
    col->insertData("hello", 5);
    col->insertData("world", 5);

    EXPECT_EQ(2, col->size());
    EXPECT_EQ("hello", (*col)[0].get<std::string>());
    EXPECT_EQ("world", (*col)[1].get<std::string>());
}

TEST(ColumnStringTest, InsertDefault) {
    auto col = ColumnString::create();
    col->insertDefault();
    col->insertDefault();

    EXPECT_EQ(2, col->size());
    EXPECT_EQ("", (*col)[0].get<std::string>());
    EXPECT_EQ("", (*col)[1].get<std::string>());
}

TEST(ColumnStringTest, InsertFrom) {
    auto col1 = ColumnString::create();
    col1->insert(Field("hello"));
    col1->insert(Field("world"));

    auto col2 = ColumnString::create();
    col2->insertFrom(*col1, 0);
    col2->insertFrom(*col1, 1);

    EXPECT_EQ(2, col2->size());
    EXPECT_EQ("hello", (*col2)[0].get<std::string>());
    EXPECT_EQ("world", (*col2)[1].get<std::string>());
}

TEST(ColumnStringTest, InsertRangeFrom) {
    auto col1 = ColumnString::create();
    col1->insert(Field("a"));
    col1->insert(Field("b"));
    col1->insert(Field("c"));
    col1->insert(Field("d"));

    auto col2 = ColumnString::create();
    col2->insertRangeFrom(*col1, 1, 2);  // Insert ["b", "c"]

    EXPECT_EQ(2, col2->size());
    EXPECT_EQ("b", (*col2)[0].get<std::string>());
    EXPECT_EQ("c", (*col2)[1].get<std::string>());
}

TEST(ColumnStringTest, PopBack) {
    auto col = ColumnString::create();
    col->insert(Field("a"));
    col->insert(Field("b"));
    col->insert(Field("c"));

    col->popBack(1);
    EXPECT_EQ(2, col->size());
    EXPECT_EQ("a", (*col)[0].get<std::string>());
    EXPECT_EQ("b", (*col)[1].get<std::string>());
}

TEST(ColumnStringTest, Filter) {
    auto col = ColumnString::create();
    col->insert(Field("a"));
    col->insert(Field("b"));
    col->insert(Field("c"));
    col->insert(Field("d"));

    Filter filt = {1, 0, 1, 0};  // Keep rows 0 and 2
    auto filtered = col->filter(filt, 2);

    EXPECT_EQ(2, filtered->size());
    EXPECT_EQ("a", (*filtered)[0].get<std::string>());
    EXPECT_EQ("c", (*filtered)[1].get<std::string>());
}

TEST(ColumnStringTest, Cut) {
    auto col = ColumnString::create();
    col->insert(Field("a"));
    col->insert(Field("b"));
    col->insert(Field("c"));
    col->insert(Field("d"));

    auto cut = col->cut(1, 2);  // Extract ["b", "c"]

    EXPECT_EQ(2, cut->size());
    EXPECT_EQ("b", (*cut)[0].get<std::string>());
    EXPECT_EQ("c", (*cut)[1].get<std::string>());
}

TEST(ColumnStringTest, CompareAt) {
    auto col1 = ColumnString::create();
    col1->insert(Field("apple"));
    col1->insert(Field("banana"));

    auto col2 = ColumnString::create();
    col2->insert(Field("avocado"));

    EXPECT_LT(col1->compareAt(0, 0, *col2, 0), 0);  // "apple" < "avocado"
    EXPECT_GT(col1->compareAt(1, 0, *col2, 0), 0);  // "banana" > "avocado"
}

TEST(ColumnStringTest, Clone) {
    auto col = ColumnString::create();
    col->insert(Field("hello"));
    col->insert(Field("world"));

    auto cloned = col->clone();
    EXPECT_EQ(2, cloned->size());
    EXPECT_EQ("hello", (*cloned)[0].get<std::string>());
    EXPECT_EQ("world", (*cloned)[1].get<std::string>());
}

TEST(ColumnStringTest, CloneResized) {
    auto col = ColumnString::create();
    col->insert(Field("a"));
    col->insert(Field("b"));
    col->insert(Field("c"));

    // Clone with larger size
    auto cloned1 = col->cloneResized(5);
    EXPECT_EQ(5, cloned1->size());
    EXPECT_EQ("a", (*cloned1)[0].get<std::string>());
    EXPECT_EQ("b", (*cloned1)[1].get<std::string>());
    EXPECT_EQ("c", (*cloned1)[2].get<std::string>());
    EXPECT_EQ("", (*cloned1)[3].get<std::string>());  // Default empty string

    // Clone with smaller size
    auto cloned2 = col->cloneResized(2);
    EXPECT_EQ(2, cloned2->size());
    EXPECT_EQ("a", (*cloned2)[0].get<std::string>());
    EXPECT_EQ("b", (*cloned2)[1].get<std::string>());
}

TEST(ColumnStringTest, EmptyStrings) {
    auto col = ColumnString::create();
    col->insert(Field(""));
    col->insert(Field("a"));
    col->insert(Field(""));

    EXPECT_EQ(3, col->size());
    EXPECT_EQ("", (*col)[0].get<std::string>());
    EXPECT_EQ("a", (*col)[1].get<std::string>());
    EXPECT_EQ("", (*col)[2].get<std::string>());
}

TEST(ColumnStringTest, LargeStrings) {
    auto col = ColumnString::create();
    std::string large(10000, 'x');
    col->insert(Field(large));
    col->insert(Field("small"));

    EXPECT_EQ(2, col->size());
    EXPECT_EQ(large, (*col)[0].get<std::string>());
    EXPECT_EQ("small", (*col)[1].get<std::string>());
}

// ==================== PODArray Tests ====================

TEST(PODArrayTest, Construction) {
    PODArray<int> arr;
    EXPECT_EQ(0, arr.size());
    EXPECT_TRUE(arr.empty());
}

TEST(PODArrayTest, PushBack) {
    PODArray<int> arr;
    arr.push_back(10);
    arr.push_back(20);
    arr.push_back(30);

    EXPECT_EQ(3, arr.size());
    EXPECT_EQ(10, arr[0]);
    EXPECT_EQ(20, arr[1]);
    EXPECT_EQ(30, arr[2]);
}

TEST(PODArrayTest, Resize) {
    PODArray<int> arr;
    arr.resize(5);
    EXPECT_EQ(5, arr.size());

    arr.resize(3);
    EXPECT_EQ(3, arr.size());
}

TEST(PODArrayTest, Reserve) {
    PODArray<int> arr;
    arr.reserve(100);
    EXPECT_GE(arr.capacity(), 100);
}

TEST(PODArrayTest, Clear) {
    PODArray<int> arr;
    arr.push_back(10);
    arr.push_back(20);

    arr.clear();
    EXPECT_EQ(0, arr.size());
    EXPECT_TRUE(arr.empty());
}
