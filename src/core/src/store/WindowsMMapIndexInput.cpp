// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/store/WindowsMMapIndexInput.h"

#ifdef _WIN32

#    include "diagon/util/Exceptions.h"

#    include <sstream>
#    include <vector>

namespace diagon::store {

// ==================== Helper Functions ====================

std::string WindowsMMapIndexInput::getWindowsErrorMessage(DWORD error_code) {
    std::ostringstream oss;
    oss << "Windows error " << error_code << ": ";

    switch (error_code) {
        case ERROR_FILE_NOT_FOUND:
            oss << "File not found";
            break;
        case ERROR_ACCESS_DENIED:
            oss << "Access denied. File may be locked by another process or insufficient "
                   "permissions.";
            break;
        case ERROR_NOT_ENOUGH_MEMORY:
            oss << "Insufficient virtual address space. Try using smaller chunk size or close "
                   "other applications.";
            break;
        case ERROR_INVALID_PARAMETER:
            oss << "Invalid parameters (internal error)";
            break;
        case ERROR_SHARING_VIOLATION:
            oss << "File is in use by another process";
            break;
        case ERROR_LOCK_VIOLATION:
            oss << "File region is locked by another process";
            break;
        default: {
            // Use FormatMessage to get system error description
            LPVOID msgBuf = nullptr;
            DWORD size = FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                    FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr, error_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&msgBuf, 0,
                nullptr);

            if (size > 0 && msgBuf != nullptr) {
                oss << static_cast<char*>(msgBuf);
                LocalFree(msgBuf);
            } else {
                oss << "Unknown error";
            }
            break;
        }
    }

    return oss.str();
}

// ==================== Constructor ====================

WindowsMMapIndexInput::WindowsMMapIndexInput(const std::filesystem::path& path, int chunk_power,
                                             bool preload, IOContext::ReadAdvice advice)
    : MMapIndexInput(path, chunk_power, preload)
    , file_handle_(INVALID_HANDLE_VALUE)
    , mapping_handle_(nullptr) {
    // Base constructor sets up metadata
    // Now perform actual Windows memory mapping

    if (file_length_ == 0) {
        return;  // Empty file, nothing to map
    }

    // Determine file flags based on read advice
    DWORD file_flags = FILE_ATTRIBUTE_NORMAL;
    if (advice == IOContext::ReadAdvice::SEQUENTIAL) {
        file_flags |= FILE_FLAG_SEQUENTIAL_SCAN;
    } else if (advice == IOContext::ReadAdvice::RANDOM) {
        file_flags |= FILE_FLAG_RANDOM_ACCESS;
    }

    // Open file with CreateFile
    file_handle_ = CreateFileW(
        path.c_str(),
        GENERIC_READ,                                            // Desired access
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,  // Share mode
        nullptr,                                                 // Security attributes
        OPEN_EXISTING,                                           // Creation disposition
        file_flags,                                              // Flags and attributes
        nullptr                                                  // Template file
    );

    if (file_handle_ == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        throw IOException("Failed to open file for mapping: " + path.string() + " - " +
                          getWindowsErrorMessage(error));
    }

    try {
        // Create file mapping object
        // Split 64-bit file size into high and low 32-bit parts
        DWORD size_high = static_cast<DWORD>(file_length_ >> 32);
        DWORD size_low = static_cast<DWORD>(file_length_ & 0xFFFFFFFF);

        mapping_handle_ = CreateFileMappingW(file_handle_,
                                             nullptr,        // Security attributes
                                             PAGE_READONLY,  // Protection
                                             size_high,      // Maximum size high
                                             size_low,       // Maximum size low
                                             nullptr         // Name (anonymous)
        );

        if (mapping_handle_ == nullptr) {
            DWORD error = GetLastError();
            CloseHandle(file_handle_);
            throw IOException("Failed to create file mapping: " + path.string() + " - " +
                              getWindowsErrorMessage(error));
        }

        // Map chunks
        mapChunks(0, file_length_);  // fd_placeholder not used on Windows

        // Apply read advice (limited support)
        if (advice != IOContext::ReadAdvice::NORMAL) {
            applyReadAdvice(advice);
        }

        // Preload if requested (limited support)
        if (preload) {
            preloadPages();
        }

    } catch (...) {
        if (mapping_handle_ != nullptr) {
            CloseHandle(mapping_handle_);
        }
        CloseHandle(file_handle_);
        throw;
    }
}

// ==================== Copy Constructor ====================

WindowsMMapIndexInput::WindowsMMapIndexInput(const WindowsMMapIndexInput& other)
    : MMapIndexInput(other)
    , file_handle_(other.file_handle_)
    , mapping_handle_(other.mapping_handle_) {
    // Base copy constructor handles chunk sharing
    // Handles are reference-counted via shared_ptr, so safe to copy
}

// ==================== Cloning and Slicing ====================

std::unique_ptr<IndexInput> WindowsMMapIndexInput::clone() const {
    return std::make_unique<WindowsMMapIndexInput>(*this);
}

std::unique_ptr<IndexInput> WindowsMMapIndexInput::slice(const std::string& sliceDescription,
                                                         int64_t offset, int64_t length) const {
    // Validate slice bounds
    int64_t max_length = is_slice_ ? slice_length_ : file_length_;
    if (offset < 0 || length < 0 || offset + length > max_length) {
        std::ostringstream oss;
        oss << "Invalid slice: offset=" << offset << ", length=" << length;
        oss << ", but parent length=" << max_length;
        throw IOException(oss.str());
    }

    // Create slice by copying and adjusting offsets
    auto sliced = std::make_unique<WindowsMMapIndexInput>(*this);

    sliced->is_slice_ = true;
    if (is_slice_) {
        // Slice of a slice: adjust offset relative to original file
        sliced->slice_offset_ = slice_offset_ + offset;
    } else {
        sliced->slice_offset_ = offset;
    }
    sliced->slice_length_ = length;
    sliced->pos_ = 0;  // Reset position to start of slice

    return sliced;
}

// ==================== Platform-Specific Mapping ====================

void WindowsMMapIndexInput::mapChunks(int fd_placeholder, int64_t file_length) {
    // Calculate chunk size
    int64_t chunk_size = 1LL << chunk_power_;

    // Allocate chunk array
    MMapChunk* chunks = new MMapChunk[num_chunks_];

    // Map each chunk
    int64_t offset = 0;
    for (size_t i = 0; i < num_chunks_; ++i) {
        // Calculate this chunk's size (last chunk may be partial)
        int64_t this_chunk_size = std::min(chunk_size, file_length - offset);

        // Split 64-bit offset into high and low 32-bit parts
        DWORD offset_high = static_cast<DWORD>(offset >> 32);
        DWORD offset_low = static_cast<DWORD>(offset & 0xFFFFFFFF);

        // Map view of file
        void* mapped = MapViewOfFile(mapping_handle_,
                                     FILE_MAP_READ,                        // Desired access
                                     offset_high,                          // File offset high
                                     offset_low,                           // File offset low
                                     static_cast<SIZE_T>(this_chunk_size)  // Number of bytes to map
        );

        if (mapped == nullptr) {
            DWORD error = GetLastError();

            // Cleanup already-mapped chunks
            for (size_t j = 0; j < i; ++j) {
                if (chunks[j].data != nullptr) {
                    UnmapViewOfFile(chunks[j].data);
                }
            }
            delete[] chunks;

            // Build error message with troubleshooting hints
            std::ostringstream oss;
            oss << "Failed to map chunk " << i << " of " << num_chunks_;
            oss << " (offset=" << offset << ", size=" << this_chunk_size << "): ";
            oss << getWindowsErrorMessage(error);

            if (error == ERROR_NOT_ENOUGH_MEMORY) {
                oss << "\n  Try: Reduce chunk size, close other applications, ";
                oss << "or use FSDirectory for smaller memory footprint.";
            }

            throw IOException(oss.str());
        }

        chunks[i].data = static_cast<uint8_t*>(mapped);
        chunks[i].length = static_cast<size_t>(this_chunk_size);
        chunks[i].fd = 0;  // Not used on Windows

        offset += this_chunk_size;
    }

    // Set up shared_ptr with custom deleter
    // Capture handles for cleanup
    HANDLE file_h = file_handle_;
    HANDLE mapping_h = mapping_handle_;

    auto deleter = [num_chunks = num_chunks_, file_h, mapping_h](MMapChunk* chunks_to_delete) {
        // Unmap all views
        for (size_t i = 0; i < num_chunks; ++i) {
            if (chunks_to_delete[i].data != nullptr) {
                UnmapViewOfFile(chunks_to_delete[i].data);
            }
        }

        // Close handles
        if (mapping_h != nullptr) {
            CloseHandle(mapping_h);
        }
        if (file_h != INVALID_HANDLE_VALUE) {
            CloseHandle(file_h);
        }

        delete[] chunks_to_delete;
    };

    chunks_ = std::shared_ptr<MMapChunk[]>(chunks, deleter);
}

void WindowsMMapIndexInput::unmapChunks(MMapChunk* chunks, size_t num_chunks, int fd_placeholder) {
    // Cleanup is handled by shared_ptr deleter
    // This method exists for interface compatibility but isn't called directly

    for (size_t i = 0; i < num_chunks; ++i) {
        if (chunks[i].data != nullptr) {
            UnmapViewOfFile(chunks[i].data);
        }
    }

    // Close handles
    if (mapping_handle_ != nullptr) {
        CloseHandle(mapping_handle_);
        mapping_handle_ = nullptr;
    }
    if (file_handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(file_handle_);
        file_handle_ = INVALID_HANDLE_VALUE;
    }

    delete[] chunks;
}

// ==================== Read Advice (Limited Windows Support) ====================

void WindowsMMapIndexInput::applyReadAdvice(IOContext::ReadAdvice advice) {
    // Windows doesn't have direct madvise() equivalent
    // FILE_FLAG_SEQUENTIAL_SCAN and FILE_FLAG_RANDOM_ACCESS are set during CreateFile()
    // which already happened in constructor

    // Windows 8+ has PrefetchVirtualMemory() API, but we'll keep this simple
    // and rely on Windows automatic prefetching heuristics

    // Note: This is less effective than POSIX madvise()
    // Windows uses its own automatic heuristics based on access patterns
}

void WindowsMMapIndexInput::preloadPages() {
    // Windows has no direct MADV_WILLNEED equivalent
    // Best we can do is touch first byte of each page to force loading

    const size_t PAGE_SIZE = 4096;  // Typical Windows page size

    for (size_t i = 0; i < num_chunks_; ++i) {
        auto& chunk = chunks_[i];
        if (chunk.data != nullptr) {
            // Touch first byte of each page in this chunk
            volatile uint8_t dummy;
            for (size_t offset = 0; offset < chunk.length; offset += PAGE_SIZE) {
                dummy = chunk.data[offset];
                (void)dummy;  // Prevent optimization
            }
        }
    }

    // Note: Windows 8+ has PrefetchVirtualMemory() which is more efficient
    // Could add platform detection and use that API when available
}

}  // namespace diagon::store

#endif  // _WIN32
