// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <chrono>
#include <iostream>
#include <map>
#include <string>
#include <vector>

// Enable search profiling by defining DIAGON_PROFILE_SEARCH
// #define DIAGON_PROFILE_SEARCH

namespace diagon {
namespace util {

#ifdef DIAGON_PROFILE_SEARCH

/**
 * Global search profiler for performance analysis
 * Thread-local to avoid contention in multi-threaded scenarios
 */
class SearchProfiler {
public:
    SearchProfiler() = default;

    // Declared in SearchProfiler.cpp (not inline to ensure single definition)
    static SearchProfiler& instance();

    void record(const std::string& name, int64_t nanoseconds) {
        samples_[name].push_back(nanoseconds);
    }

    const std::map<std::string, std::vector<int64_t>>& samples() const { return samples_; }

    void clear() { samples_.clear(); }

    void reset() { samples_.clear(); }

private:
    std::map<std::string, std::vector<int64_t>> samples_;
};

/**
 * Scoped timer for automatic timing
 */
class ProfileScope {
public:
    explicit ProfileScope(const char* name)
        : name_(name)
        , start_(std::chrono::high_resolution_clock::now()) {}

    ~ProfileScope() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_).count();
        SearchProfiler::instance().record(name_, duration);
    }

private:
    const char* name_;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_;
};

// Helper macros to properly expand __LINE__
#    define PROFILE_CONCAT_IMPL(x, y) x##y
#    define PROFILE_CONCAT(x, y) PROFILE_CONCAT_IMPL(x, y)
#    define PROFILE_SCOPE(name)                                                                    \
        ::diagon::util::ProfileScope PROFILE_CONCAT(__profile_scope_, __LINE__)(name)

#else

// No-op when profiling is disabled
#    define PROFILE_SCOPE(name)                                                                    \
        do {                                                                                       \
        } while (0)

class SearchProfiler {
public:
    static SearchProfiler& instance() {
        static SearchProfiler profiler;
        return profiler;
    }

    void clear() {}
    void reset() {}

    const std::map<std::string, std::vector<int64_t>>& samples() const {
        static std::map<std::string, std::vector<int64_t>> empty;
        return empty;
    }
};

#endif

}  // namespace util
}  // namespace diagon
