// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/CityHash.h"

#include <cstring>

namespace diagon {
namespace util {

// Simple 64-bit hash mixing functions
// Based on MurmurHash64 mixing functions

namespace {

// Simple 64-bit hash (MurmurHash64-like)
uint64_t simpleHash64(const char* data, size_t len, uint64_t seed) {
    const uint64_t m = 0xc6a4a7935bd1e995ULL;
    const int r = 47;

    uint64_t h = seed ^ (len * m);

    const uint8_t* data8 = reinterpret_cast<const uint8_t*>(data);
    const uint8_t* end = data8 + (len / 8) * 8;

    while (data8 != end) {
        uint64_t k;
        std::memcpy(&k, data8, sizeof(k));

        k *= m;
        k ^= k >> r;
        k *= m;

        h ^= k;
        h *= m;

        data8 += 8;
    }

    // Handle remaining bytes
    const uint8_t* remaining = data8;
    switch (len & 7) {
        case 7:
            h ^= uint64_t(remaining[6]) << 48;
            [[fallthrough]];
        case 6:
            h ^= uint64_t(remaining[5]) << 40;
            [[fallthrough]];
        case 5:
            h ^= uint64_t(remaining[4]) << 32;
            [[fallthrough]];
        case 4:
            h ^= uint64_t(remaining[3]) << 24;
            [[fallthrough]];
        case 3:
            h ^= uint64_t(remaining[2]) << 16;
            [[fallthrough]];
        case 2:
            h ^= uint64_t(remaining[1]) << 8;
            [[fallthrough]];
        case 1:
            h ^= uint64_t(remaining[0]);
            h *= m;
    }

    h ^= h >> r;
    h *= m;
    h ^= h >> r;

    return h;
}

}  // anonymous namespace

// ==================== Public API ====================

uint64_t CityHash64(const char* data, size_t len) {
    // Use simple hash with fixed seed
    return simpleHash64(data, len, 0x9ae16a3b2f90404fULL);
}

uint64_t CityHash64WithSeed(const char* data, size_t len, uint64_t seed) {
    return simpleHash64(data, len, seed);
}

std::pair<uint64_t, uint64_t> CityHash128(const char* data, size_t len) {
    // Simple 128-bit hash: compute two independent 64-bit hashes
    uint64_t h1 = simpleHash64(data, len, 0x9ae16a3b2f90404fULL);
    uint64_t h2 = simpleHash64(data, len, 0x85ebca6b85ebca6bULL);
    return {h1, h2};
}

}  // namespace util
}  // namespace diagon
