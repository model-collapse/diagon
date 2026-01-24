// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/mergetree/IMergeTreeDataPart.h"

#include <gtest/gtest.h>

using namespace diagon::mergetree;

// ==================== DataPartType Tests ====================

TEST(DataPartTypeTest, ToString) {
    EXPECT_EQ("Wide", dataPartTypeToString(DataPartType::Wide));
    EXPECT_EQ("Compact", dataPartTypeToString(DataPartType::Compact));
    EXPECT_EQ("InMemory", dataPartTypeToString(DataPartType::InMemory));
}

TEST(DataPartTypeTest, FromString) {
    EXPECT_EQ(DataPartType::Wide, stringToDataPartType("Wide"));
    EXPECT_EQ(DataPartType::Compact, stringToDataPartType("Compact"));
    EXPECT_EQ(DataPartType::InMemory, stringToDataPartType("InMemory"));
}

TEST(DataPartTypeTest, FromStringInvalid) {
    EXPECT_THROW(stringToDataPartType("Invalid"), std::runtime_error);
}

TEST(DataPartTypeTest, RoundTrip) {
    std::vector<DataPartType> types = {
        DataPartType::Wide,
        DataPartType::Compact,
        DataPartType::InMemory
    };

    for (auto type : types) {
        std::string str = dataPartTypeToString(type);
        DataPartType parsed = stringToDataPartType(str);
        EXPECT_EQ(type, parsed);
    }
}

// ==================== IMergeTreeDataPart Tests ====================

TEST(IMergeTreeDataPartTest, SelectPartTypeSmallBytes) {
    // Less than 10MB → Compact
    size_t bytes = 5 * 1024 * 1024;  // 5MB
    size_t rows = 200'000;

    DataPartType type = IMergeTreeDataPart::selectPartType(bytes, rows);
    EXPECT_EQ(DataPartType::Compact, type);
}

TEST(IMergeTreeDataPartTest, SelectPartTypeSmallRows) {
    // Less than 100k rows → Compact
    size_t bytes = 50 * 1024 * 1024;  // 50MB
    size_t rows = 50'000;

    DataPartType type = IMergeTreeDataPart::selectPartType(bytes, rows);
    EXPECT_EQ(DataPartType::Compact, type);
}

TEST(IMergeTreeDataPartTest, SelectPartTypeLarge) {
    // More than 10MB and 100k rows → Wide
    size_t bytes = 50 * 1024 * 1024;  // 50MB
    size_t rows = 200'000;

    DataPartType type = IMergeTreeDataPart::selectPartType(bytes, rows);
    EXPECT_EQ(DataPartType::Wide, type);
}

TEST(IMergeTreeDataPartTest, SelectPartTypeEdgeCaseBytes) {
    // Exactly 10MB with many rows → Wide
    size_t bytes = 10 * 1024 * 1024;
    size_t rows = 200'000;

    DataPartType type = IMergeTreeDataPart::selectPartType(bytes, rows);
    EXPECT_EQ(DataPartType::Wide, type);

    // Just under 10MB with many rows → Compact
    bytes = 10 * 1024 * 1024 - 1;
    type = IMergeTreeDataPart::selectPartType(bytes, rows);
    EXPECT_EQ(DataPartType::Compact, type);
}

TEST(IMergeTreeDataPartTest, SelectPartTypeEdgeCaseRows) {
    // Exactly 100k rows with large bytes → Wide
    size_t bytes = 50 * 1024 * 1024;
    size_t rows = 100'000;

    DataPartType type = IMergeTreeDataPart::selectPartType(bytes, rows);
    EXPECT_EQ(DataPartType::Wide, type);

    // Just under 100k rows with large bytes → Compact
    rows = 99'999;
    type = IMergeTreeDataPart::selectPartType(bytes, rows);
    EXPECT_EQ(DataPartType::Compact, type);
}

TEST(IMergeTreeDataPartTest, SelectPartTypeEmpty) {
    // Empty part → Compact
    size_t bytes = 0;
    size_t rows = 0;

    DataPartType type = IMergeTreeDataPart::selectPartType(bytes, rows);
    EXPECT_EQ(DataPartType::Compact, type);
}

TEST(IMergeTreeDataPartTest, SelectPartTypeVeryLarge) {
    // Very large part → Wide
    size_t bytes = 1024 * 1024 * 1024;  // 1GB
    size_t rows = 10'000'000;           // 10M rows

    DataPartType type = IMergeTreeDataPart::selectPartType(bytes, rows);
    EXPECT_EQ(DataPartType::Wide, type);
}
