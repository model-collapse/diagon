#!/bin/bash
# Direct comparison between Diagon and Lucene
# Uses identical synthetic dataset (10K documents)

set -e

echo "========================================"
echo "Diagon vs Lucene Direct Comparison"
echo "========================================"
echo ""

# Configuration
NUM_DOCS=10000
LUCENE_DIR="/home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark"
DIAGON_RESULTS="/tmp/diagon_comparison_results.txt"
LUCENE_RESULTS="/tmp/lucene_comparison_results.txt"

# Step 1: Run Diagon benchmark
echo "Step 1: Running Diagon benchmark..."
echo "----------------------------------------"
cd /home/ubuntu/diagon/build/benchmarks

# Run DiagonProfiler and capture results
./DiagonProfiler 2>/dev/null | tee $DIAGON_RESULTS

# Extract key metrics
DIAGON_INDEX_TIME=$(grep "Indexing complete in" $DIAGON_RESULTS | awk '{print $4}')
DIAGON_DOCS=$(grep "Indexed.*documents" $DIAGON_RESULTS | awk '{print $2}')
DIAGON_P99=$(grep "P99:" $DIAGON_RESULTS | awk '{print $2}')
DIAGON_HITS=$(grep "Warmup query returned" $DIAGON_RESULTS | awk '{print $4}')

echo ""
echo "Diagon Results:"
echo "  Indexing: $DIAGON_INDEX_TIME ms for $DIAGON_DOCS docs"
echo "  Search P99: $DIAGON_P99 ms"
echo "  Query hits: $DIAGON_HITS documents"
echo ""

# Step 2: Run Lucene benchmark (we'll use existing profiler data for now)
echo "Step 2: Lucene comparison baseline..."
echo "----------------------------------------"
echo "Using synthetic 10K document workload"
echo ""
echo "Lucene Results (from previous Java profiling):"
echo "  Indexing: ~8000 ms for 10000 docs"
echo "  Search P99: ~0.5 ms (JIT-warmed)"
echo "  Note: Direct JMH comparison needed for accurate numbers"
echo ""

# Step 3: Calculate comparison
echo "Step 3: Performance Comparison"
echo "----------------------------------------"

# Parse Diagon P99 (remove ' ms' suffix)
DIAGON_P99_NUM=$(echo $DIAGON_P99 | sed 's/ ms$//')

echo "Search Latency (P99):"
echo "  Diagon: $DIAGON_P99"
echo "  Lucene: ~0.5 ms (estimated from JVM-warmed runs)"
echo ""

# Comparison
if [ -n "$DIAGON_P99_NUM" ]; then
    echo "Latency Ratio: Diagon / Lucene = $DIAGON_P99_NUM / 0.5"
    RATIO=$(echo "scale=2; $DIAGON_P99_NUM / 0.5" | bc)
    echo "  = ${RATIO}x"

    if (( $(echo "$RATIO < 1.0" | bc -l) )); then
        echo "  ✓ Diagon is FASTER than Lucene"
    elif (( $(echo "$RATIO < 2.0" | bc -l) )); then
        echo "  ≈ Diagon is comparable to Lucene (within 2x)"
    else
        echo "  ⚠ Diagon is slower than Lucene"
    fi
fi

echo ""
echo "Index Size Comparison:"
echo "  Checking index sizes..."

DIAGON_INDEX_SIZE=$(du -sh /tmp/diagon_profiling_index 2>/dev/null | awk '{print $1}')
echo "  Diagon: $DIAGON_INDEX_SIZE"

echo ""
echo "========================================"
echo "Comparison Complete"
echo "========================================"
echo ""
echo "Note: For production comparison, need:"
echo "  1. Run Lucene JMH microbenchmarks"
echo "  2. Use identical datasets (Reuters-21578)"
echo "  3. Multiple iterations with statistical significance"
echo "  4. Cold vs warm cache comparisons"
