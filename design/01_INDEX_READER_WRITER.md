# Index Reader & Writer Design
## Based on Lucene IndexReader/IndexWriter

Source references:
- Lucene IndexReader: `/home/ubuntu/opensearch_warmroom/lucene/lucene/core/src/java/org/apache/lucene/index/IndexReader.java`
- Lucene IndexWriter: `/home/ubuntu/opensearch_warmroom/lucene/lucene/core/src/java/org/apache/lucene/index/IndexWriter.java`
- Lucene LeafReader: `/home/ubuntu/opensearch_warmroom/lucene/lucene/core/src/java/org/apache/lucene/index/LeafReader.java`
- Lucene CompositeReader: `/home/ubuntu/opensearch_warmroom/lucene/lucene/core/src/java/org/apache/lucene/index/CompositeReader.java`
- Lucene DirectoryReader: `/home/ubuntu/opensearch_warmroom/lucene/lucene/core/src/java/org/apache/lucene/index/DirectoryReader.java`

## IndexReader Hierarchy

### Base IndexReader (Abstract)

```cpp
/**
 * IndexReader is an abstract base class providing read access to an index.
 *
 * Sealed hierarchy with two branches:
 * - LeafReader: atomic view of a single segment
 * - CompositeReader: composed view of multiple segments
 *
 * Thread-safe for concurrent reads.
 * Point-in-time snapshot semantics.
 */
class IndexReader {
public:
    virtual ~IndexReader() = default;

    // ==================== Factory Methods ====================

    /**
     * Open an index from directory
     * @return DirectoryReader for the index
     */
    static std::unique_ptr<IndexReader> open(Directory& dir);

    /**
     * Open with ExecutorService for parallel segment loading
     */
    static std::unique_ptr<IndexReader> open(Directory& dir,
                                             ExecutorService& executor);

    /**
     * Reopen if index changed
     * @return new reader if changed, nullptr if no changes
     */
    static std::unique_ptr<IndexReader> openIfChanged(IndexReader& old);

    // ==================== Context Access ====================

    /**
     * Returns leaf contexts for all segments
     * Each context contains: LeafReader, docBase, ord
     */
    virtual std::vector<LeafReaderContext> leaves() const = 0;

    /**
     * Get reader context (for caching)
     */
    virtual IndexReaderContext getContext() const = 0;

    // ==================== Statistics ====================

    /**
     * Total number of docs (includes deleted)
     */
    virtual int maxDoc() const = 0;

    /**
     * Number of live docs (excludes deleted)
     */
    virtual int numDocs() const = 0;

    /**
     * Check if doc is deleted
     */
    virtual bool hasDeletions() const = 0;

    // ==================== Caching Support ====================

    /**
     * Cache helper for reader-level caching
     * Returns nullptr if caching not supported
     */
    virtual CacheHelper* getReaderCacheHelper() const = 0;

    // ==================== Lifecycle ====================

    /**
     * Reference counting for lifecycle management
     */
    virtual void incRef() = 0;
    virtual bool tryIncRef() = 0;
    virtual void decRef() = 0;
    virtual int getRefCount() const = 0;

protected:
    /**
     * Ensure reader is still usable
     */
    void ensureOpen() const {
        if (closed_) {
            throw AlreadyClosedException("IndexReader is closed");
        }
    }

private:
    std::atomic<bool> closed_{false};
    std::atomic<int> refCount_{1};
};
```

### LeafReader (Abstract, Atomic Segment Reader)

```cpp
/**
 * LeafReader provides atomic read access to a single segment.
 * All doc IDs are relative to this segment [0, maxDoc()).
 *
 * Implements:
 * - Terms access via terms(field)
 * - Postings via postings(term, flags)
 * - Doc values via getNumericDocValues, etc.
 * - Stored fields via storedFields()
 * - Norms via getNormValues(field)
 */
class LeafReader : public IndexReader {
public:
    // ==================== Terms & Postings ====================

    /**
     * Get Terms for a field
     * @return Terms or nullptr if field doesn't exist/has no terms
     */
    virtual Terms* terms(const std::string& field) const = 0;

    /**
     * Get PostingsEnum for a term
     * @param term The term
     * @param flags Combination of PostingsEnum::FREQS, POSITIONS, OFFSETS, PAYLOADS
     * @return PostingsEnum or nullptr if term doesn't exist
     */
    virtual PostingsEnum* postings(const Term& term, int flags = PostingsEnum::FREQS) const {
        Terms* t = terms(term.field());
        if (!t) return nullptr;

        TermsEnum* te = t->iterator();
        if (!te->seekExact(term.bytes())) return nullptr;

        return te->postings(nullptr, flags);
    }

    /**
     * Get term frequency (doc count containing term)
     */
    virtual int docFreq(const Term& term) const {
        Terms* t = terms(term.field());
        if (!t) return 0;

        TermsEnum* te = t->iterator();
        if (!te->seekExact(term.bytes())) return 0;

        return te->docFreq();
    }

    /**
     * Get total term frequency (sum of term freqs across all docs)
     */
    virtual int64_t totalTermFreq(const Term& term) const {
        Terms* t = terms(term.field());
        if (!t) return 0;

        TermsEnum* te = t->iterator();
        if (!te->seekExact(term.bytes())) return 0;

        return te->totalTermFreq();
    }

    // ==================== Doc Values (Column Access) ====================

    /**
     * Numeric doc values (single numeric value per doc)
     */
    virtual NumericDocValues* getNumericDocValues(const std::string& field) const = 0;

    /**
     * Binary doc values (single byte[] per doc)
     */
    virtual BinaryDocValues* getBinaryDocValues(const std::string& field) const = 0;

    /**
     * Sorted doc values (sorted set of byte[] values, doc→ord mapping)
     */
    virtual SortedDocValues* getSortedDocValues(const std::string& field) const = 0;

    /**
     * Sorted set doc values (doc→multiple ords mapping)
     */
    virtual SortedSetDocValues* getSortedSetDocValues(const std::string& field) const = 0;

    /**
     * Sorted numeric doc values (doc→multiple numeric values)
     */
    virtual SortedNumericDocValues* getSortedNumericDocValues(const std::string& field) const = 0;

    // ==================== Stored Fields ====================

    /**
     * Get stored fields reader
     * Use StoredFieldsReader::document(docID, visitor) to retrieve
     */
    virtual StoredFieldsReader* storedFieldsReader() const = 0;

    // ==================== Norms ====================

    /**
     * Get normalization values for field
     * Returns nullptr if field doesn't have norms
     */
    virtual NumericDocValues* getNormValues(const std::string& field) const = 0;

    // ==================== Field Metadata ====================

    /**
     * Field infos for all indexed fields
     */
    virtual const FieldInfos& getFieldInfos() const = 0;

    /**
     * Get live docs (deleted docs bitmap)
     * Returns nullptr if no deletions
     */
    virtual const Bits* getLiveDocs() const = 0;

    // ==================== Points (Numeric/Geo Indexes) ====================

    /**
     * Point values for field (if indexed with PointsFormat)
     */
    virtual PointValues* getPointValues(const std::string& field) const = 0;

    // ==================== Vector Search ====================

    /**
     * KNN vector values for field
     */
    virtual VectorValues* getVectorValues(const std::string& field) const = 0;

    // ==================== Caching ====================

    /**
     * Core cache helper (for segment-level caching)
     * Invalidated only when segment is replaced
     */
    virtual CacheHelper* getCoreCacheHelper() const = 0;

    /**
     * Reader cache helper (invalidated on any change including deletes)
     */
    virtual CacheHelper* getReaderCacheHelper() const override = 0;

    // ==================== Context ====================

    LeafReaderContext getContext() const override {
        return LeafReaderContext(const_cast<LeafReader*>(this));
    }

    std::vector<LeafReaderContext> leaves() const override {
        return {getContext()};
    }
};
```

### CompositeReader (Abstract, Multi-Segment Reader)

```cpp
/**
 * CompositeReader composes multiple sub-readers.
 * Doc IDs are remapped: docID = docBase[i] + localDocID
 */
class CompositeReader : public IndexReader {
public:
    /**
     * Get sequential sub-readers
     */
    virtual std::vector<std::shared_ptr<IndexReader>> getSequentialSubReaders() const = 0;

    // ==================== Statistics (Aggregated) ====================

    int maxDoc() const override {
        int max = 0;
        for (const auto& sub : getSequentialSubReaders()) {
            max += sub->maxDoc();
        }
        return max;
    }

    int numDocs() const override {
        int num = 0;
        for (const auto& sub : getSequentialSubReaders()) {
            num += sub->numDocs();
        }
        return num;
    }

    bool hasDeletions() const override {
        for (const auto& sub : getSequentialSubReaders()) {
            if (sub->hasDeletions()) return true;
        }
        return false;
    }

    // ==================== Context ====================

    CompositeReaderContext getContext() const override;

    std::vector<LeafReaderContext> leaves() const override {
        std::vector<LeafReaderContext> result;
        int docBase = 0;
        int leafOrd = 0;

        for (const auto& sub : getSequentialSubReaders()) {
            for (const auto& ctx : sub->leaves()) {
                result.push_back(LeafReaderContext(
                    ctx.reader(),
                    docBase,
                    leafOrd++
                ));
                docBase += ctx.reader()->maxDoc();
            }
        }

        return result;
    }
};
```

### DirectoryReader (Concrete, Main Implementation)

```cpp
/**
 * DirectoryReader is a CompositeReader that opens indexes from a Directory.
 *
 * Key responsibilities:
 * - Read segments_N file
 * - Open SegmentReaders for each segment
 * - Track generation for reopening
 * - Manage segment lifecycle
 */
class DirectoryReader : public CompositeReader {
public:
    // ==================== Factory Methods ====================

    static std::unique_ptr<DirectoryReader> open(Directory& dir);

    static std::unique_ptr<DirectoryReader> open(Directory& dir,
                                                  ExecutorService& executor);

    static std::unique_ptr<DirectoryReader> openIfChanged(DirectoryReader& old);

    static std::unique_ptr<DirectoryReader> open(const IndexCommit& commit);

    // ==================== Implementation ====================

    std::vector<std::shared_ptr<IndexReader>> getSequentialSubReaders() const override {
        return subReaders_;
    }

    /**
     * Get directory
     */
    Directory& directory() const {
        return *directory_;
    }

    /**
     * Get index commit
     */
    const IndexCommit& getIndexCommit() const {
        return *commit_;
    }

    /**
     * Get version (increments on every change)
     */
    int64_t getVersion() const {
        return version_;
    }

    /**
     * Check if this reader is current
     */
    bool isCurrent() const;

    // ==================== Caching ====================

    CacheHelper* getReaderCacheHelper() const override {
        return cacheHelper_.get();
    }

protected:
    /**
     * Called on opening to load segments
     */
    virtual void doOpen();

    /**
     * Called when closing
     */
    virtual void doClose();

private:
    Directory* directory_;
    std::unique_ptr<IndexCommit> commit_;
    std::vector<std::shared_ptr<IndexReader>> subReaders_;
    int64_t version_;
    std::unique_ptr<CacheHelper> cacheHelper_;

    /**
     * Open all segments in parallel
     */
    void openSegments(ExecutorService* executor);
};
```

## IndexWriter

### IndexWriter (Main Writing Class)

```cpp
/**
 * IndexWriter creates and maintains an index.
 *
 * Thread safety:
 * - Multiple threads can add/update/delete documents concurrently
 * - Writer uses internal locking
 * - Single writer per index directory (enforced by write lock)
 *
 * Key operations:
 * - addDocument: Index new document
 * - updateDocument: Delete by term, then add new doc (atomic)
 * - deleteDocuments: Mark documents as deleted
 * - commit: Flush and persist changes
 * - forceMerge: Merge segments
 *
 * Memory management:
 * - RAMBufferSizeMB: flush when buffer exceeds threshold
 * - MaxBufferedDocs: flush when doc count exceeds threshold
 *
 * Based on: org.apache.lucene.index.IndexWriter
 */
class IndexWriter {
public:
    // ==================== Construction ====================

    /**
     * Create writer
     * @param dir Directory for index
     * @param config Configuration (copied, not shared)
     */
    IndexWriter(Directory& dir, const IndexWriterConfig& config);

    ~IndexWriter();

    // ==================== Document Operations ====================

    /**
     * Add a document
     * @return sequence number (transient, for ordering)
     */
    int64_t addDocument(const Document& doc);

    /**
     * Update document (delete by term, then add)
     * Atomic operation within same segment
     */
    int64_t updateDocument(const Term& term, const Document& doc);

    /**
     * Delete documents matching term
     */
    int64_t deleteDocuments(const Term& term);

    /**
     * Delete documents matching terms
     */
    int64_t deleteDocuments(const std::vector<Term>& terms);

    /**
     * Delete documents matching query
     */
    int64_t deleteDocuments(const Query& query);

    /**
     * Soft-delete via field update
     */
    int64_t softUpdateDocument(const Term& term, const Document& doc,
                              const std::string& softDeletesField);

    // ==================== Commit & Merge ====================

    /**
     * Commit changes (flush + sync)
     * @return sequence number
     */
    int64_t commit();

    /**
     * Flush without committing
     */
    void flush();

    /**
     * Rollback uncommitted changes
     */
    void rollback();

    /**
     * Force merge to at most maxNumSegments
     */
    void forceMerge(int maxNumSegments);

    /**
     * Force merge deletes
     */
    void forceMergeDeletes();

    /**
     * Wait for merges to complete
     */
    void waitForMerges();

    // ==================== Configuration ====================

    const IndexWriterConfig& getConfig() const {
        return config_;
    }

    /**
     * Change merge policy (thread-safe)
     */
    void setMergePolicy(std::unique_ptr<MergePolicy> mergePolicy);

    // ==================== Statistics ====================

    /**
     * Number of docs (including pending)
     */
    int64_t getDocStats() const;

    /**
     * Number of pending docs in RAM
     */
    int numRamDocs() const;

    /**
     * RAM used by buffered docs
     */
    int64_t ramBytesUsed() const;

    /**
     * Segment count
     */
    int numSegments() const;

    // ==================== Lifecycle ====================

    /**
     * Check if closed
     */
    bool isOpen() const {
        return !closed_;
    }

    /**
     * Close writer (commits if not already)
     */
    void close();

private:
    // Configuration
    Directory& directory_;
    IndexWriterConfig config_;
    std::unique_ptr<Lock> writeLock_;

    // Internal state
    std::unique_ptr<DocumentsWriter> docWriter_;
    std::unique_ptr<ReaderPool> readerPool_;
    std::unique_ptr<SegmentInfos> segmentInfos_;
    std::unique_ptr<MergeScheduler> mergeScheduler_;

    // Sequence numbers
    std::atomic<int64_t> nextSeqNo_{0};

    // Lifecycle
    std::atomic<bool> closed_{false};
    std::mutex commitLock_;

    // Helper methods
    void ensureOpen() const;
    void maybeMerge(const MergeTrigger& trigger);
    void doFlush();
    void publishFlushedSegments();
};
```

### IndexWriterConfig

```cpp
/**
 * Configuration for IndexWriter
 * Based on: org.apache.lucene.index.IndexWriterConfig
 */
class IndexWriterConfig {
public:
    // ==================== Open Modes ====================

    enum class OpenMode {
        CREATE,              // Create new index, overwrite existing
        APPEND,              // Open existing, fail if doesn't exist
        CREATE_OR_APPEND     // Create if missing, append otherwise
    };

    // ==================== Construction ====================

    IndexWriterConfig();

    // ==================== Codec ====================

    IndexWriterConfig& setCodec(std::unique_ptr<Codec> codec) {
        codec_ = std::move(codec);
        return *this;
    }

    Codec& getCodec() const {
        return codec_ ? *codec_ : Codec::getDefault();
    }

    // ==================== RAM Buffer ====================

    /**
     * RAM buffer size in MB (default: 16MB)
     * Flush when exceeded
     */
    IndexWriterConfig& setRAMBufferSizeMB(double mb) {
        ramBufferSizeMB_ = mb;
        return *this;
    }

    double getRAMBufferSizeMB() const {
        return ramBufferSizeMB_;
    }

    /**
     * Max buffered docs (default: disabled)
     * Flush when exceeded
     */
    IndexWriterConfig& setMaxBufferedDocs(int max) {
        maxBufferedDocs_ = max;
        return *this;
    }

    int getMaxBufferedDocs() const {
        return maxBufferedDocs_;
    }

    // ==================== Merge Policy ====================

    IndexWriterConfig& setMergePolicy(std::unique_ptr<MergePolicy> policy) {
        mergePolicy_ = std::move(policy);
        return *this;
    }

    MergePolicy& getMergePolicy() const {
        return *mergePolicy_;
    }

    // ==================== Merge Scheduler ====================

    IndexWriterConfig& setMergeScheduler(std::unique_ptr<MergeScheduler> scheduler) {
        mergeScheduler_ = std::move(scheduler);
        return *this;
    }

    MergeScheduler& getMergeScheduler() const {
        return *mergeScheduler_;
    }

    // ==================== Index Sort ====================

    /**
     * Sort documents within segments by this sort
     */
    IndexWriterConfig& setIndexSort(const Sort& sort) {
        indexSort_ = sort;
        return *this;
    }

    const Sort* getIndexSort() const {
        return indexSort_ ? &*indexSort_ : nullptr;
    }

    // ==================== Open Mode ====================

    IndexWriterConfig& setOpenMode(OpenMode mode) {
        openMode_ = mode;
        return *this;
    }

    OpenMode getOpenMode() const {
        return openMode_;
    }

    // ==================== Commit ====================

    /**
     * Set user data in commit
     */
    IndexWriterConfig& setCommitOnClose(bool commit) {
        commitOnClose_ = commit;
        return *this;
    }

    bool getCommitOnClose() const {
        return commitOnClose_;
    }

    // ==================== Similarity ====================

    IndexWriterConfig& setSimilarity(std::unique_ptr<Similarity> similarity) {
        similarity_ = std::move(similarity);
        return *this;
    }

    Similarity& getSimilarity() const {
        return *similarity_;
    }

private:
    std::unique_ptr<Codec> codec_;
    double ramBufferSizeMB_{16.0};
    int maxBufferedDocs_{-1};  // Disabled
    std::unique_ptr<MergePolicy> mergePolicy_;
    std::unique_ptr<MergeScheduler> mergeScheduler_;
    std::optional<Sort> indexSort_;
    OpenMode openMode_{OpenMode::CREATE_OR_APPEND};
    bool commitOnClose_{true};
    std::unique_ptr<Similarity> similarity_;
};
```

## Reader/Writer Context Classes

### LeafReaderContext

```cpp
/**
 * Context for a LeafReader
 * Contains: reader, docBase, ord
 */
struct LeafReaderContext {
    LeafReader* reader_;
    int docBase_;          // Global doc ID offset
    int ord_;              // Ordinal in parent

    LeafReaderContext(LeafReader* reader, int docBase = 0, int ord = 0)
        : reader_(reader), docBase_(docBase), ord_(ord) {}

    LeafReader* reader() const { return reader_; }
    int docBase() const { return docBase_; }
    int ord() const { return ord_; }
};
```

### IndexReaderContext & CompositeReaderContext

```cpp
class IndexReaderContext {
public:
    virtual ~IndexReaderContext() = default;
    virtual IndexReader* reader() const = 0;
    virtual std::vector<LeafReaderContext> leaves() const = 0;
    virtual bool isTopLevel() const = 0;
};

class CompositeReaderContext : public IndexReaderContext {
    // Contains children contexts
};
```

---

## Durability and Recovery

### Write-Ahead Log (WAL) Design

**Purpose**: Ensure durability and enable crash recovery.

**Based on**: Lucene's segments_N commit point mechanism.

#### Commit Point Format

**File**: `segments_N` (N increments with each commit)

```cpp
/**
 * Commit point file format
 *
 * Header:
 *   - Magic number (4 bytes): 0x3FD76C17
 *   - Format version (4 bytes)
 *   - Generation (8 bytes): Commit counter
 *   - Lucene version (string)
 *   - Commit timestamp (8 bytes)
 *
 * Segment list:
 *   - Num segments (4 bytes)
 *   - For each segment:
 *       - Segment name (string)
 *       - Segment ID (16 bytes UUID)
 *       - Max doc (4 bytes)
 *       - Del gen (8 bytes): Generation of .liv file
 *       - Field infos gen (8 bytes)
 *       - DocValues gen (8 bytes)
 *       - Codec name (string)
 *       - Diagnostics (map<string, string>)
 *       - Files (set<string>): All files belonging to this segment
 *
 * User data:
 *   - User-provided key-value pairs
 *
 * Footer:
 *   - Checksum (8 bytes): CRC64 of all above
 */
struct CommitPoint {
    int64_t generation;
    int64_t timestamp;
    std::vector<SegmentCommitInfo> segments;
    std::map<std::string, std::string> userData;

    // Write to directory
    void write(Directory& dir) const;

    // Read from directory (finds latest segments_N)
    static CommitPoint read(Directory& dir);
};
```

#### Two-Phase Commit Protocol

**Phase 1: Prepare** (Write data files)
```cpp
void IndexWriter::prepareCommit() {
    // 1. Flush all pending documents to segments
    flush(false, false);

    // 2. Finish all merges in progress (optional: can also wait)
    // (Background merges can continue)

    // 3. Write all segment files (postings, doc values, stored fields)
    //    - data.bin, index.bin, marks.mrk, etc.
    //    - These are written with temporary names or marked as uncommitted

    // 4. Write new segments_N+1 file
    //    - Contains list of all segments
    //    - Does NOT delete old segments_N yet

    // 5. Fsync all files (if configured)
    if (config_.getDurability() == Durability::SYNC) {
        dir_->sync(getAllNewFiles());
    }

    // 6. Store commit generation for Phase 2
    pendingCommit_ = currentGeneration_ + 1;
}
```

**Phase 2: Commit** (Make visible)
```cpp
void IndexWriter::commit() {
    if (!pendingCommit_.has_value()) {
        prepareCommit();
    }

    // 7. Write segments.gen file (atomic pointer to segments_N+1)
    writeSegmentsGen(pendingCommit_.value());

    // 8. Fsync segments.gen
    if (config_.getDurability() == Durability::SYNC) {
        dir_->sync({"segments.gen"});
    }

    // 9. Delete old segments_N file
    deleteStaleCommitPoints();

    // 10. Update in-memory state
    currentGeneration_ = pendingCommit_.value();
    pendingCommit_.reset();

    // 11. Delete unused segment files (from merges, deletes)
    deleteUnusedFiles();
}
```

#### Crash Recovery Algorithm

**On IndexWriter::open()**:
```cpp
static std::unique_ptr<IndexWriter> IndexWriter::open(
    Directory& dir,
    const IndexWriterConfig& config) {

    // 1. Find latest valid commit point
    CommitPoint latest = findLatestCommitPoint(dir);

    if (!latest.isValid()) {
        if (config.getOpenMode() == OpenMode::CREATE) {
            // Initialize empty index
            return createNewIndex(dir, config);
        } else {
            throw CorruptIndexException("No valid commit point found");
        }
    }

    // 2. Validate all segments referenced in commit point
    for (const auto& segmentInfo : latest.segments) {
        validateSegment(dir, segmentInfo);  // Check files exist, checksums OK
    }

    // 3. Delete uncommitted files (from crashed writes)
    deleteUncommittedFiles(dir, latest);

    // 4. Recover live docs (pending deletes)
    // If crash happened during delete, .liv files may be incomplete
    recoverLiveDocs(dir, latest);

    // 5. Initialize writer with recovered state
    auto writer = std::make_unique<IndexWriter>(dir, config, latest);

    return writer;
}

CommitPoint findLatestCommitPoint(Directory& dir) {
    // Read segments.gen to find latest generation
    int64_t generation = readSegmentsGen(dir);

    // Try to read segments_N (start from generation, walk backwards)
    for (int64_t gen = generation; gen >= 0; --gen) {
        std::string fileName = "segments_" + std::to_string(gen);
        if (!dir.fileExists(fileName)) continue;

        try {
            CommitPoint cp = CommitPoint::read(dir, fileName);
            if (cp.validate()) {  // Checksum OK, all files exist
                return cp;
            }
        } catch (const CorruptIndexException& e) {
            // Try previous generation
            LOG_WARN << "Corrupt commit point " << fileName << ": " << e.what();
        }
    }

    return CommitPoint::invalid();
}
```

#### Fsync Policy

```cpp
enum class Durability {
    NONE,        // No fsync (fast, but data loss on crash)
    COMMIT,      // Fsync on commit only (balanced)
    SYNC         // Fsync on every segment write (safest, slowest)
};

class IndexWriterConfig {
    Durability durability_ = Durability::COMMIT;
public:
    void setDurability(Durability d) { durability_ = d; }
    Durability getDurability() const { return durability_; }
};
```

#### File Naming Convention

**Committed files**:
- `segments_N`: Commit point (N = generation)
- `segments.gen`: Pointer to latest segments_N
- `_0.si`: Segment info for segment "_0"
- `_0_Lucene104_0.pos`: Postings for segment "_0"
- `_0_1.liv`: Live docs (generation 1) for segment "_0"

**Temporary files** (deleted on recovery):
- `_0.tmp`: Temporary segment being written
- `pending_segments_N`: Pending commit point

**Key Property**: All files are immutable after commit. Updates create new files.

---

## Concurrency Model

### Thread-Safety Guarantees

#### IndexWriter Thread-Safety

```cpp
/**
 * IndexWriter thread-safety model:
 *
 * THREAD-SAFE operations (can be called concurrently):
 * - addDocument(Document)       // Uses internal DWPT thread pool
 * - addDocuments(Iterable)      // Same
 * - deleteDocuments(Term)       // Buffered, thread-safe
 * - updateDocument(Term, Doc)   // delete + add, thread-safe
 * - numDocs() / maxDoc()        // Read-only stats
 *
 * SINGLE-THREADED operations (require external synchronization):
 * - commit()                    // Exclusive lock
 * - flush()                     // Exclusive lock
 * - forceMerge(int)             // Exclusive lock
 * - close()                     // Exclusive lock
 *
 * BACKGROUND operations (automatic, internal threads):
 * - Segment merging              // Concurrent with writes
 * - Segment flushing             // Concurrent with writes
 */
class IndexWriter {
private:
    // Main lock for structural changes
    mutable std::mutex commitLock_;

    // Document writer per thread (DWPT pattern)
    std::unique_ptr<DocumentsWriter> docWriter_;

    // Background merge threads
    std::unique_ptr<ConcurrentMergeScheduler> mergeScheduler_;
};
```

#### DocumentsWriterPerThread (DWPT) Pattern

**Design**: Each thread gets its own DocumentsWriter to avoid contention.

```cpp
/**
 * DocumentsWriter manages per-thread document buffers
 *
 * Pattern:
 * - Thread-local buffers (ThreadState)
 * - Lock-free in common case (addDocument)
 * - Flush happens per-thread (independent segments)
 */
class DocumentsWriter {
private:
    // Thread pool
    std::vector<std::unique_ptr<ThreadState>> threadStates_;

    // Active thread count
    std::atomic<int> activeThreads_{0};

    // Memory tracker
    std::atomic<size_t> bytesUsed_{0};
    size_t maxRAMBytes_;

public:
    /**
     * Add document (thread-safe, lock-free in common case)
     */
    int64_t addDocument(const Document& doc) {
        // 1. Get or create thread state for current thread
        ThreadState* state = getThreadState();

        // 2. Add to thread-local buffer (no lock!)
        int64_t seqNo = state->addDocument(doc);

        // 3. Update memory usage
        size_t used = bytesUsed_.fetch_add(state->bytesUsed());

        // 4. Check if flush needed (per-thread or global)
        if (state->bytesUsed() > perThreadRAMLimit_ ||
            used > maxRAMBytes_) {
            // Flush this thread's buffer to segment
            flushThreadState(state);
        }

        return seqNo;
    }

private:
    ThreadState* getThreadState() {
        // Thread-local storage
        static thread_local ThreadState* tls = nullptr;
        if (tls == nullptr) {
            std::lock_guard lock(threadStatesLock_);
            tls = threadStates_.emplace_back(
                std::make_unique<ThreadState>()).get();
        }
        return tls;
    }

    void flushThreadState(ThreadState* state) {
        // Atomically swap out the buffer
        auto buffer = state->swapBuffer();

        // Write segment asynchronously (no lock)
        SegmentCommitInfo info = writeSegment(buffer);

        // Add to pending segments (brief lock)
        {
            std::lock_guard lock(pendingSegmentsLock_);
            pendingSegments_.push_back(info);
        }
    }
};

struct ThreadState {
    std::vector<Document> documents_;
    size_t bytesUsed_ = 0;

    int64_t addDocument(const Document& doc) {
        documents_.push_back(doc);
        bytesUsed_ += estimateSize(doc);
        return globalSeqNo_.fetch_add(1);
    }

    std::vector<Document> swapBuffer() {
        std::vector<Document> result;
        result.swap(documents_);
        bytesUsed_ = 0;
        return result;
    }
};
```

#### IndexReader Thread-Safety

```cpp
/**
 * IndexReader thread-safety model:
 *
 * - IMMUTABLE after construction
 * - Point-in-time snapshot semantics
 * - Thread-safe for all read operations (no locks needed)
 * - Segments never modified, only replaced
 *
 * Key property: Readers never see uncommitted data
 */
class IndexReader {
    // All fields are const or atomic
    const std::vector<LeafReaderContext> leaves_;
    const int maxDoc_;
    const int numDocs_;

    // Reference counting for lifecycle
    std::atomic<int> refCount_{1};
};
```

#### Reader Lifecycle and Visibility

**Point-in-time snapshot**:
```cpp
// Thread 1: Open reader
auto reader1 = IndexReader::open(*dir);
// Sees: segments A, B, C

// Thread 2: Write and commit
writer->addDocument(doc);
writer->commit();
// Creates: segment D

// Thread 1: Still sees old snapshot
reader1->numDocs();  // Does NOT include doc from segment D!

// Thread 1: Must reopen to see new data
auto reader2 = IndexReader::openIfChanged(*reader1);
if (reader2) {
    // Sees: segments A, B, C, D
    reader1 = std::move(reader2);
}
```

**Segment reference counting**:
```cpp
/**
 * Segments are reference-counted to enable safe deletion
 *
 * Workflow:
 * 1. IndexWriter creates segment → refCount = 1
 * 2. IndexReader opens segment → incRef() → refCount = 2
 * 3. Merge makes segment obsolete, IndexWriter decRef() → refCount = 1
 * 4. IndexReader closes, decRef() → refCount = 0 → delete files
 */
class SegmentReader {
private:
    std::atomic<int> refCount_{1};

public:
    void incRef() {
        refCount_.fetch_add(1);
    }

    void decRef() {
        if (refCount_.fetch_sub(1) == 1) {
            // Last reference, safe to delete
            deleteSegmentFiles();
            delete this;
        }
    }
};
```

#### Commit vs Flush

**flush()**: Write pending documents to segments (NOT visible to readers)
```cpp
writer->addDocument(doc1);
writer->flush();  // Creates segment, but NOT committed
// Readers still don't see doc1!

writer->commit();  // NOW visible
```

**commit()**: Make all flushed segments visible
```cpp
writer->addDocument(doc2);
writer->commit();  // Flush + commit in one step
// Readers can now see doc2 (via openIfChanged)
```

#### Merge Concurrency

```cpp
/**
 * Merges run in background threads, concurrent with:
 * - Document writes (addDocument)
 * - Searches (IndexReader)
 *
 * Coordination:
 * - MergeScheduler owns thread pool
 * - OneMerge tracks merge progress
 * - Writer coordinates segment lifecycle
 */
class ConcurrentMergeScheduler : public MergeScheduler {
private:
    std::vector<std::thread> mergeThreads_;
    std::atomic<int> mergeThreadCount_{0};
    int maxMergeThreads_ = 3;

public:
    void merge(IndexWriter* writer, MergePolicy::OneMerge* merge) override {
        if (mergeThreadCount_ >= maxMergeThreads_) {
            // Run in calling thread (backpressure)
            doMerge(writer, merge);
        } else {
            // Run in background thread
            mergeThreadCount_++;
            mergeThreads_.emplace_back([this, writer, merge]() {
                doMerge(writer, merge);
                mergeThreadCount_--;
            });
        }
    }

private:
    void doMerge(IndexWriter* writer, MergePolicy::OneMerge* merge) {
        try {
            // Merge segments (CPU/IO intensive, no locks)
            SegmentCommitInfo merged = writer->mergeSegments(merge);

            // Atomically update index structure (brief lock)
            writer->commitMerge(merge, merged);

        } catch (const std::exception& e) {
            // Handle merge errors
            merge->setException(e);
        }
    }
};
```

---

## Delete Operations

### Delete APIs

```cpp
class IndexWriter {
public:
    /**
     * Delete documents matching a term
     *
     * Example: deleteDocuments(Term("id", "doc123"))
     *
     * Semantics:
     * - Deletes are buffered (not applied immediately)
     * - Deletes become visible on next commit()
     * - Applied to all segments (existing and future)
     */
    int64_t deleteDocuments(const Term& term);

    /**
     * Delete documents matching multiple terms (OR logic)
     */
    int64_t deleteDocuments(const std::vector<Term>& terms);

    /**
     * Delete documents matching a query
     *
     * Example: deleteDocuments(RangeQuery("price", 0, 10))
     *
     * More expensive than term-based delete (must run query)
     */
    int64_t deleteDocuments(std::unique_ptr<Query> query);

    /**
     * Update document (atomic delete + add)
     *
     * Example: updateDocument(Term("id", "doc123"), newDoc)
     *
     * Guarantees:
     * - Atomic operation (either both succeed or both fail)
     * - No concurrent reader sees partial state
     * - New doc has same term as deleted doc
     */
    int64_t updateDocument(const Term& updateTerm,
                           const Document& doc);

    /**
     * Update documents with same updateTerm
     */
    int64_t updateDocuments(const Term& updateTerm,
                            const std::vector<Document>& docs);
};
```

### LiveDocs Bitset

**Representation**: Deleted documents are tracked via bitset per segment.

```cpp
/**
 * LiveDocs tracks which documents in a segment are deleted
 *
 * File format: .liv (Lucene Live Docs)
 * - Header (magic, version, generation)
 * - BitSet: 1 = live, 0 = deleted
 * - Footer (checksum)
 *
 * Example:
 * Segment has maxDoc=10000
 * Delete doc 5, 100, 500
 * .liv file: BitSet[10000] with bits 5,100,500 cleared
 */
class LiveDocs {
private:
    std::unique_ptr<BitSet> bits_;  // 1 = live, 0 = deleted
    int numDeletes_ = 0;
    int64_t generation_;

public:
    bool isDeleted(int docID) const {
        return !bits_->get(docID);
    }

    void delete(int docID) {
        if (bits_->get(docID)) {
            bits_->clear(docID);
            numDeletes_++;
        }
    }

    int numDeletes() const { return numDeletes_; }

    // Write to .liv file
    void write(Directory& dir,
               const std::string& segmentName,
               int maxDoc);

    // Read from .liv file
    static std::unique_ptr<LiveDocs> read(
        Directory& dir,
        const std::string& segmentName,
        int64_t generation,
        int maxDoc);
};
```

### Delete Workflow

**Step 1: Buffer deletes**
```cpp
writer->deleteDocuments(Term("id", "doc123"));
// Stored in deleteQueue (per-thread buffer)
```

**Step 2: Apply on flush/commit**
```cpp
writer->commit();
// 1. Flush pending documents
// 2. Apply buffered deletes to ALL segments
// 3. Write .liv files for segments with deletes
// 4. Update segments_N with new .liv generations
```

**Step 3: Read with deletes**
```cpp
LeafReader* reader = ...;
Bits* liveDocs = reader->getLiveDocs();

for (int doc = 0; doc < reader->maxDoc(); ++doc) {
    if (liveDocs && liveDocs->get(doc) == false) {
        continue;  // Deleted, skip
    }

    // Process live document
}
```

### Delete Implementation Details

```cpp
/**
 * BufferedUpdates: Per-thread delete buffer
 */
class BufferedUpdates {
private:
    // Term-based deletes
    std::unordered_map<Term, int64_t> deleteTerms_;

    // Query-based deletes
    std::vector<std::unique_ptr<Query>> deleteQueries_;

    // Sequence number for ordering
    int64_t gen_;

public:
    void addDelete(const Term& term) {
        deleteTerms_[term] = gen_++;
    }

    void addDelete(std::unique_ptr<Query> query) {
        deleteQueries_.push_back(std::move(query));
    }

    /**
     * Apply deletes to a segment
     * Returns: LiveDocs with deletes applied
     */
    std::unique_ptr<LiveDocs> apply(LeafReader* segment) {
        auto liveDocs = std::make_unique<LiveDocs>(segment->maxDoc());

        // Apply term deletes (fast: posting list lookup)
        for (const auto& [term, gen] : deleteTerms_) {
            PostingsEnum* pe = segment->postings(term.field(), term.bytes());
            if (pe) {
                while (pe->nextDoc() != NO_MORE_DOCS) {
                    liveDocs->delete(pe->docID());
                }
            }
        }

        // Apply query deletes (slow: must run query)
        for (const auto& query : deleteQueries_) {
            Weight* weight = query->createWeight(segment);
            Scorer* scorer = weight->scorer(segment);
            if (scorer) {
                while (scorer->nextDoc() != NO_MORE_DOCS) {
                    liveDocs->delete(scorer->docID());
                }
            }
        }

        return liveDocs;
    }
};
```

### Merge-Time Compaction

**Problem**: Deleted documents waste space and slow down queries.

**Solution**: Remove deleted documents during merge.

```cpp
/**
 * Merge with delete compaction
 */
SegmentCommitInfo mergeSegments(const std::vector<SegmentCommitInfo>& segments) {
    int newMaxDoc = 0;

    // Calculate size without deletes
    for (const auto& seg : segments) {
        newMaxDoc += seg.info()->maxDoc() - seg.getDelCount();
    }

    // Create merged segment (only live docs)
    SegmentInfo newInfo("_merged", newMaxDoc, ...);

    // Copy data, skipping deleted docs
    int docIdOut = 0;
    for (const auto& seg : segments) {
        LeafReader* reader = openReader(seg);
        Bits* liveDocs = reader->getLiveDocs();

        for (int docIdIn = 0; docIdIn < reader->maxDoc(); ++docIdIn) {
            if (liveDocs && !liveDocs->get(docIdIn)) {
                continue;  // Skip deleted doc
            }

            // Copy doc from docIdIn to docIdOut
            copyDocument(reader, docIdIn, writer, docIdOut);
            docIdOut++;
        }
    }

    assert(docIdOut == newMaxDoc);
    return SegmentCommitInfo(newInfo);
}
```

### updateDocument Implementation

**Atomic update = delete + add**:
```cpp
int64_t IndexWriter::updateDocument(const Term& updateTerm,
                                     const Document& doc) {
    // 1. Buffer delete (not applied yet)
    deleteDocuments(updateTerm);

    // 2. Add new document
    int64_t seqNo = addDocument(doc);

    // Key: Both happen in same thread, same sequence
    // Readers see either old doc or new doc, never neither

    return seqNo;
}
```

**Visibility**:
```cpp
// Before commit
reader->document(5);  // Returns old doc

writer->updateDocument(Term("id", "5"), newDoc);
writer->commit();

// After commit (with openIfChanged)
reader->document(5);  // Returns new doc
```

---

## Memory Management

### Memory Budgets

#### IndexWriter Memory Budget

```cpp
class IndexWriterConfig {
private:
    // RAM buffer size for document indexing
    double ramBufferSizeMB_ = 16.0;  // Default: 16MB

    // Max buffered documents (alternative to RAM limit)
    int maxBufferedDocs_ = -1;  // Disabled by default

    // Per-thread RAM limit (DWPT)
    double perThreadHardLimitMB_ = 1945.0;  // ~2GB per thread

public:
    /**
     * Set RAM buffer size for document indexing
     *
     * When exceeded, flush pending documents to segment.
     * Larger = fewer segments, better merge performance
     * Smaller = less memory, more frequent flushes
     *
     * Recommendation: 16-128MB for typical workloads
     */
    void setRAMBufferSizeMB(double mb) {
        if (mb <= 0) {
            throw std::invalid_argument("RAM buffer must be positive");
        }
        ramBufferSizeMB_ = mb;
    }

    double getRAMBufferSizeMB() const { return ramBufferSizeMB_; }
};
```

#### Query Execution Memory Budget

```cpp
/**
 * QueryContext: Per-query memory budget
 */
class QueryContext {
private:
    size_t maxMemoryBytes_ = 100 * 1024 * 1024;  // 100MB default
    std::atomic<size_t> memoryUsed_{0};

public:
    /**
     * Allocate memory for query execution
     * Throws MemoryLimitExceeded if budget exceeded
     */
    void* allocate(size_t bytes) {
        size_t used = memoryUsed_.fetch_add(bytes);
        if (used + bytes > maxMemoryBytes_) {
            memoryUsed_.fetch_sub(bytes);  // Rollback
            throw MemoryLimitExceededException(
                "Query memory limit exceeded: " +
                std::to_string(maxMemoryBytes_ / 1024 / 1024) + "MB");
        }
        return malloc(bytes);
    }

    void deallocate(void* ptr, size_t bytes) {
        free(ptr);
        memoryUsed_.fetch_sub(bytes);
    }

    size_t getMemoryUsed() const { return memoryUsed_; }
    size_t getMemoryLimit() const { return maxMemoryBytes_; }
};
```

### Buffer Pooling

#### Score Buffer Pool (for SIMD queries)

```cpp
/**
 * Buffer pool for reusing score arrays in SIMD queries
 * Avoids repeated allocation/initialization of large float arrays
 */
class ScoreBufferPool {
private:
    struct Buffer {
        std::vector<float> data;
        bool inUse = false;
    };

    std::vector<Buffer> pool_;
    std::mutex mutex_;
    size_t bufferSize_;

public:
    explicit ScoreBufferPool(size_t bufferSize, size_t initialPoolSize = 10)
        : bufferSize_(bufferSize) {
        pool_.reserve(initialPoolSize);
        for (size_t i = 0; i < initialPoolSize; ++i) {
            pool_.push_back({std::vector<float>(bufferSize, -INFINITY), false});
        }
    }

    /**
     * Acquire buffer from pool (or allocate new)
     */
    std::vector<float>* acquire() {
        std::lock_guard lock(mutex_);

        // Find unused buffer
        for (auto& buf : pool_) {
            if (!buf.inUse) {
                buf.inUse = true;
                // Reset to -INFINITY (reuse without reallocation)
                std::fill(buf.data.begin(), buf.data.end(), -INFINITY);
                return &buf.data;
            }
        }

        // All buffers in use, allocate new
        pool_.push_back({std::vector<float>(bufferSize_, -INFINITY), true});
        return &pool_.back().data;
    }

    /**
     * Release buffer back to pool
     */
    void release(std::vector<float>* buffer) {
        std::lock_guard lock(mutex_);

        for (auto& buf : pool_) {
            if (&buf.data == buffer) {
                buf.inUse = false;
                return;
            }
        }
    }
};

// Thread-local pool (one per thread, no lock needed)
thread_local ScoreBufferPool tlsBufferPool(100000);  // 100K docs per window
```

#### Column Buffer Pool (for COW columns)

```cpp
/**
 * Arena allocator for temporary column operations
 * Avoids COW explosion during filter/sort chains
 */
class ColumnArena {
private:
    std::vector<std::unique_ptr<uint8_t[]>> chunks_;
    size_t chunkSize_ = 1024 * 1024;  // 1MB chunks
    size_t currentOffset_ = 0;

public:
    /**
     * Allocate from arena (no individual free, bulk reset)
     */
    void* allocate(size_t bytes) {
        if (currentOffset_ + bytes > chunkSize_) {
            // Allocate new chunk
            chunks_.emplace_back(std::make_unique<uint8_t[]>(chunkSize_));
            currentOffset_ = 0;
        }

        void* ptr = chunks_.back().get() + currentOffset_;
        currentOffset_ += bytes;
        return ptr;
    }

    /**
     * Reset arena (free all at once)
     */
    void reset() {
        chunks_.clear();
        currentOffset_ = 0;
    }

    size_t getTotalAllocated() const {
        return chunks_.size() * chunkSize_;
    }
};

// Usage in query execution
QueryContext ctx;
ColumnArena arena;

// Filter operation (no COW, use arena)
auto filtered = column->filterIntoArena(mask, arena);

// Sort operation (no COW, use arena)
auto sorted = filtered->permuteIntoArena(indices, arena);

// Compute result
float avg = computeAverage(sorted);

// Free all at once
arena.reset();
```

### OOM Handling Strategy

```cpp
/**
 * OOM handling levels:
 * 1. ABORT: Crash immediately (default for correctness)
 * 2. GRACEFUL: Try to complete current operation, then refuse new work
 * 3. BEST_EFFORT: Spill to disk, reduce quality
 */
enum class OOMStrategy {
    ABORT,         // Safest: abort immediately, avoid corruption
    GRACEFUL,      // Try to finish in-progress operations
    BEST_EFFORT    // Degrade quality (e.g., skip SIMD, use streaming)
};

class IndexWriterConfig {
    OOMStrategy oomStrategy_ = OOMStrategy::ABORT;

public:
    void setOOMStrategy(OOMStrategy strategy) {
        oomStrategy_ = strategy;
    }
};

/**
 * Handle OOM in IndexWriter
 */
void IndexWriter::handleOOM() {
    switch (config_.getOOMStrategy()) {
        case OOMStrategy::ABORT:
            // Log and crash (preserves index integrity)
            LOG_FATAL << "Out of memory, aborting to prevent corruption";
            std::abort();

        case OOMStrategy::GRACEFUL:
            // Finish current flush/commit, refuse new documents
            setState(State::OOM);
            LOG_ERROR << "Out of memory, entering read-only mode";
            // New addDocument() calls will throw OOMException
            break;

        case OOMStrategy::BEST_EFFORT:
            // Force flush to free memory
            flush(true, true);  // forceFlush=true, waitForMerges=true
            break;
    }
}

/**
 * Handle OOM in query execution
 */
TopDocs IndexSearcher::search(Query* query, int n) {
    try {
        return searchInternal(query, n);
    } catch (const std::bad_alloc& e) {
        // Fallback to streaming mode (no buffer pooling)
        LOG_WARN << "OOM during search, falling back to streaming mode";
        return searchStreaming(query, n);
    }
}
```

### Memory Profiling Hooks

```cpp
/**
 * Memory profiling interface for monitoring
 */
class MemoryProfiler {
public:
    virtual void recordAllocation(const char* component,
                                   size_t bytes) = 0;
    virtual void recordDeallocation(const char* component,
                                     size_t bytes) = 0;
};

class IndexWriter {
private:
    std::shared_ptr<MemoryProfiler> memoryProfiler_;

    void allocateInternal(size_t bytes, const char* purpose) {
        void* ptr = malloc(bytes);
        if (memoryProfiler_) {
            memoryProfiler_->recordAllocation(purpose, bytes);
        }
        return ptr;
    }
};

// Example: Prometheus-based profiler
class PrometheusMemoryProfiler : public MemoryProfiler {
    void recordAllocation(const char* component, size_t bytes) override {
        prometheus::Counter& counter =
            metrics_["lucene_memory_allocated_bytes"]
                   .Add({{"component", component}});
        counter.Increment(bytes);
    }
};
```

---

## Usage Examples

### Opening and Reading

```cpp
// Open index
auto dir = FSDirectory::open("/path/to/index");
auto reader = IndexReader::open(*dir);

// Search across all segments
for (const auto& ctx : reader->leaves()) {
    LeafReader* leaf = ctx.reader();

    // Get terms for field
    Terms* terms = leaf->terms("title");
    if (!terms) continue;

    // Iterate terms
    TermsEnum* te = terms->iterator();
    while (te->next()) {
        BytesRef term = te->term();
        int docFreq = te->docFreq();

        // Get postings
        PostingsEnum* pe = te->postings(nullptr, PostingsEnum::FREQS);
        while (pe->nextDoc() != DocIdSetIterator::NO_MORE_DOCS) {
            int docID = pe->docID();
            int freq = pe->freq();
        }
    }

    // Get doc values (column access)
    NumericDocValues* ndv = leaf->getNumericDocValues("price");
    for (int doc = 0; doc < leaf->maxDoc(); ++doc) {
        if (ndv->advanceExact(doc)) {
            int64_t price = ndv->longValue();
        }
    }
}
```

### Writing

```cpp
// Create writer
auto dir = FSDirectory::open("/path/to/index");

IndexWriterConfig config;
config.setRAMBufferSizeMB(128.0);
config.setOpenMode(IndexWriterConfig::OpenMode::CREATE_OR_APPEND);

IndexWriter writer(*dir, config);

// Add documents
Document doc;
doc.add(TextField("title", "Lucene++ Design", Field::Store::YES));
doc.add(NumericDocValuesField("price", 99));
writer.addDocument(doc);

// Commit
writer.commit();

// Force merge
writer.forceMerge(1);

writer.close();
```
