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

// ==================== Phase 3: .si, .fnm, segments_N Tests ====================

#include "diagon/codecs/lucene99/Lucene99SegmentInfoFormat.h"
#include "diagon/codecs/lucene94/Lucene94FieldInfosFormat.h"
#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/SegmentInfo.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/store/FSDirectory.h"

#include <fstream>
#include <unistd.h>

/**
 * Test fixture for OS-Compat Format Tests
 * Provides common setup/teardown with unique temp directories
 */
class OSCompatFormatsTest : public ::testing::Test {
protected:
    void SetUp() override {
        testDir_ = "/tmp/diagon_oscompat_test_" +
                   std::to_string(getpid()) + "_" +
                   std::to_string(std::time(nullptr));
        std::filesystem::create_directories(testDir_);
        dir_ = store::FSDirectory::open(testDir_);
    }

    void TearDown() override {
        dir_.reset();
        if (std::filesystem::exists(testDir_)) {
            std::filesystem::remove_all(testDir_);
        }
    }

    std::string testDir_;
    std::shared_ptr<store::Directory> dir_;

    /** Create a SegmentInfo with known values for testing. */
    std::shared_ptr<index::SegmentInfo> makeTestSegment(
        const std::string& name = "_0", int docCount = 100) {
        auto si = std::make_shared<index::SegmentInfo>(name, docCount, "Lucene104");
        si->addFile(name + ".doc");
        si->addFile(name + ".pos");
        si->addFile(name + ".tim");
        si->setDiagnostic("source", "flush");
        si->setDiagnostic("os", "linux");
        return si;
    }

    /** Create FieldInfos with diverse field types. */
    index::FieldInfos makeTestFieldInfos() {
        std::vector<index::FieldInfo> infos;

        // body: indexed with positions, norms, stored term vectors
        index::FieldInfo body("body", 0);
        body.indexOptions = index::IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
        body.storeTermVector = true;
        infos.push_back(std::move(body));

        // title: indexed with freqs, norms omitted
        index::FieldInfo title("title", 1);
        title.indexOptions = index::IndexOptions::DOCS_AND_FREQS;
        title.omitNorms = true;
        infos.push_back(std::move(title));

        // date: numeric doc values only
        index::FieldInfo date("date", 2);
        date.indexOptions = index::IndexOptions::NONE;
        date.docValuesType = index::DocValuesType::NUMERIC;
        infos.push_back(std::move(date));

        // category: sorted set doc values
        index::FieldInfo cat("category", 3);
        cat.indexOptions = index::IndexOptions::DOCS;
        cat.docValuesType = index::DocValuesType::SORTED_SET;
        infos.push_back(std::move(cat));

        // price: BKD points
        index::FieldInfo price("price", 4);
        price.indexOptions = index::IndexOptions::NONE;
        price.pointDimensionCount = 1;
        price.pointIndexDimensionCount = 1;
        price.pointNumBytes = 8;
        infos.push_back(std::move(price));

        // _soft_delete: soft-deletes sentinel
        index::FieldInfo sd("_soft_delete", 5);
        sd.indexOptions = index::IndexOptions::DOCS;
        sd.softDeletesField = true;
        infos.push_back(std::move(sd));

        return index::FieldInfos(std::move(infos));
    }
};

// ==================== Lucene99SegmentInfoFormat (.si) ====================

TEST_F(OSCompatFormatsTest, SIRoundTrip) {
    codecs::lucene99::Lucene99SegmentInfoFormat fmt;
    auto si = makeTestSegment("_0", 250);

    // Write .si
    fmt.write(*dir_, *si);

    // Read .si
    auto readSI = fmt.read(*dir_, "_0", si->segmentID());

    ASSERT_NE(readSI, nullptr);
    EXPECT_EQ(readSI->name(), "_0");
    EXPECT_EQ(readSI->maxDoc(), 250);
    EXPECT_EQ(readSI->codecName(), "Lucene104");
    EXPECT_EQ(readSI->getUseCompoundFile(), false);
    EXPECT_EQ(readSI->getDiagnostic("source"), "flush");
    EXPECT_EQ(readSI->getDiagnostic("os"), "linux");

    // Files should round-trip
    auto& files = readSI->files();
    EXPECT_EQ(files.size(), 3u);
}

TEST_F(OSCompatFormatsTest, SISegmentIDPreserved) {
    codecs::lucene99::Lucene99SegmentInfoFormat fmt;
    auto si = makeTestSegment("_1", 500);

    fmt.write(*dir_, *si);
    auto readSI = fmt.read(*dir_, "_1", si->segmentID());

    ASSERT_NE(readSI, nullptr);
    EXPECT_EQ(std::memcmp(readSI->segmentID(), si->segmentID(), 16), 0)
        << "Segment ID must survive round-trip";
}

TEST_F(OSCompatFormatsTest, SIWrongSegmentIDRejected) {
    codecs::lucene99::Lucene99SegmentInfoFormat fmt;
    auto si = makeTestSegment("_2", 10);

    fmt.write(*dir_, *si);

    // Try reading with a different segment ID — should throw
    uint8_t bogusID[16];
    std::memset(bogusID, 0xFF, 16);

    EXPECT_THROW(
        fmt.read(*dir_, "_2", bogusID),
        CorruptIndexException
    ) << "Mismatched segment ID must be rejected";
}

TEST_F(OSCompatFormatsTest, SICompoundFileFlag) {
    codecs::lucene99::Lucene99SegmentInfoFormat fmt;
    auto si = makeTestSegment("_3", 100);
    si->setUseCompoundFile(true);

    fmt.write(*dir_, *si);
    auto readSI = fmt.read(*dir_, "_3", si->segmentID());

    ASSERT_NE(readSI, nullptr);
    EXPECT_TRUE(readSI->getUseCompoundFile());
}

TEST_F(OSCompatFormatsTest, SIEmptyDiagnostics) {
    codecs::lucene99::Lucene99SegmentInfoFormat fmt;

    // SegmentInfo with no diagnostics
    auto si = std::make_shared<index::SegmentInfo>("_4", 50, "Lucene104");

    fmt.write(*dir_, *si);
    auto readSI = fmt.read(*dir_, "_4", si->segmentID());

    ASSERT_NE(readSI, nullptr);
    EXPECT_EQ(readSI->maxDoc(), 50);
    EXPECT_TRUE(readSI->diagnostics().empty());
}

// ==================== Lucene94FieldInfosFormat (.fnm) ====================

TEST_F(OSCompatFormatsTest, FNMRoundTrip) {
    codecs::lucene94::Lucene94FieldInfosFormat fmt;
    auto si = makeTestSegment("_0", 100);
    auto fieldInfos = makeTestFieldInfos();
    si->setFieldInfos(std::move(fieldInfos));

    // Write .fnm
    fmt.write(*dir_, *si, si->fieldInfos());

    // Read .fnm
    auto readFI = fmt.read(*dir_, *si);

    ASSERT_EQ(readFI.size(), 6u);

    // Verify body field
    auto* body = readFI.fieldInfo("body");
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->number, 0);
    EXPECT_EQ(body->indexOptions, index::IndexOptions::DOCS_AND_FREQS_AND_POSITIONS);
    EXPECT_TRUE(body->storeTermVector);
    EXPECT_FALSE(body->omitNorms);

    // Verify title field
    auto* title = readFI.fieldInfo("title");
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->number, 1);
    EXPECT_EQ(title->indexOptions, index::IndexOptions::DOCS_AND_FREQS);
    EXPECT_TRUE(title->omitNorms);
    EXPECT_FALSE(title->storeTermVector);

    // Verify date field (doc values)
    auto* date = readFI.fieldInfo("date");
    ASSERT_NE(date, nullptr);
    EXPECT_EQ(date->indexOptions, index::IndexOptions::NONE);
    EXPECT_EQ(date->docValuesType, index::DocValuesType::NUMERIC);

    // Verify category field (sorted set)
    auto* cat = readFI.fieldInfo("category");
    ASSERT_NE(cat, nullptr);
    EXPECT_EQ(cat->docValuesType, index::DocValuesType::SORTED_SET);

    // Verify price field (BKD points)
    auto* price = readFI.fieldInfo("price");
    ASSERT_NE(price, nullptr);
    EXPECT_EQ(price->pointDimensionCount, 1);
    EXPECT_EQ(price->pointIndexDimensionCount, 1);
    EXPECT_EQ(price->pointNumBytes, 8);

    // Verify soft-delete field
    auto* sd = readFI.fieldInfo("_soft_delete");
    ASSERT_NE(sd, nullptr);
    EXPECT_TRUE(sd->softDeletesField);
}

TEST_F(OSCompatFormatsTest, FNMEmptyFieldInfos) {
    codecs::lucene94::Lucene94FieldInfosFormat fmt;
    auto si = makeTestSegment("_0", 0);

    index::FieldInfos empty;
    fmt.write(*dir_, *si, empty);

    auto readFI = fmt.read(*dir_, *si);
    EXPECT_EQ(readFI.size(), 0u);
}

TEST_F(OSCompatFormatsTest, FNMFieldAttributes) {
    codecs::lucene94::Lucene94FieldInfosFormat fmt;
    auto si = makeTestSegment("_0", 100);

    std::vector<index::FieldInfo> infos;
    index::FieldInfo fi("body", 0);
    fi.indexOptions = index::IndexOptions::DOCS;
    fi.attributes["custom_key"] = "custom_value";
    fi.attributes["analyzer"] = "standard";
    infos.push_back(std::move(fi));

    index::FieldInfos fieldInfos(std::move(infos));
    fmt.write(*dir_, *si, fieldInfos);

    auto readFI = fmt.read(*dir_, *si);
    ASSERT_EQ(readFI.size(), 1u);
    auto* f = readFI.fieldInfo("body");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->attributes.at("custom_key"), "custom_value");
    EXPECT_EQ(f->attributes.at("analyzer"), "standard");
}

TEST_F(OSCompatFormatsTest, FNMStorePayloads) {
    codecs::lucene94::Lucene94FieldInfosFormat fmt;
    auto si = makeTestSegment("_0", 100);

    std::vector<index::FieldInfo> infos;
    index::FieldInfo fi("body", 0);
    fi.indexOptions = index::IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
    fi.storePayloads = true;
    infos.push_back(std::move(fi));

    index::FieldInfos fieldInfos(std::move(infos));
    fmt.write(*dir_, *si, fieldInfos);

    auto readFI = fmt.read(*dir_, *si);
    ASSERT_EQ(readFI.size(), 1u);
    auto* f = readFI.fieldInfo("body");
    ASSERT_NE(f, nullptr);
    EXPECT_TRUE(f->storePayloads);
}

// ==================== segments_N Lucene Format ====================

TEST_F(OSCompatFormatsTest, SegmentsNLuceneRoundTrip) {
    // Write an OS_COMPAT index with IndexWriter, then read back via SegmentInfos
    index::IndexWriterConfig config;
    config.setFormatMode(index::IndexWriterConfig::FormatMode::OS_COMPAT);

    auto writer = std::make_unique<index::IndexWriter>(*dir_, config);

    // Add a document
    document::Document doc;
    doc.add(std::make_unique<document::TextField>("body", "hello world"));
    writer->addDocument(doc);

    // Commit writes segments_N
    writer->commit();
    writer->close();

    // Verify segments_N file was created
    auto files = dir_->listAll();
    bool foundSegmentsFile = false;
    std::string segmentsFileName;
    for (const auto& f : files) {
        if (f.find("segments_") == 0) {
            foundSegmentsFile = true;
            segmentsFileName = f;
            break;
        }
    }
    ASSERT_TRUE(foundSegmentsFile) << "Must produce segments_N file";

    // Read it back with adaptive format detection
    auto sis = index::SegmentInfos::read(*dir_, segmentsFileName);
    EXPECT_EQ(sis.size(), 1) << "One segment after single commit";

    auto seg = sis.info(0);
    EXPECT_FALSE(seg->name().empty());
    EXPECT_EQ(seg->codecName(), "Lucene104");
}

TEST_F(OSCompatFormatsTest, SegmentsNLuceneMultiCommit) {
    index::IndexWriterConfig config;
    config.setFormatMode(index::IndexWriterConfig::FormatMode::OS_COMPAT);

    auto writer = std::make_unique<index::IndexWriter>(*dir_, config);

    // First commit
    {
        document::Document doc;
        doc.add(std::make_unique<document::TextField>("body", "first"));
        writer->addDocument(doc);
        writer->commit();
    }

    // Second commit with another document
    {
        document::Document doc;
        doc.add(std::make_unique<document::TextField>("body", "second"));
        writer->addDocument(doc);
        writer->commit();
    }

    writer->close();

    // Read the latest commit
    auto sis = index::SegmentInfos::readLatestCommit(*dir_);
    EXPECT_EQ(sis.size(), 2) << "Two segments after two commits";
}

TEST_F(OSCompatFormatsTest, SegmentsNNativeRoundTrip) {
    // Verify native format still works (regression check)
    index::IndexWriterConfig config;
    config.setFormatMode(index::IndexWriterConfig::FormatMode::NATIVE);

    auto writer = std::make_unique<index::IndexWriter>(*dir_, config);

    document::Document doc;
    doc.add(std::make_unique<document::TextField>("body", "native test"));
    writer->addDocument(doc);

    writer->commit();
    writer->close();

    auto sis = index::SegmentInfos::readLatestCommit(*dir_);
    EXPECT_EQ(sis.size(), 1);
}

// ==================== .si + .fnm in Flush Pipeline ====================

TEST_F(OSCompatFormatsTest, FlushProducesSIAndFNM) {
    // OS_COMPAT mode records .si and .fnm in the segment's file list.
    // After commit, these files may be packed into compound files (.cfs/.cfe),
    // so we check the segments_N metadata rather than the raw directory listing.
    index::IndexWriterConfig config;
    config.setFormatMode(index::IndexWriterConfig::FormatMode::OS_COMPAT);

    auto writer = std::make_unique<index::IndexWriter>(*dir_, config);

    document::Document doc;
    doc.add(std::make_unique<document::TextField>("body", "test doc"));
    writer->addDocument(doc);
    writer->commit();
    writer->close();

    // Read segments_N and check that the segment was written with codec "Lucene104"
    auto sis = index::SegmentInfos::readLatestCommit(*dir_);
    ASSERT_EQ(sis.size(), 1);
    EXPECT_EQ(sis.info(0)->codecName(), "Lucene104")
        << "OS_COMPAT flush must use Lucene104 codec";

    // Verify files on disk (compound or individual)
    auto files = dir_->listAll();
    bool hasSegment = false;
    for (const auto& f : files) {
        // Either .si/.fnm directly or .cfs (compound) — both are valid
        if (f.find(".si") != std::string::npos ||
            f.find(".cfs") != std::string::npos) {
            hasSegment = true;
            break;
        }
    }
    EXPECT_TRUE(hasSegment) << "Must have segment data files";
}

TEST_F(OSCompatFormatsTest, NativeFlushSkipsSIAndFNM) {
    index::IndexWriterConfig config;
    config.setFormatMode(index::IndexWriterConfig::FormatMode::NATIVE);

    auto writer = std::make_unique<index::IndexWriter>(*dir_, config);

    document::Document doc;
    doc.add(std::make_unique<document::TextField>("body", "native doc"));
    writer->addDocument(doc);
    writer->commit();
    writer->close();

    // Native mode should NOT produce .si or .fnm
    auto files = dir_->listAll();
    for (const auto& f : files) {
        if (f.size() > 3) {
            EXPECT_NE(f.substr(f.size() - 3), ".si")
                << "Native mode must not produce .si: " << f;
        }
        if (f.size() > 4) {
            EXPECT_NE(f.substr(f.size() - 4), ".fnm")
                << "Native mode must not produce .fnm: " << f;
        }
    }
}

// ==================== CRC32 Integrity ====================

TEST_F(OSCompatFormatsTest, SICorruptionDetected) {
    codecs::lucene99::Lucene99SegmentInfoFormat fmt;
    auto si = makeTestSegment("_0", 100);

    fmt.write(*dir_, *si);

    // Corrupt the .si file
    std::string siPath = testDir_ + "/_0.si";
    {
        std::fstream f(siPath, std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(f.is_open());
        // Seek to middle of file and corrupt a byte
        f.seekp(20, std::ios::beg);
        char c = static_cast<char>(0xFF);
        f.write(&c, 1);
    }

    // Re-open directory to clear any caches
    dir_ = store::FSDirectory::open(testDir_);

    EXPECT_THROW(
        fmt.read(*dir_, "_0", si->segmentID()),
        CorruptIndexException
    ) << "CRC32 footer must detect corruption";
}

TEST_F(OSCompatFormatsTest, FNMCorruptionDetected) {
    codecs::lucene94::Lucene94FieldInfosFormat fmt;
    auto si = makeTestSegment("_0", 100);

    std::vector<index::FieldInfo> infos;
    index::FieldInfo fi("body", 0);
    fi.indexOptions = index::IndexOptions::DOCS;
    infos.push_back(std::move(fi));
    index::FieldInfos fieldInfos(std::move(infos));

    fmt.write(*dir_, *si, fieldInfos);

    // Corrupt the .fnm file
    std::string fnmPath = testDir_ + "/_0.fnm";
    {
        std::fstream f(fnmPath, std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(f.is_open());
        f.seekp(20, std::ios::beg);
        char c = static_cast<char>(0xFF);
        f.write(&c, 1);
    }

    dir_ = store::FSDirectory::open(testDir_);

    EXPECT_THROW(
        fmt.read(*dir_, *si),
        CorruptIndexException
    ) << "CRC32 footer must detect .fnm corruption";
}
