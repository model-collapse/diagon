// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/util/packed/DirectWriter.h"
#include "diagon/util/packed/DirectMonotonicWriter.h"
#include "diagon/store/ByteBuffersIndexOutput.h"
#include "diagon/store/ByteBuffersIndexInput.h"

#include <gtest/gtest.h>

using namespace diagon::util::packed;
using namespace diagon::store;

// ==================== DirectWriter Tests ====================

TEST(DirectWriterTest, BitsRequired) {
    EXPECT_EQ(0, DirectWriter::unsignedBitsRequired(0));
    EXPECT_EQ(1, DirectWriter::unsignedBitsRequired(1));
    EXPECT_EQ(2, DirectWriter::unsignedBitsRequired(2));
    EXPECT_EQ(2, DirectWriter::unsignedBitsRequired(3));
    EXPECT_EQ(3, DirectWriter::unsignedBitsRequired(7));
    EXPECT_EQ(4, DirectWriter::unsignedBitsRequired(15));
    EXPECT_EQ(8, DirectWriter::unsignedBitsRequired(255));
    EXPECT_EQ(16, DirectWriter::unsignedBitsRequired(65535));
    EXPECT_EQ(32, DirectWriter::unsignedBitsRequired(UINT32_MAX));
}

TEST(DirectWriterTest, WriteRead_1Bit) {
    ByteBuffersIndexOutput output("test");

    {
        DirectWriter writer(&output, 8, 1);
        writer.add(1);
        writer.add(0);
        writer.add(1);
        writer.add(1);
        writer.add(0);
        writer.add(0);
        writer.add(1);
        writer.add(0);
        writer.finish();
    }

    auto data = output.toArrayCopy();
    ByteBuffersIndexInput input("test", data);

    auto values = DirectReader::read(&input, 1, 8);
    EXPECT_EQ(8, values.size());
    EXPECT_EQ(1, values[0]);
    EXPECT_EQ(0, values[1]);
    EXPECT_EQ(1, values[2]);
    EXPECT_EQ(1, values[3]);
    EXPECT_EQ(0, values[4]);
    EXPECT_EQ(0, values[5]);
    EXPECT_EQ(1, values[6]);
    EXPECT_EQ(0, values[7]);
}

TEST(DirectWriterTest, WriteRead_3Bits) {
    ByteBuffersIndexOutput output("test");

    {
        DirectWriter writer(&output, 5, 3);
        writer.add(3);
        writer.add(7);
        writer.add(1);
        writer.add(5);
        writer.add(2);
        writer.finish();
    }

    auto data = output.toArrayCopy();
    ByteBuffersIndexInput input("test", data);

    auto values = DirectReader::read(&input, 3, 5);
    EXPECT_EQ(5, values.size());
    EXPECT_EQ(3, values[0]);
    EXPECT_EQ(7, values[1]);
    EXPECT_EQ(1, values[2]);
    EXPECT_EQ(5, values[3]);
    EXPECT_EQ(2, values[4]);
}

TEST(DirectWriterTest, WriteRead_ByteAligned) {
    ByteBuffersIndexOutput output("test");

    {
        DirectWriter writer(&output, 4, 16);
        writer.add(1000);
        writer.add(2000);
        writer.add(3000);
        writer.add(4000);
        writer.finish();
    }

    auto data = output.toArrayCopy();
    ByteBuffersIndexInput input("test", data);

    auto values = DirectReader::read(&input, 16, 4);
    EXPECT_EQ(4, values.size());
    EXPECT_EQ(1000, values[0]);
    EXPECT_EQ(2000, values[1]);
    EXPECT_EQ(3000, values[2]);
    EXPECT_EQ(4000, values[3]);
}

TEST(DirectWriterTest, GetInstance_RandomAccess) {
    ByteBuffersIndexOutput output("test");

    {
        DirectWriter writer(&output, 100, 7);
        for (int i = 0; i < 100; i++) {
            writer.add(i);
        }
        writer.finish();
    }

    auto data = output.toArrayCopy();
    ByteBuffersIndexInput input("test", data);

    // Random access
    EXPECT_EQ(0, DirectReader::getInstance(&input, 7, 0));
    EXPECT_EQ(50, DirectReader::getInstance(&input, 7, 50));
    EXPECT_EQ(99, DirectReader::getInstance(&input, 7, 99));
    EXPECT_EQ(25, DirectReader::getInstance(&input, 7, 25));
}

TEST(DirectWriterTest, AllZeros) {
    ByteBuffersIndexOutput output("test");

    {
        DirectWriter writer(&output, 10, 0);
        for (int i = 0; i < 10; i++) {
            writer.add(0);
        }
        writer.finish();
    }

    auto data = output.toArrayCopy();
    EXPECT_EQ(0, data.size());  // No data written for all zeros
}

// ==================== DirectMonotonicWriter Tests ====================

TEST(DirectMonotonicWriterTest, SimpleSequence) {
    ByteBuffersIndexOutput meta("meta");
    ByteBuffersIndexOutput data("data");

    DirectMonotonicWriter::Meta resultMeta;
    {
        DirectMonotonicWriter writer(&meta, &data, 16, 4);  // 16 values per block
        for (int64_t i = 0; i < 16; i++) {
            writer.add(i * 100);
        }
        resultMeta = writer.finish();
    }

    auto metaData = meta.toArrayCopy();
    auto packedData = data.toArrayCopy();

    resultMeta.metaFP = 0;
    resultMeta.dataFP = 0;

    ByteBuffersIndexInput metaIn("meta", metaData);
    ByteBuffersIndexInput dataIn("data", packedData);

    // Test random access
    EXPECT_EQ(0, DirectMonotonicReader::get(resultMeta, &metaIn, &dataIn, 0));
    EXPECT_EQ(500, DirectMonotonicReader::get(resultMeta, &metaIn, &dataIn, 5));
    EXPECT_EQ(1000, DirectMonotonicReader::get(resultMeta, &metaIn, &dataIn, 10));
    EXPECT_EQ(1500, DirectMonotonicReader::get(resultMeta, &metaIn, &dataIn, 15));
}

TEST(DirectMonotonicWriterTest, PerfectMonotonic) {
    ByteBuffersIndexOutput meta("meta");
    ByteBuffersIndexOutput data("data");

    DirectMonotonicWriter::Meta resultMeta;
    {
        DirectMonotonicWriter writer(&meta, &data, 32, 4);  // 16 values per block
        for (int64_t i = 0; i < 32; i++) {
            writer.add(i * 10);  // Perfect slope of 10
        }
        resultMeta = writer.finish();
    }

    auto metaData = meta.toArrayCopy();
    auto packedData = data.toArrayCopy();

    resultMeta.metaFP = 0;
    resultMeta.dataFP = 0;

    ByteBuffersIndexInput metaIn("meta", metaData);
    ByteBuffersIndexInput dataIn("data", packedData);

    // Read all values
    auto values = DirectMonotonicReader::readAll(resultMeta, &metaIn, &dataIn);
    EXPECT_EQ(32, values.size());
    for (int i = 0; i < 32; i++) {
        EXPECT_EQ(i * 10, values[i]) << "Mismatch at index " << i;
    }
}

TEST(DirectMonotonicWriterTest, NonUniformGrowth) {
    ByteBuffersIndexOutput meta("meta");
    ByteBuffersIndexOutput data("data");

    DirectMonotonicWriter::Meta resultMeta;
    {
        DirectMonotonicWriter writer(&meta, &data, 20, 4);  // 16 values per block

        // Non-uniform growth: 0, 1, 3, 6, 10, 15, 21, 28, ...
        int64_t value = 0;
        for (int64_t i = 0; i < 20; i++) {
            writer.add(value);
            value += i + 1;
        }
        resultMeta = writer.finish();
    }

    auto metaData = meta.toArrayCopy();
    auto packedData = data.toArrayCopy();

    resultMeta.metaFP = 0;
    resultMeta.dataFP = 0;

    ByteBuffersIndexInput metaIn("meta", metaData);
    ByteBuffersIndexInput dataIn("data", packedData);

    // Verify values
    int64_t expected = 0;
    for (int i = 0; i < 20; i++) {
        int64_t actual = DirectMonotonicReader::get(resultMeta, &metaIn, &dataIn, i);
        EXPECT_EQ(expected, actual) << "Mismatch at index " << i;
        expected += i + 1;
    }
}

TEST(DirectMonotonicWriterTest, LargeSequence) {
    ByteBuffersIndexOutput meta("meta");
    ByteBuffersIndexOutput data("data");

    DirectMonotonicWriter::Meta resultMeta;
    {
        DirectMonotonicWriter writer(&meta, &data, 1000, 4);  // 16 values per block
        for (int64_t i = 0; i < 1000; i++) {
            writer.add(i * i);  // Quadratic growth
        }
        resultMeta = writer.finish();
    }

    auto metaData = meta.toArrayCopy();
    auto packedData = data.toArrayCopy();

    resultMeta.metaFP = 0;
    resultMeta.dataFP = 0;

    ByteBuffersIndexInput metaIn("meta", metaData);
    ByteBuffersIndexInput dataIn("data", packedData);

    // Spot check values
    EXPECT_EQ(0, DirectMonotonicReader::get(resultMeta, &metaIn, &dataIn, 0));
    EXPECT_EQ(10000, DirectMonotonicReader::get(resultMeta, &metaIn, &dataIn, 100));
    EXPECT_EQ(250000, DirectMonotonicReader::get(resultMeta, &metaIn, &dataIn, 500));
    EXPECT_EQ(998001, DirectMonotonicReader::get(resultMeta, &metaIn, &dataIn, 999));
}
