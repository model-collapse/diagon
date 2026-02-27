// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/StoredFieldsWriter.h"

#include <algorithm>
#include <stdexcept>

namespace diagon {
namespace codecs {

// Codec constants
static const std::string CODEC_NAME = "DiagonStoredFields";
static const int VERSION = 1;

// ==================== Constructor ====================

StoredFieldsWriter::StoredFieldsWriter(const std::string& segmentName)
    : segmentName_(segmentName) {}

// ==================== Document Lifecycle ====================

void StoredFieldsWriter::startDocument() {
    if (inDocument_) {
        throw std::runtime_error("Already in document - call finishDocument() first");
    }
    if (finished_) {
        throw std::runtime_error("Writer already finished");
    }

    currentDocument_.clear();
    inDocument_ = true;
}

void StoredFieldsWriter::finishDocument() {
    if (!inDocument_) {
        throw std::runtime_error("Not in document - call startDocument() first");
    }

    // Move current document to documents buffer
    DocumentBuffer doc;
    doc.fields = std::move(currentDocument_);
    documents_.push_back(std::move(doc));

    numDocs_++;
    inDocument_ = false;
}

// ==================== Write Fields ====================

void StoredFieldsWriter::writeField(const index::FieldInfo& fieldInfo, const std::string& value) {
    if (!inDocument_) {
        throw std::runtime_error("Not in document - call startDocument() first");
    }

    StoredField field(fieldInfo.number, FieldType::STRING);
    field.stringValue = value;
    bytesUsed_ += sizeof(StoredField) + value.size();
    currentDocument_.push_back(std::move(field));
}

void StoredFieldsWriter::writeField(const index::FieldInfo& fieldInfo, int32_t value) {
    if (!inDocument_) {
        throw std::runtime_error("Not in document - call startDocument() first");
    }

    StoredField field(fieldInfo.number, FieldType::INT);
    field.numericValue = value;
    bytesUsed_ += sizeof(StoredField);
    currentDocument_.push_back(std::move(field));
}

void StoredFieldsWriter::writeField(const index::FieldInfo& fieldInfo, int64_t value) {
    if (!inDocument_) {
        throw std::runtime_error("Not in document - call startDocument() first");
    }

    StoredField field(fieldInfo.number, FieldType::LONG);
    field.numericValue = value;
    bytesUsed_ += sizeof(StoredField);
    currentDocument_.push_back(std::move(field));
}

// ==================== Finish ====================

void StoredFieldsWriter::finish(int numDocs) {
    if (inDocument_) {
        throw std::runtime_error("Still in document - call finishDocument() first");
    }
    if (finished_) {
        throw std::runtime_error("Already finished");
    }

    // Validate document count
    if (numDocs != numDocs_) {
        throw std::runtime_error("Expected " + std::to_string(numDocs) + " documents, but got " +
                                 std::to_string(numDocs_));
    }

    finished_ = true;
}

// ==================== Flush ====================

void StoredFieldsWriter::flush(store::IndexOutput& dataOut, store::IndexOutput& indexOut) {
    if (!finished_) {
        throw std::runtime_error("Must call finish() before flush()");
    }

    // Write data file and collect offsets
    std::vector<int64_t> offsets = writeData(dataOut);

    // Write index file
    writeIndex(indexOut, offsets);
}

// ==================== RAM Usage ====================

int64_t StoredFieldsWriter::ramBytesUsed() const {
    return bytesUsed_;
}

// ==================== Close ====================

void StoredFieldsWriter::close() {
    documents_.clear();
    currentDocument_.clear();
    inDocument_ = false;
    finished_ = false;
    numDocs_ = 0;
    bytesUsed_ = 0;
}

// ==================== Private Methods ====================

void StoredFieldsWriter::writeHeader(store::IndexOutput& out) {
    out.writeString(CODEC_NAME);
    out.writeVInt(VERSION);
}

std::vector<int64_t> StoredFieldsWriter::writeData(store::IndexOutput& dataOut) {
    // Write header
    writeHeader(dataOut);

    std::vector<int64_t> offsets;
    offsets.reserve(documents_.size());

    // Write each document
    for (const auto& doc : documents_) {
        // Record offset for this document
        offsets.push_back(dataOut.getFilePointer());

        // Write number of fields
        dataOut.writeVInt(static_cast<int>(doc.fields.size()));

        // Write each field
        for (const auto& field : doc.fields) {
            // Write field number
            dataOut.writeVInt(field.fieldNumber);

            // Write field type
            dataOut.writeByte(static_cast<uint8_t>(field.fieldType));

            // Write field value based on type
            switch (field.fieldType) {
                case FieldType::STRING:
                    dataOut.writeString(field.stringValue);
                    break;

                case FieldType::INT:
                    dataOut.writeVInt(static_cast<int32_t>(field.numericValue));
                    break;

                case FieldType::LONG:
                    dataOut.writeVLong(field.numericValue);
                    break;

                default:
                    throw std::runtime_error("Unknown field type");
            }
        }
    }

    return offsets;
}

void StoredFieldsWriter::writeIndex(store::IndexOutput& indexOut,
                                    const std::vector<int64_t>& offsets) {
    // Write header
    writeHeader(indexOut);

    // Write number of documents
    indexOut.writeVInt(static_cast<int>(offsets.size()));

    // Write offset for each document
    for (int64_t offset : offsets) {
        indexOut.writeVLong(offset);
    }
}

}  // namespace codecs
}  // namespace diagon
