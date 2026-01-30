# DarkAges MMO - API Contracts & Interface Specifications

**Version:** 1.0  
**Purpose:** Define contracts between work packages for parallel development  
**Status:** Active

---

## Overview

This document defines the interfaces between all major components. When implementing a Work Package (WP), adhere to these contracts to ensure compatibility with other modules.

### Interface Change Protocol
1. Propose change in interface change notification format
2. Get agreement from all affected agents
3. Update this document
4. Implement with backward compatibility if possible

---

## 1. Network Layer Contracts (WP-6-1)

### 1.1 NetworkManager Interface

**Header:** `src/server/include/netcode/NetworkManager.hpp`

```cpp
namespace DarkAges::Netcode {

using ConnectionID = uint32_t;
using EntityID = uint32_t;

struct ConnectionQuality {
    uint32_t rttMs;           // Round-trip time in milliseconds
    float packetLoss;         // 0.0 - 1.0 percentage
    float jitterMs;           // Jitter in milliseconds
    uint64_t bytesSent;       // Total bytes sent
    uint64_t bytesReceived;   // Total bytes received
    uint32_t packetsSent;     // Total packets sent
    uint32_t packetsReceived; // Total packets received
};

struct ClientInputPacket {
    ConnectionID connectionId;
    uint32_t sequenceNumber;  // Monotonic counter for reconciliation
    uint32_t timestampMs;     // Client send time
    uint8_t inputFlags;       // Bitmask: WASD + actions
    float yaw;               // Camera rotation (radians)
    float pitch;
    uint32_t targetEntity;    // For lock-on/targeting (0 = none)
};

class NetworkManager {
public:
    // Lifecycle
    bool initialize(uint16_t port);
    void shutdown();
    
    // Per-tick update (called by ZoneServer at 60Hz)
    void update(uint32_t currentTimeMs);
    
    // Input handling (output to ZoneServer)
    std::vector<ClientInputPacket> getPendingInputs();
    void clearProcessedInputs(uint32_t upToSequence);
    
    // Snapshot broadcasting (input from ZoneServer)
    void sendSnapshot(ConnectionID conn, std::span<const uint8_t> data);
    void broadcastSnapshot(std::span<const uint8_t> data);
    void sendToMultiple(const std::vector<ConnectionID>& conns, 
                        std::span<const uint8_t> data);
    
    // Reliable event sending (for combat, deaths, etc.)
    void sendEvent(ConnectionID conn, std::span<const uint8_t> data);
    void broadcastEvent(std::span<const uint8_t> data);
    
    // Connection management
    ConnectionQuality getConnectionQuality(ConnectionID conn);
    size_t getConnectionCount() const;
    void disconnect(ConnectionID conn, const std::string& reason);
    
    // Connection events (ZoneServer registers callbacks)
    using ConnectCallback = std::function<void(ConnectionID, const std::string& clientId)>;
    using DisconnectCallback = std::function<void(ConnectionID, const std::string& reason)>;
    
    void onConnect(ConnectCallback callback);
    void onDisconnect(DisconnectCallback callback);
};

} // namespace
```

### 1.2 Contract with ZoneServer

**ZoneServer → NetworkManager:**
| Method | Call Frequency | Data |
|--------|----------------|------|
| `initialize()` | Once at startup | Port number |
| `update()` | Every tick (60Hz) | Current time |
| `sendSnapshot()` | Every tick per connection | Serialized entity states |
| `broadcastSnapshot()` | Every tick | Serialized entity states |
| `sendEvent()` | On combat events | Serialized event data |

**NetworkManager → ZoneServer:**
| Callback/Event | Trigger | Data |
|----------------|---------|------|
| `onConnect` | New client connects | ConnectionID, client identifier |
| `onDisconnect` | Client disconnects | ConnectionID, reason |
| `getPendingInputs()` | Polled every tick | Vector of client inputs |

### 1.3 Contract with Client (Godot)

**Client → Server (Unreliable, 60Hz):**
```
Packet Type: INPUT (0x01)
Size: ~20 bytes
- sequence (uint32)
- timestamp (uint32)
- input_flags (uint8): bit 0=forward, 1=backward, 2=left, 3=right, 4=jump, 5=attack, 6=block, 7=sprint
- yaw (float32)
- pitch (float32)
- target_entity (uint32)
```

**Server → Client (Unreliable, 20Hz):**
```
Packet Type: SNAPSHOT (0x02)
Size: Variable (delta compressed)
- baseline_tick (uint32) - for delta compression
- entity_count (uint16)
- entity_data[] (variable, delta compressed)
```

**Server → Client (Reliable, as needed):**
```
Packet Type: EVENT (0x03)
Size: Variable
- event_type (uint8): 0=damage, 1=death, 2=spawn, 3=zone_handoff, etc.
- event_data[] (event-specific)
```

---

## 2. Database Layer Contracts (WP-6-2, WP-6-3)

### 2.1 RedisManager Interface (WP-6-2)

**Header:** `src/server/include/db/RedisManager.hpp`

```cpp
namespace DarkAges::Database {

struct PlayerSession {
    EntityID entityId;
    uint32_t zoneId;
    std::string clientId;
    uint64_t loginTime;
    uint64_t lastActivity;
    // Position stored as JSON string for flexibility
    std::string positionJson;  // {"x": 123.45, "y": 0.0, "z": 678.90}
};

struct ZoneMessage {
    uint32_t sourceZoneId;
    uint32_t targetZoneId;
    std::string messageType;  // "entity_entering", "entity_leaving", "broadcast"
    std::string payload;      // JSON payload
    uint64_t timestamp;
};

class RedisManager {
public:
    // Lifecycle
    bool initialize(const std::string& host, uint16_t port);
    void shutdown();
    
    // Session management
    bool setSession(EntityID player, const PlayerSession& session, 
                    uint32_t ttlSeconds = 3600);
    std::optional<PlayerSession> getSession(EntityID player);
    bool deleteSession(EntityID player);
    
    // Zone tracking
    bool setPlayerZone(EntityID player, uint32_t zoneId);
    std::optional<uint32_t> getPlayerZone(EntityID player);
    std::vector<EntityID> getPlayersInZone(uint32_t zoneId);
    
    // Position caching (for quick lookups)
    bool setPlayerPosition(EntityID player, float x, float y, float z);
    std::optional<std::tuple<float, float, float>> getPlayerPosition(EntityID player);
    
    // Cross-zone pub/sub
    bool subscribeToZoneChannel(uint32_t zoneId, 
                                std::function<void(const ZoneMessage&)> callback);
    bool publishToZoneChannel(uint32_t zoneId, const ZoneMessage& message);
    bool unsubscribeFromZoneChannel(uint32_t zoneId);
    
    // Health check
    bool isConnected() const;
    uint64_t getLatencyMicros();  // Last measured latency
};

} // namespace
```

### 2.2 ScyllaManager Interface (WP-6-3)

**Header:** `src/server/include/db/ScyllaManager.hpp`

```cpp
namespace DarkAges::Database {

using WriteCallback = std::function<void(bool success, const std::string& error)>;

struct CombatEvent {
    UUID eventId;           // Generated by database
    uint64_t timestamp;
    EntityID attackerId;
    EntityID targetId;
    int32_t damage;
    std::string hitType;    // "normal", "critical", "blocked", "missed"
    uint32_t zoneId;
    float positionX;
    float positionZ;
    std::string abilityName;
};

struct PlayerCombatStats {
    EntityID playerId;
    int64_t totalKills;
    int64_t totalDeaths;
    int64_t totalDamageDealt;
    int64_t totalDamageTaken;
    int64_t playtimeMinutes;
};

struct LeaderboardEntry {
    EntityID playerId;
    std::string playerName;
    int64_t score;
    int64_t rank;
};

class ScyllaManager {
public:
    // Lifecycle
    bool initialize(const std::string& host, uint16_t port, 
                    const std::string& keyspace = "darkages");
    void shutdown();
    
    // Combat event logging (async)
    void logCombatEvent(const CombatEvent& event, WriteCallback callback = nullptr);
    void logCombatEventsBatch(const std::vector<CombatEvent>& events, 
                               WriteCallback callback = nullptr);
    
    // Player stats (counters, async)
    void updatePlayerStats(const PlayerCombatStats& stats, 
                           WriteCallback callback = nullptr);
    
    // Queries (async with callback)
    using CombatHistoryCallback = std::function<void(
        const std::vector<CombatEvent>& events, bool success)>;
    
    void queryCombatHistory(EntityID playerId, 
                            uint64_t startTime, 
                            uint64_t endTime,
                            CombatHistoryCallback callback);
    
    // Leaderboard (async)
    using LeaderboardCallback = std::function<void(
        const std::vector<LeaderboardEntry>& entries, bool success)>;
    
    void getLeaderboard(const std::string& category,  // "kills", "damage", "playtime"
                        int32_t limit,
                        LeaderboardCallback callback);
    
    // Session persistence
    void saveSessionEnd(EntityID playerId, 
                        uint64_t sessionStart,
                        uint64_t sessionEnd,
                        const PlayerCombatStats& stats,
                        WriteCallback callback = nullptr);
    
    // Health check
    bool isConnected() const;
};

} // namespace
```

### 2.3 Data Schemas

**Redis Key Structure:**
```
session:{entity_id}      -> JSON PlayerSession
zone:{zone_id}:players   -> Set of entity IDs
player:{entity_id}:zone  -> Zone ID
player:{entity_id}:pos   -> "x,y,z" string
channel:zone:{zone_id}   -> Pub/sub channel
```

**ScyllaDB Tables:**
See `docs/DATABASE_SCHEMA.md` for complete CQL schema.

---

## 3. Protocol Contracts (WP-6-4)

### 3.1 FlatBuffers Schema Contract

**File:** `src/shared/proto/game_protocol.fbs`

```protobuf
namespace Protocol;

// Common types
struct Vec3 {
    x:int16;    // Quantized: actual = x / 64.0 (~1.5cm precision)
    y:int16;
    z:int16;
}

struct Quat {
    x:int8;     // Quantized to -127..127
    y:int8;
    z:int8;
    w:int8;
}

// Client -> Server (60Hz)
table ClientInput {
    sequence:uint32;
    timestamp:uint32;
    input_flags:uint8;
    yaw:float;
    pitch:float;
    target_entity:uint32;
}

// Server -> Client (20Hz)
table EntityState {
    id:uint32;
    type:uint8;           // 0=player, 1=projectile, 2=loot
    present_mask:uint16;  // Bitfield of which fields are valid
    pos:Vec3;
    rot:Quat;
    velocity:Vec3;
    health_percent:uint8; // 0-100
    anim_state:uint8;     // 0=idle, 1=run, 2=attack, 3=hit, 4=die
    team_id:uint8;
}

table Snapshot {
    server_tick:uint32;
    baseline_tick:uint32;  // For delta compression
    entity_states:[EntityState];
}

// Events (reliable, as needed)
table DamageEvent {
    target_id:uint32;
    attacker_id:uint32;
    damage:int32;
    hit_type:string;      // "normal", "critical", "blocked"
    remaining_health:int32;
}

table DeathEvent {
    victim_id:uint32;
    killer_id:uint32;
    respawn_time:uint32;
}

table ZoneHandoffEvent {
    new_zone_id:uint32;
    new_server_address:string;
    new_server_port:uint16;
    handoff_token:string; // Auth token for new connection
}

union EventPayload {
    DamageEvent,
    DeathEvent,
    ZoneHandoffEvent
}

table Event {
    event_type:uint8;     // 0=damage, 1=death, 2=zone_handoff
    timestamp:uint32;
    payload:EventPayload;
}
```

### 3.2 Delta Compression Contract

**Baseline Management:**
- Client acknowledges snapshots with `last_received_tick`
- Server maintains baselines for last 30 ticks (500ms)
- Delta computed against client's acknowledged baseline
- If baseline missing, send full snapshot

**Delta Format:**
```
[baseline_tick: uint32]
[changed_entity_count: uint16]
for each changed entity:
  [entity_id: uint32]
  [field_mask: uint16]  // Which fields changed
  [changed_field_data...]
```

---

## 4. Physics/ECS Contracts

### 4.1 Component Interface

**Position Component:**
```cpp
struct Position {
    Fixed x, y, z;           // Fixed-point: 1000 = 1.0 unit (meter)
    uint32_t timestamp_ms;   // Last update timestamp
    uint32_t zone_id;        // Current zone
};
```

**Velocity Component:**
```cpp
struct Velocity {
    Fixed dx, dy, dz;        // Units per second (fixed-point)
};
```

**InputState Component:**
```cpp
struct InputState {
    uint8_t forward : 1;
    uint8_t backward : 1;
    uint8_t left : 1;
    uint8_t right : 1;
    uint8_t jump : 1;
    uint8_t attack : 1;
    uint8_t block : 1;
    uint8_t sprint : 1;
    float yaw;
    float pitch;
    uint32_t sequence;
};
```

### 4.2 System Update Order

**ZoneServer::tick() execution order:**
1. NetworkManager::update() - Receive inputs
2. MovementSystem::update() - Process movement
3. CombatSystem::update() - Process combat
4. AreaOfInterest::update() - Calculate visibility
5. ReplicationOptimizer::update() - Build snapshots
6. NetworkManager::broadcastSnapshot() - Send updates

---

## 5. Zone/Sharding Contracts

### 5.1 ZoneServer Interface

```cpp
class ZoneServer {
public:
    bool initialize(uint32_t zoneId, const ZoneConfig& config);
    void run();  // Main loop (blocking)
    void shutdown();
    
    // Entity management
    EntityID spawnPlayer(ConnectionID conn, const std::string& clientId);
    void despawnPlayer(EntityID player);
    
    // Handoff management
    void initiateHandoff(EntityID player, uint32_t targetZoneId);
    void completeHandoff(EntityID player, const HandoffToken& token);
    void cancelHandoff(EntityID player);
    
    // Queries
    uint32_t getZoneId() const;
    size_t getPlayerCount() const;
    std::vector<EntityID> getPlayersNear(float x, float z, float radius);
};
```

### 5.2 Aura Projection Contract

**Aura Zone:** 50m buffer around zone boundary
- When entity enters aura → Notify target zone
- When entity crosses boundary → Complete handoff
- When entity returns to center → Cancel handoff

**Messages:**
```cpp
struct AuraEnterMessage {
    EntityID entityId;
    uint32_t sourceZoneId;
    uint32_t targetZoneId;
    Position position;
    Velocity velocity;
    CombatState combatState;
};

struct HandoffCompleteMessage {
    EntityID entityId;
    uint32_t fromZoneId;
    uint32_t toZoneId;
    HandoffToken token;
};
```

---

## 6. Client-Server Contracts

### 6.1 Connection Handshake

```
Client                              Server
  |                                    |
  |------- Connect (0x10) ------------>|
  |   client_version: uint32           |
  |   client_id: string                |
  |                                    |
  |<------ ConnectAck (0x11) ----------|
  |   server_version: uint32           |
  |   entity_id: uint32                |
  |   zone_id: uint32                  |
  |   server_tick: uint32              |
  |                                    |
  |------- Connected (0x12) --------->|
  |   (starts sending inputs)          |
```

### 6.2 Client Prediction Contract

**Client Responsibilities:**
1. Apply inputs immediately locally (60Hz)
2. Store input history (2-second buffer)
3. Apply server corrections with smoothing
4. Replay unacknowledged inputs after correction

**Server Responsibilities:**
1. Validate all positions (anti-cheat)
2. Send corrections when prediction error > threshold
3. Include acknowledged input sequence in snapshots

**Correction Thresholds:**
- Minor error (< 0.5m): Interpolate over 200ms
- Moderate error (0.5m - 2m): Interpolate over 100ms
- Major error (> 2m): Snap immediately

---

## 7. Testing Contracts

### 7.1 Unit Test Requirements

Every component must provide:
```cpp
// Test file naming: Test{Component}.cpp
TEST_CASE("{Component} - {Test Name}", "[{tag}]") {
    // Setup
    // Execute
    // Verify
}
```

### 7.2 Integration Test Contracts

**WP-6-5 Integration Test Interface:**
```cpp
class IntegrationTestHarness {
public:
    bool startServer(uint16_t port);
    bool stopServer();
    
    // Bot management
    ConnectionID connectBot(const std::string& botId);
    void disconnectBot(ConnectionID bot);
    void sendBotInput(ConnectionID bot, const ClientInputPacket& input);
    
    // Assertions
    void assertPosition(EntityID entity, float x, float y, float z, float tolerance);
    void assertConnected(ConnectionID conn);
    void assertReceivedSnapshot(ConnectionID conn, uint32_t withinMs);
};
```

---

## Document Control

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-01-30 | AI Coordinator | Initial API contracts |

### Related Documents
- `PHASES_6_7_8_ROADMAP.md` - Work package roadmap
- `WP_IMPLEMENTATION_GUIDE.md` - Implementation details
- `PARALLEL_AGENT_COORDINATION.md` - Agent coordination
- `docs/DATABASE_SCHEMA.md` - Database schemas
- `docs/network-protocol/` - Protocol specifications

---

*These contracts are binding for all parallel development work. Any changes must be announced via the interface change protocol.*
