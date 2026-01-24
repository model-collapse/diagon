// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace diagon {
namespace codecs {

// Forward declarations (to be implemented in future tasks)
class FieldsConsumer;
class FieldsProducer;
class SegmentWriteState;
class SegmentReadState;

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
    virtual std::unique_ptr<FieldsConsumer> fieldsConsumer(SegmentWriteState& state) = 0;

    /**
     * Create producer for reading postings
     * Called when opening segment
     *
     * NOTE: Stub - returns nullptr until formats are implemented
     */
    virtual std::unique_ptr<FieldsProducer> fieldsProducer(SegmentReadState& state) = 0;

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
 * NOTE: Stub interface - to be implemented with actual postings encoding
 */
class FieldsConsumer {
public:
    virtual ~FieldsConsumer() = default;

    /**
     * Close and flush
     */
    virtual void close() = 0;

    // TODO: Add write() and merge() methods when Fields classes are implemented
};

/**
 * Read-side API for postings
 *
 * NOTE: Stub interface - to be implemented with actual postings decoding
 */
class FieldsProducer {
public:
    virtual ~FieldsProducer() = default;

    /**
     * Check file integrity (checksums)
     */
    virtual void checkIntegrity() = 0;

    /**
     * Close
     */
    virtual void close() = 0;

    // TODO: Add terms() and iterator methods when Terms/TermsEnum are implemented
};

}  // namespace codecs
}  // namespace diagon
