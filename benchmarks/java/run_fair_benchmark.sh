#!/bin/bash
# Run the fair Lucene benchmark with production-aligned settings

set -e

LUCENE_DIR="/home/ubuntu/opensearch_warmroom/lucene"
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
JAVA_HOME="/usr/lib/jvm/jdk-25.0.2"
export PATH="$JAVA_HOME/bin:$PATH"

echo "=== Fair Apple-to-Apple Lucene Benchmark ==="
echo ""

# Check Java version
echo "Java version:"
java -version
echo ""

# Find Lucene JARs
echo "Locating Lucene JARs..."
CORE_JAR=$(find $LUCENE_DIR/lucene/core/build/libs -name "lucene-core-*.jar" | head -1)
ANALYSIS_JAR=$(find $LUCENE_DIR/lucene/analysis/common/build/libs -name "lucene-*.jar" 2>/dev/null | head -1)

if [ -z "$CORE_JAR" ]; then
    echo "ERROR: Lucene core JAR not found. Building Lucene..."
    cd $LUCENE_DIR
    ./gradlew :lucene:core:assemble :lucene:analysis:common:assemble
    CORE_JAR=$(find $LUCENE_DIR/lucene/core/build/libs -name "lucene-core-*.jar" | head -1)
    ANALYSIS_JAR=$(find $LUCENE_DIR/lucene/analysis/common/build/libs -name "lucene-*.jar" | head -1)
fi

echo "Lucene Core: $CORE_JAR"
echo "Lucene Analysis: $ANALYSIS_JAR"
echo ""

# Build classpath
CLASSPATH="$CORE_JAR"
if [ -n "$ANALYSIS_JAR" ]; then
    CLASSPATH="$CLASSPATH:$ANALYSIS_JAR"
fi

# Compile
echo "Compiling FairLuceneBenchmark.java..."
javac -cp "$CLASSPATH" "$SCRIPT_DIR/FairLuceneBenchmark.java"
echo "✓ Compilation successful"
echo ""

# Run with production-aligned JVM settings
echo "Running fair Lucene benchmark..."
echo "Settings:"
echo "  - Heap: 4GB (fixed size)"
echo "  - GC: G1GC (production default)"
echo "  - AlwaysPreTouch: enabled (reduce allocation overhead)"
echo "  - Warmup: 10,000 iterations (extended for proper JIT)"
echo "  - Directory: MMapDirectory (production default)"
echo ""

java -cp "$CLASSPATH:$SCRIPT_DIR" \
    -Xmx4g -Xms4g \
    -XX:+AlwaysPreTouch \
    -XX:+UseG1GC \
    -XX:MaxGCPauseMillis=100 \
    -XX:+ParallelRefProcEnabled \
    -Xlog:gc:file=/tmp/lucene_fair_gc.log \
    FairLuceneBenchmark 2>&1 | tee /tmp/lucene_fair_results.txt

echo ""
echo "=== Benchmark Complete ==="
echo ""
echo "Results saved to: /tmp/lucene_fair_results.txt"
echo "GC log saved to: /tmp/lucene_fair_gc.log"
