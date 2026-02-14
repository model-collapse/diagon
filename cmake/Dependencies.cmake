# ==================== Dependency Management ====================

message(STATUS "Configuring dependencies...")

# ==================== Dependency Search Paths ====================

# Detect conda environment and set up intelligent fallback
if(DEFINED ENV{CONDA_PREFIX})
    set(CONDA_PREFIX "$ENV{CONDA_PREFIX}")
    message(STATUS "Conda environment detected: ${CONDA_PREFIX}")
    # Add conda paths to search, but allow fallback to system
    list(APPEND CMAKE_PREFIX_PATH "${CONDA_PREFIX}")
    set(USING_CONDA_FALLBACK TRUE)
    message(STATUS "Using intelligent fallback: Conda first, then system libraries")
else()
    set(USING_CONDA_FALLBACK FALSE)
    message(STATUS "No conda environment detected, using system packages only")
endif()

if(DIAGON_USE_VCPKG)
    message(STATUS "Using vcpkg for dependency management")
elseif(DIAGON_USE_CONAN)
    message(STATUS "Using Conan for dependency management")
endif()

# ==================== Required Dependencies ====================

# ZLIB - Required for compression
if(NOT TARGET ZLIB::ZLIB)
    find_package(ZLIB REQUIRED)
    if(ZLIB_FOUND)
        # Check actual library location, not just link libraries
        if(ZLIB_LIBRARY)
            if(ZLIB_LIBRARY MATCHES "${CONDA_PREFIX}")
                message(STATUS "Found ZLIB: ${ZLIB_VERSION} (conda)")
            else()
                message(STATUS "Found ZLIB: ${ZLIB_VERSION} (system)")
            endif()
        else()
            message(STATUS "Found ZLIB: ${ZLIB_VERSION}")
        endif()
    endif()
endif()

# ZSTD - Required for compression
# Check for different possible target names (zstd::libzstd from package, zstd::zstd from fallback)
if(NOT TARGET zstd::libzstd AND NOT TARGET zstd::zstd)
    find_package(zstd QUIET)
    if(NOT zstd_FOUND)
        # Try alternative package names
        find_package(zstd QUIET CONFIG)
        if(NOT zstd_FOUND)
            # Fallback: manually find library and create imported target
            find_library(ZSTD_LIBRARY NAMES zstd)
            find_path(ZSTD_INCLUDE_DIR NAMES zstd.h)
            if(ZSTD_LIBRARY AND ZSTD_INCLUDE_DIR)
                add_library(zstd::libzstd UNKNOWN IMPORTED)
                set_target_properties(zstd::libzstd PROPERTIES
                    IMPORTED_LOCATION "${ZSTD_LIBRARY}"
                    INTERFACE_INCLUDE_DIRECTORIES "${ZSTD_INCLUDE_DIR}"
                )
                if(ZSTD_LIBRARY MATCHES "${CONDA_PREFIX}")
                    message(STATUS "Found ZSTD: ${ZSTD_LIBRARY} (conda)")
                else()
                    message(STATUS "Found ZSTD: ${ZSTD_LIBRARY} (system)")
                endif()
                set(HAVE_ZSTD TRUE CACHE BOOL "ZSTD library found" FORCE)
            else()
                message(FATAL_ERROR "ZSTD not found. Please install libzstd-dev")
            endif()
        else()
            set(HAVE_ZSTD TRUE CACHE BOOL "ZSTD library found" FORCE)
            message(STATUS "Found ZSTD (config)")
            # System package creates zstd::libzstd_shared, create alias to expected name
            if(TARGET zstd::libzstd_shared AND NOT TARGET zstd::libzstd)
                add_library(zstd::libzstd ALIAS zstd::libzstd_shared)
            endif()
        endif()
    else()
        set(HAVE_ZSTD TRUE CACHE BOOL "ZSTD library found" FORCE)
        message(STATUS "Found ZSTD")
        # System package creates zstd::libzstd_shared, create alias to expected name
        if(TARGET zstd::libzstd_shared AND NOT TARGET zstd::libzstd)
            add_library(zstd::libzstd ALIAS zstd::libzstd_shared)
        endif()
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
                if(LZ4_LIBRARY MATCHES "${CONDA_PREFIX}")
                    message(STATUS "Found LZ4: ${LZ4_LIBRARY} (conda)")
                else()
                    message(STATUS "Found LZ4: ${LZ4_LIBRARY} (system)")
                endif()
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

# ICU - Required for text analysis (StandardTokenizer)
# On macOS, Homebrew icu4c is keg-only; ensure ICU_ROOT is in search path
if(APPLE AND DEFINED ICU_ROOT)
    list(PREPEND CMAKE_PREFIX_PATH "${ICU_ROOT}")
endif()
if(NOT TARGET ICU::uc OR NOT TARGET ICU::i18n)
    find_package(ICU COMPONENTS uc i18n QUIET)
    if(NOT ICU_FOUND)
        # Try manually finding ICU
        # On macOS with Homebrew, icu4c is keg-only; use ICU_ROOT hint
        set(_ICU_HINTS "")
        if(DEFINED ICU_ROOT)
            list(APPEND _ICU_HINTS "${ICU_ROOT}")
        endif()
        if(APPLE)
            # Common Homebrew paths for icu4c
            execute_process(COMMAND brew --prefix icu4c
                            OUTPUT_VARIABLE _BREW_ICU_PREFIX
                            OUTPUT_STRIP_TRAILING_WHITESPACE
                            ERROR_QUIET)
            if(_BREW_ICU_PREFIX)
                list(APPEND _ICU_HINTS "${_BREW_ICU_PREFIX}")
            endif()
        endif()
        find_library(ICU_UC_LIBRARY NAMES icuuc HINTS ${_ICU_HINTS} PATH_SUFFIXES lib)
        find_library(ICU_I18N_LIBRARY NAMES icui18n HINTS ${_ICU_HINTS} PATH_SUFFIXES lib)
        find_path(ICU_INCLUDE_DIR NAMES unicode/unistr.h HINTS ${_ICU_HINTS} PATH_SUFFIXES include)
        if(ICU_UC_LIBRARY AND ICU_I18N_LIBRARY AND ICU_INCLUDE_DIR)
            add_library(ICU::uc UNKNOWN IMPORTED)
            set_target_properties(ICU::uc PROPERTIES
                IMPORTED_LOCATION "${ICU_UC_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${ICU_INCLUDE_DIR}"
            )
            add_library(ICU::i18n UNKNOWN IMPORTED)
            set_target_properties(ICU::i18n PROPERTIES
                IMPORTED_LOCATION "${ICU_I18N_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${ICU_INCLUDE_DIR}"
            )
            if(ICU_UC_LIBRARY MATCHES "${CONDA_PREFIX}")
                message(STATUS "Found ICU: ${ICU_UC_LIBRARY} (conda)")
            else()
                message(STATUS "Found ICU: ${ICU_UC_LIBRARY} (system)")
            endif()
            set(HAVE_ICU TRUE CACHE BOOL "ICU library found" FORCE)
        else()
            message(FATAL_ERROR "ICU not found. Please install libicu-dev")
        endif()
    else()
        set(HAVE_ICU TRUE CACHE BOOL "ICU library found" FORCE)
        # Detect if ICU is from conda or system
        get_target_property(ICU_IMPORTED_LOCATION ICU::uc IMPORTED_LOCATION)
        if(NOT ICU_IMPORTED_LOCATION)
            get_target_property(ICU_IMPORTED_LOCATION ICU::uc INTERFACE_LINK_LIBRARIES)
        endif()
        if(ICU_IMPORTED_LOCATION MATCHES "${CONDA_PREFIX}")
            message(STATUS "Found ICU: ${ICU_VERSION} (conda)")
        else()
            message(STATUS "Found ICU: ${ICU_VERSION} (system)")
        endif()
    endif()
endif()

# cppjieba - Required for Chinese text segmentation (JiebaTokenizer)
# Using FetchContent to download header-only library
include(FetchContent)
FetchContent_Declare(
    cppjieba
    GIT_REPOSITORY https://github.com/yanyiwu/cppjieba.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)

FetchContent_GetProperties(cppjieba)
if(NOT cppjieba_POPULATED)
    message(STATUS "Fetching cppjieba...")
    FetchContent_Populate(cppjieba)
    message(STATUS "cppjieba downloaded to ${cppjieba_SOURCE_DIR}")

    # cppjieba is header-only, create interface library
    add_library(cppjieba INTERFACE)
    # Add include directories for cppjieba and its dependency limonp
    # Mark as SYSTEM to suppress warnings from external code
    target_include_directories(cppjieba SYSTEM INTERFACE
        "${cppjieba_SOURCE_DIR}/include"
        "${cppjieba_SOURCE_DIR}/deps/limonp/include"
    )

    # Set dict path for runtime use
    set(CPPJIEBA_DICT_DIR "${cppjieba_SOURCE_DIR}/dict" CACHE PATH "cppjieba dictionary directory")
    message(STATUS "cppjieba dictionaries at: ${CPPJIEBA_DICT_DIR}")
    set(HAVE_CPPJIEBA TRUE CACHE BOOL "cppjieba library found" FORCE)
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

# ==================== Dependency Source Summary ====================

if(USING_CONDA_FALLBACK)
    message(STATUS "")
    message(STATUS "========================================")
    message(STATUS "Dependency Source Summary (Conda Fallback Enabled)")
    message(STATUS "========================================")
    message(STATUS "Strategy: Try conda first, fall back to system if not found")
    message(STATUS "Conda prefix: ${CONDA_PREFIX}")
    message(STATUS "")
    message(STATUS "NOTE: For consistent linking, all libraries should come from")
    message(STATUS "      the same source (all conda or all system). Mixed sources")
    message(STATUS "      may cause runtime conflicts.")
    message(STATUS "========================================")
    message(STATUS "")
endif()
