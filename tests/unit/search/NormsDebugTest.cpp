// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/index/SegmentReader.h"
#include "diagon/store/FSDirectory.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <iostream>

using namespace diagon;
using namespace diagon::index;
using namespace diagon::document;
using namespace diagon::store;

namespace fs = std::filesystem;

TEST(NormsDebugTest, CheckNormsValues) {
    fs::path testDir = fs::temp_directory_path() / "diagon_norms_debug_test";
    fs::remove_all(testDir);
    fs::create_directories(testDir);

    auto dir = FSDirectory::open(testDir.string());
    IndexWriterConfig config;
    config.setOpenMode(IndexWriterConfig::OpenMode::CREATE);

    {
        IndexWriter writer(*dir, config);

        // Document 0: 1 term
        Document doc0;
        doc0.add(std::make_unique<TextField>("content", "target"));
        writer.addDocument(doc0);

        // Document 1: 4 terms
        Document doc1;
        doc1.add(std::make_unique<TextField>("content", "target one two three"));
        writer.addDocument(doc1);

        writer.commit();
        writer.close();
    }

    {
        auto reader = DirectoryReader::open(*dir);
        auto leaves = reader->leaves();
        auto& leafContext = leaves[0];
        auto* leafReader = dynamic_cast<SegmentReader*>(leafContext.reader);

        auto* norms = leafReader->getNormValues("content");
        ASSERT_NE(nullptr, norms);

        // Print norms values
        std::cout << "Doc 0 norm: ";
        if (norms->advanceExact(0)) {
            int64_t norm = norms->longValue();
            std::cout << norm << std::endl;
        } else {
            std::cout << "NO NORM" << std::endl;
        }

        std::cout << "Doc 1 norm: ";
        if (norms->advanceExact(1)) {
            int64_t norm = norms->longValue();
            std::cout << norm << std::endl;
        } else {
            std::cout << "NO NORM" << std::endl;
        }
    }

    fs::remove_all(testDir);
}
