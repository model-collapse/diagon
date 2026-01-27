#include <gtest/gtest.h>
#include "analysis/Analyzer.h"
#include "analysis/WhitespaceTokenizer.h"
#include "analysis/KeywordTokenizer.h"
#include "analysis/LowercaseFilter.h"
#include "analysis/StopFilter.h"
#include "analysis/ASCIIFoldingFilter.h"

using namespace diagon::analysis;

TEST(CompositeAnalyzerTest, NoFilters) {
    auto tokenizer = std::make_unique<WhitespaceTokenizer>();

    CompositeAnalyzer analyzer(
        "test",
        std::move(tokenizer),
        std::vector<std::unique_ptr<TokenFilter>>()
    );

    auto tokens = analyzer.analyze("HELLO WORLD");

    ASSERT_EQ(tokens.size(), 2);
    EXPECT_EQ(tokens[0].getText(), "HELLO");  // Not lowercased
    EXPECT_EQ(tokens[1].getText(), "WORLD");
}

TEST(CompositeAnalyzerTest, SingleFilter) {
    auto tokenizer = std::make_unique<WhitespaceTokenizer>();

    std::vector<std::unique_ptr<TokenFilter>> filters;
    filters.push_back(std::make_unique<LowercaseFilter>());

    CompositeAnalyzer analyzer("test", std::move(tokenizer), std::move(filters));

    auto tokens = analyzer.analyze("HELLO WORLD");

    ASSERT_EQ(tokens.size(), 2);
    EXPECT_EQ(tokens[0].getText(), "hello");  // Lowercased
    EXPECT_EQ(tokens[1].getText(), "world");
}

TEST(CompositeAnalyzerTest, MultipleFilters) {
    auto tokenizer = std::make_unique<WhitespaceTokenizer>();

    std::vector<std::unique_ptr<TokenFilter>> filters;
    filters.push_back(std::make_unique<LowercaseFilter>());
    filters.push_back(std::make_unique<ASCIIFoldingFilter>());

    CompositeAnalyzer analyzer("test", std::move(tokenizer), std::move(filters));

    auto tokens = analyzer.analyze("CAFÉ RÉSUMÉ");

    ASSERT_EQ(tokens.size(), 2);
    EXPECT_EQ(tokens[0].getText(), "cafe");    // Lowercased + ASCII folded
    EXPECT_EQ(tokens[1].getText(), "resume");
}

TEST(CompositeAnalyzerTest, FilterChainOrdering) {
    // Test that filters are applied in order
    auto tokenizer = std::make_unique<WhitespaceTokenizer>();

    std::vector<std::unique_ptr<TokenFilter>> filters;
    filters.push_back(std::make_unique<ASCIIFoldingFilter>());
    filters.push_back(std::make_unique<LowercaseFilter>());

    CompositeAnalyzer analyzer("test", std::move(tokenizer), std::move(filters));

    auto tokens = analyzer.analyze("CAFÉ");

    ASSERT_EQ(tokens.size(), 1);
    // Should be: CAFÉ -> Cafe (ASCII fold) -> cafe (lowercase)
    EXPECT_EQ(tokens[0].getText(), "cafe");
}

TEST(CompositeAnalyzerTest, WithStopFilter) {
    auto tokenizer = std::make_unique<WhitespaceTokenizer>();

    std::vector<std::unique_ptr<TokenFilter>> filters;
    filters.push_back(std::make_unique<LowercaseFilter>());
    filters.push_back(std::make_unique<StopFilter>(StopFilter::StopWordSet::ENGLISH));

    CompositeAnalyzer analyzer("test", std::move(tokenizer), std::move(filters));

    auto tokens = analyzer.analyze("The quick brown fox");

    // "The" (lowercased to "the") should be removed
    ASSERT_EQ(tokens.size(), 3);
    EXPECT_EQ(tokens[0].getText(), "quick");
    EXPECT_EQ(tokens[1].getText(), "brown");
    EXPECT_EQ(tokens[2].getText(), "fox");
}

TEST(CompositeAnalyzerTest, EmptyText) {
    auto tokenizer = std::make_unique<WhitespaceTokenizer>();

    CompositeAnalyzer analyzer(
        "test",
        std::move(tokenizer),
        std::vector<std::unique_ptr<TokenFilter>>()
    );

    auto tokens = analyzer.analyze("");

    EXPECT_TRUE(tokens.empty());
}

TEST(CompositeAnalyzerTest, KeywordTokenizer) {
    auto tokenizer = std::make_unique<KeywordTokenizer>();

    std::vector<std::unique_ptr<TokenFilter>> filters;
    filters.push_back(std::make_unique<LowercaseFilter>());

    CompositeAnalyzer analyzer("test", std::move(tokenizer), std::move(filters));

    auto tokens = analyzer.analyze("HELLO WORLD");

    // KeywordTokenizer treats entire text as single token
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0].getText(), "hello world");
}

TEST(CompositeAnalyzerTest, Name) {
    auto tokenizer = std::make_unique<WhitespaceTokenizer>();

    CompositeAnalyzer analyzer(
        "custom_analyzer",
        std::move(tokenizer),
        std::vector<std::unique_ptr<TokenFilter>>()
    );

    EXPECT_EQ(analyzer.name(), "custom_analyzer");
}

TEST(CompositeAnalyzerTest, GetTokenizerName) {
    auto tokenizer = std::make_unique<WhitespaceTokenizer>();

    CompositeAnalyzer analyzer(
        "test",
        std::move(tokenizer),
        std::vector<std::unique_ptr<TokenFilter>>()
    );

    EXPECT_EQ(analyzer.getTokenizerName(), "whitespace");
}

TEST(CompositeAnalyzerTest, GetFilterNames) {
    auto tokenizer = std::make_unique<WhitespaceTokenizer>();

    std::vector<std::unique_ptr<TokenFilter>> filters;
    filters.push_back(std::make_unique<LowercaseFilter>());
    filters.push_back(std::make_unique<ASCIIFoldingFilter>());

    CompositeAnalyzer analyzer("test", std::move(tokenizer), std::move(filters));

    auto filter_names = analyzer.getFilterNames();

    ASSERT_EQ(filter_names.size(), 2);
    EXPECT_EQ(filter_names[0], "lowercase");
    EXPECT_EQ(filter_names[1], "asciifolding");
}

TEST(CompositeAnalyzerTest, Description) {
    auto tokenizer = std::make_unique<WhitespaceTokenizer>();

    std::vector<std::unique_ptr<TokenFilter>> filters;
    filters.push_back(std::make_unique<LowercaseFilter>());

    CompositeAnalyzer analyzer("test", std::move(tokenizer), std::move(filters));

    auto desc = analyzer.description();

    // Description should contain tokenizer and filter info
    EXPECT_FALSE(desc.empty());
}

TEST(CompositeAnalyzerTest, ComplexChain) {
    auto tokenizer = std::make_unique<WhitespaceTokenizer>();

    std::vector<std::unique_ptr<TokenFilter>> filters;
    filters.push_back(std::make_unique<ASCIIFoldingFilter>());
    filters.push_back(std::make_unique<LowercaseFilter>());
    filters.push_back(std::make_unique<StopFilter>(StopFilter::StopWordSet::ENGLISH));

    CompositeAnalyzer analyzer("test", std::move(tokenizer), std::move(filters));

    auto tokens = analyzer.analyze("The CAFÉ has a RÉSUMÉ service");

    // Expected:
    // 1. Tokenize: ["The", "CAFÉ", "has", "a", "RÉSUMÉ", "service"]
    // 2. ASCII fold: ["The", "CAFE", "has", "a", "RESUME", "service"]
    // 3. Lowercase: ["the", "cafe", "has", "a", "resume", "service"]
    // 4. Remove stops: ["cafe", "resume", "service"] (removes "the", "has", "a")

    ASSERT_EQ(tokens.size(), 3);
    EXPECT_EQ(tokens[0].getText(), "cafe");
    EXPECT_EQ(tokens[1].getText(), "resume");
    EXPECT_EQ(tokens[2].getText(), "service");
}

TEST(CompositeAnalyzerTest, LargeText) {
    auto tokenizer = std::make_unique<WhitespaceTokenizer>();

    std::vector<std::unique_ptr<TokenFilter>> filters;
    filters.push_back(std::make_unique<LowercaseFilter>());

    CompositeAnalyzer analyzer("test", std::move(tokenizer), std::move(filters));

    // Generate large text with 10000 words
    std::string text;
    for (int i = 0; i < 10000; ++i) {
        if (i > 0) text += " ";
        text += "WORD" + std::to_string(i);
    }

    auto tokens = analyzer.analyze(text);

    EXPECT_EQ(tokens.size(), 10000);
    for (int i = 0; i < 10000; ++i) {
        std::string expected = "word" + std::to_string(i);
        EXPECT_EQ(tokens[i].getText(), expected);
    }
}

TEST(CompositeAnalyzerTest, OffsetsPreserved) {
    auto tokenizer = std::make_unique<WhitespaceTokenizer>();

    std::vector<std::unique_ptr<TokenFilter>> filters;
    filters.push_back(std::make_unique<LowercaseFilter>());

    CompositeAnalyzer analyzer("test", std::move(tokenizer), std::move(filters));

    auto tokens = analyzer.analyze("HELLO WORLD");

    ASSERT_EQ(tokens.size(), 2);

    // Check offsets are preserved through filter chain
    EXPECT_EQ(tokens[0].getStartOffset(), 0);
    EXPECT_EQ(tokens[0].getEndOffset(), 5);
    EXPECT_EQ(tokens[1].getStartOffset(), 6);
    EXPECT_EQ(tokens[1].getEndOffset(), 11);
}
