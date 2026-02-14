// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/store/PosixMMapIndexInput.h"

#include "diagon/util/Exceptions.h"

#include <sys/mman.h>

#include <cerrno>
#include <cstring>
#include <sstream>

#include <fcntl.h>
#include <unistd.h>

namespace diagon::store {

// ==================== Constructor ====================

PosixMMapIndexInput::PosixMMapIndexInput(const std::filesystem::path& path, int chunk_power,
                                         bool preload, IOContext::ReadAdvice advice)
    : MMapIndexInput(path, chunk_power, preload) {
    // Base constructor sets up metadata
    // Now perform actual mmap
    if (file_length_ > 0 && num_chunks_ > 0) {
        // Reopen file for mapping
        int fd = ::open(path.c_str(), O_RDONLY);
        if (fd == -1) {
            throw IOException("Cannot reopen file for mapping: " + path.string());
        }

        try {
            mapChunks(fd, file_length_);

            // Apply read advice hints (best-effort)
            if (advice != IOContext::ReadAdvice::NORMAL) {
                applyReadAdvice(advice);
            }

            // Preload pages if requested (best-effort)
            if (preload) {
                preloadPages();
            }
        } catch (...) {
            ::close(fd);
            throw;
        }
    }
}

// ==================== Copy Constructor ====================

PosixMMapIndexInput::PosixMMapIndexInput(const PosixMMapIndexInput& other)
    : MMapIndexInput(other) {
    // Base copy constructor handles chunk sharing
}

// ==================== Cloning and Slicing ====================

std::unique_ptr<IndexInput> PosixMMapIndexInput::clone() const {
    // Use copy constructor to create clone
    return std::make_unique<PosixMMapIndexInput>(*this);
}

std::unique_ptr<IndexInput> PosixMMapIndexInput::slice(const std::string& sliceDescription,
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
    auto sliced = std::make_unique<PosixMMapIndexInput>(*this);

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

void PosixMMapIndexInput::mapChunks(int fd, int64_t file_length) {
    // Calculate chunk size
    int64_t chunk_size = 1LL << chunk_power_;

    // Allocate chunk array
    MMapChunk* chunks = new MMapChunk[num_chunks_];

    // Map each chunk
    int64_t offset = 0;
    for (size_t i = 0; i < num_chunks_; ++i) {
        // Calculate this chunk's size (last chunk may be partial)
        int64_t this_chunk_size = std::min(chunk_size, file_length - offset);

        // Perform mmap
        void* mapped = ::mmap(nullptr,                               // Let kernel choose address
                              static_cast<size_t>(this_chunk_size),  // Length to map
                              PROT_READ,                             // Read-only protection
                              MAP_SHARED,  // Shared mapping (don't write changes back)
                              fd,          // File descriptor
                              offset       // Offset in file
        );

        if (mapped == MAP_FAILED) {
            // Cleanup already-mapped chunks
            for (size_t j = 0; j < i; ++j) {
                if (chunks[j].data && chunks[j].data != static_cast<uint8_t*>(MAP_FAILED)) {
                    ::munmap(chunks[j].data, chunks[j].length);
                }
            }
            delete[] chunks;

            // Build error message with platform-specific guidance
            std::ostringstream oss;
            oss << "Failed to mmap file: " << path_.string();
            oss << " (chunk " << i << " of " << num_chunks_ << ", ";
            oss << "offset=" << offset << ", size=" << this_chunk_size << "): ";
            oss << std::strerror(errno);

            // Add platform-specific guidance
            if (errno == ENOMEM) {
                oss << ". Out of memory or address space exhausted. ";
                oss << "On Linux, check 'ulimit -v' (virtual memory limit) and ";
                oss << "'sysctl vm.max_map_count' (max number of memory mappings). ";
                oss << "On 32-bit systems, consider using smaller chunk size or FSDirectory.";
            } else if (errno == EACCES || errno == EPERM) {
                oss << ". Permission denied. Check file is readable and is a regular file.";
            } else if (errno == EINVAL) {
                oss << ". Invalid arguments (internal error, please report).";
            }

            throw IOException(oss.str());
        }

        // Store chunk info
        chunks[i] = MMapChunk(static_cast<uint8_t*>(mapped), static_cast<size_t>(this_chunk_size),
                              fd);

        offset += this_chunk_size;
    }

    // Create shared_ptr with custom deleter
    // The deleter will unmap chunks when the last reference is destroyed
    // Note: We capture by value (num_chunks, fd) NOT by pointer (this) to avoid UB
    chunks_ = std::shared_ptr<MMapChunk[]>(
        chunks, [num_chunks = num_chunks_, fd](MMapChunk* chunks_to_delete) {
            // Unmap all chunks (best-effort, ignore errors)
            for (size_t i = 0; i < num_chunks; ++i) {
                if (chunks_to_delete[i].data &&
                    chunks_to_delete[i].data != static_cast<uint8_t*>(MAP_FAILED)) {
                    // Ignore munmap errors (can't throw in destructor context)
                    ::munmap(chunks_to_delete[i].data, chunks_to_delete[i].length);
                }
            }

            // Close file descriptor
            if (fd != -1) {
                ::close(fd);
            }

            // Delete chunk array
            delete[] chunks_to_delete;
        });
}

// ==================== Platform-Specific Unmapping ====================

void PosixMMapIndexInput::unmapChunks(MMapChunk* chunks, size_t num_chunks, int fd) {
    // Unmap all chunks (best-effort, ignore errors)
    for (size_t i = 0; i < num_chunks; ++i) {
        if (chunks[i].data && chunks[i].data != static_cast<uint8_t*>(MAP_FAILED)) {
            // Ignore munmap errors (can't throw in destructor context)
            ::munmap(chunks[i].data, chunks[i].length);
        }
    }

    // Close file descriptor
    if (fd != -1) {
        ::close(fd);
    }

    // Delete chunk array
    delete[] chunks;
}

// ==================== Read Advice Optimization ====================

void PosixMMapIndexInput::applyReadAdvice(IOContext::ReadAdvice advice) {
    // Map IOContext::ReadAdvice to POSIX madvise constants
    int posix_advice;
    switch (advice) {
        case IOContext::ReadAdvice::SEQUENTIAL:
            posix_advice = POSIX_MADV_SEQUENTIAL;
            break;
        case IOContext::ReadAdvice::RANDOM:
            posix_advice = POSIX_MADV_RANDOM;
            break;
        case IOContext::ReadAdvice::NORMAL:
        default:
            posix_advice = POSIX_MADV_NORMAL;
            break;
    }

    // Apply advice to all chunks (best-effort, ignore errors)
    for (size_t i = 0; i < num_chunks_; ++i) {
        auto& chunk = chunks_[i];
        if (chunk.data && chunk.data != static_cast<uint8_t*>(MAP_FAILED)) {
            // posix_madvise returns 0 on success, errno on failure
            // We ignore errors as this is a best-effort optimization
            ::posix_madvise(chunk.data, chunk.length, posix_advice);
        }
    }
}

void PosixMMapIndexInput::preloadPages() {
    // Use POSIX_MADV_WILLNEED to hint the OS to load pages into physical memory
    for (size_t i = 0; i < num_chunks_; ++i) {
        auto& chunk = chunks_[i];
        if (chunk.data && chunk.data != static_cast<uint8_t*>(MAP_FAILED)) {
            // POSIX_MADV_WILLNEED initiates readahead on the specified range
            // This forces page faults now rather than on first access
            ::posix_madvise(chunk.data, chunk.length, POSIX_MADV_WILLNEED);
        }
    }
}

}  // namespace diagon::store
