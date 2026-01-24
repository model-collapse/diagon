// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <memory>
#include <string>

namespace diagon {
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

    // TODO: Add read/write methods when FieldInfos persistence is implemented
};

// ==================== SegmentInfoFormat ====================

class SegmentInfoFormat {
public:
    virtual ~SegmentInfoFormat() = default;
    virtual std::string getName() const = 0;

    // TODO: Add read/write methods when SegmentInfo is implemented
};

// ==================== NormsFormat ====================

class NormsFormat {
public:
    virtual ~NormsFormat() = default;
    virtual std::string getName() const = 0;

    // TODO: Add normsConsumer/normsProducer when norms are implemented
};

// ==================== LiveDocsFormat ====================

class LiveDocsFormat {
public:
    virtual ~LiveDocsFormat() = default;
    virtual std::string getName() const = 0;

    // TODO: Add read/write methods for deleted documents bitset
};

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
