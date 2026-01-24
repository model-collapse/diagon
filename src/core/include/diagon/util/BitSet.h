// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace diagon::util {

/**
 * @brief Fixed-length bit set backed by a uint64_t array.
 *
 * Based on: org.apache.lucene.util.FixedBitSet
 *
 * Used primarily for:
 * - Live documents tracking (1 = live, 0 = deleted)
 * - Filter results representation
 * - Sparse document ID sets
 *
 * Design decisions:
 * - Uses 64-bit words for efficient bitwise operations
 * - Cache-friendly layout for iteration
 * - Population count (cardinality) using hardware popcnt
 * - Set operations: AND, OR, XOR, ANDNOT
 *
 * Performance characteristics:
 * - set/get: O(1)
 * - nextSetBit: O(64) average (skips zero words)
 * - cardinality: O(n/64) where n is number of bits
 *
 * @note Ghost bits (past numBits) must always be clear to maintain invariants.
 */
class BitSet {
public:
    /**
     * @brief Creates a BitSet with the specified number of bits, all initially clear.
     * @param numBits Number of bits (0 to 2^31-1)
     */
    explicit BitSet(size_t numBits);

    /**
     * @brief Creates a BitSet using an existing word array.
     * @param words Backing storage (must have at least bits2words(numBits) elements)
     * @param numBits Number of bits in use
     */
    BitSet(std::vector<uint64_t> words, size_t numBits);

    // No copy (use clone() for explicit copying)
    BitSet(const BitSet&) = delete;
    BitSet& operator=(const BitSet&) = delete;

    // Move semantics
    BitSet(BitSet&&) noexcept = default;
    BitSet& operator=(BitSet&&) noexcept = default;

    /**
     * @brief Creates a deep copy of this BitSet.
     * @return New BitSet with identical contents
     */
    [[nodiscard]] std::unique_ptr<BitSet> clone() const;

    /**
     * @brief Returns the number of bits in this set.
     * @return Number of bits
     */
    [[nodiscard]] size_t length() const noexcept { return num_bits_; }

    /**
     * @brief Returns the number of bits in this set (alias for length).
     * @return Number of bits
     */
    [[nodiscard]] size_t size() const noexcept { return num_bits_; }

    /**
     * @brief Gets the value of the bit at the specified index.
     * @param index Bit index (0 to numBits-1)
     * @return true if the bit is set, false otherwise
     */
    [[nodiscard]] bool get(size_t index) const noexcept;

    /**
     * @brief Sets the bit at the specified index to 1.
     * @param index Bit index (0 to numBits-1)
     */
    void set(size_t index) noexcept;

    /**
     * @brief Sets the bit at the specified index and returns its previous value.
     * @param index Bit index (0 to numBits-1)
     * @return Previous value of the bit
     */
    bool getAndSet(size_t index) noexcept;

    /**
     * @brief Clears the bit at the specified index (sets to 0).
     * @param index Bit index (0 to numBits-1)
     */
    void clear(size_t index) noexcept;

    /**
     * @brief Clears all bits in the range [startIndex, endIndex).
     * @param startIndex Starting index (inclusive)
     * @param endIndex Ending index (exclusive)
     */
    void clear(size_t startIndex, size_t endIndex) noexcept;

    /**
     * @brief Clears all bits in this set.
     */
    void clear() noexcept;

    /**
     * @brief Returns the number of set bits (population count).
     * @return Number of 1 bits
     */
    [[nodiscard]] size_t cardinality() const noexcept;

    /**
     * @brief Returns an approximation of the cardinality.
     *
     * Default implementation returns exact cardinality. Subclasses may
     * override to trade accuracy for speed.
     *
     * @return Approximate number of 1 bits
     */
    [[nodiscard]] virtual size_t approximateCardinality() const noexcept { return cardinality(); }

    /**
     * @brief Finds the next set bit starting from index (inclusive).
     * @param index Starting index
     * @return Index of next set bit, or NO_MORE_BITS if none found
     */
    [[nodiscard]] size_t nextSetBit(size_t index) const noexcept;

    /**
     * @brief Finds the previous set bit before or at index.
     * @param index Starting index
     * @return Index of previous set bit, or NO_MORE_BITS if none found
     */
    [[nodiscard]] size_t prevSetBit(size_t index) const noexcept;

    /**
     * @brief Performs bitwise OR: this |= other.
     * @param other The other BitSet (must have same length)
     */
    void OR(const BitSet& other) noexcept;

    /**
     * @brief Performs bitwise AND: this &= other.
     * @param other The other BitSet (must have same length)
     */
    void AND(const BitSet& other) noexcept;

    /**
     * @brief Performs bitwise ANDNOT: this &= ~other.
     * @param other The other BitSet (must have same length)
     */
    void ANDNOT(const BitSet& other) noexcept;

    /**
     * @brief Performs bitwise XOR: this ^= other.
     * @param other The other BitSet (must have same length)
     */
    void XOR(const BitSet& other) noexcept;

    /**
     * @brief Checks if this BitSet intersects with another.
     * @param other The other BitSet (must have same length)
     * @return true if any bit is set in both sets
     */
    [[nodiscard]] bool intersects(const BitSet& other) const noexcept;

    /**
     * @brief Provides direct access to the backing word array.
     * @return Pointer to the uint64_t array
     */
    [[nodiscard]] const uint64_t* getBits() const noexcept { return bits_.data(); }

    /**
     * @brief Provides mutable access to the backing word array.
     * @return Pointer to the uint64_t array
     * @warning Callers must maintain the "ghost bits clear" invariant
     */
    [[nodiscard]] uint64_t* getBits() noexcept { return bits_.data(); }

    /**
     * @brief Returns the number of words in the backing array.
     * @return Number of 64-bit words
     */
    [[nodiscard]] size_t numWords() const noexcept { return num_words_; }

    // Static utility methods

    /**
     * @brief Computes the number of 64-bit words needed for numBits.
     * @param numBits Number of bits
     * @return Number of words required
     */
    [[nodiscard]] static constexpr size_t bits2words(size_t numBits) noexcept {
        return numBits == 0 ? 0 : ((numBits - 1) >> 6) + 1;
    }

    /**
     * @brief Counts set bits in the intersection of two BitSets.
     * @param a First BitSet
     * @param b Second BitSet
     * @return Population count of (a & b)
     */
    [[nodiscard]] static size_t intersectionCount(const BitSet& a, const BitSet& b) noexcept;

    /**
     * @brief Counts set bits in the union of two BitSets.
     * @param a First BitSet
     * @param b Second BitSet
     * @return Population count of (a | b)
     */
    [[nodiscard]] static size_t unionCount(const BitSet& a, const BitSet& b) noexcept;

    /**
     * @brief Counts set bits in (a AND NOT b).
     * @param a First BitSet
     * @param b Second BitSet
     * @return Population count of (a & ~b)
     */
    [[nodiscard]] static size_t andNotCount(const BitSet& a, const BitSet& b) noexcept;

    /**
     * @brief Sentinel value indicating no more set bits.
     */
    static constexpr size_t NO_MORE_BITS = static_cast<size_t>(-1);

private:
    std::vector<uint64_t> bits_;  // Backing storage
    size_t num_bits_;             // Number of bits in use
    size_t num_words_;            // Number of words in use (<= bits_.size())

    // Verify that ghost bits (past numBits) are clear
    [[nodiscard]] bool verifyGhostBitsClear() const noexcept;
};

}  // namespace diagon::util
