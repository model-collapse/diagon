# Building Diagon

This document provides detailed instructions for building Diagon from source.

## Table of Contents
- [Prerequisites](#prerequisites)
- [Dependency Management](#dependency-management)
- [Building](#building)
- [Platform-Specific Notes](#platform-specific-notes)
- [Build Options](#build-options)
- [Troubleshooting](#troubleshooting)

## Prerequisites

### Compiler Requirements
- **GCC**: 11.0 or higher (for C++20 support)
- **Clang**: 14.0 or higher
- **MSVC**: Visual Studio 2022 or higher

### Build Tools
- **CMake**: 3.20 or higher
- **Make/Ninja**: Build system generator

### Required Dependencies
- **ZLIB**: Compression and checksums
- **LZ4**: Fast compression (1.9.4+)
- **ZSTD**: High-ratio compression (1.5.5+)

### Optional Dependencies
- **Google Test**: For unit tests (1.14.0+)
- **Google Benchmark**: For performance benchmarks (1.8.3+)

## Dependency Management

Diagon supports three dependency management approaches:

### Option 1: vcpkg (Recommended for Development)

**Advantages**: Cross-platform, easy setup, consistent versions

**Setup**:
```bash
# Install vcpkg
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.sh  # Linux/macOS
# ./bootstrap-vcpkg.bat  # Windows

# Install dependencies (optional - manifest mode auto-installs)
./vcpkg install zlib zstd lz4 gtest benchmark
```

**Build**:
```bash
cd /path/to/diagon
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release

cmake --build build -j$(nproc)
```

**Note**: Diagon includes a `vcpkg.json` manifest file, so dependencies are automatically installed when using vcpkg.

### Option 2: Conan (Recommended for Production)

**Advantages**: Reproducible builds, binary caching, lockfiles

**Setup**:
```bash
# Install Conan 2.x
pip install conan

# Create conanfile.txt (or use provided one)
```

**Build**:
```bash
cd /path/to/diagon

# Install dependencies
conan install . --output-folder=build --build=missing

# Configure and build
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Release

cmake --build build -j$(nproc)
```

### Option 3: System Packages

**Advantages**: Native integration, system-wide caching

#### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    zlib1g-dev \
    libzstd-dev \
    liblz4-dev \
    libgtest-dev \
    libbenchmark-dev
```

#### Fedora/RHEL
```bash
sudo dnf install -y \
    gcc-c++ \
    cmake \
    zlib-devel \
    libzstd-devel \
    lz4-devel \
    gtest-devel \
    benchmark-devel
```

#### macOS (Homebrew)
```bash
brew install cmake zlib zstd lz4 googletest google-benchmark
```

#### Windows (vcpkg recommended)
See Option 1 above. Alternatively, use Conan or build dependencies from source.

**Build**:
```bash
cd /path/to/diagon
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## Building

### Quick Build (Default Configuration)
```bash
# Clone repository
git clone https://github.com/yourusername/diagon.git
cd diagon

# Configure (Release build with tests)
cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DDIAGON_BUILD_TESTS=ON

# Build
cmake --build build -j$(nproc)

# Run tests
cd build && ctest --output-on-failure
```

### Development Build (Debug with Sanitizers)
```bash
cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Debug \
    -DDIAGON_BUILD_TESTS=ON \
    -DCMAKE_CXX_FLAGS="-fsanitize=address -fsanitize=undefined"

cmake --build build -j$(nproc)
```

### Production Build (Optimized)
```bash
cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DDIAGON_BUILD_TESTS=OFF \
    -DDIAGON_ENABLE_SIMD=ON \
    -DCMAKE_CXX_FLAGS="-march=x86-64-v3"  # Target AVX2

cmake --build build -j$(nproc)
```

### Build with Benchmarks
```bash
cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DDIAGON_BUILD_BENCHMARKS=ON

cmake --build build -j$(nproc)
./build/diagon_benchmarks
```

## Platform-Specific Notes

### Linux

**Recommended Compilers**:
- GCC 11+ for production (better optimization)
- Clang 14+ for development (better diagnostics)

**CPU Optimization**:
```bash
# Development: Optimize for local CPU
cmake -B build -DCMAKE_CXX_FLAGS="-march=native"

# Production: Target x86-64-v3 (AVX2, BMI2, FMA)
cmake -B build -DCMAKE_CXX_FLAGS="-march=x86-64-v3"
```

**SIMD Support**: AVX2 automatically detected on x86-64

### macOS

**Apple Silicon (M1/M2)**:
- SIMD uses ARM NEON instead of AVX2
- Automatic detection via CMake
- Performance comparable to AVX2

**Intel Macs**:
- AVX2 support standard on 2013+ models
- Use `-march=native` for optimization

**Build**:
```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(sysctl -n hw.ncpu)
```

### Windows

**Visual Studio 2022**:
```powershell
# Configure
cmake -B build -S . -G "Visual Studio 17 2022" -A x64

# Build
cmake --build build --config Release

# Run tests
cd build
ctest -C Release --output-on-failure
```

**MSVC Optimizations**:
- `/O2`: Maximum speed optimization
- `/arch:AVX2`: Enable AVX2 instructions
- `/GL /LTCG`: Link-time code generation

## Build Options

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `DIAGON_BUILD_TESTS` | `ON` | Build unit and integration tests |
| `DIAGON_BUILD_BENCHMARKS` | `OFF` | Build performance benchmarks |
| `DIAGON_BUILD_SHARED` | `ON` | Build shared libraries |
| `DIAGON_BUILD_STATIC` | `ON` | Build static libraries |
| `DIAGON_ENABLE_SIMD` | `ON` | Enable SIMD optimizations (AVX2/NEON) |
| `DIAGON_USE_VCPKG` | `OFF` | Use vcpkg for dependencies |
| `DIAGON_USE_CONAN` | `OFF` | Use Conan for dependencies |

### Examples

**Minimal build (no tests, static only)**:
```bash
cmake -B build -S . \
    -DDIAGON_BUILD_TESTS=OFF \
    -DDIAGON_BUILD_BENCHMARKS=OFF \
    -DDIAGON_BUILD_SHARED=OFF
```

**Disable SIMD (for compatibility)**:
```bash
cmake -B build -S . -DDIAGON_ENABLE_SIMD=OFF
```

**Static libraries only**:
```bash
cmake -B build -S . -DDIAGON_BUILD_SHARED=OFF
```

## Installation

### System-Wide Installation
```bash
cmake --build build
sudo cmake --install build
```

**Default paths**:
- Headers: `/usr/local/include/diagon/`
- Libraries: `/usr/local/lib/`
- CMake config: `/usr/local/lib/cmake/Diagon/`

### Custom Installation Prefix
```bash
cmake -B build -S . -DCMAKE_INSTALL_PREFIX=/opt/diagon
cmake --install build
```

### Using Installed Diagon

**CMakeLists.txt**:
```cmake
find_package(Diagon REQUIRED)

add_executable(myapp main.cpp)
target_link_libraries(myapp
    Diagon::diagon_core
    Diagon::diagon_columns
    Diagon::diagon_compression
    Diagon::diagon_simd
)
```

## Troubleshooting

### CMake Cannot Find Dependencies

**Problem**: `Could not find ZLIB`, `Could not find LZ4`, etc.

**Solutions**:
1. Use vcpkg (recommended):
   ```bash
   cmake -B build -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
   ```

2. Specify paths manually:
   ```bash
   cmake -B build -DZLIB_ROOT=/path/to/zlib -DLZ4_ROOT=/path/to/lz4
   ```

3. Install system packages (see Option 3 above)

### SIMD Not Detected

**Problem**: `AVX2 not supported - SIMD optimizations disabled`

**Check CPU support**:
```bash
# Linux
lscpu | grep avx2

# macOS
sysctl machdep.cpu.features | grep AVX2
```

**Solution**: If CPU doesn't support AVX2, disable SIMD or use scalar fallback:
```bash
cmake -B build -DDIAGON_ENABLE_SIMD=OFF
```

### Compiler Version Too Old

**Problem**: `C++20 features not supported`

**Solution**: Upgrade compiler or use newer toolchain:
```bash
# Ubuntu: Install GCC 11
sudo apt-get install gcc-11 g++-11
cmake -B build -DCMAKE_CXX_COMPILER=g++-11
```

### Link Errors on Windows

**Problem**: `unresolved external symbol` errors

**Solution**: Ensure consistent build configuration:
```powershell
# Use same config for all builds
cmake --build build --config Release
ctest -C Release
```

### Out of Memory During Build

**Problem**: Compilation runs out of memory

**Solution**: Reduce parallel jobs:
```bash
# Instead of -j$(nproc), use fewer jobs
cmake --build build -j2
```

## Build Times

Estimated build times (Release, 8-core CPU):

| Target | Time |
|--------|------|
| Clean build (all modules) | 5-10 minutes |
| Incremental build (one file changed) | <30 seconds |
| Test suite | 1-2 minutes |

## Next Steps

- Read [README.md](README.md) for usage examples
- Explore [design/](design/) for architecture details
- Run tests: `cd build && ctest`
- Run benchmarks: `./build/diagon_benchmarks`

---

**Last Updated**: 2026-01-24
