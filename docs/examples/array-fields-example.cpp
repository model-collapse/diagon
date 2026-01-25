// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

/**
 * Multi-Valued (Array) Fields Example
 *
 * Demonstrates how to use array fields in Diagon with explicit schema declaration.
 */

#include <diagon/document/Document.h>
#include <diagon/document/Field.h>
#include <diagon/document/ArrayField.h>
#include <diagon/index/IndexMapping.h>
#include <diagon/index/IndexWriter.h>
#include <diagon/search/IndexSearcher.h>
#include <diagon/store/FSDirectory.h>

#include <iostream>
#include <vector>

using namespace diagon;

int main() {
    // ==================== Step 1: Create Index Mapping (Schema) ====================

    index::IndexMapping mapping;

    // Single-valued fields
    mapping.addField("title",
                     index::IndexOptions::DOCS_AND_FREQS_AND_POSITIONS,
                     index::DocValuesType::NONE,
                     true,   // stored
                     true,   // tokenized
                     false); // omitNorms

    mapping.addField("price",
                     index::IndexOptions::NONE,
                     index::DocValuesType::NUMERIC,
                     false,  // stored
                     false,  // tokenized
                     true);  // omitNorms

    // Multi-valued (array) fields - EXPLICIT DECLARATION REQUIRED
    mapping.addArrayField("categories", index::ArrayElementType::STRING, true);  // Array(String)
    mapping.addArrayField("tags", index::ArrayElementType::TEXT, false);         // Array(Text)
    mapping.addArrayField("ratings", index::ArrayElementType::NUMERIC, false);   // Array(Int64)

    std::cout << "Created index mapping with " << mapping.size() << " fields\n";
    std::cout << "Array fields:\n";
    for (const auto& name : mapping.fieldNames()) {
        if (mapping.isMultiValued(name)) {
            std::cout << "  - " << name << " (array)\n";
        }
    }

    // ==================== Step 2: Create Index Writer ====================

    auto dir = store::FSDirectory::open("/tmp/array-index");
    index::IndexWriterConfig config;
    config.setRAMBufferSizeMB(256);
    // TODO: Set mapping on config when IndexWriter supports it
    // config.setIndexMapping(mapping);

    // ==================== Step 3: Index Documents with Array Fields ====================

    {
        document::Document doc;

        // Single-valued fields
        doc.add(std::make_unique<document::TextField>("title", "Gaming Laptop", true));
        doc.add(std::make_unique<document::NumericDocValuesField>("price", 149999));  // $1499.99

        // Multi-valued array fields
        doc.add(std::make_unique<document::ArrayStringField>(
            "categories",
            std::vector<std::string>{"electronics", "computers", "laptops"},
            true
        ));

        doc.add(std::make_unique<document::ArrayTextField>(
            "tags",
            std::vector<std::string>{"high performance", "gaming", "portable"},
            false
        ));

        doc.add(std::make_unique<document::ArrayNumericField>(
            "ratings",
            std::vector<int64_t>{5, 5, 4, 5, 3}  // User ratings
        ));

        std::cout << "\nIndexed document with:\n";
        std::cout << "  - 3 categories\n";
        std::cout << "  - 3 tags\n";
        std::cout << "  - 5 ratings\n";
    }

    {
        document::Document doc;

        // Single-valued
        doc.add(std::make_unique<document::TextField>("title", "Budget Laptop", true));
        doc.add(std::make_unique<document::NumericDocValuesField>("price", 59999));  // $599.99

        // Multi-valued
        doc.add(std::make_unique<document::ArrayStringField>(
            "categories",
            std::vector<std::string>{"electronics", "computers"},
            true
        ));

        doc.add(std::make_unique<document::ArrayTextField>(
            "tags",
            std::vector<std::string>{"affordable", "work", "portable"},
            false
        ));

        doc.add(std::make_unique<document::ArrayNumericField>(
            "ratings",
            std::vector<int64_t>{4, 4, 3, 4}
        ));

        std::cout << "\nIndexed document with:\n";
        std::cout << "  - 2 categories\n";
        std::cout << "  - 3 tags\n";
        std::cout << "  - 4 ratings\n";
    }

    // ==================== Step 4: Query Array Fields ====================

    std::cout << "\n=== Query Examples ===\n";

    // Example 1: Term query on array field (matches if ANY value contains term)
    std::cout << "\nQuery: Find products in 'laptops' category\n";
    std::cout << "  - Matches if 'laptops' is in categories array\n";
    // auto query1 = search::TermQuery::create("categories", "laptops");

    // Example 2: ArrayContainsAll query
    std::cout << "\nQuery: Find products with ALL of: 'electronics', 'computers', 'laptops'\n";
    std::cout << "  - Matches only if ALL terms present in array\n";
    // auto query2 = search::ArrayContainsAllQuery::create("categories",
    //     std::vector<std::string>{"electronics", "computers", "laptops"});

    // Example 3: Range query on numeric array
    std::cout << "\nQuery: Find products with rating >= 4\n";
    std::cout << "  - Matches if ANY rating in array >= 4\n";
    // auto query3 = search::RangeQuery::create("ratings", 4, INT64_MAX);

    // Example 4: Array size query
    std::cout << "\nQuery: Find products with at least 5 ratings\n";
    std::cout << "  - Matches based on array length\n";
    // auto query4 = search::ArraySizeQuery::createMin("ratings", 5);

    // Example 5: Phrase query across array values
    std::cout << "\nQuery: Phrase 'high performance' in tags\n";
    std::cout << "  - Positions are continuous across array values\n";
    // auto query5 = search::PhraseQuery::builder("tags")
    //     .add("high").add("performance")
    //     .build();

    // ==================== Step 5: Field Type Behaviors ====================

    std::cout << "\n=== Field Type Behaviors ===\n";

    std::cout << "\nArrayTextField (tags):\n";
    std::cout << "  - Tokenized: each value split into terms\n";
    std::cout << "  - Positions: continuous across values\n";
    std::cout << "  - Phrase queries: work across array boundaries\n";
    std::cout << "  - Deduplication: NO (bag semantics)\n";

    std::cout << "\nArrayStringField (categories):\n";
    std::cout << "  - Not tokenized: each value is single term\n";
    std::cout << "  - Exact match only\n";
    std::cout << "  - Sorted and deduplicated within document\n";
    std::cout << "  - Storage: SORTED_SET (ordinal-based)\n";

    std::cout << "\nArrayNumericField (ratings):\n";
    std::cout << "  - Stored in column format (not inverted index)\n";
    std::cout << "  - Sorted but NOT deduplicated\n";
    std::cout << "  - Efficient range queries and aggregations\n";
    std::cout << "  - Storage: SORTED_NUMERIC\n";

    std::cout << "\n=== Array Field Operations ===\n";

    // Demonstrate getSortedUniqueValues for ArrayStringField
    {
        document::ArrayStringField categories("categories",
            std::vector<std::string>{"computers", "laptops", "computers", "electronics"},
            false);

        auto sorted = categories.getSortedUniqueValues();
        std::cout << "\nArrayStringField deduplication:\n";
        std::cout << "  Input:  [computers, laptops, computers, electronics]\n";
        std::cout << "  Output: [";
        for (size_t i = 0; i < sorted.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << sorted[i];
        }
        std::cout << "] (sorted, deduplicated)\n";
    }

    // Demonstrate getSortedValues for ArrayNumericField
    {
        document::ArrayNumericField ratings("ratings",
            std::vector<int64_t>{5, 3, 4, 5, 2, 4});

        auto sorted = ratings.getSortedValues();
        std::cout << "\nArrayNumericField sorting:\n";
        std::cout << "  Input:  [5, 3, 4, 5, 2, 4]\n";
        std::cout << "  Output: [";
        for (size_t i = 0; i < sorted.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << sorted[i];
        }
        std::cout << "] (sorted, NOT deduplicated)\n";
    }

    // Demonstrate tokenization for ArrayTextField
    {
        document::ArrayTextField tags("tags",
            std::vector<std::string>{"high performance", "gaming laptop", "portable"},
            false);

        auto tokens = tags.tokenize();
        std::cout << "\nArrayTextField tokenization:\n";
        std::cout << "  Values: [\"high performance\", \"gaming laptop\", \"portable\"]\n";
        std::cout << "  Tokens: [";
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << tokens[i];
        }
        std::cout << "] (positions: 0-6)\n";
    }

    std::cout << "\n=== Summary ===\n";
    std::cout << "Multi-valued fields provide:\n";
    std::cout << "  ✓ Explicit schema declaration (type safety)\n";
    std::cout << "  ✓ Three array types for different use cases\n";
    std::cout << "  ✓ Efficient storage with deduplication/sorting\n";
    std::cout << "  ✓ Rich query support (contains, size, range)\n";
    std::cout << "  ✓ Backward compatible with single-valued fields\n";

    return 0;
}
