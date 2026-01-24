// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/store/IOContext.h"
#include "diagon/store/IndexInput.h"
#include "diagon/store/IndexOutput.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace diagon::store {

// Forward declarations
class Lock;

/**
 * @brief Abstract interface for storing index files.
 *
 * Based on: org.apache.lucene.store.Directory
 *
 * Directory provides filesystem-independent storage for index files:
 * - File operations: create, delete, rename, list
 * - Stream access: IndexInput for reading, IndexOutput for writing
 * - Locking: Prevent multiple writers
 * - Durability: sync() ensures data is persisted
 *
 * Implementations:
 * - FSDirectory: Filesystem storage
 * - MMapDirectory: Memory-mapped files
 * - ByteBuffersDirectory: In-memory (testing)
 *
 * Thread-safety:
 * - Concurrent reads are safe
 * - Writes must be externally synchronized (use Lock)
 * - Do NOT synchronize on Directory instance (deadlock risk)
 *
 * Usage:
 * ```cpp
 * auto dir = FSDirectory::open("/path/to/index");
 * auto output = dir->createOutput("data.bin", IOContext::DEFAULT);
 * output->writeInt(42);
 * output->close();
 * dir->sync({"data.bin"});
 * ```
 */
class Directory {
public:
    virtual ~Directory() = default;

    // ==================== File Listing ====================

    /**
     * @brief Lists all files in the directory.
     * @return Vector of filenames (sorted)
     * @throws IOException on I/O error
     */
    virtual std::vector<std::string> listAll() const = 0;

    // ==================== File Operations ====================

    /**
     * @brief Deletes a file.
     * @param name Filename to delete
     * @throws FileNotFoundException if file doesn't exist
     * @throws IOException on I/O error
     */
    virtual void deleteFile(const std::string& name) = 0;

    /**
     * @brief Returns the byte length of a file.
     * @param name Filename
     * @return File size in bytes
     * @throws FileNotFoundException if file doesn't exist
     * @throws IOException on I/O error
     */
    virtual int64_t fileLength(const std::string& name) const = 0;

    // ==================== Stream Creation ====================

    /**
     * @brief Creates an output stream for a new file.
     *
     * The file must not already exist.
     *
     * @param name Filename to create
     * @param context I/O context hints
     * @return IndexOutput for writing
     * @throws FileAlreadyExistsException if file exists
     * @throws IOException on I/O error
     */
    virtual std::unique_ptr<IndexOutput> createOutput(const std::string& name,
                                                      const IOContext& context) = 0;

    /**
     * @brief Creates a temporary output file.
     *
     * Filename will be: prefix + "_" + unique_id + suffix + ".tmp"
     *
     * @param prefix Filename prefix
     * @param suffix Filename suffix
     * @param context I/O context hints
     * @return IndexOutput for writing
     * @throws IOException on I/O error
     */
    virtual std::unique_ptr<IndexOutput> createTempOutput(const std::string& prefix,
                                                          const std::string& suffix,
                                                          const IOContext& context) = 0;

    /**
     * @brief Opens an input stream for reading.
     * @param name Filename to read
     * @param context I/O context hints
     * @return IndexInput for reading
     * @throws FileNotFoundException if file doesn't exist
     * @throws IOException on I/O error
     */
    virtual std::unique_ptr<IndexInput> openInput(const std::string& name,
                                                  const IOContext& context) const = 0;

    // ==================== Atomic Operations ====================

    /**
     * @brief Atomically renames a file.
     *
     * dest must not exist. This operation is used by IndexWriter
     * to atomically publish commits.
     *
     * @param source Source filename
     * @param dest Destination filename (must not exist)
     * @throws IOException on I/O error
     */
    virtual void rename(const std::string& source, const std::string& dest) = 0;

    /**
     * @brief Syncs files to stable storage.
     *
     * Ensures all writes are durable (fsync).
     *
     * @param names Files to sync
     * @throws IOException on I/O error
     */
    virtual void sync(const std::vector<std::string>& names) = 0;

    /**
     * @brief Syncs directory metadata to stable storage.
     *
     * Ensures directory operations (rename, create) are durable.
     *
     * @throws IOException on I/O error
     */
    virtual void syncMetaData() = 0;

    // ==================== Locking ====================

    /**
     * @brief Obtains a lock to prevent concurrent writers.
     *
     * Typically used with name "write.lock" to ensure single writer.
     *
     * @param name Lock name
     * @return Lock object, or nullptr if cannot obtain
     * @throws LockObtainFailedException if lock held by another process
     * @throws IOException on I/O error
     */
    virtual std::unique_ptr<Lock> obtainLock(const std::string& name) = 0;

    // ==================== Lifecycle ====================

    /**
     * @brief Closes the directory.
     *
     * After close(), no operations are allowed.
     *
     * @throws IOException on I/O error
     */
    virtual void close() = 0;

    /**
     * @brief Checks if directory is closed.
     * @return true if closed
     */
    [[nodiscard]] bool isClosed() const { return closed_.load(std::memory_order_relaxed); }

    // ==================== Utilities ====================

    /**
     * @brief Returns the filesystem path (if available).
     * @return Path, or nullopt for non-filesystem directories
     */
    [[nodiscard]] virtual std::optional<std::filesystem::path> getPath() const {
        return std::nullopt;
    }

    /**
     * @brief Returns a string description for debugging.
     * @return Directory description
     */
    [[nodiscard]] virtual std::string toString() const { return "Directory"; }

protected:
    /**
     * @brief Ensures the directory is open.
     * @throws AlreadyClosedException if closed
     */
    void ensureOpen() const;

    /** Closed flag (atomic for thread-safety) */
    std::atomic<bool> closed_{false};
};

}  // namespace diagon::store
