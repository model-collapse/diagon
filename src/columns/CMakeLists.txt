# ==================== Columns Library ====================

message(STATUS "Configuring Diagon Columns Library")

# ==================== Source Files ====================

set(DIAGON_COLUMNS_SOURCES
    # IColumn interface and implementations (Module 03)
    src/IColumn.cpp
    src/ColumnVector.cpp
    src/ColumnString.cpp
    src/ColumnArray.cpp
    src/ColumnNullable.cpp
    src/ColumnTuple.cpp
    src/ColumnConst.cpp

    # Type system
    src/IDataType.cpp
    src/DataTypeNumber.cpp
    src/DataTypeString.cpp
    src/DataTypeArray.cpp
    src/DataTypeNullable.cpp
    src/DataTypeTuple.cpp

    # Serialization
    src/ISerialization.cpp
    src/SerializationNumber.cpp
    src/SerializationString.cpp
    src/SerializationArray.cpp
    src/SerializationNullable.cpp

    # MergeTree data parts (Module 05)
    src/IMergeTreeDataPart.cpp
    src/MergeTreeDataPartWide.cpp
    src/MergeTreeDataPartCompact.cpp
    src/MergeTreeDataPartInMemory.cpp

    # Readers and writers
    src/MergeTreeReaderWide.cpp
    src/MergeTreeReaderCompact.cpp
    src/MergeTreeDataPartWriterWide.cpp
    src/MergeTreeDataPartWriterCompact.cpp

    # Granularity system (Module 06)
    src/IMergeTreeIndexGranularity.cpp
    src/MergeTreeIndexGranularityConstant.cpp
    src/MergeTreeIndexGranularityAdaptive.cpp
    src/MarkInCompressedFile.cpp

    # Skip indexes (Module 11)
    src/IMergeTreeIndex.cpp
    src/MergeTreeIndexMinMax.cpp
    src/MergeTreeIndexSet.cpp
    src/MergeTreeIndexBloomFilter.cpp
    src/MergeTreeIndexFactory.cpp
)

set(DIAGON_COLUMNS_HEADERS
    # IColumn
    include/diagon/columns/IColumn.h
    include/diagon/columns/ColumnVector.h
    include/diagon/columns/ColumnString.h
    include/diagon/columns/ColumnArray.h
    include/diagon/columns/ColumnNullable.h

    # Types
    include/diagon/columns/IDataType.h
    include/diagon/columns/DataTypeNumber.h
    include/diagon/columns/DataTypeString.h

    # Serialization
    include/diagon/columns/ISerialization.h

    # MergeTree
    include/diagon/columns/IMergeTreeDataPart.h
    include/diagon/columns/MergeTreeDataPartWide.h
    include/diagon/columns/MergeTreeDataPartCompact.h

    # Granularity
    include/diagon/columns/IMergeTreeIndexGranularity.h
    include/diagon/columns/MarkInCompressedFile.h

    # Skip indexes
    include/diagon/columns/IMergeTreeIndex.h
)

# ==================== Library Target ====================

add_library(diagon_columns SHARED ${DIAGON_COLUMNS_SOURCES} ${DIAGON_COLUMNS_HEADERS})

if(DIAGON_BUILD_STATIC)
    add_library(diagon_columns_static STATIC ${DIAGON_COLUMNS_SOURCES} ${DIAGON_COLUMNS_HEADERS})
    set_target_properties(diagon_columns_static PROPERTIES OUTPUT_NAME diagon_columns)
endif()

# ==================== Include Directories ====================

target_include_directories(diagon_columns
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ==================== Link Libraries ====================

target_link_libraries(diagon_columns
    PUBLIC
        ZLIB::ZLIB
        zstd::zstd
        LZ4::LZ4
    PRIVATE
        diagon_compression
)

# ==================== Compiler Features ====================

target_compile_features(diagon_columns PUBLIC cxx_std_20)

# ==================== Export Symbols (Windows) ====================

if(WIN32 AND DIAGON_BUILD_SHARED)
    target_compile_definitions(diagon_columns
        PRIVATE DIAGON_BUILDING_DLL
        INTERFACE DIAGON_USING_DLL
    )
endif()

message(STATUS "Diagon Columns Library configured")
