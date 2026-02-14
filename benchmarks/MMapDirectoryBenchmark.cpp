// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/store/FSDirectory.h"
#include "diagon/store/IOContext.h"
#include "diagon/store/MMapDirectory.h"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <filesystem>
#include <random>
#include <vector>

using namespace diagon::store;

namespace {

// Shared test directory
std::filesystem::path test_dir;

// Test file sizes
constexpr size_t SMALL_FILE = 1 * 1024 * 1024;    // 1MB
constexpr size_t MEDIUM_FILE = 10 * 1024 * 1024;  // 10MB
constexpr size_t LARGE_FILE = 100 * 1024 * 1024;  // 100MB

// Initialize test environment
void SetupBenchmarkEnvironment() {
    test_dir = std::filesystem::temp_directory_path() / "diagon_bench_mmap";
    std::filesystem::create_directories(test_dir);

    // Create test files if they don't exist
    auto create_file = [](const std::string& name, size_t size) {
        auto path = test_dir / name;
        if (!std::filesystem::exists(path)) {
            auto dir = FSDirectory::open(test_dir);
            auto output = dir->createOutput(name, IOContext::DEFAULT);

            std::vector<uint8_t> data(size);
            for (size_t i = 0; i < size; ++i) {
                data[i] = static_cast<uint8_t>(i & 0xFF);
            }

            output->writeBytes(data.data(), data.size());
            output->close();
        }
    };

    create_file("small.bin", SMALL_FILE);
    create_file("medium.bin", MEDIUM_FILE);
    create_file("large.bin", LARGE_FILE);
}

// Cleanup test environment
void CleanupBenchmarkEnvironment() {
    if (std::filesystem::exists(test_dir)) {
        std::filesystem::remove_all(test_dir);
    }
}

}  // namespace

// ==================== Sequential Read Benchmarks ====================

static void BM_FSDirectory_SequentialRead_Small(benchmark::State& state) {
    auto dir = FSDirectory::open(test_dir);
    auto input = dir->openInput("small.bin", IOContext::DEFAULT);

    uint8_t buffer[4096];
    for (auto _ : state) {
        input->seek(0);
        size_t total = 0;
        while (total < SMALL_FILE) {
            size_t to_read = std::min(sizeof(buffer), SMALL_FILE - total);
            input->readBytes(buffer, to_read);
            total += to_read;
            benchmark::DoNotOptimize(buffer);
        }
    }

    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(SMALL_FILE));
}
BENCHMARK(BM_FSDirectory_SequentialRead_Small);

static void BM_MMapDirectory_SequentialRead_Small(benchmark::State& state) {
    auto dir = MMapDirectory::open(test_dir);
    auto input = dir->openInput("small.bin", IOContext(IOContext::Type::READONCE));

    uint8_t buffer[4096];
    for (auto _ : state) {
        input->seek(0);
        size_t total = 0;
        while (total < SMALL_FILE) {
            size_t to_read = std::min(sizeof(buffer), SMALL_FILE - total);
            input->readBytes(buffer, to_read);
            total += to_read;
            benchmark::DoNotOptimize(buffer);
        }
    }

    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(SMALL_FILE));
}
BENCHMARK(BM_MMapDirectory_SequentialRead_Small);

static void BM_FSDirectory_SequentialRead_Large(benchmark::State& state) {
    auto dir = FSDirectory::open(test_dir);
    auto input = dir->openInput("large.bin", IOContext::DEFAULT);

    uint8_t buffer[4096];
    for (auto _ : state) {
        input->seek(0);
        size_t total = 0;
        while (total < LARGE_FILE) {
            size_t to_read = std::min(sizeof(buffer), LARGE_FILE - total);
            input->readBytes(buffer, to_read);
            total += to_read;
            benchmark::DoNotOptimize(buffer);
        }
    }

    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(LARGE_FILE));
}
BENCHMARK(BM_FSDirectory_SequentialRead_Large);

static void BM_MMapDirectory_SequentialRead_Large(benchmark::State& state) {
    auto dir = MMapDirectory::open(test_dir);
    auto input = dir->openInput("large.bin", IOContext(IOContext::Type::READONCE));

    uint8_t buffer[4096];
    for (auto _ : state) {
        input->seek(0);
        size_t total = 0;
        while (total < LARGE_FILE) {
            size_t to_read = std::min(sizeof(buffer), LARGE_FILE - total);
            input->readBytes(buffer, to_read);
            total += to_read;
            benchmark::DoNotOptimize(buffer);
        }
    }

    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(LARGE_FILE));
}
BENCHMARK(BM_MMapDirectory_SequentialRead_Large);

// ==================== Random Read Benchmarks ====================

static void BM_FSDirectory_RandomRead(benchmark::State& state) {
    auto dir = FSDirectory::open(test_dir);
    auto input = dir->openInput("medium.bin", IOContext::DEFAULT);

    std::mt19937 rng(12345);
    std::uniform_int_distribution<int64_t> dist(0, MEDIUM_FILE - 1);

    uint8_t value;
    for (auto _ : state) {
        int64_t pos = dist(rng);
        input->seek(pos);
        value = input->readByte();
        benchmark::DoNotOptimize(value);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_FSDirectory_RandomRead);

static void BM_MMapDirectory_RandomRead(benchmark::State& state) {
    auto dir = MMapDirectory::open(test_dir);
    auto input = dir->openInput("medium.bin", IOContext(IOContext::Type::READ));

    std::mt19937 rng(12345);
    std::uniform_int_distribution<int64_t> dist(0, MEDIUM_FILE - 1);

    uint8_t value;
    for (auto _ : state) {
        int64_t pos = dist(rng);
        input->seek(pos);
        value = input->readByte();
        benchmark::DoNotOptimize(value);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MMapDirectory_RandomRead);

// ==================== Clone Benchmarks ====================

static void BM_FSDirectory_Clone(benchmark::State& state) {
    auto dir = FSDirectory::open(test_dir);
    auto input = dir->openInput("small.bin", IOContext::DEFAULT);

    for (auto _ : state) {
        auto cloned = input->clone();
        uint8_t value = cloned->readByte();
        benchmark::DoNotOptimize(cloned);
        benchmark::DoNotOptimize(value);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_FSDirectory_Clone);

static void BM_MMapDirectory_Clone(benchmark::State& state) {
    auto dir = MMapDirectory::open(test_dir);
    auto input = dir->openInput("small.bin", IOContext::DEFAULT);

    for (auto _ : state) {
        auto cloned = input->clone();
        uint8_t value = cloned->readByte();
        benchmark::DoNotOptimize(cloned);
        benchmark::DoNotOptimize(value);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MMapDirectory_Clone);

// ==================== Slice Benchmarks ====================

static void BM_FSDirectory_Slice(benchmark::State& state) {
    auto dir = FSDirectory::open(test_dir);
    auto input = dir->openInput("medium.bin", IOContext::DEFAULT);

    for (auto _ : state) {
        auto sliced = input->slice("bench_slice", 1024, 4096);
        uint8_t value = sliced->readByte();
        benchmark::DoNotOptimize(sliced);
        benchmark::DoNotOptimize(value);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_FSDirectory_Slice);

static void BM_MMapDirectory_Slice(benchmark::State& state) {
    auto dir = MMapDirectory::open(test_dir);
    auto input = dir->openInput("medium.bin", IOContext::DEFAULT);

    for (auto _ : state) {
        auto sliced = input->slice("bench_slice", 1024, 4096);
        uint8_t value = sliced->readByte();
        benchmark::DoNotOptimize(sliced);
        benchmark::DoNotOptimize(value);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MMapDirectory_Slice);

// ==================== Read Advice Optimization ====================

static void BM_MMapDirectory_SequentialAdvice(benchmark::State& state) {
    auto dir = MMapDirectory::open(test_dir);

    uint8_t buffer[4096];
    for (auto _ : state) {
        // Open with SEQUENTIAL advice
        auto input = dir->openInput("large.bin", IOContext(IOContext::Type::MERGE));

        size_t total = 0;
        while (total < LARGE_FILE) {
            size_t to_read = std::min(sizeof(buffer), LARGE_FILE - total);
            input->readBytes(buffer, to_read);
            total += to_read;
            benchmark::DoNotOptimize(buffer);
        }
    }

    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(LARGE_FILE));
}
BENCHMARK(BM_MMapDirectory_SequentialAdvice);

static void BM_MMapDirectory_RandomAdvice(benchmark::State& state) {
    auto dir = MMapDirectory::open(test_dir);

    std::mt19937 rng(12345);
    std::uniform_int_distribution<int64_t> dist(0, MEDIUM_FILE - 1);

    uint8_t value;
    for (auto _ : state) {
        // Open with RANDOM advice
        auto input = dir->openInput("medium.bin", IOContext(IOContext::Type::READ));

        for (int i = 0; i < 1000; ++i) {
            int64_t pos = dist(rng);
            input->seek(pos);
            value = input->readByte();
            benchmark::DoNotOptimize(value);
        }
    }

    state.SetItemsProcessed(state.iterations() * 1000);
}
BENCHMARK(BM_MMapDirectory_RandomAdvice);

// ==================== Preload Benchmarks ====================

static void BM_MMapDirectory_WithPreload(benchmark::State& state) {
    auto dir = MMapDirectory::open(test_dir);
    dir->setPreload(true);

    for (auto _ : state) {
        // Preload happens during openInput
        auto input = dir->openInput("medium.bin", IOContext::DEFAULT);

        // Quick random read
        input->seek(MEDIUM_FILE / 2);
        uint8_t value = input->readByte();
        benchmark::DoNotOptimize(value);
    }
}
BENCHMARK(BM_MMapDirectory_WithPreload);

static void BM_MMapDirectory_WithoutPreload(benchmark::State& state) {
    auto dir = MMapDirectory::open(test_dir);
    dir->setPreload(false);

    for (auto _ : state) {
        // Pages loaded on demand
        auto input = dir->openInput("medium.bin", IOContext::DEFAULT);

        // Quick random read
        input->seek(MEDIUM_FILE / 2);
        uint8_t value = input->readByte();
        benchmark::DoNotOptimize(value);
    }
}
BENCHMARK(BM_MMapDirectory_WithoutPreload);

// ==================== Multi-threaded Read Benchmarks ====================

static void BM_MMapDirectory_ConcurrentReads(benchmark::State& state) {
    static auto dir = MMapDirectory::open(test_dir);
    static auto input = dir->openInput("large.bin", IOContext::DEFAULT);

    // Each thread gets a clone
    auto clone = input->clone();

    std::mt19937 rng(state.thread_index() + 12345);
    std::uniform_int_distribution<int64_t> dist(0, LARGE_FILE - 1);

    uint8_t value;
    for (auto _ : state) {
        int64_t pos = dist(rng);
        clone->seek(pos);
        value = clone->readByte();
        benchmark::DoNotOptimize(value);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MMapDirectory_ConcurrentReads)->Threads(1)->Threads(2)->Threads(4)->Threads(8);

// ==================== Main ====================

int main(int argc, char** argv) {
    SetupBenchmarkEnvironment();

    ::benchmark::Initialize(&argc, argv);
    if (::benchmark::ReportUnrecognizedArguments(argc, argv)) {
        CleanupBenchmarkEnvironment();
        return 1;
    }

    ::benchmark::RunSpecifiedBenchmarks();
    ::benchmark::Shutdown();

    CleanupBenchmarkEnvironment();
    return 0;
}
