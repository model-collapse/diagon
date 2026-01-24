// Copyright 2024 Diagon Project
// Licensed under the Apache License, Version 2.0

#include "diagon/observability/HealthCheck.h"
#include "diagon/observability/Metrics.h"

#include <gtest/gtest.h>

#include <thread>

using namespace diagon::observability;

// ==================== MetricType Tests ====================

TEST(MetricTypeTest, EnumValues) {
    EXPECT_NE(MetricType::COUNTER, MetricType::GAUGE);
    EXPECT_NE(MetricType::GAUGE, MetricType::HISTOGRAM);
    EXPECT_NE(MetricType::HISTOGRAM, MetricType::TIMER);
}

// ==================== Counter Tests ====================

TEST(CounterTest, Construction) {
    Counter counter("test_counter");

    EXPECT_EQ("test_counter", counter.getName());
    EXPECT_EQ(MetricType::COUNTER, counter.getType());
    EXPECT_EQ(0, counter.getValue());
}

TEST(CounterTest, Increment) {
    Counter counter("test_counter");

    counter.inc();
    EXPECT_EQ(1, counter.getValue());

    counter.inc();
    counter.inc();
    EXPECT_EQ(3, counter.getValue());
}

TEST(CounterTest, Add) {
    Counter counter("test_counter");

    counter.add(10);
    EXPECT_EQ(10, counter.getValue());

    counter.add(5);
    EXPECT_EQ(15, counter.getValue());
}

TEST(CounterTest, Reset) {
    Counter counter("test_counter");

    counter.add(100);
    EXPECT_EQ(100, counter.getValue());

    counter.reset();
    EXPECT_EQ(0, counter.getValue());
}

// ==================== Gauge Tests ====================

TEST(GaugeTest, Construction) {
    Gauge gauge("test_gauge");

    EXPECT_EQ("test_gauge", gauge.getName());
    EXPECT_EQ(MetricType::GAUGE, gauge.getType());
    EXPECT_DOUBLE_EQ(0.0, gauge.getValue());
}

TEST(GaugeTest, Set) {
    Gauge gauge("test_gauge");

    gauge.set(42.5);
    EXPECT_DOUBLE_EQ(42.5, gauge.getValue());

    gauge.set(100.0);
    EXPECT_DOUBLE_EQ(100.0, gauge.getValue());
}

TEST(GaugeTest, IncDec) {
    Gauge gauge("test_gauge");

    gauge.inc();
    EXPECT_DOUBLE_EQ(1.0, gauge.getValue());

    gauge.inc();
    gauge.inc();
    EXPECT_DOUBLE_EQ(3.0, gauge.getValue());

    gauge.dec();
    EXPECT_DOUBLE_EQ(2.0, gauge.getValue());
}

// ==================== Histogram Tests ====================

TEST(HistogramTest, Construction) {
    Histogram histogram("test_histogram");

    EXPECT_EQ("test_histogram", histogram.getName());
    EXPECT_EQ(MetricType::HISTOGRAM, histogram.getType());
    EXPECT_EQ(0, histogram.getCount());
    EXPECT_DOUBLE_EQ(0.0, histogram.getSum());
}

TEST(HistogramTest, Observe) {
    Histogram histogram("test_histogram");

    histogram.observe(10.0);
    histogram.observe(20.0);
    histogram.observe(30.0);

    EXPECT_EQ(3, histogram.getCount());
    EXPECT_DOUBLE_EQ(60.0, histogram.getSum());
    EXPECT_DOUBLE_EQ(20.0, histogram.getAverage());
}

TEST(HistogramTest, Average) {
    Histogram histogram("test_histogram");

    histogram.observe(5.0);
    histogram.observe(15.0);
    histogram.observe(25.0);
    histogram.observe(35.0);

    EXPECT_EQ(4, histogram.getCount());
    EXPECT_DOUBLE_EQ(20.0, histogram.getAverage());
}

// ==================== Timer Tests ====================

TEST(TimerTest, Construction) {
    Timer timer("test_timer");

    EXPECT_EQ("test_timer", timer.getName());
    EXPECT_EQ(MetricType::TIMER, timer.getType());
    EXPECT_EQ(0, timer.getCount());
    EXPECT_DOUBLE_EQ(0.0, timer.getTotalMs());
}

TEST(TimerTest, RecordNanos) {
    Timer timer("test_timer");

    timer.record(std::chrono::nanoseconds(1000000));  // 1ms
    timer.record(std::chrono::nanoseconds(2000000));  // 2ms
    timer.record(std::chrono::nanoseconds(3000000));  // 3ms

    EXPECT_EQ(3, timer.getCount());
    EXPECT_DOUBLE_EQ(6.0, timer.getTotalMs());
    EXPECT_DOUBLE_EQ(2.0, timer.getAverageMs());
}

TEST(TimerTest, RecordDuration) {
    Timer timer("test_timer");

    timer.record(std::chrono::milliseconds(10));
    timer.record(std::chrono::milliseconds(20));

    EXPECT_EQ(2, timer.getCount());
    EXPECT_DOUBLE_EQ(30.0, timer.getTotalMs());
    EXPECT_DOUBLE_EQ(15.0, timer.getAverageMs());
}

TEST(ScopedTimerTest, AutomaticTiming) {
    Timer timer("test_timer");

    {
        ScopedTimer scoped(timer);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(1, timer.getCount());
    EXPECT_GT(timer.getTotalMs(), 9.0);  // Should be at least 10ms
}

// ==================== MetricsRegistry Tests ====================

TEST(MetricsRegistryTest, GetCounter) {
    auto& registry = MetricsRegistry::instance();
    registry.clear();

    auto counter1 = registry.getCounter("test_counter");
    ASSERT_NE(nullptr, counter1);
    EXPECT_EQ("test_counter", counter1->getName());

    // Getting same counter returns same instance
    auto counter2 = registry.getCounter("test_counter");
    EXPECT_EQ(counter1, counter2);
}

TEST(MetricsRegistryTest, GetGauge) {
    auto& registry = MetricsRegistry::instance();
    registry.clear();

    auto gauge = registry.getGauge("test_gauge");
    ASSERT_NE(nullptr, gauge);
    EXPECT_EQ("test_gauge", gauge->getName());
}

TEST(MetricsRegistryTest, GetHistogram) {
    auto& registry = MetricsRegistry::instance();
    registry.clear();

    auto histogram = registry.getHistogram("test_histogram");
    ASSERT_NE(nullptr, histogram);
    EXPECT_EQ("test_histogram", histogram->getName());
}

TEST(MetricsRegistryTest, GetTimer) {
    auto& registry = MetricsRegistry::instance();
    registry.clear();

    auto timer = registry.getTimer("test_timer");
    ASSERT_NE(nullptr, timer);
    EXPECT_EQ("test_timer", timer->getName());
}

TEST(MetricsRegistryTest, GetAllMetrics) {
    auto& registry = MetricsRegistry::instance();
    registry.clear();

    registry.getCounter("counter1");
    registry.getGauge("gauge1");
    registry.getHistogram("histogram1");
    registry.getTimer("timer1");

    auto metrics = registry.getAllMetrics();
    EXPECT_EQ(4, metrics.size());
}

TEST(MetricsRegistryTest, Clear) {
    auto& registry = MetricsRegistry::instance();
    registry.clear();  // Clear first to ensure clean state

    registry.getCounter("counter1");
    registry.getGauge("gauge1");

    EXPECT_EQ(2, registry.getAllMetrics().size());

    registry.clear();
    EXPECT_EQ(0, registry.getAllMetrics().size());
}

// ==================== HealthStatus Tests ====================

TEST(HealthStatusTest, ToString) {
    EXPECT_STREQ("HEALTHY", toString(HealthStatus::HEALTHY));
    EXPECT_STREQ("DEGRADED", toString(HealthStatus::DEGRADED));
    EXPECT_STREQ("UNHEALTHY", toString(HealthStatus::UNHEALTHY));
}

// ==================== HealthCheckResult Tests ====================

TEST(HealthCheckResultTest, Construction) {
    HealthCheckResult result;

    EXPECT_EQ(HealthStatus::HEALTHY, result.status);
    EXPECT_TRUE(result.message.empty());
}

TEST(HealthCheckResultTest, ConstructionWithStatus) {
    HealthCheckResult result(HealthStatus::DEGRADED, "Slow response");

    EXPECT_EQ(HealthStatus::DEGRADED, result.status);
    EXPECT_EQ("Slow response", result.message);
}

TEST(HealthCheckResultTest, Healthy) {
    auto result = HealthCheckResult::healthy("All systems operational");

    EXPECT_EQ(HealthStatus::HEALTHY, result.status);
    EXPECT_EQ("All systems operational", result.message);
}

TEST(HealthCheckResultTest, Degraded) {
    auto result = HealthCheckResult::degraded("High latency detected");

    EXPECT_EQ(HealthStatus::DEGRADED, result.status);
    EXPECT_EQ("High latency detected", result.message);
}

TEST(HealthCheckResultTest, Unhealthy) {
    auto result = HealthCheckResult::unhealthy("Service unavailable");

    EXPECT_EQ(HealthStatus::UNHEALTHY, result.status);
    EXPECT_EQ("Service unavailable", result.message);
}

TEST(HealthCheckResultTest, AddDetail) {
    auto result = HealthCheckResult::healthy();

    result.addDetail("cpu_usage", "75%");
    result.addDetail("memory_usage", "60%");

    EXPECT_EQ(2, result.details.size());
    EXPECT_EQ("75%", result.details["cpu_usage"]);
    EXPECT_EQ("60%", result.details["memory_usage"]);
}

// ==================== FunctionHealthCheck Tests ====================

TEST(FunctionHealthCheckTest, Construction) {
    auto check = std::make_shared<FunctionHealthCheck>(
        "test_check", []() { return HealthCheckResult::healthy(); });

    EXPECT_EQ("test_check", check->getName());
    EXPECT_TRUE(check->isCritical());
}

TEST(FunctionHealthCheckTest, NonCritical) {
    auto check = std::make_shared<FunctionHealthCheck>(
        "test_check", []() { return HealthCheckResult::healthy(); }, false);

    EXPECT_FALSE(check->isCritical());
}

TEST(FunctionHealthCheckTest, Execute) {
    auto check = std::make_shared<FunctionHealthCheck>(
        "test_check", []() { return HealthCheckResult::healthy("OK"); });

    auto result = check->check();
    EXPECT_EQ(HealthStatus::HEALTHY, result.status);
    EXPECT_EQ("OK", result.message);
}

// ==================== HealthReport Tests ====================

TEST(HealthReportTest, Construction) {
    HealthReport report;

    EXPECT_EQ(HealthStatus::HEALTHY, report.overallStatus);
    EXPECT_TRUE(report.checks.empty());
}

TEST(HealthReportTest, IsHealthy) {
    HealthReport report;
    report.overallStatus = HealthStatus::HEALTHY;

    EXPECT_TRUE(report.isHealthy());
    EXPECT_FALSE(report.isDegraded());
    EXPECT_FALSE(report.isUnhealthy());
}

TEST(HealthReportTest, IsDegraded) {
    HealthReport report;
    report.overallStatus = HealthStatus::DEGRADED;

    EXPECT_FALSE(report.isHealthy());
    EXPECT_TRUE(report.isDegraded());
    EXPECT_FALSE(report.isUnhealthy());
}

TEST(HealthReportTest, IsUnhealthy) {
    HealthReport report;
    report.overallStatus = HealthStatus::UNHEALTHY;

    EXPECT_FALSE(report.isHealthy());
    EXPECT_FALSE(report.isDegraded());
    EXPECT_TRUE(report.isUnhealthy());
}

// ==================== HealthCheckRegistry Tests ====================

TEST(HealthCheckRegistryTest, RegisterFunction) {
    auto& registry = HealthCheckRegistry::instance();
    registry.clear();

    registry.registerCheck("database", []() { return HealthCheckResult::healthy("Connected"); });

    auto names = registry.getCheckNames();
    EXPECT_EQ(1, names.size());
    EXPECT_EQ("database", names[0]);
}

TEST(HealthCheckRegistryTest, RegisterHealthCheck) {
    auto& registry = HealthCheckRegistry::instance();
    registry.clear();

    auto check = std::make_shared<FunctionHealthCheck>(
        "api", []() { return HealthCheckResult::healthy(); });

    registry.registerCheck(check);

    auto names = registry.getCheckNames();
    EXPECT_EQ(1, names.size());
    EXPECT_EQ("api", names[0]);
}

TEST(HealthCheckRegistryTest, UnregisterCheck) {
    auto& registry = HealthCheckRegistry::instance();
    registry.clear();

    registry.registerCheck("test", []() { return HealthCheckResult::healthy(); });

    EXPECT_EQ(1, registry.getCheckNames().size());

    registry.unregisterCheck("test");
    EXPECT_EQ(0, registry.getCheckNames().size());
}

TEST(HealthCheckRegistryTest, RunAllChecksAllHealthy) {
    auto& registry = HealthCheckRegistry::instance();
    registry.clear();

    registry.registerCheck("check1", []() { return HealthCheckResult::healthy("OK"); });

    registry.registerCheck("check2", []() { return HealthCheckResult::healthy("OK"); });

    auto report = registry.runAllChecks();

    EXPECT_EQ(HealthStatus::HEALTHY, report.overallStatus);
    EXPECT_EQ(2, report.checks.size());
    EXPECT_TRUE(report.isHealthy());
}

TEST(HealthCheckRegistryTest, RunAllChecksWithDegraded) {
    auto& registry = HealthCheckRegistry::instance();
    registry.clear();

    registry.registerCheck("check1", []() { return HealthCheckResult::healthy(); });

    registry.registerCheck("check2", []() { return HealthCheckResult::degraded("Slow"); });

    auto report = registry.runAllChecks();

    EXPECT_EQ(HealthStatus::DEGRADED, report.overallStatus);
    EXPECT_TRUE(report.isDegraded());
}

TEST(HealthCheckRegistryTest, RunAllChecksWithUnhealthy) {
    auto& registry = HealthCheckRegistry::instance();
    registry.clear();

    registry.registerCheck(
        "check1", []() { return HealthCheckResult::healthy(); }, true);  // critical

    registry.registerCheck(
        "check2", []() { return HealthCheckResult::unhealthy("Failed"); }, true);  // critical

    auto report = registry.runAllChecks();

    EXPECT_EQ(HealthStatus::UNHEALTHY, report.overallStatus);
    EXPECT_TRUE(report.isUnhealthy());
}

TEST(HealthCheckRegistryTest, RunAllChecksNonCriticalUnhealthy) {
    auto& registry = HealthCheckRegistry::instance();
    registry.clear();

    registry.registerCheck(
        "check1", []() { return HealthCheckResult::healthy(); }, true);  // critical

    registry.registerCheck(
        "check2", []() { return HealthCheckResult::unhealthy("Failed"); }, false);  // non-critical

    auto report = registry.runAllChecks();

    // Non-critical unhealthy should result in DEGRADED, not UNHEALTHY
    EXPECT_EQ(HealthStatus::DEGRADED, report.overallStatus);
    EXPECT_TRUE(report.isDegraded());
}

TEST(HealthCheckRegistryTest, RunSpecificCheck) {
    auto& registry = HealthCheckRegistry::instance();
    registry.clear();

    registry.registerCheck("database", []() { return HealthCheckResult::healthy("Connected"); });

    auto result = registry.runCheck("database");

    EXPECT_EQ(HealthStatus::HEALTHY, result.status);
    EXPECT_EQ("Connected", result.message);
}

TEST(HealthCheckRegistryTest, RunNonExistentCheck) {
    auto& registry = HealthCheckRegistry::instance();
    registry.clear();

    auto result = registry.runCheck("nonexistent");

    EXPECT_EQ(HealthStatus::UNHEALTHY, result.status);
}

// ==================== Integration Tests ====================

TEST(ObservabilityIntegrationTest, MetricsAndHealthChecks) {
    // Set up metrics
    auto& metrics = MetricsRegistry::instance();
    metrics.clear();

    auto requestCounter = metrics.getCounter("requests");
    auto errorCounter = metrics.getCounter("errors");

    // Simulate some traffic
    requestCounter->add(100);
    errorCounter->add(5);

    // Set up health check based on error rate
    auto& health = HealthCheckRegistry::instance();
    health.clear();

    health.registerCheck("error_rate", [&]() {
        double errorRate = static_cast<double>(errorCounter->getValue()) /
                           static_cast<double>(requestCounter->getValue());

        if (errorRate > 0.1) {
            return HealthCheckResult::unhealthy("Error rate too high");
        } else if (errorRate > 0.05) {
            return HealthCheckResult::degraded("Error rate elevated");
        } else {
            return HealthCheckResult::healthy("Error rate normal");
        }
    });

    auto report = health.runAllChecks();

    // Error rate is 5/100 = 0.05, should be healthy
    EXPECT_EQ(HealthStatus::HEALTHY, report.overallStatus);
}
