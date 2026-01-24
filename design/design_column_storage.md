# Column Storage - Low-Level Design

## Overview

Column storage provides ClickHouse-style columnar data organization for analytical queries. Key features:
- **Type-level partitioning**: Separate physical columns per data type
- **Granule-based organization**: 8K-16K rows per granule
- **Sparse indexing**: Primary key index on granule boundaries
- **Type-specific compression**: Optimized codecs per type

## Architecture

```cpp
class ColumnStorage {
public:
    explicit ColumnStorage(const std::string& field_name);

    // Write operations (during indexing)
    void add_value(uint32_t doc_id, const FieldValue& value);
    void add_null(uint32_t doc_id);
    void flush_to_disk(const std::string& base_path);

    // Read operations (during querying)
    std::unique_ptr<ColumnReader> create_reader() const;
    std::unique_ptr<ColumnScanner> create_scanner(const ScanFilter& filter) const;

    // Metadata
    size_t num_rows() const { return num_rows_; }
    size_t num_granules() const { return granules_.size(); }
    size_t memory_bytes() const;

private:
    std::string field_name_;
    uint32_t num_rows_{0};

    // Type partitions
    std::unordered_map<FieldType, TypePartition> type_partitions_;

    // Granules metadata
    std::vector<GranuleInfo> granules_;

    // Null tracking
    std::unique_ptr<Bitmap> null_bitmap_;
};
```

## Type Partitions

### Type Partition Structure

```cpp
// Separate storage for each data type
class TypePartition {
public:
    TypePartition(FieldType type, uint32_t granule_size);

    // Add value to current granule
    void add_value(uint32_t row_id, const FieldValue& value);

    // Finalize and flush
    void flush_to_disk(const std::string& output_dir);

    // Read access
    std::unique_ptr<TypePartitionReader> create_reader() const;

private:
    FieldType type_;
    uint32_t granule_size_{8192};

    // Current granule buffer
    std::unique_ptr<GranuleBuffer> current_granule_;
    std::vector<GranuleMetadata> granule_metadata_;

    // Compression
    std::unique_ptr<Compressor> compressor_;
};

// Type-specific implementations
template<typename T>
class TypedPartition : public TypePartition {
public:
    void add_value(uint32_t row_id, T value) {
        current_granule_.push_back(value);
        if (current_granule_.size() >= granule_size_) {
            flush_granule();
        }
    }

private:
    std::vector<T> current_granule_;
    std::vector<GranuleMetadata> granule_metadata_;
};
```

### Supported Types

```cpp
enum class FieldType {
    // Numeric types
    INT8, INT16, INT32, INT64,
    UINT8, UINT16, UINT32, UINT64,
    FLOAT32, FLOAT64,

    // String types
    STRING,          // Variable-length strings
    FIXED_STRING,    // Fixed-length strings
    LOW_CARD_STRING, // Dictionary-encoded strings

    // Binary
    BYTES,

    // Composite types
    ARRAY,           // Array of any type
    NESTED,          // Nested structure

    // Special
    NULL_TYPE        // Only nulls
};

// Type partition paths
// field_name.int32/
// field_name.string/
// field_name.array.string/
// field_name.nested.score.float64/
```

## Granule Organization

### Granule Metadata

```cpp
struct GranuleInfo {
    uint32_t granule_id;
    uint32_t row_start;       // First row in this granule
    uint32_t row_count;       // Number of rows

    // Statistics (for pruning)
    FieldValue min_value;     // Minimum value in granule
    FieldValue max_value;     // Maximum value in granule
    bool has_nulls;           // Contains null values

    // Type distribution (for dynamic fields)
    std::unordered_map<FieldType, uint32_t> type_counts;
};

struct GranuleMetadata {
    // File location
    uint64_t data_file_offset;        // Offset in data.bin
    uint32_t compressed_size;          // Compressed bytes
    uint32_t uncompressed_size;        // Uncompressed bytes

    // Index entry
    FieldValue first_value;            // First value (for sparse index)

    // Statistics
    uint32_t row_count;
    FieldValue min_value;
    FieldValue max_value;
};
```

### Granule File Layout

```
field_name.int32/
├── data.bin              # Compressed granule data
├── marks.idx             # Granule marks (offsets)
├── primary.idx           # Sparse primary index
└── metadata.json         # Column metadata

data.bin format:
[Compressed Granule 0] [Compressed Granule 1] [Compressed Granule 2] ...

marks.idx format:
[Mark 0] [Mark 1] [Mark 2] ...

Each mark:
  - data_file_offset: uint64_t
  - compressed_size: uint32_t
  - uncompressed_size: uint32_t
  - row_count: uint32_t

primary.idx format:
[Index Entry 0] [Index Entry 1] ...

Each entry:
  - first_value: serialized FieldValue
  - min_value: serialized FieldValue
  - max_value: serialized FieldValue
```

## Compression

### Type-Specific Compressors

```cpp
class Compressor {
public:
    virtual ~Compressor() = default;

    virtual std::vector<uint8_t> compress(
        const void* data, size_t size) const = 0;

    virtual std::vector<uint8_t> decompress(
        const void* data, size_t compressed_size,
        size_t uncompressed_size) const = 0;
};

// Integer compression
class IntegerCompressor : public Compressor {
public:
    std::vector<uint8_t> compress(const void* data, size_t size) const override {
        // 1. Delta encoding
        // 2. Frame of Reference (FOR)
        // 3. Bit packing
        // 4. LZ4 final pass
    }
};

// Float compression
class FloatCompressor : public Compressor {
public:
    std::vector<uint8_t> compress(const void* data, size_t size) const override {
        // 1. Gorilla compression for time-series
        // 2. XOR encoding for similar values
        // 3. ZSTD for generic
    }
};

// String compression
class StringCompressor : public Compressor {
public:
    std::vector<uint8_t> compress(const void* data, size_t size) const override {
        // For low-cardinality strings:
        //   1. Build dictionary
        //   2. Encode as integer IDs
        //   3. Compress dictionary with ZSTD
        //   4. Compress IDs with integer compressor

        // For high-cardinality:
        //   1. ZSTD with dictionary training
    }
};
```

### Compression Configuration

```cpp
struct CompressionConfig {
    CompressionType type;
    int level;                  // Compression level (1-9 for ZSTD)
    size_t min_compress_size;   // Don't compress if smaller than this

    // Type-specific settings
    struct {
        bool use_delta_encoding = true;
        bool use_for_encoding = true;
        bool use_bit_packing = true;
    } integer_opts;

    struct {
        bool use_dictionary = true;
        size_t max_dict_size = 65536;
        float dict_threshold = 0.5;  // Use dict if cardinality < threshold * rows
    } string_opts;
};

enum class CompressionType {
    NONE,
    LZ4,       // Fast decompression
    ZSTD,      // High ratio
    SNAPPY,    // Balanced
    CUSTOM     // Type-specific algorithms
};
```

## Reading

### Column Reader

```cpp
class ColumnReader {
public:
    // Read specific row
    FieldValue get_value(uint32_t row_id) const;

    // Read range
    std::vector<FieldValue> get_values(uint32_t start_row, uint32_t count) const;

    // Check null
    bool is_null(uint32_t row_id) const;

private:
    // Locate granule for row
    const GranuleMetadata& find_granule(uint32_t row_id) const {
        // Binary search in marks
        auto it = std::lower_bound(
            granule_marks_.begin(),
            granule_marks_.end(),
            row_id,
            [](const GranuleMark& mark, uint32_t row) {
                return mark.row_start + mark.row_count <= row;
            }
        );
        return *it;
    }

    // Load and decompress granule
    std::vector<FieldValue> load_granule(uint32_t granule_id) const;

    std::vector<GranuleMark> granule_marks_;
    std::unique_ptr<FileReader> data_file_;
    std::unique_ptr<Decompressor> decompressor_;

    // Cache for recently accessed granules
    mutable LRUCache<uint32_t, std::vector<FieldValue>> granule_cache_;
};
```

### Column Scanner (for analytical queries)

```cpp
class ColumnScanner {
public:
    explicit ColumnScanner(const ColumnReader* reader, const ScanFilter& filter);

    // Iterate over matching rows
    bool has_next() const;
    std::pair<uint32_t, FieldValue> next();  // (row_id, value)

    // Batch reading
    std::vector<std::pair<uint32_t, FieldValue>> next_batch(size_t max_size);

private:
    const ColumnReader* reader_;
    ScanFilter filter_;

    // Current granule
    uint32_t current_granule_id_{0};
    std::vector<FieldValue> current_granule_data_;
    uint32_t current_row_in_granule_{0};

    // Granule pruning
    bool should_scan_granule(const GranuleMetadata& meta) const {
        // Check min/max against filter
        if (filter_.type == FilterType::RANGE) {
            if (meta.max_value < filter_.min_value) return false;
            if (meta.min_value > filter_.max_value) return false;
        }
        return true;
    }
};
```

### Scan Filter

```cpp
struct ScanFilter {
    enum class FilterType {
        NONE,           // Scan all
        RANGE,          // value >= min && value <= max
        EQUALS,         // value == target
        IN_SET,         // value in {v1, v2, ...}
        NOT_NULL,       // value is not null
        CUSTOM          // User-defined predicate
    };

    FilterType type;
    FieldValue min_value;
    FieldValue max_value;
    FieldValue equals_value;
    std::unordered_set<FieldValue> in_set;
    std::function<bool(const FieldValue&)> predicate;
};
```

## Sparse Primary Index

### Index Structure

```cpp
class SparseIndex {
public:
    // Build index from granule first values
    void build(const std::vector<GranuleMetadata>& granules);

    // Find granules that may contain value
    std::vector<uint32_t> lookup_granules(const FieldValue& value) const;

    // Find granules in range
    std::vector<uint32_t> lookup_range(
        const FieldValue& min_value,
        const FieldValue& max_value) const;

private:
    struct IndexEntry {
        FieldValue first_value;   // First value in granule
        FieldValue min_value;     // Min value in granule
        FieldValue max_value;     // Max value in granule
        uint32_t granule_id;
    };

    std::vector<IndexEntry> entries_;  // Sorted by first_value

    // Binary search
    std::pair<uint32_t, uint32_t> search_range(
        const FieldValue& min_value,
        const FieldValue& max_value) const;
};
```

### Index Lookup Example

```
Query: SELECT * FROM table WHERE price >= 50 AND price <= 100

1. Load sparse index for "price" column
2. Binary search for granules:
   - Find first granule where max_value >= 50
   - Find last granule where min_value <= 100
3. Result: Granules [5, 6, 7, 8] may contain matching rows
4. Skip granules [0-4] and [9+]
5. Scan only selected granules with exact filter
```

## Multi-Type Column (Dynamic Fields)

### Type Router

```cpp
class DynamicColumn {
public:
    void add_value(uint32_t row_id, const FieldValue& value) {
        FieldType type = value.type();

        // Route to appropriate partition
        auto& partition = type_partitions_[type];
        if (!partition) {
            partition = create_partition(type);
        }

        partition->add_value(row_id, value);
        type_map_[row_id] = type;
    }

    FieldValue get_value(uint32_t row_id) const {
        FieldType type = type_map_.at(row_id);
        return type_partitions_.at(type)->get_value(row_id);
    }

private:
    std::unordered_map<FieldType, std::unique_ptr<TypePartition>> type_partitions_;
    std::unordered_map<uint32_t, FieldType> type_map_;  // row_id -> type
};
```

### Physical Layout

```
dynamic_field/
├── dynamic_field.int32/
│   ├── data.bin
│   ├── marks.idx
│   └── primary.idx
├── dynamic_field.string/
│   ├── data.bin
│   ├── marks.idx
│   └── primary.idx
├── dynamic_field.float64/
│   └── ...
└── type_map.bin          # Maps row_id to type partition
```

## Array Columns

### Array Storage

```cpp
class ArrayColumn {
public:
    void add_array(uint32_t row_id, const std::vector<FieldValue>& array);

    std::vector<FieldValue> get_array(uint32_t row_id) const;

private:
    // Flatten arrays into single column
    std::unique_ptr<TypePartition> values_partition_;

    // Offset array: offsets_[row_id] = start index in values
    std::vector<uint32_t> offsets_;

    // offsets_ = [0, 3, 3, 7, 10, ...]
    //            row0: values[0:3] = [a, b, c]
    //            row1: values[3:3] = [] (empty)
    //            row2: values[3:7] = [d, e, f, g]
    //            row3: values[7:10] = [h, i, j]
};
```

### Physical Layout

```
array_field.array.string/
├── values.bin          # Flattened array elements
├── offsets.bin         # Array boundaries
├── marks.idx
└── primary.idx
```

## Nested Columns

### Nested Structure

```cpp
// Example: metadata.tags (array of structs)
// Each document has: metadata: { tags: [ {name: "foo", score: 0.9}, ... ] }

// Physical storage:
metadata.tags.name.string/
metadata.tags.score.float64/
metadata.tags.offsets.bin
```

## Memory Management

### Granule Cache

```cpp
class GranuleCache {
public:
    explicit GranuleCache(size_t capacity_bytes);

    // Get granule data (load if not cached)
    const std::vector<FieldValue>& get(
        uint32_t granule_id,
        std::function<std::vector<FieldValue>()> loader);

    // Eviction policy
    void evict_lru();

private:
    size_t capacity_bytes_;
    size_t current_bytes_{0};

    struct CacheEntry {
        std::vector<FieldValue> data;
        size_t bytes;
        uint64_t last_access_time;
    };

    std::unordered_map<uint32_t, CacheEntry> cache_;

    // LRU list
    std::list<uint32_t> lru_list_;
};
```

## Performance Optimizations

### 1. Granule Prefetching

```cpp
class PrefetchingScanner : public ColumnScanner {
private:
    void prefetch_next_granules() {
        // Async load next N granules
        for (size_t i = 0; i < PREFETCH_COUNT; i++) {
            uint32_t granule_id = current_granule_id_ + i + 1;
            if (granule_id < num_granules_) {
                async_load_granule(granule_id);
            }
        }
    }

    static constexpr size_t PREFETCH_COUNT = 2;
};
```

### 2. SIMD Filtering

```cpp
// Use SIMD for filtering int32 columns
std::vector<uint32_t> filter_int32_range_simd(
    const int32_t* data, size_t count,
    int32_t min_value, int32_t max_value) {

    std::vector<uint32_t> results;
    __m256i min_vec = _mm256_set1_epi32(min_value);
    __m256i max_vec = _mm256_set1_epi32(max_value);

    for (size_t i = 0; i < count; i += 8) {
        __m256i values = _mm256_loadu_si256((__m256i*)&data[i]);

        // values >= min_value && values <= max_value
        __m256i cmp_min = _mm256_cmpgt_epi32(values, min_vec);
        __m256i cmp_max = _mm256_cmpgt_epi32(max_vec, values);
        __m256i mask = _mm256_and_si256(cmp_min, cmp_max);

        int bitmask = _mm256_movemask_ps(_mm256_castsi256_ps(mask));
        // Extract matching indices
        // ...
    }

    return results;
}
```

### 3. Adaptive Granule Size

```cpp
struct AdaptiveGranuleConfig {
    uint32_t min_rows = 4096;
    uint32_t max_rows = 16384;
    size_t target_uncompressed_bytes = 1024 * 1024;  // 1MB

    uint32_t calculate_granule_size(FieldType type) const {
        size_t value_size = type_size(type);
        uint32_t rows = target_uncompressed_bytes / value_size;
        return std::clamp(rows, min_rows, max_rows);
    }
};
```

## Testing

```cpp
class ColumnStorageTest {
    void test_type_partitioning() {
        DynamicColumn col("dynamic_field");

        // Add mixed types
        col.add_value(0, FieldValue(42));          // int32
        col.add_value(1, FieldValue("hello"));     // string
        col.add_value(2, FieldValue(3.14));        // float64
        col.add_value(3, FieldValue(100));         // int32

        // Flush to disk
        col.flush_to_disk("/tmp/test");

        // Verify physical files
        ASSERT_TRUE(fs::exists("/tmp/test/dynamic_field.int32/data.bin"));
        ASSERT_TRUE(fs::exists("/tmp/test/dynamic_field.string/data.bin"));
        ASSERT_TRUE(fs::exists("/tmp/test/dynamic_field.float64/data.bin"));
    }

    void test_range_query_pruning() {
        // Build column with 10K rows, 100 values per granule
        // Query: value >= 5000 && value <= 5500
        // Expected: Skip first 50 granules, scan 5-6 granules
    }
};
```
