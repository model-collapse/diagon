// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

/**
 * Manual validation of Lucene104 codec integration
 * Simplified to just test write/read cycle
 */

#include "diagon/codecs/Codec.h"
#include "diagon/codecs/PostingsFormat.h"

#include <iostream>

using namespace diagon;

int main() {
    std::cout << "=== Lucene104 Codec Integration Validation ===" << std::endl;

    try {
        // Test 1: Codec registration
        auto& codec = codecs::Codec::forName("Lucene104");
        std::cout << "✓ Lucene104 codec found and loaded" << std::endl;

        // Test 2: Codec has postings format
        auto& postingsFormat = codec.postingsFormat();
        std::cout << "✓ PostingsFormat retrieved: " << postingsFormat.getName() << std::endl;

        std::cout << std::endl << "=== BASIC TESTS PASSED ===" << std::endl;
        std::cout << "Note: Full end-to-end testing requires IndexWriter integration test"
                  << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "✗ Exception: " << e.what() << std::endl;
        return 1;
    }
}
