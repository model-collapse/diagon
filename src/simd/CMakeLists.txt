# ==================== SIMD Library ====================

message(STATUS "Configuring Diagon SIMD Library")

# ==================== Source Files ====================

set(DIAGON_SIMD_SOURCES
    # Unified SIMD storage (Module 14)
    src/WindowedStorage.cpp
    src/WindowIndexBuilder.cpp
    src/SIMDScorer.cpp
    src/SIMDFilter.cpp

    # SIMD postings operations
    src/SIMDPostingsDecoder.cpp
    src/SIMDPostingsEncoder.cpp

    # SIMD scoring
    src/SIMDBM25.cpp
    src/SIMDRankFeatures.cpp

    # Filter strategies
    src/FilterStrategyListMerge.cpp
    src/FilterStrategyPreFill.cpp
    src/FilterStrategySelector.cpp

    # Platform-specific implementations
    src/platform/AVX2Operations.cpp
    src/platform/NEONOperations.cpp
    src/platform/ScalarFallback.cpp
)

set(DIAGON_SIMD_HEADERS
    include/diagon/simd/WindowedStorage.h
    include/diagon/simd/WindowIndexBuilder.h
    include/diagon/simd/SIMDScorer.h
    include/diagon/simd/SIMDFilter.h
    include/diagon/simd/SIMDBM25.h
    include/diagon/simd/SIMDOperations.h
)

# ==================== Library Target ====================

add_library(diagon_simd SHARED ${DIAGON_SIMD_SOURCES} ${DIAGON_SIMD_HEADERS})

if(DIAGON_BUILD_STATIC)
    add_library(diagon_simd_static STATIC ${DIAGON_SIMD_SOURCES} ${DIAGON_SIMD_HEADERS})
    set_target_properties(diagon_simd_static PROPERTIES OUTPUT_NAME diagon_simd)
endif()

# ==================== Include Directories ====================

target_include_directories(diagon_simd
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ==================== Link Libraries ====================

target_link_libraries(diagon_simd
    PUBLIC
        diagon_core
        diagon_columns
)

# ==================== Compiler Features ====================

target_compile_features(diagon_simd PUBLIC cxx_std_20)

# ==================== SIMD-Specific Flags ====================

if(DIAGON_ENABLE_SIMD)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        # Apply AVX2 flags to AVX2-specific files
        set_source_files_properties(
            src/platform/AVX2Operations.cpp
            src/SIMDPostingsDecoder.cpp
            src/SIMDPostingsEncoder.cpp
            src/SIMDBM25.cpp
            PROPERTIES COMPILE_FLAGS "${DIAGON_SIMD_FLAGS}"
        )

        target_compile_definitions(diagon_simd PUBLIC DIAGON_USE_AVX2)

    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64")
        # ARM NEON support
        target_compile_definitions(diagon_simd PUBLIC DIAGON_USE_NEON)
    endif()

    message(STATUS "SIMD optimizations enabled for diagon_simd")
else()
    message(STATUS "SIMD optimizations disabled - using scalar fallback")
    target_compile_definitions(diagon_simd PUBLIC DIAGON_USE_SCALAR)
endif()

# ==================== Export Symbols (Windows) ====================

if(WIN32 AND DIAGON_BUILD_SHARED)
    target_compile_definitions(diagon_simd
        PRIVATE DIAGON_BUILDING_DLL
        INTERFACE DIAGON_USING_DLL
    )
endif()

message(STATUS "Diagon SIMD Library configured")
