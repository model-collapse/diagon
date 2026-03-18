// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/store/IndexInput.h"
#include "diagon/store/IndexOutput.h"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace diagon {
namespace codecs {
namespace lucene90 {

/**
 * Block-encoded integer arrays for stored fields chunk headers.
 *
 * Based on: org.apache.lucene.codecs.lucene90.compressing.StoredFieldsInts
 *
 * Encoding format:
 *   If all values equal:  byte(0x00) + VInt(value)
 *   If max <= 0xFF:       byte(0x08) + strided 8-bit packing (128-value blocks)
 *   If max <= 0xFFFF:     byte(0x10) + strided 16-bit packing (128-value blocks)
 *   Else:                 byte(0x20) + strided 32-bit packing (128-value blocks)
 *
 * Strided packing: 128 values packed into longs (big-endian writeLong).
 * - 8-bit:  8 values per long (stride=16), 16 longs per block
 * - 16-bit: 4 values per long (stride=32), 32 longs per block
 * - 32-bit: 2 values per long (stride=64), 64 longs per block
 * Remainder values written individually (byte/short/int).
 */
class StoredFieldsInts {
public:
    static constexpr int BLOCK_SIZE = 128;

    /**
     * Write int array. Chooses optimal encoding based on max value.
     */
    static void writeInts(const int* values, int start, int count, store::IndexOutput& out) {
        // Check if all values are equal
        bool allEqual = true;
        for (int i = 1; i < count; i++) {
            if (values[start + i] != values[start]) {
                allEqual = false;
                break;
            }
        }

        if (allEqual) {
            out.writeByte(0);
            out.writeVInt(values[start]);
        } else {
            // Find max value (as unsigned)
            uint32_t max = 0;
            for (int i = 0; i < count; i++) {
                max |= static_cast<uint32_t>(values[start + i]);
            }

            if (max <= 0xFF) {
                out.writeByte(8);
                writeInts8(out, count, values, start);
            } else if (max <= 0xFFFF) {
                out.writeByte(16);
                writeInts16(out, count, values, start);
            } else {
                out.writeByte(32);
                writeInts32(out, count, values, start);
            }
        }
    }

    /**
     * Read int array into a long vector.
     */
    static void readInts(store::IndexInput& in, int count, std::vector<int64_t>& values,
                         int offset) {
        int bpv = in.readByte();
        switch (bpv) {
            case 0: {
                int32_t val = in.readVInt();
                for (int i = 0; i < count; i++) {
                    values[offset + i] = val;
                }
                break;
            }
            case 8:
                readInts8(in, count, values, offset);
                break;
            case 16:
                readInts16(in, count, values, offset);
                break;
            case 32:
                readInts32(in, count, values, offset);
                break;
            default:
                throw std::runtime_error("Unsupported StoredFieldsInts bpv: " +
                                         std::to_string(bpv));
        }
    }

private:
    // ==================== 8-bit strided encoding ====================
    // 128 values → 16 longs (each long = 8 values at stride 16)

    static void writeInts8(store::IndexOutput& out, int count, const int* values, int offset) {
        int k = 0;
        for (; k < count - (BLOCK_SIZE - 1); k += BLOCK_SIZE) {
            int step = offset + k;
            for (int i = 0; i < 16; i++) {
                int64_t l = (static_cast<int64_t>(values[step + i]) << 56) |
                            (static_cast<int64_t>(values[step + 16 + i]) << 48) |
                            (static_cast<int64_t>(values[step + 32 + i]) << 40) |
                            (static_cast<int64_t>(values[step + 48 + i]) << 32) |
                            (static_cast<int64_t>(values[step + 64 + i]) << 24) |
                            (static_cast<int64_t>(values[step + 80 + i]) << 16) |
                            (static_cast<int64_t>(values[step + 96 + i]) << 8) |
                            static_cast<int64_t>(values[step + 112 + i]);
                out.writeLong(l);
            }
        }
        for (; k < count; k++) {
            out.writeByte(static_cast<uint8_t>(values[offset + k]));
        }
    }

    static void readInts8(store::IndexInput& in, int count, std::vector<int64_t>& values,
                          int offset) {
        int k = 0;
        for (; k < count - (BLOCK_SIZE - 1); k += BLOCK_SIZE) {
            int step = offset + k;
            // Read 16 longs
            for (int i = 0; i < 16; i++) {
                int64_t l = in.readLong();
                values[step + i] = (l >> 56) & 0xFFL;
                values[step + 16 + i] = (l >> 48) & 0xFFL;
                values[step + 32 + i] = (l >> 40) & 0xFFL;
                values[step + 48 + i] = (l >> 32) & 0xFFL;
                values[step + 64 + i] = (l >> 24) & 0xFFL;
                values[step + 80 + i] = (l >> 16) & 0xFFL;
                values[step + 96 + i] = (l >> 8) & 0xFFL;
                values[step + 112 + i] = l & 0xFFL;
            }
        }
        for (; k < count; k++) {
            values[offset + k] = static_cast<uint8_t>(in.readByte());
        }
    }

    // ==================== 16-bit strided encoding ====================
    // 128 values → 32 longs (each long = 4 values at stride 32)

    static void writeInts16(store::IndexOutput& out, int count, const int* values, int offset) {
        int k = 0;
        for (; k < count - (BLOCK_SIZE - 1); k += BLOCK_SIZE) {
            int step = offset + k;
            for (int i = 0; i < 32; i++) {
                int64_t l = (static_cast<int64_t>(values[step + i]) << 48) |
                            (static_cast<int64_t>(values[step + 32 + i]) << 32) |
                            (static_cast<int64_t>(values[step + 64 + i]) << 16) |
                            static_cast<int64_t>(values[step + 96 + i]);
                out.writeLong(l);
            }
        }
        for (; k < count; k++) {
            out.writeShort(static_cast<int16_t>(values[offset + k]));
        }
    }

    static void readInts16(store::IndexInput& in, int count, std::vector<int64_t>& values,
                           int offset) {
        int k = 0;
        for (; k < count - (BLOCK_SIZE - 1); k += BLOCK_SIZE) {
            int step = offset + k;
            for (int i = 0; i < 32; i++) {
                int64_t l = in.readLong();
                values[step + i] = (l >> 48) & 0xFFFFL;
                values[step + 32 + i] = (l >> 32) & 0xFFFFL;
                values[step + 64 + i] = (l >> 16) & 0xFFFFL;
                values[step + 96 + i] = l & 0xFFFFL;
            }
        }
        for (; k < count; k++) {
            values[offset + k] = static_cast<uint16_t>(in.readShort() & 0xFFFF);
        }
    }

    // ==================== 32-bit strided encoding ====================
    // 128 values → 64 longs (each long = 2 values at stride 64)

    static void writeInts32(store::IndexOutput& out, int count, const int* values, int offset) {
        int k = 0;
        for (; k < count - (BLOCK_SIZE - 1); k += BLOCK_SIZE) {
            int step = offset + k;
            for (int i = 0; i < 64; i++) {
                int64_t l = (static_cast<int64_t>(values[step + i]) << 32) |
                            (static_cast<int64_t>(static_cast<uint32_t>(values[step + 64 + i])));
                out.writeLong(l);
            }
        }
        for (; k < count; k++) {
            out.writeInt(values[offset + k]);
        }
    }

    static void readInts32(store::IndexInput& in, int count, std::vector<int64_t>& values,
                           int offset) {
        int k = 0;
        for (; k < count - (BLOCK_SIZE - 1); k += BLOCK_SIZE) {
            int step = offset + k;
            for (int i = 0; i < 64; i++) {
                int64_t l = in.readLong();
                values[step + i] = (l >> 32) & 0xFFFFFFFFL;
                values[step + 64 + i] = l & 0xFFFFFFFFL;
            }
        }
        for (; k < count; k++) {
            values[offset + k] = static_cast<uint32_t>(in.readInt());
        }
    }
};

}  // namespace lucene90
}  // namespace codecs
}  // namespace diagon
