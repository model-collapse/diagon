// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>

namespace diagon {
namespace columns {

/**
 * Type index for fast type identification
 *
 * Based on: ClickHouse TypeIndex
 */
enum class TypeIndex : uint8_t {
    Nothing = 0,

    // Integers
    UInt8,
    UInt16,
    UInt32,
    UInt64,
    Int8,
    Int16,
    Int32,
    Int64,

    // Floating point
    Float32,
    Float64,

    // String
    String,
    FixedString,

    // Complex types
    Array,
    Tuple,
    Nullable,

    // Special
    Function,
    AggregateFunction,

    // Date/Time
    Date,
    DateTime,
    DateTime64,

    // Decimal
    Decimal32,
    Decimal64,
    Decimal128,
    Decimal256,

    // Misc
    UUID,
    IPv4,
    IPv6,
};

}  // namespace columns
}  // namespace diagon
