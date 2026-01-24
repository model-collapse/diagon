// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/util/BytesRef.h"

#include <vector>
#include <memory>
#include <cstdint>

namespace diagon {
namespace util {

/**
 * Simple Finite State Transducer for term prefix → offset mapping.
 *
 * Based on: org.apache.lucene.util.fst.FST
 *
 * This is a simplified implementation for Phase 2 MVP:
 * - Supports byte sequences as inputs
 * - Maps to int64_t outputs (file pointers)
 * - Minimal memory footprint
 *
 * Full Lucene FST has many optimizations we'll add later:
 * - Packed arrays for transitions
 * - Output prefix sharing
 * - Direct addressing for dense nodes
 */
class FST {
public:
    /**
     * Output value type (file pointer to term block).
     */
    using Output = int64_t;

    /**
     * No output constant (term not found).
     */
    static constexpr Output NO_OUTPUT = -1;

    // Forward declaration for Arc (Node needs it)
    struct Arc;

    /**
     * FST Node (state in the automaton).
     */
    struct Node {
        /**
         * Outgoing arcs (transitions).
         */
        std::vector<Arc> arcs;

        /**
         * Output for this node (if final).
         */
        Output output = NO_OUTPUT;

        /**
         * Whether this is a final (accepting) state.
         */
        bool isFinal = false;

        /**
         * Find arc with given label.
         */
        const Arc* findArc(uint8_t label) const;
    };

    /**
     * FST Arc (transition between states).
     */
    struct Arc {
        /**
         * Input label (byte value).
         */
        uint8_t label;

        /**
         * Target node.
         */
        Node* target;

        /**
         * Output accumulated on this arc.
         */
        Output output;

        /**
         * Constructor.
         */
        Arc(uint8_t l, Node* t, Output o = 0)
            : label(l), target(t), output(o) {}

        /**
         * Comparison for sorting arcs by label.
         */
        bool operator<(const Arc& other) const {
            return label < other.label;
        }
    };

    /**
     * FST Builder for incremental construction.
     */
    class Builder {
    public:
        /**
         * Create builder.
         */
        Builder();

        /**
         * Add input → output mapping.
         * Inputs must be added in sorted order.
         *
         * @param input Input byte sequence
         * @param output Output value (file pointer)
         */
        void add(const BytesRef& input, Output output);

        /**
         * Finish building and return FST.
         *
         * @return Completed FST
         */
        std::unique_ptr<FST> finish();

    private:
        Node* root_;  // Use raw pointer, will be moved to FST
        BytesRef lastInput_;
        std::vector<uint8_t> lastInputData_;  // Storage for lastInput_
        bool finished_;

        Node* addNode();
    };

    /**
     * Create empty FST.
     */
    FST();

    /**
     * Lookup input in FST.
     *
     * @param input Input byte sequence
     * @return Output value or NO_OUTPUT if not found
     */
    Output get(const BytesRef& input) const;

    /**
     * Find longest prefix match.
     *
     * @param input Input byte sequence
     * @param prefixLen Output: length of matching prefix
     * @return Output value for longest matching prefix, or NO_OUTPUT
     */
    Output getLongestPrefixMatch(const BytesRef& input, int& prefixLen) const;

    /**
     * Serialize FST to bytes.
     *
     * @return Serialized FST
     */
    std::vector<uint8_t> serialize() const;

    /**
     * Deserialize FST from bytes.
     *
     * @param data Serialized FST data
     * @return Deserialized FST
     */
    static std::unique_ptr<FST> deserialize(const std::vector<uint8_t>& data);

    std::unique_ptr<Node> root_;

    friend class Builder;
};

}  // namespace util
}  // namespace diagon
