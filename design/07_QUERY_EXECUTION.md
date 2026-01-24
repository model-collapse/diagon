# Query Execution Design
## Based on Lucene Query/Weight/Scorer Framework

Source references:
- `org.apache.lucene.search.Query`
- `org.apache.lucene.search.Weight`
- `org.apache.lucene.search.Scorer`
- `org.apache.lucene.search.IndexSearcher`
- `org.apache.lucene.search.Collector`
- `org.apache.lucene.search.TopScoreDocCollector`

## Overview

Lucene uses a three-level query execution model:
1. **Query**: High-level query representation (reusable, cacheable)
2. **Weight**: Compiled query with statistics (per-IndexSearcher)
3. **Scorer**: Iterator over matching docs with scores (per-segment)

This separation enables:
- Query reuse across searches
- Weight caching for repeated queries
- Parallel segment execution

## Query (Abstract Base)

```cpp
/**
 * Query is the abstract base for all queries.
 *
 * Queries are immutable and reusable.
 * createWeight() compiles query for a specific IndexSearcher.
 *
 * Based on: org.apache.lucene.search.Query
 */
class Query {
public:
    virtual ~Query() = default;

    // ==================== Weight Creation ====================

    /**
     * Create weight for this query
     * @param searcher IndexSearcher executing the query
     * @param scoreMode How scores will be consumed
     * @param boost Boost factor for scores
     * @return Weight for execution
     */
    virtual std::unique_ptr<Weight> createWeight(
        IndexSearcher& searcher,
        ScoreMode scoreMode,
        float boost) const = 0;

    // ==================== Rewriting ====================

    /**
     * Rewrite query for optimization
     * @param reader IndexReader for statistics
     * @return Rewritten query (may be this)
     */
    virtual std::unique_ptr<Query> rewrite(IndexReader& reader) const {
        return clone();
    }

    // ==================== Utilities ====================

    /**
     * String representation
     */
    virtual std::string toString(const std::string& field) const = 0;

    /**
     * Query equality
     */
    virtual bool equals(const Query& other) const = 0;

    /**
     * Hash code for caching
     */
    virtual size_t hashCode() const = 0;

    /**
     * Clone query
     */
    virtual std::unique_ptr<Query> clone() const = 0;

protected:
    /**
     * Helper: combine boost values
     */
    static float combineBoost(float boost1, float boost2) {
        return boost1 * boost2;
    }
};
```

## ScoreMode

```cpp
/**
 * How scores will be consumed by collector
 *
 * Based on: org.apache.lucene.search.ScoreMode
 */
enum class ScoreMode {
    /**
     * Scores are needed and must be computed
     */
    COMPLETE,

    /**
     * Only doc IDs needed, scores not required
     * Enables optimizations (e.g., skip score computation)
     */
    COMPLETE_NO_SCORES,

    /**
     * Top-scoring docs needed
     * Enables early termination optimizations
     */
    TOP_SCORES
};
```

## Weight (Abstract Base)

```cpp
/**
 * Weight is the compiled form of a Query.
 *
 * Contains statistics and can create Scorers for segments.
 * One Weight per IndexSearcher, reusable across segments.
 *
 * Based on: org.apache.lucene.search.Weight
 */
class Weight {
public:
    virtual ~Weight() = default;

    // ==================== Scorer Creation ====================

    /**
     * Create scorer for segment
     * @param context Segment to search
     * @return Scorer or nullptr if no matches possible
     */
    virtual std::unique_ptr<Scorer> scorer(const LeafReaderContext& context) const = 0;

    /**
     * Create bulk scorer (optimized for collecting all hits)
     * @param context Segment to search
     * @return BulkScorer or nullptr
     */
    virtual std::unique_ptr<BulkScorer> bulkScorer(const LeafReaderContext& context) const {
        auto scorer = this->scorer(context);
        if (!scorer) {
            return nullptr;
        }
        return std::make_unique<DefaultBulkScorer>(std::move(scorer));
    }

    // ==================== Statistics ====================

    /**
     * Does this weight require scores?
     */
    virtual bool isCacheable(const LeafReaderContext& context) const {
        return true;
    }

    // ==================== Explanation ====================

    /**
     * Explain score for document
     * For debugging/relevance tuning
     */
    virtual Explanation explain(const LeafReaderContext& context, int doc) const = 0;

    // ==================== Utilities ====================

    /**
     * Get parent query
     */
    virtual const Query& getQuery() const = 0;
};
```

## Scorer (Abstract Base)

```cpp
/**
 * Scorer iterates over matching documents with scores.
 *
 * Extends DocIdSetIterator with scoring capability.
 * One Scorer per segment.
 *
 * Based on: org.apache.lucene.search.Scorer
 */
class Scorer : public DocIdSetIterator {
public:
    /**
     * Current document score
     * Only valid after nextDoc() or advance()
     */
    virtual float score() const = 0;

    /**
     * Get smoothing score
     * Used for global statistics in distributed search
     */
    virtual float smoothingScore(int docId) const {
        return 0.0f;
    }

    /**
     * Get parent weight
     */
    virtual const Weight& getWeight() const = 0;

    /**
     * Get children (for complex queries)
     */
    virtual std::vector<ChildScorable> getChildren() const {
        return {};
    }

    // ==================== Two-Phase Iteration ====================

    /**
     * Get two-phase iterator if available
     * Useful for expensive matches() checks
     */
    virtual TwoPhaseIterator* twoPhaseIterator() {
        return nullptr;
    }

    // ==================== Score Upper Bounds ====================

    /**
     * Maximum possible score
     * Used for early termination (WAND)
     */
    virtual float getMaxScore(int upTo) const {
        return std::numeric_limits<float>::max();
    }

    /**
     * Shallow advance to doc >= target
     * Cheaper than advance(), doesn't position for scoring
     */
    virtual int advanceShallow(int target) {
        return advance(target);
    }
};

struct ChildScorable {
    Scorer* child;
    std::string relationship;  // "MUST", "SHOULD", "MUST_NOT"
};
```

## DocIdSetIterator

```cpp
/**
 * Iterator over doc IDs
 *
 * Based on: org.apache.lucene.search.DocIdSetIterator
 */
class DocIdSetIterator {
public:
    static constexpr int NO_MORE_DOCS = std::numeric_limits<int>::max();

    virtual ~DocIdSetIterator() = default;

    /**
     * Current doc ID
     */
    virtual int docID() const = 0;

    /**
     * Advance to next doc
     * @return next doc ID or NO_MORE_DOCS
     */
    virtual int nextDoc() = 0;

    /**
     * Advance to doc >= target
     * @param target Minimum doc ID
     * @return doc ID >= target or NO_MORE_DOCS
     */
    virtual int advance(int target) = 0;

    /**
     * Estimated cost (number of docs)
     * Used for query optimization
     */
    virtual int64_t cost() const = 0;
};
```

## IndexSearcher

```cpp
/**
 * IndexSearcher executes queries against an IndexReader.
 *
 * Based on: org.apache.lucene.search.IndexSearcher
 */
class IndexSearcher {
public:
    explicit IndexSearcher(IndexReader& reader)
        : reader_(reader)
        , executor_(nullptr) {}

    IndexSearcher(IndexReader& reader, ExecutorService* executor)
        : reader_(reader)
        , executor_(executor) {}

    // ==================== Search Methods ====================

    /**
     * Search and collect top docs
     */
    TopDocs search(const Query& query, int n) {
        return search(query, n, Sort::RELEVANCE);
    }

    /**
     * Search with custom sort
     */
    TopDocs search(const Query& query, int n, const Sort& sort) {
        auto collector = createTopDocsCollector(n, sort);
        search(query, collector.get());
        return collector->topDocs();
    }

    /**
     * Search with custom collector
     */
    void search(const Query& query, Collector* collector) {
        // Rewrite query
        auto rewritten = query.rewrite(reader_);

        // Create weight
        auto weight = rewritten->createWeight(
            *this,
            collector->scoreMode(),
            1.0f  // Top-level boost
        );

        // Search all segments
        searchLeaves(weight.get(), collector);
    }

    /**
     * Count matching docs (optimized, no scoring)
     */
    int count(const Query& query) {
        auto rewritten = query.rewrite(reader_);
        auto weight = rewritten->createWeight(*this, ScoreMode::COMPLETE_NO_SCORES, 1.0f);

        int total = 0;
        for (const auto& ctx : reader_.leaves()) {
            total += count(weight.get(), ctx);
        }
        return total;
    }

    // ==================== Statistics ====================

    /**
     * Collection statistics (for scoring)
     */
    CollectionStatistics collectionStatistics(const std::string& field) const {
        int64_t docCount = 0;
        int64_t sumTotalTermFreq = 0;
        int64_t sumDocFreq = 0;

        for (const auto& ctx : reader_.leaves()) {
            Terms* terms = ctx.reader()->terms(field);
            if (terms) {
                docCount += terms->getDocCount();
                sumTotalTermFreq += terms->getSumTotalTermFreq();
                sumDocFreq += terms->getSumDocFreq();
            }
        }

        return CollectionStatistics(field, reader_.maxDoc(), docCount,
                                    sumTotalTermFreq, sumDocFreq);
    }

    /**
     * Term statistics (for scoring)
     */
    TermStatistics termStatistics(const Term& term) const {
        int64_t docFreq = 0;
        int64_t totalTermFreq = 0;

        for (const auto& ctx : reader_.leaves()) {
            TermsEnum* te = ctx.reader()->terms(term.field())->iterator();
            if (te->seekExact(term.bytes())) {
                docFreq += te->docFreq();
                totalTermFreq += te->totalTermFreq();
            }
        }

        return TermStatistics(term.bytes(), docFreq, totalTermFreq);
    }

    // ==================== Utilities ====================

    IndexReader& getIndexReader() { return reader_; }

    Similarity& getSimilarity() { return *similarity_; }

    void setSimilarity(std::unique_ptr<Similarity> similarity) {
        similarity_ = std::move(similarity);
    }

private:
    IndexReader& reader_;
    ExecutorService* executor_;
    std::unique_ptr<Similarity> similarity_;

    void searchLeaves(Weight* weight, Collector* collector) {
        auto leaves = reader_.leaves();

        if (executor_ && leaves.size() > 1) {
            // Parallel search
            searchLeavesParallel(weight, collector, leaves);
        } else {
            // Sequential search
            for (const auto& ctx : leaves) {
                searchLeaf(weight, ctx, collector->getLeafCollector(ctx));
            }
        }
    }

    void searchLeaf(Weight* weight, const LeafReaderContext& ctx,
                   LeafCollector* collector) {
        auto scorer = weight->scorer(ctx);
        if (!scorer) return;

        collector->setScorer(scorer.get());

        auto iterator = scorer->twoPhaseIterator();
        if (iterator) {
            // Two-phase iteration
            searchWithTwoPhase(iterator, collector);
        } else {
            // Standard iteration
            searchWithScorer(scorer.get(), collector);
        }
    }

    void searchWithScorer(Scorer* scorer, LeafCollector* collector) {
        int doc = scorer->nextDoc();
        while (doc != DocIdSetIterator::NO_MORE_DOCS) {
            collector->collect(doc);
            doc = scorer->nextDoc();
        }
    }

    int count(Weight* weight, const LeafReaderContext& ctx) {
        auto scorer = weight->scorer(ctx);
        if (!scorer) return 0;

        int count = 0;
        int doc = scorer->nextDoc();
        while (doc != DocIdSetIterator::NO_MORE_DOCS) {
            count++;
            doc = scorer->nextDoc();
        }
        return count;
    }
};
```

## Collector

```cpp
/**
 * Collector processes matching documents during search.
 *
 * Based on: org.apache.lucene.search.Collector
 */
class Collector {
public:
    virtual ~Collector() = default;

    /**
     * Get leaf collector for segment
     */
    virtual LeafCollector* getLeafCollector(const LeafReaderContext& context) = 0;

    /**
     * How will scores be used?
     */
    virtual ScoreMode scoreMode() const = 0;
};

/**
 * Collects docs from a single segment
 */
class LeafCollector {
public:
    virtual ~LeafCollector() = default;

    /**
     * Set scorer (provides scores for docs)
     */
    virtual void setScorer(Scorer* scorer) = 0;

    /**
     * Collect document
     * @param doc Document ID (segment-relative)
     */
    virtual void collect(int doc) = 0;

    /**
     * Collect range of docs (bulk collection)
     */
    virtual void collectRange(int min, int max) {
        for (int doc = min; doc < max; ++doc) {
            collect(doc);
        }
    }
};
```

## TopDocsCollector

```cpp
/**
 * Collects top-N highest scoring documents
 *
 * Based on: org.apache.lucene.search.TopScoreDocCollector
 */
class TopScoreDocCollector : public Collector {
public:
    explicit TopScoreDocCollector(int numHits)
        : numHits_(numHits)
        , pq_(numHits) {}

    LeafCollector* getLeafCollector(const LeafReaderContext& context) override {
        return new TopScoreLeafCollector(this, context.docBase());
    }

    ScoreMode scoreMode() const override {
        return ScoreMode::TOP_SCORES;
    }

    /**
     * Get top docs
     */
    TopDocs topDocs() const {
        std::vector<ScoreDoc> hits;

        // Extract from priority queue
        while (!pq_.empty()) {
            hits.push_back(pq_.top());
            pq_.pop();
        }

        // Reverse to get highest scores first
        std::reverse(hits.begin(), hits.end());

        TopDocs result;
        result.totalHits = totalHits_;
        result.scoreDocs = std::move(hits);

        return result;
    }

private:
    class TopScoreLeafCollector : public LeafCollector {
    public:
        TopScoreLeafCollector(TopScoreDocCollector* parent, int docBase)
            : parent_(parent), docBase_(docBase) {}

        void setScorer(Scorer* scorer) override {
            scorer_ = scorer;
        }

        void collect(int doc) override {
            float score = scorer_->score();
            parent_->totalHits_++;

            if (parent_->pq_.size() < parent_->numHits_) {
                parent_->pq_.push(ScoreDoc{docBase_ + doc, score});
            } else if (score > parent_->pq_.top().score) {
                parent_->pq_.pop();
                parent_->pq_.push(ScoreDoc{docBase_ + doc, score});
            }
        }

    private:
        TopScoreDocCollector* parent_;
        Scorer* scorer_;
        int docBase_;
    };

    int numHits_;
    int64_t totalHits_{0};
    mutable std::priority_queue<ScoreDoc, std::vector<ScoreDoc>, std::greater<ScoreDoc>> pq_;
};
```

## TopDocs & ScoreDoc

```cpp
/**
 * Top search results
 */
struct ScoreDoc {
    int doc;       // Document ID (global)
    float score;   // Score

    bool operator>(const ScoreDoc& other) const {
        return score < other.score;  // Min-heap (lowest scores first)
    }
};

struct TopDocs {
    int64_t totalHits;              // Total matching documents
    std::vector<ScoreDoc> scoreDocs;  // Top-N results

    TopDocs() : totalHits(0) {}
};
```

## Usage Example

```cpp
// Open index
auto reader = IndexReader::open(*dir);
IndexSearcher searcher(*reader);

// Create query
TermQuery query("title", "lucene");

// Search top 10
TopDocs results = searcher.search(query, 10);

std::cout << "Total hits: " << results.totalHits << std::endl;
for (const auto& hit : results.scoreDocs) {
    std::cout << "Doc " << hit.doc << " score=" << hit.score << std::endl;

    // Retrieve document
    Document doc = reader->document(hit.doc);
    std::cout << "  Title: " << doc.get("title") << std::endl;
}

// Count matches (optimized)
int count = searcher.count(query);
std::cout << "Matching documents: " << count << std::endl;
```

---

## Query Timeout and Cancellation

### Motivation

Long-running queries can monopolize resources and degrade system responsiveness. Timeout mechanisms enable:
- **Resource protection**: Terminate runaway queries before exhausting memory/CPU
- **SLA enforcement**: Ensure queries complete within acceptable time bounds
- **Graceful degradation**: Return partial results or fail fast
- **User responsiveness**: Allow users to cancel slow queries

### QueryContext with Timeout Support

```cpp
/**
 * QueryContext carries per-query state including timeout and cancellation.
 */
class QueryContext {
public:
    QueryContext()
        : startTime_(std::chrono::steady_clock::now()),
          timeoutMs_(0),
          cancelled_(false),
          memoryUsed_(0),
          memoryLimit_(100 * 1024 * 1024)  // 100MB default
    {}

    /**
     * Set query timeout in milliseconds
     */
    void setTimeout(int64_t timeoutMs) {
        timeoutMs_ = timeoutMs;
    }

    /**
     * Check if query has exceeded timeout
     * Returns true if timeout is set and exceeded
     */
    bool isTimeout() const {
        if (timeoutMs_ <= 0) return false;

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - startTime_
        ).count();

        return elapsed >= timeoutMs_;
    }

    /**
     * Cancel the query (thread-safe)
     */
    void cancel() {
        cancelled_.store(true, std::memory_order_release);
    }

    /**
     * Check if query has been cancelled (thread-safe)
     */
    bool isCancelled() const {
        return cancelled_.load(std::memory_order_acquire);
    }

    /**
     * Check if query should stop (timeout OR cancellation)
     */
    bool shouldStop() const {
        return isTimeout() || isCancelled();
    }

    /**
     * Get elapsed time in milliseconds
     */
    int64_t getElapsedMs() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            now - startTime_
        ).count();
    }

    // Memory tracking (see Module 01)
    void trackMemoryAllocation(size_t bytes);
    size_t getMemoryUsed() const { return memoryUsed_; }
    size_t getMemoryLimit() const { return memoryLimit_; }

private:
    std::chrono::steady_clock::time_point startTime_;
    int64_t timeoutMs_;
    std::atomic<bool> cancelled_;
    size_t memoryUsed_;
    size_t memoryLimit_;
};
```

### Periodic Timeout Checks

**Challenge**: Checking timeout on every document is too expensive (millions of checks per query).

**Solution**: Check timeout periodically (e.g., every 1024 documents).

```cpp
class DocIdSetIterator {
protected:
    QueryContext* queryContext_ = nullptr;
    int checkCounter_ = 0;
    static constexpr int CHECK_INTERVAL = 1024;

public:
    void setQueryContext(QueryContext* ctx) {
        queryContext_ = ctx;
    }

    /**
     * Advance to next document with periodic timeout check
     */
    int32_t nextDoc() {
        int32_t doc = doNextDoc();  // Subclass implementation

        // Check timeout periodically
        if (queryContext_ && (++checkCounter_ & (CHECK_INTERVAL - 1)) == 0) {
            if (queryContext_->shouldStop()) {
                throw QueryTimeoutException(
                    "Query timeout after " +
                    std::to_string(queryContext_->getElapsedMs()) + "ms"
                );
            }
        }

        return doc;
    }

protected:
    virtual int32_t doNextDoc() = 0;  // Subclass implements
};
```

**Overhead**: 1 check per 1024 docs ≈ 0.1% overhead for timeout checking.

### TimeLimitingCollector

Wrap any collector with timeout enforcement:

```cpp
/**
 * TimeLimitingCollector wraps another collector and enforces timeout.
 *
 * Based on: org.apache.lucene.search.TimeLimitingCollector
 */
class TimeLimitingCollector : public Collector {
public:
    TimeLimitingCollector(
        std::unique_ptr<Collector> collector,
        QueryContext& queryContext
    ) : collector_(std::move(collector)),
        queryContext_(queryContext),
        docCounter_(0)
    {}

    void collect(int32_t doc) override {
        // Periodic timeout check
        if ((++docCounter_ & 1023) == 0) {  // Every 1024 docs
            if (queryContext_.shouldStop()) {
                throw QueryTimeoutException(
                    "Query timeout after " +
                    std::to_string(queryContext_.getElapsedMs()) + "ms"
                );
            }
        }

        // Delegate to wrapped collector
        collector_->collect(doc);
    }

    ScoreMode scoreMode() const override {
        return collector_->scoreMode();
    }

    LeafCollector* getLeafCollector(LeafReaderContext& context) override {
        LeafCollector* leafCollector = collector_->getLeafCollector(context);
        return new TimeLimitingLeafCollector(
            leafCollector,
            queryContext_,
            docCounter_
        );
    }

private:
    std::unique_ptr<Collector> collector_;
    QueryContext& queryContext_;
    std::atomic<int> docCounter_;
};
```

### Cancellation API

**Use Case**: User clicks "Cancel" button in UI → backend cancels query.

```cpp
class IndexSearcher {
public:
    /**
     * Search with timeout and cancellation support
     */
    TopDocs search(
        const Query& query,
        int n,
        QueryContext& ctx
    ) {
        // Set query context on all iterators
        auto weight = query.createWeight(*this, ScoreMode::COMPLETE, 1.0f);
        weight->setQueryContext(&ctx);

        // Wrap collector with timeout enforcement
        auto collector = std::make_unique<TopScoreDocCollector>(n);
        auto timedCollector = std::make_unique<TimeLimitingCollector>(
            std::move(collector),
            ctx
        );

        // Execute search (may throw QueryTimeoutException)
        try {
            for (auto& leafCtx : reader_->leaves()) {
                auto scorer = weight->scorer(*leafCtx);
                if (!scorer) continue;

                auto leafCollector = timedCollector->getLeafCollector(*leafCtx);
                auto iterator = scorer->iterator();

                int32_t doc;
                while ((doc = iterator->nextDoc()) != NO_MORE_DOCS) {
                    leafCollector->collect(doc);
                }
            }
        } catch (const QueryTimeoutException& e) {
            // Log timeout and return partial results
            LOG_WARN("Query timeout: " + std::string(e.what()));
            // timedCollector may have partial results
        }

        return timedCollector->topDocs();
    }

    /**
     * Cancel a running query (called from another thread)
     */
    void cancelQuery(QueryContext& ctx) {
        ctx.cancel();
    }
};
```

**Usage**:
```cpp
// Thread 1: Execute query
QueryContext ctx;
ctx.setTimeout(5000);  // 5 second timeout

TopDocs results;
try {
    results = searcher.search(query, 100, ctx);
} catch (const QueryTimeoutException& e) {
    std::cout << "Query timeout: " << e.what() << std::endl;
}

// Thread 2: Cancel query (e.g., user clicks cancel)
searcher.cancelQuery(ctx);
```

### Partial Results on Timeout

**Option 1: Return Partial Results** (default)

```cpp
TopDocs search(const Query& query, int n, QueryContext& ctx) {
    auto collector = std::make_unique<TopScoreDocCollector>(n);
    auto timedCollector = std::make_unique<TimeLimitingCollector>(
        std::move(collector),
        ctx
    );

    try {
        // Search all segments
        for (auto& leafCtx : reader_->leaves()) {
            // ... search segment ...
        }
    } catch (const QueryTimeoutException& e) {
        // Return partial results collected so far
        LOG_WARN("Query timeout - returning partial results");
    }

    return timedCollector->topDocs();  // May be incomplete
}
```

**Option 2: Throw Exception** (fail fast)

```cpp
TopDocs search(const Query& query, int n, QueryContext& ctx) {
    ctx.setPartialResultsAllowed(false);  // Fail on timeout

    auto collector = std::make_unique<TopScoreDocCollector>(n);
    // ... search ...

    // QueryTimeoutException propagates to caller
}
```

### Resource Cleanup on Timeout

**Critical**: Ensure resources are released even if query times out.

```cpp
class ScopedQueryResources {
public:
    ScopedQueryResources(QueryContext& ctx) : ctx_(ctx) {
        // Acquire resources (e.g., column arena)
        arena_ = ctx_.getColumnArena();
    }

    ~ScopedQueryResources() {
        // Always cleanup, even on exception
        if (arena_) {
            arena_->reset();  // Bulk free all allocations
        }
        ctx_.trackMemoryAllocation(0);  // Reset counter
    }

private:
    QueryContext& ctx_;
    ColumnArena* arena_;
};

TopDocs search(const Query& query, int n, QueryContext& ctx) {
    // RAII ensures cleanup on timeout
    ScopedQueryResources resources(ctx);

    // Query execution (may timeout)
    // ...

    // resources auto-cleanup on scope exit (normal or exception)
}
```

### Timeout Granularity Trade-offs

| Check Interval | Overhead | Timeout Precision | Recommendation |
|----------------|----------|-------------------|----------------|
| Every doc | 5-10% | Exact | Too expensive |
| Every 256 docs | 1-2% | ±256 docs | Good for small indexes |
| Every 1024 docs | 0.1-0.5% | ±1024 docs | **Default (balanced)** |
| Every 4096 docs | <0.1% | ±4096 docs | Good for large indexes |
| Every 16384 docs | <0.01% | ±16384 docs | Too coarse |

**Default**: Check every 1024 docs (0.1% overhead, ±1ms precision for 100K docs/sec).

### Query Timeout Exception

```cpp
/**
 * Exception thrown when query exceeds timeout
 */
class QueryTimeoutException : public std::runtime_error {
public:
    QueryTimeoutException(const std::string& message)
        : std::runtime_error(message)
    {}
};
```

### Integration with IndexSearcher

```cpp
class IndexSearcher {
public:
    /**
     * Search with timeout (convenience method)
     */
    TopDocs search(const Query& query, int n, int64_t timeoutMs) {
        QueryContext ctx;
        ctx.setTimeout(timeoutMs);
        return search(query, n, ctx);
    }

    /**
     * Search with explicit QueryContext
     */
    TopDocs search(const Query& query, int n, QueryContext& ctx) {
        // Implementation shown above
    }

    /**
     * Count with timeout
     */
    int64_t count(const Query& query, QueryContext& ctx) {
        auto weight = query.createWeight(*this, ScoreMode::COMPLETE_NO_SCORES, 1.0f);
        weight->setQueryContext(&ctx);

        int64_t count = 0;
        int checkCounter = 0;

        for (auto& leafCtx : reader_->leaves()) {
            auto scorer = weight->scorer(*leafCtx);
            if (!scorer) continue;

            auto iterator = scorer->iterator();
            int32_t doc;
            while ((doc = iterator->nextDoc()) != NO_MORE_DOCS) {
                count++;

                // Periodic timeout check
                if ((++checkCounter & 1023) == 0 && ctx.shouldStop()) {
                    throw QueryTimeoutException(
                        "Count query timeout after " +
                        std::to_string(ctx.getElapsedMs()) + "ms"
                    );
                }
            }
        }

        return count;
    }
};
```

### Usage Examples

#### Example 1: Simple Timeout

```cpp
IndexSearcher searcher(*reader);
TermQuery query("title", "database");

// Search with 5 second timeout
try {
    TopDocs results = searcher.search(query, 100, 5000);  // 5000ms
    std::cout << "Found " << results.totalHits << " results" << std::endl;
} catch (const QueryTimeoutException& e) {
    std::cout << "Query timed out: " << e.what() << std::endl;
}
```

#### Example 2: User Cancellation

```cpp
// Shared query context
QueryContext ctx;
ctx.setTimeout(30000);  // 30 seconds

// Thread 1: Execute search
std::thread queryThread([&]() {
    try {
        TopDocs results = searcher.search(query, 1000, ctx);
        displayResults(results);
    } catch (const QueryTimeoutException& e) {
        std::cout << "Query cancelled or timed out" << std::endl;
    }
});

// Thread 2: Wait for user cancel button
std::thread cancelThread([&]() {
    waitForCancelButton();
    ctx.cancel();  // Triggers timeout in query thread
});

queryThread.join();
cancelThread.join();
```

#### Example 3: Partial Results with Timeout Flag

```cpp
QueryContext ctx;
ctx.setTimeout(10000);  // 10 seconds

TopDocs results = searcher.search(query, 100, ctx);

if (ctx.isTimeout()) {
    std::cout << "Warning: Partial results (timeout)" << std::endl;
    std::cout << "Searched for " << ctx.getElapsedMs() << "ms" << std::endl;
}

std::cout << "Found " << results.totalHits << " results" << std::endl;
```

#### Example 4: Production Query with Resource Tracking

```cpp
class ProductionQueryExecutor {
public:
    TopDocs executeQuery(
        const Query& query,
        int n,
        int64_t timeoutMs,
        size_t memoryLimitBytes
    ) {
        QueryContext ctx;
        ctx.setTimeout(timeoutMs);
        ctx.setMemoryLimit(memoryLimitBytes);

        // Track query start
        metrics_.recordQueryStart();

        TopDocs results;
        try {
            // Execute with timeout and memory limits
            ScopedQueryResources resources(ctx);
            results = searcher_.search(query, n, ctx);

            // Record success
            metrics_.recordQuerySuccess(ctx.getElapsedMs());

        } catch (const QueryTimeoutException& e) {
            // Record timeout
            metrics_.recordQueryTimeout(ctx.getElapsedMs());
            throw;

        } catch (const MemoryLimitExceededException& e) {
            // Record OOM
            metrics_.recordQueryOOM(ctx.getMemoryUsed());
            throw;
        }

        return results;
    }

private:
    IndexSearcher& searcher_;
    QueryMetrics& metrics_;
};
```

### Summary

**Key Features**:
1. ✅ **Timeout enforcement**: Per-query timeout with millisecond precision
2. ✅ **Cancellation API**: Thread-safe cancellation from external thread
3. ✅ **Partial results**: Configurable (return partial vs throw exception)
4. ✅ **Low overhead**: 0.1% overhead with 1024-doc check interval
5. ✅ **Resource cleanup**: RAII ensures cleanup on timeout/exception

**Best Practices**:
- Set timeout for all user-facing queries (recommend 5-30 seconds)
- Use partial results for exploratory queries (acceptable incompleteness)
- Use fail-fast for critical queries (must be complete or fail)
- Monitor timeout rate in production (>5% timeout rate indicates problems)
- Combine with memory limits (see Module 01) for full resource protection

**Trade-offs**:
- More frequent checks → higher overhead, better precision
- Less frequent checks → lower overhead, coarser precision
- Default (1024 docs) balances overhead (0.1%) vs precision (±1ms)

---

## Phrase Query Details

### What is a Phrase Query?

**PhraseQuery** matches documents where terms appear in a specific order at specific positions.

**Example**: Query "lucene search engine" matches:
- ✅ "lucene search engine is fast"
- ✅ "the lucene search engine"
- ❌ "search engine lucene" (wrong order)
- ❌ "lucene distributed search and engine" (too far apart with default slop=0)

### PhraseQuery Class

```cpp
/**
 * PhraseQuery matches documents where terms appear in order at specific positions.
 *
 * Based on: org.apache.lucene.search.PhraseQuery
 */
class PhraseQuery : public Query {
public:
    /**
     * Builder for constructing phrase queries
     */
    class Builder {
    public:
        Builder() : slop_(0) {}

        /**
         * Add term at position
         */
        Builder& add(const Term& term, int position) {
            terms_.push_back(term);
            positions_.push_back(position);
            return *this;
        }

        /**
         * Add term at next position (auto-increment)
         */
        Builder& add(const Term& term) {
            terms_.push_back(term);
            int nextPos = positions_.empty() ? 0 : positions_.back() + 1;
            positions_.push_back(nextPos);
            return *this;
        }

        /**
         * Set slop (edit distance for positions)
         */
        Builder& setSlop(int slop) {
            slop_ = slop;
            return *this;
        }

        /**
         * Build the phrase query
         */
        std::unique_ptr<PhraseQuery> build() {
            if (terms_.empty()) {
                throw std::invalid_argument("PhraseQuery must have at least one term");
            }
            return std::make_unique<PhraseQuery>(terms_, positions_, slop_);
        }

    private:
        std::vector<Term> terms_;
        std::vector<int> positions_;
        int slop_;
    };

    /**
     * Get terms in phrase
     */
    const std::vector<Term>& getTerms() const { return terms_; }

    /**
     * Get positions of terms
     */
    const std::vector<int>& getPositions() const { return positions_; }

    /**
     * Get slop (position edit distance)
     */
    int getSlop() const { return slop_; }

    Weight* createWeight(IndexSearcher& searcher, ScoreMode scoreMode, float boost) override;

private:
    PhraseQuery(
        const std::vector<Term>& terms,
        const std::vector<int>& positions,
        int slop
    ) : terms_(terms), positions_(positions), slop_(slop) {}

    std::vector<Term> terms_;
    std::vector<int> positions_;
    int slop_;  // Allowed position edit distance
};
```

### Slop Parameter

**Slop** is the maximum position edit distance allowed between terms.

#### Slop = 0 (Exact Phrase)

Terms must appear in exact positions:

```cpp
// Query: "quick brown fox" with slop=0
PhraseQuery::Builder()
    .add(Term("text", "quick"), 0)
    .add(Term("text", "brown"), 1)
    .add(Term("text", "fox"), 2)
    .setSlop(0)
    .build();

// Matches:
// ✅ "quick brown fox" (positions: 0, 1, 2)
// ❌ "quick red brown fox" (positions: 0, 2, 3, 4 - extra word)
// ❌ "brown quick fox" (positions: 1, 0, 2 - wrong order)
```

#### Slop = 1 (One Position Away)

Terms can be 1 position away from expected:

```cpp
// Query: "quick brown fox" with slop=1
PhraseQuery::Builder()
    .add(Term("text", "quick"))
    .add(Term("text", "brown"))
    .add(Term("text", "fox"))
    .setSlop(1)
    .build();

// Matches:
// ✅ "quick brown fox" (exact match)
// ✅ "quick RED brown fox" (one word inserted)
// ✅ "quick brown RED fox" (one word inserted)
// ❌ "quick RED BLUE brown fox" (two words inserted - exceeds slop)
```

#### Slop = N (General Case)

Slop is the **minimum number of position moves** to align terms:

```
Document: "quick red brown fox"
Positions:   0     1    2     3

Query: "quick brown fox" (expected positions: 0, 1, 2)
Actual positions: 0, 2, 3

Moves needed:
- "brown" at pos 2, expected pos 1 → 1 move
- "fox" at pos 3, expected pos 2 → 1 move
Total: 2 moves

Slop=2 matches, slop=1 does not.
```

### Position Matching Algorithm

**PhraseScorer** uses position iterators to find matching phrases:

```cpp
/**
 * PhraseScorer scores documents with matching phrases
 */
class PhraseScorer : public Scorer {
public:
    PhraseScorer(
        Weight* weight,
        std::vector<std::unique_ptr<PostingsEnum>> postings,
        const std::vector<int>& positions,
        int slop
    ) : Scorer(weight),
        postings_(std::move(postings)),
        positions_(positions),
        slop_(slop)
    {}

    int32_t nextDoc() override {
        int32_t doc;
        while ((doc = approximation_->nextDoc()) != NO_MORE_DOCS) {
            if (phraseFreq(doc) > 0) {
                return doc;
            }
        }
        return NO_MORE_DOCS;
    }

    float score() override {
        return weight_->getValueForNormalization() * phraseFreq_;
    }

private:
    /**
     * Count phrase matches in document
     */
    int phraseFreq(int32_t doc) {
        // Advance all posting lists to doc
        for (auto& posting : postings_) {
            if (posting->docID() < doc) {
                posting->advance(doc);
            }
            if (posting->docID() != doc) {
                return 0;  // Term not in doc
            }
        }

        // Find matching phrases using position lists
        if (slop_ == 0) {
            return exactPhraseFreq();
        } else {
            return sloppyPhraseFreq();
        }
    }

    /**
     * Exact phrase matching (slop=0)
     */
    int exactPhraseFreq() {
        int freq = 0;

        // Get first term's positions
        PostingsEnum* first = postings_[0].get();
        int firstPos;
        while ((firstPos = first->nextPosition()) != NO_MORE_POSITIONS) {
            // Check if all other terms match at expected positions
            bool match = true;
            for (size_t i = 1; i < postings_.size(); ++i) {
                int expectedPos = firstPos + (positions_[i] - positions_[0]);
                if (!postings_[i]->advanceToPosition(expectedPos)) {
                    match = false;
                    break;
                }
            }
            if (match) {
                freq++;
            }
        }

        return freq;
    }

    /**
     * Sloppy phrase matching (slop>0)
     */
    int sloppyPhraseFreq() {
        int freq = 0;

        // Get first term's positions
        PostingsEnum* first = postings_[0].get();
        int firstPos;
        while ((firstPos = first->nextPosition()) != NO_MORE_POSITIONS) {
            // Try to find matching phrase within slop distance
            if (matchesWithSlop(firstPos)) {
                freq++;
            }
        }

        return freq;
    }

    /**
     * Check if phrase matches within slop distance
     */
    bool matchesWithSlop(int firstPos) {
        // Collect all positions for each term
        std::vector<std::vector<int>> allPositions(postings_.size());
        allPositions[0].push_back(firstPos);

        for (size_t i = 1; i < postings_.size(); ++i) {
            // Get positions for term i near expected position
            int expectedPos = firstPos + (positions_[i] - positions_[0]);
            int minPos = std::max(0, expectedPos - slop_);
            int maxPos = expectedPos + slop_;

            postings_[i]->advanceToPosition(minPos);
            int pos;
            while ((pos = postings_[i]->nextPosition()) <= maxPos) {
                allPositions[i].push_back(pos);
            }

            if (allPositions[i].empty()) {
                return false;  // Term not within slop distance
            }
        }

        // Check if any combination matches with slop
        return hasValidCombination(allPositions);
    }

    /**
     * Check if positions can be aligned within slop
     */
    bool hasValidCombination(
        const std::vector<std::vector<int>>& allPositions
    ) {
        // Try all combinations of positions
        return tryAllCombinations(allPositions, 0, {});
    }

    bool tryAllCombinations(
        const std::vector<std::vector<int>>& allPositions,
        size_t termIdx,
        std::vector<int> currentPositions
    ) {
        if (termIdx == allPositions.size()) {
            return calculateSlop(currentPositions) <= slop_;
        }

        for (int pos : allPositions[termIdx]) {
            currentPositions.push_back(pos);
            if (tryAllCombinations(allPositions, termIdx + 1, currentPositions)) {
                return true;
            }
            currentPositions.pop_back();
        }

        return false;
    }

    /**
     * Calculate minimum slop needed for position alignment
     */
    int calculateSlop(const std::vector<int>& actualPositions) {
        int totalSlop = 0;
        for (size_t i = 0; i < actualPositions.size(); ++i) {
            int expected = positions_[i];
            int actual = actualPositions[i];
            totalSlop += std::abs(actual - expected);
        }
        return totalSlop;
    }

    std::vector<std::unique_ptr<PostingsEnum>> postings_;
    std::vector<int> positions_;
    int slop_;
    DocIdSetIterator* approximation_;
    int phraseFreq_ = 0;
};
```

### Scoring Phrase Queries

**Scoring Formula** (similar to TermQuery, but uses phrase frequency):

```
score(doc) = boost × idf × (phraseFreq / (phraseFreq + k1 × (1 - b + b × (fieldLength / avgFieldLength))))
```

Where:
- `phraseFreq`: Number of times phrase appears in document
- `idf`: Inverse document frequency (average of term IDFs)
- `k1`, `b`: BM25 parameters

**Implementation**:

```cpp
class PhraseWeight : public Weight {
public:
    float getValueForNormalization() override {
        // Combine IDF of all terms
        float idf = 0.0f;
        for (const auto& termStats : termStats_) {
            idf += computeIDF(termStats.docFreq, numDocs_);
        }
        return boost_ * idf;
    }

    Scorer* scorer(LeafReaderContext& context) override {
        // Get postings for all terms
        std::vector<std::unique_ptr<PostingsEnum>> postings;
        for (const auto& term : query_->getTerms()) {
            auto termsEnum = context.reader()->terms(term.field());
            if (!termsEnum || !termsEnum->seekExact(term.bytes())) {
                return nullptr;  // Term not in segment
            }
            postings.push_back(termsEnum->postings(PostingsEnum::POSITIONS));
        }

        return new PhraseScorer(
            this,
            std::move(postings),
            query_->getPositions(),
            query_->getSlop()
        );
    }

private:
    PhraseQuery* query_;
    float boost_;
    std::vector<TermStatistics> termStats_;
    int64_t numDocs_;
};
```

### Usage Examples

#### Example 1: Exact Phrase

```cpp
// Match "lucene search" as exact phrase
auto query = PhraseQuery::Builder()
    .add(Term("title", "lucene"))
    .add(Term("title", "search"))
    .setSlop(0)  // Exact phrase
    .build();

TopDocs results = searcher.search(*query, 10);
// Matches: "lucene search tutorial"
// Does not match: "lucene distributed search"
```

#### Example 2: Phrase with Slop

```cpp
// Match "quick fox" within 2 words
auto query = PhraseQuery::Builder()
    .add(Term("text", "quick"))
    .add(Term("text", "fox"))
    .setSlop(2)  // Allow 2 words in between
    .build();

TopDocs results = searcher.search(*query, 10);
// Matches: "quick brown fox" (1 word between)
// Matches: "quick red brown fox" (2 words between)
// Does not match: "quick red big brown fox" (3 words between)
```

#### Example 3: Multi-term Phrase

```cpp
// Match "new york city"
auto query = PhraseQuery::Builder()
    .add(Term("location", "new"))
    .add(Term("location", "york"))
    .add(Term("location", "city"))
    .setSlop(0)
    .build();

TopDocs results = searcher.search(*query, 10);
```

#### Example 4: Phrase with Custom Positions

```cpp
// Match "A _ B" (skip position 1)
auto query = PhraseQuery::Builder()
    .add(Term("text", "database"), 0)
    .add(Term("text", "system"), 2)  // Skip position 1
    .setSlop(0)
    .build();

// Matches: "database management system" (position 0, 1, 2)
// "management" at position 1 is allowed (not in query)
```

### Performance Considerations

#### Optimization 1: Conjunction Approximation

Use conjunction of term queries as approximation, then verify positions:

```cpp
// Approximation: doc contains ALL terms (no position check)
auto approximation = createConjunctionApproximation(terms_);

// Confirmation: check positions match phrase
int32_t doc;
while ((doc = approximation->nextDoc()) != NO_MORE_DOCS) {
    if (phraseFreq(doc) > 0) {
        // Actual phrase match
    }
}
```

**Speedup**: Avoids position checking for documents missing terms (common case).

#### Optimization 2: Rare Term First

Order terms by document frequency (rare terms first):

```cpp
// Sort terms by docFreq (ascending)
std::sort(terms_.begin(), terms_.end(), [](const Term& a, const Term& b) {
    return getDocFreq(a) < getDocFreq(b);
});

// Use rarest term to drive iteration
PostingsEnum* rarest = postings_[0].get();
int32_t doc;
while ((doc = rarest->nextDoc()) != NO_MORE_DOCS) {
    // Check if other terms match
}
```

**Speedup**: Iterate over smallest posting list (fewer documents to check).

#### Optimization 3: Position Index Caching

Cache position data for frequently queried phrases:

```cpp
class PositionCache {
    std::unordered_map<std::string, std::vector<std::pair<int32_t, std::vector<int>>>>
        cache_;  // term → [(docID, [positions])]

public:
    bool lookup(const Term& term, int32_t doc, std::vector<int>& positions);
    void store(const Term& term, int32_t doc, const std::vector<int>& positions);
};
```

**Speedup**: Avoid re-reading position data for hot phrases.

### Exact vs Sloppy Performance

| Slop | Algorithm Complexity | Typical Performance |
|------|---------------------|---------------------|
| 0 (exact) | O(phraseFreq) | Fast (linear scan) |
| 1-2 (near) | O(phraseFreq × positions²) | Moderate (few combinations) |
| 3-5 (loose) | O(phraseFreq × positions^N) | Slow (many combinations) |
| >5 (very loose) | O(phraseFreq × positions^N) | Very slow (exponential) |

**Recommendation**: Keep slop ≤ 3 for production queries.

### Common Use Cases

#### Use Case 1: Exact Phrase Search

```cpp
// User searches for exact phrase "machine learning"
auto query = PhraseQuery::Builder()
    .add(Term("body", "machine"))
    .add(Term("body", "learning"))
    .setSlop(0)
    .build();
```

**Use**: Academic search, technical documentation, legal documents

#### Use Case 2: Named Entity Recognition

```cpp
// Match person names with middle initial: "John _ Smith"
auto query = PhraseQuery::Builder()
    .add(Term("author", "john"), 0)
    .add(Term("author", "smith"), 2)  // Skip middle name at pos 1
    .setSlop(0)
    .build();
```

**Use**: Person names, product names, organization names

#### Use Case 3: Proximity Search

```cpp
// Match "database" near "performance" (within 5 words)
auto query = PhraseQuery::Builder()
    .add(Term("text", "database"))
    .add(Term("text", "performance"))
    .setSlop(5)
    .build();
```

**Use**: Related concept discovery, semantic search

### Limitations and Alternatives

**Limitation 1: No Wildcards**

PhraseQuery requires exact term matches. For wildcard phrases:

```cpp
// Want: "quick * fox" (any word in middle)
// Solution: Use SpanQuery instead
SpanNearQuery(
    {SpanTermQuery("quick"), SpanWildcardQuery("*"), SpanTermQuery("fox")},
    slop=0,
    inOrder=true
);
```

**Limitation 2: No Synonyms**

PhraseQuery doesn't handle synonyms. For synonym expansion:

```cpp
// Want: "fast car" OR "quick car" OR "fast automobile"
// Solution: Use BooleanQuery with multiple PhraseQuery clauses
BooleanQuery::Builder()
    .add(PhraseQuery("fast car"), BooleanClause::SHOULD)
    .add(PhraseQuery("quick car"), BooleanClause::SHOULD)
    .add(PhraseQuery("fast automobile"), BooleanClause::SHOULD)
    .build();
```

### Summary

**Key Features**:
1. ✅ **Exact phrase matching**: Slop=0 for exact term order
2. ✅ **Proximity search**: Slop>0 for nearby terms
3. ✅ **Position awareness**: Respects term positions in index
4. ✅ **BM25 scoring**: Uses phrase frequency for ranking
5. ✅ **Custom positions**: Skip positions for complex patterns

**Performance**:
- Exact phrase (slop=0): Fast (linear position scan)
- Sloppy phrase (slop≤3): Moderate (polynomial combinations)
- Very sloppy (slop>5): Slow (exponential complexity)

**Best Practices**:
- Use slop=0 for exact phrase queries
- Limit slop to 2-3 for proximity search
- Use rare term first optimization
- Cache position data for hot phrases
- Consider SpanQuery for advanced positional queries

---

## Next: Concrete Query Types

The following design documents will cover:
- **TermQuery**: Single term matching
- **BooleanQuery**: Boolean combinations (AND/OR/NOT)
- **PhraseQuery**: Phrase matching with positions
- **RangeQuery**: Range queries using columns
- **WildcardQuery**: Pattern matching
- **FuzzyQuery**: Edit distance matching

These will be detailed in separate query-specific design documents.
