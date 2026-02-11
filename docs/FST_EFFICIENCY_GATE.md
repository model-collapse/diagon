# FST Efficiency Gate - Performance Regression Detection

**Purpose**: Continuous performance monitoring to detect FST regressions > 10% before merge.

**Status**: ✅ Active (Baseline established: 2026-02-11)

---

## Overview

The FST Efficiency Gate is an automated system that:
1. Benchmarks key FST operations (construction, lookup, iteration, serialization)
2. Compares results against established baseline
3. **Warns if performance regresses > 10%**
4. **Fails if performance regresses > 20%**
5. Tracks performance trends over time

---

## Quick Start

### Option 1: CMake Task (Recommended)

```bash
cd /home/ubuntu/diagon/build

# Check for regressions (runs benchmark + comparison)
make check_fst_regression
```

**Exit codes**:
- `0`: No significant regression (PASS)
- `1`: Regression > 10% detected (FAIL)

**First-time setup** (if no baseline exists):
```bash
# Establish baseline
make update_fst_baseline

# Commit baseline (required)
git add benchmark_results/fst_baseline.json
git commit -m "Establish FST baseline on [platform/date]"
```

### Option 2: Manual Execution

```bash
cd /home/ubuntu/diagon/build/benchmarks

# Run benchmark (outputs JSON)
./FSTEfficiencyGate --benchmark_out=current_fst.json --benchmark_out_format=json

# Check for regressions against baseline
python3 ../../scripts/check_fst_regression.py \
    current_fst.json \
    ../../benchmark_results/fst_baseline.json
```

**Exit codes**:
- `0`: No significant regression (PASS)
- `1`: Regression > 10% detected (FAIL)
- `2`: Error (file not found, etc.)

---

## Benchmark Coverage

### FST Construction Benchmarks

Measures time to build FST from sorted terms.

| Benchmark | Terms | Baseline (mean) | Threshold (10%) | Notes |
|-----------|-------|-----------------|-----------------|-------|
| `BM_FST_Construction_1K` | 1,000 | 249.5 µs | 274.5 µs | Small FST |
| `BM_FST_Construction_10K` | 10,000 | 2.55 ms | 2.80 ms | Typical FST |
| `BM_FST_Construction_100K` | 100,000 | 34.19 ms | 37.61 ms | Large FST |

**What it measures**:
- FST::Builder::add() performance
- Node freezing and sharing
- Memory allocation overhead
- Hash table operations

### FST Lookup Benchmarks

Measures term dictionary lookup performance.

| Benchmark | Baseline (mean) | Threshold (10%) | Notes |
|-----------|-----------------|-----------------|-------|
| `BM_FST_Lookup_ExactMatch` | 159.8 ns | 175.8 ns | Existing terms (100% hit) |
| `BM_FST_Lookup_CacheMiss` | 37.2 ns | 40.9 ns | Missing terms (100% miss) |
| `BM_FST_Lookup_Mixed` | 126.7 ns | 139.4 ns | Realistic (70% hit, 30% miss) |

**What it measures**:
- FST::get() performance
- Arc traversal efficiency
- Branch prediction
- Cache locality

**Note**: Cache miss is FASTER than exact match because early exit (no arc traversal).

### FST Iteration Benchmarks

Measures sequential term enumeration performance.

| Benchmark | Baseline (mean) | Threshold (10%) | Per-term time | Throughput |
|-----------|-----------------|-----------------|---------------|------------|
| `BM_FST_Iteration_Full` | 310.6 µs | 341.7 µs | 31.06 ns/term | 32.2M terms/sec |
| `BM_FST_Iteration_Large` | 3.15 ms | 3.46 ms | 31.49 ns/term | 31.8M terms/sec |

**What it measures**:
- FST::getAllEntries() performance
- Sequential memory access
- Vector allocation
- Cache-friendly iteration

### FST Serialization Benchmarks

Measures FST serialization/deserialization overhead.

| Benchmark | Baseline (mean) | Threshold (10%) | Notes |
|-----------|-----------------|-----------------|-------|
| `BM_FST_Serialization` | 76.6 µs | 84.2 µs | Serialize to bytes |
| `BM_FST_Deserialization` | 43.0 µs | 47.3 µs | Deserialize from bytes |
| `BM_FST_Roundtrip` | 117.3 µs | 129.0 µs | Full roundtrip |

**What it measures**:
- Serialization format efficiency
- Memory copy overhead
- Buffer allocation

### FST Memory Footprint Benchmarks

Measures FST size in bytes (smaller is better).

| Term Count | Baseline | Bytes/term | Threshold (+10%) |
|------------|----------|------------|------------------|
| 1,000 | 35.0 KB | 35.0 | 38.5 KB |
| 10,000 | 348.8 KB | 34.9 | 383.7 KB |
| 100,000 | 3.49 MB | 34.9 | 3.84 MB |

**What it measures**:
- FST compression efficiency
- Node sharing effectiveness
- Arc encoding overhead

**Note**: Bytes/term stays constant (~35 bytes) showing good scalability.

---

## Regression Thresholds

### Performance Thresholds

| Level | Range | Action | Example |
|-------|-------|--------|---------|
| **Improvement** | < -5% | ✅ PASS (celebrate!) | 150 ns → 140 ns (-6.7%) |
| **Acceptable** | -5% to +10% | ✅ PASS | 150 ns → 160 ns (+6.7%) |
| **Warning** | +10% to +20% | ⚠️  PASS (review) | 150 ns → 175 ns (+16.7%) |
| **Critical** | > +20% | ❌ FAIL (block merge) | 150 ns → 200 ns (+33.3%) |

### Rationale

**Why 10% threshold?**
- Measurement noise: ~2-3% (warmup, CPU frequency scaling)
- Acceptable variation: ~5-7% (compiler changes, dependencies)
- **Safety margin**: 10% threshold catches real regressions while allowing normal variation

**Why 20% critical threshold?**
- Severe regressions require immediate action
- Blocks merge to prevent production impact
- Typically indicates algorithmic bug or major issue

---

## Baseline Management

### Current Baseline

**File**: `/home/ubuntu/diagon/benchmark_results/fst_baseline.json`
**Established**: 2026-02-11
**Platform**: AWS EC2 (Ubuntu, AVX2)
**Compiler**: GCC 13.3.0, Release mode (-O3 -march=native)

### When to Update Baseline

**Update baseline after**:
1. **Verified performance improvement** (e.g., new optimization merged)
2. **Major architectural change** (e.g., new FST encoding)
3. **Compiler/dependency upgrade** (e.g., GCC 14.0)
4. **Platform change** (e.g., new CI/CD runner)

**DO NOT update baseline**:
- To make CI pass (defeats purpose!)
- Without understanding performance change
- Without team review and approval

### How to Update Baseline

```bash
cd /home/ubuntu/diagon/build/benchmarks

# Run benchmark with multiple repetitions for stable baseline
./FSTEfficiencyGate \
    --benchmark_out=new_baseline.json \
    --benchmark_out_format=json \
    --benchmark_repetitions=5 \
    --benchmark_report_aggregates_only=true

# Verify improvements
python3 ../../scripts/check_fst_regression.py \
    new_baseline.json \
    ../../benchmark_results/fst_baseline.json

# If improvements are real, update baseline
cp new_baseline.json ../../benchmark_results/fst_baseline.json

# Commit with explanation
git add benchmark_results/fst_baseline.json
git commit -m "Update FST baseline: [reason for change]

[Detailed explanation of why baseline changed]
[Performance improvements or environment changes]"
```

---

## CMake Tasks

### check_fst_regression

**Purpose**: Run FST benchmark and automatically check for performance regressions.

**Usage**:
```bash
cd /home/ubuntu/diagon/build
make check_fst_regression
```

**What it does**:
1. Runs `FSTEfficiencyGate` benchmark (3 repetitions, aggregated results)
2. Outputs results to `build/benchmarks/fst_current.json`
3. Runs `check_fst_regression.py` comparing current vs baseline
4. Reports regressions, warnings, and improvements
5. Exits with code 0 (pass) or 1 (fail)

**When to use**:
- Before committing FST changes
- In pre-merge CI/CD pipeline
- After suspected performance regressions
- Regular performance monitoring

**Example output** (passing):
```
Running FST Efficiency Gate...
Running 56 benchmarks...
[Benchmark results...]

Checking for regressions against baseline...
✅ PASS: No significant regressions detected
  Improvements:    2
  Acceptable:     54
  Warnings:        0
  Critical:        0
```

**Example output** (warning):
```
⚠️  WARNING: Performance regressions detected (10-20% slower)
  ⚠️  BM_FST_Lookup_ExactMatch: 159.8 ns → 181.5 ns (+13.6%)
   Action: Review and consider optimization
   Status: PASS (within 20% threshold)
```

**Example output** (critical failure):
```
❌ FAIL: Critical regressions detected (>20% slower)
  ❌ BM_FST_Construction_10K: 2.55 ms → 3.32 ms (+30.2%)
   Action: Investigate and fix before merging
make: *** [check_fst_regression] Error 1
```

---

### update_fst_baseline

**Purpose**: Establish or update the FST performance baseline.

**Usage**:
```bash
cd /home/ubuntu/diagon/build
make update_fst_baseline
```

**What it does**:
1. Runs `FSTEfficiencyGate` benchmark (5 repetitions for stable baseline)
2. Saves results to `benchmark_results/fst_baseline.json`
3. Prints reminder to commit the new baseline

**When to use**:
- **First-time setup**: No baseline exists yet
- **After verified improvements**: Performance optimizations merged
- **After architectural changes**: New FST implementation
- **After environment changes**: New compiler, platform, or dependencies

**⚠️ IMPORTANT**: Always commit baseline with explanation:
```bash
git add benchmark_results/fst_baseline.json
git commit -m "Update FST baseline: [reason]

[Detailed explanation of why baseline changed]
[Performance improvements or environment changes]"
```

**DO NOT update baseline to make CI pass** - defeats the purpose of regression detection!

**Example workflow** (after optimization):
```bash
# 1. Verify improvement
make check_fst_regression
# Output: ✅ BM_FST_Lookup_ExactMatch: 159.8 ns → 145.2 ns (-9.1% improvement)

# 2. Update baseline (if improvement is real)
make update_fst_baseline

# 3. Commit with explanation
git add benchmark_results/fst_baseline.json
git commit -m "Update FST baseline: Optimized arc traversal

Improved lookup performance by 9.1% through better cache locality
in arc traversal. See PR #123 for details."
```

---

## CI/CD Integration

### GitHub Actions Workflow

**File**: `.github/workflows/fst_efficiency_gate.yml`

```yaml
name: FST Efficiency Gate

on:
  pull_request:
    branches: [main]
    paths:
      - 'src/core/include/diagon/util/FST.h'
      - 'src/core/src/util/FST.cpp'
      - 'src/core/src/util/PackedFST.cpp'
      - 'benchmarks/FSTEfficiencyGate.cpp'

jobs:
  fst-regression-check:
    runs-on: ubuntu-latest

    steps:
      - uses: actions/checkout@v3

      - name: Install Dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake g++ libbenchmark-dev

      - name: Build Diagon
        run: |
          cmake -DCMAKE_BUILD_TYPE=Release \
                -DDIAGON_BUILD_BENCHMARKS=ON \
                build
          cmake --build build -j$(nproc)

      - name: Check for FST Regressions
        id: regression_check
        run: |
          cd build
          make check_fst_regression 2>&1 | tee ../regression_report.txt
        continue-on-error: true

      - name: Comment on PR
        if: always()
        uses: actions/github-script@v6
        with:
          script: |
            const fs = require('fs');
            const report = fs.readFileSync('regression_report.txt', 'utf8');

            await github.rest.issues.createComment({
              issue_number: context.issue.number,
              owner: context.repo.owner,
              repo: context.repo.repo,
              body: '## FST Efficiency Gate Results\n\n```\n' + report + '\n```'
            });

      - name: Fail on Critical Regression
        if: steps.regression_check.outcome == 'failure'
        run: |
          echo "❌ Critical performance regression detected!"
          echo "See regression_report.txt for details."
          exit 1

      - name: Upload Results
        uses: actions/upload-artifact@v3
        with:
          name: fst-benchmark-results
          path: |
            build/benchmarks/fst_current.json
            regression_report.txt
```

**Trigger conditions**:
- Pull requests that modify FST code
- Changes to benchmark code
- Manual workflow dispatch

---

## Interpreting Results

### Example Output (PASS - No Regression)

```
FST Performance Regression Check
================================================================================
Baseline:  benchmark_results/fst_baseline.json
Current:   current_fst.json
Threshold: 10% regression (warning), 20% (critical)
================================================================================

Regression Analysis:
--------------------------------------------------------------------------------

🎉 Performance Improvements (2 benchmarks):
  ✅ BM_FST_Lookup_ExactMatch         Baseline: 159.8 ns  Current: 145.2 ns  Change:  -9.1%
  ✅ BM_FST_Construction_10K          Baseline:   2.55 ms Current:   2.35 ms Change:  -7.8%

✅ Acceptable Performance (54 benchmarks):
  [... all other benchmarks within threshold ...]

================================================================================
Summary:
  Improvements:    2
  Acceptable:     54
  Warnings:        0 (10-20% slower)
  Critical:        0 (>20% slower)
  Total:          56
================================================================================

✅ PASS: No significant regressions detected
```

### Example Output (WARNING - 10-20% Regression)

```
⚠️  Performance Warnings (2 benchmarks):
   (10-20% slower than baseline)
  ⚠️  BM_FST_Lookup_ExactMatch         Baseline: 159.8 ns  Current: 181.5 ns  Change: +13.6%
  ⚠️  BM_FST_Construction_10K          Baseline:   2.55 ms Current:   2.88 ms Change: +12.9%

================================================================================
Summary:
  Improvements:    0
  Acceptable:     54
  Warnings:        2 (10-20% slower)
  Critical:        0 (>20% slower)
  Total:          56
================================================================================

⚠️  WARNING: Performance regressions detected (10-20% slower)
   Action: Review and consider optimization
   Status: PASS (within 20% threshold)
```

**Action**: Review changes, profile if necessary, optimize if feasible.

### Example Output (FAIL - Critical Regression)

```
❌ CRITICAL REGRESSIONS (2 benchmarks):
   (>20% slower than baseline - IMMEDIATE ACTION REQUIRED)
  ❌ BM_FST_Lookup_ExactMatch         Baseline: 159.8 ns  Current: 215.7 ns  Change: +35.0%
  ❌ BM_FST_Construction_10K          Baseline:   2.55 ms Current:   3.32 ms Change: +30.2%

================================================================================
Summary:
  Improvements:    0
  Acceptable:     54
  Warnings:        0 (10-20% slower)
  Critical:        2 (>20% slower)
  Total:          56
================================================================================

❌ FAIL: Critical regressions detected (>20% slower)
   Action: Investigate and fix before merging
```

**Action**: **DO NOT MERGE**. Investigate root cause, fix regression, rerun benchmark.

---

## Troubleshooting

### Issue: Benchmark results vary significantly between runs

**Symptom**: Same code shows different results (>5% variation)

**Causes**:
- CPU frequency scaling not disabled
- Background processes consuming CPU
- Thermal throttling
- Swapping/memory pressure

**Fix**:
```bash
# Disable CPU frequency scaling
sudo cpupower frequency-set --governor performance

# Check for background load
top -b -n 1 | head -20

# Ensure no swapping
free -h

# Run with more repetitions for stable average
./FSTEfficiencyGate --benchmark_repetitions=10
```

### Issue: All benchmarks show regression after environment change

**Symptom**: Every benchmark is 10-20% slower

**Causes**:
- Compiler version changed
- Different CPU (e.g., CI runner vs dev machine)
- Release vs Debug build
- LTO/optimization flags changed

**Fix**:
- Establish new baseline for new environment
- Document environment change in commit
- Update baseline with team approval

### Issue: False positive regression (looks slow but isn't)

**Symptom**: Benchmark reports regression but code didn't change FST

**Causes**:
- Measurement artifact
- Test data changed
- Dependencies updated

**Fix**:
```bash
# Rerun both baseline and current with same environment
./FSTEfficiencyGate --benchmark_out=rerun_baseline.json ...
./FSTEfficiencyGate --benchmark_out=rerun_current.json ...

# Compare
python3 scripts/check_fst_regression.py rerun_current.json rerun_baseline.json
```

---

## Performance Optimization Workflow

**When regression detected**:

1. **Identify regression** (Efficiency Gate reports >10% slower)
   ```
   ⚠️  BM_FST_Lookup_ExactMatch: 159.8 ns → 181.5 ns (+13.6%)
   ```

2. **Profile to find hot path**
   ```bash
   # Run with profiler
   sudo perf record -g ./FSTEfficiencyGate --benchmark_filter=BM_FST_Lookup_ExactMatch
   sudo perf report
   ```

3. **Fix regression** (optimize hot path)

4. **Verify improvement**
   ```bash
   # Rerun benchmark
   ./FSTEfficiencyGate --benchmark_out=fixed.json ...

   # Compare
   python3 scripts/check_fst_regression.py fixed.json baseline.json
   ```

5. **Update baseline** (if legitimate improvement)
   ```bash
   cp fixed.json ../../benchmark_results/fst_baseline.json
   git add benchmark_results/fst_baseline.json
   git commit -m "Update FST baseline: Fixed lookup regression from PR #123"
   ```

---

## Historical Performance Trends

**Track performance over time** (optional):

```bash
# Save timestamped results
DATE=$(date +%Y%m%d)
./FSTEfficiencyGate --benchmark_out=fst_$DATE.json ...

# Archive
mkdir -p benchmark_results/history
mv fst_$DATE.json benchmark_results/history/

# Generate trend chart (requires gnuplot)
python3 scripts/plot_fst_trends.py benchmark_results/history/
```

---

## Related Documentation

- **FST Performance Guards**: `docs/LUCENE_FST_PERFORMANCE_BASELINE.md`
  - Lucene baseline comparison
  - Production performance targets

- **FST Verification**: `docs/FST_VERIFICATION_REPORT.md`
  - Correctness verification (143/144 tests passing)
  - Behavioral equivalence with Lucene

- **FST Performance Results**: `docs/FST_PERFORMANCE_RESULTS.md`
  - Diagon 22-47x faster than Lucene
  - Detailed analysis

---

## Summary

**Purpose**: Detect FST performance regressions > 10% before merge

**Thresholds**:
- ✅ < +10%: PASS (acceptable)
- ⚠️ +10-20%: WARNING (review)
- ❌ > +20%: FAIL (block merge)

**Coverage**: 13 benchmark categories, 56 measurements

**Baseline**: Established 2026-02-11, updated when verified improvements merge

**CI/CD**: Automated checking on PRs that modify FST code

**Status**: ✅ Active and enforced

---

**Efficiency Gate Status**: ✅ **ACTIVE - Protecting FST Performance**
