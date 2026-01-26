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
add_diagon_test(ArrayFieldTest unit/document/ArrayFieldTest.cpp)

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
add_diagon_test(MMapDirectoryTest unit/store/MMapDirectoryTest.cpp)
add_diagon_test(MMapPlatformTest unit/store/MMapPlatformTest.cpp)
add_diagon_test(MMapDirectoryIntegrationTest unit/store/MMapDirectoryIntegrationTest.cpp)
add_diagon_test(MMapDirectoryFallbackTest unit/store/MMapDirectoryFallbackTest.cpp)

# Index tests
add_diagon_test(FieldInfoTest unit/index/FieldInfoTest.cpp)
add_diagon_test(IndexMappingTest unit/index/IndexMappingTest.cpp)
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
add_diagon_test(DeletionIntegrationTest unit/index/DeletionIntegrationTest.cpp)
add_diagon_test(TieredMergePolicyTest unit/index/TieredMergePolicyTest.cpp)
add_diagon_test(ReaderReopenTest unit/index/ReaderReopenTest.cpp)
add_diagon_test(CacheHelperTest unit/index/CacheHelperTest.cpp)

# Codec tests
add_diagon_test(CodecTest unit/codecs/CodecTest.cpp)
add_diagon_test(NumericDocValuesWriterTest unit/codecs/NumericDocValuesWriterTest.cpp)
add_diagon_test(NumericDocValuesReaderTest unit/codecs/NumericDocValuesReaderTest.cpp)
add_diagon_test(NumericDocValuesIntegrationTest unit/codecs/NumericDocValuesIntegrationTest.cpp)
add_diagon_test(StoredFieldsWriterTest unit/codecs/StoredFieldsWriterTest.cpp)
add_diagon_test(StoredFieldsReaderTest unit/codecs/StoredFieldsReaderTest.cpp)
add_diagon_test(StoredFieldsIntegrationTest unit/codecs/StoredFieldsIntegrationTest.cpp)
add_diagon_test(LiveDocsFormatTest unit/codecs/LiveDocsFormatTest.cpp)
add_diagon_test(PostingsWriterTest unit/codecs/PostingsWriterTest.cpp)
add_diagon_test(PostingsReaderTest unit/codecs/PostingsReaderTest.cpp)
add_diagon_test(BlockTreeTest unit/codecs/BlockTreeTest.cpp)
add_diagon_test(NormsTest unit/codecs/NormsTest.cpp)

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
