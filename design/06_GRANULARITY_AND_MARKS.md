# Granularity and Marks Design
## Based on ClickHouse Granule System

Source references:
- `ClickHouse/src/Storages/MergeTree/MergeTreeIndexGranularity.h`
- `ClickHouse/src/Storages/MergeTree/MarkInCompressedFile.h`
- `ClickHouse/src/Storages/MergeTree/MarkRange.h`
- `ClickHouse/src/Storages/MergeTree/MarksInCompressedFile.h`

## Overview

**Granules** are the fundamental unit of data reading in column storage:
- Default size: 8192 rows per granule
- **Marks** provide file offsets to seek to granules
- **Sparse primary index** stores first value of each granule
- **Adaptive granularity** adjusts size based on data

## IMergeTreeIndexGranularity

```cpp
/**
 * Index granularity defines row distribution across marks.
 *
 * Two implementations:
 * - Constant: Fixed rows per mark (e.g., 8192)
 * - Adaptive: Variable rows based on compressed size
 *
 * Based on: ClickHouse IMergeTreeIndexGranularity
 */
class IMergeTreeIndexGranularity {
public:
    virtual ~IMergeTreeIndexGranularity() = default;

    /**
     * Number of marks in this granularity
     */
    virtual size_t getMarksCount() const = 0;

    /**
     * Get rows in specific mark/granule
     */
    virtual size_t getMarkRows(size_t mark_index) const = 0;

    /**
     * Get total rows in range [begin, end)
     */
    virtual size_t getRowsCountInRange(size_t begin, size_t end) const = 0;

    /**
     * Get total rows from start to mark
     */
    virtual size_t getRowsCountInRange(size_t end) const {
        return getRowsCountInRange(0, end);
    }

    /**
     * Total rows across all marks
     */
    virtual size_t getTotalRows() const {
        return getRowsCountInRange(getMarksCount());
    }

    /**
     * Find mark containing row
     * @return mark index
     */
    virtual size_t getMarkContainingRow(size_t row) const = 0;

    /**
     * Get number of marks needed for rows count
     */
    virtual size_t countMarksForRows(size_t from_mark, size_t number_of_rows) const = 0;

    /**
     * Has final mark (empty mark at end)?
     */
    virtual bool hasFinalMark() const = 0;

    /**
     * Is empty (no marks)?
     */
    virtual bool empty() const = 0;

    /**
     * Serialization
     */
    virtual void serialize(WriteBuffer& out) const = 0;
    virtual void deserialize(ReadBuffer& in) = 0;
};

using MergeTreeIndexGranularityPtr = std::shared_ptr<IMergeTreeIndexGranularity>;
```

## MergeTreeIndexGranularityConstant

```cpp
/**
 * Constant granularity: fixed rows per mark
 *
 * Used when: index_granularity_bytes = 0 (adaptive disabled)
 * Default: 8192 rows per mark
 *
 * Based on: ClickHouse MergeTreeIndexGranularityConstant
 */
class MergeTreeIndexGranularityConstant : public IMergeTreeIndexGranularity {
public:
    explicit MergeTreeIndexGranularityConstant(size_t granularity = 8192,
                                               size_t num_marks = 0)
        : granularity_(granularity)
        , num_marks_(num_marks) {}

    size_t getMarksCount() const override {
        return num_marks_;
    }

    size_t getMarkRows(size_t mark_index) const override {
        if (mark_index >= num_marks_) {
            throw Exception("Mark index out of range");
        }

        // Last mark may have fewer rows
        if (mark_index == num_marks_ - 1) {
            size_t total = getTotalRows();
            return total - (num_marks_ - 1) * granularity_;
        }

        return granularity_;
    }

    size_t getRowsCountInRange(size_t begin, size_t end) const override {
        if (end <= begin) return 0;
        if (end > num_marks_) end = num_marks_;

        size_t rows = (end - begin - 1) * granularity_;

        // Add rows from last mark
        rows += getMarkRows(end - 1);

        return rows;
    }

    size_t getMarkContainingRow(size_t row) const override {
        return row / granularity_;
    }

    size_t countMarksForRows(size_t from_mark, size_t number_of_rows) const override {
        size_t from_row = from_mark * granularity_;
        size_t to_row = from_row + number_of_rows;
        size_t to_mark = (to_row + granularity_ - 1) / granularity_;
        return to_mark - from_mark;
    }

    bool hasFinalMark() const override {
        return false;  // Constant granularity doesn't need final mark
    }

    bool empty() const override {
        return num_marks_ == 0;
    }

    void serialize(WriteBuffer& out) const override {
        writeIntBinary(granularity_, out);
        writeIntBinary(num_marks_, out);
    }

    void deserialize(ReadBuffer& in) override {
        readIntBinary(granularity_, in);
        readIntBinary(num_marks_, in);
    }

    /**
     * Add marks (during writing)
     */
    void addMark(size_t rows) {
        if (rows != granularity_ && num_marks_ > 0) {
            throw Exception("Constant granularity expects " +
                          std::to_string(granularity_) + " rows, got " +
                          std::to_string(rows));
        }
        ++num_marks_;
    }

private:
    size_t granularity_;  // Rows per mark (e.g., 8192)
    size_t num_marks_;    // Number of marks
};
```

## MergeTreeIndexGranularityAdaptive

```cpp
/**
 * Adaptive granularity: variable rows per mark
 *
 * Used when: index_granularity_bytes > 0 (default: 10MB)
 * Adjusts granule size to target compressed size.
 *
 * Benefits:
 * - Consistent I/O per granule
 * - Better for large/sparse columns
 *
 * Based on: ClickHouse MergeTreeIndexGranularityAdaptive
 */
class MergeTreeIndexGranularityAdaptive : public IMergeTreeIndexGranularity {
public:
    MergeTreeIndexGranularityAdaptive() = default;

    size_t getMarksCount() const override {
        return marks_rows_partial_sums_.size();
    }

    size_t getMarkRows(size_t mark_index) const override {
        if (mark_index >= marks_rows_partial_sums_.size()) {
            throw Exception("Mark index out of range");
        }

        if (mark_index == 0) {
            return marks_rows_partial_sums_[0];
        }

        return marks_rows_partial_sums_[mark_index] -
               marks_rows_partial_sums_[mark_index - 1];
    }

    size_t getRowsCountInRange(size_t begin, size_t end) const override {
        if (end <= begin) return 0;
        if (end > marks_rows_partial_sums_.size()) {
            end = marks_rows_partial_sums_.size();
        }

        size_t end_rows = marks_rows_partial_sums_[end - 1];
        size_t begin_rows = begin == 0 ? 0 : marks_rows_partial_sums_[begin - 1];

        return end_rows - begin_rows;
    }

    size_t getMarkContainingRow(size_t row) const override {
        // Binary search in cumulative sums
        auto it = std::upper_bound(
            marks_rows_partial_sums_.begin(),
            marks_rows_partial_sums_.end(),
            row
        );

        return std::distance(marks_rows_partial_sums_.begin(), it);
    }

    size_t countMarksForRows(size_t from_mark, size_t number_of_rows) const override {
        size_t rows_before = from_mark == 0 ? 0 : marks_rows_partial_sums_[from_mark - 1];
        size_t target_row = rows_before + number_of_rows;

        auto it = std::lower_bound(
            marks_rows_partial_sums_.begin() + from_mark,
            marks_rows_partial_sums_.end(),
            target_row
        );

        return std::distance(marks_rows_partial_sums_.begin() + from_mark, it) + 1;
    }

    bool hasFinalMark() const override {
        return !marks_rows_partial_sums_.empty() &&
               marks_rows_partial_sums_.back() == 0;
    }

    bool empty() const override {
        return marks_rows_partial_sums_.empty();
    }

    void serialize(WriteBuffer& out) const override {
        writeIntBinary(marks_rows_partial_sums_.size(), out);
        for (size_t rows : marks_rows_partial_sums_) {
            writeIntBinary(rows, out);
        }
    }

    void deserialize(ReadBuffer& in) override {
        size_t count;
        readIntBinary(count, in);

        marks_rows_partial_sums_.resize(count);
        for (size_t& rows : marks_rows_partial_sums_) {
            readIntBinary(rows, in);
        }
    }

    /**
     * Add mark with specific row count
     */
    void addMark(size_t rows) {
        size_t cumulative = marks_rows_partial_sums_.empty() ?
                           rows :
                           marks_rows_partial_sums_.back() + rows;

        marks_rows_partial_sums_.push_back(cumulative);
    }

    /**
     * Get cumulative rows at mark
     */
    size_t getCumulativeRows(size_t mark_index) const {
        return marks_rows_partial_sums_[mark_index];
    }

private:
    /**
     * Cumulative row counts
     * marks_rows_partial_sums_[i] = total rows from start to end of mark i
     *
     * Example: [100, 250, 408, 550]
     *   Mark 0: 100 rows
     *   Mark 1: 150 rows (250 - 100)
     *   Mark 2: 158 rows (408 - 250)
     *   Mark 3: 142 rows (550 - 408)
     */
    std::vector<size_t> marks_rows_partial_sums_;
};
```

## MarkInCompressedFile

```cpp
/**
 * Mark points to position in compressed file.
 *
 * Two-level addressing:
 * 1. offset_in_compressed_file: Position in .bin file
 * 2. offset_in_decompressed_block: Position within decompressed block
 *
 * Based on: ClickHouse MarkInCompressedFile
 */
struct MarkInCompressedFile {
    /**
     * Offset in compressed file (.bin)
     * Points to start of compressed block containing this mark
     */
    uint64_t offset_in_compressed_file;

    /**
     * Offset within decompressed block
     * Number of bytes to skip after decompressing
     */
    uint64_t offset_in_decompressed_block;

    MarkInCompressedFile() = default;

    MarkInCompressedFile(uint64_t file_offset, uint64_t block_offset)
        : offset_in_compressed_file(file_offset)
        , offset_in_decompressed_block(block_offset) {}

    bool operator==(const MarkInCompressedFile& other) const {
        return offset_in_compressed_file == other.offset_in_compressed_file &&
               offset_in_decompressed_block == other.offset_in_decompressed_block;
    }

    /**
     * Serialization (.mrk2 file format)
     */
    void serialize(WriteBuffer& out) const {
        writeIntBinary(offset_in_compressed_file, out);
        writeIntBinary(offset_in_decompressed_block, out);
    }

    void deserialize(ReadBuffer& in) {
        readIntBinary(offset_in_compressed_file, in);
        readIntBinary(offset_in_decompressed_block, in);
    }
};
```

## MarkRange

```cpp
/**
 * Range of marks to read
 *
 * Defines a contiguous range [begin, end) of marks/granules.
 *
 * Based on: ClickHouse MarkRange
 */
struct MarkRange {
    size_t begin;  // Inclusive
    size_t end;    // Exclusive

    MarkRange() : begin(0), end(0) {}

    MarkRange(size_t begin_, size_t end_) : begin(begin_), end(end_) {}

    bool operator==(const MarkRange& other) const {
        return begin == other.begin && end == other.end;
    }

    bool operator<(const MarkRange& other) const {
        return begin < other.begin || (begin == other.begin && end < other.end);
    }

    size_t getNumberOfMarks() const {
        return end - begin;
    }

    bool empty() const {
        return begin == end;
    }
};

using MarkRanges = std::vector<MarkRange>;

/**
 * Convert mark ranges to row ranges
 */
std::vector<std::pair<size_t, size_t>> markRangesToRows(
    const MarkRanges& mark_ranges,
    const IMergeTreeIndexGranularity& granularity) {

    std::vector<std::pair<size_t, size_t>> row_ranges;

    for (const auto& range : mark_ranges) {
        size_t start_row = granularity.getRowsCountInRange(range.begin);
        size_t end_row = granularity.getRowsCountInRange(range.end);

        row_ranges.emplace_back(start_row, end_row);
    }

    return row_ranges;
}
```

## Granularity Configuration

```cpp
/**
 * Configuration for adaptive granularity
 */
struct GranularityConfig {
    /**
     * Target granule size (default: 8192 rows)
     */
    size_t index_granularity = 8192;

    /**
     * Target uncompressed bytes per granule (default: 10MB)
     * Set to 0 to disable adaptive granularity
     */
    size_t index_granularity_bytes = 10 * 1024 * 1024;

    /**
     * Minimum rows per granule (default: 1024)
     * Even with adaptive granularity, don't go below this
     */
    size_t min_index_granularity_bytes = 1024;

    /**
     * Use adaptive granularity?
     */
    bool use_adaptive_granularity() const {
        return index_granularity_bytes > 0;
    }

    /**
     * Create appropriate granularity object
     */
    MergeTreeIndexGranularityPtr createGranularity() const {
        if (use_adaptive_granularity()) {
            return std::make_shared<MergeTreeIndexGranularityAdaptive>();
        } else {
            return std::make_shared<MergeTreeIndexGranularityConstant>(
                index_granularity
            );
        }
    }
};
```

## Granule Writer Helper

```cpp
/**
 * Helper for writing data with granules
 */
class GranuleWriter {
public:
    GranuleWriter(const GranularityConfig& config)
        : config_(config)
        , granularity_(config.createGranularity()) {}

    /**
     * Check if should finish current granule
     */
    bool shouldFinishGranule(size_t rows_written_in_granule,
                            size_t bytes_written_in_granule) const {
        if (config_.use_adaptive_granularity()) {
            // Adaptive: check both rows and bytes
            return bytes_written_in_granule >= config_.index_granularity_bytes ||
                   rows_written_in_granule >= config_.index_granularity;
        } else {
            // Constant: only check rows
            return rows_written_in_granule >= config_.index_granularity;
        }
    }

    /**
     * Finish granule and add mark
     */
    void finishGranule(size_t rows_in_granule) {
        granularity_->addMark(rows_in_granule);
    }

    const IMergeTreeIndexGranularity& getGranularity() const {
        return *granularity_;
    }

private:
    GranularityConfig config_;
    MergeTreeIndexGranularityPtr granularity_;
};
```

## Mark-Based Seeking

```cpp
/**
 * Seek to mark in compressed file
 */
class MarkSeeker {
public:
    MarkSeeker(ReadBufferFromFile& file,
               CompressedReadBuffer& compressed,
               const std::vector<MarkInCompressedFile>& marks)
        : file_(file)
        , compressed_(compressed)
        , marks_(marks) {}

    /**
     * Seek to specific mark
     */
    void seekToMark(size_t mark_index) {
        if (mark_index >= marks_.size()) {
            throw Exception("Mark index out of range");
        }

        const auto& mark = marks_[mark_index];

        // Seek in file
        file_.seek(mark.offset_in_compressed_file);

        // Reset compressed buffer and seek within block
        compressed_.seek(
            mark.offset_in_compressed_file,
            mark.offset_in_decompressed_block
        );

        current_mark_ = mark_index;
    }

    /**
     * Read rows from mark range
     */
    template <typename T>
    std::vector<T> readRange(size_t from_mark, size_t to_mark,
                            const IMergeTreeIndexGranularity& granularity) {
        seekToMark(from_mark);

        size_t rows_to_read = granularity.getRowsCountInRange(from_mark, to_mark);
        std::vector<T> result;
        result.reserve(rows_to_read);

        for (size_t i = 0; i < rows_to_read; ++i) {
            T value;
            readBinary(value, compressed_);
            result.push_back(value);
        }

        return result;
    }

private:
    ReadBufferFromFile& file_;
    CompressedReadBuffer& compressed_;
    const std::vector<MarkInCompressedFile>& marks_;
    size_t current_mark_{0};
};
```

## Usage Example

```cpp
// Create adaptive granularity
GranularityConfig config;
config.index_granularity = 8192;
config.index_granularity_bytes = 10 * 1024 * 1024;  // 10MB

auto granularity = config.createGranularity();

// Writing with granules
GranuleWriter writer(config);
size_t rows_in_current_granule = 0;
size_t bytes_in_current_granule = 0;

for (const auto& row : data) {
    // Write row
    write_row(row);

    rows_in_current_granule++;
    bytes_in_current_granule += row.size();

    // Check if should finish granule
    if (writer.shouldFinishGranule(rows_in_current_granule,
                                  bytes_in_current_granule)) {
        // Write mark
        MarkInCompressedFile mark;
        mark.offset_in_compressed_file = file.count();
        mark.offset_in_decompressed_block = compressed.offset();
        marks.push_back(mark);

        // Finish granule
        writer.finishGranule(rows_in_current_granule);

        rows_in_current_granule = 0;
        bytes_in_current_granule = 0;
    }
}

// Reading with marks
MarkSeeker seeker(file, compressed, marks);

// Read specific mark range
auto data = seeker.readRange<int32_t>(10, 20, writer.getGranularity());

// Find mark containing row 50000
size_t mark = granularity->getMarkContainingRow(50000);
seeker.seekToMark(mark);
```

## Mark File Formats

```cpp
/**
 * Mark file formats
 *
 * .mrk  (legacy): Simple offset pairs
 * .mrk2 (current): Offset pairs with checksums
 * .mrk3 (compact): Multiple columns in unified marks
 * .cmrk (compressed): Compressed mark files
 */

// .mrk2 format (wide format)
struct MarkFileV2 {
    std::vector<MarkInCompressedFile> marks;

    void write(const std::string& path) {
        WriteBufferFromFile out(path);
        for (const auto& mark : marks) {
            mark.serialize(out);
        }
    }

    void read(const std::string& path) {
        ReadBufferFromFile in(path);
        while (!in.eof()) {
            MarkInCompressedFile mark;
            mark.deserialize(in);
            marks.push_back(mark);
        }
    }
};

// .mrk3 format (compact format)
struct MarkFileV3 {
    struct CompactMark {
        std::vector<MarkInCompressedFile> column_marks;
        uint64_t rows_in_granule;
    };

    std::vector<CompactMark> marks;

    void write(const std::string& path) {
        WriteBufferFromFile out(path);
        for (const auto& mark : marks) {
            writeIntBinary(mark.rows_in_granule, out);
            for (const auto& col_mark : mark.column_marks) {
                col_mark.serialize(out);
            }
        }
    }
};
```
