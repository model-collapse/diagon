# Basic Indexing Examples

Practical examples for indexing documents in Diagon.

## Simple Document Indexing

```cpp
#include <diagon/index/IndexWriter.h>
#include <diagon/store/FSDirectory.h>
#include <iostream>

using namespace diagon;

int main() {
    try {
        // Create directory
        auto dir = store::FSDirectory::open("/tmp/my_index");

        // Configure writer
        index::IndexWriterConfig config;
        config.setRAMBufferSizeMB(128);

        // Create writer
        auto writer = index::IndexWriter::create(dir.get(), config);

        // Create and add a document
        index::Document doc;
        doc.addField("title", "Introduction to C++", index::FieldType::TEXT);
        doc.addField("author", "Bjarne Stroustrup", index::FieldType::TEXT);
        doc.addField("year", 2013, index::FieldType::NUMERIC);
        doc.addField("price", 59.99, index::FieldType::NUMERIC);
        doc.addField("category", "programming", index::FieldType::KEYWORD);

        writer->addDocument(doc);

        // Commit changes
        writer->commit();

        std::cout << "Indexed " << writer->numDocs() << " documents\n";

        // Close writer
        writer->close();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
```

## Bulk Indexing from CSV

```cpp
#include <diagon/index/IndexWriter.h>
#include <diagon/store/FSDirectory.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <iostream>

using namespace diagon;

struct Product {
    std::string title;
    std::string description;
    double price;
    int stock;
    std::string category;
};

std::vector<Product> loadCSV(const std::string& filename) {
    std::vector<Product> products;
    std::ifstream file(filename);
    std::string line;

    // Skip header
    std::getline(file, line);

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        Product product;

        std::getline(iss, product.title, ',');
        std::getline(iss, product.description, ',');

        std::string price_str, stock_str;
        std::getline(iss, price_str, ',');
        std::getline(iss, stock_str, ',');
        std::getline(iss, product.category, ',');

        product.price = std::stod(price_str);
        product.stock = std::stoi(stock_str);

        products.push_back(product);
    }

    return products;
}

int main() {
    try {
        // Load products from CSV
        auto products = loadCSV("products.csv");
        std::cout << "Loaded " << products.size() << " products\n";

        // Create index
        auto dir = store::FSDirectory::open("/tmp/products_index");

        index::IndexWriterConfig config;
        config.setRAMBufferSizeMB(256);
        config.setMaxBufferedDocs(10000);

        auto writer = index::IndexWriter::create(dir.get(), config);

        // Index products in batches
        const size_t BATCH_SIZE = 1000;
        std::vector<index::Document> batch;
        batch.reserve(BATCH_SIZE);

        for (size_t i = 0; i < products.size(); i++) {
            const auto& product = products[i];

            index::Document doc;
            doc.addField("title", product.title, index::FieldType::TEXT);
            doc.addField("description", product.description, index::FieldType::TEXT);
            doc.addField("price", product.price, index::FieldType::NUMERIC);
            doc.addField("stock", product.stock, index::FieldType::NUMERIC);
            doc.addField("category", product.category, index::FieldType::KEYWORD);

            batch.push_back(std::move(doc));

            if (batch.size() >= BATCH_SIZE) {
                writer->addDocuments(batch);
                batch.clear();
                std::cout << "Indexed " << (i + 1) << " products\n";
            }
        }

        // Index remaining documents
        if (!batch.empty()) {
            writer->addDocuments(batch);
        }

        // Commit
        writer->commit();

        std::cout << "Indexing complete: " << writer->numDocs() << " documents\n";

        // Close writer
        writer->close();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
```

## Indexing JSON Documents

```cpp
#include <diagon/index/IndexWriter.h>
#include <diagon/store/FSDirectory.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

using namespace diagon;
using json = nlohmann::json;

int main() {
    try {
        // Load JSON data
        std::ifstream file("documents.json");
        json data = json::parse(file);

        // Create index
        auto dir = store::FSDirectory::open("/tmp/json_index");

        index::IndexWriterConfig config;
        config.setRAMBufferSizeMB(256);

        auto writer = index::IndexWriter::create(dir.get(), config);

        // Index documents
        for (const auto& item : data["documents"]) {
            index::Document doc;

            // Text fields
            doc.addField("title", item["title"].get<std::string>(),
                        index::FieldType::TEXT);
            doc.addField("content", item["content"].get<std::string>(),
                        index::FieldType::TEXT);

            // Numeric fields
            if (item.contains("timestamp")) {
                doc.addField("timestamp", item["timestamp"].get<int64_t>(),
                           index::FieldType::NUMERIC);
            }

            // Keyword fields
            if (item.contains("category")) {
                doc.addField("category", item["category"].get<std::string>(),
                           index::FieldType::KEYWORD);
            }

            // Multi-valued fields
            if (item.contains("tags")) {
                for (const auto& tag : item["tags"]) {
                    doc.addField("tags", tag.get<std::string>(),
                               index::FieldType::KEYWORD);
                }
            }

            writer->addDocument(doc);
        }

        writer->commit();

        std::cout << "Indexed " << writer->numDocs() << " documents\n";

        writer->close();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
```

## Incremental Indexing

```cpp
#include <diagon/index/IndexWriter.h>
#include <diagon/store/FSDirectory.h>
#include <iostream>
#include <thread>
#include <chrono>

using namespace diagon;

class IncrementalIndexer {
    std::unique_ptr<store::Directory> dir_;
    std::unique_ptr<index::IndexWriter> writer_;

public:
    IncrementalIndexer(const std::string& index_path) {
        dir_ = store::FSDirectory::open(index_path);

        index::IndexWriterConfig config;
        config.setRAMBufferSizeMB(256);
        config.setOpenMode(OpenMode::CREATE_OR_APPEND);

        writer_ = index::IndexWriter::create(dir_.get(), config);
    }

    void addDocument(const index::Document& doc) {
        writer_->addDocument(doc);
    }

    void commit() {
        std::cout << "Committing changes...\n";
        writer_->commit();
        std::cout << "Total documents: " << writer_->numDocs() << "\n";
    }

    void close() {
        writer_->close();
    }
};

int main() {
    try {
        IncrementalIndexer indexer("/tmp/incremental_index");

        // Simulate continuous indexing
        for (int round = 0; round < 10; round++) {
            std::cout << "\n=== Round " << (round + 1) << " ===\n";

            // Add 100 documents
            for (int i = 0; i < 100; i++) {
                index::Document doc;
                doc.addField("id", round * 100 + i, index::FieldType::NUMERIC);
                doc.addField("content",
                           "Document " + std::to_string(round * 100 + i),
                           index::FieldType::TEXT);
                doc.addField("round", round, index::FieldType::NUMERIC);

                indexer.addDocument(doc);
            }

            // Commit after each round
            indexer.commit();

            // Wait before next round (simulate continuous updates)
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }

        indexer.close();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
```

## Indexing with Error Handling

```cpp
#include <diagon/index/IndexWriter.h>
#include <diagon/store/FSDirectory.h>
#include <iostream>
#include <vector>

using namespace diagon;

struct IndexingStats {
    size_t successful = 0;
    size_t failed = 0;
    std::vector<std::string> errors;
};

IndexingStats indexDocumentsWithRetry(
    index::IndexWriter* writer,
    const std::vector<index::Document>& docs,
    int max_retries = 3)
{
    IndexingStats stats;

    for (const auto& doc : docs) {
        bool success = false;
        int retries = 0;

        while (!success && retries < max_retries) {
            try {
                writer->addDocument(doc);
                stats.successful++;
                success = true;
            } catch (const std::bad_alloc& e) {
                // Out of memory - commit and retry
                std::cerr << "OOM, committing and retrying...\n";
                writer->commit();
                retries++;
            } catch (const std::exception& e) {
                // Other error - log and skip
                stats.errors.push_back(e.what());
                stats.failed++;
                break;
            }
        }

        if (!success && retries >= max_retries) {
            stats.errors.push_back("Max retries exceeded");
            stats.failed++;
        }
    }

    return stats;
}

int main() {
    try {
        auto dir = store::FSDirectory::open("/tmp/robust_index");

        index::IndexWriterConfig config;
        config.setRAMBufferSizeMB(128);

        auto writer = index::IndexWriter::create(dir.get(), config);

        // Create test documents
        std::vector<index::Document> docs;
        for (int i = 0; i < 10000; i++) {
            index::Document doc;
            doc.addField("id", i, index::FieldType::NUMERIC);
            doc.addField("text", "Document " + std::to_string(i),
                        index::FieldType::TEXT);
            docs.push_back(std::move(doc));
        }

        // Index with error handling
        auto stats = indexDocumentsWithRetry(writer.get(), docs);

        // Commit
        writer->commit();

        // Report statistics
        std::cout << "\n=== Indexing Statistics ===\n";
        std::cout << "Successful: " << stats.successful << "\n";
        std::cout << "Failed: " << stats.failed << "\n";

        if (!stats.errors.empty()) {
            std::cout << "\nErrors:\n";
            for (const auto& error : stats.errors) {
                std::cout << "  - " << error << "\n";
            }
        }

        writer->close();

        return stats.failed == 0 ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
```

## Updating Documents

```cpp
#include <diagon/index/IndexWriter.h>
#include <diagon/search/IndexSearcher.h>
#include <diagon/index/DirectoryReader.h>
#include <diagon/store/FSDirectory.h>
#include <iostream>

using namespace diagon;

int main() {
    try {
        auto dir = store::FSDirectory::open("/tmp/update_index");

        // Create writer
        index::IndexWriterConfig config;
        config.setRAMBufferSizeMB(128);
        config.setOpenMode(OpenMode::CREATE);

        auto writer = index::IndexWriter::create(dir.get(), config);

        // Add initial document
        {
            index::Document doc;
            doc.addField("id", 1, index::FieldType::NUMERIC);
            doc.addField("title", "Old Title", index::FieldType::TEXT);
            doc.addField("price", 99.99, index::FieldType::NUMERIC);
            writer->addDocument(doc);
            writer->commit();
        }

        std::cout << "Initial document added\n";

        // Update document
        {
            auto query = search::TermQuery::create("id", "1");

            index::Document newDoc;
            newDoc.addField("id", 1, index::FieldType::NUMERIC);
            newDoc.addField("title", "New Title", index::FieldType::TEXT);
            newDoc.addField("price", 79.99, index::FieldType::NUMERIC);

            writer->updateDocument(query.get(), newDoc);
            writer->commit();
        }

        std::cout << "Document updated\n";

        // Verify update
        {
            auto reader = index::DirectoryReader::open(writer.get());
            search::IndexSearcher searcher(reader.get());

            auto query = search::TermQuery::create("id", "1");
            auto results = searcher.search(query.get(), 1);

            if (results.totalHits > 0) {
                auto doc = searcher.doc(results.scoreDocs[0].doc);
                std::cout << "Current title: " << doc->get("title") << "\n";
                std::cout << "Current price: $" << doc->getNumeric("price") << "\n";
            }

            reader->close();
        }

        writer->close();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
```

## See Also

- [Indexing Guide](../guides/indexing.md)
- [Text Search Examples](text-search.md)
- [Core API Reference](../reference/core.md)
