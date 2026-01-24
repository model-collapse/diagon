// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/store/Directory.h"
#include "diagon/store/IndexInput.h"
#include "diagon/store/IndexOutput.h"
#include "diagon/store/Lock.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>

namespace diagon::store {

/**
 * @brief Filesystem-based directory implementation using standard C++ I/O.
 *
 * Based on: org.apache.lucene.store.FSDirectory
 *
 * FSDirectory stores index files in a regular filesystem directory using
 * standard file I/O operations. Features:
 * - Buffered reads/writes for performance
 * - fsync for durability
 * - File locking to prevent concurrent writers
 * - Atomic rename for commit operations
 *
 * Performance characteristics:
 * - Good for general-purpose use
 * - OS page cache provides caching
 * - Suitable for most workloads
 *
 * For read-heavy workloads with large files, consider MMapDirectory instead.
 */
class FSDirectory : public Directory {
public:
    /**
     * @brief Opens or creates a directory at the specified path.
     * @param path Filesystem path
     * @return FSDirectory instance
     */
    static std::unique_ptr<FSDirectory> open(const std::filesystem::path& path);

    /**
     * @brief Constructs FSDirectory for a path.
     * @param path Filesystem path (must exist)
     */
    explicit FSDirectory(const std::filesystem::path& path);

    ~FSDirectory() override = default;

    // ==================== File Listing ====================

    std::vector<std::string> listAll() const override;

    // ==================== File Operations ====================

    void deleteFile(const std::string& name) override;
    int64_t fileLength(const std::string& name) const override;

    // ==================== Stream Creation ====================

    std::unique_ptr<IndexOutput> createOutput(const std::string& name,
                                              const IOContext& context) override;

    std::unique_ptr<IndexOutput> createTempOutput(const std::string& prefix,
                                                  const std::string& suffix,
                                                  const IOContext& context) override;

    std::unique_ptr<IndexInput> openInput(const std::string& name,
                                          const IOContext& context) const override;

    // ==================== Atomic Operations ====================

    void rename(const std::string& source, const std::string& dest) override;
    void sync(const std::vector<std::string>& names) override;
    void syncMetaData() override;

    // ==================== Locking ====================

    std::unique_ptr<Lock> obtainLock(const std::string& name) override;

    // ==================== Lifecycle ====================

    void close() override;

    // ==================== Utilities ====================

    std::optional<std::filesystem::path> getPath() const override { return directory_; }

    std::string toString() const override;

private:
    std::filesystem::path directory_;

    // Generate unique temp file ID
    std::string generateTempId() const;

    // Fsync operations (platform-specific)
    void fsyncFile(const std::filesystem::path& path);
    void fsyncDirectory(const std::filesystem::path& path);
};

/**
 * @brief File-based IndexInput implementation with buffering.
 *
 * Uses std::ifstream for reading with a buffer for efficiency.
 */
class FSIndexInput : public IndexInput {
public:
    /**
     * @brief Opens a file for reading.
     * @param path File path
     * @param bufferSize Buffer size (default 8KB)
     */
    explicit FSIndexInput(const std::filesystem::path& path, size_t bufferSize = 8192);

    ~FSIndexInput() override = default;

    // ==================== Reading ====================

    uint8_t readByte() override;
    void readBytes(uint8_t* buffer, size_t length) override;

    // ==================== Positioning ====================

    int64_t getFilePointer() const override;
    void seek(int64_t pos) override;
    int64_t length() const override;

    // ==================== Cloning ====================

    std::unique_ptr<IndexInput> clone() const override;
    std::unique_ptr<IndexInput> slice(const std::string& sliceDescription, int64_t offset,
                                      int64_t length) const override;

    std::string toString() const override;

    // Constructor for slices
    FSIndexInput(const std::filesystem::path& path, int64_t offset, int64_t length,
                 size_t bufferSize);

private:
    std::filesystem::path file_path_;
    mutable std::ifstream file_;
    int64_t file_length_;
    int64_t file_position_;

    // Buffering
    std::vector<uint8_t> buffer_;
    size_t buffer_position_;
    size_t buffer_length_;

    // Slice support
    int64_t slice_offset_;
    int64_t slice_length_;
    bool is_slice_;

    // Refill buffer from file
    void refillBuffer();
};

/**
 * @brief File-based IndexOutput implementation with buffering.
 *
 * Uses std::ofstream for writing with a buffer for efficiency.
 */
class FSIndexOutput : public IndexOutput {
public:
    /**
     * @brief Creates a file for writing.
     * @param path File path
     * @param bufferSize Buffer size (default 8KB)
     */
    explicit FSIndexOutput(const std::filesystem::path& path, size_t bufferSize = 8192);

    ~FSIndexOutput() override;

    // ==================== Writing ====================

    void writeByte(uint8_t b) override;
    void writeBytes(const uint8_t* buffer, size_t length) override;

    // ==================== Positioning ====================

    int64_t getFilePointer() const override;

    // ==================== Finalization ====================

    void close() override;

    std::string getName() const override { return file_path_.filename().string(); }

private:
    std::filesystem::path file_path_;
    std::ofstream file_;
    int64_t file_position_;

    // Buffering
    std::vector<uint8_t> buffer_;
    size_t buffer_position_;

    // Flush buffer to file
    void flushBuffer();
};

/**
 * @brief Simple file-based lock using lock files.
 *
 * Creates a lock file and uses file locking (flock/fcntl on POSIX).
 */
class FSLock : public Lock {
public:
    static std::unique_ptr<Lock> obtain(const std::filesystem::path& lockPath);

    explicit FSLock(const std::filesystem::path& lockPath);
    ~FSLock() override;

    void close() override;
    void ensureValid() override;

private:
    std::filesystem::path lock_path_;
    int fd_;  // File descriptor (POSIX)
    bool closed_;
};

}  // namespace diagon::store
