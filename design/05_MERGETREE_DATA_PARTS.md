# MergeTree Data Parts Design
## Based on ClickHouse MergeTree Storage Engine

Source references:
- `ClickHouse/src/Storages/MergeTree/IMergeTreeDataPart.h`
- `ClickHouse/src/Storages/MergeTree/MergeTreeDataPartWide.h`
- `ClickHouse/src/Storages/MergeTree/MergeTreeDataPartCompact.h`
- `ClickHouse/src/Storages/MergeTree/MergeTreeDataPartWriterOnDisk.h`
- `ClickHouse/src/Storages/MergeTree/MergeTreeReaderWide.h`
- `ClickHouse/src/Storages/MergeTree/MergeTreeDataPartType.h`

## Overview

MergeTree storage engine organizes data into immutable parts (equivalent to Lucene segments).

**Key concepts:**
- **Data Part**: Immutable unit of storage (like Lucene segment)
- **Wide Format**: Separate file per column + marks (for large parts)
- **Compact Format**: Single data.bin with all columns (for small parts)
- **Granules**: 8192-row chunks within parts
- **Marks**: File offsets for random access to granules
- **Primary Index**: Sparse index on granule boundaries

## Data Part Type

```cpp
/**
 * Data part storage format
 *
 * Based on: ClickHouse MergeTreeDataPartType
 */
enum class DataPartType : uint8_t {
    /**
     * Wide format: Each column in separate file
     * Files: column.type/data.bin, column.type/marks.mrk2
     * Used for: parts > min_bytes_for_wide_part (default 10MB)
     */
    Wide,

    /**
     * Compact format: All columns in single data.bin
     * Files: data.bin, marks.mrk3
     * Used for: small parts < 10MB
     * Benefit: Fewer files, better for filesystem metadata
     */
    Compact,

    /**
     * InMemory format: Fully in-memory (no files)
     * Used for: hot data, temporary parts
     */
    InMemory,

    /**
     * Unknown: Used for error handling
     */
    Unknown
};

/**
 * Determine part type based on size
 */
DataPartType selectPartType(uint64_t rows_count, uint64_t bytes_size) {
    // Thresholds (from ClickHouse settings)
    constexpr uint64_t min_rows_for_wide_part = 0;  // Disabled by default
    constexpr uint64_t min_bytes_for_wide_part = 10 * 1024 * 1024;  // 10MB

    if (bytes_size >= min_bytes_for_wide_part ||
        (min_rows_for_wide_part > 0 && rows_count >= min_rows_for_wide_part)) {
        return DataPartType::Wide;
    }

    return DataPartType::Compact;
}
```

## IMergeTreeDataPart (Base Class)

```cpp
/**
 * Base class for data parts
 *
 * A data part is an immutable chunk of data with:
 * - Column data (compressed granules)
 * - Marks (offsets into compressed data)
 * - Primary index (sparse index on granules)
 * - Checksums (file integrity)
 *
 * Based on: ClickHouse IMergeTreeDataPart
 */
class IMergeTreeDataPart {
public:
    using Type = DataPartType;
    using Index = std::vector<ColumnPtr>;  // Primary key columns
    using Checksums = std::unordered_map<std::string, Checksum>;

    virtual ~IMergeTreeDataPart() = default;

    // ==================== Type & Identification ====================

    /**
     * Get part type (Wide/Compact/InMemory)
     */
    virtual Type getType() const = 0;

    /**
     * Get part name (e.g., "20230101_0_0_0")
     * Format: MinDate_MinBlockNum_MaxBlockNum_Level
     */
    const std::string& name() const { return name_; }

    /**
     * Get storage path
     */
    const std::filesystem::path& getFullPath() const { return full_path_; }

    // ==================== Statistics ====================

    /**
     * Number of rows in this part
     */
    uint64_t rows_count() const { return rows_count_; }

    /**
     * Bytes on disk (compressed)
     */
    uint64_t bytes_on_disk() const { return bytes_on_disk_; }

    /**
     * Primary index size in bytes
     */
    uint64_t primary_index_size() const {
        return primary_index_ ? primary_index_->size() * sizeof(void*) : 0;
    }

    // ==================== State Management ====================

    /**
     * Part state machine
     */
    enum class State : uint8_t {
        Temporary,        // Being written, not visible
        PreCommitted,     // Written but not yet committed
        Committed,        // Active and queryable
        Outdated,         // Replaced by merge, to be deleted
        Deleting,         // Being deleted
        DeleteOnDestroy   // Delete when destructor called
    };

    State getState() const { return state_; }
    void setState(State state) { state_ = state; }

    bool isActive() const { return state_ == State::Committed; }

    // ==================== Index Structures ====================

    /**
     * Get primary index
     * Vector of columns from primary key, one row per granule
     */
    const Index& getIndex() const { return *primary_index_; }

    /**
     * Load primary index from disk
     */
    void loadIndex();

    /**
     * Get index granularity
     */
    const IMergeTreeIndexGranularity& getIndexGranularity() const {
        return *index_granularity_;
    }

    /**
     * Load index granularity from disk
     */
    void loadIndexGranularity();

    // ==================== Column Access ====================

    /**
     * Check if column exists
     */
    bool hasColumn(const std::string& column_name) const {
        return columns_.find(column_name) != columns_.end();
    }

    /**
     * Get column names
     */
    std::vector<std::string> getColumnNames() const {
        std::vector<std::string> names;
        for (const auto& [name, _] : columns_) {
            names.push_back(name);
        }
        return names;
    }

    /**
     * Get column size (bytes on disk)
     */
    uint64_t getColumnSize(const std::string& column_name) const {
        auto it = column_sizes_.find(column_name);
        return it != column_sizes_.end() ? it->second : 0;
    }

    // ==================== File Management ====================

    /**
     * Get checksums for all files
     */
    const Checksums& getChecksums() const { return checksums_; }

    /**
     * Load checksums from checksums.txt
     */
    void loadChecksums();

    /**
     * Check data integrity
     */
    void checkIntegrity();

    // ==================== Creation ====================

    /**
     * Create data part from type
     */
    static std::unique_ptr<IMergeTreeDataPart> create(
        Type type,
        const std::string& name,
        const std::filesystem::path& path);

protected:
    // Metadata
    std::string name_;
    std::filesystem::path full_path_;
    State state_{State::Temporary};

    // Statistics
    uint64_t rows_count_{0};
    uint64_t bytes_on_disk_{0};

    // Index structures
    std::shared_ptr<Index> primary_index_;
    std::shared_ptr<IMergeTreeIndexGranularity> index_granularity_;

    // Column metadata
    std::unordered_map<std::string, ColumnInfo> columns_;
    std::unordered_map<std::string, uint64_t> column_sizes_;

    // Checksums
    Checksums checksums_;
};

struct Checksum {
    uint64_t file_size;
    uint128_t checksum;  // CityHash128

    bool operator==(const Checksum& other) const {
        return file_size == other.file_size && checksum == other.checksum;
    }
};
```

## Wide Format Data Part

```cpp
/**
 * Wide format: separate file per column
 *
 * File layout:
 * column1.type/data.bin       - Compressed granule data
 * column1.type/marks.mrk2     - Marks (granule offsets)
 * column1.type/primary.idx    - Sparse primary index
 * column2.type/data.bin
 * column2.type/marks.mrk2
 * ...
 * primary.idx                 - Overall primary index
 * checksums.txt               - File checksums
 * columns.txt                 - Column schema
 *
 * Based on: ClickHouse MergeTreeDataPartWide
 */
class MergeTreeDataPartWide : public IMergeTreeDataPart {
public:
    Type getType() const override {
        return Type::Wide;
    }

    /**
     * Get column file path
     * Example: "column_name.UInt32/data.bin"
     */
    std::filesystem::path getColumnPath(const std::string& column_name,
                                       const std::string& extension) const {
        // Type suffix from column info
        std::string type_suffix = columns_.at(column_name).type_name;
        return full_path_ / (column_name + "." + type_suffix) / extension;
    }

    /**
     * Get column data file
     */
    std::filesystem::path getColumnDataPath(const std::string& column_name) const {
        return getColumnPath(column_name, "data.bin");
    }

    /**
     * Get column marks file
     */
    std::filesystem::path getColumnMarksPath(const std::string& column_name) const {
        return getColumnPath(column_name, "marks.mrk2");
    }

    /**
     * List all column directories
     */
    std::vector<std::filesystem::path> getColumnDirectories() const {
        std::vector<std::filesystem::path> dirs;
        for (const auto& entry : std::filesystem::directory_iterator(full_path_)) {
            if (entry.is_directory() && entry.path().filename().string().find('.') != std::string::npos) {
                dirs.push_back(entry.path());
            }
        }
        return dirs;
    }
};
```

## Compact Format Data Part

```cpp
/**
 * Compact format: all columns in single data.bin
 *
 * File layout:
 * data.bin        - All column data (interleaved by row)
 * marks.mrk3      - Unified marks for all columns
 * primary.idx     - Primary index
 * checksums.txt   - File checksums
 * columns.txt     - Column schema
 *
 * Benefits:
 * - Fewer files (better for filesystems with inode limits)
 * - Atomic writes (all columns in one file)
 * - Better for small parts (<10MB)
 *
 * Drawbacks:
 * - Cannot read single column efficiently
 * - Larger reads for column-specific queries
 *
 * Based on: ClickHouse MergeTreeDataPartCompact
 */
class MergeTreeDataPartCompact : public IMergeTreeDataPart {
public:
    Type getType() const override {
        return Type::Compact;
    }

    /**
     * Get data file (contains all columns)
     */
    std::filesystem::path getDataPath() const {
        return full_path_ / "data.bin";
    }

    /**
     * Get marks file (contains marks for all columns)
     */
    std::filesystem::path getMarksPath() const {
        return full_path_ / "marks.mrk3";
    }

    /**
     * Compact format uses different mark structure
     * Each mark contains offsets for ALL columns
     */
    struct CompactMark {
        std::vector<MarkInCompressedFile> column_marks;
        uint64_t rows_in_granule;
    };
};
```

## Data Part Writer

### Base Writer

```cpp
/**
 * Base class for writing data parts
 *
 * Based on: ClickHouse IMergeTreeDataPartWriter
 */
class IMergeTreeDataPartWriter {
public:
    virtual ~IMergeTreeDataPartWriter() = default;

    /**
     * Write block of data
     * @param block Columns with data
     * @param permutation Optional row reordering
     */
    virtual void write(const Block& block,
                      const IColumn::Permutation* permutation = nullptr) = 0;

    /**
     * Finalize and flush
     * @param sync Perform fsync for durability
     */
    virtual void finish(bool sync = true) = 0;

    /**
     * Cancel and remove partial data
     */
    virtual void cancel() noexcept = 0;
};
```

### Wide Format Writer

```cpp
/**
 * Writer for wide format parts
 *
 * Writes each column to separate file with marks.
 *
 * Based on: ClickHouse MergeTreeDataPartWriterWide
 */
class MergeTreeDataPartWriterWide : public IMergeTreeDataPartWriter {
public:
    MergeTreeDataPartWriterWide(
        const std::string& part_path,
        const NamesAndTypesList& columns,
        const CompressionCodecPtr& codec,
        const MergeTreeIndexGranularityPtr& index_granularity);

    void write(const Block& block,
              const IColumn::Permutation* permutation) override {
        // For each column in block
        for (const auto& column : block.getColumns()) {
            writeColumn(column.name, column.column, column.type);
        }

        // Update granularity
        current_row_ += block.rows();
        if (shouldFinishGranule()) {
            finishGranule();
        }
    }

    void finish(bool sync) override {
        // Finish last granule
        if (current_row_ > last_granule_end_) {
            finishGranule();
        }

        // Close all streams
        for (auto& [name, stream] : column_streams_) {
            stream->finalize();
            if (sync) {
                stream->sync();
            }
        }

        // Calculate and write primary index
        writePrimaryIndex();

        // Write checksums
        writeChecksums();
    }

private:
    struct ColumnStream {
        std::unique_ptr<WriteBufferFromFile> data_file;
        std::unique_ptr<CompressedWriteBuffer> compressed;
        std::unique_ptr<WriteBufferFromFile> marks_file;
        std::vector<MarkInCompressedFile> marks;
    };

    std::unordered_map<std::string, std::unique_ptr<ColumnStream>> column_streams_;

    uint64_t current_row_{0};
    uint64_t last_granule_end_{0};
    MergeTreeIndexGranularityPtr index_granularity_;

    void writeColumn(const std::string& name,
                    const ColumnPtr& column,
                    const DataTypePtr& type) {
        auto& stream = column_streams_[name];

        // Serialize column data
        auto serialization = type->getDefaultSerialization();
        serialization->serializeBinaryBulk(
            *column,
            *stream->compressed,
            0,
            column->size()
        );
    }

    bool shouldFinishGranule() const {
        return current_row_ - last_granule_end_ >=
               index_granularity_->getMarkRows(granule_index_);
    }

    void finishGranule() {
        // Write mark for each column
        for (auto& [name, stream] : column_streams_) {
            MarkInCompressedFile mark;
            mark.offset_in_compressed_file = stream->data_file->count();
            mark.offset_in_decompressed_block = stream->compressed->offset();

            stream->marks.push_back(mark);

            // Write mark to marks file
            writeIntBinary(mark.offset_in_compressed_file, *stream->marks_file);
            writeIntBinary(mark.offset_in_decompressed_block, *stream->marks_file);
        }

        last_granule_end_ = current_row_;
        ++granule_index_;
    }

    void writePrimaryIndex() {
        // Collect primary key columns at granule boundaries
        // Write to primary.idx
    }

    void writeChecksums() {
        // Calculate checksums for all files
        // Write to checksums.txt
    }

    uint64_t granule_index_{0};
};
```

### Compact Format Writer

```cpp
/**
 * Writer for compact format parts
 *
 * Writes all columns to single data.bin file.
 *
 * Based on: ClickHouse MergeTreeDataPartWriterCompact
 */
class MergeTreeDataPartWriterCompact : public IMergeTreeDataPartWriter {
public:
    void write(const Block& block,
              const IColumn::Permutation* permutation) override {
        // Serialize all columns to single stream
        // Interleave by row for better locality

        for (size_t row = 0; row < block.rows(); ++row) {
            for (const auto& column : block.getColumns()) {
                // Serialize single value from each column
                column.type->getDefaultSerialization()->serializeBinary(
                    (*column.column)[row],
                    *compressed_stream_
                );
            }

            current_row_++;
            if (shouldFinishGranule()) {
                finishGranule();
            }
        }
    }

    void finish(bool sync) override {
        // Finish last granule
        if (current_row_ > last_granule_end_) {
            finishGranule();
        }

        // Close streams
        compressed_stream_->finalize();
        if (sync) {
            data_file_->sync();
        }

        // Write marks file
        writeMarks();

        // Write primary index
        writePrimaryIndex();

        // Write checksums
        writeChecksums();
    }

private:
    std::unique_ptr<WriteBufferFromFile> data_file_;
    std::unique_ptr<CompressedWriteBuffer> compressed_stream_;
    std::unique_ptr<WriteBufferFromFile> marks_file_;

    std::vector<MergeTreeDataPartCompact::CompactMark> marks_;

    uint64_t current_row_{0};
    uint64_t last_granule_end_{0};

    void finishGranule() {
        // Record mark for all columns at once
        CompactMark mark;
        mark.rows_in_granule = current_row_ - last_granule_end_;

        // One mark per column
        for (size_t i = 0; i < column_count_; ++i) {
            MarkInCompressedFile m;
            m.offset_in_compressed_file = data_file_->count();
            m.offset_in_decompressed_block = compressed_stream_->offset();
            mark.column_marks.push_back(m);
        }

        marks_.push_back(mark);
        last_granule_end_ = current_row_;
    }

    void writeMarks() {
        for (const auto& mark : marks_) {
            // Write rows in granule
            writeIntBinary(mark.rows_in_granule, *marks_file_);

            // Write marks for each column
            for (const auto& col_mark : mark.column_marks) {
                writeIntBinary(col_mark.offset_in_compressed_file, *marks_file_);
                writeIntBinary(col_mark.offset_in_decompressed_block, *marks_file_);
            }
        }
    }

    size_t column_count_;
};
```

## Data Part Reader

### Wide Format Reader

```cpp
/**
 * Reader for wide format parts
 *
 * Reads columns from separate files using marks for random access.
 *
 * Based on: ClickHouse MergeTreeReaderWide
 */
class MergeTreeReaderWide {
public:
    MergeTreeReaderWide(const MergeTreeDataPartWide* data_part);

    /**
     * Read rows from mark range
     * @param from_mark Starting mark (granule)
     * @param to_mark Ending mark (exclusive)
     * @param columns Columns to read
     * @return Block with requested columns
     */
    Block readRows(size_t from_mark, size_t to_mark,
                   const Names& columns);

    /**
     * Read specific rows from mark range
     * Useful for filtering
     */
    Block readRowsWithFilter(size_t from_mark, size_t to_mark,
                            const Names& columns,
                            const IColumn::Filter& filter);

private:
    struct ColumnStream {
        std::unique_ptr<ReadBufferFromFile> data_file;
        std::unique_ptr<CompressedReadBuffer> compressed;
        std::vector<MarkInCompressedFile> marks;
        size_t current_mark{0};
    };

    const MergeTreeDataPartWide* data_part_;
    std::unordered_map<std::string, std::unique_ptr<ColumnStream>> column_streams_;

    /**
     * Seek to mark in column stream
     */
    void seekToMark(ColumnStream& stream, size_t mark_index) {
        if (stream.current_mark == mark_index) return;

        const auto& mark = stream.marks[mark_index];

        // Seek in compressed file
        stream.data_file->seek(mark.offset_in_compressed_file);

        // Reset decompressor
        stream.compressed->seek(
            mark.offset_in_compressed_file,
            mark.offset_in_decompressed_block
        );

        stream.current_mark = mark_index;
    }

    /**
     * Read column from current position
     */
    ColumnPtr readColumn(const std::string& name,
                        const DataTypePtr& type,
                        size_t rows_to_read) {
        auto& stream = column_streams_[name];

        auto column = type->createColumn();
        auto serialization = type->getDefaultSerialization();

        serialization->deserializeBinaryBulk(
            *column,
            *stream.compressed,
            rows_to_read,
            0.0  // avg_value_size_hint
        );

        return column;
    }
};
```

### Compact Format Reader

```cpp
/**
 * Reader for compact format parts
 *
 * Reads all columns from single data.bin file.
 *
 * Based on: ClickHouse MergeTreeReaderCompact
 */
class MergeTreeReaderCompact {
public:
    Block readRows(size_t from_mark, size_t to_mark,
                   const Names& columns) {
        // Seek to first mark
        seekToMark(from_mark);

        // Read rows (all columns at once)
        Block block;
        size_t rows_to_read = calculateRowsInRange(from_mark, to_mark);

        // Read interleaved column data
        for (size_t row = 0; row < rows_to_read; ++row) {
            for (const auto& name : columns) {
                auto& column = block.getByName(name).column;
                auto& type = block.getByName(name).type;

                Field value;
                type->getDefaultSerialization()->deserializeBinary(
                    value,
                    *compressed_stream_
                );
                column->insert(value);
            }
        }

        return block;
    }

private:
    std::unique_ptr<ReadBufferFromFile> data_file_;
    std::unique_ptr<CompressedReadBuffer> compressed_stream_;
    std::vector<CompactMark> marks_;
    size_t current_mark_{0};

    void seekToMark(size_t mark_index) {
        if (current_mark_ == mark_index) return;

        // Marks in compact format point to beginning of granule
        const auto& mark = marks_[mark_index];

        // Use first column's mark (all columns share same positions)
        const auto& first_mark = mark.column_marks[0];

        data_file_->seek(first_mark.offset_in_compressed_file);
        compressed_stream_->seek(
            first_mark.offset_in_compressed_file,
            first_mark.offset_in_decompressed_block
        );

        current_mark_ = mark_index;
    }

    size_t calculateRowsInRange(size_t from_mark, size_t to_mark) const {
        size_t total_rows = 0;
        for (size_t i = from_mark; i < to_mark; ++i) {
            total_rows += marks_[i].rows_in_granule;
        }
        return total_rows;
    }
};
```

## Usage Example

```cpp
// Create part based on size
uint64_t rows = 100000;
uint64_t bytes = 15 * 1024 * 1024;  // 15MB

DataPartType type = selectPartType(rows, bytes);  // Returns Wide

// Create appropriate part
auto part = IMergeTreeDataPart::create(
    type,
    "20230101_0_0_0",
    "/var/lib/lucenepp/data/parts/"
);

// Write data
std::unique_ptr<IMergeTreeDataPartWriter> writer;
if (type == DataPartType::Wide) {
    writer = std::make_unique<MergeTreeDataPartWriterWide>(...);
} else {
    writer = std::make_unique<MergeTreeDataPartWriterCompact>(...);
}

for (const auto& block : data_blocks) {
    writer->write(block);
}

writer->finish(true);  // Sync to disk

// Read data
if (type == DataPartType::Wide) {
    MergeTreeReaderWide reader(static_cast<const MergeTreeDataPartWide*>(part.get()));
    Block result = reader.readRows(0, 10, {"column1", "column2"});
} else {
    MergeTreeReaderCompact reader(static_cast<const MergeTreeDataPartCompact*>(part.get()));
    Block result = reader.readRows(0, 10, {"column1", "column2"});
}
```
