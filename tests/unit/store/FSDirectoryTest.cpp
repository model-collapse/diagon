// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/store/FSDirectory.h"

#include "diagon/util/Exceptions.h"

#include <gtest/gtest.h>

#include <filesystem>

using namespace diagon;
using namespace diagon::store;

class FSDirectoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir = std::filesystem::temp_directory_path() / "diagon_test_fsdir";
        std::filesystem::create_directories(test_dir);
    }

    void TearDown() override { std::filesystem::remove_all(test_dir); }

    std::filesystem::path test_dir;
};

TEST_F(FSDirectoryTest, Open) {
    auto dir = FSDirectory::open(test_dir);
    ASSERT_NE(nullptr, dir);
    EXPECT_FALSE(dir->isClosed());
}

TEST_F(FSDirectoryTest, OpenCreatesDirectory) {
    auto new_dir = test_dir / "subdir";
    EXPECT_FALSE(std::filesystem::exists(new_dir));

    auto dir = FSDirectory::open(new_dir);
    EXPECT_TRUE(std::filesystem::exists(new_dir));
}

TEST_F(FSDirectoryTest, CreateOutput) {
    auto dir = FSDirectory::open(test_dir);

    auto output = dir->createOutput("test.bin", IOContext::DEFAULT);
    ASSERT_NE(nullptr, output);

    output->writeInt(42);
    output->close();

    // Verify file exists
    EXPECT_TRUE(std::filesystem::exists(test_dir / "test.bin"));
}

TEST_F(FSDirectoryTest, CreateOutputFileAlreadyExists) {
    auto dir = FSDirectory::open(test_dir);

    auto output1 = dir->createOutput("test.bin", IOContext::DEFAULT);
    output1->close();

    // Should throw FileAlreadyExistsException
    EXPECT_THROW(dir->createOutput("test.bin", IOContext::DEFAULT), FileAlreadyExistsException);
}

TEST_F(FSDirectoryTest, OpenInput) {
    auto dir = FSDirectory::open(test_dir);

    // Create file first
    auto output = dir->createOutput("test.bin", IOContext::DEFAULT);
    output->writeInt(42);
    output->close();

    // Open for reading
    auto input = dir->openInput("test.bin", IOContext::DEFAULT);
    ASSERT_NE(nullptr, input);
    EXPECT_EQ(42, input->readInt());
}

TEST_F(FSDirectoryTest, OpenInputFileNotFound) {
    auto dir = FSDirectory::open(test_dir);

    EXPECT_THROW(dir->openInput("nonexistent.bin", IOContext::DEFAULT), FileNotFoundException);
}

TEST_F(FSDirectoryTest, DeleteFile) {
    auto dir = FSDirectory::open(test_dir);

    // Create file
    auto output = dir->createOutput("test.bin", IOContext::DEFAULT);
    output->close();
    EXPECT_TRUE(std::filesystem::exists(test_dir / "test.bin"));

    // Delete it
    dir->deleteFile("test.bin");
    EXPECT_FALSE(std::filesystem::exists(test_dir / "test.bin"));
}

TEST_F(FSDirectoryTest, DeleteFileNotFound) {
    auto dir = FSDirectory::open(test_dir);

    EXPECT_THROW(dir->deleteFile("nonexistent.bin"), FileNotFoundException);
}

TEST_F(FSDirectoryTest, FileLength) {
    auto dir = FSDirectory::open(test_dir);

    auto output = dir->createOutput("test.bin", IOContext::DEFAULT);
    for (int i = 0; i < 100; i++) {
        output->writeByte(static_cast<uint8_t>(i));
    }
    output->close();

    EXPECT_EQ(100, dir->fileLength("test.bin"));
}

TEST_F(FSDirectoryTest, FileLengthNotFound) {
    auto dir = FSDirectory::open(test_dir);

    EXPECT_THROW(dir->fileLength("nonexistent.bin"), FileNotFoundException);
}

TEST_F(FSDirectoryTest, ListAll) {
    auto dir = FSDirectory::open(test_dir);

    // Initially empty
    auto files = dir->listAll();
    EXPECT_EQ(0, files.size());

    // Create some files
    dir->createOutput("file1.bin", IOContext::DEFAULT)->close();
    dir->createOutput("file2.bin", IOContext::DEFAULT)->close();
    dir->createOutput("file3.bin", IOContext::DEFAULT)->close();

    files = dir->listAll();
    EXPECT_EQ(3, files.size());

    // Should be sorted
    EXPECT_EQ("file1.bin", files[0]);
    EXPECT_EQ("file2.bin", files[1]);
    EXPECT_EQ("file3.bin", files[2]);
}

TEST_F(FSDirectoryTest, Rename) {
    auto dir = FSDirectory::open(test_dir);

    auto output = dir->createOutput("old.bin", IOContext::DEFAULT);
    output->writeInt(42);
    output->close();

    dir->rename("old.bin", "new.bin");

    EXPECT_FALSE(std::filesystem::exists(test_dir / "old.bin"));
    EXPECT_TRUE(std::filesystem::exists(test_dir / "new.bin"));

    // Verify content preserved
    auto input = dir->openInput("new.bin", IOContext::DEFAULT);
    EXPECT_EQ(42, input->readInt());
}

TEST_F(FSDirectoryTest, CreateTempOutput) {
    auto dir = FSDirectory::open(test_dir);

    auto output1 = dir->createTempOutput("prefix", "suffix", IOContext::DEFAULT);
    auto output2 = dir->createTempOutput("prefix", "suffix", IOContext::DEFAULT);

    // Names should be unique
    EXPECT_NE(output1->getName(), output2->getName());

    // Names should contain prefix, suffix, and .tmp
    EXPECT_NE(std::string::npos, output1->getName().find("prefix"));
    EXPECT_NE(std::string::npos, output1->getName().find("suffix"));
    EXPECT_NE(std::string::npos, output1->getName().find(".tmp"));
}

TEST_F(FSDirectoryTest, Sync) {
    auto dir = FSDirectory::open(test_dir);

    auto output = dir->createOutput("test.bin", IOContext::DEFAULT);
    output->writeInt(42);
    output->close();

    // Sync should not throw
    EXPECT_NO_THROW(dir->sync({"test.bin"}));
}

TEST_F(FSDirectoryTest, SyncMetaData) {
    auto dir = FSDirectory::open(test_dir);

    // SyncMetaData should not throw
    EXPECT_NO_THROW(dir->syncMetaData());
}

TEST_F(FSDirectoryTest, ObtainLock) {
    auto dir = FSDirectory::open(test_dir);

    auto lock = dir->obtainLock("write.lock");
    ASSERT_NE(nullptr, lock);

    lock->ensureValid();
    lock->close();
}

TEST_F(FSDirectoryTest, LockExclusive) {
    auto dir = FSDirectory::open(test_dir);

    auto lock1 = dir->obtainLock("write.lock");
    ASSERT_NE(nullptr, lock1);

    // Second lock should fail
    EXPECT_THROW(dir->obtainLock("write.lock"), LockObtainFailedException);

    // Release first lock
    lock1->close();

    // Now should succeed
    auto lock2 = dir->obtainLock("write.lock");
    ASSERT_NE(nullptr, lock2);
}

TEST_F(FSDirectoryTest, Close) {
    auto dir = FSDirectory::open(test_dir);
    EXPECT_FALSE(dir->isClosed());

    dir->close();
    EXPECT_TRUE(dir->isClosed());

    // Operations after close should throw
    EXPECT_THROW(dir->listAll(), AlreadyClosedException);
}

TEST_F(FSDirectoryTest, GetPath) {
    auto dir = FSDirectory::open(test_dir);

    auto path = dir->getPath();
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(test_dir, path.value());
}

TEST_F(FSDirectoryTest, ToString) {
    auto dir = FSDirectory::open(test_dir);

    std::string str = dir->toString();
    EXPECT_NE(std::string::npos, str.find("FSDirectory"));
    EXPECT_NE(std::string::npos, str.find(test_dir.string()));
}

TEST_F(FSDirectoryTest, ConcurrentReads) {
    auto dir = FSDirectory::open(test_dir);

    // Create file with sequential data
    auto output = dir->createOutput("test.bin", IOContext::DEFAULT);
    for (int i = 0; i < 1000; i++) {
        output->writeInt(i);
    }
    output->close();

    // Open multiple independent readers
    auto input1 = dir->openInput("test.bin", IOContext::DEFAULT);
    auto input2 = dir->openInput("test.bin", IOContext::DEFAULT);

    // Read from different positions
    input1->seek(100 * 4);  // Position 100
    input2->seek(500 * 4);  // Position 500

    EXPECT_EQ(100, input1->readInt());
    EXPECT_EQ(500, input2->readInt());

    // Positions should be independent
    EXPECT_EQ(101, input1->readInt());
    EXPECT_EQ(501, input2->readInt());
}

TEST_F(FSDirectoryTest, LargeFileOperations) {
    auto dir = FSDirectory::open(test_dir);

    // Write large file (larger than buffer)
    auto output = dir->createOutput("large.bin", IOContext::DEFAULT);
    for (int i = 0; i < 100000; i++) {
        output->writeInt(i);
    }
    output->close();

    // Verify size
    EXPECT_EQ(100000 * 4, dir->fileLength("large.bin"));

    // Read and verify
    auto input = dir->openInput("large.bin", IOContext::DEFAULT);
    for (int i = 0; i < 100000; i++) {
        EXPECT_EQ(i, input->readInt());
    }
}
