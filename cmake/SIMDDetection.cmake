# ==================== Detect SIMD Capabilities ====================

include(CheckCXXSourceRuns)

message(STATUS "Detecting SIMD capabilities...")

# ==================== AVX2 Detection ====================

set(AVX2_CODE "
#include <immintrin.h>
int main() {
    __m256i a = _mm256_set1_epi32(1);
    __m256i b = _mm256_set1_epi32(2);
    __m256i c = _mm256_add_epi32(a, b);
    return _mm256_extract_epi32(c, 0) == 3 ? 0 : 1;
}
")

if(DIAGON_ENABLE_SIMD AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(CMAKE_REQUIRED_FLAGS "-mavx2")
    check_cxx_source_runs("${AVX2_CODE}" HAVE_AVX2)

    if(HAVE_AVX2)
        message(STATUS "AVX2 support detected")
        add_compile_definitions(DIAGON_HAVE_AVX2)
    else()
        message(WARNING "AVX2 not supported - SIMD optimizations disabled")
        set(DIAGON_ENABLE_SIMD OFF CACHE BOOL "Enable SIMD optimizations" FORCE)
    endif()
endif()

# ==================== BMI2 Detection (PDEP/PEXT) ====================

set(BMI2_CODE "
#include <immintrin.h>
int main() {
    unsigned long long x = 0xFF00FF00FF00FF00ULL;
    unsigned long long mask = 0x5555555555555555ULL;
    unsigned long long result = _pdep_u64(x, mask);
    return result != 0 ? 0 : 1;
}
")

if(DIAGON_ENABLE_SIMD AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(CMAKE_REQUIRED_FLAGS "-mbmi2")
    check_cxx_source_runs("${BMI2_CODE}" HAVE_BMI2)

    if(HAVE_BMI2)
        message(STATUS "BMI2 support detected")
        add_compile_definitions(DIAGON_HAVE_BMI2)
    else()
        message(STATUS "BMI2 not supported")
    endif()
endif()

# ==================== FMA Detection ====================

set(FMA_CODE "
#include <immintrin.h>
int main() {
    __m256 a = _mm256_set1_ps(2.0f);
    __m256 b = _mm256_set1_ps(3.0f);
    __m256 c = _mm256_set1_ps(4.0f);
    __m256 result = _mm256_fmadd_ps(a, b, c);
    return _mm256_cvtss_f32(result) == 10.0f ? 0 : 1;
}
")

if(DIAGON_ENABLE_SIMD AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(CMAKE_REQUIRED_FLAGS "-mavx2 -mfma")
    check_cxx_source_runs("${FMA_CODE}" HAVE_FMA)

    if(HAVE_FMA)
        message(STATUS "FMA support detected")
        add_compile_definitions(DIAGON_HAVE_FMA)
    else()
        message(STATUS "FMA not supported")
    endif()
endif()

# ==================== ARM NEON Detection ====================

if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64|arm64")
    set(NEON_CODE "
    #include <arm_neon.h>
    int main() {
        int32x4_t a = vdupq_n_s32(1);
        int32x4_t b = vdupq_n_s32(2);
        int32x4_t c = vaddq_s32(a, b);
        return vgetq_lane_s32(c, 0) == 3 ? 0 : 1;
    }
    ")

    check_cxx_source_runs("${NEON_CODE}" HAVE_NEON)

    if(HAVE_NEON)
        message(STATUS "ARM NEON support detected")
        add_compile_definitions(DIAGON_HAVE_NEON)
        set(DIAGON_ENABLE_SIMD ON)
    else()
        message(STATUS "ARM NEON not supported")
        set(DIAGON_ENABLE_SIMD OFF)
    endif()
endif()

# ==================== Summary ====================

if(DIAGON_ENABLE_SIMD)
    message(STATUS "SIMD capabilities detection complete - SIMD enabled")
else()
    message(STATUS "SIMD capabilities detection complete - SIMD disabled")
endif()
