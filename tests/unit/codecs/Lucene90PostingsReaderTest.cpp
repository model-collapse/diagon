// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include <gtest/gtest.h>

#include "diagon/codecs/lucene90/Lucene90PostingsReader.h"
#include "diagon/codecs/lucene90/Lucene90ForUtil.h"
#include "diagon/codecs/lucene90/Lucene90PForUtil.h"
#include "diagon/codecs/lucene104/Lucene104PostingsWriter.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/store/ByteBuffersIndexInput.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <vector>

using namespace diagon;
using namespace diagon::codecs::lucene90;

namespace {

// ==================== Test Encoder ====================
// Reused from Lucene90ForUtilTest to produce Lucene90-format PFOR blocks.

class Lucene90TestEncoder {
public:
    static void writeLongBE(std::vector<uint8_t>& buf, int64_t val) {
        for (int i = 56; i >= 0; i -= 8) {
            buf.push_back(static_cast<uint8_t>(
                (static_cast<uint64_t>(val) >> i) & 0xFF));
        }
    }

    static void writeVLong(std::vector<uint8_t>& buf, int64_t v) {
        uint64_t val = static_cast<uint64_t>(v);
        while (val > 0x7F) {
            buf.push_back(static_cast<uint8_t>((val & 0x7F) | 0x80));
            val >>= 7;
        }
        buf.push_back(static_cast<uint8_t>(val));
    }

    static void writeVInt(std::vector<uint8_t>& buf, int32_t v) {
        uint32_t val = static_cast<uint32_t>(v);
        while (val > 0x7F) {
            buf.push_back(static_cast<uint8_t>((val & 0x7F) | 0x80));
            val >>= 7;
        }
        buf.push_back(static_cast<uint8_t>(val));
    }

    static std::vector<uint8_t> encodeForUtil(const int64_t* values, int bpv) {
        int64_t longs[128];
        int64_t tmp[64] = {};
        std::memcpy(longs, values, 128 * sizeof(int64_t));

        int nextPrimitive;
        if (bpv <= 8) {
            nextPrimitive = 8;
            collapse8(longs);
        } else if (bpv <= 16) {
            nextPrimitive = 16;
            collapse16(longs);
        } else {
            nextPrimitive = 32;
            collapse32(longs);
        }

        const int numLongsPerShift = bpv * 2;
        int numLongs;
        if (nextPrimitive == 8) numLongs = 16;
        else if (nextPrimitive == 16) numLongs = 32;
        else numLongs = 64;

        int idx = 0;
        int shift = nextPrimitive - bpv;
        for (int i = 0; i < numLongsPerShift; ++i) {
            tmp[i] = longs[idx++] << shift;
        }
        for (shift = shift - bpv; shift >= 0; shift -= bpv) {
            for (int i = 0; i < numLongsPerShift; ++i) {
                tmp[i] |= longs[idx++] << shift;
            }
        }

        const int remainingBitsPerLong = shift + bpv;
        if (remainingBitsPerLong > 0 && idx < numLongs) {
            const int64_t maskRemainingBitsPerLong = getMask(remainingBitsPerLong, nextPrimitive);
            int tmpIdx = 0;
            int remainingBitsPerValue = bpv;
            while (idx < numLongs) {
                if (remainingBitsPerValue >= remainingBitsPerLong) {
                    remainingBitsPerValue -= remainingBitsPerLong;
                    tmp[tmpIdx++] |= (static_cast<uint64_t>(longs[idx]) >> remainingBitsPerValue)
                                     & static_cast<uint64_t>(maskRemainingBitsPerLong);
                    if (remainingBitsPerValue == 0) {
                        idx++;
                        remainingBitsPerValue = bpv;
                    }
                } else {
                    const int64_t mask1 = getMask(remainingBitsPerValue, nextPrimitive);
                    const int64_t mask2 = getMask(remainingBitsPerLong - remainingBitsPerValue,
                                                   nextPrimitive);
                    tmp[tmpIdx] |= (longs[idx++] & mask1)
                                    << (remainingBitsPerLong - remainingBitsPerValue);
                    remainingBitsPerValue = bpv - remainingBitsPerLong + remainingBitsPerValue;
                    tmp[tmpIdx++] |= (static_cast<uint64_t>(longs[idx]) >> remainingBitsPerValue)
                                     & static_cast<uint64_t>(mask2);
                }
            }
        }

        std::vector<uint8_t> out;
        out.reserve(numLongsPerShift * 8);
        for (int i = 0; i < numLongsPerShift; ++i) {
            writeLongBE(out, tmp[i]);
        }
        return out;
    }

    static std::vector<uint8_t> encodePFor(const int64_t* values, int bpv, int numExceptions,
                                            const int* exceptionPositions,
                                            const int64_t* exceptionHighBits) {
        std::vector<uint8_t> out;
        const uint8_t token = static_cast<uint8_t>((numExceptions << 5) | bpv);
        out.push_back(token);

        if (bpv == 0) {
            writeVLong(out, values[0]);
        } else {
            int64_t masked[128];
            std::memcpy(masked, values, 128 * sizeof(int64_t));
            const int64_t maxUnpatched = (1LL << bpv) - 1;
            for (int i = 0; i < numExceptions; ++i) {
                masked[exceptionPositions[i]] &= maxUnpatched;
            }
            auto packed = encodeForUtil(masked, bpv);
            out.insert(out.end(), packed.begin(), packed.end());
        }

        for (int i = 0; i < numExceptions; ++i) {
            out.push_back(static_cast<uint8_t>(exceptionPositions[i]));
            out.push_back(static_cast<uint8_t>(exceptionHighBits[i]));
        }
        return out;
    }

private:
    static int64_t expandMask32(int64_t m) { return m | (m << 32); }
    static int64_t expandMask16(int64_t m) { return expandMask32(m | (m << 16)); }
    static int64_t expandMask8(int64_t m) { return expandMask16(m | (m << 8)); }
    static int64_t mask32(int bits) { return bits == 0 ? 0 : expandMask32((1LL << bits) - 1); }
    static int64_t mask16(int bits) { return bits == 0 ? 0 : expandMask16((1LL << bits) - 1); }
    static int64_t mask8(int bits) { return bits == 0 ? 0 : expandMask8((1LL << bits) - 1); }
    static int64_t getMask(int bits, int primSize) {
        if (primSize == 8) return mask8(bits);
        if (primSize == 16) return mask16(bits);
        return mask32(bits);
    }

    static void collapse8(int64_t* arr) {
        for (int i = 0; i < 16; ++i) {
            arr[i] = (arr[i] << 56) | (arr[16 + i] << 48) | (arr[32 + i] << 40)
                     | (arr[48 + i] << 32) | (arr[64 + i] << 24) | (arr[80 + i] << 16)
                     | (arr[96 + i] << 8) | arr[112 + i];
        }
    }

    static void collapse16(int64_t* arr) {
        for (int i = 0; i < 32; ++i) {
            arr[i] = (arr[i] << 48) | (arr[32 + i] << 32)
                     | (arr[64 + i] << 16) | arr[96 + i];
        }
    }

    static void collapse32(int64_t* arr) {
        for (int i = 0; i < 64; ++i) {
            arr[i] = (arr[i] << 32) | arr[64 + i];
        }
    }
};

// ==================== Postings Writer Helpers ====================
// Writes Lucene90-format postings data to a byte buffer for testing.

class Lucene90PostingsTestWriter {
public:
    // Write a complete .doc file for the given doc IDs and freqs.
    // Uses PFOR blocks for full blocks, VInt tail for remainder.
    // Returns the byte buffer.
    static std::vector<uint8_t> writeDocFile(const std::vector<int>& docIDs,
                                              const std::vector<int>& freqs,
                                              bool indexHasFreq) {
        std::vector<uint8_t> buf;
        const int BLOCK_SIZE = 128;
        int numDocs = static_cast<int>(docIDs.size());
        int blockCount = numDocs / BLOCK_SIZE;
        int tailCount = numDocs % BLOCK_SIZE;

        int lastDocID = 0;
        int idx = 0;

        // Write full PFOR blocks
        for (int b = 0; b < blockCount; ++b) {
            // Compute doc deltas
            int64_t docDeltas[128];
            for (int i = 0; i < BLOCK_SIZE; ++i) {
                docDeltas[i] = docIDs[idx + i] - lastDocID;
                lastDocID = docIDs[idx + i];
            }

            // Determine bpv for doc deltas
            int maxDelta = 0;
            for (int i = 0; i < BLOCK_SIZE; ++i) {
                if (docDeltas[i] > maxDelta) maxDelta = static_cast<int>(docDeltas[i]);
            }
            int bpv = bitsRequired(maxDelta);
            if (bpv == 0) bpv = 1;  // PForUtil needs at least bpv=1 for non-zero

            // Encode doc deltas as PFOR block
            auto docBlock = Lucene90TestEncoder::encodePFor(docDeltas, bpv, 0, nullptr, nullptr);
            buf.insert(buf.end(), docBlock.begin(), docBlock.end());

            // Encode freq block (separate PFOR)
            if (indexHasFreq) {
                int64_t freqValues[128];
                for (int i = 0; i < BLOCK_SIZE; ++i) {
                    freqValues[i] = freqs[idx + i];
                }
                int maxFreq = 0;
                for (int i = 0; i < BLOCK_SIZE; ++i) {
                    if (freqValues[i] > maxFreq) maxFreq = static_cast<int>(freqValues[i]);
                }
                int freqBpv = bitsRequired(maxFreq);
                if (freqBpv == 0) freqBpv = 1;
                auto freqBlock = Lucene90TestEncoder::encodePFor(freqValues, freqBpv, 0, nullptr, nullptr);
                buf.insert(buf.end(), freqBlock.begin(), freqBlock.end());
            }

            idx += BLOCK_SIZE;
        }

        // Write VInt tail
        if (tailCount > 0) {
            for (int i = 0; i < tailCount; ++i) {
                int docDelta = docIDs[idx + i] - lastDocID;
                lastDocID = docIDs[idx + i];

                if (indexHasFreq) {
                    int freq = freqs[idx + i];
                    if (freq == 1) {
                        Lucene90TestEncoder::writeVInt(buf, (docDelta << 1) | 1);
                    } else {
                        Lucene90TestEncoder::writeVInt(buf, docDelta << 1);
                        Lucene90TestEncoder::writeVInt(buf, freq);
                    }
                } else {
                    Lucene90TestEncoder::writeVInt(buf, docDelta);
                }
            }
        }

        return buf;
    }

    // Write a .pos file for position data.
    // positionsPerDoc[d] = list of positions for doc d.
    static std::vector<uint8_t> writePosFile(
            const std::vector<std::vector<int>>& positionsPerDoc,
            int64_t& lastPosBlockOffsetOut) {
        std::vector<uint8_t> buf;
        const int BLOCK_SIZE = 128;

        // Flatten all positions into deltas
        std::vector<int64_t> allPosDeltas;
        for (const auto& positions : positionsPerDoc) {
            int lastPos = 0;
            for (int pos : positions) {
                allPosDeltas.push_back(pos - lastPos);
                lastPos = pos;
            }
        }

        int totalPositions = static_cast<int>(allPosDeltas.size());
        int blockCount = totalPositions / BLOCK_SIZE;
        int tailCount = totalPositions % BLOCK_SIZE;

        lastPosBlockOffsetOut = -1;

        int idx = 0;
        for (int b = 0; b < blockCount; ++b) {
            int64_t block[128];
            for (int i = 0; i < BLOCK_SIZE; ++i) {
                block[i] = allPosDeltas[idx + i];
            }

            int maxVal = 0;
            for (int i = 0; i < BLOCK_SIZE; ++i) {
                if (block[i] > maxVal) maxVal = static_cast<int>(block[i]);
            }
            int bpv = bitsRequired(maxVal);
            if (bpv == 0) bpv = 1;

            auto posBlock = Lucene90TestEncoder::encodePFor(block, bpv, 0, nullptr, nullptr);
            buf.insert(buf.end(), posBlock.begin(), posBlock.end());
            idx += BLOCK_SIZE;
        }

        // VInt tail
        if (tailCount > 0) {
            lastPosBlockOffsetOut = static_cast<int64_t>(buf.size());
            for (int i = 0; i < tailCount; ++i) {
                Lucene90TestEncoder::writeVInt(buf, static_cast<int32_t>(allPosDeltas[idx + i]));
            }
        }

        return buf;
    }

    static int bitsRequired(int value) {
        if (value <= 0) return 0;
        return 32 - __builtin_clz(static_cast<unsigned>(value));
    }
};

// Helper to create a FieldInfo for testing
index::FieldInfo makeFieldInfo(const std::string& name, int number,
                               index::IndexOptions options) {
    index::FieldInfo fi(name, number);
    fi.indexOptions = options;
    fi.omitNorms = true;
    return fi;
}

}  // namespace

// ==================== Lucene90PostingsReader Tests ====================

class Lucene90PostingsReaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        fieldDocsAndFreqs_ = makeFieldInfo("body", 0, index::IndexOptions::DOCS_AND_FREQS);
        fieldDocsOnly_ = makeFieldInfo("id", 1, index::IndexOptions::DOCS);
        fieldDocsFreqsPositions_ = makeFieldInfo("text", 2,
            index::IndexOptions::DOCS_AND_FREQS_AND_POSITIONS);
    }

    index::FieldInfo fieldDocsAndFreqs_;
    index::FieldInfo fieldDocsOnly_;
    index::FieldInfo fieldDocsFreqsPositions_;
};

// Test 1: Singleton doc (docFreq=1)
TEST_F(Lucene90PostingsReaderTest, SingletonDoc) {
    Lucene90TermState ts;
    ts.docFreq = 1;
    ts.totalTermFreq = 5;
    ts.singletonDocID = 42;
    ts.docStartFP = 0;

    // Singleton needs no .doc file data, but we still need a valid IndexInput
    std::vector<uint8_t> emptyBuf = {0};
    auto docIn = std::make_unique<store::ByteBuffersIndexInput>("doc", emptyBuf);

    auto postings = std::make_unique<Lucene90BlockDocsEnum>(
        std::move(docIn), ts, true);

    EXPECT_EQ(postings->nextDoc(), 42);
    EXPECT_EQ(postings->freq(), 5);
    EXPECT_EQ(postings->nextDoc(), index::PostingsEnum::NO_MORE_DOCS);
}

// Test 2: Small posting with VInt tail (docFreq < 128, with freqs)
TEST_F(Lucene90PostingsReaderTest, SmallPostingVIntTailWithFreqs) {
    std::vector<int> docIDs = {3, 10, 25, 50, 100};
    std::vector<int> freqs = {1, 3, 1, 2, 1};

    auto docBuf = Lucene90PostingsTestWriter::writeDocFile(docIDs, freqs, true);
    auto docIn = std::make_unique<store::ByteBuffersIndexInput>("doc", docBuf);

    Lucene90TermState ts;
    ts.docFreq = 5;
    ts.totalTermFreq = 8;
    ts.docStartFP = 0;

    auto postings = std::make_unique<Lucene90BlockDocsEnum>(
        std::move(docIn), ts, true);

    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(postings->nextDoc(), docIDs[i]) << "doc " << i;
        EXPECT_EQ(postings->freq(), freqs[i]) << "freq " << i;
    }
    EXPECT_EQ(postings->nextDoc(), index::PostingsEnum::NO_MORE_DOCS);
}

// Test 3: Small posting VInt tail without freqs
TEST_F(Lucene90PostingsReaderTest, SmallPostingVIntTailNoFreqs) {
    std::vector<int> docIDs = {5, 20, 100, 200, 500};
    std::vector<int> freqs = {1, 1, 1, 1, 1};  // unused

    auto docBuf = Lucene90PostingsTestWriter::writeDocFile(docIDs, freqs, false);
    auto docIn = std::make_unique<store::ByteBuffersIndexInput>("doc", docBuf);

    Lucene90TermState ts;
    ts.docFreq = 5;
    ts.totalTermFreq = 5;
    ts.docStartFP = 0;

    auto postings = std::make_unique<Lucene90BlockDocsEnum>(
        std::move(docIn), ts, false);

    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(postings->nextDoc(), docIDs[i]) << "doc " << i;
        EXPECT_EQ(postings->freq(), 1) << "freq should be 1 for no-freq field";
    }
    EXPECT_EQ(postings->nextDoc(), index::PostingsEnum::NO_MORE_DOCS);
}

// Test 4: Single PFOR block (exactly 128 docs)
TEST_F(Lucene90PostingsReaderTest, SinglePFORBlock) {
    std::vector<int> docIDs(128);
    std::vector<int> freqs(128);
    for (int i = 0; i < 128; ++i) {
        docIDs[i] = (i + 1) * 3;  // 3, 6, 9, ..., 384
        freqs[i] = (i % 5) + 1;   // 1-5
    }

    auto docBuf = Lucene90PostingsTestWriter::writeDocFile(docIDs, freqs, true);
    auto docIn = std::make_unique<store::ByteBuffersIndexInput>("doc", docBuf);

    Lucene90TermState ts;
    ts.docFreq = 128;
    ts.totalTermFreq = 0;
    for (int f : freqs) ts.totalTermFreq += f;
    ts.docStartFP = 0;

    auto postings = std::make_unique<Lucene90BlockDocsEnum>(
        std::move(docIn), ts, true);

    for (int i = 0; i < 128; ++i) {
        EXPECT_EQ(postings->nextDoc(), docIDs[i]) << "doc " << i;
        EXPECT_EQ(postings->freq(), freqs[i]) << "freq " << i;
    }
    EXPECT_EQ(postings->nextDoc(), index::PostingsEnum::NO_MORE_DOCS);
}

// Test 5: Multiple PFOR blocks + VInt tail (300 docs)
TEST_F(Lucene90PostingsReaderTest, MultiplePFORBlocks) {
    const int numDocs = 300;
    std::vector<int> docIDs(numDocs);
    std::vector<int> freqs(numDocs);
    for (int i = 0; i < numDocs; ++i) {
        docIDs[i] = (i + 1) * 2;  // 2, 4, 6, ..., 600
        freqs[i] = (i % 3) + 1;   // 1-3
    }

    auto docBuf = Lucene90PostingsTestWriter::writeDocFile(docIDs, freqs, true);
    auto docIn = std::make_unique<store::ByteBuffersIndexInput>("doc", docBuf);

    Lucene90TermState ts;
    ts.docFreq = numDocs;
    ts.totalTermFreq = 0;
    for (int f : freqs) ts.totalTermFreq += f;
    ts.docStartFP = 0;

    auto postings = std::make_unique<Lucene90BlockDocsEnum>(
        std::move(docIn), ts, true);

    for (int i = 0; i < numDocs; ++i) {
        EXPECT_EQ(postings->nextDoc(), docIDs[i]) << "doc " << i;
        EXPECT_EQ(postings->freq(), freqs[i]) << "freq " << i;
    }
    EXPECT_EQ(postings->nextDoc(), index::PostingsEnum::NO_MORE_DOCS);
}

// Test 6: advance() across block boundaries
TEST_F(Lucene90PostingsReaderTest, AdvancePastBlock) {
    const int numDocs = 300;
    std::vector<int> docIDs(numDocs);
    std::vector<int> freqs(numDocs);
    for (int i = 0; i < numDocs; ++i) {
        docIDs[i] = (i + 1) * 10;  // 10, 20, 30, ..., 3000
        freqs[i] = 1;
    }

    auto docBuf = Lucene90PostingsTestWriter::writeDocFile(docIDs, freqs, true);
    auto docIn = std::make_unique<store::ByteBuffersIndexInput>("doc", docBuf);

    Lucene90TermState ts;
    ts.docFreq = numDocs;
    ts.totalTermFreq = numDocs;
    ts.docStartFP = 0;

    auto postings = std::make_unique<Lucene90BlockDocsEnum>(
        std::move(docIn), ts, true);

    // Advance to doc in first block
    EXPECT_EQ(postings->advance(50), 50);

    // Advance to doc in second block (past 128th doc = 1280)
    EXPECT_EQ(postings->advance(1500), 1500);

    // Advance to doc in VInt tail (past 256th doc = 2560, in tail block starting at doc 2570)
    EXPECT_EQ(postings->advance(2700), 2700);

    // Advance to last doc
    EXPECT_EQ(postings->advance(3000), 3000);

    EXPECT_EQ(postings->nextDoc(), index::PostingsEnum::NO_MORE_DOCS);
}

// Test 7: advance() to non-existent doc (should land on next higher)
TEST_F(Lucene90PostingsReaderTest, AdvanceToNonExistentDoc) {
    std::vector<int> docIDs = {10, 20, 30, 50, 100};
    std::vector<int> freqs = {1, 1, 1, 1, 1};

    auto docBuf = Lucene90PostingsTestWriter::writeDocFile(docIDs, freqs, true);
    auto docIn = std::make_unique<store::ByteBuffersIndexInput>("doc", docBuf);

    Lucene90TermState ts;
    ts.docFreq = 5;
    ts.totalTermFreq = 5;
    ts.docStartFP = 0;

    auto postings = std::make_unique<Lucene90BlockDocsEnum>(
        std::move(docIn), ts, true);

    // Advance to 15, should land on 20
    EXPECT_EQ(postings->advance(15), 20);
    // Advance to 45, should land on 50
    EXPECT_EQ(postings->advance(45), 50);
    // Advance to 101, should be NO_MORE_DOCS
    EXPECT_EQ(postings->advance(101), index::PostingsEnum::NO_MORE_DOCS);
}

// Test 8: PostingsReader creates enum via postings() method
TEST_F(Lucene90PostingsReaderTest, PostingsReaderCreateEnum) {
    std::vector<int> docIDs = {5, 15, 25};
    std::vector<int> freqs = {2, 1, 3};

    auto docBuf = Lucene90PostingsTestWriter::writeDocFile(docIDs, freqs, true);
    auto docIn = std::make_unique<store::ByteBuffersIndexInput>("doc", docBuf);

    // Create reader with mock state
    index::FieldInfos fieldInfos;
    index::SegmentReadState state(nullptr, "test_seg", 100, fieldInfos, "");

    Lucene90PostingsReader reader(state);
    reader.setDocInput(std::move(docIn));

    Lucene90TermState ts;
    ts.docFreq = 3;
    ts.totalTermFreq = 6;
    ts.docStartFP = 0;

    auto postings = reader.postings(fieldDocsAndFreqs_, ts);
    ASSERT_NE(postings, nullptr);

    EXPECT_EQ(postings->nextDoc(), 5);
    EXPECT_EQ(postings->freq(), 2);
    EXPECT_EQ(postings->nextDoc(), 15);
    EXPECT_EQ(postings->freq(), 1);
    EXPECT_EQ(postings->nextDoc(), 25);
    EXPECT_EQ(postings->freq(), 3);
    EXPECT_EQ(postings->nextDoc(), index::PostingsEnum::NO_MORE_DOCS);
}

// Test 9: Position decode (PFOR + VInt tail)
TEST_F(Lucene90PostingsReaderTest, PositionDecode) {
    // 2 docs, each with some positions
    // Doc 0: positions [0, 5, 10] (freq=3)
    // Doc 1: positions [2, 7] (freq=2)
    std::vector<int> docIDs = {10, 20};
    std::vector<int> freqs = {3, 2};
    std::vector<std::vector<int>> posPerDoc = {{0, 5, 10}, {2, 7}};

    auto docBuf = Lucene90PostingsTestWriter::writeDocFile(docIDs, freqs, true);
    int64_t lastPosBlockOffset = -1;
    auto posBuf = Lucene90PostingsTestWriter::writePosFile(posPerDoc, lastPosBlockOffset);

    auto docIn = std::make_unique<store::ByteBuffersIndexInput>("doc", docBuf);
    auto posIn = std::make_unique<store::ByteBuffersIndexInput>("pos", posBuf);

    Lucene90TermState ts;
    ts.docFreq = 2;
    ts.totalTermFreq = 5;
    ts.docStartFP = 0;
    ts.posStartFP = 0;
    ts.lastPosBlockOffset = lastPosBlockOffset;

    auto postings = std::make_unique<Lucene90BlockPosEnum>(
        std::move(docIn), std::move(posIn), ts, true);

    // Doc 0
    EXPECT_EQ(postings->nextDoc(), 10);
    EXPECT_EQ(postings->freq(), 3);
    EXPECT_EQ(postings->nextPosition(), 0);
    EXPECT_EQ(postings->nextPosition(), 5);
    EXPECT_EQ(postings->nextPosition(), 10);

    // Doc 1
    EXPECT_EQ(postings->nextDoc(), 20);
    EXPECT_EQ(postings->freq(), 2);
    EXPECT_EQ(postings->nextPosition(), 2);
    EXPECT_EQ(postings->nextPosition(), 7);

    EXPECT_EQ(postings->nextDoc(), index::PostingsEnum::NO_MORE_DOCS);
}

// Test 10: Position decode with skipped positions (calling nextDoc without consuming all positions)
TEST_F(Lucene90PostingsReaderTest, PositionSkipBetweenDocs) {
    // 3 docs, varying positions
    // Doc 0: positions [0, 3, 6, 9] (freq=4) — we'll only read 1 position
    // Doc 1: positions [1, 4] (freq=2) — we'll skip all positions
    // Doc 2: positions [0, 2, 4] (freq=3) — we'll read all
    std::vector<int> docIDs = {5, 15, 25};
    std::vector<int> freqs = {4, 2, 3};
    std::vector<std::vector<int>> posPerDoc = {{0, 3, 6, 9}, {1, 4}, {0, 2, 4}};

    auto docBuf = Lucene90PostingsTestWriter::writeDocFile(docIDs, freqs, true);
    int64_t lastPosBlockOffset = -1;
    auto posBuf = Lucene90PostingsTestWriter::writePosFile(posPerDoc, lastPosBlockOffset);

    auto docIn = std::make_unique<store::ByteBuffersIndexInput>("doc", docBuf);
    auto posIn = std::make_unique<store::ByteBuffersIndexInput>("pos", posBuf);

    Lucene90TermState ts;
    ts.docFreq = 3;
    ts.totalTermFreq = 9;
    ts.docStartFP = 0;
    ts.posStartFP = 0;
    ts.lastPosBlockOffset = lastPosBlockOffset;

    auto postings = std::make_unique<Lucene90BlockPosEnum>(
        std::move(docIn), std::move(posIn), ts, true);

    // Doc 0: read only first position
    EXPECT_EQ(postings->nextDoc(), 5);
    EXPECT_EQ(postings->freq(), 4);
    EXPECT_EQ(postings->nextPosition(), 0);
    // Skip remaining 3 positions

    // Doc 1: skip all positions
    EXPECT_EQ(postings->nextDoc(), 15);
    EXPECT_EQ(postings->freq(), 2);
    // Don't call nextPosition at all

    // Doc 2: read all positions
    EXPECT_EQ(postings->nextDoc(), 25);
    EXPECT_EQ(postings->freq(), 3);
    EXPECT_EQ(postings->nextPosition(), 0);
    EXPECT_EQ(postings->nextPosition(), 2);
    EXPECT_EQ(postings->nextPosition(), 4);

    EXPECT_EQ(postings->nextDoc(), index::PostingsEnum::NO_MORE_DOCS);
}

// Test 11: PostingsReader postingsWithPositions() via reader
TEST_F(Lucene90PostingsReaderTest, PostingsReaderWithPositions) {
    std::vector<int> docIDs = {10, 20};
    std::vector<int> freqs = {2, 1};
    std::vector<std::vector<int>> posPerDoc = {{0, 5}, {3}};

    auto docBuf = Lucene90PostingsTestWriter::writeDocFile(docIDs, freqs, true);
    int64_t lastPosBlockOffset = -1;
    auto posBuf = Lucene90PostingsTestWriter::writePosFile(posPerDoc, lastPosBlockOffset);

    index::FieldInfos fieldInfos;
    index::SegmentReadState state(nullptr, "test_seg", 100, fieldInfos, "");

    Lucene90PostingsReader reader(state);
    reader.setDocInput(std::make_unique<store::ByteBuffersIndexInput>("doc", docBuf));
    reader.setPosInput(std::make_unique<store::ByteBuffersIndexInput>("pos", posBuf));

    Lucene90TermState ts;
    ts.docFreq = 2;
    ts.totalTermFreq = 3;
    ts.docStartFP = 0;
    ts.posStartFP = 0;
    ts.lastPosBlockOffset = lastPosBlockOffset;

    auto postings = reader.postingsWithPositions(fieldDocsFreqsPositions_, ts);
    ASSERT_NE(postings, nullptr);

    EXPECT_EQ(postings->nextDoc(), 10);
    EXPECT_EQ(postings->nextPosition(), 0);
    EXPECT_EQ(postings->nextPosition(), 5);

    EXPECT_EQ(postings->nextDoc(), 20);
    EXPECT_EQ(postings->nextPosition(), 3);

    EXPECT_EQ(postings->nextDoc(), index::PostingsEnum::NO_MORE_DOCS);
}

// Test 12: Large posting with many docs triggering multiple PFOR blocks for positions
TEST_F(Lucene90PostingsReaderTest, LargePositionBlocks) {
    // 256 docs, each with freq=1 (position 0)
    // This creates 256 position deltas → 2 PFOR blocks of 128
    const int numDocs = 256;
    std::vector<int> docIDs(numDocs);
    std::vector<int> freqs(numDocs);
    std::vector<std::vector<int>> posPerDoc(numDocs);

    for (int i = 0; i < numDocs; ++i) {
        docIDs[i] = (i + 1) * 5;
        freqs[i] = 1;
        posPerDoc[i] = {0};  // Each doc has position 0 (delta = 0)
    }

    auto docBuf = Lucene90PostingsTestWriter::writeDocFile(docIDs, freqs, true);
    int64_t lastPosBlockOffset = -1;
    auto posBuf = Lucene90PostingsTestWriter::writePosFile(posPerDoc, lastPosBlockOffset);

    auto docIn = std::make_unique<store::ByteBuffersIndexInput>("doc", docBuf);
    auto posIn = std::make_unique<store::ByteBuffersIndexInput>("pos", posBuf);

    Lucene90TermState ts;
    ts.docFreq = numDocs;
    ts.totalTermFreq = numDocs;
    ts.docStartFP = 0;
    ts.posStartFP = 0;
    ts.lastPosBlockOffset = lastPosBlockOffset;

    auto postings = std::make_unique<Lucene90BlockPosEnum>(
        std::move(docIn), std::move(posIn), ts, true);

    for (int i = 0; i < numDocs; ++i) {
        EXPECT_EQ(postings->nextDoc(), docIDs[i]) << "doc " << i;
        EXPECT_EQ(postings->freq(), 1);
        EXPECT_EQ(postings->nextPosition(), 0) << "pos for doc " << i;
    }
    EXPECT_EQ(postings->nextDoc(), index::PostingsEnum::NO_MORE_DOCS);
}

// Test 13: cost() returns docFreq
TEST_F(Lucene90PostingsReaderTest, CostReturnsDocFreq) {
    std::vector<int> docIDs = {1, 2, 3};
    std::vector<int> freqs = {1, 1, 1};

    auto docBuf = Lucene90PostingsTestWriter::writeDocFile(docIDs, freqs, true);
    auto docIn = std::make_unique<store::ByteBuffersIndexInput>("doc", docBuf);

    Lucene90TermState ts;
    ts.docFreq = 3;
    ts.totalTermFreq = 3;
    ts.docStartFP = 0;

    auto postings = std::make_unique<Lucene90BlockDocsEnum>(
        std::move(docIn), ts, true);

    EXPECT_EQ(postings->cost(), 3);
}

// Test 14: impactsPostings falls back to basic postings
TEST_F(Lucene90PostingsReaderTest, ImpactsPostingsFallback) {
    std::vector<int> docIDs = {5, 15};
    std::vector<int> freqs = {2, 3};

    auto docBuf = Lucene90PostingsTestWriter::writeDocFile(docIDs, freqs, true);

    index::FieldInfos fieldInfos;
    index::SegmentReadState state(nullptr, "test_seg", 100, fieldInfos, "");

    Lucene90PostingsReader reader(state);
    reader.setDocInput(std::make_unique<store::ByteBuffersIndexInput>("doc", docBuf));

    codecs::lucene104::TermState lts;
    lts.docStartFP = 0;
    lts.posStartFP = -1;
    lts.docFreq = 2;
    lts.totalTermFreq = 5;

    auto postings = reader.impactsPostings(fieldDocsAndFreqs_, lts);
    ASSERT_NE(postings, nullptr);

    EXPECT_EQ(postings->nextDoc(), 5);
    EXPECT_EQ(postings->freq(), 2);
    EXPECT_EQ(postings->nextDoc(), 15);
    EXPECT_EQ(postings->freq(), 3);
    EXPECT_EQ(postings->nextDoc(), index::PostingsEnum::NO_MORE_DOCS);
}
