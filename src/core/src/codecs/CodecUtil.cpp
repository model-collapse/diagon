// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/CodecUtil.h"

#include "diagon/util/Exceptions.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>

namespace diagon::codecs {

// ==================== Write Methods ====================

void CodecUtil::writeHeader(store::IndexOutput& out, const std::string& codec, int32_t version) {
    if (codec.size() >= 128) {
        throw std::invalid_argument(
            "codec must be simple ASCII, less than 128 characters in length [got " + codec + "]");
    }
    writeBEInt(out, CODEC_MAGIC);
    out.writeString(codec);
    writeBEInt(out, version);
}

void CodecUtil::writeIndexHeader(store::IndexOutput& out, const std::string& codec, int32_t version,
                                 const uint8_t* segmentID, const std::string& suffix) {
    if (suffix.size() >= 256) {
        throw std::invalid_argument(
            "suffix must be simple ASCII, less than 256 characters in length [got " + suffix + "]");
    }
    writeHeader(out, codec, version);
    out.writeBytes(segmentID, ID_LENGTH);
    out.writeByte(static_cast<uint8_t>(suffix.size()));
    if (!suffix.empty()) {
        out.writeBytes(reinterpret_cast<const uint8_t*>(suffix.data()), suffix.size());
    }
}

void CodecUtil::writeFooter(store::IndexOutput& out) {
    writeBEInt(out, FOOTER_MAGIC);
    writeBEInt(out, 0);  // algorithmID = 0 (zlib-crc32)
    writeCRC(out);
}

// ==================== Read/Validate Methods ====================

int32_t CodecUtil::checkHeader(store::IndexInput& in, const std::string& codec,
                               int32_t minVersion, int32_t maxVersion) {
    int32_t actualMagic = readBEInt(in);
    if (actualMagic != CODEC_MAGIC) {
        throw CorruptIndexException(
            "codec header mismatch: actual header=" + std::to_string(actualMagic)
            + " vs expected header=" + std::to_string(CODEC_MAGIC),
            in.toString());
    }
    return checkHeaderNoMagic(in, codec, minVersion, maxVersion);
}

int32_t CodecUtil::checkHeaderNoMagic(store::IndexInput& in, const std::string& codec,
                                      int32_t minVersion, int32_t maxVersion) {
    std::string actualCodec = in.readString();
    if (actualCodec != codec) {
        throw CorruptIndexException(
            "codec mismatch: actual codec=" + actualCodec + " vs expected codec=" + codec,
            in.toString());
    }

    int32_t actualVersion = readBEInt(in);
    if (actualVersion < minVersion) {
        throw IndexFormatTooOldException(in.toString(), actualVersion, minVersion, maxVersion);
    }
    if (actualVersion > maxVersion) {
        throw IndexFormatTooNewException(in.toString(), actualVersion, minVersion, maxVersion);
    }

    return actualVersion;
}

int32_t CodecUtil::checkIndexHeader(store::IndexInput& in, const std::string& codec,
                                    int32_t minVersion, int32_t maxVersion,
                                    const uint8_t* expectedID, const std::string& expectedSuffix) {
    int32_t version = checkHeader(in, codec, minVersion, maxVersion);
    checkIndexHeaderID(in, expectedID);
    checkIndexHeaderSuffix(in, expectedSuffix);
    return version;
}

void CodecUtil::checkIndexHeaderID(store::IndexInput& in, const uint8_t* expectedID) {
    uint8_t id[ID_LENGTH];
    in.readBytes(id, ID_LENGTH);
    if (expectedID == nullptr) {
        return;  // Skip ID validation when no expected ID provided
    }
    if (std::memcmp(id, expectedID, ID_LENGTH) != 0) {
        // Format hex strings for the error message
        auto toHex = [](const uint8_t* data, int len) -> std::string {
            std::ostringstream oss;
            for (int i = 0; i < len; ++i) {
                char buf[4];
                snprintf(buf, sizeof(buf), "%02x", data[i]);
                oss << buf;
            }
            return oss.str();
        };
        throw CorruptIndexException(
            "file mismatch, expected id=" + toHex(expectedID, ID_LENGTH)
            + ", got=" + toHex(id, ID_LENGTH),
            in.toString());
    }
}

std::string CodecUtil::checkIndexHeaderSuffix(store::IndexInput& in,
                                              const std::string& expectedSuffix) {
    int suffixLength = in.readByte() & 0xFF;
    std::string suffix(suffixLength, '\0');
    if (suffixLength > 0) {
        in.readBytes(reinterpret_cast<uint8_t*>(suffix.data()), suffixLength);
    }
    if (suffix != expectedSuffix) {
        throw CorruptIndexException(
            "file mismatch, expected suffix=" + expectedSuffix + ", got=" + suffix,
            in.toString());
    }
    return suffix;
}

int64_t CodecUtil::retrieveChecksum(store::IndexInput& in) {
    if (in.length() < FOOTER_LENGTH) {
        throw CorruptIndexException(
            "misplaced codec footer (file truncated?): length=" + std::to_string(in.length())
            + " but footerLength==" + std::to_string(FOOTER_LENGTH),
            in.toString());
    }
    in.seek(in.length() - FOOTER_LENGTH);
    validateFooter(in);
    return readCRC(in);
}

void CodecUtil::validateFooter(store::IndexInput& in) {
    int64_t remaining = in.length() - in.getFilePointer();
    if (remaining < FOOTER_LENGTH) {
        throw CorruptIndexException(
            "misplaced codec footer (file truncated?): remaining=" + std::to_string(remaining)
            + ", expected=" + std::to_string(FOOTER_LENGTH)
            + ", fp=" + std::to_string(in.getFilePointer()),
            in.toString());
    }

    int32_t magic = readBEInt(in);
    if (magic != FOOTER_MAGIC) {
        throw CorruptIndexException(
            "codec footer mismatch (file truncated?): actual footer=" + std::to_string(magic)
            + " vs expected footer=" + std::to_string(FOOTER_MAGIC),
            in.toString());
    }

    int32_t algorithmID = readBEInt(in);
    if (algorithmID != 0) {
        throw CorruptIndexException(
            "codec footer mismatch: unknown algorithmID: " + std::to_string(algorithmID),
            in.toString());
    }
}

// ==================== Big-Endian I/O Helpers ====================

void CodecUtil::writeBEInt(store::IndexOutput& out, int32_t v) {
    out.writeByte(static_cast<uint8_t>((v >> 24) & 0xFF));
    out.writeByte(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.writeByte(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.writeByte(static_cast<uint8_t>(v & 0xFF));
}

int32_t CodecUtil::readBEInt(store::IndexInput& in) {
    return (static_cast<int32_t>(in.readByte() & 0xFF) << 24)
         | (static_cast<int32_t>(in.readByte() & 0xFF) << 16)
         | (static_cast<int32_t>(in.readByte() & 0xFF) << 8)
         | static_cast<int32_t>(in.readByte() & 0xFF);
}

void CodecUtil::writeBELong(store::IndexOutput& out, int64_t v) {
    writeBEInt(out, static_cast<int32_t>(v >> 32));
    writeBEInt(out, static_cast<int32_t>(v));
}

int64_t CodecUtil::readBELong(store::IndexInput& in) {
    return (static_cast<int64_t>(readBEInt(in)) << 32)
         | (static_cast<int64_t>(readBEInt(in)) & 0xFFFFFFFFL);
}

// ==================== Length Computation ====================

int CodecUtil::headerLength(const std::string& codec) {
    return 9 + static_cast<int>(codec.size());
}

int CodecUtil::indexHeaderLength(const std::string& codec, const std::string& suffix) {
    return headerLength(codec) + ID_LENGTH + 1 + static_cast<int>(suffix.size());
}

// ==================== Convenience Methods ====================

int64_t CodecUtil::checkFooter(store::IndexInput& in) {
    validateFooter(in);
    return readCRC(in);
}

// ==================== Segment ID Generation ====================

void CodecUtil::generateSegmentID(uint8_t* id) {
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    if (!urandom.good() || !urandom.read(reinterpret_cast<char*>(id), ID_LENGTH)) {
        // Fallback: use time + address-based entropy
        auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        std::memset(id, 0, ID_LENGTH);
        std::memcpy(id, &now, std::min(sizeof(now), static_cast<size_t>(ID_LENGTH)));
        // Mix in address of id buffer for extra entropy
        auto addr = reinterpret_cast<uintptr_t>(id);
        for (int i = 0; i < 8 && (8 + i) < ID_LENGTH; ++i) {
            id[8 + i] = static_cast<uint8_t>(addr >> (i * 8));
        }
    }
}

// ==================== Private Helpers ====================

int64_t CodecUtil::readCRC(store::IndexInput& in) {
    int64_t value = readBELong(in);
    if ((value & static_cast<int64_t>(0xFFFFFFFF00000000ULL)) != 0) {
        throw CorruptIndexException(
            "Illegal CRC-32 checksum: " + std::to_string(value),
            in.toString());
    }
    return value;
}

void CodecUtil::writeCRC(store::IndexOutput& out) {
    int64_t value = out.getChecksum();
    if ((value & static_cast<int64_t>(0xFFFFFFFF00000000ULL)) != 0) {
        throw std::logic_error(
            "Illegal CRC-32 checksum: " + std::to_string(value)
            + " (resource=" + out.getName() + ")");
    }
    writeBELong(out, value);
}

}  // namespace diagon::codecs
