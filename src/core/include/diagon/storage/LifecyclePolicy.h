// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/storage/StorageTier.h"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace diagon {
namespace storage {

// Forward declaration
class SegmentInfo;

/**
 * Defines when and how segments move between tiers
 *
 * Based on: OpenSearch ILM policies & ClickHouse TTL
 */
struct LifecyclePolicy {
    std::string name;

    // ==================== Hot → Warm Transition ====================

    struct HotPhase {
        /**
         * Age before moving to warm (seconds)
         * -1 = never move
         */
        int64_t max_age_seconds{7 * 24 * 3600};  // 7 days

        /**
         * Size threshold (bytes)
         * Segments larger than this move to warm earlier
         */
        int64_t max_size_bytes{50LL * 1024 * 1024 * 1024};  // 50GB

        /**
         * Force merge before transition?
         */
        bool force_merge{true};

        /**
         * Target segment count after merge
         */
        int32_t merge_max_segments{1};
    } hot;

    // ==================== Warm → Cold Transition ====================

    struct WarmPhase {
        /**
         * Age before moving to cold (seconds)
         */
        int64_t max_age_seconds{30 * 24 * 3600};  // 30 days

        /**
         * Access count threshold
         * If accessed less than N times, move to cold
         */
        int32_t min_access_count{10};

        /**
         * Recompress with higher ratio?
         */
        bool recompress{true};

        /**
         * Delete source after successful migration?
         */
        bool delete_after_migrate{true};
    } warm;

    // ==================== Cold → Frozen Transition ====================

    struct ColdPhase {
        /**
         * Age before moving to frozen (seconds)
         */
        int64_t max_age_seconds{365 * 24 * 3600};  // 365 days

        /**
         * Convert to read-only format?
         */
        bool readonly_mode{true};

        /**
         * Prune columns to reduce size?
         * List of columns to keep (empty = keep all)
         */
        std::vector<std::string> retained_columns;
    } cold;

    // ==================== Frozen → Delete ====================

    struct FrozenPhase {
        /**
         * Age before deletion (seconds)
         * -1 = never delete
         */
        int64_t max_age_seconds{-1};  // Indefinite retention
    } frozen;

    // ==================== Evaluation ====================

    /**
     * Evaluate policy for segment
     * Returns target tier or nullopt if no transition needed
     *
     * NOTE: Stub implementation - requires full SegmentInfo
     */
    std::optional<StorageTier> evaluateSegment(
        StorageTier current_tier,
        int64_t age_seconds,
        int64_t size_bytes,
        int32_t access_count) const {

        switch (current_tier) {
            case StorageTier::HOT:
                if (age_seconds >= hot.max_age_seconds ||
                    size_bytes >= hot.max_size_bytes) {
                    return StorageTier::WARM;
                }
                break;

            case StorageTier::WARM:
                if (age_seconds >= warm.max_age_seconds ||
                    access_count < warm.min_access_count) {
                    return StorageTier::COLD;
                }
                break;

            case StorageTier::COLD:
                if (cold.max_age_seconds > 0 &&
                    age_seconds >= cold.max_age_seconds) {
                    return StorageTier::FROZEN;
                }
                break;

            case StorageTier::FROZEN:
                // Frozen is terminal (or delete if max_age_seconds set)
                break;
        }

        return std::nullopt;
    }
};

}  // namespace storage
}  // namespace diagon
