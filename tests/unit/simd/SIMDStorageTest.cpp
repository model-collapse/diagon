// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/simd/ColumnWindow.h"
#include "diagon/simd/SIMDScorers.h"
#include "diagon/simd/UnifiedColumnFormat.h"
#include "diagon/simd/UnifiedSIMDQueryProcessor.h"

#include <gtest/gtest.h>

using namespace diagon::simd;

// ==================== ColumnDensity Tests ====================

TEST(ColumnDensityTest, EnumValues) {
    ColumnDensity sparse = ColumnDensity::SPARSE;
    ColumnDensity medium = ColumnDensity::MEDIUM;
    ColumnDensity dense = ColumnDensity::DENSE;

    EXPECT_NE(sparse, medium);
    EXPECT_NE(medium, dense);
    EXPECT_NE(sparse, dense);
}

// ==================== ColumnWindow Tests ====================

TEST(ColumnWindowTest, SparseConstruction) {
    ColumnWindow<int> window(0, 100000, ColumnDensity::SPARSE);

    EXPECT_EQ(0, window.docIdBase);
    EXPECT_EQ(100000, window.capacity);
    EXPECT_EQ(ColumnDensity::SPARSE, window.density);
    EXPECT_TRUE(window.empty());
}

TEST(ColumnWindowTest, DenseConstruction) {
    ColumnWindow<float> window(0, 100000, ColumnDensity::DENSE);

    EXPECT_EQ(0, window.docIdBase);
    EXPECT_EQ(100000, window.capacity);
    EXPECT_EQ(ColumnDensity::DENSE, window.density);
    EXPECT_EQ(100000, window.denseValues.size());
}

TEST(ColumnWindowTest, AddSparseValue) {
    ColumnWindow<int> window(0, 100000, ColumnDensity::SPARSE);

    window.addSparseValue(5, 10);
    window.addSparseValue(12, 20);
    window.addSparseValue(23, 30);

    EXPECT_EQ(3, window.indices.size());
    EXPECT_EQ(3, window.values.size());
    EXPECT_EQ(5, window.indices[0]);
    EXPECT_EQ(10, window.values[0]);
    EXPECT_FALSE(window.empty());
}

TEST(ColumnWindowTest, SetDenseValue) {
    ColumnWindow<float> window(0, 100000, ColumnDensity::DENSE);

    window.setDenseValue(0, 1.5f);
    window.setDenseValue(50, 2.5f);
    window.setDenseValue(99999, 3.5f);

    EXPECT_FLOAT_EQ(1.5f, window.denseValues[0]);
    EXPECT_FLOAT_EQ(2.5f, window.denseValues[50]);
    EXPECT_FLOAT_EQ(3.5f, window.denseValues[99999]);
}

TEST(ColumnWindowTest, GetSparseValue) {
    ColumnWindow<int> window(0, 100000, ColumnDensity::SPARSE);

    window.addSparseValue(5, 10);
    window.addSparseValue(12, 20);
    window.addSparseValue(23, 30);

    auto val1 = window.get(5);
    ASSERT_TRUE(val1.has_value());
    EXPECT_EQ(10, *val1);

    auto val2 = window.get(12);
    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(20, *val2);

    auto val3 = window.get(100);  // Not present
    EXPECT_FALSE(val3.has_value());
}

TEST(ColumnWindowTest, GetDenseValue) {
    ColumnWindow<float> window(0, 100000, ColumnDensity::DENSE);

    window.setDenseValue(0, 1.5f);
    window.setDenseValue(50, 2.5f);

    auto val1 = window.get(0);
    ASSERT_TRUE(val1.has_value());
    EXPECT_FLOAT_EQ(1.5f, *val1);

    auto val2 = window.get(50);
    ASSERT_TRUE(val2.has_value());
    EXPECT_FLOAT_EQ(2.5f, *val2);
}

TEST(ColumnWindowTest, BatchGetSparse) {
    ColumnWindow<int> window(0, 100000, ColumnDensity::SPARSE);

    window.addSparseValue(5, 10);
    window.addSparseValue(12, 20);
    window.addSparseValue(23, 30);

    std::vector<int> docIds = {5, 10, 12, 15, 23};
    std::vector<int> output;

    window.batchGet(docIds, output);

    ASSERT_EQ(5, output.size());
    EXPECT_EQ(10, output[0]);  // doc 5 exists
    EXPECT_EQ(0, output[1]);   // doc 10 doesn't exist
    EXPECT_EQ(20, output[2]);  // doc 12 exists
    EXPECT_EQ(0, output[3]);   // doc 15 doesn't exist
    EXPECT_EQ(30, output[4]);  // doc 23 exists
}

TEST(ColumnWindowTest, BatchGetDense) {
    ColumnWindow<float> window(0, 100000, ColumnDensity::DENSE);

    window.setDenseValue(5, 1.5f);
    window.setDenseValue(12, 2.5f);
    window.setDenseValue(23, 3.5f);

    std::vector<int> docIds = {5, 12, 23};
    std::vector<float> output;

    window.batchGet(docIds, output);

    ASSERT_EQ(3, output.size());
    EXPECT_FLOAT_EQ(1.5f, output[0]);
    EXPECT_FLOAT_EQ(2.5f, output[1]);
    EXPECT_FLOAT_EQ(3.5f, output[2]);
}

TEST(ColumnWindowTest, NonZeroCount) {
    ColumnWindow<int> sparseWindow(0, 100000, ColumnDensity::SPARSE);
    sparseWindow.addSparseValue(5, 10);
    sparseWindow.addSparseValue(12, 20);

    EXPECT_EQ(2, sparseWindow.nonZeroCount());

    ColumnWindow<float> denseWindow(0, 100, ColumnDensity::DENSE);
    denseWindow.setDenseValue(0, 1.0f);
    denseWindow.setDenseValue(50, 2.0f);

    EXPECT_EQ(2, denseWindow.nonZeroCount());
}

// ==================== DataType Tests ====================

TEST(DataTypeTest, EnumValues) {
    EXPECT_NE(DataType::INT32, DataType::INT64);
    EXPECT_NE(DataType::FLOAT32, DataType::FLOAT64);
    EXPECT_NE(DataType::INT32, DataType::BINARY);
}

// ==================== ColumnMetadata Tests ====================

TEST(ColumnMetadataTest, Construction) {
    ColumnMetadata metadata;
    metadata.name = "price";
    metadata.density = ColumnDensity::DENSE;
    metadata.valueType = DataType::FLOAT32;
    metadata.hasNulls = true;
    metadata.totalDocs = 1000000;
    metadata.nonZeroDocs = 950000;
    metadata.avgValue = 99.99f;
    metadata.maxValue = 999.99f;

    EXPECT_EQ("price", metadata.name);
    EXPECT_EQ(ColumnDensity::DENSE, metadata.density);
    EXPECT_EQ(DataType::FLOAT32, metadata.valueType);
    EXPECT_TRUE(metadata.hasNulls);
    EXPECT_EQ(1000000, metadata.totalDocs);
    EXPECT_EQ(950000, metadata.nonZeroDocs);
    EXPECT_FLOAT_EQ(99.99f, metadata.avgValue);
    EXPECT_FLOAT_EQ(999.99f, metadata.maxValue);
}

TEST(ColumnMetadataTest, PostingListMetadata) {
    ColumnMetadata metadata;
    metadata.name = "description";
    metadata.density = ColumnDensity::SPARSE;
    metadata.valueType = DataType::INT32;
    metadata.hasFrequencies = true;
    metadata.hasPositions = true;
    metadata.hasPayloads = false;

    EXPECT_EQ(ColumnDensity::SPARSE, metadata.density);
    EXPECT_TRUE(metadata.hasFrequencies);
    EXPECT_TRUE(metadata.hasPositions);
    EXPECT_FALSE(metadata.hasPayloads);
}

// ==================== UnifiedColumnFormat Tests ====================

TEST(UnifiedColumnFormatTest, Construction) {
    UnifiedColumnFormat format;

    EXPECT_EQ(100000, format.getWindowSize());
}

TEST(UnifiedColumnFormatTest, SetWindowSize) {
    UnifiedColumnFormat format;

    format.setWindowSize(50000);
    EXPECT_EQ(50000, format.getWindowSize());

    format.setWindowSize(200000);
    EXPECT_EQ(200000, format.getWindowSize());
}

TEST(UnifiedColumnFormatTest, BeginEndColumn) {
    UnifiedColumnFormat format;

    ColumnMetadata metadata;
    metadata.name = "price";
    metadata.density = ColumnDensity::DENSE;
    metadata.valueType = DataType::FLOAT32;

    // Should not throw
    EXPECT_NO_THROW(format.beginColumn("price", metadata));
    EXPECT_NO_THROW(format.endColumn());
}

TEST(UnifiedColumnFormatTest, ReadMetadata) {
    UnifiedColumnFormat format;

    // Stub returns empty metadata
    auto metadata = format.readMetadata("price");
    EXPECT_TRUE(metadata.name.empty());
}

// ==================== SIMD Scorer Tests ====================

TEST(SIMDBm25ScorerTest, Construction) {
    SIMDBm25Scorer scorer;

    EXPECT_FLOAT_EQ(1.2f, scorer.getK1());
    EXPECT_FLOAT_EQ(0.75f, scorer.getB());
    EXPECT_FLOAT_EQ(100.0f, scorer.getAvgDocLength());
}

TEST(SIMDBm25ScorerTest, CustomParameters) {
    SIMDBm25Scorer scorer(1.5f, 0.8f, 150.0f);

    EXPECT_FLOAT_EQ(1.5f, scorer.getK1());
    EXPECT_FLOAT_EQ(0.8f, scorer.getB());
    EXPECT_FLOAT_EQ(150.0f, scorer.getAvgDocLength());
}

TEST(SIMDBm25ScorerTest, SetParameters) {
    SIMDBm25Scorer scorer;

    scorer.setK1(1.5f);
    scorer.setB(0.8f);
    scorer.setAvgDocLength(150.0f);

    EXPECT_FLOAT_EQ(1.5f, scorer.getK1());
    EXPECT_FLOAT_EQ(0.8f, scorer.getB());
    EXPECT_FLOAT_EQ(150.0f, scorer.getAvgDocLength());
}

TEST(RankFeaturesScorerTest, Construction) {
    RankFeaturesScorer scorer;
    (void)scorer;  // Suppress unused variable warning
    // Should construct without error
    SUCCEED();
}

TEST(SIMDTfIdfScorerTest, Construction) {
    SIMDTfIdfScorer scorer;
    (void)scorer;  // Suppress unused variable warning
    // Should construct without error
    SUCCEED();
}

// ==================== ScoringMode Tests ====================

TEST(ScoringModeTest, EnumValues) {
    EXPECT_NE(ScoringMode::BM25, ScoringMode::RANK_FEATURES);
    EXPECT_NE(ScoringMode::BM25, ScoringMode::TF_IDF);
    EXPECT_NE(ScoringMode::RANK_FEATURES, ScoringMode::TF_IDF);
}

// ==================== ScoreDoc Tests ====================

TEST(ScoreDocTest, Construction) {
    ScoreDoc doc(42, 10.5f);

    EXPECT_EQ(42, doc.doc);
    EXPECT_FLOAT_EQ(10.5f, doc.score);
}

TEST(ScoreDocTest, DefaultConstruction) {
    ScoreDoc doc;

    EXPECT_EQ(0, doc.doc);
    EXPECT_FLOAT_EQ(0.0f, doc.score);
}

// ==================== TopDocs Tests ====================

TEST(TopDocsTest, Construction) {
    TopDocs topDocs;

    EXPECT_EQ(0, topDocs.totalHits);
    EXPECT_TRUE(topDocs.scoreDocs.empty());
}

TEST(TopDocsTest, ConstructionWithHits) {
    TopDocs topDocs(1000);

    EXPECT_EQ(1000, topDocs.totalHits);
    EXPECT_TRUE(topDocs.scoreDocs.empty());
}

TEST(TopDocsTest, AddScoreDocs) {
    TopDocs topDocs(100);

    topDocs.scoreDocs.push_back(ScoreDoc(5, 10.5f));
    topDocs.scoreDocs.push_back(ScoreDoc(12, 9.3f));
    topDocs.scoreDocs.push_back(ScoreDoc(23, 8.7f));

    EXPECT_EQ(100, topDocs.totalHits);
    EXPECT_EQ(3, topDocs.scoreDocs.size());
    EXPECT_EQ(5, topDocs.scoreDocs[0].doc);
    EXPECT_FLOAT_EQ(10.5f, topDocs.scoreDocs[0].score);
}

// ==================== UnifiedSIMDQueryProcessor Tests ====================

class MockUnifiedColumnReader : public UnifiedColumnReader {
public:
    // Mock implementation
};

TEST(UnifiedSIMDQueryProcessorTest, Construction) {
    MockUnifiedColumnReader reader;
    UnifiedSIMDQueryProcessor processor(reader);

    EXPECT_EQ(ScoringMode::BM25, processor.getScoringMode());
}

TEST(UnifiedSIMDQueryProcessorTest, ConstructionWithMode) {
    MockUnifiedColumnReader reader;
    UnifiedSIMDQueryProcessor processor(reader, ScoringMode::RANK_FEATURES);

    EXPECT_EQ(ScoringMode::RANK_FEATURES, processor.getScoringMode());
}

TEST(UnifiedSIMDQueryProcessorTest, SetScoringMode) {
    MockUnifiedColumnReader reader;
    UnifiedSIMDQueryProcessor processor(reader);

    processor.setScoringMode(ScoringMode::TF_IDF);
    EXPECT_EQ(ScoringMode::TF_IDF, processor.getScoringMode());

    processor.setScoringMode(ScoringMode::RANK_FEATURES);
    EXPECT_EQ(ScoringMode::RANK_FEATURES, processor.getScoringMode());
}

TEST(UnifiedSIMDQueryProcessorTest, GetBm25Scorer) {
    MockUnifiedColumnReader reader;
    UnifiedSIMDQueryProcessor processor(reader);

    auto& bm25 = processor.getBm25Scorer();

    EXPECT_FLOAT_EQ(1.2f, bm25.getK1());
    EXPECT_FLOAT_EQ(0.75f, bm25.getB());
}

TEST(UnifiedSIMDQueryProcessorTest, SearchOr) {
    MockUnifiedColumnReader reader;
    UnifiedSIMDQueryProcessor processor(reader);

    std::vector<std::pair<std::string, float>> queryTerms = {{"wireless", 2.5f},
                                                             {"headphones", 2.8f}};

    // Stub returns empty result
    TopDocs result = processor.searchOr(queryTerms, nullptr, 10);

    EXPECT_EQ(0, result.totalHits);
    EXPECT_TRUE(result.scoreDocs.empty());
}

TEST(UnifiedSIMDQueryProcessorTest, SearchAnd) {
    MockUnifiedColumnReader reader;
    UnifiedSIMDQueryProcessor processor(reader);

    std::vector<std::pair<std::string, float>> queryTerms = {{"wireless", 2.5f},
                                                             {"headphones", 2.8f}};

    // Stub returns empty result
    TopDocs result = processor.searchAnd(queryTerms, nullptr, 10);

    EXPECT_EQ(0, result.totalHits);
    EXPECT_TRUE(result.scoreDocs.empty());
}

TEST(UnifiedSIMDQueryProcessorTest, SearchPhrase) {
    MockUnifiedColumnReader reader;
    UnifiedSIMDQueryProcessor processor(reader);

    std::vector<std::string> terms = {"wireless", "headphones"};

    // Stub returns empty result
    TopDocs result = processor.searchPhrase(terms, nullptr, 10);

    EXPECT_EQ(0, result.totalHits);
    EXPECT_TRUE(result.scoreDocs.empty());
}

// ==================== Integration Tests ====================

TEST(SIMDIntegrationTest, WindowBuildAndQuery) {
    // Build sparse window (posting list)
    ColumnWindow<int> tfWindow(0, 100000, ColumnDensity::SPARSE);
    tfWindow.addSparseValue(5, 2);   // doc 5, tf=2
    tfWindow.addSparseValue(12, 1);  // doc 12, tf=1
    tfWindow.addSparseValue(23, 3);  // doc 23, tf=3

    // Build dense window (doc lengths)
    ColumnWindow<int> docLengthWindow(0, 100000, ColumnDensity::DENSE);
    docLengthWindow.setDenseValue(5, 50);
    docLengthWindow.setDenseValue(12, 100);
    docLengthWindow.setDenseValue(23, 75);

    // Query window
    auto tf5 = tfWindow.get(5);
    auto len5 = docLengthWindow.get(5);

    ASSERT_TRUE(tf5.has_value());
    ASSERT_TRUE(len5.has_value());
    EXPECT_EQ(2, *tf5);
    EXPECT_EQ(50, *len5);
}
