# ==================== Diagnostic Tools ====================

message(STATUS "Configuring Diagon Tools")

# ==================== LuceneFormatValidator ====================

add_executable(LuceneFormatValidator LuceneFormatValidator.cpp)

set_target_properties(LuceneFormatValidator PROPERTIES
    BUILD_RPATH_USE_ORIGIN TRUE
    INSTALL_RPATH_USE_LINK_PATH FALSE
    BUILD_RPATH "\$ORIGIN/../src/core:/usr/lib/x86_64-linux-gnu"
)

target_link_libraries(LuceneFormatValidator
    PRIVATE
        diagon_core
)

# Link ICU directly (needed because diagon_core.so doesn't embed ICU symbols)
if(TARGET ICU::uc AND TARGET ICU::i18n)
    target_link_libraries(LuceneFormatValidator PRIVATE ICU::uc ICU::i18n)
endif()

target_include_directories(LuceneFormatValidator
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core/include
)

# ==================== Lucene90TermLookupValidator ====================

add_executable(Lucene90TermLookupValidator Lucene90TermLookupValidator.cpp)

set_target_properties(Lucene90TermLookupValidator PROPERTIES
    BUILD_RPATH_USE_ORIGIN TRUE
    INSTALL_RPATH_USE_LINK_PATH FALSE
    BUILD_RPATH "\$ORIGIN/../src/core:/usr/lib/x86_64-linux-gnu"
)

target_link_libraries(Lucene90TermLookupValidator
    PRIVATE
        diagon_core
)

if(TARGET ICU::uc AND TARGET ICU::i18n)
    target_link_libraries(Lucene90TermLookupValidator PRIVATE ICU::uc ICU::i18n)
endif()

target_include_directories(Lucene90TermLookupValidator
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core/include
)
