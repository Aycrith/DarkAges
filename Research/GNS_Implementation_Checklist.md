# GameNetworkingSockets Implementation Checklist

**For:** DarkAges NetworkManager  
**GNS Version:** v1.4.1  
**Target:** 100-1000 concurrent connections, 60Hz tick, <50ms latency

---

## Phase 1: Basic Setup

### CMake Configuration
- [ ] Add GNS FetchContent or find_package
- [ ] Set `USE_CRYPTO` to MBEDTLS or OpenSSL
- [ ] Windows: Link `ws2_32`, `winmm`, `crypt32`, `iphlpapi`
- [ ] Set `BUILD_STATIC_LIB=ON` for static linking
- [ ] Set `BUILD_EXAMPLES=OFF`, `BUILD_TESTS=OFF` to reduce build time

### Headers
- [ ] Include `<steam/steamnetworkingsockets.h>`
- [ ] Include `<steam/isteamnetworkingutils.h>`
- [ ] Ensure include paths point to GNS include directory

### Initialization
- [ ] Call `GameNetworkingSockets_Init()` - check return value
- [ ] Get interface via `SteamNetworkingSockets()`
- [ ] Configure connection callback via `k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged`
- [ ] Create listen socket with `CreateListenSocketIP()` - IPv6 dual-stack
- [ ] Create poll group with `CreatePollGroup()`
- [ ] Register for callback lookup (global map)

---

## Phase 2: Connection Management

### Accepting Connections
- [ ] Handle `k_ESteamNetworkingConnectionState_Connecting`
- [ ] Validate IP address (DDoS protection)
- [ ] Call `AcceptConnection()` - check return value
- [ ] Add to poll group with `SetConnectionPollGroup()`
- [ ] Track connection state (ID, handle, IP, timestamps)
- [ ] Fire `onClientConnected` callback (queued to main thread)

### Disconnect Handling
- [ ] Handle `k_ESteamNetworkingConnectionState_ClosedByPeer`
- [ ] Handle `k_ESteamNetworkingConnectionState_ProblemDetectedLocally`
- [ ] Update connection stats before removal
- [ ] Fire `onClientDisconnected` callback
- [ ] Call `CloseConnection()` with appropriate reason
- [ ] Remove from tracking maps

### Thread Safety
- [ ] All callbacks queue work to main thread
- [ ] Connection maps protected by mutex
- [ ] Callback queue is thread-safe
- [ ] No game state accessed from GNS threads

---

## Phase 3: Message Handling

### Receiving
- [ ] Call `ReceiveMessagesOnPollGroup()` in update loop
- [ ] Process all messages until 0 returned
- [ ] Parse packet type (first byte)
- [ ] Route to appropriate handler based on type
- [ ] **CRITICAL:** Call `Release()` on every message

### Sending - Unreliable (Snapshots)
- [ ] Use `k_nSteamNetworkingSend_Unreliable`
- [ ] Add `k_nSteamNetworkingSend_NoDelay | k_nSteamNetworkingSend_NoNagle`
- [ ] Check connection active before sending
- [ ] Update byte/packet counters

### Sending - Reliable (Events)
- [ ] Use `k_nSteamNetworkingSend_Reliable`
- [ ] Optionally add `k_nSteamNetworkingSend_NoDelay` for critical events
- [ ] Check return value for errors
- [ ] Update byte/packet counters

---

## Phase 4: Game Loop Integration

### Update Order (per tick)
1. [ ] Process queued callbacks from GNS threads
2. [ ] Call `RunCallbacks()` to process connection events
3. [ ] Receive and process all pending messages
4. [ ] Extract inputs, route to game systems
5. [ ] Run game simulation
6. [ ] Send snapshots (every 3rd tick = 20Hz)
7. [ ] Send reliable events as needed

### Timing
- [ ] Target 60Hz (16.67ms per tick)
- [ ] Use `std::chrono` for precise timing
- [ ] Add small sleep to prevent CPU spinning
- [ ] Monitor tick time, log overruns

### Rate Limiting
- [ ] Check input rate per connection (max 60/sec)
- [ ] Check total bandwidth per connection
- [ ] Drop packets exceeding limits
- [ ] Disconnect persistent offenders

---

## Phase 5: Performance & Optimization

### Poll Groups
- [ ] All connections added to single poll group
- [ ] Use `ReceiveMessagesOnPollGroup()` not per-connection
- [ ] Process messages in batch if needed

### Delta Compression
- [ ] Store baseline snapshots per client
- [ ] Calculate deltas between baseline and current
- [ ] Send only changed fields
- [ ] Include `baseline_tick` in snapshot header

### Interest Management
- [ ] Only send entities within AOI (Area of Interest)
- [ ] Scale update rate by distance
- [ ] Exclude entities behind occluders
- [ ] Remove despawned entities from baseline

### Bandwidth Budget
- [ ] Per player: 5-20 KB/s downstream target
- [ ] Per player: 1-2 KB/s upstream limit
- [ ] Monitor total server bandwidth
- [ ] Degrade quality if budget exceeded

---

## Phase 6: Connection Quality

### Monitoring
- [ ] Track RTT/ping via `GetConnectionRealTimeStatus()`
- [ ] Monitor packet loss percentage
- [ ] Track jitter (connection quality variance)
- [ ] Log bytes/packets sent/received

### Quality Thresholds
- [ ] RTT < 100ms: Good
- [ ] RTT 100-300ms: Playable
- [ ] RTT > 300ms: Poor (warn user)
- [ ] Packet loss > 5%: Degrade to TCP-like behavior

### Adaptive Behavior
- [ ] Reduce snapshot rate for high-latency connections
- [ ] Increase delta compression aggressiveness
- [ ] Disable non-essential updates
- [ ] Consider kicking connections >500ms RTT

---

## Phase 7: Error Handling

### Initialization Failures
- [ ] Log GNS init errors with `SteamDatagramErrMsg`
- [ ] Check for OpenSSL/mbedTLS availability
- [ ] Verify port not already in use
- [ ] Fallback to stub implementation if GNS unavailable

### Runtime Errors
- [ ] Check `SendMessageToConnection()` return value
- [ ] Handle `k_EResultInvalidParam` (bad connection)
- [ ] Handle `k_EResultNoConnection` (disconnected)
- [ ] Mark connections for cleanup on errors

### Recovery
- [ ] Graceful shutdown on SIGTERM/SIGINT
- [ ] Send disconnect reason to all clients
- [ ] Allow time for reliable messages to send
- [ ] Cleanup all resources in reverse order

---

## Phase 8: Testing

### Unit Tests
- [ ] Initialize/shutdown lifecycle
- [ ] Connection acceptance/rejection
- [ ] Message send/receive roundtrip
- [ ] Callback firing
- [ ] Connection quality monitoring

### Integration Tests
- [ ] 100 concurrent connections
- [ ] 1000 concurrent connections (stress)
- [ ] Snapshot broadcast performance
- [ ] Delta compression correctness
- [ ] Reconnection after timeout

### Network Simulation
- [ ] Test with 50ms latency
- [ ] Test with 200ms latency
- [ ] Test with 5% packet loss
- [ ] Test with 20% packet loss
- [ ] Test bandwidth limits

---

## Phase 9: Security

### Connection Validation
- [ ] DDoS protection on connection attempt
- [ ] Rate limit connection attempts per IP
- [ ] Validate protocol version
- [ ] Check authentication token if required

### Input Validation
- [ ] Validate sequence numbers (increasing)
- [ ] Clamp all float values to valid ranges
- [ ] Check input flags for conflicts (forward+backward)
- [ ] Validate position deltas (anti-teleport)

### Rate Limiting
- [ ] Max 60 inputs/second per client
- [ ] Max 10 reliable events/second
- [ ] Max 2KB/s upstream bandwidth
- [ ] Auto-ban exceeding thresholds

---

## Common Pitfalls

### Memory Leaks
- [ ] **Always** call `msg->Release()` after processing
- [ ] Clean up callback queue on shutdown
- [ ] Remove connections from maps on disconnect

### Thread Safety
- [ ] Never call game code from GNS callbacks
- [ ] Always use mutex for shared data
- [ ] Queue callbacks to main thread

### Connection State
- [ ] Check `isActive` before sending
- [ ] Validate connection exists in maps
- [ ] Handle stale connection handles

### Message Size
- [ ] Keep unreliable messages under MTU (1200 bytes)
- [ ] Fragment large reliable messages manually
- [ ] Check max reliable message size (512KB)

---

## Debug Checklist

### Enable Debug Output
```cpp
// Set debug output function
SteamNetworkingUtils()->SetDebugOutputFunction(
    k_ESteamNetworkingSocketsDebugOutputType_Verbose,
    DebugOutputCallback
);
```

### Logging Points
- [ ] Connection accepted/rejected
- [ ] Connection quality changes
- [ ] Rate limiting triggers
- [ ] Message parse errors
- [ ] Send failures

### Performance Metrics
- [ ] Time in `update()` per tick
- [ ] Messages processed per tick
- [ ] Bytes sent/received per tick
- [ ] Connection count
- [ ] Average RTT across all connections

---

## Verification Steps

Before marking complete:

1. [ ] Server starts and listens on specified port
2. [ ] Client can connect from same machine
3. [ ] Client can connect from different machine
4. [ ] Messages flow both directions
5. [ ] Disconnection handled gracefully
6. [ ] 100 clients connect without issues
7. [ ] Memory usage stable over 1 hour
8. [ ] No crashes under stress test
9. [ ] Bandwidth within budget
10. [ ] All unit tests pass

---

## References

- Main Guide: `GameNetworkingSockets_Integration_Guide.md`
- Code Examples: `GNS_Code_Examples.hpp`
- Existing Implementation: `src/server/src/netcode/NetworkManager.cpp`
- Protocol Spec: `docs/network-protocol/PROTOCOL_SPEC.md`
- GNS GitHub: https://github.com/ValveSoftware/GameNetworkingSockets

---

**Status:** Ready for Implementation  
**Last Updated:** 2026-01-30
