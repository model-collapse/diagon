// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/FreqProxTermsWriter.h"

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"

#include <gtest/gtest.h>

using namespace diagon::index;
using namespace diagon::document;

// ==================== FieldInfosBuilder Tests ====================

TEST(FieldInfosBuilderTest, BasicFieldCreation) {
    FieldInfosBuilder builder;

    int fieldNum1 = builder.getOrAdd("title");
    int fieldNum2 = builder.getOrAdd("body");

    EXPECT_EQ(fieldNum1, 0);
    EXPECT_EQ(fieldNum2, 1);
    EXPECT_EQ(builder.getFieldCount(), 2);
}

TEST(FieldInfosBuilderTest, DuplicateFieldName) {
    FieldInfosBuilder builder;

    int fieldNum1 = builder.getOrAdd("title");
    int fieldNum2 = builder.getOrAdd("title");

    EXPECT_EQ(fieldNum1, fieldNum2);
    EXPECT_EQ(builder.getFieldCount(), 1);
}

TEST(FieldInfosBuilderTest, UpdateIndexOptions) {
    FieldInfosBuilder builder;

    // Create field first
    builder.getOrAdd("title");

    // Start with DOCS
    builder.updateIndexOptions("title", IndexOptions::DOCS);

    // Upgrade to DOCS_AND_FREQS (more permissive)
    builder.updateIndexOptions("title", IndexOptions::DOCS_AND_FREQS);

    // Try to downgrade (should keep DOCS_AND_FREQS)
    builder.updateIndexOptions("title", IndexOptions::DOCS);

    // Verify field created
    EXPECT_EQ(builder.getFieldCount(), 1);
}

TEST(FieldInfosBuilderTest, UpdateDocValuesType) {
    FieldInfosBuilder builder;

    // Set doc values type
    builder.updateDocValuesType("price", DocValuesType::NUMERIC);

    // Same type again (should be fine)
    builder.updateDocValuesType("price", DocValuesType::NUMERIC);

    EXPECT_EQ(builder.getFieldCount(), 1);
}

TEST(FieldInfosBuilderTest, DocValuesTypeConflict) {
    FieldInfosBuilder builder;

    // Set initial type
    builder.updateDocValuesType("field", DocValuesType::NUMERIC);

    // Try to change type (should throw)
    EXPECT_THROW(builder.updateDocValuesType("field", DocValuesType::BINARY),
                 std::invalid_argument);
}

TEST(FieldInfosBuilderTest, GetFieldNumber) {
    FieldInfosBuilder builder;

    builder.getOrAdd("title");
    builder.getOrAdd("body");

    EXPECT_EQ(builder.getFieldNumber("title"), 0);
    EXPECT_EQ(builder.getFieldNumber("body"), 1);
    EXPECT_EQ(builder.getFieldNumber("unknown"), -1);
}

TEST(FieldInfosBuilderTest, Reset) {
    FieldInfosBuilder builder;

    builder.getOrAdd("title");
    builder.getOrAdd("body");

    EXPECT_EQ(builder.getFieldCount(), 2);

    builder.reset();

    EXPECT_EQ(builder.getFieldCount(), 0);
    EXPECT_EQ(builder.getFieldNumber("title"), -1);
}

// ==================== FreqProxTermsWriter Tests ====================

TEST(FreqProxTermsWriterTest, EmptyDocument) {
    FieldInfosBuilder builder;
    FreqProxTermsWriter writer(builder);

    Document doc;

    writer.addDocument(doc, 0);

    EXPECT_EQ(writer.getTerms().size(), 0);
    EXPECT_EQ(builder.getFieldCount(), 0);
}

TEST(FreqProxTermsWriterTest, SingleTermSingleDoc) {
    FieldInfosBuilder builder;
    FreqProxTermsWriter writer(builder);

    Document doc;
    doc.add(std::make_unique<TextField>("title", "hello", TextField::TYPE_STORED));

    writer.addDocument(doc, 0);

    // Verify term stored
    auto terms = writer.getTerms();
    EXPECT_EQ(terms.size(), 1);
    EXPECT_EQ(terms[0], "hello");

    // Verify posting list: [docID=0, freq=1]
    auto postings = writer.getPostingList("hello");
    EXPECT_EQ(postings.size(), 2);
    EXPECT_EQ(postings[0], 0);  // docID
    EXPECT_EQ(postings[1], 1);  // freq
}

TEST(FreqProxTermsWriterTest, MultipleTermsSingleDoc) {
    FieldInfosBuilder builder;
    FreqProxTermsWriter writer(builder);

    Document doc;
    doc.add(std::make_unique<TextField>("body", "the quick brown fox", TextField::TYPE_STORED));

    writer.addDocument(doc, 0);

    // Verify all terms stored
    auto terms = writer.getTerms();
    EXPECT_EQ(terms.size(), 4);

    // Terms should be sorted
    EXPECT_EQ(terms[0], "brown");
    EXPECT_EQ(terms[1], "fox");
    EXPECT_EQ(terms[2], "quick");
    EXPECT_EQ(terms[3], "the");

    // Verify each posting list
    for (const auto& term : terms) {
        auto postings = writer.getPostingList(term);
        EXPECT_EQ(postings.size(), 2);
        EXPECT_EQ(postings[0], 0);  // docID
        EXPECT_EQ(postings[1], 1);  // freq
    }
}

TEST(FreqProxTermsWriterTest, MultipleDocsSameTerm) {
    FieldInfosBuilder builder;
    FreqProxTermsWriter writer(builder);

    // Doc 0
    Document doc0;
    doc0.add(std::make_unique<TextField>("body", "hello", TextField::TYPE_STORED));
    writer.addDocument(doc0, 0);

    // Doc 1
    Document doc1;
    doc1.add(std::make_unique<TextField>("body", "world", TextField::TYPE_STORED));
    writer.addDocument(doc1, 1);

    // Doc 2
    Document doc2;
    doc2.add(std::make_unique<TextField>("body", "hello", TextField::TYPE_STORED));
    writer.addDocument(doc2, 2);

    // Verify terms
    auto terms = writer.getTerms();
    EXPECT_EQ(terms.size(), 2);

    // Verify "hello" appears in docs 0 and 2
    auto helloPostings = writer.getPostingList("hello");
    EXPECT_EQ(helloPostings.size(), 4);  // 2 docs * 2 values
    EXPECT_EQ(helloPostings[0], 0);      // doc 0
    EXPECT_EQ(helloPostings[1], 1);      // freq
    EXPECT_EQ(helloPostings[2], 2);      // doc 2
    EXPECT_EQ(helloPostings[3], 1);      // freq

    // Verify "world" appears in doc 1
    auto worldPostings = writer.getPostingList("world");
    EXPECT_EQ(worldPostings.size(), 2);
    EXPECT_EQ(worldPostings[0], 1);  // doc 1
    EXPECT_EQ(worldPostings[1], 1);  // freq
}

TEST(FreqProxTermsWriterTest, MultipleFields) {
    FieldInfosBuilder builder;
    FreqProxTermsWriter writer(builder);

    Document doc;
    doc.add(std::make_unique<TextField>("title", "search", TextField::TYPE_STORED));
    doc.add(std::make_unique<TextField>("body", "search engine", TextField::TYPE_STORED));

    writer.addDocument(doc, 0);

    // Verify field metadata
    EXPECT_EQ(builder.getFieldCount(), 2);
    EXPECT_NE(builder.getFieldNumber("title"), -1);
    EXPECT_NE(builder.getFieldNumber("body"), -1);

    // Verify terms (search appears twice, but only one posting list)
    auto terms = writer.getTerms();
    EXPECT_EQ(terms.size(), 2);  // "engine", "search"

    // Both occurrences of "search" combined into single posting list
    auto searchPostings = writer.getPostingList("search");
    EXPECT_EQ(searchPostings.size(), 2);
    EXPECT_EQ(searchPostings[0], 0);  // docID
    EXPECT_EQ(searchPostings[1], 1);  // freq (only counts once per doc)
}

TEST(FreqProxTermsWriterTest, NonIndexedField) {
    FieldInfosBuilder builder;
    FreqProxTermsWriter writer(builder);

    Document doc;
    doc.add(std::make_unique<StringField>("id", "12345", StringField::TYPE_STORED));
    doc.add(std::make_unique<TextField>("body", "hello", TextField::TYPE_STORED));

    writer.addDocument(doc, 0);

    // StringField has IndexOptions::DOCS, so it should be indexed
    // Verify both fields tracked
    EXPECT_EQ(builder.getFieldCount(), 2);

    // Verify terms from both fields
    auto terms = writer.getTerms();
    EXPECT_EQ(terms.size(), 2);  // "12345" and "hello"
}

TEST(FreqProxTermsWriterTest, NumericDocValuesField) {
    FieldInfosBuilder builder;
    FreqProxTermsWriter writer(builder);

    Document doc;
    doc.add(std::make_unique<NumericDocValuesField>("price", 100));

    writer.addDocument(doc, 0);

    // NumericDocValuesField has IndexOptions::NONE, so no terms
    EXPECT_EQ(writer.getTerms().size(), 0);

    // But field metadata should be tracked
    EXPECT_EQ(builder.getFieldCount(), 1);
}

TEST(FreqProxTermsWriterTest, Reset) {
    FieldInfosBuilder builder;
    FreqProxTermsWriter writer(builder);

    Document doc;
    doc.add(std::make_unique<TextField>("body", "hello world", TextField::TYPE_STORED));
    writer.addDocument(doc, 0);

    EXPECT_EQ(writer.getTerms().size(), 2);

    writer.reset();

    EXPECT_EQ(writer.getTerms().size(), 0);
    EXPECT_GT(writer.bytesUsed(), 0);  // Memory retained
}

TEST(FreqProxTermsWriterTest, Clear) {
    FieldInfosBuilder builder;
    FreqProxTermsWriter writer(builder);

    Document doc;
    doc.add(std::make_unique<TextField>("body", "hello world", TextField::TYPE_STORED));
    writer.addDocument(doc, 0);

    EXPECT_GT(writer.bytesUsed(), 0);

    writer.clear();

    EXPECT_EQ(writer.getTerms().size(), 0);
    EXPECT_EQ(writer.bytesUsed(), 0);  // Memory freed
}

TEST(FreqProxTermsWriterTest, LargeDocument) {
    FieldInfosBuilder builder;
    FreqProxTermsWriter writer(builder);

    // Create document with many unique terms
    std::string text;
    for (int i = 0; i < 1000; i++) {
        text += "term" + std::to_string(i) + " ";
    }

    Document doc;
    doc.add(std::make_unique<TextField>("body", text, TextField::TYPE_STORED));

    writer.addDocument(doc, 0);

    // Verify all terms stored
    auto terms = writer.getTerms();
    EXPECT_EQ(terms.size(), 1000);

    // Verify memory usage increased
    EXPECT_GT(writer.bytesUsed(), 0);
}

TEST(FreqProxTermsWriterTest, EmptyFieldValue) {
    FieldInfosBuilder builder;
    FreqProxTermsWriter writer(builder);

    Document doc;
    doc.add(std::make_unique<TextField>("body", "", TextField::TYPE_STORED));

    writer.addDocument(doc, 0);

    // Empty field produces no terms
    EXPECT_EQ(writer.getTerms().size(), 0);

    // But field metadata tracked
    EXPECT_EQ(builder.getFieldCount(), 1);
}

TEST(FreqProxTermsWriterTest, TermNotFound) {
    FieldInfosBuilder builder;
    FreqProxTermsWriter writer(builder);

    Document doc;
    doc.add(std::make_unique<TextField>("body", "hello", TextField::TYPE_STORED));
    writer.addDocument(doc, 0);

    // Query non-existent term
    auto postings = writer.getPostingList("nonexistent");
    EXPECT_EQ(postings.size(), 0);
}
