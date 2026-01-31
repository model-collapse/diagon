#!/bin/bash
# Diagon vs Lucene: Side-by-Side Performance Comparison
# Run this script to execute the full comparison

set -e

echo "=== Diagon vs Apache Lucene: Search Performance Comparison ==="
echo ""

# ==================== Part 1: Build Diagon in Release Mode ====================
echo "Step 1: Building Diagon in Release mode..."

cd /home/ubuntu/diagon

# Clean build
rm -rf build
mkdir -p build
cd build

# Configure with Release optimizations
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS="-O3 -march=native -flto" \
      -DDIAGON_BUILD_BENCHMARKS=ON \
      ..

# Build benchmark
make LuceneComparisonBenchmark -j$(nproc)

echo "✓ Diagon build complete"
echo ""

# ==================== Part 2: Run Diagon Benchmark ====================
echo "Step 2: Running Diagon benchmarks (5 iterations)..."

cd /home/ubuntu/diagon/build/benchmarks

# Clear caches
sync && sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches' 2>/dev/null || true

# Run benchmark
./LuceneComparisonBenchmark \
    --benchmark_repetitions=5 \
    --benchmark_report_aggregates_only=true \
    --benchmark_out=/tmp/diagon_search_results.json \
    --benchmark_out_format=json

echo "✓ Diagon benchmarks complete"
echo ""

# ==================== Part 3: Set Up Lucene Benchmark ====================
echo "Step 3: Setting up Lucene benchmark..."

cd /home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark

# Build Lucene benchmark module
./gradlew assemble

echo "✓ Lucene build complete"
echo ""

# ==================== Part 4: Generate Synthetic Dataset ====================
echo "Step 4: Generating synthetic dataset for Lucene..."

# Create dataset directory
mkdir -p work/synthetic-10k

# Generate 10K documents matching Diagon's format
python3 << 'PYTHON_SCRIPT'
import random

# Same vocabulary as Diagon benchmark
VOCABULARY = [
    "the", "be", "to", "of", "and", "a", "in", "that", "have", "i",
    "it", "for", "not", "on", "with", "he", "as", "you", "do", "at",
    "this", "but", "his", "by", "from", "they", "we", "say", "her", "she",
    "or", "an", "will", "my", "one", "all", "would", "there", "their", "what",
    "so", "up", "out", "if", "about", "who", "get", "which", "go", "me",
    "when", "make", "can", "like", "time", "no", "just", "him", "know", "take",
    "people", "into", "year", "your", "good", "some", "could", "them", "see", "other",
    "than", "then", "now", "look", "only", "come", "its", "over", "think", "also",
    "back", "after", "use", "two", "how", "our", "work", "first", "well", "way",
    "even", "new", "want", "because", "any", "these", "give", "day", "most", "us"
]

random.seed(42)

with open('work/synthetic-10k/docs.txt', 'w') as f:
    for doc_id in range(10000):
        # Generate 100-word document
        words = [random.choice(VOCABULARY) for _ in range(100)]
        text = ' '.join(words)

        # Lucene LineDocSource format: title<TAB>date<TAB>body
        f.write(f"doc_{doc_id}\t2024-01-01\t{text}\n")

print("Generated 10,000 synthetic documents")
PYTHON_SCRIPT

echo "✓ Dataset generated"
echo ""

# ==================== Part 5: Create Lucene Algorithm File ====================
echo "Step 5: Creating Lucene benchmark configuration..."

cat > conf/diagon_comparison.alg << 'EOF'
# Diagon Comparison Benchmark
# Matches Diagon's LuceneComparisonBenchmark workload

analyzer=org.apache.lucene.analysis.standard.StandardAnalyzer
directory=FSDirectory
work.dir=work
docs.dir=work/synthetic-10k
content.source=org.apache.lucene.benchmark.byTask.feeds.LineDocSource
doc.maker=org.apache.lucene.benchmark.byTask.feeds.DocMaker
content.source.forever=false

# ==================== Indexing ====================
{ "IndexDocs"
    ResetSystemErase
    CreateIndex
    { "AddDocs" AddDoc > : 10000
    Optimize
    CloseIndex
}

# ==================== Search: TermQuery (Common Term) ====================
{ "SearchTermCommon"
    OpenReader
    { "Warmup" Search > : 100
    { "TermQueryCommon" Search("the") > : 10000
    CloseReader
    RepSumByPrefAndRound TermQueryCommon
}

# ==================== Search: TermQuery (Rare Term) ====================
{ "SearchTermRare"
    OpenReader
    { "Warmup" Search > : 100
    { "TermQueryRare" Search("because") > : 10000
    CloseReader
    RepSumByPrefAndRound TermQueryRare
}

# ==================== Search: BooleanQuery AND ====================
{ "SearchBooleanAND"
    OpenReader
    { "Warmup" Search > : 100
    { "BooleanAND" Search("+the +and") > : 10000
    CloseReader
    RepSumByPrefAndRound BooleanAND
}

# ==================== Search: BooleanQuery OR ====================
{ "SearchBooleanOR"
    OpenReader
    { "Warmup" Search > : 100
    { "BooleanOR" Search("people time") > : 10000
    CloseReader
    RepSumByPrefAndRound BooleanOR
}

# ==================== Search: TopK Variation ====================
{ "SearchTopK10"
    OpenReader
    { "Warmup" Search > : 100
    { "TopK10" Search("the") > : 10000
    CloseReader
    RepSumByPrefAndRound TopK10
}

{ "SearchTopK100"
    OpenReader
    { "Warmup" Search > : 100
    { "TopK100" Search("the") > : 10000
    CloseReader
    RepSumByPrefAndRound TopK100
}

{ "SearchTopK1000"
    OpenReader
    { "Warmup" Search > : 100
    { "TopK1000" Search("the") > : 1000
    CloseReader
    RepSumByPrefAndRound TopK1000
}
EOF

echo "✓ Lucene configuration created"
echo ""

# ==================== Part 6: Run Lucene Benchmark ====================
echo "Step 6: Running Lucene benchmarks..."

cd /home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark

# Clear caches
sync && sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches' 2>/dev/null || true

# Run with optimized JVM settings
java -Xmx4g -Xms4g \
     -XX:+AlwaysPreTouch \
     -XX:+UseG1GC \
     -XX:MaxGCPauseMillis=100 \
     -XX:+ParallelRefProcEnabled \
     -jar build/libs/lucene-benchmark-*.jar \
     conf/diagon_comparison.alg \
     > /tmp/lucene_search_results.txt 2>&1

echo "✓ Lucene benchmarks complete"
echo ""

# ==================== Part 7: Parse and Compare Results ====================
echo "Step 7: Parsing and comparing results..."

python3 << 'PYTHON_SCRIPT'
import json
import re
import sys

# Parse Diagon results (JSON)
with open('/tmp/diagon_search_results.json') as f:
    diagon_data = json.load(f)

diagon_results = {}
for benchmark in diagon_data['benchmarks']:
    name = benchmark['name'].replace('BM_Search_', '')
    # Convert to microseconds
    time_us = benchmark['real_time']
    qps = 1_000_000 / time_us if time_us > 0 else 0
    diagon_results[name] = {'time_us': time_us, 'qps': qps}

# Parse Lucene results (text)
with open('/tmp/lucene_search_results.txt') as f:
    lucene_text = f.read()

lucene_results = {}

# Match patterns like: "TermQueryCommon - 12345 - 0.123"
# Format: Operation - Count - Time(sec)
for match in re.finditer(r'(\w+)\s+-\s+(\d+)\s+-\s+([\d.]+)', lucene_text):
    name = match.group(1)
    count = int(match.group(2))
    time_sec = float(match.group(3))

    if count > 0 and time_sec > 0:
        qps = count / time_sec
        time_us = (time_sec / count) * 1_000_000
        lucene_results[name] = {'time_us': time_us, 'qps': qps}

# Map Lucene names to Diagon names
name_mapping = {
    'TermQueryCommon': 'TermQuery_Common',
    'TermQueryRare': 'TermQuery_Rare',
    'BooleanAND': 'BooleanAND',
    'BooleanOR': 'BooleanOR',
    'TopK10': 'TopK/10',
    'TopK100': 'TopK/100',
    'TopK1000': 'TopK/1000'
}

# Generate comparison report
print("\n" + "="*80)
print("DIAGON vs APACHE LUCENE: SEARCH PERFORMANCE COMPARISON")
print("="*80)
print("\n{:<30} {:>12} {:>12} {:>12} {:>10}".format(
    "Benchmark", "Diagon (µs)", "Lucene (µs)", "Speedup", "Winner"))
print("-" * 80)

for lucene_name, diagon_name in name_mapping.items():
    if lucene_name in lucene_results and diagon_name in diagon_results:
        d = diagon_results[diagon_name]
        l = lucene_results[lucene_name]

        speedup = l['time_us'] / d['time_us'] if d['time_us'] > 0 else 0
        winner = "✓ Diagon" if speedup > 1.0 else "✓ Lucene"

        print("{:<30} {:>12.2f} {:>12.2f} {:>12.2f}x {}".format(
            diagon_name, d['time_us'], l['time_us'], speedup, winner))

print("-" * 80)

# QPS Comparison
print("\n{:<30} {:>12} {:>12} {:>12} {:>10}".format(
    "Benchmark", "Diagon QPS", "Lucene QPS", "Ratio", "Winner"))
print("-" * 80)

for lucene_name, diagon_name in name_mapping.items():
    if lucene_name in lucene_results and diagon_name in diagon_results:
        d = diagon_results[diagon_name]
        l = lucene_results[lucene_name]

        ratio = d['qps'] / l['qps'] if l['qps'] > 0 else 0
        winner = "✓ Diagon" if ratio > 1.0 else "✓ Lucene"

        print("{:<30} {:>10.2f}M {:>10.2f}M {:>12.2f}x {}".format(
            diagon_name, d['qps']/1e6, l['qps']/1e6, ratio, winner))

print("-" * 80)

# Summary statistics
if len(name_mapping) > 0:
    speedups = []
    for lucene_name, diagon_name in name_mapping.items():
        if lucene_name in lucene_results and diagon_name in diagon_results:
            d = diagon_results[diagon_name]
            l = lucene_results[lucene_name]
            speedup = l['time_us'] / d['time_us'] if d['time_us'] > 0 else 0
            speedups.append(speedup)

    if speedups:
        avg_speedup = sum(speedups) / len(speedups)
        print(f"\nAverage Speedup: {avg_speedup:.2f}x")
        print(f"Diagon faster: {sum(1 for s in speedups if s > 1.0)}/{len(speedups)} benchmarks")
        print(f"Lucene faster: {sum(1 for s in speedups if s < 1.0)}/{len(speedups)} benchmarks")

print("\n" + "="*80)
PYTHON_SCRIPT

echo ""
echo "=== Comparison Complete ==="
echo ""
echo "Results saved to:"
echo "  - Diagon: /tmp/diagon_search_results.json"
echo "  - Lucene: /tmp/lucene_search_results.txt"
echo ""
