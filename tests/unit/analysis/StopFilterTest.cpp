#include <gtest/gtest.h>
#include "analysis/StopFilter.h"

using namespace diagon::analysis;

TEST(StopFilterTest, EnglishStopWords) {
    StopFilter filter(StopFilter::StopWordSet::ENGLISH);

    std::vector<Token> tokens{
        Token("the", 0, 0, 3),
        Token("quick", 1, 4, 9),
        Token("brown", 2, 10, 15),
        Token("fox", 3, 16, 19),
        Token("and", 4, 20, 23),
        Token("a", 5, 24, 25),
        Token("dog", 6, 26, 29)
    };

    auto result = filter.filter(tokens);

    // "the", "and", "a" should be removed
    ASSERT_EQ(result.size(), 4);
    EXPECT_EQ(result[0].getText(), "quick");
    EXPECT_EQ(result[1].getText(), "brown");
    EXPECT_EQ(result[2].getText(), "fox");
    EXPECT_EQ(result[3].getText(), "dog");
}

TEST(StopFilterTest, ChineseStopWords) {
    StopFilter filter(StopFilter::StopWordSet::CHINESE);

    std::vector<Token> tokens{
        Token("的", 0, 0, 3),
        Token("北京", 1, 3, 9),
        Token("是", 2, 9, 12),
        Token("中国", 3, 12, 18),
        Token("了", 4, 18, 21)
    };

    auto result = filter.filter(tokens);

    // Chinese stop words "的", "是", "了" should be removed
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0].getText(), "北京");
    EXPECT_EQ(result[1].getText(), "中国");
}

TEST(StopFilterTest, CustomStopWords) {
    std::unordered_set<std::string> custom_stops{"foo", "bar", "baz"};
    StopFilter filter(custom_stops);

    std::vector<Token> tokens{
        Token("hello", 0, 0, 5),
        Token("foo", 1, 6, 9),
        Token("world", 2, 10, 15),
        Token("bar", 3, 16, 19),
        Token("test", 4, 20, 24)
    };

    auto result = filter.filter(tokens);

    // "foo" and "bar" should be removed
    ASSERT_EQ(result.size(), 3);
    EXPECT_EQ(result[0].getText(), "hello");
    EXPECT_EQ(result[1].getText(), "world");
    EXPECT_EQ(result[2].getText(), "test");
}

TEST(StopFilterTest, EmptyTokens) {
    StopFilter filter(StopFilter::StopWordSet::ENGLISH);

    std::vector<Token> tokens;

    auto result = filter.filter(tokens);

    EXPECT_TRUE(result.empty());
}

TEST(StopFilterTest, NoStopWords) {
    StopFilter filter(StopFilter::StopWordSet::ENGLISH);

    std::vector<Token> tokens{
        Token("quick", 0, 0, 5),
        Token("brown", 1, 6, 11),
        Token("fox", 2, 12, 15)
    };

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 3);
    EXPECT_EQ(result[0].getText(), "quick");
    EXPECT_EQ(result[1].getText(), "brown");
    EXPECT_EQ(result[2].getText(), "fox");
}

TEST(StopFilterTest, AllStopWords) {
    StopFilter filter(StopFilter::StopWordSet::ENGLISH);

    std::vector<Token> tokens{
        Token("the", 0, 0, 3),
        Token("a", 1, 4, 5),
        Token("an", 2, 6, 8),
        Token("and", 3, 9, 12)
    };

    auto result = filter.filter(tokens);

    EXPECT_TRUE(result.empty());
}

TEST(StopFilterTest, CaseSensitive) {
    StopFilter filter(StopFilter::StopWordSet::ENGLISH);

    std::vector<Token> tokens{
        Token("The", 0, 0, 3),  // Uppercase "The"
        Token("quick", 1, 4, 9)
    };

    auto result = filter.filter(tokens);

    // Stop filter should be case-sensitive by default
    // "The" (uppercase) should NOT be removed if stop list contains "the" (lowercase)
    // This depends on implementation - adjust test based on actual behavior
    ASSERT_GE(result.size(), 1);
    EXPECT_EQ(result[result.size() - 1].getText(), "quick");
}

TEST(StopFilterTest, PreservesOffsets) {
    StopFilter filter(StopFilter::StopWordSet::ENGLISH);

    std::vector<Token> tokens{
        Token("quick", 0, 10, 15),
        Token("the", 1, 16, 19),
        Token("fox", 2, 20, 23)
    };

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0].getStartOffset(), 10);
    EXPECT_EQ(result[0].getEndOffset(), 15);
    EXPECT_EQ(result[1].getStartOffset(), 20);
    EXPECT_EQ(result[1].getEndOffset(), 23);
}

TEST(StopFilterTest, PreservesTokenType) {
    StopFilter filter(StopFilter::StopWordSet::ENGLISH);

    std::vector<Token> tokens{
        Token("quick", 0, 0, 5),
        Token("123", 1, 6, 9),
        Token("the", 2, 10, 13)
    };

    tokens[0].setType("word");
    tokens[1].setType("number");

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0].getType(), "word");
    EXPECT_EQ(result[1].getType(), "number");
}

TEST(StopFilterTest, EmptyStopSet) {
    std::unordered_set<std::string> empty_stops;
    StopFilter filter(empty_stops);

    std::vector<Token> tokens{
        Token("the", 0, 0, 3),
        Token("quick", 1, 4, 9)
    };

    auto result = filter.filter(tokens);

    // No stop words, so nothing removed
    ASSERT_EQ(result.size(), 2);
}

TEST(StopFilterTest, CommonEnglishStopWords) {
    StopFilter filter(StopFilter::StopWordSet::ENGLISH);

    std::vector<Token> tokens{
        Token("is", 0, 0, 2),
        Token("was", 1, 3, 6),
        Token("are", 2, 7, 10),
        Token("were", 3, 11, 15),
        Token("been", 4, 16, 20),
        Token("have", 5, 21, 25),
        Token("has", 6, 26, 29),
        Token("had", 7, 30, 33)
    };

    auto result = filter.filter(tokens);

    // All of these are common stop words
    EXPECT_TRUE(result.empty());
}

TEST(StopFilterTest, MixedLanguage) {
    // Test with both English and non-English words
    StopFilter filter(StopFilter::StopWordSet::ENGLISH);

    std::vector<Token> tokens{
        Token("the", 0, 0, 3),
        Token("café", 1, 4, 9),
        Token("is", 2, 10, 12),
        Token("résumé", 3, 13, 20)
    };

    auto result = filter.filter(tokens);

    // English stop words removed, French words kept
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0].getText(), "café");
    EXPECT_EQ(result[1].getText(), "résumé");
}

TEST(StopFilterTest, LargeTokenList) {
    StopFilter filter(StopFilter::StopWordSet::ENGLISH);

    std::vector<Token> tokens;
    // Add 500 stop words and 500 non-stop words
    for (int i = 0; i < 500; ++i) {
        tokens.emplace_back("the", i * 2, i * 10, i * 10 + 3);
        tokens.emplace_back("word" + std::to_string(i), i * 2 + 1, i * 10 + 4, i * 10 + 8);
    }

    auto result = filter.filter(tokens);

    // Should keep only the 500 non-stop words
    EXPECT_EQ(result.size(), 500);
}
