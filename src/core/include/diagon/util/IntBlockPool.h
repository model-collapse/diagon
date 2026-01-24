// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <deque>
#include <memory>

namespace diagon {
namespace util {

/**
 * IntBlockPool - Efficient int storage using large blocks
 *
 * Based on: org.apache.lucene.util.IntBlockPool
 *
 * Manages memory in 8K int blocks (32KB) for posting list storage.
 * Used for storing [docID, freq] pairs in in-memory posting lists.
 *
 * Memory Layout:
 *   [Block 0: 8K ints] → [Block 1: 8K ints] → ...
 *       ↑
 *     [docID0, freq0, docID1, freq1, ...]
 *
 * Posting List Format (Phase 2):
 *   - Absolute docIDs (not deltas) for simplicity
 *   - Delta encoding happens during flush to codec
 *   - Format: [docID_abs, freq, docID_abs, freq, ...]
 *
 * Features:
 * - Block-based allocation for efficiency
 * - Random access via offset
 * - std::deque ensures pointer stability
 * - Easy reset for reuse across segments
 *
 * Thread Safety: NOT thread-safe. Caller must synchronize.
 */
class IntBlockPool {
public:
    // 8K ints = 32KB blocks (matches ByteBlockPool block size)
    static constexpr int INT_BLOCK_SIZE = 8192;

    /**
     * Constructor
     */
    IntBlockPool();

    /**
     * Destructor - automatically frees all blocks
     */
    ~IntBlockPool() = default;

    // Disable copy/move (unique ownership of blocks)
    IntBlockPool(const IntBlockPool&) = delete;
    IntBlockPool& operator=(const IntBlockPool&) = delete;
    IntBlockPool(IntBlockPool&&) = delete;
    IntBlockPool& operator=(IntBlockPool&&) = delete;

    /**
     * Append int to pool
     * Returns offset where int was written
     *
     * @param value Int value to append
     * @return Absolute offset (in ints) where value was written
     */
    int append(int value);

    /**
     * Allocate space for N ints
     * Returns starting offset
     *
     * @param count Number of ints to allocate
     * @return Absolute offset (in ints) where allocation starts
     */
    int allocate(int count);

    /**
     * Write int at absolute offset
     *
     * @param offset Absolute offset (in ints)
     * @param value Value to write
     */
    void writeInt(int offset, int value);

    /**
     * Read int from absolute offset
     *
     * @param offset Absolute offset (in ints)
     * @return Int value
     */
    int readInt(int offset) const;

    /**
     * Get pointer to allocated space (for bulk writes)
     * Pointer valid until next allocate() or reset()
     *
     * @param count Number of ints needed
     * @return Pointer to allocated space
     */
    int* allocateSlice(int count);

    /**
     * Get current size (total ints written)
     */
    int size() const { return bufferUpto_ * INT_BLOCK_SIZE + intUpto_; }

    /**
     * Get bytes used (allocated memory)
     */
    int64_t bytesUsed() const {
        return static_cast<int64_t>(buffers_.size()) * INT_BLOCK_SIZE * sizeof(int);
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
    // Use std::deque for stable addresses
    std::deque<std::unique_ptr<int[]>> buffers_;

    int bufferUpto_{0};     // Current buffer index
    int* buffer_{nullptr};  // Pointer to current buffer
    int intUpto_{0};        // Offset within current buffer

    /**
     * Allocate next buffer
     */
    void nextBuffer();
};

}  // namespace util
}  // namespace diagon
