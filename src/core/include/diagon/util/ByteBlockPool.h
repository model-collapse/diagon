// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <deque>
#include <memory>

namespace diagon {
namespace util {

/**
 * ByteBlockPool - Efficient byte storage using large blocks
 *
 * Based on: org.apache.lucene.util.ByteBlockPool
 *
 * Manages memory in 32KB blocks for cache-friendly access.
 * Used for storing term bytes in in-memory posting lists.
 *
 * Memory Layout:
 *   [Block 0: 32KB] → [Block 1: 32KB] → [Block 2: 32KB] → ...
 *       ↑
 *     "term1\0term2\0term3\0..."
 *
 * Features:
 * - Block-based allocation avoids frequent malloc calls
 * - Sequential writes are cache-friendly
 * - std::deque ensures pointer stability (no reallocation invalidation)
 * - Easy reset for reuse across segments
 *
 * Thread Safety: NOT thread-safe. Caller must synchronize.
 */
class ByteBlockPool {
public:
    // 32KB blocks (Lucene's choice for cache-friendly access)
    static constexpr int BYTE_BLOCK_SIZE = 32768;

    /**
     * Constructor
     */
    ByteBlockPool();

    /**
     * Destructor - automatically frees all blocks
     */
    ~ByteBlockPool() = default;

    // Disable copy/move (unique ownership of blocks)
    ByteBlockPool(const ByteBlockPool&) = delete;
    ByteBlockPool& operator=(const ByteBlockPool&) = delete;
    ByteBlockPool(ByteBlockPool&&) = delete;
    ByteBlockPool& operator=(ByteBlockPool&&) = delete;

    /**
     * Append bytes to the pool
     * Returns offset where bytes were written
     *
     * @param bytes Bytes to append
     * @param length Number of bytes
     * @return Absolute offset in pool where bytes start
     */
    int64_t append(const uint8_t* bytes, int length);

    /**
     * Append a null-terminated string
     * Returns offset where string was written (includes null terminator)
     *
     * @param str String to append
     * @return Absolute offset in pool where string starts
     */
    int64_t append(const std::string& str);

    /**
     * Allocate space for N bytes
     * Returns pointer to allocated space
     * Pointer is valid until next allocate() or reset()
     *
     * @param size Number of bytes to allocate
     * @return Pointer to allocated space
     */
    uint8_t* allocate(int size);

    /**
     * Get byte at absolute offset
     *
     * @param offset Absolute offset in pool
     * @return Byte value
     */
    uint8_t getByte(int64_t offset) const;

    /**
     * Read bytes from pool
     *
     * @param offset Absolute offset in pool
     * @param dest Destination buffer
     * @param length Number of bytes to read
     */
    void readBytes(int64_t offset, uint8_t* dest, int length) const;

    /**
     * Read null-terminated string from pool
     *
     * @param offset Absolute offset where string starts
     * @return String (without null terminator)
     */
    std::string readString(int64_t offset) const;

    /**
     * Get current size (total bytes written)
     */
    int64_t size() const {
        return static_cast<int64_t>(bufferUpto_) * BYTE_BLOCK_SIZE + byteUpto_;
    }

    /**
     * Get bytes used (allocated memory)
     */
    int64_t bytesUsed() const {
        return static_cast<int64_t>(buffers_.size()) * BYTE_BLOCK_SIZE;
    }

    /**
     * Reset pool for reuse
     * Keeps allocated blocks but resets write position
     */
    void reset();

    /**
     * Free all memory
     * Releases all blocks
     */
    void clear();

private:
    // Use std::deque for stable addresses (no reallocation)
    std::deque<std::unique_ptr<uint8_t[]>> buffers_;

    int bufferUpto_{0};         // Current buffer index
    uint8_t* buffer_{nullptr};  // Pointer to current buffer
    int byteUpto_{0};          // Offset within current buffer

    /**
     * Allocate next buffer
     */
    void nextBuffer();
};

}  // namespace util
}  // namespace diagon
