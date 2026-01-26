# IndexWriter/Reader Implementation Plan

## Current Status: ~40% Complete

**What Works:**
- ✅ Core architecture (abstract classes, hierarchies)
- ✅ Basic IndexWriter (addDocument, commit, flush)
- ✅ DirectoryReader (open, read segments)
- ✅ SegmentReader (terms access)
- ✅ Reference counting, lifecycle management

**What's Missing:**
- ⏳ Doc values (numeric, binary, sorted)
- ⏳ Stored fields (original document retrieval)
- ⏳ Deletions (update, delete operations)
- ⏳ Merges (forceMerge, background merging)
- ⏳ Norms (length normalization)
- ⏳ Caching, NRT support

## Implementation Plan (Incremental)

### Phase 1: Make It Useful (Enable Real Queries) 🎯
**Goal:** Enable filtering, sorting, and basic queries

#### Task 1.1: NumericDocValues Writing
**Estimate:** 2-3 days
**Files to create/modify:**
- `src/core/include/diagon/codecs/NumericDocValuesWriter.h`
- `src/core/src/codecs/NumericDocValuesWriter.cpp`
- Modify `IndexWriter` to write numeric doc values on flush

**What it does:**
- Write int64_t values to .dvd (data) and .dvm (metadata) files
- Support single-valued numeric fields (one value per doc)
- Simple format: docID → value mapping

**Enables:**
- Storing numeric values (price, timestamp, quantity)
- Foundation for filtering and sorting

#### Task 1.2: NumericDocValues Reading
**Estimate:** 2-3 days
**Files to create/modify:**
- `src/core/include/diagon/codecs/NumericDocValuesReader.h`
- `src/core/src/codecs/NumericDocValuesReader.cpp`
- Modify `SegmentReader::getNumericDocValues()` to return actual reader

**What it does:**
- Read numeric doc values from .dvd/.dvm files
- Return iterator over docID → value pairs
- Lazy loading (load on first access)

**Enables:**
- Filtering: WHERE price > 100
- Sorting: ORDER BY timestamp DESC
- Field access: Get price for document

#### Task 1.3: Integration Tests
**Estimate:** 1-2 days
**Files to create:**
- `tests/integration/NumericDocValuesTest.cpp`
- `tests/integration/IndexWriterReaderRoundtripTest.cpp`

**What it tests:**
- Write numeric values, read them back
- Correctness (all values match)
- Edge cases (0, MAX_INT64, negative numbers)
- Multiple fields per document

**Validates:**
- End-to-end functionality works
- Ready for real usage

### Phase 2: Add Stored Fields (Enable Document Retrieval) 📄
**Goal:** Retrieve original document content

#### Task 2.1: StoredFieldsWriter
**Estimate:** 2-3 days
**What it does:**
- Write original field values to .fdt (data) and .fdx (index) files
- Support text and numeric stored fields
- Simple format: docID → [field1, field2, ...] mapping

**Enables:**
- Store original values (title, description)
- Return documents in search results

#### Task 2.2: StoredFieldsReader
**Estimate:** 2-3 days
**What it does:**
- Read stored fields from .fdt/.fdx files
- Return original values for a document
- Random access by docID

**Enables:**
- document() method to retrieve full documents
- Search results with original content

### Phase 3: Add Deletions (Enable Updates) 🗑️
**Goal:** Support update and delete operations

#### Task 3.1: Live Docs Bitmap
**Estimate:** 1-2 days
**What it does:**
- Track deleted docs as bitmap
- Write .liv files (live docs)
- Read live docs on segment open

**Enables:**
- Mark documents as deleted
- Filter deleted docs in queries

#### Task 3.2: updateDocument() & deleteDocuments()
**Estimate:** 2-3 days
**What it does:**
- Implement IndexWriter deletion methods
- Apply deletions to segments
- Handle delete-by-term logic

**Enables:**
- Update existing documents
- Delete by query
- Proper CRUD operations

### Phase 4: Add Basic Merging (Enable Optimization) 🔄
**Goal:** Compact segments, remove deleted docs

#### Task 4.1: MergePolicy
**Estimate:** 2-3 days
**What it does:**
- Implement TieredMergePolicy
- Select segments to merge
- Decide when to merge

**Enables:**
- Intelligent merge decisions
- Balance segment count vs size

#### Task 4.2: SegmentMerger
**Estimate:** 3-4 days
**What it does:**
- Merge multiple segments into one
- Copy terms, postings, doc values
- Remove deleted documents

**Enables:**
- forceMerge() functionality
- Segment compaction
- Space reclamation

### Phase 5: Add Advanced Features (Production Ready) 🚀
**Goal:** Production-grade reliability and performance

#### Task 5.1: Reader Reopening (NRT)
**What it does:**
- Implement openIfChanged()
- Near-real-time search
- Efficient index refresh

#### Task 5.2: Norms Support
**What it does:**
- Write/read length normalization values
- Enable BM25 scoring

#### Task 5.3: Caching
**What it does:**
- Implement CacheHelper
- Cache expensive computations
- Improve query performance

## Estimated Timeline

| Phase | Tasks | Time Estimate | Cumulative |
|-------|-------|---------------|------------|
| Phase 1 (Doc Values) | 3 tasks | 5-8 days | 50-60% complete |
| Phase 2 (Stored Fields) | 2 tasks | 4-6 days | 70-75% complete |
| Phase 3 (Deletions) | 2 tasks | 3-5 days | 80-85% complete |
| Phase 4 (Merging) | 2 tasks | 5-7 days | 90-95% complete |
| Phase 5 (Advanced) | 3 tasks | 4-6 days | 95-100% complete |
| **Total** | **12 tasks** | **21-32 days** | **100% complete** |

## Prioritization Rationale

### Why Doc Values First?
- **Immediate value:** Enables filtering and sorting (most requested features)
- **Small scope:** Focused, achievable in ~1 week
- **No dependencies:** Can implement independently
- **Foundation:** Other features build on this

### Why Stored Fields Second?
- **High value:** Users want to retrieve documents
- **Completes read path:** Can now query AND retrieve
- **Moderate complexity:** Similar pattern to doc values

### Why Deletions Third?
- **CRUD completion:** Now have full Create, Read, Update, Delete
- **Real-world requirement:** Production systems need updates
- **Moderate complexity:** Bitmap management

### Why Merging Fourth?
- **Optimization:** Nice-to-have, not must-have
- **Higher complexity:** Involves multiple formats
- **Can defer:** Index works without it (just more segments)

### Why Advanced Features Last?
- **Polish:** Nice-to-have improvements
- **Lower priority:** Core functionality already works
- **Can iterate:** Add as needed

## Success Criteria

Each phase is "done" when:
- ✅ Implementation complete and tested
- ✅ Integration tests passing
- ✅ Can demonstrate working feature
- ✅ Code reviewed and documented
- ✅ CI passing

## Next Step

**Start with Phase 1, Task 1.1: NumericDocValues Writing**

This is the foundation for everything else and provides immediate value.
