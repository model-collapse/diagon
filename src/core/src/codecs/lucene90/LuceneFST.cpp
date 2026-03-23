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
            // Read the raw bytes of the empty output.
            // ByteSequenceOutputs.readFinalOutput uses a reverse reader internally:
            // it sets position to numBytes-1 and reads backward.
            // For BlockTree, the empty output is the root block code (MSB VLong).
            // We need to reverse-deserialize it here.
            std::vector<uint8_t> rawBytes(numBytes);
            metaIn.readBytes(rawBytes.data(), numBytes);

            // Reverse-read: starts at numBytes-1, reads VInt(len) + bytes
            // This is how ByteSequenceOutputs.readFinalOutput works
            int rPos = numBytes - 1;
            // Read VInt from reverse position
            int len = 0;
            int shift = 0;
            while (true) {
                uint8_t b = rawBytes[rPos--];
                len |= (b & 0x7F) << shift;
                if ((b & 0x80) == 0) break;
                shift += 7;
            }
            if (len > 0 && len <= rPos + 1) {
                emptyOutput_.resize(len);
                for (int i = 0; i < len; ++i) {
                    emptyOutput_[i] = rawBytes[rPos--];
                }
            } else if (len == 0) {
                emptyOutput_.clear();
            } else {
                // Fallback: store raw bytes (for safety)
                emptyOutput_ = std::move(rawBytes);
            }
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

    // Read the node header at follow's target (reverse reader: pos--)
    int64_t pos = follow.target;
    uint8_t nodeFlags = readByte(pos);
    pos--;  // Reverse reader: advance = decrement

    if (nodeFlags == ARCS_FOR_DIRECT_ADDRESSING) {
        // Direct-addressing node: numArcs + bytesPerArc + bit table + firstLabel + arcs
        int numArcs = readVInt(pos);
        int bytesPerArc = readVInt(pos);

        // Bit table for presence
        int numPresenceBytes = getNumPresenceBytes(numArcs);
        int64_t bitTableStart = pos;  // First bit table byte is at pos
        pos -= numPresenceBytes;      // Skip past bit table (reverse)

        // First label
        int firstLabel = readLabel(pos);
        int64_t posArcsStart = pos;

        int arcIndex = labelToMatch - firstLabel;
        if (arcIndex < 0 || arcIndex >= numArcs) {
            return false;
        }

        if (!isBitSet(arcIndex, bitTableStart)) {
            return false;
        }

        int presenceIndex = countBitsUpTo(arcIndex, bitTableStart);

        arc = Arc{};
        arc.nodeFlags = nodeFlags;
        arc.bytesPerArc = bytesPerArc;
        arc.posArcsStart = posArcsStart;
        arc.numArcs = numArcs;
        arc.firstLabel = firstLabel;
        arc.bitTableStart = bitTableStart;
        arc.arcIdx = presenceIndex;
        arc.label = labelToMatch;

        // Arc at presenceIndex: SUBTRACT from posArcsStart
        int64_t arcPos = posArcsStart - static_cast<int64_t>(presenceIndex) * bytesPerArc;
        arc.flags = readByte(arcPos);
        arcPos--;
        // No label byte in direct-addressing (label is inferred from bit table)
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

        // SUBTRACT from posArcsStart
        int64_t arcPos = posArcsStart - static_cast<int64_t>(rangeIndex) * bytesPerArc;
        arc.flags = readByte(arcPos);
        arcPos--;
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
            // To read label, skip past flags byte: subtract (mid * bytesPerArc + 1)
            int64_t labelPos = posArcsStart - (static_cast<int64_t>(mid) * bytesPerArc + 1);
            int midLabel = readLabel(labelPos);

            if (midLabel < labelToMatch) {
                low = mid + 1;
            } else if (midLabel > labelToMatch) {
                high = mid - 1;
            } else {
                // Found it — read the full arc
                arc = Arc{};
                arc.nodeFlags = nodeFlags;
                arc.bytesPerArc = bytesPerArc;
                arc.posArcsStart = posArcsStart;
                arc.numArcs = numArcs;
                arc.arcIdx = mid;
                arc.label = midLabel;

                int64_t arcPos = posArcsStart - static_cast<int64_t>(mid) * bytesPerArc;
                arc.flags = readByte(arcPos);
                arcPos--;
                readLabel(arcPos);  // Skip label (already have it)
                readArcFields(arc, arcPos);
                return true;
            }
        }
        return false;

    } else {
        // Variable-length linear list
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

// ==================== Internal Byte Reading (Reverse Reader) ====================
//
// Lucene's FST uses a ReverseBytesReader: readByte() returns bytes[pos] then pos--.
// FSTCompiler reverses node bytes after writing, so reading backward recovers the
// original arc order. All position advancement is via DECREMENT.

uint8_t LuceneFST::readByte(int64_t pos) const {
    if (pos < 0 || pos >= static_cast<int64_t>(bytes_.size())) {
        throw std::runtime_error("FST: read out of bounds at position " + std::to_string(pos)
                                 + " (numBytes=" + std::to_string(numBytes_) + ")");
    }
    return bytes_[pos];
}

int LuceneFST::readLabel(int64_t& pos) const {
    if (inputType_ == 0) {
        // BYTE1: unsigned byte
        return readByte(pos--) & 0xFF;
    } else if (inputType_ == 1) {
        // BYTE2: reverse reader reads big-endian (same as DataInput.readShort)
        // ReverseBytesReader.readByte() at pos, then pos--, so first byte is high
        uint8_t b0 = readByte(pos--);
        uint8_t b1 = readByte(pos--);
        return ((b0 & 0xFF) << 8) | (b1 & 0xFF);
    } else {
        // BYTE4: VInt
        return readVInt(pos);
    }
}

int32_t LuceneFST::readVInt(int64_t& pos) const {
    uint8_t b = readByte(pos--);
    if ((b & 0x80) == 0) return b;
    int32_t i = b & 0x7F;
    b = readByte(pos--);
    i |= (b & 0x7F) << 7;
    if ((b & 0x80) == 0) return i;
    b = readByte(pos--);
    i |= (b & 0x7F) << 14;
    if ((b & 0x80) == 0) return i;
    b = readByte(pos--);
    i |= (b & 0x7F) << 21;
    if ((b & 0x80) == 0) return i;
    b = readByte(pos--);
    i |= (b & 0x0F) << 28;
    return i;
}

int64_t LuceneFST::readVLong(int64_t& pos) const {
    uint8_t b = readByte(pos--);
    if ((b & 0x80) == 0) return b;
    int64_t i = b & 0x7FL;
    b = readByte(pos--);
    i |= (static_cast<int64_t>(b) & 0x7FL) << 7;
    if ((b & 0x80) == 0) return i;
    b = readByte(pos--);
    i |= (static_cast<int64_t>(b) & 0x7FL) << 14;
    if ((b & 0x80) == 0) return i;
    b = readByte(pos--);
    i |= (static_cast<int64_t>(b) & 0x7FL) << 21;
    if ((b & 0x80) == 0) return i;
    b = readByte(pos--);
    i |= (static_cast<int64_t>(b) & 0x7FL) << 28;
    if ((b & 0x80) == 0) return i;
    b = readByte(pos--);
    i |= (static_cast<int64_t>(b) & 0x7FL) << 35;
    if ((b & 0x80) == 0) return i;
    b = readByte(pos--);
    i |= (static_cast<int64_t>(b) & 0x7FL) << 42;
    if ((b & 0x80) == 0) return i;
    b = readByte(pos--);
    i |= (static_cast<int64_t>(b) & 0x7FL) << 49;
    if ((b & 0x80) == 0) return i;
    b = readByte(pos--);
    i |= (static_cast<int64_t>(b) & 0x01L) << 56;
    return i;
}

std::vector<uint8_t> LuceneFST::readOutput(int64_t& pos) const {
    // ByteSequenceOutputs.read(): VInt(length) + bytes[length]
    int len = readVInt(pos);
    if (len == 0) return {};
    std::vector<uint8_t> result(len);
    for (int i = 0; i < len; ++i) {
        result[i] = readByte(pos--);
    }
    return result;
}

std::vector<uint8_t> LuceneFST::readFinalOutput(int64_t& pos) const {
    return readOutput(pos);
}

// ==================== Arc Reading (Reverse) ====================

void LuceneFST::readFirstRealTargetArc(int64_t nodeAddr, Arc& arc) const {
    int64_t pos = nodeAddr;
    uint8_t nodeFlags = readByte(pos);
    pos--;

    arc = Arc{};
    arc.nodeFlags = nodeFlags;

    if (nodeFlags == ARCS_FOR_BINARY_SEARCH || nodeFlags == ARCS_FOR_DIRECT_ADDRESSING
        || nodeFlags == ARCS_FOR_CONTINUOUS) {
        // Fixed-length arc node: read header, then first arc
        arc.numArcs = readVInt(pos);
        arc.bytesPerArc = readVInt(pos);

        if (nodeFlags == ARCS_FOR_DIRECT_ADDRESSING) {
            arc.bitTableStart = pos;
            pos -= getNumPresenceBytes(arc.numArcs);
            arc.firstLabel = readLabel(pos);
            arc.posArcsStart = pos;
            arc.arcIdx = 0;
            // Find the first set bit
            for (int i = 0; i < arc.numArcs; ++i) {
                if (isBitSet(i, arc.bitTableStart)) {
                    int presenceIdx = countBitsUpTo(i, arc.bitTableStart);
                    int64_t arcPos = arc.posArcsStart
                                     - static_cast<int64_t>(presenceIdx) * arc.bytesPerArc;
                    arc.flags = readByte(arcPos);
                    arcPos--;
                    arc.label = arc.firstLabel + i;
                    arc.arcIdx = presenceIdx;
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
            arcPos--;
            arc.label = arc.firstLabel;
            readArcFields(arc, arcPos);
        } else {
            // Binary search: first arc at index 0
            arc.posArcsStart = pos;
            arc.arcIdx = 0;
            int64_t arcPos = arc.posArcsStart;
            arc.flags = readByte(arcPos);
            arcPos--;
            arc.label = readLabel(arcPos);
            readArcFields(arc, arcPos);
        }
    } else {
        // Variable-length linear list: first byte IS the arc flags
        arc.flags = nodeFlags;
        arc.label = readLabel(pos);
        readArcFields(arc, pos);
        arc.nextArc = pos;  // Position of next sibling (lower address)
    }
}

void LuceneFST::readNextRealArc(Arc& arc) const {
    if (arc.nodeFlags == ARCS_FOR_BINARY_SEARCH || arc.nodeFlags == ARCS_FOR_DIRECT_ADDRESSING
        || arc.nodeFlags == ARCS_FOR_CONTINUOUS) {
        if (arc.nodeFlags == ARCS_FOR_DIRECT_ADDRESSING) {
            int currentBitIdx = arc.label - arc.firstLabel;
            for (int i = currentBitIdx + 1; i < arc.numArcs; ++i) {
                if (isBitSet(i, arc.bitTableStart)) {
                    int presenceIdx = countBitsUpTo(i, arc.bitTableStart);
                    int64_t arcPos = arc.posArcsStart
                                     - static_cast<int64_t>(presenceIdx) * arc.bytesPerArc;
                    arc.flags = readByte(arcPos);
                    arcPos--;
                    arc.label = arc.firstLabel + i;
                    arc.arcIdx = presenceIdx;
                    readArcFields(arc, arcPos);
                    return;
                }
            }
            arc.flags |= BIT_LAST_ARC;
        } else {
            arc.arcIdx++;
            if (arc.arcIdx >= arc.numArcs) {
                arc.flags |= BIT_LAST_ARC;
                return;
            }
            // SUBTRACT for reverse addressing
            int64_t arcPos = arc.posArcsStart - static_cast<int64_t>(arc.arcIdx) * arc.bytesPerArc;
            arc.flags = readByte(arcPos);
            arcPos--;
            if (arc.nodeFlags == ARCS_FOR_CONTINUOUS) {
                arc.label = arc.firstLabel + arc.arcIdx;
            } else {
                arc.label = readLabel(arcPos);
            }
            readArcFields(arc, arcPos);

            if (arc.arcIdx == arc.numArcs - 1) {
                arc.flags |= BIT_LAST_ARC;
            }
        }
    } else {
        // Variable-length linear list
        int64_t pos = arc.nextArc;
        arc.flags = readByte(pos);
        pos--;
        arc.label = readLabel(pos);
        readArcFields(arc, pos);
        arc.nextArc = pos;
    }
}

void LuceneFST::readArcByIndex(Arc& arc, int index) const {
    // Not used in current implementation — placeholder for future use
    (void)arc;
    (void)index;
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
        // Target is the next node (compiled before this one, at lower address).
        // For variable-length arcs, pos is already at the right place.
        // For fixed-length arcs, it's after the entire arc array.
        if (arc.nodeFlags == ARCS_FOR_BINARY_SEARCH || arc.nodeFlags == ARCS_FOR_DIRECT_ADDRESSING
            || arc.nodeFlags == ARCS_FOR_CONTINUOUS) {
            int totalArcs = arc.numArcs;
            if (arc.nodeFlags == ARCS_FOR_DIRECT_ADDRESSING) {
                totalArcs = countBitsUpTo(arc.numArcs, arc.bitTableStart);
            }
            // SUBTRACT: next node is at lower address
            arc.target = arc.posArcsStart - static_cast<int64_t>(totalArcs) * arc.bytesPerArc;
        } else {
            arc.target = pos;
        }
    } else {
        // Normal case: target encoded as VLong
        arc.target = readVLong(pos);
    }
}

// ==================== Bit Table Operations (Reverse) ====================
// Bit table bytes were written forward, then reversed with the node.
// Reading backward from bitTableStart recovers the original byte order.

bool LuceneFST::isBitSet(int bitIndex, int64_t bitTableStart) const {
    int byteIdx = bitIndex >> 3;
    int bitInByte = bitIndex & 7;
    // SUBTRACT: reverse addressing
    return (readByte(bitTableStart - byteIdx) & (1 << bitInByte)) != 0;
}

int LuceneFST::countBitsUpTo(int bitIndex, int64_t bitTableStart) const {
    int count = 0;
    int fullBytes = bitIndex >> 3;
    for (int i = 0; i < fullBytes; ++i) {
        count += __builtin_popcount(readByte(bitTableStart - i));
    }
    int remainingBits = bitIndex & 7;
    if (remainingBits > 0) {
        uint8_t lastByte = readByte(bitTableStart - fullBytes);
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
