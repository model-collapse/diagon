// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/store/IndexInput.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace diagon::store {

/**
 * @brief Memory chunk descriptor for chunked memory mapping.
 *
 * Represents a single contiguous chunk of memory-mapped file data.
 * Files larger than the chunk size are split into multiple chunks.
 */
struct MMapChunk {
    uint8_t* data;    ///< Pointer to mapped memory (or MAP_FAILED)
    size_t length;    ///< Length of this chunk in bytes
    int fd;           ///< File descriptor (for cleanup context)

    /**
     * @brief Constructs an empty chunk.
     */
    MMapChunk() : data(nullptr), length(0), fd(-1) {}

    /**
     * @brief Constructs a chunk with data.
     */
    MMapChunk(uint8_t* d, size_t len, int file_desc)
        : data(d), length(len), fd(file_desc) {}
};

/**
 * @brief Memory-mapped IndexInput implementation with chunked mapping.
 *
 * Based on: org.apache.lucene.store.ByteBufferIndexInput (Lucene's base for MemorySegmentIndexInput)
 *
 * MMapIndexInput provides zero-copy reads from memory-mapped files using
 * chunked mapping to handle large files and avoid address space fragmentation.
 *
 * Architecture:
 * - Files are split into power-of-2 sized chunks (e.g., 16GB each)
 * - Each chunk is independently mapped via mmap() (POSIX) or MapViewOfFile (Windows)
 * - Chunks are stored in a shared_ptr array for RAII cleanup
 * - Clone and slice operations share the same chunk array (zero-copy)
 *
 * Chunk selection uses fast bit operations:
 * ```
 * chunk_index = position >> chunk_power  // Fast division
 * chunk_offset = position & chunk_mask   // Fast modulo
 * ```
 *
 * Memory management:
 * - shared_ptr with custom deleter ensures automatic cleanup
 * - Clones share chunk pointers but have independent positions
 * - Slices share chunk pointers with offset/length constraints
 *
 * Thread-safety:
 * - Read-only after construction (immutable file content)
 * - Multiple clones can read concurrently (shared mapping)
 * - Each clone has independent position (no shared state)
 *
 * Platform-specific subclasses:
 * - PosixMMapIndexInput: Linux/macOS via mmap()
 * - WindowsMMapIndexInput: Windows via CreateFileMapping()
 */
class MMapIndexInput : public IndexInput {
public:
    /**
     * @brief Virtual destructor.
     *
     * Cleanup is handled automatically via shared_ptr custom deleter.
     */
    ~MMapIndexInput() override = default;

    // ==================== Basic Reading ====================

    /**
     * @brief Reads a single byte from the current position.
     *
     * @return Byte value (0-255)
     * @throws EOFException if at end of file
     * @throws IOException on read error
     *
     * Implementation uses fast chunk selection via bit operations.
     */
    uint8_t readByte() override;

    /**
     * @brief Reads bytes into a buffer.
     *
     * Handles chunk boundaries automatically by splitting reads across chunks.
     *
     * @param buffer Destination buffer
     * @param length Number of bytes to read
     * @throws EOFException if not enough bytes available
     * @throws IOException on read error
     */
    void readBytes(uint8_t* buffer, size_t length) override;

    // ==================== Fast Direct-Access Methods ====================

    /**
     * @brief Non-virtual readVInt operating directly on mapped memory.
     *
     * Avoids per-byte virtual dispatch by reading from the raw mmap pointer.
     * Falls back to base class for cross-chunk reads (extremely rare).
     */
    int32_t readVInt() override;

    /**
     * @brief Non-virtual readVLong operating directly on mapped memory.
     */
    int64_t readVLong() override;

    /**
     * @brief Non-virtual readInt operating directly on mapped memory.
     */
    int32_t readInt() override;

    /**
     * @brief Non-virtual readLong operating directly on mapped memory.
     */
    int64_t readLong() override;

    // ==================== Positioning ====================

    /**
     * @brief Gets the current file pointer (position).
     *
     * @return Current position in bytes
     */
    int64_t getFilePointer() const override;

    /**
     * @brief Seeks to a specific position.
     *
     * @param pos Target position (0-indexed)
     * @throws IOException if pos is negative or beyond end of file
     */
    void seek(int64_t pos) override;

    /**
     * @brief Gets the total length of the file.
     *
     * @return File length in bytes
     */
    int64_t length() const override;

    // ==================== Cloning and Slicing ====================

    /**
     * @brief Creates an independent clone with its own position.
     *
     * Clones share the underlying memory mapping (zero-copy) but maintain
     * independent file pointers. This enables concurrent reads.
     *
     * @return Unique pointer to cloned IndexInput
     *
     * Note: Must be implemented by concrete subclass due to abstract base.
     */
    std::unique_ptr<IndexInput> clone() const override = 0;

    /**
     * @brief Creates a slice (sub-view) of this input.
     *
     * Slices share the underlying memory mapping and restrict access to
     * a contiguous sub-range [offset, offset+length).
     *
     * @param sliceDescription Descriptive name for the slice
     * @param offset Starting offset within this input
     * @param length Length of the slice
     * @return Unique pointer to sliced IndexInput
     * @throws IOException if offset+length exceeds file length
     *
     * Note: Must be implemented by concrete subclass due to abstract base.
     */
    std::unique_ptr<IndexInput> slice(const std::string& sliceDescription,
                                      int64_t offset,
                                      int64_t length) const override = 0;

protected:
    /**
     * @brief Constructs a MMapIndexInput by mapping a file.
     *
     * @param path File path to map
     * @param chunk_power Power-of-2 for chunk size (e.g., 34 = 16GB)
     * @param preload Whether to preload all pages (Phase 2 feature)
     * @throws IOException if file cannot be opened or mapped
     *
     * Protected constructor: Use factory methods from subclasses.
     */
    MMapIndexInput(const std::filesystem::path& path, int chunk_power, bool preload);

    /**
     * @brief Copy constructor for cloning.
     *
     * Shares chunk array but resets position to 0.
     *
     * @param other Source input to clone
     */
    MMapIndexInput(const MMapIndexInput& other);

    /**
     * @brief Platform-specific mapping implementation.
     *
     * Called by constructor to perform actual mmap()/MapViewOfFile() calls.
     * Must initialize chunks_ with mapped memory.
     *
     * @param fd File descriptor (already opened)
     * @param file_length Total file length in bytes
     * @throws IOException on mapping failure
     */
    virtual void mapChunks(int fd, int64_t file_length) = 0;

    /**
     * @brief Platform-specific unmapping implementation.
     *
     * Called by shared_ptr deleter to perform munmap()/UnmapViewOfFile().
     * Must unmap all chunks and close file descriptor.
     *
     * @param chunks Array of chunks to unmap
     * @param num_chunks Number of chunks in array
     * @param fd File descriptor to close
     */
    virtual void unmapChunks(MMapChunk* chunks, size_t num_chunks, int fd) = 0;

    // ==================== Member Variables ====================

    std::filesystem::path path_;          ///< File path (for error messages)
    int chunk_power_;                     ///< Power-of-2 for chunk size
    int64_t chunk_mask_;                  ///< Bitmask for chunk offset (chunk_size - 1)
    int64_t file_length_;                 ///< Total file length in bytes
    size_t num_chunks_;                   ///< Number of chunks
    std::shared_ptr<MMapChunk[]> chunks_; ///< Array of mapped chunks (shared)

    int64_t pos_;                         ///< Current file pointer
    bool is_slice_;                       ///< Whether this is a slice
    int64_t slice_offset_;                ///< Slice start offset (if is_slice_)
    int64_t slice_length_;                ///< Slice length (if is_slice_)

    // ==================== Fast Path Helpers ====================

    /**
     * @brief Returns raw pointer into current chunk + remaining bytes.
     *
     * Used by readVInt/readVLong to avoid per-byte virtual dispatch.
     * Returns nullptr if position is invalid or spans chunk boundary
     * with fewer bytes remaining than needed.
     *
     * @param needed Minimum bytes needed in contiguous memory
     * @param[out] ptr Raw pointer to current position
     * @param[out] remaining Bytes remaining in current chunk
     * @return true if ptr is valid and remaining >= needed
     */
    inline bool getDirectPointer(size_t needed, const uint8_t*& ptr, size_t& remaining) const {
        int64_t absolute_pos = is_slice_ ? (slice_offset_ + pos_) : pos_;
        int64_t max_pos = is_slice_ ? slice_length_ : file_length_;
        if (pos_ + static_cast<int64_t>(needed) > max_pos) return false;

        int chunk_idx = static_cast<int>(absolute_pos >> chunk_power_);
        size_t chunk_offset = static_cast<size_t>(absolute_pos & chunk_mask_);

        if (chunk_idx >= static_cast<int>(num_chunks_)) return false;

        auto& chunk = chunks_[chunk_idx];
        remaining = chunk.length - chunk_offset;
        if (remaining < needed) return false;

        ptr = chunk.data + chunk_offset;
        return true;
    }
};

}  // namespace diagon::store
