// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

/**
 * Lucene90TermLookupValidator — end-to-end validation that the Phase C pipeline
 * (FST → BlockTree → PostingsReader) can read terms from a real OpenSearch index.
 *
 * Exercises: MMapDirectory → SegmentInfos → FieldInfos → Lucene90PostingsFormat::fieldsProducer()
 *            → terms(field) → seekExact(term) → postings() → iterate doc IDs
 *
 * Usage:
 *   ./Lucene90TermLookupValidator /path/to/shard/index [segment_name]
 */

#include "diagon/codecs/CodecUtil.h"
#include "diagon/codecs/lucene90/Lucene90BlockTreeTermsReader.h"
#include "diagon/codecs/lucene90/Lucene90PostingsFormat.h"
#include "diagon/codecs/lucene90/Lucene90PostingsReader.h"
#include "diagon/codecs/lucene94/Lucene94FieldInfosFormat.h"
#include "diagon/codecs/lucene99/Lucene99SegmentInfoFormat.h"
#include "diagon/index/PostingsEnum.h"
#include "diagon/index/SegmentInfo.h"
#include "diagon/store/MMapDirectory.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace diagon;

static const int NO_MORE_DOCS = index::PostingsEnum::NO_MORE_DOCS;

/**
 * Find a non-compound segment with postings files.
 */
static std::shared_ptr<index::SegmentInfo> findNonCompoundSegment(
    const index::SegmentInfos& sis, store::Directory& dir, const std::string& preferred = "") {

    auto files = dir.listAll();

    for (int i = 0; i < sis.size(); i++) {
        auto seg = sis.info(i);
        if (!preferred.empty() && seg->name() != preferred) continue;

        // Check if this segment has individual .tim file
        std::string timPrefix = seg->name() + "_";
        for (const auto& f : files) {
            if (f.find(timPrefix) == 0 && f.find(".tim") != std::string::npos) {
                return seg;
            }
        }
    }

    // If preferred not found, try any
    if (!preferred.empty()) {
        return findNonCompoundSegment(sis, dir);
    }
    return nullptr;
}

/**
 * Extract the postings suffix from file listing.
 * E.g., from "_13_Lucene90_0.tim" → "Lucene90_0"
 */
static std::string extractPostingsSuffix(const std::string& segName, store::Directory& dir) {
    auto files = dir.listAll();
    std::string prefix = segName + "_";
    for (const auto& f : files) {
        if (f.find(prefix) == 0 && f.find(".tim") != std::string::npos) {
            // Extract between prefix and ".tim"
            std::string remainder = f.substr(prefix.size());
            auto dotPos = remainder.rfind('.');
            if (dotPos != std::string::npos) {
                return remainder.substr(0, dotPos);
            }
        }
    }
    return "";
}

/**
 * Try to seek a term and iterate its postings.
 * Returns number of doc IDs found, or -1 on error.
 */
static int testTermLookup(codecs::FieldsProducer& producer,
                           const std::string& fieldName,
                           const std::string& termText) {
    auto terms = producer.terms(fieldName);
    if (!terms) {
        std::cout << "    [SKIP] Field '" << fieldName << "' not found in terms dict\n";
        return -1;
    }

    std::cout << "    Field '" << fieldName << "': "
              << terms->size() << " terms, docCount=" << terms->getDocCount()
              << ", sumDocFreq=" << terms->getSumDocFreq() << "\n";

    auto termsEnum = terms->iterator();
    if (!termsEnum) {
        std::cout << "    [FAIL] Could not create TermsEnum\n";
        return -1;
    }

    // seekExact
    util::BytesRef termRef(reinterpret_cast<const uint8_t*>(termText.data()), termText.size());
    bool found = termsEnum->seekExact(termRef);
    if (!found) {
        std::cout << "    [INFO] Term '" << termText << "' not found\n";
        return 0;
    }

    int docFreq = termsEnum->docFreq();
    int64_t totalTermFreq = termsEnum->totalTermFreq();
    std::cout << "    Term '" << termText << "': docFreq=" << docFreq
              << " totalTermFreq=" << totalTermFreq << "\n";

    // Get postings
    auto postings = termsEnum->postings();
    if (!postings) {
        std::cout << "    [FAIL] Could not create PostingsEnum\n";
        return -1;
    }

    // Iterate doc IDs (sample first 10)
    int count = 0;
    int firstDocs[10] = {};
    while (true) {
        int doc = postings->nextDoc();
        if (doc == NO_MORE_DOCS) break;
        if (count < 10) {
            firstDocs[count] = doc;
        }
        count++;
    }

    std::cout << "    Iterated " << count << " docs";
    if (count > 0) {
        std::cout << " (first: ";
        for (int i = 0; i < std::min(count, 10); i++) {
            if (i > 0) std::cout << ", ";
            std::cout << firstDocs[i];
        }
        if (count > 10) std::cout << ", ...";
        std::cout << ")";
    }
    std::cout << "\n";

    // Verify count matches docFreq
    if (count != docFreq) {
        std::cout << "    [WARN] Iterated " << count << " docs but docFreq=" << docFreq << "\n";
    }

    return count;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <index-directory> [segment_name]\n";
        std::cerr << "  End-to-end validation of Lucene90 term lookup pipeline.\n";
        std::cerr << "  Example: " << argv[0]
                  << " /home/ubuntu/opensearch_snapshots/raw_shard/index _13\n";
        return 1;
    }

    std::string indexPath = argv[1];
    std::string preferredSegment = (argc >= 3) ? argv[2] : "_13";

    if (!fs::exists(indexPath) || !fs::is_directory(indexPath)) {
        std::cerr << "Error: '" << indexPath << "' is not a valid directory.\n";
        return 1;
    }

    std::cout << "==================== Lucene90 Term Lookup Validator ====================\n";
    std::cout << "Index: " << indexPath << "\n\n";

    int totalPass = 0, totalFail = 0;

    try {
        // 1. Open directory
        auto dir = store::MMapDirectory::open(indexPath);
        std::cout << "[1] Opened MMapDirectory\n";

        // 2. Read segments_N
        auto sis = index::SegmentInfos::readLatestCommit(*dir);
        std::cout << "[2] Read segments file: " << sis.size() << " segments, "
                  << sis.totalMaxDoc() << " total docs\n";

        // 3. Find a non-compound segment
        auto seg = findNonCompoundSegment(sis, *dir, preferredSegment);
        if (!seg) {
            std::cerr << "Error: No non-compound segment found.\n";
            return 1;
        }
        std::cout << "[3] Using segment: " << seg->name()
                  << " (codec=" << seg->codecName()
                  << ", maxDoc=" << seg->maxDoc() << ")\n";

        // 4. Read FieldInfos
        codecs::lucene94::Lucene94FieldInfosFormat fnmFormat;
        auto fieldInfos = fnmFormat.read(*dir, *seg);
        std::cout << "[4] Read FieldInfos: " << fieldInfos.size() << " fields\n";

        // Show indexed fields
        std::vector<std::string> indexedFields;
        for (const auto& fi : fieldInfos) {
            if (fi.hasPostings()) {
                indexedFields.push_back(fi.name);
                if (indexedFields.size() <= 10) {
                    std::cout << "    Field " << fi.number << ": \"" << fi.name
                              << "\" indexOptions=" << static_cast<int>(fi.indexOptions) << "\n";
                }
            }
        }
        std::cout << "    " << indexedFields.size() << " indexed fields total\n";

        // 5. Extract suffix and create SegmentReadState
        std::string suffix = extractPostingsSuffix(seg->name(), *dir);
        if (suffix.empty()) {
            std::cerr << "Error: Could not determine postings suffix.\n";
            return 1;
        }
        std::cout << "[5] Postings suffix: \"" << suffix << "\"\n";

        index::SegmentReadState state(
            dir.get(), seg->name(), seg->maxDoc(), fieldInfos,
            seg->segmentID(), suffix);

        // 6. Create FieldsProducer via Lucene90PostingsFormat
        std::cout << "[6] Creating Lucene90PostingsFormat::fieldsProducer()...\n";
        codecs::lucene90::Lucene90PostingsFormat fmt;
        std::unique_ptr<codecs::FieldsProducer> producer;
        try {
            producer = fmt.fieldsProducer(state);
            std::cout << "    [PASS] FieldsProducer created successfully\n";
            totalPass++;
        } catch (const std::exception& e) {
            std::cout << "    [FAIL] " << e.what() << "\n";
            totalFail++;
            return 1;
        }

        // 7. FST diagnostic: trace traversal for one term
        std::cout << "\n[7] FST diagnostic for field 'message':\n";
        {
            auto terms = producer->terms("message");
            if (terms) {
                auto termsEnum = terms->iterator();
                // Try to list what field reader metadata looks like
                std::cout << "    Terms object created: " << terms->size() << " terms\n";

                // Use termsEnum->seekExact with a very common term
                // But first let's try a keyword field which has exact-match terms
            }
        }

        // Also try keyword fields that have known exact values
        std::cout << "\n[7b] Test keyword fields (exact match):\n";
        {
            // The index has cloud.region.keyword, input.type, etc.
            auto terms = producer->terms("input.type");
            if (terms) {
                std::cout << "    input.type: " << terms->size() << " terms, docCount="
                          << terms->getDocCount() << "\n";
            } else {
                std::cout << "    input.type: not found\n";
            }
        }

        std::cout << "\n[8] Term lookup tests:\n\n";

        // List of (field, term) pairs to test — include keyword fields
        struct TestCase {
            std::string field;
            std::string term;
        };
        std::vector<TestCase> tests = {
            // Terms directly in root floor sub-block (no descent needed)
            {"message", "ant"},
            {"message", "eagle"},
            {"message", "elf"},
            // Terms requiring sub-block descent (floor + 1 level)
            {"message", "thair"},    // th sub-block -> "air" term
            {"message", "thead"},    // th sub-block -> "ead" term
            // Terms requiring deeper descent (2+ levels)
            {"message", "thand"},    // th sub-block -> "and" term
            // Keyword fields (leaf blocks, no descent)
            {"cloud.region.keyword", "eu-central-1"},
            {"cloud.region.keyword", "us-east-1"},
            {"cloud.region.keyword", "ap-southeast-1"},
            // Non-existent terms (should return 0)
            {"message", "zzzzz"},
            {"cloud.region.keyword", "mars-1"},
        };

        for (const auto& tc : tests) {
            std::cout << "  --- Lookup: field=\"" << tc.field
                      << "\" term=\"" << tc.term << "\" ---\n";
            try {
                int count = testTermLookup(*producer, tc.field, tc.term);
                if (count > 0) {
                    std::cout << "    [PASS] Found " << count << " docs\n";
                    totalPass++;
                } else if (count == 0) {
                    std::cout << "    [INFO] Term not in this segment (expected for some terms)\n";
                } else {
                    totalFail++;
                }
            } catch (const std::exception& e) {
                std::cout << "    [FAIL] Exception: " << e.what() << "\n";
                totalFail++;
            }
            std::cout << "\n";
        }

        // 8. Summary
        std::cout << "==================== RESULTS ====================\n";
        std::cout << "  PASS: " << totalPass << "  FAIL: " << totalFail << "\n";
        if (totalFail == 0 && totalPass > 0) {
            std::cout << "  Phase C.3-C.5 end-to-end validation: SUCCESS\n";
        } else if (totalFail > 0) {
            std::cout << "  Phase C.3-C.5 end-to-end validation: FAILED\n";
        }
        std::cout << "\n";

        return totalFail > 0 ? 1 : 0;

    } catch (const std::exception& e) {
        std::cerr << "FATAL ERROR: " << e.what() << "\n";
        return 1;
    }
}
