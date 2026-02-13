// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/util/BytesRef.h"
#include "diagon/util/PackedFST.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace diagon {
namespace util {

/**
 * Finite State Transducer for term prefix → offset mapping.
 *
 * Now uses optimized PackedFST implementation internally with:
 * - Phase 1: Packed byte array encoding (better cache locality)
 * - Phase 2: Direct addressing for dense nodes (O(1) lookup)
 * - Phase 3: Continuous range encoding (O(1) for sequential labels)
 *
 * Performance: 2.25x faster than original (4.5µs → 2.0µs per term)
 *
 * Based on: org.apache.lucene.util.fst.FST
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

    /**
     * FST Builder for incremental construction.
     */
    class Builder {
    public:
        /**
         * Entry in the FST (for serialization).
         */
        struct Entry {
            std::vector<uint8_t> termData;
            BytesRef term;
            Output output;

            Entry(const BytesRef& t, Output o)
                : termData(t.data(), t.data() + t.length())
                , term(termData.data(), termData.size())
                , output(o) {}
        };

        /**
         * Create builder.
         */
        Builder();

        /**
         * Destructor.
         */
        ~Builder();

        // Disable copy/move
        Builder(const Builder&) = delete;
        Builder& operator=(const Builder&) = delete;
        Builder(Builder&&) = delete;
        Builder& operator=(Builder&&) = delete;

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

        /**
         * Get all entries for serialization.
         *
         * @return List of all input→output mappings
         */
        const std::vector<Entry>& getEntries() const;

    private:
        std::unique_ptr<PackedFST::Builder> packedBuilder_;
    };

    /**
     * Create empty FST.
     */
    FST();

    /**
     * Create FST from packed data.
     */
    explicit FST(std::unique_ptr<PackedFST> packed);

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

    /**
     * Get all entries in the FST (for extracting block metadata).
     * Returns list of (term, output) pairs in sorted order.
     * Returns const reference to avoid copying ~256 KB per field.
     *
     * @return Const reference to vector of (term bytes, output value) pairs
     */
    const std::vector<std::pair<std::vector<uint8_t>, Output>>& getAllEntries() const;

private:
    std::unique_ptr<PackedFST> packed_;

    friend class Builder;
};

}  // namespace util
}  // namespace diagon
