// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/store/IndexInput.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace diagon {
namespace codecs {
namespace lucene90 {

/**
 * Read-only Lucene FST reader for .tip (term index) files.
 *
 * Reads Lucene 9.x FST format with ByteSequenceOutputs (variable-length byte arrays).
 * Used by Lucene90BlockTreeTermsReader to navigate term blocks via prefix lookups.
 *
 * Key format differences from Diagon's native FST:
 * - Reverse byte addressing (root node at end of byte array)
 * - ByteSequenceOutputs (byte[], not int64_t)
 * - 4 node types: linear, binary-search, direct-addressing, continuous
 * - MSB VLong for block FPs (version >= 1)
 *
 * Based on: org.apache.lucene.util.fst.FST (Lucene 9.x backward-codecs)
 */
class LuceneFST {
public:
    // Arc flag bits (from FST.java)
    static constexpr uint8_t BIT_FINAL_ARC = 1 << 0;
    static constexpr uint8_t BIT_LAST_ARC = 1 << 1;
    static constexpr uint8_t BIT_TARGET_NEXT = 1 << 2;
    static constexpr uint8_t BIT_STOP_NODE = 1 << 3;
    static constexpr uint8_t BIT_ARC_HAS_OUTPUT = 1 << 4;
    static constexpr uint8_t BIT_ARC_HAS_FINAL_OUTPUT = 1 << 5;

    // Node type flags (stored as first byte of a node)
    static constexpr uint8_t ARCS_FOR_BINARY_SEARCH = 0x20;
    static constexpr uint8_t ARCS_FOR_DIRECT_ADDRESSING = 0x40;
    static constexpr uint8_t ARCS_FOR_CONTINUOUS = 0x60;

    // Special target addresses
    static constexpr int64_t FINAL_END_NODE = -1;
    static constexpr int64_t NON_FINAL_END_NODE = 0;

    // End-of-string label
    static constexpr int END_LABEL = -1;

    /**
     * Arc represents a single transition in the FST.
     */
    struct Arc {
        int label = 0;
        uint8_t flags = 0;
        uint8_t nodeFlags = 0;
        int64_t target = 0;
        std::vector<uint8_t> output;
        std::vector<uint8_t> nextFinalOutput;
        int64_t nextArc = 0;  // For linear scan: position of next sibling

        // Fixed-length arc fields (binary search / direct addressing / continuous)
        int bytesPerArc = 0;
        int64_t posArcsStart = 0;
        int arcIdx = 0;
        int numArcs = 0;
        int firstLabel = 0;
        int64_t bitTableStart = 0;

        bool isFinal() const { return (flags & BIT_FINAL_ARC) != 0; }
        bool isLast() const { return (flags & BIT_LAST_ARC) != 0; }
    };

    /**
     * Construct FST from metadata DataInput and an IndexInput for the byte array.
     *
     * @param metaIn DataInput positioned at FST metadata (from .tmd per-field)
     * @param fstIn IndexInput to read FST bytes from (e.g., .tip slice)
     * @param indexStartFP Start offset in fstIn where this field's FST begins
     */
    LuceneFST(store::IndexInput& metaIn, store::IndexInput& fstIn, int64_t indexStartFP);

    /**
     * Construct FST from pre-loaded byte array (for testing).
     */
    LuceneFST(int64_t startNode, int version, std::vector<uint8_t> bytes,
              std::vector<uint8_t> emptyOutput = {});

    /**
     * Initialize virtual root arc pointing to the FST's start node.
     */
    void getFirstArc(Arc& arc) const;

    /**
     * Find an arc with the given label from follow's target node.
     *
     * @param labelToMatch The byte label to search for
     * @param follow Arc whose target is the node to search
     * @param arc Output arc (populated if found)
     * @return true if found, false otherwise
     */
    bool findTargetArc(int labelToMatch, const Arc& follow, Arc& arc) const;

    /** Check if an arc's target has outgoing arcs. */
    static bool targetHasArcs(const Arc& arc) { return arc.target > 0; }

    /** Get the empty-string output, if any. */
    const std::vector<uint8_t>& getEmptyOutput() const { return emptyOutput_; }
    bool hasEmptyOutput() const { return !emptyOutput_.empty(); }

    /** Get FST version. */
    int version() const { return version_; }

    /**
     * Read MSB VLong encoding (used for block FPs in version >= 1).
     * MSB continuation: high bit set means more bytes follow.
     * Bits shifted left (MSB first, unlike standard VLong which is LSB first).
     */
    static int64_t readMSBVLong(const uint8_t* data, size_t& pos);

private:
    // FST metadata
    int version_ = 0;
    int64_t startNode_ = 0;
    int64_t numBytes_ = 0;
    std::vector<uint8_t> emptyOutput_;
    int inputType_ = 0;  // 0=BYTE1, 1=BYTE2, 2=BYTE4

    // FST byte array (loaded into memory)
    std::vector<uint8_t> bytes_;

    // Internal reading helpers (all positions are within bytes_)

    /** Read a single byte at the given position (reverse-addressed). */
    uint8_t readByte(int64_t pos) const;

    /** Read a label at the given position, advancing pos. */
    int readLabel(int64_t& pos) const;

    /** Read a VInt at the given position, advancing pos. */
    int32_t readVInt(int64_t& pos) const;

    /** Read a VLong at the given position, advancing pos. */
    int64_t readVLong(int64_t& pos) const;

    /** Read ByteSequenceOutputs at the given position, advancing pos. */
    std::vector<uint8_t> readOutput(int64_t& pos) const;

    /** Read final ByteSequenceOutputs at the given position, advancing pos. */
    std::vector<uint8_t> readFinalOutput(int64_t& pos) const;

    /** Read the first arc of a node at the given address. */
    void readFirstRealTargetArc(int64_t nodeAddr, Arc& arc) const;

    /** Read the next sibling arc (for linear scan nodes). */
    void readNextRealArc(Arc& arc) const;

    /** Read arc data at the current position within a fixed-length arc array. */
    void readArcByIndex(Arc& arc, int index) const;

    /** Read arc fields (output, finalOutput, target) from position pos. */
    void readArcFields(Arc& arc, int64_t& pos) const;

    /** Check if a bit is set in the direct-addressing bit table. */
    bool isBitSet(int bitIndex, int64_t bitTableStart) const;

    /** Count set bits up to (exclusive) the given index. */
    int countBitsUpTo(int bitIndex, int64_t bitTableStart) const;

    /** Number of presence bytes for a given label range. */
    static int getNumPresenceBytes(int labelRange) { return (labelRange + 7) >> 3; }
};

}  // namespace lucene90
}  // namespace codecs
}  // namespace diagon
