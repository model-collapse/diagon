// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace diagon {
namespace observability {

/**
 * Metric types
 */
enum class MetricType {
    COUNTER,    // Monotonically increasing value
    GAUGE,      // Value that can go up or down
    HISTOGRAM,  // Distribution of values
    TIMER       // Duration measurements
};

/**
 * Base metric interface
 */
class Metric {
public:
    virtual ~Metric() = default;
    virtual MetricType getType() const = 0;
    virtual std::string getName() const = 0;
    virtual double getValue() const = 0;
};

/**
 * Counter metric - monotonically increasing
 *
 * Use for: requests served, bytes transferred, errors occurred
 */
class Counter : public Metric {
public:
    explicit Counter(const std::string& name)
        : name_(name)
        , value_(0) {}

    MetricType getType() const override { return MetricType::COUNTER; }

    std::string getName() const override { return name_; }

    double getValue() const override { return value_.load(); }

    /**
     * Increment counter by 1
     */
    void inc() { value_.fetch_add(1, std::memory_order_relaxed); }

    /**
     * Increment counter by value
     */
    void add(int64_t value) { value_.fetch_add(value, std::memory_order_relaxed); }

    /**
     * Reset counter to 0
     */
    void reset() { value_.store(0, std::memory_order_relaxed); }

private:
    std::string name_;
    std::atomic<int64_t> value_;
};

/**
 * Gauge metric - value that can go up or down
 *
 * Use for: memory usage, queue size, active connections
 */
class Gauge : public Metric {
public:
    explicit Gauge(const std::string& name)
        : name_(name)
        , value_(0) {}

    MetricType getType() const override { return MetricType::GAUGE; }

    std::string getName() const override { return name_; }

    double getValue() const override {
        return value_.load() / 1000.0;  // Stored as int64 * 1000
    }

    /**
     * Set gauge to value
     */
    void set(double value) {
        // Convert double to int64 for atomic storage (multiply by 1000 for precision)
        int64_t int_value = static_cast<int64_t>(value * 1000.0);
        value_.store(int_value, std::memory_order_relaxed);
    }

    /**
     * Increment gauge
     */
    void inc() { value_.fetch_add(1000, std::memory_order_relaxed); }

    /**
     * Decrement gauge
     */
    void dec() { value_.fetch_sub(1000, std::memory_order_relaxed); }

private:
    std::string name_;
    std::atomic<int64_t> value_;  // Stored as int64 * 1000
};

/**
 * Histogram metric - distribution of values
 *
 * Use for: request latencies, response sizes
 *
 * NOTE: Simplified implementation - full version would use buckets
 */
class Histogram : public Metric {
public:
    explicit Histogram(const std::string& name)
        : name_(name)
        , count_(0)
        , sum_(0) {}

    MetricType getType() const override { return MetricType::HISTOGRAM; }

    std::string getName() const override { return name_; }

    double getValue() const override {
        int64_t count = count_.load();
        if (count == 0)
            return 0.0;
        return (sum_.load() / static_cast<double>(count)) / 1000.0;  // Stored as value * 1000
    }

    /**
     * Observe a value
     */
    void observe(double value) {
        std::lock_guard<std::mutex> lock(mutex_);
        values_.push_back(value);
        count_.fetch_add(1, std::memory_order_relaxed);
        sum_.fetch_add(static_cast<int64_t>(value * 1000.0), std::memory_order_relaxed);
    }

    /**
     * Get count of observations
     */
    int64_t getCount() const { return count_.load(); }

    /**
     * Get sum of all observations
     */
    double getSum() const { return sum_.load() / 1000.0; }

    /**
     * Get average value
     */
    double getAverage() const { return getValue(); }

private:
    std::string name_;
    std::atomic<int64_t> count_;
    std::atomic<int64_t> sum_;  // Stored as value * 1000
    std::vector<double> values_;
    mutable std::mutex mutex_;
};

/**
 * Timer metric - duration measurements
 *
 * Use for: query execution time, index write time
 */
class Timer : public Metric {
public:
    explicit Timer(const std::string& name)
        : name_(name)
        , count_(0)
        , totalNanos_(0) {}

    MetricType getType() const override { return MetricType::TIMER; }

    std::string getName() const override { return name_; }

    double getValue() const override {
        int64_t count = count_.load();
        if (count == 0)
            return 0.0;
        // Return average in milliseconds
        return (totalNanos_.load() / count) / 1000000.0;
    }

    /**
     * Record duration in nanoseconds
     */
    void record(int64_t nanos) {
        count_.fetch_add(1, std::memory_order_relaxed);
        totalNanos_.fetch_add(nanos, std::memory_order_relaxed);
    }

    /**
     * Record duration from chrono duration
     */
    template<typename Duration>
    void record(const Duration& duration) {
        auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
        record(nanos);
    }

    /**
     * Get count of measurements
     */
    int64_t getCount() const { return count_.load(); }

    /**
     * Get total duration in milliseconds
     */
    double getTotalMs() const { return totalNanos_.load() / 1000000.0; }

    /**
     * Get average duration in milliseconds
     */
    double getAverageMs() const { return getValue(); }

private:
    std::string name_;
    std::atomic<int64_t> count_;
    std::atomic<int64_t> totalNanos_;
};

/**
 * RAII timer for automatic duration measurement
 */
class ScopedTimer {
public:
    explicit ScopedTimer(Timer& timer)
        : timer_(timer)
        , start_(std::chrono::steady_clock::now()) {}

    ~ScopedTimer() {
        auto end = std::chrono::steady_clock::now();
        auto duration = end - start_;
        timer_.record(duration);
    }

    // Non-copyable, non-movable
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    Timer& timer_;
    std::chrono::steady_clock::time_point start_;
};

/**
 * Metrics registry
 */
class MetricsRegistry {
public:
    static MetricsRegistry& instance() {
        static MetricsRegistry registry;
        return registry;
    }

    /**
     * Register or get counter
     */
    std::shared_ptr<Counter> getCounter(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = counters_.find(name);
        if (it != counters_.end()) {
            return it->second;
        }

        auto counter = std::make_shared<Counter>(name);
        counters_[name] = counter;
        return counter;
    }

    /**
     * Register or get gauge
     */
    std::shared_ptr<Gauge> getGauge(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = gauges_.find(name);
        if (it != gauges_.end()) {
            return it->second;
        }

        auto gauge = std::make_shared<Gauge>(name);
        gauges_[name] = gauge;
        return gauge;
    }

    /**
     * Register or get histogram
     */
    std::shared_ptr<Histogram> getHistogram(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = histograms_.find(name);
        if (it != histograms_.end()) {
            return it->second;
        }

        auto histogram = std::make_shared<Histogram>(name);
        histograms_[name] = histogram;
        return histogram;
    }

    /**
     * Register or get timer
     */
    std::shared_ptr<Timer> getTimer(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = timers_.find(name);
        if (it != timers_.end()) {
            return it->second;
        }

        auto timer = std::make_shared<Timer>(name);
        timers_[name] = timer;
        return timer;
    }

    /**
     * Get all metrics
     */
    std::vector<std::shared_ptr<Metric>> getAllMetrics() const {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<std::shared_ptr<Metric>> metrics;

        for (const auto& [name, counter] : counters_) {
            metrics.push_back(counter);
        }
        for (const auto& [name, gauge] : gauges_) {
            metrics.push_back(gauge);
        }
        for (const auto& [name, histogram] : histograms_) {
            metrics.push_back(histogram);
        }
        for (const auto& [name, timer] : timers_) {
            metrics.push_back(timer);
        }

        return metrics;
    }

    /**
     * Clear all metrics
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        counters_.clear();
        gauges_.clear();
        histograms_.clear();
        timers_.clear();
    }

private:
    MetricsRegistry() = default;

    std::map<std::string, std::shared_ptr<Counter>> counters_;
    std::map<std::string, std::shared_ptr<Gauge>> gauges_;
    std::map<std::string, std::shared_ptr<Histogram>> histograms_;
    std::map<std::string, std::shared_ptr<Timer>> timers_;
    mutable std::mutex mutex_;
};

}  // namespace observability
}  // namespace diagon
