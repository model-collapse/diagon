// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/skipindex/MergeTreeIndexBloomFilter.h"

#include "diagon/store/IndexInput.h"
#include "diagon/store/IndexOutput.h"

#include <algorithm>
#include <stdexcept>

namespace diagon {
namespace index {
namespace skipindex {

// ==================== GRANULE ====================

MergeTreeIndexGranuleBloomFilter::MergeTreeIndexGranuleBloomFilter(size_t bits_per_row,
                                                                   size_t hash_functions,
                                                                   size_t num_columns)
    : bits_per_row_(bits_per_row)
    , hash_functions_(hash_functions) {
    bloom_filters_.reserve(num_columns);
    for (size_t i = 0; i < num_columns; ++i) {
        bloom_filters_.push_back(nullptr);  // Will be created when data is available
    }
}

void MergeTreeIndexGranuleBloomFilter::serialize(store::IndexOutput* output) const {
    // Write total rows
    output->writeVLong(total_rows_);

    if (total_rows_ > 0) {
        // Write each bloom filter
        for (const auto& filter : bloom_filters_) {
            if (!filter) {
                throw diagon::IOException("Cannot serialize null bloom filter");
            }

            // Write filter size and data
            size_t filter_bytes = filter->sizeBytes();
            output->writeVLong(filter_bytes);

            // Write filter data
            const auto& data = filter->data();
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
            output->writeBytes(bytes, filter_bytes);
        }
    }
}

void MergeTreeIndexGranuleBloomFilter::deserialize(store::IndexInput* input,
                                                   MergeTreeIndexVersion version) {
    (void)version;  // Version not used yet

    // Read total rows
    total_rows_ = input->readVLong();

    if (total_rows_ > 0) {
        // Read each bloom filter
        for (size_t i = 0; i < bloom_filters_.size(); ++i) {
            // Read filter size
            size_t filter_bytes = input->readVLong();

            // Create bloom filter
            auto filter = std::make_shared<util::BloomFilter>(filter_bytes, hash_functions_, 0);

            // Read filter data
            auto& data = filter->data();
            uint8_t* bytes = reinterpret_cast<uint8_t*>(data.data());
            input->readBytes(bytes, filter_bytes);

            bloom_filters_[i] = std::move(filter);
        }
    }
}

size_t MergeTreeIndexGranuleBloomFilter::memoryUsageBytes() const {
    if (total_rows_ == 0) {
        return 0;
    }

    size_t total = 0;
    for (const auto& filter : bloom_filters_) {
        if (filter) {
            total += filter->memoryUsageBytes();
        }
    }
    return total;
}

// ==================== AGGREGATOR ====================

MergeTreeIndexAggregatorBloomFilter::MergeTreeIndexAggregatorBloomFilter(
    size_t bits_per_row, size_t hash_functions, const std::vector<std::string>& column_names)
    : bits_per_row_(bits_per_row)
    , hash_functions_(hash_functions)
    , column_names_(column_names) {
    column_hashes_.resize(column_names.size());
}

MergeTreeIndexGranulePtr MergeTreeIndexAggregatorBloomFilter::getGranuleAndReset() {
    auto granule = std::make_shared<MergeTreeIndexGranuleBloomFilter>(
        bits_per_row_, hash_functions_, column_names_.size());

    // Build bloom filters from accumulated hashes
    granule->total_rows_ = total_rows_;

    if (total_rows_ > 0) {
        for (size_t col = 0; col < column_hashes_.size(); ++col) {
            // Calculate filter size based on bits per row and total rows
            size_t filter_bits = bits_per_row_ * total_rows_;
            size_t filter_bytes = (filter_bits + 7) / 8;  // Round up to bytes

            // Create bloom filter
            auto filter = std::make_shared<util::BloomFilter>(filter_bytes, hash_functions_, 0);

            // Add all accumulated hashes
            for (uint64_t hash : column_hashes_[col]) {
                filter->addHash(hash);
            }

            granule->bloom_filters_[col] = std::move(filter);
        }
    }

    // Reset state for next granule
    for (auto& hashes : column_hashes_) {
        hashes.clear();
    }
    total_rows_ = 0;

    return granule;
}

void MergeTreeIndexAggregatorBloomFilter::update(
    const std::vector<std::vector<uint64_t>>& column_hashes) {
    if (column_hashes.size() != column_names_.size()) {
        throw std::invalid_argument("Column hash count mismatch: expected " +
                                    std::to_string(column_names_.size()) + " got " +
                                    std::to_string(column_hashes.size()));
    }

    // Accumulate hashes for each column
    size_t num_rows = column_hashes[0].size();
    for (size_t row = 0; row < num_rows; ++row) {
        for (size_t col = 0; col < column_names_.size(); ++col) {
            column_hashes_[col].insert(column_hashes[col][row]);
        }
        ++total_rows_;
    }
}

void MergeTreeIndexAggregatorBloomFilter::addRow(const std::vector<uint64_t>& row_hashes) {
    if (row_hashes.size() != column_names_.size()) {
        throw std::invalid_argument("Row hash count mismatch: expected " +
                                    std::to_string(column_names_.size()) + " got " +
                                    std::to_string(row_hashes.size()));
    }

    for (size_t col = 0; col < column_names_.size(); ++col) {
        column_hashes_[col].insert(row_hashes[col]);
    }
    ++total_rows_;
}

// ==================== CONDITION ====================

MergeTreeIndexConditionBloomFilter::MergeTreeIndexConditionBloomFilter(
    const std::vector<std::string>& index_columns, size_t hash_functions)
    : index_columns_(index_columns)
    , hash_functions_(hash_functions) {}

bool MergeTreeIndexConditionBloomFilter::mayBeTrueOnGranule(
    MergeTreeIndexGranulePtr granule) const {
    auto* bf_granule = dynamic_cast<MergeTreeIndexGranuleBloomFilter*>(granule.get());
    if (!bf_granule || bf_granule->empty()) {
        // Empty granule or wrong type - cannot skip
        return true;
    }

    // Check all predicates (implicit AND)
    for (const auto& pred : predicates_) {
        if (!checkPredicate(pred, bf_granule)) {
            // Predicate definitely false - can skip this granule
            return false;
        }
    }

    // All predicates MAY be true
    return true;
}

void MergeTreeIndexConditionBloomFilter::addEqualsPredicate(const std::string& column_name,
                                                            uint64_t value_hash) {
    int col_idx = findColumnIndex(column_name);
    if (col_idx < 0) {
        // Column not indexed - cannot filter
        return;
    }

    Predicate pred;
    pred.type = PredicateType::EQUALS;
    pred.column_idx = static_cast<size_t>(col_idx);
    pred.value_hashes.push_back(value_hash);

    predicates_.push_back(pred);
}

void MergeTreeIndexConditionBloomFilter::addInPredicate(const std::string& column_name,
                                                        const std::vector<uint64_t>& value_hashes) {
    int col_idx = findColumnIndex(column_name);
    if (col_idx < 0) {
        // Column not indexed - cannot filter
        return;
    }

    Predicate pred;
    pred.type = PredicateType::IN;
    pred.column_idx = static_cast<size_t>(col_idx);
    pred.value_hashes = value_hashes;

    predicates_.push_back(pred);
}

int MergeTreeIndexConditionBloomFilter::findColumnIndex(const std::string& column_name) const {
    for (size_t i = 0; i < index_columns_.size(); ++i) {
        if (index_columns_[i] == column_name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool MergeTreeIndexConditionBloomFilter::checkPredicate(
    const Predicate& pred, const MergeTreeIndexGranuleBloomFilter* granule) const {
    const auto& filters = granule->getFilters();
    if (pred.column_idx >= filters.size() || !filters[pred.column_idx]) {
        // Invalid column or null filter - cannot skip
        return true;
    }

    const auto& filter = filters[pred.column_idx];

    switch (pred.type) {
        case PredicateType::EQUALS: {
            // Check if value might be in filter
            return filter->containsHash(pred.value_hashes[0]);
        }

        case PredicateType::IN: {
            // Check if ANY value might be in filter
            for (uint64_t hash : pred.value_hashes) {
                if (filter->containsHash(hash)) {
                    return true;  // At least one value may match
                }
            }
            return false;  // No values match - can skip
        }

        default:
            return true;  // Unknown predicate - cannot skip
    }
}

// ==================== INDEX ====================

MergeTreeIndexBloomFilter::MergeTreeIndexBloomFilter(const std::string& index_name,
                                                     const std::vector<std::string>& columns,
                                                     size_t granularity, size_t bits_per_row,
                                                     size_t hash_functions)
    : IMergeTreeIndex(IndexDescription(index_name, IndexType::BLOOM_FILTER, granularity))
    , index_name_(index_name)
    , columns_(columns)
    , granularity_(granularity)
    , bits_per_row_(bits_per_row)
    , hash_functions_(hash_functions) {
    if (columns.empty()) {
        throw std::invalid_argument("BloomFilter index requires at least one column");
    }
    if (bits_per_row == 0) {
        throw std::invalid_argument("BloomFilter bits_per_row must be positive");
    }
    if (hash_functions == 0) {
        throw std::invalid_argument("BloomFilter hash_functions must be positive");
    }
}

MergeTreeIndexGranulePtr MergeTreeIndexBloomFilter::createIndexGranule() const {
    return std::make_shared<MergeTreeIndexGranuleBloomFilter>(bits_per_row_, hash_functions_,
                                                              columns_.size());
}

MergeTreeIndexAggregatorPtr MergeTreeIndexBloomFilter::createIndexAggregator() const {
    return std::make_shared<MergeTreeIndexAggregatorBloomFilter>(bits_per_row_, hash_functions_,
                                                                 columns_);
}

MergeTreeIndexConditionPtr MergeTreeIndexBloomFilter::createIndexCondition() const {
    return std::make_shared<MergeTreeIndexConditionBloomFilter>(columns_, hash_functions_);
}

}  // namespace skipindex
}  // namespace index
}  // namespace diagon
