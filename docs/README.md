# Diagon Documentation

Welcome to the Diagon Search Engine documentation. This guide will help you understand, build, and use Diagon in your projects.

## Documentation Structure

### User Guides
- **[Quick Start](guides/quick-start.md)** - Get started with Diagon in 5 minutes
- **[Indexing Guide](guides/indexing.md)** - Learn how to index documents
- **[Search Guide](guides/searching.md)** - Execute queries and filters
- **[Performance Guide](guides/performance.md)** - Optimize for speed and efficiency

### API Reference
- **[API Reference Index](reference/README.md)** - Complete API documentation
  - [Core APIs](reference/core.md) - IndexWriter, IndexReader, IndexSearcher
  - [Field Types](reference/field-types.md) - TextField, StringField, NumericDocValuesField, Array fields
  - [Compression](reference/compression.md) - LZ4, ZSTD codecs
  - [SIMD Optimization](reference/simd.md) - AVX2 accelerated scoring

### Examples
- **[Basic Indexing](examples/basic-indexing.md)** - Simple indexing examples
- **[Text Search](examples/text-search.md)** - Full-text search examples
- **[Hybrid Queries](examples/hybrid-queries.md)** - Combine text and filters

### Developer Documentation
- **[Architecture Overview](../design/00_ARCHITECTURE_OVERVIEW.md)** - System design
- **[Build Instructions](../BUILD.md)** - Building from source
- **[Contributing Guide](../CONTRIBUTING.md)** - How to contribute
- **[Design Documents](../design/README.md)** - Detailed design specs

## Quick Links

- [GitHub Repository](https://github.com/model-collapse/diagon)
- [Issue Tracker](https://github.com/model-collapse/diagon/issues)
- [Discussions](https://github.com/model-collapse/diagon/discussions)

## Getting Help

- **Questions**: Use [GitHub Discussions](https://github.com/model-collapse/diagon/discussions)
- **Bug Reports**: File an [issue](https://github.com/model-collapse/diagon/issues)
- **Feature Requests**: Start a [discussion](https://github.com/model-collapse/diagon/discussions)

## License

Diagon is licensed under the Apache License 2.0. See [LICENSE](../LICENSE) for details.
