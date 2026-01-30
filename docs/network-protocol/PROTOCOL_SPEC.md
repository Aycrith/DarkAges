# DarkAges MMO - Network Protocol Specification

**Version:** 1.0  
**Work Package:** WP-6-4  
**Status:** Specification Complete → Ready for Implementation

---

## 1. Protocol Overview

### 1.1 Transport Layer

| Aspect | Specification |
|--------|---------------|
| **Transport** | UDP via GameNetworkingSockets |
| **Reliability** | GNS handles reliable/unreliable channels |
| **Encryption** | GNS SRV (optional, WP-8-2) |
| **MTU** | 1200 bytes (conservative for VPNs) |
| **Max Packet** | 512KB (GNS limitation) |

### 1.2 Update Rates

| Direction | Frequency | Reliability | Purpose |
|-----------|-----------|-------------|---------|
| Client → Server | 60Hz | Unreliable | Player inputs |
| Server → Client | 20Hz | Unreliable | World snapshots |
| Server → Client | As needed | Reliable | Events (combat, deaths) |
| Both | On connect | Reliable | Handshake |

### 1.3 Protocol Versioning

```cpp
constexpr uint32_t PROTOCOL_VERSION_MAJOR = 1;
constexpr uint32_t PROTOCOL_VERSION_MINOR = 0;
constexpr uint32_t PROTOCOL_VERSION = (PROTOCOL_VERSION_MAJOR << 16) | PROTOCOL_VERSION_MINOR;
// Protocol version 1.0 = 0x00010000
```

---

## 2. Packet Structure

### 2.1 Common Header

All packets have a 1-byte type prefix:

```cpp
enum class PacketType : uint8_t {
    // Connection
    CONNECT         = 0x10,
    CONNECT_ACK     = 0x11,
    CONNECTED       = 0x12,
    DISCONNECT      = 0x13,
    HEARTBEAT       = 0x14,
    
    // Game Data
    INPUT           = 0x20,
    SNAPSHOT        = 0x21,
    EVENT           = 0x22,
    
    // Zone Management
    ZONE_HANDOFF    = 0x30,
    ZONE_ENTER      = 0x31,
    ZONE_LEAVE      = 0x32,
    
    // Debug
    DEBUG_TEXT      = 0xF0,
    PING            = 0xF1,
    PONG            = 0xF2
};
```

### 2.2 Connect (Client → Server)

**Packet Type:** 0x10  
**Reliability:** Reliable  
**Direction:** Client → Server

```cpp
struct ConnectPacket {
    uint8_t type = 0x10;
    uint32_t protocol_version;
    uint32_t client_version;    // Game client version
    char client_id[32];         // Unique client identifier
    char auth_token[64];        // Authentication token (optional)
};
```

**Size:** 104 bytes

### 2.3 ConnectAck (Server → Client)

**Packet Type:** 0x11  
**Reliability:** Reliable  
**Direction:** Server → Client

```cpp
struct ConnectAckPacket {
    uint8_t type = 0x11;
    uint32_t server_protocol_version;
    uint32_t entity_id;         // Assigned entity ID
    uint32_t zone_id;           // Starting zone
    uint32_t server_tick;       // Current server tick
    uint64_t server_time_ms;    // Server timestamp
    uint8_t result;             // 0=success, 1=version_mismatch, 2=auth_failed, 3=server_full
    char message[64];           // Error message if failed
};
```

**Size:** 89 bytes

### 2.4 Input (Client → Server)

**Packet Type:** 0x20  
**Reliability:** Unreliable  
**Direction:** Client → Server  
**Frequency:** 60Hz

```cpp
struct InputPacket {
    uint8_t type = 0x20;
    uint32_t sequence;          // Monotonic counter (for reconciliation)
    uint32_t timestamp;         // Client send time (ms)
    uint8_t input_flags;        // Bitmask (see below)
    float yaw;                  // Camera rotation Y (radians, -π to π)
    float pitch;                // Camera rotation X (radians, -π/2 to π/2)
    uint32_t target_entity;     // Lock-on target (0 = none)
};
```

**Input Flags Bitmap:**
```cpp
enum InputFlags : uint8_t {
    INPUT_FORWARD   = 1 << 0,   // 0x01 - W key
    INPUT_BACKWARD  = 1 << 1,   // 0x02 - S key
    INPUT_LEFT      = 1 << 2,   // 0x04 - A key
    INPUT_RIGHT     = 1 << 3,   // 0x08 - D key
    INPUT_JUMP      = 1 << 4,   // 0x10 - Space
    INPUT_ATTACK    = 1 << 5,   // 0x20 - Left mouse
    INPUT_BLOCK     = 1 << 6,   // 0x40 - Right mouse
    INPUT_SPRINT    = 1 << 7    // 0x80 - Shift
};
```

**Size:** 21 bytes

### 2.5 Snapshot (Server → Client)

**Packet Type:** 0x21  
**Reliability:** Unreliable  
**Direction:** Server → Client  
**Frequency:** 20Hz

#### Full Snapshot Format (Baseline)

```cpp
struct FullSnapshotPacket {
    uint8_t type = 0x21;
    uint32_t server_tick;
    uint32_t baseline_tick;     // Same as server_tick for full snapshot
    uint16_t entity_count;
    EntityStateData entities[]; // Variable length
};
```

#### Delta Snapshot Format

```cpp
struct DeltaSnapshotPacket {
    uint8_t type = 0x21;
    uint32_t server_tick;
    uint32_t baseline_tick;     // Reference tick for delta
    uint16_t changed_count;
    uint16_t removed_count;
    EntityDeltaData changed[];  // Changed entities
    uint32_t removed[];         // Removed entity IDs
};
```

**Entity State Data:**
```cpp
struct EntityStateData {
    uint32_t entity_id;
    uint8_t entity_type;        // 0=player, 1=projectile, 2=loot
    uint16_t present_mask;      // Which fields are valid
    
    // Optional based on present_mask bits
    QuantizedVec3 position;     // Bits 0-2: x, y, z
    QuantizedQuat rotation;     // Bits 3-6: x, y, z, w
    QuantizedVec3 velocity;     // Bits 7-9: dx, dy, dz
    uint8_t health_percent;     // Bit 10: 0-100
    uint8_t anim_state;         // Bit 11: animation state
    uint8_t team_id;            // Bit 12: team
};

struct QuantizedVec3 {
    int16_t x;  // Actual = x / 64.0 (precision ~1.5cm)
    int16_t y;
    int16_t z;
};

struct QuantizedQuat {
    int8_t x;   // Quantized -127..127
    int8_t y;
    int8_t z;
    int8_t w;
};
```

**Present Mask Bits:**
```cpp
enum EntityFieldMask : uint16_t {
    FIELD_POS_X     = 1 << 0,
    FIELD_POS_Y     = 1 << 1,
    FIELD_POS_Z     = 1 << 2,
    FIELD_ROT_X     = 1 << 3,
    FIELD_ROT_Y     = 1 << 4,
    FIELD_ROT_Z     = 1 << 5,
    FIELD_ROT_W     = 1 << 6,
    FIELD_VEL_X     = 1 << 7,
    FIELD_VEL_Y     = 1 << 8,
    FIELD_VEL_Z     = 1 << 9,
    FIELD_HEALTH    = 1 << 10,
    FIELD_ANIM      = 1 << 11,
    FIELD_TEAM      = 1 << 12
};
```

### 2.6 Event (Server → Client)

**Packet Type:** 0x22  
**Reliability:** Reliable  
**Direction:** Server → Client  
**Frequency:** As needed

```cpp
struct EventPacket {
    uint8_t type = 0x22;
    uint32_t event_id;          // Unique event ID
    uint32_t timestamp;
    uint8_t event_type;         // See EventType enum
    uint16_t payload_length;
    uint8_t payload[];          // Event-specific data
};

enum EventType : uint8_t {
    EVENT_DAMAGE            = 0x01,
    EVENT_DEATH             = 0x02,
    EVENT_SPAWN             = 0x03,
    EVENT_DESPAWN           = 0x04,
    EVENT_ABILITY_COOLDOWN  = 0x05,
    EVENT_ZONE_HANDOFF      = 0x06,
    EVENT_CHAT              = 0x07,
    EVENT_SYSTEM            = 0x08
};
```

**Damage Event Payload:**
```cpp
struct DamageEventPayload {
    uint32_t target_id;
    uint32_t attacker_id;
    int32_t damage;
    uint8_t hit_type;           // 0=normal, 1=critical, 2=blocked, 3=missed
    uint8_t remaining_health;
    char ability_name[16];
};
```

**Death Event Payload:**
```cpp
struct DeathEventPayload {
    uint32_t victim_id;
    uint32_t killer_id;
    uint32_t respawn_time_ms;   // When player can respawn
    uint8_t kill_type;          // 0=normal, 1=headshot, etc.
};
```

**Zone Handoff Event Payload:**
```cpp
struct ZoneHandoffEventPayload {
    uint32_t new_zone_id;
    char new_server_address[32];
    uint16_t new_server_port;
    char handoff_token[32];     // Auth token for new server
    uint32_t handoff_timeout_ms;
};
```

---

## 3. Delta Compression Algorithm

### 3.1 Overview

Delta compression reduces bandwidth by only sending changed fields.

**Target:** 80%+ bandwidth reduction  
**Baseline Storage:** 30 ticks (1.5 seconds @ 20Hz)

### 3.2 Algorithm

```cpp
class DeltaCompressor {
public:
    // Generate delta between current state and baseline
    std::vector<uint8_t> compress(
        uint32_t baselineTick,
        const std::vector<EntityState>& currentStates,
        const std::unordered_map<uint32_t, EntityState>& baselineStates
    ) {
        DeltaSnapshot delta;
        delta.server_tick = currentTick;
        delta.baseline_tick = baselineTick;
        
        for (const auto& current : currentStates) {
            auto it = baselineStates.find(current.entity_id);
            if (it == baselineStates.end()) {
                // New entity - send full state
                delta.added.push_back(current);
            } else {
                // Existing entity - calculate delta
                EntityDelta diff = calculateDelta(it->second, current);
                if (!diff.isEmpty()) {
                    delta.changed.push_back(diff);
                }
            }
        }
        
        // Find removed entities
        for (const auto& [id, baseline] : baselineStates) {
            if (!existsInCurrent(id)) {
                delta.removed.push_back(id);
            }
        }
        
        return serialize(delta);
    }
    
private:
    EntityDelta calculateDelta(const EntityState& baseline, const EntityState& current) {
        EntityDelta delta;
        delta.entity_id = current.entity_id;
        delta.present_mask = 0;
        
        if (baseline.position.x != current.position.x) {
            delta.position.x = current.position.x;
            delta.present_mask |= FIELD_POS_X;
        }
        // ... repeat for all fields
        
        return delta;
    }
};
```

### 3.3 Baseline Management

**Client-side:**
```cpp
class BaselineManager {
    static constexpr size_t MAX_BASELINES = 30;
    std::deque<std::pair<uint32_t, Snapshot>> baselines;
    
public:
    void storeBaseline(uint32_t tick, const Snapshot& snapshot) {
        baselines.push_back({tick, snapshot});
        if (baselines.size() > MAX_BASELINES) {
            baselines.pop_front();
        }
    }
    
    Snapshot decompress(uint32_t baselineTick, const DeltaSnapshot& delta) {
        auto it = findBaseline(baselineTick);
        if (it == baselines.end()) {
            // Baseline missing - request full snapshot
            requestFullSnapshot();
            return Snapshot{};
        }
        
        return applyDelta(it->second, delta);
    }
};
```

**Acknowledgment:**
- Client includes `last_received_tick` in input packets
- Server uses this to select appropriate baseline
- If client's baseline is too old, send full snapshot

---

## 4. Connection Lifecycle

### 4.1 Connection Establishment

```
Client                              Server
  |                                    |
  |--- CONNECT (0x10) ---------------->|
  |   protocol_version                 |
  |   client_id                        |
  |                                    |
  |<-- CONNECT_ACK (0x11) -------------|
  |   entity_id                        |
  |   zone_id                          |
  |   server_tick                      |
  |                                    |
  |--- CONNECTED (0x12) -------------->|
  |   (acknowledgment)                 |
  |                                    |
  |=== GAMEPLAY BEGINS ===============|
  |   INPUT (0x20) ---------->        |
  |        <---------- SNAPSHOT (0x21)|
```

### 4.2 Disconnection

**Graceful Disconnect:**
```
Client/Server --- DISCONNECT (0x13) ---> Other side
                reason_code: uint8
                message: string
```

**Disconnect Reasons:**
```cpp
enum DisconnectReason : uint8_t {
    DISCONNECT_NORMAL           = 0x00,  // Intentional disconnect
    DISCONNECT_TIMEOUT          = 0x01,  // Connection timeout
    DISCONNECT_KICK             = 0x02,  // Admin kick
    DISCONNECT_BAN              = 0x03,  // Banned
    DISCONNECT_SERVER_SHUTDOWN  = 0x04,  // Server shutting down
    DISCONNECT_ZONE_HANDOFF     = 0x05,  // Moving to new zone server
    DISCONNECT_PROTOCOL_ERROR   = 0x06,  // Protocol violation
    DISCONNECT_CHEAT_DETECTED   = 0x07,  // Anti-cheat trigger
    DISCONNECT_RATE_LIMITED     = 0x08,  // Too many packets
    DISCONNECT_SERVER_FULL      = 0x09   // Server at capacity
};
```

### 4.3 Heartbeat

**Purpose:** Keep NAT mappings alive, detect dead connections

```
Client/Server --- HEARTBEAT (0x14) ---> Other side
                timestamp: uint32

Other side ----- HEARTBEAT (0x14) ---> Response
```

**Frequency:** Every 5 seconds (configurable)

---

## 5. Bandwidth Optimization

### 5.1 Interest Management (AOI)

Only send entities within player's Area of Interest:

| Distance | Update Rate | Priority |
|----------|-------------|----------|
| 0-50m    | 20Hz        | Critical |
| 50-100m  | 10Hz        | High     |
| 100-200m | 5Hz         | Medium   |
| >200m    | Excluded    | None     |

### 5.2 Field Compression

| Field | Original | Compressed | Savings |
|-------|----------|------------|---------|
| Position (3 floats) | 12 bytes | 6 bytes (int16) | 50% |
| Rotation (4 floats) | 16 bytes | 4 bytes (int8) | 75% |
| Velocity (3 floats) | 12 bytes | 6 bytes (int16) | 50% |
| Health (float) | 4 bytes | 1 byte (uint8) | 75% |

### 5.3 Packet Coalescing

Multiple small reliable events can be coalesced into a single packet:

```cpp
struct CoalescedEventPacket {
    uint8_t type = 0x23;  // COALESCED_EVENTS
    uint8_t event_count;
    struct {
        uint8_t event_type;
        uint16_t payload_length;
        uint8_t payload[];
    } events[];
};
```

---

## 6. Security Considerations

### 6.1 Input Validation

All input packets must be validated:

```cpp
bool validateInput(const InputPacket& input) {
    // Check sequence number (must be increasing)
    if (input.sequence <= lastSequence) {
        return false;  // Replay attack or out of order
    }
    
    // Clamp yaw/pitch to valid ranges
    if (input.yaw < -PI || input.yaw > PI) {
        return false;
    }
    if (input.pitch < -PI/2 || input.pitch > PI/2) {
        return false;
    }
    
    // Validate input flags (no conflicting directions)
    if ((input.input_flags & INPUT_FORWARD) && (input.input_flags & INPUT_BACKWARD)) {
        return false;
    }
    if ((input.input_flags & INPUT_LEFT) && (input.input_flags & INPUT_RIGHT)) {
        return false;
    }
    
    return true;
}
```

### 6.2 Rate Limiting

| Packet Type | Max Rate | Burst |
|-------------|----------|-------|
| Input | 60/sec | 10 |
| Events (reliable) | 10/sec | 5 |
| Connect | 1/min | 1 |
| Total bandwidth | 2KB/s up | - |

### 6.3 Sequence Number Verification

Protects against replay attacks:

```cpp
class SequenceValidator {
    static constexpr uint32_t WINDOW_SIZE = 1024;
    uint32_t highestSequence = 0;
    std::bitset<WINDOW_SIZE> receivedWindow;
    
public:
    bool isValid(uint32_t sequence) {
        if (sequence > highestSequence) {
            // New high sequence
            uint32_t diff = sequence - highestSequence;
            if (diff >= WINDOW_SIZE) {
                receivedWindow.reset();
            } else {
                receivedWindow <<= diff;
            }
            receivedWindow.set(0);
            highestSequence = sequence;
            return true;
        }
        
        // Check if within window and not already received
        uint32_t diff = highestSequence - sequence;
        if (diff >= WINDOW_SIZE) {
            return false;  // Too old
        }
        
        if (receivedWindow.test(diff)) {
            return false;  // Already received (duplicate/replay)
        }
        
        receivedWindow.set(diff);
        return true;
    }
};
```

---

## 7. Implementation Checklist (WP-6-4)

### 7.1 FlatBuffers Schema
- [ ] Create `game_protocol.fbs`
- [ ] Define ClientInput table
- [ ] Define EntityState table
- [ ] Define Snapshot table
- [ ] Define Event union
- [ ] Generate C++ code
- [ ] Generate C# code (for Godot)

### 7.2 Serialization
- [ ] Implement serializeInput()
- [ ] Implement deserializeInput()
- [ ] Implement serializeSnapshot()
- [ ] Implement deserializeSnapshot()
- [ ] Implement serializeEvent()
- [ ] Implement deserializeEvent()

### 7.3 Delta Compression
- [ ] Implement DeltaCompressor class
- [ ] Implement delta encoding
- [ ] Implement delta decoding
- [ ] Baseline management
- [ ] Full snapshot fallback

### 7.4 Testing
- [ ] Round-trip serialization test
- [ ] Delta compression ratio test
- [ ] <50μs per entity benchmark
- [ ] Backward compatibility test
- [ ] Fuzz testing

---

## Document Control

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-01-30 | AI Coordinator | Initial protocol specification |

### Related Documents
- `docs/API_CONTRACTS.md` - API contracts
- `WP_IMPLEMENTATION_GUIDE.md` - Implementation guide
- `PARALLEL_AGENT_COORDINATION.md` - Agent coordination
