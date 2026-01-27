#include <gtest/gtest.h>
#include "analysis/LowercaseFilter.h"

using namespace diagon::analysis;

TEST(LowercaseFilterTest, BasicLowercase) {
    LowercaseFilter filter;

    std::vector<Token> tokens{
        Token("HELLO", 0, 0, 5),
        Token("World", 1, 6, 11)
    };

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0].getText(), "hello");
    EXPECT_EQ(result[1].getText(), "world");
}

TEST(LowercaseFilterTest, EmptyTokens) {
    LowercaseFilter filter;

    std::vector<Token> tokens;

    auto result = filter.filter(tokens);

    EXPECT_TRUE(result.empty());
}

TEST(LowercaseFilterTest, AlreadyLowercase) {
    LowercaseFilter filter;

    std::vector<Token> tokens{
        Token("hello", 0, 0, 5),
        Token("world", 1, 6, 11)
    };

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0].getText(), "hello");
    EXPECT_EQ(result[1].getText(), "world");
}

TEST(LowercaseFilterTest, MixedCase) {
    LowercaseFilter filter;

    std::vector<Token> tokens{
        Token("HeLLo", 0, 0, 5),
        Token("WoRLd", 1, 6, 11)
    };

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0].getText(), "hello");
    EXPECT_EQ(result[1].getText(), "world");
}

TEST(LowercaseFilterTest, UnicodeUppercase) {
    LowercaseFilter filter;

    std::vector<Token> tokens{
        Token("CAFÉ", 0, 0, 5),
        Token("RÉSUMÉ", 1, 6, 13)
    };

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0].getText(), "café");
    EXPECT_EQ(result[1].getText(), "résumé");
}

TEST(LowercaseFilterTest, PreservesOffsets) {
    LowercaseFilter filter;

    std::vector<Token> tokens{
        Token("HELLO", 0, 10, 15),
        Token("WORLD", 1, 20, 25)
    };

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0].getText(), "hello");
    EXPECT_EQ(result[0].getStartOffset(), 10);
    EXPECT_EQ(result[0].getEndOffset(), 15);
    EXPECT_EQ(result[1].getText(), "world");
    EXPECT_EQ(result[1].getStartOffset(), 20);
    EXPECT_EQ(result[1].getEndOffset(), 25);
}

TEST(LowercaseFilterTest, PreservesTokenType) {
    LowercaseFilter filter;

    std::vector<Token> tokens{
        Token("HELLO", 0, 0, 5),
        Token("123", 1, 6, 9),
        Token("ABC123", 2, 10, 16)
    };

    // Set types
    tokens[0].setType("word");
    tokens[1].setType("number");
    tokens[2].setType("alphanum");

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 3);
    EXPECT_EQ(result[0].getType(), "word");
    EXPECT_EQ(result[1].getType(), "number");
    EXPECT_EQ(result[2].getType(), "alphanum");
}

TEST(LowercaseFilterTest, PreservesPosition) {
    LowercaseFilter filter;

    std::vector<Token> tokens{
        Token("HELLO", 1, 0, 5),
        Token("WORLD", 2, 6, 11)
    };

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0].getPosition(), 1);
    EXPECT_EQ(result[1].getPosition(), 2);
}

TEST(LowercaseFilterTest, EmptyTokenText) {
    LowercaseFilter filter;

    std::vector<Token> tokens{
        Token("", 0, 0, 0)
    };

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 1);
    EXPECT_TRUE(result[0].getText().empty());
}

TEST(LowercaseFilterTest, NumbersUnchanged) {
    LowercaseFilter filter;

    std::vector<Token> tokens{
        Token("123", 0, 0, 3),
        Token("456", 1, 4, 7)
    };

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0].getText(), "123");
    EXPECT_EQ(result[1].getText(), "456");
}

TEST(LowercaseFilterTest, PunctuationUnchanged) {
    LowercaseFilter filter;

    std::vector<Token> tokens{
        Token("HELLO!", 0, 0, 6),
        Token("WORLD?", 1, 7, 13)
    };

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0].getText(), "hello!");
    EXPECT_EQ(result[1].getText(), "world?");
}

TEST(LowercaseFilterTest, GermanUmlaut) {
    LowercaseFilter filter;

    std::vector<Token> tokens{
        Token("ÜBER", 0, 0, 5),
        Token("SCHÖN", 1, 6, 12)
    };

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0].getText(), "über");
    EXPECT_EQ(result[1].getText(), "schön");
}

TEST(LowercaseFilterTest, GreekLetters) {
    LowercaseFilter filter;

    std::vector<Token> tokens{
        Token("ΑΒΓΔ", 0, 0, 8)
    };

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0].getText(), "αβγδ");
}

TEST(LowercaseFilterTest, LargeTokenList) {
    LowercaseFilter filter;

    std::vector<Token> tokens;
    for (int i = 0; i < 1000; ++i) {
        tokens.emplace_back("WORD" + std::to_string(i), i, i * 10, i * 10 + 4);
    }

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 1000);
    for (int i = 0; i < 1000; ++i) {
        std::string expected = "word" + std::to_string(i);
        EXPECT_EQ(result[i].getText(), expected);
    }
}
