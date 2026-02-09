# Benchmark Agent Workflows

## Overview

While Claude Code doesn't have specialized benchmark subagents, the **`general-purpose` agent** can autonomously run benchmarks as part of multi-step workflows. This document shows how to use agents for benchmark validation.

## MANDATORY: Agents MUST Use Skills

**CRITICAL RULE**: When agents perform benchmarking, they MUST use the Skill tool to invoke benchmark skills. They are FORBIDDEN from writing custom benchmark code.

### ✅ Correct Agent Behavior
```python
Task(
    subagent_type="general-purpose",
    prompt="""
    Use the Skill tool to run benchmark:
    Skill(skill="benchmark_lucene_multiterm")

    Then analyze the generated report.
    """
)
```

### ❌ FORBIDDEN Agent Behavior
```python
Task(
    subagent_type="general-purpose",
    prompt="""
    Write a C++ benchmark to compare multi-term queries...
    """
)
# WRONG: This leads to custom code instead of using skills
```

### Why This Matters

Agents have autonomy and may choose to:
- Write custom benchmark code (WRONG)
- Create new benchmark implementations (WRONG)
- Bypass skills infrastructure (WRONG)

**Solution**: Explicit instructions in agent prompts to use Skill tool.

## When to Use Agents vs Skills

| Scenario | Use | Why |
|----------|-----|-----|
| User explicitly asks to benchmark | **Skill**: `/benchmark_diagon` | Direct user command |
| Part of autonomous workflow | **Agent**: `general-purpose` | Claude-initiated validation |
| After optimization work | **Agent** (if autonomous) or **Skill** (if user-directed) | Depends on workflow |
| Pre-commit validation | **Agent** | Part of automated checks |
| User wants manual control | **Skill** | User invokes directly |

## Agent Capabilities

The `general-purpose` agent has access to **all tools** including:
- ✅ Skills (can invoke `/benchmark_diagon`, etc.)
- ✅ Bash (can run build commands)
- ✅ Read (can analyze reports)
- ✅ Grep (can search for regressions)

This makes it powerful for autonomous benchmark workflows.

## Benchmark Workflow Patterns

### Pattern 1: Performance Validation After Optimization

**Scenario**: You optimize code and want to validate improvement autonomously.

**Task Call**:
```python
Task(
    subagent_type="general-purpose",
    description="Validate optimization performance",
    prompt="""
I just optimized WANDScorer.cpp for better early termination.

Please validate the optimization:

1. Build benchmarks using the Skill tool:
   Skill(skill="build_diagon", args="target=benchmarks")

2. Run pure Diagon benchmark using the Skill tool:
   Skill(skill="benchmark_diagon")

   IMPORTANT: Use the Skill tool above. Do NOT write custom benchmark code.

3. Analyze the report in benchmark_results/:
   - Check if regression detected (look for ❌ or "slower")
   - Compare with baseline
   - Focus on OR query performance (WAND impact)

4. Report back:
   - Status: IMPROVED / STABLE / REGRESSED
   - Key metrics (throughput, query latency)
   - Any concerns

Be concise but thorough.
"""
)
```

**When Claude would spawn this:**
- After making WAND optimization changes
- As part of optimization workflow
- Before suggesting commit

**Expected output:**
```
✅ IMPROVED
- Throughput: 6,800 docs/sec (+9.7% vs baseline)
- OR query P99: 2,800μs (-12% improvement)
- No regressions detected
- WAND early termination working effectively

Recommendation: Changes validated, ready to commit.
```

---

### Pattern 2: Competitive Analysis

**Scenario**: Validate speedup vs Lucene for release.

**Task Call**:
```python
Task(
    subagent_type="general-purpose",
    description="Compare with Lucene",
    prompt="""
We're preparing for a release. Please run competitive analysis vs Lucene.

MANDATORY: Use the Skill tool for all operations. Do NOT write custom code.

1. Build benchmarks using Skill tool:
   Skill(skill="build_diagon", args="target=benchmarks")

2. Run Lucene comparison using Skill tool:
   Skill(skill="benchmark_reuters_lucene", args="benchmark=both")

3. Analyze results:
   - Check speedup ratios vs target (3-10x)
   - Identify any areas where we're slower
   - Check multi-term query advantage

4. Report:
   - Overall speedup achieved
   - Areas meeting target
   - Areas needing work
   - Marketing highlights (max speedup, key advantages)

Focus on factual comparison, no exaggeration.
"""
)
```

**Expected output:**
```
✅ Competitive Analysis Complete

Overall: 4.2x faster than Lucene (average)

Meeting targets:
- Single-term: 3.8x faster ✅
- Boolean AND: 4.1x faster ✅
- Boolean OR: 5.2x faster ✅ (WAND advantage)
- Multi-term: 4.8x faster ✅

Below target:
- None

Marketing highlights:
- "Up to 5.2x faster than Apache Lucene"
- "50% faster for complex Boolean queries"
- WAND early termination provides significant OR query advantage

Ready for release announcement.
```

---

### Pattern 3: Multi-Term Query Focus

**Scenario**: After optimizing Boolean query execution.

**Task Call**:
```python
Task(
    subagent_type="general-purpose",
    description="Validate multi-term queries",
    prompt="""
I optimized BooleanQuery intersection algorithm.

Please validate multi-term query performance:

1. Build: /build_diagon target=benchmarks

2. Run multi-term benchmark: /benchmark_diagon_multiterm

3. Analyze:
   - Check AND query performance (should improve)
   - Check scalability (sub-linear growth?)
   - Compare P99 latencies with targets:
     * 2-term AND: <2ms
     * 5-term AND: <5ms
     * 10-term AND: <10ms

4. Report:
   - Which queries improved
   - Which meet targets
   - Scalability assessment

Focus on AND queries (my optimization target).
"""
)
```

---

### Pattern 4: Regression Detection

**Scenario**: Check for performance regressions before commit.

**Task Call**:
```python
Task(
    subagent_type="general-purpose",
    description="Check for regressions",
    prompt="""
Before committing these changes, please check for performance regressions.

1. Build: /build_diagon target=benchmarks

2. Run baseline benchmark: /benchmark_diagon

3. Check the report for regressions:
   - Look for ❌ symbols
   - Look for "slower" or "regression" keywords
   - Check if any metrics >10% worse than baseline

4. Report:
   - PASS (no regressions) or FAIL (regressions detected)
   - If FAIL: list specific regressions
   - Severity: critical (>20% slower) or minor (10-20% slower)

Be strict: any regression should be flagged.
"""
)
```

**Expected output (PASS)**:
```
✅ PASS - No regressions detected

All metrics stable or improved:
- Indexing: 6,200 docs/sec (stable, +0.3%)
- Single-term P99: 450μs (stable)
- Boolean AND P99: 1,200μs (stable)
- Boolean OR P99: 3,100μs (improved, -3%)

Safe to commit.
```

**Expected output (FAIL)**:
```
❌ FAIL - Regressions detected

Critical regressions:
- Indexing throughput: 4,800 docs/sec (-22% vs baseline)
  Baseline: 6,200 docs/sec
  Status: CRITICAL

Minor regressions:
- Boolean AND P99: 1,350μs (+12% vs baseline)
  Target: <2ms (still meets target)
  Status: MINOR

Recommendation: Investigate indexing regression before committing.
```

---

### Pattern 5: Pre-Commit Full Validation

**Scenario**: Comprehensive validation before important commit.

**Task Call**:
```python
Task(
    subagent_type="general-purpose",
    description="Full pre-commit validation",
    prompt="""
I'm about to commit significant optimization work. Please run full validation.

1. Build everything cleanly:
   /build_diagon target=all clean=true

2. Run comprehensive benchmarks:
   /benchmark_diagon benchmark=both
   /benchmark_diagon_multiterm

3. Analyze all reports:
   - Check for any regressions
   - Verify improvements claimed
   - Check stability (variance)

4. Final report:
   - Overall status: PASS/FAIL
   - Key improvements
   - Any concerns
   - Commit recommendation

This is important - be thorough.
"""
)
```

---

### Pattern 6: Post-Merge Validation

**Scenario**: After merging PR, validate production readiness.

**Task Call**:
```python
Task(
    subagent_type="general-purpose",
    description="Post-merge validation",
    prompt="""
PR just merged. Please validate production readiness.

1. Clean build from scratch:
   /build_diagon target=benchmarks clean=true

2. Run full benchmark suite:
   /benchmark_diagon benchmark=both
   /benchmark_reuters_lucene

3. Check:
   - Build succeeded (no warnings/errors)
   - All benchmarks pass
   - Performance targets met
   - No regressions vs pre-merge baseline

4. Report:
   - Build status
   - Benchmark status
   - Production ready: YES/NO
   - Any post-merge issues

Tag me if any issues found.
"""
)
```

---

## Example: Complete Optimization Workflow

Here's how Claude would use agents in a complete optimization workflow:

### User Request
```
"Optimize the WAND scorer for OR queries"
```

### Claude's Autonomous Workflow

**Step 1: Profile current performance**
```python
# Claude uses Bash tool to profile
perf record ./benchmarks/ReutersBenchmark --filter=OR
perf report > profile.txt
```

**Step 2: Make optimization changes**
```python
# Claude edits WANDScorer.cpp
Edit(file_path="src/core/src/search/WANDScorer.cpp", ...)
```

**Step 3: Spawn validation agent**
```python
Task(
    subagent_type="general-purpose",
    description="Validate WAND optimization",
    prompt="""
I optimized WAND early termination. Please validate:

1. Build: /build_diagon target=benchmarks
2. Benchmark: /benchmark_diagon_multiterm query_type=or
3. Check OR query performance vs baseline
4. Report: improved/stable/regressed with metrics
"""
)
```

**Agent returns:**
```
✅ IMPROVED
OR queries 18% faster (P99: 2,650μs → 2,180μs)
WAND early termination rate: 78% → 89%
Ready to commit.
```

**Step 4: Claude suggests commit**
```
Based on validation, the optimization improved OR query performance
by 18%. Should I create a commit?
```

**This workflow is fully autonomous** - user just said "optimize WAND", Claude did everything including validation.

---

## Workflow Decision Tree

```
User Request
    ↓
Is this part of autonomous work?
    ├─ YES → Use Task(general-purpose) agent
    │         Agent can invoke skills autonomously
    │         Example: optimization workflow
    │
    └─ NO → Is user asking to benchmark directly?
            ├─ YES → Use Skill (/benchmark_diagon)
            │         User explicitly wants benchmark
            │         Example: "run benchmark"
            │
            └─ NO → Clarify intent
                    Ask user if they want autonomous workflow
```

## Agent vs Skill: Detailed Comparison

### Skills (`/benchmark_diagon`)

**Characteristics:**
- User-invoked (user types command)
- Synchronous (user waits)
- Single-purpose (just run benchmark)
- Interactive (user sees progress)

**Best for:**
- Direct benchmark requests
- Manual performance checking
- User wants to see output real-time
- Exploratory work

**Example:**
```
User: "Run a benchmark"
Claude: [Invokes /benchmark_diagon skill]
User: [Waits, sees output]
```

### Agents (`general-purpose`)

**Characteristics:**
- Claude-invoked (part of workflow)
- Autonomous (runs independently)
- Multi-step (build + benchmark + analyze)
- Integrated (part of larger task)

**Best for:**
- Optimization workflows
- Pre-commit validation
- Complex multi-step processes
- Claude needs validation without user interaction

**Example:**
```
User: "Optimize WAND"
Claude: [Makes changes]
Claude: [Spawns agent to validate]
Agent: [Builds, benchmarks, reports back]
Claude: [Uses results to decide next step]
```

## Best Practices

### 1. Be Specific in Agent Prompts

**❌ Vague:**
```python
Task(prompt="Run benchmark and check results")
```

**✅ Specific:**
```python
Task(prompt="""
1. Build: /build_diagon target=benchmarks
2. Benchmark: /benchmark_diagon
3. Check for regressions (look for ❌ in report)
4. Report: PASS/FAIL with key metrics
""")
```

### 2. Specify Expected Output Format

**✅ Good:**
```python
Task(prompt="""
...
Report format:
- Status: IMPROVED/STABLE/REGRESSED
- Key metric: [value] ([change] vs baseline)
- Recommendation: [action]
""")
```

### 3. Focus Agent on Relevant Metrics

**✅ Focused:**
```python
Task(prompt="""
I optimized OR queries. Check:
- OR query P99 latency
- WAND early termination rate
- Don't worry about indexing or AND queries
""")
```

### 4. Give Context About Changes

**✅ Contextual:**
```python
Task(prompt="""
I optimized ByteBlockPool allocation in indexing.

Expected impact:
- Indexing throughput should improve
- Memory allocations should decrease
- Query performance should be unchanged

Validate these expectations.
""")
```

### 5. Request Actionable Output

**✅ Actionable:**
```python
Task(prompt="""
...
End with clear recommendation:
- "Safe to commit" (if all good)
- "Fix [issue] before committing" (if problems)
- "Consider [optimization]" (if opportunities found)
""")
```

## Integration with Skills

Agents can invoke skills directly. Here's the relationship:

```
general-purpose Agent
    ↓ can invoke
Skills (/benchmark_diagon, etc.)
    ↓ execute
Benchmark executables (ReutersBenchmark)
    ↓ generate
Reports (benchmark_results/*.md)
    ↓ analyzed by
Agent
    ↓ reports to
Claude (main workflow)
```

**Key insight**: Agents bridge autonomous workflows and user-facing skills.

## Common Patterns Summary

| Pattern | Agent Task | When Used |
|---------|-----------|-----------|
| **Performance Validation** | Build + benchmark + check regressions | After optimization |
| **Competitive Analysis** | Build + Lucene comparison + analyze | Pre-release |
| **Multi-Term Focus** | Build + multi-term benchmark + analyze | After query optimization |
| **Regression Detection** | Build + benchmark + strict checking | Pre-commit |
| **Full Validation** | Build all + all benchmarks + thorough check | Important commits |
| **Post-Merge** | Clean build + full suite + production check | After PR merge |

## Example Task Calls (Copy-Paste Ready)

### Quick Validation
```python
Task(
    subagent_type="general-purpose",
    description="Quick performance check",
    prompt="/build_diagon target=benchmarks && /benchmark_diagon && grep 'Overall Result' benchmark_results/diagon_*.md | tail -1"
)
```

### Regression Check Only
```python
Task(
    subagent_type="general-purpose",
    description="Check for regressions",
    prompt="/build_diagon target=benchmarks && /benchmark_diagon && if grep -q '❌' benchmark_results/diagon_*.md; then echo 'FAIL: Regressions detected'; else echo 'PASS: No regressions'; fi"
)
```

### Lucene Comparison
```python
Task(
    subagent_type="general-purpose",
    description="Compare with Lucene",
    prompt="/build_diagon target=benchmarks && /benchmark_reuters_lucene && grep 'Speedup' benchmark_results/reuters_lucene_*.md | tail -10"
)
```

## Limitations

**What agents CANNOT do:**
- ❌ Create specialized benchmark agent types (system limitation)
- ❌ Persist state between invocations (each task is independent)
- ❌ Modify the agent infrastructure

**What agents CAN do:**
- ✅ Invoke any skill
- ✅ Use any tool (Bash, Read, Grep, etc.)
- ✅ Multi-step workflows
- ✅ Analyze and report results

## Future Enhancement Ideas

If specialized benchmark agents become available:

1. **`voltagent-benchmark:performance-validator`**
   - Pre-configured for pure Diagon validation
   - Knows default regression thresholds
   - Optimized prompt templates

2. **`voltagent-benchmark:competitive-analyst`**
   - Pre-configured for Lucene comparison
   - Knows speedup targets
   - Marketing-ready output

3. **`voltagent-benchmark:regression-detector`**
   - Strict checking mode
   - Historical baseline tracking
   - Automatic bisection for regressions

Until then, `general-purpose` agent works well with explicit prompts.

---

## Quick Reference

**When should Claude spawn benchmark agents?**
- ✅ After optimization work (validate improvement)
- ✅ Pre-commit (check regressions)
- ✅ Post-merge (production validation)
- ✅ Complex workflows (multi-step validation)
- ❌ User explicitly asks to benchmark (use skill instead)
- ❌ Simple manual checks (use skill instead)

**Which skill should agent invoke?**
- `/benchmark_diagon` - Pure Diagon performance
- `/benchmark_reuters_lucene` - vs Lucene comparison
- `/benchmark_diagon_multiterm` - Multi-term focus
- `/benchmark_lucene_multiterm` - Multi-term competitive

**Key principle:**
> Agents enable autonomous validation as part of workflows.
> Skills provide direct user control.
> Use the right tool for the right context.

---

**Last Updated:** 2026-02-09
**Status:** ✅ Production workflows documented
**Agent Type:** `general-purpose` (system-provided)
