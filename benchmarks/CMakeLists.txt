# ==================== Benchmarks ====================

message(STATUS "Configuring Diagon Benchmarks")

# ==================== Find Google Benchmark ====================

find_package(benchmark REQUIRED)

# ==================== Helper Function ====================

function(add_diagon_benchmark BENCHMARK_NAME)
    add_executable(${BENCHMARK_NAME} ${ARGN})

    # Set explicit RPATH to prefer system libraries
    set_target_properties(${BENCHMARK_NAME} PROPERTIES
        BUILD_RPATH_USE_ORIGIN TRUE
        INSTALL_RPATH_USE_LINK_PATH FALSE
        BUILD_RPATH "\$ORIGIN/../src/core:/usr/lib/x86_64-linux-gnu"
    )

    target_link_libraries(${BENCHMARK_NAME}
        PRIVATE
            diagon_core
            benchmark::benchmark
            benchmark::benchmark_main
    )

    # Link ICU directly (needed because diagon_core.so doesn't embed ICU symbols)
    if(TARGET ICU::uc AND TARGET ICU::i18n)
        target_link_libraries(${BENCHMARK_NAME} PRIVATE ICU::uc ICU::i18n)
    endif()

    target_include_directories(${BENCHMARK_NAME}
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src/core/include
    )

    # Register as benchmark target
    add_custom_target(run_${BENCHMARK_NAME}
        COMMAND ${BENCHMARK_NAME}
        DEPENDS ${BENCHMARK_NAME}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Running benchmark: ${BENCHMARK_NAME}"
    )
endfunction()

# ==================== Indexing Benchmarks ====================

add_diagon_benchmark(IndexingBenchmark
    IndexingBenchmark.cpp
)

# ==================== Search Benchmarks ====================

add_diagon_benchmark(SearchBenchmark
    SearchBenchmark.cpp
)

# ==================== SIMD Benchmarks ====================

add_diagon_benchmark(SIMDBenchmark
    SIMDBenchmark.cpp
)

add_diagon_benchmark(StreamVByteSIMDBenchmark
    StreamVByteSIMDBenchmark.cpp
)

# ==================== MMap Directory Benchmarks ====================

add_diagon_benchmark(MMapDirectoryBenchmark
    MMapDirectoryBenchmark.cpp
)

# ==================== Postings Format Benchmarks ====================

add_diagon_benchmark(Lucene104BatchBenchmark
    Lucene104BatchBenchmark.cpp
)

add_diagon_benchmark(BatchScoringBenchmark
    BatchScoringBenchmark.cpp
)

add_diagon_benchmark(SIMDComparisonBenchmark
    SIMDComparisonBenchmark.cpp
)

add_diagon_benchmark(PostingsFormatBenchmark
    PostingsFormatBenchmark.cpp
)

add_diagon_benchmark(PostingsOptimizationBenchmark
    PostingsOptimizationBenchmark.cpp
)

# ==================== Columnar Ingestion Benchmarks ====================

add_diagon_benchmark(ColumnarIngestionBenchmark
    ColumnarIngestionBenchmark.cpp
)

# ==================== Lucene Comparison Benchmarks ====================

add_diagon_benchmark(LuceneCompatBenchmark
    LuceneCompatBenchmark.cpp
)

# ==================== Standalone Benchmarks ====================

# Helper function for standalone benchmarks (no Google Benchmark dependency)
function(add_standalone_benchmark BENCHMARK_NAME)
    add_executable(${BENCHMARK_NAME} ${ARGN})

    target_link_libraries(${BENCHMARK_NAME}
        PRIVATE
            diagon_core
    )

    target_include_directories(${BENCHMARK_NAME}
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src/core/include
    )

    # Register as benchmark target
    add_custom_target(run_${BENCHMARK_NAME}
        COMMAND ${BENCHMARK_NAME}
        DEPENDS ${BENCHMARK_NAME}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Running benchmark: ${BENCHMARK_NAME}"
    )
endfunction()

# Bloom Filter Benchmark (standalone, uses MSMarco dataset)
add_standalone_benchmark(BloomFilterBenchmark
    BloomFilterBenchmark.cpp
)

# Block-Max Quantized Index Benchmark (standalone, uses MSMarco dataset)
add_standalone_benchmark(BlockMaxQuantizedIndexBenchmark
    BlockMaxQuantizedIndexBenchmark.cpp
)

# Lucene104 Integration Validation (Phase 4.1: BlockTreeTermsWriter)
add_standalone_benchmark(Lucene104IntegrationValidation
    Lucene104IntegrationValidation.cpp
)

# Tokenizer Benchmark (micro-benchmark for FastTokenizer)
add_diagon_benchmark(TokenizerBenchmark
    TokenizerBenchmark.cpp
)

# ==================== Lucene Comparison Benchmark ====================

add_diagon_benchmark(LuceneComparisonBenchmark
    LuceneComparisonBenchmark.cpp
)

# ==================== Scale Testing Benchmark ====================

add_diagon_benchmark(ScaleComparisonBenchmark
    ScaleComparisonBenchmark.cpp
)

# ==================== Diagon Profiler (CONJUGATE Performance Comparison) ====================

# Diagon profiler for comparing against Lucene (requires nlohmann/json)
add_executable(DiagonProfiler diagon_profiler.cpp)

target_link_libraries(DiagonProfiler
    PRIVATE
        diagon_core
)

# Link ICU directly
if(TARGET ICU::uc AND TARGET ICU::i18n)
    target_link_libraries(DiagonProfiler PRIVATE ICU::uc ICU::i18n)
endif()

target_include_directories(DiagonProfiler
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core/include
)

# Register as benchmark target
add_custom_target(run_DiagonProfiler
    COMMAND DiagonProfiler
    DEPENDS DiagonProfiler
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running Diagon profiler for Lucene comparison"
)

# Diagon profiler debug tool
add_executable(DiagonProfilerDebug diagon_profiler_debug.cpp)

target_link_libraries(DiagonProfilerDebug
    PRIVATE
        diagon_core
)

# Link ICU directly
if(TARGET ICU::uc AND TARGET ICU::i18n)
    target_link_libraries(DiagonProfilerDebug PRIVATE ICU::uc ICU::i18n)
endif()

target_include_directories(DiagonProfilerDebug
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core/include
)

# Index diagnostics tool
add_executable(IndexDiagnostics index_diagnostics.cpp)

target_link_libraries(IndexDiagnostics
    PRIVATE
        diagon_core
)

# Link ICU directly
if(TARGET ICU::uc AND TARGET ICU::i18n)
    target_link_libraries(IndexDiagnostics PRIVATE ICU::uc ICU::i18n)
endif()

target_include_directories(IndexDiagnostics
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core/include
)

# Profiler verification tool
add_executable(ProfilerVerification profiler_verification.cpp)

target_link_libraries(ProfilerVerification
    PRIVATE
        diagon_core
)

# Link ICU directly
if(TARGET ICU::uc AND TARGET ICU::i18n)
    target_link_libraries(ProfilerVerification PRIVATE ICU::uc ICU::i18n)
endif()

target_include_directories(ProfilerVerification
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core/include
)

# Field inspector tool
add_executable(FieldInspector field_inspector.cpp)

target_link_libraries(FieldInspector
    PRIVATE
        diagon_core
)

# Link ICU directly
if(TARGET ICU::uc AND TARGET ICU::i18n)
    target_link_libraries(FieldInspector PRIVATE ICU::uc ICU::i18n)
endif()

target_include_directories(FieldInspector
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core/include
)

# Search trace tool
add_executable(SearchTrace search_trace.cpp)

target_link_libraries(SearchTrace
    PRIVATE
        diagon_core
)

# Link ICU directly
if(TARGET ICU::uc AND TARGET ICU::i18n)
    target_link_libraries(SearchTrace PRIVATE ICU::uc ICU::i18n)
endif()

target_include_directories(SearchTrace
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core/include
)

# Minimal field test tool
add_executable(MinimalFieldTest minimal_field_test.cpp)

target_link_libraries(MinimalFieldTest
    PRIVATE
        diagon_core
)

# Link ICU directly
if(TARGET ICU::uc AND TARGET ICU::i18n)
    target_link_libraries(MinimalFieldTest PRIVATE ICU::uc ICU::i18n)
endif()

target_include_directories(MinimalFieldTest
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core/include
)

# Test get terms tool
add_executable(TestGetTerms test_get_terms.cpp)

target_link_libraries(TestGetTerms
    PRIVATE
        diagon_core
)

# Link ICU directly
if(TARGET ICU::uc AND TARGET ICU::i18n)
    target_link_libraries(TestGetTerms PRIVATE ICU::uc ICU::i18n)
endif()

target_include_directories(TestGetTerms
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core/include
)

# Field isolation test
add_executable(FieldIsolationTest field_isolation_test.cpp)

target_link_libraries(FieldIsolationTest
    PRIVATE
        diagon_core
)

# Link ICU directly
if(TARGET ICU::uc AND TARGET ICU::i18n)
    target_link_libraries(FieldIsolationTest PRIVATE ICU::uc ICU::i18n)
endif()

target_include_directories(FieldIsolationTest
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core/include
)

# Multi-block regression test
add_executable(MultiBlockRegressionTest multiblock_regression_test.cpp)

target_link_libraries(MultiBlockRegressionTest
    PRIVATE
        diagon_core
)

# Link ICU directly
if(TARGET ICU::uc AND TARGET ICU::i18n)
    target_link_libraries(MultiBlockRegressionTest PRIVATE ICU::uc ICU::i18n)
endif()

target_include_directories(MultiBlockRegressionTest
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core/include
)

# Production-scale scalability test
add_executable(ScalabilityTest scalability_test.cpp)

target_link_libraries(ScalabilityTest
    PRIVATE
        diagon_core
)

# Link ICU directly
if(TARGET ICU::uc AND TARGET ICU::i18n)
    target_link_libraries(ScalabilityTest PRIVATE ICU::uc ICU::i18n)
endif()

target_include_directories(ScalabilityTest
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Reuters-21578 benchmark
add_executable(ReutersBenchmark reuters_benchmark.cpp)

target_link_libraries(ReutersBenchmark
    PRIVATE
        diagon_core
)

# Link ICU directly
if(TARGET ICU::uc AND TARGET ICU::i18n)
    target_link_libraries(ReutersBenchmark PRIVATE ICU::uc ICU::i18n)
endif()

target_include_directories(ReutersBenchmark
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# ==================== Combined Benchmark Target ====================

add_custom_target(run_all_benchmarks
    DEPENDS run_IndexingBenchmark run_SearchBenchmark run_SIMDBenchmark run_MMapDirectoryBenchmark run_PostingsFormatBenchmark run_PostingsOptimizationBenchmark run_ColumnarIngestionBenchmark run_LuceneCompatBenchmark
    COMMENT "Running all benchmarks"
)

message(STATUS "Diagon Benchmarks configured")
