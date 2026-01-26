// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace diagon {
namespace util {

/**
 * BloomFilter - Space-efficient probabilistic data structure
 *
 * Based on ClickHouse's BloomFilter implementation with double hashing.
 *
 * Key features:
 * - Probabilistic membership testing (can have false positives, never false negatives)
 * - Space efficient: O(m) bits where m = size_bytes * 8
 * - Fast operations: O(k) where k = num_hashes
 * - Double hashing: pos = (hash1 + hash2 * i + i^2) % (8 * size)
 *
 * Use cases:
 * - Skip indexes for equality checks (WHERE col = value)
 * - Duplicate detection
 * - Cache filters
 *
 * False positive rate (approximately):
 *   p ≈ (1 - e^(-k*n/m))^k
 * where:
 *   k = number of hash functions
 *   n = number of inserted elements
 *   m = number of bits (8 * size_bytes)
 *
 * Optimal k for given n and m:
 *   k = (m/n) * ln(2)
 *
 * Example configuration:
 * - 8 bits per element, 3 hash functions → ~2% false positive rate
 * - 16 bits per element, 11 hash functions → ~0.01% false positive rate
 */
class BloomFilter {
public:
    using Word = uint64_t;
    using Container = std::vector<Word>;

    // ==================== Construction ====================

    /**
     * Create bloom filter with specified parameters
     *
     * @param size_bytes Size of filter in bytes (will be rounded up to word boundary)
     * @param num_hashes Number of hash functions (typically 3-11)
     * @param seed Random seed for hash function generation
     *
     * @throws std::invalid_argument if size_bytes or num_hashes is 0
     */
    BloomFilter(size_t size_bytes, size_t num_hashes, uint64_t seed = 0);

    ~BloomFilter() = default;

    // Move-only type
    BloomFilter(BloomFilter&&) = default;
    BloomFilter& operator=(BloomFilter&&) = default;
    BloomFilter(const BloomFilter&) = delete;
    BloomFilter& operator=(const BloomFilter&) = delete;

    // ==================== Membership Operations ====================

    /**
     * Add element to bloom filter
     *
     * @param data Pointer to data bytes
     * @param len Length in bytes
     */
    void add(const char* data, size_t len);

    /**
     * Add pre-computed hash value
     *
     * Useful when hash is already computed for other purposes.
     *
     * @param hash Pre-computed hash value
     */
    void addHash(uint64_t hash);

    /**
     * Check if element might be in the set
     *
     * @param data Pointer to data bytes
     * @param len Length in bytes
     * @return true if element MIGHT be in set (false positive possible)
     *         false if element is DEFINITELY NOT in set (never false negative)
     */
    bool contains(const char* data, size_t len) const;

    /**
     * Check if pre-computed hash might be in the set
     *
     * @param hash Pre-computed hash value
     * @return true if hash MIGHT be in set, false if DEFINITELY NOT
     */
    bool containsHash(uint64_t hash) const;

    /**
     * Clear all bits (reset filter to empty state)
     */
    void clear();

    // ==================== Set Operations ====================

    /**
     * Check if this filter contains all elements from another filter
     *
     * Filters must have same size and seed.
     *
     * @param other Another bloom filter
     * @return true if this ⊇ other (this contains everything from other)
     */
    bool containsAll(const BloomFilter& other) const;

    /**
     * Merge another bloom filter into this one (union operation)
     *
     * Filters must have same size and seed.
     * After merge, this filter will return true for all elements
     * that were in either filter.
     *
     * @param other Another bloom filter to merge
     */
    void merge(const BloomFilter& other);

    // ==================== Properties ====================

    /**
     * Check if filter has no elements (all bits are 0)
     */
    bool empty() const;

    /**
     * Get size in bytes
     */
    size_t sizeBytes() const { return size_bytes_; }

    /**
     * Get number of hash functions
     */
    size_t numHashes() const { return num_hashes_; }

    /**
     * Get seed value
     */
    uint64_t seed() const { return seed_; }

    /**
     * Memory usage in bytes (includes internal overhead)
     */
    size_t memoryUsageBytes() const;

    /**
     * Get raw bit vector (for serialization)
     */
    const Container& data() const { return filter_; }

    /**
     * Get mutable raw bit vector (for deserialization)
     */
    Container& data() { return filter_; }

    // ==================== Statistics ====================

    /**
     * Count number of set bits (popcount)
     *
     * Useful for estimating filter saturation.
     */
    size_t popcount() const;

    /**
     * Estimate false positive rate based on current saturation
     *
     * Uses formula: p ≈ (set_bits / total_bits)^k
     * where k = num_hashes
     */
    double estimateFalsePositiveRate() const;

    // ==================== Comparison ====================

    friend bool operator==(const BloomFilter& a, const BloomFilter& b);
    friend bool operator!=(const BloomFilter& a, const BloomFilter& b) {
        return !(a == b);
    }

private:
    // Constants for seed generation
    static constexpr uint64_t SEED_GEN_A = 845897321ULL;
    static constexpr uint64_t SEED_GEN_B = 217728422ULL;
    static constexpr size_t WORD_BITS = 8 * sizeof(Word);

    // Parameters
    size_t size_bytes_;     // Size in bytes
    size_t num_hashes_;     // Number of hash functions
    uint64_t seed_;         // Random seed

    // Derived values
    size_t num_words_;      // Number of 64-bit words
    size_t num_bits_;       // Total number of bits (8 * size_bytes)

    // Bit vector
    Container filter_;

    /**
     * Compute bit positions for an element
     *
     * Uses double hashing: pos_i = (hash1 + hash2 * i + i^2) % num_bits
     *
     * @param data Element data
     * @param len Element length
     * @param positions Output array of size num_hashes (pre-allocated)
     */
    void computePositions(const char* data, size_t len, size_t* positions) const;

    /**
     * Compute bit positions from pre-computed hash
     */
    void computePositionsFromHash(uint64_t hash, size_t* positions) const;

    /**
     * Set bit at position
     */
    inline void setBit(size_t pos) {
        filter_[pos / WORD_BITS] |= (1ULL << (pos % WORD_BITS));
    }

    /**
     * Check if bit at position is set
     */
    inline bool testBit(size_t pos) const {
        return (filter_[pos / WORD_BITS] & (1ULL << (pos % WORD_BITS))) != 0;
    }
};

using BloomFilterPtr = std::shared_ptr<BloomFilter>;

} // namespace util
} // namespace diagon
