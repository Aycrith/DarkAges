// [NETWORK_AGENT] Network Protocol unit tests

#include <catch2/catch_test_macros.hpp>
#include "netcode/NetworkManager.hpp"
#include <vector>

using namespace DarkAges;

TEST_CASE("Protocol serialization - InputState", "[network]") {
    SECTION("Serialize and deserialize input") {
        InputState input;
        input.forward = 1;
        input.left = 1;
        input.jump = 1;
        input.yaw = 1.57f;  // ~90 degrees
        input.pitch = 0.5f;
        input.sequence = 12345;
        input.timestamp_ms = 1000000;
        
        auto data = Protocol::serializeInput(input);
        REQUIRE_FALSE(data.empty());
        
        InputState deserialized;
        bool success = Protocol::deserializeInput(data, deserialized);
        
        REQUIRE(success);
        REQUIRE(deserialized.forward == input.forward);
        REQUIRE(deserialized.left == input.left);
        REQUIRE(deserialized.jump == input.jump);
        REQUIRE(deserialized.yaw == input.yaw);
        REQUIRE(deserialized.pitch == input.pitch);
        REQUIRE(deserialized.sequence == input.sequence);
        REQUIRE(deserialized.timestamp_ms == input.timestamp_ms);
    }
    
    SECTION("Deserialize with insufficient data fails") {
        std::vector<uint8_t> smallData = {0x01, 0x02, 0x03};
        InputState deserialized;
        
        bool success = Protocol::deserializeInput(smallData, deserialized);
        
        REQUIRE_FALSE(success);
    }
}

TEST_CASE("Connection quality tracking", "[network]") {
    ConnectionQuality quality;
    
    SECTION("Default values") {
        REQUIRE(quality.rttMs == 0);
        REQUIRE(quality.packetLoss == 0.0f);
        REQUIRE(quality.packetsSent == 0);
        REQUIRE(quality.packetsReceived == 0);
    }
}

TEST_CASE("NetworkManager lifecycle", "[network]") {
    NetworkManager net;
    
    SECTION("Initialize and shutdown") {
        REQUIRE(net.getConnectionCount() == 0);
        REQUIRE(net.getTotalBytesSent() == 0);
        REQUIRE(net.getTotalBytesReceived() == 0);
        
        bool success = net.initialize(9999);  // Different port to avoid conflicts
        REQUIRE(success);
        
        net.shutdown();
    }
}

TEST_CASE("Packet type enumeration", "[network]") {
    // Ensure packet types have expected values for protocol compatibility
    REQUIRE(static_cast<uint8_t>(PacketType::ClientInput) == 1);
    REQUIRE(static_cast<uint8_t>(PacketType::ServerSnapshot) == 2);
    REQUIRE(static_cast<uint8_t>(PacketType::ReliableEvent) == 3);
    REQUIRE(static_cast<uint8_t>(PacketType::Ping) == 4);
    REQUIRE(static_cast<uint8_t>(PacketType::Handshake) == 5);
    REQUIRE(static_cast<uint8_t>(PacketType::Disconnect) == 6);
}
