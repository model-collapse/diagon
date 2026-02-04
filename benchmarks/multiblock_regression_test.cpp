// Regression test for multi-block term dictionary traversal
// Verifies that BlockTreeTermsReader correctly handles multiple blocks

#include "diagon/index/IndexWriter.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/store/FSDirectory.h"

#include <iostream>
#include <sstream>
#include <vector>
#include <algorithm>

using namespace diagon;

int main() {
    std::cout << "=== Multi-Block Traversal Regression Test ===" << std::endl << std::endl;

    // Create index with enough terms to span multiple blocks (48 terms per block)
    std::string indexPath = "/tmp/multiblock_test";
    [[maybe_unused]] int ret1 = system(("rm -rf " + indexPath).c_str());
    [[maybe_unused]] int ret2 = system(("mkdir -p " + indexPath).c_str());

    auto dir = store::FSDirectory::open(indexPath);
    index::IndexWriterConfig config;
    auto writer = std::make_unique<index::IndexWriter>(*dir, config);

    std::cout << "Indexing documents with 200 unique terms..." << std::endl;

    // Create 200 unique terms across 5 documents
    // This will create ~5 blocks (200 / 48 ≈ 4.17)
    std::vector<std::string> allTerms;
    for (int i = 0; i < 200; i++) {
        allTerms.push_back("term" + std::to_string(i));
    }

    // Sort terms alphabetically (as they will be in the index)
    std::sort(allTerms.begin(), allTerms.end());

    // Create 5 documents, each with 40 terms
    for (int docId = 0; docId < 5; docId++) {
        document::Document doc;

        std::ostringstream content;
        for (int i = docId * 40; i < (docId + 1) * 40 && i < 200; i++) {
            if (i > docId * 40) content << " ";
            content << allTerms[i];
        }

        doc.add(std::make_unique<document::TextField>("field", content.str()));
        doc.add(std::make_unique<document::StringField>("docid", std::to_string(docId)));
        writer->addDocument(doc);
    }

    writer->commit();
    writer.reset();

    std::cout << "✓ Indexed 5 documents with 200 terms" << std::endl << std::endl;

    // Read back and test
    auto reader = index::DirectoryReader::open(*dir);
    auto leaves = reader->leaves();
    if (leaves.empty()) {
        std::cerr << "✗ No leaf segments found" << std::endl;
        return 1;
    }

    auto& leaf = leaves[0];
    auto terms = leaf.reader->terms("field");
    if (!terms) {
        std::cerr << "✗ No terms for field 'field'" << std::endl;
        return 1;
    }

    // Test 1: Verify total term count
    std::cout << "Test 1: Verify total term count" << std::endl;
    int64_t termCount = terms->size();
    std::cout << "  Total terms: " << termCount << " (expected: 200)" << std::endl;
    if (termCount != 200) {
        std::cerr << "✗ Wrong number of terms" << std::endl;
        return 1;
    }
    std::cout << "✓ Correct term count" << std::endl << std::endl;

    // Test 2: Full iteration with next() - must cross block boundaries
    std::cout << "Test 2: Full iteration with next() across all blocks" << std::endl;
    auto iter = terms->iterator();
    std::vector<std::string> iteratedTerms;
    while (iter->next()) {
        auto termBytes = iter->term();
        std::string term(reinterpret_cast<const char*>(termBytes.data()), termBytes.length());
        iteratedTerms.push_back(term);
    }

    std::cout << "  Iterated terms: " << iteratedTerms.size() << " (expected: 200)" << std::endl;
    if (iteratedTerms.size() != 200) {
        std::cerr << "✗ Iteration didn't return all terms" << std::endl;
        return 1;
    }

    // Verify order
    bool ordered = std::is_sorted(iteratedTerms.begin(), iteratedTerms.end());
    if (!ordered) {
        std::cerr << "✗ Terms not in sorted order" << std::endl;
        return 1;
    }

    // Verify match expected
    if (iteratedTerms != allTerms) {
        std::cerr << "✗ Iterated terms don't match expected terms" << std::endl;
        std::cerr << "  First mismatch: " << std::endl;
        for (size_t i = 0; i < std::min(iteratedTerms.size(), allTerms.size()); i++) {
            if (iteratedTerms[i] != allTerms[i]) {
                std::cerr << "    Position " << i << ": got '" << iteratedTerms[i]
                          << "', expected '" << allTerms[i] << "'" << std::endl;
                break;
            }
        }
        return 1;
    }

    std::cout << "✓ Full iteration successful across all blocks" << std::endl << std::endl;

    // Test 3: seekExact() to terms in different blocks
    std::cout << "Test 3: seekExact() to terms in different blocks" << std::endl;

    // Seek to first block (term0)
    auto seekIter1 = terms->iterator();
    if (!seekIter1->seekExact(util::BytesRef(reinterpret_cast<const uint8_t*>("term0"), 5))) {
        std::cerr << "✗ Failed to seek to 'term0' (first block)" << std::endl;
        return 1;
    }
    std::cout << "  ✓ Found 'term0' in first block" << std::endl;

    // Seek to middle block (term100 ≈ block #3)
    auto seekIter2 = terms->iterator();
    if (!seekIter2->seekExact(util::BytesRef(reinterpret_cast<const uint8_t*>("term100"), 7))) {
        std::cerr << "✗ Failed to seek to 'term100' (middle block)" << std::endl;
        return 1;
    }
    std::cout << "  ✓ Found 'term100' in middle block" << std::endl;

    // Seek to last block (term199)
    auto seekIter3 = terms->iterator();
    if (!seekIter3->seekExact(util::BytesRef(reinterpret_cast<const uint8_t*>("term199"), 7))) {
        std::cerr << "✗ Failed to seek to 'term199' (last block)" << std::endl;
        return 1;
    }
    std::cout << "  ✓ Found 'term199' in last block" << std::endl;

    // Seek to non-existent term
    auto seekIter4 = terms->iterator();
    if (seekIter4->seekExact(util::BytesRef(reinterpret_cast<const uint8_t*>("term999"), 7))) {
        std::cerr << "✗ Found non-existent term 'term999'" << std::endl;
        return 1;
    }
    std::cout << "  ✓ Correctly reported non-existent term" << std::endl << std::endl;

    // Test 4: seekCeil() across block boundaries
    std::cout << "Test 4: seekCeil() across block boundaries" << std::endl;

    // Seek to exact term in first block
    auto ceilIter1 = terms->iterator();
    auto status1 = ceilIter1->seekCeil(util::BytesRef(reinterpret_cast<const uint8_t*>("term0"), 5));
    if (status1 != index::TermsEnum::SeekStatus::FOUND) {
        std::cerr << "✗ seekCeil('term0') didn't return FOUND" << std::endl;
        return 1;
    }
    std::cout << "  ✓ seekCeil('term0') = FOUND" << std::endl;

    // Seek to existing term in middle (term100 exists)
    auto ceilIter2 = terms->iterator();
    auto status2 = ceilIter2->seekCeil(util::BytesRef(reinterpret_cast<const uint8_t*>("term100"), 7));
    if (status2 != index::TermsEnum::SeekStatus::FOUND) {
        std::cerr << "✗ seekCeil('term100') didn't return FOUND" << std::endl;
        return 1;
    }
    auto foundTerm = ceilIter2->term();
    std::string foundStr(reinterpret_cast<const char*>(foundTerm.data()), foundTerm.length());
    if (foundStr != "term100") {
        std::cerr << "✗ seekCeil('term100') returned wrong term: '" << foundStr << "'" << std::endl;
        return 1;
    }
    std::cout << "  ✓ seekCeil('term100') = FOUND 'term100'" << std::endl;

    // Seek to non-existent term (should return next term)
    auto ceilIter3 = terms->iterator();
    auto status3 = ceilIter3->seekCeil(util::BytesRef(reinterpret_cast<const uint8_t*>("term0999"), 8));
    if (status3 != index::TermsEnum::SeekStatus::NOT_FOUND) {
        std::cerr << "✗ seekCeil('term0999') should return NOT_FOUND" << std::endl;
        return 1;
    }
    auto ceiledTerm = ceilIter3->term();
    std::string ceiledStr(reinterpret_cast<const char*>(ceiledTerm.data()), ceiledTerm.length());
    // 'term0999' sorts between 'term099' and 'term1', so should return 'term1'
    if (ceiledStr != "term1") {
        std::cerr << "✗ seekCeil('term0999') returned wrong ceiling: '" << ceiledStr << "'" << std::endl;
        return 1;
    }
    std::cout << "  ✓ seekCeil('term0999') = NOT_FOUND, ceiling = 'term1'" << std::endl;

    // Seek past all terms
    auto ceilIter4 = terms->iterator();
    auto status4 = ceilIter4->seekCeil(util::BytesRef(reinterpret_cast<const uint8_t*>("term999"), 7));
    if (status4 != index::TermsEnum::SeekStatus::END) {
        std::cerr << "✗ seekCeil('term999') should return END" << std::endl;
        return 1;
    }
    std::cout << "  ✓ seekCeil('term999') = END" << std::endl << std::endl;

    // Test 5: Block boundary edge case - iterate across boundary
    std::cout << "Test 5: Iterate across block boundary starting from term140" << std::endl;

    // Seek to term140 (near block boundary based on debug output)
    auto boundaryIter = terms->iterator();
    if (!boundaryIter->seekExact(util::BytesRef(reinterpret_cast<const uint8_t*>("term140"), 7))) {
        std::cerr << "✗ Failed to seek to 'term140'" << std::endl;
        return 1;
    }

    // Iterate across boundary
    std::vector<std::string> boundaryTerms;
    boundaryTerms.push_back("term140");  // Starting term
    for (int i = 0; i < 5; i++) {
        if (!boundaryIter->next()) {
            std::cerr << "✗ Iteration stopped prematurely at boundary" << std::endl;
            return 1;
        }
        auto term = boundaryIter->term();
        std::string termStr(reinterpret_cast<const char*>(term.data()), term.length());
        boundaryTerms.push_back(termStr);
    }

    std::cout << "  Terms across boundary: ";
    for (const auto& t : boundaryTerms) {
        std::cout << t << " ";
    }
    std::cout << std::endl;

    // Verify we got 6 terms and they're in order
    if (boundaryTerms.size() != 6) {
        std::cerr << "✗ Expected 6 terms, got " << boundaryTerms.size() << std::endl;
        return 1;
    }

    // Verify sorted order
    bool sorted = std::is_sorted(boundaryTerms.begin(), boundaryTerms.end());
    if (!sorted) {
        std::cerr << "✗ Terms not in sorted order across boundary" << std::endl;
        return 1;
    }

    std::cout << "✓ Successfully iterated across block boundary" << std::endl << std::endl;

    std::cout << "========================================" << std::endl;
    std::cout << "✅ ALL MULTI-BLOCK TESTS PASSED" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
