// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace diagon {
namespace storage {

/**
 * Storage tier for segments
 *
 * Based on: OpenSearch ILM & ClickHouse TTL
 */
enum class StorageTier : uint8_t {
    /**
     * HOT tier: Fast storage (NVMe/RAM)
     * - Recent data (< 7 days)
     * - High query volume
     * - Full indexing and caching
     */
    HOT = 0,

    /**
     * WARM tier: Standard storage (SATA SSD)
     * - Older data (7-30 days)
     * - Moderate query volume
     * - Reduced caching
     */
    WARM = 1,

    /**
     * COLD tier: Object storage (S3, Azure Blob)
     * - Archived data (30-365 days)
     * - Infrequent queries
     * - On-demand loading
     */
    COLD = 2,

    /**
     * FROZEN tier: Deep archive (Glacier, Tape)
     * - Historical data (> 365 days)
     * - Compliance/audit only
     * - Hours to access
     */
    FROZEN = 3
};

constexpr const char* toString(StorageTier tier) {
    switch (tier) {
        case StorageTier::HOT: return "hot";
        case StorageTier::WARM: return "warm";
        case StorageTier::COLD: return "cold";
        case StorageTier::FROZEN: return "frozen";
    }
    return "unknown";
}

/**
 * Per-tier storage configuration
 */
struct TierConfig {
    StorageTier tier;

    // ==================== Storage Backend ====================

    /**
     * Directory implementation for this tier
     * - HOT: ByteBuffersDirectory (RAM) or MMapDirectory
     * - WARM: FSDirectory or MMapDirectory
     * - COLD: S3Directory or AzureDirectory
     * - FROZEN: GlacierDirectory
     */
    std::string directory_type;

    /**
     * Base path for segment storage
     */
    std::filesystem::path base_path;

    // ==================== Performance Tuning ====================

    /**
     * Max memory for caching (bytes)
     * - HOT: Large (e.g., 16GB)
     * - WARM: Medium (e.g., 4GB)
     * - COLD: Small (e.g., 512MB)
     * - FROZEN: Minimal (e.g., 64MB)
     */
    size_t max_cache_bytes;

    /**
     * Enable memory-mapped I/O?
     * - HOT/WARM: true
     * - COLD/FROZEN: false (on-demand reads)
     */
    bool use_mmap;

    /**
     * Read-ahead buffer size
     */
    size_t read_ahead_bytes;

    // ==================== Query Optimization ====================

    /**
     * Participate in queries by default?
     * false = require explicit tier specification
     */
    bool searchable;

    /**
     * Enable skip index filtering?
     */
    bool use_skip_indexes;

    /**
     * Max concurrent queries against this tier
     */
    size_t max_concurrent_queries;

    // ==================== Lifecycle ====================

    /**
     * Allow new writes?
     * - HOT: true
     * - WARM/COLD/FROZEN: false (read-only)
     */
    bool writable;

    /**
     * Compress segments on migration?
     */
    bool compress_on_migrate;

    /**
     * Compression codec for this tier
     * - HOT: LZ4 (fast)
     * - WARM: ZSTD (balanced)
     * - COLD/FROZEN: ZSTD level 9 (max compression)
     */
    std::string compression_codec;
};

}  // namespace storage
}  // namespace diagon
