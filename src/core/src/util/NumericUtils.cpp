// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/NumericUtils.h"

#include <bit>
#include <cstring>

#if defined(_MSC_VER)
#include <stdlib.h>
#define bswap_32(x) _byteswap_ulong(x)
#define bswap_64(x) _byteswap_uint64(x)
#elif defined(__GNUC__) || defined(__clang__)
#define bswap_32(x) __builtin_bswap32(x)
#define bswap_64(x) __builtin_bswap64(x)
#else
// Fallback implementation
static inline uint32_t bswap_32(uint32_t x) {
    return ((x & 0x000000FFU) << 24) | ((x & 0x0000FF00U) << 8) |
           ((x & 0x00FF0000U) >> 8) | ((x & 0xFF000000U) >> 24);
}

static inline uint64_t bswap_64(uint64_t x) {
    return ((x & 0x00000000000000FFULL) << 56) | ((x & 0x000000000000FF00ULL) << 40) |
           ((x & 0x0000000000FF0000ULL) << 24) | ((x & 0x00000000FF000000ULL) << 8) |
           ((x & 0x000000FF00000000ULL) >> 8) | ((x & 0x0000FF0000000000ULL) >> 24) |
           ((x & 0x00FF000000000000ULL) >> 40) | ((x & 0xFF00000000000000ULL) >> 56);
}
#endif

namespace diagon::util {

int64_t NumericUtils::doubleToSortableLong(double value) noexcept {
    const int64_t bits = std::bit_cast<int64_t>(value);
    return sortableDoubleBits(bits);
}

double NumericUtils::sortableLongToDouble(int64_t encoded) noexcept {
    const int64_t bits = sortableDoubleBits(encoded);
    return std::bit_cast<double>(bits);
}

int32_t NumericUtils::floatToSortableInt(float value) noexcept {
    const int32_t bits = std::bit_cast<int32_t>(value);
    return sortableFloatBits(bits);
}

float NumericUtils::sortableIntToFloat(int32_t encoded) noexcept {
    const int32_t bits = sortableFloatBits(encoded);
    return std::bit_cast<float>(bits);
}

void NumericUtils::intToBytesBE(int32_t value, uint8_t* dest) noexcept {
    uint32_t uvalue = static_cast<uint32_t>(value);
    if constexpr (std::endian::native == std::endian::little) {
        uvalue = bswap_32(uvalue);
    }
    std::memcpy(dest, &uvalue, sizeof(uvalue));
}

void NumericUtils::longToBytesBE(int64_t value, uint8_t* dest) noexcept {
    uint64_t uvalue = static_cast<uint64_t>(value);
    if constexpr (std::endian::native == std::endian::little) {
        uvalue = bswap_64(uvalue);
    }
    std::memcpy(dest, &uvalue, sizeof(uvalue));
}

int32_t NumericUtils::bytesToIntBE(const uint8_t* src) noexcept {
    uint32_t uvalue;
    std::memcpy(&uvalue, src, sizeof(uvalue));
    if constexpr (std::endian::native == std::endian::little) {
        uvalue = bswap_32(uvalue);
    }
    return static_cast<int32_t>(uvalue);
}

int64_t NumericUtils::bytesToLongBE(const uint8_t* src) noexcept {
    uint64_t uvalue;
    std::memcpy(&uvalue, src, sizeof(uvalue));
    if constexpr (std::endian::native == std::endian::little) {
        uvalue = bswap_64(uvalue);
    }
    return static_cast<int64_t>(uvalue);
}

void NumericUtils::floatToBytesBE(float value, uint8_t* dest) noexcept {
    const int32_t sortable = floatToSortableInt(value);
    intToBytesBE(sortable, dest);
}

void NumericUtils::doubleToBytesBE(double value, uint8_t* dest) noexcept {
    const int64_t sortable = doubleToSortableLong(value);
    longToBytesBE(sortable, dest);
}

float NumericUtils::bytesToFloatBE(const uint8_t* src) noexcept {
    const int32_t sortable = bytesToIntBE(src);
    return sortableIntToFloat(sortable);
}

double NumericUtils::bytesToDoubleBE(const uint8_t* src) noexcept {
    const int64_t sortable = bytesToLongBE(src);
    return sortableLongToDouble(sortable);
}

} // namespace diagon::util
