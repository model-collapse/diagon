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

add_diagon_benchmark(WANDBenchmark
    WANDBenchmark.cpp
)

add_diagon_benchmark(ReutersWANDBenchmark
    ReutersWANDBenchmark.cpp
)

add_diagon_benchmark(LuceneMultiTermComparison
    LuceneMultiTermComparison.cpp
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

# ==================== Reuters Reindexer Tool ====================

add_executable(ReutersReindexer ReutersReindexer.cpp)
target_link_libraries(ReutersReindexer diagon_core)

# ==================== Tokenizer Comparison Tool ====================

add_executable(TokenizerComparison TokenizerComparison.cpp)
target_link_libraries(TokenizerComparison diagon_core)

# ==================== StandardTokenizer Test ====================

add_executable(StandardTokenizerTest StandardTokenizerTest.cpp)
target_link_libraries(StandardTokenizerTest diagon_core)

# ==================== Quick BM25 Test ====================

add_executable(QuickBM25Test QuickBM25Test.cpp)
target_link_libraries(QuickBM25Test diagon_core)

# ==================== BM25 Diagnostic Test ====================

add_executable(BM25DiagnosticTest BM25DiagnosticTest.cpp)
target_link_libraries(BM25DiagnosticTest diagon_core)

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

# ==================== Diagon Profiler V1 (CONJUGATE Performance Comparison) ====================

# Diagon profiler V1 for comparing against Lucene (requires nlohmann/json)
add_executable(DiagonProfilerV1 diagon_profiler.cpp)

target_link_libraries(DiagonProfilerV1
    PRIVATE
        diagon_core
)

# Link ICU directly
if(TARGET ICU::uc AND TARGET ICU::i18n)
    target_link_libraries(DiagonProfilerV1 PRIVATE ICU::uc ICU::i18n)
endif()

target_include_directories(DiagonProfilerV1
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core/include
)

# Register as benchmark target
add_custom_target(run_DiagonProfilerV1
    COMMAND DiagonProfilerV1
    DEPENDS DiagonProfilerV1
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running Diagon profiler V1 for Lucene comparison"
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

# Diagon Profiler (matching Lucene profiler for comparison)
add_executable(DiagonProfiler DiagonProfiler.cpp)

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
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Detailed Query Profiler (phase-by-phase breakdown)
add_executable(DetailedQueryProfiler DetailedQueryProfiler.cpp)

target_link_libraries(DetailedQueryProfiler
    PRIVATE
        diagon_core
)

# Link ICU directly
if(TARGET ICU::uc AND TARGET ICU::i18n)
    target_link_libraries(DetailedQueryProfiler PRIVATE ICU::uc ICU::i18n)
endif()

target_include_directories(DetailedQueryProfiler
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Compare Top-10 results with Lucene
add_executable(CompareTop10 CompareTop10.cpp)

target_link_libraries(CompareTop10
    PRIVATE
        diagon_core
)

# Link ICU directly
if(TARGET ICU::uc AND TARGET ICU::i18n)
    target_link_libraries(CompareTop10 PRIVATE ICU::uc ICU::i18n)
endif()

target_include_directories(CompareTop10
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core/include
)

# Check term statistics
add_executable(CheckTermStats CheckTermStats.cpp)

target_link_libraries(CheckTermStats
    PRIVATE
        diagon_core
)

# Link ICU directly
if(TARGET ICU::uc AND TARGET ICU::i18n)
    target_link_libraries(CheckTermStats PRIVATE ICU::uc ICU::i18n)
endif()

target_include_directories(CheckTermStats
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core/include
)

# WAND Instrumented Profiler
add_executable(WANDInstrumentedProfiler WANDInstrumentedProfiler.cpp)

target_link_libraries(WANDInstrumentedProfiler
    PRIVATE
        diagon_core
)

# Link ICU directly
if(TARGET ICU::uc AND TARGET ICU::i18n)
    target_link_libraries(WANDInstrumentedProfiler PRIVATE ICU::uc ICU::i18n)
endif()

target_include_directories(WANDInstrumentedProfiler
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core/include
)

# Debug WAND tool
add_executable(debug_wand debug_wand.cpp)

# Score breakdown diagnostic tool
add_executable(score_breakdown score_breakdown.cpp)

target_link_libraries(score_breakdown
    PRIVATE
        diagon_core
)

# Link ICU directly
if(TARGET ICU::uc AND TARGET ICU::i18n)
    target_link_libraries(score_breakdown PRIVATE ICU::uc ICU::i18n)
endif()

target_include_directories(score_breakdown
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(debug_wand
    PRIVATE
        diagon_core
)

# Link ICU directly
if(TARGET ICU::uc AND TARGET ICU::i18n)
    target_link_libraries(debug_wand PRIVATE ICU::uc ICU::i18n)
endif()

target_include_directories(debug_wand
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# ==================== Combined Benchmark Target ====================

add_custom_target(run_all_benchmarks
    DEPENDS run_IndexingBenchmark run_SearchBenchmark run_SIMDBenchmark run_MMapDirectoryBenchmark run_PostingsFormatBenchmark run_PostingsOptimizationBenchmark run_ColumnarIngestionBenchmark run_LuceneCompatBenchmark
    COMMENT "Running all benchmarks"
)

# ==================== Results Comparison Tools ====================

# ReutersResultsComparison - not a benchmark, just outputs results
add_executable(ReutersResultsComparison ReutersResultsComparison.cpp)

set_target_properties(ReutersResultsComparison PROPERTIES
    BUILD_RPATH_USE_ORIGIN TRUE
    INSTALL_RPATH_USE_LINK_PATH FALSE
    BUILD_RPATH "\$ORIGIN/../src/core:/usr/lib/x86_64-linux-gnu"
)

target_link_libraries(ReutersResultsComparison
    PRIVATE
        diagon_core
)

# Link ICU directly
if(TARGET ICU::uc AND TARGET ICU::i18n)
    target_link_libraries(ReutersResultsComparison PRIVATE ICU::uc ICU::i18n)
endif()

target_include_directories(ReutersResultsComparison
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core/include
)

# DiagonIndexDiagnostics - inspect index statistics
add_executable(DiagonIndexDiagnostics DiagonIndexDiagnostics.cpp)

set_target_properties(DiagonIndexDiagnostics PROPERTIES
    BUILD_RPATH_USE_ORIGIN TRUE
    INSTALL_RPATH_USE_LINK_PATH FALSE
    BUILD_RPATH "\$ORIGIN/../src/core:/usr/lib/x86_64-linux-gnu"
)

target_link_libraries(DiagonIndexDiagnostics
    PRIVATE
        diagon_core
)

# Link ICU directly
if(TARGET ICU::uc AND TARGET ICU::i18n)
    target_link_libraries(DiagonIndexDiagnostics PRIVATE ICU::uc ICU::i18n)
endif()

target_include_directories(DiagonIndexDiagnostics
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core/include
)

message(STATUS "Diagon Benchmarks configured")

add_executable(PostingsDecodingProfiler PostingsDecodingProfiler.cpp)
target_link_libraries(PostingsDecodingProfiler
    PRIVATE
        diagon_core
)

add_executable(FSTTraversalProfiler FSTTraversalProfiler.cpp)
target_link_libraries(FSTTraversalProfiler
    PRIVATE
        diagon_core
)

add_executable(FSTProfileBenchmark FSTProfileBenchmark.cpp)
target_link_libraries(FSTProfileBenchmark
    PRIVATE
        diagon_core
)

add_executable(BM25ProfileBenchmark BM25ProfileBenchmark.cpp)
target_link_libraries(BM25ProfileBenchmark
    PRIVATE
        diagon_core
)

add_executable(BM25DetailedProfiler BM25DetailedProfiler.cpp)
target_link_libraries(BM25DetailedProfiler
    PRIVATE
        diagon_core
)

add_executable(SearchInternalProfiler SearchInternalProfiler.cpp)
target_link_libraries(SearchInternalProfiler
    PRIVATE
        diagon_core
)
target_compile_definitions(SearchInternalProfiler PRIVATE DIAGON_PROFILE_SEARCH)

add_executable(ManualSearchProfiler ManualSearchProfiler.cpp)
target_link_libraries(ManualSearchProfiler
    PRIVATE
        diagon_core
)

add_executable(WANDEffectivenessProfiler WANDEffectivenessProfiler.cpp)
target_link_libraries(WANDEffectivenessProfiler
    PRIVATE
        diagon_core
)

add_executable(WANDDebugStats WANDDebugStats.cpp)
target_link_libraries(WANDDebugStats
    PRIVATE
        diagon_core
)

add_executable(TestIndexBug TestIndexBug.cpp)
target_link_libraries(TestIndexBug
    PRIVATE
        diagon_core
)

add_executable(InspectIndexFields InspectIndexFields.cpp)
target_link_libraries(InspectIndexFields
    PRIVATE
        diagon_core
)

add_executable(InspectIndexFiles InspectIndexFiles.cpp)
target_link_libraries(InspectIndexFiles
    PRIVATE
        diagon_core
)

add_executable(DebugTestIndex DebugTestIndex.cpp)
target_link_libraries(DebugTestIndex
    PRIVATE
        diagon_core
)

add_executable(ProfileOR5 ProfileOR5.cpp)
target_link_libraries(ProfileOR5
    PRIVATE
        diagon_core
)

add_executable(ProfileOR5Short ProfileOR5Short.cpp)
target_link_libraries(ProfileOR5Short
    PRIVATE
        diagon_core
)

add_executable(ProfileIndexOpening ProfileIndexOpening.cpp)
target_link_libraries(ProfileIndexOpening
    PRIVATE
        diagon_core
)

add_executable(AnalyzeHitCountDifference AnalyzeHitCountDifference.cpp)
target_link_libraries(AnalyzeHitCountDifference
    PRIVATE
        diagon_core
)

add_executable(TestWANDHitCounting TestWANDHitCounting.cpp)
target_link_libraries(TestWANDHitCounting
    PRIVATE
        diagon_core
)

add_executable(CheckLongTokens CheckLongTokens.cpp)
target_link_libraries(CheckLongTokens
    PRIVATE
        diagon_core
)

# FST Efficiency Gate (Performance Regression Detection)
add_diagon_benchmark(FSTEfficiencyGate FSTEfficiencyGate.cpp)

# FST Regression Detection Task
add_custom_target(check_fst_regression
    COMMAND ${CMAKE_COMMAND} -E echo "Running FST Efficiency Gate..."
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/FSTEfficiencyGate
        --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/fst_current.json
        --benchmark_out_format=json
        --benchmark_repetitions=3
        --benchmark_report_aggregates_only=true
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "Checking for regressions against baseline..."
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check_fst_regression.py
        ${CMAKE_CURRENT_BINARY_DIR}/fst_current.json
        ${CMAKE_SOURCE_DIR}/benchmark_results/fst_baseline.json
    DEPENDS FSTEfficiencyGate
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running FST performance regression detection"
    VERBATIM
)

# FST Baseline Update Task (use after verified improvements)
add_custom_target(update_fst_baseline
    COMMAND ${CMAKE_COMMAND} -E echo "Establishing new FST performance baseline..."
    COMMAND ${CMAKE_COMMAND} -E echo "Running benchmark with 5 repetitions for stable baseline..."
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/FSTEfficiencyGate
        --benchmark_out=${CMAKE_SOURCE_DIR}/benchmark_results/fst_baseline.json
        --benchmark_out_format=json
        --benchmark_repetitions=5
        --benchmark_report_aggregates_only=true
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "✅ Baseline updated: benchmark_results/fst_baseline.json"
    COMMAND ${CMAKE_COMMAND} -E echo "⚠️  IMPORTANT: Commit this baseline with explanation:"
    COMMAND ${CMAKE_COMMAND} -E echo "   git add benchmark_results/fst_baseline.json"
    COMMAND ${CMAKE_COMMAND} -E echo "   git commit -m \"Update FST baseline: [reason]\""
    DEPENDS FSTEfficiencyGate
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Establishing new FST performance baseline"
    VERBATIM
)

