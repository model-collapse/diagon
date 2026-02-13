/**
 * Reuters-21578 Dataset Adapter
 *
 * Reads Reuters-21578 documents from extracted text files.
 * Format:
 *   Line 1: Date (e.g., "26-FEB-1987 15:01:01.79")
 *   Line 2: Empty
 *   Line 3: Title
 *   Line 4: Empty
 *   Lines 5+: Body text
 *
 * Based on Lucene's ReutersContentSource.
 */

#pragma once

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>

namespace diagon::benchmarks {

class ReutersDatasetAdapter {
public:
    /**
     * Constructor
     * @param datasetPath Path to directory containing Reuters .txt files
     */
    explicit ReutersDatasetAdapter(const std::string& datasetPath)
        : datasetPath_(datasetPath)
        , currentFileIndex_(0)
        , docCount_(0) {
        // Find all .txt files in the dataset directory
        loadFileList();

        // Open first file
        if (!files_.empty()) {
            openNextFile();
        }
    }

    /**
     * Read next document
     * @param doc Output document (will be populated)
     * @return true if document was read, false if no more documents
     */
    bool nextDocument(document::Document& doc) {
        // Try to read from current file
        while (currentFile_.is_open() || currentFileIndex_ < files_.size()) {
            if (!currentFile_.is_open()) {
                if (!openNextFile()) {
                    return false; // No more files
                }
            }

            // Try to parse a document from current file
            if (parseDocument(doc)) {
                docCount_++;
                return true;
            }

            // Current file exhausted, try next file
            currentFile_.close();
        }

        return false;
    }

    /**
     * Get total document count processed so far
     */
    int getDocumentCount() const {
        return docCount_;
    }

    /**
     * Reset to beginning
     */
    void reset() {
        currentFileIndex_ = 0;
        docCount_ = 0;
        currentFile_.close();
        if (!files_.empty()) {
            openNextFile();
        }
    }

private:
    void loadFileList() {
        namespace fs = std::filesystem;

        try {
            for (const auto& entry : fs::directory_iterator(datasetPath_)) {
                if (entry.is_regular_file() && entry.path().extension() == ".txt") {
                    files_.push_back(entry.path().string());
                }
            }

            // Sort files for consistent ordering
            std::sort(files_.begin(), files_.end());
        } catch (const std::exception& e) {
            // Directory doesn't exist or not accessible
            files_.clear();
        }
    }

    bool openNextFile() {
        if (currentFileIndex_ >= files_.size()) {
            return false;
        }

        currentFile_.open(files_[currentFileIndex_++]);
        return currentFile_.is_open();
    }

    bool parseDocument(document::Document& doc) {
        if (!currentFile_.is_open()) {
            return false;
        }

        std::string line;

        // Skip empty lines at start of file
        while (std::getline(currentFile_, line) && line.empty()) {}

        if (line.empty() && currentFile_.eof()) {
            return false; // End of file
        }

        // Line 1: Date
        std::string date = line;

        // Line 2: Should be empty (skip)
        std::getline(currentFile_, line);

        // Line 3: Title
        if (!std::getline(currentFile_, line)) {
            return false; // Malformed document
        }
        std::string title = line;

        // Line 4: Should be empty (skip)
        std::getline(currentFile_, line);

        // Lines 5+: Body (read until empty line or EOF)
        std::ostringstream bodyStream;
        bool firstLine = true;
        while (std::getline(currentFile_, line)) {
            // Empty line indicates end of document
            if (line.empty()) {
                break;
            }

            if (!firstLine) {
                bodyStream << " ";
            }
            bodyStream << line;
            firstLine = false;
        }

        std::string body = bodyStream.str();

        // Skip documents with empty body (match Lucene behavior)
        // Lucene filters documents without body content:
        //   - 737 files with only date (2 lines)
        //   - 1,798 files with title but no body (4 lines)
        // Total filtered: 2,535 files
        // This reduces index from 21,578 files to 19,043 docs
        if (body.empty()) {
            return false; // Skip documents without body
        }

        // Populate document
        doc.add(std::make_unique<document::TextField>("title", title));
        doc.add(std::make_unique<document::TextField>("body", body));
        doc.add(std::make_unique<document::StringField>("date", date));

        return true;
    }

    std::string datasetPath_;
    std::vector<std::string> files_;
    size_t currentFileIndex_;
    std::ifstream currentFile_;
    int docCount_;
};

} // namespace diagon::benchmarks
