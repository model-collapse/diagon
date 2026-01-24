# Merge System Design
## Based on Lucene Merge Framework

Source references:
- `org.apache.lucene.index.MergePolicy`
- `org.apache.lucene.index.TieredMergePolicy`
- `org.apache.lucene.index.MergeScheduler`
- `org.apache.lucene.index.ConcurrentMergeScheduler`
- `org.apache.lucene.index.OneMerge`
- `ClickHouse/src/Storages/MergeTree/MergeTask.h`

## Overview

The merge system maintains index efficiency by:
- **Merging small segments** into larger ones
- **Reclaiming deleted documents** through compaction
- **Background execution** without blocking writes
- **Configurable policies** for when/what to merge

## MergePolicy (Abstract)

```cpp
/**
 * MergePolicy determines which segments to merge.
 *
 * Called by IndexWriter after flush/commit.
 * Returns MergeSpecification describing merges to perform.
 *
 * Based on: org.apache.lucene.index.MergePolicy
 */
class MergePolicy {
public:
    virtual ~MergePolicy() = default;

    // ==================== Merge Selection ====================

    /**
     * Find merges needed after flush
     * @param trigger What triggered this check
     * @param segmentInfos Current segments
     * @return MergeSpecification or nullptr if no merges needed
     */
    virtual MergeSpecification* findMerges(
        MergeTrigger trigger,
        const SegmentInfos& segmentInfos) = 0;

    /**
     * Find merge to run when segments are needed for searching
     * More aggressive than findMerges()
     */
    virtual MergeSpecification* findForcedMerges(
        const SegmentInfos& segmentInfos,
        int maxSegmentCount,
        const std::map<SegmentCommitInfo*, bool>& segmentsToMerge) = 0;

    /**
     * Find merges needed only to reclaim deletes
     */
    virtual MergeSpecification* findForcedDeletesMerges(
        const SegmentInfos& segmentInfos) = 0;

    // ==================== Configuration ====================

    /**
     * Set max merged segment size (bytes)
     */
    virtual void setMaxMergedSegmentMB(double mb) = 0;

    /**
     * Set floor segment size (bytes)
     * Segments below this are always eligible for merge
     */
    virtual void setFloorSegmentMB(double mb) = 0;

    // ==================== Utilities ====================

    /**
     * Check if segment is mergeable
     * (not too large, not being merged, etc.)
     */
    virtual bool isMerged(const SegmentInfos& infos,
                         const SegmentCommitInfo& info) const {
        return info.getDelCount() == 0;
    }

    /**
     * Check if segment size is within acceptable range
     */
    virtual bool keepFullyDeletedSegment(const SegmentCommitInfo& info) const {
        return false;  // Delete fully deleted segments
    }

protected:
    /**
     * Get size of segment in MB
     */
    double sizeMB(const SegmentCommitInfo& info) const {
        return info.sizeInBytes() / (1024.0 * 1024.0);
    }

    /**
     * Get size including deletes
     */
    double sizeWithDeletes(const SegmentCommitInfo& info) const {
        double size = sizeMB(info);
        double delRatio = info.getDelRatio();
        return size * (1.0 - delRatio);
    }
};

/**
 * What triggered the merge check
 */
enum class MergeTrigger {
    SEGMENT_FLUSH,      // After flushing new segment
    FULL_FLUSH,         // After full flush
    COMMIT,             // During commit
    GET_READER,         // When opening reader
    CLOSING,            // During close
    EXPLICIT            // Explicit forceMerge() call
};
```

## MergeSpecification

```cpp
/**
 * Describes a set of merges to perform
 *
 * Based on: org.apache.lucene.index.MergeSpecification
 */
class MergeSpecification {
public:
    /**
     * Add merge
     */
    void add(std::unique_ptr<OneMerge> merge) {
        merges_.push_back(std::move(merge));
    }

    /**
     * Get all merges
     */
    const std::vector<std::unique_ptr<OneMerge>>& getMerges() const {
        return merges_;
    }

    /**
     * Number of merges
     */
    size_t size() const {
        return merges_.size();
    }

    /**
     * Description
     */
    std::string segString() const {
        std::string s;
        for (const auto& merge : merges_) {
            if (!s.empty()) s += " ";
            s += merge->segString();
        }
        return s;
    }

private:
    std::vector<std::unique_ptr<OneMerge>> merges_;
};
```

## OneMerge

```cpp
/**
 * OneMerge describes a single merge operation.
 *
 * Tracks segments to merge, merge progress, and result.
 *
 * Based on: org.apache.lucene.index.OneMerge
 */
class OneMerge {
public:
    /**
     * Constructor
     * @param segments Segments to merge
     */
    explicit OneMerge(const std::vector<SegmentCommitInfo*>& segments)
        : segments_(segments) {
        totalDocCount_ = 0;
        for (auto* seg : segments) {
            totalDocCount_ += seg->info().maxDoc();
        }
    }

    // ==================== Segment Info ====================

    /**
     * Segments being merged
     */
    const std::vector<SegmentCommitInfo*>& getSegments() const {
        return segments_;
    }

    /**
     * Merged segment info (set after merge)
     */
    SegmentCommitInfo* getMergeInfo() const {
        return mergeInfo_;
    }

    void setMergeInfo(SegmentCommitInfo* info) {
        mergeInfo_ = info;
    }

    // ==================== Statistics ====================

    /**
     * Total documents to merge
     */
    int64_t getTotalDocCount() const {
        return totalDocCount_;
    }

    /**
     * Estimated merge bytes
     */
    int64_t estimatedMergeBytes() const {
        int64_t total = 0;
        for (auto* seg : segments_) {
            total += seg->sizeInBytes();
        }
        return total;
    }

    // ==================== Progress Tracking ====================

    /**
     * Set merge progress (0.0 to 1.0)
     */
    void setProgress(double progress) {
        progress_ = progress;
    }

    double getProgress() const {
        return progress_;
    }

    /**
     * Mark merge as aborted
     */
    void setAborted() {
        aborted_ = true;
    }

    bool isAborted() const {
        return aborted_;
    }

    // ==================== Error Handling ====================

    /**
     * Set exception that occurred during merge
     */
    void setException(std::exception_ptr e) {
        exception_ = e;
    }

    std::exception_ptr getException() const {
        return exception_;
    }

    // ==================== Utilities ====================

    /**
     * String representation
     */
    std::string segString() const {
        std::string s = "merge=[";
        for (size_t i = 0; i < segments_.size(); ++i) {
            if (i > 0) s += " ";
            s += segments_[i]->info().name();
        }
        s += "]";
        return s;
    }

private:
    std::vector<SegmentCommitInfo*> segments_;
    SegmentCommitInfo* mergeInfo_{nullptr};
    int64_t totalDocCount_;
    std::atomic<double> progress_{0.0};
    std::atomic<bool> aborted_{false};
    std::exception_ptr exception_;
};
```

## TieredMergePolicy

```cpp
/**
 * TieredMergePolicy merges segments of roughly similar size.
 *
 * Default merge policy.
 * Aims for logarithmic segment distribution.
 *
 * Based on: org.apache.lucene.index.TieredMergePolicy
 */
class TieredMergePolicy : public MergePolicy {
public:
    TieredMergePolicy() = default;

    // ==================== Configuration ====================

    /**
     * Max merged segment size (default: 5GB)
     */
    void setMaxMergedSegmentMB(double mb) override {
        maxMergedSegmentBytes_ = static_cast<int64_t>(mb * 1024 * 1024);
    }

    /**
     * Floor segment size (default: 2MB)
     * Segments below this are always merged
     */
    void setFloorSegmentMB(double mb) override {
        floorSegmentBytes_ = static_cast<int64_t>(mb * 1024 * 1024);
    }

    /**
     * Segments per tier (default: 10)
     * Target number of segments per tier
     */
    void setSegmentsPerTier(double segsPerTier) {
        segsPerTier_ = segsPerTier;
    }

    /**
     * Max merge at once (default: 10)
     */
    void setMaxMergeAtOnce(int maxMergeAtOnce) {
        maxMergeAtOnce_ = maxMergeAtOnce;
    }

    /**
     * Max merge at once (explicit) (default: 30)
     * For forceMerge()
     */
    void setMaxMergeAtOnceExplicit(int max) {
        maxMergeAtOnceExplicit_ = max;
    }

    // ==================== Merge Finding ====================

    MergeSpecification* findMerges(
        MergeTrigger trigger,
        const SegmentInfos& infos) override {

        auto mergeable = getMergeableSegments(infos);

        if (mergeable.empty()) {
            return nullptr;
        }

        // Sort by size
        std::sort(mergeable.begin(), mergeable.end(),
            [](const auto& a, const auto& b) {
                return a->sizeInBytes() < b->sizeInBytes();
            }
        );

        // Find best merge
        auto spec = findBestMerge(mergeable, maxMergeAtOnce_);

        return spec;
    }

    MergeSpecification* findForcedMerges(
        const SegmentInfos& infos,
        int maxSegmentCount,
        const std::map<SegmentCommitInfo*, bool>& segmentsToMerge) override {

        auto mergeable = getMergeableSegments(infos);

        if (mergeable.size() <= maxSegmentCount) {
            return nullptr;
        }

        // Aggressive merging for forceMerge
        auto spec = std::make_unique<MergeSpecification>();

        while (mergeable.size() > maxSegmentCount) {
            // Merge smallest segments
            std::vector<SegmentCommitInfo*> toMerge;
            int count = std::min(
                maxMergeAtOnceExplicit_,
                static_cast<int>(mergeable.size()) - maxSegmentCount + 1
            );

            for (int i = 0; i < count; ++i) {
                toMerge.push_back(mergeable[i]);
            }

            spec->add(std::make_unique<OneMerge>(toMerge));

            // Remove merged segments
            mergeable.erase(mergeable.begin(), mergeable.begin() + count);
        }

        return spec->size() > 0 ? spec.release() : nullptr;
    }

    MergeSpecification* findForcedDeletesMerges(
        const SegmentInfos& infos) override {

        auto spec = std::make_unique<MergeSpecification>();

        for (auto* info : infos) {
            double delRatio = info->getDelRatio();

            // Merge if >10% deleted
            if (delRatio > 0.10) {
                std::vector<SegmentCommitInfo*> toMerge = {info};
                spec->add(std::make_unique<OneMerge>(toMerge));
            }
        }

        return spec->size() > 0 ? spec.release() : nullptr;
    }

private:
    // Configuration
    int64_t maxMergedSegmentBytes_{5368709120LL};  // 5GB
    int64_t floorSegmentBytes_{2097152};            // 2MB
    double segsPerTier_{10.0};
    int maxMergeAtOnce_{10};
    int maxMergeAtOnceExplicit_{30};

    /**
     * Get segments eligible for merge
     */
    std::vector<SegmentCommitInfo*> getMergeableSegments(
        const SegmentInfos& infos) const {

        std::vector<SegmentCommitInfo*> mergeable;

        for (auto* info : infos) {
            if (info->sizeInBytes() < maxMergedSegmentBytes_) {
                mergeable.push_back(info);
            }
        }

        return mergeable;
    }

    /**
     * Find best merge from candidates
     */
    MergeSpecification* findBestMerge(
        const std::vector<SegmentCommitInfo*>& candidates,
        int maxMergeAtOnce) const {

        if (candidates.size() < 2) {
            return nullptr;
        }

        // Score potential merges
        struct MergeCandidate {
            std::vector<SegmentCommitInfo*> segments;
            double score;
        };

        std::vector<MergeCandidate> potentialMerges;

        // Try merging consecutive segments
        for (size_t i = 0; i < candidates.size(); ++i) {
            for (int count = 2; count <= maxMergeAtOnce && i + count <= candidates.size(); ++count) {
                std::vector<SegmentCommitInfo*> toMerge;
                for (int j = 0; j < count; ++j) {
                    toMerge.push_back(candidates[i + j]);
                }

                double score = scoreMerge(toMerge);
                potentialMerges.push_back({toMerge, score});
            }
        }

        if (potentialMerges.empty()) {
            return nullptr;
        }

        // Pick best score
        auto best = std::max_element(
            potentialMerges.begin(),
            potentialMerges.end(),
            [](const auto& a, const auto& b) {
                return a.score < b.score;
            }
        );

        auto spec = std::make_unique<MergeSpecification>();
        spec->add(std::make_unique<OneMerge>(best->segments));

        return spec.release();
    }

    /**
     * Score a potential merge (higher is better)
     */
    double scoreMerge(const std::vector<SegmentCommitInfo*>& segments) const {
        int64_t totalSize = 0;
        int64_t minSize = std::numeric_limits<int64_t>::max();
        int64_t maxSize = 0;

        for (auto* seg : segments) {
            int64_t size = seg->sizeInBytes();
            totalSize += size;
            minSize = std::min(minSize, size);
            maxSize = std::max(maxSize, size);
        }

        // Prefer merging similar-sized segments
        double sizeRatio = static_cast<double>(maxSize) / minSize;

        // Prefer merging small segments
        double avgSize = totalSize / segments.size();
        double sizeScore = 1.0 / (1.0 + avgSize / floorSegmentBytes_);

        // Combined score
        return sizeScore / sizeRatio;
    }
};
```

## MergeScheduler (Abstract)

```cpp
/**
 * MergeScheduler executes merges.
 *
 * Controls how/when merges run (background threads, etc.)
 *
 * Based on: org.apache.lucene.index.MergeScheduler
 */
class MergeScheduler {
public:
    virtual ~MergeScheduler() = default;

    /**
     * Run merges
     * @param writer IndexWriter
     * @param trigger What triggered merges
     * @param newMergesFound New merges from MergePolicy
     */
    virtual void merge(IndexWriter* writer,
                      MergeTrigger trigger,
                      bool newMergesFound) = 0;

    /**
     * Close scheduler
     * Waits for pending merges
     */
    virtual void close() = 0;
};
```

## ConcurrentMergeScheduler

```cpp
/**
 * ConcurrentMergeScheduler runs merges in background threads.
 *
 * Default scheduler.
 * Runs multiple merges concurrently.
 *
 * Based on: org.apache.lucene.index.ConcurrentMergeScheduler
 */
class ConcurrentMergeScheduler : public MergeScheduler {
public:
    ConcurrentMergeScheduler() {
        // Default: (numCores / 2) threads, min 1, max 4
        int cores = std::thread::hardware_concurrency();
        maxThreadCount_ = std::clamp(cores / 2, 1, 4);
        maxMergeCount_ = maxThreadCount_ + 5;  // Allow some queueing
    }

    // ==================== Configuration ====================

    /**
     * Max concurrent merge threads (default: cores/2, max 4)
     */
    void setMaxMergeCount(int max) {
        maxMergeCount_ = max;
    }

    /**
     * Max merge threads
     */
    void setMaxThreadCount(int max) {
        maxThreadCount_ = max;
    }

    // ==================== Merge Execution ====================

    void merge(IndexWriter* writer,
              MergeTrigger trigger,
              bool newMergesFound) override {

        std::unique_lock<std::mutex> lock(mutex_);

        if (closed_) {
            return;
        }

        while (true) {
            // Get next pending merge
            OneMerge* merge = writer->getNextMerge();
            if (!merge) {
                break;
            }

            // Wait if too many merges running
            while (mergeThreads_.size() >= maxThreadCount_) {
                cv_.wait(lock);
                if (closed_) return;
            }

            // Start merge thread
            auto thread = std::make_unique<MergeThread>(writer, merge, this);
            mergeThreads_.push_back(std::move(thread));
        }
    }

    void close() override {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            closed_ = true;
            cv_.notify_all();
        }

        // Wait for all merges to complete
        for (auto& thread : mergeThreads_) {
            thread->join();
        }

        mergeThreads_.clear();
    }

private:
    class MergeThread {
    public:
        MergeThread(IndexWriter* writer, OneMerge* merge,
                   ConcurrentMergeScheduler* scheduler)
            : writer_(writer)
            , merge_(merge)
            , scheduler_(scheduler) {

            thread_ = std::thread(&MergeThread::run, this);
        }

        void join() {
            if (thread_.joinable()) {
                thread_.join();
            }
        }

    private:
        void run() {
            try {
                writer_->merge(merge_);
            } catch (const std::exception& e) {
                merge_->setException(std::current_exception());
            }

            // Notify scheduler
            {
                std::lock_guard<std::mutex> lock(scheduler_->mutex_);
                scheduler_->mergeThreads_.erase(
                    std::remove_if(
                        scheduler_->mergeThreads_.begin(),
                        scheduler_->mergeThreads_.end(),
                        [this](const auto& t) { return t.get() == this; }
                    ),
                    scheduler_->mergeThreads_.end()
                );
                scheduler_->cv_.notify_all();
            }
        }

        IndexWriter* writer_;
        OneMerge* merge_;
        ConcurrentMergeScheduler* scheduler_;
        std::thread thread_;
    };

    int maxThreadCount_;
    int maxMergeCount_;
    std::vector<std::unique_ptr<MergeThread>> mergeThreads_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool closed_{false};
};
```

## IndexWriter Integration

```cpp
/**
 * IndexWriter merge methods
 */
class IndexWriter {
public:
    /**
     * Maybe trigger merge
     */
    void maybeMerge(MergeTrigger trigger) {
        auto spec = config_.getMergePolicy().findMerges(trigger, *segmentInfos_);

        if (spec && spec->size() > 0) {
            // Add merges to queue
            for (auto& merge : spec->getMerges()) {
                pendingMerges_.push(std::move(merge));
            }

            // Notify scheduler
            config_.getMergeScheduler().merge(this, trigger, true);
        }
    }

    /**
     * Get next pending merge
     */
    OneMerge* getNextMerge() {
        std::lock_guard<std::mutex> lock(mergeMutex_);

        if (pendingMerges_.empty()) {
            return nullptr;
        }

        auto merge = std::move(pendingMerges_.front());
        pendingMerges_.pop();

        runningMerges_.push_back(std::move(merge));
        return runningMerges_.back().get();
    }

    /**
     * Execute merge
     */
    void merge(OneMerge* merge) {
        // Create merger
        SegmentMerger merger(merge->getSegments(), newSegmentName());

        // Perform merge
        auto mergedSegment = merger.merge(merge);

        // Add merged segment
        {
            std::lock_guard<std::mutex> lock(commitMutex_);

            // Remove source segments
            for (auto* seg : merge->getSegments()) {
                segmentInfos_->remove(seg);
            }

            // Add merged segment
            merge->setMergeInfo(mergedSegment);
            segmentInfos_->add(mergedSegment);
        }

        // Checkpoint
        checkpoint();
    }

    /**
     * Force merge to N segments
     */
    void forceMerge(int maxNumSegments) {
        ensureOpen();

        flush();

        std::map<SegmentCommitInfo*, bool> segmentsToMerge;
        for (auto* seg : *segmentInfos_) {
            segmentsToMerge[seg] = true;
        }

        while (segmentInfos_->size() > maxNumSegments) {
            auto spec = config_.getMergePolicy().findForcedMerges(
                *segmentInfos_,
                maxNumSegments,
                segmentsToMerge
            );

            if (!spec || spec->size() == 0) {
                break;
            }

            // Execute merges sequentially for forceMerge
            for (auto& merge : spec->getMerges()) {
                this->merge(merge.get());
            }
        }
    }

private:
    std::queue<std::unique_ptr<OneMerge>> pendingMerges_;
    std::vector<std::unique_ptr<OneMerge>> runningMerges_;
    std::mutex mergeMutex_;
};
```

## Usage Example

```cpp
// Configure merge policy
auto mergePolicy = std::make_unique<TieredMergePolicy>();
mergePolicy->setMaxMergedSegmentMB(5000);  // 5GB
mergePolicy->setFloorSegmentMB(2);         // 2MB
mergePolicy->setSegmentsPerTier(10);

// Configure merge scheduler
auto mergeScheduler = std::make_unique<ConcurrentMergeScheduler>();
mergeScheduler->setMaxThreadCount(4);

// Configure writer
IndexWriterConfig config;
config.setMergePolicy(std::move(mergePolicy));
config.setMergeScheduler(std::move(mergeScheduler));

IndexWriter writer(dir, config);

// Merges happen automatically on flush/commit
writer.addDocument(doc1);
writer.commit();  // May trigger background merge

// Force merge to 1 segment
writer.forceMerge(1);

writer.close();
```

---

## Write Amplification Analysis

### What is Write Amplification?

**Write amplification** is the ratio of bytes written to storage vs. bytes in indexed documents.

```
Write Amplification Factor (WAF) = Total Bytes Written to Disk / Logical Document Bytes
```

**Example**:
- User indexes 100GB of documents
- System writes 1.5TB to disk (initial write + merges)
- Write amplification = 1.5TB / 100GB = **15×**

### Why Write Amplification Occurs

LSM-tree based systems (like Lucene) rewrite data during merges:

1. **Initial Write**: Document indexed → new segment flushed (1× write)
2. **First Merge**: Small segments merged → rewritten (2× write)
3. **Second Merge**: Medium segments merged → rewritten (3× write)
4. **Nth Merge**: Large segments merged → rewritten (N× write)

**Cumulative Effect**: Each byte may be written 10-30× over its lifetime.

### Expected Write Amplification Factors

**TieredMergePolicy** (default):

| Workload Type | WAF Range | Notes |
|---------------|-----------|-------|
| Write-once (no updates) | 10-15× | Each segment merged 2-3 times |
| Moderate updates (10% delete rate) | 15-20× | Deleted docs compacted during merge |
| Heavy updates (50% delete rate) | 20-30× | Frequent rewriting to reclaim space |
| Force merge to 1 segment | 30-50× | Rewrites entire index |

**LogByteSizeMergePolicy**:

| Workload Type | WAF Range | Notes |
|---------------|-----------|-------|
| Write-once | 12-18× | More aggressive merging |
| Moderate updates | 18-25× | Higher overhead due to frequent merges |
| Heavy updates | 25-40× | Worse than TieredMergePolicy |

### Factors Affecting Write Amplification

#### 1. Merge Factor (segmentsPerTier)

**Higher merge factor** → Lower write amplification, but more segments:

```cpp
// Low merge factor (aggressive merging)
mergePolicy->setSegmentsPerTier(5);
// WAF: 15-20×, Segment count: 5-10, Read performance: Excellent

// Medium merge factor (balanced)
mergePolicy->setSegmentsPerTier(10);  // Default
// WAF: 10-15×, Segment count: 10-30, Read performance: Good

// High merge factor (lazy merging)
mergePolicy->setSegmentsPerTier(20);
// WAF: 8-12×, Segment count: 50-100, Read performance: Degraded
```

**Trade-off**: Lower WAF vs. read performance (more segments = slower queries).

#### 2. maxMergedSegmentMB

**Smaller max segment size** → More merges → Higher WAF:

```cpp
// Small segments (more merges)
mergePolicy->setMaxMergedSegmentMB(1000);  // 1GB
// WAF: 18-25×, Merges stop at 1GB (never merge to 5GB)

// Large segments (fewer merges)
mergePolicy->setMaxMergedSegmentMB(5000);  // 5GB (default)
// WAF: 10-15×, Segments can grow larger before stopping
```

#### 3. Delete Rate

**Higher delete rate** → More wasted space → More compaction merges:

```cpp
// No deletes
// WAF: 10-15× (baseline)

// 10% delete rate
// WAF: 15-20× (+50% overhead for compaction)

// 50% delete rate
// WAF: 20-30× (+2× overhead, half of data is garbage)
```

**TieredMergePolicy** merges segments with >30% deleted docs aggressively.

#### 4. Index Size

**Larger indexes** → More merge tiers → Higher cumulative WAF:

| Index Size | Merge Tiers | Estimated WAF |
|------------|-------------|---------------|
| < 10GB | 2-3 tiers | 10-12× |
| 10-100GB | 3-4 tiers | 12-15× |
| 100GB-1TB | 4-5 tiers | 15-20× |
| > 1TB | 5-6 tiers | 20-25× |

**Reason**: Each tier requires rewriting data (e.g., 10MB → 100MB → 1GB → 10GB).

### SSD Lifetime Impact

**SSDs have limited write endurance** (measured in TBW - Terabytes Written):

#### Example Calculation

**Setup**:
- Index size: 500GB
- Daily index rate: 50GB/day
- Write amplification: 15×
- SSD: 1TB consumer-grade (300 TBW endurance)

**Daily writes**:
```
50GB/day × 15× WAF = 750GB/day written to SSD
```

**SSD lifetime**:
```
300 TBW / (750GB/day) = 400 TBW / 0.75 TB/day = 533 days ≈ 1.5 years
```

**With optimized WAF (10×)**:
```
50GB/day × 10× WAF = 500GB/day
300 TBW / 0.5 TB/day = 600 days ≈ 1.6 years
```

**Impact**: Reducing WAF from 15× to 10× extends SSD life by ~20%.

#### SSD Endurance by Type

| SSD Type | Endurance (TBW/TB) | Recommended WAF | Use Case |
|----------|-------------------|-----------------|----------|
| Consumer (QLC) | 300 TBW/TB | < 10× | Development, low write rate |
| Consumer (TLC) | 600 TBW/TB | < 15× | Production, moderate write rate |
| Enterprise (TLC) | 1,500 TBW/TB | < 20× | High write rate workloads |
| Enterprise (SLC) | 5,000+ TBW/TB | < 30× | Write-intensive, mission-critical |

**Recommendation**: For production, use enterprise SSDs (1,500+ TBW/TB) or limit WAF to <15×.

### Tuning Guidance

#### Workload 1: Write-Heavy, Read-Light (Log Ingestion)

**Goal**: Minimize write amplification

**Configuration**:
```cpp
auto policy = std::make_unique<TieredMergePolicy>();
policy->setSegmentsPerTier(15);           // Lazy merging
policy->setMaxMergedSegmentMB(10000);     // 10GB (large segments)
policy->setFloorSegmentMB(10);            // 10MB (reduce tiny segment overhead)

auto scheduler = std::make_unique<ConcurrentMergeScheduler>();
scheduler->setMaxThreadCount(2);          // Limit merge threads (save CPU for indexing)
```

**Expected**:
- WAF: 8-12× (low)
- Segment count: 50-100
- Query latency: Higher (many segments)
- **Trade-off**: Acceptable for log analytics where writes dominate

#### Workload 2: Balanced Read/Write (E-commerce Search)

**Goal**: Balance write amplification and query performance

**Configuration**:
```cpp
auto policy = std::make_unique<TieredMergePolicy>();
policy->setSegmentsPerTier(10);           // Default (balanced)
policy->setMaxMergedSegmentMB(5000);      // 5GB
policy->setFloorSegmentMB(2);             // 2MB

auto scheduler = std::make_unique<ConcurrentMergeScheduler>();
scheduler->setMaxThreadCount(4);          // Parallel merges
```

**Expected**:
- WAF: 12-15× (moderate)
- Segment count: 10-30
- Query latency: Low
- **Trade-off**: Optimal for most use cases

#### Workload 3: Read-Heavy, Write-Light (Static Content)

**Goal**: Minimize segment count for fast queries

**Configuration**:
```cpp
auto policy = std::make_unique<TieredMergePolicy>();
policy->setSegmentsPerTier(5);            // Aggressive merging
policy->setMaxMergedSegmentMB(5000);      // 5GB
policy->setFloorSegmentMB(2);             // 2MB

auto scheduler = std::make_unique<ConcurrentMergeScheduler>();
scheduler->setMaxThreadCount(6);          // Max merge parallelism

// After bulk load, force merge to 1 segment
writer.forceMerge(1);
```

**Expected**:
- WAF: 15-20× during build, 30-50× for forceMerge(1)
- Segment count: 1-5 (optimal for queries)
- Query latency: Minimal
- **Trade-off**: Higher initial write cost, but amortized over long read-only period

#### Workload 4: High Update Rate (User Profiles)

**Goal**: Balance compaction overhead with query performance

**Configuration**:
```cpp
auto policy = std::make_unique<TieredMergePolicy>();
policy->setSegmentsPerTier(8);            // Slightly lazy
policy->setMaxMergedSegmentMB(5000);      // 5GB
policy->setDeletesPctAllowed(25);         // Merge at 25% deletes (default: 33%)

auto scheduler = std::make_unique<ConcurrentMergeScheduler>();
scheduler->setMaxThreadCount(4);
```

**Expected**:
- WAF: 18-25× (higher due to updates)
- Segment count: 15-40
- Query latency: Moderate
- **Trade-off**: More aggressive compaction to reclaim space from deletes

### Configuration Recommendations

#### Minimize Write Amplification

```cpp
// Best for: Write-heavy workloads, consumer SSDs
auto policy = std::make_unique<TieredMergePolicy>();
policy->setSegmentsPerTier(20);           // Very lazy merging
policy->setMaxMergedSegmentMB(10000);     // 10GB max
policy->setFloorSegmentMB(50);            // 50MB floor (batch small segments)
policy->setDeletesPctAllowed(50);         // Tolerate more deletes before merge
```

**WAF**: 8-12×
**Drawback**: 50-150 segments → 2-3× slower queries

#### Minimize Query Latency

```cpp
// Best for: Read-heavy workloads, enterprise SSDs
auto policy = std::make_unique<TieredMergePolicy>();
policy->setSegmentsPerTier(5);            // Aggressive merging
policy->setMaxMergedSegmentMB(5000);      // 5GB
policy->setFloorSegmentMB(2);             // 2MB
policy->setDeletesPctAllowed(20);         // Compact deletes aggressively
```

**WAF**: 15-20×
**Benefit**: 5-15 segments → optimal query performance

#### Balanced (Default)

```cpp
// Best for: Most production workloads
auto policy = std::make_unique<TieredMergePolicy>();
policy->setSegmentsPerTier(10);           // Default
policy->setMaxMergedSegmentMB(5000);      // 5GB
policy->setFloorSegmentMB(2);             // 2MB
policy->setDeletesPctAllowed(33);         // Default
```

**WAF**: 12-15×
**Segment count**: 10-30
**Query latency**: Good

### Monitoring Write Amplification

**Track actual WAF** in production:

```cpp
class WriteAmplificationTracker {
    std::atomic<uint64_t> logicalBytesIndexed_{0};
    std::atomic<uint64_t> physicalBytesWritten_{0};

public:
    void recordIndex(size_t bytes) {
        logicalBytesIndexed_ += bytes;
    }

    void recordWrite(size_t bytes) {
        physicalBytesWritten_ += bytes;
    }

    double getCurrentWAF() const {
        uint64_t logical = logicalBytesIndexed_.load();
        uint64_t physical = physicalBytesWritten_.load();
        return logical > 0 ? static_cast<double>(physical) / logical : 0.0;
    }
};
```

**Alert thresholds**:
- WAF > 20×: Consider increasing `segmentsPerTier` or reducing deletes
- WAF > 30×: Review merge policy configuration
- WAF > 50×: Investigate excessive `forceMerge()` calls

### Summary

**Key Insights**:
1. ✅ Write amplification is **inherent to LSM-tree designs** (10-30× typical)
2. ✅ Trade-off: **Lower WAF vs. query performance** (more segments = higher WAF, fewer segments = lower WAF but slower queries)
3. ✅ SSD lifetime directly impacted: **reducing WAF from 15× to 10× extends life by 50%**
4. ✅ Tuning knobs: `segmentsPerTier` (primary), `maxMergedSegmentMB`, `deletesPctAllowed`

**Recommendations**:
- **Consumer SSDs**: Target WAF < 10× (lazy merging, tolerate more segments)
- **Enterprise SSDs**: WAF 12-15× acceptable (balanced merging)
- **Read-heavy workloads**: WAF 15-20× acceptable (aggressive merging for query speed)
- **Monitor in production**: Track WAF and adjust merge policy if > 20×

---
