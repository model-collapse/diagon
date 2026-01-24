// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>

namespace diagon {
namespace granularity {

/**
 * Mark points to position in compressed file.
 *
 * Two-level addressing:
 * 1. offset_in_compressed_file: Position in .bin file
 * 2. offset_in_decompressed_block: Position within decompressed block
 *
 * Based on: ClickHouse MarkInCompressedFile
 */
struct MarkInCompressedFile {
    /**
     * Offset in compressed file (.bin)
     * Points to start of compressed block containing this mark
     */
    uint64_t offset_in_compressed_file;

    /**
     * Offset within decompressed block
     * Number of bytes to skip after decompressing
     */
    uint64_t offset_in_decompressed_block;

    MarkInCompressedFile()
        : offset_in_compressed_file(0)
        , offset_in_decompressed_block(0) {}

    MarkInCompressedFile(uint64_t file_offset, uint64_t block_offset)
        : offset_in_compressed_file(file_offset)
        , offset_in_decompressed_block(block_offset) {}

    bool operator==(const MarkInCompressedFile& other) const {
        return offset_in_compressed_file == other.offset_in_compressed_file &&
               offset_in_decompressed_block == other.offset_in_decompressed_block;
    }

    bool operator!=(const MarkInCompressedFile& other) const { return !(*this == other); }
};

}  // namespace granularity
}  // namespace diagon
