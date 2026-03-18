// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/store/CompoundDirectory.h"

#include "diagon/codecs/CodecUtil.h"
#include "diagon/store/CompoundFileWriter.h"
#include "diagon/store/IOContext.h"
#include "diagon/util/Exceptions.h"

#include <algorithm>

namespace diagon::store {

// ==================== Static Factory ====================

std::unique_ptr<CompoundDirectory> CompoundDirectory::open(Directory& dir,
                                                           const std::string& segmentName) {
    std::string entriesFile = CompoundFileWriter::getEntriesFileName(segmentName);
    std::string dataFile = CompoundFileWriter::getDataFileName(segmentName);

    // Read entry table from .cfe (auto-detects Diagon native vs Lucene format)
    auto entries = readEntries(dir, entriesFile);

    // Open handle to .cfs data file
    auto handle = dir.openInput(dataFile, IOContext::READ);

    // Validate .cfs header if it's Lucene format (starts with CodecUtil magic).
    // Entry offsets in .cfe are absolute from start of .cfs, so we just validate
    // the header for integrity and then reset the position.
    if (handle->length() >= 4) {
        int64_t savedPos = handle->getFilePointer();
        int32_t magic = handle->readInt();
        handle->seek(savedPos);

        if (magic == codecs::CodecUtil::CODEC_MAGIC) {
            // Lucene90 compound data file — validate codec name and version
            codecs::CodecUtil::checkHeader(*handle, "Lucene90CompoundData", 0, 0);
            // Skip segmentID(16) + suffixLen(1) + suffix(N) — don't validate these
            handle->seek(0);
        }
    }

    return std::make_unique<CompoundDirectory>(dir, segmentName, std::move(entries),
                                               std::move(handle));
}

// ==================== Constructor/Destructor ====================

CompoundDirectory::CompoundDirectory(Directory& dir, const std::string& segmentName,
                                     std::unordered_map<std::string, FileEntry> entries,
                                     std::unique_ptr<IndexInput> handle)
    : directory_(dir)
    , segmentName_(segmentName)
    , entries_(std::move(entries))
    , handle_(std::move(handle)) {}

CompoundDirectory::~CompoundDirectory() {
    if (!isClosed()) {
        try {
            close();
        } catch (...) {
            // Ignore exceptions in destructor
        }
    }
}

// ==================== Read Operations ====================

std::vector<std::string> CompoundDirectory::listAll() const {
    ensureOpen();

    std::vector<std::string> result;
    result.reserve(entries_.size());
    for (const auto& [strippedName, entry] : entries_) {
        // Reconstruct full filename: segmentName + strippedName
        result.push_back(segmentName_ + strippedName);
    }
    std::sort(result.begin(), result.end());
    return result;
}

int64_t CompoundDirectory::fileLength(const std::string& name) const {
    ensureOpen();

    std::string id = stripSegmentName(name, segmentName_);
    auto it = entries_.find(id);
    if (it == entries_.end()) {
        throw FileNotFoundException("File not found in compound file: " + name);
    }
    return it->second.length;
}

std::unique_ptr<IndexInput> CompoundDirectory::openInput(const std::string& name,
                                                         const IOContext& context) const {
    ensureOpen();

    std::string id = stripSegmentName(name, segmentName_);
    auto it = entries_.find(id);
    if (it == entries_.end()) {
        throw FileNotFoundException("No sub-file '" + id + "' found in compound file '" +
                                    segmentName_ + ".cfs' (fileName=" + name + ")");
    }

    const FileEntry& entry = it->second;
    return handle_->slice(name, entry.offset, entry.length);
}

// ==================== Write Operations (Unsupported) ====================

void CompoundDirectory::deleteFile(const std::string& name) {
    throw UnsupportedOperationException("CompoundDirectory is read-only");
}

std::unique_ptr<IndexOutput> CompoundDirectory::createOutput(const std::string& name,
                                                             const IOContext& context) {
    throw UnsupportedOperationException("CompoundDirectory is read-only");
}

std::unique_ptr<IndexOutput> CompoundDirectory::createTempOutput(const std::string& prefix,
                                                                 const std::string& suffix,
                                                                 const IOContext& context) {
    throw UnsupportedOperationException("CompoundDirectory is read-only");
}

void CompoundDirectory::rename(const std::string& source, const std::string& dest) {
    throw UnsupportedOperationException("CompoundDirectory is read-only");
}

void CompoundDirectory::sync(const std::vector<std::string>& names) {
    throw UnsupportedOperationException("CompoundDirectory is read-only");
}

void CompoundDirectory::syncMetaData() {
    // No-op for compound directory (matches Lucene)
}

std::unique_ptr<Lock> CompoundDirectory::obtainLock(const std::string& name) {
    throw UnsupportedOperationException("CompoundDirectory is read-only");
}

// ==================== Lifecycle ====================

void CompoundDirectory::close() {
    if (!isClosed()) {
        closed_.store(true, std::memory_order_relaxed);
        handle_.reset();
    }
}

// ==================== Utilities ====================

std::string CompoundDirectory::toString() const {
    return "CompoundDirectory(segment=\"" + segmentName_ + "\" in dir=" + directory_.toString() +
           ")";
}

std::string CompoundDirectory::stripSegmentName(const std::string& fileName,
                                                const std::string& segmentName) {
    if (fileName.size() > segmentName.size() &&
        fileName.compare(0, segmentName.size(), segmentName) == 0) {
        return fileName.substr(segmentName.size());
    }
    return fileName;
}

// ==================== Entry Reading ====================

std::unordered_map<std::string, CompoundDirectory::FileEntry>
CompoundDirectory::readEntries(Directory& dir, const std::string& entriesFile) {
    auto input = dir.openInput(entriesFile, IOContext::READONCE);

    // Detect format: Lucene (CodecUtil header) vs Diagon native (VInt count first).
    // CodecUtil magic = 0x3fd76c17; Diagon VInt count is always small (< 128 = 0x80).
    int64_t savedPos = input->getFilePointer();
    int32_t firstInt = input->readInt();
    input->seek(savedPos);

    if (firstInt == codecs::CodecUtil::CODEC_MAGIC) {
        // Lucene format: CodecUtil IndexHeader + entries + CodecUtil Footer
        return readEntriesLucene(*input);
    }

    // Diagon native format: VInt(count) + entries (no header/footer)
    return readEntriesNative(*input);
}

std::unordered_map<std::string, CompoundDirectory::FileEntry>
CompoundDirectory::readEntriesNative(IndexInput& input) {
    int32_t numEntries = input.readVInt();
    if (numEntries < 0) {
        throw IOException("Corrupt compound entries file: negative entry count");
    }

    std::unordered_map<std::string, FileEntry> entries;
    entries.reserve(numEntries);

    for (int32_t i = 0; i < numEntries; i++) {
        std::string name = input.readString();
        int64_t offset = input.readLong();
        int64_t length = input.readLong();

        if (offset < 0 || length < 0) {
            throw IOException("Corrupt compound entries file: negative offset/length for '" + name +
                              "'");
        }

        auto [it, inserted] = entries.emplace(std::move(name), FileEntry{offset, length});
        if (!inserted) {
            throw IOException("Duplicate entry in compound file: '" + it->first + "'");
        }
    }

    return entries;
}

std::unordered_map<std::string, CompoundDirectory::FileEntry>
CompoundDirectory::readEntriesLucene(IndexInput& input) {
    // Validate Lucene90 compound entries header
    // Codec name: "Lucene90CompoundEntries", version: 0
    codecs::CodecUtil::checkHeader(input, "Lucene90CompoundEntries", 0, 0);
    // Skip segmentID(16) + suffixLen(1) + suffix — we don't validate these here
    input.skipBytes(codecs::CodecUtil::ID_LENGTH);  // 16-byte segment ID
    uint8_t suffixLen = input.readByte();
    if (suffixLen > 0) {
        input.skipBytes(suffixLen);
    }

    int32_t numEntries = input.readVInt();
    if (numEntries < 0) {
        throw IOException("Corrupt compound entries file: negative entry count");
    }

    std::unordered_map<std::string, FileEntry> entries;
    entries.reserve(numEntries);

    for (int32_t i = 0; i < numEntries; i++) {
        std::string name = input.readString();
        // Lucene writes offset/length in little-endian via ByteBuffersDataOutput
        int64_t offset = input.readLongLE();
        int64_t length = input.readLongLE();

        if (offset < 0 || length < 0) {
            throw IOException("Corrupt compound entries file: negative offset/length for '" + name +
                              "'");
        }

        auto [it, inserted] = entries.emplace(std::move(name), FileEntry{offset, length});
        if (!inserted) {
            throw IOException("Duplicate entry in compound file: '" + it->first + "'");
        }
    }

    // Validate footer (CodecUtil footer is always big-endian)
    codecs::CodecUtil::checkFooter(input);

    return entries;
}

}  // namespace diagon::store
