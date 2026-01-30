# DarkAges Work Package Implementation Guide

**Purpose:** Technical specifications for each work package  
**Audience:** AI Development Agents  
**Prerequisites:** Read PHASES_6_7_8_ROADMAP.md first

---

## Quick Reference

```
Phase 6 (Infrastructure):
  WP-6-1: GameNetworkingSockets - NETWORK agent
  WP-6-2: Redis Integration - DATABASE agent
  WP-6-3: ScyllaDB Integration - DATABASE agent
  WP-6-4: FlatBuffers Protocol - NETWORK agent
  WP-6-5: Integration Testing - DEVOPS agent

Phase 7 (Client):
  WP-7-1: Godot Foundation - CLIENT agent
  WP-7-2: Client Prediction - CLIENT agent
  WP-7-3: Entity Interpolation - CLIENT agent
  WP-7-4: Combat UI - CLIENT agent
  WP-7-5: Zone Handoff - CLIENT agent
  WP-7-6: Performance - CLIENT agent

Phase 8 (Production):
  WP-8-1: Monitoring - DEVOPS agent
  WP-8-2: Security Audit - SECURITY agent
  WP-8-3: Chaos Testing - DEVOPS agent
  WP-8-4: Auto-Scaling - DEVOPS agent
  WP-8-5: Load Testing - DEVOPS agent
```

---

## WP-6-1: GameNetworkingSockets Integration

### Agent: NETWORK
### Duration: 2 weeks

### Technical Requirements

**Dependencies to Install:**
```bash
# MSYS2
pacman -S mingw-w64-ucrt-x86_64-openssl

# Build GameNetworkingSockets from source
git clone https://github.com/ValveSoftware/GameNetworkingSockets.git
cd GameNetworkingSockets
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja
```

**Files to Modify:**
- `src/server/src/netcode/NetworkManager.cpp` (replace stub)
- `CMakeLists.txt` (add GNS linking)
- `src/server/include/netcode/NetworkManager.hpp` (may need updates)

### Implementation Steps

#### Week 1: Basic Connection Management

1. **Initialize GNS** (Day 1-2)
```cpp
// In NetworkManager constructor
SteamDatagramErrMsg errMsg;
if (!GameNetworkingSockets_Init(nullptr, errMsg)) {
    LogError("GameNetworkingSockets_Init failed: %s", errMsg);
}

interface_ = SteamNetworkingSockets();
```

2. **Create Listen Socket** (Day 2-3)
```cpp
SteamNetworkingConfigValue_t opt;
opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, 
           (void*)OnConnectionStatusChanged);

listenSocket_ = interface_->CreateListenSocketIP(addr, 1, &opt);
```

3. **Connection Callbacks** (Day 3-4)
```cpp
static void OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info) {
    // Handle connected, disconnected, problem detected
}
```

4. **Send/Receive Packets** (Day 4-5)
```cpp
// Send unreliable (position updates)
interface_->SendMessageToConnection(
    conn, data, size, k_nSteamNetworkingSend_Unreliable, nullptr);

// Send reliable (combat events)
interface_->SendMessageToConnection(
    conn, data, size, k_nSteamNetworkingSend_Reliable, nullptr);
```

#### Week 2: Advanced Features

5. **Connection Quality** (Day 6-7)
```cpp
SteamNetConnectionRealTimeStatus_t status;
interface_->GetConnectionRealTimeStatus(conn, &status, 0, nullptr);

// Extract: ping, packet_loss, send_rate, recv_rate
ConnectionQuality quality;
quality.rttMs = status.m_nPing;
quality.packetLoss = status.m_flPacketsDroppedPct;
```

6. **IPv6 Support** (Day 8)
```cpp
// Dual-stack - GNS handles both automatically
// Just bind to "0.0.0.0" for IPv4 or "::" for dual-stack
```

7. **Integration & Testing** (Day 9-10)
- Connect to ZoneServer tick loop
- Verify 1000 concurrent connections
- Test with WP-6-5 integration tests

### Interface Contract

**Input from ZoneServer:**
```cpp
// Every tick
void update(uint32_t currentTimeMs);

// When entity state changes
void sendSnapshot(ConnectionID conn, std::span<const uint8_t> data);
void broadcastSnapshot(std::span<const uint8_t> data);
```

**Output to ZoneServer:**
```cpp
// After update()
std::vector<ClientInputPacket> getPendingInputs();

// On demand
ConnectionQuality getConnectionQuality(ConnectionID conn);
```

### Quality Gate Tests
```cpp
TEST_CASE("GNS 1000 connections") {
    NetworkManager mgr;
    mgr.initialize(7777);
    
    // Spawn 1000 client connections
    // Verify all connected
    // Run for 1 hour
    REQUIRE(mgr.getConnectionCount() == 1000);
}

TEST_CASE("GNS latency < 50ms") {
    // LAN connection test
    auto quality = mgr.getConnectionQuality(conn);
    REQUIRE(quality.rttMs < 50);
}
```

### Common Pitfalls
- **Thread safety:** GNS callbacks on different thread - use queue
- **Message limits:** 512KB max per message - fragment if needed
- **Cleanup:** Always CloseConnection() before destruction

---

## WP-6-2: Redis Hot-State Integration

### Agent: DATABASE
### Duration: 2 weeks

### Technical Requirements

**Dependencies:**
```bash
pacman -S mingw-w64-ucrt-x86_64-hiredis

# Or build from source
git clone https://github.com/redis/hiredis.git
```

**Docker for Testing:**
```yaml
# infra/docker-compose.yml
redis:
  image: redis:7-alpine
  ports:
    - "6379:6379"
  command: redis-server --maxmemory 2gb --maxmemory-policy allkeys-lru
```

### Implementation Steps

#### Week 1: Core Redis Operations

1. **Connection Pool** (Day 1-2)
```cpp
class RedisConnectionPool {
    std::vector<redisContext*> connections_;
    
public:
    redisContext* acquire() {
        // Return available connection or create new
    }
    void release(redisContext* ctx) {
        // Return to pool
    }
};
```

2. **Session State Store** (Day 3-4)
```cpp
// Store player session
void RedisManager::setSession(EntityID player, const PlayerSession& session) {
    auto* ctx = pool_.acquire();
    
    std::string key = "session:" + std::to_string(static_cast<uint32_t>(player));
    std::string value = serialize(session); // JSON or MessagePack
    
    redisCommand(ctx, "SET %s %s EX 3600", key.c_str(), value.c_str());
    // EX 3600 = expire after 1 hour
    
    pool_.release(ctx);
}
```

3. **Cross-Zone Pub/Sub** (Day 5-7)
```cpp
// Subscribe to zone channel
void subscribeToZoneChannel(uint32_t zoneId, std::function<void(const ZoneMessage&)> callback) {
    std::string channel = "zone:" + std::to_string(zoneId);
    
    // Run in separate thread
    std::thread([this, channel, callback]() {
        auto* ctx = redisConnect("localhost", 6379);
        redisSubscribe(ctx, channel.c_str());
        
        redisReply* reply;
        while (redisGetReply(ctx, (void**)&reply) == REDIS_OK) {
            if (reply->type == REDIS_REPLY_ARRAY && reply->elements == 3) {
                std::string message(reply->element[2]->str);
                callback(deserialize(message));
            }
            freeReplyObject(reply);
        }
    }).detach();
}
```

#### Week 2: Optimization & Failover

4. **Pipeline Batching** (Day 8-9)
```cpp
void pipelineSet(const std::vector<std::pair<std::string, std::string>>& kvs) {
    auto* ctx = pool_.acquire();
    
    redisAppendCommand(ctx, "MULTI");
    for (const auto& [k, v] : kvs) {
        redisAppendCommand(ctx, "SET %s %s", k.c_str(), v.c_str());
    }
    redisAppendCommand(ctx, "EXEC");
    
    // Get all replies
    redisReply* reply;
    for (size_t i = 0; i < kvs.size() + 2; ++i) {
        redisGetReply(ctx, (void**)&reply);
        freeReplyObject(reply);
    }
    
    pool_.release(ctx);
}
```

5. **Auto-Reconnect** (Day 10-11)
```cpp
redisContext* getConnection() {
    auto* ctx = pool_.acquire();
    if (ctx == nullptr || ctx->err) {
        // Reconnect
        redisFree(ctx);
        ctx = redisConnectWithTimeout(host_.c_str(), port_, timeout_);
    }
    return ctx;
}
```

6. **Integration** (Day 12-14)
- Replace stub implementation
- Test with ZoneServer
- Load testing with WP-6-5

### Quality Gate Tests
```cpp
TEST_CASE("Redis < 1ms latency") {
    auto start = std::chrono::high_resolution_clock::now();
    redis.set("test_key", "test_value");
    auto end = std::chrono::high_resolution_clock::now();
    
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    REQUIRE(us < 1000);
}

TEST_CASE("Redis 10k ops/sec") {
    // Benchmark 10k SET operations
    // Must complete in < 1 second
}
```

---

## WP-6-3: ScyllaDB Persistence Layer

### Agent: DATABASE
### Duration: 2 weeks

### Technical Requirements

**Dependencies:**
```bash
# Build cassandra-cpp-driver
git clone https://github.com/datastax/cpp-driver.git
cd cpp-driver
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja
```

**Schema Definition:**
```cql
-- schema/combat_events.cql
CREATE KEYSPACE IF NOT EXISTS darkages 
WITH replication = {'class': 'SimpleStrategy', 'replication_factor': 3};

CREATE TABLE IF NOT EXISTS darkages.combat_events (
    event_id uuid PRIMARY KEY,
    timestamp timestamp,
    attacker_id bigint,
    target_id bigint,
    damage int,
    hit_type text,
    zone_id int,
    position_x double,
    position_z double
) WITH compaction = {'class': 'TimeWindowCompactionStrategy'}
  AND ttl = 2592000;  -- 30 day TTL

CREATE TABLE IF NOT EXISTS darkages.player_stats (
    player_id bigint PRIMARY KEY,
    total_kills counter,
    total_deaths counter,
    total_damage_dealt counter,
    total_damage_taken counter,
    playtime_minutes counter
);
```

### Implementation Steps

#### Week 1: Core ScyllaDB Operations

1. **Cluster Connection** (Day 1-2)
```cpp
class ScyllaManager {
    CassCluster* cluster_;
    CassSession* session_;
    
public:
    bool initialize(const std::string& host, uint16_t port) {
        cluster_ = cass_cluster_new();
        session_ = cass_session_new();
        
        cass_cluster_set_contact_points(cluster_, host.c_str());
        cass_cluster_set_port(cluster_, port);
        cass_cluster_set_num_threads_io(cluster_, 4);
        cass_cluster_set_queue_size_io(cluster_, 10000);
        
        CassFuture* connect = cass_session_connect(session_, cluster_);
        CassError rc = cass_future_error_code(connect);
        cass_future_free(connect);
        
        return rc == CASS_OK;
    }
};
```

2. **Async Writes** (Day 3-5)
```cpp
void logCombatEvent(const CombatEvent& event, WriteCallback callback) {
    const char* query = 
        "INSERT INTO darkages.combat_events "
        "(event_id, timestamp, attacker_id, target_id, damage, hit_type, zone_id) "
        "VALUES (uuid(), ?, ?, ?, ?, ?, ?)";
    
    CassStatement* statement = cass_statement_new(query, 6);
    cass_statement_bind_int64(statement, 0, event.timestamp);
    cass_statement_bind_int64(statement, 1, static_cast<int64_t>(event.attacker));
    cass_statement_bind_int64(statement, 2, static_cast<int64_t>(event.target));
    cass_statement_bind_int32(statement, 3, event.damage);
    cass_statement_bind_string(statement, 4, event.hitType.c_str());
    cass_statement_bind_int32(statement, 5, event.zoneId);
    
    CassFuture* future = cass_session_execute(session_, statement);
    
    // Set callback
    cass_future_set_callback(future, [](CassFuture* f, void* data) {
        auto* cb = static_cast<WriteCallback*>(data);
        CassError rc = cass_future_error_code(f);
        (*cb)(rc == CASS_OK);
        delete cb;
    }, new WriteCallback(callback));
    
    cass_future_free(future);
    cass_statement_free(statement);
}
```

3. **Counter Updates** (Day 6-7)
```cpp
void updatePlayerStats(const PlayerCombatStats& stats, WriteCallback callback) {
    const char* query = 
        "UPDATE darkages.player_stats SET "
        "total_kills = total_kills + ?, "
        "total_deaths = total_deaths + ?, "
        "total_damage_dealt = total_damage_dealt + ? "
        "WHERE player_id = ?";
    
    CassStatement* statement = cass_statement_new(query, 4);
    cass_statement_bind_int64(statement, 0, stats.kills);
    cass_statement_bind_int64(statement, 1, stats.deaths);
    cass_statement_bind_int64(statement, 2, stats.damageDealt);
    cass_statement_bind_int64(statement, 3, static_cast<int64_t>(stats.playerId));
    
    // Execute async...
}
```

#### Week 2: Batch Processing & Optimization

4. **Batch Writes** (Day 8-10)
```cpp
void logCombatEventsBatch(const std::vector<CombatEvent>& events, WriteCallback callback) {
    CassBatch* batch = cass_batch_new(CASS_BATCH_TYPE_UNLOGGED);
    
    for (const auto& event : events) {
        CassStatement* statement = createCombatEventStatement(event);
        cass_batch_add_statement(batch, statement);
        cass_statement_free(statement);
    }
    
    CassFuture* future = cass_session_execute_batch(session_, batch);
    // Set callback...
    
    cass_batch_free(batch);
}
```

5. **Prepared Statements** (Day 11-12)
```cpp
class ScyllaManager {
    const CassPrepared* insertCombatEvent_;
    const CassPrepared* updatePlayerStats_;
    
public:
    bool prepareStatements() {
        CassFuture* prepare = cass_session_prepare(session_, 
            "INSERT INTO darkages.combat_events (...) VALUES (...)");
        
        CassError rc = cass_future_error_code(prepare);
        if (rc != CASS_OK) return false;
        
        insertCombatEvent_ = cass_future_get_prepared(prepare);
        cass_future_free(prepare);
        return true;
    }
};
```

6. **Integration** (Day 13-14)
- Replace stub implementation
- Verify write performance
- Test failover scenarios

### Quality Gate Tests
```cpp
TEST_CASE("ScyllaDB 100k writes/sec") {
    // Generate 100k combat events
    // Write all async
    // Verify completion in < 1 second
}

TEST_CASE("ScyllaDB write latency < 10ms p99") {
    // Write 10k events
    // Measure latency percentiles
    // p99 must be < 10ms
}
```

---

## WP-7-1: Godot Project Foundation

### Agent: CLIENT
### Duration: 2 weeks

### Technical Requirements

**Godot Version:** 4.2+ (stable)

**Project Structure:**
```
src/client/
├── project.godot
├── scenes/
│   ├── player.tscn
│   ├── world.tscn
│   └── ui/
├── scripts/
│   ├── player.gd
│   ├── network_client.gd
│   └── protocol.gd
├── assets/
│   ├── models/
│   └── materials/
└── addons/
    └── flatbuffers/
```

### Implementation Steps

#### Week 1: Project Setup & Basic Movement

1. **Project Configuration** (Day 1)
```ini
# project.godot
[application]
config/name="DarkAges Client"
config/features=PackedStringArray("4.2", "Forward Plus")
run/main_scene="res://scenes/world.tscn"

[display]
window/size/viewport_width=1920
window/size/viewport_height=1080
window/vsync/vsync_mode=1

[physics]
common/physics_ticks_per_second=60
```

2. **Player Scene** (Day 2)
```gdscript
# scenes/player.tscn
[gd_scene load_steps=2 format=3 uid="uid://player"]

[node name="Player" type="CharacterBody3D"]
script = ExtResource("1_player")

[node name="Mesh" type="MeshInstance3D" parent="."]
mesh = SubResource("CapsuleMesh")

[node name="Camera" type="Camera3D" parent="."]
transform = Transform3D(1, 0, 0, 0, 0.866, 0.5, 0, -0.5, 0.866, 0, 3, 4)

[node name="Collision" type="CollisionShape3D" parent="."]
shape = SubResource("CapsuleShape3D")
```

3. **Player Controller** (Day 3-4)
```gdscript
# scripts/player.gd
class_name Player
extends CharacterBody3D

@export var speed: float = 6.0
@export var sprint_multiplier: float = 1.5

var input_state: Dictionary = {
    "forward": false,
    "backward": false,
    "left": false,
    "right": false,
    "sprint": false,
    "attack": false
}

func _physics_process(delta: float) -> void:
    var direction := Vector3.ZERO
    
    if Input.is_action_pressed("move_forward"):
        direction -= transform.basis.z
        input_state.forward = true
    
    if Input.is_action_pressed("move_backward"):
        direction += transform.basis.z
        input_state.backward = true
    
    if Input.is_action_pressed("move_left"):
        direction -= transform.basis.x
        input_state.left = true
    
    if Input.is_action_pressed("move_right"):
        direction += transform.basis.x
        input_state.right = true
    
    input_state.sprint = Input.is_action_pressed("sprint")
    
    if direction != Vector3.ZERO:
        direction = direction.normalized()
        var current_speed = speed * (sprint_multiplier if input_state.sprint else 1.0)
        velocity.x = direction.x * current_speed
        velocity.z = direction.z * current_speed
    else:
        velocity.x = move_toward(velocity.x, 0, speed * delta * 10)
        velocity.z = move_toward(velocity.z, 0, speed * delta * 10)
    
    move_and_slide()
    
    # Send input to server
    NetworkClient.send_input(input_state)
```

4. **Camera Controller** (Day 5)
```gdscript
# scripts/camera_controller.gd
extends Node3D

@export var mouse_sensitivity: float = 0.005
@export var min_pitch: float = -60.0
@export var max_pitch: float = 60.0

var yaw: float = 0.0
var pitch: float = 0.0

func _input(event: InputEvent) -> void:
    if event is InputEventMouseMotion:
        yaw -= event.relative.x * mouse_sensitivity
        pitch -= event.relative.y * mouse_sensitivity
        pitch = clamp(pitch, deg_to_rad(min_pitch), deg_to_rad(max_pitch))
        
        rotation.y = yaw
        rotation.x = pitch
```

#### Week 2: Networking & Protocol

5. **Network Client** (Day 6-8)
```gdscript
# scripts/network_client.gd
extends Node
class_name NetworkClient

signal connected
signal disconnected(reason: String)
signal snapshot_received(states: Array)

var socket: PacketPeerUDP
var server_address: String = "127.0.0.1"
var server_port: int = 7777
var sequence_number: int = 0

func connect_to_server() -> bool:
    socket = PacketPeerUDP.new()
    socket.set_dest_address(server_address, server_port)
    
    # Send handshake
    var handshake = PackedByteArray()
    handshake.append(0x01)  # Handshake packet type
    socket.put_packet(handshake)
    
    connected.emit()
    return true

func send_input(input: Dictionary) -> void:
    var packet = PackedByteArray()
    packet.append(0x02)  # Input packet type
    packet.append(sequence_number)
    sequence_number += 1
    
    # Serialize input flags
    var flags: int = 0
    if input.get("forward", false): flags |= 0x01
    if input.get("backward", false): flags |= 0x02
    if input.get("left", false): flags |= 0x04
    if input.get("right", false): flags |= 0x08
    if input.get("sprint", false): flags |= 0x10
    if input.get("attack", false): flags |= 0x20
    packet.append(flags)
    
    socket.put_packet(packet)

func _process(delta: float) -> void:
    while socket.get_available_bytes() > 0:
        var packet = socket.get_packet()
        handle_packet(packet)

func handle_packet(data: PackedByteArray) -> void:
    if data.size() < 1:
        return
    
    var packet_type = data[0]
    match packet_type:
        0x03:  # Snapshot
            parse_snapshot(data.slice(1))
        0x04:  # Event
            parse_event(data.slice(1))

func parse_snapshot(data: PackedByteArray) -> void:
    # Parse FlatBuffers snapshot
    # Emit snapshot_received
    pass
```

6. **Protocol Deserialization** (Day 9-10)
```gdscript
# scripts/protocol.gd
class_name Protocol

static func deserialize_entity_state(data: PackedByteArray) -> Dictionary:
    # Use FlatBuffers GDExtension or manual parsing
    var state = {
        "entity_id": 0,
        "position": Vector3.ZERO,
        "velocity": Vector3.ZERO,
        "health": 100
    }
    
    # Parse based on protocol spec
    # This is simplified - actual implementation uses FlatBuffers
    
    return state
```

### Quality Gate Tests
```gdscript
# tests/test_player.gd
func test_player_movement():
    var player = Player.new()
    var start_pos = player.position
    
    # Simulate forward input
    player.input_state.forward = true
    player._physics_process(1.0)
    
    assert(player.position.z < start_pos.z, "Player should move forward")

func test_network_connection():
    var client = NetworkClient.new()
    var success = client.connect_to_server()
    assert(success, "Should connect to server")
```

---

## WP-8-1: Production Monitoring

### Agent: DEVOPS
### Duration: 2 weeks

### Technical Requirements

**Components:**
- Prometheus (metrics collection)
- Grafana (visualization)
- AlertManager (alerts)
- Loki (log aggregation)

**Docker Compose:**
```yaml
# infra/monitoring/docker-compose.yml
version: '3'
services:
  prometheus:
    image: prom/prometheus:latest
    ports:
      - "9090:9090"
    volumes:
      - ./prometheus.yml:/etc/prometheus/prometheus.yml
  
  grafana:
    image: grafana/grafana:latest
    ports:
      - "3000:3000"
    environment:
      - GF_SECURITY_ADMIN_PASSWORD=admin
  
  loki:
    image: grafana/loki:latest
    ports:
      - "3100:3100"
```

### Implementation Steps

#### Week 1: Metrics Export

1. **Prometheus C++ Client** (Day 1-2)
```cpp
// src/server/include/monitoring/Metrics.hpp
#pragma once
#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <prometheus/exposer.h>

namespace DarkAges::Monitoring {

class Metrics {
public:
    static Metrics& instance();
    
    bool initialize(uint16_t port = 9100);
    
    // Player metrics
    prometheus::Gauge& players_online;
    prometheus::Counter& players_connected_total;
    prometheus::Counter& players_disconnected_total;
    
    // Performance metrics
    prometheus::Histogram& tick_duration_seconds;
    prometheus::Gauge& memory_usage_bytes;
    
    // Network metrics
    prometheus::Counter& packets_sent_total;
    prometheus::Counter& packets_received_total;
    prometheus::Histogram& packet_size_bytes;
    
    // Combat metrics
    prometheus::Counter& attacks_total;
    prometheus::Counter& hits_total;
    prometheus::Histogram& damage_dealt;

private:
    std::unique_ptr<prometheus::Exposer> exposer_;
    std::shared_ptr<prometheus::Registry> registry_;
};

} // namespace
```

2. **Tick Performance Tracking** (Day 3-4)
```cpp
// In ZoneServer::tick()
void ZoneServer::tick(uint32_t currentTimeMs) {
    auto start = std::chrono::high_resolution_clock::now();
    
    // ... existing tick logic ...
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double>(end - start).count();
    
    Metrics::instance().tick_duration_seconds.Observe(duration);
    
    // Alert if over budget
    if (duration > 0.016) {  // 16ms = 60Hz
        LogWarning("Tick overrun: %.3fms", duration * 1000);
    }
}
```

3. **Player Count Tracking** (Day 5)
```cpp
// In ZoneServer::onPlayerConnect()
void ZoneServer::onPlayerConnect(EntityID player) {
    Metrics::instance().players_online.Increment();
    Metrics::instance().players_connected_total.Increment();
}

void ZoneServer::onPlayerDisconnect(EntityID player) {
    Metrics::instance().players_online.Decrement();
    Metrics::instance().players_disconnected_total.Increment();
}
```

#### Week 2: Dashboards & Alerting

4. **Grafana Dashboard** (Day 6-8)
```json
// infra/monitoring/dashboards/server-overview.json
{
  "dashboard": {
    "title": "DarkAges Server Overview",
    "panels": [
      {
        "title": "Players Online",
        "type": "stat",
        "targets": [{
          "expr": "players_online"
        }]
      },
      {
        "title": "Tick Duration",
        "type": "graph",
        "targets": [{
          "expr": "histogram_quantile(0.99, tick_duration_seconds_bucket)"
        }]
      },
      {
        "title": "Network Traffic",
        "type": "graph",
        "targets": [
          {"expr": "rate(packets_sent_total[1m])"},
          {"expr": "rate(packets_received_total[1m])"}
        ]
      }
    ]
  }
}
```

5. **Alerting Rules** (Day 9-10)
```yaml
# infra/monitoring/alerting/rules.yml
groups:
  - name: darkages
    rules:
      - alert: HighTickDuration
        expr: histogram_quantile(0.99, tick_duration_seconds) > 0.016
        for: 1m
        labels:
          severity: warning
        annotations:
          summary: "Server tick time exceeding budget"
          
      - alert: HighMemoryUsage
        expr: memory_usage_bytes > 3000000000  # 3GB
        for: 5m
        labels:
          severity: critical
        annotations:
          summary: "Server memory usage critical"
          
      - alert: PlayerDrop
        expr: rate(players_disconnected_total[5m]) > 10
        for: 1m
        labels:
          severity: warning
        annotations:
          summary: "Unusual player disconnect rate"
```

### Quality Gate
```bash
# Verify metrics endpoint
curl http://localhost:9100/metrics | grep players_online

# Verify Grafana loads
curl -I http://localhost:3000
```

---

## Cross-Package Integration

### Integration Points

```
WP-6-1 (GNS) ──┼──▶ WP-6-5 (Integration Tests)
     │              │
     │              ▼
     │         WP-7-1 (Godot)
     │              │
     └────────────┼──▶ WP-8-1 (Monitoring)
                    │
WP-6-2 (Redis) ────┼──▶ WP-8-3 (Chaos)
     │              │
WP-6-3 (Scylla) ──┼──▶ WP-8-5 (Load Test)
```

### Interface Contracts Summary

| Source | Target | Data | Format |
|--------|--------|------|--------|
| WP-6-1 | ZoneServer | ClientInputPacket[] | struct |
| WP-6-1 | WP-8-1 | Connection metrics | Prometheus |
| WP-6-2 | ZoneServer | Session data | JSON |
| WP-6-2 | WP-6-5 | Zone messages | Pub/Sub |
| WP-6-3 | CombatSystem | Event logging | Async callback |
| WP-6-4 | WP-7-1 | Protocol schema | FlatBuffers |
| WP-7-1 | WP-6-1 | Client inputs | UDP packets |
| WP-8-1 | All | Metrics | HTTP /metrics |

---

## Document Control

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-01-30 | AI Coordinator | Initial work package specifications |

**Next Steps:**
1. Assign agents to work packages
2. Begin WP-6-1, WP-6-2, WP-6-3 in parallel
3. Weekly integration sync meetings
