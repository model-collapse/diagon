# Diagon Benchmark Report Template (Elaborated)

**Version**: 2.0 (Elaborated)
**Purpose**: Comprehensive, honest, and actionable benchmark reporting
**Audience**: Developers, reviewers, project stakeholders

---

# Benchmark Report: [Benchmark Name]

> **Template Instructions**: Replace all `[placeholders]` with actual data. Remove this instruction block before finalizing.

**Report ID**: `[BENCHMARK_NAME]_[YYYYMMDD_HHMMSS]`
**Generated**: [YYYY-MM-DD HH:MM:SS] (ISO 8601 format)
**Diagon Version**: [git commit hash (8 chars) or semantic version]
**Benchmark Skill**: [skill name] v[version]
**Benchmark Duration**: [Total time to complete benchmark]

---

## Executive Summary

> **Purpose**: Provide decision-makers with a 1-page overview of results, findings, and critical issues.
> **Guidelines**: Be direct, factual, no marketing language. If results are bad, say so clearly.

**Overall Result**: [Choose one: ✅ PASS | ⚠️ PARTIAL | ❌ FAIL]

**Result Criteria**:
- ✅ **PASS**: All targets met, no critical issues, ready for production consideration
- ⚠️ **PARTIAL**: Most targets met, minor issues present, acceptable for iteration
- ❌ **FAIL**: Critical targets missed, must fix before proceeding

### Key Findings

> List 3-5 most important findings only. Focus on impact, not details.

1. **[Finding #1]**: [Brief description with key metric]
   - Impact: [High/Medium/Low]
   - Status: [✅/⚠️/❌]

2. **[Finding #2]**: [Brief description with key metric]
   - Impact: [High/Medium/Low]
   - Status: [✅/⚠️/❌]

3. **[Finding #3]**: [Brief description with key metric]
   - Impact: [High/Medium/Low]
   - Status: [✅/⚠️/❌]

### Performance vs Target Summary

> Direct comparison with project goals from CLAUDE.md

| Goal | Target | Achieved | Status | Gap |
|------|--------|----------|--------|-----|
| Search speed vs Lucene | 3-10x faster | [X.X]x | [✅/⚠️/❌] | [+/-X.X]x |
| Indexing throughput | ≥5,000 docs/sec | [value] | [✅/⚠️/❌] | [+/-XXX] docs/sec |
| Index size vs Lucene | Competitive (±20%) | [±XX%] | [✅/⚠️/❌] | [value] |
| Query correctness | 100% | [XX.X%] | [✅/⚠️/❌] | [-X.X%] |
| Memory efficiency | [target] | [value] | [✅/⚠️/❌] | [delta] |

### Critical Issues

> **CRITICAL**: List ALL critical issues here. Do NOT hide or minimize problems.
> If no critical issues, write "None" - do not skip this section.

[Choose one:]
- **None** - No critical issues identified
- **Critical Issues Present**:
  1. ❌ [Issue #1]: [Description, impact, severity]
  2. ❌ [Issue #2]: [Description, impact, severity]

### Immediate Attention Required

> Issues that must be addressed before next release/iteration

[List of immediate action items, or "None"]

---

## Test Environment

> **Purpose**: Enable exact reproduction of results on different machines
> **Requirement**: Include ALL details needed for reproducibility

### Hardware Configuration

| Component | Specification | Notes |
|-----------|---------------|-------|
| **CPU** | [Model, family, stepping] | [cores, threads, frequency] |
| **Architecture** | [x86_64, ARM64, etc.] | [Extensions: AVX2, BMI2, etc.] |
| **RAM** | [Capacity, type, speed] | [Available during test] |
| **Storage** | [Type, model, capacity] | [SSD/HDD, NVMe/SATA, read/write speed] |
| **Network** | [If applicable] | [Bandwidth, latency] |

**Hardware Verification**:
```bash
# Commands used to verify hardware
lscpu | grep -E "Model name|CPU\(s\)|MHz"
free -h
df -h
cat /proc/cpuinfo | grep flags | head -1
```

### Software Environment

| Component | Version | Configuration |
|-----------|---------|---------------|
| **Operating System** | [Distribution, version, kernel] | [e.g., Ubuntu 22.04.3 LTS, Kernel 6.14.0] |
| **Compiler** | [GCC/Clang version] | [Full version string] |
| **Build Type** | [Release/Debug/RelWithDebInfo] | [MUST be Release for benchmarks] |
| **Optimization Flags** | [e.g., -O3 -march=native] | [List all flags] |
| **LTO** | [ON/OFF] | [MUST be OFF per BUILD_SOP.md] |
| **Diagon Version** | [git commit hash] | [Branch, commit date] |
| **Build Timestamp** | [YYYY-MM-DD HH:MM:SS] | [When binary was built] |

**Compiler Verification**:
```bash
# Exact compiler used
g++ --version  # or clang++ --version
```

**Build Configuration**:
```bash
# CMake configuration used
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS="-O3 -march=native" \
      -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
      -DDIAGON_BUILD_BENCHMARKS=ON \
      ..
```

### Dependencies

| Library | Version | Source | Purpose |
|---------|---------|--------|---------|
| **ICU** | [version] | [system/conda] | Unicode support |
| **ZSTD** | [version] | [system/conda] | Compression |
| **LZ4** | [version] | [system/conda] | Fast compression |
| **Google Benchmark** | [version] | [system] | Benchmark framework |

**Dependency Verification**:
```bash
# Verify ICU linking
ldd build/benchmarks/[benchmark] | grep icu

# Library versions
pkg-config --modversion icu-uc
pkg-config --modversion libzstd
pkg-config --modversion liblz4
```

### Dataset Information

| Property | Value | Verification |
|----------|-------|--------------|
| **Name** | [e.g., Reuters-21578] | [Official name] |
| **Version/Date** | [version or collection date] | [If applicable] |
| **Document Count** | [count] | [Exact number] |
| **Total Size** | [MB/GB] | [Uncompressed size] |
| **Location** | [Absolute path] | [Full path to dataset] |
| **Verification** | [✅ Yes / ❌ No] | [How verified] |
| **Checksum** | [If available] | [MD5/SHA256] |

**Dataset Verification Commands**:
```bash
# Verify dataset completeness
find [dataset_path] -name "*.txt" | wc -l
# Expected: [count]

# Verify dataset integrity
du -sh [dataset_path]
# Expected: [size]
```

### Test Conditions

> Document any special conditions or constraints

| Condition | Status | Notes |
|-----------|--------|-------|
| **System Load** | [idle/loaded] | [Other processes running?] |
| **Thermal State** | [cool/warm/hot] | [CPU temperature] |
| **Power Mode** | [performance/balanced] | [CPU governor] |
| **Disk Cache** | [cold/warm] | [Was cache dropped?] |
| **Network** | [connected/offline] | [Impact on test?] |

---

## Indexing Performance

> **Purpose**: Measure how efficiently Diagon can build an index
> **Baseline**: Compare with Apache Lucene on same dataset

### Indexing Metrics

| Metric | Value | Target | Status | vs Target |
|--------|-------|--------|--------|-----------|
| **Documents Indexed** | [count] | [expected] | [✅/❌] | [delta] |
| **Time (seconds)** | [value] | [range] | [✅/⚠️/❌] | [+/-X.X]s |
| **Throughput (docs/sec)** | [value] | ≥[target] | [✅/⚠️/❌] | [+/-XXX] |
| **Index Size (MB)** | [value] | [range] | [✅/⚠️/❌] | [delta] |
| **Storage (bytes/doc)** | [value] | [range] | [✅/⚠️/❌] | [delta] |
| **Peak Memory (MB)** | [value] | [if tracked] | [✅/⚠️/❌] | [delta] |
| **CPU Utilization (%)** | [value] | [if tracked] | [✅/⚠️/❌] | [delta] |

**Target Rationale**:
- Throughput target based on: [e.g., Lucene baseline, previous runs]
- Index size target based on: [e.g., Lucene index size ±20%]
- Storage efficiency: [e.g., comparable to Lucene's compression]

### Head-to-Head Comparison with Lucene

| Metric | Diagon | Lucene | Ratio | Status | Assessment |
|--------|--------|--------|-------|--------|------------|
| **Throughput (docs/sec)** | [value] | [value] | [X.XX]x | [✅/⚠️/❌] | [faster/slower/equal] |
| **Index Size (MB)** | [value] | [value] | [X.XX]x | [✅/⚠️/❌] | [larger/smaller/equal] |
| **Time (seconds)** | [value] | [value] | [X.XX]x | [✅/⚠️/❌] | [faster/slower/equal] |
| **Storage (bytes/doc)** | [value] | [value] | [X.XX]x | [✅/⚠️/❌] | [more/less/equal efficient] |
| **Memory Usage (MB)** | [value] | [value] | [X.XX]x | [✅/⚠️/❌] | [more/less/equal] |

**Status Indicators**:
- ✅ **Competitive**: Within 10% of Lucene or better
- ⚠️ **Acceptable**: 10-30% difference from Lucene
- ❌ **Concerning**: >30% worse than Lucene

### Indexing Performance Analysis

**Strengths**:
- [What went well in indexing?]
- [Which metrics exceeded expectations?]
- [Specific technical achievements]

**Weaknesses**:
- [What could be improved?]
- [Which metrics fell short?]
- [Specific bottlenecks identified]

**Bottlenecks Identified**:
1. [Bottleneck #1]: [Description, impact, evidence]
2. [Bottleneck #2]: [Description, impact, evidence]

**Optimization Opportunities**:
1. [Opportunity #1]: [Description, potential impact]
2. [Opportunity #2]: [Description, potential impact]

---

## Query Performance

> **Purpose**: Measure search speed and compare with Lucene
> **Critical**: This is THE key metric for Diagon (target: 3-10x faster)

### Performance Measurement Methodology

**Latency Metrics Explained**:
- **P50 (Median)**: 50% of queries complete in this time or less
- **P95**: 95% of queries complete in this time or less (near-maximum)
- **P99**: 99% of queries complete in this time or less (tail latency)

**Why P99 Matters**: Tail latency (P99) represents worst-case user experience. Even if average is fast, high P99 means some users experience slow queries.

**Measurement Method**:
- Warm-up runs: [count] queries before measurement
- Measured iterations: [count] queries per test
- Statistical method: [e.g., percentile calculation, outlier removal]
- Timing precision: [microsecond/nanosecond]

### Single-Term Query Performance

> Baseline test: How fast can Diagon find documents containing a single term?

| Query | Field | Hits | P50 (μs) | P95 (μs) | P99 (μs) | Lucene P99 | Speedup | Status |
|-------|-------|------|----------|----------|----------|------------|---------|--------|
| "[term1]" | [field] | [n] | [value] | [value] | [value] | [value] | [X.X]x | [✅/⚠️/❌] |
| "[term2]" | [field] | [n] | [value] | [value] | [value] | [value] | [X.X]x | [✅/⚠️/❌] |
| "[term3]" | [field] | [n] | [value] | [value] | [value] | [value] | [X.X]x | [✅/⚠️/❌] |

**Summary Statistics**:
- Average P99 latency: [value] μs
- Average speedup vs Lucene: [X.X]x
- Min speedup: [X.X]x (query: [term])
- Max speedup: [X.X]x (query: [term])
- **Target**: 3-5x faster than Lucene
- **Status**: [✅ Target met | ⚠️ Below target | ❌ Slower than Lucene]

**Analysis**:
- [Why are some queries faster/slower?]
- [Term frequency impact on performance]
- [Optimization opportunities]

### Boolean AND Query Performance

> Test: Queries requiring ALL terms to match (intersection)

| Query | Fields | Hits | P50 (μs) | P95 (μs) | P99 (μs) | Lucene P99 | Speedup | Status |
|-------|--------|------|----------|----------|----------|------------|---------|--------|
| "[term1] AND [term2]" | [fields] | [n] | [value] | [value] | [value] | [value] | [X.X]x | [✅/⚠️/❌] |
| "[term1] AND [term2] AND [term3]" | [fields] | [n] | [value] | [value] | [value] | [value] | [X.X]x | [✅/⚠️/❌] |

**Summary Statistics**:
- Average P99 latency: [value] μs
- Average speedup vs Lucene: [X.X]x
- **Target**: 3-5x faster than Lucene
- **Status**: [✅ Target met | ⚠️ Below target | ❌ Slower than Lucene]

**Analysis**:
- [How does AND query optimization perform?]
- [Impact of term selectivity]
- [WAND effectiveness for AND queries]

### Boolean OR Query Performance

> Test: Queries matching ANY term (union) - most challenging for early termination

| Query | Fields | Hits | P50 (μs) | P95 (μs) | P99 (μs) | Lucene P99 | Speedup | Status |
|-------|--------|------|----------|----------|----------|------------|---------|--------|
| "[term1] OR [term2]" (2-term) | [fields] | [n] | [value] | [value] | [value] | [value] | [X.X]x | [✅/⚠️/❌] |
| "[5-term OR query]" (5-term) | [fields] | [n] | [value] | [value] | [value] | [value] | [X.X]x | [✅/⚠️/❌] |
| "[10-term OR query]" (10-term) | [fields] | [n] | [value] | [value] | [value] | [value] | [X.X]x | [✅/⚠️/❌] |

**Summary Statistics**:
- Average P99 latency: [value] μs
- Average speedup vs Lucene: [X.X]x
- **Target**: 3-5x faster than Lucene (challenging due to union)
- **Status**: [✅ Target met | ⚠️ Below target | ❌ Slower than Lucene]

**Analysis**:
- [WAND early termination effectiveness]
- [Block-max optimization impact]
- [Scalability with term count]

### Complex Query Performance

> If tested: More complex query patterns (phrase, proximity, wildcard, etc.)

[Add tables similar to above for any complex queries tested]

### Query Performance Summary

**Overall Performance vs Lucene**:

| Query Type | Count | Avg Speedup | Min Speedup | Max Speedup | Target | Status |
|------------|-------|-------------|-------------|-------------|--------|--------|
| Single-term | [n] | [X.X]x | [X.X]x | [X.X]x | 3-5x | [✅/⚠️/❌] |
| Boolean AND | [n] | [X.X]x | [X.X]x | [X.X]x | 3-5x | [✅/⚠️/❌] |
| Boolean OR | [n] | [X.X]x | [X.X]x | [X.X]x | 3-5x | [✅/⚠️/❌] |
| Complex | [n] | [X.X]x | [X.X]x | [X.X]x | 3-5x | [✅/⚠️/❌] |
| **Overall** | [total] | **[X.X]x** | **[X.X]x** | **[X.X]x** | **3-10x** | **[✅/⚠️/❌]** |

**Target Achievement**:
- ✅ **Target Exceeded**: >5x faster than Lucene (excellent)
- ✅ **Target Met**: 3-5x faster than Lucene (good)
- ⚠️ **Below Target**: 1.5-3x faster than Lucene (acceptable but improve)
- ❌ **Target Missed**: <1.5x faster or slower than Lucene (critical issue)

---

## Performance Analysis

> **Purpose**: Honest assessment of results - strengths AND weaknesses
> **Critical**: Do NOT hide problems. Be direct about what needs improvement.

### Strengths ✅

> What went well? Where did we excel?

1. **[Strength #1]**:
   - **Evidence**: [Specific metrics, data points]
   - **Significance**: [Why this matters]
   - **Contributing Factors**: [What made this work well]

2. **[Strength #2]**:
   - **Evidence**: [Specific metrics, data points]
   - **Significance**: [Why this matters]
   - **Contributing Factors**: [What made this work well]

3. **[Strength #3]**:
   - **Evidence**: [Specific metrics, data points]
   - **Significance**: [Why this matters]
   - **Contributing Factors**: [What made this work well]

### Areas for Improvement ⚠️

> What fell short? Where can we do better?

1. **[Improvement Area #1]**:
   - **Current State**: [Actual performance]
   - **Target State**: [Desired performance]
   - **Gap**: [Quantified difference]
   - **Impact**: [Why this matters]
   - **Root Cause Hypothesis**: [Why we think this is happening]
   - **Proposed Actions**: [How to improve]

2. **[Improvement Area #2]**:
   - **Current State**: [Actual performance]
   - **Target State**: [Desired performance]
   - **Gap**: [Quantified difference]
   - **Impact**: [Why this matters]
   - **Root Cause Hypothesis**: [Why we think this is happening]
   - **Proposed Actions**: [How to improve]

### Critical Issues ❌

> MUST-FIX problems that block progress or violate requirements

[Choose one:]

**None** - No critical issues identified

OR

**Critical Issues Present**:

1. **❌ [Issue #1]**:
   - **Description**: [What's wrong]
   - **Evidence**: [Data showing the problem]
   - **Severity**: Critical (blocks progress/violates requirements)
   - **Impact**: [Effect on project goals]
   - **Root Cause**: [Known/suspected cause]
   - **Required Action**: [What MUST be done]
   - **Timeline**: [When it must be fixed]

2. **❌ [Issue #2]**:
   - **Description**: [What's wrong]
   - **Evidence**: [Data showing the problem]
   - **Severity**: Critical
   - **Impact**: [Effect on project goals]
   - **Root Cause**: [Known/suspected cause]
   - **Required Action**: [What MUST be done]
   - **Timeline**: [When it must be fixed]

---

## Detailed Comparison with Lucene

> **Purpose**: Comprehensive head-to-head analysis with baseline
> **Baseline**: Apache Lucene is the industry standard for search performance

### Methodology

**Lucene Benchmark Configuration**:
- Version: [Lucene version tested]
- JVM: [Java version, heap settings]
- Configuration: [Index settings, similarity, etc.]
- Dataset: [Same dataset as Diagon test]
- Hardware: [Same hardware as Diagon test]
- Measurement: [Same methodology as Diagon]

**Comparison Fairness**:
- [ ] Same hardware used for both tests
- [ ] Same dataset used for both tests
- [ ] Both tests use Release/optimized builds
- [ ] Both tests measured with same methodology
- [ ] Caches cleared/warmed consistently
- [ ] System load controlled for both tests

### Indexing Comparison

| Aspect | Diagon | Lucene | Ratio | Assessment |
|--------|--------|--------|-------|------------|
| **Throughput** | [value] docs/sec | [value] docs/sec | [X.XX]x | [✅/⚠️/❌] |
| **Time** | [value]s | [value]s | [X.XX]x | [✅/⚠️/❌] |
| **Index Size** | [value] MB | [value] MB | [X.XX]x | [✅/⚠️/❌] |
| **Compression** | [value] bytes/doc | [value] bytes/doc | [X.XX]x | [✅/⚠️/❌] |
| **Memory** | [value] MB | [value] MB | [X.XX]x | [✅/⚠️/❌] |

**Assessment**: [✅ Competitive | ⚠️ Acceptable | ❌ Needs improvement]

**Analysis**:
- Indexing is [faster/slower/equal]: [Explanation]
- Index size is [larger/smaller/equal]: [Explanation]
- Trade-offs: [Any trade-offs between speed and size]

### Query Latency Comparison (P99)

| Query Type | Diagon P99 | Lucene P99 | Speedup | Target | Status |
|------------|------------|------------|---------|--------|--------|
| Single-term | [value] μs | [value] μs | [X.X]x | 3-5x | [✅/⚠️/❌] |
| Boolean AND | [value] μs | [value] μs | [X.X]x | 3-5x | [✅/⚠️/❌] |
| Boolean OR | [value] μs | [value] μs | [X.X]x | 3-5x | [✅/⚠️/❌] |
| **Average** | **[value] μs** | **[value] μs** | **[X.X]x** | **3-10x** | **[✅/⚠️/❌]** |

**Assessment**: [✅ Target exceeded (3-10x) | ⚠️ Below target | ❌ Slower than Lucene]

**Analysis**:
- Overall: Diagon is [X.X]x [faster/slower] than Lucene
- Best performance: [Query type with highest speedup]
- Worst performance: [Query type with lowest speedup]
- Consistency: [How consistent is the speedup across queries]

### Query Correctness Comparison

| Aspect | Diagon | Lucene | Match | Status |
|--------|--------|--------|-------|--------|
| **Total Queries Tested** | [count] | [count] | [Y/N] | [✅/❌] |
| **Hit Count Match** | [count correct] | [baseline] | [XX]% | [✅/⚠️/❌] |
| **Scoring Match** | [if tested] | [baseline] | [XX]% | [✅/⚠️/❌] |
| **Result Order Match** | [if tested] | [baseline] | [XX]% | [✅/⚠️/❌] |

**Correctness Issues**:
[None | List any queries where results don't match Lucene]

**Assessment**: [✅ 100% correct | ⚠️ Minor discrepancies | ❌ Significant errors]

### Feature Parity

| Feature | Diagon | Lucene | Status | Notes |
|---------|--------|--------|--------|-------|
| Single-term queries | [✅/❌] | ✅ | [✅/⚠️/❌] | [Any differences] |
| Boolean queries | [✅/❌] | ✅ | [✅/⚠️/❌] | [Any differences] |
| Phrase queries | [✅/❌] | ✅ | [✅/⚠️/❌] | [Any differences] |
| Wildcard queries | [✅/❌] | ✅ | [✅/⚠️/❌] | [Any differences] |
| Fuzzy queries | [✅/❌] | ✅ | [✅/⚠️/❌] | [Any differences] |
| BM25 scoring | [✅/❌] | ✅ | [✅/⚠️/❌] | [Any differences] |

### Target Achievement Matrix

| Goal | Target | Achieved | Gap | Status | Priority |
|------|--------|----------|-----|--------|----------|
| **Search speed vs Lucene** | 3-10x faster | [X.X]x | [±X.X]x | [✅/⚠️/❌] | Critical |
| **Indexing throughput** | ≥5K docs/sec | [value] | [±XXX] | [✅/⚠️/❌] | High |
| **Index size** | Competitive (±20%) | [±XX%] | [XX%] | [✅/⚠️/❌] | Medium |
| **Query correctness** | 100% | [XX%] | [-X%] | [✅/⚠️/❌] | Critical |
| **Memory efficiency** | [target] | [value] | [delta] | [✅/⚠️/❌] | Medium |
| **Feature parity** | [target%] | [XX%] | [-X%] | [✅/⚠️/❌] | Low |

**Overall Target Achievement**: [XX%] of targets met

---

## Issues and Concerns

> **Purpose**: Categorize all problems by severity for prioritization
> **Critical**: Use severity categories honestly - don't downplay problems

### Issue Severity Definitions

- **❌ Critical (Must Fix)**: Blocks progress, violates requirements, or causes data corruption
- **⚠️ Important (Should Fix)**: Significant impact on performance or usability, should address soon
- **ℹ️ Minor (Nice to Fix)**: Small improvements, low impact, can defer

### Critical Issues (Must Fix) ❌

> Problems that MUST be resolved before proceeding

[Choose one:]

**None** - No critical issues identified

OR

**Critical Issues**:

1. **❌ [Issue Title]**:
   - **Category**: [Performance/Correctness/Stability/Build]
   - **Description**: [Detailed description of the problem]
   - **Evidence**: [Data, logs, or observations proving this is critical]
   - **Impact**: [Effect on project goals, users, or stability]
   - **Root Cause**: [Known/Suspected cause]
   - **Affected Components**: [Which modules/features are affected]
   - **Workaround**: [Any temporary workaround, or "None"]
   - **Required Fix**: [What must be done to resolve]
   - **Estimated Effort**: [If known: hours/days/weeks]
   - **Blocking**: [What does this block?]
   - **Owner**: [Who should fix this]
   - **Deadline**: [When must this be fixed]

### Important Issues (Should Fix) ⚠️

> Significant problems that should be addressed soon

[List using same format as critical issues, or "None"]

### Minor Issues (Nice to Fix) ℹ️

> Small improvements with low priority

[List using same format as critical issues, or "None"]

### Known Limitations

> Documented limitations that are not bugs but users should know about

1. **[Limitation #1]**: [Description]
   - Impact: [Low/Medium/High]
   - Planned: [Will fix / Won't fix / Under consideration]

### False Positives / Non-Issues

> Things that might look like problems but aren't

[List any potential confusion points and explanations]

---

## Recommendations

> **Purpose**: Provide actionable next steps based on findings
> **Requirement**: All recommendations must be specific, measurable, achievable

### Prioritization Matrix

| Priority | Focus | Timeline | Effort | Impact |
|----------|-------|----------|--------|--------|
| **P0 (Critical)** | Must do now | Immediate (this week) | Any | Blocks progress |
| **P1 (High)** | Should do soon | Short-term (this sprint) | High ROI | Significant improvement |
| **P2 (Medium)** | Nice to have | Medium-term (next sprint) | Good ROI | Moderate improvement |
| **P3 (Low)** | Can defer | Long-term (backlog) | Any ROI | Small improvement |

### Immediate Actions (P0)

> Must be completed before next iteration/release

[Choose one:]

**None** - No immediate actions required

OR

**Required Actions**:

1. **[Action #1]**:
   - **Goal**: [What this achieves]
   - **Tasks**:
     - [ ] [Specific task 1]
     - [ ] [Specific task 2]
   - **Success Criteria**: [How to measure success]
   - **Estimated Effort**: [hours/days]
   - **Owner**: [Who should do this]
   - **Deadline**: [When this must be done]
   - **Depends On**: [Any prerequisites]

### Short-term Improvements (P1)

> Should be completed in next 1-2 weeks

1. **[Improvement #1]**:
   - **Goal**: [What this achieves]
   - **Current Performance**: [Baseline]
   - **Target Performance**: [Goal]
   - **Expected Impact**: [Estimated improvement]
   - **Approach**: [High-level approach]
   - **Tasks**:
     - [ ] [Specific task 1]
     - [ ] [Specific task 2]
   - **Success Criteria**: [How to measure success]
   - **Estimated Effort**: [days/weeks]
   - **Risk**: [Low/Medium/High]

### Long-term Optimizations (P2-P3)

> Future improvements, nice-to-haves

1. **[Optimization #1]**:
   - **Goal**: [What this achieves]
   - **Expected Impact**: [Estimated improvement]
   - **Approach**: [High-level approach]
   - **Prerequisites**: [What's needed first]
   - **Estimated Effort**: [weeks/months]
   - **Priority**: [P2/P3]

### Research Opportunities

> Areas worth investigating but uncertain ROI

1. **[Research #1]**:
   - **Question**: [What we want to learn]
   - **Motivation**: [Why this matters]
   - **Approach**: [How to investigate]
   - **Expected Time**: [days/weeks]
   - **Go/No-Go Criteria**: [When to proceed or abandon]

---

## Raw Data

> **Purpose**: Complete transparency - include ALL output for verification
> **Requirement**: Include everything, don't sanitize or filter

### Indexing Output (Complete)

```
[Paste complete, unmodified output from indexing phase]

Example:
========================================
Diagon Indexing Benchmark
========================================
Dataset: Reuters-21578
Documents: 21,578
Location: /home/ubuntu/.../reuters-out/

Phase 1: Indexing
========================================
Cleaning index directory...
✓ Index directory ready: /tmp/diagon_reuters_index

Configuring index writer...
✓ IndexWriter created
  Max buffered docs: 50000
  RAM buffer: 512 MB

Reading documents from dataset...
  Indexed 1000 documents (0.16s)
  Indexed 2000 documents (0.32s)
  ...
  Indexed 21000 documents (3.20s)
  Indexed 21578 documents (3.35s)

Committing index...
✓ Index commit complete (3.48s total)

Indexing Statistics:
  Documents: 21,578
  Time: 3.48 seconds
  Throughput: 6,200 docs/sec
  Index size: 12.5 MB (13,107,200 bytes)
  Storage: 608 bytes/doc
  Peak memory: 845 MB
  Segments: 1
```

### Query Output (Complete)

```
[Paste complete, unmodified output from query phase]

Example:
Phase 2: Search Performance
========================================
Opening index...
✓ Index opened successfully
  Documents: 21,578
  Segments: 1

Running queries (100 iterations each)...

Query 1: 'dollar' (body field)
  Hits: 2,847
  Latencies (100 iterations):
    P50: 380 μs
    P75: 405 μs
    P90: 425 μs
    P95: 438 μs
    P99: 450 μs
  Lucene P99: 2,100 μs
  Speedup: 4.7x faster ✅

[Continue for all queries...]
```

### Build Information

```
[Complete build configuration and verification]

Example:
CMake Configuration:
--------------------
CMake version: 3.25.1
Generator: Unix Makefiles
Build type: Release
C++ compiler: /usr/bin/g++ (GCC 13.1.0)
C++ flags: -O3 -march=native
Interprocedural optimization: OFF

Compiler Details:
-----------------
g++ (Ubuntu 13.1.0-8ubuntu1) 13.1.0
Copyright (C) 2023 Free Software Foundation, Inc.

Build Flags:
------------
CMAKE_BUILD_TYPE: Release
CMAKE_CXX_FLAGS: -O3 -march=native
CMAKE_CXX_FLAGS_RELEASE: -DNDEBUG
CMAKE_INTERPROCEDURAL_OPTIMIZATION: OFF
DIAGON_BUILD_BENCHMARKS: ON
DIAGON_BUILD_TESTS: ON

Dependencies:
-------------
ICU: 74.2 (system)
  - libicuuc.so.74
  - libicui18n.so.74
ZSTD: 1.5.5 (system)
LZ4: 1.9.4 (system)
ZLIB: 1.2.13 (system)
Google Benchmark: 1.8.3 (system)

Library Linking (from ldd):
----------------------------
libdiagon_core.so =>
  linux-vdso.so.1
  libicuuc.so.74 => /lib/x86_64-linux-gnu/libicuuc.so.74
  libicui18n.so.74 => /lib/x86_64-linux-gnu/libicui18n.so.74
  libzstd.so.1 => /lib/x86_64-linux-gnu/libzstd.so.1
  liblz4.so.1 => /lib/x86_64-linux-gnu/liblz4.so.1
  libz.so.1 => /lib/x86_64-linux-gnu/libz.so.1
  ...

Git Information:
----------------
Commit: 7611493a
Branch: main
Date: 2026-02-06 04:40:15
Author: Claude Code
Message: Optimize indexing with aggressive vector pre-allocation

Build Timestamp: 2026-02-09 14:25:00
```

### System Information

```
[Complete system configuration]

Example:
Operating System:
-----------------
$ lsb_release -a
Distributor ID: Ubuntu
Description:    Ubuntu 22.04.3 LTS
Release:        22.04
Codename:       jammy

$ uname -a
Linux ip-172-31-45-123 6.14.0-1015-aws #17-Ubuntu SMP x86_64 GNU/Linux

CPU Information:
----------------
$ lscpu
Architecture:        x86_64
CPU op-mode(s):      32-bit, 64-bit
Byte Order:          Little Endian
CPU(s):              8
On-line CPU(s) list: 0-7
Thread(s) per core:  2
Core(s) per socket:  4
Socket(s):           1
Vendor ID:           GenuineIntel
Model name:          Intel(R) Xeon(R) CPU E5-2686 v4 @ 2.30GHz
CPU MHz:             2299.998
CPU max MHz:         3000.0000
CPU min MHz:         1200.0000
BogoMIPS:            4600.06
Virtualization:      VT-x
L1d cache:           32K
L1i cache:           32K
L2 cache:            256K
L3 cache:            46080K
Flags:               fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca
                     cmov pat pse36 clflush mmx fxsr sse sse2 ht syscall nx
                     pdpe1gb rdtscp lm constant_tsc rep_good nopl xtopology
                     nonstop_tsc cpuid tsc_known_freq pni pclmulqdq ssse3
                     fma cx16 pcid sse4_1 sse4_2 x2apic movbe popcnt
                     tsc_deadline_timer aes xsave avx f16c rdrand hypervisor
                     lahf_lm abm 3dnowprefetch fsgsbase bmi1 avx2 smep bmi2
                     erms invpcid xsaveopt

Memory Information:
-------------------
$ free -h
              total        used        free      shared  buff/cache   available
Mem:           31Gi       2.3Gi        27Gi       1.0Mi       1.9Gi        28Gi
Swap:            0B          0B          0B

Storage Information:
--------------------
$ df -h
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p1  468G   18G  450G   4% /

$ sudo nvme list
Node             Model                    Firmware    Size
---------------- ------------------------ --------- ----------
/dev/nvme0n1     Amazon EC2 NVMe          1.0       500.00 GB

Disk Performance:
-----------------
$ sudo hdparm -t /dev/nvme0n1
/dev/nvme0n1:
 Timing buffered disk reads: 4200 MB in  3.00 seconds = 1400.00 MB/sec
```

### Benchmark Configuration

```
[Exact benchmark parameters and settings]

Example:
Benchmark Configuration:
------------------------
Benchmark: ReutersBenchmark
Version: 1.0.0
Framework: Custom (not Google Benchmark)

Dataset Configuration:
  Path: /home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark/work/reuters-out/
  Files: 21,578 verified
  Format: Plain text, one file per document
  Adapter: SimpleReutersAdapter

Index Configuration:
  Location: /tmp/diagon_reuters_index
  Max buffered docs: 50,000
  RAM buffer: 512 MB
  Codec: Lucene104
  Similarity: BM25 (k1=1.2, b=0.75)

Query Configuration:
  Iterations per query: 100
  Warm-up iterations: 10
  Top-K results: 10
  Timing method: high_resolution_clock
  Timing precision: microseconds

System Configuration:
  CPU governor: performance
  Disk cache: Dropped before indexing
  Other processes: Minimal (only system daemons)
  Network: Offline
```

---

## Reproducibility

> **Purpose**: Enable anyone to exactly reproduce these results
> **Requirement**: Provide copy-paste ready commands with NO placeholders

### Prerequisites Verification

**Check system meets requirements**:
```bash
# Verify GCC version
g++ --version
# Required: GCC 11+ or Clang 14+

# Verify CMake version
cmake --version
# Required: CMake 3.20+

# Verify system libraries
ldconfig -p | grep -E "libicu|libzstd|liblz4|libz\.so"
# Should show all required libraries

# Check disk space
df -h /tmp
# Need at least 1 GB free for index
```

### Build Commands (Exact)

**Step 1: Clean environment**:
```bash
cd /home/ubuntu/diagon
rm -rf build
mkdir build
cd build
```

**Step 2: Configure CMake**:
```bash
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS="-O3 -march=native" \
      -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
      -DDIAGON_BUILD_BENCHMARKS=ON \
      -DDIAGON_BUILD_TESTS=ON \
      ..
```

**Step 3: Build core library**:
```bash
make diagon_core -j8
```

**Step 4: Verify ICU linking** (CRITICAL):
```bash
ldd src/core/libdiagon_core.so | grep icu
# MUST show: libicuuc.so and libicui18n.so
# If missing, rebuild following BUILD_SOP.md
```

**Step 5: Build benchmark**:
```bash
make ReutersBenchmark -j8
# Or: make ReutersWANDBenchmark -j8
# Or: make benchmarks -j8  # for all
```

**Step 6: Verify benchmark built**:
```bash
ls -lh benchmarks/ReutersBenchmark
./benchmarks/ReutersBenchmark --help  # Should run without errors
```

### Dataset Setup (Exact)

**Verify dataset exists**:
```bash
REUTERS_PATH="/home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark/work/reuters-out"
ls "$REUTERS_PATH"/*.txt | wc -l
# Should output: 21578
```

**If dataset missing, download**:
```bash
cd /home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark
ant get-reuters
# Wait for download and extraction (may take 5-10 minutes)

# Verify
ls work/reuters-out/*.txt | wc -l
# Should output: 21578
```

**Dataset integrity check**:
```bash
# Check total size
du -sh "$REUTERS_PATH"
# Expected: ~27 MB

# Check a sample file
head -20 "$REUTERS_PATH/reut2-000.sgm-0.txt"
# Should show Reuters article text
```

### Benchmark Execution (Exact)

**Prepare environment**:
```bash
# Drop disk caches (requires sudo)
sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'

# Set CPU governor to performance
sudo cpupower frequency-set -g performance

# Close unnecessary applications
# Stop any CPU-intensive services
```

**Run benchmark**:
```bash
cd /home/ubuntu/diagon/build/benchmarks

# Clean old index
rm -rf /tmp/diagon_reuters_index

# Run benchmark (standard)
./ReutersBenchmark

# Or run with output saved
./ReutersBenchmark 2>&1 | tee ~/reuters_benchmark_$(date +%Y%m%d_%H%M%S).log
```

**Run WAND benchmark** (if applicable):
```bash
# Run WAND optimization test
./ReutersWANDBenchmark

# Or with Google Benchmark options
./ReutersWANDBenchmark --benchmark_min_time=1.0
```

### Running Lucene Baseline (For Comparison)

**Setup Lucene** (if not already done):
```bash
cd /home/ubuntu/opensearch_warmroom/lucene
./gradlew jar
# Or: ant compile
```

**Run Lucene benchmark**:
```bash
cd /home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark
ant run-task -Dtask.alg=conf/reuters.alg 2>&1 | tee ~/lucene_benchmark_$(date +%Y%m%d_%H%M%S).log
```

**Extract Lucene results**:
```bash
# Results typically in: work/
# Look for files like: BenchResults*.txt
cat work/BenchResults*.txt
```

### Verification Steps

**After running benchmark**:
```bash
# Check index was created
ls -lh /tmp/diagon_reuters_index/
# Should show segment files

# Check index size
du -sh /tmp/diagon_reuters_index/
# Should be ~10-15 MB

# Verify no errors in output
# grep -i error ~/reuters_benchmark_*.log
# Should have no matches
```

### Troubleshooting

**If benchmark fails**:
```bash
# 1. Check build is correct
cd /home/ubuntu/diagon/build
make clean
rm CMakeCache.txt
# Then rebuild from Step 1

# 2. Verify ICU
ldd benchmarks/ReutersBenchmark | grep icu

# 3. Check dataset
ls /home/ubuntu/opensearch_warmroom/lucene/lucene/benchmark/work/reuters-out/*.txt | wc -l

# 4. Check disk space
df -h /tmp

# 5. Check for errors
./benchmarks/ReutersBenchmark 2>&1 | head -100
```

**Common issues**:
```bash
# Issue: "undefined reference to icu"
# Fix: Rebuild with correct ICU linking (see BUILD_SOP.md)

# Issue: "dataset not found"
# Fix: Download dataset (see Dataset Setup above)

# Issue: "Permission denied"
# Fix: Check /tmp is writable: touch /tmp/test && rm /tmp/test
```

---

## Appendix

### Glossary

**Performance Terms**:
- **P50 (Median)**: The value at which 50% of samples fall below
- **P95**: The value at which 95% of samples fall below (near-worst case)
- **P99**: The value at which 99% of samples fall below (tail latency)
- **Throughput**: Number of operations per unit time (e.g., docs/sec)
- **Latency**: Time taken to complete a single operation (e.g., microseconds)
- **Speedup**: Ratio of baseline performance to measured performance (e.g., 5x faster)

**Index Terms**:
- **Segment**: A self-contained inverted index (Lucene concept)
- **Posting**: An occurrence of a term in a document
- **Postings List**: All postings for a given term
- **Skip List**: Data structure for fast postings list traversal
- **Block-Max**: Maximum score within a posting list block (for WAND)

**Benchmark Terms**:
- **Cold Cache**: First run, data not in OS cache
- **Warm Cache**: Subsequent runs, data in OS cache
- **Baseline**: Reference implementation for comparison (Lucene)
- **Regression**: Performance decrease compared to previous version
- **Correctness**: Whether results match expected/baseline results

### Acronyms

- **BM25**: Best Match 25 (scoring algorithm)
- **WAND**: Weak AND (early termination algorithm)
- **FST**: Finite State Transducer (term dictionary)
- **IR**: Information Retrieval
- **LTO**: Link-Time Optimization
- **P50/P95/P99**: 50th/95th/99th percentile
- **SOP**: Standard Operating Procedure

### References

**Lucene Resources**:
- Official site: https://lucene.apache.org/
- API docs: https://lucene.apache.org/core/9_11_0/
- Benchmark guide: [URL to Lucene benchmark docs]
- Source code: https://github.com/apache/lucene

**Reuters-21578 Dataset**:
- Official page: https://www.daviddlewis.com/resources/testcollections/reuters21578/
- Description: Collection of 21,578 Reuters newswire articles from 1987
- License: [License information]
- Download: [How to download if not available locally]

**Diagon Resources**:
- Repository: [GitHub URL]
- Documentation: [docs/ directory]
- Commit for this run: [GitHub commit URL]
- Build SOP: [Path to BUILD_SOP.md]

**Related Papers**:
- BM25 algorithm: Robertson & Zaragoza, "The Probabilistic Relevance Framework: BM25 and Beyond"
- WAND algorithm: Broder et al., "Efficient Query Evaluation using a Two-Level Retrieval Process"
- Block-Max WAND: Ding & Suel, "Faster Top-k Document Retrieval Using Block-Max Indexes"

### Change Log

**Template Version History**:
- v2.0 (2026-02-09): Elaborated version with detailed instructions
- v1.0 (2026-02-09): Initial template version

**Report History** (if multiple reports):
- [Date]: [Version], [Major changes]

### Contact Information

**Report Issues**:
- GitHub Issues: [URL]
- Email: [Contact email]
- Slack: [Channel]

**Review Process**:
- Reports should be reviewed by: [Role/Name]
- Review criteria: [Quality checklist reference]
- Approval process: [How reports are approved]

---

## Signature and Approval

**Report Generated By**: [Name/Tool]
**Report Generation Date**: [YYYY-MM-DD HH:MM:SS]
**Report Generated From**: [Automated/Manual]

**Technical Review**:
- **Reviewed By**: [Name, Role]
- **Review Date**: [YYYY-MM-DD]
- **Review Status**: [✅ Approved | ⚠️ Conditional | ❌ Rejected | ⏳ Pending]
- **Review Comments**: [Any comments or concerns]

**Project Lead Approval**:
- **Approved By**: [Name, Role]
- **Approval Date**: [YYYY-MM-DD]
- **Approval Status**: [✅ Approved | ⚠️ Conditional | ❌ Rejected | ⏳ Pending]
- **Approval Comments**: [Any comments or decision notes]

**Next Steps**:
- [ ] Share report with team
- [ ] Schedule review meeting (if needed)
- [ ] Create tickets for recommended actions
- [ ] Update project roadmap based on findings
- [ ] Archive report in [location]

---

## Footer

**Template Compliance**: This report follows the Diagon Benchmark Report Template v2.0

**Project Tenets** (from CLAUDE.md):
- ✅ **Be Self-discipline**: This report is based on correctly built artifacts
- ✅ **Be Humble and Straight**: Issues reported directly, not buried
- ✅ **Be Honest**: All data is actual measured results, clearly annotated
- ✅ **Be Rational**: Findings based on observations, not predictions
- ✅ **Insist Highest Standard**: Target 3-10x faster, no excuses for falling short

**Quality Assurance**:
- All required sections: [✅ Complete | ⚠️ Some missing | ❌ Major gaps]
- All metrics measured: [✅ All | ⚠️ Most | ❌ Some missing]
- Lucene comparison: [✅ Complete | ⚠️ Partial | ❌ Missing]
- Reproducibility: [✅ Fully documented | ⚠️ Mostly | ❌ Insufficient]

---

**END OF REPORT**

*Generated by Diagon Benchmark Framework*
*Template Version: 2.0 (Elaborated)*
*Report Date: [YYYY-MM-DD]*
