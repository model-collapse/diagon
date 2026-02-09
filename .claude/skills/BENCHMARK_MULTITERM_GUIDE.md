# Multi-Term Query Benchmark Guide

## Overview

The multi-term benchmark skills provide specialized testing for Boolean query performance, focusing on the most common and complex query patterns in real-world search workloads.

## Available Skills

### 1. `/benchmark_diagon_multiterm` - Pure Diagon multi-term performance

**Focus**: Absolute multi-term query performance and scalability

**Quick usage:**
```bash
/benchmark_diagon_multiterm                           # Test all
/benchmark_diagon_multiterm query_type=or             # OR queries only
/benchmark_diagon_multiterm term_counts=small         # 2-3 terms only
```

### 2. `/benchmark_lucene_multiterm` - Diagon vs Lucene comparison

**Focus**: Competitive multi-term query performance analysis

**Quick usage:**
```bash
/benchmark_lucene_multiterm                           # Compare all
/benchmark_lucene_multiterm query_type=or             # OR focus (WAND advantage)
/benchmark_lucene_multiterm target_speedup=5.0        # Higher target
```

## Why Multi-Term Benchmarks?

### Real-World Relevance

Multi-term queries are:
- **Most common**: 70%+ of real queries have 2+ terms
- **Most complex**: Require sophisticated algorithms (intersection, union)
- **Performance-critical**: 10-100x slower than single-term queries
- **Optimization target**: Where most performance gains can be achieved

### Query Complexity Distribution

```
Single-term queries:   ~30%  (simple, fast)
2-term queries:        ~35%  (common, moderate)
3-5 term queries:      ~25%  (complex, slower)
6+ term queries:       ~10%  (rare, challenging)
```

### Performance Impact

```
Query Type          | Typical Latency | Optimization Potential
--------------------|-----------------|----------------------
Single-term         | 0.1-0.5 ms      | Low (already fast)
2-term AND          | 0.5-2 ms        | Medium
2-term OR           | 2-10 ms         | High (WAND)
5-term AND          | 2-8 ms          | Medium
5-term OR           | 10-100 ms       | Very high (WAND)
10-term OR          | 50-500 ms       | Extreme (WAND critical)
```

## Query Types Tested

### Boolean AND Queries (Intersection)

**Characteristics:**
- Require intersection of posting lists
- Result set typically small (more selective)
- Performance depends on shortest list
- Early termination opportunities

**Test Queries:**

**2-term AND:**
```
"oil AND price"           # High frequency both (challenging)
"trade AND export"        # Medium frequency (typical)
"economic AND policy"     # Lower frequency (easier)
```

**3-term AND:**
```
"oil AND price AND market"              # High frequency
"trade AND export AND agreement"        # Medium frequency
"economic AND policy AND government"    # Lower frequency
```

**5-term AND:**
```
"oil AND price AND market AND crude AND barrel"
"trade AND export AND import AND deficit AND surplus"
```

**10-term AND** (stress test):
```
"oil AND price AND market AND crude AND barrel AND production AND opec AND energy AND petroleum AND industry"
```

**Expected Performance:**
- 2-term: <2ms (P99)
- 3-term: <3ms (P99)
- 5-term: <5ms (P99)
- 10-term: <10ms (P99)

### Boolean OR Queries (Union)

**Characteristics:**
- Require union of posting lists
- Result set typically large
- Performance benefits from WAND early termination
- Most challenging query type without optimization

**Test Queries:**

**2-term OR:**
```
"oil OR petroleum"        # Similar terms (overlap)
"dollar OR currency"      # Related terms (some overlap)
"trade OR export"         # Related concepts (moderate overlap)
```

**3-term OR:**
```
"oil OR petroleum OR energy"
"dollar OR currency OR exchange"
"trade OR export OR import"
```

**5-term OR:**
```
"oil OR trade OR market OR price OR dollar"
"bank OR financial OR credit OR loan OR mortgage"
"stock OR share OR equity OR trading OR investor"
```

**10-term OR** (WAND stress test):
```
"oil OR trade OR market OR price OR dollar OR economy OR bank OR stock OR government OR company"
```

**Expected Performance:**
- 2-term: <3ms (P99)
- 3-term: <5ms (P99)
- 5-term: <8ms (P99)
- 10-term: <15ms (P99)

**WAND Advantage:**
- Without WAND: OR queries scale linearly or worse with term count
- With WAND: Sub-linear scaling through early termination
- Expected speedup vs Lucene: 3.5-6x for OR queries

### Mixed Boolean Queries

**Characteristics:**
- Combine AND and OR operators
- Require query planning and optimization
- Test optimizer effectiveness
- Real-world complexity

**Test Queries:**

**AND with OR (nested):**
```
"(oil OR petroleum) AND (price OR cost)"
"(trade OR export) AND (deficit OR surplus)"
"(bank OR financial) AND (crisis OR problem)"
```

**OR with AND (nested):**
```
"(oil AND price) OR (trade AND deficit)"
"(dollar AND exchange) OR (currency AND rate)"
```

**Complex nested:**
```
"(oil OR petroleum) AND (price OR cost) AND market"
"((trade OR export) AND deficit) OR ((import OR goods) AND surplus)"
```

**Expected Performance:**
- Simple mixed: <5ms (P99)
- Complex mixed: <10ms (P99)

## Term Frequency Analysis

### High Frequency Terms (>1000 documents)

**Examples:** `market`, `price`, `trade`, `bank`, `government`

**Characteristics:**
- Large posting lists (10,000+ entries)
- AND queries: Early termination important
- OR queries: Most challenging (many candidates)
- WAND: Critical for performance

**Expected Behavior:**
- AND: Fast (early termination on high-freq terms)
- OR: Challenging without WAND

### Medium Frequency Terms (100-1000 documents)

**Examples:** `oil`, `dollar`, `export`, `stock`, `economic`

**Characteristics:**
- Moderate posting lists (1,000-10,000 entries)
- Balanced performance
- Typical real-world scenario

**Expected Behavior:**
- AND: Moderate speed
- OR: Benefits significantly from WAND

### Low Frequency Terms (<100 documents)

**Examples:** `petroleum`, `deficit`, `mortgage`, `equity`

**Characteristics:**
- Small posting lists (<1,000 entries)
- Fast intersection
- Less benefit from WAND

**Expected Behavior:**
- AND: Very fast (small intersection)
- OR: Fast (fewer candidates)

### Mixed Frequency Queries

**Purpose:** Test query optimizer's frequency-based ordering

**Example:**
```
"petroleum AND market"  # Low-freq AND high-freq
```

**Expected:** Optimizer should process low-freq term first for early termination

## Performance Targets

### Absolute Latency Targets (Diagon)

#### Boolean AND (P99 latency)
```
2-term:  <2ms   (2,000 μs)
3-term:  <3ms   (3,000 μs)
5-term:  <5ms   (5,000 μs)
10-term: <10ms  (10,000 μs)
```

#### Boolean OR (P99 latency)
```
2-term:  <3ms   (3,000 μs)
3-term:  <5ms   (5,000 μs)
5-term:  <8ms   (8,000 μs)
10-term: <15ms  (15,000 μs)
```

#### Mixed Boolean (P99 latency)
```
Simple:  <5ms   (5,000 μs)
Complex: <10ms  (10,000 μs)
```

### Speedup Targets (Diagon vs Lucene)

#### Boolean AND Queries
```
2-term:  ≥3.0x faster
3-term:  ≥3.0x faster
5-term:  ≥3.5x faster
10-term: ≥4.0x faster
```

#### Boolean OR Queries (WAND advantage)
```
2-term:  ≥3.5x faster
3-term:  ≥4.0x faster
5-term:  ≥5.0x faster
10-term: ≥6.0x faster
```

#### Mixed Boolean Queries
```
Simple:  ≥3.0x faster
Complex: ≥4.0x faster
```

### Scalability Targets

**Sub-linear scaling:**
- Latency should grow slower than term count
- OR queries with WAND: Most sub-linear
- AND queries: Near-linear acceptable

**Example:**
```
Term Count | AND Latency | OR Latency (no WAND) | OR Latency (WAND)
-----------|-------------|---------------------|------------------
2          | 2ms         | 5ms                 | 3ms
5          | 5ms         | 25ms                | 8ms  (3.1x better)
10         | 10ms        | 100ms               | 15ms (6.7x better)

Growth rate: Linear | Super-linear | Sub-linear (ideal)
```

## Usage Examples

### Daily Multi-Term Testing

After making query optimization changes:

```bash
# Test all multi-term queries
/benchmark_diagon_multiterm

# Review results
cat benchmark_results/multiterm_*.md | grep -A5 "Performance"
```

### Focus on OR Queries (WAND Testing)

```bash
# Test only OR queries (where WAND matters most)
/benchmark_diagon_multiterm query_type=or

# Check WAND effectiveness
cat benchmark_results/multiterm_*.md | grep -A10 "WAND"
```

### Stress Testing with High Term Counts

```bash
# Test only complex queries (6-10 terms)
/benchmark_diagon_multiterm term_counts=large iterations=500
```

### Competitive Analysis

```bash
# Compare with Lucene for all multi-term queries
/benchmark_lucene_multiterm

# Focus on OR queries (maximum advantage)
/benchmark_lucene_multiterm query_type=or

# Raise target for critical queries
/benchmark_lucene_multiterm query_type=or target_speedup=5.0
```

### Pre-Release Validation

```bash
# Comprehensive multi-term testing
/benchmark_diagon_multiterm benchmark=all iterations=1000
/benchmark_lucene_multiterm target_speedup=3.0

# Check all targets met
grep "Status: ✅" benchmark_results/multiterm_*.md
```

## Report Sections

Both multi-term skills generate reports following `.claude/BENCHMARK_REPORT_TEMPLATE_V2.md` with enhanced sections:

### Standard Sections (10 required)

1. Executive Summary
2. Test Environment
3. Indexing Performance (if applicable)
4. Query Performance
5. Performance Analysis
6. Detailed Comparison
7. Issues and Concerns
8. Recommendations
9. Raw Data
10. Reproducibility

### Enhanced Multi-Term Sections

#### Boolean AND Performance Table
```markdown
| Term Count | Sample Query | Hits | P50 (μs) | P95 (μs) | P99 (μs) | Target | Status |
|------------|--------------|------|----------|----------|----------|--------|--------|
| 2 | oil AND price | 450 | 1200 | 1800 | 2000 | <2ms | ✅ PASS |
| 3 | oil AND price AND market | 120 | 2000 | 2800 | 3000 | <3ms | ✅ PASS |
| 5 | [5-term query] | 45 | 3500 | 4800 | 5000 | <5ms | ✅ PASS |
| 10 | [10-term query] | 12 | 7000 | 9500 | 10000 | <10ms | ✅ PASS |
```

#### Boolean OR Performance Table
```markdown
| Term Count | Sample Query | Hits | P50 (μs) | P95 (μs) | P99 (μs) | Target | Status |
|------------|--------------|------|----------|----------|----------|--------|--------|
| 2 | oil OR petroleum | 2500 | 2000 | 2800 | 3000 | <3ms | ✅ PASS |
| 3 | oil OR petroleum OR energy | 4200 | 3500 | 4800 | 5000 | <5ms | ✅ PASS |
| 5 | [5-term OR] | 8500 | 6000 | 7800 | 8000 | <8ms | ✅ PASS |
| 10 | [10-term OR] | 12000 | 12000 | 14500 | 15000 | <15ms | ✅ PASS |
```

#### Scalability Analysis

**Latency vs Term Count:**
- Graph or table showing growth rate
- Linear, sub-linear, or super-linear classification
- Comparison of AND vs OR scaling

**WAND Effectiveness:**
- Documents scanned vs total posting list size
- Early termination rate
- Performance benefit calculation

#### Speedup Analysis (comparison benchmark)

**Speedup vs Term Count:**
```markdown
| Term Count | Query Type | Lucene P99 | Diagon P99 | Speedup | Target | Status |
|------------|------------|------------|------------|---------|--------|--------|
| 2 | AND | 6000 μs | 2000 μs | 3.0x | 3.0x | ✅ PASS |
| 5 | AND | 18000 μs | 5000 μs | 3.6x | 3.5x | ✅ PASS |
| 2 | OR | 12000 μs | 3000 μs | 4.0x | 3.5x | ✅ PASS |
| 5 | OR | 60000 μs | 8000 μs | 7.5x | 5.0x | ✅ PASS |
```

**Overall Statistics:**
- Average speedup across all queries
- Minimum speedup (worst case)
- Maximum speedup (best case)
- Percentage of queries meeting target

## Interpreting Results

### Good Signs ✅

**Absolute Performance:**
- All latencies meet targets
- Sub-linear scaling for OR queries
- Consistent performance across iterations
- Low variance (stable)

**Competitive Performance:**
- Speedup meets or exceeds targets
- Speedup increases with term count
- OR queries show significant advantage (WAND)
- No regressions vs baseline

### Warning Signs ⚠️

**Absolute Performance:**
- Some latencies near targets (within 10%)
- Near-linear scaling for OR queries (WAND not optimal)
- Higher variance across iterations
- Performance degradation with term count

**Competitive Performance:**
- Speedup slightly below target (80-100% of target)
- Speedup inconsistent across query types
- Less advantage for OR queries than expected

### Critical Issues ❌

**Absolute Performance:**
- Latencies significantly exceed targets (>20%)
- Linear or super-linear scaling for OR queries
- Very high variance (unstable)
- Performance crashes or errors

**Competitive Performance:**
- Speedup well below target (<80%)
- Slower than Lucene for any query type
- No WAND advantage for OR queries
- Regressions vs previous benchmarks

## Troubleshooting

### OR Queries Too Slow

**Symptom:** OR query latencies exceed targets, especially for high term counts

**Possible Causes:**
- WAND not implemented or not activated
- Block-max metadata not available
- Early termination not working
- Heap operations inefficient

**Investigation:**
```bash
# Check WAND is enabled
grep "WAND" benchmark_results/multiterm_*.md

# Profile OR query execution
perf record ./benchmark --filter=OR
perf report
```

### AND Queries Not Scaling

**Symptom:** AND query latency grows linearly or super-linearly with term count

**Possible Causes:**
- Not processing shortest list first
- No frequency-based optimization
- Excessive allocations
- Poor cache locality

**Investigation:**
```bash
# Check query plan
./benchmark --explain "oil AND price AND market"

# Profile AND query
perf stat -e cache-misses,cache-references ./benchmark --filter=AND
```

### Speedup Below Target

**Symptom:** Diagon faster than Lucene, but not meeting target speedup

**Possible Causes:**
- Lucene baseline incorrect or outdated
- Measurement methodology different
- Diagon not fully optimized
- Target too aggressive

**Investigation:**
- Verify Lucene baseline data
- Run side-by-side comparison
- Profile both systems
- Re-evaluate target based on data

### High Variance

**Symptom:** Large differences between P50, P95, P99 latencies

**Possible Causes:**
- Cold vs warm cache effects
- Background system activity
- GC or memory allocation spikes
- Competing processes

**Investigation:**
```bash
# Check system load
mpstat 1 10

# Check for memory pressure
vmstat 1 10

# Re-run with more iterations
/benchmark_diagon_multiterm iterations=1000
```

## Best Practices

### When to Run Multi-Term Benchmarks

**After query optimization work:**
```bash
# Make query changes
vim src/core/search/BooleanQuery.cpp

# Build
/build_diagon target=benchmarks

# Test multi-term queries
/benchmark_diagon_multiterm
```

**Before releases:**
```bash
# Comprehensive testing
/benchmark_diagon_multiterm iterations=500
/benchmark_lucene_multiterm target_speedup=3.0

# Review all results
cat benchmark_results/multiterm_*.md
```

**For WAND validation:**
```bash
# Focus on OR queries
/benchmark_diagon_multiterm query_type=or
/benchmark_lucene_multiterm query_type=or

# Check WAND effectiveness
grep -A10 "WAND" benchmark_results/multiterm_*.md
```

### Choosing Parameters

**Query Type:**
- `all` - Default, comprehensive testing
- `and` - After intersection algorithm changes
- `or` - After WAND or union changes
- `mixed` - After query planner changes

**Term Counts:**
- `all` - Default, full coverage
- `small` - Quick smoke test (2-3 terms)
- `medium` - Typical workload (4-5 terms)
- `large` - Stress test (6-10 terms)

**Iterations:**
- `100` - Default, good balance
- `500` - More stable results
- `1000` - High precision for critical measurements
- `10` - Quick sanity check (not for reporting)

## Related Documentation

- **Report template:** `.claude/BENCHMARK_REPORT_TEMPLATE_V2.md`
- **General benchmarks:** `.claude/skills/BENCHMARK_DIAGON_GUIDE.md`
- **Lucene comparison:** `.claude/skills/BENCHMARK_REUTERS_GUIDE.md`
- **Skills overview:** `.claude/skills/SKILLS_OVERVIEW.md`
- **Build SOP:** `BUILD_SOP.md`

---

**Last Updated:** 2026-02-09
**Skills:** `benchmark_diagon_multiterm`, `benchmark_lucene_multiterm`
**Dataset:** Reuters-21578 (21,578 documents)
**Focus:** Multi-term Boolean query performance and optimization
