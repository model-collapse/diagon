# Diagon Build Standard Operating Procedure (SOP)

**Purpose:** Ensure reliable, reproducible builds without compilation or linking errors

**Last Updated:** 2026-01-31

---

## Root Causes of Build Failures

### Problem 1: LTO (Link-Time Optimization) Issues

**Symptom:** `undefined reference to icu_73::...` despite ICU being linked

**Root Cause:**
- CMake uses `-flto=auto -fno-fat-lto-objects` in Release mode
- Creates LLVM IR plugin objects that defer symbol resolution
- ICU symbols embedded in .so but not resolved until final executable link
- Benchmarks fail to link because symbols appear undefined

**Solution:** Disable LTO for reliable builds (see Build Procedure below)

### Problem 2: Mixed Conda/System Libraries

**Symptom:** Linking conflicts, version mismatches

**Root Cause:**
- `/home/ubuntu/miniconda3/lib/libzstd.a` conflicts with system libzstd
- CMake finds conda libraries first due to PATH
- Mix of conda and system libs causes ABI incompatibilities

**Solution (Automatic):** CMake now implements intelligent fallback:
- ✅ Detects conda environment automatically
- ✅ Tries conda libraries first
- ✅ Falls back to system if not found
- ✅ Reports source for each library (conda/system)
- ⚠️ Warns if libraries come from mixed sources

**Manual Override:** If you want to force system-only, set PATH:
```bash
export PATH=/usr/bin:/bin:/usr/local/bin:$PATH
```

### Problem 3: Stale CMake Cache

**Symptom:** Random build failures, old settings persist

**Root Cause:**
- CMakeCache.txt remembers old configurations
- Partial rebuilds miss changes
- Cached targets from previous failed builds

**Solution:** Always start with clean build directory

### Problem 4: Pre-compiled Binaries Mask Bugs

**Symptom:** Benchmarks run but code doesn't compile

**Root Cause:**
- Old binaries from previous successful builds
- Code changes break compilation but tests still pass
- Confusion about what's actually working

**Solution:** Force rebuild, never trust old binaries

---

## Standard Build Procedure

### Step 1: Clean Environment

```bash
# CRITICAL: Always start clean
cd /home/ubuntu/diagon
rm -rf build
mkdir build
cd build

# Optional: Force system libraries only (if conda causes issues)
# export PATH=/usr/bin:/bin:/usr/local/bin:$PATH
#
# Note: CMake now automatically tries conda first, then falls back to system.
# You only need to set PATH if you want to force system-only libraries.
```

**Why:** Removes all cached state, old binaries, and stale configurations

**Automatic Fallback:** CMake will detect your conda environment and intelligently:
- Try conda libraries first (ICU, ZLIB, LZ4, ZSTD)
- Fall back to system libraries if not available in conda
- Report the source of each library during configuration

### Step 2: Configure CMake (Release Mode)

```bash
# RECOMMENDED: Release without LTO
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS="-O3 -march=native" \
      -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
      -DDIAGON_BUILD_BENCHMARKS=ON \
      -DDIAGON_BUILD_TESTS=ON \
      ..
```

**Key Flags:**
- `CMAKE_BUILD_TYPE=Release` - Optimizations enabled
- `-O3 -march=native` - Maximum performance, native CPU
- `CMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF` - **DISABLE LTO** (critical!)
- `DIAGON_BUILD_BENCHMARKS=ON` - Build benchmarks
- `DIAGON_BUILD_TESTS=ON` - Build tests

**Why Disable LTO:**
- LTO causes undefined symbol errors with shared libraries
- Benefits minimal for our use case (~2-5% max)
- Increases compile time significantly
- Creates hard-to-debug linking issues

**Alternative: Debug Mode (for development only)**

```bash
cmake -DCMAKE_BUILD_TYPE=Debug \
      -DDIAGON_BUILD_BENCHMARKS=ON \
      -DDIAGON_BUILD_TESTS=ON \
      ..
```

**Debug vs Release:**
| Mode | Speed | Symbols | LTO | Use Case |
|------|-------|---------|-----|----------|
| Debug | Slow (10-100x) | Yes | No | Development, debugging |
| Release (no LTO) | Fast | No | No | **RECOMMENDED for benchmarks** |
| Release (LTO) | Fast | No | Yes | **AVOID - causes link errors** |

### Step 3: Verify Configuration

```bash
# Check that ICU was found
cmake .. 2>&1 | grep -i icu
# Expected: "-- Found ICU: 74.2" or similar

# Check LTO is disabled
grep "INTERPROCEDURAL_OPTIMIZATION" CMakeCache.txt
# Expected: CMAKE_INTERPROCEDURAL_OPTIMIZATION:BOOL=OFF
```

**Why:** Catch configuration errors before building

### Step 4: Build Core Library

```bash
# Build diagon_core first (detects compilation errors early)
make diagon_core -j8

# Verify library was created and linked correctly
ls -lh src/core/libdiagon_core.so
ldd src/core/libdiagon_core.so | grep icu
# Expected: Should show libicuuc.so and libicui18n.so
```

**Why:** Core library is foundation; if it fails, everything fails

### Step 5: Build Benchmarks

```bash
# Build specific benchmark
make SearchBenchmark -j8

# Or build all benchmarks
make benchmarks -j8
```

**Common Benchmarks:**
- `SearchBenchmark` - Basic search performance
- `LuceneComparisonBenchmark` - Diagon vs Lucene comparison
- `Lucene104BatchBenchmark` - Lucene104 codec batch operations
- `IndexingBenchmark` - Index build performance

### Step 6: Verify Build

```bash
# Check benchmark was created
ls -lh benchmarks/SearchBenchmark

# Check it's linked correctly
ldd benchmarks/SearchBenchmark | grep diagon_core
# Expected: libdiagon_core.so => /path/to/libdiagon_core.so

# Test run (quick sanity check)
./benchmarks/SearchBenchmark --benchmark_filter=BM_TermQuerySearch/1000 --benchmark_min_time=0.1
```

**Why:** Verify the build actually works before running full benchmarks

---

## Running Benchmarks

### Quick Test (1-2 minutes)

```bash
cd /home/ubuntu/diagon/build
./benchmarks/SearchBenchmark --benchmark_filter=BM_TermQuerySearch/1000
```

### Full Benchmark Suite (10-30 minutes)

```bash
# Save results to file
./benchmarks/SearchBenchmark --benchmark_out=results.json \
                             --benchmark_out_format=json

# Or run specific document counts
./benchmarks/LuceneComparisonBenchmark --benchmark_filter=.*10000.*
```

### 10M Document Benchmark

**Option 1: Use existing benchmark with larger dataset**

Modify benchmark source to accept command-line argument:

```cpp
// In benchmark .cpp file
int main(int argc, char** argv) {
    int numDocs = 10000;  // default
    if (argc > 1) {
        numDocs = std::stoi(argv[1]);
    }
    // ... create index with numDocs
}
```

**Option 2: Create dedicated 10M benchmark**

See `BUILD_SOP.md` companion file: `Scale10MBenchmark.cpp`

---

## Troubleshooting

### Error: `undefined reference to icu_73::...`

**Cause:** Conda ICU (version 73) conflicts with system ICU (version 74)

**Root Cause:**
- libdiagon_core.so compiled against conda ICU 73
- Trying to link benchmark with system ICU 74
- ABI incompatibility (icu_73 symbols vs icu_74)

**Fix Option 1: Use pre-compiled binaries** (Quick workaround)
```bash
# Pre-compiled benchmarks (from Jan 24) still work
/home/ubuntu/diagon/benchmarks/SearchBenchmark
```

**Fix Option 2: Rebuild diagon_core with system ICU** (Proper fix)
```bash
cd /home/ubuntu/diagon
rm -rf build
mkdir build
cd build

# Temporarily disable conda from PATH
export PATH=/usr/bin:/bin:/usr/local/bin:$PATH

cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
      -DDIAGON_BUILD_BENCHMARKS=ON ..
make diagon_core -j8

# Check ICU version used
nm -D src/core/libdiagon_core.so | grep "U.*icu_" | head -1
# Should show icu_74 not icu_73
```

**Fix Option 3: Use conda ICU consistently** (Alternative)
```bash
# Keep conda in PATH, install all deps from conda
conda install -c conda-forge icu=73 zstd lz4
# Then build normally
```

### Error: `use of deleted function '...'`

**Cause:** C++ compilation error (e.g., trying to copy unique_ptr)

**Fix:**
1. Read error message carefully
2. Find the problematic code (usually shown in error)
3. Fix the C++ issue (don't copy non-copyables, implement virtual functions, etc.)
4. Rebuild from clean state

### Error: `ZSTD target not found`

**Cause:** System package creates `zstd::libzstd_shared` target, but CMake expects `zstd::libzstd`

**Root Cause:**
- System libzstd-dev package's CMake config creates `zstd::libzstd_shared`
- Our CMakeLists.txt looks for `zstd::libzstd` or `zstd::zstd`
- Target name mismatch causes fatal error

**Fix:** (Already applied to cmake/Dependencies.cmake)
```cmake
# In cmake/Dependencies.cmake, after find_package(zstd)
if(TARGET zstd::libzstd_shared AND NOT TARGET zstd::libzstd)
    add_library(zstd::libzstd ALIAS zstd::libzstd_shared)
endif()
```

**Status:** ✅ Fixed in current codebase (2026-01-31)

### Error: `multiple definition of ...`

**Cause:** Same symbol defined in multiple .cpp files

**Fix:**
1. Find duplicate definitions
2. Make one `static` or remove duplicates
3. Use `inline` for header-only definitions

### Error: Pre-compiled binary runs but compilation fails

**Cause:** Old binary from previous successful build

**Fix:**
```bash
# Force rebuild from scratch
rm -rf build
mkdir build
cd build
# ... full build procedure
```

**Prevention:** Always delete old binaries when testing new code

### Compilation succeeds but benchmark crashes

**Possible causes:**
1. Debug mode vs Release mode ABI mismatch
2. Linking wrong library version
3. Missing data files (dictionaries, etc.)

**Fix:**
```bash
# Verify all libraries are consistent
ldd benchmarks/SearchBenchmark
# All paths should be from build/ directory, not /usr/lib mixing

# Check for ABI issues
nm -D src/core/libdiagon_core.so | grep mangledSymbol
# Symbols should match between library and executable
```

---

## Checklist for Every Build

- [ ] Start with clean build directory (`rm -rf build && mkdir build`)
- [ ] Use Release mode without LTO (`CMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF`)
- [ ] Verify ICU found (`cmake .. | grep ICU`)
- [ ] Build core library first (`make diagon_core`)
- [ ] Check ICU linked (`ldd libdiagon_core.so | grep icu`)
- [ ] Build benchmark (`make SearchBenchmark`)
- [ ] Test run before full benchmark
- [ ] Save results to `/tmp/` or timestamped file

---

## Quick Reference

### Build Commands

```bash
# Full clean build (RECOMMENDED)
cd /home/ubuntu/diagon
rm -rf build && mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS="-O3 -march=native" \
      -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
      -DDIAGON_BUILD_BENCHMARKS=ON ..
make diagon_core -j8
make SearchBenchmark -j8
./benchmarks/SearchBenchmark
```

### Verification Commands

```bash
# Check ICU linking
ldd build/src/core/libdiagon_core.so | grep icu

# Check benchmark linking
ldd build/benchmarks/SearchBenchmark | grep diagon_core

# List all benchmarks
make help | grep benchmark
```

### Clean Commands

```bash
# Clean build directory
rm -rf build

# Clean generated files
git clean -fdx  # WARNING: Deletes all untracked files!
```

---

## Common Mistakes to AVOID

1. ❌ **Incremental builds after CMake changes**
   - ✅ Always `rm -rf build` after CMakeLists.txt changes

2. ❌ **Mixing Debug and Release builds**
   - ✅ One build directory per build type

3. ❌ **Using LTO in Release mode**
   - ✅ Explicitly disable: `CMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF`

4. ❌ **Trusting pre-compiled binaries**
   - ✅ Rebuild from scratch before benchmarks

5. ❌ **Building individual targets with `make <target>`**
   - ✅ Build `diagon_core` first, then benchmarks

6. ❌ **Ignoring `ldd` output**
   - ✅ Always verify ICU is linked to libdiagon_core.so

7. ❌ **Running benchmarks in Debug mode**
   - ✅ Use Release mode (but without LTO)

---

## Performance Expectations

### Build Times (8-core machine)

- Clean build: ~2-3 minutes
- Incremental: ~10-30 seconds
- With LTO: ~5-10 minutes (avoid!)

### Benchmark Times

- 10K docs: ~10-30 seconds
- 100K docs: ~2-5 minutes
- 10M docs: ~30-60 minutes

---

## Environment Requirements

### System Libraries

```bash
# Required packages
sudo apt-get install -y \
    build-essential \
    cmake \
    libicu-dev \
    libz-dev \
    liblz4-dev \
    libzstd-dev

# Verify installations
ldconfig -p | grep libicu
ldconfig -p | grep libzstd
```

### Compiler

- GCC 11+ or Clang 14+
- C++20 support required

---

## Success Criteria

A successful build should:

1. ✅ Compile without errors or warnings (warnings are treated as errors via `-Werror`)
2. ✅ Link without undefined symbols
3. ✅ `ldd libdiagon_core.so` shows ICU libraries
4. ✅ Benchmark runs without crashes
5. ✅ Results are reasonable (not all zeros, not crashes)

**Note:** The build uses `-Werror` to treat all warnings as errors. This ensures:
- Zero-warning policy for production code
- Immediate feedback on code quality issues
- Consistent builds across different environments
- External libraries (cppjieba) are marked as SYSTEM to suppress their warnings

---

**Last Updated:** 2026-01-31
**Tested On:** Ubuntu 22.04, GCC 13.1.0
**Maintained By:** Claude Code

