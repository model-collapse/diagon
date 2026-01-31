// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104NormsReader.h"

#include "diagon/index/SegmentInfo.h"
#include "diagon/store/Directory.h"
#include "diagon/util/Exceptions.h"

#include <cmath>
#include <stdexcept>

namespace diagon {
namespace codecs {
namespace lucene104 {

// ==================== Norm Decoding ====================

float Lucene104NormsReader::decodeNormValue(int8_t norm) {
    if (norm == 127) {
        return 1.0f;  // Maximum norm for empty fields
    }

    // Decode: byte represents 127 / sqrt(length)
    // So: length = (127 / byte)^2
    // And: norm_factor = 1 / sqrt(length) = byte / 127
    return static_cast<float>(norm) / 127.0f;
}

// ==================== Constructor/Destructor ====================

Lucene104NormsReader::Lucene104NormsReader(index::SegmentReadState& state)
    : state_(state) {
    // Open norms data file (.nvd)
    std::string baseName = state.segmentName;
    if (!state.segmentSuffix.empty()) {
        baseName += "_" + state.segmentSuffix;
    }
    std::string dataName = baseName + ".nvd";
    std::string metaName = baseName + ".nvm";

    try {
        data_ = state.directory->openInput(dataName, state.context);
        meta_ = state.directory->openInput(metaName, state.context);
    } catch (const FileNotFoundException&) {
        // No norms files - this is OK if no fields have norms
        data_.reset();
        meta_.reset();
        return;
    }

    // Read metadata file to build field offset map
    readMetadata();
}

Lucene104NormsReader::~Lucene104NormsReader() {
    if (!closed_) {
        try {
            close();
        } catch (...) {
            // Suppress exceptions in destructor
        }
    }
}

// ==================== NormsProducer Implementation ====================

std::unique_ptr<index::NumericDocValues>
Lucene104NormsReader::getNorms(const index::FieldInfo& field) {
    if (!data_) {
        throw IOException("No norms data available");
    }

    // Check cache
    auto it = normsCache_.find(field.number);
    if (it != normsCache_.end()) {
        return std::make_unique<NormsValues>(it->second);
    }

    // Load norms from disk
    std::vector<int8_t> norms = loadNorms(field);

    // Cache for future access
    normsCache_[field.number] = norms;

    return std::make_unique<NormsValues>(std::move(norms));
}

std::vector<int8_t> Lucene104NormsReader::loadNorms(const index::FieldInfo& field) {
    // Find field metadata
    auto it = fieldMetadata_.find(field.number);
    if (it == fieldMetadata_.end()) {
        throw IOException("No norms metadata for field: " + field.name);
    }

    const FieldMetadata& metadata = it->second;

    // Seek to field offset in data file
    data_->seek(metadata.offset);

    // Read norms bytes
    std::vector<int8_t> norms;
    norms.reserve(metadata.count);

    for (int i = 0; i < metadata.count; i++) {
        uint8_t byte = data_->readByte();
        norms.push_back(static_cast<int8_t>(byte));
    }

    return norms;
}

void Lucene104NormsReader::readMetadata() {
    if (!meta_) {
        return;
    }

    // Read header
    std::string header = meta_->readString();
    if (header != "NORMS_META") {
        throw IOException("Invalid norms metadata header: " + header);
    }

    int version = meta_->readInt();
    if (version != 1) {
        throw IOException("Unsupported norms format version: " + std::to_string(version));
    }

    // Read field metadata entries
    while (meta_->getFilePointer() < meta_->length()) {
        FieldMetadata metadata;
        metadata.fieldNumber = meta_->readInt();
        metadata.offset = meta_->readLong();
        metadata.count = meta_->readInt();

        fieldMetadata_[metadata.fieldNumber] = metadata;
    }
}

void Lucene104NormsReader::checkIntegrity() {
    // TODO: Implement checksum verification
    // For Phase 5, this is a no-op
}

void Lucene104NormsReader::close() {
    if (closed_) {
        return;
    }

    try {
        // Release the input (no explicit close needed for IndexInput)
        data_.reset();

        normsCache_.clear();
        closed_ = true;
    } catch (const std::exception& e) {
        throw IOException("Failed to close norms reader: " + std::string(e.what()));
    }
}

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
