// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstddef>

namespace diagon::util {

/**
 * @brief Interface for bitset-like structures
 *
 * Based on: org.apache.lucene.util.Bits
 *
 * Bits provides a read-only view of a bit array. Used for:
 * - Live documents (1 = live, 0 = deleted)
 * - Filter results
 * - Doc ID sets
 *
 * Design:
 * - Read-only interface (no set/clear methods)
 * - Simple get(index) and length() API
 * - Implementations can be backed by various formats
 *
 * Common implementations:
 * - BitSet (dense bit array)
 * - MatchAllBits (all bits set)
 * - MatchNoBits (all bits clear)
 */
class Bits {
public:
    virtual ~Bits() = default;

    /**
     * @brief Gets the value of the bit at the specified index
     * @param index Bit index (0 to length()-1)
     * @return true if the bit is set, false otherwise
     */
    [[nodiscard]] virtual bool get(size_t index) const noexcept = 0;

    /**
     * @brief Returns the number of bits in this set
     * @return Number of bits
     */
    [[nodiscard]] virtual size_t length() const noexcept = 0;
};

/**
 * @brief Bits implementation where all bits are set
 *
 * Used when no deletions exist - all documents are live
 */
class MatchAllBits : public Bits {
public:
    explicit MatchAllBits(size_t length)
        : length_(length) {}

    [[nodiscard]] bool get(size_t index) const noexcept override { return true; }

    [[nodiscard]] size_t length() const noexcept override { return length_; }

private:
    size_t length_;
};

/**
 * @brief Bits implementation where all bits are clear
 *
 * Used for empty sets
 */
class MatchNoBits : public Bits {
public:
    explicit MatchNoBits(size_t length)
        : length_(length) {}

    [[nodiscard]] bool get(size_t index) const noexcept override { return false; }

    [[nodiscard]] size_t length() const noexcept override { return length_; }

private:
    size_t length_;
};

}  // namespace diagon::util
