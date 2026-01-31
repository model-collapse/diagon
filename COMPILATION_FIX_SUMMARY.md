# Compilation Issue: Root Cause and Fix

**Date:** 2026-01-31
**Issue:** IndexWriter compilation failure preventing benchmarks
**Status:** ✅ Fixed for IndexWriter, ⚠️ MatchAllDocsQuery needs separate fix

---

## The Mystery

**Question:** Why did benchmarks run successfully on Jan 31 03:51, but compilation fails now?

**Answer:** The successful benchmarks used **pre-compiled binaries** from January 24-25, which were built **before** the bug was introduced!

---

## Timeline

1. **Jan 24-25:** Binaries compiled successfully
   - `/home/ubuntu/diagon/benchmarks/SearchBenchmark` (Jan 24 13:08)
   - `/home/ubuntu/diagon/src/core/libdiagon_core.so` (Jan 25 08:35)

2. **Jan 27 16:03:** Commit 3d9ae0d "Integrate TieredMergePolicy" **introduced the bug**
   - Added `IndexWriterConfig config_` member to IndexWriter
   - IndexWriterConfig contains `unique_ptr<MergePolicy>` → non-copyable
   - Constructor tries `config_(config)` → copy attempted → compilation error!

3. **Jan 31 03:51:** Benchmarks ran successfully
   - Used old pre-compiled binaries from Jan 24-25
   - Never recompiled after Jan 27 commit

4. **Jan 31 08:25+:** Tried to rebuild
   - Hit compilation error introduced by Jan 27 commit
   - IndexWriter constructor cannot copy IndexWriterConfig

---

## The Bug

### Problem

```cpp
// IndexWriter.h (line 268)
class IndexWriter {
private:
    IndexWriterConfig config_;  // Tries to copy!
};

// IndexWriterConfig has:
std::unique_ptr<MergePolicy> mergePolicy_;  // Cannot be copied!

// IndexWriter.cpp (line 39)
IndexWriter::IndexWriter(Directory& dir, const IndexWriterConfig& config)
    : config_(config) {  // ❌ Error: cannot copy unique_ptr!
```

**Error Message:**
```
error: use of deleted function 'IndexWriterConfig::IndexWriterConfig(const IndexWriterConfig&)'
note: implicitly deleted because the default definition would be ill-formed:
error: use of deleted function 'std::unique_ptr<MergePolicy>::unique_ptr(const unique_ptr&)'
```

### Why This Happened

The TieredMergePolicy integration needed to store merge policy in IndexWriterConfig. Using `unique_ptr` makes sense (exclusive ownership), but it also makes IndexWriterConfig non-copyable. The code tried to store a copy of config in IndexWriter, which fails.

---

## The Fix

### Solution: Extract Values Instead of Copying

**Before (broken):**
```cpp
// IndexWriter.h
class IndexWriter {
private:
    IndexWriterConfig config_;  // Tries to copy entire config
};

// IndexWriter.cpp
IndexWriter::IndexWriter(Directory& dir, const IndexWriterConfig& config)
    : config_(config) {  // ❌ Copy fails!
    // ...
    if (config_.getMergePolicy()) { ... }
    switch (config_.getOpenMode()) { ... }
}
```

**After (fixed):**
```cpp
// IndexWriter.h
class IndexWriter {
private:
    bool commitOnClose_;              // Extract individual values
    IndexWriterConfig::OpenMode openMode_;
    // No config_ member!
};

// IndexWriter.cpp
IndexWriter::IndexWriter(Directory& dir, const IndexWriterConfig& config)
    : commitOnClose_(config.getCommitOnClose())
    , openMode_(config.getOpenMode()) {  // ✅ Extract values only
    // ...
    if (config.getMergePolicy()) { ... }  // Use parameter directly
    switch (openMode_) { ... }            // Use stored value
}
```

### Changes Made

**File: src/core/include/diagon/index/IndexWriter.h**

1. **Removed:** `IndexWriterConfig config_;`
2. **Added:**
   - `bool commitOnClose_;`
   - `IndexWriterConfig::OpenMode openMode_;`
3. **Removed:** `const IndexWriterConfig& getConfig() const` method

**File: src/core/src/index/IndexWriter.cpp**

1. **Constructor initialization list:** Extract values from config
   ```cpp
   : commitOnClose_(config.getCommitOnClose())
   , openMode_(config.getOpenMode())
   ```

2. **Constructor body:** Use `config` parameter directly
   ```cpp
   if (config.getMergePolicy()) { ... }
   config.getRAMBufferSizeMB()
   config.getMaxBufferedDocs()
   ```

3. **Destructor:** Use stored `commitOnClose_`
   ```cpp
   if (commitOnClose_) { commit(); }
   ```

4. **initializeIndex():** Use stored `openMode_`
   ```cpp
   switch (openMode_) { ... }
   ```

---

## Verification

**Before fix:**
```bash
$ make SearchBenchmark
error: use of deleted function 'IndexWriterConfig::IndexWriterConfig(const IndexWriterConfig&)'
```

**After fix:**
```bash
$ make SearchBenchmark
[ 27%] Building CXX object src/core/CMakeFiles/diagon_core.dir/src/index/IndexWriter.cpp.o
✅ IndexWriter.cpp compiles successfully!
```

**Remaining issue:**
```
MatchAllDocsQuery.cpp: error: invalid new-expression of abstract class type 'MatchAllScorer'
```
This is a separate issue - MatchAllScorer doesn't implement pure virtual functions.

---

## Lessons Learned

### 1. **Latent Bugs Can Hide in Pre-Compiled Binaries**

The bug was introduced on Jan 27, but benchmarks continued working because they used old binaries from Jan 24. This masked the issue for 4 days until we tried to recompile.

### 2. **unique_ptr Makes Classes Non-Copyable**

When a class contains `unique_ptr`, it becomes non-copyable by default. Options:
- ✅ Don't store by value (extract what you need)
- ✅ Use `shared_ptr` if sharing is acceptable
- ✅ Implement move semantics
- ❌ Try to copy (won't compile)

### 3. **C++ Rule of Five**

If you have:
- Destructor
- Copy constructor
- Copy assignment
- Move constructor
- Move assignment

You should explicitly define or delete all of them. Having `unique_ptr` members implicitly deletes copy operations.

---

## Alternative Fixes Considered

### Option 1: Store by Reference (Rejected)

```cpp
const IndexWriterConfig& config_;
```

**Problem:** What if the original config goes out of scope?

### Option 2: Use shared_ptr (Possible)

```cpp
// IndexWriterConfig.h
std::shared_ptr<MergePolicy> mergePolicy_;

// IndexWriter.h
IndexWriterConfig config_;  // Now copyable
```

**Tradeoff:** Allows sharing but changes ownership semantics.

### Option 3: Store Pointer (Possible)

```cpp
std::unique_ptr<IndexWriterConfig> config_;

IndexWriter::IndexWriter(Directory& dir, const IndexWriterConfig& config)
    : config_(std::make_unique<IndexWriterConfig>(config)) {
```

**Problem:** Still requires IndexWriterConfig to be copyable.

### Option 4: Extract Values (✅ Chosen)

**Advantages:**
- No copying needed
- Clear ownership
- Minimal changes
- Only stores what's actually used

---

## Files Changed

```
modified:   src/core/include/diagon/index/IndexWriter.h
modified:   src/core/src/index/IndexWriter.cpp
```

**Commit needed:** Yes, these fixes should be committed.

---

## Next Steps

### Immediate

1. ✅ IndexWriter fixed
2. ⏳ Fix MatchAllDocsQuery (separate issue)
   - MatchAllScorer doesn't implement pure virtuals
   - Not blocking for most benchmarks

3. ⏳ Build and test benchmarks

### Testing

Once MatchAllDocsQuery is fixed:

```bash
# Build in Release mode
cd /home/ubuntu/diagon/build
rm -rf *
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS="-O3 -march=native" \
      -DDIAGON_BUILD_BENCHMARKS=ON ..
make SearchBenchmark -j8

# Run benchmarks
./benchmarks/SearchBenchmark
```

### 10M Document Comparison

After successful build:

1. Create simple benchmark that doesn't use MatchAllDocsQuery
2. Index 10M documents
3. Run same queries as Lucene
4. Compare results

---

## Summary

**Root cause:** TieredMergePolicy commit made IndexWriterConfig non-copyable

**Hidden because:** Old pre-compiled binaries continued working

**Fix:** Extract individual config values instead of copying entire config

**Status:** IndexWriter fixed ✅, MatchAllDocsQuery pending ⚠️

**Impact:** Can now build most components; benchmarks may work with old binaries

---

**Prepared by:** Claude Code
**Date:** 2026-01-31
**Files modified:** IndexWriter.h, IndexWriter.cpp
**Ready to commit:** Yes
