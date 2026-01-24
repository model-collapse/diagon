// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace diagon::store {

/**
 * @brief Abstract base class for reading index files with random access.
 *
 * Based on: org.apache.lucene.store.IndexInput
 *
 * IndexInput provides:
 * - Random access via seek()
 * - Efficient bulk reads
 * - VInt/VLong variable-length integer encoding
 * - Cloning for independent file pointers
 * - Slicing for sub-views
 *
 * Design principles:
 * - Immutable after creation (position can change, but not file contents)
 * - Thread-safe: Multiple clones can read concurrently
 * - Efficient: Implementations should use buffering or memory mapping
 *
 * Performance:
 * - readByte(): O(1) amortized (buffered)
 * - seek(): O(1) for most implementations
 * - readBytes(): Optimized bulk transfer
 */
class IndexInput {
public:
    virtual ~IndexInput() = default;

    // ==================== Basic Reading ====================

    /**
     * @brief Reads a single byte.
     * @return The byte read (0-255)
     * @throws EOFException if at end of file
     * @throws IOException on I/O error
     */
    virtual uint8_t readByte() = 0;

    /**
     * @brief Reads bytes into a buffer.
     * @param buffer Destination buffer
     * @param length Number of bytes to read
     * @throws EOFException if not enough bytes available
     * @throws IOException on I/O error
     */
    virtual void readBytes(uint8_t* buffer, size_t length) = 0;

    // ==================== Multi-byte Reads ====================

    /**
     * @brief Reads a 16-bit short (big-endian).
     * @return The short value
     */
    virtual int16_t readShort() {
        return static_cast<int16_t>((readByte() << 8) | readByte());
    }

    /**
     * @brief Reads a 32-bit integer (big-endian).
     * @return The int value
     */
    virtual int32_t readInt() {
        return (static_cast<int32_t>(readShort()) << 16) |
               (readShort() & 0xFFFF);
    }

    /**
     * @brief Reads a 64-bit long (big-endian).
     * @return The long value
     */
    virtual int64_t readLong() {
        return (static_cast<int64_t>(readInt()) << 32) |
               (readInt() & 0xFFFFFFFFLL);
    }

    // ==================== Variable-Length Encoding ====================

    /**
     * @brief Reads a variable-length integer (1-5 bytes).
     *
     * Format: 7 bits per byte, high bit indicates continuation
     * - 0xxxxxxx: single byte (0-127)
     * - 1xxxxxxx 0xxxxxxx: two bytes
     * - etc.
     *
     * @return The decoded integer
     * @throws IOException on invalid encoding
     */
    virtual int32_t readVInt();

    /**
     * @brief Reads a variable-length long (1-9 bytes).
     * @return The decoded long
     * @throws IOException on invalid encoding
     */
    virtual int64_t readVLong();

    /**
     * @brief Reads a string (length-prefixed, UTF-8 encoded).
     * @return The decoded string
     */
    virtual std::string readString();

    // ==================== Positioning ====================

    /**
     * @brief Returns the current file pointer position.
     * @return Position in bytes from start of file
     */
    virtual int64_t getFilePointer() const = 0;

    /**
     * @brief Seeks to an absolute position.
     * @param pos Position in bytes from start of file
     * @throws IOException if pos is invalid
     */
    virtual void seek(int64_t pos) = 0;

    /**
     * @brief Returns the length of the file.
     * @return File length in bytes
     */
    virtual int64_t length() const = 0;

    /**
     * @brief Returns the file name for diagnostic purposes.
     * @return File name or slice description
     */
    virtual std::string toString() const {
        return "IndexInput";
    }

    // ==================== Cloning and Slicing ====================

    /**
     * @brief Creates an independent clone with its own file pointer.
     *
     * Clones share the underlying file handle but maintain independent
     * read positions. This enables concurrent reads from multiple threads.
     *
     * @return A new IndexInput that shares file data
     */
    virtual std::unique_ptr<IndexInput> clone() const = 0;

    /**
     * @brief Creates a slice (view of a sub-range).
     *
     * A slice acts like an independent IndexInput that can only access
     * a contiguous sub-range of the parent. Position 0 in the slice
     * corresponds to offset in the parent.
     *
     * @param sliceDescription Description for debugging
     * @param offset Starting offset in parent
     * @param length Length of slice
     * @return A new IndexInput representing the slice
     */
    virtual std::unique_ptr<IndexInput> slice(
        const std::string& sliceDescription,
        int64_t offset,
        int64_t length) const = 0;

    // ==================== Utilities ====================

    /**
     * @brief Skips over bytes without reading them.
     * @param numBytes Number of bytes to skip
     */
    virtual void skipBytes(int64_t numBytes) {
        seek(getFilePointer() + numBytes);
    }

    /**
     * @brief Checks if we're at end of file.
     * @return true if no more bytes can be read
     */
    [[nodiscard]] bool eof() const {
        return getFilePointer() >= length();
    }

protected:
    /**
     * @brief Helper for clone() implementations.
     * @param resourceDescription Description of the cloned resource
     */
    IndexInput() = default;
};

} // namespace diagon::store
