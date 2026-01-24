// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace diagon {
namespace index {

// Forward declaration
class OneMerge;

/**
 * Describes a set of merges to perform
 *
 * Based on: org.apache.lucene.index.MergeSpecification
 */
class MergeSpecification {
public:
    /**
     * Add merge
     */
    void add(std::unique_ptr<OneMerge> merge) {
        merges_.push_back(std::move(merge));
    }

    /**
     * Get all merges
     */
    const std::vector<std::unique_ptr<OneMerge>>& getMerges() const {
        return merges_;
    }

    /**
     * Number of merges
     */
    size_t size() const {
        return merges_.size();
    }

    /**
     * Is empty?
     */
    bool empty() const {
        return merges_.empty();
    }

    /**
     * Description
     */
    std::string segString() const {
        std::string s;
        for (size_t i = 0; i < merges_.size(); ++i) {
            if (i > 0) s += " ";
            s += "[merge " + std::to_string(i) + "]";
        }
        return s;
    }

private:
    std::vector<std::unique_ptr<OneMerge>> merges_;
};

}  // namespace index
}  // namespace diagon
