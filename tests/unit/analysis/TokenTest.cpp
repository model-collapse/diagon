#include "analysis/Token.h"

#include <gtest/gtest.h>

using namespace diagon::analysis;

TEST(TokenTest, BasicConstruction) {
    Token token("hello", 0, 0, 5);

    EXPECT_EQ(token.getText(), "hello");
    EXPECT_EQ(token.getPosition(), 0);
    EXPECT_EQ(token.getStartOffset(), 0);
    EXPECT_EQ(token.getEndOffset(), 5);
}

TEST(TokenTest, ConstructionWithPosition) {
    Token token("test", 2, 10, 14);

    EXPECT_EQ(token.getText(), "test");
    EXPECT_EQ(token.getPosition(), 2);
    EXPECT_EQ(token.getStartOffset(), 10);
    EXPECT_EQ(token.getEndOffset(), 14);
}

TEST(TokenTest, EmptyText) {
    Token token("", 0, 0, 0);

    EXPECT_TRUE(token.getText().empty());
    EXPECT_EQ(token.getStartOffset(), 0);
    EXPECT_EQ(token.getEndOffset(), 0);
    EXPECT_TRUE(token.empty());
}

TEST(TokenTest, UnicodeText) {
    // UTF-8 encoded text
    Token token("café", 0, 0, 5);

    EXPECT_EQ(token.getText(), "café");
    EXPECT_EQ(token.getStartOffset(), 0);
    EXPECT_EQ(token.getEndOffset(), 5);
}

TEST(TokenTest, ChineseText) {
    // Chinese characters in UTF-8
    Token token("北京", 0, 0, 6);

    EXPECT_EQ(token.getText(), "北京");
    EXPECT_EQ(token.getStartOffset(), 0);
    EXPECT_EQ(token.getEndOffset(), 6);
}

TEST(TokenTest, TokenType) {
    Token token("hello", 0, 0, 5);
    token.setType("word");

    EXPECT_EQ(token.getType(), "word");
}

TEST(TokenTest, DifferentTypes) {
    Token word("hello", 0, 0, 5);
    word.setType("word");

    Token num("123", 1, 6, 9);
    num.setType("number");

    Token alphanum("abc123", 2, 10, 16);
    alphanum.setType("alphanum");

    EXPECT_EQ(word.getType(), "word");
    EXPECT_EQ(num.getType(), "number");
    EXPECT_EQ(alphanum.getType(), "alphanum");
}

TEST(TokenTest, DefaultConstructor) {
    Token token;

    EXPECT_TRUE(token.empty());
    EXPECT_EQ(token.getText(), "");
    EXPECT_EQ(token.getPosition(), 0);
}

TEST(TokenTest, CopyConstructor) {
    Token t1("test", 2, 0, 4);
    t1.setType("word");

    Token t2 = t1;

    EXPECT_EQ(t2.getText(), "test");
    EXPECT_EQ(t2.getPosition(), 2);
    EXPECT_EQ(t2.getStartOffset(), 0);
    EXPECT_EQ(t2.getEndOffset(), 4);
    EXPECT_EQ(t2.getType(), "word");
}

TEST(TokenTest, MoveConstructor) {
    Token t1("test", 2, 0, 4);
    t1.setType("word");

    Token t2 = std::move(t1);

    EXPECT_EQ(t2.getText(), "test");
    EXPECT_EQ(t2.getPosition(), 2);
    EXPECT_EQ(t2.getStartOffset(), 0);
    EXPECT_EQ(t2.getEndOffset(), 4);
    EXPECT_EQ(t2.getType(), "word");
}

TEST(TokenTest, CopyAssignment) {
    Token t1("test", 0, 0, 4);
    t1.setType("word");

    Token t2("other", 1, 5, 10);
    t2.setType("number");

    t2 = t1;

    EXPECT_EQ(t2.getText(), "test");
    EXPECT_EQ(t2.getPosition(), 0);
    EXPECT_EQ(t2.getStartOffset(), 0);
    EXPECT_EQ(t2.getEndOffset(), 4);
    EXPECT_EQ(t2.getType(), "word");
}

TEST(TokenTest, MoveAssignment) {
    Token t1("test", 0, 0, 4);
    t1.setType("word");

    Token t2("other", 1, 5, 10);
    t2.setType("number");

    t2 = std::move(t1);

    EXPECT_EQ(t2.getText(), "test");
    EXPECT_EQ(t2.getPosition(), 0);
    EXPECT_EQ(t2.getStartOffset(), 0);
    EXPECT_EQ(t2.getEndOffset(), 4);
    EXPECT_EQ(t2.getType(), "word");
}

TEST(TokenTest, LargeOffsets) {
    // Test with large document offsets
    Token token("word", 100, 1000000, 1000004);

    EXPECT_EQ(token.getStartOffset(), 1000000);
    EXPECT_EQ(token.getEndOffset(), 1000004);
    EXPECT_EQ(token.getPosition(), 100);
}

TEST(TokenTest, Length) {
    Token token("hello", 0, 0, 5);

    EXPECT_EQ(token.length(), 5);
}

TEST(TokenTest, LongText) {
    // Test with very long token text
    std::string long_text(10000, 'a');
    Token token(long_text, 0, 0, 10000);

    EXPECT_EQ(token.getText().length(), 10000);
    EXPECT_EQ(token.length(), 10000);
    EXPECT_EQ(token.getText(), long_text);
}

TEST(TokenTest, EqualityOperator) {
    Token t1("test", 0, 0, 4);
    Token t2("test", 0, 0, 4);
    Token t3("other", 0, 0, 5);

    EXPECT_EQ(t1, t2);
    EXPECT_NE(t1, t3);
}

TEST(TokenTest, InequalityOperator) {
    Token t1("test", 0, 0, 4);
    Token t2("other", 0, 0, 5);

    EXPECT_NE(t1, t2);
}

TEST(TokenTest, Setters) {
    Token token("hello", 0, 0, 5);

    token.setText("world");
    token.setPosition(10);
    token.setStartOffset(100);
    token.setEndOffset(105);
    token.setType("word");

    EXPECT_EQ(token.getText(), "world");
    EXPECT_EQ(token.getPosition(), 10);
    EXPECT_EQ(token.getStartOffset(), 100);
    EXPECT_EQ(token.getEndOffset(), 105);
    EXPECT_EQ(token.getType(), "word");
}
