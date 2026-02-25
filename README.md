<div align="center">
  <img src="https://raw.githubusercontent.com/model-collapse/diagon/main/icon.png" alt="Diagon Logo" width="200"/>
  <h1>Diagon</h1>
  <p><em>A high-performance C++ search engine library</em></p>
</div>

<div align="center">

[![CI](https://github.com/model-collapse/diagon/workflows/CI/badge.svg)](https://github.com/model-collapse/diagon/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

</div>

**DIAGON** (**D**iverse **I**ndex **A**rchitecture for **G**ranular **O**LAP **a**nd **N**atural language search) is a C++ search library combining Apache Lucene's inverted index with ClickHouse's columnar storage. Think of it as **Apache Lucene for C++** — low-level primitives for building search engines, not a ready-to-use search server.

## Benchmark: Diagon vs Apache Lucene 11

Reuters-21578 dataset, AWS c7i (64 vCPU @ 2.6 GHz). Lucene: JDK 25, `-Xmx4g`, G1GC. Diagon: `-O3 -march=native`, MMapDirectory.

**Query Latency (P99):**

| Query | Diagon | Lucene | Speedup |
|-------|--------|--------|---------|
| Single-term | 44 us | 790 us | **18x** |
| OR-2 | 82 us | 800 us | **9.8x** |
| OR-5 | 167 us | 970 us | **5.8x** |
| OR-10 | 248 us | 1,080 us | **4.4x** |
| OR-20 | 366 us | 840 us | **2.3x** |
| OR-50 | 1,056 us | 1,410 us | **1.3x** |
| AND-2 | 65 us | 220 us | **3.4x** |

**Query Latency (P50):**

| Query | Diagon | Lucene | Speedup |
|-------|--------|--------|---------|
| Single-term | 27 us | 300 us | **11.1x** |
| OR-2 | 64 us | 260 us | **4.1x** |
| OR-5 | 131 us | 540 us | **4.1x** |
| OR-10 | 234 us | 310 us | **1.3x** |
| OR-50 | 1,026 us | 1,160 us | **1.1x** |

**Indexing:** Diagon 10,417 docs/sec, Lucene 24,520 docs/sec. Index size: 11 MB vs 10.5 MB.

Full report: [`benchmark_results/reuters_lucene_20260217_053200.md`](benchmark_results/reuters_lucene_20260217_053200.md)

## Get Started

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt-get install -y build-essential cmake libicu-dev libz-dev liblz4-dev libzstd-dev
```

Requires GCC 11+ or Clang 14+, CMake 3.20+, C++20.

### Build

```bash
git clone https://github.com/model-collapse/diagon.git
cd diagon
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS="-O3 -march=native" \
      -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
      -DDIAGON_BUILD_BENCHMARKS=ON \
      -DDIAGON_BUILD_TESTS=ON ..
make -j$(nproc)
```

### Index Documents

```cpp
#include <diagon/index/IndexWriter.h>
#include <diagon/document/Document.h>
#include <diagon/document/Field.h>
#include <diagon/store/FSDirectory.h>

using namespace diagon;

// Open directory and create writer
auto dir = store::FSDirectory::open("/tmp/my_index");
index::IndexWriterConfig config;
config.setRAMBufferSizeMB(256);
auto writer = index::IndexWriter::create(dir.get(), config);

// Add a document
document::Document doc;
doc.addField(std::make_unique<document::TextField>(
    "title", "Building Search Applications with Diagon", true));
doc.addField(std::make_unique<document::StringField>(
    "category", "software", true));
doc.addField(std::make_unique<document::NumericDocValuesField>(
    "price", 9999));

writer->addDocument(doc);
writer->commit();
```

### Search

```cpp
#include <diagon/index/DirectoryReader.h>
#include <diagon/search/IndexSearcher.h>
#include <diagon/search/TermQuery.h>
#include <diagon/store/MMapDirectory.h>

using namespace diagon;

// Use MMapDirectory for fast reads (zero-copy via mmap)
auto dir = store::MMapDirectory::open("/tmp/my_index");
auto reader = index::DirectoryReader::open(dir.get());
search::IndexSearcher searcher(reader.get());

// Single-term query
auto query = search::TermQuery::create("title", "search");
auto results = searcher.search(query.get(), 10);

for (const auto& hit : results.scoreDocs) {
    std::cout << "Doc " << hit.doc << ", score " << hit.score << "\n";
}
```

### Boolean & Phrase Queries

```cpp
// Boolean OR (uses WAND for early termination)
auto q1 = search::TermQuery::create("title", "search");
auto q2 = search::TermQuery::create("title", "engine");
auto boolQuery = search::BooleanQuery::builder()
    .add(q1.get(), search::BooleanClause::Occur::SHOULD)
    .add(q2.get(), search::BooleanClause::Occur::SHOULD)
    .build();

// Phrase query: "search engine" (exact adjacency)
auto phraseQuery = search::PhraseQuery::builder("title")
    .add("search")
    .add("engine")
    .build();

// Numeric range filter
auto rangeQuery = search::NumericRangeQuery::create("price", 0, 10000);
```

### Run Tests & Benchmarks

```bash
cd build
ctest --output-on-failure           # Run tests
./benchmarks/ReutersBenchmark       # Reuters-21578 benchmark
./benchmarks/ReutersWANDBenchmark   # WAND multi-term benchmark
```

## Key Features

- **BM25 scoring** with SIMD acceleration (AVX2 / ARM NEON)
- **WAND (Block-Max)** early termination for top-K retrieval
- **Phrase queries** with position-aware matching
- **FST term dictionary** + StreamVByte postings compression
- **MMapDirectory** for zero-copy reads via OS page cache
- **Text analysis**: 8 analyzers, 6 tokenizers, Chinese (Jieba) support
- **Columnar storage** with granule-based indexing (ClickHouse-style)

## Documentation

- [`BUILD_SOP.md`](BUILD_SOP.md) — Build troubleshooting and details
- [`design/`](design/README.md) — 15 architecture design modules
- [`docs/guides/`](docs/guides/) — Quick start, indexing, searching, performance
- [`docs/reference/`](docs/reference/) — API reference (core, SIMD, compression, field types)
- [`docs/examples/`](docs/examples/) — Code examples

## License

Apache License 2.0 — See [LICENSE](LICENSE).

## Acknowledgments

Built upon Apache Lucene (inverted index design), ClickHouse (columnar storage), and the SINDI paper (SIMD acceleration).
