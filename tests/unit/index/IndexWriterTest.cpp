// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/IndexWriter.h"
#include "diagon/store/FSDirectory.h"
#include "diagon/document/Document.h"
#include "diagon/document/Field.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <thread>
#include <vector>

using namespace diagon;
using namespace diagon::index;
using namespace diagon::store;
using namespace diagon::document;

class IndexWriterTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir = std::filesystem::temp_directory_path() / "diagon_test_writer";
        std::filesystem::create_directories(test_dir);
        dir = FSDirectory::open(test_dir);
    }

    void TearDown() override {
        dir->close();
        std::filesystem::remove_all(test_dir);
    }

    std::filesystem::path test_dir;
    std::unique_ptr<FSDirectory> dir;

    // Helper to create a simple document
    Document createDocument(const std::string& content) {
        Document doc;
        doc.add(std::make_unique<TextField>("body", content, TextField::TYPE_STORED));
        return doc;
    }
};

// ==================== IndexWriterConfig Tests ====================

TEST_F(IndexWriterTest, ConfigDefaultValues) {
    IndexWriterConfig config;

    EXPECT_DOUBLE_EQ(16.0, config.getRAMBufferSizeMB());
    EXPECT_EQ(-1, config.getMaxBufferedDocs());
    EXPECT_EQ(IndexWriterConfig::OpenMode::CREATE_OR_APPEND, config.getOpenMode());
    EXPECT_TRUE(config.getCommitOnClose());
    EXPECT_TRUE(config.getUseCompoundFile());
}

TEST_F(IndexWriterTest, ConfigSetRAMBufferSize) {
    IndexWriterConfig config;

    config.setRAMBufferSizeMB(32.0);
    EXPECT_DOUBLE_EQ(32.0, config.getRAMBufferSizeMB());

    config.setRAMBufferSizeMB(128.0);
    EXPECT_DOUBLE_EQ(128.0, config.getRAMBufferSizeMB());
}

TEST_F(IndexWriterTest, ConfigSetMaxBufferedDocs) {
    IndexWriterConfig config;

    config.setMaxBufferedDocs(1000);
    EXPECT_EQ(1000, config.getMaxBufferedDocs());

    config.setMaxBufferedDocs(10000);
    EXPECT_EQ(10000, config.getMaxBufferedDocs());
}

TEST_F(IndexWriterTest, ConfigSetOpenMode) {
    IndexWriterConfig config;

    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
    EXPECT_EQ(IndexWriterConfig::OpenMode::CREATE, config.getOpenMode());

    config.setOpenMode(IndexWriterConfig::OpenMode::APPEND);
    EXPECT_EQ(IndexWriterConfig::OpenMode::APPEND, config.getOpenMode());

    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE_OR_APPEND);
    EXPECT_EQ(IndexWriterConfig::OpenMode::CREATE_OR_APPEND, config.getOpenMode());
}

TEST_F(IndexWriterTest, ConfigSetCommitOnClose) {
    IndexWriterConfig config;

    config.setCommitOnClose(false);
    EXPECT_FALSE(config.getCommitOnClose());

    config.setCommitOnClose(true);
    EXPECT_TRUE(config.getCommitOnClose());
}

TEST_F(IndexWriterTest, ConfigSetUseCompoundFile) {
    IndexWriterConfig config;

    config.setUseCompoundFile(false);
    EXPECT_FALSE(config.getUseCompoundFile());

    config.setUseCompoundFile(true);
    EXPECT_TRUE(config.getUseCompoundFile());
}

TEST_F(IndexWriterTest, ConfigFluentInterface) {
    IndexWriterConfig config;

    config.setRAMBufferSizeMB(64.0)
          .setMaxBufferedDocs(5000)
          .setOpenMode(IndexWriterConfig::OpenMode::CREATE)
          .setCommitOnClose(false)
          .setUseCompoundFile(false);

    EXPECT_DOUBLE_EQ(64.0, config.getRAMBufferSizeMB());
    EXPECT_EQ(5000, config.getMaxBufferedDocs());
    EXPECT_EQ(IndexWriterConfig::OpenMode::CREATE, config.getOpenMode());
    EXPECT_FALSE(config.getCommitOnClose());
    EXPECT_FALSE(config.getUseCompoundFile());
}

// ==================== IndexWriter Construction Tests ====================

TEST_F(IndexWriterTest, ConstructorSuccess) {
    IndexWriterConfig config;
    auto writer = std::make_unique<IndexWriter>(*dir, config);

    EXPECT_TRUE(writer->isOpen());
    EXPECT_EQ(1, writer->getSequenceNumber());
}

TEST_F(IndexWriterTest, ConstructorObtainsWriteLock) {
    IndexWriterConfig config;
    auto writer1 = std::make_unique<IndexWriter>(*dir, config);

    // Second writer should fail to obtain lock
    EXPECT_THROW(
        auto writer2 = std::make_unique<IndexWriter>(*dir, config),
        LockObtainFailedException
    );
}

TEST_F(IndexWriterTest, WriteLockReleasedOnClose) {
    IndexWriterConfig config;

    {
        auto writer = std::make_unique<IndexWriter>(*dir, config);
        writer->close();
    }

    // Should be able to open new writer after first is closed
    EXPECT_NO_THROW(
        auto writer2 = std::make_unique<IndexWriter>(*dir, config)
    );
}

TEST_F(IndexWriterTest, WriteLockReleasedOnDestruction) {
    IndexWriterConfig config;

    {
        auto writer = std::make_unique<IndexWriter>(*dir, config);
        // Writer destroyed without explicit close
    }

    // Should be able to open new writer after first is destroyed
    EXPECT_NO_THROW(
        auto writer2 = std::make_unique<IndexWriter>(*dir, config)
    );
}

// ==================== IndexWriter Lifecycle Tests ====================

TEST_F(IndexWriterTest, IsOpenAfterConstruction) {
    IndexWriterConfig config;
    auto writer = std::make_unique<IndexWriter>(*dir, config);

    EXPECT_TRUE(writer->isOpen());
}

TEST_F(IndexWriterTest, IsClosedAfterClose) {
    IndexWriterConfig config;
    auto writer = std::make_unique<IndexWriter>(*dir, config);

    writer->close();
    EXPECT_FALSE(writer->isOpen());
}

TEST_F(IndexWriterTest, DoubleCloseIsIdempotent) {
    IndexWriterConfig config;
    auto writer = std::make_unique<IndexWriter>(*dir, config);

    writer->close();
    EXPECT_NO_THROW(writer->close());  // Should not throw
    EXPECT_FALSE(writer->isOpen());
}

TEST_F(IndexWriterTest, OperationsAfterCloseThrow) {
    IndexWriterConfig config;
    auto writer = std::make_unique<IndexWriter>(*dir, config);

    writer->close();

    Document doc = createDocument("test");
    EXPECT_THROW(writer->addDocument(doc), AlreadyClosedException);
    EXPECT_THROW(writer->updateDocument(), AlreadyClosedException);
    EXPECT_THROW(writer->deleteDocuments(), AlreadyClosedException);
    EXPECT_THROW(writer->commit(), AlreadyClosedException);
    EXPECT_THROW(writer->flush(), AlreadyClosedException);
    EXPECT_THROW(writer->rollback(), AlreadyClosedException);
    EXPECT_THROW(writer->forceMerge(1), AlreadyClosedException);
    EXPECT_THROW(writer->waitForMerges(), AlreadyClosedException);
}

// ==================== Sequence Number Tests ====================

TEST_F(IndexWriterTest, InitialSequenceNumber) {
    IndexWriterConfig config;
    auto writer = std::make_unique<IndexWriter>(*dir, config);

    EXPECT_EQ(1, writer->getSequenceNumber());
}

TEST_F(IndexWriterTest, AddDocumentIncrementsSequenceNumber) {
    IndexWriterConfig config;
    auto writer = std::make_unique<IndexWriter>(*dir, config);

    Document doc1 = createDocument("test1");
    int64_t seqNo1 = writer->addDocument(doc1);
    EXPECT_EQ(1, seqNo1);
    EXPECT_EQ(2, writer->getSequenceNumber());

    Document doc2 = createDocument("test2");
    int64_t seqNo2 = writer->addDocument(doc2);
    EXPECT_EQ(2, seqNo2);
    EXPECT_EQ(3, writer->getSequenceNumber());
}

TEST_F(IndexWriterTest, UpdateDocumentIncrementsSequenceNumber) {
    IndexWriterConfig config;
    auto writer = std::make_unique<IndexWriter>(*dir, config);

    int64_t seqNo = writer->updateDocument();
    EXPECT_EQ(1, seqNo);
    EXPECT_EQ(2, writer->getSequenceNumber());
}

TEST_F(IndexWriterTest, DeleteDocumentsIncrementsSequenceNumber) {
    IndexWriterConfig config;
    auto writer = std::make_unique<IndexWriter>(*dir, config);

    int64_t seqNo = writer->deleteDocuments();
    EXPECT_EQ(1, seqNo);
    EXPECT_EQ(2, writer->getSequenceNumber());
}

TEST_F(IndexWriterTest, CommitIncrementsSequenceNumber) {
    IndexWriterConfig config;
    auto writer = std::make_unique<IndexWriter>(*dir, config);

    int64_t seqNo = writer->commit();
    EXPECT_EQ(1, seqNo);
    EXPECT_EQ(2, writer->getSequenceNumber());
}

TEST_F(IndexWriterTest, SequenceNumbersAreMonotonic) {
    IndexWriterConfig config;
    auto writer = std::make_unique<IndexWriter>(*dir, config);

    std::vector<int64_t> seqNos;
    Document doc1 = createDocument("test1");
    seqNos.push_back(writer->addDocument(doc1));
    seqNos.push_back(writer->updateDocument());
    seqNos.push_back(writer->deleteDocuments());
    seqNos.push_back(writer->commit());
    Document doc2 = createDocument("test2");
    seqNos.push_back(writer->addDocument(doc2));

    // Check all sequence numbers are unique and increasing
    for (size_t i = 1; i < seqNos.size(); i++) {
        EXPECT_GT(seqNos[i], seqNos[i-1]);
    }
}

// ==================== Configuration Access Tests ====================

TEST_F(IndexWriterTest, GetConfigReturnsConfiguration) {
    IndexWriterConfig config;
    config.setRAMBufferSizeMB(32.0);
    config.setMaxBufferedDocs(1000);

    auto writer = std::make_unique<IndexWriter>(*dir, config);

    const auto& writerConfig = writer->getConfig();
    EXPECT_DOUBLE_EQ(32.0, writerConfig.getRAMBufferSizeMB());
    EXPECT_EQ(1000, writerConfig.getMaxBufferedDocs());
}

// ==================== Commit Tests ====================

TEST_F(IndexWriterTest, CommitOnOpenWriter) {
    IndexWriterConfig config;
    auto writer = std::make_unique<IndexWriter>(*dir, config);

    EXPECT_NO_THROW(writer->commit());
}

TEST_F(IndexWriterTest, MultipleCommits) {
    IndexWriterConfig config;
    auto writer = std::make_unique<IndexWriter>(*dir, config);

    writer->commit();
    writer->commit();
    writer->commit();

    // Should succeed without error
    EXPECT_TRUE(writer->isOpen());
}

// ==================== Flush Tests ====================

TEST_F(IndexWriterTest, FlushOnOpenWriter) {
    IndexWriterConfig config;
    auto writer = std::make_unique<IndexWriter>(*dir, config);

    EXPECT_NO_THROW(writer->flush());
}

// ==================== Rollback Tests ====================

TEST_F(IndexWriterTest, RollbackOnOpenWriter) {
    IndexWriterConfig config;
    auto writer = std::make_unique<IndexWriter>(*dir, config);

    EXPECT_NO_THROW(writer->rollback());
}

// ==================== Force Merge Tests ====================

TEST_F(IndexWriterTest, ForceMergeValidArgument) {
    IndexWriterConfig config;
    auto writer = std::make_unique<IndexWriter>(*dir, config);

    EXPECT_NO_THROW(writer->forceMerge(1));
    EXPECT_NO_THROW(writer->forceMerge(5));
}

TEST_F(IndexWriterTest, ForceMergeInvalidArgument) {
    IndexWriterConfig config;
    auto writer = std::make_unique<IndexWriter>(*dir, config);

    EXPECT_THROW(writer->forceMerge(0), std::invalid_argument);
    EXPECT_THROW(writer->forceMerge(-1), std::invalid_argument);
}

// ==================== Wait For Merges Tests ====================

TEST_F(IndexWriterTest, WaitForMerges) {
    IndexWriterConfig config;
    auto writer = std::make_unique<IndexWriter>(*dir, config);

    EXPECT_NO_THROW(writer->waitForMerges());
}

// ==================== Thread Safety Tests ====================

TEST_F(IndexWriterTest, ConcurrentAddDocument) {
    IndexWriterConfig config;
    auto writer = std::make_unique<IndexWriter>(*dir, config);

    const int numThreads = 10;
    const int opsPerThread = 100;
    std::vector<std::thread> threads;
    std::vector<int64_t> seqNos(numThreads * opsPerThread);

    for (int t = 0; t < numThreads; t++) {
        threads.emplace_back([&writer, &seqNos, t, opsPerThread]() {
            for (int i = 0; i < opsPerThread; i++) {
                Document doc;
                doc.add(std::make_unique<TextField>("body", "test", TextField::TYPE_STORED));
                seqNos[t * opsPerThread + i] = writer->addDocument(doc);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Check all sequence numbers are unique
    std::sort(seqNos.begin(), seqNos.end());
    for (size_t i = 1; i < seqNos.size(); i++) {
        EXPECT_NE(seqNos[i], seqNos[i-1]) << "Duplicate sequence number found";
    }
}

TEST_F(IndexWriterTest, ConcurrentCommit) {
    IndexWriterConfig config;
    auto writer = std::make_unique<IndexWriter>(*dir, config);

    const int numThreads = 10;
    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};

    for (int t = 0; t < numThreads; t++) {
        threads.emplace_back([&writer, &successCount]() {
            try {
                writer->commit();
                successCount++;
            } catch (...) {
                // Ignore exceptions
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // All commits should succeed (they're serialized)
    EXPECT_EQ(numThreads, successCount.load());
}

TEST_F(IndexWriterTest, ConcurrentCloseIsSafe) {
    IndexWriterConfig config;
    auto writer = std::make_unique<IndexWriter>(*dir, config);

    const int numThreads = 10;
    std::vector<std::thread> threads;

    for (int t = 0; t < numThreads; t++) {
        threads.emplace_back([&writer]() {
            try {
                writer->close();
            } catch (...) {
                // Ignore exceptions
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Writer should be closed
    EXPECT_FALSE(writer->isOpen());
}

// ==================== Destructor Tests ====================

TEST_F(IndexWriterTest, DestructorWithCommitOnClose) {
    IndexWriterConfig config;
    config.setCommitOnClose(true);

    {
        auto writer = std::make_unique<IndexWriter>(*dir, config);
        Document doc = Document();
        doc.add(std::make_unique<TextField>("body", "test", TextField::TYPE_STORED));
        writer->addDocument(doc);
        // Destructor should commit
    }

    // Should be able to open new writer
    EXPECT_NO_THROW(
        auto writer2 = std::make_unique<IndexWriter>(*dir, config)
    );
}

TEST_F(IndexWriterTest, DestructorWithoutCommitOnClose) {
    IndexWriterConfig config;
    config.setCommitOnClose(false);

    {
        auto writer = std::make_unique<IndexWriter>(*dir, config);
        Document doc = Document();
        doc.add(std::make_unique<TextField>("body", "test", TextField::TYPE_STORED));
        writer->addDocument(doc);
        // Destructor should not commit
    }

    // Should be able to open new writer
    EXPECT_NO_THROW(
        auto writer2 = std::make_unique<IndexWriter>(*dir, config)
    );
}

// ==================== Edge Cases ====================

TEST_F(IndexWriterTest, HighSequenceNumbers) {
    IndexWriterConfig config;
    auto writer = std::make_unique<IndexWriter>(*dir, config);

    // Generate many sequence numbers
    for (int i = 0; i < 10000; i++) {
        Document doc = Document();
        doc.add(std::make_unique<TextField>("body", "test", TextField::TYPE_STORED));
        writer->addDocument(doc);
    }

    EXPECT_GT(writer->getSequenceNumber(), 10000);
}

TEST_F(IndexWriterTest, ReopenAfterClose) {
    IndexWriterConfig config;

    auto writer1 = std::make_unique<IndexWriter>(*dir, config);
    Document doc = Document();
    doc.add(std::make_unique<TextField>("body", "test", TextField::TYPE_STORED));
    writer1->addDocument(doc);
    writer1->close();

    // Reopen
    auto writer2 = std::make_unique<IndexWriter>(*dir, config);
    EXPECT_TRUE(writer2->isOpen());
    EXPECT_EQ(1, writer2->getSequenceNumber());  // New writer starts at 1
}
