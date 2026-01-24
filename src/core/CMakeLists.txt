# ==================== Core Library ====================

message(STATUS "Configuring Diagon Core Library")

# ==================== Source Files ====================

set(DIAGON_CORE_SOURCES
    # Util (Utilities)
    src/util/BytesRef.cpp
    src/util/BitSet.cpp
    src/util/NumericUtils.cpp

    # Store (Directory abstraction - Module 09)
    src/store/IOContext.cpp
    src/store/Directory.cpp
    src/store/IndexInput.cpp
    src/store/IndexOutput.cpp
    src/store/FSDirectory.cpp

    # Index (FieldInfo - Module 10, IndexReader/Writer - Module 01)
    src/index/FieldInfo.cpp
    src/index/IndexReader.cpp
    src/index/IndexWriter.cpp

    # Codecs (Codec architecture - Module 02)
    src/codecs/Codec.cpp
    src/codecs/PostingsFormat.cpp
    src/codecs/DocValuesFormat.cpp
    src/codecs/ColumnFormat.cpp
    src/codecs/lucene104/Lucene104Codec.cpp
)

set(DIAGON_CORE_HEADERS
    # Util
    include/diagon/util/BytesRef.h
    include/diagon/util/BitSet.h
    include/diagon/util/NumericUtils.h
    include/diagon/util/Exceptions.h

    # Store
    include/diagon/store/IOContext.h
    include/diagon/store/Directory.h
    include/diagon/store/IndexInput.h
    include/diagon/store/IndexOutput.h
    include/diagon/store/FSDirectory.h
    include/diagon/store/Lock.h

    # Index
    include/diagon/index/FieldInfo.h
    include/diagon/index/IndexReader.h
    include/diagon/index/IndexWriter.h
    include/diagon/index/MergePolicy.h
    include/diagon/index/MergeSpecification.h
    include/diagon/index/OneMerge.h
    include/diagon/index/MergeScheduler.h
    include/diagon/index/TieredMergePolicy.h

    # Skip Indexes
    include/diagon/index/skipindex/IMergeTreeIndexGranule.h
    include/diagon/index/skipindex/IMergeTreeIndexAggregator.h
    include/diagon/index/skipindex/IMergeTreeIndexCondition.h
    include/diagon/index/skipindex/IMergeTreeIndex.h
    include/diagon/index/skipindex/MergeTreeIndexMinMax.h

    # Codecs
    include/diagon/codecs/Codec.h
    include/diagon/codecs/PostingsFormat.h
    include/diagon/codecs/DocValuesFormat.h
    include/diagon/codecs/ColumnFormat.h
    include/diagon/codecs/OtherFormats.h
    include/diagon/codecs/SegmentState.h
    include/diagon/codecs/lucene104/Lucene104Codec.h

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
)

# ==================== Library Target ====================

add_library(diagon_core SHARED ${DIAGON_CORE_SOURCES} ${DIAGON_CORE_HEADERS})

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

# ==================== Link Libraries ====================

# Core utilities have no external dependencies (yet)
# target_link_libraries(diagon_core
#     PUBLIC
#         ZLIB::ZLIB
#         zstd::zstd
#         LZ4::LZ4
#     # TODO: Add when implemented
#     # PRIVATE
#     #     diagon_columns
#     #     diagon_compression
# )

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
