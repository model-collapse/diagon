// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene104/Lucene104NormsWriter.h"

#include "diagon/codecs/lucene104/Lucene104NormsReader.h"
#include "diagon/index/SegmentInfo.h"
#include "diagon/store/Directory.h"
#include "diagon/util/Exceptions.h"

#include <cmath>
#include <stdexcept>

namespace diagon {
namespace codecs {
namespace lucene104 {

// ==================== Norm Encoding ====================

/**
 * Encode norm value using SmallFloat encoding
 *
 * Lucene encodes norms as: 1.0 / sqrt(length)
 * This is then packed into a signed byte using a logarithmic scale.
 *
 * For Phase 5, we use a simplified encoding:
 * - norm = min(127, 127 / sqrt(length))
 * - Stores inverted length: shorter docs get higher norms
 */
int8_t Lucene104NormsWriter::encodeNormValue(int64_t length) {
    if (length <= 0) {
        return 127;  // Empty field gets maximum norm
    }

    // Calculate: 127 / sqrt(length)
    // This gives higher values for shorter documents
    double sqrtLength = std::sqrt(static_cast<double>(length));
    double encoded = 127.0 / sqrtLength;

    // Clamp to byte range
    if (encoded > 127.0) {
        return 127;
    }
    if (encoded < -128.0) {
        return -128;
    }

    return static_cast<int8_t>(encoded);
}

// ==================== Constructor/Destructor ====================

Lucene104NormsWriter::Lucene104NormsWriter(index::SegmentWriteState& state)
    : state_(state) {
    // Open norms data file (.nvd)
    std::string baseName = state.segmentName;
    if (!state.segmentSuffix.empty()) {
        baseName += "_" + state.segmentSuffix;
    }
    std::string dataName = baseName + ".nvd";
    data_ = state.directory->createOutput(dataName, state.context);

    // Open norms metadata file (.nvm)
    std::string metaName = baseName + ".nvm";
    meta_ = state.directory->createOutput(metaName, state.context);

    // Write metadata header
    meta_->writeString("NORMS_META");
    meta_->writeInt(2);  // Version 2: sparse norms encoding
}

Lucene104NormsWriter::~Lucene104NormsWriter() {
    if (!closed_) {
        try {
            close();
        } catch (...) {
            // Suppress exceptions in destructor
        }
    }
}

// ==================== NormsConsumer Implementation ====================

void Lucene104NormsWriter::addNormsField(const index::FieldInfo& field,
                                         NormsProducer& normsProducer) {
    if (closed_) {
        throw AlreadyClosedException("NormsWriter already closed");
    }

    // Get norms from producer
    auto normsIter = normsProducer.getNorms(field);
    if (!normsIter) {
        throw std::invalid_argument("Field has no norms: " + field.name);
    }

    // Collect norms values
    std::vector<int8_t> norms;
    int maxDoc = state_.maxDoc;
    if (maxDoc == 0) {
        throw std::invalid_argument("Cannot write norms for empty segment");
    }
    norms.reserve(maxDoc);

    for (int doc = 0; doc < maxDoc; doc++) {
        if (normsIter->advanceExact(doc)) {
            int64_t value = normsIter->longValue();
            norms.push_back(static_cast<int8_t>(value));
        } else {
            // No norm value for this doc - use default
            norms.push_back(0);
        }
    }

    // Write norms data
    writeNormsData(field, norms);
}

int8_t Lucene104NormsWriter::findDefaultNorm(const std::vector<int8_t>& norms) {
    // Count occurrences of each norm value to find the most common
    int counts[256] = {};
    for (int8_t norm : norms) {
        counts[static_cast<uint8_t>(norm)]++;
    }

    int bestCount = 0;
    uint8_t bestValue = 0;
    for (int i = 0; i < 256; i++) {
        if (counts[i] > bestCount) {
            bestCount = counts[i];
            bestValue = static_cast<uint8_t>(i);
        }
    }
    return static_cast<int8_t>(bestValue);
}

void Lucene104NormsWriter::writeNormsData(const index::FieldInfo& field,
                                          const std::vector<int8_t>& norms) {
    // Find the most common norm value (default)
    int8_t defaultNorm = findDefaultNorm(norms);

    // Count sparse entries (non-default values)
    int numSparse = 0;
    for (int8_t norm : norms) {
        if (norm != defaultNorm) {
            numSparse++;
        }
    }

    // Decide encoding: sparse only if it saves space
    // Dense: numDocs bytes
    // Sparse: 1 (default) + VInt(numSparse) + numSparse * (VInt(docID) + 1 byte)
    // For Reuters: ~19K docs, if 80% share default → sparse = 1 + 2 + 3800*4 ≈ 15.2K vs dense 19K
    bool useSparse = (numSparse < static_cast<int>(norms.size()) / 2);

    // Write metadata: field number, offset, count, encoding, defaultNorm
    meta_->writeInt(field.number);
    meta_->writeLong(data_->getFilePointer());
    meta_->writeInt(static_cast<int>(norms.size()));
    meta_->writeByte(useSparse ? 1 : 0);  // encoding: 0=dense, 1=sparse
    meta_->writeByte(static_cast<uint8_t>(defaultNorm));

    if (useSparse) {
        // Sparse format: VInt(numSparseEntries) + [VInt(docID) + byte(norm)] per entry
        data_->writeVInt(numSparse);
        for (int doc = 0; doc < static_cast<int>(norms.size()); doc++) {
            if (norms[doc] != defaultNorm) {
                data_->writeVInt(doc);
                data_->writeByte(static_cast<uint8_t>(norms[doc]));
            }
        }
    } else {
        // Dense format: simple byte array (same as version 1)
        for (int8_t norm : norms) {
            data_->writeByte(static_cast<uint8_t>(norm));
        }
    }
}

void Lucene104NormsWriter::close() {
    if (closed_) {
        return;
    }

    try {
        // Close metadata file
        if (meta_) {
            meta_->close();
            meta_.reset();
        }

        // Close data file
        if (data_) {
            data_->close();
            data_.reset();
        }

        closed_ = true;
    } catch (const std::exception& e) {
        throw IOException("Failed to close norms writer: " + std::string(e.what()));
    }
}

// ==================== Format Implementation ====================

std::unique_ptr<NormsConsumer>
Lucene104NormsFormat::normsConsumer(index::SegmentWriteState& state) {
    return std::make_unique<Lucene104NormsWriter>(state);
}

std::unique_ptr<NormsProducer> Lucene104NormsFormat::normsProducer(index::SegmentReadState& state) {
    return std::make_unique<Lucene104NormsReader>(state);
}

}  // namespace lucene104
}  // namespace codecs
}  // namespace diagon
