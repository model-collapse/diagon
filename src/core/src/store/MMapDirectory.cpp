// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/store/MMapDirectory.h"

#include "diagon/store/PosixMMapIndexInput.h"
#ifdef _WIN32
#include "diagon/store/WindowsMMapIndexInput.h"
#endif
#include "diagon/util/Exceptions.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace diagon::store {

// ==================== Static Factory Methods ====================

std::unique_ptr<MMapDirectory> MMapDirectory::open(const std::filesystem::path& path) {
    return std::make_unique<MMapDirectory>(path);
}

std::unique_ptr<MMapDirectory> MMapDirectory::open(const std::filesystem::path& path,
                                                    int chunk_power) {
    return std::make_unique<MMapDirectory>(path, chunk_power);
}

// ==================== Constructors ====================

MMapDirectory::MMapDirectory(const std::filesystem::path& path)
    : FSDirectory(path),
      chunk_power_(getDefaultChunkPower()),
      preload_(false),
      use_fallback_(false) {
    // FSDirectory constructor validates that path exists and is a directory
}

MMapDirectory::MMapDirectory(const std::filesystem::path& path, int chunk_power)
    : FSDirectory(path),
      chunk_power_(chunk_power),
      preload_(false),
      use_fallback_(false) {
    // Validate chunk power
    validateChunkPower(chunk_power);
}

// ==================== Stream Creation ====================

std::unique_ptr<IndexInput> MMapDirectory::openInput(const std::string& name,
                                                      const IOContext& context) const {
    ensureOpen();

    auto file_path = getPath().value() / name;

    // Check file exists - these errors always throw, no fallback
    if (!std::filesystem::exists(file_path)) {
        throw FileNotFoundException("File not found: " + name);
    }

    // Check it's a regular file - these errors always throw, no fallback
    if (!std::filesystem::is_regular_file(file_path)) {
        throw IOException("Not a regular file: " + name);
    }

    try {
        // Get read advice from IOContext
        IOContext::ReadAdvice advice = context.getReadAdvice();

        // Platform-specific creation
#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
        return std::make_unique<PosixMMapIndexInput>(file_path, chunk_power_, preload_, advice);
#elif defined(_WIN32)
        return std::make_unique<WindowsMMapIndexInput>(file_path, chunk_power_, preload_, advice);
#else
        throw UnsupportedOperationException(
            "MMapDirectory not supported on this platform. Use FSDirectory instead.");
#endif
    } catch (const FileNotFoundException&) {
        // File-related errors always throw, never fall back
        throw;
    } catch (const IOException& e) {
        // Memory mapping failed - check if fallback is enabled
        if (use_fallback_) {
            // Log warning to stderr
            std::cerr << "WARNING: MMapDirectory failed to map file '" << name << "': "
                      << e.what() << "\n";
            std::cerr << "         Falling back to buffered I/O (FSDirectory). "
                      << "Performance will be reduced.\n";
            std::cerr << "         To avoid this warning, use FSDirectory directly "
                      << "or fix the underlying mmap issue.\n";

            // Fall back to FSDirectory implementation
            return FSDirectory::openInput(name, context);
        }

        // Fallback disabled, re-throw
        throw;
    } catch (const UnsupportedOperationException&) {
        // Platform not supported - check if fallback is enabled
        if (use_fallback_) {
            std::cerr << "WARNING: MMapDirectory not supported on this platform.\n";
            std::cerr << "         Falling back to buffered I/O (FSDirectory).\n";

            // Fall back to FSDirectory implementation
            return FSDirectory::openInput(name, context);
        }

        // Fallback disabled, re-throw
        throw;
    } catch (const std::exception& e) {
        // Wrap other exceptions
        throw IOException("Failed to open memory-mapped file '" + name + "': " + e.what());
    }
}

// ==================== Utilities ====================

std::string MMapDirectory::toString() const {
    std::ostringstream oss;
    oss << "MMapDirectory@" << getPath().value().string();
    oss << " (chunk=" << (getChunkSize() / (1024 * 1024)) << "MB";
    if (preload_) {
        oss << ", preload=true";
    }
    if (use_fallback_) {
        oss << ", fallback=true";
    }
    oss << ")";
    return oss.str();
}

// ==================== Private Static Methods ====================

int MMapDirectory::getDefaultChunkPower() {
    // Detect system architecture
    if (sizeof(void*) == 8) {
        // 64-bit: Use 16GB chunks (2^34)
        return DEFAULT_CHUNK_POWER_64;
    } else {
        // 32-bit: Use 256MB chunks (2^28) to avoid address space exhaustion
        return DEFAULT_CHUNK_POWER_32;
    }
}

void MMapDirectory::validateChunkPower(int chunk_power) {
    // Chunk size must be between 2^20 (1MB) and 2^40 (1TB)
    constexpr int MIN_CHUNK_POWER = 20;  // 1MB
    constexpr int MAX_CHUNK_POWER = 40;  // 1TB

    if (chunk_power < MIN_CHUNK_POWER || chunk_power > MAX_CHUNK_POWER) {
        std::ostringstream oss;
        oss << "Invalid chunk_power: " << chunk_power;
        oss << " (must be between " << MIN_CHUNK_POWER << " and " << MAX_CHUNK_POWER << ")";
        oss << ". Corresponds to chunk sizes between 1MB and 1TB.";
        throw std::invalid_argument(oss.str());
    }

    // Warn if chunk power is unusually large on 32-bit systems
    if (sizeof(void*) == 4 && chunk_power > 30) {
        // Log warning: chunk size > 1GB on 32-bit may exhaust address space
        // For now, allow but consider logging in the future
    }
}

}  // namespace diagon::store
