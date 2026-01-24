// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/storage/LifecyclePolicy.h"
#include "diagon/storage/StorageTier.h"

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace diagon {

// Forward declarations
namespace store {
class Directory;
}  // namespace store

namespace storage {

/**
 * Manages segment lifecycle across storage tiers
 *
 * Based on: OpenSearch ILM & ClickHouse TTL management
 *
 * NOTE: Stub implementation - directory creation and file operations
 * require full storage implementation.
 */
class TierManager {
public:
    TierManager(const std::map<StorageTier, TierConfig>& configs,
                const LifecyclePolicy& policy)
        : configs_(configs)
        , policy_(policy) {}

    // ==================== Segment Registration ====================

    /**
     * Register new segment (initially in HOT tier)
     */
    void registerSegment(const std::string& segment_name, size_t size_bytes) {
        std::lock_guard lock(mutex_);

        segment_metadata_[segment_name] = SegmentMetadata{
            .tier = StorageTier::HOT,
            .creation_time = std::chrono::system_clock::now(),
            .last_access_time = std::chrono::system_clock::now(),
            .access_count = 0,
            .size_bytes = static_cast<int64_t>(size_bytes)
        };
    }

    // ==================== Tier Query ====================

    /**
     * Get current tier for segment
     */
    StorageTier getSegmentTier(const std::string& segment_name) const {
        std::lock_guard lock(mutex_);

        auto it = segment_metadata_.find(segment_name);
        if (it == segment_metadata_.end()) {
            throw std::invalid_argument("Unknown segment: " + segment_name);
        }

        return it->second.tier;
    }

    /**
     * Get tier configuration
     */
    const TierConfig& getConfig(StorageTier tier) const {
        auto it = configs_.find(tier);
        if (it == configs_.end()) {
            throw std::invalid_argument("Tier not configured: " + std::string(toString(tier)));
        }
        return it->second;
    }

    // ==================== Access Tracking ====================

    /**
     * Record segment access (for warm tier decisions)
     */
    void recordAccess(const std::string& segment_name) {
        std::lock_guard lock(mutex_);

        auto it = segment_metadata_.find(segment_name);
        if (it != segment_metadata_.end()) {
            it->second.last_access_time = std::chrono::system_clock::now();
            it->second.access_count++;
        }
    }

    /**
     * Get access count for segment
     */
    int32_t getAccessCount(const std::string& segment_name) const {
        std::lock_guard lock(mutex_);

        auto it = segment_metadata_.find(segment_name);
        if (it == segment_metadata_.end()) {
            return 0;
        }

        return it->second.access_count;
    }

    // ==================== Lifecycle Management ====================

    /**
     * Evaluate all segments and identify migrations
     * Returns list of (segment, target_tier) pairs
     */
    std::vector<std::pair<std::string, StorageTier>> evaluateMigrations() {
        std::lock_guard lock(mutex_);

        std::vector<std::pair<std::string, StorageTier>> migrations;

        auto now = std::chrono::system_clock::now();

        for (const auto& [segment_name, metadata] : segment_metadata_) {
            // Calculate age
            auto age = now - metadata.creation_time;
            auto age_seconds = std::chrono::duration_cast<std::chrono::seconds>(age).count();

            // Evaluate policy
            auto target_tier = policy_.evaluateSegment(
                metadata.tier,
                age_seconds,
                metadata.size_bytes,
                metadata.access_count
            );

            if (target_tier.has_value()) {
                migrations.emplace_back(segment_name, *target_tier);
            }
        }

        return migrations;
    }

    /**
     * Migrate segment to target tier
     *
     * NOTE: Stub - actual file migration requires full Directory implementation
     */
    void migrateSegment(const std::string& segment_name, StorageTier target_tier) {
        std::lock_guard lock(mutex_);

        auto it = segment_metadata_.find(segment_name);
        if (it == segment_metadata_.end()) {
            throw std::invalid_argument("Unknown segment: " + segment_name);
        }

        auto& metadata = it->second;
        StorageTier source_tier = metadata.tier;

        if (source_tier == target_tier) {
            return;  // Already in target tier
        }

        // Update metadata
        metadata.tier = target_tier;

        // Actual file migration would happen here
        // (copying/recompressing files between directories)
    }

    // ==================== Query Optimization ====================

    /**
     * Get searchable tiers for query
     * Respects tier config searchable flag
     */
    std::vector<StorageTier> getSearchableTiers() const {
        std::vector<StorageTier> tiers;

        for (const auto& [tier, config] : configs_) {
            if (config.searchable) {
                tiers.push_back(tier);
            }
        }

        return tiers;
    }

    /**
     * Get segments in specified tiers
     */
    std::vector<std::string> getSegmentsInTiers(
        const std::vector<StorageTier>& tiers) const {

        std::lock_guard lock(mutex_);

        std::vector<std::string> segments;

        for (const auto& [segment_name, metadata] : segment_metadata_) {
            if (std::find(tiers.begin(), tiers.end(), metadata.tier) != tiers.end()) {
                segments.push_back(segment_name);
            }
        }

        return segments;
    }

    /**
     * Get all registered segments
     */
    std::vector<std::string> getAllSegments() const {
        std::lock_guard lock(mutex_);

        std::vector<std::string> segments;
        for (const auto& [segment_name, _] : segment_metadata_) {
            segments.push_back(segment_name);
        }

        return segments;
    }

    /**
     * Get lifecycle policy
     */
    const LifecyclePolicy& getPolicy() const {
        return policy_;
    }

private:
    struct SegmentMetadata {
        StorageTier tier;
        std::chrono::system_clock::time_point creation_time;
        std::chrono::system_clock::time_point last_access_time;
        int32_t access_count;
        int64_t size_bytes;
    };

    std::map<StorageTier, TierConfig> configs_;
    LifecyclePolicy policy_;

    std::map<std::string, SegmentMetadata> segment_metadata_;

    mutable std::mutex mutex_;
};

}  // namespace storage
}  // namespace diagon
