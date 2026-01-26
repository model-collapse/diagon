// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <cstddef>
#include <cstdint>

// Platform detection and SIMD intrinsics
#if defined(__AVX2__)
    #include <immintrin.h>  // AVX2
    #define DIAGON_HAVE_AVX2 1
    #define DIAGON_SIMD_WIDTH_BYTES 32
    #define DIAGON_SIMD_WIDTH_I32 8
    #define DIAGON_SIMD_WIDTH_F32 8
#elif defined(__SSE4_2__)
    #include <nmmintrin.h>  // SSE4.2
    #define DIAGON_HAVE_SSE4_2 1
    #define DIAGON_SIMD_WIDTH_BYTES 16
    #define DIAGON_SIMD_WIDTH_I32 4
    #define DIAGON_SIMD_WIDTH_F32 4
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    #include <arm_neon.h>
    #define DIAGON_HAVE_NEON 1
    #define DIAGON_SIMD_WIDTH_BYTES 16
    #define DIAGON_SIMD_WIDTH_I32 4
    #define DIAGON_SIMD_WIDTH_F32 4
#else
    // Scalar fallback
    #define DIAGON_SIMD_WIDTH_BYTES 8
    #define DIAGON_SIMD_WIDTH_I32 1
    #define DIAGON_SIMD_WIDTH_F32 1
#endif

// FMA (Fused Multiply-Add) detection
#if defined(__FMA__)
    #define DIAGON_HAVE_FMA 1
#endif

namespace diagon {
namespace util {
namespace simd {

/**
 * @brief Prefetch utilities for reducing cache miss penalties
 *
 * Provides cross-platform prefetch instructions to hint the CPU
 * about future memory accesses, reducing cache miss latency.
 */
class Prefetch {
public:
    /**
     * @brief Prefetch hint: which cache level to target
     */
    enum class Locality {
        // Non-temporal: bypass cache (for data used once)
        NTA = 0,
        // Low temporal locality: L3 cache
        LOW = 1,
        // Medium temporal locality: L2 cache
        MEDIUM = 2,
        // High temporal locality: L1 cache
        HIGH = 3
    };

    /**
     * @brief Prefetch data for reading
     *
     * Hints the CPU to load the cache line containing 'addr' into cache.
     * This is a hint and may be ignored by the CPU.
     *
     * @param addr Memory address to prefetch
     * @param locality Cache level hint (default: HIGH = L1 cache)
     *
     * Usage:
     *   Prefetch::read(&data[i + LOOKAHEAD_DISTANCE]);
     */
    static inline void read(const void* addr, Locality locality = Locality::HIGH) {
#if defined(__GNUC__) || defined(__clang__)
        // GCC/Clang builtin: __builtin_prefetch(addr, rw, locality)
        // rw=0 for read, rw=1 for write
        // locality: 0=NTA, 1=LOW, 2=MEDIUM, 3=HIGH
        // Note: locality must be a compile-time constant
        switch (locality) {
            case Locality::HIGH:
                __builtin_prefetch(addr, 0, 3);
                break;
            case Locality::MEDIUM:
                __builtin_prefetch(addr, 0, 2);
                break;
            case Locality::LOW:
                __builtin_prefetch(addr, 0, 1);
                break;
            case Locality::NTA:
                __builtin_prefetch(addr, 0, 0);
                break;
        }
#elif defined(_MSC_VER)
        // MSVC intrinsics
        switch (locality) {
            case Locality::HIGH:
                _mm_prefetch(static_cast<const char*>(addr), _MM_HINT_T0);  // L1
                break;
            case Locality::MEDIUM:
                _mm_prefetch(static_cast<const char*>(addr), _MM_HINT_T1);  // L2
                break;
            case Locality::LOW:
                _mm_prefetch(static_cast<const char*>(addr), _MM_HINT_T2);  // L3
                break;
            case Locality::NTA:
                _mm_prefetch(static_cast<const char*>(addr), _MM_HINT_NTA); // Non-temporal
                break;
        }
#else
        // No-op on unsupported platforms
        (void)addr;
        (void)locality;
#endif
    }

    /**
     * @brief Prefetch data for writing
     *
     * Hints the CPU to load the cache line in exclusive state for writing.
     *
     * @param addr Memory address to prefetch
     * @param locality Cache level hint (default: HIGH = L1 cache)
     */
    static inline void write(void* addr, Locality locality = Locality::HIGH) {
#if defined(__GNUC__) || defined(__clang__)
        // Note: locality must be a compile-time constant
        switch (locality) {
            case Locality::HIGH:
                __builtin_prefetch(addr, 1, 3);
                break;
            case Locality::MEDIUM:
                __builtin_prefetch(addr, 1, 2);
                break;
            case Locality::LOW:
                __builtin_prefetch(addr, 1, 1);
                break;
            case Locality::NTA:
                __builtin_prefetch(addr, 1, 0);
                break;
        }
#elif defined(_MSC_VER)
        // MSVC doesn't distinguish read/write prefetch, use read variant
        read(addr, locality);
#else
        (void)addr;
        (void)locality;
#endif
    }

    /**
     * @brief Prefetch multiple cache lines
     *
     * Useful for prefetching large contiguous regions.
     *
     * @param addr Starting address
     * @param size Size in bytes to prefetch
     * @param locality Cache level hint
     *
     * Usage:
     *   Prefetch::readRange(large_buffer, 4096);  // Prefetch 4KB
     */
    static inline void readRange(const void* addr, size_t size, Locality locality = Locality::HIGH) {
        constexpr size_t CACHE_LINE_SIZE = 64;  // Typical x86/ARM cache line

        const uint8_t* ptr = static_cast<const uint8_t*>(addr);
        const uint8_t* end = ptr + size;

        // Prefetch every cache line in the range
        for (; ptr < end; ptr += CACHE_LINE_SIZE) {
            read(ptr, locality);
        }
    }
};

/**
 * @brief SIMD operation hints for optimal prefetch distance
 *
 * Different operations have different optimal prefetch distances
 * based on memory access patterns and computation intensity.
 */
struct PrefetchDistance {
    // Sequential scan: moderate lookahead (8-16 cache lines)
    static constexpr size_t SEQUENTIAL_SCAN = 512;  // 8 cache lines

    // Random access: shorter lookahead (2-4 cache lines)
    static constexpr size_t RANDOM_ACCESS = 128;    // 2 cache lines

    // Heavy computation: longer lookahead (16-32 cache lines)
    static constexpr size_t COMPUTE_INTENSIVE = 1024;  // 16 cache lines

    // Posting list iteration: moderate (4-8 cache lines)
    static constexpr size_t POSTING_LIST = 256;     // 4 cache lines
};

/**
 * @brief Memory alignment utilities for SIMD operations
 */
class Alignment {
public:
    /**
     * @brief Check if pointer is aligned to specified boundary
     *
     * @param ptr Pointer to check
     * @param alignment Alignment requirement in bytes (must be power of 2)
     * @return true if aligned, false otherwise
     */
    static inline bool isAligned(const void* ptr, size_t alignment) {
        return (reinterpret_cast<uintptr_t>(ptr) & (alignment - 1)) == 0;
    }

    /**
     * @brief Check if pointer is aligned for SIMD operations
     */
    static inline bool isSIMDAligned(const void* ptr) {
        return isAligned(ptr, DIAGON_SIMD_WIDTH_BYTES);
    }

    /**
     * @brief Align pointer up to specified boundary
     *
     * @param ptr Pointer to align
     * @param alignment Alignment in bytes (must be power of 2)
     * @return Aligned pointer (may be same as input if already aligned)
     */
    static inline const void* alignUp(const void* ptr, size_t alignment) {
        uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
        return reinterpret_cast<const void*>(aligned);
    }

    /**
     * @brief Calculate number of bytes until next alignment boundary
     *
     * @param ptr Pointer to check
     * @param alignment Alignment requirement
     * @return Number of bytes to skip to reach alignment (0 if already aligned)
     */
    static inline size_t bytesToAlign(const void* ptr, size_t alignment) {
        uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        uintptr_t misalignment = addr & (alignment - 1);
        return misalignment == 0 ? 0 : alignment - misalignment;
    }
};

/**
 * @brief Cache line size constants
 */
struct CacheConstants {
    // Typical cache line size on x86 and ARM
    static constexpr size_t LINE_SIZE = 64;

    // L1 cache size (typical)
    static constexpr size_t L1_SIZE = 32 * 1024;      // 32 KB

    // L2 cache size (typical)
    static constexpr size_t L2_SIZE = 256 * 1024;     // 256 KB

    // L3 cache size (typical, varies widely)
    static constexpr size_t L3_SIZE = 8 * 1024 * 1024;  // 8 MB
};

}  // namespace simd
}  // namespace util
}  // namespace diagon
