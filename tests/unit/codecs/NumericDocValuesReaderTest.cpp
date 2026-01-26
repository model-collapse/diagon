// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/NumericDocValuesReader.h"
#include "diagon/codecs/NumericDocValuesWriter.h"

#include "diagon/index/FieldInfo.h"
#include "diagon/store/ByteBuffersIndexInput.h"
#include "diagon/store/ByteBuffersIndexOutput.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace diagon::codecs;
using namespace diagon::index;
using namespace diagon::store;

// ==================== NumericDocValuesReader Tests ====================

TEST(NumericDocValuesReaderTest, BasicReadWrite) {
    // Write values
    NumericDocValuesWriter writer("_0", 5);
    FieldInfo fieldInfo("price", 0);

    writer.addValue(fieldInfo, 0, 100);
    writer.addValue(fieldInfo, 1, 200);
    writer.addValue(fieldInfo, 2, 150);

    writer.finishField(fieldInfo);

    auto dataOut = std::make_unique<ByteBuffersIndexOutput>("test.dvd");
    auto metaOut = std::make_unique<ByteBuffersIndexOutput>("test.dvm");

    writer.flush(*dataOut, *metaOut);

    // Convert outputs to inputs
    auto dataIn = std::make_unique<ByteBuffersIndexInput>("test.dvd", dataOut->toArrayCopy());
    auto metaIn = std::make_unique<ByteBuffersIndexInput>("test.dvm", metaOut->toArrayCopy());

    // Read values back
    NumericDocValuesReader reader(std::move(dataIn), std::move(metaIn));

    // Check field exists
    EXPECT_TRUE(reader.hasField("price"));
    EXPECT_FALSE(reader.hasField("nonexistent"));

    // Get numeric doc values
    auto dv = reader.getNumeric("price");
    ASSERT_NE(dv, nullptr);

    // Verify values using advanceExact
    EXPECT_TRUE(dv->advanceExact(0));
    EXPECT_EQ(dv->longValue(), 100);

    EXPECT_TRUE(dv->advanceExact(1));
    EXPECT_EQ(dv->longValue(), 200);

    EXPECT_TRUE(dv->advanceExact(2));
    EXPECT_EQ(dv->longValue(), 150);

    // Docs without values return 0 (in our simple format)
    EXPECT_TRUE(dv->advanceExact(3));
    EXPECT_EQ(dv->longValue(), 0);
}

TEST(NumericDocValuesReaderTest, MultipleFields) {
    // Write values for multiple fields
    NumericDocValuesWriter writer("_0", 3);
    FieldInfo priceInfo("price", 0);
    FieldInfo quantityInfo("quantity", 1);

    writer.addValue(priceInfo, 0, 100);
    writer.addValue(priceInfo, 1, 200);

    writer.addValue(quantityInfo, 0, 10);
    writer.addValue(quantityInfo, 1, 20);

    writer.finishField(priceInfo);
    writer.finishField(quantityInfo);

    auto dataOut = std::make_unique<ByteBuffersIndexOutput>("test.dvd");
    auto metaOut = std::make_unique<ByteBuffersIndexOutput>("test.dvm");

    writer.flush(*dataOut, *metaOut);

    // Convert outputs to inputs
    auto dataIn = std::make_unique<ByteBuffersIndexInput>("test.dvd", dataOut->toArrayCopy());
    auto metaIn = std::make_unique<ByteBuffersIndexInput>("test.dvm", metaOut->toArrayCopy());

    // Read values back
    NumericDocValuesReader reader(std::move(dataIn), std::move(metaIn));

    // Check both fields exist
    EXPECT_TRUE(reader.hasField("price"));
    EXPECT_TRUE(reader.hasField("quantity"));

    // Get price doc values
    auto priceDv = reader.getNumeric("price");
    ASSERT_NE(priceDv, nullptr);
    EXPECT_TRUE(priceDv->advanceExact(0));
    EXPECT_EQ(priceDv->longValue(), 100);
    EXPECT_TRUE(priceDv->advanceExact(1));
    EXPECT_EQ(priceDv->longValue(), 200);

    // Get quantity doc values
    auto quantityDv = reader.getNumeric("quantity");
    ASSERT_NE(quantityDv, nullptr);
    EXPECT_TRUE(quantityDv->advanceExact(0));
    EXPECT_EQ(quantityDv->longValue(), 10);
    EXPECT_TRUE(quantityDv->advanceExact(1));
    EXPECT_EQ(quantityDv->longValue(), 20);
}

TEST(NumericDocValuesReaderTest, Iteration) {
    // Write values
    NumericDocValuesWriter writer("_0", 5);
    FieldInfo fieldInfo("score", 0);

    writer.addValue(fieldInfo, 0, 10);
    writer.addValue(fieldInfo, 1, 20);
    writer.addValue(fieldInfo, 2, 30);
    writer.addValue(fieldInfo, 3, 40);
    writer.addValue(fieldInfo, 4, 50);

    writer.finishField(fieldInfo);

    auto dataOut = std::make_unique<ByteBuffersIndexOutput>("test.dvd");
    auto metaOut = std::make_unique<ByteBuffersIndexOutput>("test.dvm");

    writer.flush(*dataOut, *metaOut);

    // Convert outputs to inputs
    auto dataIn = std::make_unique<ByteBuffersIndexInput>("test.dvd", dataOut->toArrayCopy());
    auto metaIn = std::make_unique<ByteBuffersIndexInput>("test.dvm", metaOut->toArrayCopy());

    // Read values back
    NumericDocValuesReader reader(std::move(dataIn), std::move(metaIn));

    auto dv = reader.getNumeric("score");
    ASSERT_NE(dv, nullptr);

    // Test nextDoc() iteration
    int docID = dv->nextDoc();
    EXPECT_EQ(docID, 0);
    EXPECT_EQ(dv->longValue(), 10);

    docID = dv->nextDoc();
    EXPECT_EQ(docID, 1);
    EXPECT_EQ(dv->longValue(), 20);

    docID = dv->nextDoc();
    EXPECT_EQ(docID, 2);
    EXPECT_EQ(dv->longValue(), 30);

    // Test advance()
    docID = dv->advance(4);
    EXPECT_EQ(docID, 4);
    EXPECT_EQ(dv->longValue(), 50);
}

TEST(NumericDocValuesReaderTest, FieldMetadata) {
    // Write values
    NumericDocValuesWriter writer("_0", 5);
    FieldInfo fieldInfo("price", 0);

    writer.addValue(fieldInfo, 0, 50);
    writer.addValue(fieldInfo, 1, 300);
    writer.addValue(fieldInfo, 2, 100);

    writer.finishField(fieldInfo);

    auto dataOut = std::make_unique<ByteBuffersIndexOutput>("test.dvd");
    auto metaOut = std::make_unique<ByteBuffersIndexOutput>("test.dvm");

    writer.flush(*dataOut, *metaOut);

    // Convert outputs to inputs
    auto dataIn = std::make_unique<ByteBuffersIndexInput>("test.dvd", dataOut->toArrayCopy());
    auto metaIn = std::make_unique<ByteBuffersIndexInput>("test.dvm", metaOut->toArrayCopy());

    // Read values back
    NumericDocValuesReader reader(std::move(dataIn), std::move(metaIn));

    // Check metadata
    const auto* meta = reader.getFieldMetadata("price");
    ASSERT_NE(meta, nullptr);
    EXPECT_EQ(meta->fieldName, "price");
    EXPECT_EQ(meta->fieldNumber, 0);
    EXPECT_EQ(meta->numDocs, 5);
    EXPECT_EQ(meta->numValues, 3);
    EXPECT_EQ(meta->minValue, 50);
    EXPECT_EQ(meta->maxValue, 300);
}
