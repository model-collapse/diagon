// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

// Benchmark: Compare original vs optimized posting list decoding

#include "diagon/codecs/lucene104/Lucene104PostingsReader.h"
#include "diagon/codecs/lucene104/Lucene104PostingsReaderOptimized.h"
#include "diagon/codecs/lucene104/Lucene104PostingsWriter.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/index/PostingsEnum.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/search/DocIdSetIterator.h"
#include "diagon/store/ByteBuffersIndexInput.h"
#include "diagon/store/ByteBuffersIndexOutput.h"
#include "diagon/util/StreamVByte.h"

#include <benchmark/benchmark.h>

#include <random>
#include <vector>

using namespace diagon::codecs::lucene104;
using namespace diagon::index;
using namespace diagon::search;
using namespace diagon::store;
using namespace diagon::util;

// ==================== Helper Functions ====================

// Write posting list data in StreamVByte format
void writePostingList(ByteBuffersIndexOutput& out, const std::vector<int>& docDeltas,
                      const std::vector<int>& freqs) {
    size_t numDocs = docDeltas.size();
    size_t pos = 0;

    // Write full groups of 4 using StreamVByte
    while (pos + 4 <= numDocs) {
        uint32_t docGroup[4];
        uint32_t freqGroup[4];

        for (int i = 0; i < 4; ++i) {
            docGroup[i] = static_cast<uint32_t>(docDeltas[pos + i]);
            freqGroup[i] = static_cast<uint32_t>(freqs[pos + i]);
        }

        uint8_t docEncoded[17];
        int docBytes = StreamVByte::encode(docGroup, 4, docEncoded);
        out.writeBytes(docEncoded, docBytes);

        uint8_t freqEncoded[17];
        int freqBytes = StreamVByte::encode(freqGroup, 4, freqEncoded);
        out.writeBytes(freqEncoded, freqBytes);

        pos += 4;
    }

    // Write remaining docs using VInt
    while (pos < numDocs) {
        out.writeVInt(docDeltas[pos]);
        out.writeVInt(freqs[pos]);
        pos++;
    }
}

// Generate realistic posting list data
struct PostingListData {
    std::vector<int> docDeltas;
    std::vector<int> freqs;
    int64_t totalTermFreq;
};

PostingListData generatePostingList(int numDocs, int avgDocDelta = 10) {
    PostingListData data;
    data.docDeltas.reserve(numDocs);
    data.freqs.reserve(numDocs);

    std::mt19937 rng(42);  // Fixed seed for reproducibility
    std::uniform_int_distribution<int> deltaDist(1, avgDocDelta * 2);
    std::uniform_int_distribution<int> freqDist(1, 5);

    data.totalTermFreq = 0;
    for (int i = 0; i < numDocs; ++i) {
        data.docDeltas.push_back(deltaDist(rng));
        int freq = freqDist(rng);
        data.freqs.push_back(freq);
        data.totalTermFreq += freq;
    }

    return data;
}

// ==================== Helper Functions for Segment State ====================

SegmentWriteState createWriteState() {
    std::vector<FieldInfo> fields;
    FieldInfos fieldInfos(fields);
    return SegmentWriteState(nullptr, "bench", 100000, fieldInfos, "");
}

SegmentReadState createReadState() {
    std::vector<FieldInfo> fields;
    FieldInfos fieldInfos(fields);
    return SegmentReadState(nullptr, "bench", 100000, fieldInfos, "");
}

FieldInfo createField(const std::string& name, IndexOptions options) {
    FieldInfo field;
    field.name = name;
    field.number = 0;
    field.indexOptions = options;
    field.storeTermVector = false;
    field.omitNorms = false;
    field.storePayloads = false;
    field.docValuesType = DocValuesType::NONE;
    field.dvGen = -1;
    return field;
}

// ==================== Benchmark: Original Implementation ====================

static void BM_PostingsDecode_Original(benchmark::State& state) {
    int numDocs = state.range(0);
    auto data = generatePostingList(numDocs);

    // Write posting list
    ByteBuffersIndexOutput out("bench.doc");
    writePostingList(out, data.docDeltas, data.freqs);
    std::vector<uint8_t> encodedData = out.toArrayCopy();

    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

    // Benchmark decoding
    for (auto _ : state) {
        // Create reader
        auto readState = createReadState();
        Lucene104PostingsReader reader(readState);
        reader.setInput(std::make_unique<ByteBuffersIndexInput>("bench.doc", encodedData));

        TermState termState;
        termState.docStartFP = 0;
        termState.docFreq = numDocs;
        termState.totalTermFreq = data.totalTermFreq;

        auto postings = reader.postings(field, termState);

        // Iterate through all docs
        int count = 0;
        while (postings->nextDoc() != DocIdSetIterator::NO_MORE_DOCS) {
            benchmark::DoNotOptimize(postings->docID());
            benchmark::DoNotOptimize(postings->freq());
            count++;
        }
        benchmark::DoNotOptimize(count);
    }

    state.SetItemsProcessed(state.iterations() * numDocs);
    state.SetBytesProcessed(state.iterations() * encodedData.size());
}

// ==================== Benchmark: Optimized Implementation ====================

static void BM_PostingsDecode_Optimized(benchmark::State& state) {
    int numDocs = state.range(0);
    auto data = generatePostingList(numDocs);

    // Write posting list
    ByteBuffersIndexOutput out("bench.doc");
    writePostingList(out, data.docDeltas, data.freqs);
    std::vector<uint8_t> encodedData = out.toArrayCopy();

    // Benchmark decoding with optimized implementation
    for (auto _ : state) {
        // Create input for this iteration
        ByteBuffersIndexInput input("bench.doc", encodedData);

        TermState termState;
        termState.docStartFP = 0;
        termState.docFreq = numDocs;
        termState.totalTermFreq = data.totalTermFreq;

        // Directly construct optimized PostingsEnum
        Lucene104PostingsEnumOptimized postings(&input, termState, true);

        // Iterate through all docs
        int count = 0;
        while (postings.nextDoc() != DocIdSetIterator::NO_MORE_DOCS) {
            benchmark::DoNotOptimize(postings.docID());
            benchmark::DoNotOptimize(postings.freq());
            count++;
        }
        benchmark::DoNotOptimize(count);
    }

    state.SetItemsProcessed(state.iterations() * numDocs);
    state.SetBytesProcessed(state.iterations() * encodedData.size());
}

// ==================== Register Benchmarks ====================

// Test with various posting list sizes
BENCHMARK(BM_PostingsDecode_Original)
    ->Arg(100)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK(BM_PostingsDecode_Optimized)
    ->Arg(100)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
