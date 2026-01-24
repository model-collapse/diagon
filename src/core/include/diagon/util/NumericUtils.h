// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <cstring>

namespace diagon::util {

/**
 * @brief Helper functions to encode numeric values as sortable bytes.
 *
 * Based on: org.apache.lucene.util.NumericUtils
 *
 * Used for:
 * - Converting floating-point to sortable integer representation
 * - BKD tree indexing (point values)
 * - Numeric range queries
 * - Sorting numeric doc values
 *
 * Design decisions:
 * - IEEE 754 bit manipulation for sortable floats/doubles
 * - Sign bit flipping for proper sort order
 * - Zero-copy conversions using bit_cast (C++20)
 * - No precision loss in conversions
 *
 * Sort order semantics:
 * - Negative numbers < Zero < Positive numbers
 * - NaN is greater than positive infinity (per IEEE 754)
 *
 * Performance:
 * - All conversions are O(1) bit manipulations
 * - No floating-point arithmetic involved
 */
class NumericUtils {
public:
    // No instances
    NumericUtils() = delete;

    /**
     * @brief Converts a double to a sortable long representation.
     *
     * The conversion flips bits to ensure lexicographic byte order matches
     * numeric order. This allows doubles to be compared as longs.
     *
     * @param value The double value to convert
     * @return Sortable long representation
     * @see sortableLongToDouble
     */
    [[nodiscard]] static int64_t doubleToSortableLong(double value) noexcept;

    /**
     * @brief Converts a sortable long back to a double.
     *
     * @param encoded The sortable long from doubleToSortableLong
     * @return The original double value
     * @see doubleToSortableLong
     */
    [[nodiscard]] static double sortableLongToDouble(int64_t encoded) noexcept;

    /**
     * @brief Converts a float to a sortable int representation.
     *
     * The conversion flips bits to ensure lexicographic byte order matches
     * numeric order. This allows floats to be compared as ints.
     *
     * @param value The float value to convert
     * @return Sortable int representation
     * @see sortableIntToFloat
     */
    [[nodiscard]] static int32_t floatToSortableInt(float value) noexcept;

    /**
     * @brief Converts a sortable int back to a float.
     *
     * @param encoded The sortable int from floatToSortableInt
     * @return The original float value
     * @see floatToSortableInt
     */
    [[nodiscard]] static float sortableIntToFloat(int32_t encoded) noexcept;

    /**
     * @brief Converts IEEE 754 double bits to sortable order.
     *
     * Algorithm:
     * - If sign bit is 0 (positive), flip all bits
     * - If sign bit is 1 (negative), flip sign bit only
     *
     * This makes:
     * - Negative values: 0x0000... to 0x7FFF... (ascending)
     * - Positive values: 0x8000... to 0xFFFF... (ascending)
     *
     * @param bits IEEE 754 representation of double
     * @return Sortable bit pattern
     */
    [[nodiscard]] static int64_t sortableDoubleBits(int64_t bits) noexcept {
        return bits ^ ((bits >> 63) & 0x7FFFFFFFFFFFFFFFLL);
    }

    /**
     * @brief Converts IEEE 754 float bits to sortable order.
     *
     * Algorithm:
     * - If sign bit is 0 (positive), flip all bits
     * - If sign bit is 1 (negative), flip sign bit only
     *
     * @param bits IEEE 754 representation of float
     * @return Sortable bit pattern
     */
    [[nodiscard]] static int32_t sortableFloatBits(int32_t bits) noexcept {
        return bits ^ ((bits >> 31) & 0x7FFFFFFF);
    }

    /**
     * @brief Converts an integer to big-endian byte array.
     *
     * Used for BKD tree indexing and numeric sorting.
     *
     * @param value The integer value
     * @param dest Output buffer (must have at least 4 bytes)
     */
    static void intToBytesBE(int32_t value, uint8_t* dest) noexcept;

    /**
     * @brief Converts a long to big-endian byte array.
     *
     * Used for BKD tree indexing and numeric sorting.
     *
     * @param value The long value
     * @param dest Output buffer (must have at least 8 bytes)
     */
    static void longToBytesBE(int64_t value, uint8_t* dest) noexcept;

    /**
     * @brief Converts big-endian byte array to integer.
     *
     * @param src Source buffer (must have at least 4 bytes)
     * @return The integer value
     */
    [[nodiscard]] static int32_t bytesToIntBE(const uint8_t* src) noexcept;

    /**
     * @brief Converts big-endian byte array to long.
     *
     * @param src Source buffer (must have at least 8 bytes)
     * @return The long value
     */
    [[nodiscard]] static int64_t bytesToLongBE(const uint8_t* src) noexcept;

    /**
     * @brief Converts a float to big-endian sortable byte array.
     *
     * Combines floatToSortableInt and intToBytesBE.
     *
     * @param value The float value
     * @param dest Output buffer (must have at least 4 bytes)
     */
    static void floatToBytesBE(float value, uint8_t* dest) noexcept;

    /**
     * @brief Converts a double to big-endian sortable byte array.
     *
     * Combines doubleToSortableLong and longToBytesBE.
     *
     * @param value The double value
     * @param dest Output buffer (must have at least 8 bytes)
     */
    static void doubleToBytesBE(double value, uint8_t* dest) noexcept;

    /**
     * @brief Converts big-endian sortable bytes to float.
     *
     * @param src Source buffer (must have at least 4 bytes)
     * @return The float value
     */
    [[nodiscard]] static float bytesToFloatBE(const uint8_t* src) noexcept;

    /**
     * @brief Converts big-endian sortable bytes to double.
     *
     * @param src Source buffer (must have at least 8 bytes)
     * @return The double value
     */
    [[nodiscard]] static double bytesToDoubleBE(const uint8_t* src) noexcept;

private:
    // Endianness conversion helpers
    template <typename T>
    static T swapBytes(T value) noexcept;
};

} // namespace diagon::util
