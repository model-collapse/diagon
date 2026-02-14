// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/PackedFST.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace diagon {
namespace util {

// ==================== PackedFST ====================

PackedFST::PackedFST()
    : rootOffset_(0)
    , entriesLoaded_(false) {}

PackedFST::PackedFST(std::vector<uint8_t> data, size_t rootOffset)
    : data_(std::move(data))
    , rootOffset_(rootOffset)
    , entriesLoaded_(false) {}

PackedFST::PackedFST(std::vector<uint8_t> data, size_t rootOffset,
                     std::vector<std::pair<std::vector<uint8_t>, Output>> entries)
    : data_(std::move(data))
    , rootOffset_(rootOffset)
    , entries_(std::move(entries))
    , entriesLoaded_(true) {}

PackedFST::Output PackedFST::get(const BytesRef& input) const {
    if (data_.empty()) {
        return NO_OUTPUT;
    }

    size_t currentOffset = rootOffset_;
    Output accOutput = 0;

    for (size_t i = 0; i < input.length(); i++) {
        ByteReader reader(data_, currentOffset);

        // Read node encoding type
        uint8_t encoding = reader.readByte();

        ArcResult result;
        switch (static_cast<ArcEncoding>(encoding)) {
            case ARCS_FOR_DIRECT_ADDRESSING:
                result = findArcDirectAddressing(reader, input[i]);
                break;
            case ARCS_FOR_BINARY_SEARCH:
                result = findArcBinarySearch(reader, input[i]);
                break;
            case ARCS_FOR_CONTINUOUS:
                result = findArcContinuous(reader, input[i]);
                break;
            case ARCS_FOR_LINEAR_SCAN:
                result = findArcLinearScan(reader, input[i]);
                break;
            default:
                throw std::runtime_error("Invalid FST encoding");
        }

        if (!result.found) {
            return NO_OUTPUT;
        }

        accOutput += result.output;
        currentOffset = result.targetOffset;
    }

    // Check if final node
    ByteReader reader(data_, currentOffset);
    reader.readByte();  // Skip encoding
    bool isFinal = reader.readByte();
    if (!isFinal) {
        return NO_OUTPUT;
    }

    Output finalOutput = reader.readVLong();
    return accOutput + finalOutput;
}

PackedFST::Output PackedFST::getLongestPrefixMatch(const BytesRef& input, int& prefixLen) const {
    if (data_.empty()) {
        prefixLen = 0;
        return NO_OUTPUT;
    }

    size_t currentOffset = rootOffset_;
    Output accOutput = 0;
    Output lastFinalOutput = NO_OUTPUT;
    int lastFinalPos = 0;

    // Check if root is final
    {
        ByteReader reader(data_, currentOffset);
        reader.readByte();  // encoding
        bool isFinal = reader.readByte();
        if (isFinal) {
            lastFinalOutput = reader.readVLong();
            lastFinalPos = 0;
        }
    }

    for (size_t i = 0; i < input.length(); i++) {
        ByteReader reader(data_, currentOffset);
        uint8_t encoding = reader.readByte();

        ArcResult result;
        switch (static_cast<ArcEncoding>(encoding)) {
            case ARCS_FOR_DIRECT_ADDRESSING:
                result = findArcDirectAddressing(reader, input[i]);
                break;
            case ARCS_FOR_BINARY_SEARCH:
                result = findArcBinarySearch(reader, input[i]);
                break;
            case ARCS_FOR_CONTINUOUS:
                result = findArcContinuous(reader, input[i]);
                break;
            case ARCS_FOR_LINEAR_SCAN:
                result = findArcLinearScan(reader, input[i]);
                break;
            default:
                throw std::runtime_error("Invalid FST encoding");
        }

        if (!result.found) {
            break;
        }

        accOutput += result.output;
        currentOffset = result.targetOffset;

        // Check if current node is final
        ByteReader finalReader(data_, currentOffset);
        finalReader.readByte();  // encoding
        bool isFinal = finalReader.readByte();
        if (isFinal) {
            Output finalOutput = finalReader.readVLong();
            lastFinalOutput = accOutput + finalOutput;
            lastFinalPos = static_cast<int>(i + 1);
        }
    }

    prefixLen = lastFinalPos;
    return lastFinalOutput;
}

// ==================== Arc Lookup Methods ====================

PackedFST::ArcResult PackedFST::findArcDirectAddressing(ByteReader& reader, uint8_t label) const {
    ArcResult result;

    // Read direct addressing metadata
    // Format:
    // [encoding:byte][isFinal:byte][finalOutput:vlong?][numArcs:vint][bytesPerArc:vint][firstLabel:byte][bitTable:bytes][arcs...]
    reader.readByte();  // Skip isFinal
    if (data_[reader.getPosition() - 1]) {
        reader.readVLong();  // Skip finalOutput
    }

    int32_t numArcs = reader.readVInt();
    int32_t bytesPerArc = reader.readVInt();
    uint8_t firstLabel = reader.readByte();

    // Read bit table
    int bitTableBytes = (numArcs + 7) / 8;
    size_t bitTableStart = reader.getPosition();
    std::vector<uint8_t> bitTable(data_.begin() + bitTableStart,
                                  data_.begin() + bitTableStart + bitTableBytes);
    reader.setPosition(bitTableStart + bitTableBytes);

    // Check if label is in range
    int arcIndex = label - firstLabel;
    if (arcIndex < 0 || arcIndex >= numArcs) {
        return result;  // Not found
    }

    // Check bit table
    if (!isBitSet(arcIndex, bitTable)) {
        return result;  // Arc not present
    }

    // Count bits set before this index to find arc position
    int arcPosition = countBitsUpTo(arcIndex, bitTable);

    // Skip to the arc
    size_t arcStart = reader.getPosition() + arcPosition * bytesPerArc;
    reader.setPosition(arcStart);

    // Read arc data (FIXED sizes to match bytesPerArc)
    uint8_t arcLabel = reader.readByte();
    if (arcLabel != label) {
        throw std::runtime_error("FST direct addressing label mismatch");
    }

    result.found = true;
    result.output = reader.readFixedInt64();
    result.targetOffset = reader.readFixedInt32();

    return result;
}

PackedFST::ArcResult PackedFST::findArcBinarySearch(ByteReader& reader, uint8_t label) const {
    ArcResult result;

    // Skip isFinal/finalOutput
    reader.readByte();  // Skip isFinal
    if (data_[reader.getPosition() - 1]) {
        reader.readVLong();  // Skip finalOutput
    }

    int32_t numArcs = reader.readVInt();
    int32_t bytesPerArc = reader.readVInt();
    size_t arcsStart = reader.getPosition();

    // Binary search
    int low = 0;
    int high = numArcs - 1;

    while (low <= high) {
        int mid = (low + high) / 2;
        reader.setPosition(arcsStart + mid * bytesPerArc);

        uint8_t midLabel = reader.readByte();
        if (midLabel < label) {
            low = mid + 1;
        } else if (midLabel > label) {
            high = mid - 1;
        } else {
            // Found! (FIXED sizes to match bytesPerArc)
            result.found = true;
            result.output = reader.readFixedInt64();
            result.targetOffset = reader.readFixedInt32();
            return result;
        }
    }

    return result;  // Not found
}

PackedFST::ArcResult PackedFST::findArcContinuous(ByteReader& reader, uint8_t label) const {
    ArcResult result;

    // Skip isFinal/finalOutput
    reader.readByte();  // Skip isFinal
    if (data_[reader.getPosition() - 1]) {
        reader.readVLong();  // Skip finalOutput
    }

    int32_t numArcs = reader.readVInt();
    int32_t bytesPerArc = reader.readVInt();
    uint8_t firstLabel = reader.readByte();

    // Direct indexing for continuous range
    int arcIndex = label - firstLabel;
    if (arcIndex < 0 || arcIndex >= numArcs) {
        return result;  // Not in range
    }

    // Skip to the arc
    size_t arcStart = reader.getPosition() + arcIndex * bytesPerArc;
    reader.setPosition(arcStart);

    // Read arc data (no label needed for continuous range) - FIXED sizes
    result.found = true;
    result.output = reader.readFixedInt64();
    result.targetOffset = reader.readFixedInt32();

    return result;
}

PackedFST::ArcResult PackedFST::findArcLinearScan(ByteReader& reader, uint8_t label) const {
    ArcResult result;

    // Skip isFinal/finalOutput
    reader.readByte();  // Skip isFinal
    if (data_[reader.getPosition() - 1]) {
        reader.readVLong();  // Skip finalOutput
    }

    int32_t numArcs = reader.readVInt();

    // Linear scan through arcs
    for (int i = 0; i < numArcs; i++) {
        uint8_t arcLabel = reader.readByte();
        Output arcOutput = reader.readVLong();
        size_t targetOffset = reader.readVInt();

        if (arcLabel == label) {
            result.found = true;
            result.output = arcOutput;
            result.targetOffset = targetOffset;
            return result;
        } else if (arcLabel > label) {
            // Arcs are sorted, so we can stop
            return result;
        }
    }

    return result;
}

// ==================== Bit Table Operations ====================

bool PackedFST::isBitSet(int bitIndex, const std::vector<uint8_t>& bitTable) {
    int byteIndex = bitIndex / 8;
    int bitOffset = bitIndex % 8;
    if (byteIndex >= static_cast<int>(bitTable.size())) {
        return false;
    }
    return (bitTable[byteIndex] & (1 << bitOffset)) != 0;
}

int PackedFST::countBitsUpTo(int bitIndex, const std::vector<uint8_t>& bitTable) {
    int count = 0;
    for (int i = 0; i < bitIndex; i++) {
        if (isBitSet(i, bitTable)) {
            count++;
        }
    }
    return count;
}

// ==================== Serialization Helpers ====================

namespace {
void writeVInt(std::vector<uint8_t>& data, int32_t value) {
    while (value > 0x7F) {
        data.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    data.push_back(static_cast<uint8_t>(value));
}
}  // namespace

// ==================== Serialization ====================

std::vector<uint8_t> PackedFST::serialize() const {
    std::vector<uint8_t> result;

    // Write root offset
    writeVInt(result, static_cast<int32_t>(rootOffset_));

    // If we have serialized entries, use them directly (avoids deserialize + re-serialize)
    if (!serializedEntries_.empty() && !entriesLoaded_) {
        result.insert(result.end(), serializedEntries_.begin(), serializedEntries_.end());
    } else {
        // Otherwise serialize from entries_
        loadEntriesIfNeeded();

        // Write number of entries
        writeVInt(result, static_cast<int32_t>(entries_.size()));

        // Write each entry
        for (const auto& [term, output] : entries_) {
            writeVInt(result, static_cast<int32_t>(term.size()));
            result.insert(result.end(), term.begin(), term.end());
            // Write output as VLong (it's a file pointer, can be large)
            int64_t val = output;
            while (val > 0x7F) {
                result.push_back(static_cast<uint8_t>((val & 0x7F) | 0x80));
                val >>= 7;
            }
            result.push_back(static_cast<uint8_t>(val));
        }
    }

    // Write FST data
    result.insert(result.end(), data_.begin(), data_.end());

    return result;
}

std::unique_ptr<PackedFST> PackedFST::deserialize(const std::vector<uint8_t>& data) {
    ByteReader reader(data, 0);
    size_t rootOffset = reader.readVInt();

    // Save starting position of entries section
    size_t entriesStart = reader.getPosition();

    // Read number of entries
    int numEntries = reader.readVInt();

    // OPTIMIZATION: Skip loading entries into memory
    // Instead, store the raw serialized data for lazy loading
    for (int i = 0; i < numEntries; i++) {
        int termLen = reader.readVInt();
        // Skip term bytes
        reader.setPosition(reader.getPosition() + termLen);
        // Skip output
        reader.readVLong();
    }

    size_t entriesEnd = reader.getPosition();
    std::vector<uint8_t> fstData(data.begin() + reader.getPosition(), data.end());

    // Store serialized entries for lazy loading
    std::vector<uint8_t> serializedEntries(data.begin() + entriesStart, data.begin() + entriesEnd);

    auto fst = std::make_unique<PackedFST>(std::move(fstData), rootOffset);
    fst->serializedEntries_ = std::move(serializedEntries);
    fst->entriesLoaded_ = false;

    return fst;
}

const std::vector<std::pair<std::vector<uint8_t>, PackedFST::Output>>&
PackedFST::getAllEntries() const {
    loadEntriesIfNeeded();
    return entries_;
}

void PackedFST::loadEntriesIfNeeded() const {
    if (entriesLoaded_) {
        return;
    }

    // Lazy load: deserialize entries from stored raw data
    if (serializedEntries_.empty()) {
        entriesLoaded_ = true;
        return;
    }

    ByteReader reader(serializedEntries_, 0);
    int numEntries = reader.readVInt();

    entries_.reserve(numEntries);
    for (int i = 0; i < numEntries; i++) {
        int termLen = reader.readVInt();
        std::vector<uint8_t> term;
        term.reserve(termLen);
        for (int j = 0; j < termLen; j++) {
            term.push_back(reader.readByte());
        }
        int64_t output = reader.readVLong();
        entries_.emplace_back(std::move(term), output);
    }

    entriesLoaded_ = true;
}

// ==================== PackedFST::Builder ====================

PackedFST::Builder::Builder()
    : root_(new BuildNode())
    , finished_(false) {}

PackedFST::Builder::~Builder() {
    if (root_ != nullptr) {
        deleteNodeRecursive(root_);
        root_ = nullptr;
    }
}

void PackedFST::Builder::add(const BytesRef& input, Output output) {
    if (finished_) {
        throw std::runtime_error("FST already finished");
    }

    // Check sorted order
    if (lastInput_.length() > 0 && input <= lastInput_) {
        throw std::invalid_argument("Inputs must be added in sorted order");
    }

    // Find common prefix
    size_t prefixLen = 0;
    size_t minLen = std::min(lastInput_.length(), input.length());
    while (prefixLen < minLen && lastInput_[prefixLen] == input[prefixLen]) {
        prefixLen++;
    }

    // Traverse to common prefix node
    BuildNode* current = root_;
    for (size_t i = 0; i < prefixLen; i++) {
        const auto* arc = current->findArc(lastInput_[i]);
        if (!arc) {
            throw std::runtime_error("FST structure corrupted");
        }
        current = arc->target;
    }

    // Add new path
    for (size_t i = prefixLen; i < input.length(); i++) {
        BuildNode* newNode = new BuildNode();
        BuildNode::Arc newArc(input[i], newNode);
        current->arcs.push_back(newArc);
        std::sort(current->arcs.begin(), current->arcs.end());
        current = newNode;
    }

    // Mark final node
    current->isFinal = true;
    current->output = output;

    // Record entry
    entries_.emplace_back(input, output);

    // Save last input
    lastInputData_.assign(input.data(), input.data() + input.length());
    lastInput_ = BytesRef(lastInputData_.data(), lastInputData_.size());
}

std::unique_ptr<PackedFST> PackedFST::Builder::finish() {
    if (finished_) {
        throw std::runtime_error("FST already finished");
    }
    finished_ = true;

    // Pack the FST into byte array
    std::vector<uint8_t> packedData;
    size_t rootOffset = packNode(root_, packedData);

    // Convert Builder::Entry to PackedFST entries format
    std::vector<std::pair<std::vector<uint8_t>, PackedFST::Output>> fstEntries;
    fstEntries.reserve(entries_.size());
    for (const auto& entry : entries_) {
        fstEntries.emplace_back(entry.termData, entry.output);
    }

    auto fst = std::make_unique<PackedFST>(std::move(packedData), rootOffset,
                                           std::move(fstEntries));

    // Clean up build tree
    deleteNodeRecursive(root_);
    root_ = nullptr;

    return fst;
}

// ==================== Packing Methods ====================

size_t PackedFST::Builder::packNode(BuildNode* node, std::vector<uint8_t>& data) {
    if (node->nodeOffset != 0) {
        // Already packed
        return node->nodeOffset;
    }

    // Pack children first (depth-first)
    for (auto& arc : node->arcs) {
        packNode(arc.target, data);
    }

    // Choose encoding
    node->encoding = chooseEncoding(node);
    node->nodeOffset = data.size();

    // Write encoding byte
    data.push_back(static_cast<uint8_t>(node->encoding));

    // Write isFinal and output
    data.push_back(node->isFinal ? 1 : 0);
    if (node->isFinal) {
        writeVLong(data, node->output);
    }

    // Pack arcs based on encoding
    switch (node->encoding) {
        case ARCS_FOR_DIRECT_ADDRESSING:
            packDirectAddressing(node, data);
            break;
        case ARCS_FOR_BINARY_SEARCH:
            packBinarySearch(node, data);
            break;
        case ARCS_FOR_CONTINUOUS:
            packContinuous(node, data);
            break;
        case ARCS_FOR_LINEAR_SCAN:
            packLinearScan(node, data);
            break;
    }

    return node->nodeOffset;
}

PackedFST::ArcEncoding PackedFST::Builder::chooseEncoding(const BuildNode* node) const {
    if (node->arcs.empty()) {
        return ARCS_FOR_LINEAR_SCAN;
    }

    int numArcs = node->arcs.size();
    uint8_t minLabel = node->arcs.front().label;
    uint8_t maxLabel = node->arcs.back().label;
    int labelRange = maxLabel - minLabel + 1;

    // Check if continuous (all labels present in range)
    if (labelRange == numArcs) {
        return ARCS_FOR_CONTINUOUS;
    }

    // Check if dense enough for direct addressing
    // Use direct addressing if range <= 64 and density >= 25%
    if (labelRange <= 64 && numArcs >= labelRange / 4) {
        return ARCS_FOR_DIRECT_ADDRESSING;
    }

    // Use binary search for moderate density
    if (numArcs >= 6) {
        return ARCS_FOR_BINARY_SEARCH;
    }

    // Linear scan for sparse nodes
    return ARCS_FOR_LINEAR_SCAN;
}

void PackedFST::Builder::packDirectAddressing(const BuildNode* node, std::vector<uint8_t>& data) {
    uint8_t minLabel = node->arcs.front().label;
    uint8_t maxLabel = node->arcs.back().label;
    int numArcs = maxLabel - minLabel + 1;

    // Calculate fixed arc size
    int bytesPerArc = 1 + 8 + 4;  // label + output (max 8) + target (max 4)

    writeVInt(data, numArcs);
    writeVInt(data, bytesPerArc);
    data.push_back(minLabel);

    // Build bit table
    int bitTableBytes = (numArcs + 7) / 8;
    std::vector<uint8_t> bitTable(bitTableBytes, 0);

    for (const auto& arc : node->arcs) {
        int bitIndex = arc.label - minLabel;
        int byteIndex = bitIndex / 8;
        int bitOffset = bitIndex % 8;
        bitTable[byteIndex] |= (1 << bitOffset);
    }

    // Write bit table
    data.insert(data.end(), bitTable.begin(), bitTable.end());

    // Write arcs (only present ones) with FIXED sizes
    for (const auto& arc : node->arcs) {
        data.push_back(arc.label);
        writeFixedInt64(data, arc.output);
        writeFixedInt32(data, static_cast<int32_t>(arc.target->nodeOffset));
    }
}

void PackedFST::Builder::packBinarySearch(const BuildNode* node, std::vector<uint8_t>& data) {
    int bytesPerArc = 1 + 8 + 4;  // label + output + target

    writeVInt(data, static_cast<int32_t>(node->arcs.size()));
    writeVInt(data, bytesPerArc);

    // Write arcs with FIXED sizes to match bytesPerArc
    for (const auto& arc : node->arcs) {
        data.push_back(arc.label);
        writeFixedInt64(data, arc.output);
        writeFixedInt32(data, static_cast<int32_t>(arc.target->nodeOffset));
    }
}

void PackedFST::Builder::packContinuous(const BuildNode* node, std::vector<uint8_t>& data) {
    uint8_t firstLabel = node->arcs.front().label;
    int bytesPerArc = 8 + 4;  // output + target (no label needed)

    writeVInt(data, static_cast<int32_t>(node->arcs.size()));
    writeVInt(data, bytesPerArc);
    data.push_back(firstLabel);

    // Write arcs with FIXED sizes to match bytesPerArc
    for (const auto& arc : node->arcs) {
        writeFixedInt64(data, arc.output);
        writeFixedInt32(data, static_cast<int32_t>(arc.target->nodeOffset));
    }
}

void PackedFST::Builder::packLinearScan(const BuildNode* node, std::vector<uint8_t>& data) {
    writeVInt(data, static_cast<int32_t>(node->arcs.size()));

    for (const auto& arc : node->arcs) {
        data.push_back(arc.label);
        writeVLong(data, arc.output);
        writeVInt(data, static_cast<int32_t>(arc.target->nodeOffset));
    }
}

void PackedFST::Builder::writeVInt(std::vector<uint8_t>& data, int32_t value) {
    while (value > 0x7F) {
        data.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    data.push_back(static_cast<uint8_t>(value));
}

void PackedFST::Builder::writeVLong(std::vector<uint8_t>& data, int64_t value) {
    while (value > 0x7F) {
        data.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    data.push_back(static_cast<uint8_t>(value));
}

// Fixed-size encoding for direct addressing and binary search
void PackedFST::Builder::writeFixedInt64(std::vector<uint8_t>& data, int64_t value) {
    // Write 8 bytes in little-endian order
    for (int i = 0; i < 8; i++) {
        data.push_back(static_cast<uint8_t>(value & 0xFF));
        value >>= 8;
    }
}

void PackedFST::Builder::writeFixedInt32(std::vector<uint8_t>& data, int32_t value) {
    // Write 4 bytes in little-endian order
    for (int i = 0; i < 4; i++) {
        data.push_back(static_cast<uint8_t>(value & 0xFF));
        value >>= 8;
    }
}

void PackedFST::Builder::deleteNodeRecursive(BuildNode* node) {
    for (auto& arc : node->arcs) {
        deleteNodeRecursive(arc.target);
    }
    delete node;
}

// ==================== BuildNode ====================

PackedFST::Builder::BuildNode::~BuildNode() {
    // Children are deleted by Builder::deleteNodeRecursive
}

const PackedFST::Builder::BuildNode::Arc*
PackedFST::Builder::BuildNode::findArc(uint8_t label) const {
    auto it = std::lower_bound(arcs.begin(), arcs.end(), label,
                               [](const Arc& arc, uint8_t val) { return arc.label < val; });
    if (it != arcs.end() && it->label == label) {
        return &(*it);
    }
    return nullptr;
}

}  // namespace util
}  // namespace diagon
