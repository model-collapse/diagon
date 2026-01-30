# ==================== Benchmarks ====================

message(STATUS "Configuring Diagon Benchmarks")

# ==================== Find Google Benchmark ====================

find_package(benchmark REQUIRED)

# ==================== Helper Function ====================

function(add_diagon_benchmark BENCHMARK_NAME)
    add_executable(${BENCHMARK_NAME} ${ARGN})

    target_link_libraries(${BENCHMARK_NAME}
        PRIVATE
            diagon_core
            benchmark::benchmark
            benchmark::benchmark_main
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

# ==================== MMap Directory Benchmarks ====================

add_diagon_benchmark(MMapDirectoryBenchmark
    MMapDirectoryBenchmark.cpp
)

# ==================== Postings Format Benchmarks ====================

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

# ==================== Combined Benchmark Target ====================

add_custom_target(run_all_benchmarks
    DEPENDS run_IndexingBenchmark run_SearchBenchmark run_SIMDBenchmark run_MMapDirectoryBenchmark run_PostingsFormatBenchmark run_PostingsOptimizationBenchmark run_ColumnarIngestionBenchmark run_LuceneCompatBenchmark
    COMMENT "Running all benchmarks"
)

message(STATUS "Diagon Benchmarks configured")
