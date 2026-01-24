// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/BytesRef.h"

#include <cassert>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace diagon::util {

namespace {

// MurmurHash3 implementation (32-bit)
// Based on: org.apache.lucene.util.StringHelper.murmurhash3_x86_32
constexpr uint32_t MURMUR3_SEED = 0x9747b28c;  // GOOD_FAST_HASH_SEED from Lucene

inline uint32_t rotl32(uint32_t x, int8_t r) {
    return (x << r) | (x >> (32 - r));
}

inline uint32_t fmix32(uint32_t h) {
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

uint32_t murmurhash3_x86_32(const uint8_t* data, size_t len, uint32_t seed) {
    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;

    uint32_t h1 = seed;

    // Body: process 4-byte blocks
    const size_t nblocks = len / 4;
    const uint32_t* blocks = reinterpret_cast<const uint32_t*>(data);

    for (size_t i = 0; i < nblocks; i++) {
        uint32_t k1 = blocks[i];

        k1 *= c1;
        k1 = rotl32(k1, 15);
        k1 *= c2;

        h1 ^= k1;
        h1 = rotl32(h1, 13);
        h1 = h1 * 5 + 0xe6546b64;
    }

    // Tail: process remaining bytes
    const uint8_t* tail = data + nblocks * 4;
    uint32_t k1 = 0;

    switch (len & 3) {
    case 3:
        k1 ^= tail[2] << 16;
        [[fallthrough]];
    case 2:
        k1 ^= tail[1] << 8;
        [[fallthrough]];
    case 1:
        k1 ^= tail[0];
        k1 *= c1;
        k1 = rotl32(k1, 15);
        k1 *= c2;
        h1 ^= k1;
    }

    // Finalization
    h1 ^= static_cast<uint32_t>(len);
    h1 = fmix32(h1);

    return h1;
}

// Simple UTF-8 validation
bool isValidUTF8(const uint8_t* data, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint8_t byte = data[i];

        if ((byte & 0x80) == 0) {
            // 1-byte character (ASCII)
            i++;
        } else if ((byte & 0xE0) == 0xC0) {
            // 2-byte character
            if (i + 1 >= len || (data[i + 1] & 0xC0) != 0x80) return false;
            i += 2;
        } else if ((byte & 0xF0) == 0xE0) {
            // 3-byte character
            if (i + 2 >= len || (data[i + 1] & 0xC0) != 0x80 ||
                (data[i + 2] & 0xC0) != 0x80)
                return false;
            i += 3;
        } else if ((byte & 0xF8) == 0xF0) {
            // 4-byte character
            if (i + 3 >= len || (data[i + 1] & 0xC0) != 0x80 ||
                (data[i + 2] & 0xC0) != 0x80 || (data[i + 3] & 0xC0) != 0x80)
                return false;
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

} // anonymous namespace

BytesRef::BytesRef(size_t capacity)
    : owned_storage_(std::make_shared<std::vector<uint8_t>>(capacity)) {
    data_ = std::span<const uint8_t>(*owned_storage_);
}

BytesRef::BytesRef(const uint8_t* data, size_t offset, size_t length) noexcept
    : data_(data + offset, length) {
    assert(data != nullptr || length == 0);
}

BytesRef::BytesRef(const uint8_t* data, size_t length) noexcept
    : data_(data, length) {
    assert(data != nullptr || length == 0);
}

BytesRef::BytesRef(const std::vector<uint8_t>& vec) noexcept
    : data_(vec.data(), vec.size()) {}

BytesRef::BytesRef(std::string_view text)
    : owned_storage_(std::make_shared<std::vector<uint8_t>>(stringToUTF8(text))) {
    data_ = std::span<const uint8_t>(*owned_storage_);
}

BytesRef::BytesRef(std::span<const uint8_t> span) noexcept : data_(span) {}

BytesRef BytesRef::deepCopy() const {
    if (empty()) {
        return BytesRef();
    }

    auto copy = std::make_shared<std::vector<uint8_t>>(data_.begin(), data_.end());
    BytesRef result;
    result.owned_storage_ = std::move(copy);
    result.data_ = std::span<const uint8_t>(*result.owned_storage_);
    return result;
}

std::string BytesRef::utf8ToString() const {
    if (empty()) {
        return "";
    }

    if (!isValidUTF8(data_.data(), data_.size())) {
        throw std::runtime_error("Invalid UTF-8 byte sequence");
    }

    return std::string(reinterpret_cast<const char*>(data_.data()), data_.size());
}

std::string BytesRef::toString() const {
    if (empty()) {
        return "[]";
    }

    std::ostringstream oss;
    oss << '[';
    for (size_t i = 0; i < data_.size(); i++) {
        if (i > 0) {
            oss << ' ';
        }
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<unsigned>(data_[i]);
    }
    oss << ']';
    return oss.str();
}

bool BytesRef::equals(const BytesRef& other) const noexcept {
    if (size() != other.size()) {
        return false;
    }
    return std::memcmp(data(), other.data(), size()) == 0;
}

int BytesRef::compareTo(const BytesRef& other) const noexcept {
    const size_t min_len = std::min(size(), other.size());

    const int cmp = std::memcmp(data(), other.data(), min_len);
    if (cmp != 0) {
        return cmp < 0 ? -1 : 1;
    }

    // If prefixes are equal, shorter comes first
    if (size() < other.size()) {
        return -1;
    } else if (size() > other.size()) {
        return 1;
    }
    return 0;
}

size_t BytesRef::hashCode() const noexcept {
    if (empty()) {
        return 0;
    }
    return murmurhash3_x86_32(data(), size(), MURMUR3_SEED);
}

BytesRef BytesRef::slice(size_t offset, size_t length) const {
    assert(offset + length <= size());
    return BytesRef(data() + offset, length);
}

std::vector<uint8_t> BytesRef::stringToUTF8(std::string_view text) {
    // In C++, std::string is already UTF-8 encoded on most platforms
    // For proper UTF-8 conversion from UTF-16/32, we'd need a library like ICU
    // For now, assume input is already UTF-8
    std::vector<uint8_t> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return bytes;
}

} // namespace diagon::util
