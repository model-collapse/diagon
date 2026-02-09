# Skill Migration: JSON → SKILL.md Format

**Date**: February 9, 2026
**Status**: ✅ Complete

## Problem Discovered

Skills were not being recognized when invoked with `/skill_name`.

## Root Causes

1. ❌ **Wrong location**: Skills were in project-local `.claude/skills/` instead of global `~/.claude/skills/`
2. ❌ **Wrong format**: Skills were JSON files instead of SKILL.md markdown files
3. ❌ **Wrong structure**: Skills were flat files instead of directories containing SKILL.md

## Solution Applied

### Correct Skill Structure

```
~/.claude/skills/                          ← Global directory
├── build_diagon/
│   └── SKILL.md                          ← Directory + SKILL.md
├── benchmark_diagon/
│   └── SKILL.md
├── benchmark_diagon_multiterm/
│   └── SKILL.md
├── benchmark_lucene_multiterm/
│   └── SKILL.md
└── benchmark_reuters_lucene/
    └── SKILL.md
```

### SKILL.md Format (YAML + Markdown)

```markdown
---
name: skill_name
description: Brief description
allowed-tools: Bash, Read, Write
user-invocable: true
argument-hint: [arg1=default] [arg2]
---

# Skill Title

Detailed instructions in markdown format...

## When to Use This Skill
...

## Arguments
...

## Execution Steps
...
```

## Skills Migrated

All 5 skills successfully migrated:

1. **`/build_diagon`** ✅
   - Location: `~/.claude/skills/build_diagon/SKILL.md`
   - Status: Registered and tested
   - Function: Build Diagon following BUILD_SOP.md

2. **`/benchmark_diagon`** ✅
   - Location: `~/.claude/skills/benchmark_diagon/SKILL.md`
   - Status: Registered
   - Function: Pure Diagon performance tracking

3. **`/benchmark_reuters_lucene`** ✅
   - Location: `~/.claude/skills/benchmark_reuters_lucene/SKILL.md`
   - Status: Registered
   - Function: Diagon vs Lucene comparison

4. **`/benchmark_diagon_multiterm`** ✅
   - Location: `~/.claude/skills/benchmark_diagon_multiterm/SKILL.md`
   - Status: Registered
   - Function: Multi-term query deep dive

5. **`/benchmark_lucene_multiterm`** ✅
   - Location: `~/.claude/skills/benchmark_lucene_multiterm/SKILL.md`
   - Status: Registered
   - Function: Competitive multi-term analysis

## Verification

Skills now appear in Claude Code system reminders:

```
The following skills are available for use with the Skill tool:

- benchmark_diagon: Run pure Diagon performance benchmark...
- benchmark_diagon_multiterm: Benchmark Diagon multi-term query...
- benchmark_lucene_multiterm: Benchmark Diagon vs Lucene multi-term...
- benchmark_reuters_lucene: Benchmark Diagon vs Apache Lucene...
- build_diagon: Build Diagon C++ search engine...
- mermaid-diagram: Generate beautiful hand-drawn flowchart diagrams...
```

## Cleanup Actions

1. ✅ Removed old JSON files from `/home/ubuntu/diagon/.claude/skills/`
2. ✅ Created proper SKILL.md files in `~/.claude/skills/`
3. ✅ Updated README.md with location and format notes
4. ✅ Tested `/build_diagon` skill successfully

## Testing

Tested `/build_diagon` skill:
```bash
/build_diagon
```

Result:
- ✅ Skill recognized
- ✅ CMake configuration successful
- ✅ Build completed (libdiagon_core.so)
- ✅ ICU verification passed
- ✅ All outputs correct

## Key Learnings

1. **Skills must be global**: Place in `~/.claude/skills/`, not project-local
2. **Format matters**: YAML frontmatter + markdown, not JSON
3. **Structure is directory**: Each skill is a directory with SKILL.md inside
4. **Registration is automatic**: Skills appear after creation, no manual registration needed
5. **Example to follow**: See `~/.claude/skills/mermaid-diagram/SKILL.md`

## Documentation Updates

Updated files:
- `/home/ubuntu/diagon/.claude/skills/README.md` - Added location and format warnings
- Created this migration notes document

## Next Steps

No action required. All skills are working properly.

For future skill creation:
1. Create directory: `mkdir ~/.claude/skills/new_skill/`
2. Create SKILL.md with proper YAML frontmatter
3. Test with `/new_skill`
4. Add documentation to project `.claude/skills/` directory (optional)

## References

- Working example: `~/.claude/skills/mermaid-diagram/SKILL.md`
- Project docs: `/home/ubuntu/diagon/.claude/skills/`
- Workflow docs: `/home/ubuntu/diagon/.claude/BENCHMARK_AGENT_WORKFLOWS.md`

---

**Migration completed successfully**: February 9, 2026
