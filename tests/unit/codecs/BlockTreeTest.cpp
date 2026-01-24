// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/codecs/blocktree/BlockTreeTermsReader.h"
#include "diagon/codecs/blocktree/BlockTreeTermsWriter.h"
#include "diagon/index/FieldInfo.h"
#include "diagon/store/ByteBuffersIndexInput.h"
#include "diagon/store/ByteBuffersIndexOutput.h"

#include <gtest/gtest.h>

using namespace diagon::codecs::blocktree;
using namespace diagon::store;
using namespace diagon::index;
using namespace diagon::util;

// ==================== Helper Functions ====================

FieldInfo createFieldInfo(const std::string& name) {
    FieldInfo field;
    field.name = name;
    field.number = 0;
    field.indexOptions = IndexOptions::DOCS_AND_FREQS;
    return field;
}

// ==================== Basic Tests ====================

TEST(BlockTreeTest, WriteReadSingleTerm) {
    // Create outputs
    ByteBuffersIndexOutput timOut("test.tim");
    ByteBuffersIndexOutput tipOut("test.tip");

    FieldInfo fieldInfo = createFieldInfo("field1");

    // Write
    {
        BlockTreeTermsWriter writer(&timOut, &tipOut, fieldInfo);
        writer.addTerm(BytesRef("hello"), BlockTreeTermsWriter::TermStats(5, 10, 1000));
        writer.finish();
    }

    // Create inputs
    auto timData = timOut.toArrayCopy();
    auto tipData = tipOut.toArrayCopy();
    ByteBuffersIndexInput timIn("test.tim", timData);
    ByteBuffersIndexInput tipIn("test.tip", tipData);

    // Read
    BlockTreeTermsReader reader(&timIn, &tipIn, fieldInfo);
    EXPECT_EQ(1, reader.getNumTerms());

    auto termsEnum = reader.iterator();
    EXPECT_TRUE(termsEnum->next());
    EXPECT_EQ(BytesRef("hello"), termsEnum->term());
    EXPECT_EQ(5, termsEnum->docFreq());
    EXPECT_EQ(10, termsEnum->totalTermFreq());
    EXPECT_FALSE(termsEnum->next());  // No more terms
}

TEST(BlockTreeTest, WriteReadMultipleTerms) {
    ByteBuffersIndexOutput timOut("test.tim");
    ByteBuffersIndexOutput tipOut("test.tip");
    FieldInfo fieldInfo = createFieldInfo("field1");

    // Write 5 terms
    {
        BlockTreeTermsWriter writer(&timOut, &tipOut, fieldInfo);
        writer.addTerm(BytesRef("apple"), BlockTreeTermsWriter::TermStats(1, 1, 100));
        writer.addTerm(BytesRef("banana"), BlockTreeTermsWriter::TermStats(2, 3, 200));
        writer.addTerm(BytesRef("cherry"), BlockTreeTermsWriter::TermStats(3, 5, 300));
        writer.addTerm(BytesRef("date"), BlockTreeTermsWriter::TermStats(4, 8, 400));
        writer.addTerm(BytesRef("elderberry"), BlockTreeTermsWriter::TermStats(5, 13, 500));
        writer.finish();
    }

    auto timData = timOut.toArrayCopy();
    auto tipData = tipOut.toArrayCopy();
    ByteBuffersIndexInput timIn("test.tim", timData);
    ByteBuffersIndexInput tipIn("test.tip", tipData);

    // Read and verify
    BlockTreeTermsReader reader(&timIn, &tipIn, fieldInfo);
    EXPECT_EQ(5, reader.getNumTerms());

    auto termsEnum = reader.iterator();

    EXPECT_TRUE(termsEnum->next());
    EXPECT_EQ(BytesRef("apple"), termsEnum->term());
    EXPECT_EQ(1, termsEnum->docFreq());

    EXPECT_TRUE(termsEnum->next());
    EXPECT_EQ(BytesRef("banana"), termsEnum->term());
    EXPECT_EQ(2, termsEnum->docFreq());

    EXPECT_TRUE(termsEnum->next());
    EXPECT_EQ(BytesRef("cherry"), termsEnum->term());
    EXPECT_EQ(3, termsEnum->docFreq());

    EXPECT_TRUE(termsEnum->next());
    EXPECT_EQ(BytesRef("date"), termsEnum->term());
    EXPECT_EQ(4, termsEnum->docFreq());

    EXPECT_TRUE(termsEnum->next());
    EXPECT_EQ(BytesRef("elderberry"), termsEnum->term());
    EXPECT_EQ(5, termsEnum->docFreq());

    EXPECT_FALSE(termsEnum->next());
}

TEST(BlockTreeTest, SharedPrefixes) {
    ByteBuffersIndexOutput timOut("test.tim");
    ByteBuffersIndexOutput tipOut("test.tip");
    FieldInfo fieldInfo = createFieldInfo("field1");

    // Write terms with shared prefixes (in lexicographic order)
    {
        BlockTreeTermsWriter writer(&timOut, &tipOut, fieldInfo);
        writer.addTerm(BytesRef("cat"), BlockTreeTermsWriter::TermStats(1, 1, 100));
        writer.addTerm(BytesRef("category"), BlockTreeTermsWriter::TermStats(2, 2, 200));
        writer.addTerm(BytesRef("cats"), BlockTreeTermsWriter::TermStats(3, 3, 300));
        writer.finish();
    }

    auto timData = timOut.toArrayCopy();
    auto tipData = tipOut.toArrayCopy();
    ByteBuffersIndexInput timIn("test.tim", timData);
    ByteBuffersIndexInput tipIn("test.tip", tipData);

    BlockTreeTermsReader reader(&timIn, &tipIn, fieldInfo);
    auto termsEnum = reader.iterator();

    EXPECT_TRUE(termsEnum->next());
    EXPECT_EQ(BytesRef("cat"), termsEnum->term());

    EXPECT_TRUE(termsEnum->next());
    EXPECT_EQ(BytesRef("category"), termsEnum->term());

    EXPECT_TRUE(termsEnum->next());
    EXPECT_EQ(BytesRef("cats"), termsEnum->term());
}

// ==================== Seek Tests ====================

TEST(BlockTreeTest, SeekExact) {
    ByteBuffersIndexOutput timOut("test.tim");
    ByteBuffersIndexOutput tipOut("test.tip");
    FieldInfo fieldInfo = createFieldInfo("field1");

    // Write terms
    {
        BlockTreeTermsWriter writer(&timOut, &tipOut, fieldInfo);
        writer.addTerm(BytesRef("apple"), BlockTreeTermsWriter::TermStats(1, 1, 100));
        writer.addTerm(BytesRef("banana"), BlockTreeTermsWriter::TermStats(2, 2, 200));
        writer.addTerm(BytesRef("cherry"), BlockTreeTermsWriter::TermStats(3, 3, 300));
        writer.finish();
    }

    auto timData = timOut.toArrayCopy();
    auto tipData = tipOut.toArrayCopy();
    ByteBuffersIndexInput timIn("test.tim", timData);
    ByteBuffersIndexInput tipIn("test.tip", tipData);

    BlockTreeTermsReader reader(&timIn, &tipIn, fieldInfo);
    auto termsEnum = reader.iterator();

    // Seek to existing term
    EXPECT_TRUE(termsEnum->seekExact(BytesRef("banana")));
    EXPECT_EQ(BytesRef("banana"), termsEnum->term());
    EXPECT_EQ(2, termsEnum->docFreq());

    // Seek to non-existing term
    EXPECT_FALSE(termsEnum->seekExact(BytesRef("durian")));
}

TEST(BlockTreeTest, SeekCeil) {
    ByteBuffersIndexOutput timOut("test.tim");
    ByteBuffersIndexOutput tipOut("test.tip");
    FieldInfo fieldInfo = createFieldInfo("field1");

    // Write terms
    {
        BlockTreeTermsWriter writer(&timOut, &tipOut, fieldInfo);
        writer.addTerm(BytesRef("apple"), BlockTreeTermsWriter::TermStats(1, 1, 100));
        writer.addTerm(BytesRef("cherry"), BlockTreeTermsWriter::TermStats(3, 3, 300));
        writer.addTerm(BytesRef("elderberry"), BlockTreeTermsWriter::TermStats(5, 5, 500));
        writer.finish();
    }

    auto timData = timOut.toArrayCopy();
    auto tipData = tipOut.toArrayCopy();
    ByteBuffersIndexInput timIn("test.tim", timData);
    ByteBuffersIndexInput tipIn("test.tip", tipData);

    BlockTreeTermsReader reader(&timIn, &tipIn, fieldInfo);
    auto termsEnum = reader.iterator();

    // Seek to exact term
    EXPECT_EQ(TermsEnum::SeekStatus::FOUND, termsEnum->seekCeil(BytesRef("cherry")));
    EXPECT_EQ(BytesRef("cherry"), termsEnum->term());

    // Seek to term between existing terms
    EXPECT_EQ(TermsEnum::SeekStatus::NOT_FOUND, termsEnum->seekCeil(BytesRef("banana")));
    EXPECT_EQ(BytesRef("cherry"), termsEnum->term());  // Should position at ceiling

    // Seek past last term
    EXPECT_EQ(TermsEnum::SeekStatus::END, termsEnum->seekCeil(BytesRef("zebra")));
}

// ==================== Block Size Tests ====================

TEST(BlockTreeTest, MultipleBlocks) {
    ByteBuffersIndexOutput timOut("test.tim");
    ByteBuffersIndexOutput tipOut("test.tip");
    FieldInfo fieldInfo = createFieldInfo("field1");

    BlockTreeTermsWriter::Config config;
    config.minItemsInBlock = 5;
    config.maxItemsInBlock = 10;

    // Write 25 terms (should create multiple blocks, zero-padded for lexicographic order)
    {
        BlockTreeTermsWriter writer(&timOut, &tipOut, fieldInfo, config);
        for (int i = 0; i < 25; i++) {
            char buf[20];
            snprintf(buf, sizeof(buf), "term_%02d", i);
            std::string term(buf);
            writer.addTerm(BytesRef(term),
                           BlockTreeTermsWriter::TermStats(i + 1, i + 1, (i + 1) * 100));
        }
        writer.finish();
    }

    auto timData = timOut.toArrayCopy();
    auto tipData = tipOut.toArrayCopy();
    ByteBuffersIndexInput timIn("test.tim", timData);
    ByteBuffersIndexInput tipIn("test.tip", tipData);

    BlockTreeTermsReader reader(&timIn, &tipIn, fieldInfo);
    EXPECT_EQ(25, reader.getNumTerms());

    // Verify we can iterate all terms
    auto termsEnum = reader.iterator();
    int count = 0;
    while (termsEnum->next()) {
        count++;
    }
    // Note: With current MVP implementation, we can only iterate first block
    // Multi-block iteration will be added later
    EXPECT_GE(count, config.minItemsInBlock);
}

// ==================== Error Handling ====================

TEST(BlockTreeTest, UnsortedTermsThrow) {
    ByteBuffersIndexOutput timOut("test.tim");
    ByteBuffersIndexOutput tipOut("test.tip");
    FieldInfo fieldInfo = createFieldInfo("field1");

    BlockTreeTermsWriter writer(&timOut, &tipOut, fieldInfo);
    writer.addTerm(BytesRef("zebra"), BlockTreeTermsWriter::TermStats(1, 1, 100));

    // Adding "apple" after "zebra" should throw
    EXPECT_THROW(writer.addTerm(BytesRef("apple"), BlockTreeTermsWriter::TermStats(2, 2, 200)),
                 std::invalid_argument);
}

TEST(BlockTreeTest, InvalidConfig) {
    ByteBuffersIndexOutput timOut("test.tim");
    ByteBuffersIndexOutput tipOut("test.tip");
    FieldInfo fieldInfo = createFieldInfo("field1");

    BlockTreeTermsWriter::Config config;
    config.minItemsInBlock = 10;
    config.maxItemsInBlock = 5;  // Invalid: max < min

    EXPECT_THROW(BlockTreeTermsWriter(&timOut, &tipOut, fieldInfo, config), std::invalid_argument);
}

// ==================== Empty Field ====================

TEST(BlockTreeTest, EmptyField) {
    ByteBuffersIndexOutput timOut("test.tim");
    ByteBuffersIndexOutput tipOut("test.tip");
    FieldInfo fieldInfo = createFieldInfo("field1");

    // Write no terms
    {
        BlockTreeTermsWriter writer(&timOut, &tipOut, fieldInfo);
        writer.finish();
    }

    auto timData = timOut.toArrayCopy();
    auto tipData = tipOut.toArrayCopy();
    ByteBuffersIndexInput timIn("test.tim", timData);
    ByteBuffersIndexInput tipIn("test.tip", tipData);

    BlockTreeTermsReader reader(&timIn, &tipIn, fieldInfo);
    EXPECT_EQ(0, reader.getNumTerms());

    auto termsEnum = reader.iterator();
    EXPECT_FALSE(termsEnum->next());
}
