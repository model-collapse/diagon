// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/Codec.h"
#include "diagon/codecs/ColumnFormat.h"
#include "diagon/codecs/DocValuesFormat.h"
#include "diagon/codecs/LiveDocsFormat.h"
#include "diagon/codecs/NormsFormat.h"
#include "diagon/codecs/OtherFormats.h"
#include "diagon/codecs/PostingsFormat.h"

#include <memory>
#include <string>

namespace diagon {
namespace codecs {
namespace lucene104 {

// ==================== Stub Format Implementations ====================

/**
 * Lucene104 PostingsFormat (stub)
 */
class Lucene104PostingsFormat : public PostingsFormat {
public:
    std::string getName() const override { return "Lucene104PostingsFormat"; }

    std::unique_ptr<FieldsConsumer> fieldsConsumer(SegmentWriteState& state) override {
        return nullptr;  // Stub
    }

    std::unique_ptr<FieldsProducer> fieldsProducer(SegmentReadState& state) override {
        return nullptr;  // Stub
    }
};

/**
 * Lucene104 DocValuesFormat (stub)
 */
class Lucene104DocValuesFormat : public DocValuesFormat {
public:
    std::string getName() const override { return "Lucene104DocValuesFormat"; }

    std::unique_ptr<DocValuesConsumer> fieldsConsumer(SegmentWriteState& state) override {
        return nullptr;  // Stub
    }

    std::unique_ptr<DocValuesProducer> fieldsProducer(SegmentReadState& state) override {
        return nullptr;  // Stub
    }
};

/**
 * MergeTree ColumnFormat (stub - ClickHouse-style)
 */
class MergeTreeColumnFormat : public ColumnFormat {
public:
    std::string getName() const override { return "MergeTreeColumnFormat"; }

    std::unique_ptr<ColumnsConsumer> fieldsConsumer(SegmentWriteState& state) override {
        return nullptr;  // Stub
    }

    std::unique_ptr<ColumnsProducer> fieldsProducer(SegmentReadState& state) override {
        return nullptr;  // Stub
    }

    DataPartType selectPartType(int64_t estimatedBytes, int32_t estimatedDocs) const override {
        // Use COMPACT for small segments (< 10MB or < 100k docs)
        // Use WIDE for large segments
        if (estimatedBytes < 10 * 1024 * 1024 || estimatedDocs < 100000) {
            return DataPartType::COMPACT;
        }
        return DataPartType::WIDE;
    }
};

/**
 * Stub format implementations for other formats
 */
class Lucene104StoredFieldsFormat : public StoredFieldsFormat {
public:
    std::string getName() const override { return "Lucene104StoredFieldsFormat"; }
};

class Lucene104TermVectorsFormat : public TermVectorsFormat {
public:
    std::string getName() const override { return "Lucene104TermVectorsFormat"; }
};

class Lucene104FieldInfosFormat : public FieldInfosFormat {
public:
    std::string getName() const override { return "Lucene104FieldInfosFormat"; }
};

class Lucene104SegmentInfoFormat : public SegmentInfoFormat {
public:
    std::string getName() const override { return "Lucene104SegmentInfoFormat"; }
};

// NormsFormat is now implemented in Lucene104NormsWriter.h
// No need for stub here

// LiveDocsFormat is now a concrete class (see codecs/LiveDocsFormat.h)
// No need for Lucene104LiveDocsFormat stub

class Lucene104PointsFormat : public PointsFormat {
public:
    std::string getName() const override { return "Lucene104PointsFormat"; }
};

class Lucene104VectorFormat : public VectorFormat {
public:
    std::string getName() const override { return "Lucene104VectorFormat"; }
};

// ==================== Lucene104Codec ====================

/**
 * Default codec implementation (version 104)
 *
 * Based on: org.apache.lucene.codecs.lucene104.Lucene104Codec
 *
 * NOTE: All format implementations are stubs. Full functionality will be
 * implemented incrementally in future tasks as the underlying systems
 * (FST, compression, column storage, etc.) are built.
 */
class Lucene104Codec : public Codec {
public:
    Lucene104Codec();

    // ==================== Format Accessors ====================

    PostingsFormat& postingsFormat() override { return *postingsFormat_; }
    DocValuesFormat& docValuesFormat() override { return *docValuesFormat_; }
    ColumnFormat& columnFormat() override { return *columnFormat_; }
    StoredFieldsFormat& storedFieldsFormat() override { return *storedFieldsFormat_; }
    TermVectorsFormat& termVectorsFormat() override { return *termVectorsFormat_; }
    FieldInfosFormat& fieldInfosFormat() override { return *fieldInfosFormat_; }
    SegmentInfoFormat& segmentInfoFormat() override { return *segmentInfoFormat_; }
    NormsFormat& normsFormat() override { return *normsFormat_; }
    LiveDocsFormat& liveDocsFormat() override { return *liveDocsFormat_; }
    PointsFormat& pointsFormat() override { return *pointsFormat_; }
    VectorFormat& vectorFormat() override { return *vectorFormat_; }

    // ==================== Identification ====================

    std::string getName() const override { return "Lucene104"; }

    // ==================== Capabilities ====================

    uint64_t getCapabilities() const override {
        return static_cast<uint64_t>(Capability::POSTINGS) |
               static_cast<uint64_t>(Capability::DOC_VALUES) |
               static_cast<uint64_t>(Capability::COLUMN_STORAGE) |
               static_cast<uint64_t>(Capability::SKIP_INDEXES) |
               static_cast<uint64_t>(Capability::SIMD_ACCELERATION) |
               static_cast<uint64_t>(Capability::COMPRESSION_ZSTD) |
               static_cast<uint64_t>(Capability::ADAPTIVE_GRANULES);
        // Note: VECTORS not included (deferred to v2.0)
    }

private:
    std::unique_ptr<PostingsFormat> postingsFormat_;
    std::unique_ptr<DocValuesFormat> docValuesFormat_;
    std::unique_ptr<ColumnFormat> columnFormat_;
    std::unique_ptr<StoredFieldsFormat> storedFieldsFormat_;
    std::unique_ptr<TermVectorsFormat> termVectorsFormat_;
    std::unique_ptr<FieldInfosFormat> fieldInfosFormat_;
    std::unique_ptr<SegmentInfoFormat> segmentInfoFormat_;
    std::unique_ptr<NormsFormat> normsFormat_;
    std::unique_ptr<LiveDocsFormat> liveDocsFormat_;
    std::unique_ptr<PointsFormat> pointsFormat_;
    std::unique_ptr<VectorFormat> vectorFormat_;
};

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
