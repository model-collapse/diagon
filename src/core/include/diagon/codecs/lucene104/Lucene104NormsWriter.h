// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/NormsFormat.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/store/IndexOutput.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace diagon {
namespace codecs {
namespace lucene104 {

/**
 * Lucene104NormsWriter - Write norms using Lucene 10.4 format
 *
 * Based on: org.apache.lucene.codecs.lucene90.Lucene90NormsConsumer
 *
 * File Format:
 * - Extension: .nvd (norms data), .nvm (norms metadata)
 * - Format: Simple byte array, 1 byte per document
 * - Encoding: Norms stored as signed byte (-128 to 127)
 *
 * Norms Encoding:
 * - Raw field length: number of tokens in field
 * - Encoded: SmallFloat.floatToByte315(1.0f / sqrt(length))
 * - Shorter documents get higher norms (more weight)
 * - Longer documents get lower norms (less weight)
 *
 * Phase 5 Implementation:
 * - Simple byte array storage
 * - No compression
 * - Direct file I/O
 */
class Lucene104NormsWriter : public NormsConsumer {
public:
    /**
     * Constructor
     *
     * @param state Segment write state
     */
    explicit Lucene104NormsWriter(index::SegmentWriteState& state);

    /**
     * Destructor
     */
    ~Lucene104NormsWriter() override;

    // ==================== NormsConsumer Implementation ====================

    /**
     * Write norms for a field
     *
     * @param field Field metadata
     * @param normsProducer Producer of norms values
     */
    void addNormsField(const index::FieldInfo& field, NormsProducer& normsProducer) override;

    /**
     * Close and flush
     */
    void close() override;

private:
    /**
     * Encode field length to norm byte
     *
     * @param length Field length (number of tokens)
     * @return Norm byte (-128 to 127)
     */
    static int8_t encodeNormValue(int64_t length);

    /**
     * Write norms data file for a field
     *
     * @param field Field metadata
     * @param norms Norms values (doc → byte)
     */
    void writeNormsData(const index::FieldInfo& field, const std::vector<int8_t>& norms);

    index::SegmentWriteState& state_;
    std::unique_ptr<store::IndexOutput> data_;    // .nvd file
    std::unique_ptr<store::IndexOutput> meta_;    // .nvm file
    bool closed_{false};
};

/**
 * Lucene104NormsFormat - Norms format for Lucene 10.4
 */
class Lucene104NormsFormat : public NormsFormat {
public:
    std::string getName() const override { return "Lucene104Norms"; }

    std::unique_ptr<NormsConsumer> normsConsumer(index::SegmentWriteState& state) override;
    std::unique_ptr<NormsProducer> normsProducer(index::SegmentReadState& state) override;
};

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
