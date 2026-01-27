#include <gtest/gtest.h>
#include "analysis/WhitespaceTokenizer.h"

using namespace diagon::analysis;

TEST(WhitespaceTokenizerTest, BasicTokenization) {
    WhitespaceTokenizer tokenizer;

    auto tokens = tokenizer.tokenize("hello world test");

    ASSERT_EQ(tokens.size(), 3);
    EXPECT_EQ(tokens[0].getText(), "hello");
    EXPECT_EQ(tokens[1].getText(), "world");
    EXPECT_EQ(tokens[2].getText(), "test");
}

TEST(WhitespaceTokenizerTest, EmptyText) {
    WhitespaceTokenizer tokenizer;

    auto tokens = tokenizer.tokenize("");

    EXPECT_TRUE(tokens.empty());
}

TEST(WhitespaceTokenizerTest, OnlyWhitespace) {
    WhitespaceTokenizer tokenizer;

    auto tokens = tokenizer.tokenize("   \t\n  ");

    EXPECT_TRUE(tokens.empty());
}

TEST(WhitespaceTokenizerTest, MultipleWhitespace) {
    WhitespaceTokenizer tokenizer;

    auto tokens = tokenizer.tokenize("hello    world\t\ttest\n\nfoo");

    ASSERT_EQ(tokens.size(), 4);
    EXPECT_EQ(tokens[0].getText(), "hello");
    EXPECT_EQ(tokens[1].getText(), "world");
    EXPECT_EQ(tokens[2].getText(), "test");
    EXPECT_EQ(tokens[3].getText(), "foo");
}

TEST(WhitespaceTokenizerTest, LeadingWhitespace) {
    WhitespaceTokenizer tokenizer;

    auto tokens = tokenizer.tokenize("  hello world");

    ASSERT_EQ(tokens.size(), 2);
    EXPECT_EQ(tokens[0].getText(), "hello");
    EXPECT_EQ(tokens[1].getText(), "world");
}

TEST(WhitespaceTokenizerTest, TrailingWhitespace) {
    WhitespaceTokenizer tokenizer;

    auto tokens = tokenizer.tokenize("hello world  ");

    ASSERT_EQ(tokens.size(), 2);
    EXPECT_EQ(tokens[0].getText(), "hello");
    EXPECT_EQ(tokens[1].getText(), "world");
}

TEST(WhitespaceTokenizerTest, UnicodeText) {
    WhitespaceTokenizer tokenizer;

    auto tokens = tokenizer.tokenize("café résumé naïve");

    ASSERT_EQ(tokens.size(), 3);
    EXPECT_EQ(tokens[0].getText(), "café");
    EXPECT_EQ(tokens[1].getText(), "résumé");
    EXPECT_EQ(tokens[2].getText(), "naïve");
}

TEST(WhitespaceTokenizerTest, ChineseText) {
    WhitespaceTokenizer tokenizer;

    auto tokens = tokenizer.tokenize("我爱 北京 天安门");

    ASSERT_EQ(tokens.size(), 3);
    EXPECT_EQ(tokens[0].getText(), "我爱");
    EXPECT_EQ(tokens[1].getText(), "北京");
    EXPECT_EQ(tokens[2].getText(), "天安门");
}

TEST(WhitespaceTokenizerTest, PunctuationNotSplit) {
    WhitespaceTokenizer tokenizer;

    auto tokens = tokenizer.tokenize("hello, world! test?");

    ASSERT_EQ(tokens.size(), 3);
    EXPECT_EQ(tokens[0].getText(), "hello,");
    EXPECT_EQ(tokens[1].getText(), "world!");
    EXPECT_EQ(tokens[2].getText(), "test?");
}

TEST(WhitespaceTokenizerTest, OffsetCorrectness) {
    WhitespaceTokenizer tokenizer;

    auto tokens = tokenizer.tokenize("hello world");

    ASSERT_EQ(tokens.size(), 2);

    // "hello" at position 0-5
    EXPECT_EQ(tokens[0].getStartOffset(), 0);
    EXPECT_EQ(tokens[0].getEndOffset(), 5);

    // "world" at position 6-11
    EXPECT_EQ(tokens[1].getStartOffset(), 6);
    EXPECT_EQ(tokens[1].getEndOffset(), 11);
}

TEST(WhitespaceTokenizerTest, OffsetWithMultipleSpaces) {
    WhitespaceTokenizer tokenizer;

    auto tokens = tokenizer.tokenize("hello   world");

    ASSERT_EQ(tokens.size(), 2);

    // "hello" at position 0-5
    EXPECT_EQ(tokens[0].getStartOffset(), 0);
    EXPECT_EQ(tokens[0].getEndOffset(), 5);

    // "world" at position 8-13
    EXPECT_EQ(tokens[1].getStartOffset(), 8);
    EXPECT_EQ(tokens[1].getEndOffset(), 13);
}

TEST(WhitespaceTokenizerTest, SingleToken) {
    WhitespaceTokenizer tokenizer;

    auto tokens = tokenizer.tokenize("hello");

    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0].getText(), "hello");
    EXPECT_EQ(tokens[0].getStartOffset(), 0);
    EXPECT_EQ(tokens[0].getEndOffset(), 5);
}

TEST(WhitespaceTokenizerTest, TabsAndNewlines) {
    WhitespaceTokenizer tokenizer;

    auto tokens = tokenizer.tokenize("hello\tworld\ntest\rfoo");

    ASSERT_EQ(tokens.size(), 4);
    EXPECT_EQ(tokens[0].getText(), "hello");
    EXPECT_EQ(tokens[1].getText(), "world");
    EXPECT_EQ(tokens[2].getText(), "test");
    EXPECT_EQ(tokens[3].getText(), "foo");
}

TEST(WhitespaceTokenizerTest, LongText) {
    WhitespaceTokenizer tokenizer;

    // Create text with 1000 words
    std::string text;
    for (int i = 0; i < 1000; ++i) {
        if (i > 0) text += " ";
        text += "word" + std::to_string(i);
    }

    auto tokens = tokenizer.tokenize(text);

    EXPECT_EQ(tokens.size(), 1000);
}

TEST(WhitespaceTokenizerTest, TokenType) {
    WhitespaceTokenizer tokenizer;

    auto tokens = tokenizer.tokenize("hello world");

    ASSERT_EQ(tokens.size(), 2);
    // Check if tokens have a type set (implementation dependent)
    // Type may be empty or "word" depending on implementation
}
