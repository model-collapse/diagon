# Text Search Examples

Practical examples for searching and querying in Diagon.

## Simple Term Search

```cpp
#include <diagon/search/IndexSearcher.h>
#include <diagon/index/DirectoryReader.h>
#include <diagon/store/FSDirectory.h>
#include <iostream>

using namespace diagon;

int main() {
    try {
        // Open index
        auto dir = store::FSDirectory::open("/tmp/my_index");
        auto reader = index::DirectoryReader::open(dir.get());
        search::IndexSearcher searcher(reader.get());

        // Create query
        auto query = search::TermQuery::create("title", "search");

        // Execute search
        auto results = searcher.search(query.get(), 10);

        // Display results
        std::cout << "Found " << results.totalHits << " results:\n\n";

        for (const auto& hit : results.scoreDocs) {
            auto doc = searcher.doc(hit.doc);
            std::cout << "Doc " << hit.doc
                     << " (score: " << hit.score << ")\n";
            std::cout << "  Title: " << doc->get("title") << "\n";
            std::cout << "  Category: " << doc->get("category") << "\n\n";
        }

        reader->close();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
```

## Boolean Query Search

```cpp
#include <diagon/search/IndexSearcher.h>
#include <diagon/index/DirectoryReader.h>
#include <diagon/store/FSDirectory.h>
#include <iostream>

using namespace diagon;
using namespace search;

int main() {
    try {
        auto dir = store::FSDirectory::open("/tmp/my_index");
        auto reader = index::DirectoryReader::open(dir.get());
        IndexSearcher searcher(reader.get());

        // Build boolean query:
        // - MUST contain "laptop" in title
        // - SHOULD contain "gaming" (boosts score)
        // - MUST NOT be "refurbished"
        auto query = BooleanQuery::Builder()
            .add(TermQuery::create("title", "laptop"), BooleanClause::MUST)
            .add(TermQuery::create("title", "gaming"), BooleanClause::SHOULD)
            .add(TermQuery::create("condition", "refurbished"),
                 BooleanClause::MUST_NOT)
            .build();

        auto results = searcher.search(query.get(), 20);

        std::cout << "Found " << results.totalHits << " laptops:\n\n";

        for (const auto& hit : results.scoreDocs) {
            auto doc = searcher.doc(hit.doc);
            std::cout << doc->get("title")
                     << " - $" << doc->getNumeric("price")
                     << " (score: " << hit.score << ")\n";
        }

        reader->close();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
```

## Phrase Query Search

```cpp
#include <diagon/search/IndexSearcher.h>
#include <diagon/index/DirectoryReader.h>
#include <diagon/store/FSDirectory.h>
#include <iostream>

using namespace diagon;

int main() {
    try {
        auto dir = store::FSDirectory::open("/tmp/my_index");
        auto reader = index::DirectoryReader::open(dir.get());
        search::IndexSearcher searcher(reader.get());

        // Exact phrase match
        auto exactQuery = search::PhraseQuery::create("content",
            {"search", "engine"});

        std::cout << "=== Exact phrase: 'search engine' ===\n";
        auto results = searcher.search(exactQuery.get(), 5);

        for (const auto& hit : results.scoreDocs) {
            auto doc = searcher.doc(hit.doc);
            std::cout << doc->get("title") << "\n";
        }

        // With slop (proximity)
        auto proximityQuery = search::PhraseQuery::create("content",
            {"search", "engine"}, 2);  // Allow 2 words between

        std::cout << "\n=== Proximity: 'search ... engine' (slop=2) ===\n";
        results = searcher.search(proximityQuery.get(), 5);

        for (const auto& hit : results.scoreDocs) {
            auto doc = searcher.doc(hit.doc);
            std::cout << doc->get("title") << "\n";
        }

        reader->close();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
```

## Pagination Example

```cpp
#include <diagon/search/IndexSearcher.h>
#include <diagon/index/DirectoryReader.h>
#include <diagon/store/FSDirectory.h>
#include <iostream>
#include <string>

using namespace diagon;

void displayPage(search::IndexSearcher& searcher,
                const search::TopDocs& results,
                int page_num)
{
    std::cout << "\n=== Page " << page_num << " ===\n";
    std::cout << "Total results: " << results.totalHits << "\n\n";

    for (size_t i = 0; i < results.scoreDocs.size(); i++) {
        const auto& hit = results.scoreDocs[i];
        auto doc = searcher.doc(hit.doc);

        std::cout << ((page_num - 1) * 10 + i + 1) << ". "
                 << doc->get("title")
                 << " (score: " << hit.score << ")\n";
    }
}

int main() {
    try {
        auto dir = store::FSDirectory::open("/tmp/my_index");
        auto reader = index::DirectoryReader::open(dir.get());
        search::IndexSearcher searcher(reader.get());

        auto query = search::TermQuery::create("category", "books");

        // Page 1
        auto results = searcher.search(query.get(), 10);
        displayPage(searcher, results, 1);

        // Page 2 using searchAfter
        if (!results.scoreDocs.empty()) {
            std::cout << "\nPress Enter for next page...";
            std::cin.get();

            auto lastDoc = results.scoreDocs.back();
            results = searcher.searchAfter(lastDoc, query.get(), 10);
            displayPage(searcher, results, 2);
        }

        // Page 3
        if (!results.scoreDocs.empty()) {
            std::cout << "\nPress Enter for next page...";
            std::cin.get();

            auto lastDoc = results.scoreDocs.back();
            results = searcher.searchAfter(lastDoc, query.get(), 10);
            displayPage(searcher, results, 3);
        }

        reader->close();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
```

## Search with Scoring Explanation

```cpp
#include <diagon/search/IndexSearcher.h>
#include <diagon/index/DirectoryReader.h>
#include <diagon/store/FSDirectory.h>
#include <iostream>

using namespace diagon;

int main() {
    try {
        auto dir = store::FSDirectory::open("/tmp/my_index");
        auto reader = index::DirectoryReader::open(dir.get());
        search::IndexSearcher searcher(reader.get());

        // Create query
        auto query = search::TermQuery::create("title", "search");

        // Search
        auto results = searcher.search(query.get(), 5);

        std::cout << "Top 5 results with score explanations:\n\n";

        for (const auto& hit : results.scoreDocs) {
            auto doc = searcher.doc(hit.doc);

            std::cout << "=== " << doc->get("title") << " ===\n";
            std::cout << "Score: " << hit.score << "\n";

            // Get explanation
            auto explanation = searcher.explain(query.get(), hit.doc);
            std::cout << explanation->toString() << "\n\n";
        }

        reader->close();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
```

## Custom BM25 Parameters

```cpp
#include <diagon/search/IndexSearcher.h>
#include <diagon/index/DirectoryReader.h>
#include <diagon/store/FSDirectory.h>
#include <iostream>

using namespace diagon;

void searchWithBM25(search::IndexSearcher& searcher,
                   search::Query* query,
                   float k1, float b,
                   const std::string& label)
{
    // Set custom BM25 similarity
    auto similarity = std::make_unique<search::BM25Similarity>(k1, b);
    searcher.setSimilarity(std::move(similarity));

    // Search
    auto results = searcher.search(query, 5);

    std::cout << "\n=== " << label << " (k1=" << k1 << ", b=" << b << ") ===\n";

    for (const auto& hit : results.scoreDocs) {
        auto doc = searcher.doc(hit.doc);
        std::cout << doc->get("title")
                 << " (score: " << hit.score << ")\n";
    }
}

int main() {
    try {
        auto dir = store::FSDirectory::open("/tmp/my_index");
        auto reader = index::DirectoryReader::open(dir.get());
        search::IndexSearcher searcher(reader.get());

        auto query = search::TermQuery::create("title", "search");

        // Default BM25
        searchWithBM25(searcher, query.get(), 1.2f, 0.75f, "Default BM25");

        // High k1 (term frequency matters more)
        searchWithBM25(searcher, query.get(), 2.0f, 0.75f, "High k1");

        // Low b (less length normalization)
        searchWithBM25(searcher, query.get(), 1.2f, 0.3f, "Low b");

        // No length normalization
        searchWithBM25(searcher, query.get(), 1.2f, 0.0f, "No length norm");

        reader->close();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
```

## Multi-Field Search

```cpp
#include <diagon/search/IndexSearcher.h>
#include <diagon/index/DirectoryReader.h>
#include <diagon/store/FSDirectory.h>
#include <iostream>

using namespace diagon;
using namespace search;

int main() {
    try {
        auto dir = store::FSDirectory::open("/tmp/my_index");
        auto reader = index::DirectoryReader::open(dir.get());
        IndexSearcher searcher(reader.get());

        // Search for "laptop" in multiple fields with different weights
        auto titleQuery = TermQuery::create("title", "laptop");
        titleQuery->setBoost(3.0f);  // Title matches are most important

        auto descQuery = TermQuery::create("description", "laptop");
        descQuery->setBoost(1.0f);  // Description matches are standard

        auto categoryQuery = TermQuery::create("category", "laptop");
        categoryQuery->setBoost(0.5f);  // Category matches are less important

        auto query = BooleanQuery::Builder()
            .add(std::move(titleQuery), BooleanClause::SHOULD)
            .add(std::move(descQuery), BooleanClause::SHOULD)
            .add(std::move(categoryQuery), BooleanClause::SHOULD)
            .setMinimumNumberShouldMatch(1)  // At least one must match
            .build();

        auto results = searcher.search(query.get(), 10);

        std::cout << "Found " << results.totalHits << " results:\n\n";

        for (const auto& hit : results.scoreDocs) {
            auto doc = searcher.doc(hit.doc);
            std::cout << doc->get("title")
                     << " (score: " << hit.score << ")\n";
        }

        reader->close();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
```

## Near-Real-Time Search

```cpp
#include <diagon/index/IndexWriter.h>
#include <diagon/search/IndexSearcher.h>
#include <diagon/index/DirectoryReader.h>
#include <diagon/store/FSDirectory.h>
#include <iostream>
#include <thread>
#include <chrono>

using namespace diagon;

void indexWorker(index::IndexWriter* writer) {
    for (int i = 0; i < 100; i++) {
        index::Document doc;
        doc.addField("id", i, index::FieldType::NUMERIC);
        doc.addField("title", "Document " + std::to_string(i),
                    index::FieldType::TEXT);

        writer->addDocument(doc);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

int main() {
    try {
        auto dir = store::FSDirectory::open("/tmp/nrt_index");

        index::IndexWriterConfig config;
        config.setRAMBufferSizeMB(64);
        config.setOpenMode(OpenMode::CREATE);

        auto writer = index::IndexWriter::create(dir.get(), config);

        // Open NRT reader
        auto reader = index::DirectoryReader::open(writer.get());

        // Start indexing in background
        std::thread indexThread(indexWorker, writer.get());

        // Periodically search and see new documents
        for (int round = 0; round < 10; round++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            // Reopen reader to see new documents
            auto newReader = index::DirectoryReader::openIfChanged(reader.get());
            if (newReader) {
                reader->close();
                reader = std::move(newReader);
            }

            // Search
            search::IndexSearcher searcher(reader.get());
            auto query = search::MatchAllQuery::create();
            auto results = searcher.search(query.get(), 1);

            std::cout << "Round " << (round + 1)
                     << ": Found " << results.totalHits
                     << " documents\n";
        }

        // Wait for indexing to complete
        indexThread.join();

        // Final commit
        writer->commit();

        reader->close();
        writer->close();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
```

## See Also

- [Searching Guide](../guides/searching.md)
- [Hybrid Query Examples](hybrid-queries.md)
- [Core API Reference](../reference/core.md)
