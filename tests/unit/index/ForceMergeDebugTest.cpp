// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/IndexWriter.h"
#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/store/FSDirectory.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <iostream>

using namespace diagon;
using namespace diagon::index;
using namespace diagon::document;
using namespace diagon::store;

namespace fs = std::filesystem;

/**
 * Debug test to understand forceMerge behavior
 */
TEST(ForceMergeDebugTest, SimpleDebug) {
    fs::path testDir = fs::temp_directory_path() / "diagon_debug_test";
    fs::remove_all(testDir);
    fs::create_directories(testDir);

    auto dir = FSDirectory::open(testDir.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);
    config.setMaxBufferedDocs(5);

    IndexWriter writer(*dir, config);

    std::cout << "Initial segments: " << writer.getSegmentInfos().size() << std::endl;

    // Add 10 docs
    for (int i = 0; i < 10; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("content", "doc" + std::to_string(i)));
        writer.addDocument(doc);
    }

    std::cout << "After adding 10 docs, segments: " << writer.getSegmentInfos().size() << std::endl;
    std::cout << "Docs in RAM: " << writer.getNumDocsInRAM() << std::endl;

    // Flush
    std::cout << "Calling flush()..." << std::endl;
    writer.flush();

    std::cout << "After flush, segments: " << writer.getSegmentInfos().size() << std::endl;
    std::cout << "Docs in RAM: " << writer.getNumDocsInRAM() << std::endl;

    if (writer.getSegmentInfos().size() == 0) {
        std::cout << "WARNING: No segments created by flush!" << std::endl;
        fs::remove_all(testDir);
        return;
    }

    // Add more docs
    for (int i = 10; i < 20; i++) {
        Document doc;
        doc.add(std::make_unique<TextField>("content", "doc" + std::to_string(i)));
        writer.addDocument(doc);
    }
    writer.flush();

    std::cout << "After second flush, segments: " << writer.getSegmentInfos().size() << std::endl;

    if (writer.getSegmentInfos().size() < 2) {
        std::cout << "WARNING: Less than 2 segments, forceMerge won't do anything" << std::endl;
        fs::remove_all(testDir);
        return;
    }

    // Try forceMerge with timeout detection
    std::cout << "Calling forceMerge(1)..." << std::endl;

    try {
        writer.forceMerge(1);
        std::cout << "forceMerge completed successfully!" << std::endl;
        std::cout << "Final segments: " << writer.getSegmentInfos().size() << std::endl;
    } catch (const std::exception& e) {
        std::cout << "forceMerge threw exception: " << e.what() << std::endl;
    }

    writer.close();
    fs::remove_all(testDir);
}
