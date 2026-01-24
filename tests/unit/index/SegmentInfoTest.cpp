// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/SegmentInfo.h"

#include <gtest/gtest.h>

using namespace diagon::index;

// ==================== SegmentInfo Tests ====================

TEST(SegmentInfoTest, BasicConstruction) {
    SegmentInfo info("_0", 100, "Lucene104");

    EXPECT_EQ(info.name(), "_0");
    EXPECT_EQ(info.maxDoc(), 100);
    EXPECT_EQ(info.codecName(), "Lucene104");
    EXPECT_EQ(info.files().size(), 0);
    EXPECT_EQ(info.diagnostics().size(), 0);
    EXPECT_EQ(info.sizeInBytes(), 0);
}

TEST(SegmentInfoTest, DefaultCodec) {
    SegmentInfo info("_0", 50);

    EXPECT_EQ(info.codecName(), "Lucene104");
}

TEST(SegmentInfoTest, AddFile) {
    SegmentInfo info("_0", 100);

    info.addFile("_0.cfs");
    info.addFile("_0.cfe");

    EXPECT_EQ(info.files().size(), 2);
    EXPECT_EQ(info.files()[0], "_0.cfs");
    EXPECT_EQ(info.files()[1], "_0.cfe");
}

TEST(SegmentInfoTest, AddDuplicateFile) {
    SegmentInfo info("_0", 100);

    info.addFile("_0.cfs");
    info.addFile("_0.cfs");  // Duplicate

    // Should not add duplicate
    EXPECT_EQ(info.files().size(), 1);
}

TEST(SegmentInfoTest, SetFiles) {
    SegmentInfo info("_0", 100);

    std::vector<std::string> files = {"_0.cfs", "_0.cfe", "_0.si"};
    info.setFiles(files);

    EXPECT_EQ(info.files().size(), 3);
    EXPECT_EQ(info.files()[0], "_0.cfs");
    EXPECT_EQ(info.files()[1], "_0.cfe");
    EXPECT_EQ(info.files()[2], "_0.si");
}

TEST(SegmentInfoTest, SetDiagnostic) {
    SegmentInfo info("_0", 100);

    info.setDiagnostic("source", "flush");
    info.setDiagnostic("timestamp", "2024-01-24");

    EXPECT_EQ(info.diagnostics().size(), 2);
    EXPECT_EQ(info.getDiagnostic("source"), "flush");
    EXPECT_EQ(info.getDiagnostic("timestamp"), "2024-01-24");
}

TEST(SegmentInfoTest, GetNonExistentDiagnostic) {
    SegmentInfo info("_0", 100);

    EXPECT_EQ(info.getDiagnostic("nonexistent"), "");
}

TEST(SegmentInfoTest, OverwriteDiagnostic) {
    SegmentInfo info("_0", 100);

    info.setDiagnostic("key", "value1");
    info.setDiagnostic("key", "value2");

    EXPECT_EQ(info.diagnostics().size(), 1);
    EXPECT_EQ(info.getDiagnostic("key"), "value2");
}

TEST(SegmentInfoTest, SetSizeInBytes) {
    SegmentInfo info("_0", 100);

    info.setSizeInBytes(1024 * 1024);  // 1MB

    EXPECT_EQ(info.sizeInBytes(), 1024 * 1024);
}

TEST(SegmentInfoTest, MultipleFiles) {
    SegmentInfo info("_1", 500);

    // Add typical segment files
    info.addFile("_1.fdx");  // Field data index
    info.addFile("_1.fdt");  // Field data
    info.addFile("_1.tim");  // Terms index
    info.addFile("_1.tip");  // Terms
    info.addFile("_1.doc");  // Doc IDs
    info.addFile("_1.pos");  // Positions

    EXPECT_EQ(info.files().size(), 6);
}

TEST(SegmentInfoTest, LargeDocCount) {
    SegmentInfo info("_a", 1000000);  // 1 million docs

    EXPECT_EQ(info.maxDoc(), 1000000);
}

// ==================== SegmentInfos Tests ====================

TEST(SegmentInfosTest, InitialState) {
    SegmentInfos infos;

    EXPECT_EQ(infos.size(), 0);
    EXPECT_EQ(infos.totalMaxDoc(), 0);
    EXPECT_EQ(infos.getGeneration(), 0);
    EXPECT_EQ(infos.getVersion(), 0);
}

TEST(SegmentInfosTest, AddSegment) {
    SegmentInfos infos;

    auto seg0 = std::make_shared<SegmentInfo>("_0", 100);
    infos.add(seg0);

    EXPECT_EQ(infos.size(), 1);
    EXPECT_EQ(infos.totalMaxDoc(), 100);
    EXPECT_EQ(infos.getVersion(), 1);  // Version incremented
}

TEST(SegmentInfosTest, AddMultipleSegments) {
    SegmentInfos infos;

    auto seg0 = std::make_shared<SegmentInfo>("_0", 100);
    auto seg1 = std::make_shared<SegmentInfo>("_1", 200);
    auto seg2 = std::make_shared<SegmentInfo>("_2", 150);

    infos.add(seg0);
    infos.add(seg1);
    infos.add(seg2);

    EXPECT_EQ(infos.size(), 3);
    EXPECT_EQ(infos.totalMaxDoc(), 450);  // 100 + 200 + 150
}

TEST(SegmentInfosTest, GetSegmentByIndex) {
    SegmentInfos infos;

    auto seg0 = std::make_shared<SegmentInfo>("_0", 100);
    auto seg1 = std::make_shared<SegmentInfo>("_1", 200);

    infos.add(seg0);
    infos.add(seg1);

    EXPECT_EQ(infos.info(0)->name(), "_0");
    EXPECT_EQ(infos.info(0)->maxDoc(), 100);
    EXPECT_EQ(infos.info(1)->name(), "_1");
    EXPECT_EQ(infos.info(1)->maxDoc(), 200);
}

TEST(SegmentInfosTest, GetSegmentOutOfRange) {
    SegmentInfos infos;

    auto seg0 = std::make_shared<SegmentInfo>("_0", 100);
    infos.add(seg0);

    EXPECT_THROW(infos.info(-1), std::out_of_range);
    EXPECT_THROW(infos.info(1), std::out_of_range);
}

TEST(SegmentInfosTest, GenerationIncrement) {
    SegmentInfos infos;

    EXPECT_EQ(infos.getGeneration(), 0);

    infos.incrementGeneration();
    EXPECT_EQ(infos.getGeneration(), 1);

    infos.incrementGeneration();
    EXPECT_EQ(infos.getGeneration(), 2);
}

TEST(SegmentInfosTest, VersionIncrement) {
    SegmentInfos infos;

    EXPECT_EQ(infos.getVersion(), 0);

    auto seg0 = std::make_shared<SegmentInfo>("_0", 100);
    infos.add(seg0);  // Version increments on add

    EXPECT_EQ(infos.getVersion(), 1);

    auto seg1 = std::make_shared<SegmentInfo>("_1", 200);
    infos.add(seg1);

    EXPECT_EQ(infos.getVersion(), 2);
}

TEST(SegmentInfosTest, Clear) {
    SegmentInfos infos;

    auto seg0 = std::make_shared<SegmentInfo>("_0", 100);
    auto seg1 = std::make_shared<SegmentInfo>("_1", 200);
    infos.add(seg0);
    infos.add(seg1);

    EXPECT_EQ(infos.size(), 2);

    int64_t versionBefore = infos.getVersion();
    infos.clear();

    EXPECT_EQ(infos.size(), 0);
    EXPECT_EQ(infos.totalMaxDoc(), 0);
    EXPECT_GT(infos.getVersion(), versionBefore);  // Version incremented
}

TEST(SegmentInfosTest, GetSegmentsFileName) {
    EXPECT_EQ(SegmentInfos::getSegmentsFileName(0), "segments_0");
    EXPECT_EQ(SegmentInfos::getSegmentsFileName(1), "segments_1");
    EXPECT_EQ(SegmentInfos::getSegmentsFileName(10), "segments_a");
    EXPECT_EQ(SegmentInfos::getSegmentsFileName(15), "segments_f");
    EXPECT_EQ(SegmentInfos::getSegmentsFileName(255), "segments_ff");
}

TEST(SegmentInfosTest, RemoveSegment) {
    SegmentInfos infos;

    auto seg0 = std::make_shared<SegmentInfo>("_0", 100);
    auto seg1 = std::make_shared<SegmentInfo>("_1", 200);
    auto seg2 = std::make_shared<SegmentInfo>("_2", 150);

    infos.add(seg0);
    infos.add(seg1);
    infos.add(seg2);

    EXPECT_EQ(infos.size(), 3);

    // Remove middle segment
    infos.remove(1);

    EXPECT_EQ(infos.size(), 2);
    EXPECT_EQ(infos.info(0)->name(), "_0");
    EXPECT_EQ(infos.info(1)->name(), "_2");
    EXPECT_EQ(infos.totalMaxDoc(), 250);  // 100 + 150
}

TEST(SegmentInfosTest, RemoveOutOfRange) {
    SegmentInfos infos;

    auto seg0 = std::make_shared<SegmentInfo>("_0", 100);
    infos.add(seg0);

    EXPECT_THROW(infos.remove(-1), std::out_of_range);
    EXPECT_THROW(infos.remove(1), std::out_of_range);
}

TEST(SegmentInfosTest, SegmentsVector) {
    SegmentInfos infos;

    auto seg0 = std::make_shared<SegmentInfo>("_0", 100);
    auto seg1 = std::make_shared<SegmentInfo>("_1", 200);

    infos.add(seg0);
    infos.add(seg1);

    const auto& segments = infos.segments();
    EXPECT_EQ(segments.size(), 2);
    EXPECT_EQ(segments[0]->name(), "_0");
    EXPECT_EQ(segments[1]->name(), "_1");
}

TEST(SegmentInfosTest, EmptyTotalMaxDoc) {
    SegmentInfos infos;

    EXPECT_EQ(infos.totalMaxDoc(), 0);
}

TEST(SegmentInfosTest, LargeSegmentCollection) {
    SegmentInfos infos;

    // Add 100 segments
    for (int i = 0; i < 100; i++) {
        auto seg = std::make_shared<SegmentInfo>("_" + std::to_string(i), 1000);
        infos.add(seg);
    }

    EXPECT_EQ(infos.size(), 100);
    EXPECT_EQ(infos.totalMaxDoc(), 100000);  // 100 * 1000
}

// ==================== Integration Tests ====================

TEST(SegmentInfoIntegrationTest, SegmentWithMetadata) {
    // Create segment with complete metadata
    auto info = std::make_shared<SegmentInfo>("_0", 500, "Lucene104");

    // Add files
    info->addFile("_0.cfs");
    info->addFile("_0.cfe");
    info->addFile("_0.si");

    // Add diagnostics
    info->setDiagnostic("source", "flush");
    info->setDiagnostic("os", "linux");
    info->setDiagnostic("timestamp", "2024-01-24");

    // Set size
    info->setSizeInBytes(2 * 1024 * 1024);  // 2MB

    // Verify
    EXPECT_EQ(info->name(), "_0");
    EXPECT_EQ(info->maxDoc(), 500);
    EXPECT_EQ(info->codecName(), "Lucene104");
    EXPECT_EQ(info->files().size(), 3);
    EXPECT_EQ(info->diagnostics().size(), 3);
    EXPECT_EQ(info->sizeInBytes(), 2 * 1024 * 1024);
}

TEST(SegmentInfoIntegrationTest, IndexWithMultipleSegments) {
    SegmentInfos infos;

    // Segment 0: 1000 docs
    auto seg0 = std::make_shared<SegmentInfo>("_0", 1000);
    seg0->setDiagnostic("source", "flush");
    seg0->setSizeInBytes(5 * 1024 * 1024);  // 5MB
    infos.add(seg0);

    // Segment 1: 500 docs
    auto seg1 = std::make_shared<SegmentInfo>("_1", 500);
    seg1->setDiagnostic("source", "flush");
    seg1->setSizeInBytes(2 * 1024 * 1024);  // 2MB
    infos.add(seg1);

    // Segment 2: 2000 docs (merged)
    auto seg2 = std::make_shared<SegmentInfo>("_2", 2000);
    seg2->setDiagnostic("source", "merge");
    seg2->setSizeInBytes(10 * 1024 * 1024);  // 10MB
    infos.add(seg2);

    // Verify index state
    EXPECT_EQ(infos.size(), 3);
    EXPECT_EQ(infos.totalMaxDoc(), 3500);  // 1000 + 500 + 2000

    // Verify individual segments
    EXPECT_EQ(infos.info(0)->maxDoc(), 1000);
    EXPECT_EQ(infos.info(1)->maxDoc(), 500);
    EXPECT_EQ(infos.info(2)->maxDoc(), 2000);
    EXPECT_EQ(infos.info(2)->getDiagnostic("source"), "merge");
}

TEST(SegmentInfoIntegrationTest, GenerationTracking) {
    SegmentInfos infos;

    // Initial commit (generation 0)
    auto seg0 = std::make_shared<SegmentInfo>("_0", 100);
    infos.add(seg0);
    infos.incrementGeneration();
    EXPECT_EQ(infos.getGeneration(), 1);
    EXPECT_EQ(SegmentInfos::getSegmentsFileName(infos.getGeneration()), "segments_1");

    // Second commit (generation 1)
    auto seg1 = std::make_shared<SegmentInfo>("_1", 200);
    infos.add(seg1);
    infos.incrementGeneration();
    EXPECT_EQ(infos.getGeneration(), 2);
    EXPECT_EQ(SegmentInfos::getSegmentsFileName(infos.getGeneration()), "segments_2");

    // Third commit (generation 2)
    auto seg2 = std::make_shared<SegmentInfo>("_2", 150);
    infos.add(seg2);
    infos.incrementGeneration();
    EXPECT_EQ(infos.getGeneration(), 3);
    EXPECT_EQ(SegmentInfos::getSegmentsFileName(infos.getGeneration()), "segments_3");
}
