I'll provide comprehensive technical specifications, implementation guides, and AI agent instructions to support your local development team. This documentation is designed to be consumed by AI coding agents with minimal context.

---

# DARK AGES MMO - TECHNICAL IMPLEMENTATION SPECIFICATION
**Document Version**: 1.0  
**Target**: Local AI Development Agents  
**Stack**: Godot 4.x (Client) | C++20/EnTT (Server) | Redis/ScyllaDB (Data)

---

## SECTION 1: AI AGENT CONTEXT PRIMER

### Project Overview
You are implementing a **third-person PvP MMO** inspired by Dark Age of Camelot and Ark Raiders. Key constraints:
- **100-1000 concurrent players per shard** via spatial sharding
- **60Hz server tick** with <50ms input latency
- **Authoritative server** (never trust client)
- **Zero budget** (open-source only)

### Critical Architecture Concepts
1. **Spatial Sharding**: World divided into 200m hexagonal zones; each zone = 1 server process
2. **Aura Projection**: 50m buffer zones between servers for seamless handoffs
3. **Client-Side Prediction**: Client predicts movement immediately, server corrects if wrong
4. **Delta Compression**: Only send changed bytes between server/client (not full state)
5. **ECS (Entity Component System)**: Data-oriented design for cache efficiency

---

## SECTION 2: CORE SYSTEMS IMPLEMENTATION

### 2.1 Server ECS Architecture (C++/EnTT)

**File**: `src/server/ecs/CoreTypes.hpp`
```cpp
#pragma once
#include <cstdint>
#include <entt/entt.hpp>
#include <glm/glm.hpp>

// Fixed-point arithmetic for determinism
using Fixed = int32_t;  // 1000 units = 1.0f
constexpr Fixed FIXED_PRECISION = 1000;

struct Position {
    Fixed x, y, z;  // Quantized coordinates
    uint32_t timestamp_ms;  // Server tick timestamp
};

struct Velocity {
    Fixed dx, dy, dz;
};

struct InputState {
    uint8_t forward : 1;    // Bit-packed inputs
    uint8_t backward : 1;
    uint8_t left : 1;
    uint8_t right : 1;
    uint8_t jump : 1;
    uint8_t attack : 1;
    uint8_t block : 1;
    uint8_t sprint : 1;
    float yaw;              // Camera rotation (radians)
    float pitch;
    uint32_t sequence;      // Input sequence number for reconciliation
};

struct CombatState {
    int16_t health;         // 0-10000 (fixed point, 1 decimal)
    int16_t max_health;
    uint8_t team_id;
    uint8_t class_type;
    uint32_t last_attacker; // Entity ID
    uint64_t hit_history[60]; // Circular buffer for lag comp (1s @ 60Hz)
};

// Component used for spatial partitioning
struct SpatialHash {
    uint32_t cell_x;
    uint32_t cell_z;
    uint32_t zone_id;
};
```

**File**: `src/server/systems/MovementSystem.hpp`
```cpp
#pragma once
#include "ecs/CoreTypes.hpp"
#include <entt/entt.hpp>

class MovementSystem {
    static constexpr float MAX_SPEED = 6.0f;      // m/s
    static constexpr float SPRINT_MULT = 1.5f;
    static constexpr float ACCELERATION = 10.0f;  // m/s^2
    static constexpr float DT = 1.0f / 60.0f;     // Fixed timestep
    
public:
    void update(entt::registry& registry, float dt) {
        auto view = registry.view<Position, Velocity, InputState>();
        
        view.each([](auto& pos, auto& vel, auto& input) {
            // Convert input to direction vector
            glm::vec3 dir(0.0f);
            if (input.forward) dir += glm::vec3(0, 0, -1);
            if (input.backward) dir += glm::vec3(0, 0, 1);
            if (input.left) dir += glm::vec3(-1, 0, 0);
            if (input.right) dir += glm::vec3(1, 0, 0);
            
            if (glm::length(dir) > 0) {
                dir = glm::normalize(dir);
                // Apply rotation from yaw
                float yaw = input.yaw;
                glm::vec3 rotated(
                    dir.x * cos(yaw) - dir.z * sin(yaw),
                    0,
                    dir.x * sin(yaw) + dir.z * cos(yaw)
                );
                
                float speed = MAX_SPEED * (input.sprint ? SPRINT_MULT : 1.0f);
                glm::vec3 target_vel = rotated * speed;
                
                // Smooth acceleration (kinematic, not physics-driven)
                glm::vec3 current_vel(vel.dx / 1000.0f, vel.dy / 1000.0f, vel.dz / 1000.0f);
                glm::vec3 new_vel = glm::mix(current_vel, target_vel, ACCELERATION * DT);
                
                // Update velocity (convert back to fixed)
                vel.dx = static_cast<Fixed>(new_vel.x * 1000);
                vel.dz = static_cast<Fixed>(new_vel.z * 1000);
            } else {
                // Deceleration when no input
                vel.dx = static_cast<Fixed>(vel.dx * 0.9f);
                vel.dz = static_cast<Fixed>(vel.dz * 0.9f);
            }
            
            // Apply velocity to position
            pos.x += static_cast<Fixed>((vel.dx / 1000.0f) * DT * 1000);
            pos.z += static_cast<Fixed>((vel.dz / 1000.0f) * DT * 1000);
            pos.timestamp_ms += static_cast<uint32_t>(DT * 1000);
        });
    }
    
    // Server-side validation (anti-cheat)
    bool validateMovement(const Position& old_pos, const Position& new_pos, uint32_t dt_ms) {
        float dx = (new_pos.x - old_pos.x) / 1000.0f;
        float dz = (new_pos.z - old_pos.z) / 1000.0f;
        float dist = sqrt(dx*dx + dz*dz);
        float max_dist = MAX_SPEED * SPRINT_MULT * (dt_ms / 1000.0f) * 1.2f; // 20% tolerance
        
        return dist <= max_dist;
    }
};
```

### 2.2 Network Protocol Specification

**File**: `src/shared/proto/game_protocol.fbs` (FlatBuffers)
```protobuf
namespace Protocol;

// Client -> Server (Inputs only)
table ClientInput {
    sequence:uint;              // Monotonic counter for reconciliation
    timestamp:uint;             // Client send time (ms)
    input_flags:uint8;          // Bitmask: WASD + actions
    yaw:float;                  // Camera rotation
    pitch:float;
    target_entity:uint32;       // For lock-on/targeting
}

// Server -> Client (State snapshot)
struct Vec3 {
    x:int16;    // Quantized: actual = x / 64.0 (precision ~1.5cm)
    y:int16;
    z:int16;
}

struct Quat {
    x:int8;     // Quantized to -127..127
    y:int8;
    z:int8;
    w:int8;
}

table EntityState {
    id:uint32;
    type:uint8;                 // 0=player, 1=projectile, 2=loot
    present_mask:uint16;        // Bitfield: which fields are valid
    
    pos:Vec3;
    rot:Quat;
    velocity:Vec3;              // Optional (predicted)
    
    health_percent:uint8;       // 0-100
    anim_state:uint8;           // Enum: idle, run, attack, hit, die
    team_id:uint8;
    
    // For delta compression: baseline tick this delta is relative to
    baseline_tick:uint;         
}

table Snapshot {
    server_tick:uint;           // Current server simulation tick
    baseline_tick:uint;         // Delta baseline (0 = full snapshot)
    entities:[EntityState];
    removed_entities:[uint32];  // Entities despawned
}

// For RPC/Events (reliable ordered channel)
table EventPacket {
    event_id:uint16;            // Enum: damage, pickup, zone_change, etc
    timestamp:uint;
    source_entity:uint32;
    target_entity:uint32;
    params:[float];             // Event-specific data
}

root_type Snapshot;
```

**Delta Compression Algorithm** (Pseudocode for Agent):
```
Algorithm: CreateDeltaSnapshot
Input: current_entities[], client_baseline_tick, client_last_ack_tick
Output: Delta-encoded snapshot bytes

1. Retrieve historical state for client_baseline_tick from circular buffer
2. For each entity in current_entities:
   a. Get previous state at client_baseline_tick
   b. If entity didn't exist: flag as "new" (full state)
   c. If position delta > threshold (>0.5m): include quantized pos
   d. If rotation delta > threshold (>5 deg): include compressed quat
   e. If health changed: include health_percent
   f. Pack into bitstream using EntityState table schema
3. For entities in baseline but not current: add to removed_entities[]
4. Return FlatBuffers serialized bytes
```

### 2.3 Spatial Hashing Implementation

**File**: `src/server/spatial/SpatialHash.hpp`
```cpp
#pragma once
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <glm/glm.hpp>

class SpatialHash {
    static constexpr float CELL_SIZE = 10.0f;  // 10m cells
    float inv_cell_size = 1.0f / CELL_SIZE;
    
    // Hash key for 2D grid
    struct CellCoord {
        int32_t x, z;
        bool operator==(const CellCoord& other) const {
            return x == other.x && z == other.z;
        }
    };
    
    struct Hash {
        size_t operator()(const CellCoord& c) const {
            return std::hash<int64_t>()((static_cast<int64_t>(c.x) << 32) | c.z);
        }
    };
    
    std::unordered_map<CellCoord, std::vector<uint32_t>, Hash> grid;
    
public:
    void clear() { grid.clear(); }
    
    void insert(uint32_t entity_id, float x, float z) {
        CellCoord cell = {
            static_cast<int32_t>(floor(x * inv_cell_size)),
            static_cast<int32_t>(floor(z * inv_cell_size))
        };
        grid[cell].push_back(entity_id);
    }
    
    std::vector<uint32_t> query(float x, float z, float radius) {
        std::vector<uint32_t> result;
        int32_t r_cells = static_cast<int32_t>(ceil(radius * inv_cell_size));
        CellCoord center = {
            static_cast<int32_t>(floor(x * inv_cell_size)),
            static_cast<int32_t>(floor(z * inv_cell_size))
        };
        
        for(int dx = -r_cells; dx <= r_cells; ++dx) {
            for(int dz = -r_cells; dz <= r_cells; ++dz) {
                CellCoord cell{center.x + dx, center.z + dz};
                auto it = grid.find(cell);
                if(it != grid.end()) {
                    result.insert(result.end(), it->second.begin(), it->second.end());
                }
            }
        }
        return result;
    }
    
    // Get adjacent cells for collision detection (broad-phase)
    std::vector<uint32_t> queryAdjacent(float x, float z) {
        return query(x, z, CELL_SIZE * 1.5f);
    }
};
```

### 2.4 Lag Compensation (Server Rewind)

**File**: `src/server/combat/LagCompensation.hpp`
```cpp
#pragma once
#include <vector>
#include <cstdint>
#include <deque>
#include "ecs/CoreTypes.hpp"

class LagCompensator {
    // Store 2 seconds of history @ 60Hz = 120 frames
    static constexpr size_t HISTORY_SIZE = 120;
    static constexpr uint32_t TICK_MS = 16; // 60Hz
    
    struct HistoryFrame {
        uint32_t timestamp;
        std::vector<std::pair<uint32_t, Position>> positions; // entity_id -> pos
    };
    
    std::deque<HistoryFrame> history;
    
public:
    void recordFrame(uint32_t timestamp, const std::vector<std::pair<uint32_t, Position>>& positions) {
        history.push_back({timestamp, positions});
        if(history.size() > HISTORY_SIZE) {
            history.pop_front();
        }
    }
    
    // Get entity positions as they were at "target_time" (ms in past)
    std::vector<std::pair<uint32_t, Position>> rewind(uint32_t target_time) {
        std::vector<std::pair<uint32_t, Position>> result;
        
        // Find frames bracketing target_time
        size_t idx = 0;
        while(idx < history.size() && history[idx].timestamp < target_time) {
            ++idx;
        }
        
        if(idx == 0 || idx >= history.size()) return result;
        
        // Interpolate between history[idx-1] and history[idx]
        const auto& before = history[idx-1];
        const auto& after = history[idx];
        float t = static_cast<float>(target_time - before.timestamp) / 
                  static_cast<float>(after.timestamp - before.timestamp);
        
        // Build map of "after" positions for quick lookup
        std::unordered_map<uint32_t, Position> after_map;
        for(const auto& [id, pos] : after.positions) {
            after_map[id] = pos;
        }
        
        // Interpolate
        for(const auto& [id, pos_before] : before.positions) {
            auto it = after_map.find(id);
            if(it != after_map.end()) {
                Position interpolated;
                interpolated.x = static_cast<Fixed>(pos_before.x + (it->second.x - pos_before.x) * t);
                interpolated.z = static_cast<Fixed>(pos_before.z + (it->second.z - pos_before.z) * t);
                result.push_back({id, interpolated});
            }
        }
        
        return result;
    }
    
    // Calculate rewind time for an attack
    uint32_t calculateRewindTime(uint32_t server_time, uint32_t client_rtt_ms) {
        // Rewind to: Now - (RTT/2 + processing buffer)
        return server_time - (client_rtt_ms / 2 + 50); // 50ms processing buffer
    }
};
```

---

## SECTION 3: CLIENT IMPLEMENTATION (Godot)

### 3.1 Client-Side Prediction

**File**: `src/client/scripts/PlayerController.cs` (or GDScript equivalent)
```csharp
using Godot;
using System.Collections.Generic;

public partial class PredictedPlayer : CharacterBody3D
{
    // Configuration
    [Export] public float MaxSpeed = 6.0f;
    [Export] public float Acceleration = 10.0f;
    
    // State
    private uint _inputSequence = 0;
    private Queue<PredictedInput> _pendingInputs = new();
    private Vector3 _serverPosition = Vector3.Zero;
    private Vector3 _predictedPosition = Vector3.Zero;
    private bool _reconciling = false;
    
    struct PredictedInput {
        public uint Sequence;
        public uint Timestamp;
        public Vector2 InputDir;
        public float Yaw;
        public Vector3 ResultPos; // Where we predicted we'd be
    }
    
    public override void _PhysicsProcess(double delta) {
        // 1. Gather input
        var input = Input.GetVector("left", "right", "forward", "backward");
        uint now = Time.GetTicksMsec();
        
        // 2. Apply locally immediately (prediction)
        ApplyMovement(input, (float)delta);
        _predictedPosition = GlobalPosition;
        
        // 3. Store for reconciliation
        var predInput = new PredictedInput {
            Sequence = _inputSequence++,
            Timestamp = now,
            InputDir = input,
            Yaw = Rotation.Y,
            ResultPos = _predictedPosition
        };
        _pendingInputs.Enqueue(predInput);
        
        // 4. Send to server
        NetworkManager.SendInput(predInput);
        
        // 5. Reconcile if needed
        if(_reconciling) {
            Reconcile();
        }
    }
    
    // Called when server snapshot received
    public void OnServerCorrection(uint lastProcessedSeq, Vector3 serverPos, Vector3 serverVel) {
        // Remove acknowledged inputs
        while(_pendingInputs.Count > 0 && _pendingInputs.Peek().Sequence <= lastProcessedSeq) {
            _pendingInputs.Dequeue();
        }
        
        // Check error
        float error = _predictedPosition.DistanceTo(serverPos);
        if(error > 0.1f) {
            // Snap if large error (cheat attempt or major desync)
            if(error > 2.0f) {
                GlobalPosition = serverPos;
                _predictedPosition = serverPos;
            } else {
                // Small drift - reconcile by replaying unacked inputs
                _serverPosition = serverPos;
                _reconciling = true;
            }
        }
    }
    
    private void Reconcile() {
        GlobalPosition = _serverPosition;
        
        // Replay all pending inputs on top of server state
        foreach(var input in _pendingInputs) {
            ApplyMovement(input.InputDir, 1.0f/60.0f);
        }
        
        _predictedPosition = GlobalPosition;
        _reconciling = false;
    }
    
    private void ApplyMovement(Vector2 inputDir, float dt) {
        var direction = new Vector3(inputDir.X, 0, inputDir.Y).Normalized();
        if(direction != Vector3.Zero) {
            Velocity = Velocity.Lerp(direction * MaxSpeed, Acceleration * dt);
        } else {
            Velocity = Velocity.Lerp(Vector3.Zero, Acceleration * dt);
        }
        MoveAndSlide();
    }
}
```

### 3.2 Entity Interpolation (Other Players)

**File**: `src/client/scripts/RemotePlayer.cs`
```csharp
using Godot;
using System.Collections.Generic;

public partial class RemotePlayer : Node3D {
    // Interpolation buffer: holds last 10 states (166ms @ 60Hz)
    private Queue<EntityState> _stateBuffer = new();
    private const int BUFFER_SIZE = 3; // ~100ms delay for smoothness
    private const float INTERP_DELAY = 0.1f; // 100ms behind present
    
    private Vector3 _lastPos;
    private Vector3 _targetPos;
    private double _interpAlpha = 0.0;
    
    public void OnSnapshotReceived(EntityState state) {
        _stateBuffer.Enqueue(state);
        if(_stateBuffer.Count > 10) _stateBuffer.Dequeue();
        
        // Only interpolate if we have enough states
        if(_stateBuffer.Count >= 2) {
            // Implementation: Interpolate between state[N-2] and state[N-1]
            // Display at "current server time - INTERP_DELAY"
        }
    }
    
    public override void _Process(double delta) {
        if(_stateBuffer.Count < 2) return;
        
        // Simple interpolation between last two states
        _interpAlpha += delta * 60.0; // Assuming 60Hz server
        if(_interpAlpha > 1.0) _interpAlpha = 1.0;
        
        GlobalPosition = _lastPos.Lerp(_targetPos, (float)_interpAlpha);
    }
}
```

---

## SECTION 4: BUILD SYSTEMS & DEPLOYMENT

### 4.1 Server Build Configuration

**File**: `src/server/CMakeLists.txt`
```cmake
cmake_minimum_required(VERSION 3.16)
project(DarkAgesServer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release) # For performance testing

# Find packages
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# EnTT
add_subdirectory(${CMAKE_SOURCE_DIR}/../../deps/entt ${CMAKE_BINARY_DIR}/entt)

# GameNetworkingSockets (build from source or find package)
set(GNS_DIR ${CMAKE_SOURCE_DIR}/../../deps/GameNetworkingSockets)
add_subdirectory(${GNS_DIR} ${CMAKE_BINARY_DIR}/gns)

# Godot GDExtension (optional, for shared libs)
# ...

# Source files
file(GLOB_RECURSE SOURCES 
    "src/*.cpp"
    "src/*.hpp"
)

add_executable(server ${SOURCES})

target_link_libraries(server PRIVATE
    Threads::Threads
    OpenSSL::SSL
    EnTT::EnTT
    GameNetworkingSockets::GameNetworkingSockets
)

# Compiler flags for performance
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(server PRIVATE 
        -O3 
        -march=native 
        -flto
        -Wall -Wextra -Wpedantic
    )
endif()

# Testing
enable_testing()
add_subdirectory(tests)
```

### 4.2 Docker Configuration

**File**: `infra/dockerfile.server`
```dockerfile
FROM alpine:3.18 AS builder
RUN apk add --no-cache g++ cmake make openssl-dev linux-headers
COPY ./deps /deps
COPY ./src/server /src
WORKDIR /build
RUN cmake /src && make -j$(nproc)

FROM alpine:3.18
RUN apk add --no-cache libstdc++ openssl
COPY --from=builder /build/server /app/server
EXPOSE 7777/udp
WORKDIR /app
CMD ["./server", "--port", "7777", "--zone", "1"]
```

**File**: `infra/docker-compose.dev.yml`
```yaml
version: '3.8'
services:
  zone-1:
    build:
      context: ../
      dockerfile: infra/dockerfile.server
    ports:
      - "7777:7777/udp"
    environment:
      - ZONE_ID=1
      - REDIS_HOST=redis
      - SCYLLA_HOST=scylla
    depends_on:
      - redis
      - scylla
    deploy:
      resources:
        limits:
          cpus: '2'
          memory: 4G
  
  redis:
    image: redis:7-alpine
    ports:
      - "6379:6379"
    volumes:
      - redis_data:/data
  
  scylla:
    image: scylladb/scylla:5.2
    command: --developer-mode=1 --smp=2 --memory=2G
    ports:
      - "9042:9042"
    volumes:
      - scylla_data:/var/lib/scylla

volumes:
  redis_data:
  scylla_data:
```

---

## SECTION 5: AI AGENT TASK SPECIFICATIONS

### Task 1: Networking Layer Agent

**Objective**: Implement GameNetworkingSockets wrapper with the following interface:

```cpp
class NetworkManager {
public:
    // Initialize on port, return true if success
    bool Initialize(uint16_t port);
    
    // Non-blocking update, process incoming packets
    void Update();
    
    // Send snapshot to specific client (unreliable channel)
    void SendSnapshot(uint32_t client_id, const std::vector<uint8_t>& data);
    
    // Send event (reliable ordered)
    void SendEvent(uint32_t client_id, const EventPacket& event);
    
    // Receive input from clients
    std::vector<ClientInput> GetPendingInputs();
    
    // Callbacks for connect/disconnect
    std::function<void(uint32_t client_id)> OnClientConnected;
    std::function<void(uint32_t client_id)> OnClientDisconnected;
};
```

**Requirements**:
- Use SteamGameNetworkingSockets library
- Support both IPv4 and IPv6
- Implement connection quality stats (ping, packet loss)
- Handle automatic reconnection for temporary packet loss
- **Testing**: Create echo server that bounces inputs back with timestamp

### Task 2: Physics Systems Agent

**Objective**: Implement spatial hashing + kinematic character controller

**Deliverables**:
1. `SpatialHash` class (as specified in Section 2.3)
2. `KinematicController` system:
   - Sweept sphere collision detection (radius 0.5m)
   - Handle sliding along walls (velocity projection)
   - Ground check for jumping
3. `BroadPhaseSystem` that:
   - Updates SpatialHash every frame
   - Detects potential collisions (/player vs player, /player vs projectile)
   - Triggers `OnCollision` events

**Performance Requirements**:
- Must handle 500 entities in same cell without frame drops (<16ms)
- Spatial query for 10m radius must complete <0.1ms

### Task 3: Database Persistence Agent

**Objective**: Redis/ScyllaDB integration layer

**Schema Requirements**:

**Redis (Hot State - TTL 5 minutes)**:
```
Key: "entity:{id}:state" -> Hash {x, y, z, health, timestamp}
Key: "zone:{id}:entities" -> Set of entity IDs
Key: "player:{id}:session" -> Hash {zone_id, connection_id}
```

**ScyllaDB (Cold Storage)**:
```sql
CREATE TABLE player_profiles (
    player_id UUID PRIMARY KEY,
    username TEXT,
    class_type INT,
    level INT,
    experience BIGINT,
    inventory map<UUID, INT>, -- item_id -> count
    last_login TIMESTAMP
);

CREATE TABLE combat_logs (
    log_id UUID,
    player_id UUID,
    event_type TEXT, -- "kill", "death", "damage"
    target_id UUID,
    damage INT,
    timestamp TIMESTAMP,
    zone_id INT,
    PRIMARY KEY ((player_id), timestamp)
) WITH CLUSTERING ORDER BY (timestamp DESC);

CREATE TABLE zone_states (
    zone_id INT,
    entity_id UUID,
    entity_type TEXT,
    x FLOAT,
    z FLOAT,
    state JSONB, -- flexible component data
    updated_at TIMESTAMP,
    PRIMARY KEY (zone_id, entity_id)
);
```

**Implementation**:
- `CacheManager` class with async write queue
- Write-through pattern: Update Redis immediately, queue ScyllaDB writes
- Batch writes to ScyllaDB every 5 seconds or 1000 queued updates

### Task 4: Client Networking Agent (Godot)

**Objective**: GDExtension or GDScript network client

**Features**:
- Connect to server via UDP
- Send `ClientInput` packets @ 60Hz
- Receive and parse snapshots
- Entity interpolation for remote players
- Client-side prediction for local player
- UI display of ping/packet loss

**Integration Points**:
- Emit Godot signals: `OnEntitySpawned(id, type)`, `OnEntityStateUpdate(id, state)`
- Must work with Godot's physics timestep (variable delta)

---

## SECTION 6: TESTING PROTOCOLS

### 6.1 Unit Testing Framework

**File**: `src/server/tests/TestSpatialHash.cpp`
```cpp
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include "spatial/SpatialHash.hpp"

TEST_CASE("Spatial Hash basic operations", "[spatial]") {
    SpatialHash hash;
    
    SECTION("Insert and query") {
        hash.insert(1, 5.0f, 5.0f);
        hash.insert(2, 5.1f, 5.1f); // Same cell (10m cells)
        hash.insert(3, 100.0f, 100.0f); // Different cell
        
        auto nearby = hash.query(5.0f, 5.0f, 1.0f);
        REQUIRE(nearby.size() == 2);
        REQUIRE(std::find(nearby.begin(), nearby.end(), 1) != nearby.end());
        REQUIRE(std::find(nearby.begin(), nearby.end(), 2) != nearby.end());
    }
    
    SECTION("Performance - 1000 entities") {
        // Insert 1000 random entities
        for(int i=0; i<1000; ++i) {
            hash.insert(i, rand()%1000, rand()%1000);
        }
        
        // Query 100 times, must average < 0.1ms
        auto start = std::chrono::high_resolution_clock::now();
        for(int i=0; i<100; ++i) {
            hash.query(500.0f, 500.0f, 50.0f);
        }
        auto elapsed = std::chrono::high_resolution_clock::now() - start;
        REQUIRE(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() / 100.0 < 100.0);
    }
}
```

### 6.2 Integration Testing (Headless Bots)

**Python Stress Test Script** (`tools/stress-test/bot_swarm.py`):
```python
import asyncio
import struct
import random
import time
from dataclasses import dataclass

@dataclass
class Bot:
    client_id: int
    x: float = 0.0
    z: float = 0.0
    sequence: int = 0
    latency_ms: int = 50  # Simulated latency
    
    def generate_input(self):
        # Random walk
        self.x += random.uniform(-0.1, 0.1)
        self.z += random.uniform(-0.1, 0.1)
        
        # Pack into binary format matching ClientInput flatbuffer
        return struct.pack('<IIBff', 
            self.sequence,
            int(time.time() * 1000) & 0xFFFFFFFF,
            random.randint(0, 255),  # input flags
            random.uniform(0, 6.28), # yaw
            0.0 # pitch
        )

async def run_bot(host, port, bot_id):
    # Connect using asyncio UDP
    loop = asyncio.get_event_loop()
    transport, protocol = await loop.create_datagram_endpoint(
        asyncio.DatagramProtocol,
        remote_addr=(host, port)
    )
    
    bot = Bot(bot_id)
    
    try:
        while True:
            # Send input
            data = bot.generate_input()
            transport.sendto(data)
            
            # Simulate processing
            await asyncio.sleep(1/60)  # 60Hz
    finally:
        transport.close()

async def main():
    # Spawn 100 bots
    tasks = [run_bot("127.0.0.1", 7777, i) for i in range(100)]
    await asyncio.gather(*tasks)

if __name__ == "__main__":
    asyncio.run(main())
```

### 6.3 Validation Checkpoints

**Before proceeding to next phase, verify**:

1. **Single-Player Movement**:
   - Client moves smoothly with prediction
   - Server receives inputs and validates positions
   - No rubber-banding on localhost

2. **Multi-Player Synchronization (2 clients)**:
   - Client A sees Client B moving smoothly (interpolation)
   - Server rejects invalid movement (try modifying client to teleport)
   - Lag compensation: If Client A shoots Client B, hit registers correctly even with 200ms simulated latency

3. **Spatial Sharding**:
   - Two zone servers communicate via Redis
   - Player can walk from Zone A to Zone B without disconnect
   - Entities in buffer zone (50m) exist on both servers

4. **Density Test**:
   - 200 bots in single zone, server maintains 60 FPS (16ms tick)
   - Memory usage < 4GB
   - Network bandwidth per bot < 20 KB/s

---

## SECTION 7: DEBUGGING & PROFILING TOOLS

### 7.1 Server Profiling

**Instrumentation Points**:
```cpp
// In main loop
auto frame_start = std::chrono::high_resolution_clock::now();

systems.update_network();
systems.update_movement();
systems.update_combat();
systems.update_replication();

auto frame_end = std::chrono::high_resolution_clock::now();
auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(frame_end - frame_start).count();

if(elapsed_us > 16666) { // Over budget (60Hz)
    std::cerr << "FRAME OVERRUN: " << elapsed_us << "us" << std::endl;
    // Print breakdown by system
}
```

**Perfetto Integration** (Chrome tracing):
```cpp
// Add to CMake: FetchContent_Declare(perfetto ...)
#include <perfetto.h>

TRACE_EVENT_BEGIN("server", "MovementSystem");
movement_system.update(registry);
TRACE_EVENT_END("server");
```

### 7.2 Network Debugging

**Packet Loss Simulation** (Linux):
```bash
# On server host, throttle connection to test lag comp
tc qdisc add dev eth0 root netem delay 150ms 20ms loss 2%

# Remove when done
tc qdisc del dev eth0 root
```

**Protocol Verification**:
```cpp
// Debug mode: Log every 100th packet
if(sequence % 100 == 0) {
    std::cout << "[NET] Seq=" << sequence 
              << " Pos=(" << pos.x << "," << pos.z << ")"
              << " Input=" << (int)input_flags << std::endl;
}
```

---

## SECTION 8: SECURITY CHECKLIST

**Implement these before any public testing**:

- [ ] **Input Validation**: Clamp all floats, check bounds on positions (world size limits)
- [ ] **Rate Limiting**: Max 60 inputs/sec per client, drop connections exceeding
- [ ] **Movement Validation**: Server calculates max possible distance per tick, rejects impossible movement
- [ ] **Memory Safety**: Use AddressSanitizer (`-fsanitize=address`) in debug builds
- [ ] **Network Encryption**: Enable SRV encryption in GameNetworkingSockets
- [ ] **Anti-Cheat Logging**: Log all damage events with full context (positions, timestamps) for replay analysis

---

## SECTION 9: GLOSSARY FOR AI AGENTS

| Term | Definition |
|------|------------|
| **ECS** | Entity Component System: Data-oriented architecture where logic (Systems) processes data (Components) attached to IDs (Entities) |
| **Spatial Hash** | 2D/3D grid acceleration structure for querying nearby objects in O(1) time |
| **Delta Compression** | Sending only changed data between frames, not full state |
| **Client-Side Prediction** | Client simulates input locally immediately, server corrects later if wrong |
| **Lag Compensation** | Server rewinds game state to match what shooter saw when validating hits |
| **Aura Projection** | 50m buffer zone where entities exist on both adjacent zone servers |
| **Spatial Sharding** | Splitting world into zones run by different servers |
| **EnTT** | Header-only C++ ECS library (github.com/skypjack/entt) |
| **Fixed Point** | Integer math simulating decimals (e.g., 1000 = 1.0) for determinism |
| **Kinematic** | Movement controlled by code/animation, not physics simulation |

---

## SECTION 10: QUICK REFERENCE (Copy-Paste for Agents)

**Start New Feature Branch**:
```bash
git checkout -b feature/[name]
git push -u origin feature/[name]
```

**Build Server**:
```bash
cd src/server && mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

**Run Tests**:
```bash
cd build
ctest --output-on-failure
./tests/server_tests
```

**Deploy Local Stack**:
```bash
cd infra
docker-compose -f docker-compose.dev.yml up --build
```

---

This specification provides complete implementation details for AI agents to develop the Dark Ages MMO server and client. Each section is self-contained and can be assigned to different specialized agents (Networking, Physics, Database, Client) with clear interfaces between components.