// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/store/MMapIndexInput.h"

#include "diagon/util/Exceptions.h"
#include "diagon/util/SIMDUtils.h"

#include <sys/stat.h>

#include <algorithm>
#include <cstring>
#include <sstream>

#include <fcntl.h>
#include <unistd.h>

namespace diagon::store {

// ==================== Constructor ====================

MMapIndexInput::MMapIndexInput(const std::filesystem::path& path, int chunk_power, bool preload)
    : path_(path)
    , chunk_power_(chunk_power)
    , chunk_mask_((1LL << chunk_power) - 1)
    , file_length_(0)
    , num_chunks_(0)
    , chunks_(nullptr)
    , pos_(0)
    , is_slice_(false)
    , slice_offset_(0)
    , slice_length_(0) {
    // Open file descriptor
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        throw IOException("Cannot open file: " + path.string());
    }

    // Get file size
    struct stat st;
    if (::fstat(fd, &st) == -1) {
        ::close(fd);
        throw IOException("Cannot stat file: " + path.string());
    }
    file_length_ = st.st_size;

    // Handle empty file
    if (file_length_ == 0) {
        ::close(fd);
        // Create empty chunk array
        num_chunks_ = 0;
        chunks_ = std::shared_ptr<MMapChunk[]>(new MMapChunk[0]);
        return;
    }

    // Calculate number of chunks
    int64_t chunk_size = 1LL << chunk_power;
    num_chunks_ = static_cast<size_t>((file_length_ + chunk_size - 1) / chunk_size);

    // Note: Actual mapping is done by subclass constructor
    // (cannot call pure virtual from base constructor)
    ::close(fd);  // Close fd, subclass will reopen
}

// ==================== Copy Constructor (for cloning) ====================

MMapIndexInput::MMapIndexInput(const MMapIndexInput& other)
    : path_(other.path_)
    , chunk_power_(other.chunk_power_)
    , chunk_mask_(other.chunk_mask_)
    , file_length_(other.file_length_)
    , num_chunks_(other.num_chunks_)
    , chunks_(other.chunks_)
    ,  // Share chunks (zero-copy)
    pos_(0)
    ,  // Reset position
    is_slice_(other.is_slice_)
    , slice_offset_(other.slice_offset_)
    , slice_length_(other.slice_length_) {
    // Clones share the underlying memory mapping but have independent positions
}

// ==================== Reading ====================

uint8_t MMapIndexInput::readByte() {
    // Check bounds
    int64_t absolute_pos;
    if (is_slice_) {
        if (pos_ >= slice_length_) {
            throw EOFException("Read past end of slice");
        }
        absolute_pos = slice_offset_ + pos_;
    } else {
        if (pos_ >= file_length_) {
            throw EOFException("Read past EOF");
        }
        absolute_pos = pos_;
    }

    // Fast path: single chunk read
    int chunk_idx = static_cast<int>(absolute_pos >> chunk_power_);
    size_t chunk_offset = static_cast<size_t>(absolute_pos & chunk_mask_);

    if (chunk_idx >= static_cast<int>(num_chunks_)) {
        throw EOFException("Read past EOF (chunk index out of bounds)");
    }

    auto& chunk = chunks_[chunk_idx];
    if (chunk_offset >= chunk.length) {
        throw EOFException("Read past chunk boundary");
    }

    pos_++;
    return chunk.data[chunk_offset];
}

void MMapIndexInput::readBytes(uint8_t* buffer, size_t length) {
    if (length == 0) {
        return;
    }

    // Check bounds
    int64_t available;
    if (is_slice_) {
        available = slice_length_ - pos_;
    } else {
        available = file_length_ - pos_;
    }

    if (length > static_cast<size_t>(available)) {
        std::ostringstream oss;
        oss << "Read past EOF: requested " << length << " bytes, but only " << available
            << " available";
        throw EOFException(oss.str());
    }

    // Handle chunk boundary crossing
    size_t remaining = length;
    size_t buffer_offset = 0;

    while (remaining > 0) {
        int64_t absolute_pos = is_slice_ ? (slice_offset_ + pos_) : pos_;

        // Calculate chunk and offset
        int chunk_idx = static_cast<int>(absolute_pos >> chunk_power_);
        size_t chunk_offset = static_cast<size_t>(absolute_pos & chunk_mask_);

        if (chunk_idx >= static_cast<int>(num_chunks_)) {
            throw EOFException("Read past EOF (chunk index out of bounds)");
        }

        auto& chunk = chunks_[chunk_idx];

        // Calculate how much we can read from this chunk
        size_t available_in_chunk = chunk.length - chunk_offset;
        size_t to_copy = std::min(remaining, available_in_chunk);

        // Prefetch next chunk if we're about to cross boundary
        // This reduces cache miss penalty for sequential reads spanning chunks
        if (chunk_offset + to_copy >= chunk.length &&
            chunk_idx + 1 < static_cast<int>(num_chunks_)) {
            // Prefetch first cache line of next chunk into L1 cache
            util::simd::Prefetch::read(chunks_[chunk_idx + 1].data,
                                       util::simd::Prefetch::Locality::HIGH);
        }

        // Copy data
        std::memcpy(buffer + buffer_offset, chunk.data + chunk_offset, to_copy);

        // Update positions
        pos_ += to_copy;
        buffer_offset += to_copy;
        remaining -= to_copy;
    }
}

// ==================== Positioning ====================

int64_t MMapIndexInput::getFilePointer() const {
    return pos_;
}

void MMapIndexInput::seek(int64_t pos) {
    if (pos < 0) {
        throw IOException("Negative position: " + std::to_string(pos));
    }

    int64_t max_pos = is_slice_ ? slice_length_ : file_length_;
    if (pos > max_pos) {
        std::ostringstream oss;
        oss << "Seek beyond EOF: position " << pos << ", but length is " << max_pos;
        throw IOException(oss.str());
    }

    pos_ = pos;
}

int64_t MMapIndexInput::length() const {
    return is_slice_ ? slice_length_ : file_length_;
}

// ==================== Fast Direct-Access Methods ====================

int32_t MMapIndexInput::readVInt() {
    const uint8_t* ptr;
    size_t remaining;

    // Fast path: enough bytes in current chunk for max VInt (5 bytes)
    if (getDirectPointer(5, ptr, remaining)) {
        uint8_t b = ptr[0];
        if ((b & 0x80) == 0) {
            pos_ += 1;
            return static_cast<int32_t>(b);
        }
        int32_t i = b & 0x7F;

        b = ptr[1];
        i |= (b & 0x7F) << 7;
        if ((b & 0x80) == 0) {
            pos_ += 2;
            return i;
        }

        b = ptr[2];
        i |= (b & 0x7F) << 14;
        if ((b & 0x80) == 0) {
            pos_ += 3;
            return i;
        }

        b = ptr[3];
        i |= (b & 0x7F) << 21;
        if ((b & 0x80) == 0) {
            pos_ += 4;
            return i;
        }

        b = ptr[4];
        i |= (b & 0x0F) << 28;
        pos_ += 5;
        return i;
    }

    // Slow path: cross-chunk read (extremely rare)
    return IndexInput::readVInt();
}

int64_t MMapIndexInput::readVLong() {
    const uint8_t* ptr;
    size_t remaining;

    // Fast path: enough bytes in current chunk for max VLong (9 bytes)
    if (getDirectPointer(9, ptr, remaining)) {
        uint8_t b = ptr[0];
        if ((b & 0x80) == 0) {
            pos_ += 1;
            return static_cast<int64_t>(b);
        }
        int64_t i = b & 0x7FL;
        int consumed = 1;

        for (int shift = 7; shift < 63; shift += 7) {
            b = ptr[consumed];
            consumed++;
            i |= (b & 0x7FL) << shift;
            if ((b & 0x80) == 0) {
                pos_ += consumed;
                return i;
            }
        }

        // 9th byte
        b = ptr[consumed];
        consumed++;
        i |= (b & 0x7FL) << 63;
        pos_ += consumed;
        return i;
    }

    // Slow path: cross-chunk read
    return IndexInput::readVLong();
}

int32_t MMapIndexInput::readInt() {
    const uint8_t* ptr;
    size_t remaining;

    if (getDirectPointer(4, ptr, remaining)) {
        int32_t result = (static_cast<int32_t>(ptr[0]) << 24) |
                         (static_cast<int32_t>(ptr[1]) << 16) |
                         (static_cast<int32_t>(ptr[2]) << 8) | static_cast<int32_t>(ptr[3]);
        pos_ += 4;
        return result;
    }

    return IndexInput::readInt();
}

int64_t MMapIndexInput::readLong() {
    const uint8_t* ptr;
    size_t remaining;

    if (getDirectPointer(8, ptr, remaining)) {
        int64_t result = (static_cast<int64_t>(ptr[0]) << 56) |
                         (static_cast<int64_t>(ptr[1]) << 48) |
                         (static_cast<int64_t>(ptr[2]) << 40) |
                         (static_cast<int64_t>(ptr[3]) << 32) |
                         (static_cast<int64_t>(ptr[4]) << 24) |
                         (static_cast<int64_t>(ptr[5]) << 16) |
                         (static_cast<int64_t>(ptr[6]) << 8) | static_cast<int64_t>(ptr[7]);
        pos_ += 8;
        return result;
    }

    return IndexInput::readLong();
}

}  // namespace diagon::store
