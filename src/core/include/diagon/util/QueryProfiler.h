// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>
#include <iostream>
#include <iomanip>

namespace diagon {
namespace util {

/**
 * QueryProfiler - Thread-local query profiling with minimal overhead
 *
 * Usage:
 *   QueryProfiler::instance().beginPhase("fst_lookup");
 *   // ... do work ...
 *   QueryProfiler::instance().endPhase("fst_lookup");
 *
 * Enabled only when DIAGON_PROFILING is defined at compile time.
 */
class QueryProfiler {
public:
    struct PhaseStats {
        int64_t totalNanos = 0;
        int64_t count = 0;
        int64_t minNanos = INT64_MAX;
        int64_t maxNanos = 0;

        void add(int64_t nanos) {
            totalNanos += nanos;
            count++;
            if (nanos < minNanos) minNanos = nanos;
            if (nanos > maxNanos) maxNanos = nanos;
        }

        double avgNanos() const {
            return count > 0 ? (double)totalNanos / count : 0.0;
        }
    };

    static QueryProfiler& instance() {
        thread_local static QueryProfiler profiler;
        return profiler;
    }

    void reset() {
        phases_.clear();
        activePhases_.clear();
    }

    void beginPhase(const std::string& name) {
#ifdef DIAGON_PROFILING
        activePhases_[name] = std::chrono::high_resolution_clock::now();
#endif
    }

    void endPhase(const std::string& name) {
#ifdef DIAGON_PROFILING
        auto it = activePhases_.find(name);
        if (it != activePhases_.end()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::high_resolution_clock::now() - it->second
            ).count();
            phases_[name].add(elapsed);
            activePhases_.erase(it);
        }
#endif
    }

    void incrementCounter(const std::string& name, int64_t value = 1) {
#ifdef DIAGON_PROFILING
        counters_[name] += value;
#endif
    }

    const std::unordered_map<std::string, PhaseStats>& getPhases() const {
        return phases_;
    }

    const std::unordered_map<std::string, int64_t>& getCounters() const {
        return counters_;
    }

    void printReport(std::ostream& out = std::cout) const {
        out << "\n========== Query Profiling Report ==========\n\n";

        // Calculate total time
        int64_t totalTime = 0;
        for (const auto& [name, stats] : phases_) {
            totalTime += stats.totalNanos;
        }

        // Print phases sorted by total time
        std::vector<std::pair<std::string, PhaseStats>> sorted;
        for (const auto& p : phases_) {
            sorted.push_back(p);
        }
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
            return a.second.totalNanos > b.second.totalNanos;
        });

        out << "Phases (sorted by total time):\n";
        out << std::string(80, '-') << "\n";
        out << std::setw(30) << std::left << "Phase"
            << std::setw(12) << std::right << "Total (ns)"
            << std::setw(10) << "Count"
            << std::setw(12) << "Avg (ns)"
            << std::setw(8) << "% Time\n";
        out << std::string(80, '-') << "\n";

        for (const auto& [name, stats] : sorted) {
            double pct = totalTime > 0 ? (100.0 * stats.totalNanos / totalTime) : 0.0;
            out << std::setw(30) << std::left << name
                << std::setw(12) << std::right << stats.totalNanos
                << std::setw(10) << stats.count
                << std::setw(12) << std::fixed << std::setprecision(1) << stats.avgNanos()
                << std::setw(7) << std::fixed << std::setprecision(1) << pct << "%\n";
        }

        out << std::string(80, '-') << "\n";
        out << std::setw(30) << std::left << "TOTAL"
            << std::setw(12) << std::right << totalTime
            << std::setw(10) << ""
            << std::setw(12) << ""
            << std::setw(7) << "100.0%\n";

        // Print counters
        if (!counters_.empty()) {
            out << "\nCounters:\n";
            out << std::string(50, '-') << "\n";
            for (const auto& [name, value] : counters_) {
                out << std::setw(40) << std::left << name
                    << std::setw(10) << std::right << value << "\n";
            }
        }

        out << "\n============================================\n";
    }

private:
    QueryProfiler() = default;

    std::unordered_map<std::string, PhaseStats> phases_;
    std::unordered_map<std::string, std::chrono::high_resolution_clock::time_point> activePhases_;
    std::unordered_map<std::string, int64_t> counters_;
};

// RAII helper for automatic phase timing
class ScopedPhase {
public:
    explicit ScopedPhase(const std::string& name) : name_(name) {
        QueryProfiler::instance().beginPhase(name_);
    }

    ~ScopedPhase() {
        QueryProfiler::instance().endPhase(name_);
    }

private:
    std::string name_;
};

// Macros for convenient profiling
#ifdef DIAGON_PROFILING
#define PROFILE_PHASE(name) diagon::util::ScopedPhase _profile_##__LINE__(name)
#define PROFILE_BEGIN(name) diagon::util::QueryProfiler::instance().beginPhase(name)
#define PROFILE_END(name) diagon::util::QueryProfiler::instance().endPhase(name)
#define PROFILE_COUNT(name, value) diagon::util::QueryProfiler::instance().incrementCounter(name, value)
#else
#define PROFILE_PHASE(name) do {} while(0)
#define PROFILE_BEGIN(name) do {} while(0)
#define PROFILE_END(name) do {} while(0)
#define PROFILE_COUNT(name, value) do {} while(0)
#endif

}  // namespace util
}  // namespace diagon
