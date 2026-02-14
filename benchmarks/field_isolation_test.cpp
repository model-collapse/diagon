// Test to verify field isolation in FreqProxTermsWriter
// Checks that terms in one field don't appear in another field

#include "diagon/document/Document.h"
#include "diagon/document/Field.h"
#include "diagon/index/DirectoryReader.h"
#include "diagon/index/IndexWriter.h"
#include "diagon/store/FSDirectory.h"

#include <cassert>
#include <iostream>

using namespace diagon;

int main() {
    std::cout << "=== Field Isolation Test ===" << std::endl << std::endl;

    // Create index
    std::string indexPath = "/tmp/field_isolation_test";
    [[maybe_unused]] int ret1 = system(("rm -rf " + indexPath).c_str());
    [[maybe_unused]] int ret2 = system(("mkdir -p " + indexPath).c_str());

    auto dir = store::FSDirectory::open(indexPath);
    index::IndexWriterConfig config;
    auto writer = std::make_unique<index::IndexWriter>(*dir, config);

    // Add documents with overlapping terms in different fields
    std::cout << "Indexing documents..." << std::endl;

    // Doc 1: "apple" in field1, "banana" in field2
    document::Document doc1;
    doc1.add(std::make_unique<document::TextField>("field1", "apple orange"));
    doc1.add(std::make_unique<document::TextField>("field2", "banana grape"));
    writer->addDocument(doc1);

    // Doc 2: "banana" in field1, "apple" in field2 (reversed)
    document::Document doc2;
    doc2.add(std::make_unique<document::TextField>("field1", "banana grape"));
    doc2.add(std::make_unique<document::TextField>("field2", "apple orange"));
    writer->addDocument(doc2);

    // Doc 3: Same term "test" in both fields
    document::Document doc3;
    doc3.add(std::make_unique<document::TextField>("field1", "test common"));
    doc3.add(std::make_unique<document::TextField>("field2", "test shared"));
    writer->addDocument(doc3);

    writer->commit();
    writer.reset();

    std::cout << "✓ Indexed 3 documents" << std::endl << std::endl;

    // Read back and verify field isolation
    std::cout << "Verifying field isolation..." << std::endl;
    auto reader = index::DirectoryReader::open(*dir);

    auto leaves = reader->leaves();
    if (leaves.empty()) {
        std::cerr << "✗ No leaf segments found" << std::endl;
        return 1;
    }

    auto& leaf = leaves[0];

    // Test 1: Check field1 terms
    std::cout << "\nTest 1: field1 terms" << std::endl;
    auto field1Terms = leaf.reader->terms("field1");
    if (!field1Terms) {
        std::cerr << "✗ No terms for field1" << std::endl;
        return 1;
    }

    auto field1Enum = field1Terms->iterator();
    std::vector<std::string> field1TermList;
    while (field1Enum->next()) {
        auto termBytes = field1Enum->term();
        std::string term(reinterpret_cast<const char*>(termBytes.data()), termBytes.length());
        field1TermList.push_back(term);
        std::cout << "  - '" << term << "' (docFreq=" << field1Enum->docFreq() << ")" << std::endl;
    }

    // Expected in field1: "apple", "banana", "common", "grape", "orange", "test"
    std::vector<std::string> expectedField1 = {"apple", "banana", "common",
                                               "grape", "orange", "test"};
    if (field1TermList != expectedField1) {
        std::cerr << "✗ field1 terms don't match expected" << std::endl;
        std::cerr << "  Expected: ";
        for (const auto& t : expectedField1)
            std::cerr << t << " ";
        std::cerr << "\n  Got: ";
        for (const auto& t : field1TermList)
            std::cerr << t << " ";
        std::cerr << std::endl;
    } else {
        std::cout << "✓ field1 has correct terms" << std::endl;
    }

    // Test 2: Check field2 terms
    std::cout << "\nTest 2: field2 terms" << std::endl;
    auto field2Terms = leaf.reader->terms("field2");
    if (!field2Terms) {
        std::cerr << "✗ No terms for field2" << std::endl;
        return 1;
    }

    auto field2Enum = field2Terms->iterator();
    std::vector<std::string> field2TermList;
    while (field2Enum->next()) {
        auto termBytes = field2Enum->term();
        std::string term(reinterpret_cast<const char*>(termBytes.data()), termBytes.length());
        field2TermList.push_back(term);
        std::cout << "  - '" << term << "' (docFreq=" << field2Enum->docFreq() << ")" << std::endl;
    }

    // Expected in field2: "apple", "banana", "grape", "orange", "shared", "test"
    std::vector<std::string> expectedField2 = {"apple",  "banana", "grape",
                                               "orange", "shared", "test"};
    if (field2TermList != expectedField2) {
        std::cerr << "✗ field2 terms don't match expected" << std::endl;
        std::cerr << "  Expected: ";
        for (const auto& t : expectedField2)
            std::cerr << t << " ";
        std::cerr << "\n  Got: ";
        for (const auto& t : field2TermList)
            std::cerr << t << " ";
        std::cerr << std::endl;
    } else {
        std::cout << "✓ field2 has correct terms" << std::endl;
    }

    // Test 3: Verify "apple" has correct doc frequencies per field
    std::cout << "\nTest 3: Verify term 'apple' isolation" << std::endl;

    // In field1: "apple" appears in doc1 only (docFreq=1)
    auto field1EnumApple = field1Terms->iterator();
    if (field1EnumApple->seekExact(util::BytesRef(reinterpret_cast<const uint8_t*>("apple"), 5))) {
        int docFreq = field1EnumApple->docFreq();
        std::cout << "  field1:'apple' docFreq=" << docFreq << " (expected: 1)" << std::endl;
        if (docFreq != 1) {
            std::cerr << "✗ field1:'apple' has wrong docFreq" << std::endl;
            return 1;
        }
    } else {
        std::cerr << "✗ 'apple' not found in field1" << std::endl;
        return 1;
    }

    // In field2: "apple" appears in doc2 only (docFreq=1)
    auto field2EnumApple = field2Terms->iterator();
    if (field2EnumApple->seekExact(util::BytesRef(reinterpret_cast<const uint8_t*>("apple"), 5))) {
        int docFreq = field2EnumApple->docFreq();
        std::cout << "  field2:'apple' docFreq=" << docFreq << " (expected: 1)" << std::endl;
        if (docFreq != 1) {
            std::cerr << "✗ field2:'apple' has wrong docFreq" << std::endl;
            return 1;
        }
    } else {
        std::cerr << "✗ 'apple' not found in field2" << std::endl;
        return 1;
    }

    std::cout << "✓ Term 'apple' correctly isolated per field" << std::endl;

    // Test 4: Verify "test" (appears in both fields, same document)
    std::cout << "\nTest 4: Verify term 'test' isolation (same doc, both fields)" << std::endl;

    auto field1EnumTest = field1Terms->iterator();
    if (field1EnumTest->seekExact(util::BytesRef(reinterpret_cast<const uint8_t*>("test"), 4))) {
        int docFreq = field1EnumTest->docFreq();
        std::cout << "  field1:'test' docFreq=" << docFreq << " (expected: 1)" << std::endl;
        if (docFreq != 1) {
            std::cerr << "✗ field1:'test' has wrong docFreq" << std::endl;
            return 1;
        }
    } else {
        std::cerr << "✗ 'test' not found in field1" << std::endl;
        return 1;
    }

    auto field2EnumTest = field2Terms->iterator();
    if (field2EnumTest->seekExact(util::BytesRef(reinterpret_cast<const uint8_t*>("test"), 4))) {
        int docFreq = field2EnumTest->docFreq();
        std::cout << "  field2:'test' docFreq=" << docFreq << " (expected: 1)" << std::endl;
        if (docFreq != 1) {
            std::cerr << "✗ field2:'test' has wrong docFreq" << std::endl;
            return 1;
        }
    } else {
        std::cerr << "✗ 'test' not found in field2" << std::endl;
        return 1;
    }

    std::cout << "✓ Term 'test' correctly isolated per field" << std::endl;

    std::cout << "\n========================================" << std::endl;
    std::cout << "✅ ALL FIELD ISOLATION TESTS PASSED" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
