# ==================== Core Library ====================

message(STATUS "Configuring Diagon Core Library")

# ==================== Source Files ====================

set(DIAGON_CORE_SOURCES
    # Util (Utilities)
    src/util/BytesRef.cpp
    src/util/BitSet.cpp
    src/util/NumericUtils.cpp
    src/util/FST.cpp
    src/util/ByteBlockPool.cpp
    src/util/IntBlockPool.cpp
    src/util/StreamVByte.cpp
    src/util/BloomFilter.cpp
    src/util/CityHash.cpp
    src/util/packed/DirectWriter.cpp
    src/util/packed/DirectMonotonicWriter.cpp

    # Store (Directory abstraction - Module 09)
    src/store/IOContext.cpp
    src/store/Directory.cpp
    src/store/IndexInput.cpp
    src/store/IndexOutput.cpp
    src/store/FSDirectory.cpp
    src/store/MMapDirectory.cpp
    src/store/MMapIndexInput.cpp

    # Document (User-facing API for documents and fields)
    src/document/Field.cpp
    src/document/ArrayField.cpp
    src/document/SparseVectorField.cpp

    # Index (FieldInfo - Module 10, IndexReader/Writer - Module 01, IndexMapping - Module 15)
    src/index/FieldInfo.cpp
    src/index/IndexMapping.cpp
    src/index/IndexReader.cpp
    src/index/IndexWriter.cpp
    src/index/FreqProxTermsWriter.cpp
    src/index/DocumentsWriterPerThread.cpp
    src/index/DocumentsWriter.cpp
    src/index/SegmentInfo.cpp
    src/index/SegmentReader.cpp
    src/index/DirectoryReader.cpp
    src/index/TieredMergePolicy.cpp
    src/index/SegmentMerger.cpp
    src/index/skipindex/MergeTreeIndexBloomFilter.cpp
    src/index/BlockMaxQuantizedIndex.cpp

    # Codecs (Codec architecture - Module 02)
    src/codecs/Codec.cpp
    src/codecs/PostingsFormat.cpp
    src/codecs/DocValuesFormat.cpp
    src/codecs/ColumnFormat.cpp
    src/codecs/NumericDocValuesWriter.cpp
    src/codecs/NumericDocValuesReader.cpp
    src/codecs/StoredFieldsWriter.cpp
    src/codecs/StoredFieldsReader.cpp
    src/codecs/LiveDocsFormat.cpp
    src/codecs/SimpleFieldsConsumer.cpp
    src/codecs/SimpleFieldsProducer.cpp
    src/codecs/lucene104/Lucene104Codec.cpp
    src/codecs/lucene104/Lucene104PostingsWriter.cpp
    src/codecs/lucene104/Lucene104PostingsReader.cpp
    src/codecs/lucene104/Lucene104PostingsReaderOptimized.cpp
    src/codecs/lucene104/Lucene104NormsWriter.cpp
    src/codecs/lucene104/Lucene104NormsReader.cpp
    src/codecs/blocktree/BlockTreeTermsWriter.cpp
    src/codecs/blocktree/BlockTreeTermsReader.cpp

    # Search (Query execution - Module 07)
    src/search/TopScoreDocCollector.cpp
    src/search/TermQuery.cpp
    src/search/NumericRangeQuery.cpp
    src/search/DoubleRangeQuery.cpp
    src/search/BooleanQuery.cpp
    src/search/IndexSearcher.cpp
    src/search/BM25ScorerSIMD.cpp

    # Sparse (Sparse vector support)
    src/sparse/SparseVector.cpp
    src/sparse/SindiScorer.cpp
    src/sparse/SindiIndex.cpp
    src/sparse/QBlockIndex.cpp

    # Analysis (Text analysis framework)
    src/analysis/Token.cpp
    src/analysis/Analyzer.cpp
    src/analysis/WhitespaceTokenizer.cpp
    src/analysis/KeywordTokenizer.cpp
    src/analysis/StandardTokenizer.cpp
    src/analysis/JiebaTokenizer.cpp
    src/analysis/LowercaseFilter.cpp
    src/analysis/AnalyzerFactory.cpp
)

# Platform-specific mmap implementation
if(UNIX)
    list(APPEND DIAGON_CORE_SOURCES src/store/PosixMMapIndexInput.cpp)
elseif(WIN32)
    list(APPEND DIAGON_CORE_SOURCES src/store/WindowsMMapIndexInput.cpp)
endif()

set(DIAGON_CORE_HEADERS
    # Util
    include/diagon/util/BytesRef.h
    include/diagon/util/Bits.h
    include/diagon/util/BitSet.h
    include/diagon/util/NumericUtils.h
    include/diagon/util/Exceptions.h
    include/diagon/util/VByte.h
    include/diagon/util/StreamVByte.h
    include/diagon/util/SIMDUtils.h
    include/diagon/util/FST.h
    include/diagon/util/ByteBlockPool.h
    include/diagon/util/IntBlockPool.h
    include/diagon/util/BloomFilter.h
    include/diagon/util/CityHash.h
    include/diagon/util/packed/DirectWriter.h
    include/diagon/util/packed/DirectMonotonicWriter.h

    # Store
    include/diagon/store/IOContext.h
    include/diagon/store/Directory.h
    include/diagon/store/IndexInput.h
    include/diagon/store/IndexOutput.h
    include/diagon/store/ByteBuffersIndexOutput.h
    include/diagon/store/ByteBuffersIndexInput.h
    include/diagon/store/FSDirectory.h
    include/diagon/store/MMapDirectory.h
    include/diagon/store/MMapIndexInput.h
    include/diagon/store/PosixMMapIndexInput.h
    include/diagon/store/WindowsMMapIndexInput.h
    include/diagon/store/Lock.h

    # Document
    include/diagon/document/IndexableField.h
    include/diagon/document/Field.h
    include/diagon/document/ArrayField.h
    include/diagon/document/SparseVectorField.h
    include/diagon/document/Document.h

    # Index
    include/diagon/index/CacheHelper.h
    include/diagon/index/FieldInfo.h
    include/diagon/index/IndexMapping.h
    include/diagon/index/IndexReader.h
    include/diagon/index/SegmentWriteState.h
    include/diagon/index/PostingsEnum.h
    include/diagon/index/TermsEnum.h
    include/diagon/index/Terms.h
    include/diagon/index/DocValues.h
    include/diagon/index/LeafReaderContext.h
    include/diagon/index/IndexWriter.h
    include/diagon/index/MergePolicy.h
    include/diagon/index/MergeSpecification.h
    include/diagon/index/OneMerge.h
    include/diagon/index/MergeScheduler.h
    include/diagon/index/TieredMergePolicy.h
    include/diagon/index/SegmentMerger.h
    include/diagon/index/FreqProxTermsWriter.h
    include/diagon/index/DocumentsWriterPerThread.h
    include/diagon/index/DocumentsWriter.h
    include/diagon/index/SegmentInfo.h
    include/diagon/index/SegmentReader.h
    include/diagon/index/DirectoryReader.h
    include/diagon/index/Term.h

    # Skip Indexes
    include/diagon/index/skipindex/IMergeTreeIndexGranule.h
    include/diagon/index/skipindex/IMergeTreeIndexAggregator.h
    include/diagon/index/skipindex/IMergeTreeIndexCondition.h
    include/diagon/index/skipindex/IMergeTreeIndex.h
    include/diagon/index/skipindex/MergeTreeIndexMinMax.h
    include/diagon/index/skipindex/MergeTreeIndexBloomFilter.h
    include/diagon/index/BlockMaxQuantizedIndex.h

    # Codecs
    include/diagon/codecs/Codec.h
    include/diagon/codecs/PostingsFormat.h
    include/diagon/codecs/DocValuesFormat.h
    include/diagon/codecs/ColumnFormat.h
    include/diagon/codecs/NumericDocValuesWriter.h
    include/diagon/codecs/NumericDocValuesReader.h
    include/diagon/codecs/StoredFieldsWriter.h
    include/diagon/codecs/StoredFieldsReader.h
    include/diagon/codecs/LiveDocsFormat.h
    include/diagon/codecs/NormsFormat.h
    include/diagon/codecs/OtherFormats.h
    include/diagon/codecs/SegmentState.h
    include/diagon/codecs/SimpleFieldsConsumer.h
    include/diagon/codecs/SimpleFieldsProducer.h
    include/diagon/codecs/lucene104/Lucene104Codec.h
    include/diagon/codecs/lucene104/Lucene104PostingsWriter.h
    include/diagon/codecs/lucene104/Lucene104PostingsReader.h
    include/diagon/codecs/lucene104/Lucene104PostingsReaderOptimized.h
    include/diagon/codecs/lucene104/Lucene104NormsWriter.h
    include/diagon/codecs/lucene104/Lucene104NormsReader.h
    include/diagon/codecs/blocktree/BlockTreeTermsWriter.h
    include/diagon/codecs/blocktree/BlockTreeTermsReader.h

    # Columns
    include/diagon/columns/TypeIndex.h
    include/diagon/columns/Field.h
    include/diagon/columns/PODArray.h
    include/diagon/columns/IColumn.h
    include/diagon/columns/ColumnVector.h
    include/diagon/columns/ColumnString.h

    # Compression
    include/diagon/compression/ICompressionCodec.h
    include/diagon/compression/CompressionCodecs.h

    # MergeTree
    include/diagon/mergetree/IMergeTreeDataPart.h

    # Granularity
    include/diagon/granularity/IMergeTreeIndexGranularity.h
    include/diagon/granularity/MergeTreeIndexGranularityConstant.h
    include/diagon/granularity/MergeTreeIndexGranularityAdaptive.h
    include/diagon/granularity/MarkInCompressedFile.h
    include/diagon/granularity/MarkRange.h
    include/diagon/granularity/GranularityConfig.h

    # Search
    include/diagon/search/DocIdSetIterator.h
    include/diagon/search/ScoreMode.h
    include/diagon/search/Query.h
    include/diagon/search/Scorer.h
    include/diagon/search/Weight.h
    include/diagon/search/IndexSearcher.h
    include/diagon/search/Filter.h
    include/diagon/search/DocIdSet.h
    include/diagon/search/BooleanClause.h
    include/diagon/search/BooleanQuery.h
    include/diagon/search/TopDocs.h
    include/diagon/search/Collector.h
    include/diagon/search/TopScoreDocCollector.h
    include/diagon/search/TermQuery.h
    include/diagon/search/NumericRangeQuery.h
    include/diagon/search/DoubleRangeQuery.h
    include/diagon/search/BM25Similarity.h

    # Sparse
    include/diagon/sparse/SparseVector.h
    include/diagon/sparse/SindiScorer.h
    include/diagon/sparse/SindiIndex.h
    include/diagon/sparse/QBlockIndex.h

    # Storage Tiers
    include/diagon/storage/StorageTier.h
    include/diagon/storage/LifecyclePolicy.h
    include/diagon/storage/TierManager.h
    include/diagon/storage/TierMigrationService.h

    # SIMD Storage
    include/diagon/simd/ColumnWindow.h
    include/diagon/simd/UnifiedColumnFormat.h
    include/diagon/simd/SIMDScorers.h
    include/diagon/simd/UnifiedSIMDQueryProcessor.h

    # Observability
    include/diagon/observability/Metrics.h
    include/diagon/observability/HealthCheck.h

    # Analysis
    include/analysis/Token.h
    include/analysis/Tokenizer.h
    include/analysis/TokenFilter.h
    include/analysis/Analyzer.h
    include/analysis/WhitespaceTokenizer.h
    include/analysis/KeywordTokenizer.h
    include/analysis/StandardTokenizer.h
    include/analysis/JiebaTokenizer.h
    include/analysis/LowercaseFilter.h
)

# ==================== Conditional SIMD Sources ====================

# BM25ScorerSIMD.cpp is always compiled (has #ifdef guards internally)
# We only add AVX2 compiler flags when AVX2 is available

# ==================== Library Target ====================

add_library(diagon_core SHARED ${DIAGON_CORE_SOURCES} ${DIAGON_CORE_HEADERS})

# TopScoreDocCollector needs proper NaN/infinity handling, disable -ffast-math for it
if(CMAKE_BUILD_TYPE MATCHES "Release|RelWithDebInfo")
    set_source_files_properties(
        src/search/TopScoreDocCollector.cpp
        PROPERTIES COMPILE_FLAGS "-fno-fast-math"
    )
endif()

# BM25ScorerSIMD needs AVX2 and FMA instructions
if(DIAGON_ENABLE_SIMD AND HAVE_AVX2)
    set_source_files_properties(
        src/search/BM25ScorerSIMD.cpp
        PROPERTIES COMPILE_FLAGS "-mavx2 -mfma"
    )
endif()

# SindiScorer needs AVX2 instructions
if(DIAGON_ENABLE_SIMD AND HAVE_AVX2)
    set_source_files_properties(
        src/sparse/SindiScorer.cpp
        PROPERTIES COMPILE_FLAGS "-mavx2"
    )
endif()

# Alternative static library
if(DIAGON_BUILD_STATIC)
    add_library(diagon_core_static STATIC ${DIAGON_CORE_SOURCES} ${DIAGON_CORE_HEADERS})
    set_target_properties(diagon_core_static PROPERTIES OUTPUT_NAME diagon_core)
endif()

# ==================== Include Directories ====================

target_include_directories(diagon_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Static library needs same include directories
if(DIAGON_BUILD_STATIC)
    target_include_directories(diagon_core_static
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
endif()

# ==================== Link Libraries ====================

# Link compression libraries
target_link_libraries(diagon_core
    PUBLIC
        ZLIB::ZLIB
)

# Link ZSTD (try zstd::libzstd first, then fallback to zstd::zstd)
if(TARGET zstd::libzstd)
    target_link_libraries(diagon_core PUBLIC zstd::libzstd)
elseif(TARGET zstd::zstd)
    target_link_libraries(diagon_core PUBLIC zstd::zstd)
else()
    message(FATAL_ERROR "ZSTD target not found")
endif()

if(TARGET LZ4::LZ4)
    target_link_libraries(diagon_core PUBLIC LZ4::LZ4)
else()
    target_link_libraries(diagon_core PUBLIC ${LZ4_LIBRARY})
    target_include_directories(diagon_core PUBLIC ${LZ4_INCLUDE_DIR})
endif()

# Define compression library availability
if(HAVE_LZ4)
    target_compile_definitions(diagon_core PUBLIC HAVE_LZ4)
endif()

if(HAVE_ZSTD)
    target_compile_definitions(diagon_core PUBLIC HAVE_ZSTD)
endif()

# Link ICU libraries (for text analysis)
if(TARGET ICU::uc AND TARGET ICU::i18n)
    target_link_libraries(diagon_core PUBLIC ICU::uc ICU::i18n)
else()
    message(FATAL_ERROR "ICU libraries not found")
endif()

# Link cppjieba (for Chinese text segmentation)
# cppjieba is header-only, linking brings in include directories
if(TARGET cppjieba)
    target_link_libraries(diagon_core PRIVATE cppjieba)

    # Add dictionary path as compile definition
    if(DEFINED CPPJIEBA_DICT_DIR)
        target_compile_definitions(diagon_core PRIVATE
            CPPJIEBA_DICT_DIR="${CPPJIEBA_DICT_DIR}")
    endif()

    # Also link for static library if building
    if(DIAGON_BUILD_STATIC)
        target_link_libraries(diagon_core_static PRIVATE cppjieba)
        if(DEFINED CPPJIEBA_DICT_DIR)
            target_compile_definitions(diagon_core_static PRIVATE
                CPPJIEBA_DICT_DIR="${CPPJIEBA_DICT_DIR}")
        endif()
    endif()
else()
    message(FATAL_ERROR "cppjieba not found")
endif()

# ==================== Compiler Features ====================

target_compile_features(diagon_core PUBLIC cxx_std_20)

# ==================== Export Symbols (Windows) ====================

if(WIN32 AND DIAGON_BUILD_SHARED)
    target_compile_definitions(diagon_core
        PRIVATE DIAGON_BUILDING_DLL
        INTERFACE DIAGON_USING_DLL
    )
endif()

message(STATUS "Diagon Core Library configured (utilities only)")
