// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <filesystem>
#include <memory>

namespace diagon::store {

/**
 * @brief Abstract interface for locking to prevent concurrent access.
 *
 * Based on: org.apache.lucene.store.Lock
 *
 * Locks are used to ensure only one IndexWriter can modify an index
 * at a time. The typical usage is:
 *
 * ```cpp
 * auto lock = directory->obtainLock("write.lock");
 * // ... perform writes ...
 * lock->close();  // or let it go out of scope
 * ```
 *
 * Lock implementations should be:
 * - Process-safe (prevent multiple processes)
 * - Thread-safe (prevent multiple threads)
 * - Automatic cleanup on process crash (where possible)
 *
 * On POSIX systems, file locks (flock/fcntl) or lock files can be used.
 * On Windows, CreateFile with exclusive access works.
 */
class Lock {
public:
    virtual ~Lock() = default;

    /**
     * @brief Releases the lock.
     *
     * After close(), the lock is no longer held.
     */
    virtual void close() = 0;

    /**
     * @brief Ensures the lock is still held.
     * @throws LockObtainFailedException if lock was lost
     */
    virtual void ensureValid() = 0;

protected:
    Lock() = default;
};

} // namespace diagon::store
