// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/store/CompoundFileWriter.h"

#include "diagon/store/IOContext.h"
#include "diagon/util/Exceptions.h"

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

namespace diagon::store {

std::string CompoundFileWriter::getDataFileName(const std::string& segmentName) {
    return segmentName + DATA_EXTENSION;
}

std::string CompoundFileWriter::getEntriesFileName(const std::string& segmentName) {
    return segmentName + ENTRIES_EXTENSION;
}

std::string CompoundFileWriter::stripSegmentName(const std::string& fileName,
                                                 const std::string& segmentName) {
    if (fileName.size() > segmentName.size() &&
        fileName.compare(0, segmentName.size(), segmentName) == 0) {
        return fileName.substr(segmentName.size());
    }
    return fileName;
}

void CompoundFileWriter::write(Directory& dir, const std::string& segmentName,
                               const std::vector<std::string>& fileNames) {
    if (fileNames.empty()) {
        return;
    }

    std::string dataFile = getDataFileName(segmentName);
    std::string entriesFile = getEntriesFileName(segmentName);

    // Create output streams for .cfs and .cfe
    auto data = dir.createOutput(dataFile, IOContext::DEFAULT);
    auto entries = dir.createOutput(entriesFile, IOContext::DEFAULT);

    // Sort files by size (smallest first) for page cache efficiency
    struct SizedFile {
        std::string name;
        int64_t length;
    };

    std::vector<SizedFile> files;
    files.reserve(fileNames.size());
    for (const auto& name : fileNames) {
        files.push_back({name, dir.fileLength(name)});
    }
    std::sort(files.begin(), files.end(),
              [](const SizedFile& a, const SizedFile& b) { return a.length < b.length; });

    // Write number of entries to .cfe
    entries->writeVInt(static_cast<int32_t>(files.size()));

    // Copy buffer for transferring file data
    constexpr size_t COPY_BUFFER_SIZE = 16384;
    std::vector<uint8_t> copyBuffer(COPY_BUFFER_SIZE);

    for (const auto& sizedFile : files) {
        // Align the data file pointer to ALIGNMENT_BYTES boundary
        int64_t currentPos = data->getFilePointer();
        int64_t remainder = currentPos % ALIGNMENT_BYTES;
        if (remainder != 0) {
            int64_t padding = ALIGNMENT_BYTES - remainder;
            // Write zero padding bytes
            for (int64_t i = 0; i < padding; i++) {
                data->writeByte(0);
            }
        }
        int64_t startOffset = data->getFilePointer();

        // Copy file contents into .cfs
        auto input = dir.openInput(sizedFile.name, IOContext::READONCE);
        int64_t remaining = input->length();
        while (remaining > 0) {
            auto toRead = static_cast<size_t>(
                std::min(remaining, static_cast<int64_t>(COPY_BUFFER_SIZE)));
            input->readBytes(copyBuffer.data(), toRead);
            data->writeBytes(copyBuffer.data(), toRead);
            remaining -= toRead;
        }

        int64_t endOffset = data->getFilePointer();
        int64_t length = endOffset - startOffset;

        // Write entry to .cfe: [stripped_filename, offset, length]
        std::string strippedName = stripSegmentName(sizedFile.name, segmentName);
        entries->writeString(strippedName);
        entries->writeLong(startOffset);
        entries->writeLong(length);
    }

    // Close both files
    data->close();
    entries->close();
}

}  // namespace diagon::store
