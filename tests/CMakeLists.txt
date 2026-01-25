# ==================== Tests ====================

message(STATUS "Configuring Diagon Tests")

# ==================== Find Google Test ====================

find_package(GTest REQUIRED)
include(GoogleTest)

# ==================== Helper Function ====================

function(add_diagon_test TEST_NAME)
    add_executable(${TEST_NAME} ${ARGN})

    # Link against diagon_core which transitively provides all dependencies
    # Plus directly link compression libraries since compression code is header-only
    target_link_libraries(${TEST_NAME}
        PRIVATE
            diagon_core
            GTest::gtest
            GTest::gtest_main
            ZLIB::ZLIB
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

# Document tests
add_diagon_test(DocumentTest unit/document/DocumentTest.cpp)

# Util tests
add_diagon_test(BytesRefTest unit/util/BytesRefTest.cpp)
add_diagon_test(BitSetTest unit/util/BitSetTest.cpp)
add_diagon_test(NumericUtilsTest unit/util/NumericUtilsTest.cpp)
add_diagon_test(FSTTest unit/util/FSTTest.cpp)
add_diagon_test(BlockPoolTest unit/util/BlockPoolTest.cpp)

# NumericUtilsTest needs proper NaN/Infinity handling, disable -ffast-math
if(CMAKE_BUILD_TYPE MATCHES "Release|RelWithDebInfo")
    target_compile_options(NumericUtilsTest PRIVATE -fno-fast-math)
endif()

# Store tests
add_diagon_test(IOContextTest unit/store/IOContextTest.cpp)
add_diagon_test(IndexInputOutputTest unit/store/IndexInputOutputTest.cpp)
add_diagon_test(FSDirectoryTest unit/store/FSDirectoryTest.cpp)

# Index tests
add_diagon_test(FieldInfoTest unit/index/FieldInfoTest.cpp)
add_diagon_test(IndexReaderTest unit/index/IndexReaderTest.cpp)
add_diagon_test(IndexWriterTest unit/index/IndexWriterTest.cpp)
add_diagon_test(IndexWriterIntegrationTest unit/index/IndexWriterIntegrationTest.cpp)
add_diagon_test(MergeTest unit/index/MergeTest.cpp)
add_diagon_test(SkipIndexTest unit/index/SkipIndexTest.cpp)
add_diagon_test(FreqProxTermsWriterTest unit/index/FreqProxTermsWriterTest.cpp)
add_diagon_test(DocumentsWriterPerThreadTest unit/index/DocumentsWriterPerThreadTest.cpp)
add_diagon_test(DocumentsWriterTest unit/index/DocumentsWriterTest.cpp)
add_diagon_test(SegmentInfoTest unit/index/SegmentInfoTest.cpp)
add_diagon_test(SegmentFlushTest unit/index/SegmentFlushTest.cpp)

# Codec tests
add_diagon_test(CodecTest unit/codecs/CodecTest.cpp)
add_diagon_test(PostingsWriterTest unit/codecs/PostingsWriterTest.cpp)
add_diagon_test(PostingsReaderTest unit/codecs/PostingsReaderTest.cpp)
add_diagon_test(BlockTreeTest unit/codecs/BlockTreeTest.cpp)

# Column tests
add_diagon_test(ColumnTest unit/columns/ColumnTest.cpp)

# ColumnTest needs proper NaN handling, disable -ffast-math
if(CMAKE_BUILD_TYPE MATCHES "Release|RelWithDebInfo")
    target_compile_options(ColumnTest PRIVATE -fno-fast-math)
endif()

# Compression tests
add_diagon_test(CompressionTest unit/compression/CompressionTest.cpp)

# MergeTree tests
add_diagon_test(MergeTreeTest unit/mergetree/MergeTreeTest.cpp)

# Granularity tests
add_diagon_test(GranularityTest unit/granularity/GranularityTest.cpp)

# Search tests
add_diagon_test(SearchTest unit/search/SearchTest.cpp)
add_diagon_test(TopScoreDocCollectorTest unit/search/TopScoreDocCollectorTest.cpp)

# TopScoreDocCollectorTest needs proper NaN handling, disable -ffast-math
if(CMAKE_BUILD_TYPE MATCHES "Release|RelWithDebInfo")
    target_compile_options(TopScoreDocCollectorTest PRIVATE -fno-fast-math)
endif()

# Filter tests
add_diagon_test(FilterTest unit/search/FilterTest.cpp)

# Storage tests
add_diagon_test(StorageTierTest unit/storage/StorageTierTest.cpp)

# SIMD tests
add_diagon_test(SIMDStorageTest unit/simd/SIMDStorageTest.cpp)

# Observability tests
add_diagon_test(ObservabilityTest unit/observability/ObservabilityTest.cpp)

# Util tests
add_diagon_test(VByteTest unit/util/VByteTest.cpp)

message(STATUS "Diagon Tests configured")
add_diagon_test(DirectWriterTest unit/util/packed/DirectWriterTest.cpp)

## Phase 4 tests
add_diagon_test(SegmentInfosReadTest unit/index/SegmentInfosReadTest.cpp)
add_diagon_test(SimpleFieldsProducerTest unit/codecs/SimpleFieldsProducerTest.cpp)
add_diagon_test(SegmentReaderTest unit/index/SegmentReaderTest.cpp)
add_diagon_test(DirectoryReaderTest unit/index/DirectoryReaderTest.cpp)
add_diagon_test(IndexSearcherTest unit/search/IndexSearcherTest.cpp)
add_diagon_test(BM25ScorerSIMDTest unit/search/BM25ScorerSIMDTest.cpp)
