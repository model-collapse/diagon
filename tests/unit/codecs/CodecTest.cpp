// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/Codec.h"

#include "diagon/codecs/ColumnFormat.h"
#include "diagon/codecs/DocValuesFormat.h"
#include "diagon/codecs/LiveDocsFormat.h"
#include "diagon/codecs/NormsFormat.h"
#include "diagon/codecs/PostingsFormat.h"
#include "diagon/codecs/lucene104/Lucene104Codec.h"
#include "diagon/codecs/lucene104/Lucene104NormsWriter.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace diagon::codecs;
using namespace diagon::codecs::lucene104;

// ==================== Codec Tests ====================

TEST(CodecTest, GetDefaultCodec) {
    Codec& codec = Codec::getDefault();
    EXPECT_EQ("Lucene104", codec.getName());
}

TEST(CodecTest, GetCodecByName) {
    Codec& codec = Codec::forName("Lucene104");
    EXPECT_EQ("Lucene104", codec.getName());
}

TEST(CodecTest, GetCodecByNameNotFound) {
    EXPECT_THROW(Codec::forName("NonExistent"), std::runtime_error);
}

TEST(CodecTest, AvailableCodecs) {
    auto codecs = Codec::availableCodecs();
    EXPECT_GE(codecs.size(), 1);

    // Should contain Lucene104
    bool foundLucene104 = false;
    for (const auto& name : codecs) {
        if (name == "Lucene104") {
            foundLucene104 = true;
            break;
        }
    }
    EXPECT_TRUE(foundLucene104);
}

TEST(CodecTest, RegisterCustomCodec) {
    // Create a custom codec
    class CustomCodec : public Codec {
    public:
        std::string getName() const override { return "Custom"; }
        uint64_t getCapabilities() const override { return 0; }

        // Stub implementations
        PostingsFormat& postingsFormat() override {
            static Lucene104PostingsFormat fmt;
            return fmt;
        }
        DocValuesFormat& docValuesFormat() override {
            static Lucene104DocValuesFormat fmt;
            return fmt;
        }
        ColumnFormat& columnFormat() override {
            static MergeTreeColumnFormat fmt;
            return fmt;
        }
        StoredFieldsFormat& storedFieldsFormat() override {
            static Lucene104StoredFieldsFormat fmt;
            return fmt;
        }
        TermVectorsFormat& termVectorsFormat() override {
            static Lucene104TermVectorsFormat fmt;
            return fmt;
        }
        FieldInfosFormat& fieldInfosFormat() override {
            static Lucene104FieldInfosFormat fmt;
            return fmt;
        }
        SegmentInfoFormat& segmentInfoFormat() override {
            static Lucene104SegmentInfoFormat fmt;
            return fmt;
        }
        NormsFormat& normsFormat() override {
            static Lucene104NormsFormat fmt;
            return fmt;
        }
        LiveDocsFormat& liveDocsFormat() override {
            static LiveDocsFormat fmt;
            return fmt;
        }
        PointsFormat& pointsFormat() override {
            static Lucene104PointsFormat fmt;
            return fmt;
        }
        VectorFormat& vectorFormat() override {
            static Lucene104VectorFormat fmt;
            return fmt;
        }
    };

    // Register custom codec
    Codec::registerCodec("Custom", []() { return std::make_unique<CustomCodec>(); });

    // Verify it's registered
    Codec& codec = Codec::forName("Custom");
    EXPECT_EQ("Custom", codec.getName());
}

TEST(CodecTest, CodecSingleton) {
    // Same name should return same instance
    Codec& codec1 = Codec::forName("Lucene104");
    Codec& codec2 = Codec::forName("Lucene104");
    EXPECT_EQ(&codec1, &codec2);
}

// ==================== Lucene104Codec Tests ====================

TEST(Lucene104CodecTest, Name) {
    Lucene104Codec codec;
    EXPECT_EQ("Lucene104", codec.getName());
}

TEST(Lucene104CodecTest, PostingsFormat) {
    Lucene104Codec codec;
    PostingsFormat& format = codec.postingsFormat();
    EXPECT_EQ("Lucene104PostingsFormat", format.getName());
}

TEST(Lucene104CodecTest, DocValuesFormat) {
    Lucene104Codec codec;
    DocValuesFormat& format = codec.docValuesFormat();
    EXPECT_EQ("Lucene104DocValuesFormat", format.getName());
}

TEST(Lucene104CodecTest, ColumnFormat) {
    Lucene104Codec codec;
    ColumnFormat& format = codec.columnFormat();
    EXPECT_EQ("MergeTreeColumnFormat", format.getName());
}

TEST(Lucene104CodecTest, StoredFieldsFormat) {
    Lucene104Codec codec;
    StoredFieldsFormat& format = codec.storedFieldsFormat();
    EXPECT_EQ("Lucene104StoredFieldsFormat", format.getName());
}

TEST(Lucene104CodecTest, TermVectorsFormat) {
    Lucene104Codec codec;
    TermVectorsFormat& format = codec.termVectorsFormat();
    EXPECT_EQ("Lucene104TermVectorsFormat", format.getName());
}

TEST(Lucene104CodecTest, FieldInfosFormat) {
    Lucene104Codec codec;
    FieldInfosFormat& format = codec.fieldInfosFormat();
    EXPECT_EQ("Lucene104FieldInfosFormat", format.getName());
}

TEST(Lucene104CodecTest, SegmentInfoFormat) {
    Lucene104Codec codec;
    SegmentInfoFormat& format = codec.segmentInfoFormat();
    EXPECT_EQ("Lucene104SegmentInfoFormat", format.getName());
}

TEST(Lucene104CodecTest, NormsFormat) {
    Lucene104Codec codec;
    NormsFormat& format = codec.normsFormat();
    EXPECT_EQ("Lucene104Norms", format.getName());
}

TEST(Lucene104CodecTest, LiveDocsFormat) {
    Lucene104Codec codec;
    LiveDocsFormat& format = codec.liveDocsFormat();
    // LiveDocsFormat is a concrete class without getName()
    // Just verify we can get the format
    EXPECT_NE(&format, nullptr);
}

TEST(Lucene104CodecTest, PointsFormat) {
    Lucene104Codec codec;
    PointsFormat& format = codec.pointsFormat();
    EXPECT_EQ("Lucene104PointsFormat", format.getName());
}

TEST(Lucene104CodecTest, VectorFormat) {
    Lucene104Codec codec;
    VectorFormat& format = codec.vectorFormat();
    EXPECT_EQ("Lucene104VectorFormat", format.getName());
}

TEST(Lucene104CodecTest, Capabilities) {
    Lucene104Codec codec;
    uint64_t caps = codec.getCapabilities();

    // Should have these capabilities
    EXPECT_TRUE(codec.hasCapability(Codec::Capability::POSTINGS));
    EXPECT_TRUE(codec.hasCapability(Codec::Capability::DOC_VALUES));
    EXPECT_TRUE(codec.hasCapability(Codec::Capability::COLUMN_STORAGE));
    EXPECT_TRUE(codec.hasCapability(Codec::Capability::SKIP_INDEXES));
    EXPECT_TRUE(codec.hasCapability(Codec::Capability::SIMD_ACCELERATION));
    EXPECT_TRUE(codec.hasCapability(Codec::Capability::COMPRESSION_ZSTD));
    EXPECT_TRUE(codec.hasCapability(Codec::Capability::ADAPTIVE_GRANULES));

    // Should NOT have this capability (deferred to v2.0)
    EXPECT_FALSE(codec.hasCapability(Codec::Capability::VECTORS));
}

TEST(Lucene104CodecTest, ConcurrentAccess) {
    Lucene104Codec codec;
    EXPECT_FALSE(codec.supportsConcurrentAccess());
}

// ==================== ColumnFormat Tests ====================

TEST(ColumnFormatTest, SelectPartTypeSmallSegment) {
    MergeTreeColumnFormat format;

    // Small segment (< 10MB) → COMPACT
    DataPartType type = format.selectPartType(5 * 1024 * 1024, 50000);
    EXPECT_EQ(DataPartType::COMPACT, type);
}

TEST(ColumnFormatTest, SelectPartTypeMediumSegment) {
    MergeTreeColumnFormat format;

    // Small docs (< 100k docs) → COMPACT
    DataPartType type = format.selectPartType(20 * 1024 * 1024, 50000);
    EXPECT_EQ(DataPartType::COMPACT, type);
}

TEST(ColumnFormatTest, SelectPartTypeLargeSegment) {
    MergeTreeColumnFormat format;

    // Large segment (>= 10MB and >= 100k docs) → WIDE
    DataPartType type = format.selectPartType(50 * 1024 * 1024, 200000);
    EXPECT_EQ(DataPartType::WIDE, type);
}

TEST(ColumnFormatTest, SelectPartTypeEdgeCaseBytes) {
    MergeTreeColumnFormat format;

    // Exactly 10MB
    DataPartType type1 = format.selectPartType(10 * 1024 * 1024, 200000);
    EXPECT_EQ(DataPartType::WIDE, type1);

    // Just under 10MB
    DataPartType type2 = format.selectPartType(10 * 1024 * 1024 - 1, 200000);
    EXPECT_EQ(DataPartType::COMPACT, type2);
}

TEST(ColumnFormatTest, SelectPartTypeEdgeCaseDocs) {
    MergeTreeColumnFormat format;

    // Exactly 100k docs
    DataPartType type1 = format.selectPartType(50 * 1024 * 1024, 100000);
    EXPECT_EQ(DataPartType::WIDE, type1);

    // Just under 100k docs
    DataPartType type2 = format.selectPartType(50 * 1024 * 1024, 99999);
    EXPECT_EQ(DataPartType::COMPACT, type2);
}

// ==================== Capability Tests ====================

TEST(CapabilityTest, SingleCapability) {
    // Test individual capabilities
    uint64_t caps = static_cast<uint64_t>(Codec::Capability::POSTINGS);

    Codec* testCodec = nullptr;
    class TestCodec : public Codec {
    public:
        TestCodec(uint64_t c)
            : caps_(c) {}
        std::string getName() const override { return "Test"; }
        uint64_t getCapabilities() const override { return caps_; }

        // Stub implementations
        PostingsFormat& postingsFormat() override {
            static Lucene104PostingsFormat fmt;
            return fmt;
        }
        DocValuesFormat& docValuesFormat() override {
            static Lucene104DocValuesFormat fmt;
            return fmt;
        }
        ColumnFormat& columnFormat() override {
            static MergeTreeColumnFormat fmt;
            return fmt;
        }
        StoredFieldsFormat& storedFieldsFormat() override {
            static Lucene104StoredFieldsFormat fmt;
            return fmt;
        }
        TermVectorsFormat& termVectorsFormat() override {
            static Lucene104TermVectorsFormat fmt;
            return fmt;
        }
        FieldInfosFormat& fieldInfosFormat() override {
            static Lucene104FieldInfosFormat fmt;
            return fmt;
        }
        SegmentInfoFormat& segmentInfoFormat() override {
            static Lucene104SegmentInfoFormat fmt;
            return fmt;
        }
        NormsFormat& normsFormat() override {
            static Lucene104NormsFormat fmt;
            return fmt;
        }
        LiveDocsFormat& liveDocsFormat() override {
            static LiveDocsFormat fmt;
            return fmt;
        }
        PointsFormat& pointsFormat() override {
            static Lucene104PointsFormat fmt;
            return fmt;
        }
        VectorFormat& vectorFormat() override {
            static Lucene104VectorFormat fmt;
            return fmt;
        }

    private:
        uint64_t caps_;
    };

    TestCodec codec(caps);
    EXPECT_TRUE(codec.hasCapability(Codec::Capability::POSTINGS));
    EXPECT_FALSE(codec.hasCapability(Codec::Capability::DOC_VALUES));
}

TEST(CapabilityTest, MultipleCapabilities) {
    uint64_t caps = static_cast<uint64_t>(Codec::Capability::POSTINGS) |
                    static_cast<uint64_t>(Codec::Capability::DOC_VALUES) |
                    static_cast<uint64_t>(Codec::Capability::COLUMN_STORAGE);

    class TestCodec : public Codec {
    public:
        TestCodec(uint64_t c)
            : caps_(c) {}
        std::string getName() const override { return "Test"; }
        uint64_t getCapabilities() const override { return caps_; }

        // Stub implementations (same as above)
        PostingsFormat& postingsFormat() override {
            static Lucene104PostingsFormat fmt;
            return fmt;
        }
        DocValuesFormat& docValuesFormat() override {
            static Lucene104DocValuesFormat fmt;
            return fmt;
        }
        ColumnFormat& columnFormat() override {
            static MergeTreeColumnFormat fmt;
            return fmt;
        }
        StoredFieldsFormat& storedFieldsFormat() override {
            static Lucene104StoredFieldsFormat fmt;
            return fmt;
        }
        TermVectorsFormat& termVectorsFormat() override {
            static Lucene104TermVectorsFormat fmt;
            return fmt;
        }
        FieldInfosFormat& fieldInfosFormat() override {
            static Lucene104FieldInfosFormat fmt;
            return fmt;
        }
        SegmentInfoFormat& segmentInfoFormat() override {
            static Lucene104SegmentInfoFormat fmt;
            return fmt;
        }
        NormsFormat& normsFormat() override {
            static Lucene104NormsFormat fmt;
            return fmt;
        }
        LiveDocsFormat& liveDocsFormat() override {
            static LiveDocsFormat fmt;
            return fmt;
        }
        PointsFormat& pointsFormat() override {
            static Lucene104PointsFormat fmt;
            return fmt;
        }
        VectorFormat& vectorFormat() override {
            static Lucene104VectorFormat fmt;
            return fmt;
        }

    private:
        uint64_t caps_;
    };

    TestCodec codec(caps);
    EXPECT_TRUE(codec.hasCapability(Codec::Capability::POSTINGS));
    EXPECT_TRUE(codec.hasCapability(Codec::Capability::DOC_VALUES));
    EXPECT_TRUE(codec.hasCapability(Codec::Capability::COLUMN_STORAGE));
    EXPECT_FALSE(codec.hasCapability(Codec::Capability::VECTORS));
}

TEST(CapabilityTest, NoCapabilities) {
    class TestCodec : public Codec {
    public:
        std::string getName() const override { return "Test"; }
        uint64_t getCapabilities() const override { return 0; }

        // Stub implementations (same as above)
        PostingsFormat& postingsFormat() override {
            static Lucene104PostingsFormat fmt;
            return fmt;
        }
        DocValuesFormat& docValuesFormat() override {
            static Lucene104DocValuesFormat fmt;
            return fmt;
        }
        ColumnFormat& columnFormat() override {
            static MergeTreeColumnFormat fmt;
            return fmt;
        }
        StoredFieldsFormat& storedFieldsFormat() override {
            static Lucene104StoredFieldsFormat fmt;
            return fmt;
        }
        TermVectorsFormat& termVectorsFormat() override {
            static Lucene104TermVectorsFormat fmt;
            return fmt;
        }
        FieldInfosFormat& fieldInfosFormat() override {
            static Lucene104FieldInfosFormat fmt;
            return fmt;
        }
        SegmentInfoFormat& segmentInfoFormat() override {
            static Lucene104SegmentInfoFormat fmt;
            return fmt;
        }
        NormsFormat& normsFormat() override {
            static Lucene104NormsFormat fmt;
            return fmt;
        }
        LiveDocsFormat& liveDocsFormat() override {
            static LiveDocsFormat fmt;
            return fmt;
        }
        PointsFormat& pointsFormat() override {
            static Lucene104PointsFormat fmt;
            return fmt;
        }
        VectorFormat& vectorFormat() override {
            static Lucene104VectorFormat fmt;
            return fmt;
        }
    };

    TestCodec codec;
    EXPECT_FALSE(codec.hasCapability(Codec::Capability::POSTINGS));
    EXPECT_FALSE(codec.hasCapability(Codec::Capability::DOC_VALUES));
    EXPECT_FALSE(codec.hasCapability(Codec::Capability::COLUMN_STORAGE));
}
