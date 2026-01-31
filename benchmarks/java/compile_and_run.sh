#!/bin/bash
# Compile and run the fair Lucene benchmark

set -e

LUCENE_DIR="/home/ubuntu/opensearch_warmroom/lucene"
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
JAVA_HOME="/usr/lib/jvm/jdk-25.0.2"
export PATH="$JAVA_HOME/bin:$PATH"

echo "=== Fair Lucene Benchmark Setup ==="
echo ""

# Check Java version
echo "Java version:"
java -version
echo ""

# Find Lucene JARs
echo "Locating Lucene JARs..."
CORE_JAR=$(find $LUCENE_DIR/lucene/core/build/libs -name "lucene-core-*.jar" | head -1)
ANALYSIS_JAR=$(find $LUCENE_DIR/lucene/core.analysis.common/build/libs -name "lucene-*.jar" 2>/dev/null | head -1)

# If analysis jar not found, try alternate location
if [ -z "$ANALYSIS_JAR" ]; then
    ANALYSIS_JAR=$(find $LUCENE_DIR/lucene/analysis/common/build/libs -name "lucene-*.jar" 2>/dev/null | head -1)
fi

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
echo "Compiling LuceneBenchmark.java..."
javac -cp "$CLASSPATH" "$SCRIPT_DIR/LuceneBenchmark.java"
echo "✓ Compilation successful"
echo ""

# Run with optimized JVM settings
echo "Running Lucene benchmark with optimized JVM settings..."
echo ""

java -cp "$CLASSPATH:$SCRIPT_DIR" \
    -Xmx4g -Xms4g \
    -XX:+AlwaysPreTouch \
    -XX:+UseG1GC \
    -XX:MaxGCPauseMillis=100 \
    -XX:+ParallelRefProcEnabled \
    LuceneBenchmark

echo ""
echo "=== Benchmark Complete ==="
