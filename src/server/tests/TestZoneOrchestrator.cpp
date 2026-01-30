#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "zones/ZoneOrchestrator.hpp"
#include <cstddef>
#include <cstdint>

using namespace DarkAges;
using Catch::Approx;

TEST_CASE("WorldPartition creates correct zones", "[zones]") {
    SECTION("2x2 grid creates 4 zones") {
        auto zones = WorldPartition::createGrid(2, 2, -1000.0f, 1000.0f, -1000.0f, 1000.0f);
        
        REQUIRE(zones.size() == 4);
        
        // Check zone 1 bounds (bottom-left)
        REQUIRE(zones[0].zoneId == 1);
        REQUIRE(zones[0].minX == Approx(-1000.0f));
        REQUIRE(zones[0].maxX == Approx(0.0f + 50.0f));  // + aura buffer
        REQUIRE(zones[0].minZ == Approx(-1000.0f));
        REQUIRE(zones[0].maxZ == Approx(0.0f + 50.0f));  // + aura buffer
    }
    
    SECTION("Zone adjacencies are correct") {
        auto zones = WorldPartition::createGrid(2, 2, -1000.0f, 1000.0f, -1000.0f, 1000.0f);
        
        // Zone 1 (bottom-left) should be adjacent to zone 2 (right) and zone 3 (top)
        REQUIRE(zones[0].adjacentZones.size() == 2);
        REQUIRE(std::find(zones[0].adjacentZones.begin(), zones[0].adjacentZones.end(), 2) != zones[0].adjacentZones.end());
        REQUIRE(std::find(zones[0].adjacentZones.begin(), zones[0].adjacentZones.end(), 3) != zones[0].adjacentZones.end());
        
        // Zone 4 (top-right) should be adjacent to zone 2 (left) and zone 3 (bottom)
        REQUIRE(zones[3].adjacentZones.size() == 2);
        REQUIRE(std::find(zones[3].adjacentZones.begin(), zones[3].adjacentZones.end(), 2) != zones[3].adjacentZones.end());
        REQUIRE(std::find(zones[3].adjacentZones.begin(), zones[3].adjacentZones.end(), 3) != zones[3].adjacentZones.end());
    }
    
    SECTION("Ports are assigned sequentially") {
        auto zones = WorldPartition::createGrid(2, 2, -1000.0f, 1000.0f, -1000.0f, 1000.0f);
        
        uint16_t basePort = Constants::DEFAULT_SERVER_PORT;
        for (size_t i = 0; i < zones.size(); i++) {
            REQUIRE(zones[i].port == basePort + zones[i].zoneId);
        }
    }
}

TEST_CASE("Zone assignment based on position", "[zones]") {
    auto zones = WorldPartition::createGrid(2, 1, -1000.0f, 1000.0f, -500.0f, 500.0f);
    
    SECTION("Position in left zone") {
        // Position in left zone core (outside buffer)
        auto* zone = WorldPartition::findZoneForPosition(zones, -400.0f, 0.0f);
        REQUIRE(zone != nullptr);
        REQUIRE(zone->zoneId == 1);
    }
    
    SECTION("Position in right zone") {
        // Position in right zone core
        auto* zone = WorldPartition::findZoneForPosition(zones, 400.0f, 0.0f);
        REQUIRE(zone != nullptr);
        REQUIRE(zone->zoneId == 2);
    }
    
    SECTION("Position in buffer returns nullptr for core") {
        // Position in buffer between zones (0 is the boundary)
        auto* zone = WorldPartition::findZoneForPosition(zones, 0.0f, 0.0f);
        // 0 is within buffer of both zones, so no zone "contains" it in their core
        REQUIRE(zone == nullptr);
    }
    
    SECTION("Position outside world returns nullptr") {
        auto* zone = WorldPartition::findZoneForPosition(zones, 2000.0f, 0.0f);
        REQUIRE(zone == nullptr);
    }
}

TEST_CASE("Aura buffer detection", "[zones]") {
    auto zones = WorldPartition::createGrid(2, 1, -1000.0f, 1000.0f, -500.0f, 500.0f);
    
    SECTION("Position in buffer detected correctly") {
        // Position on boundary should be in both zones' aura buffers
        float x = 25.0f;  // Inside zone 2's buffer (zone 2 minX is -50 with buffer)
        
        bool inZone1Buffer = zones[0].isInAuraBuffer(x, 0.0f);
        bool inZone2Buffer = zones[1].isInAuraBuffer(x, 0.0f);
        
        // x=25 is in zone 2's core (zone 2 core starts at 50)
        // Actually let's check the math: zone 2 minX = -50 (with buffer), maxX = 1050
        // Zone 2 core: 50 to 950
        
        // Position at 25 should be in zone 1's buffer (zone 1 maxX is 1050, core ends at 950)
        // Wait, let me recalculate:
        // Zone 1: minX=-1000, maxX=0+50=50 (with buffer), core: -950 to 0
        // Zone 2: minX=0-50=-50, maxX=1000+50=1050, core: 50 to 1000
        
        // So x=25 is between 0 and 50, which is in zone 1's buffer and zone 2's buffer
        REQUIRE((inZone1Buffer || inZone2Buffer));  // Parentheses required for Catch2
    }
    
    SECTION("Aura overlap query returns multiple zones") {
        // Position on boundary should be in both zones
        float x = 0.0f;
        auto overlapping = WorldPartition::findZonesWithAuraOverlap(zones, x, 0.0f);
        
        // Should be in both zones' bounds (including aura buffers)
        REQUIRE(overlapping.size() == 2);
    }
}

TEST_CASE("Hexagonal grid creation", "[zones]") {
    SECTION("Hex grid creates correct number of zones") {
        // 0 rings = 1 zone (center)
        auto zones0 = WorldPartition::createHexagonalGrid(0, 100.0f, 0.0f, 0.0f);
        REQUIRE(zones0.size() == 1);
        
        // 1 ring = 1 center + 6 surrounding = 7 zones
        auto zones1 = WorldPartition::createHexagonalGrid(1, 100.0f, 0.0f, 0.0f);
        REQUIRE(zones1.size() == 7);
        
        // 2 rings = 1 + 6 + 12 = 19 zones
        auto zones2 = WorldPartition::createHexagonalGrid(2, 100.0f, 0.0f, 0.0f);
        REQUIRE(zones2.size() == 19);
    }
    
    SECTION("Hex zones have correct adjacencies") {
        auto zones = WorldPartition::createHexagonalGrid(1, 100.0f, 0.0f, 0.0f);
        
        // Center zone (zone 1) should have 6 adjacent zones
        REQUIRE(zones[0].adjacentZones.size() == 6);
        
        // Peripheral zones should have fewer (3 on average for hex grid)
        for (size_t i = 1; i < zones.size(); i++) {
            REQUIRE(zones[i].adjacentZones.size() >= 2);
        }
    }
}

TEST_CASE("ZoneOrchestrator initialization", "[zones]") {
    ZoneOrchestrator orchestrator;
    auto zones = WorldPartition::createGrid(2, 2, -1000.0f, 1000.0f, -1000.0f, 1000.0f);
    
    SECTION("Initialize creates zone instances") {
        bool result = orchestrator.initialize(zones, nullptr);
        REQUIRE(result == true);
        
        // All zones should start offline
        auto onlineZones = orchestrator.getOnlineZones();
        REQUIRE(onlineZones.empty());
        
        // Should be able to query zone info
        const ZoneInstance* zone = orchestrator.getZone(1);
        REQUIRE(zone != nullptr);
        REQUIRE(zone->state == ZoneState::OFFLINE);
    }
}

TEST_CASE("ZoneOrchestrator player assignment", "[zones]") {
    ZoneOrchestrator orchestrator;
    auto zones = WorldPartition::createGrid(2, 2, -1000.0f, 1000.0f, -1000.0f, 1000.0f);
    orchestrator.initialize(zones, nullptr);
    
    SECTION("Assign player starts zone automatically") {
        uint64_t playerId = 12345;
        uint32_t assignedZone = orchestrator.assignPlayerToZone(playerId, -400.0f, -400.0f);
        
        REQUIRE(assignedZone == 1);
        
        // Zone should now be online
        auto onlineZones = orchestrator.getOnlineZones();
        REQUIRE(onlineZones.size() == 1);
        REQUIRE(onlineZones[0] == 1);
    }
    
    SECTION("Player assignment tracks count") {
        orchestrator.assignPlayerToZone(1, -400.0f, -400.0f);
        orchestrator.assignPlayerToZone(2, -400.0f, -300.0f);
        
        const ZoneInstance* zone = orchestrator.getZone(1);
        REQUIRE(zone->playerCount == 2);
        REQUIRE(orchestrator.getTotalPlayerCount() == 2);
    }
    
    SECTION("Get player zone returns correct assignment") {
        uint64_t playerId = 999;
        orchestrator.assignPlayerToZone(playerId, 400.0f, 400.0f);
        
        uint32_t zoneId = orchestrator.getPlayerZone(playerId);
        REQUIRE(zoneId == 4);  // Top-right zone
    }
    
    SECTION("Get player zone returns 0 for unassigned") {
        uint32_t zoneId = orchestrator.getPlayerZone(99999);
        REQUIRE(zoneId == 0);
    }
}

TEST_CASE("ZoneOrchestrator player removal", "[zones]") {
    ZoneOrchestrator orchestrator;
    auto zones = WorldPartition::createGrid(2, 2, -1000.0f, 1000.0f, -1000.0f, 1000.0f);
    orchestrator.initialize(zones, nullptr);
    
    SECTION("Remove player updates counts") {
        orchestrator.assignPlayerToZone(1, -400.0f, -400.0f);
        REQUIRE(orchestrator.getTotalPlayerCount() == 1);
        
        orchestrator.removePlayer(1);
        REQUIRE(orchestrator.getTotalPlayerCount() == 0);
        REQUIRE(orchestrator.getPlayerZone(1) == 0);
    }
    
    SECTION("Remove unassigned player is safe") {
        orchestrator.removePlayer(99999);  // Should not crash
        REQUIRE(orchestrator.getTotalPlayerCount() == 0);
    }
}

TEST_CASE("ZoneOrchestrator migration detection", "[zones]") {
    ZoneOrchestrator orchestrator;
    auto zones = WorldPartition::createGrid(2, 2, -1000.0f, 1000.0f, -1000.0f, 1000.0f);
    orchestrator.initialize(zones, nullptr);
    
    SECTION("Migration triggered when crossing zones") {
        // Assign player to zone 1
        uint64_t playerId = 1;
        orchestrator.assignPlayerToZone(playerId, -400.0f, -400.0f);
        REQUIRE(orchestrator.getPlayerZone(playerId) == 1);
        
        // Check if should migrate (position now in zone 2)
        uint32_t targetZone = 0;
        bool shouldMigrate = orchestrator.shouldMigratePlayer(playerId, 400.0f, -400.0f, targetZone);
        
        REQUIRE(shouldMigrate == true);
        REQUIRE(targetZone == 2);
    }
    
    SECTION("No migration when staying in same zone") {
        uint64_t playerId = 1;
        orchestrator.assignPlayerToZone(playerId, -400.0f, -400.0f);
        
        uint32_t targetZone = 0;
        bool shouldMigrate = orchestrator.shouldMigratePlayer(playerId, -300.0f, -300.0f, targetZone);
        
        REQUIRE(shouldMigrate == false);
    }
    
    SECTION("No migration for unassigned player") {
        uint32_t targetZone = 0;
        bool shouldMigrate = orchestrator.shouldMigratePlayer(99999, 0.0f, 0.0f, targetZone);
        
        REQUIRE(shouldMigrate == false);
    }
}

TEST_CASE("ZoneOrchestrator zone capacity", "[zones]") {
    ZoneOrchestrator orchestrator;
    auto zones = WorldPartition::createGrid(2, 1, -1000.0f, 1000.0f, -500.0f, 500.0f);
    orchestrator.initialize(zones, nullptr);
    
    SECTION("Zone capacity limits") {
        const ZoneInstance* zone = orchestrator.getZone(1);
        REQUIRE(zone->maxPlayers == Constants::MAX_PLAYERS_PER_ZONE);
        REQUIRE(zone->hasCapacity() == false);  // Offline zones have no capacity
        
        // Start the zone
        orchestrator.startZone(1);
        
        zone = orchestrator.getZone(1);
        REQUIRE(zone->hasCapacity() == true);
    }
}

TEST_CASE("ZoneOrchestrator callbacks", "[zones]") {
    ZoneOrchestrator orchestrator;
    auto zones = WorldPartition::createGrid(2, 2, -1000.0f, 1000.0f, -1000.0f, 1000.0f);
    orchestrator.initialize(zones, nullptr);
    
    SECTION("Zone started callback fires") {
        uint32_t callbackZoneId = 0;
        orchestrator.setOnZoneStarted([&callbackZoneId](uint32_t zoneId) {
            callbackZoneId = zoneId;
        });
        
        orchestrator.startZone(1);
        REQUIRE(callbackZoneId == 1);
    }
    
    SECTION("Zone shutdown callback fires") {
        uint32_t callbackZoneId = 0;
        orchestrator.setOnZoneShutdown([&callbackZoneId](uint32_t zoneId) {
            callbackZoneId = zoneId;
        });
        
        orchestrator.startZone(1);
        orchestrator.shutdownZone(1);
        REQUIRE(callbackZoneId == 1);
    }
    
    SECTION("Player migration callback fires") {
        uint64_t callbackPlayerId = 0;
        uint32_t callbackFromZone = 0;
        uint32_t callbackToZone = 0;
        
        orchestrator.setOnPlayerMigration([&](uint64_t playerId, uint32_t fromZone, uint32_t toZone) {
            callbackPlayerId = playerId;
            callbackFromZone = fromZone;
            callbackToZone = toZone;
        });
        
        orchestrator.assignPlayerToZone(1, -400.0f, -400.0f);
        
        uint32_t targetZone = 0;
        orchestrator.shouldMigratePlayer(1, 400.0f, -400.0f, targetZone);
        
        REQUIRE(callbackPlayerId == 1);
        REQUIRE(callbackFromZone == 1);
        REQUIRE(callbackToZone == 2);
    }
}

TEST_CASE("ZoneDefinition overlap calculation", "[zones]") {
    auto zones = WorldPartition::createGrid(2, 1, -1000.0f, 1000.0f, -500.0f, 500.0f);
    
    SECTION("Adjacent zones have overlap") {
        float minX, maxX, minZ, maxZ;
        bool hasOverlap = zones[0].calculateOverlap(zones[1], minX, maxX, minZ, maxZ);
        
        REQUIRE(hasOverlap == true);
        // Overlap should be the buffer region
        REQUIRE(maxX - minX == Approx(100.0f));  // 2 * AURA_BUFFER_METERS
    }
    
    SECTION("Distance to edge calculation") {
        // Position inside zone
        float dist = zones[0].distanceToEdge(-400.0f, 0.0f);
        REQUIRE(dist < 0);  // Negative when inside
        
        // Position outside zone
        dist = zones[0].distanceToEdge(1000.0f, 0.0f);
        REQUIRE(dist > 0);  // Positive when outside
    }
}
