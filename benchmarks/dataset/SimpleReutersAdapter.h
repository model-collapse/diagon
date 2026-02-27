/**
 * Simple Reuters-21578 Dataset Adapter
 *
 * Each .txt file is treated as a single document (1 file = 1 document).
 * This matches Lucene's benchmark behavior where each Reuters file
 * contains exactly one article.
 *
 * File format:
 *   Line 1: Date
 *   Line 2: Empty
 *   Line 3: Title
 *   Line 4: Empty
 *   Lines 5+: Body text
 */

#pragma once

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/FieldInfo.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace diagon::benchmarks {

class SimpleReutersAdapter {
public:
    explicit SimpleReutersAdapter(const std::string& datasetPath)
        : currentIndex_(0)
        , docCount_(0) {
        namespace fs = std::filesystem;
        for (const auto& entry : fs::directory_iterator(datasetPath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".txt") {
                files_.push_back(entry.path().string());
            }
        }
        std::sort(files_.begin(), files_.end());
    }

    bool nextDocument(document::Document& doc) {
        while (currentIndex_ < files_.size()) {
            std::ifstream ifs(files_[currentIndex_++]);
            if (!ifs.is_open())
                continue;

            std::string line;

            // Line 1: Date
            if (!std::getline(ifs, line))
                continue;
            std::string date = line;

            // Line 2: Empty (skip)
            std::getline(ifs, line);

            // Line 3: Title
            if (!std::getline(ifs, line))
                continue;
            std::string title = line;

            // Line 4: Empty (skip)
            std::getline(ifs, line);

            // Lines 5+: Body (pre-reserve and append directly, avoid ostringstream)
            std::string bodyStr;
            bodyStr.reserve(2048);
            bool first = true;
            while (std::getline(ifs, line)) {
                if (!first)
                    bodyStr += ' ';
                bodyStr += line;
                first = false;
            }
            if (bodyStr.empty())
                continue;

            // Use DOCS_AND_FREQS_AND_POSITIONS to enable phrase queries.
            // Matches Lucene's default TextField behavior.
            static const document::FieldType ftPos = []() {
                document::FieldType ft;
                ft.indexOptions = index::IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
                ft.stored = false;
                ft.tokenized = true;
                return ft;
            }();

            doc.add(std::make_unique<document::TextField>("title", title, ftPos));
            doc.add(std::make_unique<document::TextField>("body", bodyStr, ftPos));
            doc.add(std::make_unique<document::StringField>("date", date));

            docCount_++;
            return true;
        }
        return false;
    }

    int getDocumentCount() const { return docCount_; }

    void reset() {
        currentIndex_ = 0;
        docCount_ = 0;
    }

private:
    std::vector<std::string> files_;
    size_t currentIndex_;
    int docCount_;
};

}  // namespace diagon::benchmarks
