// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/lucene90/LuceneFST.h"

#include "diagon/codecs/CodecUtil.h"
#include "diagon/util/Exceptions.h"

#include <algorithm>
#include <stdexcept>

namespace diagon {
namespace codecs {
namespace lucene90 {

// FST file format constants (from FST.java)
static constexpr const char* FILE_FORMAT_NAME = "FST";
static constexpr int32_t VERSION_START = 7;
static constexpr int32_t VERSION_CURRENT = 10;
static constexpr int32_t VERSION_LITTLE_ENDIAN = 8;

// ==================== Constructors ====================

LuceneFST::LuceneFST(store::IndexInput& metaIn, store::IndexInput& fstIn,
                       int64_t indexStartFP) {
    // Read FST metadata from .tmd (per-field)
    // Format: CodecUtil header + emptyOutput + inputType + startNode + numBytes
    version_ = CodecUtil::checkHeader(metaIn, FILE_FORMAT_NAME, VERSION_START, VERSION_CURRENT);

    // Read empty output (if FST accepts empty string)
    uint8_t hasEmptyOutput = metaIn.readByte();
    if (hasEmptyOutput == 1) {
        int numBytes = metaIn.readVInt();
        if (numBytes > 0) {
            // Read empty output bytes, then deserialize in reverse order
            std::vector<uint8_t> emptyBytes(numBytes);
            metaIn.readBytes(emptyBytes.data(), numBytes);
            // ByteSequenceOutputs.readFinalOutput reads from a reverse reader:
            // setPosition(numBytes-1), then readVInt(len), readBytes(len)
            // For ByteSequenceOutputs, readFinalOutput = read() = readVInt(len) + bytes
            // The reverse reader reads from high address downward.
            // In practice, for BlockTree the empty output is the root block code.
            // We need to reverse-read: position starts at numBytes-1 going down.
            // Store raw bytes for now; the caller decodes via readMSBVLong at use site.
            // The reverse-reader deserialization is complex and not needed for BlockTree
            // since the root code is always the first field's output, decoded by FieldReader.
            emptyOutput_ = std::move(emptyBytes);
        }
    }

    // Input type: 0=BYTE1, 1=BYTE2, 2=BYTE4
    inputType_ = metaIn.readByte();
    if (inputType_ > 2) {
        throw CorruptIndexException("invalid FST input type: " + std::to_string(inputType_),
                                     metaIn.toString());
    }

    // Start node address
    startNode_ = metaIn.readVLong();

    // Total FST byte array size
    numBytes_ = metaIn.readVLong();

    // Load FST byte array from .tip
    bytes_.resize(numBytes_);
    fstIn.seek(indexStartFP);
    fstIn.readBytes(bytes_.data(), numBytes_);
}

LuceneFST::LuceneFST(int64_t startNode, int version, std::vector<uint8_t> bytes,
                       std::vector<uint8_t> emptyOutput)
    : version_(version)
    , startNode_(startNode)
    , numBytes_(static_cast<int64_t>(bytes.size()))
    , emptyOutput_(std::move(emptyOutput))
    , inputType_(0)
    , bytes_(std::move(bytes)) {}

// ==================== Public API ====================

void LuceneFST::getFirstArc(Arc& arc) const {
    arc = Arc{};

    if (!emptyOutput_.empty()) {
        arc.flags = BIT_FINAL_ARC | BIT_LAST_ARC | BIT_ARC_HAS_FINAL_OUTPUT;
        // The empty output is the root block code (stored as raw reverse-encoded bytes).
        // For BlockTree, the caller decodes this via readMSBVLong to get rootBlockFP + flags.
        arc.nextFinalOutput = emptyOutput_;
    } else {
        arc.flags = BIT_LAST_ARC;
    }
    arc.target = startNode_;
}

bool LuceneFST::findTargetArc(int labelToMatch, const Arc& follow, Arc& arc) const {
    // Handle END_LABEL (end-of-string)
    if (labelToMatch == END_LABEL) {
        if (follow.isFinal()) {
            arc = Arc{};
            arc.label = END_LABEL;
            arc.target = FINAL_END_NODE;
            arc.output = follow.nextFinalOutput;
            arc.flags = BIT_LAST_ARC;
            return true;
        }
        return false;
    }

    if (!targetHasArcs(follow)) {
        return false;
    }

    // Read the node header at follow's target
    int64_t pos = follow.target;
    uint8_t nodeFlags = readByte(pos);
    pos++;

    if (nodeFlags == ARCS_FOR_DIRECT_ADDRESSING) {
        // Direct-addressing node: bit table + firstLabel + fixed-length arcs
        int numArcs = readVInt(pos);
        int bytesPerArc = readVInt(pos);

        // Bit table for presence
        int numPresenceBytes = getNumPresenceBytes(numArcs);
        int64_t bitTableStart = pos;
        pos += numPresenceBytes;

        // First label
        int firstLabel = readLabel(pos);
        int64_t posArcsStart = pos;

        int arcIndex = labelToMatch - firstLabel;
        if (arcIndex < 0 || arcIndex >= numArcs) {
            return false;
        }

        // Check presence bit
        if (!isBitSet(arcIndex, bitTableStart)) {
            return false;
        }

        // Count set bits before this index to find arc position
        int presenceIndex = countBitsUpTo(arcIndex, bitTableStart);

        // Read arc at presenceIndex
        arc = Arc{};
        arc.nodeFlags = nodeFlags;
        arc.bytesPerArc = bytesPerArc;
        arc.posArcsStart = posArcsStart;
        arc.numArcs = numArcs;
        arc.firstLabel = firstLabel;
        arc.bitTableStart = bitTableStart;
        arc.arcIdx = presenceIndex;
        arc.label = labelToMatch;

        int64_t arcPos = posArcsStart + static_cast<int64_t>(presenceIndex) * bytesPerArc;
        arc.flags = readByte(arcPos);
        arcPos++;
        // No label byte in direct-addressing (label is inferred)
        readArcFields(arc, arcPos);
        return true;

    } else if (nodeFlags == ARCS_FOR_CONTINUOUS) {
        // Continuous node: all labels present in range [firstLabel, firstLabel+numArcs)
        int numArcs = readVInt(pos);
        int bytesPerArc = readVInt(pos);
        int firstLabel = readLabel(pos);
        int64_t posArcsStart = pos;

        int rangeIndex = labelToMatch - firstLabel;
        if (rangeIndex < 0 || rangeIndex >= numArcs) {
            return false;
        }

        arc = Arc{};
        arc.nodeFlags = nodeFlags;
        arc.bytesPerArc = bytesPerArc;
        arc.posArcsStart = posArcsStart;
        arc.numArcs = numArcs;
        arc.firstLabel = firstLabel;
        arc.arcIdx = rangeIndex;
        arc.label = labelToMatch;

        int64_t arcPos = posArcsStart + static_cast<int64_t>(rangeIndex) * bytesPerArc;
        arc.flags = readByte(arcPos);
        arcPos++;
        // No label byte in continuous (label is inferred)
        readArcFields(arc, arcPos);
        return true;

    } else if (nodeFlags == ARCS_FOR_BINARY_SEARCH) {
        // Binary-search node: sorted fixed-length arcs
        int numArcs = readVInt(pos);
        int bytesPerArc = readVInt(pos);
        int64_t posArcsStart = pos;

        int low = 0, high = numArcs - 1;
        while (low <= high) {
            int mid = static_cast<int>(static_cast<unsigned>(low + high) >> 1);
            int64_t arcPos = posArcsStart + static_cast<int64_t>(mid) * bytesPerArc;
            int64_t readPos = arcPos;
            uint8_t arcFlags = readByte(readPos);
            readPos++;
            int midLabel = readLabel(readPos);

            if (midLabel < labelToMatch) {
                low = mid + 1;
            } else if (midLabel > labelToMatch) {
                high = mid - 1;
            } else {
                // Found it
                arc = Arc{};
                arc.nodeFlags = nodeFlags;
                arc.bytesPerArc = bytesPerArc;
                arc.posArcsStart = posArcsStart;
                arc.numArcs = numArcs;
                arc.arcIdx = mid;
                arc.label = midLabel;
                arc.flags = arcFlags;
                readArcFields(arc, readPos);
                return true;
            }
        }
        return false;

    } else {
        // Variable-length linear list
        // Read arcs sequentially until we find the label or pass it
        readFirstRealTargetArc(follow.target, arc);

        while (true) {
            if (arc.label == labelToMatch) {
                return true;
            }
            if (arc.label > labelToMatch || arc.isLast()) {
                return false;
            }
            readNextRealArc(arc);
        }
    }
}

// ==================== Internal Byte Reading ====================

uint8_t LuceneFST::readByte(int64_t pos) const {
    if (pos < 0 || pos >= static_cast<int64_t>(bytes_.size())) {
        throw std::runtime_error("FST: read out of bounds at position " + std::to_string(pos));
    }
    return bytes_[pos];
}

int LuceneFST::readLabel(int64_t& pos) const {
    if (inputType_ == 0) {
        // BYTE1: unsigned byte
        return readByte(pos++) & 0xFF;
    } else if (inputType_ == 1) {
        // BYTE2: unsigned short
        if (version_ < VERSION_LITTLE_ENDIAN) {
            // Big-endian (old versions)
            uint8_t b0 = readByte(pos++);
            uint8_t b1 = readByte(pos++);
            return ((b0 & 0xFF) << 8) | (b1 & 0xFF);
        } else {
            // Little-endian (version >= 8)
            uint8_t b0 = readByte(pos++);
            uint8_t b1 = readByte(pos++);
            return (b0 & 0xFF) | ((b1 & 0xFF) << 8);
        }
    } else {
        // BYTE4: VInt
        return readVInt(pos);
    }
}

int32_t LuceneFST::readVInt(int64_t& pos) const {
    uint8_t b = readByte(pos++);
    if ((b & 0x80) == 0) return b;
    int32_t i = b & 0x7F;
    b = readByte(pos++);
    i |= (b & 0x7F) << 7;
    if ((b & 0x80) == 0) return i;
    b = readByte(pos++);
    i |= (b & 0x7F) << 14;
    if ((b & 0x80) == 0) return i;
    b = readByte(pos++);
    i |= (b & 0x7F) << 21;
    if ((b & 0x80) == 0) return i;
    b = readByte(pos++);
    i |= (b & 0x0F) << 28;
    return i;
}

int64_t LuceneFST::readVLong(int64_t& pos) const {
    uint8_t b = readByte(pos++);
    if ((b & 0x80) == 0) return b;
    int64_t i = b & 0x7FL;
    b = readByte(pos++);
    i |= (static_cast<int64_t>(b) & 0x7FL) << 7;
    if ((b & 0x80) == 0) return i;
    b = readByte(pos++);
    i |= (static_cast<int64_t>(b) & 0x7FL) << 14;
    if ((b & 0x80) == 0) return i;
    b = readByte(pos++);
    i |= (static_cast<int64_t>(b) & 0x7FL) << 21;
    if ((b & 0x80) == 0) return i;
    b = readByte(pos++);
    i |= (static_cast<int64_t>(b) & 0x7FL) << 28;
    if ((b & 0x80) == 0) return i;
    b = readByte(pos++);
    i |= (static_cast<int64_t>(b) & 0x7FL) << 35;
    if ((b & 0x80) == 0) return i;
    b = readByte(pos++);
    i |= (static_cast<int64_t>(b) & 0x7FL) << 42;
    if ((b & 0x80) == 0) return i;
    b = readByte(pos++);
    i |= (static_cast<int64_t>(b) & 0x7FL) << 49;
    if ((b & 0x80) == 0) return i;
    b = readByte(pos++);
    i |= (static_cast<int64_t>(b) & 0x01L) << 56;
    return i;
}

std::vector<uint8_t> LuceneFST::readOutput(int64_t& pos) const {
    // ByteSequenceOutputs.read(): VInt(length) + bytes[length]
    int len = readVInt(pos);
    if (len == 0) return {};
    std::vector<uint8_t> result(len);
    for (int i = 0; i < len; ++i) {
        result[i] = readByte(pos++);
    }
    return result;
}

std::vector<uint8_t> LuceneFST::readFinalOutput(int64_t& pos) const {
    // ByteSequenceOutputs.readFinalOutput() = read()
    return readOutput(pos);
}

// ==================== Arc Reading ====================

void LuceneFST::readFirstRealTargetArc(int64_t nodeAddr, Arc& arc) const {
    int64_t pos = nodeAddr;
    uint8_t nodeFlags = readByte(pos);
    pos++;

    arc = Arc{};
    arc.nodeFlags = nodeFlags;

    if (nodeFlags == ARCS_FOR_BINARY_SEARCH || nodeFlags == ARCS_FOR_DIRECT_ADDRESSING
        || nodeFlags == ARCS_FOR_CONTINUOUS) {
        // Fixed-length arc node: read header, then first arc
        arc.numArcs = readVInt(pos);
        arc.bytesPerArc = readVInt(pos);

        if (nodeFlags == ARCS_FOR_DIRECT_ADDRESSING) {
            arc.bitTableStart = pos;
            pos += getNumPresenceBytes(arc.numArcs);
            arc.firstLabel = readLabel(pos);
            arc.posArcsStart = pos;
            // Read the first present arc
            arc.arcIdx = 0;
            // For direct-addressing, find the first set bit
            for (int i = 0; i < arc.numArcs; ++i) {
                if (isBitSet(i, arc.bitTableStart)) {
                    int64_t arcPos = arc.posArcsStart
                                     + static_cast<int64_t>(countBitsUpTo(i, arc.bitTableStart))
                                           * arc.bytesPerArc;
                    arc.flags = readByte(arcPos);
                    arcPos++;
                    arc.label = arc.firstLabel + i;
                    arc.arcIdx = countBitsUpTo(i, arc.bitTableStart);
                    readArcFields(arc, arcPos);
                    return;
                }
            }
        } else if (nodeFlags == ARCS_FOR_CONTINUOUS) {
            arc.firstLabel = readLabel(pos);
            arc.posArcsStart = pos;
            arc.arcIdx = 0;
            int64_t arcPos = arc.posArcsStart;
            arc.flags = readByte(arcPos);
            arcPos++;
            arc.label = arc.firstLabel;
            readArcFields(arc, arcPos);
        } else {
            // Binary search: first arc at index 0
            arc.posArcsStart = pos;
            arc.arcIdx = 0;
            int64_t arcPos = arc.posArcsStart;
            arc.flags = readByte(arcPos);
            arcPos++;
            arc.label = readLabel(arcPos);
            readArcFields(arc, arcPos);
        }
    } else {
        // Variable-length linear list: first arc starts right here
        arc.flags = nodeFlags;  // The first byte we read IS the arc flags
        arc.label = readLabel(pos);
        readArcFields(arc, pos);
        arc.nextArc = pos;  // Position after this arc's data
    }
}

void LuceneFST::readNextRealArc(Arc& arc) const {
    if (arc.nodeFlags == ARCS_FOR_BINARY_SEARCH || arc.nodeFlags == ARCS_FOR_DIRECT_ADDRESSING
        || arc.nodeFlags == ARCS_FOR_CONTINUOUS) {
        // Fixed-length arcs: advance to next index
        if (arc.nodeFlags == ARCS_FOR_DIRECT_ADDRESSING) {
            // Find next set bit after current
            int currentBitIdx = arc.label - arc.firstLabel;
            for (int i = currentBitIdx + 1; i < arc.numArcs; ++i) {
                if (isBitSet(i, arc.bitTableStart)) {
                    int presenceIdx = countBitsUpTo(i, arc.bitTableStart);
                    int64_t arcPos = arc.posArcsStart
                                     + static_cast<int64_t>(presenceIdx) * arc.bytesPerArc;
                    arc.flags = readByte(arcPos);
                    arcPos++;
                    arc.label = arc.firstLabel + i;
                    arc.arcIdx = presenceIdx;
                    readArcFields(arc, arcPos);
                    return;
                }
            }
            // No more arcs
            arc.flags |= BIT_LAST_ARC;
        } else {
            // Binary search or continuous: simple index advance
            arc.arcIdx++;
            if (arc.arcIdx >= arc.numArcs) {
                arc.flags |= BIT_LAST_ARC;
                return;
            }
            int64_t arcPos = arc.posArcsStart + static_cast<int64_t>(arc.arcIdx) * arc.bytesPerArc;
            arc.flags = readByte(arcPos);
            arcPos++;
            if (arc.nodeFlags == ARCS_FOR_CONTINUOUS) {
                arc.label = arc.firstLabel + arc.arcIdx;
            } else {
                arc.label = readLabel(arcPos);
            }
            readArcFields(arc, arcPos);

            // Mark as last if this is the final arc
            if (arc.arcIdx == arc.numArcs - 1) {
                arc.flags |= BIT_LAST_ARC;
            }
        }
    } else {
        // Variable-length linear list: read from nextArc position
        int64_t pos = arc.nextArc;
        arc.flags = readByte(pos);
        pos++;
        arc.label = readLabel(pos);
        readArcFields(arc, pos);
        arc.nextArc = pos;
    }
}

void LuceneFST::readArcFields(Arc& arc, int64_t& pos) const {
    // Read output
    if (arc.flags & BIT_ARC_HAS_OUTPUT) {
        arc.output = readOutput(pos);
    } else {
        arc.output.clear();
    }

    // Read final output
    if (arc.flags & BIT_ARC_HAS_FINAL_OUTPUT) {
        arc.nextFinalOutput = readFinalOutput(pos);
    } else {
        arc.nextFinalOutput.clear();
    }

    // Read target
    if (arc.flags & BIT_STOP_NODE) {
        if (arc.flags & BIT_FINAL_ARC) {
            arc.target = FINAL_END_NODE;
        } else {
            arc.target = NON_FINAL_END_NODE;
        }
    } else if (arc.flags & BIT_TARGET_NEXT) {
        // Target is the next node in the byte array.
        // For fixed-length arcs, it's after the last arc.
        // For variable-length arcs, it's after this arc's data (pos).
        if (arc.nodeFlags == ARCS_FOR_BINARY_SEARCH || arc.nodeFlags == ARCS_FOR_DIRECT_ADDRESSING
            || arc.nodeFlags == ARCS_FOR_CONTINUOUS) {
            // For fixed-length arc arrays, target is after the entire arc array
            // Calculate based on total arcs * bytesPerArc
            int totalArcs = arc.numArcs;
            if (arc.nodeFlags == ARCS_FOR_DIRECT_ADDRESSING) {
                totalArcs = countBitsUpTo(arc.numArcs, arc.bitTableStart);
            }
            arc.target = arc.posArcsStart + static_cast<int64_t>(totalArcs) * arc.bytesPerArc;
        } else {
            arc.target = pos;
        }
    } else {
        // Normal case: target encoded as VLong
        arc.target = readVLong(pos);
    }
}

// ==================== Bit Table Operations ====================

bool LuceneFST::isBitSet(int bitIndex, int64_t bitTableStart) const {
    int byteIdx = bitIndex >> 3;
    int bitInByte = bitIndex & 7;
    return (readByte(bitTableStart + byteIdx) & (1 << bitInByte)) != 0;
}

int LuceneFST::countBitsUpTo(int bitIndex, int64_t bitTableStart) const {
    int count = 0;
    int fullBytes = bitIndex >> 3;
    for (int i = 0; i < fullBytes; ++i) {
        count += __builtin_popcount(readByte(bitTableStart + i));
    }
    int remainingBits = bitIndex & 7;
    if (remainingBits > 0) {
        uint8_t lastByte = readByte(bitTableStart + fullBytes);
        uint8_t mask = (1u << remainingBits) - 1;
        count += __builtin_popcount(lastByte & mask);
    }
    return count;
}

// ==================== Static Helpers ====================

int64_t LuceneFST::readMSBVLong(const uint8_t* data, size_t& pos) {
    int64_t l = 0;
    while (true) {
        uint8_t b = data[pos++];
        l = (l << 7) | (b & 0x7FL);
        if ((b & 0x80) == 0) {
            break;
        }
    }
    return l;
}

}  // namespace lucene90
}  // namespace codecs
}  // namespace diagon
