// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/SegmentInfo.h"

#include "diagon/codecs/CodecUtil.h"
#include "diagon/store/Directory.h"
#include "diagon/store/IndexInput.h"

#include <algorithm>
#include <cstring>
#include <random>
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
    // Generate random 16-byte segment ID
    generateSegmentID(segmentID_);
}

void SegmentInfo::setSegmentID(const uint8_t* id) {
    std::memcpy(segmentID_, id, ID_LENGTH);
}

void SegmentInfo::generateSegmentID(uint8_t* out) {
    // Use std::random_device for cryptographic-quality randomness.
    // Matches Lucene's StringHelper.randomId() which uses SecureRandom.
    std::random_device rd;
    // random_device produces unsigned int (typically 32 bits).
    // Fill 16 bytes = 4 random ints.
    for (int i = 0; i < ID_LENGTH; i += sizeof(unsigned int)) {
        unsigned int val = rd();
        int remaining = ID_LENGTH - i;
        int toCopy = remaining < static_cast<int>(sizeof(unsigned int))
                         ? remaining
                         : static_cast<int>(sizeof(unsigned int));
        std::memcpy(out + i, &val, toCopy);
    }
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

/**
 * Convert a 64-bit generation to base-36 string (matching Lucene's
 * Long.toString(generation, Character.MAX_RADIX)).
 */
static std::string toBase36(int64_t value) {
    if (value == 0)
        return "0";
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::string result;
    uint64_t v = static_cast<uint64_t>(value);
    while (v > 0) {
        result.push_back(digits[v % 36]);
        v /= 36;
    }
    std::reverse(result.begin(), result.end());
    return result;
}

std::string SegmentInfos::getSegmentsFileName(int64_t generation) {
    // Format: segments_N where N is base-36
    // Examples: segments_0, segments_1, ..., segments_a, segments_z
    return "segments_" + toBase36(generation);
}

void SegmentInfos::remove(int index) {
    if (index < 0 || index >= static_cast<int>(segments_.size())) {
        throw std::out_of_range("Segment index out of range");
    }
    segments_.erase(segments_.begin() + index);
    incrementVersion();
}

// ==================== Phase 4: Read segments_N ====================

/**
 * Read segments_N in Lucene format (CodecUtil-framed).
 *
 * Wire format:
 *   IndexHeader: magic + "segments" + version + segmentID + suffix
 *   LuceneVersion: VInt(major) VInt(minor) VInt(bugfix)
 *   Version: int64
 *   NameCounter: int32
 *   SegCount: int32
 *   [MinSegmentLuceneVersion if segCount > 0]
 *   Per segment: SegName, SegID(16), SegCodec, DelGen, DelCount,
 *                FieldInfosGen, DocValuesGen, SoftDelCount, SciID,
 *                FieldInfosFiles (set), DocValuesUpdatesFiles (map of sets)
 *   CommitUserData: map<string,string>
 *   Footer: CodecUtil footer
 */
static SegmentInfos readLuceneFormat(store::IndexInput& in) {
    // Position is at byte 4 (after magic, which was already read by caller).
    // checkHeaderNoMagic reads codecName + version (big-endian).
    int32_t version = codecs::CodecUtil::checkHeaderNoMagic(
        in, "segments", 9, 10);

    // Read 16-byte segmentID + suffix (part of index header framing)
    uint8_t segId[codecs::CodecUtil::ID_LENGTH];
    in.readBytes(segId, codecs::CodecUtil::ID_LENGTH);
    uint8_t suffixLen = in.readByte();
    if (suffixLen > 0) {
        in.skipBytes(suffixLen);
    }

    // LuceneVersion (major.minor.bugfix)
    in.readVInt();
    in.readVInt();
    in.readVInt();

    // indexCreatedVersion (the major version that created this index)
    in.readVInt();

    // Version (index version counter) — big-endian int64
    int64_t indexVersion = in.readLong();

    // NameCounter — variable-length long (VLong)
    in.readVLong();

    // SegCount
    int32_t segCount = in.readInt();

    // MinSegmentLuceneVersion (only present if segCount > 0)
    if (segCount > 0) {
        in.readVInt();
        in.readVInt();
        in.readVInt();
    }

    SegmentInfos sis;
    // Set version from file (generation is set by caller from filename)
    // Use a simple loop to bump version to match
    for (int64_t v = 0; v < indexVersion; v++) {
        sis.incrementVersion();
    }

    for (int32_t i = 0; i < segCount; i++) {
        std::string segName = in.readString();

        uint8_t segID[SegmentInfo::ID_LENGTH];
        in.readBytes(segID, SegmentInfo::ID_LENGTH);

        std::string codecName = in.readString();

        int64_t delGen = in.readLong();
        (void)delGen;

        int32_t delCount = in.readInt();

        int64_t fieldInfosGen = in.readLong();
        (void)fieldInfosGen;

        int64_t docValuesGen = in.readLong();
        (void)docValuesGen;

        int32_t softDelCount = in.readInt();
        (void)softDelCount;

        // SCI ID (only in format > VERSION_74 = 9, i.e., version >= 10)
        if (version > 9) {
            uint8_t sciMarker = in.readByte();
            if (sciMarker == 1) {
                // Read 16-byte SCI ID
                in.skipBytes(SegmentInfo::ID_LENGTH);
            } else if (sciMarker != 0) {
                throw std::runtime_error("Invalid SegmentCommitInfo ID marker: " +
                                        std::to_string(sciMarker));
            }
        }

        // FieldInfosFiles: set of strings
        in.readSetOfStrings();

        // DocValuesUpdatesFiles: Map<int, Set<String>>
        int32_t dvUpdatesCount = in.readInt();
        for (int32_t j = 0; j < dvUpdatesCount; j++) {
            in.readInt();           // field number
            in.readSetOfStrings();  // update files
        }

        // Create SegmentInfo with maxDoc=0 (will be filled when .si is read)
        auto si = std::make_shared<SegmentInfo>(segName, 0, codecName);
        si->setSegmentID(segID);
        si->setDelCount(delCount);
        sis.add(si);
    }

    // CommitUserData: map<string, string>
    in.readMapOfStrings();

    // Footer
    codecs::CodecUtil::checkFooter(in);

    return sis;
}

int64_t SegmentInfos::findMaxGeneration(store::Directory& dir) {
    auto files = dir.listAll();
    int64_t maxGeneration = -1;

    for (const auto& file : files) {
        if (file.find("segments_") == 0) {
            // Parse generation from filename (hex format after "segments_")
            std::string genStr = file.substr(9);  // Skip "segments_"
            if (!genStr.empty()) {
                try {
                    int64_t gen = std::stoll(genStr, nullptr, 36);
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

    // Peek at the next byte to distinguish Diagon native from Lucene format.
    // - Diagon native: next 4 bytes are big-endian int32 version=1 (byte 5 = 0x00)
    // - Lucene format: next byte is VInt string length for codec name (1..127)
    int64_t savedPos = input->getFilePointer();  // should be 4 after magic
    uint8_t nextByte = input->readByte();
    input->seek(savedPos);

    if (nextByte > 0 && nextByte < 128) {
        // Lucene format: byte 5 is VInt string length of codec name
        auto sis = readLuceneFormat(*input);
        // Set generation from filename
        std::string genStr = fileName.substr(9);  // Skip "segments_"
        if (!genStr.empty()) {
            try {
                int64_t gen = std::stoll(genStr, nullptr, 36);
                // Bump generation to match file
                while (sis.getGeneration() < gen) {
                    sis.incrementGeneration();
                }
            } catch (...) {
                // Ignore parse errors
            }
        }
        return sis;
    }

    // Diagon native format: read version
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

            // Read point dimensions (BKD tree)
            fieldInfo.pointDimensionCount = input->readInt();
            fieldInfo.pointIndexDimensionCount = input->readInt();
            fieldInfo.pointNumBytes = input->readInt();

            fieldInfos.push_back(std::move(fieldInfo));
        }

        // Set FieldInfos on SegmentInfo
        segmentInfo->setFieldInfos(FieldInfos(std::move(fieldInfos)));

        // Read compound file flag
        bool isCompoundFile = (input->readByte() != 0);
        segmentInfo->setUseCompoundFile(isCompoundFile);

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
