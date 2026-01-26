// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/DocValues.h"
#include "diagon/index/FieldInfo.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace diagon {
namespace codecs {

// Forward declarations
class NormsConsumer;
class NormsProducer;
class SegmentWriteState;
class SegmentReadState;

/**
 * NormsFormat - Encodes per-document length normalization factors
 *
 * Based on: org.apache.lucene.codecs.NormsFormat
 *
 * Norms store a single numeric value per document per indexed field.
 * They're used for length normalization in BM25 scoring.
 *
 * Typically stored as 1 byte per document, encoding:
 * - Document field length (number of tokens)
 * - Field boost (if specified)
 *
 * Phase 5 Design:
 * - Simple byte array storage (1 byte per doc)
 * - No compression initially
 * - Direct file I/O
 *
 * Future Enhancements:
 * - Compression for sparse norms
 * - Skip large norms files (similar to Lucene90)
 * - Merge with doc values storage
 */
class NormsFormat {
public:
    virtual ~NormsFormat() = default;

    /**
     * Unique name for this format
     */
    virtual std::string getName() const = 0;

    // ==================== Producer/Consumer ====================

    /**
     * Create consumer for writing norms
     *
     * @param state Segment write state
     * @return Consumer instance
     */
    virtual std::unique_ptr<NormsConsumer> normsConsumer(SegmentWriteState& state) = 0;

    /**
     * Create producer for reading norms
     *
     * @param state Segment read state
     * @return Producer instance
     */
    virtual std::unique_ptr<NormsProducer> normsProducer(SegmentReadState& state) = 0;

    // ==================== Factory ====================

    static NormsFormat& forName(const std::string& name);
    static void registerFormat(const std::string& name,
                               std::function<std::unique_ptr<NormsFormat>()> factory);

private:
    static std::unordered_map<std::string, std::function<std::unique_ptr<NormsFormat>()>>&
    getRegistry();
};

/**
 * NormsConsumer - Write norms to disk
 *
 * Based on: org.apache.lucene.codecs.NormsConsumer
 *
 * Writes per-document normalization factors during indexing.
 * Called once per indexed field that has norms enabled.
 *
 * The producer passed to addNormsField provides the norms values
 * to be encoded (typically field lengths). The consumer encodes
 * them and writes to disk.
 */
class NormsConsumer {
public:
    virtual ~NormsConsumer() = default;

    /**
     * Write norms for a field
     *
     * @param field Field metadata
     * @param normsProducer Producer of norms values (doc → norm byte)
     * @throws IOException if write fails
     */
    virtual void addNormsField(const index::FieldInfo& field,
                               NormsProducer& normsProducer) = 0;

    /**
     * Close and flush any pending data
     */
    virtual void close() = 0;
};

/**
 * NormsProducer - Read norms from disk
 *
 * Based on: org.apache.lucene.codecs.NormsProducer
 *
 * Provides access to per-document normalization factors.
 * Used during search to apply length normalization in BM25 scoring.
 *
 * Norms are returned as NumericDocValues (doc → int64),
 * but typically stored as a single byte per document.
 */
class NormsProducer {
public:
    virtual ~NormsProducer() = default;

    /**
     * Get norms for a field
     *
     * @param field Field metadata
     * @return Iterator over norms (doc → norm value)
     * @throws IOException if field has no norms or read fails
     */
    virtual std::unique_ptr<index::NumericDocValues> getNorms(const index::FieldInfo& field) = 0;

    /**
     * Check integrity of all norms data
     * @throws IOException if integrity check fails
     */
    virtual void checkIntegrity() = 0;

    /**
     * Close and release resources
     */
    virtual void close() = 0;
};

}  // namespace codecs
}  // namespace diagon
