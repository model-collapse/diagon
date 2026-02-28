cmake_minimum_required(VERSION 3.20)
project(Diagon
    VERSION 0.1.1
    LANGUAGES CXX
    DESCRIPTION "C++ search engine combining Lucene and ClickHouse"
)

# ==================== Options ====================

option(DIAGON_BUILD_TESTS "Build tests" ON)
option(DIAGON_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(DIAGON_BUILD_SHARED "Build shared library" ON)
option(DIAGON_BUILD_STATIC "Build static library" ON)
option(DIAGON_ENABLE_SIMD "Enable SIMD optimizations" ON)
option(DIAGON_USE_VCPKG "Use vcpkg for dependencies" OFF)
option(DIAGON_USE_CONAN "Use Conan for dependencies" OFF)

# ==================== C++ Standard ====================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==================== Build Type ====================

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Project: ${PROJECT_NAME} v${PROJECT_VERSION}")

# ==================== Include CMake Modules ====================

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(Dependencies)
include(CompilerFlags)
include(SIMDDetection)

# ==================== External Dependencies ====================
# Note: Dependencies are found in Dependencies.cmake above

# Optional: Enable testing
if(DIAGON_BUILD_TESTS)
    enable_testing()
    include(CTest)
endif()

# ==================== Subdirectories ====================
# Order matters: dependencies must be added before dependents

# TODO: Uncomment as implementations are completed
# add_subdirectory(src/compression)  # No dependencies (first)
# add_subdirectory(src/columns)      # Depends on: compression
add_subdirectory(src/core)         # Core utilities (minimal implementation)
# add_subdirectory(src/simd)         # Depends on: core, columns

if(DIAGON_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# ==================== Benchmarks ====================

if(DIAGON_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# ==================== Installation ====================

include(GNUInstallDirs)

install(TARGETS diagon_core
    EXPORT DiagonTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY
    src/core/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/diagon
    FILES_MATCHING PATTERN "*.h"
)

# ==================== Export Targets ====================

install(EXPORT DiagonTargets
    FILE DiagonTargets.cmake
    NAMESPACE Diagon::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Diagon
)

# ==================== Package Config ====================

include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DiagonConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/DiagonConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Diagon
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/DiagonConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/DiagonConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/DiagonConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Diagon
)

# ==================== Summary ====================

message(STATUS "")
message(STATUS "========================================")
message(STATUS "Diagon Configuration Summary")
message(STATUS "========================================")
message(STATUS "Build type:         ${CMAKE_BUILD_TYPE}")
message(STATUS "Build tests:        ${DIAGON_BUILD_TESTS}")
message(STATUS "Build benchmarks:   ${DIAGON_BUILD_BENCHMARKS}")
message(STATUS "Build shared:       ${DIAGON_BUILD_SHARED}")
message(STATUS "Build static:       ${DIAGON_BUILD_STATIC}")
message(STATUS "Enable SIMD:        ${DIAGON_ENABLE_SIMD}")
message(STATUS "========================================")
message(STATUS "")
