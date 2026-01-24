# ==================== Dependency Management ====================

message(STATUS "Configuring dependencies...")

# ==================== Dependency Search Paths ====================

if(DIAGON_USE_VCPKG)
    message(STATUS "Using vcpkg for dependency management")
elseif(DIAGON_USE_CONAN)
    message(STATUS "Using Conan for dependency management")
else()
    message(STATUS "Using system packages for dependencies")
endif()

# ==================== Required Dependencies ====================

# ZLIB - Required for compression
if(NOT TARGET ZLIB::ZLIB)
    find_package(ZLIB REQUIRED)
    if(ZLIB_FOUND)
        message(STATUS "Found ZLIB: ${ZLIB_VERSION}")
    endif()
endif()

# ZSTD - Required for compression
if(NOT TARGET zstd::zstd)
    find_package(zstd QUIET)
    if(NOT zstd_FOUND)
        # Try alternative package names
        find_package(zstd QUIET CONFIG)
        if(NOT zstd_FOUND)
            find_library(ZSTD_LIBRARY NAMES zstd)
            find_path(ZSTD_INCLUDE_DIR NAMES zstd.h)
            if(ZSTD_LIBRARY AND ZSTD_INCLUDE_DIR)
                add_library(zstd::zstd UNKNOWN IMPORTED)
                set_target_properties(zstd::zstd PROPERTIES
                    IMPORTED_LOCATION "${ZSTD_LIBRARY}"
                    INTERFACE_INCLUDE_DIRECTORIES "${ZSTD_INCLUDE_DIR}"
                )
                message(STATUS "Found ZSTD: ${ZSTD_LIBRARY}")
                set(HAVE_ZSTD TRUE CACHE BOOL "ZSTD library found" FORCE)
            else()
                message(FATAL_ERROR "ZSTD not found. Please install libzstd-dev")
            endif()
        else()
            set(HAVE_ZSTD TRUE CACHE BOOL "ZSTD library found" FORCE)
            get_target_property(ZSTD_LIBRARY zstd::zstd LOCATION)
            message(STATUS "Found ZSTD: ${ZSTD_LIBRARY}")
        endif()
    else()
        set(HAVE_ZSTD TRUE CACHE BOOL "ZSTD library found" FORCE)
        message(STATUS "Found ZSTD")
    endif()
endif()

# LZ4 - Required for compression
if(NOT TARGET LZ4::LZ4)
    find_package(lz4 QUIET)
    if(NOT lz4_FOUND)
        # Try alternative package names
        find_package(LZ4 QUIET CONFIG)
        if(NOT LZ4_FOUND)
            find_library(LZ4_LIBRARY NAMES lz4)
            find_path(LZ4_INCLUDE_DIR NAMES lz4.h)
            if(LZ4_LIBRARY AND LZ4_INCLUDE_DIR)
                add_library(LZ4::LZ4 UNKNOWN IMPORTED)
                set_target_properties(LZ4::LZ4 PROPERTIES
                    IMPORTED_LOCATION "${LZ4_LIBRARY}"
                    INTERFACE_INCLUDE_DIRECTORIES "${LZ4_INCLUDE_DIR}"
                )
                message(STATUS "Found LZ4: ${LZ4_LIBRARY}")
                set(HAVE_LZ4 TRUE CACHE BOOL "LZ4 library found" FORCE)
            else()
                message(FATAL_ERROR "LZ4 not found. Please install liblz4-dev")
            endif()
        else()
            set(HAVE_LZ4 TRUE CACHE BOOL "LZ4 library found" FORCE)
            get_target_property(LZ4_LIBRARY LZ4::LZ4 LOCATION)
            message(STATUS "Found LZ4: ${LZ4_LIBRARY}")
        endif()
    else()
        set(HAVE_LZ4 TRUE CACHE BOOL "LZ4 library found" FORCE)
        message(STATUS "Found LZ4")
    endif()
endif()

# ==================== Optional Dependencies ====================

# Google Test - For unit tests
if(DIAGON_BUILD_TESTS)
    if(NOT TARGET GTest::gtest)
        find_package(GTest QUIET)
        if(NOT GTest_FOUND)
            message(WARNING "Google Test not found. Tests will be disabled.")
            set(DIAGON_BUILD_TESTS OFF CACHE BOOL "Build tests" FORCE)
        else()
            message(STATUS "Found Google Test: ${GTest_VERSION}")
        endif()
    endif()
endif()

# Google Benchmark - For performance tests
if(DIAGON_BUILD_BENCHMARKS)
    if(NOT TARGET benchmark::benchmark)
        find_package(benchmark QUIET)
        if(NOT benchmark_FOUND)
            message(WARNING "Google Benchmark not found. Benchmarks will be disabled.")
            set(DIAGON_BUILD_BENCHMARKS OFF CACHE BOOL "Build benchmarks" FORCE)
        else()
            message(STATUS "Found Google Benchmark")
        endif()
    endif()
endif()

message(STATUS "Dependency configuration complete")
