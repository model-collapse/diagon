#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Lightweight profiling using RDTSC (CPU timestamp counter)
class ProfileHelper {
public:
    struct Stats {
        uint64_t calls = 0;
        uint64_t total_cycles = 0;
        uint64_t min_cycles = UINT64_MAX;
        uint64_t max_cycles = 0;

        double avg_cycles() const { return calls > 0 ? (double)total_cycles / calls : 0; }
    };

    static ProfileHelper& getInstance() {
        static ProfileHelper instance;
        return instance;
    }

    // Read CPU timestamp counter
    static inline uint64_t rdtsc() {
        uint32_t lo, hi;
        __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
        return ((uint64_t)hi << 32) | lo;
    }

    // Start timing a section
    void start(const std::string& name) { starts_[name] = rdtsc(); }

    // End timing a section
    void end(const std::string& name) {
        uint64_t end_cycles = rdtsc();
        auto it = starts_.find(name);
        if (it != starts_.end()) {
            uint64_t elapsed = end_cycles - it->second;

            auto& stat = stats_[name];
            stat.calls++;
            stat.total_cycles += elapsed;
            stat.min_cycles = std::min(stat.min_cycles, elapsed);
            stat.max_cycles = std::max(stat.max_cycles, elapsed);

            starts_.erase(it);
        }
    }

    // Get statistics
    const std::map<std::string, Stats>& getStats() const { return stats_; }

    // Reset all stats
    void reset() {
        stats_.clear();
        starts_.clear();
    }

    // Print report
    void printReport(double cpu_freq_ghz = 2.5) const {
        printf("\n=== ProfileHelper Report (CPU: %.2f GHz) ===\n", cpu_freq_ghz);
        printf("%-40s %12s %12s %12s %12s %12s\n", "Section", "Calls", "Avg Cycles", "Min Cycles",
               "Max Cycles", "Avg Time(ns)");
        printf("%-40s %12s %12s %12s %12s %12s\n", "----------------------------------------",
               "------------", "------------", "------------", "------------", "------------");

        for (const auto& [name, stat] : stats_) {
            printf("%-40s %12lu %12.0f %12lu %12lu %12.1f\n", name.c_str(), stat.calls,
                   stat.avg_cycles(), stat.min_cycles, stat.max_cycles,
                   stat.avg_cycles() / cpu_freq_ghz);
        }
        printf("\n");
    }

private:
    ProfileHelper() = default;
    std::map<std::string, uint64_t> starts_;
    std::map<std::string, Stats> stats_;
};

// RAII helper for automatic start/end
class ScopedProfile {
public:
    explicit ScopedProfile(const std::string& name)
        : name_(name) {
        ProfileHelper::getInstance().start(name_);
    }

    ~ScopedProfile() { ProfileHelper::getInstance().end(name_); }

private:
    std::string name_;
};

// Macro for easy profiling
#define PROFILE_SCOPE(name) ScopedProfile _profile_##__LINE__(name)
