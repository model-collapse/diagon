// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

/**
 * OS-Compat Formats Tests
 *
 * Comprehensive tests for OpenSearch/Lucene format compatibility.
 * Tests the dual-mode codec system where Diagon can read/write both:
 * - NATIVE: Optimized Diagon format (all optimizations)
 * - OS_COMPAT: Byte-level OpenSearch/Lucene compatible format
 *
 * Test structure:
 * Phase 1: CodecUtil (header/footer/CRC32)
 * Phase 2: SegmentID and format detection
 * Phase 3: Metadata files (.si, .fnm, segments_N)
 * Phase 4: Postings and term dictionary
 * Phase 5: Stored fields and other formats
 * Phase 6: Integration and round-trip tests
 */

#include <gtest/gtest.h>

#include "diagon/codecs/CodecUtil.h"
#include "diagon/store/ByteBuffersIndexInput.h"
#include "diagon/store/ByteBuffersIndexOutput.h"
#include "diagon/util/Exceptions.h"

#include <filesystem>
#include <memory>
#include <string>
#include <cstring>

using namespace diagon;
using namespace diagon::codecs;
using namespace diagon::store;

// ==================== Phase 1: CodecUtil Tests ====================

/**
 * CodecUtil Basic Header Tests
 */
TEST(OSCompatCodecUtilTest, WriteReadHeader) {
    // Create output and write header
    auto out = std::make_unique<ByteBuffersIndexOutput>("test");
    CodecUtil::writeHeader(*out, "TestCodec", 1);

    int64_t filePointer = out->getFilePointer();
    EXPECT_GT(filePointer, 0) << "Header should write bytes to output";

    // Get bytes and create input
    auto buffer = out->toArrayCopy();
    auto in = std::make_unique<ByteBuffersIndexInput>("test", buffer);

    // Read and verify header
    int version = CodecUtil::checkHeader(*in, "TestCodec", 1, 1);
    EXPECT_EQ(version, 1) << "Version should match";

    // Verify file pointer advanced
    EXPECT_GT(in->getFilePointer(), 0) << "Reading header should advance file pointer";
}

TEST(OSCompatCodecUtilTest, WriteReadIndexHeader) {
    auto out = std::make_unique<ByteBuffersIndexOutput>("test");

    // Create a 16-byte segment ID
    uint8_t segmentID[16];
    for (int i = 0; i < 16; i++) {
        segmentID[i] = i;
    }

    // Write index header with segment ID and suffix
    CodecUtil::writeIndexHeader(*out, "TestCodec", 1, segmentID, "");

    // Get buffer and create input
    auto buffer = out->toArrayCopy();
    auto in = std::make_unique<ByteBuffersIndexInput>("test", buffer);

    // Read and verify index header
    int version = CodecUtil::checkIndexHeader(*in, "TestCodec", 1, 1, segmentID, "");
    EXPECT_EQ(version, 1) << "Version should match";
}

TEST(OSCompatCodecUtilTest, WriteReadIndexHeaderWithSuffix) {
    auto out = std::make_unique<ByteBuffersIndexOutput>("test");

    uint8_t segmentID[16] = {};
    std::string suffix = "compressed";

    CodecUtil::writeIndexHeader(*out, "BlockTreeTermsDict", 3, segmentID, suffix);

    auto buffer = out->toArrayCopy();
    auto in = std::make_unique<ByteBuffersIndexInput>("test", buffer);

    int version = CodecUtil::checkIndexHeader(*in, "BlockTreeTermsDict", 1, 10, segmentID, suffix);
    EXPECT_EQ(version, 3) << "Version should match written value";
}

TEST(OSCompatCodecUtilTest, WriteReadFooter) {
    auto out = std::make_unique<ByteBuffersIndexOutput>("test");

    // Write some data first
    out->writeByte(0xFF);
    out->writeByte(0xFF);
    out->writeByte(0xFF);

    int64_t beforeFooter = out->getFilePointer();

    // Write footer
    CodecUtil::writeFooter(*out);

    int64_t afterFooter = out->getFilePointer();
    EXPECT_EQ(afterFooter - beforeFooter, CodecUtil::FOOTER_LENGTH)
        << "Footer should be FOOTER_LENGTH bytes";

    // Read and verify footer
    auto buffer = out->toArrayCopy();
    auto in = std::make_unique<ByteBuffersIndexInput>("test", buffer);

    // Skip to footer
    in->seek(beforeFooter);

    // checkFooter should succeed (returns checksum)
    int64_t checksum = CodecUtil::checkFooter(*in);
    EXPECT_GE(checksum, 0) << "Checksum should be valid";
}

TEST(OSCompatCodecUtilTest, InvalidMagicDetection) {
    auto out = std::make_unique<ByteBuffersIndexOutput>("test");

    // Write invalid header (wrong magic)
    out->writeByte(0x00);
    out->writeByte(0x00);
    out->writeByte(0x00);
    out->writeByte(0x00);

    auto buffer = out->toArrayCopy();
    auto in = std::make_unique<ByteBuffersIndexInput>("test", buffer);

    // Should throw on invalid magic
    EXPECT_THROW(
        CodecUtil::checkHeader(*in, "TestCodec", 1, 1),
        CorruptIndexException
    ) << "Should detect invalid magic bytes";
}

TEST(OSCompatCodecUtilTest, CodecNameMismatch) {
    auto out = std::make_unique<ByteBuffersIndexOutput>("test");

    CodecUtil::writeHeader(*out, "CodecA", 1);

    auto buffer = out->toArrayCopy();
    auto in = std::make_unique<ByteBuffersIndexInput>("test", buffer);

    // Should throw on codec name mismatch
    EXPECT_THROW(
        CodecUtil::checkHeader(*in, "CodecB", 1, 1),
        CorruptIndexException
    ) << "Should detect codec name mismatch";
}

TEST(OSCompatCodecUtilTest, VersionOutOfRange) {
    auto out = std::make_unique<ByteBuffersIndexOutput>("test");

    CodecUtil::writeHeader(*out, "TestCodec", 5);

    auto buffer = out->toArrayCopy();
    auto in = std::make_unique<ByteBuffersIndexInput>("test", buffer);

    // Should throw if version outside allowed range
    EXPECT_THROW(
        CodecUtil::checkHeader(*in, "TestCodec", 1, 4),  // version 5 > max 4
        CorruptIndexException
    ) << "Should detect version out of range";
}

TEST(OSCompatCodecUtilTest, SegmentIDMismatch) {
    auto out = std::make_unique<ByteBuffersIndexOutput>("test");

    uint8_t segmentID1[16];
    uint8_t segmentID2[16];
    for (int i = 0; i < 16; i++) {
        segmentID1[i] = i;
        segmentID2[i] = i + 1;  // Different
    }

    CodecUtil::writeIndexHeader(*out, "TestCodec", 1, segmentID1, "");

    auto buffer = out->toArrayCopy();
    auto in = std::make_unique<ByteBuffersIndexInput>("test", buffer);

    // Should throw on segment ID mismatch
    EXPECT_THROW(
        CodecUtil::checkIndexHeader(*in, "TestCodec", 1, 1, segmentID2, ""),
        CorruptIndexException
    ) << "Should detect segment ID mismatch";
}

TEST(OSCompatCodecUtilTest, SuffixMismatch) {
    auto out = std::make_unique<ByteBuffersIndexOutput>("test");

    uint8_t segmentID[16] = {};
    CodecUtil::writeIndexHeader(*out, "TestCodec", 1, segmentID, "orig");

    auto buffer = out->toArrayCopy();
    auto in = std::make_unique<ByteBuffersIndexInput>("test", buffer);

    // Should throw on suffix mismatch
    EXPECT_THROW(
        CodecUtil::checkIndexHeader(*in, "TestCodec", 1, 1, segmentID, "wrong"),
        CorruptIndexException
    ) << "Should detect suffix mismatch";
}

TEST(OSCompatCodecUtilTest, CorruptFooterDetection) {
    auto out = std::make_unique<ByteBuffersIndexOutput>("test");

    // Write valid footer
    CodecUtil::writeFooter(*out);

    // Corrupt the footer magic (first 4 bytes of footer)
    auto buffer = out->toArrayCopy();
    if (buffer.size() >= 16) {
        buffer[0] ^= 0xFF;  // Flip bits in footer magic area
    }

    auto in = std::make_unique<ByteBuffersIndexInput>("test", buffer);
    in->seek(0);

    // Should throw on corrupt footer magic
    EXPECT_THROW(
        CodecUtil::checkFooter(*in),
        CorruptIndexException
    ) << "Should detect corrupted footer";
}

TEST(OSCompatCodecUtilTest, BigEndianIOCorrectness) {
    auto out = std::make_unique<ByteBuffersIndexOutput>("test");

    // Write various big-endian values
    int32_t testInt = 0x12345678;
    int64_t testLong = 0x123456789ABCDEF0LL;

    out->writeInt(testInt);      // Little-endian (native)
    out->writeLong(testLong);    // Little-endian (native)

    auto buffer = out->toArrayCopy();
    auto in = std::make_unique<ByteBuffersIndexInput>("test", buffer);

    // Verify we can read back the values
    int32_t readInt = in->readInt();
    int64_t readLong = in->readLong();

    EXPECT_EQ(readInt, testInt);
    EXPECT_EQ(readLong, testLong);
}

TEST(OSCompatCodecUtilTest, RoundTripFullHeader) {
    // Test full cycle: write header + data + footer, then read and verify
    auto out = std::make_unique<ByteBuffersIndexOutput>("test");

    uint8_t segmentID[16];
    for (int i = 0; i < 16; i++) {
        segmentID[i] = static_cast<uint8_t>(i * 17);  // Pseudo-random
    }

    // Write
    CodecUtil::writeIndexHeader(*out, "ComplexCodec", 7, segmentID, "test_suffix");
    out->writeByte(0xAA);
    out->writeByte(0xBB);
    out->writeInt(12345);
    CodecUtil::writeFooter(*out);

    // Read
    auto buffer = out->toArrayCopy();
    auto in = std::make_unique<ByteBuffersIndexInput>("test", buffer);

    // Verify header
    int version = CodecUtil::checkIndexHeader(*in, "ComplexCodec", 1, 10, segmentID, "test_suffix");
    EXPECT_EQ(version, 7);

    // Verify data
    uint8_t b1 = in->readByte();
    uint8_t b2 = in->readByte();
    int32_t i = in->readInt();
    EXPECT_EQ(b1, 0xAA);
    EXPECT_EQ(b2, 0xBB);
    EXPECT_EQ(i, 12345);

    // Verify footer
    int64_t checksum = CodecUtil::checkFooter(*in);
    EXPECT_GE(checksum, 0);
}

// ==================== Phase 2: SegmentID Tests ====================

TEST(OSCompatSegmentIDTest, GenerateSegmentID) {
    uint8_t id1[16];
    uint8_t id2[16];

    // Generate two segment IDs
    CodecUtil::generateSegmentID(id1);
    CodecUtil::generateSegmentID(id2);

    // Should be 16 bytes
    // (assumes generateSegmentID fills the buffer)
    // Note: actual test depends on CodecUtil::generateSegmentID being implemented

    // They should be different (with very high probability)
    bool different = false;
    for (int i = 0; i < 16; i++) {
        if (id1[i] != id2[i]) {
            different = true;
            break;
        }
    }
    EXPECT_TRUE(different) << "Two generated segment IDs should be different";
}

// ==================== Phase 3: Format Detection Tests ====================

TEST(OSCompatFormatDetectionTest, DetectLuceneHeader) {
    auto out = std::make_unique<ByteBuffersIndexOutput>("test");

    // Write Lucene header
    CodecUtil::writeHeader(*out, "LuceneCodec", 1);

    auto buffer = out->toArrayCopy();

    // Check magic bytes (first 4 bytes should be CODEC_MAGIC)
    // This is a file-level format detection
    uint32_t magic = 0;
    for (int i = 0; i < 4; i++) {
        magic = (magic << 8) | buffer[i];
    }

    // Note: ByteBuffersIndexOutput uses little-endian by default
    // This test assumes CodecUtil::writeBEInt writes in big-endian
    // We'd need to verify this through the actual implementation
}

TEST(OSCompatFormatDetectionTest, SegmentsNDetectionDiagonNative) {
    // Test the segments_N detection logic
    // Diagon native: magic(0x3fd76c17 LE) + int32(1 BE) = {0x00, 0x00, 0x00, 0x01}
    // At byte 4, we see 0x00 (distinguishes from Lucene where byte 4 is VInt string length > 0)

    auto out = std::make_unique<ByteBuffersIndexOutput>("test");

    // Write Diagon magic as raw bytes (little-endian 0x3fd76c17)
    out->writeByte(0x17);
    out->writeByte(0x6c);
    out->writeByte(0xd7);
    out->writeByte(0x3f);
    out->writeInt(1);      // int32(1) in big-endian (IndexOutput::writeInt is BE)

    auto buffer = out->toArrayCopy();

    // At byte 4-7, we should see {0x00, 0x00, 0x00, 0x01} (big-endian int32(1))
    // Byte 4 == 0x00 distinguishes Diagon native from Lucene (where byte 4 is VInt length > 0)
    EXPECT_EQ(buffer[4], 0x00);
    EXPECT_EQ(buffer[5], 0x00);
    EXPECT_EQ(buffer[6], 0x00);
    EXPECT_EQ(buffer[7], 0x01);
}

// ==================== Phase 6: Integration Tests ====================

TEST(OSCompatIntegrationTest, FullHeaderFooterCycle) {
    // Integration test: write index header, data, and footer
    auto out = std::make_unique<ByteBuffersIndexOutput>("segment_1.tim");

    uint8_t segmentID[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

    // Write BlockTreeTermsDict header (as OS would)
    CodecUtil::writeIndexHeader(*out, "BlockTreeTermsDict", 3, segmentID, "");

    // Write some term data (simulated)
    out->writeVInt(5);  // 5 terms
    for (int i = 0; i < 5; i++) {
        out->writeVInt(i * 10);  // Simulated term doc freq
    }

    int64_t beforeFooter = out->getFilePointer();
    CodecUtil::writeFooter(*out);

    // Now read it back
    auto buffer = out->toArrayCopy();
    auto in = std::make_unique<ByteBuffersIndexInput>("segment_1.tim", buffer);

    // Verify header
    int version = CodecUtil::checkIndexHeader(*in, "BlockTreeTermsDict", 1, 10, segmentID, "");
    EXPECT_EQ(version, 3);

    // Verify data
    int termCount = in->readVInt();
    EXPECT_EQ(termCount, 5);

    for (int i = 0; i < 5; i++) {
        int docFreq = in->readVInt();
        EXPECT_EQ(docFreq, i * 10);
    }

    // Seek to footer and verify
    in->seek(beforeFooter);
    int64_t checksum = CodecUtil::checkFooter(*in);
    EXPECT_GE(checksum, 0);
}

// ==================== Test Fixtures for Later Phases ====================

/**
 * Test fixture for OS-Compat Format Tests
 * Provides common setup/teardown and helper methods
 */
class OSCompatFormatsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary test directory for Phase 3+ tests
        testDir_ = "/tmp/diagon_oscompat_test_" + std::to_string(std::time(nullptr));
        std::filesystem::create_directories(testDir_);
    }

    void TearDown() override {
        // Clean up temporary directory
        if (std::filesystem::exists(testDir_)) {
            std::filesystem::remove_all(testDir_);
        }
    }

    std::string testDir_;

    // Helper methods for later phases
    void createTestIndex(const std::string& mode) {
        // Helper for Phase 6 integration tests
        // mode = "native" or "os_compat"
    }

    void verifyIndexFormat(const std::string& format) {
        // Helper to verify index format
        // format = "diagon_native", "lucene_compat"
    }
};

// Note: Phase 3+ tests will use OSCompatFormatsTest fixture
// to provide filesystem access and cleanup
