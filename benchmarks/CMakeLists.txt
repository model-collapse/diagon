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

# Helper function for standalone benchmarks (no Google Benchmark dependency)
function(add_standalone_benchmark BENCHMARK_NAME)
    add_executable(${BENCHMARK_NAME} ${ARGN})

    target_link_libraries(${BENCHMARK_NAME}
        PRIVATE
            diagon_core
    )

    # Link ICU directly
    if(TARGET ICU::uc AND TARGET ICU::i18n)
        target_link_libraries(${BENCHMARK_NAME} PRIVATE ICU::uc ICU::i18n)
    endif()

    target_include_directories(${BENCHMARK_NAME}
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src/core/include
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_custom_target(run_${BENCHMARK_NAME}
        COMMAND ${BENCHMARK_NAME}
        DEPENDS ${BENCHMARK_NAME}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Running benchmark: ${BENCHMARK_NAME}"
    )
endfunction()

# ==================== Google Benchmark Benchmarks ====================

add_diagon_benchmark(IndexingBenchmark IndexingBenchmark.cpp)
add_diagon_benchmark(SearchBenchmark SearchBenchmark.cpp)
add_diagon_benchmark(ReutersWANDBenchmark ReutersWANDBenchmark.cpp)
add_diagon_benchmark(SIMDBenchmark SIMDBenchmark.cpp)
add_diagon_benchmark(StreamVByteSIMDBenchmark StreamVByteSIMDBenchmark.cpp)
add_diagon_benchmark(MMapDirectoryBenchmark MMapDirectoryBenchmark.cpp)
add_diagon_benchmark(Lucene104BatchBenchmark Lucene104BatchBenchmark.cpp)
add_diagon_benchmark(BatchScoringBenchmark BatchScoringBenchmark.cpp)
add_diagon_benchmark(PostingsFormatBenchmark PostingsFormatBenchmark.cpp)
add_diagon_benchmark(PostingsOptimizationBenchmark PostingsOptimizationBenchmark.cpp)
add_diagon_benchmark(ColumnarIngestionBenchmark ColumnarIngestionBenchmark.cpp)
# LuceneCompatBenchmark disabled: uses Java-style factory API not yet implemented in C++
# add_diagon_benchmark(LuceneCompatBenchmark LuceneCompatBenchmark.cpp)
add_diagon_benchmark(LuceneComparisonBenchmark LuceneComparisonBenchmark.cpp)
add_diagon_benchmark(ScaleComparisonBenchmark ScaleComparisonBenchmark.cpp)
add_diagon_benchmark(TokenizerBenchmark TokenizerBenchmark.cpp)
add_diagon_benchmark(FSTEfficiencyGate FSTEfficiencyGate.cpp)

# ==================== Standalone Benchmarks ====================

add_standalone_benchmark(BloomFilterBenchmark BloomFilterBenchmark.cpp)
add_standalone_benchmark(BlockMaxQuantizedIndexBenchmark BlockMaxQuantizedIndexBenchmark.cpp)
add_standalone_benchmark(Lucene104IntegrationValidation Lucene104IntegrationValidation.cpp)
add_standalone_benchmark(ScalabilityTest scalability_test.cpp)
add_standalone_benchmark(ProfileWAND ProfileWAND.cpp)

# ==================== Reuters Benchmark ====================

add_executable(ReutersBenchmark reuters_benchmark.cpp)
target_link_libraries(ReutersBenchmark PRIVATE diagon_core)
if(TARGET ICU::uc AND TARGET ICU::i18n)
    target_link_libraries(ReutersBenchmark PRIVATE ICU::uc ICU::i18n)
endif()
target_include_directories(ReutersBenchmark PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ==================== Combined Benchmark Target ====================

add_custom_target(run_all_benchmarks
    DEPENDS run_IndexingBenchmark run_SearchBenchmark run_SIMDBenchmark run_MMapDirectoryBenchmark run_PostingsFormatBenchmark run_PostingsOptimizationBenchmark run_ColumnarIngestionBenchmark
    COMMENT "Running all benchmarks"
)

message(STATUS "Diagon Benchmarks configured")
