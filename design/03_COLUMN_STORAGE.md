# Column Storage Design
## Based on ClickHouse Column System

Source references:
- ClickHouse IColumn: `/home/ubuntu/opensearch_warmroom/ClickHouse/src/Columns/IColumn.h`
- ColumnVector: `/home/ubuntu/opensearch_warmroom/ClickHouse/src/Columns/ColumnVector.h`
- ColumnString: `/home/ubuntu/opensearch_warmroom/ClickHouse/src/Columns/ColumnString.h`
- IDataType: `/home/ubuntu/opensearch_warmroom/ClickHouse/src/DataTypes/IDataType.h`
- ISerialization: `/home/ubuntu/opensearch_warmroom/ClickHouse/src/DataTypes/Serializations/ISerialization.h`
- MergeTreeDataPart: `/home/ubuntu/opensearch_warmroom/ClickHouse/src/Storages/MergeTree/IMergeTreeDataPart.h`

## Overview

Column storage provides ClickHouse-style columnar organization:
- **IColumn**: In-memory column interface with COW semantics
- **IDataType**: Type system with serialization factories
- **ISerialization**: Binary serialization for each type
- **MergeTreeDataPart**: Wide/Compact on-disk formats
- **Granule-based**: 8192-row chunks with marks for random access
- **Type partitioning**: Dynamic fields split by type

## IColumn Interface (In-Memory)

```cpp
/**
 * IColumn is the in-memory representation of a column.
 *
 * COW (Copy-On-Write) semantics:
 * - Columns are immutable by default
 * - mutate() creates writable copy if shared
 * - Efficient for caching and sharing
 *
 * Based on: ClickHouse IColumn
 */
class IColumn : public std::enable_shared_from_this<IColumn> {
public:
    using Ptr = std::shared_ptr<IColumn>;
    using MutablePtr = std::shared_ptr<IColumn>;

    virtual ~IColumn() = default;

    // ==================== Type Information ====================

    /**
     * Column type name (e.g., "UInt32", "String", "Array(String)")
     */
    virtual std::string getName() const = 0;

    /**
     * Get type index for fast comparison
     */
    virtual TypeIndex getDataType() const = 0;

    // ==================== Size ====================

    /**
     * Number of rows in column
     */
    virtual size_t size() const = 0;

    /**
     * Allocated memory in bytes
     */
    virtual size_t byteSize() const = 0;

    /**
     * Allocated bytes for elements [offset, offset+limit)
     */
    virtual size_t byteSizeAt(size_t offset, size_t limit) const {
        return byteSize();  // Default: no per-element info
    }

    // ==================== Data Access ====================

    /**
     * Get element as Field (variant type)
     */
    virtual Field operator[](size_t n) const = 0;

    /**
     * Get element at position
     */
    virtual void get(size_t n, Field& res) const = 0;

    /**
     * Get raw data pointer (if column is contiguous)
     * Returns nullptr if not applicable
     */
    virtual const char* getRawData() const {
        return nullptr;
    }

    /**
     * Is column numeric and contiguous?
     */
    virtual bool isNumeric() const {
        return false;
    }

    // ==================== Insertion ====================

    /**
     * Insert value from Field
     */
    virtual void insert(const Field& x) = 0;

    /**
     * Insert value from another column
     */
    virtual void insertFrom(const IColumn& src, size_t n) = 0;

    /**
     * Insert range from another column
     */
    virtual void insertRangeFrom(const IColumn& src, size_t start, size_t length) = 0;

    /**
     * Insert default value
     */
    virtual void insertDefault() = 0;

    /**
     * Insert multiple copies of default
     */
    virtual void insertManyDefaults(size_t length) = 0;

    /**
     * Pop last element
     */
    virtual void popBack(size_t n) = 0;

    // ==================== Filtering & Slicing ====================

    /**
     * Create column with filtered rows
     * @param filt Bit mask (1 = keep row)
     * @param resultSizeHint Expected result size (-1 if unknown)
     */
    virtual Ptr filter(const Filter& filt, ssize_t resultSizeHint) const = 0;

    /**
     * Create column with specified rows
     * @param indices Row indices to keep
     */
    virtual Ptr index(const IColumn& indices, size_t limit) const = 0;

    /**
     * Permute rows
     */
    virtual Ptr permute(const Permutation& perm, size_t limit) const = 0;

    /**
     * Extract range [offset, offset+length)
     */
    virtual Ptr cut(size_t offset, size_t length) const = 0;

    /**
     * Replicate rows: row i repeated offsets[i+1]-offsets[i] times
     */
    virtual Ptr replicate(const Offsets& offsets) const = 0;

    // ==================== Aggregation ====================

    /**
     * Update hash for each row
     */
    virtual void updateHashWithValue(size_t n, SipHash& hash) const = 0;

    /**
     * Update WeakHash32 for block hash
     */
    virtual void updateWeakHash32(WeakHash32& hash) const = 0;

    // ==================== Comparison ====================

    /**
     * Compare row n with row m in rhs
     * @return <0 if less, 0 if equal, >0 if greater
     */
    virtual int compareAt(size_t n, size_t m, const IColumn& rhs,
                         int nan_direction_hint) const = 0;

    /**
     * Get permutation that sorts column
     */
    virtual void getPermutation(bool reverse, size_t limit, int nan_direction_hint,
                               Permutation& res) const = 0;

    // ==================== COW Operations ====================

    /**
     * Assume mutable (for in-place modifications)
     * Use after creating new column or calling mutate()
     */
    MutablePtr assumeMutable() {
        return shared_from_this();
    }

    /**
     * Create mutable copy if shared, otherwise return this
     */
    MutablePtr mutate() {
        if (use_count() > 1) {
            return cloneResized(size());
        }
        return assumeMutable();
    }

    // ==================== Cloning ====================

    /**
     * Deep copy
     */
    virtual MutablePtr clone() const = 0;

    /**
     * Clone and resize
     */
    virtual MutablePtr cloneResized(size_t size) const = 0;

    /**
     * Clone empty column (same type, zero size)
     */
    virtual MutablePtr cloneEmpty() const = 0;

    // ==================== Concatenation ====================

    /**
     * Insert data from multiple columns at once
     */
    virtual void insertMany(const std::vector<const IColumn*>& columns,
                           const std::vector<size_t>& offsets,
                           const std::vector<size_t>& lengths) {
        // Default: iterate
        for (size_t i = 0; i < columns.size(); ++i) {
            insertRangeFrom(*columns[i], offsets[i], lengths[i]);
        }
    }

    // ==================== Utilities ====================

    /**
     * Check if column is ColumnConst
     */
    virtual bool isConst() const {
        return false;
    }

    /**
     * Check if column is ColumnNullable
     */
    virtual bool isNullable() const {
        return false;
    }

    /**
     * Check if column can be inside Nullable
     */
    virtual bool canBeInsideNullable() const {
        return true;
    }

    /**
     * Is column a ColumnString?
     */
    virtual bool isCollationSupported() const {
        return false;
    }

private:
    /**
     * Reference count (for COW)
     */
    long use_count() const {
        return shared_from_this().use_count();
    }
};
```

## Concrete Column Types

### ColumnVector<T> (Numeric Types)

```cpp
/**
 * Column for fixed-size numeric types.
 * Uses std::vector<T> with POD_ARRAY for efficient reallocation.
 *
 * Based on: ClickHouse ColumnVector
 */
template <typename T>
class ColumnVector : public IColumn {
public:
    using Self = ColumnVector<T>;
    using value_type = T;
    using Container = PODArray<T>;  // Efficient vector for POD types

    // ==================== Construction ====================

    ColumnVector() = default;

    explicit ColumnVector(size_t n) : data_(n) {}

    ColumnVector(size_t n, const T& value) : data_(n, value) {}

    // ==================== Type ====================

    std::string getName() const override {
        return std::string("Vector(") + TypeName<T>::get() + ")";
    }

    TypeIndex getDataType() const override;

    // ==================== Size ====================

    size_t size() const override {
        return data_.size();
    }

    size_t byteSize() const override {
        return data_.size() * sizeof(T);
    }

    // ==================== Data Access ====================

    Field operator[](size_t n) const override {
        return Field(data_[n]);
    }

    void get(size_t n, Field& res) const override {
        res = Field(data_[n]);
    }

    /**
     * Direct access to data
     */
    T& getElement(size_t n) {
        return data_[n];
    }

    const T& getElement(size_t n) const {
        return data_[n];
    }

    Container& getData() {
        return data_;
    }

    const Container& getData() const {
        return data_;
    }

    const char* getRawData() const override {
        return reinterpret_cast<const char*>(data_.data());
    }

    bool isNumeric() const override {
        return true;
    }

    // ==================== Insertion ====================

    void insert(const Field& x) override {
        data_.push_back(x.get<T>());
    }

    void insertFrom(const IColumn& src, size_t n) override {
        const Self& src_vec = assert_cast<const Self&>(src);
        data_.push_back(src_vec.getData()[n]);
    }

    void insertRangeFrom(const IColumn& src, size_t start, size_t length) override {
        const Self& src_vec = assert_cast<const Self&>(src);
        const auto& src_data = src_vec.getData();

        size_t old_size = data_.size();
        data_.resize(old_size + length);
        std::memcpy(&data_[old_size], &src_data[start], length * sizeof(T));
    }

    void insertDefault() override {
        data_.push_back(T());
    }

    void insertManyDefaults(size_t length) override {
        size_t old_size = data_.size();
        data_.resize(old_size + length);
        std::memset(&data_[old_size], 0, length * sizeof(T));
    }

    void popBack(size_t n) override {
        data_.resize(data_.size() - n);
    }

    // ==================== Filtering ====================

    Ptr filter(const Filter& filt, ssize_t result_size_hint) const override {
        size_t count = filt.size();
        if (count != size()) {
            throw Exception("Size of filter doesn't match column size");
        }

        if (result_size_hint < 0) {
            result_size_hint = countBytesInFilter(filt);
        }

        auto res = Self::create();
        res->getData().reserve(result_size_hint);

        for (size_t i = 0; i < count; ++i) {
            if (filt[i]) {
                res->getData().push_back(data_[i]);
            }
        }

        return res;
    }

    // ==================== Comparison ====================

    int compareAt(size_t n, size_t m, const IColumn& rhs,
                 int nan_direction_hint) const override {
        const Self& rhs_vec = assert_cast<const Self&>(rhs);

        if constexpr (std::is_floating_point_v<T>) {
            // Handle NaN
            bool lhs_is_nan = std::isnan(data_[n]);
            bool rhs_is_nan = std::isnan(rhs_vec.data_[m]);

            if (lhs_is_nan || rhs_is_nan) {
                if (lhs_is_nan && rhs_is_nan) return 0;
                return lhs_is_nan ? nan_direction_hint : -nan_direction_hint;
            }
        }

        if (data_[n] < rhs_vec.data_[m]) return -1;
        if (data_[n] > rhs_vec.data_[m]) return 1;
        return 0;
    }

    // ==================== Cloning ====================

    MutablePtr clone() const override {
        auto res = Self::create(data_.size());
        res->getData() = data_;
        return res;
    }

    MutablePtr cloneResized(size_t size) const override {
        auto res = Self::create();
        if (size > 0) {
            size_t count = std::min(this->size(), size);
            res->getData().resize(size);
            std::memcpy(res->getData().data(), data_.data(), count * sizeof(T));
        }
        return res;
    }

    MutablePtr cloneEmpty() const override {
        return Self::create();
    }

    // ==================== Factory ====================

    static Ptr create() {
        return std::make_shared<Self>();
    }

    static Ptr create(size_t n) {
        return std::make_shared<Self>(n);
    }

    static Ptr create(size_t n, const T& value) {
        return std::make_shared<Self>(n, value);
    }

private:
    Container data_;
};

// Common numeric column types
using ColumnUInt8 = ColumnVector<uint8_t>;
using ColumnUInt16 = ColumnVector<uint16_t>;
using ColumnUInt32 = ColumnVector<uint32_t>;
using ColumnUInt64 = ColumnVector<uint64_t>;
using ColumnInt8 = ColumnVector<int8_t>;
using ColumnInt16 = ColumnVector<int16_t>;
using ColumnInt32 = ColumnVector<int32_t>;
using ColumnInt64 = ColumnVector<int64_t>;
using ColumnFloat32 = ColumnVector<float>;
using ColumnFloat64 = ColumnVector<double>;
```

### ColumnString (Variable-Length Strings)

```cpp
/**
 * Column for variable-length strings.
 *
 * Storage: offsets array + chars buffer
 * - offsets[i] = end position of string i in chars
 * - string i = chars[offsets[i-1] .. offsets[i])
 *
 * Based on: ClickHouse ColumnString
 */
class ColumnString : public IColumn {
public:
    using Chars = PODArray<uint8_t>;
    using Offsets = PODArray<uint64_t>;

    // ==================== Type ====================

    std::string getName() const override {
        return "String";
    }

    TypeIndex getDataType() const override {
        return TypeIndex::String;
    }

    // ==================== Size ====================

    size_t size() const override {
        return offsets_.size();
    }

    size_t byteSize() const override {
        return chars_.size() + offsets_.size() * sizeof(uint64_t);
    }

    // ==================== Data Access ====================

    Field operator[](size_t n) const override {
        size_t offset = offsetAt(n);
        size_t size = sizeAt(n);
        return Field(std::string(reinterpret_cast<const char*>(&chars_[offset]), size));
    }

    /**
     * Get string at index n
     */
    std::string_view getDataAt(size_t n) const {
        size_t offset = offsetAt(n);
        size_t size = sizeAt(n);
        return std::string_view(reinterpret_cast<const char*>(&chars_[offset]), size);
    }

    /**
     * Insert string
     */
    void insertData(const char* pos, size_t length) {
        size_t old_size = chars_.size();
        chars_.resize(old_size + length);
        std::memcpy(&chars_[old_size], pos, length);
        offsets_.push_back(chars_.size());
    }

    // ==================== Direct Access ====================

    Chars& getChars() {
        return chars_;
    }

    const Chars& getChars() const {
        return chars_;
    }

    Offsets& getOffsets() {
        return offsets_;
    }

    const Offsets& getOffsets() const {
        return offsets_;
    }

    // ==================== Insertion ====================

    void insert(const Field& x) override {
        const std::string& s = x.get<std::string>();
        insertData(s.data(), s.size());
    }

    void insertFrom(const IColumn& src, size_t n) override {
        const ColumnString& src_string = assert_cast<const ColumnString&>(src);
        std::string_view sv = src_string.getDataAt(n);
        insertData(sv.data(), sv.size());
    }

    void insertRangeFrom(const IColumn& src, size_t start, size_t length) override {
        const ColumnString& src_string = assert_cast<const ColumnString&>(src);

        if (length == 0) return;

        size_t nested_offset = src_string.offsetAt(start);
        size_t nested_length = src_string.offsets_[start + length - 1] - nested_offset;

        size_t old_chars_size = chars_.size();
        chars_.resize(old_chars_size + nested_length);
        std::memcpy(&chars_[old_chars_size], &src_string.chars_[nested_offset], nested_length);

        offsets_.reserve(offsets_.size() + length);
        for (size_t i = 0; i < length; ++i) {
            offsets_.push_back(old_chars_size + src_string.sizeAt(start + i));
            old_chars_size += src_string.sizeAt(start + i);
        }
    }

    // ==================== Comparison ====================

    int compareAt(size_t n, size_t m, const IColumn& rhs,
                 int /*nan_direction_hint*/) const override {
        const ColumnString& rhs_string = assert_cast<const ColumnString&>(rhs);

        std::string_view lhs_sv = getDataAt(n);
        std::string_view rhs_sv = rhs_string.getDataAt(m);

        return lhs_sv.compare(rhs_sv);
    }

    // ==================== Cloning ====================

    MutablePtr clone() const override {
        auto res = ColumnString::create();
        res->chars_ = chars_;
        res->offsets_ = offsets_;
        return res;
    }

    static Ptr create() {
        return std::make_shared<ColumnString>();
    }

private:
    uint64_t offsetAt(size_t i) const {
        return i == 0 ? 0 : offsets_[i - 1];
    }

    size_t sizeAt(size_t i) const {
        return i == 0 ? offsets_[0] : (offsets_[i] - offsets_[i - 1]);
    }

    Chars chars_;      // Concatenated string data
    Offsets offsets_;  // End positions
};
```

### ColumnArray

```cpp
/**
 * Column for arrays of any type.
 *
 * Storage: offsets + nested column
 * - offsets[i] = end position of array i in nested column
 * - array i = nested[offsets[i-1] .. offsets[i])
 *
 * Based on: ClickHouse ColumnArray
 */
class ColumnArray : public IColumn {
public:
    using Offsets = ColumnVector<uint64_t>::Container;

    ColumnArray(MutablePtr data)
        : data_(std::move(data)) {}

    ColumnArray(MutablePtr data, MutablePtr offsets)
        : data_(std::move(data))
        , offsets_(std::move(offsets)) {}

    std::string getName() const override {
        return "Array(" + data_->getName() + ")";
    }

    size_t size() const override {
        return getOffsets().size();
    }

    Field operator[](size_t n) const override {
        size_t offset = offsetAt(n);
        size_t array_size = sizeAt(n);

        Array res(array_size);
        for (size_t i = 0; i < array_size; ++i) {
            res[i] = (*data_)[offset + i];
        }

        return Field(res);
    }

    /**
     * Get nested column data
     */
    IColumn& getData() {
        return *data_;
    }

    const IColumn& getData() const {
        return *data_;
    }

    /**
     * Get offsets
     */
    Offsets& getOffsets() {
        return assert_cast<ColumnVector<uint64_t>&>(*offsets_).getData();
    }

    const Offsets& getOffsets() const {
        return assert_cast<const ColumnVector<uint64_t>&>(*offsets_).getData();
    }

    static Ptr create(MutablePtr data) {
        return std::make_shared<ColumnArray>(std::move(data));
    }

private:
    uint64_t offsetAt(size_t i) const {
        const auto& offs = getOffsets();
        return i == 0 ? 0 : offs[i - 1];
    }

    size_t sizeAt(size_t i) const {
        const auto& offs = getOffsets();
        return i == 0 ? offs[0] : (offs[i] - offs[i - 1]);
    }

    MutablePtr data_;      // Nested column (flattened arrays)
    MutablePtr offsets_;   // Array boundaries (ColumnUInt64)
};
```

### ColumnNullable

```cpp
/**
 * Column wrapper for nullable types.
 *
 * Storage: null_map + nested column
 * - null_map[i] = 1 if row i is NULL
 * - nested[i] = value if not NULL (undefined if NULL)
 *
 * Based on: ClickHouse ColumnNullable
 */
class ColumnNullable : public IColumn {
public:
    ColumnNullable(MutablePtr nested, MutablePtr null_map)
        : nested_(std::move(nested))
        , null_map_(std::move(null_map)) {}

    std::string getName() const override {
        return "Nullable(" + nested_->getName() + ")";
    }

    bool isNullable() const override {
        return true;
    }

    size_t size() const override {
        return getNullMapData().size();
    }

    Field operator[](size_t n) const override {
        if (isNullAt(n)) {
            return Field();  // NULL
        }
        return (*nested_)[n];
    }

    /**
     * Check if row is NULL
     */
    bool isNullAt(size_t n) const {
        return getNullMapData()[n] != 0;
    }

    /**
     * Get nested column (non-NULL data)
     */
    IColumn& getNestedColumn() {
        return *nested_;
    }

    const IColumn& getNestedColumn() const {
        return *nested_;
    }

    /**
     * Get null map
     */
    ColumnUInt8& getNullMapColumn() {
        return assert_cast<ColumnUInt8&>(*null_map_);
    }

    const ColumnUInt8& getNullMapColumn() const {
        return assert_cast<const ColumnUInt8&>(*null_map_);
    }

    const ColumnUInt8::Container& getNullMapData() const {
        return getNullMapColumn().getData();
    }

    static Ptr create(MutablePtr nested, MutablePtr null_map) {
        return std::make_shared<ColumnNullable>(std::move(nested), std::move(null_map));
    }

private:
    MutablePtr nested_;     // Actual data
    MutablePtr null_map_;   // NULL flags (ColumnUInt8)
};
```

## IDataType (Type System)

```cpp
/**
 * IDataType represents a data type.
 *
 * Responsibilities:
 * - Create ISerialization for binary I/O
 * - Create default IColumn
 * - Type equality and conversion
 *
 * Based on: ClickHouse IDataType
 */
class IDataType {
public:
    virtual ~IDataType() = default;

    /**
     * Type name (e.g., "UInt32", "String", "Array(String)")
     */
    virtual std::string getName() const = 0;

    /**
     * Type index for fast comparison
     */
    virtual TypeIndex getTypeId() const = 0;

    /**
     * Create serialization for this type
     */
    virtual SerializationPtr getDefaultSerialization() const = 0;

    /**
     * Create default column
     */
    virtual MutableColumnPtr createColumn() const = 0;

    /**
     * Create column with constant value
     */
    virtual ColumnPtr createColumnConst(size_t size, const Field& field) const;

    /**
     * Create column with default values
     */
    virtual ColumnPtr createColumnConstWithDefaultValue(size_t size) const;

    /**
     * Get default value
     */
    virtual Field getDefault() const = 0;

    /**
     * Check if type equals another
     */
    virtual bool equals(const IDataType& rhs) const = 0;

    /**
     * Can this type be inside Nullable?
     */
    virtual bool canBeInsideNullable() const {
        return true;
    }

    /**
     * Is this type numeric?
     */
    virtual bool isNumeric() const {
        return false;
    }

    /**
     * Is this type integer?
     */
    virtual bool isInteger() const {
        return false;
    }

    /**
     * Nested type (for Array, Nullable, etc.)
     */
    virtual const IDataType* tryGetSubcolumnType(const std::string& subcolumn_name) const {
        return nullptr;
    }
};

using DataTypePtr = std::shared_ptr<const IDataType>;
```

## ISerialization (Binary I/O)

```cpp
/**
 * ISerialization handles binary serialization for a data type.
 *
 * Multiple serialization kinds:
 * - DEFAULT: Standard binary format
 * - SPARSE: Optimized for sparse data
 *
 * Based on: ClickHouse ISerialization
 */
class ISerialization {
public:
    enum class Kind : uint8_t {
        DEFAULT = 0,
        SPARSE = 1
    };

    virtual ~ISerialization() = default;

    /**
     * Serialize column to binary
     * @param column Column to serialize
     * @param ostr Output stream
     * @param offset Start row
     * @param limit Number of rows
     */
    virtual void serializeBinaryBulk(
        const IColumn& column,
        WriteBuffer& ostr,
        size_t offset,
        size_t limit) const = 0;

    /**
     * Deserialize column from binary
     * @param column Column to fill
     * @param istr Input stream
     * @param limit Number of rows
     * @param avg_value_size_hint Average value size hint
     */
    virtual void deserializeBinaryBulk(
        IColumn& column,
        ReadBuffer& istr,
        size_t limit,
        double avg_value_size_hint) const = 0;

    /**
     * Serialize single value
     */
    virtual void serializeBinary(const Field& field, WriteBuffer& ostr) const = 0;

    /**
     * Deserialize single value
     */
    virtual void deserializeBinary(Field& field, ReadBuffer& istr) const = 0;
};

using SerializationPtr = std::shared_ptr<const ISerialization>;
```

## Usage Example

```cpp
// Create column
auto col = ColumnInt32::create();
col->insert(Field(42));
col->insert(Field(100));

// Filter
Filter filt = {1, 0, 1};  // Keep rows 0 and 2
auto filtered = col->filter(filt, 2);

// COW semantics
auto col2 = col;  // Shallow copy
auto col3 = col->mutate();  // Deep copy if shared

// Array column
auto nested = ColumnString::create();
nested->insertData("hello", 5);
nested->insertData("world", 5);

auto offsets = ColumnUInt64::create();
offsets->insert(Field(uint64_t(2)));  // Array of 2 strings

auto arr_col = ColumnArray::create(std::move(nested), std::move(offsets));

// Nullable column
auto data = ColumnInt32::create();
data->insert(Field(42));
data->insert(Field(100));

auto null_map = ColumnUInt8::create();
null_map->insert(Field(uint8_t(0)));  // Not NULL
null_map->insert(Field(uint8_t(1)));  // NULL

auto nullable = ColumnNullable::create(std::move(data), std::move(null_map));
```

---

## Memory Management

### Integration with Module 01 Memory Management

Column storage integrates with the memory management system defined in **Module 01 (INDEX_READER_WRITER.md)**. See Module 01 for:
- **ColumnArena**: Arena allocator for bulk column allocations
- **Query memory budgets**: Per-query memory limits
- **OOM handling strategies**: ABORT, GRACEFUL, BEST_EFFORT

### COW (Copy-On-Write) Cleanup Rules

**Rule 1: Automatic Cleanup via Shared Pointers**

Columns use `std::shared_ptr` for automatic reference counting:

```cpp
{
    auto col = ColumnInt32::create();  // refcount = 1
    col->insert(Field(42));

    auto col2 = col;  // refcount = 2 (shallow copy)

    // col2 goes out of scope → refcount = 1
}
// col goes out of scope → refcount = 0 → memory freed
```

**Rule 2: Mutate Creates Deep Copy Only When Shared**

```cpp
auto col = ColumnInt32::create();  // refcount = 1
col->insert(Field(42));

auto col2 = col;  // refcount = 2

// Mutate col: refcount > 1 → deep copy required
auto mutable_col = col->mutate();  // New allocation, col unchanged
mutable_col->insert(Field(100));

// col and col2 still point to original (immutable)
// mutable_col is independent
```

**Rule 3: Clear Unused Columns Explicitly**

For long-running queries, clear temporary columns to free memory:

```cpp
void processQuery(QueryContext& ctx) {
    auto temp_col = ColumnInt32::create();
    // ... use temp_col ...

    // Explicitly release if not needed for result
    temp_col.reset();  // Force cleanup before query completes

    // Continue with other operations
}
```

### Memory Ownership Transfer Semantics

**Transfer Ownership with std::move**

When building columns, use `std::move` to transfer ownership (avoid refcount increments):

```cpp
// GOOD: Transfer ownership (no copy)
auto nested = ColumnString::create();
nested->insertData("hello", 5);
auto arr = ColumnArray::create(std::move(nested), std::move(offsets));
// nested is now nullptr, arr owns the data

// BAD: Keeps extra reference (unnecessary refcount)
auto arr = ColumnArray::create(nested, offsets);
// nested still valid but refcount = 2 (shared with arr)
```

**Return Columns by Value (RVO)**

Return columns by value to enable Return Value Optimization:

```cpp
// GOOD: RVO eliminates copy
ColumnPtr buildColumn() {
    auto col = ColumnInt32::create();
    col->insert(Field(42));
    return col;  // No copy, moves ColumnPtr
}

// BAD: Unnecessary shared_ptr operations
ColumnPtr buildColumn() {
    auto col = std::make_shared<ColumnInt32>();
    return std::shared_ptr<IColumn>(col);  // Extra refcount ops
}
```

### Integration with ColumnArena

**Use ColumnArena for Bulk Allocations**

For operations creating many temporary columns, use `ColumnArena` from Module 01:

```cpp
#include "ColumnArena.h"  // From Module 01

void aggregateColumns(const std::vector<ColumnPtr>& inputs, QueryContext& ctx) {
    // Get arena from query context
    ColumnArena& arena = ctx.getColumnArena();

    // Allocate scratch space without individual mallocs
    uint8_t* scratch = arena.allocate(1024 * 1024);  // 1MB scratch

    // Process columns...

    // Arena bulk-frees all allocations when query completes
}
```

**Arena Benefits**:
- **Batched allocations**: Single large malloc instead of many small ones
- **Bulk deallocation**: Free entire arena at once (O(1) instead of O(N))
- **Reduced fragmentation**: Sequential allocations, no interleaved frees
- **Cache-friendly**: Temporally-related data is spatially co-located

**When to Use Arena**:
- Temporary columns during query execution (aggregate states, intermediate results)
- SIMD score buffers (see Module 01: ScoreBufferPool)
- Short-lived filter bitmaps

**When NOT to Use Arena**:
- Result columns returned to user (need individual lifetime)
- Cached columns (outlive single query)
- Columns stored in segments (persistent)

### Memory Pressure Handling

**Check Memory Budget Before Large Allocations**

```cpp
void IndexWriter::flushColumn(const ColumnPtr& col) {
    size_t colSize = col->byteSize();

    // Check against RAM budget (from Module 01)
    if (bytesUsed_ + colSize > config_.getRAMBufferSizeMB() * 1024 * 1024) {
        // Flush to disk before adding more
        doFlush();
    }

    bytesUsed_ += colSize;
    pendingColumns_.push_back(col);
}
```

**Query Memory Budget Integration**

```cpp
class QueryContext {
    size_t memoryUsed_ = 0;
    size_t memoryLimit_;  // From Module 01 (default: 100MB)
    ColumnArena arena_;

    void trackColumnAllocation(size_t bytes) {
        memoryUsed_ += bytes;
        if (memoryUsed_ > memoryLimit_) {
            throw MemoryLimitExceededException(
                "Query exceeded memory limit: " +
                std::to_string(memoryUsed_) + " > " +
                std::to_string(memoryLimit_)
            );
        }
    }
};
```

### Avoiding Memory Explosion with COW

**Problem**: Repeated mutations on shared columns cause memory explosion.

**Bad Pattern**:
```cpp
auto col = ColumnInt32::create();
for (int i = 0; i < 1000000; ++i) {
    col = col->mutate();  // Creates new copy every iteration!
    col->insert(Field(i));
}
// Memory usage: O(N²) - disaster!
```

**Good Pattern**:
```cpp
auto col = ColumnInt32::create();
col->reserve(1000000);  // Pre-allocate

for (int i = 0; i < 1000000; ++i) {
    col->insert(Field(i));  // No mutation needed (not shared)
}
// Memory usage: O(N) - optimal
```

**Check Mutability Before Loops**:
```cpp
void processColumn(ColumnPtr col) {
    // Ensure exclusive ownership before loop
    if (col.use_count() > 1) {
        col = col->mutate();  // One-time copy
    }

    // Now col is exclusively owned, mutations are in-place
    for (size_t i = 0; i < col->size(); ++i) {
        col->set(i, transformValue(col->get(i)));
    }
}
```

### Summary

**Key Integration Points with Module 01**:
1. ✅ Use **ColumnArena** for temporary query-lifetime allocations
2. ✅ Respect **memory budgets** (RAMBufferSizeMB, query memory limit)
3. ✅ Handle **OOM gracefully** using Module 01 strategies

**COW Best Practices**:
1. ✅ Use `std::move` for ownership transfer
2. ✅ Check `use_count()` before mutating in loops
3. ✅ Pre-allocate with `reserve()` when size known
4. ✅ Clear temporary columns explicitly in long queries

**Memory Ownership Rules**:
1. ✅ Columns use `std::shared_ptr` (automatic cleanup)
2. ✅ `mutate()` creates deep copy only if shared (refcount > 1)
3. ✅ Return columns by value (RVO optimizes)
4. ✅ Arena allocations bulk-freed on query completion

---
