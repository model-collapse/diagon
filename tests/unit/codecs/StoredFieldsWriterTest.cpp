// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/StoredFieldsWriter.h"

#include "diagon/index/FieldInfo.h"
#include "diagon/store/ByteBuffersIndexOutput.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace diagon::codecs;
using namespace diagon::index;
using namespace diagon::store;

// ==================== StoredFieldsWriter Tests ====================

TEST(StoredFieldsWriterTest, BasicWriting) {
    // Create writer
    StoredFieldsWriter writer("_0");

    // Create field infos
    FieldInfo field1("title", 0);
    FieldInfo field2("count", 1);

    // Write first document
    writer.startDocument();
    writer.writeField(field1, std::string("Test Document"));
    writer.writeField(field2, int64_t(42));
    writer.finishDocument();

    // Write second document
    writer.startDocument();
    writer.writeField(field1, std::string("Another Document"));
    writer.writeField(field2, int64_t(100));
    writer.finishDocument();

    // Finish
    writer.finish(2);

    // Create output buffers
    auto dataOut = std::make_unique<ByteBuffersIndexOutput>("test.fdt");
    auto indexOut = std::make_unique<ByteBuffersIndexOutput>("test.fdx");

    // Flush
    writer.flush(*dataOut, *indexOut);

    // Verify data was written
    EXPECT_GT(dataOut->getFilePointer(), 0);
    EXPECT_GT(indexOut->getFilePointer(), 0);

    writer.close();
}

TEST(StoredFieldsWriterTest, MultipleFields) {
    StoredFieldsWriter writer("_0");

    FieldInfo field1("name", 0);
    FieldInfo field2("age", 1);
    FieldInfo field3("score", 2);

    // Write document with multiple fields
    writer.startDocument();
    writer.writeField(field1, std::string("John Doe"));
    writer.writeField(field2, int32_t(30));
    writer.writeField(field3, int64_t(9500));
    writer.finishDocument();

    writer.finish(1);

    auto dataOut = std::make_unique<ByteBuffersIndexOutput>("test.fdt");
    auto indexOut = std::make_unique<ByteBuffersIndexOutput>("test.fdx");

    writer.flush(*dataOut, *indexOut);

    EXPECT_GT(dataOut->getFilePointer(), 0);
    EXPECT_GT(indexOut->getFilePointer(), 0);

    writer.close();
}

TEST(StoredFieldsWriterTest, EmptyDocument) {
    StoredFieldsWriter writer("_0");

    // Write document with no fields
    writer.startDocument();
    writer.finishDocument();

    writer.finish(1);

    auto dataOut = std::make_unique<ByteBuffersIndexOutput>("test.fdt");
    auto indexOut = std::make_unique<ByteBuffersIndexOutput>("test.fdx");

    writer.flush(*dataOut, *indexOut);

    // Should still write headers and empty document
    EXPECT_GT(dataOut->getFilePointer(), 0);
    EXPECT_GT(indexOut->getFilePointer(), 0);

    writer.close();
}

TEST(StoredFieldsWriterTest, ErrorHandling) {
    StoredFieldsWriter writer("_0");

    FieldInfo field("test", 0);

    // Try to write field without starting document - should throw
    EXPECT_THROW(writer.writeField(field, std::string("value")), std::runtime_error);

    // Start document
    writer.startDocument();

    // Try to start another document without finishing - should throw
    EXPECT_THROW(writer.startDocument(), std::runtime_error);

    // Finish document
    writer.finishDocument();

    // Try to finish document without being in one - should throw
    EXPECT_THROW(writer.finishDocument(), std::runtime_error);

    // Finish writing with wrong count - should throw
    EXPECT_THROW(writer.finish(999), std::runtime_error);

    // Correct finish
    writer.finish(1);

    // Try to finish again - should throw
    EXPECT_THROW(writer.finish(1), std::runtime_error);

    writer.close();
}

TEST(StoredFieldsWriterTest, RAMUsage) {
    StoredFieldsWriter writer("_0");

    // Initially should have minimal RAM usage
    EXPECT_EQ(writer.ramBytesUsed(), 0);

    FieldInfo field("text", 0);

    // Add some documents
    for (int i = 0; i < 5; i++) {
        writer.startDocument();
        writer.writeField(field, std::string("Document " + std::to_string(i)));
        writer.finishDocument();
    }

    // Should have RAM usage now
    EXPECT_GT(writer.ramBytesUsed(), 0);

    writer.finish(5);
    writer.close();
}

TEST(StoredFieldsWriterTest, DifferentFieldTypes) {
    StoredFieldsWriter writer("_0");

    FieldInfo stringField("text", 0);
    FieldInfo intField("count", 1);
    FieldInfo longField("timestamp", 2);

    writer.startDocument();
    writer.writeField(stringField, std::string("Hello World"));
    writer.writeField(intField, int32_t(123));
    writer.writeField(longField, int64_t(1234567890L));
    writer.finishDocument();

    writer.finish(1);

    auto dataOut = std::make_unique<ByteBuffersIndexOutput>("test.fdt");
    auto indexOut = std::make_unique<ByteBuffersIndexOutput>("test.fdx");

    writer.flush(*dataOut, *indexOut);

    EXPECT_GT(dataOut->getFilePointer(), 0);

    writer.close();
}
