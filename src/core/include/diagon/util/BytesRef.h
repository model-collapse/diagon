// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace diagon::util {

/**
 * @brief Represents a byte sequence as a slice (offset + length) into an existing buffer.
 *
 * Based on: org.apache.lucene.util.BytesRef
 *
 * This class is the fundamental binary data container used throughout Diagon for:
 * - Terms in the inverted index (UTF-8 encoded)
 * - Binary field values
 * - Codec format headers
 * - Keys in hash tables
 *
 * Design decisions:
 * - Uses std::span<const uint8_t> for immutable view semantics (C++20)
 * - Supports both owned (std::vector) and borrowed (raw pointer) storage
 * - UTF-8 encoding/decoding for string interop
 * - Lexicographic comparison for term ordering
 *
 * @note Unlike Java's BytesRef which uses mutable byte[], this C++ version
 *       provides both mutable and immutable variants for type safety.
 */
class BytesRef {
public:
    /**
     * @brief Creates an empty BytesRef with no backing storage.
     */
    BytesRef() noexcept = default;

    /**
     * @brief Creates a BytesRef with owned storage of the specified capacity.
     * @param capacity Initial capacity in bytes
     */
    explicit BytesRef(size_t capacity);

    /**
     * @brief Creates a BytesRef from a raw byte buffer (borrowed, non-owning).
     * @param data Pointer to the byte buffer (must outlive this BytesRef)
     * @param offset Starting offset in the buffer
     * @param length Number of bytes to reference
     */
    BytesRef(const uint8_t* data, size_t offset, size_t length) noexcept;

    /**
     * @brief Creates a BytesRef from a raw byte buffer (borrowed, non-owning).
     * @param data Pointer to the byte buffer (must outlive this BytesRef)
     * @param length Number of bytes to reference
     */
    BytesRef(const uint8_t* data, size_t length) noexcept;

    /**
     * @brief Creates a BytesRef from a vector (borrowed, non-owning).
     * @param vec Vector containing the bytes (must outlive this BytesRef)
     */
    explicit BytesRef(const std::vector<uint8_t>& vec) noexcept;

    /**
     * @brief Creates a BytesRef from a string (converts to UTF-8, owned storage).
     * @param text UTF-8 encoded string
     */
    explicit BytesRef(std::string_view text);

    /**
     * @brief Creates a BytesRef from a span (borrowed, non-owning).
     * @param span Span of bytes (must outlive this BytesRef)
     */
    explicit BytesRef(std::span<const uint8_t> span) noexcept;

    // Copy semantics: shallow copy (shares data)
    BytesRef(const BytesRef&) noexcept = default;
    BytesRef& operator=(const BytesRef&) noexcept = default;

    // Move semantics
    BytesRef(BytesRef&&) noexcept = default;
    BytesRef& operator=(BytesRef&&) noexcept = default;

    /**
     * @brief Creates a deep copy with owned storage.
     * @return A new BytesRef with its own copy of the data
     */
    [[nodiscard]] BytesRef deepCopy() const;

    /**
     * @brief Returns a span view of the bytes.
     * @return std::span<const uint8_t> view of the data
     */
    [[nodiscard]] std::span<const uint8_t> bytes() const noexcept { return data_; }

    /**
     * @brief Returns the raw data pointer.
     * @return Pointer to the first byte, or nullptr if empty
     */
    [[nodiscard]] const uint8_t* data() const noexcept { return data_.data(); }

    /**
     * @brief Returns the length in bytes.
     * @return Number of bytes
     */
    [[nodiscard]] size_t length() const noexcept { return data_.size(); }

    /**
     * @brief Returns the length in bytes.
     * @return Number of bytes
     */
    [[nodiscard]] size_t size() const noexcept { return data_.size(); }

    /**
     * @brief Checks if the BytesRef is empty.
     * @return true if length is 0
     */
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }

    /**
     * @brief Converts UTF-8 bytes to a UTF-8 string.
     * @return std::string containing the decoded text
     * @throws std::runtime_error if bytes are not valid UTF-8
     */
    [[nodiscard]] std::string utf8ToString() const;

    /**
     * @brief Returns a hex-encoded string representation.
     * @return String in format "[6c 75 63 65 6e 65]"
     */
    [[nodiscard]] std::string toString() const;

    /**
     * @brief Compares this BytesRef with another for equality.
     * @param other The other BytesRef to compare
     * @return true if byte contents are identical
     */
    [[nodiscard]] bool equals(const BytesRef& other) const noexcept;

    /**
     * @brief Compares this BytesRef with another lexicographically.
     * @param other The other BytesRef to compare
     * @return -1 if this < other, 0 if equal, 1 if this > other
     */
    [[nodiscard]] int compareTo(const BytesRef& other) const noexcept;

    /**
     * @brief Computes a hash code for this BytesRef.
     * @return 32-bit hash value using MurmurHash3
     */
    [[nodiscard]] size_t hashCode() const noexcept;

    // Operator overloads for convenience
    bool operator==(const BytesRef& other) const noexcept { return equals(other); }

    bool operator!=(const BytesRef& other) const noexcept { return !equals(other); }

    bool operator<(const BytesRef& other) const noexcept { return compareTo(other) < 0; }

    bool operator<=(const BytesRef& other) const noexcept { return compareTo(other) <= 0; }

    bool operator>(const BytesRef& other) const noexcept { return compareTo(other) > 0; }

    bool operator>=(const BytesRef& other) const noexcept { return compareTo(other) >= 0; }

    /**
     * @brief Array subscript operator.
     * @param index Index of the byte to access
     * @return The byte at the specified index
     */
    uint8_t operator[](size_t index) const noexcept { return data_[index]; }

    /**
     * @brief Creates a sub-slice of this BytesRef.
     * @param offset Starting offset
     * @param length Number of bytes
     * @return New BytesRef referencing a sub-range
     */
    [[nodiscard]] BytesRef slice(size_t offset, size_t length) const;

private:
    // Backing storage (either owned or borrowed)
    std::span<const uint8_t> data_;

    // Owned storage (if this BytesRef owns its data)
    std::shared_ptr<std::vector<uint8_t>> owned_storage_;

    // Helper: Convert UTF-8 string to bytes
    static std::vector<uint8_t> stringToUTF8(std::string_view text);
};

}  // namespace diagon::util

// Hash function for use in std::unordered_map
namespace std {
template<>
struct hash<diagon::util::BytesRef> {
    size_t operator()(const diagon::util::BytesRef& ref) const noexcept { return ref.hashCode(); }
};
}  // namespace std
