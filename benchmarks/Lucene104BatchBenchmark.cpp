// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

/**
 * Standalone benchmark for Lucene104PostingsEnumBatch
 *
 * Tests batch postings decoder performance with real StreamVByte encoded data.
 * Bypasses DocumentsWriterPerThread/SegmentReader to directly test codec performance.
 */

#include "diagon/codecs/lucene104/Lucene104PostingsEnumBatch.h"
#include "diagon/codecs/lucene104/Lucene104PostingsReader.h"
#include "diagon/codecs/lucene104/Lucene104PostingsWriter.h"
#include "diagon/index/BatchPostingsEnum.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/store/ByteBuffersIndexInput.h"
#include "diagon/store/ByteBuffersIndexOutput.h"

#include <benchmark/benchmark.h>

#include <random>
#include <vector>

using namespace diagon;
using namespace diagon::codecs::lucene104;
using namespace diagon::index;
using namespace diagon::store;

namespace {

/**
 * Create test postings data in memory using Lucene104PostingsWriter
 */
std::unique_ptr<ByteBuffersIndexInput> createTestPostings(int numDocs, int avgFreq) {
    // Create in-memory output
    auto output = std::make_unique<ByteBuffersIndexOutput>("test.doc");

    // Write postings using Lucene104 format (StreamVByte encoded)
    // For this benchmark, we'll manually write the StreamVByte format
    // to avoid needing full SegmentWriteState setup

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> freqDist(1, avgFreq * 2);

    // Write groups of 4 documents (StreamVByte group size)
    for (int group = 0; group < numDocs / 4; group++) {
        // Encode 4 doc deltas
        std::vector<uint32_t> docDeltas(4);
        for (int i = 0; i < 4; i++) {
            docDeltas[i] = (group == 0 && i == 0) ? (i + 1)
                                                  : 1;  // First doc absolute, rest delta=1
        }

        // StreamVByte encode doc deltas
        uint8_t controlByte = 0;
        std::vector<uint8_t> dataBytes;
        for (int i = 0; i < 4; i++) {
            uint32_t val = docDeltas[i];
            int length = 0;
            if (val < 256) {
                length = 0;  // 1 byte
                dataBytes.push_back(val & 0xFF);
            } else if (val < 65536) {
                length = 1;  // 2 bytes
                dataBytes.push_back(val & 0xFF);
                dataBytes.push_back((val >> 8) & 0xFF);
            } else if (val < 16777216) {
                length = 2;  // 3 bytes
                dataBytes.push_back(val & 0xFF);
                dataBytes.push_back((val >> 8) & 0xFF);
                dataBytes.push_back((val >> 16) & 0xFF);
            } else {
                length = 3;  // 4 bytes
                dataBytes.push_back(val & 0xFF);
                dataBytes.push_back((val >> 8) & 0xFF);
                dataBytes.push_back((val >> 16) & 0xFF);
                dataBytes.push_back((val >> 24) & 0xFF);
            }
            controlByte |= (length << (i * 2));
        }

        // Write control byte and data bytes
        output->writeByte(controlByte);
        output->writeBytes(dataBytes.data(), dataBytes.size());

        // Encode 4 frequencies
        std::vector<uint32_t> freqs(4);
        for (int i = 0; i < 4; i++) {
            freqs[i] = freqDist(rng);
        }

        // StreamVByte encode frequencies
        controlByte = 0;
        dataBytes.clear();
        for (int i = 0; i < 4; i++) {
            uint32_t val = freqs[i];
            int length = 0;
            if (val < 256) {
                length = 0;
                dataBytes.push_back(val & 0xFF);
            } else if (val < 65536) {
                length = 1;
                dataBytes.push_back(val & 0xFF);
                dataBytes.push_back((val >> 8) & 0xFF);
            } else if (val < 16777216) {
                length = 2;
                dataBytes.push_back(val & 0xFF);
                dataBytes.push_back((val >> 8) & 0xFF);
                dataBytes.push_back((val >> 16) & 0xFF);
            } else {
                length = 3;
                dataBytes.push_back(val & 0xFF);
                dataBytes.push_back((val >> 8) & 0xFF);
                dataBytes.push_back((val >> 16) & 0xFF);
                dataBytes.push_back((val >> 24) & 0xFF);
            }
            controlByte |= (length << (i * 2));
        }

        output->writeByte(controlByte);
        output->writeBytes(dataBytes.data(), dataBytes.size());
    }

    // Convert to input by getting the bytes from output
    const auto& bytes = output->toArrayCopy();
    return std::make_unique<ByteBuffersIndexInput>("test.doc", bytes);
}

}  // namespace

/**
 * Benchmark one-at-a-time iteration (baseline)
 */
static void BM_Lucene104_OneAtATime(benchmark::State& state) {
    int numDocs = state.range(0);

    // Create test postings
    auto input = createTestPostings(numDocs, 5);

    // Create term state
    TermState termState;
    termState.docFreq = numDocs;
    termState.totalTermFreq = numDocs * 5;
    termState.docStartFP = 0;

    for (auto _ : state) {
        // Clone input for each iteration (enum takes ownership)
        input->seek(0);
        auto clonedInput = input->clone();

        // Create regular one-at-a-time enum directly
        Lucene104PostingsEnum regularEnum(std::move(clonedInput), termState, true);

        // Iterate one-at-a-time (standard approach)
        int docsScored = 0;
        while (regularEnum.nextDoc() != index::PostingsEnum::NO_MORE_DOCS) {
            int docID = regularEnum.docID();
            int freq = regularEnum.freq();
            docsScored++;
            benchmark::DoNotOptimize(docID);
            benchmark::DoNotOptimize(freq);
        }

        benchmark::DoNotOptimize(docsScored);
    }

    state.SetItemsProcessed(state.iterations() * numDocs);
}

/**
 * Benchmark batch-at-a-time iteration (P1.1)
 */
static void BM_Lucene104_BatchAtATime(benchmark::State& state) {
    int numDocs = state.range(0);

    // Create test postings
    auto input = createTestPostings(numDocs, 5);

    // Create term state
    TermState termState;
    termState.docFreq = numDocs;
    termState.totalTermFreq = numDocs * 5;
    termState.docStartFP = 0;

    for (auto _ : state) {
        // Clone input for each iteration (enum takes ownership)
        input->seek(0);
        auto clonedInput = input->clone();

        // Create batch enum directly
        Lucene104PostingsEnumBatch batchEnum(std::move(clonedInput), termState, true);

        // Simulate batch iteration (8 docs at a time)
        PostingsBatch batch(8);
        int docsScored = 0;
        while (true) {
            int count = batchEnum.nextBatch(batch);
            if (count == 0)
                break;
            docsScored += count;
        }

        benchmark::DoNotOptimize(docsScored);
    }

    state.SetItemsProcessed(state.iterations() * numDocs);
}

// Register benchmarks
BENCHMARK(BM_Lucene104_OneAtATime)->Arg(1000)->Arg(10000);
BENCHMARK(BM_Lucene104_BatchAtATime)->Arg(1000)->Arg(10000);

BENCHMARK_MAIN();
