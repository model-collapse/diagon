// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <memory>

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
 * Lifetime: The shared_ptr<LeafReader> keeps the segment reader alive
 * for as long as any context referencing it exists.
 *
 * Based on: org.apache.lucene.index.LeafReaderContext
 */
struct LeafReaderContext {
    /**
     * The reader for this leaf (shared ownership).
     * Callers may use reader->method() directly; the shared_ptr keeps
     * the LeafReader alive for the lifetime of this context.
     */
    std::shared_ptr<LeafReader> reader;

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
    LeafReaderContext(std::shared_ptr<LeafReader> r, int base = 0, int ordinal = 0)
        : reader(std::move(r))
        , docBase(base)
        , ord(ordinal) {}
};

}  // namespace index
}  // namespace diagon
