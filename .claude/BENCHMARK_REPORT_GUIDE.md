# Benchmark Report Guide

## Overview

This guide explains how to use the benchmark report template and how the benchmark skills should generate reports.

## Template Location

- **Template**: `.claude/BENCHMARK_REPORT_TEMPLATE.md`
- **Purpose**: Standard format for all Diagon benchmark reports
- **Version**: 1.0.0

## Key Principles

### From CLAUDE.md Tenets

1. **Be Self-discipline**
   - Only report correctly built artifacts
   - No predicted/guessed data
   - All results must be from actual runs

2. **Be Humble and Straight**
   - Don't hide issues in long text
   - Report real data directly
   - Objective comparison only

3. **Be Honest**
   - Annotate "predicted" vs "experimented"
   - Don't disguise unreliable data
   - No confident narrative for fake data

4. **Be Rational**
   - Each finding based on observation
   - Deep dive before conclusions
   - No clueless possibilities

5. **Insist Highest Standard**
   - Target: 3-10x faster than Lucene
   - No excuses for falling behind
   - Be ashamed if slower

## Report Sections

### 1. Executive Summary (Required)
**Purpose**: Quick overview for decision makers

**Must include**:
- Overall result (PASS/PARTIAL/FAIL)
- 3-5 key findings (most important only)
- Performance vs target summary
- Critical issues (if any)

**Guidelines**:
- Keep to 1 page maximum
- Use bullet points
- Be direct and clear
- No marketing language

### 2. Test Environment (Required)
**Purpose**: Enable reproducibility

**Must include**:
- Hardware specs (CPU, RAM, storage)
- Software versions (compiler, OS, Diagon commit)
- Dataset information
- Build configuration

**Guidelines**:
- Include all details needed to reproduce
- Specify exact versions
- Document any non-standard setup

### 3. Indexing Performance (Required)
**Purpose**: Measure indexing capabilities

**Must include**:
- Throughput (docs/sec)
- Time taken
- Index size (MB and bytes/doc)
- Comparison with Lucene

**Guidelines**:
- Use table format for clarity
- Show target vs actual
- Status indicators (✅/⚠️/❌)
- Brief analysis (2-3 sentences)

### 4. Query Performance (Required)
**Purpose**: Measure search speed vs Lucene

**Must include**:
- P50, P95, P99 latencies
- Queries grouped by type (single-term, AND, OR)
- Comparison with Lucene (Xx faster/slower)
- Overall summary

**Guidelines**:
- Separate tables per query type
- Always show vs Lucene comparison
- Highlight target achievement
- Use consistent units (microseconds)

### 5. Performance Analysis (Required)
**Purpose**: Honest assessment of results

**Must include**:
- Strengths (what went well)
- Areas for improvement (what fell short)
- Critical issues (must-fix problems)

**Guidelines**:
- Be brutally honest
- Don't hide problems
- Specific, not vague
- Actionable insights

### 6. Comparison with Lucene (Required)
**Purpose**: Direct head-to-head evaluation

**Must include**:
- Indexing comparison
- Query latency comparison
- Index size comparison
- Target achievement table

**Guidelines**:
- Show actual numbers, not just ratios
- Clear assessment (✅/⚠️/❌)
- Explain any significant differences

### 7. Issues and Concerns (Required)
**Purpose**: Track problems and blockers

**Must include**:
- Critical issues (must fix)
- Important issues (should fix)
- Minor issues (nice to fix)

**Guidelines**:
- Categorize by severity
- "None" if no issues (don't skip section)
- Specific problem descriptions
- Impact on goals

### 8. Recommendations (Required)
**Purpose**: Actionable next steps

**Must include**:
- Immediate actions
- Short-term improvements
- Long-term optimizations

**Guidelines**:
- Prioritized list
- Concrete actions
- Based on data from report
- Feasible and rational

### 9. Raw Data (Required)
**Purpose**: Full transparency and debugging

**Must include**:
- Complete indexing output
- Full query results
- Build information
- System information

**Guidelines**:
- Include everything
- Use code blocks for formatting
- Don't sanitize or filter
- Enable deep investigation

### 10. Reproducibility (Required)
**Purpose**: Allow exact reproduction

**Must include**:
- Build commands
- Benchmark commands
- Dataset setup instructions

**Guidelines**:
- Copy-paste ready
- No placeholders
- Exact paths and versions
- Complete workflow

## Status Indicators

### Overall Result
- **✅ PASS**: All targets met, no critical issues
- **⚠️ PARTIAL**: Some targets met, minor issues present
- **❌ FAIL**: Targets missed, critical issues present

### Individual Metrics
- **✅**: Met or exceeded target
- **⚠️**: Below target but acceptable
- **❌**: Significantly below target or failure

## Performance Targets

### Indexing
- **Throughput**: ≥5,000 docs/sec
- **Index size**: Within 20% of Lucene
- **Time**: Reasonable for dataset size

### Query Latency
- **Target**: 3-10x faster than Lucene
- **Minimum**: At least as fast as Lucene
- **Critical**: Never slower than Lucene

### Query Correctness
- **Target**: 100% correct results
- **Minimum**: 99.9% correct
- **Critical**: Any incorrect results must be flagged

## Report Generation

### Automated Generation
Benchmark skills should:
1. Run benchmarks with proper setup
2. Collect all metrics
3. Compare with Lucene baseline
4. Generate report following template
5. Save to `benchmark_results/` directory
6. Use naming: `[BENCHMARK]_[YYYYMMDD_HHMMSS].md`

### Manual Review
After generation:
1. Verify all sections complete
2. Check for accuracy
3. Add analysis if needed
4. Review against tenets
5. Sign off if approved

## Example Reports

### Good Report Example
```markdown
# Benchmark Report: Reuters-21578

**Result**: ✅ PASS

**Key Findings**:
- Query latency: 5.2x faster than Lucene (exceeded target)
- Indexing: 6,200 docs/sec (above target)
- Index size: 12.5 MB (competitive with Lucene's 11.8 MB)

**Critical Issues**: None
```

### Bad Report Example (Don't Do This)
```markdown
# Benchmark Report

We ran some tests and everything looks great!
Performance is amazing and much better than before.
We're confident this will be the fastest search engine ever.
```

**Problems**:
- No specific numbers
- Marketing language
- No comparison with Lucene
- No raw data
- Not following template

## Report Storage

### Location
```
/home/ubuntu/diagon/benchmark_results/
├── reuters_lucene_20260209_143022.md
├── reuters_lucene_20260209_150134.md
└── wand_optimization_20260209_152345.md
```

### Naming Convention
```
[benchmark_type]_[YYYYMMDD]_[HHMMSS].md
```

Examples:
- `reuters_lucene_20260209_143022.md`
- `wand_optimization_20260209_150000.md`
- `custom_dataset_20260209_160000.md`

## Integration with Skills

### Benchmark Skills Should
1. **Collect data**: All metrics from benchmark run
2. **Compare**: Against Lucene baseline
3. **Analyze**: Performance vs targets
4. **Generate**: Report following template
5. **Save**: To benchmark_results/ directory
6. **Display**: Summary to user

### Template Usage
```bash
# Generate report
/benchmark_reuters_lucene

# Output includes:
# - Console summary
# - Full report saved to file
# - Path to report displayed
```

## Quality Checklist

Before finalizing a report, verify:

- [ ] All required sections present
- [ ] Executive summary clear and concise
- [ ] All metrics have actual values (no placeholders)
- [ ] Comparison with Lucene included
- [ ] Status indicators used correctly
- [ ] Issues section honest and complete
- [ ] Recommendations actionable and rational
- [ ] Raw data included
- [ ] Reproducibility section complete
- [ ] Report follows CLAUDE.md tenets
- [ ] No marketing language or exaggeration
- [ ] Problems not hidden or downplayed

## Common Mistakes

### ❌ Don't Do This

1. **Hiding Issues**: Burying problems in long paragraphs
   ```
   Overall performance was excellent with some minor optimizations
   still needed in a few edge cases that don't really impact most
   users and will be addressed in future iterations...
   ```

2. **Vague Numbers**: Using imprecise descriptions
   ```
   Query performance was much faster than before
   Index size is reasonable
   ```

3. **Missing Comparison**: Not comparing with Lucene
   ```
   Query latency: 500μs
   [No Lucene comparison!]
   ```

4. **Marketing Speak**: Overselling results
   ```
   Revolutionary performance breakthrough!
   Unprecedented speed improvements!
   ```

### ✅ Do This Instead

1. **Direct Issue Reporting**:
   ```
   Critical Issues:
   - OR queries 1.2x slower than Lucene (target: 3-5x faster)
   - Must investigate WAND optimization
   ```

2. **Specific Numbers**:
   ```
   Query latency (P99): 450μs
   Throughput: 6,200 docs/sec
   Index size: 12.5 MB
   ```

3. **Always Compare**:
   ```
   | Metric | Diagon | Lucene | Ratio |
   |--------|--------|--------|-------|
   | Latency | 450μs | 2,100μs | 4.7x faster |
   ```

4. **Factual Reporting**:
   ```
   Results: 4.7x faster than Lucene on average
   Target (3-10x faster): ✅ Met
   ```

## Continuous Improvement

### Track Over Time
- Save all reports
- Compare with previous runs
- Track progress toward goals
- Identify regressions

### Learn from Reports
- Review what worked
- Understand what didn't
- Apply lessons to next iteration
- Share findings with team

---

**Template Version**: 1.0.0
**Last Updated**: 2026-02-09
**Maintained By**: Diagon Project
