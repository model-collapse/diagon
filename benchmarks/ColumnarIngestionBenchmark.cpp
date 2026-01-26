// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

// Benchmark: ClickHouse-style columnar ingestion with granules and compression
//
// This benchmark simulates ClickHouse's MergeTree storage:
// - Columnar storage (each field is a separate column)
// - Granule-based writes (8192 rows per granule)
// - LZ4/ZSTD compression per granule
// - Mark file for random access
//
// Compares with traditional row-oriented storage to demonstrate advantages.

#include "diagon/columns/ColumnString.h"
#include "diagon/columns/ColumnVector.h"
#include "diagon/columns/IColumn.h"
#include "diagon/compression/CompressionCodecs.h"
#include "diagon/granularity/MergeTreeIndexGranularityConstant.h"
#include "diagon/store/ByteBuffersIndexOutput.h"
#include "diagon/store/FSDirectory.h"

#include <benchmark/benchmark.h>
#include <algorithm>
#include <chrono>
#include <random>
#include <sstream>
#include <vector>

using namespace diagon::columns;
using namespace diagon::compression;
using namespace diagon::granularity;
using namespace diagon::store;

// ==================== Document Schema ====================

struct Document {
    int64_t id;
    std::string title;
    std::string content;
    int64_t timestamp;
    double score;
    int32_t category;

    static Document generate(int64_t id, std::mt19937& rng) {
        Document doc;
        doc.id = id;

        // Generate realistic title (5-10 words)
        std::uniform_int_distribution<int> title_len(5, 10);
        std::uniform_int_distribution<int> word_len(3, 12);
        std::ostringstream title_ss;
        for (int i = 0; i < title_len(rng); ++i) {
            if (i > 0)
                title_ss << " ";
            // Generate random word
            for (int j = 0; j < word_len(rng); ++j) {
                title_ss << static_cast<char>('a' + (rng() % 26));
            }
        }
        doc.title = title_ss.str();

        // Generate realistic content (50-200 words)
        std::uniform_int_distribution<int> content_len(50, 200);
        std::ostringstream content_ss;
        for (int i = 0; i < content_len(rng); ++i) {
            if (i > 0)
                content_ss << " ";
            for (int j = 0; j < word_len(rng); ++j) {
                content_ss << static_cast<char>('a' + (rng() % 26));
            }
        }
        doc.content = content_ss.str();

        doc.timestamp = 1600000000 + (rng() % 100000000);
        doc.score = (rng() % 1000) / 10.0;
        doc.category = rng() % 10;

        return doc;
    }
};

// ==================== Row-Oriented Storage ====================

class RowOrientedWriter {
public:
    explicit RowOrientedWriter(const std::string& path)
        : out_(path) {}

    void write(const Document& doc) {
        // Write document as a single serialized row
        out_.writeLong(doc.id);
        writeString(doc.title);
        writeString(doc.content);
        out_.writeLong(doc.timestamp);
        // Write double as long (reinterpret bits)
        int64_t score_bits;
        std::memcpy(&score_bits, &doc.score, sizeof(double));
        out_.writeLong(score_bits);
        out_.writeInt(doc.category);
    }

    size_t size() const { return out_.size(); }

private:
    ByteBuffersIndexOutput out_;

    void writeString(const std::string& s) {
        out_.writeVInt(s.size());
        out_.writeBytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }
};

// ==================== Column-Oriented Storage ====================

class ColumnarWriter {
public:
    ColumnarWriter(const std::string& path, size_t granule_size,
                   CompressionCodecPtr codec)
        : granule_size_(granule_size)
        , codec_(codec)
        , granularity_(granule_size)
        , id_column_(ColumnInt64::create())
        , title_column_(ColumnString::create())
        , content_column_(ColumnString::create())
        , timestamp_column_(ColumnInt64::create())
        , score_column_(ColumnFloat64::create())
        , category_column_(ColumnInt32::create())
        , rows_in_current_granule_(0)
        , total_compressed_size_(0) {}

    void write(const Document& doc) {
        // Append to columns
        id_column_->getData().push_back(doc.id);
        title_column_->insertData(doc.title.data(), doc.title.size());
        content_column_->insertData(doc.content.data(), doc.content.size());
        timestamp_column_->getData().push_back(doc.timestamp);
        score_column_->getData().push_back(doc.score);
        category_column_->getData().push_back(doc.category);

        rows_in_current_granule_++;

        // Flush granule if full
        if (rows_in_current_granule_ >= granule_size_) {
            flushGranule();
        }
    }

    void finalize() {
        // Flush remaining partial granule
        if (rows_in_current_granule_ > 0) {
            flushGranule();
        }
    }

    size_t getCompressedSize() const { return total_compressed_size_; }

    size_t getUncompressedSize() const {
        return id_column_->byteSize() + title_column_->byteSize() +
               content_column_->byteSize() + timestamp_column_->byteSize() +
               score_column_->byteSize() + category_column_->byteSize();
    }

    double getCompressionRatio() const {
        if (total_compressed_size_ == 0)
            return 0.0;
        return static_cast<double>(getUncompressedSize()) / total_compressed_size_;
    }

private:
    size_t granule_size_;
    CompressionCodecPtr codec_;
    MergeTreeIndexGranularityConstant granularity_;

    // Columns
    std::shared_ptr<ColumnInt64> id_column_;
    std::shared_ptr<ColumnString> title_column_;
    std::shared_ptr<ColumnString> content_column_;
    std::shared_ptr<ColumnInt64> timestamp_column_;
    std::shared_ptr<ColumnFloat64> score_column_;
    std::shared_ptr<ColumnInt32> category_column_;

    size_t rows_in_current_granule_;
    size_t total_compressed_size_;

    void flushGranule() {
        // Compress each column's data for this granule
        total_compressed_size_ += compressColumn(id_column_);
        total_compressed_size_ += compressColumn(title_column_);
        total_compressed_size_ += compressColumn(content_column_);
        total_compressed_size_ += compressColumn(timestamp_column_);
        total_compressed_size_ += compressColumn(score_column_);
        total_compressed_size_ += compressColumn(category_column_);

        granularity_.addMark(rows_in_current_granule_);

        // Clear columns for next granule
        id_column_ = ColumnInt64::create();
        title_column_ = ColumnString::create();
        content_column_ = ColumnString::create();
        timestamp_column_ = ColumnInt64::create();
        score_column_ = ColumnFloat64::create();
        category_column_ = ColumnInt32::create();

        rows_in_current_granule_ = 0;
    }

    size_t compressColumn(const ColumnPtr& column) {
        const char* data = column->getRawData();
        size_t data_size = column->byteSize();

        if (!data || data_size == 0) {
            // String column - serialize first
            // For benchmark purposes, just estimate size
            return data_size;
        }

        // Compress data
        size_t max_compressed_size = codec_->getMaxCompressedSize(data_size);
        std::vector<char> compressed(max_compressed_size);

        size_t compressed_size = codec_->compress(data, data_size, compressed.data(),
                                                   max_compressed_size);

        return compressed_size;
    }
};

// ==================== Benchmark: Row-Oriented Ingestion ====================

static void BM_Ingestion_RowOriented(benchmark::State& state) {
    int num_docs = state.range(0);

    std::mt19937 rng(42);
    std::vector<Document> docs;
    docs.reserve(num_docs);
    for (int i = 0; i < num_docs; ++i) {
        docs.push_back(Document::generate(i, rng));
    }

    for (auto _ : state) {
        RowOrientedWriter writer("bench_row.dat");

        auto start = std::chrono::high_resolution_clock::now();
        for (const auto& doc : docs) {
            writer.write(doc);
        }
        auto end = std::chrono::high_resolution_clock::now();

        auto duration =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        state.SetIterationTime(duration.count() / 1000000.0);

        size_t bytes_written = writer.size();
        state.counters["bytes_written"] = bytes_written;
        state.counters["MB_per_sec"] =
            benchmark::Counter(bytes_written, benchmark::Counter::kIsRate,
                               benchmark::Counter::OneK::kIs1024);
    }

    state.SetItemsProcessed(state.iterations() * num_docs);
    state.SetLabel("Row-oriented (no compression)");
}

// ==================== Benchmark: Columnar Ingestion (LZ4) ====================

static void BM_Ingestion_Columnar_LZ4(benchmark::State& state) {
    int num_docs = state.range(0);
    size_t granule_size = 8192;  // ClickHouse default

    std::mt19937 rng(42);
    std::vector<Document> docs;
    docs.reserve(num_docs);
    for (int i = 0; i < num_docs; ++i) {
        docs.push_back(Document::generate(i, rng));
    }

    auto codec = LZ4Codec::create();

    for (auto _ : state) {
        ColumnarWriter writer("bench_columnar.dat", granule_size, codec);

        auto start = std::chrono::high_resolution_clock::now();
        for (const auto& doc : docs) {
            writer.write(doc);
        }
        writer.finalize();
        auto end = std::chrono::high_resolution_clock::now();

        auto duration =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        state.SetIterationTime(duration.count() / 1000000.0);

        size_t compressed_size = writer.getCompressedSize();
        size_t uncompressed_size = writer.getUncompressedSize();
        double compression_ratio = writer.getCompressionRatio();

        state.counters["compressed_bytes"] = compressed_size;
        state.counters["uncompressed_bytes"] = uncompressed_size;
        state.counters["compression_ratio"] = compression_ratio;
        state.counters["MB_per_sec"] =
            benchmark::Counter(compressed_size, benchmark::Counter::kIsRate,
                               benchmark::Counter::OneK::kIs1024);
    }

    state.SetItemsProcessed(state.iterations() * num_docs);
    state.SetLabel("Columnar + LZ4 (granule=8192)");
}

// ==================== Benchmark: Columnar Ingestion (ZSTD) ====================

static void BM_Ingestion_Columnar_ZSTD(benchmark::State& state) {
    int num_docs = state.range(0);
    size_t granule_size = 8192;

    std::mt19937 rng(42);
    std::vector<Document> docs;
    docs.reserve(num_docs);
    for (int i = 0; i < num_docs; ++i) {
        docs.push_back(Document::generate(i, rng));
    }

    auto codec = ZSTDCodec::create(3);  // Level 3 (ClickHouse default)

    for (auto _ : state) {
        ColumnarWriter writer("bench_columnar.dat", granule_size, codec);

        auto start = std::chrono::high_resolution_clock::now();
        for (const auto& doc : docs) {
            writer.write(doc);
        }
        writer.finalize();
        auto end = std::chrono::high_resolution_clock::now();

        auto duration =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        state.SetIterationTime(duration.count() / 1000000.0);

        size_t compressed_size = writer.getCompressedSize();
        size_t uncompressed_size = writer.getUncompressedSize();
        double compression_ratio = writer.getCompressionRatio();

        state.counters["compressed_bytes"] = compressed_size;
        state.counters["uncompressed_bytes"] = uncompressed_size;
        state.counters["compression_ratio"] = compression_ratio;
        state.counters["MB_per_sec"] =
            benchmark::Counter(compressed_size, benchmark::Counter::kIsRate,
                               benchmark::Counter::OneK::kIs1024);
    }

    state.SetItemsProcessed(state.iterations() * num_docs);
    state.SetLabel("Columnar + ZSTD(3) (granule=8192)");
}

// ==================== Benchmark: Columnar Ingestion (No Compression) ====================

static void BM_Ingestion_Columnar_NoCompression(benchmark::State& state) {
    int num_docs = state.range(0);
    size_t granule_size = 8192;

    std::mt19937 rng(42);
    std::vector<Document> docs;
    docs.reserve(num_docs);
    for (int i = 0; i < num_docs; ++i) {
        docs.push_back(Document::generate(i, rng));
    }

    auto codec = NoneCodec::create();

    for (auto _ : state) {
        ColumnarWriter writer("bench_columnar.dat", granule_size, codec);

        auto start = std::chrono::high_resolution_clock::now();
        for (const auto& doc : docs) {
            writer.write(doc);
        }
        writer.finalize();
        auto end = std::chrono::high_resolution_clock::now();

        auto duration =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        state.SetIterationTime(duration.count() / 1000000.0);

        size_t bytes_written = writer.getUncompressedSize();
        state.counters["bytes_written"] = bytes_written;
        state.counters["MB_per_sec"] =
            benchmark::Counter(bytes_written, benchmark::Counter::kIsRate,
                               benchmark::Counter::OneK::kIs1024);
    }

    state.SetItemsProcessed(state.iterations() * num_docs);
    state.SetLabel("Columnar (no compression, granule=8192)");
}

// ==================== Register Benchmarks ====================

// Test with various document counts
BENCHMARK(BM_Ingestion_RowOriented)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Ingestion_Columnar_NoCompression)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Ingestion_Columnar_LZ4)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_Ingestion_Columnar_ZSTD)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
