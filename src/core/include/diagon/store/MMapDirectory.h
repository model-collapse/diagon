// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/store/FSDirectory.h"
#include "diagon/store/IndexInput.h"
#include "diagon/store/IOContext.h"

#include <filesystem>
#include <memory>

namespace diagon::store {

/**
 * @brief Memory-mapped file directory implementation for efficient read access.
 *
 * Based on: org.apache.lucene.store.MMapDirectory
 *
 * MMapDirectory uses memory-mapped files for zero-copy reading, providing
 * significant performance benefits for read-heavy workloads:
 *
 * Performance characteristics:
 * - Zero-copy reads: Direct memory access without buffering
 * - OS-managed paging: Automatic caching in page cache
 * - Random access: Efficient seeks without system calls
 * - Clone efficiency: Shared memory mapping across clones
 *
 * Advantages over FSDirectory:
 * - Sequential reads: ~10-20% faster (fewer system calls)
 * - Random reads: ~2-3x faster (zero-copy, OS page cache)
 * - Clone operations: ~100x faster (shared memory)
 *
 * Technical details:
 * - Uses chunked mapping (16GB chunks on 64-bit, 256MB on 32-bit)
 * - Power-of-2 chunk sizes enable fast bit-mask arithmetic
 * - RAII resource management via shared_ptr
 * - Platform-specific: POSIX mmap() on Linux/macOS, CreateFileMapping() on Windows
 *
 * Usage:
 * ```cpp
 * auto dir = MMapDirectory::open("/path/to/index");
 * auto input = dir->openInput("data.bin", IOContext::READ);
 * // Direct memory-mapped access
 * ```
 *
 * Limitations:
 * - Read-only: Use FSDirectory for writing
 * - Address space: May fail on 32-bit systems with large files
 * - Platform-dependent: Requires OS support for mmap
 *
 * For mixed workloads or small files, FSDirectory may be sufficient.
 */
class MMapDirectory : public FSDirectory {
public:
    /**
     * @brief Default chunk size power for 64-bit systems (16GB = 2^34).
     * On 32-bit systems, uses 28 (256MB) to avoid address space exhaustion.
     */
    static constexpr int DEFAULT_CHUNK_POWER_64 = 34;  // 16GB
    static constexpr int DEFAULT_CHUNK_POWER_32 = 28;  // 256MB

    /**
     * @brief Opens a MMapDirectory at the specified path.
     *
     * @param path Filesystem path to the index directory
     * @return Unique pointer to MMapDirectory
     * @throws IOException if path doesn't exist or isn't a directory
     *
     * Uses default chunk size based on system architecture (64-bit vs 32-bit).
     */
    static std::unique_ptr<MMapDirectory> open(const std::filesystem::path& path);

    /**
     * @brief Opens a MMapDirectory with custom chunk size.
     *
     * @param path Filesystem path to the index directory
     * @param chunk_power Power-of-2 for chunk size (e.g., 30 = 1GB chunks)
     * @return Unique pointer to MMapDirectory
     * @throws IOException if path doesn't exist or isn't a directory
     * @throws IllegalArgumentException if chunk_power is invalid
     *
     * Chunk size must be between 20 (1MB) and 40 (1TB).
     */
    static std::unique_ptr<MMapDirectory> open(const std::filesystem::path& path, int chunk_power);

    /**
     * @brief Constructs a MMapDirectory with default chunk size.
     *
     * @param path Filesystem path (must exist and be a directory)
     */
    explicit MMapDirectory(const std::filesystem::path& path);

    /**
     * @brief Constructs a MMapDirectory with custom chunk size.
     *
     * @param path Filesystem path (must exist and be a directory)
     * @param chunk_power Power-of-2 for chunk size
     * @throws IllegalArgumentException if chunk_power is invalid (not in [20, 40])
     */
    MMapDirectory(const std::filesystem::path& path, int chunk_power);

    /**
     * @brief Destructor.
     */
    ~MMapDirectory() override = default;

    // ==================== Stream Creation ====================

    /**
     * @brief Opens a memory-mapped IndexInput for reading.
     *
     * @param name Filename within the directory
     * @param context I/O context hints (currently unused in Phase 1)
     * @return Unique pointer to memory-mapped IndexInput
     * @throws IOException if file doesn't exist or cannot be mapped
     *
     * The returned IndexInput uses memory-mapped access for zero-copy reads.
     * Multiple clones can share the same underlying memory mapping.
     */
    std::unique_ptr<IndexInput> openInput(const std::string& name,
                                          const IOContext& context) const override;

    // ==================== Configuration ====================

    /**
     * @brief Gets the chunk size power (e.g., 34 for 16GB chunks).
     *
     * @return Chunk power value
     */
    int getChunkPower() const { return chunk_power_; }

    /**
     * @brief Gets the actual chunk size in bytes (e.g., 17179869184 for 16GB).
     *
     * @return Chunk size in bytes
     */
    int64_t getChunkSize() const { return 1LL << chunk_power_; }

    /**
     * @brief Enables or disables preloading (Phase 2 feature).
     *
     * When enabled, all mapped pages are immediately loaded into physical
     * memory (via madvise MADV_WILLNEED). This trades slower open time
     * for faster first access.
     *
     * @param preload True to enable preload, false to disable (default)
     *
     * Note: Currently a no-op in Phase 1. Will be implemented in Phase 2.
     */
    void setPreload(bool preload) { preload_ = preload; }

    /**
     * @brief Checks if preload is enabled.
     *
     * @return True if preload is enabled
     */
    bool isPreload() const { return preload_; }

    /**
     * @brief Enables or disables graceful fallback to FSDirectory on mmap failure.
     *
     * When enabled, if memory mapping fails (e.g., ENOMEM on 32-bit systems,
     * platform not supported), openInput() will automatically fall back to
     * buffered I/O via FSDirectory. A warning will be logged to stderr.
     *
     * When disabled (default), mmap failures throw IOException immediately.
     *
     * @param use_fallback True to enable fallback (default: false)
     *
     * Use cases for fallback:
     * - 32-bit systems where address space may be limited
     * - Testing environments with restrictive ulimits
     * - Platforms with partial mmap support
     *
     * Note: Fallback still throws exceptions for file-not-found errors.
     * Only mmap-specific failures trigger fallback.
     */
    void setUseFallback(bool use_fallback) { use_fallback_ = use_fallback; }

    /**
     * @brief Checks if fallback to FSDirectory is enabled.
     *
     * @return True if fallback is enabled
     */
    bool isUseFallback() const { return use_fallback_; }

    // ==================== Utilities ====================

    /**
     * @brief Returns string representation.
     *
     * @return String like "MMapDirectory@/path/to/index (chunk=16GB)"
     */
    std::string toString() const override;

private:
    int chunk_power_;    ///< Power-of-2 for chunk size (e.g., 34 = 16GB)
    bool preload_;       ///< Whether to preload mapped pages (Phase 2)
    bool use_fallback_;  ///< Whether to fall back to FSDirectory on mmap failure

    /**
     * @brief Gets default chunk power based on system architecture.
     *
     * @return DEFAULT_CHUNK_POWER_64 on 64-bit, DEFAULT_CHUNK_POWER_32 on 32-bit
     */
    static int getDefaultChunkPower();

    /**
     * @brief Validates chunk power is in valid range [20, 40].
     *
     * @param chunk_power Power to validate
     * @throws IllegalArgumentException if out of range
     */
    static void validateChunkPower(int chunk_power);
};

}  // namespace diagon::store
