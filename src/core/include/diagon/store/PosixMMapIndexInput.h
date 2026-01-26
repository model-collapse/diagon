// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/store/MMapIndexInput.h"
#include "diagon/store/IOContext.h"

#include <filesystem>

namespace diagon::store {

/**
 * @brief POSIX-specific memory-mapped IndexInput implementation.
 *
 * Uses POSIX mmap() and munmap() for memory mapping on Linux and macOS.
 *
 * Platform requirements:
 * - Linux: mmap() with MAP_SHARED
 * - macOS: mmap() with MAP_SHARED
 *
 * Memory mapping details:
 * - Protection: PROT_READ (read-only)
 * - Flags: MAP_SHARED (changes not written back to file)
 * - Address: nullptr (kernel chooses address)
 *
 * Phase 1 implementation:
 * - Basic mmap()/munmap() without optimization
 * - No madvise() hints (deferred to Phase 2)
 * - No preload support (deferred to Phase 2)
 *
 * Phase 2 additions:
 * - madvise() with MADV_SEQUENTIAL, MADV_RANDOM, MADV_NORMAL
 * - Preload support via MADV_WILLNEED
 * - IOContext-based read advice
 *
 * Error handling:
 * - ENOMEM: Address space exhausted (common on 32-bit)
 * - EINVAL: Invalid arguments (should not happen with validated inputs)
 * - EACCES: Permission denied (file not readable)
 *
 * Platform availability:
 * - Linux: All versions
 * - macOS: All versions
 * - BSD: Should work but untested
 * - Windows: Not supported (use WindowsMMapIndexInput)
 */
class PosixMMapIndexInput : public MMapIndexInput {
public:
    /**
     * @brief Constructs a POSIX memory-mapped IndexInput.
     *
     * Opens the file, maps it into memory using mmap(), and sets up
     * chunked access for efficient large file handling.
     *
     * @param path File path to map
     * @param chunk_power Power-of-2 for chunk size (e.g., 34 = 16GB)
     * @param preload Whether to preload all pages into physical memory
     * @param advice Read advice hint for OS optimization (SEQUENTIAL, RANDOM, NORMAL)
     * @throws IOException if file cannot be opened or mapped
     * @throws EOFException if file is empty (0 bytes)
     *
     * File descriptor is kept open until all clones/slices are destroyed
     * (managed by shared_ptr reference counting).
     */
    PosixMMapIndexInput(const std::filesystem::path& path,
                        int chunk_power,
                        bool preload,
                        IOContext::ReadAdvice advice = IOContext::ReadAdvice::NORMAL);

    /**
     * @brief Copy constructor for cloning.
     *
     * Used internally by clone() and slice(). Public to allow make_unique usage.
     *
     * @param other Source input to clone
     */
    PosixMMapIndexInput(const PosixMMapIndexInput& other);

    /**
     * @brief Destructor.
     *
     * Cleanup is automatic via shared_ptr custom deleter calling unmapChunks().
     */
    ~PosixMMapIndexInput() override = default;

    // ==================== Cloning and Slicing ====================

    /**
     * @brief Creates an independent clone with its own position.
     *
     * @return Unique pointer to cloned IndexInput
     */
    std::unique_ptr<IndexInput> clone() const override;

    /**
     * @brief Creates a slice (sub-view) of this input.
     *
     * @param sliceDescription Descriptive name for the slice
     * @param offset Starting offset within this input
     * @param length Length of the slice
     * @return Unique pointer to sliced IndexInput
     * @throws IOException if offset+length exceeds file length
     */
    std::unique_ptr<IndexInput> slice(const std::string& sliceDescription,
                                      int64_t offset,
                                      int64_t length) const override;

protected:
    /**
     * @brief Maps file chunks using POSIX mmap().
     *
     * Called by constructor to perform actual memory mapping.
     * Creates one mmap() call per chunk for chunked access.
     *
     * Algorithm:
     * 1. Calculate number of chunks: ceil(file_length / chunk_size)
     * 2. For each chunk:
     *    a. Calculate chunk offset and size
     *    b. Call mmap(nullptr, chunk_size, PROT_READ, MAP_SHARED, fd, offset)
     *    c. Verify mapping succeeded (data != MAP_FAILED)
     * 3. Store chunks in chunks_ array
     *
     * @param fd File descriptor (already opened with O_RDONLY)
     * @param file_length Total file length in bytes
     * @throws IOException on mmap() failure with platform-specific error message
     *
     * Error messages provide actionable guidance:
     * - ENOMEM: "Check 'ulimit -v' and 'sysctl vm.max_map_count'"
     * - EACCES: "File not readable or not a regular file"
     * - EINVAL: "Invalid mmap arguments (internal error)"
     */
    void mapChunks(int fd, int64_t file_length) override;

    /**
     * @brief Unmaps chunks using POSIX munmap().
     *
     * Called by shared_ptr custom deleter when last reference is destroyed.
     * Must unmap all chunks and close the file descriptor.
     *
     * Algorithm:
     * 1. For each chunk:
     *    a. If data != nullptr and data != MAP_FAILED:
     *       - Call munmap(data, length)
     *       - Ignore errors (best-effort cleanup)
     * 2. Close file descriptor with ::close(fd)
     * 3. Delete chunk array
     *
     * @param chunks Array of chunks to unmap
     * @param num_chunks Number of chunks in array
     * @param fd File descriptor to close
     *
     * Note: Errors during unmapping are logged but not propagated
     * (destructors should not throw).
     */
    void unmapChunks(MMapChunk* chunks, size_t num_chunks, int fd) override;

private:
    /**
     * @brief Applies read advice hints to all mapped chunks.
     *
     * Calls posix_madvise() with the appropriate hint:
     * - SEQUENTIAL → POSIX_MADV_SEQUENTIAL (aggressive read-ahead)
     * - RANDOM → POSIX_MADV_RANDOM (disable read-ahead)
     * - NORMAL → POSIX_MADV_NORMAL (default behavior)
     *
     * @param advice Read advice to apply
     *
     * Note: Errors are logged but not thrown (best-effort optimization).
     */
    void applyReadAdvice(IOContext::ReadAdvice advice);

    /**
     * @brief Preloads all mapped pages into physical memory.
     *
     * Calls posix_madvise() with POSIX_MADV_WILLNEED on all chunks,
     * forcing the OS to load pages into RAM immediately. This trades
     * slower open time for faster first access (no page faults).
     *
     * Note: Errors are logged but not thrown (best-effort optimization).
     */
    void preloadPages();
};

}  // namespace diagon::store
