# Quick Start Guide

Get started with Diagon in just a few minutes. This guide covers installation, basic indexing, and searching.

## Prerequisites

- C++20 compatible compiler (GCC 11+, Clang 14+, or MSVC 2022+)
- CMake 3.20+
- Dependencies: ZLIB, LZ4, ZSTD (auto-installed via vcpkg/conan)

## Installation

### Option 1: Using vcpkg (Recommended)

```bash
# Install vcpkg
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg && ./bootstrap-vcpkg.sh

# Clone and build Diagon
git clone https://github.com/model-collapse/diagon.git
cd diagon

cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release

cmake --build build -j$(nproc)
```

### Option 2: Using System Packages

```bash
# Ubuntu/Debian
sudo apt-get install zlib1g-dev libzstd-dev liblz4-dev

# Build
git clone https://github.com/model-collapse/diagon.git
cd diagon
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

See [BUILD.md](../../BUILD.md) for detailed build instructions.

## Your First Index

Here's a complete example that creates an index, adds documents, and searches them:

```cpp
#include <diagon/index/IndexWriter.h>
#include <diagon/search/IndexSearcher.h>
#include <diagon/store/FSDirectory.h>
#include <iostream>

using namespace diagon;

int main() {
    try {
        // Step 1: Create directory
        auto dir = store::FSDirectory::open("/tmp/my_index");

        // Step 2: Configure and create writer
        index::IndexWriterConfig config;
        config.setRAMBufferSizeMB(256);  // 256MB buffer before flush
        auto writer = index::IndexWriter::create(dir.get(), config);

        // Step 3: Add documents
        {
            index::Document doc;
            doc.addField("title", "Introduction to Search Engines",
                        index::FieldType::TEXT);
            doc.addField("price", 29.99, index::FieldType::NUMERIC);
            doc.addField("category", "books", index::FieldType::KEYWORD);
            doc.addField("year", 2024, index::FieldType::NUMERIC);
            writer->addDocument(doc);
        }

        {
            index::Document doc;
            doc.addField("title", "Advanced Search Techniques",
                        index::FieldType::TEXT);
            doc.addField("price", 49.99, index::FieldType::NUMERIC);
            doc.addField("category", "books", index::FieldType::KEYWORD);
            doc.addField("year", 2025, index::FieldType::NUMERIC);
            writer->addDocument(doc);
        }

        {
            index::Document doc;
            doc.addField("title", "Search Engine Internals",
                        index::FieldType::TEXT);
            doc.addField("price", 39.99, index::FieldType::NUMERIC);
            doc.addField("category", "technical", index::FieldType::KEYWORD);
            doc.addField("year", 2024, index::FieldType::NUMERIC);
            writer->addDocument(doc);
        }

        // Step 4: Commit changes
        writer->commit();
        std::cout << "Indexed " << writer->numDocs() << " documents\n";

        // Step 5: Open reader and create searcher
        auto reader = index::DirectoryReader::open(dir.get());
        search::IndexSearcher searcher(reader.get());

        // Step 6: Search for documents
        auto query = search::TermQuery::create("title", "search");
        auto results = searcher.search(query.get(), 10);

        std::cout << "\nFound " << results.totalHits << " results:\n";
        for (const auto& hit : results.scoreDocs) {
            auto doc = searcher.doc(hit.doc);
            std::cout << "  Doc " << hit.doc
                     << " (score: " << hit.score << "): "
                     << doc->get("title") << "\n";
        }

        // Step 7: Search with filter
        auto filter = search::RangeFilter::create("price", 0.0, 35.0);
        results = searcher.search(query.get(), filter.get(), 10);

        std::cout << "\nFiltered results (price <= $35):\n";
        std::cout << "Found " << results.totalHits << " results:\n";
        for (const auto& hit : results.scoreDocs) {
            auto doc = searcher.doc(hit.doc);
            std::cout << "  " << doc->get("title")
                     << " ($" << doc->getNumeric("price") << ")\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
```

### Expected Output

```
Indexed 3 documents

Found 3 results:
  Doc 0 (score: 1.234): Introduction to Search Engines
  Doc 1 (score: 1.456): Advanced Search Techniques
  Doc 2 (score: 1.345): Search Engine Internals

Filtered results (price <= $35):
Found 2 results:
  Introduction to Search Engines ($29.99)
  Search Engine Internals ($39.99)
```

## What Just Happened?

1. **Created a directory**: `FSDirectory` provides access to the file system for index storage
2. **Configured IndexWriter**: Set memory buffer size before flushing to disk
3. **Added documents**: Each document has multiple fields with different types
4. **Committed changes**: Made documents searchable by flushing segments
5. **Opened reader**: Created read-only view of the index
6. **Executed queries**:
   - Text search on "title" field
   - Combined text search with numeric filter on "price"

## Field Types

Diagon supports three field types:

| Type | Description | Use Cases |
|------|-------------|-----------|
| `TEXT` | Full-text searchable | Titles, descriptions, content |
| `NUMERIC` | Numeric values (int, long, float, double) | Prices, ratings, counts, timestamps |
| `KEYWORD` | Exact-match strings | Categories, tags, IDs |

## Next Steps

- **[Indexing Guide](indexing.md)** - Learn about advanced indexing features
- **[Search Guide](searching.md)** - Explore query types and scoring
- **[Performance Guide](performance.md)** - Optimize for your use case
- **[API Documentation](../api/core.md)** - Detailed API reference

## Common Issues

### Build Failures

If you encounter build errors, check:
- Compiler version (GCC 11+, Clang 14+)
- CMake version (3.20+)
- All dependencies installed

See [BUILD.md](../../BUILD.md) troubleshooting section.

### Runtime Errors

**Directory not found**: Ensure parent directory exists
```cpp
// Create parent directory first
std::filesystem::create_directories("/tmp");
auto dir = FSDirectory::open("/tmp/my_index");
```

**Out of memory**: Reduce RAM buffer size
```cpp
config.setRAMBufferSizeMB(64);  // Smaller buffer
```

## Getting Help

- [GitHub Issues](https://github.com/model-collapse/diagon/issues)
- [GitHub Discussions](https://github.com/model-collapse/diagon/discussions)
- [Contributing Guide](../../CONTRIBUTING.md)
