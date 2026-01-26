// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/BloomFilter.h"
#include "diagon/util/CityHash.h"

#include <stdexcept>
#include <bit>  // std::popcount (C++20)

namespace diagon {
namespace util {

// Maximum bloom filter size: 1GB
static constexpr size_t MAX_BLOOM_FILTER_SIZE = 1ULL << 30;

// ==================== Construction ====================

BloomFilter::BloomFilter(size_t size_bytes, size_t num_hashes, uint64_t seed)
    : size_bytes_(size_bytes)
    , num_hashes_(num_hashes)
    , seed_(seed)
    , num_words_((size_bytes + sizeof(Word) - 1) / sizeof(Word))
    , num_bits_(size_bytes * 8)
    , filter_(num_words_, 0) {

    if (size_bytes == 0) {
        throw std::invalid_argument("BloomFilter size cannot be zero");
    }
    if (num_hashes == 0) {
        throw std::invalid_argument("BloomFilter number of hashes cannot be zero");
    }
    if (size_bytes > MAX_BLOOM_FILTER_SIZE) {
        throw std::invalid_argument("BloomFilter size cannot exceed 1GB");
    }
}

// ==================== Membership Operations ====================

void BloomFilter::add(const char* data, size_t len) {
    // Compute hash positions
    size_t positions[32];  // Stack allocation for common case
    std::vector<size_t> heap_positions;

    size_t* pos_array = positions;
    if (num_hashes_ > 32) {
        heap_positions.resize(num_hashes_);
        pos_array = heap_positions.data();
    }

    computePositions(data, len, pos_array);

    // Set all bits
    for (size_t i = 0; i < num_hashes_; ++i) {
        setBit(pos_array[i]);
    }
}

void BloomFilter::addHash(uint64_t hash) {
    // Compute hash positions
    size_t positions[32];
    std::vector<size_t> heap_positions;

    size_t* pos_array = positions;
    if (num_hashes_ > 32) {
        heap_positions.resize(num_hashes_);
        pos_array = heap_positions.data();
    }

    computePositionsFromHash(hash, pos_array);

    // Set all bits
    for (size_t i = 0; i < num_hashes_; ++i) {
        setBit(pos_array[i]);
    }
}

bool BloomFilter::contains(const char* data, size_t len) const {
    // Compute hash positions
    size_t positions[32];
    std::vector<size_t> heap_positions;

    size_t* pos_array = positions;
    if (num_hashes_ > 32) {
        heap_positions.resize(num_hashes_);
        pos_array = heap_positions.data();
    }

    computePositions(data, len, pos_array);

    // Check all bits
    for (size_t i = 0; i < num_hashes_; ++i) {
        if (!testBit(pos_array[i])) {
            return false;  // Definitely not in set
        }
    }

    return true;  // Might be in set
}

bool BloomFilter::containsHash(uint64_t hash) const {
    // Compute hash positions
    size_t positions[32];
    std::vector<size_t> heap_positions;

    size_t* pos_array = positions;
    if (num_hashes_ > 32) {
        heap_positions.resize(num_hashes_);
        pos_array = heap_positions.data();
    }

    computePositionsFromHash(hash, pos_array);

    // Check all bits
    for (size_t i = 0; i < num_hashes_; ++i) {
        if (!testBit(pos_array[i])) {
            return false;
        }
    }

    return true;
}

void BloomFilter::clear() {
    std::fill(filter_.begin(), filter_.end(), 0);
}

// ==================== Set Operations ====================

bool BloomFilter::containsAll(const BloomFilter& other) const {
    if (size_bytes_ != other.size_bytes_ || seed_ != other.seed_) {
        throw std::invalid_argument(
            "BloomFilter::containsAll requires filters with same size and seed");
    }

    // Check if this ⊇ other
    // For each word, (this & other) must equal other
    for (size_t i = 0; i < num_words_; ++i) {
        if ((filter_[i] & other.filter_[i]) != other.filter_[i]) {
            return false;
        }
    }

    return true;
}

void BloomFilter::merge(const BloomFilter& other) {
    if (size_bytes_ != other.size_bytes_ || seed_ != other.seed_) {
        throw std::invalid_argument(
            "BloomFilter::merge requires filters with same size and seed");
    }

    // Bitwise OR each word
    for (size_t i = 0; i < num_words_; ++i) {
        filter_[i] |= other.filter_[i];
    }
}

// ==================== Properties ====================

bool BloomFilter::empty() const {
    for (size_t i = 0; i < num_words_; ++i) {
        if (filter_[i] != 0) {
            return false;
        }
    }
    return true;
}

size_t BloomFilter::memoryUsageBytes() const {
    return filter_.capacity() * sizeof(Word);
}

// ==================== Statistics ====================

size_t BloomFilter::popcount() const {
    size_t count = 0;
    for (size_t i = 0; i < num_words_; ++i) {
        count += std::popcount(filter_[i]);
    }
    return count;
}

double BloomFilter::estimateFalsePositiveRate() const {
    // False positive rate ≈ (fraction of set bits)^k
    double fraction_set = static_cast<double>(popcount()) / num_bits_;

    // Compute fraction_set^num_hashes
    double fpr = 1.0;
    for (size_t i = 0; i < num_hashes_; ++i) {
        fpr *= fraction_set;
    }

    return fpr;
}

// ==================== Comparison ====================

bool operator==(const BloomFilter& a, const BloomFilter& b) {
    if (a.size_bytes_ != b.size_bytes_ ||
        a.num_hashes_ != b.num_hashes_ ||
        a.seed_ != b.seed_) {
        return false;
    }

    return a.filter_ == b.filter_;
}

// ==================== Private Helpers ====================

void BloomFilter::computePositions(const char* data, size_t len, size_t* positions) const {
    // Compute two independent hashes using CityHash
    uint64_t hash1 = CityHash64WithSeed(data, len, seed_);
    uint64_t hash2 = CityHash64WithSeed(data, len, SEED_GEN_A * seed_ + SEED_GEN_B);

    // Double hashing: pos_i = (hash1 + hash2 * i + i^2) % num_bits
    uint64_t acc = hash1;
    for (size_t i = 0; i < num_hashes_; ++i) {
        positions[i] = (acc + i * i) % num_bits_;
        acc += hash2;
    }
}

void BloomFilter::computePositionsFromHash(uint64_t hash, size_t* positions) const {
    // Generate second hash from first
    uint64_t hash1 = hash;
    uint64_t hash2 = CityHash64WithSeed(
        reinterpret_cast<const char*>(&hash),
        sizeof(hash),
        SEED_GEN_A * seed_ + SEED_GEN_B);

    // Double hashing
    uint64_t acc = hash1;
    for (size_t i = 0; i < num_hashes_; ++i) {
        positions[i] = (acc + i * i) % num_bits_;
        acc += hash2;
    }
}

} // namespace util
} // namespace diagon
