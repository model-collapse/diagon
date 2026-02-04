#!/bin/bash
# Local performance comparison script
# Compares current performance against stored baseline

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BASELINE_FILE="$PROJECT_ROOT/performance_baseline.json"
RESULTS_DIR="$PROJECT_ROOT/performance_history"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "========================================"
echo "Diagon Performance Comparison"
echo "========================================"
echo ""

# Ensure build exists
if [ ! -d "$PROJECT_ROOT/build" ]; then
    echo "Error: Build directory not found. Run cmake first."
    exit 1
fi

# Run benchmarks
echo "Step 1: Running benchmarks..."
echo "----------------------------------------"
cd "$PROJECT_ROOT/build/benchmarks"

./DiagonProfiler 2>/dev/null | tee /tmp/diagon_perf_results.txt

# Extract metrics
P99=$(grep "P99:" /tmp/diagon_perf_results.txt | awk '{print $2}' | sed 's/ ms$//')
DOCS=$(grep "✓ Indexed" /tmp/diagon_perf_results.txt | awk '{print $3}')
INDEX_TIME=$(grep "✓ Indexing complete in" /tmp/diagon_perf_results.txt | awk '{print $5}')
HITS=$(grep "✓ Warmup query returned" /tmp/diagon_perf_results.txt | awk '{print $5}')

echo ""
echo "Current Results:"
echo "  P99 Latency: ${P99} ms"
echo "  Documents: ${DOCS}"
echo "  Index Time: ${INDEX_TIME} ms"
echo "  Query Hits: ${HITS}"
echo ""

# Load baseline
if [ ! -f "$BASELINE_FILE" ]; then
    echo "No baseline found. Creating initial baseline..."
    mkdir -p "$(dirname "$BASELINE_FILE")"

    cat > "$BASELINE_FILE" <<EOF
{
  "p99": "$P99",
  "docs": "$DOCS",
  "index_time": "$INDEX_TIME",
  "hits": "$HITS",
  "commit": "$(git rev-parse HEAD 2>/dev/null || echo 'unknown')",
  "date": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
EOF

    echo "✓ Baseline created: $BASELINE_FILE"
    exit 0
fi

# Compare with baseline
echo "Step 2: Comparing with baseline..."
echo "----------------------------------------"

BASELINE_P99=$(jq -r '.p99' "$BASELINE_FILE")
BASELINE_DOCS=$(jq -r '.docs' "$BASELINE_FILE")
BASELINE_DATE=$(jq -r '.date' "$BASELINE_FILE")

echo "Baseline (from $BASELINE_DATE):"
echo "  P99 Latency: ${BASELINE_P99} ms"
echo "  Documents: ${BASELINE_DOCS}"
echo ""

# Calculate percentage change
REGRESSION=$(echo "scale=2; (($P99 - $BASELINE_P99) / $BASELINE_P99) * 100" | bc)

echo "Comparison:"
echo "  P99 Change: ${REGRESSION}%"

# Determine status
if (( $(echo "$REGRESSION > 10" | bc -l) )); then
    echo -e "  Status: ${RED}⚠️  REGRESSION DETECTED${NC}"
    echo ""
    echo "Performance regressed by more than 10%!"
    echo "Current: ${P99} ms"
    echo "Baseline: ${BASELINE_P99} ms"
    echo ""
    echo "Action Required:"
    echo "  1. Investigate recent changes"
    echo "  2. Profile hot paths"
    echo "  3. Revert if necessary"
    exit 1
elif (( $(echo "$REGRESSION < -10" | bc -l) )); then
    echo -e "  Status: ${GREEN}✅ IMPROVEMENT DETECTED${NC}"
    echo ""
    echo "Performance improved by $(echo "scale=2; -1 * $REGRESSION" | bc)%!"
    echo "Consider updating baseline."
elif (( $(echo "$REGRESSION > 5" | bc -l) )); then
    echo -e "  Status: ${YELLOW}⚠️  MINOR REGRESSION${NC}"
    echo ""
    echo "Performance slightly degraded (${REGRESSION}%)."
    echo "Monitor for further regressions."
else
    echo -e "  Status: ${GREEN}✅ STABLE${NC}"
fi

echo ""

# Save to history
mkdir -p "$RESULTS_DIR"
COMMIT_HASH=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
DATE_STR=$(date +%Y-%m-%d)

cat > "$RESULTS_DIR/${DATE_STR}_${COMMIT_HASH}.json" <<EOF
{
  "date": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "commit": "$COMMIT_HASH",
  "p99": "$P99",
  "docs": "$DOCS",
  "index_time": "$INDEX_TIME",
  "hits": "$HITS",
  "regression_vs_baseline": "$REGRESSION"
}
EOF

echo "✓ Results saved to: $RESULTS_DIR/${DATE_STR}_${COMMIT_HASH}.json"
echo ""

# Ask to update baseline
if (( $(echo "$REGRESSION < -5" | bc -l) )); then
    echo "Would you like to update the baseline? (y/n)"
    read -r UPDATE_BASELINE

    if [ "$UPDATE_BASELINE" = "y" ]; then
        cat > "$BASELINE_FILE" <<EOF
{
  "p99": "$P99",
  "docs": "$DOCS",
  "index_time": "$INDEX_TIME",
  "hits": "$HITS",
  "commit": "$COMMIT_HASH",
  "date": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
EOF
        echo "✓ Baseline updated"
    fi
fi

echo ""
echo "========================================"
echo "Comparison Complete"
echo "========================================"
