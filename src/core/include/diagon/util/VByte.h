// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <vector>

namespace diagon {
namespace util {

/**
 * VByte (Variable Byte) encoding for integers
 *
 * Encodes integers using 7 bits per byte, with high bit as continuation flag.
 * Small integers use fewer bytes:
 * - [0, 127] → 1 byte
 * - [128, 16383] → 2 bytes
 * - [16384, 2097151] → 3 bytes
 * - etc.
 *
 * Based on: Lucene's VInt encoding
 */
class VByte {
public:
    /**
     * Encode unsigned 32-bit integer
     * @param value Value to encode
     * @param output Buffer to write to
     * @return Number of bytes written
     */
    static int encodeUInt32(uint32_t value, uint8_t* output) {
        int bytes = 0;
        while (value >= 0x80) {
            output[bytes++] = static_cast<uint8_t>((value & 0x7F) | 0x80);
            value >>= 7;
        }
        output[bytes++] = static_cast<uint8_t>(value & 0x7F);
        return bytes;
    }

    /**
     * Encode signed 32-bit integer using zig-zag encoding
     * Maps negative values to positive: 0, -1, 1, -2, 2, -3, 3, ...
     * @param value Value to encode
     * @param output Buffer to write to
     * @return Number of bytes written
     */
    static int encodeInt32(int32_t value, uint8_t* output) {
        // Zig-zag encoding: (n << 1) ^ (n >> 31)
        uint32_t zigzag = (static_cast<uint32_t>(value) << 1) ^
                          static_cast<uint32_t>(value >> 31);
        return encodeUInt32(zigzag, output);
    }

    /**
     * Encode unsigned 64-bit integer
     * @param value Value to encode
     * @param output Buffer to write to
     * @return Number of bytes written
     */
    static int encodeUInt64(uint64_t value, uint8_t* output) {
        int bytes = 0;
        while (value >= 0x80) {
            output[bytes++] = static_cast<uint8_t>((value & 0x7F) | 0x80);
            value >>= 7;
        }
        output[bytes++] = static_cast<uint8_t>(value & 0x7F);
        return bytes;
    }

    /**
     * Encode signed 64-bit integer using zig-zag encoding
     * @param value Value to encode
     * @param output Buffer to write to
     * @return Number of bytes written
     */
    static int encodeInt64(int64_t value, uint8_t* output) {
        // Zig-zag encoding: (n << 1) ^ (n >> 63)
        uint64_t zigzag = (static_cast<uint64_t>(value) << 1) ^
                          static_cast<uint64_t>(value >> 63);
        return encodeUInt64(zigzag, output);
    }

    /**
     * Decode unsigned 32-bit integer
     * @param input Buffer to read from
     * @param bytesRead Output: number of bytes read
     * @return Decoded value
     */
    static uint32_t decodeUInt32(const uint8_t* input, int* bytesRead) {
        uint32_t value = 0;
        int shift = 0;
        int bytes = 0;
        uint8_t byte;
        do {
            byte = input[bytes++];
            value |= static_cast<uint32_t>(byte & 0x7F) << shift;
            shift += 7;
        } while ((byte & 0x80) != 0);
        *bytesRead = bytes;
        return value;
    }

    /**
     * Decode signed 32-bit integer (zig-zag encoded)
     * @param input Buffer to read from
     * @param bytesRead Output: number of bytes read
     * @return Decoded value
     */
    static int32_t decodeInt32(const uint8_t* input, int* bytesRead) {
        uint32_t zigzag = decodeUInt32(input, bytesRead);
        // Reverse zig-zag: (n >>> 1) ^ -(n & 1)
        return static_cast<int32_t>((zigzag >> 1) ^ (~(zigzag & 1) + 1));
    }

    /**
     * Decode unsigned 64-bit integer
     * @param input Buffer to read from
     * @param bytesRead Output: number of bytes read
     * @return Decoded value
     */
    static uint64_t decodeUInt64(const uint8_t* input, int* bytesRead) {
        uint64_t value = 0;
        int shift = 0;
        int bytes = 0;
        uint8_t byte;
        do {
            byte = input[bytes++];
            value |= static_cast<uint64_t>(byte & 0x7F) << shift;
            shift += 7;
        } while ((byte & 0x80) != 0);
        *bytesRead = bytes;
        return value;
    }

    /**
     * Decode signed 64-bit integer (zig-zag encoded)
     * @param input Buffer to read from
     * @param bytesRead Output: number of bytes read
     * @return Decoded value
     */
    static int64_t decodeInt64(const uint8_t* input, int* bytesRead) {
        uint64_t zigzag = decodeUInt64(input, bytesRead);
        // Reverse zig-zag: (n >>> 1) ^ -(n & 1)
        return static_cast<int64_t>((zigzag >> 1) ^ (~(zigzag & 1) + 1));
    }

    /**
     * Calculate encoded size for uint32
     */
    static int encodedSize(uint32_t value) {
        int bytes = 1;
        while (value >= 0x80) {
            bytes++;
            value >>= 7;
        }
        return bytes;
    }

    /**
     * Calculate encoded size for uint64
     */
    static int encodedSize(uint64_t value) {
        int bytes = 1;
        while (value >= 0x80) {
            bytes++;
            value >>= 7;
        }
        return bytes;
    }
};

}  // namespace util
}  // namespace diagon
