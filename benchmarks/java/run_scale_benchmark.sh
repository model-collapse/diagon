#!/bin/bash
# Run 10M document scale benchmark

set -e

LUCENE_DIR="/home/ubuntu/opensearch_warmroom/lucene"
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
JAVA_HOME="/usr/lib/jvm/jdk-25.0.2"
export PATH="$JAVA_HOME/bin:$PATH"

NUM_DOCS=${1:-10000000}  # Default 10M, can override

echo "=== Lucene 10M Document Scale Benchmark ==="
echo ""
echo "Configuration:"
echo "  - Documents: $(printf "%'d" $NUM_DOCS)"
echo "  - Words per doc: 100"
echo "  - Java: $(java -version 2>&1 | head -1)"
echo ""

# Find Lucene JARs
CORE_JAR=$(find $LUCENE_DIR/lucene/core/build/libs -name "lucene-core-*.jar" | head -1)
ANALYSIS_JAR=$(find $LUCENE_DIR/lucene/analysis/common/build/libs -name "lucene-*.jar" | head -1)

if [ -z "$CORE_JAR" ]; then
    echo "ERROR: Lucene JARs not found"
    exit 1
fi

CLASSPATH="$CORE_JAR:$ANALYSIS_JAR"

# Compile
echo "Compiling..."
javac -cp "$CLASSPATH" "$SCRIPT_DIR/ScaleBenchmark.java"
echo "✓ Compiled"
echo ""

# Run with production JVM settings
echo "Starting benchmark (this will take 10-20 minutes)..."
echo ""

java -cp "$CLASSPATH:$SCRIPT_DIR" \
    -Xmx8g -Xms8g \
    -XX:+AlwaysPreTouch \
    -XX:+UseG1GC \
    -XX:MaxGCPauseMillis=100 \
    -XX:+ParallelRefProcEnabled \
    ScaleBenchmark $NUM_DOCS 100 2>&1 | tee /tmp/lucene_10m_results.txt

echo ""
echo "=== Complete ==="
echo "Results saved to: /tmp/lucene_10m_results.txt"
echo "Index location: /tmp/lucene_scale_10m"
