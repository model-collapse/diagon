// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/index/ConcurrentMergeScheduler.h"

namespace diagon {
namespace index {

ConcurrentMergeScheduler::ConcurrentMergeScheduler()
    : mergeThread_(&ConcurrentMergeScheduler::mergeLoop, this) {}

ConcurrentMergeScheduler::~ConcurrentMergeScheduler() {
    shutdown();
}

void ConcurrentMergeScheduler::submit(std::function<void()> work) {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        pendingMerges_.push({std::move(work)});
        activeMerges_.fetch_add(1, std::memory_order_relaxed);
    }
    queueCV_.notify_one();
}

void ConcurrentMergeScheduler::waitForMerges() {
    std::unique_lock<std::mutex> lock(queueMutex_);
    doneCV_.wait(lock, [this] { return activeMerges_.load(std::memory_order_relaxed) == 0; });
}

void ConcurrentMergeScheduler::shutdown() {
    if (shutdown_.exchange(true, std::memory_order_acq_rel)) {
        return;  // Already shut down
    }
    queueCV_.notify_one();
    if (mergeThread_.joinable()) {
        mergeThread_.join();
    }
}

bool ConcurrentMergeScheduler::hasPendingMerges() const {
    return activeMerges_.load(std::memory_order_relaxed) > 0;
}

void ConcurrentMergeScheduler::mergeLoop() {
    while (true) {
        MergeTask task;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCV_.wait(lock, [this] {
                return !pendingMerges_.empty() || shutdown_.load(std::memory_order_relaxed);
            });

            if (pendingMerges_.empty()) {
                // shutdown_ is true and queue is drained
                return;
            }

            task = std::move(pendingMerges_.front());
            pendingMerges_.pop();
        }

        // Execute merge work (no lock held — this is the expensive I/O)
        try {
            task.work();
        } catch (const std::exception&) {
            // Swallow: a failed merge should not crash the scheduler thread.
            // The segments remain unmerged and will be retried next cycle.
        }

        // Signal completion (must run even if work() threw)
        activeMerges_.fetch_sub(1, std::memory_order_relaxed);
        doneCV_.notify_all();
    }
}

}  // namespace index
}  // namespace diagon
