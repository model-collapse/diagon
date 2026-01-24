// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>

namespace diagon {
namespace index {

// Forward declaration
class LeafReader;

/**
 * Context for a LeafReader within a composite index.
 *
 * Provides the document ID base offset for mapping local doc IDs
 * to global doc IDs.
 *
 * Based on: org.apache.lucene.index.LeafReaderContext
 */
struct LeafReaderContext {
    /**
     * The reader for this leaf
     */
    LeafReader* reader;

    /**
     * The doc base for this leaf.
     * Documents in this leaf have doc IDs in range [docBase, docBase + maxDoc)
     */
    int docBase;

    /**
     * Ordinal of this leaf within the parent composite reader.
     */
    int ord;

    /**
     * Constructor
     */
    LeafReaderContext(LeafReader* r, int base = 0, int ordinal = 0)
        : reader(r)
        , docBase(base)
        , ord(ordinal) {}
};

}  // namespace index
}  // namespace diagon
