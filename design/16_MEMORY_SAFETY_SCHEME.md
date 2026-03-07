# 16. Memory Safety Scheme

## Problem Statement

Issue #12 exposed a cache leak pattern (unbounded map growth via unique keys) that leaked ~936 MB per search. Codebase audit reveals this is one instance of a broader class of memory safety issues stemming from Diagon's C++ port of Lucene's Java API patterns. Java's GC masks problems that become critical in C++:

1. **Unbounded caches** — mutable maps that grow without eviction
2. **Dangling raw pointers** — methods return `T*` to cache-owned `unique_ptr<T>`
3. **Mixed ownership** — `shared_ptr` coexists with manual `incRef()/decRef()`
4. **No lifecycle guards** — ref-counting has no RAII wrapper

## Inventory of Risk Sites

### Tier 1: Unbounded Cache Growth (Memory Leaks)

| Cache | Location | Key Type | Eviction | Risk |
|-------|----------|----------|----------|------|
| `numericDocValuesCache_` | SegmentReader.h:246 | field name | Replace on same field | **FIXED** (Issue #12) |
| `termsCache_` | SegmentReader.h:240 | field name | Never | Bounded by field count |
| `normsCache_` | SegmentReader.h:256 | field name | Never | Bounded by field count |
| `fieldReaders_` | Lucene104FieldsProducer.h:98 | field name | Never | Bounded by field count |
| `entries_` | PackedFST.h:273 | N/A (vector) | Never | Bounded by FST size |
| Codec `instances` | Codec.cpp:39 | codec name | Never | Bounded by codec types (~3) |
| `segments_` | DocumentsWriter | index | Never within session | Grows with indexing |

**Assessment**: After Issue #12 fix, remaining caches are bounded by field count (typically 5-20 per segment) or codec count (~3). No urgent leak risk, but `DocumentsWriter::segments_` accumulates across the writer's lifetime.

### Tier 2: Dangling Raw Pointers (Use-After-Free)

| Method | Returns | Backed By | Invalidated When |
|--------|---------|-----------|------------------|
| `SegmentReader::terms()` | `Terms*` | `termsCache_[field]` | `doClose()` clears cache |
| `SegmentReader::getNumericDocValues()` | `NumericDocValues*` | `numericDocValuesCache_[field]` | `doClose()` or next call for same field |
| `SegmentReader::getNormValues()` | `NumericDocValues*` | `normsCache_[field]` | `doClose()` clears cache |
| `SegmentReader::storedFieldsReader()` | `StoredFieldsReader*` | `storedFieldsReader_` | `doClose()` resets |
| `SegmentReader::getLiveDocs()` | `const Bits*` | `liveDocs_` | `doClose()` resets |
| `LeafReaderContext::reader` | `LeafReader*` | parent DirectoryReader | parent `decRef()` to 0 |

**Assessment**: These are the highest-risk sites. A user who holds a `Terms*` across a reader close gets a crash. Currently "safe" because our search path consumes pointers within a single query scope, but fragile.

### Tier 3: Ownership Model Confusion

- `DirectoryReader` holds `shared_ptr<SegmentReader>` but also calls manual `incRef()/decRef()`
- `SegmentReader` has both `shared_ptr` semantics (from DirectoryReader) and `refCount_` (from IndexReader base)
- No RAII guard for `incRef()/decRef()` — exception-unsafe

## Strategy: 3-Phase Plan

### Phase 1: Guardrails (Immediate — No API Changes)

**Goal**: Prevent leaks and catch dangling pointers without changing public interfaces.

#### 1a. SegmentReader Cache Size Assertion

Add debug-mode assertions that fire if any per-reader cache exceeds a reasonable bound (e.g., 100 entries). This catches bugs like Issue #12 during testing:

```cpp
// In SegmentReader methods that insert into caches:
assert(numericDocValuesCache_.size() < 100 &&
       "Cache growing unbounded — possible leak (see Issue #12)");
```

#### 1b. DocumentsWriter Segment Pruning

After `collectNewSegments()` moves segments to `segmentInfos_`, prune the DocumentsWriter's internal `segments_` vector to prevent unbounded growth during long indexing sessions:

```cpp
void IndexWriter::collectNewSegments() {
    auto allSegments = documentsWriter_->getSegmentInfos();
    for (size_t i = collectedSegmentCount_; i < allSegments.size(); i++) {
        segmentInfos_.add(allSegments[i]);
    }
    collectedSegmentCount_ = allSegments.size();
    // Phase 1b: Prune collected segments from DocumentsWriter
    documentsWriter_->pruneCollectedSegments(collectedSegmentCount_);
}
```

#### 1c. RefCountGuard RAII Wrapper

```cpp
// New file: src/core/include/diagon/index/RefCountGuard.h
class RefCountGuard {
    IndexReader* reader_;
public:
    explicit RefCountGuard(IndexReader* r) : reader_(r) {
        if (reader_) reader_->incRef();
    }
    ~RefCountGuard() {
        if (reader_) reader_->decRef();
    }
    RefCountGuard(RefCountGuard&& o) noexcept : reader_(o.reader_) {
        o.reader_ = nullptr;
    }
    RefCountGuard(const RefCountGuard&) = delete;
    RefCountGuard& operator=(const RefCountGuard&) = delete;
};
```

Use internally at all call sites that hold reader pointers across scopes.

### Phase 2: Safe Pointer Returns (Medium-Term — Internal API Change)

**Goal**: Eliminate dangling raw pointers from the reader API.

#### 2a. Cache Returns as `shared_ptr`

Change the 6 dangerous raw-pointer returns to return `shared_ptr`:

```cpp
// Before (unsafe):
Terms* SegmentReader::terms(const std::string& field) const;

// After (safe):
std::shared_ptr<Terms> SegmentReader::terms(const std::string& field) const;
```

The cache stores `shared_ptr<Terms>` instead of `unique_ptr<Terms>`. Callers hold a `shared_ptr` that keeps the object alive even if the reader is closed.

**Affected methods** (all on SegmentReader/LeafReader):
- `terms()` → `shared_ptr<Terms>`
- `getNumericDocValues()` → `shared_ptr<NumericDocValues>`
- `getNormValues()` → `shared_ptr<NumericDocValues>`
- `storedFieldsReader()` → `shared_ptr<StoredFieldsReader>`
- `getLiveDocs()` → `shared_ptr<const Bits>`
- `getPointValues()` → `shared_ptr<PointValues>`

**Impact**: All scorer/query code that calls these methods needs updating (return type changes from `T*` to `shared_ptr<T>`). This is a mechanical refactor.

#### 2b. LeafReaderContext Safe Reference

```cpp
// Before:
struct LeafReaderContext {
    LeafReader* reader;  // dangling if parent closed
};

// After:
struct LeafReaderContext {
    std::shared_ptr<LeafReader> reader;  // prevents premature destruction
};
```

### Phase 3: Unified Ownership Model (Long-Term — Architecture)

**Goal**: Eliminate the dual ownership problem (shared_ptr + manual refCount).

#### 3a. Remove Manual Ref-Counting from IndexReader

Migrate from `incRef()/decRef()` to pure `shared_ptr` ownership:

```cpp
// Before:
class IndexReader {
    std::atomic<int> refCount_{1};
public:
    void incRef();
    void decRef();  // calls doClose() when count reaches 0
};

// After:
class IndexReader : public std::enable_shared_from_this<IndexReader> {
public:
    // No manual ref-counting — shared_ptr handles lifecycle
    // doClose() called from destructor or explicit close()
};
```

**Key change**: `DirectoryReader::open()` returns `shared_ptr<DirectoryReader>`. All internal storage uses `shared_ptr`. `doClose()` is called from the destructor.

**Migration path**:
1. Add `shared_from_this()` support
2. Change `DirectoryReader::open()` return type to `shared_ptr`
3. Remove `incRef()/decRef()` from public API
4. Update all callers (search code, benchmarks, tests)

#### 3b. Memory Budget for Caches

Add a per-reader memory budget that applies across all caches:

```cpp
class SegmentReader {
    size_t cacheMemoryBudget_ = 64 * 1024 * 1024;  // 64 MB default
    mutable std::atomic<size_t> cacheMemoryUsed_{0};

    void evictIfOverBudget() const {
        if (cacheMemoryUsed_ > cacheMemoryBudget_) {
            // LRU eviction across all caches
        }
    }
};
```

This is defense-in-depth — even if a bug creates unbounded growth, the budget caps total memory.

## Testing Strategy

### Memory Leak Detection Tests

```cpp
// Test: Repeated queries don't leak memory
TEST(MemoryLeakTest, RepeatedQueriesStableMemory) {
    auto reader = DirectoryReader::open(dir);
    auto searcher = IndexSearcher(reader);

    size_t baselineRSS = getCurrentRSS();

    for (int i = 0; i < 1000; i++) {
        auto query = TermQuery("content", "test");
        searcher.search(query, 10);
    }

    size_t finalRSS = getCurrentRSS();
    // Allow 10% growth for legitimate caching, but not 100x
    EXPECT_LT(finalRSS, baselineRSS * 1.1);
}

// Test: Cache size bounded
TEST(CacheBoundTest, SegmentReaderCacheBounded) {
    auto reader = SegmentReader::open(dir, segInfo);
    for (int i = 0; i < 100; i++) {
        reader->getNumericDocValues("field_" + std::to_string(i));
    }
    // Cache should have at most 100 entries (one per field)
    // Not 100 * queryCount entries
}
```

### CI Integration

Add ASan (AddressSanitizer) and LSan (LeakSanitizer) to CI:

```cmake
# CMakeLists.txt option for sanitizer builds
option(DIAGON_SANITIZE "Enable sanitizers" OFF)
if(DIAGON_SANITIZE)
    add_compile_options(-fsanitize=address,leak -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,leak)
endif()
```

## Priority Order

| Phase | Item | Effort | Impact | When |
|-------|------|--------|--------|------|
| 1a | Cache size assertions | 1 hour | Catches future Issue #12 variants | Now |
| 1b | DocumentsWriter pruning | 2 hours | Fixes long-session leak | Now |
| 1c | RefCountGuard | 2 hours | Prevents ref-count errors | Now |
| 2a | shared_ptr cache returns | 1-2 days | Eliminates dangling pointers | Next milestone |
| 2b | LeafReaderContext safe ref | 2 hours | Eliminates context dangling | Next milestone |
| 3a | Remove manual ref-counting | 3-5 days | Clean ownership model | Future |
| 3b | Memory budget | 2-3 days | Defense-in-depth | Future |
| CI | ASan/LSan in CI | 2 hours | Catches all leaks automatically | Now |

## References

- Issue #12: NumericDocValues cache leak (~936 MB/search)
- Lucene: `IndexReader` uses `CloseableReference` + GC
- ClickHouse: Uses `shared_ptr` + `ColumnPtr` (COW semantics) throughout
