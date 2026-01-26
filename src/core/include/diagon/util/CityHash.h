// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

namespace diagon {
namespace util {

/**
 * CityHash - Fast non-cryptographic hash functions
 *
 * These are simplified wrappers around CityHash algorithms.
 * For production, link against Google's CityHash library.
 *
 * References:
 * - https://github.com/google/cityhash
 * - ClickHouse uses CityHash for bloom filters and skip indexes
 */

/**
 * Compute 64-bit CityHash
 *
 * @param data Input data
 * @param len Length in bytes
 * @return 64-bit hash value
 */
uint64_t CityHash64(const char* data, size_t len);

/**
 * Compute 64-bit CityHash with seed
 *
 * @param data Input data
 * @param len Length in bytes
 * @param seed Seed value for hash personalization
 * @return 64-bit hash value
 */
uint64_t CityHash64WithSeed(const char* data, size_t len, uint64_t seed);

/**
 * Compute 128-bit CityHash
 *
 * @param data Input data
 * @param len Length in bytes
 * @return Pair of 64-bit values representing 128-bit hash
 */
std::pair<uint64_t, uint64_t> CityHash128(const char* data, size_t len);

} // namespace util
} // namespace diagon
