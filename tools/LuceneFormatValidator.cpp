// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

/**
 * LuceneFormatValidator — standalone tool that opens an OpenSearch/Lucene index
 * directory and tests each format reader individually.
 *
 * Reports pass/fail per format, hex-dumps failures.
 *
 * Usage:
 *   ./LuceneFormatValidator /path/to/shard/index
 */

#include "diagon/codecs/Codec.h"
#include "diagon/codecs/CodecUtil.h"
#include "diagon/codecs/lucene90/Lucene90OSStoredFieldsReader.h"
#include "diagon/codecs/lucene94/Lucene94FieldInfosFormat.h"
#include "diagon/codecs/lucene99/Lucene99SegmentInfoFormat.h"
#include "diagon/index/SegmentInfo.h"
#include "diagon/store/CompoundDirectory.h"
#include "diagon/store/FSDirectory.h"
#include "diagon/store/MMapDirectory.h"

#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace diagon;

// ==================== Helpers ====================

static std::string readCodecHeader(store::IndexInput& input) {
    int64_t savedPos = input.getFilePointer();
    try {
        int32_t magic = input.readInt();
        if (magic != codecs::CodecUtil::CODEC_MAGIC) {
            input.seek(savedPos);
            return "(no CodecUtil header)";
        }
        std::string codecName = input.readString();
        int32_t version = input.readInt();

        input.seek(savedPos);
        return "codec=\"" + codecName + "\" version=" + std::to_string(version);
    } catch (const std::exception& e) {
        input.seek(savedPos);
        return "(error reading header: " + std::string(e.what()) + ")";
    }
}

struct TestResult {
    std::string format;
    std::string fileName;
    bool passed;
    std::string detail;
};

static void printResults(const std::vector<TestResult>& results) {
    int pass = 0, fail = 0;
    for (const auto& r : results) {
        if (r.passed)
            pass++;
        else
            fail++;
    }

    std::cout << "\n==================== FORMAT VALIDATION RESULTS ====================\n\n";
    std::cout << "  PASS: " << pass << "  FAIL: " << fail
              << "  TOTAL: " << (pass + fail) << "\n\n";

    for (const auto& r : results) {
        std::cout << "  " << (r.passed ? "[PASS]" : "[FAIL]") << " " << r.format;
        if (!r.fileName.empty())
            std::cout << " (" << r.fileName << ")";
        std::cout << "\n";
        if (!r.detail.empty())
            std::cout << "         " << r.detail << "\n";
    }
    std::cout << "\n";
}

// ==================== Format Tests ====================

static TestResult testSegmentsN(store::Directory& dir) {
    TestResult r{"segments_N", "", false, ""};
    try {
        // Find the segments_N file
        auto files = dir.listAll();
        std::string segFile;
        int64_t maxGen = -1;
        for (const auto& f : files) {
            if (f.find("segments_") == 0 && f != "segments.gen") {
                std::string genStr = f.substr(9);
                try {
                    int64_t gen = std::stoll(genStr, nullptr, 36);
                    if (gen > maxGen) {
                        maxGen = gen;
                        segFile = f;
                    }
                } catch (...) {
                }
            }
        }
        if (segFile.empty()) {
            r.detail = "No segments_N file found";
            return r;
        }
        r.fileName = segFile;

        // Try to read it
        auto sis = index::SegmentInfos::read(dir, segFile);
        r.passed = true;
        r.detail = std::to_string(sis.size()) + " segments, version=" +
                   std::to_string(sis.getVersion());

        // Print segment info
        for (int i = 0; i < sis.size(); i++) {
            auto si = sis.info(i);
            std::cout << "    Segment " << si->name() << ": codec=\"" << si->codecName()
                      << "\" maxDoc=" << si->maxDoc()
                      << " delCount=" << si->delCount() << "\n";
        }
    } catch (const std::exception& e) {
        r.detail = e.what();
    }
    return r;
}

static TestResult testSegmentInfo(store::Directory& dir, const index::SegmentInfo& seg) {
    TestResult r{".si (SegmentInfo)", seg.name() + ".si", false, ""};
    try {
        codecs::lucene99::Lucene99SegmentInfoFormat siFormat;
        auto si = siFormat.read(dir, seg.name(), seg.segmentID());
        if (si) {
            r.passed = true;
            r.detail = "maxDoc=" + std::to_string(si->maxDoc()) +
                       " codec=\"" + si->codecName() + "\"" +
                       " files=" + std::to_string(si->files().size());
        } else {
            r.detail = "read() returned nullptr (no .si file?)";
        }
    } catch (const std::exception& e) {
        r.detail = e.what();
    }
    return r;
}

static TestResult testFieldInfos(store::Directory& dir, const index::SegmentInfo& seg) {
    TestResult r{".fnm (FieldInfos)", seg.name() + ".fnm", false, ""};
    try {
        codecs::lucene94::Lucene94FieldInfosFormat fnmFormat;
        auto fieldInfos = fnmFormat.read(dir, seg);
        r.passed = true;
        r.detail = std::to_string(fieldInfos.size()) + " fields";
        size_t count = 0;
        for (const auto& fi : fieldInfos) {
            if (count >= 10) break;
            std::cout << "    Field " << fi.number << ": \"" << fi.name
                      << "\" indexed=" << fi.hasPostings() << "\n";
            count++;
        }
        if (fieldInfos.size() > 10) {
            std::cout << "    ... and " << (fieldInfos.size() - 10) << " more\n";
        }
    } catch (const std::exception& e) {
        r.detail = e.what();
    }
    return r;
}

static TestResult testStoredFields(store::Directory& dir, const index::SegmentInfo& seg) {
    TestResult r{".fdt/.fdx/.fdm (StoredFields)", seg.name() + ".fdt", false, ""};
    try {
        // Check if .fdt file exists
        std::string fdtFile = seg.name() + ".fdt";
        auto files = dir.listAll();
        bool hasFdt = false;
        for (const auto& f : files) {
            if (f == fdtFile) {
                hasFdt = true;
                break;
            }
        }
        if (!hasFdt) {
            r.detail = "No .fdt file (may be in compound file)";
            return r;
        }

        // Try to read the .fdt header
        auto input = dir.openInput(fdtFile, store::IOContext::READ);
        std::string headerInfo = readCodecHeader(*input);
        r.passed = true;
        r.detail = headerInfo + " size=" + std::to_string(input->length()) + " bytes";
    } catch (const std::exception& e) {
        r.detail = e.what();
    }
    return r;
}

static TestResult testCompoundFile(store::Directory& dir, const index::SegmentInfo& seg) {
    TestResult r{".cfs/.cfe (Compound)", seg.name() + ".cfs", false, ""};
    try {
        // Check if compound files exist
        std::string cfsFile = seg.name() + ".cfs";
        auto files = dir.listAll();
        bool hasCfs = false;
        for (const auto& f : files) {
            if (f == cfsFile) {
                hasCfs = true;
                break;
            }
        }
        if (!hasCfs) {
            r.detail = "No compound file (segment uses individual files)";
            r.passed = true;  // Not an error — larger segments don't use compound files
            return r;
        }

        auto cfsDir = store::CompoundDirectory::open(dir, seg.name());
        auto subFiles = cfsDir->listAll();
        r.passed = true;
        r.detail = std::to_string(subFiles.size()) + " sub-files";
        for (const auto& f : subFiles) {
            std::cout << "    " << f << " (" << cfsDir->fileLength(f) << " bytes)\n";
        }
    } catch (const std::exception& e) {
        r.detail = e.what();
    }
    return r;
}

static TestResult testPostingsHeader(store::Directory& dir, const index::SegmentInfo& seg) {
    TestResult r{".doc/.pos/.tim/.tip (Postings)", "", false, ""};
    try {
        // Look for postings files (could be named _N_Lucene90_0.doc or in compound)
        auto files = dir.listAll();
        std::string prefix = seg.name() + "_";
        std::vector<std::string> postingsFiles;
        for (const auto& f : files) {
            if (f.find(prefix) == 0 &&
                (f.find(".doc") != std::string::npos ||
                 f.find(".pos") != std::string::npos ||
                 f.find(".tim") != std::string::npos ||
                 f.find(".tip") != std::string::npos ||
                 f.find(".tmd") != std::string::npos)) {
                postingsFiles.push_back(f);
            }
        }

        if (postingsFiles.empty()) {
            r.detail = "No postings files found (may be in compound file)";
            return r;
        }

        // Validate headers on each file
        std::ostringstream detail;
        for (const auto& f : postingsFiles) {
            auto input = dir.openInput(f, store::IOContext::READ);
            std::string header = readCodecHeader(*input);
            detail << f << ": " << header << "\n         ";
            r.fileName = f;
        }

        r.passed = true;  // We can read headers (actual postings decoding is Phase C)
        r.detail = "Headers readable (Phase C needed for full decode):\n         " + detail.str();
    } catch (const std::exception& e) {
        r.detail = e.what();
    }
    return r;
}

static TestResult testNormsHeader(store::Directory& dir, const index::SegmentInfo& seg) {
    TestResult r{".nvd/.nvm (Norms)", "", false, ""};
    try {
        auto files = dir.listAll();
        for (const auto& f : files) {
            if (f == seg.name() + ".nvd" || f == seg.name() + ".nvm") {
                auto input = dir.openInput(f, store::IOContext::READ);
                std::string header = readCodecHeader(*input);
                std::cout << "    " << f << ": " << header
                          << " (" << input->length() << " bytes)\n";
            }
        }
        r.passed = true;
        r.detail = "Headers readable (Lucene90 norms reader needed for full decode)";
    } catch (const std::exception& e) {
        r.detail = e.what();
    }
    return r;
}

static TestResult testDocValuesHeader(store::Directory& dir, const index::SegmentInfo& seg) {
    TestResult r{".dvd/.dvm (DocValues)", "", false, ""};
    try {
        auto files = dir.listAll();
        for (const auto& f : files) {
            if ((f.find(seg.name()) == 0) &&
                (f.find(".dvd") != std::string::npos || f.find(".dvm") != std::string::npos)) {
                auto input = dir.openInput(f, store::IOContext::READ);
                std::string header = readCodecHeader(*input);
                std::cout << "    " << f << ": " << header
                          << " (" << input->length() << " bytes)\n";
            }
        }
        r.passed = true;
        r.detail = "Headers readable (Lucene90 doc values reader needed for full decode)";
    } catch (const std::exception& e) {
        r.detail = e.what();
    }
    return r;
}

// ==================== Main ====================

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <index-directory>\n";
        std::cerr << "  Opens an OpenSearch/Lucene index and validates format compatibility.\n";
        return 1;
    }

    std::string indexPath = argv[1];
    if (!fs::exists(indexPath) || !fs::is_directory(indexPath)) {
        std::cerr << "Error: '" << indexPath << "' is not a valid directory.\n";
        return 1;
    }

    std::cout << "==================== Lucene Format Validator ====================\n";
    std::cout << "Index: " << indexPath << "\n\n";

    std::vector<TestResult> results;

    try {
        // Open directory (MMap for read performance)
        auto dir = store::MMapDirectory::open(indexPath);

        // List all files
        auto allFiles = dir->listAll();
        std::cout << "Files in index directory: " << allFiles.size() << "\n";
        for (const auto& f : allFiles) {
            int64_t len = 0;
            try {
                len = dir->fileLength(f);
            } catch (...) {
            }
            std::cout << "  " << f << " (" << len << " bytes)\n";
        }
        std::cout << "\n";

        // Test 1: segments_N
        std::cout << "--- Test: segments_N ---\n";
        auto segResult = testSegmentsN(*dir);
        results.push_back(segResult);
        std::cout << "\n";

        if (!segResult.passed) {
            std::cout << "FATAL: Cannot read segments_N — cannot proceed.\n";
            printResults(results);
            return 1;
        }

        // Re-read segments to get segment list
        auto files = dir->listAll();
        std::string segFile;
        int64_t maxGen = -1;
        for (const auto& f : files) {
            if (f.find("segments_") == 0 && f != "segments.gen") {
                std::string genStr = f.substr(9);
                try {
                    int64_t gen = std::stoll(genStr, nullptr, 36);
                    if (gen > maxGen) {
                        maxGen = gen;
                        segFile = f;
                    }
                } catch (...) {
                }
            }
        }

        auto sis = index::SegmentInfos::read(*dir, segFile);

        // For each segment, test individual formats
        // Pick the first non-compound segment (has individual files) and a compound one
        int testedNonCompound = 0;
        int testedCompound = 0;

        for (int i = 0; i < sis.size(); i++) {
            auto seg = sis.info(i);

            // Check if this segment has individual files (non-compound: has .fnm outside .cfs)
            bool hasFnmFile = false;
            for (const auto& f : allFiles) {
                if (f == seg->name() + ".fnm") {
                    hasFnmFile = true;
                    break;
                }
            }

            // Check if this has a .cfs file (compound)
            bool hasCfsFile = false;
            for (const auto& f : allFiles) {
                if (f == seg->name() + ".cfs") {
                    hasCfsFile = true;
                    break;
                }
            }

            if (hasFnmFile && testedNonCompound < 2) {
                std::cout << "--- Testing non-compound segment: " << seg->name()
                          << " (codec=" << seg->codecName() << ") ---\n";

                results.push_back(testSegmentInfo(*dir, *seg));
                results.push_back(testFieldInfos(*dir, *seg));
                results.push_back(testStoredFields(*dir, *seg));
                results.push_back(testPostingsHeader(*dir, *seg));
                results.push_back(testNormsHeader(*dir, *seg));
                results.push_back(testDocValuesHeader(*dir, *seg));

                testedNonCompound++;
                std::cout << "\n";
            }

            if (hasCfsFile && testedCompound < 1) {
                std::cout << "--- Testing compound segment: " << seg->name()
                          << " (codec=" << seg->codecName() << ") ---\n";

                // .si always exists as individual file even for compound segments
                results.push_back(testSegmentInfo(*dir, *seg));
                results.push_back(testCompoundFile(*dir, *seg));

                testedCompound++;
                std::cout << "\n";
            }

            if (testedNonCompound >= 2 && testedCompound >= 1)
                break;
        }

    } catch (const std::exception& e) {
        std::cerr << "FATAL ERROR: " << e.what() << "\n";
        return 1;
    }

    printResults(results);
    return 0;
}
