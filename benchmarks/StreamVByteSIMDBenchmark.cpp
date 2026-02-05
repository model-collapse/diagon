// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

/**
 * Benchmark for StreamVByte SIMD optimization validation
 *
 * Expected Results (after precomputed lookup table fix):
 * - Decode speed: 4-5 billion ints/sec (28-35× faster than 142M ints/sec baseline)
 * - Expected baseline: 142 M ints/sec (runtime mask generation)
 * - Expected optimized: 4-5 B ints/sec (precomputed tables)
 *
 * Test workload: 1M integers with varying sizes
 */

#include <benchmark/benchmark.h>
#include "diagon/util/StreamVByte.h"

#include <random>
#include <vector>
#include <cstring>

using namespace diagon::util;

// ==================== Test Data Generation ====================

class StreamVByteFixture : public benchmark::Fixture {
public:
    static constexpr int NUM_INTS = 1000000;  // 1M integers
    static constexpr int MAX_ENCODED_SIZE = NUM_INTS * 5;  // Worst case: 5 bytes/int

    std::vector<uint32_t> values_;
    std::vector<uint8_t> encoded_;
    std::vector<uint32_t> decoded_;
    int encodedSize_;

    void SetUp(const ::benchmark::State& state) override {
        // Generate test data with realistic distribution
        std::mt19937 rng(42);

        // Distribution: 60% small (<256), 30% medium (<65536), 10% large
        std::uniform_real_distribution<> dist(0.0, 1.0);
        std::uniform_int_distribution<uint32_t> small(0, 255);
        std::uniform_int_distribution<uint32_t> medium(256, 65535);
        std::uniform_int_distribution<uint32_t> large(65536, 1000000);

        values_.resize(NUM_INTS);
        for (int i = 0; i < NUM_INTS; ++i) {
            double p = dist(rng);
            if (p < 0.6) {
                values_[i] = small(rng);
            } else if (p < 0.9) {
                values_[i] = medium(rng);
            } else {
                values_[i] = large(rng);
            }
        }

        // Encode all values
        encoded_.resize(MAX_ENCODED_SIZE);
        encodedSize_ = 0;

        for (int i = 0; i < NUM_INTS; i += 4) {
            int count = std::min(4, NUM_INTS - i);
            int bytes = StreamVByte::encode(&values_[i], count, &encoded_[encodedSize_]);
            encodedSize_ += bytes;
        }

        decoded_.resize(NUM_INTS);
    }
};

// ==================== Decode Benchmarks ====================

BENCHMARK_DEFINE_F(StreamVByteFixture, Decode_1M_Integers)(benchmark::State& state) {
    for (auto _ : state) {
        // Decode 1M integers
        int bytesRead = StreamVByte::decode(encoded_.data(), NUM_INTS, decoded_.data());
        benchmark::DoNotOptimize(bytesRead);
        benchmark::DoNotOptimize(decoded_.data());
    }

    // Calculate throughput
    double intsPerSec = (NUM_INTS * state.iterations()) /
                        (state.iterations() * state.iterations() /
                         (double)state.iterations());
    state.counters["ints/sec"] = benchmark::Counter(
        intsPerSec * 1e9 / state.iterations(),  // Convert to ints/sec
        benchmark::Counter::kIsRate
    );
    state.counters["bytes/int"] = benchmark::Counter(
        (double)encodedSize_ / NUM_INTS,
        benchmark::Counter::kAvgThreads
    );
}

BENCHMARK_REGISTER_F(StreamVByteFixture, Decode_1M_Integers)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(100);

// ==================== Micro-benchmarks ====================

// Benchmark decode4 (4 integers at a time)
static void BM_StreamVByte_Decode4_SmallInts(benchmark::State& state) {
    // Small integers (1 byte each): 4 bytes data + 1 control = 5 bytes
    uint8_t encoded[17] = {0x00, 10, 20, 30, 40};  // Control=0x00 (all 1-byte)
    uint32_t output[4];

    for (auto _ : state) {
        int bytes = StreamVByte::decode4(encoded, output);
        benchmark::DoNotOptimize(bytes);
        benchmark::DoNotOptimize(output);
    }

    // Expected: 4 ints in ~1-2 nanoseconds (2-4 billion ints/sec)
    state.counters["ints/sec"] = benchmark::Counter(
        4, benchmark::Counter::kIsIterationInvariantRate
    );
}
BENCHMARK(BM_StreamVByte_Decode4_SmallInts);

static void BM_StreamVByte_Decode4_MixedSizes(benchmark::State& state) {
    // Mixed sizes: 1,2,3,4 bytes = 10 bytes data + 1 control = 11 bytes
    uint8_t encoded[17] = {
        0xE4,  // Control: 0b11_10_01_00 = 4,3,2,1 bytes
        10,                              // Int0: 1 byte (10)
        0x20, 0x30,                      // Int1: 2 bytes (12320)
        0x40, 0x50, 0x60,                // Int2: 3 bytes (6312000)
        0x70, 0x80, 0x90, 0xA0           // Int3: 4 bytes (2694918256)
    };
    uint32_t output[4];

    for (auto _ : state) {
        int bytes = StreamVByte::decode4(encoded, output);
        benchmark::DoNotOptimize(bytes);
        benchmark::DoNotOptimize(output);
    }

    state.counters["ints/sec"] = benchmark::Counter(
        4, benchmark::Counter::kIsIterationInvariantRate
    );
}
BENCHMARK(BM_StreamVByte_Decode4_MixedSizes);

// Benchmark decodeBulk (1K integers)
static void BM_StreamVByte_DecodeBulk_1K(benchmark::State& state) {
    constexpr int N = 1024;
    std::vector<uint32_t> values(N);
    std::vector<uint8_t> encoded(N * 5);
    std::vector<uint32_t> decoded(N);

    // Generate small sequential integers (good compression)
    for (int i = 0; i < N; ++i) {
        values[i] = i + 1;
    }

    // Encode
    int encodedSize = 0;
    for (int i = 0; i < N; i += 4) {
        encodedSize += StreamVByte::encode(&values[i], 4, &encoded[encodedSize]);
    }

    for (auto _ : state) {
        int bytes = StreamVByte::decodeBulk(encoded.data(), N, decoded.data());
        benchmark::DoNotOptimize(bytes);
        benchmark::DoNotOptimize(decoded.data());
    }

    state.counters["ints/sec"] = benchmark::Counter(
        N, benchmark::Counter::kIsIterationInvariantRate
    );
}
BENCHMARK(BM_StreamVByte_DecodeBulk_1K);

// ==================== Comparison: Encode vs Decode ====================

static void BM_StreamVByte_Encode_1K(benchmark::State& state) {
    constexpr int N = 1024;
    std::vector<uint32_t> values(N);
    std::vector<uint8_t> encoded(N * 5);

    // Sequential integers
    for (int i = 0; i < N; ++i) {
        values[i] = i + 1;
    }

    for (auto _ : state) {
        int encodedSize = 0;
        for (int i = 0; i < N; i += 4) {
            encodedSize += StreamVByte::encode(&values[i], 4, &encoded[encodedSize]);
        }
        benchmark::DoNotOptimize(encodedSize);
    }

    state.counters["ints/sec"] = benchmark::Counter(
        N, benchmark::Counter::kIsIterationInvariantRate
    );
}
BENCHMARK(BM_StreamVByte_Encode_1K);

// ==================== Main ====================

BENCHMARK_MAIN();
