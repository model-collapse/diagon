#pragma once

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"

#include <fstream>
#include <string>

namespace diagon::benchmarks {

/**
 * Adapter to read Lucene's LineDocSource format.
 *
 * Format: title<TAB>date<TAB>body<NEWLINE>
 *
 * This adapter enables Diagon to read datasets prepared by Lucene's
 * benchmark suite, ensuring apple-to-apple comparisons.
 *
 * Example usage:
 *   LuceneDatasetAdapter adapter("reuters.txt");
 *   Document doc;
 *   while (adapter.nextDocument(doc)) {
 *       writer.addDocument(doc);
 *   }
 */
class LuceneDatasetAdapter {
public:
    /**
     * Open a Lucene line doc format file for reading.
     * @param path Path to the line doc file
     */
    explicit LuceneDatasetAdapter(const std::string& path)
        : file_(path)
        , documentCount_(0) {
        if (!file_.is_open()) {
            throw std::runtime_error("Failed to open dataset file: " + path);
        }
    }

    /**
     * Read the next document from the dataset.
     * @param doc Document to populate with fields
     * @return true if document was read, false if EOF
     */
    bool nextDocument(document::Document& doc) {
        std::string line;
        if (!std::getline(file_, line)) {
            return false;
        }

        // Skip empty lines
        if (line.empty()) {
            return nextDocument(doc);
        }

        // Parse tab-separated line: title<TAB>date<TAB>body
        size_t tab1 = line.find('\t');
        if (tab1 == std::string::npos) {
            // Malformed line, skip
            return nextDocument(doc);
        }

        size_t tab2 = line.find('\t', tab1 + 1);
        if (tab2 == std::string::npos) {
            // Malformed line, skip
            return nextDocument(doc);
        }

        std::string title = line.substr(0, tab1);
        std::string date = line.substr(tab1 + 1, tab2 - tab1 - 1);
        std::string body = line.substr(tab2 + 1);

        // Clear previous fields
        doc = document::Document();

        // Add fields (match Lucene field names)
        doc.add(std::make_unique<document::TextField>("title", title));
        doc.add(std::make_unique<document::TextField>("body", body));
        doc.add(std::make_unique<document::StringField>("date", date));

        documentCount_++;
        return true;
    }

    /**
     * Get the number of documents read so far.
     * @return Document count
     */
    size_t documentCount() const { return documentCount_; }

    /**
     * Reset the file to beginning for re-reading.
     */
    void reset() {
        file_.clear();
        file_.seekg(0);
        documentCount_ = 0;
    }

private:
    std::ifstream file_;
    size_t documentCount_;
};

}  // namespace diagon::benchmarks
