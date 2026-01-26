// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/StoredFieldsReader.h"
#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DocumentsWriterPerThread.h"
#include "diagon/index/SegmentReader.h"
#include "diagon/store/FSDirectory.h"
#include "diagon/store/MMapDirectory.h"
#include "diagon/store/IOContext.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace diagon::codecs;
using namespace diagon::document;
using namespace diagon::index;
using namespace diagon::store;

// ==================== Integration Test Fixture ====================

class MMapDirectoryIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir = std::filesystem::temp_directory_path() /
                   ("diagon_mmap_int_test_" + std::to_string(std::time(nullptr)));
        std::filesystem::create_directories(test_dir);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir);
    }

    std::filesystem::path test_dir;
};

// ==================== Write with FSDirectory, Read with MMapDirectory ====================

TEST_F(MMapDirectoryIntegrationTest, WriteWithFSDirectoryReadWithMMap) {
    std::shared_ptr<SegmentInfo> segmentInfo;

    // Phase 1: Write documents using FSDirectory
    {
        auto fs_dir = std::make_unique<FSDirectory>(test_dir);

        DocumentsWriterPerThread::Config config;
        config.maxBufferedDocs = 100;
        config.ramBufferSizeMB = 16;

        DocumentsWriterPerThread dwpt(config, fs_dir.get());

        // Write 50 documents with various field types
        for (int i = 0; i < 50; i++) {
            Document doc;

            // Stored string field
            FieldType stringType;
            stringType.stored = true;
            doc.add(std::make_unique<Field>("name", "Document_" + std::to_string(i), stringType));

            // Stored numeric fields
            FieldType numericType;
            numericType.stored = true;
            doc.add(std::make_unique<Field>("id", int64_t(i), numericType));
            doc.add(std::make_unique<Field>("value", int64_t(i * 100), numericType));

            dwpt.addDocument(doc);
        }

        // Flush to create segment files
        segmentInfo = dwpt.flush();
        ASSERT_NE(segmentInfo, nullptr);
        EXPECT_EQ(segmentInfo->maxDoc(), 50);
    }

    // Phase 2: Read documents using MMapDirectory
    {
        auto mmap_dir = MMapDirectory::open(test_dir);
        ASSERT_NE(mmap_dir, nullptr);

        auto reader = SegmentReader::open(*mmap_dir, segmentInfo);
        ASSERT_NE(reader, nullptr);

        auto storedFieldsReader = reader->storedFieldsReader();
        ASSERT_NE(storedFieldsReader, nullptr);
        EXPECT_EQ(storedFieldsReader->numDocs(), 50);

        // Verify all documents
        for (int i = 0; i < 50; i++) {
            auto fields = storedFieldsReader->document(i);

            ASSERT_TRUE(fields.find("name") != fields.end());
            EXPECT_EQ(std::get<std::string>(fields["name"]), "Document_" + std::to_string(i));

            ASSERT_TRUE(fields.find("id") != fields.end());
            EXPECT_EQ(std::get<int64_t>(fields["id"]), i);

            ASSERT_TRUE(fields.find("value") != fields.end());
            EXPECT_EQ(std::get<int64_t>(fields["value"]), i * 100);
        }
    }
}

// ==================== Write and Read Both with MMapDirectory ====================

TEST_F(MMapDirectoryIntegrationTest, WriteAndReadWithMMap) {
    std::shared_ptr<SegmentInfo> segmentInfo;

    // Create MMapDirectory for both writing and reading
    auto mmap_dir = MMapDirectory::open(test_dir);
    ASSERT_NE(mmap_dir, nullptr);

    // Phase 1: Write documents
    {
        DocumentsWriterPerThread::Config config;
        config.maxBufferedDocs = 100;
        config.ramBufferSizeMB = 16;

        DocumentsWriterPerThread dwpt(config, mmap_dir.get());

        // Write documents
        for (int i = 0; i < 30; i++) {
            Document doc;

            FieldType type;
            type.stored = true;
            doc.add(std::make_unique<Field>("text", "Content_" + std::to_string(i), type));
            doc.add(std::make_unique<Field>("number", int64_t(i * 10), type));

            dwpt.addDocument(doc);
        }

        segmentInfo = dwpt.flush();
        ASSERT_NE(segmentInfo, nullptr);
    }

    // Phase 2: Read documents with same directory
    {
        auto reader = SegmentReader::open(*mmap_dir, segmentInfo);
        auto storedFieldsReader = reader->storedFieldsReader();

        EXPECT_EQ(storedFieldsReader->numDocs(), 30);

        // Verify random access
        std::vector<int> indices = {0, 15, 29, 10, 20};
        for (int idx : indices) {
            auto fields = storedFieldsReader->document(idx);

            EXPECT_EQ(std::get<std::string>(fields["text"]), "Content_" + std::to_string(idx));
            EXPECT_EQ(std::get<int64_t>(fields["number"]), idx * 10);
        }
    }
}

// ==================== Concurrent Reads with Clone ====================

TEST_F(MMapDirectoryIntegrationTest, ConcurrentReadsWithClone) {
    std::shared_ptr<SegmentInfo> segmentInfo;

    // Write data
    {
        auto fs_dir = std::make_unique<FSDirectory>(test_dir);
        DocumentsWriterPerThread::Config config;
        DocumentsWriterPerThread dwpt(config, fs_dir.get());

        for (int i = 0; i < 100; i++) {
            Document doc;
            FieldType type;
            type.stored = true;
            doc.add(std::make_unique<Field>("id", int64_t(i), type));
            doc.add(std::make_unique<Field>("data", "Data_" + std::to_string(i), type));
            dwpt.addDocument(doc);
        }

        segmentInfo = dwpt.flush();
    }

    // Read concurrently with multiple threads
    auto mmap_dir = MMapDirectory::open(test_dir);

    constexpr int NUM_THREADS = 4;
    constexpr int READS_PER_THREAD = 25;

    std::vector<std::thread> threads;
    std::atomic<int> errors{0};

    for (int t = 0; t < NUM_THREADS; t++) {
        threads.emplace_back([&, t]() {
            try {
                auto reader = SegmentReader::open(*mmap_dir, segmentInfo);
                auto storedFieldsReader = reader->storedFieldsReader();

                // Each thread reads different documents
                for (int i = 0; i < READS_PER_THREAD; i++) {
                    int doc_id = t * READS_PER_THREAD + i;
                    auto fields = storedFieldsReader->document(doc_id);

                    if (std::get<int64_t>(fields["id"]) != doc_id) {
                        errors++;
                    }
                    if (std::get<std::string>(fields["data"]) != "Data_" + std::to_string(doc_id)) {
                        errors++;
                    }
                }
            } catch (...) {
                errors++;
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(errors, 0) << "Concurrent reads should not produce errors";
}

// ==================== Large File Performance ====================

TEST_F(MMapDirectoryIntegrationTest, LargeFileHandling) {
    std::shared_ptr<SegmentInfo> segmentInfo;

    // Write a larger dataset
    {
        auto fs_dir = std::make_unique<FSDirectory>(test_dir);
        DocumentsWriterPerThread::Config config;
        config.maxBufferedDocs = 1000;
        DocumentsWriterPerThread dwpt(config, fs_dir.get());

        // Write 500 documents with larger content
        for (int i = 0; i < 500; i++) {
            Document doc;
            FieldType type;
            type.stored = true;

            // Create larger strings to ensure files exceed single chunk
            std::string large_text(1024, 'A' + (i % 26));  // 1KB per doc
            doc.add(std::make_unique<Field>("content", large_text, type));
            doc.add(std::make_unique<Field>("id", int64_t(i), type));

            dwpt.addDocument(doc);
        }

        segmentInfo = dwpt.flush();
    }

    // Read with MMapDirectory
    auto mmap_dir = MMapDirectory::open(test_dir);
    auto reader = SegmentReader::open(*mmap_dir, segmentInfo);
    auto storedFieldsReader = reader->storedFieldsReader();

    EXPECT_EQ(storedFieldsReader->numDocs(), 500);

    // Verify random access to documents across the file
    std::vector<int> sample_docs = {0, 100, 250, 400, 499};
    for (int doc_id : sample_docs) {
        auto fields = storedFieldsReader->document(doc_id);

        ASSERT_TRUE(fields.find("id") != fields.end());
        EXPECT_EQ(std::get<int64_t>(fields["id"]), doc_id);

        ASSERT_TRUE(fields.find("content") != fields.end());
        auto content = std::get<std::string>(fields["content"]);
        EXPECT_EQ(content.size(), 1024);
        EXPECT_EQ(content[0], 'A' + (doc_id % 26));
    }
}

// ==================== Different IOContext Hints ====================

TEST_F(MMapDirectoryIntegrationTest, DifferentIOContextHints) {
    // Create test file
    auto fs_dir = std::make_unique<FSDirectory>(test_dir);
    auto output = fs_dir->createOutput("test.bin", IOContext::DEFAULT);

    std::vector<uint8_t> data(10 * 1024 * 1024);  // 10MB
    for (size_t i = 0; i < data.size(); i++) {
        data[i] = static_cast<uint8_t>(i & 0xFF);
    }
    output->writeBytes(data.data(), data.size());
    output->close();

    auto mmap_dir = MMapDirectory::open(test_dir);

    // Test SEQUENTIAL access
    {
        auto input = mmap_dir->openInput("test.bin", IOContext(IOContext::Type::MERGE));
        ASSERT_NE(input, nullptr);

        uint8_t buffer[1024];
        input->readBytes(buffer, 1024);

        for (int i = 0; i < 1024; i++) {
            EXPECT_EQ(buffer[i], i & 0xFF);
        }
    }

    // Test RANDOM access
    {
        auto input = mmap_dir->openInput("test.bin", IOContext(IOContext::Type::READ));
        ASSERT_NE(input, nullptr);

        // Random seeks
        std::vector<int64_t> positions = {100, 50000, 1000000, 5000000, 9000000};
        for (auto pos : positions) {
            input->seek(pos);
            uint8_t value = input->readByte();
            EXPECT_EQ(value, pos & 0xFF);
        }
    }

    // Test NORMAL access
    {
        auto input = mmap_dir->openInput("test.bin", IOContext::DEFAULT);
        ASSERT_NE(input, nullptr);

        input->seek(1024);
        uint8_t value = input->readByte();
        EXPECT_EQ(value, 1024 & 0xFF);
    }
}

// ==================== Preload Configuration ====================

TEST_F(MMapDirectoryIntegrationTest, PreloadConfiguration) {
    // Create test file
    auto fs_dir = std::make_unique<FSDirectory>(test_dir);
    auto output = fs_dir->createOutput("data.bin", IOContext::DEFAULT);

    std::vector<uint8_t> data(5 * 1024 * 1024);  // 5MB
    for (size_t i = 0; i < data.size(); i++) {
        data[i] = static_cast<uint8_t>(i & 0xFF);
    }
    output->writeBytes(data.data(), data.size());
    output->close();

    // Test with preload enabled
    {
        auto mmap_dir = MMapDirectory::open(test_dir);
        mmap_dir->setPreload(true);
        EXPECT_TRUE(mmap_dir->isPreload());

        auto input = mmap_dir->openInput("data.bin", IOContext::DEFAULT);
        ASSERT_NE(input, nullptr);

        // Access should be fast (pages already loaded)
        input->seek(2 * 1024 * 1024);
        uint8_t value = input->readByte();
        EXPECT_EQ(value, (2 * 1024 * 1024) & 0xFF);
    }

    // Test with preload disabled
    {
        auto mmap_dir = MMapDirectory::open(test_dir);
        mmap_dir->setPreload(false);
        EXPECT_FALSE(mmap_dir->isPreload());

        auto input = mmap_dir->openInput("data.bin", IOContext::DEFAULT);
        ASSERT_NE(input, nullptr);

        // Pages loaded on demand
        input->seek(3 * 1024 * 1024);
        uint8_t value = input->readByte();
        EXPECT_EQ(value, (3 * 1024 * 1024) & 0xFF);
    }
}

// ==================== Error Handling ====================

TEST_F(MMapDirectoryIntegrationTest, FileNotFoundError) {
    auto mmap_dir = MMapDirectory::open(test_dir);

    EXPECT_THROW(
        mmap_dir->openInput("nonexistent.bin", IOContext::DEFAULT),
        std::exception
    );
}

TEST_F(MMapDirectoryIntegrationTest, ReadPastEOF) {
    // Create small file
    auto fs_dir = std::make_unique<FSDirectory>(test_dir);
    auto output = fs_dir->createOutput("small.bin", IOContext::DEFAULT);
    uint8_t data[100];
    output->writeBytes(data, 100);
    output->close();

    auto mmap_dir = MMapDirectory::open(test_dir);
    auto input = mmap_dir->openInput("small.bin", IOContext::DEFAULT);

    EXPECT_EQ(input->length(), 100);

    // Seek past end
    EXPECT_THROW(input->seek(200), std::exception);

    // Read past end
    input->seek(99);
    input->readByte();  // OK - last byte
    EXPECT_THROW(input->readByte(), std::exception);  // Past EOF
}

// ==================== Mixed Directory Operations ====================

TEST_F(MMapDirectoryIntegrationTest, MixedFSAndMMapOperations) {
    // Create files with FSDirectory
    {
        auto fs_dir = std::make_unique<FSDirectory>(test_dir);

        auto out1 = fs_dir->createOutput("file1.bin", IOContext::DEFAULT);
        uint8_t data1[] = {1, 2, 3, 4, 5};
        out1->writeBytes(data1, 5);
        out1->close();

        auto out2 = fs_dir->createOutput("file2.bin", IOContext::DEFAULT);
        uint8_t data2[] = {10, 20, 30, 40, 50};
        out2->writeBytes(data2, 5);
        out2->close();
    }

    // Read with MMapDirectory
    {
        auto mmap_dir = MMapDirectory::open(test_dir);

        auto in1 = mmap_dir->openInput("file1.bin", IOContext::DEFAULT);
        EXPECT_EQ(in1->readByte(), 1);
        EXPECT_EQ(in1->readByte(), 2);

        auto in2 = mmap_dir->openInput("file2.bin", IOContext::DEFAULT);
        EXPECT_EQ(in2->readByte(), 10);
        EXPECT_EQ(in2->readByte(), 20);
    }
}
