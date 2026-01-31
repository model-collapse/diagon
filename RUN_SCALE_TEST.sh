#!/bin/bash
# Diagon Scale Testing: Compare performance at 100K, 1M, 10M document scales
# This script builds indexes, runs benchmarks, and compares with Lucene

set -e

echo "================================================================"
echo "DIAGON SCALE TESTING: 100K, 1M, 10M Documents"
echo "================================================================"
echo ""

# ==================== Configuration ====================

# Dataset sizes to test (comment out 10M for faster testing)
SCALES=(
    "100000:100K"
    "1000000:1M"
    # "10000000:10M"  # Uncomment for full scale (requires ~4GB RAM, 30+ min)
)

RESULTS_DIR="/tmp/diagon_scale_results"
mkdir -p "$RESULTS_DIR"

# ==================== Part 1: Build Diagon in Release Mode ====================

echo "Step 1: Building Diagon in Release mode..."
echo ""

cd /home/ubuntu/diagon

# Clean build
if [ -d "build" ]; then
    echo "Cleaning previous build..."
    rm -rf build
fi

mkdir -p build
cd build

# Configure with Release optimizations
echo "Configuring CMake..."
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS="-O3 -march=native -flto" \
      -DDIAGON_BUILD_BENCHMARKS=ON \
      .. > /dev/null 2>&1

# Build benchmark
echo "Building ScaleComparisonBenchmark..."
make ScaleComparisonBenchmark -j$(nproc) > /dev/null 2>&1

if [ ! -f "benchmarks/ScaleComparisonBenchmark" ]; then
    echo "ERROR: Build failed!"
    exit 1
fi

echo "✓ Diagon build complete"
echo ""

# ==================== Part 2: Run Diagon Scale Benchmarks ====================

echo "Step 2: Running Diagon scale benchmarks..."
echo ""
echo "This will:"
echo "  - Build indexes for each scale (cached after first run)"
echo "  - Run 5 query types: TermQuery, BooleanAND, BooleanOR, RareTerm, TopK"
echo "  - Measure latency and QPS at each scale"
echo ""

cd /home/ubuntu/diagon/build/benchmarks

# Clear caches
sync && sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches' 2>/dev/null || echo "Note: Cannot clear caches (need sudo)"

# Run benchmark
echo "Running benchmarks (this may take 10-30 minutes depending on scale)..."
./ScaleComparisonBenchmark \
    --benchmark_out="$RESULTS_DIR/diagon_scale_results.json" \
    --benchmark_out_format=json \
    2>&1 | tee "$RESULTS_DIR/diagon_scale_output.txt"

echo ""
echo "✓ Diagon benchmarks complete"
echo ""

# ==================== Part 3: Set Up Lucene Benchmark ====================

echo "Step 3: Setting up Lucene benchmark..."
echo ""

cd /home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark

# Build if needed
if [ ! -f "build/libs/lucene-benchmark-10.0.0-SNAPSHOT.jar" ]; then
    echo "Building Lucene benchmark module..."
    ./gradlew assemble > /dev/null 2>&1
fi

echo "✓ Lucene build ready"
echo ""

# ==================== Part 4: Generate Datasets for Lucene ====================

echo "Step 4: Generating synthetic datasets for Lucene..."
echo ""

mkdir -p work

# Python script to generate datasets
cat > /tmp/generate_lucene_dataset.py << 'PYTHON_SCRIPT'
import random
import sys

# Same vocabulary as Diagon
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

def generate_dataset(num_docs, output_file):
    random.seed(42)

    print(f"Generating {num_docs} documents to {output_file}...")

    with open(output_file, 'w') as f:
        for doc_id in range(num_docs):
            # Generate 100-word document
            words = [random.choice(VOCABULARY) for _ in range(100)]
            text = ' '.join(words)

            # Lucene LineDocSource format: title<TAB>date<TAB>body
            f.write(f"doc_{doc_id}\t2024-01-01\t{text}\n")

            if (doc_id + 1) % 10000 == 0:
                print(f"  Progress: {doc_id + 1}/{num_docs}")

    print(f"✓ Generated {num_docs} documents")

if __name__ == '__main__':
    num_docs = int(sys.argv[1])
    output_file = sys.argv[2]
    generate_dataset(num_docs, output_file)
PYTHON_SCRIPT

# Generate datasets for each scale
for scale_info in "${SCALES[@]}"; do
    IFS=':' read -r num_docs scale_name <<< "$scale_info"

    output_dir="work/synthetic-${scale_name}"
    output_file="$output_dir/docs.txt"

    if [ -f "$output_file" ]; then
        echo "Dataset ${scale_name} already exists, skipping generation"
    else
        mkdir -p "$output_dir"
        python3 /tmp/generate_lucene_dataset.py "$num_docs" "$output_file"
    fi
done

echo ""
echo "✓ Datasets generated"
echo ""

# ==================== Part 5: Create Lucene Algorithm Files ====================

echo "Step 5: Creating Lucene benchmark configurations..."
echo ""

for scale_info in "${SCALES[@]}"; do
    IFS=':' read -r num_docs scale_name <<< "$scale_info"

    config_file="conf/diagon_scale_${scale_name}.alg"

    cat > "$config_file" << EOF
# Diagon Scale Test: ${scale_name} documents

analyzer=org.apache.lucene.analysis.standard.StandardAnalyzer
directory=FSDirectory
work.dir=work
docs.dir=work/synthetic-${scale_name}
content.source=org.apache.lucene.benchmark.byTask.feeds.LineDocSource
doc.maker=org.apache.lucene.benchmark.byTask.feeds.DocMaker
content.source.forever=false

# ==================== Indexing ====================
{ "IndexDocs"
    ResetSystemErase
    CreateIndex
    { "AddDocs" AddDoc > : ${num_docs}
    Optimize
    CloseIndex
    RepSumByPrefAndRound
}

# ==================== Search: TermQuery (Common Term) ====================
{ "SearchTermCommon"
    OpenReader
    { "Warmup" Search > : 100
    { "TermQueryCommon" Search("the") > : 1000
    CloseReader
    RepSumByPrefAndRound TermQueryCommon
}

# ==================== Search: TermQuery (Rare Term) ====================
{ "SearchTermRare"
    OpenReader
    { "Warmup" Search > : 100
    { "TermQueryRare" Search("because") > : 1000
    CloseReader
    RepSumByPrefAndRound TermQueryRare
}

# ==================== Search: BooleanQuery AND ====================
{ "SearchBooleanAND"
    OpenReader
    { "Warmup" Search > : 100
    { "BooleanAND" Search("+the +and") > : 1000
    CloseReader
    RepSumByPrefAndRound BooleanAND
}

# ==================== Search: BooleanQuery OR ====================
{ "SearchBooleanOR"
    OpenReader
    { "Warmup" Search > : 100
    { "BooleanOR" Search("people time") > : 1000
    CloseReader
    RepSumByPrefAndRound BooleanOR
}
EOF

    echo "✓ Created configuration for ${scale_name}"
done

echo ""

# ==================== Part 6: Run Lucene Benchmarks ====================

echo "Step 6: Running Lucene benchmarks..."
echo ""

for scale_info in "${SCALES[@]}"; do
    IFS=':' read -r num_docs scale_name <<< "$scale_info"

    echo "Running Lucene benchmark for ${scale_name} documents..."

    # Clear caches
    sync && sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches' 2>/dev/null || true

    # Run Lucene benchmark
    java -Xmx8g -Xms8g \
         -XX:+AlwaysPreTouch \
         -XX:+UseG1GC \
         -XX:MaxGCPauseMillis=100 \
         -XX:+ParallelRefProcEnabled \
         -jar build/libs/lucene-benchmark-*.jar \
         conf/diagon_scale_${scale_name}.alg \
         > "$RESULTS_DIR/lucene_scale_${scale_name}.txt" 2>&1

    echo "✓ Lucene benchmark for ${scale_name} complete"
done

echo ""
echo "✓ All Lucene benchmarks complete"
echo ""

# ==================== Part 7: Parse and Compare Results ====================

echo "Step 7: Parsing and comparing results..."
echo ""

cat > /tmp/compare_scale_results.py << 'PYTHON_SCRIPT'
import json
import re
import sys

# Parse Diagon results
with open('/tmp/diagon_scale_results/diagon_scale_results.json') as f:
    diagon_data = json.load(f)

# Organize Diagon results by scale and query type
diagon_results = {}
for benchmark in diagon_data['benchmarks']:
    name = benchmark['name']

    # Extract scale from label
    label = benchmark.get('label', '')

    # Extract query type
    if 'TermQuery' in name and 'Boolean' not in name and 'Rare' not in name:
        query_type = 'TermQuery_Common'
    elif 'RareTerm' in name:
        query_type = 'TermQuery_Rare'
    elif 'BooleanAND' in name:
        query_type = 'BooleanAND'
    elif 'BooleanOR' in name:
        query_type = 'BooleanOR'
    elif 'TopK' in name:
        query_type = 'TopK'
    else:
        continue

    time_us = benchmark['real_time']
    qps = 1_000_000 / time_us if time_us > 0 else 0

    if label not in diagon_results:
        diagon_results[label] = {}

    diagon_results[label][query_type] = {
        'time_us': time_us,
        'qps': qps,
        'docs': benchmark.get('docs', 0),
        'index_mb': benchmark.get('index_mb', 0)
    }

# Parse Lucene results
lucene_results = {}
scales = ['100K', '1M', '10M']

for scale in scales:
    lucene_file = f'/tmp/diagon_scale_results/lucene_scale_{scale}.txt'
    try:
        with open(lucene_file) as f:
            lucene_text = f.read()

        lucene_results[scale] = {}

        # Extract search results
        for match in re.finditer(r'(\w+)\s+-\s+(\d+)\s+-\s+([\d.]+)', lucene_text):
            name = match.group(1)
            count = int(match.group(2))
            time_sec = float(match.group(3))

            if count > 0 and time_sec > 0:
                qps = count / time_sec
                time_us = (time_sec / count) * 1_000_000
                lucene_results[scale][name] = {
                    'time_us': time_us,
                    'qps': qps
                }

        # Extract indexing stats
        index_match = re.search(r'AddDocs\s+-\s+(\d+)\s+-\s+([\d.]+)', lucene_text)
        if index_match:
            count = int(index_match.group(1))
            time_sec = float(index_match.group(2))
            lucene_results[scale]['indexing_docs_per_sec'] = count / time_sec

    except FileNotFoundError:
        print(f"Warning: Lucene results for {scale} not found")

# Map query names
query_mapping = {
    'TermQueryCommon': 'TermQuery_Common',
    'TermQueryRare': 'TermQuery_Rare',
    'BooleanAND': 'BooleanAND',
    'BooleanOR': 'BooleanOR'
}

# Generate comparison report
print("\n" + "="*100)
print("DIAGON vs APACHE LUCENE: SCALE TESTING COMPARISON")
print("="*100)

for scale in scales:
    if scale not in diagon_results and scale not in lucene_results:
        continue

    print(f"\n{'='*100}")
    print(f"SCALE: {scale} Documents")
    print(f"{'='*100}")

    # Index statistics
    if scale in diagon_results and 'TermQuery_Common' in diagon_results[scale]:
        d = diagon_results[scale]['TermQuery_Common']
        print(f"\nIndex Statistics:")
        print(f"  Diagon:")
        print(f"    - Documents: {d['docs']:,}")
        print(f"    - Index size: {d['index_mb']:.1f} MB")
        print(f"    - Bytes per doc: {d['index_mb'] * 1024 * 1024 / d['docs']:.1f}")

    # Search performance
    print(f"\nSearch Performance:")
    print(f"  {'':<30} {'Diagon (µs)':>15} {'Lucene (µs)':>15} {'Speedup':>12} {'Winner':>10}")
    print(f"  {'-'*82}")

    for lucene_name, diagon_name in query_mapping.items():
        if scale in diagon_results and diagon_name in diagon_results[scale]:
            d = diagon_results[scale][diagon_name]

            if scale in lucene_results and lucene_name in lucene_results[scale]:
                l = lucene_results[scale][lucene_name]

                speedup = l['time_us'] / d['time_us'] if d['time_us'] > 0 else 0
                winner = "✓ Diagon" if speedup > 1.0 else "✓ Lucene"

                print(f"  {diagon_name:<30} {d['time_us']:>15.2f} {l['time_us']:>15.2f} {speedup:>12.2f}x {winner:>10}")

    print(f"\n  QPS (Queries Per Second):")
    print(f"  {'':<30} {'Diagon QPS':>15} {'Lucene QPS':>15} {'Ratio':>12} {'Winner':>10}")
    print(f"  {'-'*82}")

    for lucene_name, diagon_name in query_mapping.items():
        if scale in diagon_results and diagon_name in diagon_results[scale]:
            d = diagon_results[scale][diagon_name]

            if scale in lucene_results and lucene_name in lucene_results[scale]:
                l = lucene_results[scale][lucene_name]

                ratio = d['qps'] / l['qps'] if l['qps'] > 0 else 0
                winner = "✓ Diagon" if ratio > 1.0 else "✓ Lucene"

                print(f"  {diagon_name:<30} {d['qps']/1e6:>13.2f}M {l['qps']/1e6:>13.2f}M {ratio:>12.2f}x {winner:>10}")

print("\n" + "="*100)
print("ANALYSIS")
print("="*100)

# Calculate average speedups per scale
for scale in scales:
    if scale not in diagon_results or scale not in lucene_results:
        continue

    speedups = []
    for lucene_name, diagon_name in query_mapping.items():
        if diagon_name in diagon_results[scale] and lucene_name in lucene_results[scale]:
            d = diagon_results[scale][diagon_name]
            l = lucene_results[scale][lucene_name]
            speedup = l['time_us'] / d['time_us'] if d['time_us'] > 0 else 0
            speedups.append(speedup)

    if speedups:
        avg_speedup = sum(speedups) / len(speedups)
        diagon_faster = sum(1 for s in speedups if s > 1.0)
        lucene_faster = sum(1 for s in speedups if s < 1.0)

        print(f"\n{scale} Documents:")
        print(f"  Average Speedup: {avg_speedup:.2f}x")
        print(f"  Diagon faster: {diagon_faster}/{len(speedups)} queries")
        print(f"  Lucene faster: {lucene_faster}/{len(speedups)} queries")

print("\n" + "="*100)
PYTHON_SCRIPT

python3 /tmp/compare_scale_results.py | tee "$RESULTS_DIR/comparison_report.txt"

echo ""
echo "================================================================"
echo "SCALE TESTING COMPLETE"
echo "================================================================"
echo ""
echo "Results saved to: $RESULTS_DIR"
echo ""
echo "Files:"
echo "  - diagon_scale_results.json    (Diagon benchmark data)"
echo "  - diagon_scale_output.txt      (Diagon console output)"
echo "  - lucene_scale_100K.txt        (Lucene 100K results)"
echo "  - lucene_scale_1M.txt          (Lucene 1M results)"
echo "  - comparison_report.txt        (Side-by-side comparison)"
echo ""
