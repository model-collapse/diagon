// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/FieldInfo.h"

#include <memory>
#include <string>

namespace diagon {

namespace store {
class Directory;
}

namespace index {
class SegmentInfo;
}

namespace codecs {

// Forward declarations
class SegmentWriteState;
class SegmentReadState;

/**
 * NOTE: These are stub format interfaces that will be implemented in future tasks.
 * They provide the complete codec interface but don't yet have full functionality.
 */

// ==================== StoredFieldsFormat ====================

class StoredFieldsFormat {
public:
    virtual ~StoredFieldsFormat() = default;
    virtual std::string getName() const = 0;

    // TODO: Add fieldsReader/fieldsWriter when document storage is implemented
};

// ==================== TermVectorsFormat ====================

class TermVectorsFormat {
public:
    virtual ~TermVectorsFormat() = default;
    virtual std::string getName() const = 0;

    // TODO: Add vectorsReader/vectorsWriter when term vectors are implemented
};

// ==================== FieldInfosFormat ====================

class FieldInfosFormat {
public:
    virtual ~FieldInfosFormat() = default;
    virtual std::string getName() const = 0;

    /**
     * Write field infos to a per-segment .fnm file.
     * Default: no-op (native mode stores field infos in segments_N).
     */
    virtual void write(store::Directory& dir, const index::SegmentInfo& si,
                       const index::FieldInfos& fieldInfos) {}

    /**
     * Read field infos from a per-segment .fnm file.
     * Default: returns empty FieldInfos (native mode reads from segments_N).
     */
    virtual index::FieldInfos read(store::Directory& dir, const index::SegmentInfo& si) {
        return {};
    }
};

// ==================== SegmentInfoFormat ====================

class SegmentInfoFormat {
public:
    virtual ~SegmentInfoFormat() = default;
    virtual std::string getName() const = 0;

    /**
     * Write segment info to a per-segment .si file.
     * Default: no-op (native mode stores segment info in segments_N).
     */
    virtual void write(store::Directory& dir, const index::SegmentInfo& si) {}

    /**
     * Read segment info from a per-segment .si file.
     * Default: returns nullptr (native mode reads from segments_N).
     */
    virtual std::shared_ptr<index::SegmentInfo> read(store::Directory& dir,
                                                      const std::string& segmentName,
                                                      const uint8_t* segmentID) {
        return nullptr;
    }
};

// ==================== NormsFormat ====================
// Now implemented in NormsFormat.h

// ==================== LiveDocsFormat ====================
// Now implemented in LiveDocsFormat.h

// ==================== PointsFormat ====================

class PointsFormat {
public:
    virtual ~PointsFormat() = default;
    virtual std::string getName() const = 0;

    // TODO: Add fieldsWriter/fieldsReader when BKD tree is implemented
};

// ==================== VectorFormat ====================

class VectorFormat {
public:
    virtual ~VectorFormat() = default;
    virtual std::string getName() const = 0;

    // TODO: Add fieldsWriter/fieldsReader when vector search (HNSW) is implemented
};

}  // namespace codecs
}  // namespace diagon
