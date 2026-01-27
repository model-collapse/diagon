#include <gtest/gtest.h>
#include "analysis/Analyzer.h"

using namespace diagon::analysis;

TEST(AnalyzerFactoryTest, CreateStandard) {
    auto analyzer = AnalyzerFactory::createStandard();

    ASSERT_NE(analyzer, nullptr);
    EXPECT_EQ(analyzer->name(), "standard");
}

TEST(AnalyzerFactoryTest, CreateSimple) {
    auto analyzer = AnalyzerFactory::createSimple();

    ASSERT_NE(analyzer, nullptr);
    EXPECT_EQ(analyzer->name(), "simple");
}

TEST(AnalyzerFactoryTest, CreateWhitespace) {
    auto analyzer = AnalyzerFactory::createWhitespace();

    ASSERT_NE(analyzer, nullptr);
    EXPECT_EQ(analyzer->name(), "whitespace");
}

TEST(AnalyzerFactoryTest, CreateKeyword) {
    auto analyzer = AnalyzerFactory::createKeyword();

    ASSERT_NE(analyzer, nullptr);
    EXPECT_EQ(analyzer->name(), "keyword");
}

TEST(AnalyzerFactoryTest, CreateEnglish) {
    auto analyzer = AnalyzerFactory::createEnglish();

    ASSERT_NE(analyzer, nullptr);
    EXPECT_EQ(analyzer->name(), "english");
}

TEST(AnalyzerFactoryTest, CreateMultilingual) {
    auto analyzer = AnalyzerFactory::createMultilingual();

    ASSERT_NE(analyzer, nullptr);
    EXPECT_EQ(analyzer->name(), "multilingual");
}

TEST(AnalyzerFactoryTest, CreateSearch) {
    auto analyzer = AnalyzerFactory::createSearch();

    ASSERT_NE(analyzer, nullptr);
    EXPECT_EQ(analyzer->name(), "search");
}

TEST(AnalyzerFactoryTest, StandardAnalyzerBehavior) {
    auto analyzer = AnalyzerFactory::createStandard();

    auto tokens = analyzer->analyze("The quick brown fox");

    // Standard analyzer: tokenize + lowercase + remove stop words
    // "The" should be removed as stop word
    ASSERT_GE(tokens.size(), 3);

    // Should contain content words in lowercase
    bool has_quick = false;
    bool has_brown = false;
    bool has_fox = false;

    for (const auto& token : tokens) {
        if (token.getText() == "quick") has_quick = true;
        if (token.getText() == "brown") has_brown = true;
        if (token.getText() == "fox") has_fox = true;
    }

    EXPECT_TRUE(has_quick);
    EXPECT_TRUE(has_brown);
    EXPECT_TRUE(has_fox);
}

TEST(AnalyzerFactoryTest, SimpleAnalyzerBehavior) {
    auto analyzer = AnalyzerFactory::createSimple();

    auto tokens = analyzer->analyze("Hello World Test");

    // Simple analyzer: whitespace + lowercase
    ASSERT_EQ(tokens.size(), 3);
    EXPECT_EQ(tokens[0].getText(), "hello");
    EXPECT_EQ(tokens[1].getText(), "world");
    EXPECT_EQ(tokens[2].getText(), "test");
}

TEST(AnalyzerFactoryTest, WhitespaceAnalyzerBehavior) {
    auto analyzer = AnalyzerFactory::createWhitespace();

    auto tokens = analyzer->analyze("Hello World Test");

    // Whitespace analyzer: tokenize only, no lowercase
    ASSERT_EQ(tokens.size(), 3);
    EXPECT_EQ(tokens[0].getText(), "Hello");
    EXPECT_EQ(tokens[1].getText(), "World");
    EXPECT_EQ(tokens[2].getText(), "Test");
}

TEST(AnalyzerFactoryTest, KeywordAnalyzerBehavior) {
    auto analyzer = AnalyzerFactory::createKeyword();

    auto tokens = analyzer->analyze("Hello World Test");

    // Keyword analyzer: entire text as single token
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0].getText(), "Hello World Test");
}

TEST(AnalyzerFactoryTest, EnglishAnalyzerBehavior) {
    auto analyzer = AnalyzerFactory::createEnglish();

    auto tokens = analyzer->analyze("The café has résumé service");

    // English analyzer: tokenize + lowercase + ASCII folding + stop words
    // Should have: ["cafe", "resume", "service"]
    // (removes "the", "has", folds accents)

    bool has_cafe = false;
    bool has_resume = false;
    bool has_service = false;

    for (const auto& token : tokens) {
        if (token.getText() == "cafe") has_cafe = true;
        if (token.getText() == "resume") has_resume = true;
        if (token.getText() == "service") has_service = true;
    }

    EXPECT_TRUE(has_cafe);
    EXPECT_TRUE(has_resume);
    EXPECT_TRUE(has_service);
}

TEST(AnalyzerFactoryTest, MultilingualAnalyzerBehavior) {
    auto analyzer = AnalyzerFactory::createMultilingual();

    auto tokens = analyzer->analyze("Hello café");

    // Multilingual analyzer: tokenize + lowercase + ASCII folding (no stop words)
    ASSERT_EQ(tokens.size(), 2);
    EXPECT_EQ(tokens[0].getText(), "hello");
    EXPECT_EQ(tokens[1].getText(), "cafe");
}

TEST(AnalyzerFactoryTest, SearchAnalyzerBehavior) {
    auto analyzer = AnalyzerFactory::createSearch();

    auto tokens = analyzer->analyze("The café résumé");

    // Search analyzer optimized for queries
    // Behavior depends on implementation, but should handle accents

    ASSERT_GE(tokens.size(), 1);

    // Should have processed the text
    bool has_processed = false;
    for (const auto& token : tokens) {
        if (!token.getText().empty()) {
            has_processed = true;
            break;
        }
    }

    EXPECT_TRUE(has_processed);
}

TEST(AnalyzerFactoryTest, IndependentInstances) {
    auto analyzer1 = AnalyzerFactory::createStandard();
    auto analyzer2 = AnalyzerFactory::createStandard();

    // Each call should create a new instance
    EXPECT_NE(analyzer1.get(), analyzer2.get());
}

TEST(AnalyzerFactoryTest, AllAnalyzersHaveComponents) {
    std::vector<std::unique_ptr<Analyzer>> analyzers;
    analyzers.push_back(AnalyzerFactory::createStandard());
    analyzers.push_back(AnalyzerFactory::createSimple());
    analyzers.push_back(AnalyzerFactory::createWhitespace());
    analyzers.push_back(AnalyzerFactory::createKeyword());
    analyzers.push_back(AnalyzerFactory::createEnglish());
    analyzers.push_back(AnalyzerFactory::createMultilingual());
    analyzers.push_back(AnalyzerFactory::createSearch());

    for (const auto& analyzer : analyzers) {
        // Each analyzer should have a tokenizer name
        EXPECT_FALSE(analyzer->getTokenizerName().empty());

        // Each analyzer should have a description
        EXPECT_FALSE(analyzer->description().empty());
    }
}

TEST(AnalyzerFactoryTest, EmptyTextHandling) {
    auto analyzers = {
        AnalyzerFactory::createStandard(),
        AnalyzerFactory::createSimple(),
        AnalyzerFactory::createWhitespace(),
        AnalyzerFactory::createKeyword(),
        AnalyzerFactory::createEnglish(),
        AnalyzerFactory::createMultilingual(),
        AnalyzerFactory::createSearch()
    };

    for (auto& analyzer : analyzers) {
        auto tokens = analyzer->analyze("");
        // All analyzers should handle empty text gracefully
        EXPECT_TRUE(tokens.empty() || tokens.size() == 0);
    }
}
