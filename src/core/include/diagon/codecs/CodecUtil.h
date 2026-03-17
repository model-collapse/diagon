// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/store/IndexInput.h"
#include "diagon/store/IndexOutput.h"

#include <cstdint>
#include <string>

namespace diagon::codecs {

/**
 * Utility class for reading and writing versioned headers and footers.
 *
 * Implements Lucene's CodecUtil format for OpenSearch/Lucene compatibility.
 * All header/footer integers are big-endian (network byte order).
 *
 * Based on: org.apache.lucene.codecs.CodecUtil
 *
 * File layout:
 *   Header:  CODEC_MAGIC(4) + codecName(VInt+bytes) + version(4)
 *   IndexHeader: Header + segmentID(16) + suffixLen(1) + suffix(N)
 *   Footer: FOOTER_MAGIC(4) + algorithmID(4) + CRC32(8) = 16 bytes
 */
class CodecUtil {
public:
    /** Magic number identifying the start of a codec header: 0x3fd76c17 */
    static constexpr int32_t CODEC_MAGIC = 0x3fd76c17;

    /** Magic number identifying the start of a codec footer: ~CODEC_MAGIC */
    static constexpr int32_t FOOTER_MAGIC = ~CODEC_MAGIC;  // 0xc0289de8

    /** Length of a codec footer in bytes (magic + algorithmID + checksum) */
    static constexpr int FOOTER_LENGTH = 16;

    /** Length of a segment ID in bytes (Lucene StringHelper.ID_LENGTH) */
    static constexpr int ID_LENGTH = 16;

    // ==================== Write Methods ====================

    /**
     * Writes a codec header: magic(BE) + codecName(String) + version(BE).
     *
     * @param out Output stream
     * @param codec Codec name (simple ASCII, < 128 chars)
     * @param version Version number
     */
    static void writeHeader(store::IndexOutput& out, const std::string& codec, int32_t version);

    /**
     * Writes an index header: header + segmentID(16 bytes) + suffix.
     *
     * @param out Output stream
     * @param codec Codec name (simple ASCII, < 128 chars)
     * @param version Version number
     * @param segmentID 16-byte segment identifier
     * @param suffix File suffix (simple ASCII, < 256 chars)
     */
    static void writeIndexHeader(store::IndexOutput& out, const std::string& codec, int32_t version,
                                 const uint8_t* segmentID, const std::string& suffix);

    /**
     * Writes a codec footer: FOOTER_MAGIC(BE) + algorithmID(0, BE) + CRC32(BE long).
     *
     * The CRC32 covers all bytes written before this footer, including the
     * footer magic and algorithm ID (but not the checksum itself — it covers
     * up to and including the algorithmID bytes).
     *
     * @param out Output stream (must support getChecksum())
     */
    static void writeFooter(store::IndexOutput& out);

    // ==================== Read/Validate Methods ====================

    /**
     * Reads and validates a header previously written with writeHeader().
     *
     * @param in Input stream, positioned at header start
     * @param codec Expected codec name
     * @param minVersion Minimum acceptable version
     * @param maxVersion Maximum acceptable version
     * @return The actual version found
     * @throws CorruptIndexException if magic/codec mismatch
     * @throws IndexFormatTooOldException if version < minVersion
     * @throws IndexFormatTooNewException if version > maxVersion
     */
    static int32_t checkHeader(store::IndexInput& in, const std::string& codec,
                               int32_t minVersion, int32_t maxVersion);

    /**
     * Like checkHeader() but assumes the magic has already been read and validated.
     */
    static int32_t checkHeaderNoMagic(store::IndexInput& in, const std::string& codec,
                                      int32_t minVersion, int32_t maxVersion);

    /**
     * Reads and validates an index header previously written with writeIndexHeader().
     *
     * @param in Input stream, positioned at header start
     * @param codec Expected codec name
     * @param minVersion Minimum acceptable version
     * @param maxVersion Maximum acceptable version
     * @param expectedID Expected 16-byte segment ID
     * @param expectedSuffix Expected file suffix
     * @return The actual version found
     */
    static int32_t checkIndexHeader(store::IndexInput& in, const std::string& codec,
                                    int32_t minVersion, int32_t maxVersion,
                                    const uint8_t* expectedID, const std::string& expectedSuffix);

    /**
     * Reads and validates just the segment ID portion of an index header.
     *
     * @param in Input stream, positioned after the codec header
     * @param expectedID Expected 16-byte segment ID
     */
    static void checkIndexHeaderID(store::IndexInput& in, const uint8_t* expectedID);

    /**
     * Reads and validates just the suffix portion of an index header.
     *
     * @param in Input stream, positioned after the segment ID
     * @param expectedSuffix Expected file suffix
     * @return The actual suffix found
     */
    static std::string checkIndexHeaderSuffix(store::IndexInput& in,
                                              const std::string& expectedSuffix);

    /**
     * Validates a footer and returns the stored checksum.
     *
     * NOTE: This reads and validates the footer structure but does NOT
     * verify the checksum against actual file contents (that requires a
     * ChecksumIndexInput which computes CRC32 over all preceding bytes).
     *
     * @param in Input stream, positioned at footer start
     * @return The checksum value stored in the footer
     * @throws CorruptIndexException if footer structure is invalid
     */
    static int64_t retrieveChecksum(store::IndexInput& in);

    /**
     * Validates footer and returns the stored checksum (convenience method).
     *
     * Equivalent to validateFooter() + readCRC(). This is what Lucene calls
     * checkFooter() in CodecUtil.java.
     *
     * @param in Input stream, positioned at footer start
     * @return The checksum value stored in the footer
     * @throws CorruptIndexException if footer structure is invalid
     */
    static int64_t checkFooter(store::IndexInput& in);

    /**
     * Validates footer structure (magic + algorithmID).
     *
     * @param in Input stream, positioned at footer start
     * @throws CorruptIndexException if footer is invalid
     */
    static void validateFooter(store::IndexInput& in);

    // ==================== Segment ID Generation ====================

    /**
     * Generates a random 16-byte segment ID using /dev/urandom.
     *
     * @param id Output buffer for the 16-byte ID
     */
    static void generateSegmentID(uint8_t* id);

    // ==================== Big-Endian I/O Helpers ====================

    /** Write a 32-bit integer in big-endian (network) byte order. */
    static void writeBEInt(store::IndexOutput& out, int32_t v);

    /** Read a 32-bit integer in big-endian (network) byte order. */
    static int32_t readBEInt(store::IndexInput& in);

    /** Write a 64-bit long in big-endian (network) byte order. */
    static void writeBELong(store::IndexOutput& out, int64_t v);

    /** Read a 64-bit long in big-endian (network) byte order. */
    static int64_t readBELong(store::IndexInput& in);

    // ==================== Length Computation ====================

    /**
     * Computes the length of a codec header.
     * = 4 (magic) + 1 (VInt string length) + codec.length() + 4 (version)
     * = 9 + codec.length()
     */
    static int headerLength(const std::string& codec);

    /**
     * Computes the length of an index header.
     * = headerLength(codec) + 16 (segmentID) + 1 (suffixLen) + suffix.length()
     */
    static int indexHeaderLength(const std::string& codec, const std::string& suffix);

    /**
     * Returns the length of a codec footer (always 16).
     */
    static constexpr int footerLength() { return FOOTER_LENGTH; }

private:
    CodecUtil() = delete;  // No instances

    /** Reads CRC32 value as a 64-bit long. Validates upper 32 bits are zero. */
    static int64_t readCRC(store::IndexInput& in);

    /** Writes CRC32 from output's getChecksum() as a 64-bit long. */
    static void writeCRC(store::IndexOutput& out);
};

}  // namespace diagon::codecs
