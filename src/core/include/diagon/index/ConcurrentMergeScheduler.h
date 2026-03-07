// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

namespace diagon {
namespace index {

/**
 * Runs merges on a background thread so addDocument() is not blocked.
 *
 * Based on: org.apache.lucene.index.ConcurrentMergeScheduler
 *
 * Lifecycle:
 *   - Created by IndexWriter (starts background thread immediately)
 *   - submit() enqueues merge work
 *   - waitForMerges() blocks until all pending+running merges finish
 *   - shutdown() drains remaining tasks, then joins the thread
 *   - Destructor calls shutdown() if not already called
 */
class ConcurrentMergeScheduler {
public:
    ConcurrentMergeScheduler();
    ~ConcurrentMergeScheduler();

    // Non-copyable, non-movable
    ConcurrentMergeScheduler(const ConcurrentMergeScheduler&) = delete;
    ConcurrentMergeScheduler& operator=(const ConcurrentMergeScheduler&) = delete;

    /**
     * Submit merge work to background thread.
     * Returns immediately; work executes asynchronously.
     */
    void submit(std::function<void()> work);

    /**
     * Block until all pending and running merges complete.
     * Called by IndexWriter before commit/close.
     */
    void waitForMerges();

    /**
     * Drain pending merges and join the background thread.
     * Safe to call multiple times.
     */
    void shutdown();

    /**
     * Check if any merge is running or pending.
     */
    bool hasPendingMerges() const;

private:
    struct MergeTask {
        std::function<void()> work;
    };

    void mergeLoop();

    // IMPORTANT: mergeThread_ MUST be declared LAST. C++ initializes members in
    // declaration order, and the thread immediately accesses the mutex/condvar/atomics.
    // If mergeThread_ were first, those members wouldn't be constructed yet (UB).
    mutable std::mutex queueMutex_;
    std::condition_variable queueCV_;  // Signaled when task enqueued or shutdown
    std::condition_variable doneCV_;   // Signaled when a task completes
    std::queue<MergeTask> pendingMerges_;
    std::atomic<bool> shutdown_{false};
    std::atomic<int> activeMerges_{0};  // Running + queued count
    std::thread mergeThread_;           // Must be last — see comment above
};

}  // namespace index
}  // namespace diagon
