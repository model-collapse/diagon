# Build System Design
## CMake-based Build System for Lucene++

**Status**: Infrastructure Design
**Purpose**: Define build system, dependencies, and compilation requirements

---

## Overview

Lucene++ uses **CMake** as the build system generator for:
- Cross-platform builds (Linux, macOS, Windows)
- Dependency management
- Compiler configuration
- Testing framework integration
- Installation and packaging

**Design Goals**:
- Simple developer onboarding (single cmake command)
- Fast incremental builds
- Reproducible builds across environments
- Support for both static and shared libraries

---

## Project Structure

```
lucene-pp/
├── CMakeLists.txt                    # Root CMake file
├── cmake/
│   ├── Dependencies.cmake            # External dependency resolution
│   ├── CompilerFlags.cmake           # Compiler-specific flags
│   ├── SIMDDetection.cmake           # SIMD capability detection
│   └── Testing.cmake                 # Test configuration
├── src/
│   ├── core/
│   │   ├── CMakeLists.txt           # Core library build
│   │   ├── index/                   # IndexReader/Writer
│   │   ├── search/                  # Query execution
│   │   ├── codecs/                  # Codec system
│   │   ├── store/                   # Directory abstraction
│   │   └── util/                    # Utilities
│   ├── columns/
│   │   └── CMakeLists.txt           # Column storage library
│   └── simd/
│       └── CMakeLists.txt           # SIMD-optimized code
├── tests/
│   ├── CMakeLists.txt               # Test configuration
│   ├── unit/                        # Unit tests
│   ├── integration/                 # Integration tests
│   └── benchmark/                   # Performance benchmarks
├── third_party/                     # Vendored dependencies (optional)
└── docs/                            # Documentation
```

---

## Root CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)
project(LucenePP
    VERSION 1.0.0
    LANGUAGES CXX
    DESCRIPTION "C++ search engine combining Lucene and ClickHouse"
)

# ==================== Options ====================

option(LUCENEPP_BUILD_TESTS "Build tests" ON)
option(LUCENEPP_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(LUCENEPP_BUILD_SHARED "Build shared library" ON)
option(LUCENEPP_BUILD_STATIC "Build static library" ON)
option(LUCENEPP_ENABLE_SIMD "Enable SIMD optimizations" ON)
option(LUCENEPP_USE_VCPKG "Use vcpkg for dependencies" OFF)
option(LUCENEPP_USE_CONAN "Use Conan for dependencies" OFF)

# ==================== C++ Standard ====================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==================== Build Type ====================

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# ==================== Include CMake Modules ====================

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(Dependencies)
include(CompilerFlags)
include(SIMDDetection)

# ==================== External Dependencies ====================

find_package(ZLIB REQUIRED)
find_package(zstd REQUIRED)
find_package(LZ4 REQUIRED)

# Optional: Google Test for unit tests
if(LUCENEPP_BUILD_TESTS)
    find_package(GTest REQUIRED)
    enable_testing()
    include(CTest)
endif()

# Optional: Google Benchmark for performance tests
if(LUCENEPP_BUILD_BENCHMARKS)
    find_package(benchmark REQUIRED)
endif()

# ==================== Subdirectories ====================

add_subdirectory(src/core)
add_subdirectory(src/columns)
add_subdirectory(src/simd)

if(LUCENEPP_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# ==================== Installation ====================

include(GNUInstallDirs)

install(TARGETS lucenepp_core lucenepp_columns lucenepp_simd
    EXPORT LucenePPTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY src/core/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/lucenepp
    FILES_MATCHING PATTERN "*.h"
)

# ==================== Export Targets ====================

install(EXPORT LucenePPTargets
    FILE LucenePPTargets.cmake
    NAMESPACE LucenePP::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LucenePP
)

# ==================== Package Config ====================

include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/LucenePPConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/LucenePPConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LucenePP
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/LucenePPConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/LucenePPConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/LucenePPConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LucenePP
)
```

---

## Dependency Management

### Option 1: vcpkg (Recommended for Development)

**vcpkg** is a cross-platform package manager from Microsoft.

**Setup**:
```bash
# Install vcpkg
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.sh  # Linux/macOS
# ./bootstrap-vcpkg.bat  # Windows

# Install dependencies
./vcpkg install zlib zstd lz4 gtest benchmark
```

**CMake Integration**:
```bash
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DLUCENEPP_USE_VCPKG=ON
```

**vcpkg.json** (manifest mode):
```json
{
  "name": "lucenepp",
  "version": "1.0.0",
  "dependencies": [
    "zlib",
    "zstd",
    "lz4",
    {
      "name": "gtest",
      "default-features": false
    },
    {
      "name": "benchmark",
      "default-features": false
    }
  ]
}
```

### Option 2: Conan (Recommended for Production)

**Conan** is a decentralized C++ package manager.

**conanfile.txt**:
```ini
[requires]
zlib/1.3
zstd/1.5.5
lz4/1.9.4
gtest/1.14.0
benchmark/1.8.3

[generators]
CMakeDeps
CMakeToolchain

[options]
zlib/*:shared=False
zstd/*:shared=False
lz4/*:shared=False
```

**Build with Conan**:
```bash
# Install dependencies
conan install . --output-folder=build --build=missing

# Configure and build
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake \
    -DLUCENEPP_USE_CONAN=ON

cmake --build build
```

### Option 3: System Packages

Use system package manager for dependencies:

**Ubuntu/Debian**:
```bash
sudo apt-get install \
    zlib1g-dev \
    libzstd-dev \
    liblz4-dev \
    libgtest-dev \
    libbenchmark-dev
```

**macOS (Homebrew)**:
```bash
brew install zlib zstd lz4 googletest google-benchmark
```

**CMake** will find system packages automatically via `find_package()`.

---

## Compiler Flags

### cmake/CompilerFlags.cmake

```cmake
# ==================== Compiler Detection ====================

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(LUCENEPP_COMPILER_GNU_LIKE TRUE)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    set(LUCENEPP_COMPILER_MSVC TRUE)
endif()

# ==================== Warning Flags ====================

if(LUCENEPP_COMPILER_GNU_LIKE)
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Werror                     # Treat warnings as errors
        -Wno-unused-parameter       # Allow unused parameters
        -Wno-sign-compare           # Common in index code
    )
elseif(LUCENEPP_COMPILER_MSVC)
    add_compile_options(
        /W4                         # Warning level 4
        /WX                         # Treat warnings as errors
        /wd4100                     # Unused parameter (common)
        /wd4244                     # Conversion warnings
    )
endif()

# ==================== Optimization Flags ====================

if(CMAKE_BUILD_TYPE MATCHES "Release|RelWithDebInfo")
    if(LUCENEPP_COMPILER_GNU_LIKE)
        add_compile_options(
            -O3                     # Maximum optimization
            -DNDEBUG                # Disable assertions
            -ffast-math             # Fast floating-point math
            -march=native           # Optimize for local CPU (dev builds)
            # -march=x86-64-v3      # Target AVX2 (production builds)
        )
    elseif(LUCENEPP_COMPILER_MSVC)
        add_compile_options(
            /O2                     # Maximum optimization
            /DNDEBUG                # Disable assertions
            /fp:fast                # Fast floating-point
            /arch:AVX2              # Enable AVX2
        )
    endif()
endif()

if(CMAKE_BUILD_TYPE MATCHES "Debug")
    if(LUCENEPP_COMPILER_GNU_LIKE)
        add_compile_options(
            -O0                     # No optimization
            -g                      # Debug symbols
            -fsanitize=address      # Address sanitizer
            -fsanitize=undefined    # Undefined behavior sanitizer
        )
        add_link_options(
            -fsanitize=address
            -fsanitize=undefined
        )
    elseif(LUCENEPP_COMPILER_MSVC)
        add_compile_options(
            /Od                     # No optimization
            /Zi                     # Debug symbols
        )
    endif()
endif()

# ==================== SIMD Flags ====================

if(LUCENEPP_ENABLE_SIMD)
    if(LUCENEPP_COMPILER_GNU_LIKE)
        # Enable SIMD for specific files
        set(LUCENEPP_SIMD_FLAGS
            -mavx2
            -mfma
            -mbmi2
        )
    elseif(LUCENEPP_COMPILER_MSVC)
        set(LUCENEPP_SIMD_FLAGS
            /arch:AVX2
        )
    endif()

    # Apply to SIMD source files only
    # set_source_files_properties(
    #     src/simd/postings_simd.cpp
    #     PROPERTIES COMPILE_FLAGS "${LUCENEPP_SIMD_FLAGS}"
    # )
endif()

# ==================== Link-Time Optimization (LTO) ====================

if(CMAKE_BUILD_TYPE MATCHES "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT IPO_SUPPORTED)

    if(IPO_SUPPORTED)
        message(STATUS "Link-Time Optimization (LTO) enabled")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(WARNING "Link-Time Optimization (LTO) not supported")
    endif()
endif()
```

---

## SIMD Detection

### cmake/SIMDDetection.cmake

```cmake
# ==================== Detect SIMD Capabilities ====================

include(CheckCXXSourceRuns)

# Check for AVX2 support
set(AVX2_CODE "
#include <immintrin.h>
int main() {
    __m256i a = _mm256_set1_epi32(1);
    __m256i b = _mm256_set1_epi32(2);
    __m256i c = _mm256_add_epi32(a, b);
    return _mm256_extract_epi32(c, 0) == 3 ? 0 : 1;
}
")

if(LUCENEPP_ENABLE_SIMD)
    set(CMAKE_REQUIRED_FLAGS "-mavx2")
    check_cxx_source_runs("${AVX2_CODE}" HAVE_AVX2)

    if(HAVE_AVX2)
        message(STATUS "AVX2 support detected")
        add_compile_definitions(LUCENEPP_HAVE_AVX2)
    else()
        message(WARNING "AVX2 not supported - SIMD optimizations disabled")
        set(LUCENEPP_ENABLE_SIMD OFF)
    endif()
endif()

# Check for BMI2 support (for PDEP/PEXT instructions)
set(BMI2_CODE "
#include <immintrin.h>
int main() {
    unsigned long long x = 0xFF00FF00FF00FF00ULL;
    unsigned long long mask = 0x5555555555555555ULL;
    unsigned long long result = _pdep_u64(x, mask);
    return result != 0 ? 0 : 1;
}
")

if(LUCENEPP_ENABLE_SIMD)
    set(CMAKE_REQUIRED_FLAGS "-mbmi2")
    check_cxx_source_runs("${BMI2_CODE}" HAVE_BMI2)

    if(HAVE_BMI2)
        message(STATUS "BMI2 support detected")
        add_compile_definitions(LUCENEPP_HAVE_BMI2)
    endif()
endif()
```

---

## Core Library Build

### src/core/CMakeLists.txt

```cmake
# ==================== Core Library Sources ====================

set(LUCENEPP_CORE_SOURCES
    # Index
    index/IndexReader.cpp
    index/IndexWriter.cpp
    index/DirectoryReader.cpp
    index/SegmentReader.cpp
    index/DocumentsWriter.cpp

    # Search
    search/IndexSearcher.cpp
    search/Query.cpp
    search/TermQuery.cpp
    search/BooleanQuery.cpp
    search/PhraseQuery.cpp
    search/Scorer.cpp
    search/Collector.cpp

    # Codecs
    codecs/Codec.cpp
    codecs/Lucene104Codec.cpp
    codecs/PostingsFormat.cpp
    codecs/DocValuesFormat.cpp

    # Store
    store/Directory.cpp
    store/FSDirectory.cpp
    store/MMapDirectory.cpp
    store/IndexInput.cpp
    store/IndexOutput.cpp

    # Util
    util/BitSet.cpp
    util/BytesRef.cpp
)

set(LUCENEPP_CORE_HEADERS
    include/lucenepp/index/IndexReader.h
    include/lucenepp/index/IndexWriter.h
    include/lucenepp/search/Query.h
    include/lucenepp/search/IndexSearcher.h
    # ... other headers
)

# ==================== Core Library Target ====================

if(LUCENEPP_BUILD_SHARED)
    add_library(lucenepp_core SHARED ${LUCENEPP_CORE_SOURCES})
    target_compile_definitions(lucenepp_core PRIVATE LUCENEPP_BUILDING_DLL)
endif()

if(LUCENEPP_BUILD_STATIC)
    add_library(lucenepp_core_static STATIC ${LUCENEPP_CORE_SOURCES})
    set_target_properties(lucenepp_core_static PROPERTIES OUTPUT_NAME lucenepp_core)
endif()

# Use shared library as default target
add_library(lucenepp_core ALIAS lucenepp_core)

# ==================== Include Directories ====================

target_include_directories(lucenepp_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ==================== Link Libraries ====================

target_link_libraries(lucenepp_core
    PUBLIC
        ZLIB::ZLIB
        zstd::zstd
        LZ4::LZ4
    PRIVATE
        lucenepp_columns
)

# ==================== Compiler Features ====================

target_compile_features(lucenepp_core PUBLIC cxx_std_20)

# ==================== Export Symbols (Windows) ====================

if(WIN32 AND LUCENEPP_BUILD_SHARED)
    target_compile_definitions(lucenepp_core
        PRIVATE LUCENEPP_BUILDING_DLL
        INTERFACE LUCENEPP_USING_DLL
    )
endif()
```

---

## Testing Configuration

### tests/CMakeLists.txt

```cmake
# ==================== Unit Tests ====================

set(UNIT_TEST_SOURCES
    unit/index/IndexWriterTest.cpp
    unit/index/IndexReaderTest.cpp
    unit/search/TermQueryTest.cpp
    unit/search/BooleanQueryTest.cpp
    unit/search/PhraseQueryTest.cpp
    unit/codecs/CodecTest.cpp
    unit/store/DirectoryTest.cpp
)

add_executable(lucenepp_unit_tests ${UNIT_TEST_SOURCES})

target_link_libraries(lucenepp_unit_tests
    PRIVATE
        lucenepp_core
        GTest::gtest
        GTest::gtest_main
)

# Discover tests automatically
include(GoogleTest)
gtest_discover_tests(lucenepp_unit_tests)

# ==================== Integration Tests ====================

set(INTEGRATION_TEST_SOURCES
    integration/EndToEndTest.cpp
    integration/ConcurrencyTest.cpp
    integration/CrashRecoveryTest.cpp
)

add_executable(lucenepp_integration_tests ${INTEGRATION_TEST_SOURCES})

target_link_libraries(lucenepp_integration_tests
    PRIVATE
        lucenepp_core
        GTest::gtest
        GTest::gtest_main
)

gtest_discover_tests(lucenepp_integration_tests)

# ==================== Benchmarks ====================

if(LUCENEPP_BUILD_BENCHMARKS)
    set(BENCHMARK_SOURCES
        benchmark/IndexingBenchmark.cpp
        benchmark/SearchBenchmark.cpp
        benchmark/SIMDBenchmark.cpp
    )

    add_executable(lucenepp_benchmarks ${BENCHMARK_SOURCES})

    target_link_libraries(lucenepp_benchmarks
        PRIVATE
            lucenepp_core
            benchmark::benchmark
            benchmark::benchmark_main
    )
endif()
```

---

## Build Commands

### Development Build (Debug)

```bash
# Configure
cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Debug \
    -DLUCENEPP_BUILD_TESTS=ON \
    -DLUCENEPP_ENABLE_SIMD=ON

# Build
cmake --build build -j$(nproc)

# Run tests
cd build && ctest --output-on-failure
```

### Release Build (Optimized)

```bash
# Configure
cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DLUCENEPP_BUILD_TESTS=OFF \
    -DLUCENEPP_ENABLE_SIMD=ON

# Build with all optimizations
cmake --build build -j$(nproc)

# Install
sudo cmake --install build
```

### Production Build (Specific CPU Target)

```bash
# Configure for x86-64-v3 (AVX2, BMI2, FMA)
cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-march=x86-64-v3" \
    -DLUCENEPP_ENABLE_SIMD=ON

cmake --build build -j$(nproc)
```

### Cross-Compilation (ARM64)

```bash
# Configure for ARM64 with NEON
cmake -B build -S . \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
    -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
    -DLUCENEPP_ENABLE_SIMD=ON

cmake --build build
```

---

## Platform-Specific Notes

### Linux

**Recommended Compilers**:
- GCC 11+ (for C++20 support)
- Clang 14+ (better diagnostics)

**Optimization Flags**:
```bash
-march=native          # Development builds
-march=x86-64-v3       # Production (AVX2)
-mtune=generic         # Portable performance
```

### macOS

**Recommended Compiler**: AppleClang 14+ (Xcode 14+)

**Apple Silicon (M1/M2)**:
- SIMD uses NEON instead of AVX2
- Automatic detection via CMake

**Intel Macs**:
- AVX2 support standard on 2013+ models

### Windows

**Recommended Compiler**: MSVC 2022 (Visual Studio 17.0+)

**MSVC Flags**:
```
/O2                   # Maximum optimization
/arch:AVX2            # Enable AVX2
/GL                   # Whole program optimization
/Gy                   # Function-level linking
```

**Build Command**:
```powershell
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

---

## Continuous Integration

### GitHub Actions Example

**.github/workflows/ci.yml**:
```yaml
name: CI

on: [push, pull_request]

jobs:
  build-linux:
    runs-on: ubuntu-22.04
    strategy:
      matrix:
        compiler: [gcc-11, clang-14]
        build_type: [Debug, Release]

    steps:
    - uses: actions/checkout@v3

    - name: Install dependencies
      run: |
        sudo apt-get update
        sudo apt-get install -y zlib1g-dev libzstd-dev liblz4-dev libgtest-dev

    - name: Configure
      run: |
        cmake -B build -S . \
          -DCMAKE_BUILD_TYPE=${{ matrix.build_type }} \
          -DCMAKE_CXX_COMPILER=${{ matrix.compiler }}

    - name: Build
      run: cmake --build build -j$(nproc)

    - name: Test
      run: cd build && ctest --output-on-failure

  build-macos:
    runs-on: macos-13
    steps:
    - uses: actions/checkout@v3

    - name: Install dependencies
      run: brew install zstd lz4 googletest

    - name: Configure
      run: cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

    - name: Build
      run: cmake --build build -j$(sysctl -n hw.ncpu)

    - name: Test
      run: cd build && ctest --output-on-failure

  build-windows:
    runs-on: windows-2022
    steps:
    - uses: actions/checkout@v3

    - name: Setup vcpkg
      uses: lukka/run-vcpkg@v11
      with:
        vcpkgGitCommitId: 'latest'

    - name: Configure
      run: |
        cmake -B build -S . `
          -DCMAKE_TOOLCHAIN_FILE=${{ github.workspace }}/vcpkg/scripts/buildsystems/vcpkg.cmake

    - name: Build
      run: cmake --build build --config Release

    - name: Test
      run: cd build && ctest -C Release --output-on-failure
```

---

## Summary

**Key Features**:
1. ✅ **CMake 3.20+**: Modern CMake with target-based design
2. ✅ **Cross-platform**: Linux, macOS, Windows support
3. ✅ **Dependency management**: vcpkg, Conan, or system packages
4. ✅ **Compiler optimization**: -O3, LTO, SIMD flags
5. ✅ **SIMD detection**: Automatic AVX2/BMI2 capability detection
6. ✅ **Testing integration**: Google Test for unit/integration tests
7. ✅ **CI/CD ready**: GitHub Actions example provided

**Best Practices**:
- Use vcpkg for development (easy setup)
- Use Conan for production (reproducible builds)
- Enable sanitizers in Debug builds
- Use LTO in Release builds for 5-10% performance gain
- Target x86-64-v3 (AVX2) for production binaries

**Build Times** (estimated):
- Clean build (Release): 5-10 minutes (8-core CPU)
- Incremental build: <30 seconds
- Test suite: 1-2 minutes

---

**Design Status**: Complete ✅
**Next Document**: TESTING_STRATEGY.md
