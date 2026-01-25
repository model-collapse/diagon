# API Reference

Complete reference documentation for Diagon's APIs.

## Core APIs

- **[Core API Reference](core.md)** - IndexWriter, IndexReader, DirectoryReader, IndexSearcher
  - Creating and configuring indexes
  - Adding, updating, and deleting documents
  - Searching and retrieving results
  - Index lifecycle management

## Field Types

- **[Field Types Reference](field-types.md)** - Complete guide to supported field types
  - TextField - Full-text searchable fields
  - StringField - Exact-match keyword fields
  - NumericDocValuesField - Numeric values for filtering and sorting
  - ArrayTextField, ArrayStringField, ArrayNumericField - Multi-valued fields
  - FieldType configuration options
  - Field selection guide

## Compression

- **[Compression API Reference](compression.md)** - Compression codecs
  - LZ4 codec - Fast compression for hot data
  - ZSTD codec - High compression for cold data
  - Custom codec implementation
  - Compression strategies

## SIMD Optimization

- **[SIMD API Reference](simd.md)** - AVX2 accelerated operations
  - SIMD-optimized scoring (BM25)
  - Vector operations
  - Performance characteristics
  - CPU feature detection

## See Also

- [Indexing Guide](../guides/indexing.md) - How to index documents
- [Searching Guide](../guides/searching.md) - How to execute queries
- [Performance Guide](../guides/performance.md) - Optimization techniques
- [Examples](../examples/) - Code examples
