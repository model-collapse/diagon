# Diagon Columns Library

**Module**: Column-oriented storage system
**Based on**: ClickHouse MergeTree engine
**Design References**: Modules 03, 05, 06, 11

## Overview

The columns library implements ClickHouse-style columnar storage with granule-based indexing, supporting both Wide and Compact data part formats. This enables efficient analytical queries with skip indexes and adaptive granularity.

## Module Structure

### IColumn Interface (Module 03)
**Copy-on-write columnar data structures**

- `IColumn`: Abstract column interface with COW semantics
- `ColumnVector<T>`: Contiguous array for numeric types
- `ColumnString`: Variable-length strings with offsets array
- `ColumnArray`: Nested arrays
- `ColumnNullable`: Nullable wrapper column
- `ColumnTuple`: Multi-field tuples
- `ColumnConst`: Constant value column

**COW Semantics**:
```cpp
// Shallow copy (reference counting)
auto col2 = col1->clone();

// Mutate triggers copy if refcount > 1
auto mutable_col = col->assumeMutable();
mutable_col->insert(value);
```

**Memory Management**:
- Reference counting for sharing
- Arena allocator integration for small objects
- Memory budgets to prevent OOM
- See design document 03_COLUMN_STORAGE.md for COW rules

### Type System
**Data type abstraction**

- `IDataType`: Type metadata and behavior
- `DataTypeNumber<T>`: Numeric types (Int32, Float64, etc.)
- `DataTypeString`: Variable-length text
- `DataTypeArray`: Array types
- `DataTypeNullable`: Nullable type wrapper

**Type Capabilities**:
- Serialization format selection
- Default value construction
- SQL type name
- Codec compatibility

### Serialization (ISerialization)
**Binary encoding for column data**

- `ISerialization`: Abstract serialization interface
- `SerializationNumber<T>`: Numeric value encoding
- `SerializationString`: String encoding (offsets + chars)
- `SerializationArray`: Nested array encoding
- `SerializationNullable`: Null bitmap + value encoding

**Design Pattern**: Type-specific serialization allows optimal encoding per column type (e.g., Delta encoding for timestamps, Gorilla for floats).

### MergeTree Data Parts (Module 05)
**Wide vs Compact storage formats**

#### IMergeTreeDataPart
- `MergeTreeDataPartWide`: Separate file per column
  - `column_name.bin`: Column data
  - `column_name.mrk2`: Mark file
  - Optimal for large parts (>10MB)

- `MergeTreeDataPartCompact`: Single data.bin file
  - All columns in one file with shared marks
  - Optimal for small parts (<10MB)

- `MergeTreeDataPartInMemory`: Fully in-memory part
  - No file I/O, used for recent data

**State Machine**:
```
Temporary → Committed → Outdated → Deleted
```

**Checksums**: `checksums.txt` with xxHash64 for integrity validation

#### Readers and Writers
- `MergeTreeReaderWide/Compact`: Read column data
- `MergeTreeDataPartWriterWide/Compact`: Write column data
- Granule-based I/O with mark files

### Granularity System (Module 06)
**Adaptive and constant granularity**

- `IMergeTreeIndexGranularity`: Granularity abstraction
- `MergeTreeIndexGranularityConstant`: Fixed 8192 rows per granule
- `MergeTreeIndexGranularityAdaptive`: Variable rows based on size target

**MarkInCompressedFile**:
```cpp
struct MarkInCompressedFile {
    uint64_t offset_in_compressed_file;      // File position
    uint64_t offset_in_decompressed_block;   // Block position
};
```

**Two-Level Addressing**:
1. Mark → compressed block offset
2. Within block → decompressed offset

**Adaptive Granularity**:
- Target: ~512KB uncompressed per granule
- Adjusts row count to hit size target
- Fewer marks for wide rows, more for narrow rows

### Skip Indexes (Module 11)
**Granule-level pruning**

- `IMergeTreeIndex`: Abstract skip index
- `IMergeTreeIndexGranule`: Per-granule metadata
- `IMergeTreeIndexAggregator`: Build-time aggregation
- `IMergeTreeIndexCondition`: Read-time filtering

**Implementations**:
1. **MinMax**: Track min/max values per granule
   - Efficient for range queries
   - Low overhead (~16 bytes per granule)

2. **Set**: Track unique values per granule
   - Efficient for equality checks
   - Configurable max_size

3. **BloomFilter**: Probabilistic membership test
   - Efficient for IN queries
   - False positive rate ~1%

**Integration**: Filters (Module 07a) use skip indexes to prune 90%+ of granules for analytical queries.

## Implementation Status

### Completed
- [ ] IColumn interface
- [ ] ColumnVector, ColumnString implementations
- [ ] IDataType system
- [ ] ISerialization interfaces

### In Progress
- [ ] MergeTree data parts (Wide/Compact)
- [ ] Granularity system
- [ ] Skip indexes

### TODO
- [ ] Memory budgets and OOM prevention
- [ ] Arena allocator integration
- [ ] COW optimization (minimize copies)
- [ ] Adaptive granularity tuning

## Dependencies

### Internal
- `diagon_compression`: LZ4, ZSTD, Delta, Gorilla codecs

### External
- ZLIB: Checksums
- ZSTD, LZ4: Compression

## Column Storage Examples

### Creating Columns
```cpp
// Numeric column
auto col = ColumnVector<Int32>::create();
col->insertValue(42);
col->insertValue(100);

// String column
auto str_col = ColumnString::create();
str_col->insertData("hello", 5);
str_col->insertData("world", 5);

// Nullable column
auto nullable = ColumnNullable::create(std::move(col), ColumnUInt8::create());
nullable->insertDefault();  // NULL
```

### COW Operations
```cpp
// Share column
auto col2 = col->clone();  // Shallow copy (refcount++)

// Mutate (triggers copy if shared)
auto mut = col->assumeMutable();
mut->popBack();  // Safe mutation
```

### Reading with Granules
```cpp
// Open data part
auto part = MergeTreeDataPartWide::create(...);

// Read column with granule range
auto reader = MergeTreeReaderWide::create(part);
auto column = reader->readColumn("field", mark_range);
```

## Performance Considerations

### Memory Management
- COW reduces copies for read-only operations
- Arena allocator for temporary objects
- Memory budgets prevent OOM during large queries

### I/O Efficiency
- Granule-based reading: Only load needed granules
- Skip indexes: 90%+ granule pruning for analytical queries
- Compression: 3-10× reduction depending on data type

### Wide vs Compact Selection
- **Wide**: Use for large parts (>10MB), better parallelism
- **Compact**: Use for small parts (<10MB), fewer files

## Testing

### Unit Tests
- `ColumnVectorTest`: Insert, COW, mutations
- `ColumnStringTest`: Variable-length data
- `GranularityTest`: Constant and adaptive granularity
- `SkipIndexTest`: MinMax, Set, BloomFilter accuracy

### Integration Tests
- End-to-end column storage and retrieval
- Wide/Compact format compatibility
- Skip index effectiveness

### Benchmarks
- Column insert throughput
- Read performance with skip indexes
- Compression ratios by codec

## References

### Design Documents
- `design/03_COLUMN_STORAGE.md`: IColumn and COW design
- `design/05_MERGETREE_DATA_PARTS.md`: Wide/Compact formats
- `design/06_GRANULARITY_AND_MARKS.md`: Granule system
- `design/11_SKIP_INDEXES.md`: Skip index design

### ClickHouse Source Code
- `ClickHouse/src/Columns/IColumn.h`
- `ClickHouse/src/Storages/MergeTree/IMergeTreeDataPart.h`
- `ClickHouse/src/Storages/MergeTree/MergeTreeIndexGranularity.h`

---

**Last Updated**: 2026-01-24
**Status**: Initial structure created, implementation in progress
