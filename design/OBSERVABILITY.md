# Observability Design
## Metrics, Logging, Tracing, and Monitoring for Lucene++

**Status**: Infrastructure Design
**Purpose**: Define observability strategy for production deployments

---

## Overview

Lucene++ provides comprehensive observability through:
- **Metrics**: Real-time performance and health indicators (Prometheus)
- **Logging**: Structured application logs (spdlog)
- **Tracing**: Distributed request tracing (OpenTelemetry)
- **Health checks**: Endpoint for load balancer integration
- **Dashboards**: Pre-built Grafana dashboards

**Design Goals**:
- Low overhead (<1% CPU for observability)
- Production-ready integrations
- Actionable alerts for operators
- Developer-friendly debugging

---

## Metrics

### Metrics Framework: Prometheus

**Library**: prometheus-cpp

**Why Prometheus**:
- Industry standard for metrics
- Pull-based model (no agent needed)
- Powerful query language (PromQL)
- Native Grafana integration

### Metric Categories

#### 1. Indexing Metrics

```cpp
// Indexing throughput
counter documents_indexed_total{};

// Indexing latency
histogram document_indexing_duration_seconds{
    buckets: {0.001, 0.01, 0.1, 1.0, 10.0}
};

// Flush operations
counter flushes_total{};
histogram flush_duration_seconds{
    buckets: {0.1, 1.0, 5.0, 10.0, 30.0}
};

// Commit operations
counter commits_total{};
histogram commit_duration_seconds{
    buckets: {0.5, 1.0, 5.0, 10.0, 30.0}
};

// RAM buffer usage
gauge ram_buffer_used_bytes{};
gauge ram_buffer_limit_bytes{};
```

#### 2. Query Metrics

```cpp
// Query throughput
counter queries_total{
    labels: {"query_type"}  // term, boolean, phrase, etc.
};

// Query latency
histogram query_duration_seconds{
    labels: {"query_type"},
    buckets: {0.001, 0.01, 0.1, 0.5, 1.0, 5.0}
};

// Query timeout
counter queries_timeout_total{};

// Query errors
counter queries_error_total{
    labels: {"error_type"}  // timeout, oom, parse_error, etc.
};
```

#### 3. Segment Metrics

```cpp
// Segment count
gauge segments_count{};

// Segment size
gauge segments_total_size_bytes{};
histogram segment_size_bytes{
    buckets: {1MB, 10MB, 100MB, 1GB, 10GB}
};

// Documents per segment
histogram segment_document_count{
    buckets: {100, 1K, 10K, 100K, 1M}
};

// Deleted documents
gauge deleted_documents_total{};
gauge deleted_documents_ratio{};  // percentage
```

#### 4. Merge Metrics

```cpp
// Merge operations
counter merges_total{};
counter merges_running{};

// Merge throughput
counter merge_bytes_total{};
histogram merge_duration_seconds{
    buckets: {1, 10, 60, 300, 600}  // seconds
};

// Write amplification
gauge write_amplification_factor{};
```

#### 5. Memory Metrics

```cpp
// Heap usage
gauge heap_used_bytes{};
gauge heap_limit_bytes{};

// Query memory
gauge query_memory_used_bytes{};
gauge query_memory_limit_bytes{};

// Column arena
gauge column_arena_allocated_bytes{};
counter column_arena_allocations_total{};
```

#### 6. I/O Metrics

```cpp
// Disk reads
counter disk_reads_total{};
counter disk_read_bytes_total{};
histogram disk_read_duration_seconds{
    buckets: {0.001, 0.01, 0.1, 1.0}
};

// Disk writes
counter disk_writes_total{};
counter disk_write_bytes_total{};
histogram disk_write_duration_seconds{
    buckets: {0.001, 0.01, 0.1, 1.0}
};

// Cache hits
counter cache_hits_total{
    labels: {"cache_type"}  // filter, fielddata, etc.
};
counter cache_misses_total{
    labels: {"cache_type"}
};
```

### Metrics Implementation

```cpp
class MetricsCollector {
public:
    static MetricsCollector& instance() {
        static MetricsCollector instance;
        return instance;
    }

    // Indexing metrics
    void recordDocumentIndexed() {
        documents_indexed_total_.Increment();
    }

    void recordIndexingLatency(double seconds) {
        document_indexing_duration_seconds_.Observe(seconds);
    }

    // Query metrics
    void recordQuery(const std::string& queryType, double durationSeconds) {
        queries_total_.Add({{"query_type", queryType}}, 1);
        query_duration_seconds_.Add({{"query_type", queryType}}).Observe(durationSeconds);
    }

    void recordQueryTimeout() {
        queries_timeout_total_.Increment();
    }

    // Segment metrics
    void updateSegmentCount(int count) {
        segments_count_.Set(count);
    }

    void updateSegmentSize(int64_t bytes) {
        segments_total_size_bytes_.Set(bytes);
        segment_size_bytes_.Observe(bytes);
    }

    // Memory metrics
    void updateMemoryUsage(int64_t heapUsed, int64_t heapLimit) {
        heap_used_bytes_.Set(heapUsed);
        heap_limit_bytes_.Set(heapLimit);
    }

    // Expose metrics endpoint
    std::string serializeMetrics() {
        return prometheus::TextSerializer().Serialize(registry_);
    }

private:
    prometheus::Registry registry_;

    // Metric definitions
    prometheus::Counter& documents_indexed_total_;
    prometheus::Histogram& document_indexing_duration_seconds_;
    // ... other metrics
};
```

### Metrics Endpoint

Expose metrics via HTTP endpoint for Prometheus scraping:

```cpp
#include <httplib.h>  // cpp-httplib for simple HTTP server

void startMetricsServer(int port = 9090) {
    httplib::Server server;

    server.Get("/metrics", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(
            MetricsCollector::instance().serializeMetrics(),
            "text/plain"
        );
    });

    server.listen("0.0.0.0", port);
}
```

---

## Logging

### Logging Framework: spdlog

**Library**: spdlog (header-only, high-performance)

**Why spdlog**:
- Fast (1M+ log entries/sec)
- Header-only (easy integration)
- Multiple sinks (console, file, syslog)
- Structured logging support

### Log Levels

```cpp
enum class LogLevel {
    TRACE,    // Detailed debugging (disabled in production)
    DEBUG,    // Development diagnostics
    INFO,     // Normal operation (indexing, query execution)
    WARN,     // Recoverable errors (query timeout, merge delay)
    ERROR,    // Errors requiring attention (OOM, disk full)
    CRITICAL  // System failure (crash imminent)
};
```

### Log Configuration

```cpp
void setupLogging() {
    // Console sink (development)
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::info);

    // File sink (production)
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        "lucenepp.log",
        1024 * 1024 * 100,  // 100MB per file
        3                   // Keep 3 rotated files
    );
    file_sink->set_level(spdlog::level::debug);

    // Syslog sink (production)
    auto syslog_sink = std::make_shared<spdlog::sinks::syslog_sink_mt>(
        "lucenepp",
        LOG_PID,
        LOG_USER
    );
    syslog_sink->set_level(spdlog::level::warn);

    // Create logger with multiple sinks
    spdlog::logger logger("lucenepp", {console_sink, file_sink, syslog_sink});
    logger.set_level(spdlog::level::trace);  // Capture all, filter at sink level
    logger.set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] [%t] %v");

    spdlog::set_default_logger(std::make_shared<spdlog::logger>(logger));
}
```

### Structured Logging

```cpp
// Use fmt-style formatting with structured fields
SPDLOG_INFO("Document indexed: id={}, size={} bytes, duration={} ms",
    docID, docSize, durationMs);

SPDLOG_WARN("Query timeout: query_type={}, timeout={} ms, docs_searched={}",
    queryType, timeoutMs, docsSearched);

SPDLOG_ERROR("Merge failed: segment_count={}, error={}",
    segmentCount, error.what());

SPDLOG_CRITICAL("OOM detected: heap_used={} MB, heap_limit={} MB",
    heapUsedMB, heapLimitMB);
```

### Contextual Logging

```cpp
class LogContext {
public:
    LogContext(const std::string& operation, const std::string& id)
        : operation_(operation), id_(id)
    {
        SPDLOG_DEBUG("START: operation={}, id={}", operation_, id_);
        start_ = std::chrono::steady_clock::now();
    }

    ~LogContext() {
        auto elapsed = std::chrono::steady_clock::now() - start_;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        SPDLOG_DEBUG("END: operation={}, id={}, duration={} ms",
            operation_, id_, ms);
    }

private:
    std::string operation_;
    std::string id_;
    std::chrono::steady_clock::time_point start_;
};

// Usage
void IndexWriter::addDocument(const Document& doc) {
    LogContext ctx("addDocument", doc.get("id"));
    // ... indexing logic ...
}
```

---

## Tracing

### Tracing Framework: OpenTelemetry

**Library**: opentelemetry-cpp

**Why OpenTelemetry**:
- Vendor-neutral standard
- Distributed tracing across services
- Integrates with Jaeger, Zipkin, etc.

### Trace Spans

```cpp
#include <opentelemetry/trace/provider.h>

namespace trace_api = opentelemetry::trace;

class TracingManager {
public:
    static void init() {
        // Setup tracing provider (Jaeger, Zipkin, etc.)
        auto provider = createJaegerTraceProvider();
        trace_api::Provider::SetTracerProvider(provider);
    }

    static trace_api::Tracer& getTracer() {
        static auto tracer = trace_api::Provider::GetTracerProvider()
            ->GetTracer("lucenepp", "1.0.0");
        return *tracer;
    }
};

// Usage
void IndexSearcher::search(const Query& query, int n) {
    auto span = TracingManager::getTracer().StartSpan("search");
    span->SetAttribute("query.type", query.getType());
    span->SetAttribute("query.topN", n);

    auto scope = trace_api::Scope(span);

    try {
        // Execute search
        TopDocs results = executeSearch(query, n);

        span->SetAttribute("results.totalHits", results.totalHits);
        span->SetStatus(trace_api::StatusCode::kOk);
    } catch (const std::exception& e) {
        span->SetStatus(trace_api::StatusCode::kError, e.what());
        throw;
    }

    span->End();
}
```

### Nested Spans

```cpp
void IndexSearcher::search(const Query& query, int n) {
    auto searchSpan = TracingManager::getTracer().StartSpan("search");
    auto searchScope = trace_api::Scope(searchSpan);

    // Child span: query rewrite
    {
        auto rewriteSpan = TracingManager::getTracer().StartSpan("query_rewrite");
        auto rewriteScope = trace_api::Scope(rewriteSpan);
        query.rewrite();
    }

    // Child span: execute on each segment
    for (const auto& segment : segments) {
        auto segmentSpan = TracingManager::getTracer().StartSpan("search_segment");
        segmentSpan->SetAttribute("segment.id", segment.id);
        auto segmentScope = trace_api::Scope(segmentSpan);

        searchSegment(segment, query, n);
    }

    searchSpan->End();
}
```

---

## Health Checks

### Health Check Endpoint

```cpp
struct HealthStatus {
    enum class Status {
        HEALTHY,    // All systems operational
        DEGRADED,   // Some issues but functional
        UNHEALTHY   // Critical failures
    };

    Status status;
    std::string message;
    std::map<std::string, std::string> details;
};

class HealthChecker {
public:
    HealthStatus check() {
        HealthStatus health;
        health.status = HealthStatus::Status::HEALTHY;

        // Check disk space
        if (getDiskSpaceGB() < 10) {
            health.status = HealthStatus::Status::DEGRADED;
            health.details["disk_space"] = "low";
            health.message = "Disk space below 10GB";
        }

        // Check memory usage
        double memoryUsagePercent = getMemoryUsagePercent();
        if (memoryUsagePercent > 90) {
            health.status = HealthStatus::Status::DEGRADED;
            health.details["memory"] = "high";
            health.message = "Memory usage >90%";
        }

        // Check merge backlog
        int mergeBacklog = getMergeBacklogCount();
        if (mergeBacklog > 10) {
            health.status = HealthStatus::Status::DEGRADED;
            health.details["merge_backlog"] = std::to_string(mergeBacklog);
        }

        // Check for critical errors
        if (hasCriticalError()) {
            health.status = HealthStatus::Status::UNHEALTHY;
            health.message = "Critical error detected";
        }

        return health;
    }
};

// HTTP endpoint
server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
    HealthStatus health = HealthChecker().check();

    int statusCode = 200;
    if (health.status == HealthStatus::Status::DEGRADED) {
        statusCode = 503;  // Service Unavailable
    } else if (health.status == HealthStatus::Status::UNHEALTHY) {
        statusCode = 503;
    }

    res.status = statusCode;
    res.set_content(serializeHealthStatus(health), "application/json");
});
```

---

## Monitoring Dashboards

### Grafana Dashboard: Indexing Performance

**Panels**:

1. **Indexing Throughput** (Graph)
   - Query: `rate(documents_indexed_total[5m])`
   - Y-axis: docs/sec

2. **Indexing Latency** (Heatmap)
   - Query: `histogram_quantile(0.95, rate(document_indexing_duration_seconds_bucket[5m]))`
   - Y-axis: seconds (p95)

3. **RAM Buffer Usage** (Gauge)
   - Query: `ram_buffer_used_bytes / ram_buffer_limit_bytes * 100`
   - Y-axis: percentage

4. **Flush Rate** (Graph)
   - Query: `rate(flushes_total[5m])`

5. **Segment Count** (Graph)
   - Query: `segments_count`

### Grafana Dashboard: Query Performance

**Panels**:

1. **Query Rate** (Graph)
   - Query: `sum(rate(queries_total[5m])) by (query_type)`
   - Legend: query_type

2. **Query Latency p50/p95/p99** (Graph)
   - Queries:
     - `histogram_quantile(0.50, rate(query_duration_seconds_bucket[5m]))`
     - `histogram_quantile(0.95, rate(query_duration_seconds_bucket[5m]))`
     - `histogram_quantile(0.99, rate(query_duration_seconds_bucket[5m]))`

3. **Query Timeout Rate** (Stat)
   - Query: `rate(queries_timeout_total[5m])`
   - Alert: > 0.05 (5% timeout rate)

4. **Query Errors** (Table)
   - Query: `sum(rate(queries_error_total[5m])) by (error_type)`

### Grafana Dashboard: System Health

**Panels**:

1. **Heap Usage** (Graph)
   - Query: `heap_used_bytes / heap_limit_bytes * 100`

2. **Disk I/O** (Graph)
   - Read: `rate(disk_read_bytes_total[5m])`
   - Write: `rate(disk_write_bytes_total[5m])`

3. **Write Amplification** (Gauge)
   - Query: `write_amplification_factor`
   - Alert: > 20

4. **Cache Hit Rate** (Graph)
   - Query: `sum(rate(cache_hits_total[5m])) / (sum(rate(cache_hits_total[5m])) + sum(rate(cache_misses_total[5m])))`

---

## Alerting

### Prometheus Alerts

**prometheus.rules.yml**:
```yaml
groups:
  - name: lucenepp_alerts
    rules:
      # High query timeout rate
      - alert: HighQueryTimeoutRate
        expr: rate(queries_timeout_total[5m]) > 0.05
        for: 5m
        labels:
          severity: warning
        annotations:
          summary: "High query timeout rate (>5%)"

      # High memory usage
      - alert: HighMemoryUsage
        expr: heap_used_bytes / heap_limit_bytes > 0.9
        for: 5m
        labels:
          severity: warning
        annotations:
          summary: "Memory usage >90%"

      # High write amplification
      - alert: HighWriteAmplification
        expr: write_amplification_factor > 25
        for: 30m
        labels:
          severity: warning
        annotations:
          summary: "Write amplification >25×"

      # Merge backlog
      - alert: MergeBacklog
        expr: merges_running > 10
        for: 10m
        labels:
          severity: warning
        annotations:
          summary: "Large merge backlog (>10 concurrent merges)"

      # Service unhealthy
      - alert: ServiceUnhealthy
        expr: up{job="lucenepp"} == 0
        for: 1m
        labels:
          severity: critical
        annotations:
          summary: "Lucene++ service is down"
```

---

## Performance Overhead

**Metrics Collection**: <0.5% CPU overhead
**Logging** (INFO level): <0.3% CPU overhead
**Tracing** (sampling rate 1%): <0.1% CPU overhead

**Total Observability Overhead**: <1% CPU

---

## Summary

**Key Components**:
1. ✅ **Metrics**: Prometheus with 50+ metrics covering indexing, query, merge, memory, I/O
2. ✅ **Logging**: spdlog with structured logging, multiple sinks, log rotation
3. ✅ **Tracing**: OpenTelemetry for distributed tracing
4. ✅ **Health checks**: HTTP endpoint for load balancer integration
5. ✅ **Dashboards**: Pre-built Grafana dashboards for indexing, query, and system health
6. ✅ **Alerting**: Prometheus alerts for critical conditions

**Best Practices**:
- Enable INFO-level logging in production
- Sample 1-10% of traces (adjustable based on traffic)
- Set up alerts for timeout rate, memory usage, write amplification
- Monitor p95/p99 query latency (not just average)
- Track write amplification for SSD lifetime estimation

**Integration**:
- Prometheus: Scrape `/metrics` endpoint every 15 seconds
- Grafana: Import pre-built dashboards from `dashboards/`
- Jaeger: Configure OpenTelemetry exporter for trace collection
- Alertmanager: Route alerts to Slack, PagerDuty, email

---

**Design Status**: Complete ✅
**Design Phase**: **COMPLETE** - All infrastructure documents finished!
