// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include "diagon/storage/TierManager.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

namespace diagon {
namespace storage {

/**
 * Background service for automatic tier migrations
 *
 * Based on: OpenSearch ILM background tasks
 */
class TierMigrationService {
public:
    TierMigrationService(TierManager& tier_manager,
                        std::chrono::seconds check_interval = std::chrono::hours(1))
        : tier_manager_(tier_manager)
        , check_interval_(check_interval)
        , running_(false) {}

    ~TierMigrationService() {
        stop();
    }

    /**
     * Start background migration worker
     */
    void start() {
        if (running_.load()) {
            return;  // Already running
        }

        running_ = true;
        worker_thread_ = std::thread([this]() {
            this->run();
        });
    }

    /**
     * Stop background worker
     */
    void stop() {
        if (!running_.load()) {
            return;  // Not running
        }

        running_ = false;
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

    /**
     * Check if service is running
     */
    bool isRunning() const {
        return running_.load();
    }

    /**
     * Get check interval
     */
    std::chrono::seconds getCheckInterval() const {
        return check_interval_;
    }

    /**
     * Set check interval
     */
    void setCheckInterval(std::chrono::seconds interval) {
        check_interval_ = interval;
    }

private:
    TierManager& tier_manager_;
    std::chrono::seconds check_interval_;
    std::atomic<bool> running_;
    std::thread worker_thread_;

    void run() {
        while (running_) {
            try {
                // Evaluate all segments
                auto migrations = tier_manager_.evaluateMigrations();

                // Execute migrations
                for (const auto& [segment_name, target_tier] : migrations) {
                    if (!running_) break;

                    std::cout << "Migrating segment " << segment_name
                             << " to " << toString(target_tier) << std::endl;

                    tier_manager_.migrateSegment(segment_name, target_tier);
                }

            } catch (const std::exception& e) {
                std::cerr << "Migration error: " << e.what() << std::endl;
            }

            // Sleep until next check
            std::this_thread::sleep_for(check_interval_);
        }
    }
};

}  // namespace storage
}  // namespace diagon
