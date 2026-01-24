# ==================== Tests ====================

message(STATUS "Configuring Diagon Tests")

# ==================== Find Google Test ====================

find_package(GTest REQUIRED)
include(GoogleTest)

# ==================== Helper Function ====================

function(add_diagon_test TEST_NAME)
    add_executable(${TEST_NAME} ${ARGN})

    target_link_libraries(${TEST_NAME}
        PRIVATE
            diagon_core
            GTest::gtest
            GTest::gtest_main
    )

    target_include_directories(${TEST_NAME}
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src/core/include
    )

    gtest_discover_tests(${TEST_NAME}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        PROPERTIES
            LABELS "unit"
    )
endfunction()

# ==================== Unit Tests ====================

# Util tests
add_diagon_test(BytesRefTest unit/util/BytesRefTest.cpp)
add_diagon_test(BitSetTest unit/util/BitSetTest.cpp)
add_diagon_test(NumericUtilsTest unit/util/NumericUtilsTest.cpp)

# Store tests
add_diagon_test(IOContextTest unit/store/IOContextTest.cpp)
add_diagon_test(IndexInputOutputTest unit/store/IndexInputOutputTest.cpp)
add_diagon_test(FSDirectoryTest unit/store/FSDirectoryTest.cpp)

# Index tests
add_diagon_test(FieldInfoTest unit/index/FieldInfoTest.cpp)
add_diagon_test(IndexReaderTest unit/index/IndexReaderTest.cpp)
add_diagon_test(IndexWriterTest unit/index/IndexWriterTest.cpp)
add_diagon_test(MergeTest unit/index/MergeTest.cpp)
add_diagon_test(SkipIndexTest unit/index/SkipIndexTest.cpp)

# Codec tests
add_diagon_test(CodecTest unit/codecs/CodecTest.cpp)

# Column tests
add_diagon_test(ColumnTest unit/columns/ColumnTest.cpp)

# Compression tests
add_diagon_test(CompressionTest unit/compression/CompressionTest.cpp)

# MergeTree tests
add_diagon_test(MergeTreeTest unit/mergetree/MergeTreeTest.cpp)

# Granularity tests
add_diagon_test(GranularityTest unit/granularity/GranularityTest.cpp)

# Search tests
add_diagon_test(SearchTest unit/search/SearchTest.cpp)

# Filter tests
add_diagon_test(FilterTest unit/search/FilterTest.cpp)

# Storage tests
add_diagon_test(StorageTierTest unit/storage/StorageTierTest.cpp)

# SIMD tests
add_diagon_test(SIMDStorageTest unit/simd/SIMDStorageTest.cpp)

# Observability tests
add_diagon_test(ObservabilityTest unit/observability/ObservabilityTest.cpp)

message(STATUS "Diagon Tests configured")
