/**
 * ClickBench Dataset Adapter
 *
 * Parses hits.tsv from the ClickBench dataset (Yandex.Metrica web analytics).
 * Each line has 105 tab-separated columns. We index a subset of 15 columns
 * as Diagon fields for analytical query benchmarking.
 *
 * Dataset: https://datasets.clickhouse.com/hits_compatible/hits.tsv.gz
 * Schema: https://github.com/ClickHouse/ClickBench
 *
 * Column mapping (0-indexed TSV columns):
 *   0  WatchID           -> NumericDocValues
 *   4  EventTime         -> StringField (stored, YYYY-MM-DD HH:MM:SS)
 *   5  EventDate         -> StringField (YYYY-MM-DD)
 *   6  CounterID         -> NumericDocValues + StringField
 *   7  ClientIP          -> NumericDocValues
 *   8  RegionID          -> NumericDocValues + StringField
 *   9  UserID            -> NumericDocValues + StringField
 *  13  URL               -> TextField (tokenized)
 *  14  Referer           -> TextField (tokenized)
 *  20  ResolutionWidth   -> NumericDocValues
 *  38  SearchEngineID    -> NumericDocValues + StringField
 *  39  SearchPhrase      -> TextField + StringField
 *  40  AdvEngineID       -> NumericDocValues + StringField
 *  14  IsRefresh (col 52)-> StringField
 *  61  DontCountHits     -> StringField
 *  52  IsLink            -> StringField
 *  53  IsDownload        -> StringField
 */

#pragma once

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/FieldInfo.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace diagon::benchmarks {

class ClickBenchAdapter {
public:
    /**
     * @param datasetPath Path to hits.tsv file
     * @param maxDocs Maximum documents to index (0 = unlimited)
     */
    explicit ClickBenchAdapter(const std::string& datasetPath, int maxDocs = 10000000)
        : maxDocs_(maxDocs)
        , docCount_(0) {
        ifs_.open(datasetPath);
        if (!ifs_.is_open()) {
            throw std::runtime_error("Cannot open ClickBench dataset: " + datasetPath);
        }
    }

    const std::unordered_map<std::string, int64_t>& getLastNumericValues() const {
        return lastNumericValues_;
    }

    bool nextDocument(document::Document& doc) {
        if (maxDocs_ > 0 && docCount_ >= maxDocs_) {
            return false;
        }

        std::string line;
        while (std::getline(ifs_, line)) {
            if (line.empty()) continue;

            // Split by tabs
            std::vector<std::string> cols;
            cols.reserve(105);
            splitTSV(line, cols);

            // hits.tsv has 105 columns; skip malformed lines
            if (cols.size() < 62) continue;

            // TextField field type: tokenized, positions for phrase queries
            static const document::FieldType ftText = []() {
                document::FieldType ft;
                ft.indexOptions = index::IndexOptions::DOCS_AND_FREQS_AND_POSITIONS;
                ft.stored = false;
                ft.tokenized = true;
                return ft;
            }();

            // Clear numeric values from previous document
            lastNumericValues_.clear();

            // -- Numeric DocValues --
            addNumericField(doc, "WatchID", cols[0]);
            addNumericField(doc, "CounterID", cols[6]);
            addNumericField(doc, "ClientIP", cols[7]);
            addNumericField(doc, "RegionID", cols[8]);
            addNumericField(doc, "UserID", cols[9]);
            addNumericField(doc, "ResolutionWidth", cols[20]);
            addNumericField(doc, "SearchEngineID", cols[38]);
            addNumericField(doc, "AdvEngineID", cols[40]);

            // -- StringFields (for exact TermQuery matching) --
            doc.add(std::make_unique<document::StringField>("EventDate", cols[5]));
            doc.add(std::make_unique<document::StringField>("CounterID_s", cols[6]));
            doc.add(std::make_unique<document::StringField>("RegionID_s", cols[8]));
            doc.add(std::make_unique<document::StringField>("UserID_s", cols[9]));
            doc.add(std::make_unique<document::StringField>("SearchEngineID_s", cols[38]));
            doc.add(std::make_unique<document::StringField>("AdvEngineID_s", cols[40]));
            doc.add(std::make_unique<document::StringField>("IsRefresh", cols[52]));
            doc.add(std::make_unique<document::StringField>("DontCountHits", cols[61]));
            doc.add(std::make_unique<document::StringField>("IsLink", cols[52]));
            doc.add(std::make_unique<document::StringField>("IsDownload", cols[53]));

            // -- TextFields (tokenized for full-text search) --
            if (!cols[13].empty()) {
                doc.add(std::make_unique<document::TextField>("URL", cols[13], ftText));
            }
            if (!cols[14].empty()) {
                doc.add(std::make_unique<document::TextField>("Referer", cols[14], ftText));
            }

            // SearchPhrase: both tokenized (for text search) and exact (for empty check)
            doc.add(std::make_unique<document::StringField>("SearchPhrase_s", cols[39]));
            if (!cols[39].empty()) {
                doc.add(std::make_unique<document::TextField>("SearchPhrase", cols[39], ftText));
            }

            docCount_++;
            return true;
        }
        return false;
    }

    int getDocumentCount() const { return docCount_; }

    void reset() {
        ifs_.clear();
        ifs_.seekg(0, std::ios::beg);
        docCount_ = 0;
    }

private:
    void splitTSV(const std::string& line, std::vector<std::string>& out) {
        size_t start = 0;
        size_t end = line.find('\t');
        while (end != std::string::npos) {
            out.emplace_back(line, start, end - start);
            start = end + 1;
            end = line.find('\t', start);
        }
        out.emplace_back(line, start);
    }

    void addNumericField(document::Document& doc, const std::string& name,
                         const std::string& val) {
        if (val.empty()) return;
        try {
            int64_t v = std::stoll(val);
            doc.add(std::make_unique<document::NumericDocValuesField>(name, v));
            lastNumericValues_[name] = v;
        } catch (...) {
            // Skip non-numeric values silently
        }
    }

    std::ifstream ifs_;
    int maxDocs_;
    int docCount_;
    std::unordered_map<std::string, int64_t> lastNumericValues_;
};

}  // namespace diagon::benchmarks
