#include <gtest/gtest.h>
#include "analysis/ASCIIFoldingFilter.h"

using namespace diagon::analysis;

TEST(ASCIIFoldingFilterTest, BasicAccents) {
    ASCIIFoldingFilter filter;

    std::vector<Token> tokens{
        Token("café", 0, 0, 5),
        Token("résumé", 1, 6, 13)
    };

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0].getText(), "cafe");
    EXPECT_EQ(result[1].getText(), "resume");
}

TEST(ASCIIFoldingFilterTest, EmptyTokens) {
    ASCIIFoldingFilter filter;

    std::vector<Token> tokens;

    auto result = filter.filter(tokens);

    EXPECT_TRUE(result.empty());
}

TEST(ASCIIFoldingFilterTest, AlreadyASCII) {
    ASCIIFoldingFilter filter;

    std::vector<Token> tokens{
        Token("hello", 0, 0, 5),
        Token("world", 1, 6, 11)
    };

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0].getText(), "hello");
    EXPECT_EQ(result[1].getText(), "world");
}

TEST(ASCIIFoldingFilterTest, FrenchAccents) {
    ASCIIFoldingFilter filter;

    std::vector<Token> tokens{
        Token("français", 0, 0, 9),
        Token("école", 1, 10, 16),
        Token("éléphant", 2, 17, 26)
    };

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 3);
    EXPECT_EQ(result[0].getText(), "francais");
    EXPECT_EQ(result[1].getText(), "ecole");
    EXPECT_EQ(result[2].getText(), "elephant");
}

TEST(ASCIIFoldingFilterTest, GermanUmlauts) {
    ASCIIFoldingFilter filter;

    std::vector<Token> tokens{
        Token("über", 0, 0, 5),
        Token("schön", 1, 6, 12),
        Token("Müller", 2, 13, 20)
    };

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 3);
    EXPECT_EQ(result[0].getText(), "uber");
    EXPECT_EQ(result[1].getText(), "schon");
    EXPECT_EQ(result[2].getText(), "Muller");
}

TEST(ASCIIFoldingFilterTest, SpanishAccents) {
    ASCIIFoldingFilter filter;

    std::vector<Token> tokens{
        Token("español", 0, 0, 8),
        Token("niño", 1, 9, 14),
        Token("años", 2, 15, 20)
    };

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 3);
    EXPECT_EQ(result[0].getText(), "espanol");
    EXPECT_EQ(result[1].getText(), "nino");
    EXPECT_EQ(result[2].getText(), "anos");
}

TEST(ASCIIFoldingFilterTest, PortugueseAccents) {
    ASCIIFoldingFilter filter;

    std::vector<Token> tokens{
        Token("português", 0, 0, 10),
        Token("ação", 1, 11, 16)
    };

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0].getText(), "portugues");
    EXPECT_EQ(result[1].getText(), "acao");
}

TEST(ASCIIFoldingFilterTest, ItalianAccents) {
    ASCIIFoldingFilter filter;

    std::vector<Token> tokens{
        Token("città", 0, 0, 6),
        Token("perché", 1, 7, 14)
    };

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0].getText(), "citta");
    EXPECT_EQ(result[1].getText(), "perche");
}

TEST(ASCIIFoldingFilterTest, NordicCharacters) {
    ASCIIFoldingFilter filter;

    std::vector<Token> tokens{
        Token("Ångström", 0, 0, 9),
        Token("Øyvind", 1, 10, 17),
        Token("Åse", 2, 18, 22)
    };

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 3);
    EXPECT_EQ(result[0].getText(), "Angstrom");
    EXPECT_EQ(result[1].getText(), "Oyvind");
    EXPECT_EQ(result[2].getText(), "Ase");
}

TEST(ASCIIFoldingFilterTest, PreservesOffsets) {
    ASCIIFoldingFilter filter;

    std::vector<Token> tokens{
        Token("café", 0, 10, 15),
        Token("résumé", 1, 20, 27)
    };

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 2);
    // Offsets should be preserved from original tokens
    EXPECT_EQ(result[0].getStartOffset(), 10);
    EXPECT_EQ(result[0].getEndOffset(), 15);
    EXPECT_EQ(result[1].getStartOffset(), 20);
    EXPECT_EQ(result[1].getEndOffset(), 27);
}

TEST(ASCIIFoldingFilterTest, PreservesTokenType) {
    ASCIIFoldingFilter filter;

    std::vector<Token> tokens{
        Token("café", 0, 0, 5),
        Token("123", 1, 6, 9)
    };

    tokens[0].setType("word");
    tokens[1].setType("number");

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0].getType(), "word");
    EXPECT_EQ(result[1].getType(), "number");
}

TEST(ASCIIFoldingFilterTest, EmptyTokenText) {
    ASCIIFoldingFilter filter;

    std::vector<Token> tokens{
        Token("", 0, 0, 0)
    };

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 1);
    EXPECT_TRUE(result[0].getText().empty());
}

TEST(ASCIIFoldingFilterTest, NumbersUnchanged) {
    ASCIIFoldingFilter filter;

    std::vector<Token> tokens{
        Token("123", 0, 0, 3),
        Token("456", 1, 4, 7)
    };

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0].getText(), "123");
    EXPECT_EQ(result[1].getText(), "456");
}

TEST(ASCIIFoldingFilterTest, PunctuationUnchanged) {
    ASCIIFoldingFilter filter;

    std::vector<Token> tokens{
        Token("café!", 0, 0, 6),
        Token("résumé?", 1, 7, 15)
    };

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0].getText(), "cafe!");
    EXPECT_EQ(result[1].getText(), "resume?");
}

TEST(ASCIIFoldingFilterTest, MixedAccents) {
    ASCIIFoldingFilter filter;

    std::vector<Token> tokens{
        Token("naïve", 0, 0, 6),
        Token("façade", 1, 7, 14),
        Token("crème", 2, 15, 21)
    };

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 3);
    EXPECT_EQ(result[0].getText(), "naive");
    EXPECT_EQ(result[1].getText(), "facade");
    EXPECT_EQ(result[2].getText(), "creme");
}

TEST(ASCIIFoldingFilterTest, LargeTokenList) {
    ASCIIFoldingFilter filter;

    std::vector<Token> tokens;
    for (int i = 0; i < 1000; ++i) {
        tokens.emplace_back("café", i, i * 10, i * 10 + 5);
    }

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 1000);
    for (const auto& token : result) {
        EXPECT_EQ(token.getText(), "cafe");
    }
}

TEST(ASCIIFoldingFilterTest, ChineseUnchanged) {
    ASCIIFoldingFilter filter;

    std::vector<Token> tokens{
        Token("北京", 0, 0, 6)
    };

    auto result = filter.filter(tokens);

    ASSERT_EQ(result.size(), 1);
    // Chinese characters should remain unchanged (they don't fold to ASCII)
    EXPECT_EQ(result[0].getText(), "北京");
}
