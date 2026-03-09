// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/BKDReader.h"
#include "diagon/codecs/BKDWriter.h"
#include "diagon/codecs/PointValuesWriter.h"
#include "diagon/index/BKDConfig.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/index/PointValues.h"
#include "diagon/store/FSDirectory.h"
#include "diagon/store/IOContext.h"
#include "diagon/util/NumericUtils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <random>
#include <set>
#include <vector>

namespace fs = std::filesystem;
using namespace diagon;

class BKDTreeTest : public ::testing::Test {
protected:
    void SetUp() override {
        testDir_ = fs::temp_directory_path() / ("bkd_test_" + std::to_string(getpid()));
        fs::create_directories(testDir_);
        directory_ = store::FSDirectory::open(testDir_.string());
    }

    void TearDown() override {
        directory_.reset();
        fs::remove_all(testDir_);
    }

    fs::path testDir_;
    std::unique_ptr<store::Directory> directory_;
};

// ==================== BKDConfig Tests ====================

TEST_F(BKDTreeTest, ConfigForLong) {
    auto config = index::BKDConfig::forLong();
    EXPECT_EQ(config.numDims, 1);
    EXPECT_EQ(config.numIndexDims, 1);
    EXPECT_EQ(config.bytesPerDim, 8);
    EXPECT_EQ(config.packedBytesLength, 8);
    EXPECT_EQ(config.packedIndexBytesLength, 8);
    EXPECT_EQ(config.maxPointsPerLeaf, 512);
}

TEST_F(BKDTreeTest, ConfigValidation) {
    EXPECT_THROW(index::BKDConfig(0, 0, 8), std::invalid_argument);
    EXPECT_THROW(index::BKDConfig(17, 1, 8), std::invalid_argument);
    EXPECT_THROW(index::BKDConfig(1, 2, 8), std::invalid_argument);
    EXPECT_THROW(index::BKDConfig(1, 1, 0), std::invalid_argument);
    EXPECT_NO_THROW(index::BKDConfig(1, 1, 8));
}

// ==================== BKDWriter + BKDReader Round-Trip ====================

TEST_F(BKDTreeTest, RoundTripBasic) {
    // Create some points
    int numPoints = 100;
    std::vector<int32_t> docIDs(numPoints);
    std::vector<uint8_t> packedValues(numPoints * 8);

    for (int i = 0; i < numPoints; i++) {
        docIDs[i] = i;
        int64_t value = static_cast<int64_t>(i * 10);
        util::NumericUtils::longToBytesBE(value, packedValues.data() + i * 8);
    }

    auto config = index::BKDConfig::forLong();

    // Write
    auto kdmOut = directory_->createOutput("test.kdm", store::IOContext::DEFAULT);
    auto kdiOut = directory_->createOutput("test.kdi", store::IOContext::DEFAULT);
    auto kddOut = directory_->createOutput("test.kdd", store::IOContext::DEFAULT);

    kdmOut->writeVInt(1);  // numFields = 1

    codecs::BKDWriter writer(config);
    writer.writeField("price", 0, docIDs, packedValues, *kdmOut, *kdiOut, *kddOut);

    kdmOut->close();
    kdiOut->close();
    kddOut->close();

    // Read
    auto kdmIn = directory_->openInput("test.kdm", store::IOContext::READ);
    auto kdiIn = directory_->openInput("test.kdi", store::IOContext::READ);
    auto kddIn = directory_->openInput("test.kdd", store::IOContext::READ);

    auto readers = codecs::BKDReader::loadFields(*kdmIn, *kdiIn, *kddIn);

    ASSERT_EQ(readers.size(), 1u);
    ASSERT_NE(readers.find("price"), readers.end());

    auto& reader = readers["price"];
    EXPECT_EQ(reader->getNumDimensions(), 1);
    EXPECT_EQ(reader->getBytesPerDimension(), 8);
    EXPECT_EQ(reader->size(), numPoints);
    EXPECT_EQ(reader->getDocCount(), numPoints);

    // Verify min/max
    int64_t minVal = util::NumericUtils::bytesToLongBE(reader->getMinPackedValue());
    int64_t maxVal = util::NumericUtils::bytesToLongBE(reader->getMaxPackedValue());
    EXPECT_EQ(minVal, 0);
    EXPECT_EQ(maxVal, 990);
}

TEST_F(BKDTreeTest, IntersectFullRange) {
    int numPoints = 200;
    std::vector<int32_t> docIDs(numPoints);
    std::vector<uint8_t> packedValues(numPoints * 8);

    for (int i = 0; i < numPoints; i++) {
        docIDs[i] = i;
        int64_t value = static_cast<int64_t>(i);
        util::NumericUtils::longToBytesBE(value, packedValues.data() + i * 8);
    }

    auto config = index::BKDConfig::forLong();

    auto kdmOut = directory_->createOutput("test.kdm", store::IOContext::DEFAULT);
    auto kdiOut = directory_->createOutput("test.kdi", store::IOContext::DEFAULT);
    auto kddOut = directory_->createOutput("test.kdd", store::IOContext::DEFAULT);

    kdmOut->writeVInt(1);
    codecs::BKDWriter writer(config);
    writer.writeField("val", 0, docIDs, packedValues, *kdmOut, *kdiOut, *kddOut);

    kdmOut->close();
    kdiOut->close();
    kddOut->close();

    auto kdmIn = directory_->openInput("test.kdm", store::IOContext::READ);
    auto kdiIn = directory_->openInput("test.kdi", store::IOContext::READ);
    auto kddIn = directory_->openInput("test.kdd", store::IOContext::READ);

    auto readers = codecs::BKDReader::loadFields(*kdmIn, *kdiIn, *kddIn);
    auto& reader = readers["val"];

    // Query: [50, 150] — should match 101 points
    std::vector<uint8_t> lower(8), upper(8);
    util::NumericUtils::longToBytesBE(50, lower.data());
    util::NumericUtils::longToBytesBE(150, upper.data());

    std::set<int> matchedDocs;

    struct RangeVisitor : public index::PointValues::IntersectVisitor {
        std::set<int>& docs;
        const uint8_t* lower;
        const uint8_t* upper;

        RangeVisitor(std::set<int>& d, const uint8_t* lo, const uint8_t* hi)
            : docs(d)
            , lower(lo)
            , upper(hi) {}

        void visit(int docID) override { docs.insert(docID); }

        void visit(int docID, const uint8_t* packedValue) override {
            if (std::memcmp(packedValue, lower, 8) >= 0 &&
                std::memcmp(packedValue, upper, 8) <= 0) {
                docs.insert(docID);
            }
        }

        index::PointValues::Relation compare(const uint8_t* minPacked,
                                             const uint8_t* maxPacked) override {
            if (std::memcmp(maxPacked, lower, 8) < 0)
                return index::PointValues::Relation::CELL_OUTSIDE_QUERY;
            if (std::memcmp(minPacked, upper, 8) > 0)
                return index::PointValues::Relation::CELL_OUTSIDE_QUERY;
            if (std::memcmp(minPacked, lower, 8) >= 0 && std::memcmp(maxPacked, upper, 8) <= 0)
                return index::PointValues::Relation::CELL_INSIDE_QUERY;
            return index::PointValues::Relation::CELL_CROSSES_QUERY;
        }
    };

    RangeVisitor visitor(matchedDocs, lower.data(), upper.data());
    reader->intersect(visitor);

    EXPECT_EQ(matchedDocs.size(), 101u);  // [50..150] inclusive
    for (int i = 50; i <= 150; i++) {
        EXPECT_TRUE(matchedDocs.count(i) > 0) << "Missing doc " << i;
    }
}

TEST_F(BKDTreeTest, EmptyField) {
    auto kdmOut = directory_->createOutput("test.kdm", store::IOContext::DEFAULT);
    auto kdiOut = directory_->createOutput("test.kdi", store::IOContext::DEFAULT);
    auto kddOut = directory_->createOutput("test.kdd", store::IOContext::DEFAULT);

    kdmOut->writeVInt(0);  // No fields

    kdmOut->close();
    kdiOut->close();
    kddOut->close();

    auto kdmIn = directory_->openInput("test.kdm", store::IOContext::READ);
    auto kdiIn = directory_->openInput("test.kdi", store::IOContext::READ);
    auto kddIn = directory_->openInput("test.kdd", store::IOContext::READ);

    auto readers = codecs::BKDReader::loadFields(*kdmIn, *kdiIn, *kddIn);
    EXPECT_EQ(readers.size(), 0u);
}

TEST_F(BKDTreeTest, SinglePoint) {
    std::vector<int32_t> docIDs = {42};
    std::vector<uint8_t> packedValues(8);
    util::NumericUtils::longToBytesBE(12345, packedValues.data());

    auto config = index::BKDConfig::forLong();

    auto kdmOut = directory_->createOutput("test.kdm", store::IOContext::DEFAULT);
    auto kdiOut = directory_->createOutput("test.kdi", store::IOContext::DEFAULT);
    auto kddOut = directory_->createOutput("test.kdd", store::IOContext::DEFAULT);

    kdmOut->writeVInt(1);
    codecs::BKDWriter writer(config);
    writer.writeField("f", 0, docIDs, packedValues, *kdmOut, *kdiOut, *kddOut);

    kdmOut->close();
    kdiOut->close();
    kddOut->close();

    auto kdmIn = directory_->openInput("test.kdm", store::IOContext::READ);
    auto kdiIn = directory_->openInput("test.kdi", store::IOContext::READ);
    auto kddIn = directory_->openInput("test.kdd", store::IOContext::READ);

    auto readers = codecs::BKDReader::loadFields(*kdmIn, *kdiIn, *kddIn);
    auto& reader = readers["f"];
    EXPECT_EQ(reader->size(), 1);
    EXPECT_EQ(reader->getDocCount(), 1);

    int64_t minVal = util::NumericUtils::bytesToLongBE(reader->getMinPackedValue());
    int64_t maxVal = util::NumericUtils::bytesToLongBE(reader->getMaxPackedValue());
    EXPECT_EQ(minVal, 12345);
    EXPECT_EQ(maxVal, 12345);
}

TEST_F(BKDTreeTest, AllSameValues) {
    int numPoints = 50;
    std::vector<int32_t> docIDs(numPoints);
    std::vector<uint8_t> packedValues(numPoints * 8);

    for (int i = 0; i < numPoints; i++) {
        docIDs[i] = i;
        util::NumericUtils::longToBytesBE(999, packedValues.data() + i * 8);
    }

    auto config = index::BKDConfig::forLong();

    auto kdmOut = directory_->createOutput("test.kdm", store::IOContext::DEFAULT);
    auto kdiOut = directory_->createOutput("test.kdi", store::IOContext::DEFAULT);
    auto kddOut = directory_->createOutput("test.kdd", store::IOContext::DEFAULT);

    kdmOut->writeVInt(1);
    codecs::BKDWriter writer(config);
    writer.writeField("f", 0, docIDs, packedValues, *kdmOut, *kdiOut, *kddOut);

    kdmOut->close();
    kdiOut->close();
    kddOut->close();

    auto kdmIn = directory_->openInput("test.kdm", store::IOContext::READ);
    auto kdiIn = directory_->openInput("test.kdi", store::IOContext::READ);
    auto kddIn = directory_->openInput("test.kdd", store::IOContext::READ);

    auto readers = codecs::BKDReader::loadFields(*kdmIn, *kdiIn, *kddIn);
    auto& reader = readers["f"];

    int64_t minVal = util::NumericUtils::bytesToLongBE(reader->getMinPackedValue());
    int64_t maxVal = util::NumericUtils::bytesToLongBE(reader->getMaxPackedValue());
    EXPECT_EQ(minVal, 999);
    EXPECT_EQ(maxVal, 999);
}

TEST_F(BKDTreeTest, PointValuesWriterRoundTrip) {
    // Test PointValuesWriter (the buffer layer used by DWPT)
    index::FieldInfo fi;
    fi.name = "timestamp";
    fi.number = 0;
    fi.pointDimensionCount = 1;
    fi.pointIndexDimensionCount = 1;
    fi.pointNumBytes = 8;

    codecs::PointValuesWriter pvWriter("_test", 1000);

    for (int i = 0; i < 100; i++) {
        uint8_t packed[8];
        util::NumericUtils::longToBytesBE(static_cast<int64_t>(i * 100), packed);
        pvWriter.addPoint(fi, i, packed);
    }

    EXPECT_TRUE(pvWriter.hasPoints());
    EXPECT_GT(pvWriter.ramBytesUsed(), 0);

    auto kdmOut = directory_->createOutput("test.kdm", store::IOContext::DEFAULT);
    auto kdiOut = directory_->createOutput("test.kdi", store::IOContext::DEFAULT);
    auto kddOut = directory_->createOutput("test.kdd", store::IOContext::DEFAULT);

    pvWriter.flush(*kdmOut, *kdiOut, *kddOut);

    kdmOut->close();
    kdiOut->close();
    kddOut->close();

    auto kdmIn = directory_->openInput("test.kdm", store::IOContext::READ);
    auto kdiIn = directory_->openInput("test.kdi", store::IOContext::READ);
    auto kddIn = directory_->openInput("test.kdd", store::IOContext::READ);

    auto readers = codecs::BKDReader::loadFields(*kdmIn, *kdiIn, *kddIn);
    ASSERT_EQ(readers.size(), 1u);
    ASSERT_NE(readers.find("timestamp"), readers.end());

    auto& reader = readers["timestamp"];
    EXPECT_EQ(reader->size(), 100);
}

TEST_F(BKDTreeTest, LargeDataset) {
    // Test with enough points to create multiple inner nodes
    int numPoints = 2000;
    std::vector<int32_t> docIDs(numPoints);
    std::vector<uint8_t> packedValues(numPoints * 8);

    std::mt19937 rng(42);
    std::uniform_int_distribution<int64_t> dist(0, 1000000);

    for (int i = 0; i < numPoints; i++) {
        docIDs[i] = i;
        int64_t value = dist(rng);
        util::NumericUtils::longToBytesBE(value, packedValues.data() + i * 8);
    }

    auto config = index::BKDConfig::forLong();

    auto kdmOut = directory_->createOutput("test.kdm", store::IOContext::DEFAULT);
    auto kdiOut = directory_->createOutput("test.kdi", store::IOContext::DEFAULT);
    auto kddOut = directory_->createOutput("test.kdd", store::IOContext::DEFAULT);

    kdmOut->writeVInt(1);
    codecs::BKDWriter writer(config);
    writer.writeField("val", 0, docIDs, packedValues, *kdmOut, *kdiOut, *kddOut);

    kdmOut->close();
    kdiOut->close();
    kddOut->close();

    auto kdmIn = directory_->openInput("test.kdm", store::IOContext::READ);
    auto kdiIn = directory_->openInput("test.kdi", store::IOContext::READ);
    auto kddIn = directory_->openInput("test.kdd", store::IOContext::READ);

    auto readers = codecs::BKDReader::loadFields(*kdmIn, *kdiIn, *kddIn);
    auto& reader = readers["val"];

    EXPECT_EQ(reader->size(), numPoints);

    // Query: [100000, 200000]
    std::vector<uint8_t> lower(8), upper(8);
    util::NumericUtils::longToBytesBE(100000, lower.data());
    util::NumericUtils::longToBytesBE(200000, upper.data());

    // Count expected matches
    int expected = 0;
    for (int i = 0; i < numPoints; i++) {
        int64_t val = util::NumericUtils::bytesToLongBE(packedValues.data() + i * 8);
        if (val >= 100000 && val <= 200000) {
            expected++;
        }
    }

    // Note: packedValues were reordered by writer (sorted). Use the reader to verify.
    std::set<int> matched;
    struct CountVisitor : public index::PointValues::IntersectVisitor {
        std::set<int>& docs;
        const uint8_t* lo;
        const uint8_t* hi;

        CountVisitor(std::set<int>& d, const uint8_t* l, const uint8_t* h)
            : docs(d)
            , lo(l)
            , hi(h) {}

        void visit(int docID) override { docs.insert(docID); }

        void visit(int docID, const uint8_t* packedValue) override {
            if (std::memcmp(packedValue, lo, 8) >= 0 && std::memcmp(packedValue, hi, 8) <= 0) {
                docs.insert(docID);
            }
        }

        index::PointValues::Relation compare(const uint8_t* minPacked,
                                             const uint8_t* maxPacked) override {
            if (std::memcmp(maxPacked, lo, 8) < 0)
                return index::PointValues::Relation::CELL_OUTSIDE_QUERY;
            if (std::memcmp(minPacked, hi, 8) > 0)
                return index::PointValues::Relation::CELL_OUTSIDE_QUERY;
            if (std::memcmp(minPacked, lo, 8) >= 0 && std::memcmp(maxPacked, hi, 8) <= 0)
                return index::PointValues::Relation::CELL_INSIDE_QUERY;
            return index::PointValues::Relation::CELL_CROSSES_QUERY;
        }
    };

    CountVisitor visitor(matched, lower.data(), upper.data());
    reader->intersect(visitor);

    EXPECT_EQ(static_cast<int>(matched.size()), expected)
        << "Matched " << matched.size() << " but expected " << expected;
}

TEST_F(BKDTreeTest, NoPruningForOutOfRangeQuery) {
    int numPoints = 100;
    std::vector<int32_t> docIDs(numPoints);
    std::vector<uint8_t> packedValues(numPoints * 8);

    for (int i = 0; i < numPoints; i++) {
        docIDs[i] = i;
        util::NumericUtils::longToBytesBE(static_cast<int64_t>(i), packedValues.data() + i * 8);
    }

    auto config = index::BKDConfig::forLong();

    auto kdmOut = directory_->createOutput("test.kdm", store::IOContext::DEFAULT);
    auto kdiOut = directory_->createOutput("test.kdi", store::IOContext::DEFAULT);
    auto kddOut = directory_->createOutput("test.kdd", store::IOContext::DEFAULT);

    kdmOut->writeVInt(1);
    codecs::BKDWriter writer(config);
    writer.writeField("val", 0, docIDs, packedValues, *kdmOut, *kdiOut, *kddOut);

    kdmOut->close();
    kdiOut->close();
    kddOut->close();

    auto kdmIn = directory_->openInput("test.kdm", store::IOContext::READ);
    auto kdiIn = directory_->openInput("test.kdi", store::IOContext::READ);
    auto kddIn = directory_->openInput("test.kdd", store::IOContext::READ);

    auto readers = codecs::BKDReader::loadFields(*kdmIn, *kdiIn, *kddIn);
    auto& reader = readers["val"];

    // Query: [500, 600] — completely outside range [0, 99]
    std::vector<uint8_t> lower(8), upper(8);
    util::NumericUtils::longToBytesBE(500, lower.data());
    util::NumericUtils::longToBytesBE(600, upper.data());

    std::set<int> matched;
    struct EmptyVisitor : public index::PointValues::IntersectVisitor {
        std::set<int>& docs;
        const uint8_t* lo;
        const uint8_t* hi;

        EmptyVisitor(std::set<int>& d, const uint8_t* l, const uint8_t* h)
            : docs(d)
            , lo(l)
            , hi(h) {}

        void visit(int docID) override { docs.insert(docID); }
        void visit(int docID, const uint8_t* packedValue) override {
            if (std::memcmp(packedValue, lo, 8) >= 0 && std::memcmp(packedValue, hi, 8) <= 0)
                docs.insert(docID);
        }
        index::PointValues::Relation compare(const uint8_t* minPacked,
                                             const uint8_t* maxPacked) override {
            if (std::memcmp(maxPacked, lo, 8) < 0)
                return index::PointValues::Relation::CELL_OUTSIDE_QUERY;
            if (std::memcmp(minPacked, hi, 8) > 0)
                return index::PointValues::Relation::CELL_OUTSIDE_QUERY;
            if (std::memcmp(minPacked, lo, 8) >= 0 && std::memcmp(maxPacked, hi, 8) <= 0)
                return index::PointValues::Relation::CELL_INSIDE_QUERY;
            return index::PointValues::Relation::CELL_CROSSES_QUERY;
        }
    };

    EmptyVisitor visitor(matched, lower.data(), upper.data());
    reader->intersect(visitor);

    EXPECT_EQ(matched.size(), 0u);
}
