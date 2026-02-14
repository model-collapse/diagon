// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/blocktree/BlockTreeTermsReader.h"
#include "diagon/codecs/blocktree/BlockTreeTermsWriter.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/store/ByteBuffersIndexInput.h"
#include "diagon/store/ByteBuffersIndexOutput.h"
#include "diagon/util/BytesRef.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using namespace diagon;
using namespace diagon::codecs::blocktree;
using namespace diagon::index;
using namespace diagon::store;
using namespace diagon::util;

namespace {

// Helper: Create field info
FieldInfo createFieldInfo(const std::string& name) {
    FieldInfo info;
    info.name = name;
    info.number = 0;
    info.indexOptions = IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
    return info;
}

// Helper: Write and read terms using in-memory buffers
void writeAndReadTerms(const std::vector<std::pair<std::string, int64_t>>& terms,
                       const FieldInfo& fieldInfo,
                       std::function<void(BlockTreeTermsReader&)> testFunc) {
    // Write phase
    ByteBuffersIndexOutput timOut("test.tim");
    ByteBuffersIndexOutput tipOut("test.tip");

    {
        BlockTreeTermsWriter::Config config;
        config.minItemsInBlock = 25;
        config.maxItemsInBlock = 48;

        BlockTreeTermsWriter writer(&timOut, &tipOut, fieldInfo, config);

        for (const auto& [term, output] : terms) {
            BlockTreeTermsWriter::TermStats stats;
            stats.docFreq = 1;
            stats.totalTermFreq = 1;
            stats.postingsFP = output;
            writer.addTerm(BytesRef(term), stats);
        }

        writer.finish();
    }

    // Read phase
    auto timData = timOut.toArrayCopy();
    auto tipData = tipOut.toArrayCopy();
    ByteBuffersIndexInput timIn("test.tim", timData);
    ByteBuffersIndexInput tipIn("test.tip", tipData);

    BlockTreeTermsReader reader(&timIn, &tipIn, fieldInfo);

    // Run test function
    testFunc(reader);
}

}  // anonymous namespace

// ==================== TIP2 Format Tests ====================

TEST(BlockTreeFSTTest, WriteTIP2_ReadBack) {
    FieldInfo fieldInfo = createFieldInfo("test_field");

    std::vector<std::pair<std::string, int64_t>> terms = {
        {"apple", 100}, {"banana", 200}, {"cherry", 300}, {"date", 400}};

    writeAndReadTerms(terms, fieldInfo, [&](BlockTreeTermsReader& reader) {
        EXPECT_EQ(4, reader.getNumTerms());

        // Verify terms can be found
        auto termsEnum = reader.iterator();
        EXPECT_TRUE(termsEnum->seekExact(BytesRef("apple")));
        EXPECT_TRUE(termsEnum->seekExact(BytesRef("banana")));
        EXPECT_TRUE(termsEnum->seekExact(BytesRef("cherry")));
        EXPECT_TRUE(termsEnum->seekExact(BytesRef("date")));

        // Verify non-existent terms
        EXPECT_FALSE(termsEnum->seekExact(BytesRef("elderberry")));
    });
}

TEST(BlockTreeFSTTest, TIP2_MultipleBlocks) {
    // Write enough terms to span multiple blocks (>48 terms per block)
    FieldInfo fieldInfo = createFieldInfo("test_field");

    std::vector<std::pair<std::string, int64_t>> terms;
    for (int i = 0; i < 200; i++) {
        char buf[20];
        snprintf(buf, sizeof(buf), "term_%04d", i);
        terms.emplace_back(buf, i * 100);
    }

    writeAndReadTerms(terms, fieldInfo, [&](BlockTreeTermsReader& reader) {
        EXPECT_EQ(200, reader.getNumTerms());

        // Verify samples across blocks
        auto termsEnum = reader.iterator();
        EXPECT_TRUE(termsEnum->seekExact(BytesRef("term_0000")));
        EXPECT_TRUE(termsEnum->seekExact(BytesRef("term_0050")));
        EXPECT_TRUE(termsEnum->seekExact(BytesRef("term_0100")));
        EXPECT_TRUE(termsEnum->seekExact(BytesRef("term_0199")));
    });
}

TEST(BlockTreeFSTTest, TIP2_IterateAllTerms) {
    FieldInfo fieldInfo = createFieldInfo("test_field");

    std::vector<std::pair<std::string, int64_t>> terms = {
        {"cat", 10}, {"dog", 20}, {"elephant", 30}, {"fox", 40}, {"giraffe", 50}};

    writeAndReadTerms(terms, fieldInfo, [&](BlockTreeTermsReader& reader) {
        auto termsEnum = reader.iterator();

        std::vector<std::string> foundTerms;
        while (termsEnum->next()) {
            BytesRef term = termsEnum->term();
            foundTerms.emplace_back(reinterpret_cast<const char*>(term.data()), term.length());
        }

        // Verify all terms found in order
        ASSERT_EQ(5, foundTerms.size());
        EXPECT_EQ("cat", foundTerms[0]);
        EXPECT_EQ("dog", foundTerms[1]);
        EXPECT_EQ("elephant", foundTerms[2]);
        EXPECT_EQ("fox", foundTerms[3]);
        EXPECT_EQ("giraffe", foundTerms[4]);
    });
}

TEST(BlockTreeFSTTest, TIP2_SeekCeil) {
    FieldInfo fieldInfo = createFieldInfo("test_field");

    std::vector<std::pair<std::string, int64_t>> terms = {
        {"apple", 10}, {"banana", 20}, {"cherry", 30}, {"date", 40}};

    writeAndReadTerms(terms, fieldInfo, [&](BlockTreeTermsReader& reader) {
        auto termsEnum = reader.iterator();

        // Exact match
        EXPECT_EQ(TermsEnum::SeekStatus::FOUND, termsEnum->seekCeil(BytesRef("banana")));
        EXPECT_EQ(BytesRef("banana"), termsEnum->term());

        // Between terms (should find ceiling)
        EXPECT_EQ(TermsEnum::SeekStatus::NOT_FOUND, termsEnum->seekCeil(BytesRef("avocado")));
        EXPECT_EQ(BytesRef("banana"), termsEnum->term());

        // Before all terms
        EXPECT_EQ(TermsEnum::SeekStatus::NOT_FOUND, termsEnum->seekCeil(BytesRef("aardvark")));
        EXPECT_EQ(BytesRef("apple"), termsEnum->term());

        // After all terms
        EXPECT_EQ(TermsEnum::SeekStatus::END, termsEnum->seekCeil(BytesRef("zebra")));
    });
}

// ==================== Block Metadata Extraction ====================

TEST(BlockTreeFSTTest, BlockMetadata_ExtractFromFST) {
    FieldInfo fieldInfo = createFieldInfo("test_field");

    // Exactly 48 terms (one full block)
    std::vector<std::pair<std::string, int64_t>> terms;
    for (int i = 0; i < 48; i++) {
        char buf[20];
        snprintf(buf, sizeof(buf), "term_%02d", i);
        terms.emplace_back(buf, i * 100);
    }

    writeAndReadTerms(terms, fieldInfo, [&](BlockTreeTermsReader& reader) {
        // BlockTreeTermsReader should have extracted block metadata from FST
        // Verify by iterating all terms
        auto termsEnum = reader.iterator();

        int count = 0;
        while (termsEnum->next()) {
            count++;
        }

        EXPECT_EQ(48, count);
    });
}

TEST(BlockTreeFSTTest, BlockMetadata_MultipleBlocks) {
    FieldInfo fieldInfo = createFieldInfo("test_field");

    std::vector<std::pair<std::string, int64_t>> terms;
    for (int i = 0; i < 150; i++) {  // Will create ~3-4 blocks
        char buf[20];
        snprintf(buf, sizeof(buf), "term_%03d", i);
        terms.emplace_back(buf, i * 100);
    }

    writeAndReadTerms(terms, fieldInfo, [&](BlockTreeTermsReader& reader) {
        // Verify we can seek across block boundaries
        auto termsEnum = reader.iterator();

        // Seek to term in first block
        EXPECT_TRUE(termsEnum->seekExact(BytesRef("term_000")));

        // Seek to term in last block
        EXPECT_TRUE(termsEnum->seekExact(BytesRef("term_149")));

        // Seek to term in middle block
        EXPECT_TRUE(termsEnum->seekExact(BytesRef("term_075")));
    });
}

// ==================== Empty Field ====================

TEST(BlockTreeFSTTest, EmptyField_TIP2Format) {
    FieldInfo fieldInfo = createFieldInfo("empty_field");

    std::vector<std::pair<std::string, int64_t>> terms;  // Empty

    writeAndReadTerms(terms, fieldInfo, [&](BlockTreeTermsReader& reader) {
        EXPECT_EQ(0, reader.getNumTerms());

        // Iteration should immediately return false
        auto termsEnum = reader.iterator();
        EXPECT_FALSE(termsEnum->next());
    });
}

// ==================== Large Field ====================

TEST(BlockTreeFSTTest, LargeField_TIP2) {
    FieldInfo fieldInfo = createFieldInfo("large_field");

    std::vector<std::pair<std::string, int64_t>> terms;
    for (int i = 0; i < 1000; i++) {
        char buf[30];
        snprintf(buf, sizeof(buf), "term_%04d", i);
        terms.emplace_back(buf, i * 1000);
    }

    writeAndReadTerms(terms, fieldInfo, [&](BlockTreeTermsReader& reader) {
        EXPECT_EQ(1000, reader.getNumTerms());

        // Verify all terms are accessible
        auto termsEnum = reader.iterator();
        int count = 0;
        while (termsEnum->next()) {
            count++;
        }
        EXPECT_EQ(1000, count);
    });
}

// ==================== Shared Prefix ====================

TEST(BlockTreeFSTTest, SharedPrefix) {
    FieldInfo fieldInfo = createFieldInfo("prefix_field");

    std::vector<std::pair<std::string, int64_t>> terms;
    for (int i = 0; i < 100; i++) {
        char buf[30];
        snprintf(buf, sizeof(buf), "common_prefix_%03d", i);
        terms.emplace_back(buf, i * 100);
    }

    writeAndReadTerms(terms, fieldInfo, [&](BlockTreeTermsReader& reader) {
        EXPECT_EQ(100, reader.getNumTerms());

        // Verify correctness
        auto termsEnum = reader.iterator();
        EXPECT_TRUE(termsEnum->seekExact(BytesRef("common_prefix_000")));
        EXPECT_TRUE(termsEnum->seekExact(BytesRef("common_prefix_050")));
        EXPECT_TRUE(termsEnum->seekExact(BytesRef("common_prefix_099")));
    });
}
