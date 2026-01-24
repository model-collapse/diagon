// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/compression/CompressionCodecs.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace diagon::compression;

// ==================== NoneCodec Tests ====================

TEST(NoneCodecTest, Name) {
    auto codec = NoneCodec::create();
    EXPECT_EQ("None", codec->getName());
    EXPECT_EQ(static_cast<uint8_t>(CodecId::None), codec->getCodecId());
}

TEST(NoneCodecTest, CompressDecompress) {
    auto codec = NoneCodec::create();

    std::string input = "Hello, World!";
    std::vector<char> compressed(codec->getMaxCompressedSize(input.size()));
    std::vector<char> decompressed(input.size());

    // Compress
    size_t comp_size = codec->compress(
        input.data(), input.size(),
        compressed.data(), compressed.size());

    EXPECT_EQ(input.size(), comp_size);

    // Decompress
    size_t decomp_size = codec->decompress(
        compressed.data(), comp_size,
        decompressed.data(), decompressed.size());

    EXPECT_EQ(input.size(), decomp_size);
    EXPECT_EQ(input, std::string(decompressed.data(), decomp_size));
}

TEST(NoneCodecTest, MaxCompressedSize) {
    auto codec = NoneCodec::create();
    EXPECT_EQ(100, codec->getMaxCompressedSize(100));
    EXPECT_EQ(1000, codec->getMaxCompressedSize(1000));
}

// ==================== LZ4Codec Tests ====================

TEST(LZ4CodecTest, Name) {
    auto codec = LZ4Codec::create();
    EXPECT_EQ("LZ4", codec->getName());
    EXPECT_EQ(static_cast<uint8_t>(CodecId::LZ4), codec->getCodecId());
}

TEST(LZ4CodecTest, CompressDecompressStub) {
    auto codec = LZ4Codec::create();

    std::string input = "Test data for LZ4";
    std::vector<char> compressed(codec->getMaxCompressedSize(input.size()));
    std::vector<char> decompressed(input.size());

    // Compress (stub just copies)
    size_t comp_size = codec->compress(
        input.data(), input.size(),
        compressed.data(), compressed.size());

    // Decompress (stub just copies)
    size_t decomp_size = codec->decompress(
        compressed.data(), comp_size,
        decompressed.data(), decompressed.size());

    EXPECT_EQ(input, std::string(decompressed.data(), decomp_size));
}

TEST(LZ4CodecTest, MaxCompressedSize) {
    auto codec = LZ4Codec::create();
    // Should have some overhead for worst case
    EXPECT_GT(codec->getMaxCompressedSize(100), 100);
}

// ==================== ZSTDCodec Tests ====================

TEST(ZSTDCodecTest, Name) {
    auto codec = ZSTDCodec::create();
    EXPECT_EQ("ZSTD", codec->getName());
    EXPECT_EQ(static_cast<uint8_t>(CodecId::ZSTD), codec->getCodecId());
}

TEST(ZSTDCodecTest, Level) {
    auto codec1 = ZSTDCodec::create(1);
    EXPECT_EQ(1, codec1->getLevel());

    auto codec2 = ZSTDCodec::create(9);
    EXPECT_EQ(9, codec2->getLevel());

    auto codec3 = ZSTDCodec::create();  // Default level
    EXPECT_EQ(3, codec3->getLevel());
}

TEST(ZSTDCodecTest, CompressDecompressStub) {
    auto codec = ZSTDCodec::create();

    std::string input = "Test data for ZSTD compression";
    std::vector<char> compressed(codec->getMaxCompressedSize(input.size()));
    std::vector<char> decompressed(input.size());

    // Compress (stub just copies)
    size_t comp_size = codec->compress(
        input.data(), input.size(),
        compressed.data(), compressed.size());

    // Decompress (stub just copies)
    size_t decomp_size = codec->decompress(
        compressed.data(), comp_size,
        decompressed.data(), decompressed.size());

    EXPECT_EQ(input, std::string(decompressed.data(), decomp_size));
}

// ==================== CompressionCodecFactory Tests ====================

TEST(CompressionCodecFactoryTest, GetCodecByName) {
    auto none = CompressionCodecFactory::getCodec("None");
    EXPECT_EQ("None", none->getName());

    auto lz4 = CompressionCodecFactory::getCodec("LZ4");
    EXPECT_EQ("LZ4", lz4->getName());

    auto zstd = CompressionCodecFactory::getCodec("ZSTD");
    EXPECT_EQ("ZSTD", zstd->getName());
}

TEST(CompressionCodecFactoryTest, GetCodecByNameInvalid) {
    EXPECT_THROW(
        CompressionCodecFactory::getCodec("InvalidCodec"),
        std::runtime_error);
}

TEST(CompressionCodecFactoryTest, GetCodecById) {
    auto none = CompressionCodecFactory::getCodecById(
        static_cast<uint8_t>(CodecId::None));
    EXPECT_EQ("None", none->getName());

    auto lz4 = CompressionCodecFactory::getCodecById(
        static_cast<uint8_t>(CodecId::LZ4));
    EXPECT_EQ("LZ4", lz4->getName());

    auto zstd = CompressionCodecFactory::getCodecById(
        static_cast<uint8_t>(CodecId::ZSTD));
    EXPECT_EQ("ZSTD", zstd->getName());
}

TEST(CompressionCodecFactoryTest, GetCodecByIdInvalid) {
    EXPECT_THROW(
        CompressionCodecFactory::getCodecById(0xFF),
        std::runtime_error);
}

TEST(CompressionCodecFactoryTest, GetDefault) {
    auto codec = CompressionCodecFactory::getDefault();
    EXPECT_EQ("LZ4", codec->getName());
}

// ==================== Integration Tests ====================

TEST(CompressionIntegrationTest, RoundTripAllCodecs) {
    std::string input = "The quick brown fox jumps over the lazy dog.";

    std::vector<std::string> codec_names = {"None", "LZ4", "ZSTD"};

    for (const auto& name : codec_names) {
        auto codec = CompressionCodecFactory::getCodec(name);

        std::vector<char> compressed(codec->getMaxCompressedSize(input.size()));
        std::vector<char> decompressed(input.size());

        size_t comp_size = codec->compress(
            input.data(), input.size(),
            compressed.data(), compressed.size());

        size_t decomp_size = codec->decompress(
            compressed.data(), comp_size,
            decompressed.data(), decompressed.size());

        EXPECT_EQ(input.size(), decomp_size) << "Codec: " << name;
        EXPECT_EQ(input, std::string(decompressed.data(), decomp_size))
            << "Codec: " << name;
    }
}

TEST(CompressionIntegrationTest, LargeData) {
    // Generate large repetitive data
    std::vector<char> input(10000, 'A');

    auto codec = NoneCodec::create();

    std::vector<char> compressed(codec->getMaxCompressedSize(input.size()));
    std::vector<char> decompressed(input.size());

    size_t comp_size = codec->compress(
        input.data(), input.size(),
        compressed.data(), compressed.size());

    size_t decomp_size = codec->decompress(
        compressed.data(), comp_size,
        decompressed.data(), decompressed.size());

    EXPECT_EQ(input.size(), decomp_size);
    EXPECT_EQ(input, std::vector<char>(decompressed.begin(),
                                       decompressed.begin() + decomp_size));
}

TEST(CompressionIntegrationTest, EmptyData) {
    std::string input;

    auto codec = NoneCodec::create();

    std::vector<char> compressed(codec->getMaxCompressedSize(1));
    std::vector<char> decompressed(1);

    size_t comp_size = codec->compress(
        input.data(), input.size(),
        compressed.data(), compressed.size());

    EXPECT_EQ(0, comp_size);

    size_t decomp_size = codec->decompress(
        compressed.data(), comp_size,
        decompressed.data(), decompressed.size());

    EXPECT_EQ(0, decomp_size);
}
