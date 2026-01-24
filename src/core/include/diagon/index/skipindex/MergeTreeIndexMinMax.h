// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/index/skipindex/IMergeTreeIndex.h"

#include <limits>
#include <string>

namespace diagon {
namespace index {
namespace skipindex {

/**
 * MinMax granule stores min/max values for range queries
 *
 * Based on: ClickHouse MergeTreeIndexGranuleMinMax
 */
class MergeTreeIndexGranuleMinMax : public IMergeTreeIndexGranule {
public:
    explicit MergeTreeIndexGranuleMinMax(size_t columns_count = 0)
        : min_values_()
        , max_values_() {
        // Vectors start empty, will be resized on first add
    }

    bool empty() const override {
        // Granule is empty if no values have been added
        return min_values_.empty() && max_values_.empty();
    }

    size_t memoryUsageBytes() const override {
        // Stub: would compute actual size
        return min_values_.size() * 16;  // Approximate
    }

    void addMinValue(double value) {
        if (min_values_.size() == 0) {
            min_values_.resize(1, std::numeric_limits<double>::max());
        }
        if (value < min_values_[0]) {
            min_values_[0] = value;
        }
    }

    void addMaxValue(double value) {
        if (max_values_.size() == 0) {
            max_values_.resize(1, std::numeric_limits<double>::lowest());
        }
        if (value > max_values_[0]) {
            max_values_[0] = value;
        }
    }

    double getMinValue(size_t col = 0) const {
        if (col >= min_values_.size() || min_values_.size() == 0) {
            return 0.0;
        }
        return min_values_[col];
    }

    double getMaxValue(size_t col = 0) const {
        if (col >= max_values_.size() || max_values_.size() == 0) {
            return 0.0;
        }
        return max_values_[col];
    }

private:
    std::vector<double> min_values_;
    std::vector<double> max_values_;
};

/**
 * MinMax aggregator
 */
class MergeTreeIndexAggregatorMinMax : public IMergeTreeIndexAggregator {
public:
    explicit MergeTreeIndexAggregatorMinMax(size_t columns_count)
        : granule_(std::make_shared<MergeTreeIndexGranuleMinMax>(columns_count)) {}

    bool empty() const override {
        return granule_->empty();
    }

    MergeTreeIndexGranulePtr getGranuleAndReset() override {
        auto result = granule_;
        granule_ = std::make_shared<MergeTreeIndexGranuleMinMax>(1);
        return result;
    }

    void addValue(double value) {
        granule_->addMinValue(value);
        granule_->addMaxValue(value);
    }

private:
    std::shared_ptr<MergeTreeIndexGranuleMinMax> granule_;
};

/**
 * MinMax condition for range queries
 */
class MergeTreeIndexConditionMinMax : public IMergeTreeIndexCondition {
public:
    MergeTreeIndexConditionMinMax()
        : min_threshold_(std::numeric_limits<double>::lowest())
        , max_threshold_(std::numeric_limits<double>::max()) {}

    bool alwaysUnknownOrTrue() const override {
        return false;  // MinMax can filter
    }

    bool mayBeTrueOnGranule(MergeTreeIndexGranulePtr granule) const override {
        auto minmax_granule = std::dynamic_pointer_cast<MergeTreeIndexGranuleMinMax>(granule);
        if (!minmax_granule) {
            return true;  // Unknown granule type, assume match
        }

        // Check if range overlaps with condition
        double min_val = minmax_granule->getMinValue();
        double max_val = minmax_granule->getMaxValue();

        // If max < min_threshold or min > max_threshold, skip granule
        if (max_val < min_threshold_ || min_val > max_threshold_) {
            return false;
        }

        return true;
    }

    std::string getDescription() const override {
        return "MinMax condition";
    }

    void setRange(double min_threshold, double max_threshold) {
        min_threshold_ = min_threshold;
        max_threshold_ = max_threshold;
    }

private:
    double min_threshold_;
    double max_threshold_;
};

/**
 * MinMax index implementation
 */
class MergeTreeIndexMinMax : public IMergeTreeIndex {
public:
    explicit MergeTreeIndexMinMax(const IndexDescription& desc)
        : IMergeTreeIndex(desc) {}

    MergeTreeIndexGranulePtr createIndexGranule() const override {
        return std::make_shared<MergeTreeIndexGranuleMinMax>(1);
    }

    MergeTreeIndexAggregatorPtr createIndexAggregator() const override {
        return std::make_shared<MergeTreeIndexAggregatorMinMax>(1);
    }

    MergeTreeIndexConditionPtr createIndexCondition() const override {
        return std::make_shared<MergeTreeIndexConditionMinMax>();
    }
};

}  // namespace skipindex
}  // namespace index
}  // namespace diagon
