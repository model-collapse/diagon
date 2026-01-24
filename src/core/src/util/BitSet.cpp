// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/BitSet.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstring>

namespace diagon::util {

BitSet::BitSet(size_t numBits)
    : bits_(bits2words(numBits), 0)
    , num_bits_(numBits)
    , num_words_(bits2words(numBits)) {}

BitSet::BitSet(std::vector<uint64_t> words, size_t numBits)
    : bits_(std::move(words))
    , num_bits_(numBits)
    , num_words_(bits2words(numBits)) {
    assert(bits_.size() >= num_words_);
    assert(verifyGhostBitsClear());
}

std::unique_ptr<BitSet> BitSet::clone() const {
    return std::make_unique<BitSet>(bits_, num_bits_);
}

bool BitSet::get(size_t index) const noexcept {
    assert(index < num_bits_);
    const size_t wordIndex = index >> 6;  // index / 64
    const size_t bitIndex = index & 63;   // index % 64
    return (bits_[wordIndex] & (1ULL << bitIndex)) != 0;
}

void BitSet::set(size_t index) noexcept {
    assert(index < num_bits_);
    const size_t wordIndex = index >> 6;
    const size_t bitIndex = index & 63;
    bits_[wordIndex] |= (1ULL << bitIndex);
}

bool BitSet::getAndSet(size_t index) noexcept {
    assert(index < num_bits_);
    const size_t wordIndex = index >> 6;
    const size_t bitIndex = index & 63;
    const uint64_t mask = 1ULL << bitIndex;
    const bool previous = (bits_[wordIndex] & mask) != 0;
    bits_[wordIndex] |= mask;
    return previous;
}

void BitSet::clear(size_t index) noexcept {
    assert(index < num_bits_);
    const size_t wordIndex = index >> 6;
    const size_t bitIndex = index & 63;
    bits_[wordIndex] &= ~(1ULL << bitIndex);
}

void BitSet::clear(size_t startIndex, size_t endIndex) noexcept {
    assert(startIndex <= endIndex);
    assert(endIndex <= num_bits_);

    if (startIndex == endIndex) {
        return;
    }

    const size_t startWord = startIndex >> 6;
    const size_t endWord = (endIndex - 1) >> 6;

    // Create masks for partial words
    const uint64_t startMask = ~((1ULL << (startIndex & 63)) - 1);
    const uint64_t endMask = (1ULL << ((endIndex - 1) & 63)) |
                             ((1ULL << ((endIndex - 1) & 63)) - 1);

    if (startWord == endWord) {
        // Range within a single word
        bits_[startWord] &= ~(startMask & endMask);
    } else {
        // Clear partial start word
        bits_[startWord] &= ~startMask;

        // Clear complete words
        for (size_t i = startWord + 1; i < endWord; i++) {
            bits_[i] = 0;
        }

        // Clear partial end word
        bits_[endWord] &= ~endMask;
    }
}

void BitSet::clear() noexcept {
    std::fill(bits_.begin(), bits_.end(), 0);
}

size_t BitSet::cardinality() const noexcept {
    size_t count = 0;
    for (size_t i = 0; i < num_words_; i++) {
        count += std::popcount(bits_[i]);
    }
    return count;
}

size_t BitSet::nextSetBit(size_t index) const noexcept {
    if (index >= num_bits_) {
        return NO_MORE_BITS;
    }

    size_t wordIndex = index >> 6;
    size_t bitIndex = index & 63;

    // Check remaining bits in current word
    uint64_t word = bits_[wordIndex] >> bitIndex;
    if (word != 0) {
        return index + std::countr_zero(word);
    }

    // Scan subsequent words
    for (wordIndex++; wordIndex < num_words_; wordIndex++) {
        word = bits_[wordIndex];
        if (word != 0) {
            const size_t result = (wordIndex << 6) + std::countr_zero(word);
            return result < num_bits_ ? result : NO_MORE_BITS;
        }
    }

    return NO_MORE_BITS;
}

size_t BitSet::prevSetBit(size_t index) const noexcept {
    if (index >= num_bits_) {
        index = num_bits_ - 1;
    }

    const size_t wordIndex = index >> 6;
    const size_t bitIndex = index & 63;

    // Check current word up to bitIndex
    uint64_t word = bits_[wordIndex] & ((1ULL << (bitIndex + 1)) - 1);
    if (word != 0) {
        return (wordIndex << 6) + (63 - std::countl_zero(word));
    }

    // Scan previous words
    for (size_t i = wordIndex; i > 0; i--) {
        word = bits_[i - 1];
        if (word != 0) {
            return ((i - 1) << 6) + (63 - std::countl_zero(word));
        }
    }

    return NO_MORE_BITS;
}

void BitSet::OR(const BitSet& other) noexcept {
    assert(num_bits_ == other.num_bits_);
    const size_t minWords = std::min(num_words_, other.num_words_);
    for (size_t i = 0; i < minWords; i++) {
        bits_[i] |= other.bits_[i];
    }
}

void BitSet::AND(const BitSet& other) noexcept {
    assert(num_bits_ == other.num_bits_);
    const size_t minWords = std::min(num_words_, other.num_words_);
    for (size_t i = 0; i < minWords; i++) {
        bits_[i] &= other.bits_[i];
    }
    // Clear remaining words
    for (size_t i = minWords; i < num_words_; i++) {
        bits_[i] = 0;
    }
}

void BitSet::ANDNOT(const BitSet& other) noexcept {
    assert(num_bits_ == other.num_bits_);
    const size_t minWords = std::min(num_words_, other.num_words_);
    for (size_t i = 0; i < minWords; i++) {
        bits_[i] &= ~other.bits_[i];
    }
}

void BitSet::XOR(const BitSet& other) noexcept {
    assert(num_bits_ == other.num_bits_);
    const size_t minWords = std::min(num_words_, other.num_words_);
    for (size_t i = 0; i < minWords; i++) {
        bits_[i] ^= other.bits_[i];
    }
}

bool BitSet::intersects(const BitSet& other) const noexcept {
    assert(num_bits_ == other.num_bits_);
    const size_t minWords = std::min(num_words_, other.num_words_);
    for (size_t i = 0; i < minWords; i++) {
        if ((bits_[i] & other.bits_[i]) != 0) {
            return true;
        }
    }
    return false;
}

size_t BitSet::intersectionCount(const BitSet& a, const BitSet& b) noexcept {
    size_t count = 0;
    const size_t minWords = std::min(a.num_words_, b.num_words_);
    for (size_t i = 0; i < minWords; i++) {
        count += std::popcount(a.bits_[i] & b.bits_[i]);
    }
    return count;
}

size_t BitSet::unionCount(const BitSet& a, const BitSet& b) noexcept {
    size_t count = 0;
    const size_t minWords = std::min(a.num_words_, b.num_words_);

    // Count common words
    for (size_t i = 0; i < minWords; i++) {
        count += std::popcount(a.bits_[i] | b.bits_[i]);
    }

    // Count remaining words from a
    for (size_t i = minWords; i < a.num_words_; i++) {
        count += std::popcount(a.bits_[i]);
    }

    // Count remaining words from b
    for (size_t i = minWords; i < b.num_words_; i++) {
        count += std::popcount(b.bits_[i]);
    }

    return count;
}

size_t BitSet::andNotCount(const BitSet& a, const BitSet& b) noexcept {
    size_t count = 0;
    const size_t minWords = std::min(a.num_words_, b.num_words_);

    // Count common words
    for (size_t i = 0; i < minWords; i++) {
        count += std::popcount(a.bits_[i] & ~b.bits_[i]);
    }

    // Count remaining words from a
    for (size_t i = minWords; i < a.num_words_; i++) {
        count += std::popcount(a.bits_[i]);
    }

    return count;
}

bool BitSet::verifyGhostBitsClear() const noexcept {
    // Check words past num_words_
    for (size_t i = num_words_; i < bits_.size(); i++) {
        if (bits_[i] != 0) {
            return false;
        }
    }

    // Check high bits in the last word (if not word-aligned)
    if ((num_bits_ & 63) == 0) {
        return true;
    }

    const uint64_t mask = ~0ULL << (num_bits_ & 63);
    return (bits_[num_words_ - 1] & mask) == 0;
}

}  // namespace diagon::util
