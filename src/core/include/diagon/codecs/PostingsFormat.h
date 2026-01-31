// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/SegmentWriteState.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace diagon {

// Forward declarations in correct namespace
namespace index {
class Fields;
class NormsProducer;
class Terms;
}

namespace codecs {

// Forward declarations
class FieldsConsumer;
class FieldsProducer;

/**
 * PostingsFormat encodes/decodes inverted index.
 *
 * Producer/Consumer pattern:
 * - FieldsConsumer: Write during indexing
 * - FieldsProducer: Read during searching
 *
 * File extensions: .tim, .tip, .doc, .pos, .pay
 *
 * Based on: org.apache.lucene.codecs.PostingsFormat
 *
 * NOTE: Stub implementation - full functionality requires:
 * - FST term dictionary (Task TBD)
 * - Postings compression (Task TBD)
 * - Skip lists (Task TBD)
 */
class PostingsFormat {
public:
    virtual ~PostingsFormat() = default;

    /**
     * Unique name (e.g., "Lucene104")
     */
    virtual std::string getName() const = 0;

    // ==================== Producer/Consumer ====================

    /**
     * Create consumer for writing postings
     * Called during segment flush
     *
     * NOTE: Stub - returns nullptr until formats are implemented
     */
    virtual std::unique_ptr<FieldsConsumer> fieldsConsumer(index::SegmentWriteState& state) = 0;

    /**
     * Create producer for reading postings
     * Called when opening segment
     *
     * NOTE: Stub - returns nullptr until formats are implemented
     */
    virtual std::unique_ptr<FieldsProducer> fieldsProducer(index::SegmentReadState& state) = 0;

    // ==================== Factory & Registration ====================

    /**
     * Get format by name
     * @throws std::runtime_error if not found
     */
    static PostingsFormat& forName(const std::string& name);

    /**
     * Register format
     */
    static void registerFormat(const std::string& name,
                               std::function<std::unique_ptr<PostingsFormat>()> factory);

private:
    static std::unordered_map<std::string, std::function<std::unique_ptr<PostingsFormat>()>>&
    getRegistry();
};

/**
 * Write-side API for postings
 *
 * Uses streaming "pull" API: codec iterates over Fields/Terms/Postings
 * provided by the indexer.
 *
 * Based on: org.apache.lucene.codecs.FieldsConsumer
 */
class FieldsConsumer {
public:
    virtual ~FieldsConsumer() = default;

    /**
     * Write all fields, terms and postings using streaming API
     *
     * This is the "pull" API: codec iterates over the provided Fields
     * and writes terms/postings to disk format.
     *
     * @param fields Fields to write (provides iterator over fields/terms)
     * @param norms Norms producer (optional, can be nullptr)
     */
    virtual void write(index::Fields& fields, index::NormsProducer* norms) = 0;

    /**
     * Close and flush
     */
    virtual void close() = 0;
};

/**
 * Read-side API for postings
 *
 * Provides access to Terms for each field in a segment.
 * Implementations handle format-specific details (e.g., Lucene104, Simple).
 */
class FieldsProducer {
public:
    virtual ~FieldsProducer() = default;

    /**
     * Get Terms for a field
     *
     * @param field Field name
     * @return Terms instance, or nullptr if field doesn't exist or has no postings
     */
    virtual std::unique_ptr<::diagon::index::Terms> terms(const std::string& field) = 0;

    /**
     * Check file integrity (checksums)
     */
    virtual void checkIntegrity() = 0;

    /**
     * Close
     */
    virtual void close() = 0;
};

}  // namespace codecs
}  // namespace diagon
