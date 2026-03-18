// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/codecs/OtherFormats.h"
#include "diagon/codecs/lucene90/Lucene90OSStoredFieldsReader.h"
#include "diagon/codecs/lucene90/Lucene90OSStoredFieldsWriter.h"

#include <memory>
#include <string>

namespace diagon {
namespace codecs {
namespace lucene90 {

/**
 * Lucene 90-compatible stored fields format (BEST_SPEED mode).
 *
 * Produces 3 files per segment:
 *   .fdt — LZ4-compressed stored field data
 *   .fdx — DirectMonotonic chunk index
 *   .fdm — index metadata + statistics
 *
 * Compatible with OpenSearch/Lucene stored fields reader.
 */
class Lucene90OSStoredFieldsFormat : public StoredFieldsFormat {
public:
    // Format name for .fdt header (BEST_SPEED mode)
    static constexpr const char* FORMAT_NAME = "Lucene90StoredFieldsFastData";

    std::string getName() const override { return "Lucene90StoredFieldsFormat"; }

    /**
     * Create a stored fields writer for a new segment.
     */
    std::unique_ptr<Lucene90OSStoredFieldsWriter> fieldsWriter(store::Directory& dir,
                                                                 const std::string& segmentName,
                                                                 const uint8_t* segmentID) {
        return std::make_unique<Lucene90OSStoredFieldsWriter>(dir, segmentName, segmentID,
                                                                FORMAT_NAME);
    }

    /**
     * Create a stored fields reader for an existing segment.
     */
    std::unique_ptr<Lucene90OSStoredFieldsReader> fieldsReader(store::Directory& dir,
                                                                 const std::string& segmentName,
                                                                 const uint8_t* segmentID,
                                                                 const index::FieldInfos& fieldInfos) {
        return std::make_unique<Lucene90OSStoredFieldsReader>(dir, segmentName, segmentID,
                                                                fieldInfos, FORMAT_NAME);
    }
};

}  // namespace lucene90
}  // namespace codecs
}  // namespace diagon
