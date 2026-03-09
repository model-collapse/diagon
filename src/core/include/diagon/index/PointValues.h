// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>

namespace diagon {
namespace index {

/**
 * PointValues - Interface for accessing BKD tree point values
 *
 * Based on: org.apache.lucene.index.PointValues
 *
 * Provides O(log N) range queries via BKD tree traversal.
 * The IntersectVisitor pattern allows the tree to prune entire
 * subtrees that don't match the query, achieving sub-linear performance.
 */
class PointValues {
public:
    virtual ~PointValues() = default;

    /**
     * Relationship between a cell's bounds and the query range.
     * Used by IntersectVisitor::compare() to guide tree traversal.
     */
    enum class Relation {
        CELL_INSIDE_QUERY,   // Cell is fully inside query -> collect all points
        CELL_OUTSIDE_QUERY,  // Cell is fully outside query -> prune subtree
        CELL_CROSSES_QUERY   // Cell partially overlaps -> recurse deeper
    };

    /**
     * Visitor for BKD tree intersection.
     *
     * Based on: org.apache.lucene.index.PointValues.IntersectVisitor
     */
    struct IntersectVisitor {
        virtual ~IntersectVisitor() = default;

        /**
         * Called when a cell is fully inside the query range.
         * All points in the cell match — no value check needed.
         */
        virtual void visit(int docID) = 0;

        /**
         * Called at leaf level when the cell crosses the query range.
         * The visitor must check packedValue against query bounds.
         */
        virtual void visit(int docID, const uint8_t* packedValue) = 0;

        /**
         * Compare cell bounds against query range.
         * @param minPackedValue minimum packed value in the cell
         * @param maxPackedValue maximum packed value in the cell
         * @return Relation indicating how to proceed
         */
        virtual Relation compare(const uint8_t* minPackedValue, const uint8_t* maxPackedValue) = 0;
    };

    /**
     * Intersect this point tree with the given visitor.
     * The visitor's compare() method guides pruning.
     */
    virtual void intersect(IntersectVisitor& visitor) const = 0;

    /** Number of data dimensions */
    virtual int getNumDimensions() const = 0;

    /** Number of index dimensions */
    virtual int getNumIndexDimensions() const = 0;

    /** Bytes per dimension */
    virtual int getBytesPerDimension() const = 0;

    /** Total number of indexed points */
    virtual int size() const = 0;

    /** Total number of documents with at least one point */
    virtual int getDocCount() const = 0;

    /** Minimum packed value across all points */
    virtual const uint8_t* getMinPackedValue() const = 0;

    /** Maximum packed value across all points */
    virtual const uint8_t* getMaxPackedValue() const = 0;
};

}  // namespace index
}  // namespace diagon
