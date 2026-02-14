// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

// Benchmark: StreamVByte vs VInt for posting list decoding

#include "diagon/codecs/lucene104/Lucene104PostingsReader.h"
#include "diagon/codecs/lucene104/Lucene104PostingsWriter.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/index/SegmentWriteState.h"
#include "diagon/store/ByteBuffersIndexInput.h"
#include "diagon/store/ByteBuffersIndexOutput.h"
#include "diagon/util/StreamVByte.h"

#include <benchmark/benchmark.h>

#include <random>
#include <vector>

using namespace diagon::codecs::lucene104;
using namespace diagon::index;
using namespace diagon::store;
using namespace diagon::util;

// ==================== Helper Functions ====================

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

// Write postings in StreamVByte format (current)
void writeStreamVByteFormat(ByteBuffersIndexOutput& out, const std::vector<int>& docDeltas,
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

// Write postings in old VInt format (baseline)
void writeVIntFormat(ByteBuffersIndexOutput& out, const std::vector<int>& docDeltas,
                     const std::vector<int>& freqs) {
    for (size_t i = 0; i < docDeltas.size(); ++i) {
        out.writeVInt(docDeltas[i]);
        out.writeVInt(freqs[i]);
    }
}

// Generate synthetic posting list data
struct PostingListData {
    std::vector<int> docDeltas;
    std::vector<int> freqs;
    int64_t totalTermFreq;
};

PostingListData generatePostingList(int numDocs, int avgFreq = 5, int seed = 42) {
    PostingListData data;
    data.docDeltas.reserve(numDocs);
    data.freqs.reserve(numDocs);

    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> deltaDist(1, 10);  // Small deltas (clustered)
    std::uniform_int_distribution<int> freqDist(1, avgFreq * 2);

    data.totalTermFreq = 0;

    // First doc
    data.docDeltas.push_back(0);
    int freq = freqDist(rng);
    data.freqs.push_back(freq);
    data.totalTermFreq += freq;

    // Remaining docs
    for (int i = 1; i < numDocs; ++i) {
        data.docDeltas.push_back(deltaDist(rng));
        freq = freqDist(rng);
        data.freqs.push_back(freq);
        data.totalTermFreq += freq;
    }

    return data;
}

// ==================== Benchmarks ====================

// Benchmark: Decode posting list with StreamVByte (current implementation)
static void BM_PostingsDecode_StreamVByte(benchmark::State& state) {
    int numDocs = state.range(0);
    auto data = generatePostingList(numDocs);

    // Pre-encode data
    ByteBuffersIndexOutput out("bench.doc");
    writeStreamVByteFormat(out, data.docDeltas, data.freqs);
    std::vector<uint8_t> encodedData = out.toArrayCopy();

    auto field = createField("content", IndexOptions::DOCS_AND_FREQS);

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
        while (postings->nextDoc() != PostingsEnum::NO_MORE_DOCS) {
            benchmark::DoNotOptimize(postings->docID());
            benchmark::DoNotOptimize(postings->freq());
            count++;
        }

        benchmark::DoNotOptimize(count);
    }

    state.SetItemsProcessed(state.iterations() * numDocs);
    state.SetBytesProcessed(state.iterations() * encodedData.size());
}

// Benchmark: Decode posting list with VInt (baseline - for comparison)
static void BM_PostingsDecode_VInt_Baseline(benchmark::State& state) {
    int numDocs = state.range(0);
    auto data = generatePostingList(numDocs);

    // Encode using old VInt format
    ByteBuffersIndexOutput out("bench.doc");
    writeVIntFormat(out, data.docDeltas, data.freqs);
    std::vector<uint8_t> encodedData = out.toArrayCopy();

    for (auto _ : state) {
        // Manually decode VInts (simulating old reader)
        ByteBuffersIndexInput input("bench.doc", encodedData);

        int count = 0;
        for (int i = 0; i < numDocs; ++i) {
            int docDelta = input.readVInt();
            int freq = input.readVInt();
            benchmark::DoNotOptimize(docDelta);
            benchmark::DoNotOptimize(freq);
            count++;
        }

        benchmark::DoNotOptimize(count);
    }

    state.SetItemsProcessed(state.iterations() * numDocs);
    state.SetBytesProcessed(state.iterations() * encodedData.size());
}

// Benchmark: Encode posting list with StreamVByte
static void BM_PostingsEncode_StreamVByte(benchmark::State& state) {
    int numDocs = state.range(0);
    auto data = generatePostingList(numDocs);

    for (auto _ : state) {
        ByteBuffersIndexOutput out("bench.doc");
        writeStreamVByteFormat(out, data.docDeltas, data.freqs);
        auto encoded = out.toArrayCopy();
        benchmark::DoNotOptimize(encoded);
    }

    state.SetItemsProcessed(state.iterations() * numDocs);
}

// Benchmark: Encode posting list with VInt
static void BM_PostingsEncode_VInt_Baseline(benchmark::State& state) {
    int numDocs = state.range(0);
    auto data = generatePostingList(numDocs);

    for (auto _ : state) {
        ByteBuffersIndexOutput out("bench.doc");
        writeVIntFormat(out, data.docDeltas, data.freqs);
        auto encoded = out.toArrayCopy();
        benchmark::DoNotOptimize(encoded);
    }

    state.SetItemsProcessed(state.iterations() * numDocs);
}

// Benchmark: Pure StreamVByte decode throughput (no reader overhead)
static void BM_StreamVByte_Decode_Raw(benchmark::State& state) {
    int numGroups = state.range(0);  // Number of 4-doc groups

    // Prepare encoded data
    std::vector<uint8_t> encodedData;
    encodedData.reserve(numGroups * 10);  // Rough estimate

    for (int g = 0; g < numGroups; ++g) {
        uint32_t values[4] = {1, 2, 3, 4};
        uint8_t encoded[17];
        int bytes = StreamVByte::encode(values, 4, encoded);
        encodedData.insert(encodedData.end(), encoded, encoded + bytes);
    }

    for (auto _ : state) {
        uint32_t decoded[4];
        size_t pos = 0;

        for (int g = 0; g < numGroups; ++g) {
            StreamVByte::decode4(encodedData.data() + pos, decoded);
            benchmark::DoNotOptimize(decoded);

            // Calculate bytes consumed (simplified)
            uint8_t controlByte = encodedData[pos];
            int dataBytes = 0;
            for (int i = 0; i < 4; ++i) {
                dataBytes += ((controlByte >> (i * 2)) & 0x03) + 1;
            }
            pos += 1 + dataBytes;
        }
    }

    state.SetItemsProcessed(state.iterations() * numGroups * 4);
    state.SetBytesProcessed(state.iterations() * encodedData.size());
}

// Benchmark: Pure VInt decode throughput (no reader overhead)
static void BM_VInt_Decode_Raw(benchmark::State& state) {
    int numValues = state.range(0) * 4;  // Match StreamVByte group count

    // Prepare encoded data
    ByteBuffersIndexOutput out("raw.vint");
    for (int i = 0; i < numValues; ++i) {
        out.writeVInt(i % 100);  // Small values (1-2 bytes)
    }
    std::vector<uint8_t> encodedData = out.toArrayCopy();

    for (auto _ : state) {
        ByteBuffersIndexInput input("raw.vint", encodedData);

        for (int i = 0; i < numValues; ++i) {
            int value = input.readVInt();
            benchmark::DoNotOptimize(value);
        }
    }

    state.SetItemsProcessed(state.iterations() * numValues);
    state.SetBytesProcessed(state.iterations() * encodedData.size());
}

// ==================== Benchmark Registration ====================

// Posting list decode benchmarks (with reader overhead)
BENCHMARK(BM_PostingsDecode_StreamVByte)
    ->Arg(100)     // 100 docs (25 StreamVByte groups)
    ->Arg(1000)    // 1K docs
    ->Arg(10000)   // 10K docs
    ->Arg(100000)  // 100K docs
    ->Unit(benchmark::kMicrosecond);

BENCHMARK(BM_PostingsDecode_VInt_Baseline)
    ->Arg(100)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->Unit(benchmark::kMicrosecond);

// Posting list encode benchmarks
BENCHMARK(BM_PostingsEncode_StreamVByte)
    ->Arg(100)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK(BM_PostingsEncode_VInt_Baseline)
    ->Arg(100)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->Unit(benchmark::kMicrosecond);

// Raw decode benchmarks (no reader overhead)
BENCHMARK(BM_StreamVByte_Decode_Raw)
    ->Arg(25)     // 100 integers
    ->Arg(250)    // 1K integers
    ->Arg(2500)   // 10K integers
    ->Arg(25000)  // 100K integers
    ->Unit(benchmark::kMicrosecond);

BENCHMARK(BM_VInt_Decode_Raw)
    ->Arg(25)
    ->Arg(250)
    ->Arg(2500)
    ->Arg(25000)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
