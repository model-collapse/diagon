// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene99/Lucene99SegmentInfoFormat.h"

#include "diagon/codecs/CodecUtil.h"
#include "diagon/index/SegmentInfo.h"
#include "diagon/store/Directory.h"

#include <set>

namespace diagon {
namespace codecs {
namespace lucene99 {

void Lucene99SegmentInfoFormat::write(store::Directory& dir, const index::SegmentInfo& si) {
    std::string fileName = siFileName(si.name());
    auto output = dir.createOutput(fileName, store::IOContext::DEFAULT);

    // Write IndexHeader
    CodecUtil::writeIndexHeader(*output, CODEC_NAME, VERSION_CURRENT,
                                si.segmentID(), "");

    // Write Lucene version triple (little-endian int32 to match Lucene's
    // ByteBuffersDataOutput which uses native byte order on x86)
    output->writeIntLE(LUCENE_VERSION_MAJOR);
    output->writeIntLE(LUCENE_VERSION_MINOR);
    output->writeIntLE(LUCENE_VERSION_BUGFIX);

    // HasMinVersion: byte(1) — we always write a min version
    output->writeByte(1);
    // MinVersion = same as version
    output->writeIntLE(LUCENE_VERSION_MAJOR);
    output->writeIntLE(LUCENE_VERSION_MINOR);
    output->writeIntLE(LUCENE_VERSION_BUGFIX);

    // DocCount (little-endian int32)
    output->writeIntLE(si.maxDoc());

    // IsCompoundFile
    output->writeByte(si.getUseCompoundFile() ? 1 : 0);

    // HasBlocks (Lucene 9.9+ — always false for Diagon)
    output->writeByte(0);

    // Diagnostics
    output->writeMapOfStrings(si.diagnostics());

    // Files — convert vector to set
    std::set<std::string> fileSet(si.files().begin(), si.files().end());
    output->writeSetOfStrings(fileSet);

    // Attributes (empty for now)
    std::map<std::string, std::string> emptyAttrs;
    output->writeMapOfStrings(emptyAttrs);

    // Index sort fields: VInt(0) — no sorting
    output->writeVInt(0);

    // Footer
    CodecUtil::writeFooter(*output);

    output->close();
    dir.sync({fileName});
}

std::shared_ptr<index::SegmentInfo> Lucene99SegmentInfoFormat::read(
    store::Directory& dir, const std::string& segmentName, const uint8_t* segmentID) {
    std::string fileName = siFileName(segmentName);
    auto input = dir.openInput(fileName, store::IOContext::READ);

    // Validate IndexHeader
    int version = CodecUtil::checkIndexHeader(*input, CODEC_NAME,
                                              VERSION_START, VERSION_CURRENT,
                                              segmentID, "");

    // Read Lucene version triple (little-endian int32, ignored — we don't use it).
    // Lucene writes these through ByteBuffersDataOutput which uses native (LE) byte order.
    input->readIntLE();  // major
    input->readIntLE();  // minor
    input->readIntLE();  // bugfix

    // HasMinVersion
    uint8_t hasMinVersion = input->readByte();
    if (hasMinVersion == 1) {
        input->readIntLE();  // major
        input->readIntLE();  // minor
        input->readIntLE();  // bugfix
    }

    // DocCount (little-endian int32)
    int32_t docCount = input->readIntLE();

    // IsCompoundFile
    bool isCompoundFile = input->readByte() != 0;

    // HasBlocks (version >= 2)
    if (version >= 2) {
        input->readByte();  // hasBlocks — ignored
    }

    // Diagnostics
    auto diagnostics = input->readMapOfStrings();

    // Files
    auto files = input->readSetOfStrings();

    // Attributes
    auto attributes = input->readMapOfStrings();

    // Index sort fields
    int32_t numSortFields = input->readVInt();
    // Skip sort field data if present (not supported by Diagon)
    for (int32_t i = 0; i < numSortFields; i++) {
        input->readString();  // field name
        input->readVInt();    // sort type
        input->readVInt();    // reverse flag
        input->readVInt();    // missing value
    }

    // Validate footer
    CodecUtil::checkFooter(*input);

    // Build SegmentInfo
    auto si = std::make_shared<index::SegmentInfo>(segmentName, docCount, "Lucene104");
    si->setSegmentID(segmentID);
    si->setUseCompoundFile(isCompoundFile);
    si->setFiles(std::vector<std::string>(files.begin(), files.end()));
    for (const auto& [key, value] : diagnostics) {
        si->setDiagnostic(key, value);
    }

    return si;
}

}  // namespace lucene99
}  // namespace codecs
}  // namespace diagon
