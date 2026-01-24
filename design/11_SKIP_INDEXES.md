# Skip Index System Design
## Based on ClickHouse MergeTree Skip Indexes

Source references:
- `ClickHouse/src/Storages/MergeTree/MergeTreeIndices.h`
- `ClickHouse/src/Storages/MergeTree/MergeTreeIndexMinMax.h`
- `ClickHouse/src/Storages/MergeTree/MergeTreeIndexSet.h`
- `ClickHouse/src/Storages/MergeTree/MergeTreeIndexBloomFilter.h`
- `ClickHouse/src/Storages/MergeTree/MergeTreeDataPartWriterOnDisk.cpp`

## Overview

Skip indexes enable efficient data skipping by maintaining lightweight metadata about data granules. During query execution, the index can determine if an entire granule can be skipped without reading its data.

**Key Concepts**:
- **Granule**: Unit of data reading (typically 8192 rows)
- **Index Granularity**: How many data granules per index granule (default: 1)
- **Skip Index**: Per-granule metadata enabling range pruning
- **Three-phase lifecycle**: Aggregate (write) → Serialize → Filter (read)

**Index Types**:
- **MinMax**: Tracks min/max values per granule (range queries)
- **Set**: Stores unique values per granule (membership tests)
- **BloomFilter**: Probabilistic membership test (equality checks)

## Core Interfaces

### IMergeTreeIndexGranule (Serializable Metadata)

```cpp
/**
 * Granule-level index data
 *
 * One granule per N data granules (configurable granularity).
 * Serialized to .idx file alongside data part.
 *
 * Based on: IMergeTreeIndexGranule
 */
class IMergeTreeIndexGranule {
public:
    virtual ~IMergeTreeIndexGranule() = default;

    // ==================== Serialization ====================

    /**
     * Serialize to single output stream
     */
    virtual void serializeBinary(WriteBuffer& ostr) const = 0;

    /**
     * Deserialize from single input stream
     * @param version Format version for backward compatibility
     */
    virtual void deserializeBinary(ReadBuffer& istr,
                                   MergeTreeIndexVersion version) = 0;

    // ==================== Properties ====================

    /**
     * Does this granule contain no data?
     */
    virtual bool empty() const = 0;

    /**
     * Memory footprint in bytes
     */
    virtual size_t memoryUsageBytes() const = 0;
};

using MergeTreeIndexGranulePtr = std::shared_ptr<IMergeTreeIndexGranule>;
using MergeTreeIndexGranules = std::vector<MergeTreeIndexGranulePtr>;

// Format version for backward compatibility
using MergeTreeIndexVersion = uint8_t;

constexpr MergeTreeIndexVersion MINMAX_VERSION_V1 = 1;  // Original
constexpr MergeTreeIndexVersion MINMAX_VERSION_V2 = 2;  // Nullable support
```

### IMergeTreeIndexAggregator (Write-Time Builder)

```cpp
/**
 * Accumulates index data during writes
 *
 * One aggregator per index per segment.
 * Accumulates rows until granularity boundary, then emits granule.
 *
 * Based on: IMergeTreeIndexAggregator
 */
class IMergeTreeIndexAggregator {
public:
    virtual ~IMergeTreeIndexAggregator() = default;

    // ==================== State Management ====================

    /**
     * Has no accumulated data?
     */
    virtual bool empty() const = 0;

    /**
     * Create granule from accumulated data and reset state
     * Called when granularity boundary reached
     */
    virtual MergeTreeIndexGranulePtr getGranuleAndReset() = 0;

    // ==================== Data Accumulation ====================

    /**
     * Accumulate index data from block rows
     *
     * @param block Block containing indexed columns
     * @param pos Input/output row position (updated after processing)
     * @param limit Max rows to process from current position
     */
    virtual void update(const Block& block, size_t* pos, size_t limit) = 0;
};

using MergeTreeIndexAggregatorPtr = std::shared_ptr<IMergeTreeIndexAggregator>;
```

### IMergeTreeIndexCondition (Read-Time Filter)

```cpp
/**
 * Query-time condition evaluation for granule filtering
 *
 * Converts WHERE clause to index-specific representation.
 * Tests each granule for potential matches.
 *
 * Based on: IMergeTreeIndexCondition
 */
class IMergeTreeIndexCondition {
public:
    virtual ~IMergeTreeIndexCondition() = default;

    // ==================== Query Analysis ====================

    /**
     * Can this index help with the query?
     * @return true if index cannot filter any data
     */
    virtual bool alwaysUnknownOrTrue() const = 0;

    // ==================== Granule Filtering ====================

    /**
     * Can data in this granule match query condition?
     *
     * @param granule Index granule metadata
     * @return true if granule MAY contain matching rows (read it)
     *         false if granule CANNOT contain matches (skip it)
     */
    virtual bool mayBeTrueOnGranule(MergeTreeIndexGranulePtr granule) const = 0;

    // ==================== Description ====================

    virtual std::string getDescription() const = 0;
};

using MergeTreeIndexConditionPtr = std::shared_ptr<IMergeTreeIndexCondition>;
```

### IMergeTreeIndex (Index Definition)

```cpp
/**
 * Skip index definition and factory
 *
 * Provides factory methods for granules, aggregators, and conditions.
 * Defines file naming and serialization format.
 *
 * Based on: IMergeTreeIndex
 */
class IMergeTreeIndex {
public:
    explicit IMergeTreeIndex(const IndexDescription& index)
        : index_(index) {}

    virtual ~IMergeTreeIndex() = default;

    // ==================== File Naming ====================

    /**
     * Index file name: "skp_idx_<name>.idx"
     */
    std::string getFileName() const {
        return INDEX_FILE_PREFIX + index_.name;
    }

    /**
     * File extension
     */
    virtual std::string getFileExtension() const {
        return ".idx";
    }

    /**
     * How many data granules per index granule
     */
    size_t getGranularity() const {
        return index_.granularity;
    }

    // ==================== Factory Methods ====================

    /**
     * Create empty granule for deserialization
     */
    virtual MergeTreeIndexGranulePtr createIndexGranule() const = 0;

    /**
     * Create aggregator for write-time building
     */
    virtual MergeTreeIndexAggregatorPtr createIndexAggregator() const = 0;

    /**
     * Create condition for read-time filtering
     * @param predicate WHERE clause AST node
     * @param context Query context
     */
    virtual MergeTreeIndexConditionPtr createIndexCondition(
        const ASTNode* predicate,
        const QueryContext& context) const = 0;

    // ==================== Properties ====================

    /**
     * Index metadata
     */
    const IndexDescription& getDescription() const {
        return index_;
    }

protected:
    static constexpr const char* INDEX_FILE_PREFIX = "skp_idx_";

    IndexDescription index_;
};

using MergeTreeIndexPtr = std::shared_ptr<IMergeTreeIndex>;
using MergeTreeIndices = std::vector<MergeTreeIndexPtr>;
```

### IndexDescription (Metadata)

```cpp
/**
 * Index configuration metadata
 */
struct IndexDescription {
    std::string name;                   // Index name
    std::string type;                   // "minmax", "set", "bloom_filter"
    std::vector<std::string> columns;   // Indexed column names
    Block sample_block;                 // Column types
    size_t granularity;                 // Data granules per index granule

    // Type-specific parameters
    std::map<std::string, std::string> parameters;

    // Examples:
    // - Set index: {"max_rows": "8192"}
    // - BloomFilter: {"bits_per_row": "8", "hash_functions": "3"}
};
```

## MinMax Index

```cpp
/**
 * MinMax index tracks min/max values per granule
 *
 * Use for: Range queries (WHERE col > x AND col < y)
 * Storage: 2 * sizeof(value) per column per granule
 *
 * Based on: MergeTreeIndexMinMax
 */

// ==================== GRANULE ====================

class MergeTreeIndexGranuleMinMax : public IMergeTreeIndexGranule {
public:
    MergeTreeIndexGranuleMinMax(const std::string& index_name,
                                const Block& index_sample_block)
        : index_name_(index_name)
        , index_sample_block_(index_sample_block) {

        // Initialize ranges for each column
        hyperrectangle_.reserve(index_sample_block.columns());
    }

    void serializeBinary(WriteBuffer& ostr) const override {
        // Serialize each range (min, max) for each column
        for (size_t i = 0; i < hyperrectangle_.size(); ++i) {
            const auto& range = hyperrectangle_[i];
            const auto& column = index_sample_block_.getByPosition(i);

            // Serialize left (min) bound
            column.type->serializeBinary(range.left, ostr);

            // Serialize right (max) bound
            column.type->serializeBinary(range.right, ostr);
        }
    }

    void deserializeBinary(ReadBuffer& istr, MergeTreeIndexVersion version) override {
        hyperrectangle_.clear();

        for (size_t i = 0; i < index_sample_block_.columns(); ++i) {
            const auto& column = index_sample_block_.getByPosition(i);

            Range range;
            column.type->deserializeBinary(range.left, istr);
            column.type->deserializeBinary(range.right, istr);

            hyperrectangle_.push_back(range);
        }
    }

    bool empty() const override {
        return hyperrectangle_.empty();
    }

    size_t memoryUsageBytes() const override {
        return hyperrectangle_.capacity() * sizeof(Range);
    }

    // Per-column min/max ranges
    std::vector<Range> hyperrectangle_;

private:
    std::string index_name_;
    Block index_sample_block_;
};

// ==================== AGGREGATOR ====================

class MergeTreeIndexAggregatorMinMax : public IMergeTreeIndexAggregator {
public:
    MergeTreeIndexAggregatorMinMax(const std::string& index_name,
                                   const Block& index_sample_block)
        : index_name_(index_name)
        , index_sample_block_(index_sample_block) {

        hyperrectangle_.resize(index_sample_block.columns());
    }

    bool empty() const override {
        return hyperrectangle_.empty();
    }

    MergeTreeIndexGranulePtr getGranuleAndReset() override {
        auto granule = std::make_shared<MergeTreeIndexGranuleMinMax>(
            index_name_, index_sample_block_);

        granule->hyperrectangle_ = std::move(hyperrectangle_);

        // Reset for next granule
        hyperrectangle_.clear();
        hyperrectangle_.resize(index_sample_block_.columns());

        return granule;
    }

    void update(const Block& block, size_t* pos, size_t limit) override {
        if (empty()) {
            // First update: initialize ranges from first value
            for (size_t col = 0; col < index_sample_block_.columns(); ++col) {
                const auto& column = block.getByName(
                    index_sample_block_.getByPosition(col).name);

                Field value;
                column.column->get(*pos, value);

                hyperrectangle_[col].left = value;
                hyperrectangle_[col].right = value;
            }
        }

        // Process rows [pos, pos+limit)
        size_t end = *pos + limit;
        for (size_t row = *pos; row < end; ++row) {
            for (size_t col = 0; col < index_sample_block_.columns(); ++col) {
                const auto& column = block.getByName(
                    index_sample_block_.getByPosition(col).name);

                Field value;
                column.column->get(row, value);

                // Update min
                if (value < hyperrectangle_[col].left) {
                    hyperrectangle_[col].left = value;
                }

                // Update max
                if (value > hyperrectangle_[col].right) {
                    hyperrectangle_[col].right = value;
                }
            }
        }

        *pos = end;
    }

private:
    std::string index_name_;
    Block index_sample_block_;
    std::vector<Range> hyperrectangle_;
};

// ==================== CONDITION ====================

class MergeTreeIndexConditionMinMax : public IMergeTreeIndexCondition {
public:
    MergeTreeIndexConditionMinMax(const IndexDescription& index,
                                  const ASTNode* predicate,
                                  const QueryContext& context)
        : index_(index) {

        // Parse WHERE clause and build KeyCondition (RPN-based evaluator)
        key_condition_ = buildKeyCondition(predicate, context);
    }

    bool alwaysUnknownOrTrue() const override {
        return key_condition_.alwaysUnknownOrTrue();
    }

    bool mayBeTrueOnGranule(MergeTreeIndexGranulePtr idx_granule) const override {
        auto* granule = static_cast<MergeTreeIndexGranuleMinMax*>(idx_granule.get());

        // Check if query ranges intersect granule ranges
        return key_condition_.checkInHyperrectangle(
            granule->hyperrectangle_,
            index_.sample_block.getDataTypes());
    }

    std::string getDescription() const override {
        return "minmax index condition";
    }

private:
    IndexDescription index_;
    KeyCondition key_condition_;
};

// ==================== INDEX ====================

class MergeTreeIndexMinMax : public IMergeTreeIndex {
public:
    explicit MergeTreeIndexMinMax(const IndexDescription& index)
        : IMergeTreeIndex(index) {}

    MergeTreeIndexGranulePtr createIndexGranule() const override {
        return std::make_shared<MergeTreeIndexGranuleMinMax>(
            index_.name, index_.sample_block);
    }

    MergeTreeIndexAggregatorPtr createIndexAggregator() const override {
        return std::make_shared<MergeTreeIndexAggregatorMinMax>(
            index_.name, index_.sample_block);
    }

    MergeTreeIndexConditionPtr createIndexCondition(
        const ASTNode* predicate,
        const QueryContext& context) const override {

        return std::make_shared<MergeTreeIndexConditionMinMax>(
            index_, predicate, context);
    }

    std::string getFileExtension() const override {
        return ".idx2";  // Version 2 with Nullable support
    }
};
```

## Set Index

```cpp
/**
 * Set index stores unique values per granule
 *
 * Use for: Equality and IN queries (WHERE col = x OR col IN (...))
 * Storage: Actual unique values (limited by max_rows)
 *
 * Based on: MergeTreeIndexSet
 */

// ==================== GRANULE ====================

class MergeTreeIndexGranuleSet : public IMergeTreeIndexGranule {
public:
    MergeTreeIndexGranuleSet(const std::string& index_name,
                             const Block& index_sample_block,
                             size_t max_rows)
        : index_name_(index_name)
        , index_sample_block_(index_sample_block)
        , max_rows_(max_rows) {}

    void serializeBinary(WriteBuffer& ostr) const override {
        // Write number of rows
        writeVarUInt(block_.rows(), ostr);

        if (block_.rows() > 0) {
            // Write each column's data
            for (size_t i = 0; i < block_.columns(); ++i) {
                const auto& column = block_.getByPosition(i);
                column.type->serializeBinaryBulk(*column.column, ostr, 0, block_.rows());
            }
        }
    }

    void deserializeBinary(ReadBuffer& istr, MergeTreeIndexVersion version) override {
        size_t rows;
        readVarUInt(rows, istr);

        if (rows > 0) {
            MutableColumns columns;
            for (size_t i = 0; i < index_sample_block_.columns(); ++i) {
                const auto& column = index_sample_block_.getByPosition(i);
                auto col = column.type->createColumn();
                column.type->deserializeBinaryBulk(*col, istr, rows, 0);
                columns.push_back(std::move(col));
            }

            block_ = index_sample_block_.cloneWithColumns(std::move(columns));
        }
    }

    bool empty() const override {
        return block_.rows() == 0;
    }

    size_t memoryUsageBytes() const override {
        return block_.bytes();
    }

    // Unique values stored as Block rows
    Block block_;

private:
    std::string index_name_;
    Block index_sample_block_;
    size_t max_rows_;
};

// ==================== AGGREGATOR ====================

class MergeTreeIndexAggregatorSet : public IMergeTreeIndexAggregator {
public:
    MergeTreeIndexAggregatorSet(const std::string& index_name,
                                const Block& index_sample_block,
                                size_t max_rows)
        : index_name_(index_name)
        , index_sample_block_(index_sample_block)
        , max_rows_(max_rows) {

        // Initialize hash set for deduplication
        for (size_t i = 0; i < index_sample_block.columns(); ++i) {
            const auto& column = index_sample_block.getByPosition(i);
            columns_.push_back(column.type->createColumn());
        }
    }

    bool empty() const override {
        return size() == 0;
    }

    size_t size() const {
        return columns_[0]->size();
    }

    MergeTreeIndexGranulePtr getGranuleAndReset() override {
        Block block = index_sample_block_.cloneWithColumns(std::move(columns_));

        auto granule = std::make_shared<MergeTreeIndexGranuleSet>(
            index_name_, index_sample_block_, max_rows_);

        granule->block_ = std::move(block);

        // Reset for next granule
        columns_.clear();
        for (size_t i = 0; i < index_sample_block_.columns(); ++i) {
            const auto& column = index_sample_block_.getByPosition(i);
            columns_.push_back(column.type->createColumn());
        }
        unique_values_.clear();

        return granule;
    }

    void update(const Block& block, size_t* pos, size_t limit) override {
        size_t end = *pos + limit;

        for (size_t row = *pos; row < end; ++row) {
            // Check if already at max capacity
            if (size() >= max_rows_) {
                // Too many unique values - store empty granule
                columns_.clear();
                for (auto& col : columns_) {
                    col->clear();
                }
                unique_values_.clear();
                break;
            }

            // Extract row hash for deduplication
            UInt128 hash = computeHash(block, row);

            // Insert if not seen before
            if (unique_values_.insert(hash).second) {
                for (size_t col = 0; col < index_sample_block_.columns(); ++col) {
                    const auto& column = block.getByName(
                        index_sample_block_.getByPosition(col).name);

                    columns_[col]->insertFrom(*column.column, row);
                }
            }
        }

        *pos = end;
    }

private:
    std::string index_name_;
    Block index_sample_block_;
    size_t max_rows_;

    MutableColumns columns_;
    std::unordered_set<UInt128> unique_values_;  // For deduplication

    UInt128 computeHash(const Block& block, size_t row) const {
        SipHasher hash;
        for (size_t col = 0; col < index_sample_block_.columns(); ++col) {
            const auto& column = block.getByName(
                index_sample_block_.getByPosition(col).name);
            column.column->updateHashWithValue(row, hash);
        }
        return hash.get128();
    }
};

// ==================== CONDITION ====================

class MergeTreeIndexConditionSet : public IMergeTreeIndexCondition {
public:
    MergeTreeIndexConditionSet(const IndexDescription& index,
                               const ASTNode* predicate,
                               const QueryContext& context,
                               size_t max_rows)
        : index_(index)
        , max_rows_(max_rows) {

        // Extract key columns and build expression evaluator
        key_columns_ = extractKeyColumns(predicate, index);
        actions_ = buildActions(predicate, context, key_columns_);
    }

    bool alwaysUnknownOrTrue() const override {
        return actions_ == nullptr || key_columns_.empty();
    }

    bool mayBeTrueOnGranule(MergeTreeIndexGranulePtr idx_granule) const override {
        auto* granule = static_cast<MergeTreeIndexGranuleSet*>(idx_granule.get());

        if (granule->empty()) {
            // Empty granule means too many unique values - cannot skip
            return true;
        }

        // Check if any queried value exists in granule's set
        return checkInSet(granule->block_);
    }

    std::string getDescription() const override {
        return "set index condition";
    }

private:
    IndexDescription index_;
    size_t max_rows_;

    std::unordered_set<std::string> key_columns_;
    ExpressionActionsPtr actions_;

    bool checkInSet(const Block& granule_block) const {
        // Evaluate expression against granule values
        // Returns true if ANY row in granule_block satisfies condition
        Block block = granule_block;
        actions_->execute(block);

        const auto& result_column = block.getByName(actions_->getOutputColumnName());
        const auto& filter = typeid_cast<const ColumnUInt8&>(*result_column.column);

        // If any value is 1, granule may contain matches
        for (size_t i = 0; i < filter.size(); ++i) {
            if (filter.getData()[i]) {
                return true;
            }
        }

        return false;
    }
};

// ==================== INDEX ====================

class MergeTreeIndexSet : public IMergeTreeIndex {
public:
    MergeTreeIndexSet(const IndexDescription& index, size_t max_rows)
        : IMergeTreeIndex(index)
        , max_rows_(max_rows) {}

    MergeTreeIndexGranulePtr createIndexGranule() const override {
        return std::make_shared<MergeTreeIndexGranuleSet>(
            index_.name, index_.sample_block, max_rows_);
    }

    MergeTreeIndexAggregatorPtr createIndexAggregator() const override {
        return std::make_shared<MergeTreeIndexAggregatorSet>(
            index_.name, index_.sample_block, max_rows_);
    }

    MergeTreeIndexConditionPtr createIndexCondition(
        const ASTNode* predicate,
        const QueryContext& context) const override {

        return std::make_shared<MergeTreeIndexConditionSet>(
            index_, predicate, context, max_rows_);
    }

private:
    size_t max_rows_;
};
```

## BloomFilter Index

```cpp
/**
 * BloomFilter index for probabilistic membership test
 *
 * Use for: Equality checks (WHERE col = x)
 * Storage: bits_per_row * rows per granule (e.g., 8 bits/row * 8192 rows = 8KB)
 *
 * Based on: MergeTreeIndexBloomFilter
 */

// ==================== GRANULE ====================

class MergeTreeIndexGranuleBloomFilter : public IMergeTreeIndexGranule {
public:
    MergeTreeIndexGranuleBloomFilter(size_t bits_per_row,
                                     size_t hash_functions,
                                     size_t index_columns)
        : bits_per_row_(bits_per_row)
        , hash_functions_(hash_functions) {

        bloom_filters_.resize(index_columns);
    }

    void serializeBinary(WriteBuffer& ostr) const override {
        writeVarUInt(total_rows_, ostr);

        if (total_rows_ > 0) {
            for (const auto& filter : bloom_filters_) {
                size_t bytes = (bits_per_row_ * total_rows_ + 7) / 8;
                ostr.write(reinterpret_cast<const char*>(filter->getData()), bytes);
            }
        }
    }

    void deserializeBinary(ReadBuffer& istr, MergeTreeIndexVersion version) override {
        readVarUInt(total_rows_, istr);

        if (total_rows_ > 0) {
            for (auto& filter : bloom_filters_) {
                filter = std::make_shared<BloomFilter>(bits_per_row_ * total_rows_, hash_functions_);

                size_t bytes = (bits_per_row_ * total_rows_ + 7) / 8;
                istr.read(reinterpret_cast<char*>(filter->getData()), bytes);
            }
        }
    }

    bool empty() const override {
        return total_rows_ == 0;
    }

    size_t memoryUsageBytes() const override {
        if (total_rows_ == 0) return 0;

        size_t bytes_per_filter = (bits_per_row_ * total_rows_ + 7) / 8;
        return bytes_per_filter * bloom_filters_.size();
    }

    const std::vector<BloomFilterPtr>& getFilters() const {
        return bloom_filters_;
    }

private:
    size_t bits_per_row_;
    size_t hash_functions_;
    size_t total_rows_{0};

    std::vector<BloomFilterPtr> bloom_filters_;  // One per column
};

// ==================== AGGREGATOR ====================

class MergeTreeIndexAggregatorBloomFilter : public IMergeTreeIndexAggregator {
public:
    MergeTreeIndexAggregatorBloomFilter(size_t bits_per_row,
                                        size_t hash_functions,
                                        const std::vector<std::string>& column_names)
        : bits_per_row_(bits_per_row)
        , hash_functions_(hash_functions)
        , column_names_(column_names) {

        column_hashes_.resize(column_names.size());
    }

    bool empty() const override {
        return total_rows_ == 0;
    }

    MergeTreeIndexGranulePtr getGranuleAndReset() override {
        auto granule = std::make_shared<MergeTreeIndexGranuleBloomFilter>(
            bits_per_row_, hash_functions_, column_names_.size());

        // Build bloom filters from accumulated hashes
        granule->total_rows_ = total_rows_;

        for (size_t col = 0; col < column_hashes_.size(); ++col) {
            auto filter = std::make_shared<BloomFilter>(
                bits_per_row_ * total_rows_, hash_functions_);

            for (UInt64 hash : column_hashes_[col]) {
                filter->add(hash);
            }

            granule->bloom_filters_[col] = std::move(filter);
        }

        // Reset
        for (auto& hashes : column_hashes_) {
            hashes.clear();
        }
        total_rows_ = 0;

        return granule;
    }

    void update(const Block& block, size_t* pos, size_t limit) override {
        size_t end = *pos + limit;

        for (size_t row = *pos; row < end; ++row) {
            for (size_t col = 0; col < column_names_.size(); ++col) {
                const auto& column = block.getByName(column_names_[col]);

                // Compute hash of value
                UInt64 hash = column.type->hash(*column.column, row);

                column_hashes_[col].insert(hash);
            }

            ++total_rows_;
        }

        *pos = end;
    }

private:
    size_t bits_per_row_;
    size_t hash_functions_;
    std::vector<std::string> column_names_;

    std::vector<std::unordered_set<UInt64>> column_hashes_;
    size_t total_rows_{0};
};

// ==================== CONDITION ====================

class MergeTreeIndexConditionBloomFilter : public IMergeTreeIndexCondition {
public:
    MergeTreeIndexConditionBloomFilter(const IndexDescription& index,
                                       const ASTNode* predicate,
                                       const QueryContext& context,
                                       size_t hash_functions)
        : index_(index)
        , hash_functions_(hash_functions) {

        // Build RPN from WHERE clause
        rpn_ = buildRPN(predicate, context);
    }

    bool alwaysUnknownOrTrue() const override {
        return rpn_.empty();
    }

    bool mayBeTrueOnGranule(MergeTreeIndexGranulePtr idx_granule) const override {
        auto* granule = static_cast<MergeTreeIndexGranuleBloomFilter*>(idx_granule.get());

        if (granule->empty()) {
            return true;
        }

        // Evaluate RPN against bloom filters
        return evaluateRPN(rpn_, granule->getFilters());
    }

    std::string getDescription() const override {
        return "bloom_filter index condition";
    }

private:
    enum class Function {
        EQUALS,
        IN,
        NOT_IN,
        AND,
        OR,
        NOT,
        ALWAYS_TRUE,
        ALWAYS_FALSE
    };

    struct RPNElement {
        Function function;
        std::vector<std::pair<size_t, ColumnPtr>> predicate;  // Column index + values
    };

    IndexDescription index_;
    size_t hash_functions_;
    std::vector<RPNElement> rpn_;

    bool evaluateRPN(const std::vector<RPNElement>& rpn,
                    const std::vector<BloomFilterPtr>& filters) const {
        std::stack<bool> stack;

        for (const auto& element : rpn) {
            switch (element.function) {
                case Function::EQUALS: {
                    // Check if value exists in bloom filter
                    bool may_be_true = false;
                    for (const auto& [col_idx, values] : element.predicate) {
                        for (size_t row = 0; row < values->size(); ++row) {
                            UInt64 hash = computeHash(values, row);
                            if (filters[col_idx]->contains(hash)) {
                                may_be_true = true;
                                break;
                            }
                        }
                    }
                    stack.push(may_be_true);
                    break;
                }

                case Function::AND: {
                    bool right = stack.top(); stack.pop();
                    bool left = stack.top(); stack.pop();
                    stack.push(left && right);
                    break;
                }

                case Function::OR: {
                    bool right = stack.top(); stack.pop();
                    bool left = stack.top(); stack.pop();
                    stack.push(left || right);
                    break;
                }

                case Function::NOT: {
                    bool operand = stack.top(); stack.pop();
                    stack.push(!operand);
                    break;
                }

                case Function::ALWAYS_TRUE:
                    stack.push(true);
                    break;

                case Function::ALWAYS_FALSE:
                    stack.push(false);
                    break;

                default:
                    stack.push(true);  // Unknown - cannot skip
            }
        }

        return stack.empty() ? true : stack.top();
    }
};

// ==================== INDEX ====================

class MergeTreeIndexBloomFilter : public IMergeTreeIndex {
public:
    MergeTreeIndexBloomFilter(const IndexDescription& index,
                              size_t bits_per_row,
                              size_t hash_functions)
        : IMergeTreeIndex(index)
        , bits_per_row_(bits_per_row)
        , hash_functions_(hash_functions) {}

    MergeTreeIndexGranulePtr createIndexGranule() const override {
        return std::make_shared<MergeTreeIndexGranuleBloomFilter>(
            bits_per_row_, hash_functions_, index_.columns.size());
    }

    MergeTreeIndexAggregatorPtr createIndexAggregator() const override {
        return std::make_shared<MergeTreeIndexAggregatorBloomFilter>(
            bits_per_row_, hash_functions_, index_.columns);
    }

    MergeTreeIndexConditionPtr createIndexCondition(
        const ASTNode* predicate,
        const QueryContext& context) const override {

        return std::make_shared<MergeTreeIndexConditionBloomFilter>(
            index_, predicate, context, hash_functions_);
    }

private:
    size_t bits_per_row_;
    size_t hash_functions_;
};
```

## Index Factory

```cpp
/**
 * Factory for creating skip indexes
 *
 * Based on: MergeTreeIndexFactory
 */
class MergeTreeIndexFactory {
public:
    static MergeTreeIndexFactory& instance() {
        static MergeTreeIndexFactory factory;
        return factory;
    }

    using Creator = std::function<MergeTreeIndexPtr(const IndexDescription&)>;
    using Validator = std::function<void(const IndexDescription&)>;

    /**
     * Register index type
     */
    void registerCreator(const std::string& type, Creator creator) {
        creators_[type] = std::move(creator);
    }

    void registerValidator(const std::string& type, Validator validator) {
        validators_[type] = std::move(validator);
    }

    /**
     * Create index from description
     */
    MergeTreeIndexPtr create(const IndexDescription& index) const {
        // Validate
        auto validator_it = validators_.find(index.type);
        if (validator_it != validators_.end()) {
            validator_it->second(index);
        }

        // Create
        auto creator_it = creators_.find(index.type);
        if (creator_it == creators_.end()) {
            throw std::invalid_argument("Unknown index type: " + index.type);
        }

        return creator_it->second(index);
    }

private:
    MergeTreeIndexFactory() {
        // Register built-in indexes
        registerMinMax();
        registerSet();
        registerBloomFilter();
    }

    void registerMinMax() {
        registerCreator("minmax", [](const IndexDescription& index) {
            return std::make_shared<MergeTreeIndexMinMax>(index);
        });

        registerValidator("minmax", [](const IndexDescription& index) {
            for (const auto& col : index.sample_block) {
                if (!col.type->isComparable()) {
                    throw std::invalid_argument("MinMax index requires comparable types");
                }
            }
        });
    }

    void registerSet() {
        registerCreator("set", [](const IndexDescription& index) {
            size_t max_rows = 8192;  // Default
            auto it = index.parameters.find("max_rows");
            if (it != index.parameters.end()) {
                max_rows = std::stoull(it->second);
            }
            return std::make_shared<MergeTreeIndexSet>(index, max_rows);
        });

        registerValidator("set", [](const IndexDescription& index) {
            // No specific requirements
        });
    }

    void registerBloomFilter() {
        registerCreator("bloom_filter", [](const IndexDescription& index) {
            size_t bits_per_row = 8;  // Default: 8 bits per row
            size_t hash_functions = 3;  // Default: 3 hash functions

            auto bits_it = index.parameters.find("bits_per_row");
            if (bits_it != index.parameters.end()) {
                bits_per_row = std::stoull(bits_it->second);
            }

            auto hash_it = index.parameters.find("hash_functions");
            if (hash_it != index.parameters.end()) {
                hash_functions = std::stoull(hash_it->second);
            }

            return std::make_shared<MergeTreeIndexBloomFilter>(
                index, bits_per_row, hash_functions);
        });

        registerValidator("bloom_filter", [](const IndexDescription& index) {
            // Bloom filters work on most types
        });
    }

    std::map<std::string, Creator> creators_;
    std::map<std::string, Validator> validators_;
};
```

## Usage Example

```cpp
// ==================== Index Creation ====================

// Create index descriptions
IndexDescription minmax_idx;
minmax_idx.name = "time_idx";
minmax_idx.type = "minmax";
minmax_idx.columns = {"timestamp"};
minmax_idx.granularity = 1;  // One index granule per data granule

IndexDescription set_idx;
set_idx.name = "status_idx";
set_idx.type = "set";
set_idx.columns = {"status_code"};
set_idx.granularity = 4;  // One index granule per 4 data granules
set_idx.parameters["max_rows"] = "100";  // Max 100 unique values

// Create indexes
auto& factory = MergeTreeIndexFactory::instance();
auto time_index = factory.create(minmax_idx);
auto status_index = factory.create(set_idx);

// ==================== Write Path ====================

// Create aggregators
auto time_agg = time_index->createIndexAggregator();
auto status_agg = status_index->createIndexAggregator();

// Process data granule
Block block = ...;  // 8192 rows
size_t pos = 0;

// Accumulate index data
time_agg->update(block, &pos, block.rows());
status_agg->update(block, &pos, block.rows());

// When granularity boundary reached
if (granule_count == time_index->getGranularity()) {
    // Serialize index granule
    auto granule = time_agg->getGranuleAndReset();
    granule->serializeBinary(index_file_out);
}

// ==================== Read Path ====================

// Create conditions from query
ASTNode* where_clause = parseQuery("WHERE timestamp >= 1000 AND timestamp < 2000");
auto time_condition = time_index->createIndexCondition(where_clause, context);

// Check if index can help
if (time_condition->alwaysUnknownOrTrue()) {
    // Cannot use this index
} else {
    // Filter granules
    std::vector<bool> granule_matches;

    for (size_t granule_id = 0; granule_id < total_granules; ++granule_id) {
        // Deserialize index granule
        auto granule = time_index->createIndexGranule();
        granule->deserializeBinary(index_file_in, version);

        // Check if can skip
        bool may_match = time_condition->mayBeTrueOnGranule(granule);
        granule_matches[granule_id] = may_match;
    }

    // Read only matching granules
    for (size_t i = 0; i < total_granules; ++i) {
        if (granule_matches[i]) {
            readDataGranule(i);
        } else {
            // Skip this granule!
        }
    }
}
```

## Design Notes

### Index Granularity vs Data Granularity
- **Data granularity**: 8192 rows (default)
- **Index granularity**: Configurable multiplier (default: 1)
- Example: Index granularity = 4 → one index granule covers 4 * 8192 = 32768 rows

### False Positives
- All indexes can have false positives (may return true when actually false)
- MinMax: Query range partially overlaps granule range
- Set: Granule exceeded max_rows (stores empty granule)
- BloomFilter: Hash collision

### File Format
```
<segment_name>/
  skp_idx_<name>.idx   - Index granule data
  skp_idx_<name>.mrk2  - Marks (file offsets to granules)
```

### Performance Characteristics
| Index Type | Storage per Granule | Build Cost | Filter Cost |
|------------|---------------------|------------|-------------|
| MinMax | 2 * sizeof(value) per col | O(n) | O(1) |
| Set | sizeof(unique values) | O(n log n) | O(m) where m = query values |
| BloomFilter | bits_per_row * rows | O(n * k) | O(m * k) where k = hash functions |

### Best Practices
1. **MinMax**: Use for range queries on sorted/clustered columns
2. **Set**: Use for low-cardinality columns (< 1000 unique values)
3. **BloomFilter**: Use for high-cardinality equality checks
4. **Granularity**: Higher values reduce storage but decrease selectivity

---

**Design Status**: Complete ✅
**Next Module**: 12_STORAGE_TIERS.md (Hot/Warm/Cold lifecycle management)
