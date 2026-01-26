// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/skipindex/IMergeTreeIndexGranule.h"
#include "diagon/index/skipindex/IMergeTreeIndexAggregator.h"
#include "diagon/index/skipindex/IMergeTreeIndexCondition.h"
#include "diagon/index/skipindex/IMergeTreeIndex.h"
#include "diagon/util/BloomFilter.h"

#include <vector>
#include <memory>
#include <string>
#include <unordered_set>

namespace diagon {

namespace store {
class IndexOutput;
class IndexInput;
}

namespace index {
namespace skipindex {

/**
 * BloomFilter index for probabilistic membership testing
 *
 * Use for: Equality checks (WHERE col = value)
 * Storage: bits_per_row * rows per granule (e.g., 8 bits/row * 8192 rows = 8KB)
 *
 * Based on: ClickHouse MergeTreeIndexBloomFilter
 */

// ==================== GRANULE ====================

class MergeTreeIndexGranuleBloomFilter : public IMergeTreeIndexGranule {
public:
    MergeTreeIndexGranuleBloomFilter(
        size_t bits_per_row,
        size_t hash_functions,
        size_t num_columns);

    // ==================== Serialization ====================

    /**
     * Serialize to output stream
     *
     * Format:
     * - total_rows (varint)
     * - For each column:
     *   - bloom filter bits (raw bytes)
     */
    void serialize(store::IndexOutput* output) const;

    /**
     * Deserialize from input stream
     */
    void deserialize(store::IndexInput* input, MergeTreeIndexVersion version);

    // ==================== Properties ====================

    bool empty() const override {
        return total_rows_ == 0;
    }

    size_t memoryUsageBytes() const override;

    const std::vector<util::BloomFilterPtr>& getFilters() const {
        return bloom_filters_;
    }

    size_t totalRows() const { return total_rows_; }
    size_t bitsPerRow() const { return bits_per_row_; }
    size_t hashFunctions() const { return hash_functions_; }

private:
    friend class MergeTreeIndexAggregatorBloomFilter;

    size_t bits_per_row_;
    size_t hash_functions_;
    size_t total_rows_{0};

    std::vector<util::BloomFilterPtr> bloom_filters_;  // One per column
};

// ==================== AGGREGATOR ====================

class MergeTreeIndexAggregatorBloomFilter : public IMergeTreeIndexAggregator {
public:
    MergeTreeIndexAggregatorBloomFilter(
        size_t bits_per_row,
        size_t hash_functions,
        const std::vector<std::string>& column_names);

    // ==================== State Management ====================

    bool empty() const override {
        return total_rows_ == 0;
    }

    MergeTreeIndexGranulePtr getGranuleAndReset() override;

    // ==================== Data Accumulation ====================

    /**
     * Accumulate hash values for rows
     *
     * @param column_hashes Vector of hash values per column per row
     *                      Format: column_hashes[column_idx][row_idx] = hash
     */
    void update(const std::vector<std::vector<uint64_t>>& column_hashes);

    /**
     * Add single row of hashes
     */
    void addRow(const std::vector<uint64_t>& row_hashes);

private:
    size_t bits_per_row_;
    size_t hash_functions_;
    std::vector<std::string> column_names_;

    // Accumulate unique hashes per column
    std::vector<std::unordered_set<uint64_t>> column_hashes_;
    size_t total_rows_{0};
};

// ==================== CONDITION ====================

/**
 * Query-time condition evaluation for bloom filter
 *
 * Supports:
 * - Equality: col = value
 * - IN clause: col IN (v1, v2, v3)
 * - AND/OR/NOT combinations
 */
class MergeTreeIndexConditionBloomFilter : public IMergeTreeIndexCondition {
public:
    MergeTreeIndexConditionBloomFilter(
        const std::vector<std::string>& index_columns,
        size_t hash_functions);

    // ==================== Query Analysis ====================

    bool alwaysUnknownOrTrue() const override {
        return predicates_.empty();
    }

    // ==================== Granule Filtering ====================

    bool mayBeTrueOnGranule(MergeTreeIndexGranulePtr granule) const override;

    std::string getDescription() const override {
        return "bloom_filter index condition";
    }

    // ==================== Predicate Building ====================

    /**
     * Add equality predicate: col = value
     *
     * @param column_name Column name
     * @param value_hash Hash of the value to search for
     */
    void addEqualsPredicate(const std::string& column_name, uint64_t value_hash);

    /**
     * Add IN predicate: col IN (values)
     *
     * @param column_name Column name
     * @param value_hashes Hashes of values to search for
     */
    void addInPredicate(const std::string& column_name,
                       const std::vector<uint64_t>& value_hashes);

private:
    enum class PredicateType {
        EQUALS,
        IN
    };

    struct Predicate {
        PredicateType type;
        size_t column_idx;
        std::vector<uint64_t> value_hashes;
    };

    std::vector<std::string> index_columns_;
    size_t hash_functions_;
    std::vector<Predicate> predicates_;

    /**
     * Find column index by name
     */
    int findColumnIndex(const std::string& column_name) const;

    /**
     * Check if predicate matches granule
     */
    bool checkPredicate(const Predicate& pred,
                       const MergeTreeIndexGranuleBloomFilter* granule) const;
};

// ==================== INDEX ====================

class MergeTreeIndexBloomFilter : public IMergeTreeIndex {
public:
    MergeTreeIndexBloomFilter(
        const std::string& index_name,
        const std::vector<std::string>& columns,
        size_t granularity,
        size_t bits_per_row = 8,
        size_t hash_functions = 3);

    // ==================== Factory Methods ====================

    MergeTreeIndexGranulePtr createIndexGranule() const override;

    MergeTreeIndexAggregatorPtr createIndexAggregator() const override;

    MergeTreeIndexConditionPtr createIndexCondition() const override;

    // ==================== File Naming ====================

    std::string getFileExtension() const override {
        return ".idx";
    }

    // ==================== Parameters ====================

    size_t bitsPerRow() const { return bits_per_row_; }
    size_t hashFunctions() const { return hash_functions_; }
    const std::vector<std::string>& columns() const { return columns_; }

private:
    std::string index_name_;
    std::vector<std::string> columns_;
    size_t granularity_;
    size_t bits_per_row_;
    size_t hash_functions_;
};

}  // namespace skipindex
}  // namespace index
}  // namespace diagon
