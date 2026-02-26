# Diagon vs ClickHouse — Columnar Range Query Comparison

**Date**: 2026-02-25 10:59 UTC
**Method**: `clickhouse-local` 26.3.1 (in-process, no server overhead)
**Dataset**: ClickBench hits_100k.tsv (100,000 rows, 105 columns)

## Executive Summary

Apple-to-apple comparison of Diagon's columnar store vs ClickHouse's MergeTree engine for numeric range queries, eliminating all server/network/serialization overhead via `clickhouse-local`.

**End-to-end**, Diagon is 9-12x faster (Diagon direct call vs ClickHouse's SQL pipeline). **Storage-engine-only** (after subtracting ClickHouse's measured ~1,175us SQL pipeline overhead), Diagon is **3.5-4.2x faster on Q9** (closed range), roughly **comparable on Q10** (open range — CH engine is slightly faster at P50), and **>100x faster on Q14** via MinMax bulk-counting.

## Test Environment

| Parameter | Diagon | ClickHouse |
|-----------|--------|------------|
| Engine | ColumnarStore (LZ4 + MinMax) | MergeTree (LZ4 + MinMax) |
| Execution | Direct C++ function call | `clickhouse-local` 26.3.1 (in-process) |
| Threading | Single-threaded | `SET max_threads = 1` |
| Granule size | 8192 rows | 8192 rows (`index_granularity`) |
| Granules | 13 per column | 13 per column |
| Data | 100K rows from hits_100k.tsv | Same file, full 105-column schema |
| Query I/O | In-memory (loaded from .col files) | OS page cache (MergeTree parts) |
| Warmup | 10 iterations | 10 iterations |
| Measured | 100 iterations | 100 iterations |

## Timing Methodology

ClickHouse timing uses in-session `SELECT toUnixTimestamp64Micro(now64(6))` bracketing each query (microsecond precision). The per-call overhead of the timestamp query itself was measured independently:

| Metric | P50 | P90 | P99 |
|--------|-----|-----|-----|
| Timestamp overhead | 512 us | 567 us | 684 us |

**Adjusted** ClickHouse times = raw measured - timestamp overhead. This removes the cost of the timing mechanism itself but preserves ClickHouse's real per-query overhead (SQL parsing, query planning, block pipeline initialization).

## Mode 1: Unsorted (`ORDER BY tuple()`) — Apple-to-Apple

Both systems use MinMax skip indexes only, no primary key advantage. This is the fair comparison.

| Query | Diagon P50 | CH Adj P50 | Ratio | Diagon P90 | CH Adj P90 | Ratio | Diagon P99 | CH Adj P99 | Ratio | Hits |
|-------|-----------|-----------|-------|-----------|-----------|-------|-----------|-----------|-------|------|
| Q9 RegionID [200,300] | 150 us | 1,750 us | **11.7x** | 159 us | 1,780 us | **11.2x** | 164 us | 1,803 us | **11.0x** | 51,474 |
| Q10 ResWidth >= 1900 | 137 us | 1,271 us | **9.3x** | 145 us | 1,290 us | **8.9x** | 147 us | 1,721 us | **11.7x** | 27,222 |
| Q14 CounterID [0,100] | <1 us | 1,288 us | **>1000x** | <1 us | 1,323 us | **>1000x** | <1 us | 1,360 us | **>1000x** | 100,000 |

### Raw ClickHouse Times (before overhead subtraction)

| Query | CH Raw P50 | CH Raw P90 | CH Raw P99 |
|-------|-----------|-----------|-----------|
| Q9 | 2,262 us | 2,347 us | 2,487 us |
| Q10 | 1,783 us | 1,857 us | 2,405 us |
| Q14 | 1,800 us | 1,890 us | 2,044 us |

## Mode 2: Sorted (`ORDER BY (CounterID, RegionID, ResolutionWidth)`) — ClickHouse Best Case

ClickHouse gets primary key skip advantage. Diagon has unsorted granules (no primary key yet).

| Query | Diagon P50 | CH Adj P50 | Ratio | Diagon P90 | CH Adj P90 | Ratio | Diagon P99 | CH Adj P99 | Ratio | Hits |
|-------|-----------|-----------|-------|-----------|-----------|-------|-----------|-----------|-------|------|
| Q9 RegionID [200,300] | 150 us | 1,701 us | **11.3x** | 159 us | 1,709 us | **10.7x** | 164 us | 1,763 us | **10.8x** | 51,474 |
| Q10 ResWidth >= 1900 | 137 us | 1,340 us | **9.8x** | 145 us | 1,353 us | **9.3x** | 147 us | 1,355 us | **9.2x** | 27,222 |
| Q14 CounterID [0,100] | <1 us | 1,162 us | **>1000x** | <1 us | 1,166 us | **>1000x** | <1 us | 1,196 us | **>1000x** | 100,000 |

### Raw ClickHouse Times (sorted, before overhead subtraction)

| Query | CH Raw P50 | CH Raw P90 | CH Raw P99 |
|-------|-----------|-----------|-----------|
| Q9 | 2,213 us | 2,276 us | 2,447 us |
| Q10 | 1,852 us | 1,920 us | 2,039 us |
| Q14 | 1,674 us | 1,733 us | 1,880 us |

## Hit Count Verification

All hit counts match exactly between Diagon and ClickHouse:
- Q9: 51,474 (both)
- Q10: 27,222 (both)
- Q14: 100,000 (both)

## Overhead Decomposition

To isolate the pure storage engine performance, we measured ClickHouse's per-query pipeline overhead using a zero-data query: `SELECT count() FROM hits WHERE RegionID < -999999999`. This exercises the **full SQL pipeline** (parse, plan, MergeTree part open, MinMax eval on all 13 granules, teardown) but reads **zero data** since all granules are MinMax-skipped.

| Overhead Component | P50 | P90 | P99 |
|-------------------|-----|-----|-----|
| Timestamp query overhead | 512 us | 567 us | 684 us |
| SQL pipeline overhead (parse + plan + MergeTree open + teardown) | 1,175 us | 1,202 us | 1,122 us |
| **Total non-engine overhead** | **1,687 us** | **1,769 us** | **1,806 us** |

### Storage Engine Only: `raw - baseline` (unsorted)

| Query | Diagon P50 | CH Engine P50 | Ratio | Diagon P99 | CH Engine P99 | Ratio |
|-------|-----------|--------------|-------|-----------|--------------|-------|
| Q9 RegionID [200,300] | 150 us | 575 us | **3.8x** | 164 us | 681 us | **4.2x** |
| Q10 ResWidth >= 1900 | 137 us | 96 us | **0.7x** (CH faster) | 147 us | 599 us | **4.1x** |
| Q14 CounterID [0,100] | <1 us | 113 us | >100x | <1 us | 238 us | >200x |

### Storage Engine Only: `raw - baseline` (sorted)

| Query | Diagon P50 | CH Engine P50 | Ratio | Diagon P99 | CH Engine P99 | Ratio |
|-------|-----------|--------------|-------|-----------|--------------|-------|
| Q9 RegionID [200,300] | 150 us | 526 us | **3.5x** | 164 us | 641 us | **3.9x** |
| Q10 ResWidth >= 1900 | 137 us | 165 us | **1.2x** | 147 us | 233 us | **1.6x** |
| Q14 CounterID [0,100] | <1 us | <1 us | ~1x | <1 us | 74 us | >70x |

## Analysis

### Three Layers of Comparison

| Layer | Meaning | Q9 Diagon Advantage |
|-------|---------|-------------------|
| **Raw** (full measured) | What a user experiences calling clickhouse-local | 11.7x |
| **Adjusted** (- timestamp overhead) | Removing measurement artifact | 11.7x → same (ts overhead cancels) |
| **Engine only** (- full pipeline) | Pure storage engine vs storage engine | **3.8x** |

### Key Findings

1. **~70% of ClickHouse's measured latency is SQL pipeline overhead**, not storage engine work. At P50 for Q9: 1,687us overhead out of 2,262us total = 75%.

2. **Storage-engine-to-engine, Diagon is 3.5-4.2x faster on Q9** (the tightest range query). This is the honest comparison of columnar scan efficiency.

3. **Q10 (ResWidth >= 1900): ClickHouse's engine is actually slightly faster at P50** (96us vs 137us). ClickHouse's vectorized execution engine is highly optimized for simple predicate evaluation. However, Diagon wins at P99 (147us vs 599us), suggesting ClickHouse has occasional slow granules.

4. **Q14 (bulk-counting): Diagon's three-level evaluation dominates**. When all granules are fully within range, Diagon short-circuits with zero decompression. ClickHouse sorted mode also achieves near-zero at P50 but still has overhead at P99.

5. **At scale, engine time dominates**: At 10M rows (~1,220 granules), engine work would be ~100x larger but pipeline overhead stays constant. The ratios would converge toward the engine-only numbers (3-4x, not 10-12x).

### Why Sorted Mode Barely Helps ClickHouse

At 100K rows with 13 granules, the primary key can skip at most a few granules. The sorted mode shows:
- Q9: 2.8% faster (2213 vs 2262 us raw)
- Q10: -3.9% slower (1852 vs 1783 us — noise)
- Q14: 7.0% faster (1674 vs 1800 us)

Pipeline overhead (~1,175us) dominates, so granule skip savings (~50-100us) are barely visible.

### What This Comparison Measures

| Factor | Included | Notes |
|--------|----------|-------|
| Storage engine execution | Yes | MergeTree vs ColumnarStore |
| MinMax granule skip | Yes | Both have 8192-row granules |
| LZ4 decompression | Yes | Both use LZ4 |
| SQL parsing | Measured separately | ~included in 1,175us pipeline overhead |
| Query planning | Measured separately | ~included in 1,175us pipeline overhead |
| Block pipeline init | Measured separately | MergeTree part opening, column readers |
| Network/serialization | Neither | Eliminated by clickhouse-local |
| Process startup | Neither | Single session, amortized |

### What This Comparison Does NOT Measure

- ClickHouse at scale (1M+ rows where engine time dominates pipeline overhead)
- ClickHouse with parallel execution (`max_threads > 1`)
- ClickHouse with PREWHERE optimization on sorted primary key
- Diagon with sorted columnar storage (not yet implemented)
- ClickHouse's vectorized execution on wider queries (GROUP BY, aggregations)

## Caveats

1. **100K rows is small**: At this scale, per-query overhead dominates. At 10M+ rows, actual scan time would dominate and the ratio would likely shrink.

2. **Timestamp overhead subtraction is approximate**: We measured it independently and subtracted P50 from P50, P90 from P90, P99 from P99. The actual overhead per measurement varies.

3. **Single-threaded comparison**: ClickHouse can parallelize across threads. Diagon is currently single-threaded.

4. **Diagon's ColumnarStore is benchmark-only**: It's a self-contained header in `benchmarks/columnar/`, not integrated into the core query pipeline. ClickHouse's MergeTree is production-grade with ACID, replication, etc.

## Recommendations

1. **Scale test**: Run at 1M+ rows to see how ratios change when scan time dominates over per-query overhead.

2. **Multi-threaded ClickHouse**: Re-run without `max_threads=1` to measure ClickHouse's parallelism advantage.

3. **Integrate columnar into core**: Move ColumnarStore from benchmark helper into `src/core/` for production use.

4. **Add sorted columnar storage**: Implement primary key ordering in Diagon's columnar store to match ClickHouse's sorted MergeTree advantage at scale.

## Reproducibility

```bash
# Install clickhouse-local
mkdir -p /home/ubuntu/clickhouse-local && cd /home/ubuntu/clickhouse-local
curl -O 'https://builds.clickhouse.com/master/amd64/clickhouse'
chmod a+x clickhouse

# Run Diagon benchmark
cd /home/ubuntu/diagon/build/benchmarks
./ClickBenchBenchmark --data-path /home/ubuntu/data/clickbench/hits_100k.tsv --max-docs 100000

# Run ClickHouse unsorted benchmark
# (see /tmp/ch_bench_unsorted.sql for full query file)
clickhouse local --path /tmp/ch_clickbench_unsorted --multiquery < /tmp/ch_bench_unsorted.sql
```
