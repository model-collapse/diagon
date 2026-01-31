// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/FieldInfo.h"
#include "diagon/store/Directory.h"
#include "diagon/store/IndexInput.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace diagon {
namespace codecs {

/**
 * StoredFieldsReader - Reads stored fields from .fdt/.fdx files
 *
 * Based on: org.apache.lucene.codecs.StoredFieldsReader
 *
 * File Format:
 * - .fdx (index) file: Maps document ID to offset in .fdt file
 * - .fdt (data) file: Sequential storage of stored field values
 *
 * Design:
 * - Random access to documents via .fdx index
 * - Lazy reading - only read requested documents
 * - Returns fields as map of name → value
 *
 * Usage:
 *   StoredFieldsReader reader(directory, segmentName, fieldInfos);
 *   auto fields = reader.document(docID);
 *   std::string title = std::get<std::string>(fields["title"]);
 *   int64_t count = std::get<int64_t>(fields["count"]);
 *
 * Thread Safety:
 * - NOT thread-safe (clone for each thread)
 */
class StoredFieldsReader {
public:
    /**
     * Field value type (STRING, INT32, or INT64)
     */
    using FieldValue = std::variant<std::string, int32_t, int64_t>;

    /**
     * Document fields (map from field name to value)
     */
    using DocumentFields = std::unordered_map<std::string, FieldValue>;

    /**
     * Constructor - opens .fdt and .fdx files
     *
     * @param directory Directory containing segment files
     * @param segmentName Segment name (e.g., "_0")
     * @param fieldInfos Field metadata for looking up field names
     */
    StoredFieldsReader(store::Directory* directory, const std::string& segmentName,
                       const ::diagon::index::FieldInfos& fieldInfos);

    /**
     * Destructor - closes input files
     */
    ~StoredFieldsReader();

    // Disable copy/move
    StoredFieldsReader(const StoredFieldsReader&) = delete;
    StoredFieldsReader& operator=(const StoredFieldsReader&) = delete;
    StoredFieldsReader(StoredFieldsReader&&) = delete;
    StoredFieldsReader& operator=(StoredFieldsReader&&) = delete;

    /**
     * Read all stored fields for a document
     *
     * @param docID Document ID (0-based)
     * @return Map from field name to field value
     * @throws std::runtime_error if docID out of range
     */
    DocumentFields document(int docID);

    /**
     * Get number of documents
     */
    int numDocs() const { return numDocs_; }

    /**
     * Close input files
     */
    void close();

private:
    /**
     * Read index file (.fdx) to build offset array
     */
    void readIndex();

    /**
     * Verify codec header matches expected format
     */
    void verifyHeader(store::IndexInput& input, const std::string& expectedCodec);

    // Directory and segment name
    store::Directory* directory_;
    std::string segmentName_;

    // Field metadata
    const ::diagon::index::FieldInfos& fieldInfos_;

    // Input files
    std::unique_ptr<store::IndexInput> dataInput_;   // .fdt file
    std::unique_ptr<store::IndexInput> indexInput_;  // .fdx file

    // Document offsets in .fdt file (from .fdx index)
    std::vector<int64_t> offsets_;

    // Number of documents
    int numDocs_{0};

    // Closed flag
    bool closed_{false};
};

}  // namespace codecs
}  // namespace diagon
