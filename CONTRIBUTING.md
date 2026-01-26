# Contributing to Diagon

Thank you for your interest in contributing to Diagon! This document provides guidelines and information for contributors.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [Development Workflow](#development-workflow)
- [Code Style](#code-style)
- [Testing](#testing)
- [Pull Request Process](#pull-request-process)
- [Commit Messages](#commit-messages)

## Code of Conduct

This project adheres to the [Contributor Covenant Code of Conduct](CODE_OF_CONDUCT.md). By participating, you are expected to uphold this code. Please report unacceptable behavior to the project maintainers.

Key principles:
- **Be respectful**: Treat all contributors with respect and professionalism
- **Be constructive**: Provide helpful feedback and constructive criticism
- **Be collaborative**: Work together to improve the project
- **Be inclusive**: Welcome contributors of all backgrounds and experience levels

## Getting Started

### Prerequisites

- C++20 compatible compiler (GCC 11+, Clang 14+)
- CMake 3.20+
- Git
- Dependencies: LZ4, ZSTD, zlib, Google Test, Google Benchmark

### Building

```bash
# Clone the repository
git clone https://github.com/model-collapse/diagon.git
cd diagon

# Configure
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DDIAGON_BUILD_TESTS=ON \
  -DDIAGON_BUILD_BENCHMARKS=ON

# Build
cmake --build build --parallel

# Run tests
cd build && ctest --output-on-failure
```

### IDE Setup

**Visual Studio Code:**
- Install C/C++ extension
- Install CMake Tools extension
- The project includes `.clang-format` for automatic formatting

**CLion:**
- Open the project directory
- CLion will automatically detect CMakeLists.txt
- Enable ClangFormat in Settings → Editor → Code Style → C/C++

## Development Workflow

1. **Fork and Clone**
   ```bash
   git clone https://github.com/YOUR_USERNAME/diagon.git
   cd diagon
   git remote add upstream https://github.com/model-collapse/diagon.git
   ```

2. **Create a Branch**
   ```bash
   git checkout -b feature/your-feature-name
   # or
   git checkout -b fix/your-bug-fix
   ```

3. **Make Changes**
   - Write code following our style guide
   - Add tests for new functionality
   - Update documentation as needed

4. **Test Locally**
   ```bash
   cmake --build build --parallel
   cd build && ctest --output-on-failure
   ```

5. **Commit and Push**
   ```bash
   git add .
   git commit -m "Brief description of changes"
   git push origin feature/your-feature-name
   ```

6. **Create Pull Request**
   - Go to GitHub and create a PR from your branch
   - Fill out the PR template
   - Wait for CI checks to pass
   - Address review feedback

## Code Style

### Formatting

We use **clang-format** for consistent code formatting:

```bash
# Format a single file
clang-format -i src/your_file.cpp

# Format all C++ files
find src tests -name '*.cpp' -o -name '*.h' | xargs clang-format -i
```

### Style Guidelines

- **Indentation**: 4 spaces (no tabs)
- **Line length**: 100 characters maximum
- **Naming conventions**:
  - Classes: `PascalCase` (e.g., `BM25Scorer`)
  - Functions: `camelCase` (e.g., `computeScore()`)
  - Variables: `camelCase` (e.g., `termFrequency`)
  - Private members: `camelCase_` with trailing underscore (e.g., `data_`)
  - Constants: `UPPER_SNAKE_CASE` (e.g., `MAX_BUFFER_SIZE`)
- **Braces**: K&R style (opening brace on same line)
- **Includes**: Group in order:
  1. Project headers (`"diagon/..."`)
  2. Other project headers
  3. External library headers (`<lz4.h>`)
  4. System C++ headers (`<vector>`)
  5. System C headers (`<stdio.h>`)

### Example

```cpp
// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/search/BM25Scorer.h"

#include <algorithm>
#include <vector>

namespace diagon {
namespace search {

class ExampleClass {
public:
    ExampleClass(int value) : value_(value) {}

    int computeSum(const std::vector<int>& numbers) {
        int sum = 0;
        for (int num : numbers) {
            sum += num;
        }
        return sum;
    }

private:
    int value_;
};

}  // namespace search
}  // namespace diagon
```

## Testing

### Writing Tests

- All new features must include tests
- Use Google Test framework
- Place tests in `tests/unit/` directory
- Test file naming: `*Test.cpp`

**Example:**

```cpp
#include "diagon/your/feature.h"
#include <gtest/gtest.h>

TEST(YourFeatureTest, BasicFunctionality) {
    YourClass obj;
    EXPECT_EQ(42, obj.getValue());
}

TEST(YourFeatureTest, EdgeCase) {
    YourClass obj;
    EXPECT_THROW(obj.invalidOperation(), std::runtime_error);
}
```

### Running Tests

```bash
# All tests
cd build && ctest

# Specific test
cd build && ./tests/YourFeatureTest

# With verbose output
cd build && ctest --output-on-failure --verbose
```

### Benchmarks

For performance-sensitive code, add benchmarks:

```cpp
#include <benchmark/benchmark.h>
#include "diagon/your/feature.h"

static void BM_YourFeature(benchmark::State& state) {
    YourClass obj;
    for (auto _ : state) {
        benchmark::DoNotOptimize(obj.operation());
    }
}
BENCHMARK(BM_YourFeature);

BENCHMARK_MAIN();
```

## Pull Request Process

### Before Submitting

- [ ] All tests pass locally
- [ ] Code follows style guidelines (run clang-format)
- [ ] New tests added for new functionality
- [ ] Documentation updated (if applicable)
- [ ] Commit messages follow guidelines

### PR Checklist

1. **Title**: Clear, concise description (< 72 characters)
2. **Description**: Explain what, why, and how
3. **Tests**: Ensure CI passes
4. **Documentation**: Update relevant docs
5. **Breaking Changes**: Clearly marked and explained

### Review Process

- Maintainers will review your PR within 48 hours
- Address feedback promptly
- Keep the PR focused (one feature/fix per PR)
- Squash commits before merging (if requested)

## Commit Messages

### Format

```
Brief summary (50 chars or less)

More detailed explanation (if needed). Wrap at 72 characters.
Explain the problem being solved, not just what changed.

- Bullet points are okay
- Use present tense: "Add feature" not "Added feature"
- Reference issues: "Fixes #123" or "Related to #456"
```

### Examples

**Good:**
```
Add AVX2 SIMD BM25 scorer for 4-8x speedup

Implements vectorized BM25 scoring using AVX2 intrinsics to process
8 documents in parallel. Includes fallback to scalar implementation
for non-AVX2 CPUs.

Performance: 4-8x faster on AVX2 hardware
Tests: 10 new unit tests added
```

**Bad:**
```
fixed stuff
```

### Types

- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation only
- `style`: Formatting, no code change
- `refactor`: Code restructuring
- `perf`: Performance improvement
- `test`: Adding tests
- `chore`: Maintenance tasks

## Storage Directories

Diagon supports multiple storage directory implementations for different use cases:

### FSDirectory (Buffered I/O)

Standard filesystem directory with 8KB buffering. Use for:
- Writing operations (indexing, merging)
- Mixed read/write workloads
- Small files or limited memory
- Cross-platform compatibility

```cpp
auto dir = diagon::store::FSDirectory::open("/path/to/index");
auto input = dir->openInput("data.bin", IOContext::DEFAULT);
```

### MMapDirectory (Memory-Mapped I/O)

Zero-copy memory-mapped file access for read-heavy workloads. Use for:
- Read-only operations (searching, analyzing)
- Large files with random access patterns
- High-performance search queries
- Systems with sufficient RAM

```cpp
auto dir = diagon::store::MMapDirectory::open("/path/to/index");

// Configure chunk size (default: 16GB on 64-bit, 256MB on 32-bit)
auto custom_dir = diagon::store::MMapDirectory::open("/path/to/index", 30);  // 1GB chunks

// Enable preload (load all pages into RAM immediately)
dir->setPreload(true);

// Enable fallback to FSDirectory on mmap failure
dir->setUseFallback(true);

auto input = dir->openInput("data.bin", IOContext::READ);
```

**Performance characteristics:**
- Sequential reads: 10-20% faster than FSDirectory
- Random reads: 2-3× faster than FSDirectory
- Clone operations: ~100× faster (zero-copy)

**Configuration:**

| Option | Default | Description |
|--------|---------|-------------|
| `chunk_power` | 34 (64-bit)<br>28 (32-bit) | Power-of-2 for chunk size<br>Valid range: 20-40 (1MB-1TB) |
| `preload` | `false` | Load all pages into RAM on open |
| `use_fallback` | `false` | Fall back to FSDirectory on mmap failure |

**IOContext read advice:**
- `IOContext::MERGE` → SEQUENTIAL (aggressive read-ahead)
- `IOContext::READ` → RANDOM (disable read-ahead)
- `IOContext::DEFAULT` → NORMAL (default OS behavior)

**Platform support:**
- ✅ Linux (POSIX mmap)
- ✅ macOS (POSIX mmap)
- ⏸️ Windows (deferred to future release)

**Fallback mechanism:**

When `setUseFallback(true)` is enabled, MMapDirectory automatically falls back to FSDirectory if memory mapping fails (e.g., ENOMEM, platform not supported). Useful for:
- 32-bit systems with limited address space
- Restrictive ulimit configurations
- Testing environments

```cpp
auto dir = diagon::store::MMapDirectory::open("/path/to/index");
dir->setUseFallback(true);

// If mmap fails, will automatically use buffered I/O with warning to stderr
auto input = dir->openInput("data.bin", IOContext::DEFAULT);
```

**System requirements:**

For best performance with MMapDirectory:
- Check virtual memory limits: `ulimit -v`
- Check max memory mappings (Linux): `sysctl vm.max_map_count`
- Recommended: `vm.max_map_count >= 262144` (default on most systems)

### ByteBuffersDirectory (In-Memory)

In-memory directory for testing. Use for:
- Unit tests
- Temporary indexes
- Development and debugging

```cpp
auto dir = std::make_unique<diagon::store::ByteBuffersDirectory>();
```

### Choosing a Directory

| Use Case | Recommended Directory |
|----------|----------------------|
| Indexing documents | FSDirectory |
| Search queries (read-only) | MMapDirectory |
| Testing | ByteBuffersDirectory |
| Small indexes (<1GB) | FSDirectory |
| Large indexes (>10GB) | MMapDirectory (read), FSDirectory (write) |
| 32-bit systems | FSDirectory or MMapDirectory with small chunks |

## Questions?

- **Issues**: [GitHub Issues](https://github.com/model-collapse/diagon/issues)
- **Discussions**: [GitHub Discussions](https://github.com/model-collapse/diagon/discussions)
- **Documentation**: See [docs/](docs/) directory

Thank you for contributing to Diagon! 🚀
