# ==================== Compression Library ====================

message(STATUS "Configuring Diagon Compression Library")

# ==================== Source Files ====================

set(DIAGON_COMPRESSION_SOURCES
    # Compression codec interface (Module 04)
    src/ICompressionCodec.cpp
    src/CompressionFactory.cpp

    # Generic codecs
    src/CompressionCodecNone.cpp
    src/CompressionCodecLZ4.cpp
    src/CompressionCodecZSTD.cpp

    # Type-specific codecs
    src/CompressionCodecDelta.cpp
    src/CompressionCodecDoubleDelta.cpp
    src/CompressionCodecGorilla.cpp
    src/CompressionCodecT64.cpp

    # Codec chaining
    src/CompressionCodecMultiple.cpp

    # Compressed I/O
    src/CompressedReadBuffer.cpp
    src/CompressedWriteBuffer.cpp
    src/CompressedBlockHeader.cpp
)

set(DIAGON_COMPRESSION_HEADERS
    include/diagon/compression/ICompressionCodec.h
    include/diagon/compression/CompressionFactory.h
    include/diagon/compression/CompressionCodecLZ4.h
    include/diagon/compression/CompressionCodecZSTD.h
    include/diagon/compression/CompressionCodecDelta.h
    include/diagon/compression/CompressionCodecGorilla.h
    include/diagon/compression/CompressedReadBuffer.h
    include/diagon/compression/CompressedWriteBuffer.h
)

# ==================== Library Target ====================

add_library(diagon_compression SHARED ${DIAGON_COMPRESSION_SOURCES} ${DIAGON_COMPRESSION_HEADERS})

if(DIAGON_BUILD_STATIC)
    add_library(diagon_compression_static STATIC ${DIAGON_COMPRESSION_SOURCES} ${DIAGON_COMPRESSION_HEADERS})
    set_target_properties(diagon_compression_static PROPERTIES OUTPUT_NAME diagon_compression)
endif()

# ==================== Include Directories ====================

target_include_directories(diagon_compression
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ==================== Link Libraries ====================

target_link_libraries(diagon_compression
    PUBLIC
        ZLIB::ZLIB
        zstd::zstd
        LZ4::LZ4
)

# ==================== Compiler Features ====================

target_compile_features(diagon_compression PUBLIC cxx_std_20)

# ==================== Export Symbols (Windows) ====================

if(WIN32 AND DIAGON_BUILD_SHARED)
    target_compile_definitions(diagon_compression
        PRIVATE DIAGON_BUILDING_DLL
        INTERFACE DIAGON_USING_DLL
    )
endif()

message(STATUS "Diagon Compression Library configured")
