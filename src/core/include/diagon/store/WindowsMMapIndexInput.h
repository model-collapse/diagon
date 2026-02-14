// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/store/IOContext.h"
#include "diagon/store/MMapIndexInput.h"

#include <filesystem>

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#endif

namespace diagon::store {

#ifdef _WIN32

/**
 * @brief Windows-specific memory-mapped IndexInput implementation.
 *
 * Uses Windows CreateFileMapping() and MapViewOfFile() for memory mapping.
 *
 * Platform requirements:
 * - Windows Vista or later (requires 64-bit file support)
 * - Sufficient virtual address space (typically not an issue on 64-bit Windows)
 *
 * Memory mapping details:
 * - Protection: PAGE_READONLY (read-only access)
 * - Mapping flags: FILE_MAP_READ
 * - File access: GENERIC_READ
 * - Share mode: FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE
 *
 * Implementation notes:
 * - Each chunk requires a separate MapViewOfFile() call
 * - File handle kept open until all mappings are released
 * - Handles managed via RAII with shared_ptr custom deleter
 *
 * Read advice hints:
 * - Windows doesn't have direct equivalent to POSIX madvise()
 * - Uses FILE_FLAG_SEQUENTIAL_SCAN hint during file open
 * - SetFileCompletionNotificationModes() for async I/O optimization
 *
 * Error handling:
 * - ERROR_NOT_ENOUGH_MEMORY: Virtual address space exhausted
 * - ERROR_ACCESS_DENIED: Permission denied or file in use
 * - ERROR_INVALID_PARAMETER: Invalid arguments
 *
 * Platform availability:
 * - Windows: All supported versions (Vista+)
 * - Other platforms: Not available (use PosixMMapIndexInput)
 */
class WindowsMMapIndexInput : public MMapIndexInput {
public:
    /**
     * @brief Constructs a Windows memory-mapped IndexInput.
     *
     * Opens the file with CreateFile(), creates file mapping with
     * CreateFileMapping(), and maps chunks using MapViewOfFile().
     *
     * @param path File path to map
     * @param chunk_power Power-of-2 for chunk size (e.g., 34 = 16GB)
     * @param preload Whether to preload pages (limited support on Windows)
     * @param advice Read advice hint (SEQUENTIAL/RANDOM/NORMAL)
     * @throws IOException if file cannot be opened or mapped
     * @throws EOFException if file is empty (0 bytes)
     *
     * File and mapping handles are kept open until all clones/slices
     * are destroyed (managed by shared_ptr reference counting).
     */
    WindowsMMapIndexInput(const std::filesystem::path& path, int chunk_power, bool preload,
                          IOContext::ReadAdvice advice = IOContext::ReadAdvice::NORMAL);

    /**
     * @brief Copy constructor for cloning.
     *
     * Used internally by clone() and slice(). Public to allow make_unique usage.
     *
     * @param other Source input to clone
     */
    WindowsMMapIndexInput(const WindowsMMapIndexInput& other);

    /**
     * @brief Destructor.
     *
     * Cleanup is automatic via shared_ptr custom deleter calling unmapChunks().
     */
    ~WindowsMMapIndexInput() override = default;

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
    std::unique_ptr<IndexInput> slice(const std::string& sliceDescription, int64_t offset,
                                      int64_t length) const override;

protected:
    /**
     * @brief Maps file chunks using Windows CreateFileMapping/MapViewOfFile.
     *
     * Called by constructor to perform actual memory mapping.
     * Creates one MapViewOfFile() call per chunk.
     *
     * Algorithm:
     * 1. Calculate number of chunks: ceil(file_length / chunk_size)
     * 2. Create file mapping object via CreateFileMapping()
     * 3. For each chunk:
     *    a. Calculate chunk offset and size
     *    b. Call MapViewOfFile(FILE_MAP_READ, offset_high, offset_low, size)
     *    c. Verify mapping succeeded (pointer != NULL)
     * 4. Store chunks in chunks_ array
     *
     * @param fd_placeholder Placeholder (Windows uses HANDLE, not int fd)
     * @param file_length Total file length in bytes
     * @throws IOException on mapping failure with Windows error details
     *
     * Error messages provide actionable guidance:
     * - ERROR_NOT_ENOUGH_MEMORY: "Insufficient virtual address space"
     * - ERROR_ACCESS_DENIED: "File access denied or locked by another process"
     * - ERROR_INVALID_PARAMETER: "Invalid mapping parameters (internal error)"
     */
    void mapChunks(int fd_placeholder, int64_t file_length) override;

    /**
     * @brief Unmaps chunks using Windows UnmapViewOfFile().
     *
     * Called by shared_ptr custom deleter when last reference is destroyed.
     * Must unmap all views and close handles.
     *
     * Algorithm:
     * 1. For each chunk:
     *    a. If view pointer is valid:
     *       - Call UnmapViewOfFile(view_pointer)
     *       - Ignore errors (best-effort cleanup)
     * 2. Close file mapping handle with CloseHandle()
     * 3. Close file handle with CloseHandle()
     * 4. Delete chunk array
     *
     * @param chunks Array of chunks to unmap
     * @param num_chunks Number of chunks in array
     * @param fd_placeholder Placeholder (not used on Windows)
     *
     * Note: Errors during unmapping are logged but not propagated.
     */
    void unmapChunks(MMapChunk* chunks, size_t num_chunks, int fd_placeholder) override;

private:
    /**
     * @brief Windows file handle (HANDLE type).
     *
     * Stored for cleanup. The actual handle is kept in the shared_ptr
     * custom deleter context via lambda capture.
     */
    HANDLE file_handle_;

    /**
     * @brief Windows file mapping handle (HANDLE type).
     *
     * Created via CreateFileMapping(). Closed when last reference is released.
     */
    HANDLE mapping_handle_;

    /**
     * @brief Applies read advice hints (limited support on Windows).
     *
     * Windows doesn't have direct madvise() equivalent. This method:
     * - Uses FILE_FLAG_SEQUENTIAL_SCAN during CreateFile() for sequential access
     * - No specific optimization for random access (Windows handles this automatically)
     * - Logs warnings about limited hint support
     *
     * @param advice Read advice to apply
     *
     * Note: Less effective than POSIX madvise(). Windows uses its own
     * automatic prefetching heuristics based on access patterns.
     */
    void applyReadAdvice(IOContext::ReadAdvice advice);

    /**
     * @brief Preloads pages (limited support on Windows).
     *
     * Windows has no direct equivalent to POSIX_MADV_WILLNEED.
     * This method attempts to touch first byte of each page to
     * force loading, but this is less efficient than POSIX preload.
     *
     * Note: Consider using PrefetchVirtualMemory() on Windows 8+,
     * but this requires additional platform detection.
     */
    void preloadPages();

    /**
     * @brief Helper to convert Windows error codes to readable messages.
     *
     * @param error_code Windows GetLastError() code
     * @return Human-readable error message with troubleshooting hints
     */
    static std::string getWindowsErrorMessage(DWORD error_code);
};

#endif  // _WIN32

}  // namespace diagon::store
