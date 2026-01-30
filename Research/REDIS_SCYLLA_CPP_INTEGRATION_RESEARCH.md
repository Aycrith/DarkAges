# Redis and ScyllaDB C++ Client Integration Research

## Executive Summary

This document provides comprehensive research on C++ client integration patterns for Redis (hiredis) and ScyllaDB (cassandra-cpp-driver) for the DarkAges MMO project. It covers architecture patterns, connection management, performance optimization, error handling, and Docker integration.

---

## Table of Contents

1. [Redis (hiredis) Integration](#1-redis-hiredis-integration)
2. [ScyllaDB (cassandra-cpp-driver) Integration](#2-scylladb-cassandra-cpp-driver-integration)
3. [Docker Integration Patterns](#3-docker-integration-patterns)
4. [Performance Optimization](#4-performance-optimization)
5. [Error Handling Strategies](#5-error-handling-strategies)
6. [Code Examples](#6-code-examples)
7. [Recommendations for DarkAges](#7-recommendations-for-darkages)

---

## 1. Redis (hiredis) Integration

### 1.1 Architecture Overview

hiredis is the official C client library for Redis. It provides:
- **Synchronous API**: Blocking operations suitable for simple use cases
- **Asynchronous API**: Non-blocking operations with event loop integration
- **Reply Parsing API**: Standalone protocol parser for custom implementations

### 1.2 Connection Patterns

#### Single Connection (Synchronous)
```cpp
redisContext* ctx = redisConnect("127.0.0.1", 6379);
if (ctx == nullptr || ctx->err) {
    // Handle connection error
}
```

#### Connection Pool Pattern
Since hiredis is **NOT thread-safe**, a connection pool is essential for multi-threaded servers:

```cpp
class RedisConnectionPool {
private:
    std::vector<redisContext*> connections_;
    std::queue<redisContext*> available_;
    std::mutex mutex_;
    std::condition_variable cv_;
    
public:
    redisContext* acquire() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !available_.empty(); });
        auto* ctx = available_.front();
        available_.pop();
        return ctx;
    }
    
    void release(redisContext* ctx) {
        std::lock_guard<std::mutex> lock(mutex_);
        available_.push(ctx);
        cv_.notify_one();
    }
};
```

**Recommended Pool Size**: 2-4 connections per thread/core for Redis, as Redis is single-threaded.

### 1.3 Synchronous vs Asynchronous Operations

| Aspect | Synchronous | Asynchronous |
|--------|-------------|--------------|
| Use Case | Simple operations, low concurrency | High throughput, many concurrent clients |
| Blocking | Yes, blocks until response | No, returns immediately |
| Event Loop | Not required | Required (libev, libevent, libuv) |
| Complexity | Simple | Higher complexity |
| Performance | Limited by RTT | Better throughput with pipelining |

### 1.4 Asynchronous API with Event Loop

```cpp
#include <hiredis/hiredis.h>
#include <hiredis/async.h>
#include <hiredis/adapters/libev.h>

void connectCallback(const redisAsyncContext* ctx, int status) {
    if (status != REDIS_OK) {
        printf("Connect error: %s\n", ctx->errstr);
    }
}

void disconnectCallback(const redisAsyncContext* ctx, int status) {
    printf("Disconnected...\n");
}

void getCallback(redisAsyncContext* ctx, void* reply, void* privdata) {
    redisReply* r = static_cast<redisReply*>(reply);
    if (r == nullptr) return;
    
    printf("Got reply: %s\n", r->str);
    freeReplyObject(reply);
}

// Initialize async context
redisAsyncContext* ctx = redisAsyncConnect("127.0.0.1", 6379);
if (ctx->err) {
    printf("Connect error: %s\n", ctx->errstr);
}

// Attach to event loop (libev example)
redisLibevAttach(EV_DEFAULT_ ctx);
redisAsyncSetConnectCallback(ctx, connectCallback);
redisAsyncSetDisconnectCallback(ctx, disconnectCallback);

// Send async command
redisAsyncCommand(ctx, getCallback, nullptr, "GET key");

// Run event loop
ev_loop(EV_DEFAULT_ 0);
```

### 1.5 Pub/Sub Implementation

```cpp
void subscribeCallback(redisAsyncContext* ctx, void* reply, void* privdata) {
    redisReply* r = static_cast<redisReply*>(reply);
    if (r == nullptr) return;
    
    // Pub/Sub messages have array format:
    // ["message", "channel", "payload"]
    if (r->type == REDIS_REPLY_ARRAY && r->elements == 3) {
        const char* msgType = r->element[0]->str;
        const char* channel = r->element[1]->str;
        const char* payload = r->element[2]->str;
        
        printf("[%s] %s: %s\n", msgType, channel, payload);
    }
    
    freeReplyObject(reply);
}

// Subscribe to channels
redisAsyncCommand(ctx, subscribeCallback, nullptr, "SUBSCRIBE game.events zone.123");
```

**Important**: In pub/sub mode, only subscription-related commands are allowed on that connection.

### 1.6 Pipelining for Performance

Pipelining reduces RTT (Round Trip Time) by sending multiple commands without waiting for responses:

```cpp
// Without pipelining - 4 RTTs
redisCommand(ctx, "INCR counter");
redisCommand(ctx, "INCR counter");
redisCommand(ctx, "INCR counter");
redisCommand(ctx, "INCR counter");

// With pipelining - 1 RTT (implicit in async mode)
redisAppendCommand(ctx, "INCR counter");
redisAppendCommand(ctx, "INCR counter");
redisAppendCommand(ctx, "INCR counter");
redisAppendCommand(ctx, "INCR counter");

// Read all responses
redisReply* reply;
for (int i = 0; i < 4; i++) {
    redisGetReply(ctx, (void**)&reply);
    printf("Reply: %lld\n", reply->integer);
    freeReplyObject(reply);
}
```

**Best Practice**: Batch commands in groups of 100-1000 to balance memory usage and throughput.

### 1.7 Binary Serialization

For game state serialization, consider these formats:

| Format | Speed | Size | Compatibility |
|--------|-------|------|---------------|
| Protocol Buffers | Fast | Very compact | Excellent |
| MessagePack | Fast | Compact | Good |
| FlatBuffers | Very Fast | Moderate | Good (zero-copy) |
| JSON | Slow | Large | Universal |

**Recommended for DarkAges**: Protocol Buffers for persistence, FlatBuffers for hot state (zero-copy deserialization).

```cpp
// Using Protocol Buffers with hiredis
PlayerState state;
state.set_player_id(12345);
state.set_health(100);
state.SerializeToString(&serialized);

// Store as binary
redisCommand(ctx, "SET player:12345 %b", 
    serialized.data(), serialized.size());

// Retrieve
redisReply* reply = redisCommand(ctx, "GET player:12345");
if (reply->type == REDIS_REPLY_STRING) {
    state.ParseFromArray(reply->str, reply->len);
}
```

---

## 2. ScyllaDB (cassandra-cpp-driver) Integration

### 2.1 Architecture Overview

The ScyllaDB C++ driver (forked from DataStax cassandra-cpp-driver) provides:
- **Shard-awareness**: Direct routing to specific ScyllaDB shards
- **Async I/O**: Non-blocking operations with futures/callbacks
- **Connection pooling**: Automatic connection management
- **Prepared statements**: Query plan caching for performance
- **Batch operations**: Grouped mutations

### 2.2 Connection Setup

```cpp
#include <cassandra.h>

// Create cluster configuration
CassCluster* cluster = cass_cluster_new();
CassSession* session = cass_session_new();

// Add contact points (include multiple for redundancy)
cass_cluster_set_contact_points(cluster, "127.0.0.1,127.0.0.2,127.0.0.3");

// ScyllaDB-specific optimizations
cass_cluster_set_local_port_range(cluster, 49152, 65535);
cass_cluster_set_core_connections_per_host(cluster, 32);  // Multiple per shard

// Connection pool settings
cass_cluster_set_core_connections_per_host(cluster, 2);
cass_cluster_set_max_connections_per_host(cluster, 8);

// I/O settings
cass_cluster_set_num_threads_io(cluster, 4);
cass_cluster_set_queue_size_io(cluster, 10000);

// Timeouts
cass_cluster_set_connect_timeout(cluster, 5000);  // 5 seconds
cass_cluster_set_request_timeout(cluster, 10000); // 10 seconds

// Connect
CassFuture* connect_future = cass_session_connect(session, cluster);
CassError rc = cass_future_error_code(connect_future);

if (rc != CASS_OK) {
    const char* message;
    size_t message_length;
    cass_future_error_message(connect_future, &message, &message_length);
    fprintf(stderr, "Connect error: '%.*s'\n", (int)message_length, message);
}

cass_future_free(connect_future);
```

### 2.3 Prepared Statements

Prepared statements cache the query plan on the server, reducing parsing overhead:

```cpp
// Prepare the statement
const char* query = "INSERT INTO combat_events (event_id, timestamp, damage) VALUES (?, ?, ?)";
CassFuture* prepare_future = cass_session_prepare(session, query);

CassError rc = cass_future_error_code(prepare_future);
if (rc != CASS_OK) {
    // Handle error
}

// Get prepared statement (cache this!)
const CassPrepared* prepared = cass_future_get_prepared(prepare_future);
cass_future_free(prepare_future);

// Bind and execute multiple times
CassStatement* statement = cass_prepared_bind(prepared);
cass_statement_bind_uuid(statement, 0, event_id);
cass_statement_bind_int64(statement, 1, timestamp);
cass_statement_bind_int32(statement, 2, damage);

CassFuture* execute_future = cass_session_execute(session, statement);
// ... handle result

cass_statement_free(statement);
cass_prepared_free(prepared);
```

**Performance Tip**: Prepare statements once at startup and reuse. Preparing on every request eliminates the benefit.

### 2.4 Batch Operations

```cpp
// Create unlogged batch for single-partition writes (best performance)
CassBatch* batch = cass_batch_new(CASS_BATCH_TYPE_UNLOGGED);

// Add multiple statements
for (const auto& event : events) {
    CassStatement* stmt = cass_statement_new(
        "INSERT INTO combat_events (id, ts, damage) VALUES (?, ?, ?)", 3);
    cass_statement_bind_uuid(stmt, 0, event.id);
    cass_statement_bind_int64(stmt, 1, event.timestamp);
    cass_statement_bind_int32(stmt, 2, event.damage);
    
    cass_batch_add_statement(batch, stmt);
    cass_statement_free(stmt);  // Can free after adding
}

// Execute batch
CassFuture* batch_future = cass_session_execute_batch(session, batch);
CassError rc = cass_future_error_code(batch_future);

cass_future_free(batch_future);
cass_batch_free(batch);
```

**Batch Types**:
- `CASS_BATCH_TYPE_LOGGED`: Atomic across partitions (slower, use for critical data)
- `CASS_BATCH_TYPE_UNLOGGED`: No atomicity guarantee (faster, use for single partition)
- `CASS_BATCH_TYPE_COUNTER`: For counter updates only

### 2.5 Async Operations with Callbacks

```cpp
// Callback function
void on_query_complete(CassFuture* future, void* data) {
    CassError rc = cass_future_error_code(future);
    
    if (rc != CASS_OK) {
        const char* message;
        size_t length;
        cass_future_error_message(future, &message, &length);
        fprintf(stderr, "Query error: '%.*s'\n", (int)length, message);
        return;
    }
    
    // Process result
    const CassResult* result = cass_future_get_result(future);
    CassIterator* rows = cass_iterator_from_result(result);
    
    while (cass_iterator_next(rows)) {
        const CassRow* row = cass_iterator_get_row(rows);
        // Process row...
    }
    
    cass_iterator_free(rows);
    cass_result_free(result);
}

// Execute with callback
CassStatement* statement = cass_statement_new("SELECT * FROM players", 0);
CassFuture* future = cass_session_execute(session, statement);

// Set callback (non-blocking)
cass_future_set_callback(future, on_query_complete, user_data);

cass_future_free(future);  // Safe to free after setting callback
cass_statement_free(statement);
```

### 2.6 Time-Series Data Patterns

For combat events and game metrics:

```sql
-- Use TimeWindowCompactionStrategy for time-series data
CREATE TABLE combat_events (
    day_bucket text,           -- Partition key (YYYY-MM-DD)
    timestamp timestamp,       -- Clustering key
    event_id uuid,
    attacker_id bigint,
    target_id bigint,
    damage int,
    event_type text,
    PRIMARY KEY ((day_bucket), timestamp, event_id)
) WITH CLUSTERING ORDER BY (timestamp DESC, event_id ASC)
  AND compaction = { 'class' : 'TimeWindowCompactionStrategy' }
  AND default_time_to_live = 2592000;  -- 30 days
```

**Key Design Principles**:
1. **Time-based bucketing**: Partition by day/hour to prevent unbounded partition growth
2. **TWCS compaction**: Optimized for time-series (no overlapping time windows)
3. **TTL**: Automatic expiration of old data
4. **DESC ordering**: Most recent events first for queries

---

## 3. Docker Integration Patterns

### 3.1 docker-compose.yml Structure

```yaml
version: '3.8'

services:
  redis:
    image: redis:7-alpine
    container_name: darkages-redis
    ports:
      - "6379:6379"
    volumes:
      - redis_data:/data
    command: >
      redis-server
      --appendonly yes
      --maxmemory 256mb
      --maxmemory-policy allkeys-lru
      --save 60 1000
    healthcheck:
      test: ["CMD", "redis-cli", "ping"]
      interval: 5s
      timeout: 3s
      retries: 5
    networks:
      - darkages-network

  scylla:
    image: scylladb/scylla:5.2
    container_name: darkages-scylla
    ports:
      - "9042:9042"  # CQL
      - "9180:9180"  # Prometheus metrics
    volumes:
      - scylla_data:/var/lib/scylla
    command: >
      --developer-mode=1
      --smp=2
      --memory=2G
      --overprovisioned
    healthcheck:
      test: ["CMD", "cqlsh", "-e", "describe keyspaces"]
      interval: 30s
      timeout: 10s
      retries: 5
      start_period: 60s
    networks:
      - darkages-network

  # Initialize ScyllaDB schema
  scylla-init:
    image: scylladb/scylla:5.2
    depends_on:
      scylla:
        condition: service_healthy
    volumes:
      - ./scylla/init.cql:/init.cql
    entrypoint: >
      bash -c "
        echo 'Waiting for ScyllaDB...' &&
        sleep 10 &&
        cqlsh scylla -f /init.cql &&
        echo 'Schema initialized'
      "
    networks:
      - darkages-network

volumes:
  redis_data:
  scylla_data:

networks:
  darkages-network:
    driver: bridge
    ipam:
      config:
        - subnet: 172.20.0.0/16
```

### 3.2 Health Check Best Practices

**Redis**:
```yaml
healthcheck:
  test: ["CMD", "redis-cli", "ping"]
  interval: 5s
  timeout: 3s
  retries: 5
```

**ScyllaDB**:
```yaml
healthcheck:
  test: ["CMD", "cqlsh", "-e", "describe keyspaces"]
  interval: 30s
  timeout: 10s
  retries: 5
  start_period: 60s  # ScyllaDB needs time to initialize
```

### 3.3 Data Persistence

**Redis**:
- Use `--appendonly yes` for AOF persistence
- Mount volume to `/data`
- Consider RDB snapshots with `--save` intervals

**ScyllaDB**:
- Mount volume to `/var/lib/scylla`
- Requires XFS filesystem for production
- Use `--developer-mode=1` only for local dev

### 3.4 Production Considerations

```yaml
# Production ScyllaDB configuration
scylla:
  image: scylladb/scylla:5.2
  volumes:
    - type: bind
      source: /var/lib/scylla  # Host XFS filesystem
      target: /var/lib/scylla
  command: >
    --developer-mode=0
    --smp=4
    --memory=8G
    --listen-address=0.0.0.0
    --rpc-address=0.0.0.0
  deploy:
    resources:
      limits:
        cpus: '4'
        memory: 10G
```

---

## 4. Performance Optimization

### 4.1 Redis Performance Tuning

| Setting | Default | Recommended | Impact |
|---------|---------|-------------|--------|
| Pipeline batch size | - | 100-1000 | 5-10x throughput |
| Connection pool | 1 | 2-4 per thread | Better concurrency |
| Max memory | unlimited | Set limit + LRU | Predictable behavior |
| TCP keepalive | 300 | 60 | Faster failure detection |

### 4.2 ScyllaDB Performance Tuning

| Setting | Default | Recommended | Impact |
|---------|---------|-------------|--------|
| I/O threads | 1 per core | 4 | Better parallelism |
| Core connections | 1 | 2 per shard | Better throughput |
| Max connections | 2 | 8 per shard | Burst handling |
| Prepared statements | - | Cache forever | Reduce parsing |
| Batch size | - | 100-1000 | Optimal throughput |

### 4.3 Connection Pool Sizing

**Formula**: 
```
Redis: connections = min(threads * 2, 50)
ScyllaDB: connections = shards * 4
```

**DarkAges Recommendations**:
- **Redis**: 8-16 connections total (for 60Hz game server)
- **ScyllaDB**: 32 connections (assuming 8 shards * 4)

### 4.4 Pipelining Guidelines

**Redis**:
- Batch 100-1000 commands per pipeline
- Monitor memory usage (server queues replies)
- Use async API for automatic pipelining

**ScyllaDB**:
- Use unlogged batches for single-partition writes
- Limit batches to 1000 statements
- Avoid multi-partition batches (performance penalty)

---

## 5. Error Handling Strategies

### 5.1 Connection Error Handling

```cpp
class RedisConnection {
public:
    bool connect(const std::string& host, int port) {
        int retries = 0;
        const int MAX_RETRIES = 5;
        
        while (retries < MAX_RETRIES) {
            context_ = redisConnect(host.c_str(), port);
            
            if (context_ != nullptr && !context_->err) {
                return true;
            }
            
            // Exponential backoff
            int delay = std::min(1000 * (1 << retries), 30000);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            retries++;
        }
        
        return false;
    }
    
    bool isConnected() {
        // Ping to check connection health
        redisReply* reply = redisCommand(context_, "PING");
        if (reply == nullptr) return false;
        
        bool ok = (reply->type == REDIS_REPLY_STATUS && 
                   std::string(reply->str) == "PONG");
        freeReplyObject(reply);
        return ok;
    }
    
    bool reconnect() {
        if (context_) redisFree(context_);
        return connect(host_, port_);
    }
};
```

### 5.2 Circuit Breaker Pattern

```cpp
enum class CircuitState { CLOSED, OPEN, HALF_OPEN };

class CircuitBreaker {
private:
    CircuitState state_ = CircuitState::CLOSED;
    std::atomic<int> failures_{0};
    std::atomic<int> successes_{0};
    std::chrono::steady_clock::time_point lastFailureTime_;
    
    const int FAILURE_THRESHOLD = 5;
    const int SUCCESS_THRESHOLD = 3;
    const std::chrono::seconds TIMEOUT{30};
    
public:
    bool allowRequest() {
        if (state_ == CircuitState::CLOSED) return true;
        
        if (state_ == CircuitState::OPEN) {
            auto elapsed = std::chrono::steady_clock::now() - lastFailureTime_;
            if (elapsed > TIMEOUT) {
                state_ = CircuitState::HALF_OPEN;
                successes_ = 0;
                return true;
            }
            return false;
        }
        
        return true;  // HALF_OPEN
    }
    
    void recordSuccess() {
        if (state_ == CircuitState::HALF_OPEN) {
            if (++successes_ >= SUCCESS_THRESHOLD) {
                state_ = CircuitState::CLOSED;
                failures_ = 0;
            }
        } else {
            failures_ = 0;
        }
    }
    
    void recordFailure() {
        lastFailureTime_ = std::chrono::steady_clock::now();
        
        if (state_ == CircuitState::HALF_OPEN) {
            state_ = CircuitState::OPEN;
            return;
        }
        
        if (++failures_ >= FAILURE_THRESHOLD) {
            state_ = CircuitState::OPEN;
        }
    }
};
```

### 5.3 Timeout Handling

```cpp
// Redis with timeout
struct timeval timeout = { 5, 0 };  // 5 seconds
redisContext* ctx = redisConnectWithTimeout(host, port, timeout);

// ScyllaDB with timeout
cass_cluster_set_connect_timeout(cluster, 5000);   // 5s
cass_cluster_set_request_timeout(cluster, 10000);  // 10s
```

### 5.4 Retry Strategy

| Error Type | Retry? | Strategy |
|------------|--------|----------|
| Connection refused | Yes | Exponential backoff |
| Timeout | Yes | Immediate retry (1-2x) |
| Invalid query | No | Log and alert |
| Rate limited | Yes | Exponential backoff |
| Cluster unavailable | Yes | Wait + longer backoff |

---

## 6. Code Examples

### 6.1 Complete Redis Client Wrapper

```cpp
// RedisClient.hpp
#pragma once

#include <hiredis/hiredis.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace darkages::db {

struct RedisConfig {
    std::string host = "localhost";
    int port = 6379;
    int connect_timeout_ms = 5000;
    int command_timeout_ms = 10000;
    int pool_size = 4;
};

class RedisClient {
public:
    explicit RedisClient(const RedisConfig& config);
    ~RedisClient();
    
    // Non-copyable
    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;
    
    // Connection
    bool connect();
    void disconnect();
    bool isConnected();
    
    // Basic operations
    bool set(const std::string& key, const std::string& value);
    bool setex(const std::string& key, const std::string& value, int ttl_seconds);
    std::optional<std::string> get(const std::string& key);
    bool del(const std::string& key);
    bool exists(const std::string& key);
    
    // Binary operations
    bool setBinary(const std::string& key, const void* data, size_t len);
    std::vector<uint8_t> getBinary(const std::string& key);
    
    // Pub/Sub
    bool publish(const std::string& channel, const std::string& message);
    void subscribe(const std::vector<std::string>& channels, 
                   std::function<void(const std::string&, const std::string&)> handler);
    
    // Pipelining
    void pipelineBegin();
    void pipelineAdd(const std::string& command);
    std::vector<std::optional<std::string>> pipelineExecute();
    
private:
    RedisConfig config_;
    redisContext* context_ = nullptr;
    std::mutex mutex_;
    
    bool reconnect();
    void freeReply(redisReply* reply);
};

} // namespace darkages::db
```

### 6.2 Complete ScyllaDB Client Wrapper

```cpp
// ScyllaClient.hpp
#pragma once

#include <cassandra.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <atomic>

namespace darkages::db {

struct ScyllaConfig {
    std::vector<std::string> hosts = {"localhost"};
    int port = 9042;
    std::string keyspace = "darkages";
    int io_threads = 4;
    int core_connections = 2;
    int max_connections = 8;
    int connect_timeout_ms = 5000;
    int request_timeout_ms = 10000;
};

template<typename T>
using QueryCallback = std::function<void(bool success, const T& result)>;

class ScyllaClient {
public:
    explicit ScyllaClient(const ScyllaConfig& config);
    ~ScyllaClient();
    
    bool connect();
    void disconnect();
    bool isConnected() const { return connected_; }
    
    // Prepared statement management
    bool prepareStatement(const std::string& name, const std::string& query);
    
    // Async operations
    template<typename T>
    void executeAsync(const std::string& prepared_name,
                      std::function<void(CassStatement*)> binder,
                      QueryCallback<T> callback);
    
    // Batch operations
    void beginBatch();
    void addToBatch(const std::string& prepared_name,
                    std::function<void(CassStatement*)> binder);
    void executeBatch(std::function<void(bool)> callback);
    
    // Synchronous helpers (for initialization)
    bool executeSync(const std::string& query);
    
    // Metrics
    struct Metrics {
        std::atomic<uint64_t> queries_submitted{0};
        std::atomic<uint64_t> queries_completed{0};
        std::atomic<uint64_t> queries_failed{0};
        std::atomic<uint64_t> pending_queries{0};
    };
    const Metrics& getMetrics() const { return metrics_; }
    
private:
    ScyllaConfig config_;
    CassCluster* cluster_ = nullptr;
    CassSession* session_ = nullptr;
    std::atomic<bool> connected_{false};
    
    std::unordered_map<std::string, const CassPrepared*> prepared_statements_;
    std::mutex prepared_mutex_;
    
    CassBatch* current_batch_ = nullptr;
    std::mutex batch_mutex_;
    
    Metrics metrics_;
    
    static void onQueryComplete(CassFuture* future, void* data);
};

} // namespace darkages::db
```

---

## 7. Recommendations for DarkAges

### 7.1 Redis Implementation Strategy

**Use Case Mapping**:
| Data Type | Redis Structure | TTL |
|-----------|-----------------|-----|
| Player session | Hash | 1 hour |
| Entity positions | Hash (per zone) | 5 minutes |
| Combat buffs | Hash | Duration + 5s |
| Leaderboard cache | Sorted Set | 1 minute |
| Rate limiting | String (INCR) | 1 minute |

**Recommended Architecture**:
- **Synchronous API** for game tick operations (predictable latency)
- **Connection pool** with 8-16 connections
- **Binary serialization** using FlatBuffers for hot state
- **Pub/Sub** for cross-zone event broadcasting

### 7.2 ScyllaDB Implementation Strategy

**Schema Design** (already implemented in SCYLLA_IMPLEMENTATION.md):
```sql
-- Combat events with time-series optimization
CREATE TABLE combat_events (
    day_bucket text,
    timestamp timestamp,
    event_id uuid,
    attacker_id bigint,
    target_id bigint,
    damage int,
    PRIMARY KEY ((day_bucket), timestamp, event_id)
) WITH compaction = { 'class' : 'TimeWindowCompactionStrategy' }
  AND default_time_to_live = 2592000;

-- Counters for player stats
CREATE TABLE player_stats (
    player_id bigint PRIMARY KEY,
    total_kills counter,
    total_deaths counter,
    ...
);
```

**Recommended Configuration**:
```cpp
// I/O settings for 100k writes/sec target
cass_cluster_set_num_threads_io(cluster, 4);
cass_cluster_set_queue_size_io(cluster, 10000);

// Connection pool
cass_cluster_set_core_connections_per_host(cluster, 2);
cass_cluster_set_max_connections_per_host(cluster, 8);

// Timeouts
cass_cluster_set_request_timeout(cluster, 10000);

// Consistency (adjust based on requirements)
cass_cluster_set_consistency(cluster, CASS_CONSISTENCY_LOCAL_QUORUM);
```

### 7.3 Integration Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     Game Server (C++)                        │
│  ┌─────────────────┐  ┌──────────────────────────────────┐  │
│  │  Game Loop      │  │  Database Layer                  │  │
│  │  (60Hz)         │  │  ┌──────────────┐ ┌───────────┐  │  │
│  │                 │  │  │ RedisManager │ │ScyllaManager│ │  │
│  │  - Physics      │  │  │              │ │             │  │  │
│  │  - Combat       │  │  │ - Hot State  │ │Persistence │  │  │
│  │  - Network      │  │  │ - Session    │ │ - Events   │  │  │
│  │                 │  │  │ - Cache      │ │ - Stats    │  │  │
│  └─────────────────┘  │  └──────────────┘ └───────────┘  │  │
│                       └──────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
            │                           │
            ▼                           ▼
    ┌──────────────┐            ┌──────────────┐
    │    Redis     │            │   ScyllaDB   │
    │   (Hot State)│            │(Persistence) │
    │  < 1ms access│            │ 100k writes/s│
    └──────────────┘            └──────────────┘
```

### 7.4 Error Recovery Flow

```
Write Request
    │
    ▼
┌─────────────┐
│ Circuit     │──Open?──┐
│ Breaker     │         │
└─────────────┘         ▼
    │ Closed       Return Error
    ▼                    │
Send to Database         │
    │                    │
    ▼                    │
Success?                 │
    │                    │
Yes─┴─No◄────────────────┘
│      │
▼      ▼
Success  Retry < 3?
Callback    │
         Yes┴─No
         │     │
         ▼     ▼
     Retry   Record Failure
     w/      Update Circuit
     backoff  Breaker
```

### 7.5 Performance Monitoring

**Key Metrics to Track**:

| Metric | Target | Alert Threshold |
|--------|--------|-----------------|
| Redis latency (p99) | < 1ms | > 5ms |
| ScyllaDB write latency (p99) | < 10ms | > 50ms |
| ScyllaDB read latency (p99) | < 50ms | > 100ms |
| Connection pool utilization | < 80% | > 90% |
| Error rate | < 0.1% | > 1% |
| Circuit breaker open | 0 | > 0 |

---

## 8. References

### Documentation
- [hiredis GitHub](https://github.com/redis/hiredis)
- [DataStax C++ Driver Docs](https://docs.datastax.com/en/developer/cpp-driver/latest/)
- [ScyllaDB C++ Driver Docs](https://cpp-driver.docs.scylladb.com/)
- [ScyllaDB Docker Best Practices](https://docs.scylladb.com/manual/stable/operating-scylla/procedures/tips/best-practices-scylla-on-docker.html)

### Existing DarkAges Documentation
- [SCYLLA_IMPLEMENTATION.md](../docs/SCYLLA_IMPLEMENTATION.md)
- [DATABASE_SCHEMA.md](../docs/DATABASE_SCHEMA.md)
- [API_CONTRACTS.md](../docs/API_CONTRACTS.md)

---

*Research completed: 2026-01-30*
