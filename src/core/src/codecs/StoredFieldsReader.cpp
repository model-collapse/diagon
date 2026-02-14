// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/StoredFieldsReader.h"

#include "diagon/store/IOContext.h"

#include <stdexcept>

namespace diagon {
namespace codecs {

// Codec constants (must match StoredFieldsWriter)
static const std::string CODEC_NAME = "DiagonStoredFields";
static const int VERSION = 1;

// Field types (must match StoredFieldsWriter)
enum class FieldType : uint8_t {
    STRING = 0,
    INT = 1,
    LONG = 2
};

StoredFieldsReader::StoredFieldsReader(store::Directory* directory, const std::string& segmentName,
                                       const index::FieldInfos& fieldInfos)
    : directory_(directory)
    , segmentName_(segmentName)
    , fieldInfos_(fieldInfos) {
    // Open .fdt (data) file
    dataInput_ = directory_->openInput(segmentName + ".fdt", store::IOContext::READ);

    // Open .fdx (index) file
    indexInput_ = directory_->openInput(segmentName + ".fdx", store::IOContext::READ);

    // Read index to build offsets array
    readIndex();
}

StoredFieldsReader::~StoredFieldsReader() {
    if (!closed_) {
        close();
    }
}

void StoredFieldsReader::verifyHeader(store::IndexInput& input, const std::string& expectedCodec) {
    // Read codec name
    std::string codec = input.readString();
    if (codec != expectedCodec) {
        throw std::runtime_error("Invalid codec: expected " + expectedCodec + ", got " + codec);
    }

    // Read version
    int version = input.readVInt();
    if (version != VERSION) {
        throw std::runtime_error("Invalid version: expected " + std::to_string(VERSION) + ", got " +
                                 std::to_string(version));
    }
}

void StoredFieldsReader::readIndex() {
    // Verify index file header
    verifyHeader(*indexInput_, CODEC_NAME);

    // Read number of documents
    numDocs_ = indexInput_->readVInt();

    // Read offsets
    offsets_.reserve(numDocs_);
    for (int i = 0; i < numDocs_; i++) {
        int64_t offset = indexInput_->readVLong();
        offsets_.push_back(offset);
    }

    // Verify data file header
    verifyHeader(*dataInput_, CODEC_NAME);
}

StoredFieldsReader::DocumentFields StoredFieldsReader::document(int docID) {
    if (closed_) {
        throw std::runtime_error("StoredFieldsReader is closed");
    }

    if (docID < 0 || docID >= numDocs_) {
        throw std::runtime_error("Document ID out of range: " + std::to_string(docID));
    }

    // Seek to document position in .fdt file
    int64_t offset = offsets_[docID];
    dataInput_->seek(offset);

    // Read number of fields
    int numFields = dataInput_->readVInt();

    // Read fields
    DocumentFields fields;
    for (int i = 0; i < numFields; i++) {
        // Read field number
        int32_t fieldNumber = dataInput_->readVInt();

        // Look up field name from FieldInfos
        const index::FieldInfo* fieldInfo = nullptr;
        for (const auto& fi : fieldInfos_) {
            if (fi.number == fieldNumber) {
                fieldInfo = &fi;
                break;
            }
        }

        if (!fieldInfo) {
            throw std::runtime_error("Unknown field number: " + std::to_string(fieldNumber));
        }

        // Read field type
        uint8_t typeCode = dataInput_->readByte();
        FieldType fieldType = static_cast<FieldType>(typeCode);

        // Read value based on type
        switch (fieldType) {
            case FieldType::STRING: {
                std::string value = dataInput_->readString();
                fields[fieldInfo->name] = value;
                break;
            }
            case FieldType::INT: {
                int32_t value = dataInput_->readVInt();
                fields[fieldInfo->name] = value;
                break;
            }
            case FieldType::LONG: {
                int64_t value = dataInput_->readVLong();
                fields[fieldInfo->name] = value;
                break;
            }
            default:
                throw std::runtime_error("Unknown field type: " + std::to_string(typeCode));
        }
    }

    return fields;
}

void StoredFieldsReader::close() {
    if (closed_) {
        return;
    }

    // IndexInput uses RAII - just reset the unique_ptr
    dataInput_.reset();
    indexInput_.reset();

    closed_ = true;
}

}  // namespace codecs
}  // namespace diagon
