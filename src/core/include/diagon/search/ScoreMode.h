// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

namespace diagon {
namespace search {

/**
 * How scores will be consumed by collector
 *
 * Based on: org.apache.lucene.search.ScoreMode
 */
enum class ScoreMode {
    /**
     * Scores are needed and must be computed
     */
    COMPLETE,

    /**
     * Only doc IDs needed, scores not required
     * Enables optimizations (e.g., skip score computation)
     */
    COMPLETE_NO_SCORES,

    /**
     * Top-scoring docs needed
     * Enables early termination optimizations
     */
    TOP_SCORES
};

}  // namespace search
}  // namespace diagon
