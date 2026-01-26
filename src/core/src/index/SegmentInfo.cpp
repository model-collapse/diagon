// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/SegmentInfo.h"

#include "diagon/store/Directory.h"
#include "diagon/store/IndexInput.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace diagon {
namespace index {

// ==================== SegmentInfo ====================

SegmentInfo::SegmentInfo(const std::string& name, int maxDoc, const std::string& codecName)
    : name_(name)
    , maxDoc_(maxDoc)
    , codecName_(codecName)
    , fieldInfos_(std::vector<FieldInfo>{}) {  // Initialize with empty vector
}

void SegmentInfo::addFile(const std::string& fileName) {
    // Check if file already exists
    auto it = std::find(files_.begin(), files_.end(), fileName);
    if (it == files_.end()) {
        files_.push_back(fileName);
    }
}

void SegmentInfo::setFiles(const std::vector<std::string>& files) {
    files_ = files;
}

void SegmentInfo::setDiagnostic(const std::string& key, const std::string& value) {
    diagnostics_[key] = value;
}

std::string SegmentInfo::getDiagnostic(const std::string& key) const {
    auto it = diagnostics_.find(key);
    if (it != diagnostics_.end()) {
        return it->second;
    }
    return "";
}

// ==================== SegmentInfos ====================

SegmentInfos::SegmentInfos() {}

void SegmentInfos::add(std::shared_ptr<SegmentInfo> segmentInfo) {
    segments_.push_back(segmentInfo);
    incrementVersion();
}

std::shared_ptr<SegmentInfo> SegmentInfos::info(int index) const {
    if (index < 0 || index >= static_cast<int>(segments_.size())) {
        throw std::out_of_range("Segment index out of range");
    }
    return segments_[index];
}

int SegmentInfos::totalMaxDoc() const {
    int total = 0;
    for (const auto& seg : segments_) {
        total += seg->maxDoc();
    }
    return total;
}

void SegmentInfos::clear() {
    segments_.clear();
    incrementVersion();
}

std::string SegmentInfos::getSegmentsFileName(int64_t generation) {
    // Format: segments_N where N is base-36
    // Examples: segments_0, segments_1, ..., segments_a, segments_z
    std::ostringstream oss;
    oss << "segments_" << std::hex << generation;
    return oss.str();
}

void SegmentInfos::remove(int index) {
    if (index < 0 || index >= static_cast<int>(segments_.size())) {
        throw std::out_of_range("Segment index out of range");
    }
    segments_.erase(segments_.begin() + index);
    incrementVersion();
}

// ==================== Phase 4: Read segments_N ====================

int64_t SegmentInfos::findMaxGeneration(store::Directory& dir) {
    auto files = dir.listAll();
    int64_t maxGeneration = -1;

    for (const auto& file : files) {
        if (file.find("segments_") == 0) {
            // Parse generation from filename (hex format after "segments_")
            std::string genStr = file.substr(9);  // Skip "segments_"
            if (!genStr.empty()) {
                try {
                    int64_t gen = std::stoll(genStr, nullptr, 16);
                    maxGeneration = std::max(maxGeneration, gen);
                } catch (...) {
                    // Ignore files with invalid generation format
                }
            }
        }
    }

    return maxGeneration;
}

SegmentInfos SegmentInfos::read(store::Directory& dir, const std::string& fileName) {
    using namespace diagon::store;

    // Open input stream
    auto input = dir.openInput(fileName, IOContext::DEFAULT);

    // Read and validate magic header
    int32_t magic = input->readInt();
    if (magic != 0x3fd76c17) {
        throw IOException("Invalid segments file magic: " + std::to_string(magic));
    }

    // Read version
    int32_t version = input->readInt();
    if (version != 1) {
        throw IOException("Unsupported segments file version: " + std::to_string(version));
    }

    // Read generation
    int64_t generation = input->readLong();

    // Read number of segments
    int32_t numSegments = input->readInt();

    // Create SegmentInfos instance
    SegmentInfos infos;
    infos.generation_ = generation;

    // Read each segment
    for (int i = 0; i < numSegments; i++) {
        // Read segment name
        std::string segmentName = input->readString();

        // Read maxDoc
        int32_t maxDoc = input->readInt();

        // Read codec name
        std::string codecName = input->readString();

        // Create SegmentInfo
        auto segmentInfo = std::make_shared<SegmentInfo>(segmentName, maxDoc, codecName);

        // Read file list
        int32_t numFiles = input->readInt();
        std::vector<std::string> files;
        files.reserve(numFiles);
        for (int32_t j = 0; j < numFiles; j++) {
            files.push_back(input->readString());
        }
        segmentInfo->setFiles(files);

        // Read diagnostics
        int32_t numDiagnostics = input->readInt();
        for (int32_t j = 0; j < numDiagnostics; j++) {
            std::string key = input->readString();
            std::string value = input->readString();
            segmentInfo->setDiagnostic(key, value);
        }

        // Read size in bytes
        int64_t sizeInBytes = input->readLong();
        segmentInfo->setSizeInBytes(sizeInBytes);

        // Read delCount (Phase 3)
        int32_t delCount = input->readInt();
        segmentInfo->setDelCount(delCount);

        // Read FieldInfos (Phase 4)
        int32_t numFields = input->readInt();
        std::vector<FieldInfo> fieldInfos;
        fieldInfos.reserve(numFields);

        for (int32_t j = 0; j < numFields; j++) {
            FieldInfo fieldInfo;

            // Read field name and number
            fieldInfo.name = input->readString();
            fieldInfo.number = input->readInt();

            // Read index options
            fieldInfo.indexOptions = static_cast<IndexOptions>(input->readInt());

            // Read doc values type
            fieldInfo.docValuesType = static_cast<DocValuesType>(input->readInt());

            // Read flags
            fieldInfo.omitNorms = (input->readByte() != 0);
            fieldInfo.storeTermVector = (input->readByte() != 0);
            fieldInfo.storePayloads = (input->readByte() != 0);

            fieldInfos.push_back(std::move(fieldInfo));
        }

        // Set FieldInfos on SegmentInfo
        segmentInfo->setFieldInfos(FieldInfos(std::move(fieldInfos)));

        // Add to collection
        infos.segments_.push_back(segmentInfo);
    }

    // IndexInput automatically closes when destroyed (RAII)
    return infos;
}

SegmentInfos SegmentInfos::readLatestCommit(store::Directory& dir) {
    // Find maximum generation
    int64_t maxGeneration = findMaxGeneration(dir);

    if (maxGeneration < 0) {
        throw IOException("No segments_N files found in directory");
    }

    // Read the latest segments file
    std::string fileName = getSegmentsFileName(maxGeneration);
    return read(dir, fileName);
}

}  // namespace index
}  // namespace diagon
