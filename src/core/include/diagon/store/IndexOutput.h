// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/util/Exceptions.h"

#include <cstdint>
#include <string>

namespace diagon::store {

/**
 * @brief Abstract base class for writing index files sequentially.
 *
 * Based on: org.apache.lucene.store.IndexOutput
 *
 * IndexOutput provides:
 * - Sequential writes
 * - Efficient bulk writes
 * - VInt/VLong variable-length integer encoding
 * - Optional checksum computation
 * - Atomic finalization
 *
 * Design principles:
 * - Write-only (no seek/read)
 * - Must call close() to finalize
 * - Checksum computed incrementally (if supported)
 *
 * Usage pattern:
 * ```cpp
 * auto output = directory->createOutput("data.bin", IOContext::DEFAULT);
 * output->writeInt(42);
 * output->writeString("hello");
 * output->close();  // MUST call to finalize
 * ```
 */
class IndexOutput {
public:
    virtual ~IndexOutput() = default;

    // ==================== Basic Writing ====================

    /**
     * @brief Writes a single byte.
     * @param b The byte to write (0-255)
     * @throws IOException on I/O error
     */
    virtual void writeByte(uint8_t b) = 0;

    /**
     * @brief Writes bytes from a buffer.
     * @param buffer Source buffer
     * @param length Number of bytes to write
     * @throws IOException on I/O error
     */
    virtual void writeBytes(const uint8_t* buffer, size_t length) = 0;

    // ==================== Multi-byte Writes ====================

    /**
     * @brief Writes a 16-bit short (big-endian).
     * @param s The short value
     */
    virtual void writeShort(int16_t s) {
        writeByte(static_cast<uint8_t>(s >> 8));
        writeByte(static_cast<uint8_t>(s));
    }

    /**
     * @brief Writes a 32-bit integer (big-endian).
     * @param i The int value
     */
    virtual void writeInt(int32_t i) {
        writeShort(static_cast<int16_t>(i >> 16));
        writeShort(static_cast<int16_t>(i));
    }

    /**
     * @brief Writes a 64-bit long (big-endian).
     * @param l The long value
     */
    virtual void writeLong(int64_t l) {
        writeInt(static_cast<int32_t>(l >> 32));
        writeInt(static_cast<int32_t>(l));
    }

    // ==================== Variable-Length Encoding ====================

    /**
     * @brief Writes a variable-length integer (1-5 bytes).
     *
     * Format: 7 bits per byte, high bit indicates continuation
     * - 0-127: 1 byte
     * - 128-16383: 2 bytes
     * - etc.
     *
     * @param i The integer to encode
     */
    virtual void writeVInt(int32_t i);

    /**
     * @brief Writes a variable-length long (1-9 bytes).
     * @param l The long to encode
     */
    virtual void writeVLong(int64_t l);

    /**
     * @brief Writes a string (length-prefixed, UTF-8 encoded).
     * @param s The string to write
     */
    virtual void writeString(const std::string& s);

    // ==================== Positioning ====================

    /**
     * @brief Returns the current file pointer position.
     * @return Position in bytes from start of file
     */
    virtual int64_t getFilePointer() const = 0;

    /**
     * @brief Returns the checksum of bytes written so far.
     *
     * Only supported if this output computes checksums.
     * Subclasses that don't support checksums should throw
     * UnsupportedOperationException.
     *
     * @return Current checksum value
     * @throws UnsupportedOperationException if not supported
     */
    virtual int64_t getChecksum() const {
        throw UnsupportedOperationException("Checksums not supported");
    }

    // ==================== Finalization ====================

    /**
     * @brief Closes and finalizes the output.
     *
     * This MUST be called to ensure data is flushed and the file
     * is properly finalized. After close(), no further writes are allowed.
     *
     * @throws IOException on I/O error
     */
    virtual void close() = 0;

    /**
     * @brief Returns the file name for diagnostic purposes.
     * @return File name
     */
    virtual std::string getName() const = 0;

protected:
    IndexOutput() = default;
};

} // namespace diagon::store
