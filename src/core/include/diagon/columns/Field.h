// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace diagon {
namespace columns {

// Forward declaration for Array
class Field;

/**
 * Array of Field values
 */
using Array = std::vector<Field>;

/**
 * Field is a variant type that can hold any column value.
 *
 * Based on: ClickHouse Field
 *
 * NOTE: Simplified version - full ClickHouse Field includes Decimal, Tuple, etc.
 */
class Field {
public:
    using FieldVariant = std::variant<
        std::monostate,    // NULL/Nothing
        uint64_t,          // UInt64
        int64_t,           // Int64
        float,             // Float32
        double,            // Float64
        std::string,       // String
        Array              // Array
    >;

    // ==================== Construction ====================

    Field() : value_(std::monostate{}) {}

    Field(uint64_t value) : value_(value) {}
    Field(int64_t value) : value_(value) {}
    Field(uint32_t value) : value_(static_cast<uint64_t>(value)) {}
    Field(int32_t value) : value_(static_cast<int64_t>(value)) {}
    Field(uint16_t value) : value_(static_cast<uint64_t>(value)) {}
    Field(int16_t value) : value_(static_cast<int64_t>(value)) {}
    Field(uint8_t value) : value_(static_cast<uint64_t>(value)) {}
    Field(int8_t value) : value_(static_cast<int64_t>(value)) {}
    Field(float value) : value_(value) {}
    Field(double value) : value_(value) {}
    Field(const std::string& value) : value_(value) {}
    Field(std::string&& value) : value_(std::move(value)) {}
    Field(const char* value) : value_(std::string(value)) {}
    Field(const Array& value) : value_(value) {}
    Field(Array&& value) : value_(std::move(value)) {}

    // ==================== Type Checking ====================

    bool isNull() const {
        return std::holds_alternative<std::monostate>(value_);
    }

    bool isUInt() const {
        return std::holds_alternative<uint64_t>(value_);
    }

    bool isInt() const {
        return std::holds_alternative<int64_t>(value_);
    }

    bool isFloat() const {
        return std::holds_alternative<float>(value_);
    }

    bool isDouble() const {
        return std::holds_alternative<double>(value_);
    }

    bool isString() const {
        return std::holds_alternative<std::string>(value_);
    }

    bool isArray() const {
        return std::holds_alternative<Array>(value_);
    }

    // ==================== Value Access ====================

    template <typename T>
    T get() const {
        return std::get<T>(value_);
    }

    template <typename T>
    const T* tryGet() const {
        return std::get_if<T>(&value_);
    }

    // ==================== Comparison ====================

    bool operator==(const Field& rhs) const {
        return value_ == rhs.value_;
    }

    bool operator!=(const Field& rhs) const {
        return value_ != rhs.value_;
    }

    bool operator<(const Field& rhs) const {
        return value_ < rhs.value_;
    }

    // ==================== Variant Access ====================

    const FieldVariant& getValue() const {
        return value_;
    }

private:
    FieldVariant value_;
};

}  // namespace columns
}  // namespace diagon
