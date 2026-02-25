/**
 * ColumnarStore — Header-only columnar writer + reader with LZ4 compression
 * and MinMax skip indexes for granule-level range query acceleration.
 *
 * Uses the same granule size (8192 rows) as ClickHouse/Diagon MergeTree.
 * Data layout: contiguous int64_t arrays per granule, LZ4-compressed.
 * Metadata: per-granule min/max values + file offsets for skip evaluation.
 *
 * Three-level range evaluation:
 *   1. MinMax SKIP   — granule entirely outside range (no I/O)
 *   2. MinMax BULK   — granule entirely within range (add numRows, no decompress)
 *   3. Decompress+Scan — partial overlap, LZ4 decompress and scan values
 */

#pragma once

#include "diagon/compression/CompressionCodecs.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace diagon {
namespace columnar {

// File format constants
static constexpr uint32_t DCOL_MAGIC = 0x44434F4C;  // "DCOL"
static constexpr uint32_t DCOL_VERSION = 1;
static constexpr uint32_t DEFAULT_GRANULE_SIZE = 8192;

struct GranuleInfo {
    int64_t minValue;
    int64_t maxValue;
    uint64_t fileOffset;      // offset into data section of .col file
    uint32_t compressedSize;
    uint32_t numRows;
    uint32_t startDocId;
};

// ============================================================
// ColumnarWriter
// ============================================================

class ColumnarWriter {
public:
    explicit ColumnarWriter(const std::string& basePath, uint32_t granuleSize = DEFAULT_GRANULE_SIZE)
        : basePath_(basePath)
        , granuleSize_(granuleSize)
        , currentDocId_(0) {}

    void defineColumn(const std::string& name) {
        columns_[name] = ColumnState{};
    }

    void addValue(const std::string& name, int64_t value) {
        auto it = columns_.find(name);
        if (it == columns_.end()) return;

        auto& col = it->second;
        col.buffer.push_back(value);

        // Track min/max for current granule
        if (value < col.currentMin) col.currentMin = value;
        if (value > col.currentMax) col.currentMax = value;
    }

    void endDocument() {
        currentDocId_++;

        // Check if any column has a full granule
        // (all columns should be in sync since we add one value per doc per column)
        for (auto& [name, col] : columns_) {
            if (col.buffer.size() >= granuleSize_) {
                flushGranule(col);
            }
        }
    }

    void close() {
        // Flush any partial granules
        for (auto& [name, col] : columns_) {
            if (!col.buffer.empty()) {
                flushGranule(col);
            }
        }

        // Write .col and .meta files for each column
        // Ensure output directory exists
        std::string mkdirCmd = "mkdir -p " + basePath_;
        [[maybe_unused]] int ret = system(mkdirCmd.c_str());

        for (const auto& [name, col] : columns_) {
            writeColumnFiles(name, col);
        }
    }

    uint32_t getDocCount() const { return currentDocId_; }

private:
    struct ColumnState {
        std::vector<int64_t> buffer;                // current granule accumulator
        std::vector<GranuleInfo> granules;           // completed granule metadata
        std::vector<char> compressedData;            // accumulated compressed bytes
        int64_t currentMin = std::numeric_limits<int64_t>::max();
        int64_t currentMax = std::numeric_limits<int64_t>::min();
        uint32_t granuleStartDocId = 0;
    };

    void flushGranule(ColumnState& col) {
        uint32_t numRows = static_cast<uint32_t>(col.buffer.size());
        size_t rawSize = numRows * sizeof(int64_t);

        // LZ4 compress
        auto codec = compression::LZ4Codec::create();
        size_t maxCompressed = codec->getMaxCompressedSize(rawSize);
        std::vector<char> compBuf(maxCompressed);

        size_t compressedSize = codec->compress(
            reinterpret_cast<const char*>(col.buffer.data()),
            rawSize,
            compBuf.data(),
            maxCompressed);

        // Record granule info
        GranuleInfo info;
        info.minValue = col.currentMin;
        info.maxValue = col.currentMax;
        info.fileOffset = static_cast<uint64_t>(col.compressedData.size());
        info.compressedSize = static_cast<uint32_t>(compressedSize);
        info.numRows = numRows;
        info.startDocId = col.granuleStartDocId;
        col.granules.push_back(info);

        // Append compressed data
        col.compressedData.insert(col.compressedData.end(),
                                  compBuf.data(), compBuf.data() + compressedSize);

        // Reset buffer and min/max
        col.buffer.clear();
        col.currentMin = std::numeric_limits<int64_t>::max();
        col.currentMax = std::numeric_limits<int64_t>::min();
        col.granuleStartDocId = currentDocId_;
    }

    void writeColumnFiles(const std::string& name, const ColumnState& col) {
        // Write .col file
        {
            std::string colPath = basePath_ + "/" + name + ".col";
            std::ofstream ofs(colPath, std::ios::binary);
            if (!ofs) throw std::runtime_error("Cannot create " + colPath);

            // Header (24 bytes)
            uint32_t magic = DCOL_MAGIC;
            uint32_t version = DCOL_VERSION;
            uint32_t numGranules = static_cast<uint32_t>(col.granules.size());
            uint32_t granSize = granuleSize_;
            uint64_t totalRows = currentDocId_;

            ofs.write(reinterpret_cast<const char*>(&magic), 4);
            ofs.write(reinterpret_cast<const char*>(&version), 4);
            ofs.write(reinterpret_cast<const char*>(&numGranules), 4);
            ofs.write(reinterpret_cast<const char*>(&granSize), 4);
            ofs.write(reinterpret_cast<const char*>(&totalRows), 8);

            // Data section
            ofs.write(col.compressedData.data(),
                      static_cast<std::streamsize>(col.compressedData.size()));
        }

        // Write .meta file
        {
            std::string metaPath = basePath_ + "/" + name + ".meta";
            std::ofstream ofs(metaPath, std::ios::binary);
            if (!ofs) throw std::runtime_error("Cannot create " + metaPath);

            uint32_t numGranules = static_cast<uint32_t>(col.granules.size());
            ofs.write(reinterpret_cast<const char*>(&numGranules), 4);

            for (const auto& g : col.granules) {
                ofs.write(reinterpret_cast<const char*>(&g.minValue), 8);
                ofs.write(reinterpret_cast<const char*>(&g.maxValue), 8);
                ofs.write(reinterpret_cast<const char*>(&g.fileOffset), 8);
                ofs.write(reinterpret_cast<const char*>(&g.compressedSize), 4);
                ofs.write(reinterpret_cast<const char*>(&g.numRows), 4);
                ofs.write(reinterpret_cast<const char*>(&g.startDocId), 4);
            }
        }
    }

    std::string basePath_;
    uint32_t granuleSize_;
    uint32_t currentDocId_;
    std::unordered_map<std::string, ColumnState> columns_;
};

// ============================================================
// ColumnarReader
// ============================================================

class ColumnarReader {
public:
    ColumnarReader() = default;

    void open(const std::string& basePath, const std::string& columnName) {
        columnName_ = columnName;

        // Read .meta file
        {
            std::string metaPath = basePath + "/" + columnName + ".meta";
            std::ifstream ifs(metaPath, std::ios::binary);
            if (!ifs) throw std::runtime_error("Cannot open " + metaPath);

            uint32_t numGranules = 0;
            ifs.read(reinterpret_cast<char*>(&numGranules), 4);

            granules_.resize(numGranules);
            for (uint32_t i = 0; i < numGranules; i++) {
                auto& g = granules_[i];
                ifs.read(reinterpret_cast<char*>(&g.minValue), 8);
                ifs.read(reinterpret_cast<char*>(&g.maxValue), 8);
                ifs.read(reinterpret_cast<char*>(&g.fileOffset), 8);
                ifs.read(reinterpret_cast<char*>(&g.compressedSize), 4);
                ifs.read(reinterpret_cast<char*>(&g.numRows), 4);
                ifs.read(reinterpret_cast<char*>(&g.startDocId), 4);
            }
        }

        // Read .col file — load data section into memory (skip 24-byte header)
        {
            std::string colPath = basePath + "/" + columnName + ".col";
            std::ifstream ifs(colPath, std::ios::binary | std::ios::ate);
            if (!ifs) throw std::runtime_error("Cannot open " + colPath);

            auto fileSize = ifs.tellg();
            if (fileSize <= 24) {
                data_.clear();
                totalDocs_ = 0;
                return;
            }

            // Read header to get totalRows
            ifs.seekg(0);
            uint32_t magic, version, numGranules, granSize;
            uint64_t totalRows;
            ifs.read(reinterpret_cast<char*>(&magic), 4);
            ifs.read(reinterpret_cast<char*>(&version), 4);
            ifs.read(reinterpret_cast<char*>(&numGranules), 4);
            ifs.read(reinterpret_cast<char*>(&granSize), 4);
            ifs.read(reinterpret_cast<char*>(&totalRows), 8);

            if (magic != DCOL_MAGIC) {
                throw std::runtime_error("Invalid .col file magic: " + colPath);
            }

            totalDocs_ = static_cast<int>(totalRows);

            // Read data section
            size_t dataSize = static_cast<size_t>(fileSize) - 24;
            data_.resize(dataSize);
            ifs.read(data_.data(), static_cast<std::streamsize>(dataSize));
        }
    }

    /**
     * Count rows matching range [lower, upper] with configurable inclusivity.
     * Uses three-level evaluation:
     *   1. SKIP:  granule max < lower or min > upper
     *   2. BULK:  granule entirely within range (min >= lower, max <= upper)
     *   3. SCAN:  partial overlap — decompress and scan
     */
    int rangeCount(int64_t lower, int64_t upper, bool includeLower, bool includeUpper) {
        lastGranulesScanned_ = 0;
        lastGranulesSkipped_ = 0;
        lastGranulesBulkCounted_ = 0;

        int count = 0;
        auto codec = compression::LZ4Codec::create();

        for (const auto& g : granules_) {
            // Level 1: MinMax SKIP
            bool skipLower = includeLower ? (g.maxValue < lower) : (g.maxValue <= lower);
            bool skipUpper = includeUpper ? (g.minValue > upper) : (g.minValue >= upper);

            if (skipLower || skipUpper) {
                lastGranulesSkipped_++;
                continue;
            }

            // Level 2: MinMax BULK — entire granule within range
            bool bulkLower = includeLower ? (g.minValue >= lower) : (g.minValue > lower);
            bool bulkUpper = includeUpper ? (g.maxValue <= upper) : (g.maxValue < upper);

            if (bulkLower && bulkUpper) {
                count += static_cast<int>(g.numRows);
                lastGranulesBulkCounted_++;
                continue;
            }

            // Level 3: Decompress and scan
            size_t rawSize = g.numRows * sizeof(int64_t);
            std::vector<int64_t> values(g.numRows);

            codec->decompress(
                data_.data() + g.fileOffset,
                g.compressedSize,
                reinterpret_cast<char*>(values.data()),
                rawSize);

            for (uint32_t i = 0; i < g.numRows; i++) {
                int64_t v = values[i];
                bool matchLower = includeLower ? (v >= lower) : (v > lower);
                bool matchUpper = includeUpper ? (v <= upper) : (v < upper);
                if (matchLower && matchUpper) {
                    count++;
                }
            }
            lastGranulesScanned_++;
        }

        return count;
    }

    int totalDocs() const { return totalDocs_; }
    int granulesTotal() const { return static_cast<int>(granules_.size()); }
    int granulesScanned() const { return lastGranulesScanned_; }
    int granulesSkipped() const { return lastGranulesSkipped_; }
    int granulesBulkCounted() const { return lastGranulesBulkCounted_; }

private:
    std::string columnName_;
    std::vector<GranuleInfo> granules_;
    std::vector<char> data_;  // compressed data section from .col file
    int totalDocs_ = 0;

    // Stats from last query
    int lastGranulesScanned_ = 0;
    int lastGranulesSkipped_ = 0;
    int lastGranulesBulkCounted_ = 0;
};

}  // namespace columnar
}  // namespace diagon
