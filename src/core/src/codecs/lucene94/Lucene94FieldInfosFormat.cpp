// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene94/Lucene94FieldInfosFormat.h"

#include "diagon/codecs/CodecUtil.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/index/SegmentInfo.h"
#include "diagon/store/Directory.h"
#include "diagon/util/Exceptions.h"

namespace diagon {
namespace codecs {
namespace lucene94 {

void Lucene94FieldInfosFormat::write(store::Directory& dir, const index::SegmentInfo& si,
                                     const index::FieldInfos& fieldInfos) {
    std::string fileName = fnmFileName(si.name());
    auto output = dir.createOutput(fileName, store::IOContext::DEFAULT);

    // IndexHeader: codec="Lucene94FieldInfos", version=1, segmentID, suffix=""
    CodecUtil::writeIndexHeader(*output, CODEC_NAME, VERSION_CURRENT,
                                si.segmentID(), "");

    // FieldCount
    output->writeVInt(static_cast<int32_t>(fieldInfos.size()));

    for (const auto& fi : fieldInfos) {
        // FieldName
        output->writeString(fi.name);

        // FieldNumber
        output->writeVInt(fi.number);

        // FieldBits
        uint8_t bits = 0;
        if (fi.storeTermVector) bits |= STORE_TERMVECTOR;
        if (fi.omitNorms) bits |= OMIT_NORMS;
        if (fi.storePayloads) bits |= STORE_PAYLOADS;
        if (fi.softDeletesField) bits |= SOFT_DELETES;
        if (fi.isParentField) bits |= PARENT_FIELD;
        output->writeByte(bits);

        // IndexOptions as byte
        output->writeByte(static_cast<uint8_t>(fi.indexOptions));

        // DocValuesType as byte
        output->writeByte(static_cast<uint8_t>(fi.docValuesType));

        // DocValuesSkipIndexType (VERSION_DOCVALUE_SKIPPER = 1)
        output->writeByte(static_cast<uint8_t>(fi.docValuesSkipIndex));

        // DocValuesGen (-1 for fresh segments)
        output->writeLong(-1);

        // Attributes
        output->writeMapOfStrings(fi.attributes);

        // PointDimensionCount
        output->writeVInt(fi.pointDimensionCount);
        if (fi.pointDimensionCount > 0) {
            output->writeVInt(fi.pointIndexDimensionCount);
            output->writeVInt(fi.pointNumBytes);
        }

        // VectorDimension (0 — no vector support yet)
        output->writeVInt(0);
        // VectorEncoding (0)
        output->writeByte(0);
        // VectorSimilarity (0)
        output->writeByte(0);
    }

    CodecUtil::writeFooter(*output);
    output->close();
    dir.sync({fileName});
}

index::FieldInfos Lucene94FieldInfosFormat::read(store::Directory& dir,
                                                  const index::SegmentInfo& si) {
    std::string fileName = fnmFileName(si.name());
    auto input = dir.openInput(fileName, store::IOContext::READ);

    // Validate IndexHeader
    int32_t version = CodecUtil::checkIndexHeader(*input, CODEC_NAME,
                                                   VERSION_START, VERSION_CURRENT,
                                                   si.segmentID(), "");

    // FieldCount
    int32_t fieldCount = input->readVInt();
    if (fieldCount < 0) {
        throw CorruptIndexException(
            "invalid fieldCount: " + std::to_string(fieldCount),
            input->toString());
    }

    std::vector<index::FieldInfo> infos;
    infos.reserve(static_cast<size_t>(fieldCount));

    for (int32_t i = 0; i < fieldCount; i++) {
        // FieldName
        std::string name = input->readString();

        // FieldNumber
        int32_t fieldNumber = input->readVInt();

        // FieldBits
        uint8_t bits = input->readByte();

        // IndexOptions
        uint8_t indexOptionsByte = input->readByte();
        if (indexOptionsByte > 4) {
            throw CorruptIndexException(
                "invalid indexOptions: " + std::to_string(indexOptionsByte),
                input->toString());
        }
        auto indexOptions = static_cast<index::IndexOptions>(indexOptionsByte);

        // DocValuesType
        uint8_t dvTypeByte = input->readByte();
        if (dvTypeByte > 5) {
            throw CorruptIndexException(
                "invalid docValuesType: " + std::to_string(dvTypeByte),
                input->toString());
        }
        auto docValuesType = static_cast<index::DocValuesType>(dvTypeByte);

        // DocValuesSkipIndexType (version >= 1 = VERSION_DOCVALUE_SKIPPER)
        index::DocValuesSkipIndexType dvSkipIndex = index::DocValuesSkipIndexType::NONE;
        if (version >= 1) {
            dvSkipIndex = static_cast<index::DocValuesSkipIndexType>(input->readByte());
        }

        // DocValuesGen
        int64_t dvGen = input->readLong();

        // Attributes
        auto attributes = input->readMapOfStrings();

        // PointDimensionCount
        int32_t pointDimensionCount = input->readVInt();
        int32_t pointIndexDimensionCount = 0;
        int32_t pointNumBytes = 0;
        if (pointDimensionCount > 0) {
            pointIndexDimensionCount = input->readVInt();
            pointNumBytes = input->readVInt();
        }

        // VectorDimension (skip — not supported)
        int32_t vectorDim = input->readVInt();
        if (vectorDim > 0) {
            // VectorEncoding + VectorSimilarity
            input->readByte();
            input->readByte();
        } else {
            // Even with vectorDim==0, Lucene 9.4 writes encoding + similarity bytes
            input->readByte();  // vectorEncoding
            input->readByte();  // vectorSimilarity
        }

        // Build FieldInfo
        index::FieldInfo fi(std::move(name), fieldNumber);
        fi.indexOptions = indexOptions;
        fi.storeTermVector = (bits & STORE_TERMVECTOR) != 0;
        fi.omitNorms = (bits & OMIT_NORMS) != 0;
        fi.storePayloads = (bits & STORE_PAYLOADS) != 0;
        fi.softDeletesField = (bits & SOFT_DELETES) != 0;
        fi.isParentField = (bits & PARENT_FIELD) != 0;
        fi.docValuesType = docValuesType;
        fi.docValuesSkipIndex = dvSkipIndex;
        fi.dvGen = dvGen;
        fi.attributes = std::move(attributes);
        fi.pointDimensionCount = pointDimensionCount;
        fi.pointIndexDimensionCount = pointIndexDimensionCount;
        fi.pointNumBytes = pointNumBytes;

        infos.push_back(std::move(fi));
    }

    // Validate footer
    CodecUtil::checkFooter(*input);

    return index::FieldInfos(std::move(infos));
}

}  // namespace lucene94
}  // namespace codecs
}  // namespace diagon
