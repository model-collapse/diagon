#include <gtest/gtest.h>
#include "analysis/KeywordTokenizer.h"

using namespace diagon::analysis;

TEST(KeywordTokenizerTest, BasicTokenization) {
    KeywordTokenizer tokenizer;

    auto tokens = tokenizer.tokenize("hello world");

    // KeywordTokenizer should produce single token
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0].getText(), "hello world");
}

TEST(KeywordTokenizerTest, EmptyText) {
    KeywordTokenizer tokenizer;

    auto tokens = tokenizer.tokenize("");

    EXPECT_TRUE(tokens.empty());
}

TEST(KeywordTokenizerTest, WhitespacePreserved) {
    KeywordTokenizer tokenizer;

    auto tokens = tokenizer.tokenize("hello   world\t\ttest");

    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0].getText(), "hello   world\t\ttest");
}

TEST(KeywordTokenizerTest, PunctuationPreserved) {
    KeywordTokenizer tokenizer;

    auto tokens = tokenizer.tokenize("hello, world! how are you?");

    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0].getText(), "hello, world! how are you?");
}

TEST(KeywordTokenizerTest, UnicodeText) {
    KeywordTokenizer tokenizer;

    auto tokens = tokenizer.tokenize("café résumé naïve");

    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0].getText(), "café résumé naïve");
}

TEST(KeywordTokenizerTest, ChineseText) {
    KeywordTokenizer tokenizer;

    auto tokens = tokenizer.tokenize("我爱北京天安门");

    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0].getText(), "我爱北京天安门");
}

TEST(KeywordTokenizerTest, NewlinesPreserved) {
    KeywordTokenizer tokenizer;

    auto tokens = tokenizer.tokenize("hello\nworld\ntest");

    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0].getText(), "hello\nworld\ntest");
}

TEST(KeywordTokenizerTest, OffsetCorrectness) {
    KeywordTokenizer tokenizer;

    auto tokens = tokenizer.tokenize("hello world");

    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0].getStartOffset(), 0);
    EXPECT_EQ(tokens[0].getEndOffset(), 11);
}

TEST(KeywordTokenizerTest, LongText) {
    KeywordTokenizer tokenizer;

    std::string long_text(10000, 'a');
    auto tokens = tokenizer.tokenize(long_text);

    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0].getText().length(), 10000);
    EXPECT_EQ(tokens[0].getStartOffset(), 0);
    EXPECT_EQ(tokens[0].getEndOffset(), 10000);
}

TEST(KeywordTokenizerTest, TokenName) {
    KeywordTokenizer tokenizer;

    EXPECT_EQ(tokenizer.name(), "keyword");
}

TEST(KeywordTokenizerTest, OnlyWhitespace) {
    KeywordTokenizer tokenizer;

    auto tokens = tokenizer.tokenize("   \t\n  ");

    // Even whitespace-only text produces a token
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0].getText(), "   \t\n  ");
}

TEST(KeywordTokenizerTest, SpecialCharacters) {
    KeywordTokenizer tokenizer;

    auto tokens = tokenizer.tokenize("test@example.com:8080/path?query=value");

    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0].getText(), "test@example.com:8080/path?query=value");
}
