// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/NumericDocValuesWriter.h"

#include "diagon/index/FieldInfo.h"
#include "diagon/store/ByteBuffersIndexOutput.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace diagon::codecs;
using namespace diagon::index;
using namespace diagon::store;

// ==================== NumericDocValuesWriter Tests ====================

TEST(NumericDocValuesWriterTest, BasicWriting) {
    // Create writer
    NumericDocValuesWriter writer("_0", 10);

    // Create field info
    FieldInfo fieldInfo("price", 0);

    // Add values
    writer.addValue(fieldInfo, 0, 100);
    writer.addValue(fieldInfo, 1, 200);
    writer.addValue(fieldInfo, 2, 150);

    // Finish field
    writer.finishField(fieldInfo);

    // Create output buffers
    auto dataOut = std::make_unique<ByteBuffersIndexOutput>("test.dvd");
    auto metaOut = std::make_unique<ByteBuffersIndexOutput>("test.dvm");

    // Flush
    writer.flush(*dataOut, *metaOut);

    // Verify data was written
    EXPECT_GT(dataOut->getFilePointer(), 0);
    EXPECT_GT(metaOut->getFilePointer(), 0);
}

TEST(NumericDocValuesWriterTest, MultipleFields) {
    // Create writer
    NumericDocValuesWriter writer("_0", 5);

    // Create field infos
    FieldInfo priceInfo("price", 0);
    FieldInfo quantityInfo("quantity", 1);

    // Add values for price
    writer.addValue(priceInfo, 0, 100);
    writer.addValue(priceInfo, 1, 200);

    // Add values for quantity
    writer.addValue(quantityInfo, 0, 10);
    writer.addValue(quantityInfo, 1, 20);

    // Finish fields
    writer.finishField(priceInfo);
    writer.finishField(quantityInfo);

    // Create output buffers
    auto dataOut = std::make_unique<ByteBuffersIndexOutput>("test.dvd");
    auto metaOut = std::make_unique<ByteBuffersIndexOutput>("test.dvm");

    // Flush
    writer.flush(*dataOut, *metaOut);

    // Verify data was written
    EXPECT_GT(dataOut->getFilePointer(), 0);
    EXPECT_GT(metaOut->getFilePointer(), 0);
}

TEST(NumericDocValuesWriterTest, DuplicateDocID) {
    // Create writer
    NumericDocValuesWriter writer("_0", 10);

    // Create field info
    FieldInfo fieldInfo("price", 0);

    // Add value
    writer.addValue(fieldInfo, 0, 100);

    // Try to add duplicate - should throw
    EXPECT_THROW(writer.addValue(fieldInfo, 0, 200), std::invalid_argument);
}

TEST(NumericDocValuesWriterTest, DocIDOutOfRange) {
    // Create writer with maxDoc=5
    NumericDocValuesWriter writer("_0", 5);

    // Create field info
    FieldInfo fieldInfo("price", 0);

    // Try to add doc with ID >= maxDoc - should throw
    EXPECT_THROW(writer.addValue(fieldInfo, 5, 100), std::invalid_argument);
    EXPECT_THROW(writer.addValue(fieldInfo, -1, 100), std::invalid_argument);
}

TEST(NumericDocValuesWriterTest, RAMUsage) {
    // Create writer
    NumericDocValuesWriter writer("_0", 100);

    // Initially should have no RAM usage
    EXPECT_EQ(writer.ramBytesUsed(), 0);

    // Create field info
    FieldInfo fieldInfo("price", 0);

    // Add some values
    writer.addValue(fieldInfo, 0, 100);
    writer.addValue(fieldInfo, 1, 200);
    writer.addValue(fieldInfo, 2, 150);

    // Should have RAM usage now (values array + bitmap)
    // 100 docs * 8 bytes (int64_t) + 100 bytes (bitmap) = 900 bytes
    EXPECT_GT(writer.ramBytesUsed(), 800);  // At least 800 bytes
}

TEST(NumericDocValuesWriterTest, MinMaxTracking) {
    // Create writer
    NumericDocValuesWriter writer("_0", 10);

    // Create field info
    FieldInfo fieldInfo("price", 0);

    // Add values (min=50, max=300)
    writer.addValue(fieldInfo, 0, 100);
    writer.addValue(fieldInfo, 1, 300);
    writer.addValue(fieldInfo, 2, 50);
    writer.addValue(fieldInfo, 3, 200);

    // Finish field
    writer.finishField(fieldInfo);

    // Create output buffers
    auto dataOut = std::make_unique<ByteBuffersIndexOutput>("test.dvd");
    auto metaOut = std::make_unique<ByteBuffersIndexOutput>("test.dvm");

    // Flush
    writer.flush(*dataOut, *metaOut);

    // Note: We can't easily verify min/max from here without reading back the files
    // This is just a smoke test to ensure no crashes
    EXPECT_GT(dataOut->getFilePointer(), 0);
}
