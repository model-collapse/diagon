// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace diagon {
namespace observability {

/**
 * Health status
 */
enum class HealthStatus {
    HEALTHY,
    DEGRADED,
    UNHEALTHY
};

inline const char* toString(HealthStatus status) {
    switch (status) {
        case HealthStatus::HEALTHY: return "HEALTHY";
        case HealthStatus::DEGRADED: return "DEGRADED";
        case HealthStatus::UNHEALTHY: return "UNHEALTHY";
    }
    return "UNKNOWN";
}

/**
 * Health check result
 */
struct HealthCheckResult {
    HealthStatus status;
    std::string message;
    std::map<std::string, std::string> details;

    HealthCheckResult()
        : status(HealthStatus::HEALTHY) {}

    explicit HealthCheckResult(HealthStatus status_, const std::string& message_ = "")
        : status(status_), message(message_) {}

    /**
     * Create healthy result
     */
    static HealthCheckResult healthy(const std::string& message = "OK") {
        return HealthCheckResult(HealthStatus::HEALTHY, message);
    }

    /**
     * Create degraded result
     */
    static HealthCheckResult degraded(const std::string& message) {
        return HealthCheckResult(HealthStatus::DEGRADED, message);
    }

    /**
     * Create unhealthy result
     */
    static HealthCheckResult unhealthy(const std::string& message) {
        return HealthCheckResult(HealthStatus::UNHEALTHY, message);
    }

    /**
     * Add detail
     */
    void addDetail(const std::string& key, const std::string& value) {
        details[key] = value;
    }
};

/**
 * Health check function type
 */
using HealthCheckFunc = std::function<HealthCheckResult()>;

/**
 * Health check interface
 */
class HealthCheck {
public:
    virtual ~HealthCheck() = default;

    /**
     * Get check name
     */
    virtual std::string getName() const = 0;

    /**
     * Execute health check
     */
    virtual HealthCheckResult check() = 0;

    /**
     * Is this check critical?
     * Critical checks failing = overall status UNHEALTHY
     * Non-critical checks failing = overall status DEGRADED
     */
    virtual bool isCritical() const {
        return true;
    }
};

/**
 * Function-based health check
 */
class FunctionHealthCheck : public HealthCheck {
public:
    FunctionHealthCheck(const std::string& name, HealthCheckFunc func, bool critical = true)
        : name_(name), func_(func), critical_(critical) {}

    std::string getName() const override {
        return name_;
    }

    HealthCheckResult check() override {
        return func_();
    }

    bool isCritical() const override {
        return critical_;
    }

private:
    std::string name_;
    HealthCheckFunc func_;
    bool critical_;
};

/**
 * Overall health report
 */
struct HealthReport {
    HealthStatus overallStatus;
    std::map<std::string, HealthCheckResult> checks;

    HealthReport() : overallStatus(HealthStatus::HEALTHY) {}

    /**
     * Get overall status string
     */
    std::string getOverallStatusString() const {
        return toString(overallStatus);
    }

    /**
     * Is system healthy?
     */
    bool isHealthy() const {
        return overallStatus == HealthStatus::HEALTHY;
    }

    /**
     * Is system degraded?
     */
    bool isDegraded() const {
        return overallStatus == HealthStatus::DEGRADED;
    }

    /**
     * Is system unhealthy?
     */
    bool isUnhealthy() const {
        return overallStatus == HealthStatus::UNHEALTHY;
    }
};

/**
 * Health check registry
 */
class HealthCheckRegistry {
public:
    static HealthCheckRegistry& instance() {
        static HealthCheckRegistry registry;
        return registry;
    }

    /**
     * Register health check
     */
    void registerCheck(std::shared_ptr<HealthCheck> check) {
        std::lock_guard<std::mutex> lock(mutex_);
        checks_[check->getName()] = check;
    }

    /**
     * Register function-based health check
     */
    void registerCheck(const std::string& name, HealthCheckFunc func, bool critical = true) {
        auto check = std::make_shared<FunctionHealthCheck>(name, func, critical);
        registerCheck(check);
    }

    /**
     * Unregister health check
     */
    void unregisterCheck(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        checks_.erase(name);
    }

    /**
     * Run all health checks
     */
    HealthReport runAllChecks() {
        std::lock_guard<std::mutex> lock(mutex_);

        HealthReport report;
        report.overallStatus = HealthStatus::HEALTHY;

        for (const auto& [name, check] : checks_) {
            auto result = check->check();
            report.checks[name] = result;

            // Update overall status
            if (result.status == HealthStatus::UNHEALTHY && check->isCritical()) {
                report.overallStatus = HealthStatus::UNHEALTHY;
            } else if (result.status == HealthStatus::DEGRADED &&
                      report.overallStatus == HealthStatus::HEALTHY) {
                report.overallStatus = HealthStatus::DEGRADED;
            } else if (result.status == HealthStatus::UNHEALTHY &&
                      !check->isCritical() &&
                      report.overallStatus == HealthStatus::HEALTHY) {
                report.overallStatus = HealthStatus::DEGRADED;
            }
        }

        return report;
    }

    /**
     * Run specific health check
     */
    HealthCheckResult runCheck(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = checks_.find(name);
        if (it == checks_.end()) {
            return HealthCheckResult::unhealthy("Check not found: " + name);
        }

        return it->second->check();
    }

    /**
     * Get all registered check names
     */
    std::vector<std::string> getCheckNames() const {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<std::string> names;
        for (const auto& [name, check] : checks_) {
            names.push_back(name);
        }
        return names;
    }

    /**
     * Clear all checks
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        checks_.clear();
    }

private:
    HealthCheckRegistry() = default;

    std::map<std::string, std::shared_ptr<HealthCheck>> checks_;
    mutable std::mutex mutex_;
};

}  // namespace observability
}  // namespace diagon
