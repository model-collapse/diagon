// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/DocValuesFormat.h"
#include "diagon/codecs/NormsFormat.h"
#include "diagon/codecs/OtherFormats.h"
#include "diagon/codecs/PostingsFormat.h"

#include <stdexcept>

namespace diagon {
namespace codecs {
namespace lucene95 {

/**
 * Stub Lucene90 postings format — throws on access.
 * Will be replaced by a real Lucene90PostingsReader in Phase C.
 */
class Lucene90PostingsFormatStub : public PostingsFormat {
public:
    std::string getName() const override { return "Lucene90PostingsFormat"; }

    std::unique_ptr<FieldsConsumer> fieldsConsumer(index::SegmentWriteState& state) override {
        throw std::runtime_error(
            "Lucene90PostingsFormat: write not supported (read-only codec for OS indices)");
    }

    std::unique_ptr<FieldsProducer> fieldsProducer(index::SegmentReadState& state) override {
        throw std::runtime_error(
            "Lucene90PostingsFormat: reader not yet implemented (Phase C). "
            "Requires 128-block ForUtil + multi-level skip list + FST .tip reader.");
    }
};

/**
 * Stub Lucene90 norms format — throws on access.
 */
class Lucene90NormsFormatStub : public NormsFormat {
public:
    std::string getName() const override { return "Lucene90NormsFormat"; }

    std::unique_ptr<NormsConsumer> normsConsumer(index::SegmentWriteState& state) override {
        throw std::runtime_error(
            "Lucene90NormsFormat: write not supported (read-only codec)");
    }

    std::unique_ptr<NormsProducer> normsProducer(index::SegmentReadState& state) override {
        throw std::runtime_error(
            "Lucene90NormsFormat: reader not yet implemented. "
            "Requires Lucene90 norms wire format decoder.");
    }
};

/**
 * Stub Lucene90 doc values format — throws on access.
 */
class Lucene90DocValuesFormatStub : public DocValuesFormat {
public:
    std::string getName() const override { return "Lucene90DocValuesFormat"; }

    std::unique_ptr<DocValuesConsumer> fieldsConsumer(SegmentWriteState& state) override {
        throw std::runtime_error(
            "Lucene90DocValuesFormat: write not supported (read-only codec)");
    }

    std::unique_ptr<DocValuesProducer> fieldsProducer(SegmentReadState& state) override {
        throw std::runtime_error(
            "Lucene90DocValuesFormat: reader not yet implemented. "
            "Requires Lucene90 doc values wire format decoder.");
    }
};

/**
 * Stub Lucene90 points format — throws on access.
 */
class Lucene90PointsFormatStub : public PointsFormat {
public:
    std::string getName() const override { return "Lucene90PointsFormat"; }
};

/**
 * Stub term vectors format — throws on access.
 */
class Lucene90TermVectorsFormatStub : public TermVectorsFormat {
public:
    std::string getName() const override { return "Lucene90TermVectorsFormat"; }
};

/**
 * Stub vector format — throws on access.
 */
class Lucene95HnswVectorsFormatStub : public VectorFormat {
public:
    std::string getName() const override { return "Lucene95HnswVectorsFormat"; }
};

}  // namespace lucene95
}  // namespace codecs
}  // namespace diagon
