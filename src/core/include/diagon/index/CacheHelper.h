// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <atomic>
#include <functional>
#include <memory>

namespace diagon {
namespace index {

/**
 * CacheKey - Unique identifier for cached data
 *
 * Based on: org.apache.lucene.index.IndexReader.CacheKey
 *
 * CacheKey is a lightweight object that uniquely identifies a reader instance.
 * It's used as a key in external caches (e.g., FieldCache, QueryCache).
 *
 * Key properties:
 * - Unique per reader instance
 * - Comparable by identity (pointer comparison)
 * - Invalidated when reader changes
 *
 * Usage:
 *   CacheHelper* helper = reader->getReaderCacheHelper();
 *   if (helper) {
 *       CacheKey* key = helper->getKey();
 *       // Use key as map key for caching
 *   }
 */
class CacheKey {
public:
    CacheKey() = default;

    // Non-copyable, non-movable (identity-based)
    CacheKey(const CacheKey&) = delete;
    CacheKey& operator=(const CacheKey&) = delete;
    CacheKey(CacheKey&&) = delete;
    CacheKey& operator=(CacheKey&&) = delete;

    virtual ~CacheKey() = default;

    /**
     * Compare cache keys by identity (pointer comparison)
     */
    bool operator==(const CacheKey& other) const { return this == &other; }

    bool operator!=(const CacheKey& other) const { return this != &other; }

    /**
     * Hash code for use in hash maps
     */
    size_t hashCode() const { return reinterpret_cast<size_t>(this); }
};

/**
 * CacheHelper - Provides cache invalidation support for IndexReader
 *
 * Based on: org.apache.lucene.index.IndexReader.CacheHelper
 *
 * CacheHelper provides:
 * 1. A CacheKey for identifying the reader
 * 2. A mechanism to detect when cached data should be invalidated
 *
 * Two types of CacheHelper:
 * 1. Core Cache Helper (LeafReader::getCoreCacheHelper()):
 *    - Invalidated only when segment is replaced
 *    - Safe to cache term dictionaries, doc values, etc.
 *    - Never invalidated by deletions
 *
 * 2. Reader Cache Helper (IndexReader::getReaderCacheHelper()):
 *    - Invalidated on any change (including deletions)
 *    - Safe to cache document counts, statistics
 *    - Invalidated when reader is reopened
 *
 * Usage:
 *   CacheHelper* helper = reader->getReaderCacheHelper();
 *   if (!helper) {
 *       // Reader doesn't support caching
 *       return;
 *   }
 *
 *   CacheKey* key = helper->getKey();
 *   if (cache.contains(key)) {
 *       return cache.get(key);
 *   }
 *
 *   auto result = computeExpensiveValue(reader);
 *   cache.put(key, result);
 *   return result;
 *
 * Thread Safety:
 * - CacheHelper and CacheKey are thread-safe
 * - Multiple threads can safely access the same helper/key
 */
class CacheHelper {
public:
    /**
     * Constructor
     *
     * Creates a new cache helper with a unique cache key.
     */
    CacheHelper()
        : key_(std::make_shared<CacheKey>()) {}

    virtual ~CacheHelper() = default;

    /**
     * Get the cache key for this reader
     *
     * The key is stable across the lifetime of the reader.
     * Different readers (even for the same data) have different keys.
     *
     * @return Pointer to the cache key (never null)
     */
    CacheKey* getKey() const { return key_.get(); }

    /**
     * Add a close listener
     *
     * The listener is called when the reader is closed.
     * Use this to remove entries from external caches.
     *
     * Phase 5: Not implemented yet (deferred to future)
     *
     * @param listener Callback to invoke on close
     */
    virtual void addCloseListener(std::function<void(CacheKey*)> listener) {
        // TODO: Implement in future phase
        (void)listener;  // Suppress unused parameter warning
    }

private:
    // Shared ownership of cache key
    // The key remains valid as long as the helper exists
    std::shared_ptr<CacheKey> key_;
};

}  // namespace index
}  // namespace diagon
