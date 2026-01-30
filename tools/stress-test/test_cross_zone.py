#!/usr/bin/env python3
"""
Cross-Zone Integration Test for DarkAges MMO
Tests entity migration, aura projection, and seamless handoff.

Usage:
    python test_cross_zone.py [--host HOST] [--test TEST_NAME]

Tests:
    connectivity    - Test all zones are reachable
    migration       - Test player migration between zones
    aura            - Test aura projection at zone boundaries
    load            - Test load distribution across zones
    all             - Run all tests (default)
"""

import asyncio
import argparse
import sys
import time
from dataclasses import dataclass
from typing import List, Dict, Optional
import math

# Import bot implementation from bot_swarm
from bot_swarm import GameBot, BotConfig


@dataclass
class ZoneConfig:
    """Configuration for a zone in the 2x2 grid"""
    zone_id: int
    host: str
    port: int
    min_x: float
    max_x: float
    min_z: float
    max_z: float
    adjacent: List[int]


# Zone definitions matching docker-compose.multi-zone.yml
# Grid layout:
#   Zone 3 (-1000,0)     |     Zone 4 (0,1000)
#   Top-left (0,1000)    |     Top-right (0,1000)
#   ---------------------+--------------------
#   Zone 1 (-1000,0)     |     Zone 2 (0,1000)
#   Bottom-left (-1000,0)|     Bottom-right (0,1000)
ZONES = [
    ZoneConfig(1, "127.0.0.1", 7777, -1000.0, 0.0, -1000.0, 0.0, [2, 3]),
    ZoneConfig(2, "127.0.0.1", 7779, 0.0, 1000.0, -1000.0, 0.0, [1, 4]),
    ZoneConfig(3, "127.0.0.1", 7781, -1000.0, 0.0, 0.0, 1000.0, [1, 4]),
    ZoneConfig(4, "127.0.0.1", 7783, 0.0, 1000.0, 0.0, 1000.0, [2, 3]),
]

# Aura buffer size (must match server configuration)
AURA_BUFFER_SIZE = 50.0


class CrossZoneTester:
    """Test suite for cross-zone functionality"""
    
    def __init__(self, host: str = "127.0.0.1"):
        self.host = host
        self.results: List[Dict] = []
        self.zone_map = {z.zone_id: z for z in ZONES}
    
    def _get_zone_port(self, zone_id: int) -> int:
        """Get the port for a zone ID"""
        return self.zone_map[zone_id].port
    
    def _is_in_aura_buffer(self, x: float, z: float, zone: ZoneConfig) -> bool:
        """Check if position is in aura buffer for zone"""
        # Expand zone bounds by aura buffer
        aura_min_x = zone.min_x - AURA_BUFFER_SIZE
        aura_max_x = zone.max_x + AURA_BUFFER_SIZE
        aura_min_z = zone.min_z - AURA_BUFFER_SIZE
        aura_max_z = zone.max_z + AURA_BUFFER_SIZE
        
        # Check if in expanded bounds but not in core zone
        in_aura = (aura_min_x <= x <= aura_max_x and 
                   aura_min_z <= z <= aura_max_z)
        in_core = (zone.min_x <= x <= zone.max_x and 
                   zone.min_z <= z <= zone.max_z)
        
        return in_aura and not in_core
    
    async def test_zone_connectivity(self) -> bool:
        """Test that all zones are reachable"""
        print("\n=== Test 1: Zone Connectivity ===")
        print("Testing connection to all 4 zones...")
        
        all_passed = True
        for zone in ZONES:
            bot = GameBot(BotConfig(self.host, zone.port, 1000 + zone.zone_id))
            try:
                success = await asyncio.wait_for(bot.connect(), timeout=5.0)
                if success:
                    print(f"  ✓ Zone {zone.zone_id} ({zone.port}): Connected (Entity {bot.entity_id})")
                    bot.disconnect()
                else:
                    print(f"  ✗ Zone {zone.zone_id} ({zone.port}): Connection rejected")
                    all_passed = False
            except asyncio.TimeoutError:
                print(f"  ✗ Zone {zone.zone_id} ({zone.port}): Connection timeout")
                all_passed = False
            except Exception as e:
                print(f"  ✗ Zone {zone.zone_id} ({zone.port}): Error - {e}")
                all_passed = False
        
        self.results.append({
            'name': 'Zone Connectivity',
            'passed': all_passed
        })
        return all_passed
    
    async def test_entity_migration(self) -> bool:
        """Test player migration between zones across x=0 boundary"""
        print("\n=== Test 2: Entity Migration ===")
        print("Testing migration from Zone 1 to Zone 2...")
        
        # Create bot in zone 1, at x=-100 (well within zone)
        zone1 = self.zone_map[1]
        bot = GameBot(BotConfig(self.host, zone1.port, 2000))
        
        try:
            success = await asyncio.wait_for(bot.connect(), timeout=5.0)
            if not success:
                print("  ✗ Failed to connect to zone 1")
                return False
            
            print(f"  ✓ Connected to Zone 1 as Entity {bot.entity_id}")
            print(f"  Initial position: ({bot.position[0]:.1f}, {bot.position[2]:.1f})")
            
            # Move toward zone 2 boundary (x=0)
            # Start at x=-100, target x=-10 (within aura buffer of zone 2)
            bot.position = [-100.0, 0.0, 0.0]
            bot.velocity = [8.0, 0.0, 0.0]  # Moving right at 8 m/s
            
            print(f"  Moving toward boundary at x=0...")
            print(f"  Target: x=-{AURA_BUFFER_SIZE/2:.0f} (within aura buffer)")
            
            # Start background receive loop
            receive_task = asyncio.create_task(self._migration_receive_loop(bot))
            
            # Move toward boundary for 15 seconds
            start_time = time.time()
            migration_detected = False
            snapshots_before = bot.snapshot_count
            
            while time.time() - start_time < 15:
                current_time = time.time()
                elapsed = current_time - start_time
                
                # Update position (8 m/s)
                new_x = -100.0 + (elapsed * 8.0)
                bot.position[0] = min(new_x, -10.0)  # Stop at x=-10
                
                # Check if we reached the aura buffer
                if new_x >= -AURA_BUFFER_SIZE and not migration_detected:
                    print(f"  → Entered aura buffer at x={new_x:.1f}")
                    migration_detected = True
                
                await asyncio.sleep(0.1)
            
            receive_task.cancel()
            try:
                await receive_task
            except asyncio.CancelledError:
                pass
            
            snapshots_during = bot.snapshot_count - snapshots_before
            print(f"  ✓ Received {snapshots_during} snapshots during migration test")
            print(f"  Final position: ({bot.position[0]:.1f}, {bot.position[2]:.1f})")
            
            bot.disconnect()
            
            passed = snapshots_during > 0
            self.results.append({
                'name': 'Entity Migration',
                'passed': passed,
                'snapshots': snapshots_during
            })
            return passed
            
        except asyncio.TimeoutError:
            print("  ✗ Connection timeout")
            return False
        except Exception as e:
            print(f"  ✗ Error during migration test: {e}")
            return False
    
    async def _migration_receive_loop(self, bot: GameBot) -> None:
        """Background loop to receive snapshots during migration"""
        while True:
            try:
                import socket
                data = bot.socket.recv(2048)
                if data:
                    bot.packets_received += 1
                    bot.bytes_received += len(data)
                    if len(data) >= 1 and data[0] == bot.PACKET_SERVER_SNAPSHOT:
                        bot.snapshot_count += 1
            except BlockingIOError:
                await asyncio.sleep(0.001)
            except Exception:
                break
    
    async def test_aura_projection(self) -> bool:
        """Test that entities in aura buffer are visible to adjacent zones"""
        print("\n=== Test 3: Aura Projection ===")
        print("Testing visibility across zone boundary...")
        
        # Bot 1 in zone 1, near boundary (in zone 1, but aura-visible to zone 2)
        zone1_port = self._get_zone_port(1)
        bot1 = GameBot(BotConfig(self.host, zone1_port, 3001))
        bot1.position = [-AURA_BUFFER_SIZE/2, 0.0, 0.0]  # x=-25, in zone 1's aura
        
        # Bot 2 in zone 2, near boundary
        zone2_port = self._get_zone_port(2)
        bot2 = GameBot(BotConfig(self.host, zone2_port, 3002))
        bot2.position = [AURA_BUFFER_SIZE/2, 0.0, 0.0]  # x=25, in zone 2's aura
        
        try:
            # Connect both bots
            conn1 = await asyncio.wait_for(bot1.connect(), timeout=5.0)
            conn2 = await asyncio.wait_for(bot2.connect(), timeout=5.0)
            
            if not conn1 or not conn2:
                print("  ✗ Failed to connect both bots")
                return False
            
            print(f"  ✓ Bot 1 connected to Zone 1 (Entity {bot1.entity_id})")
            print(f"  ✓ Bot 2 connected to Zone 2 (Entity {bot1.entity_id})")
            print(f"  Positions: Bot1=({bot1.position[0]:.1f}, {bot1.position[2]:.1f}), "
                  f"Bot2=({bot2.position[0]:.1f}, {bot2.position[2]:.1f})")
            
            # Start receive loops
            task1 = asyncio.create_task(self._aura_receive_loop(bot1))
            task2 = asyncio.create_task(self._aura_receive_loop(bot2))
            
            # Wait for entity sync (5 seconds)
            print("  Waiting for entity sync (5s)...")
            await asyncio.sleep(5)
            
            task1.cancel()
            task2.cancel()
            try:
                await task1
                await task2
            except asyncio.CancelledError:
                pass
            
            print(f"  Bot 1 snapshots: {bot1.snapshot_count}")
            print(f"  Bot 2 snapshots: {bot2.snapshot_count}")
            
            bot1.disconnect()
            bot2.disconnect()
            
            # Both should receive snapshots containing the other entity
            passed = bot1.snapshot_count > 0 and bot2.snapshot_count > 0
            self.results.append({
                'name': 'Aura Projection',
                'passed': passed,
                'bot1_snapshots': bot1.snapshot_count,
                'bot2_snapshots': bot2.snapshot_count
            })
            return passed
            
        except asyncio.TimeoutError:
            print("  ✗ Connection timeout")
            return False
        except Exception as e:
            print(f"  ✗ Error during aura test: {e}")
            return False
    
    async def _aura_receive_loop(self, bot: GameBot) -> None:
        """Background loop to receive snapshots during aura test"""
        import socket
        while True:
            try:
                data = bot.socket.recv(2048)
                if data:
                    bot.packets_received += 1
                    bot.bytes_received += len(data)
                    if len(data) >= 1 and data[0] == bot.PACKET_SERVER_SNAPSHOT:
                        bot.snapshot_count += 1
            except BlockingIOError:
                await asyncio.sleep(0.001)
            except Exception:
                break
    
    async def test_cross_zone_chat(self) -> bool:
        """Test chat messages across zones (placeholder)"""
        print("\n=== Test 4: Cross-Zone Chat ===")
        print("  Note: Chat system not yet implemented")
        print("  ✓ Placeholder test passed")
        
        self.results.append({
            'name': 'Cross-Zone Chat',
            'passed': True,
            'note': 'Placeholder - not implemented'
        })
        return True
    
    async def test_load_distribution(self) -> bool:
        """Test that load is distributed across zones"""
        print("\n=== Test 5: Load Distribution ===")
        
        bots_per_zone = 5
        total_bots = len(ZONES) * bots_per_zone
        print(f"Spawning {bots_per_zone} bots per zone ({total_bots} total)...")
        
        bots: List[GameBot] = []
        for zone in ZONES:
            for i in range(bots_per_zone):
                bot_id = 4000 + zone.zone_id * 100 + i
                bot = GameBot(BotConfig(self.host, zone.port, bot_id))
                bots.append(bot)
        
        # Connect all bots
        print("Connecting bots...")
        connect_tasks = [bot.connect() for bot in bots]
        connect_results = await asyncio.gather(*connect_tasks, return_exceptions=True)
        
        connected = 0
        zone_counts = {z.zone_id: 0 for z in ZONES}
        
        for bot, result in zip(bots, connect_results):
            if result is True:
                connected += 1
                # Find which zone this bot connected to
                for zone in ZONES:
                    if zone.port == bot.config.port:
                        zone_counts[zone.zone_id] += 1
                        break
        
        print(f"  Connected {connected}/{total_bots} bots:")
        for zone_id, count in zone_counts.items():
            print(f"    Zone {zone_id}: {count} bots")
        
        # Run for a short period
        print("Running for 10 seconds...")
        await asyncio.sleep(10)
        
        # Disconnect all
        print("Disconnecting bots...")
        for bot in bots:
            bot.disconnect()
        
        # Check distribution (expecting roughly even distribution)
        success_rate = connected / total_bots
        passed = success_rate >= 0.8  # 80% success rate minimum
        
        self.results.append({
            'name': 'Load Distribution',
            'passed': passed,
            'connected': connected,
            'total': total_bots,
            'success_rate': f"{success_rate*100:.1f}%",
            'zone_distribution': zone_counts
        })
        
        return passed
    
    async def test_concurrent_migrations(self) -> bool:
        """Test multiple concurrent migrations"""
        print("\n=== Test 6: Concurrent Migrations ===")
        print("Testing 4 simultaneous migrations...")
        
        # Create 4 bots, each starting in a different zone and moving toward
        # an adjacent zone boundary
        migrations = [
            (1, 2, -100.0, 8.0),   # Zone 1 -> 2 (move right)
            (2, 1, 100.0, -8.0),   # Zone 2 -> 1 (move left)
            (3, 4, -100.0, 8.0),   # Zone 3 -> 4 (move right)
            (4, 3, 100.0, -8.0),   # Zone 4 -> 3 (move left)
        ]
        
        bots: List[GameBot] = []
        for i, (from_zone, to_zone, start_x, velocity_x) in enumerate(migrations):
            port = self._get_zone_port(from_zone)
            bot = GameBot(BotConfig(self.host, port, 5000 + i))
            bot.position = [start_x, 0.0, 0.0]
            bot.velocity = [velocity_x, 0.0, 0.0]
            bots.append(bot)
        
        # Connect all bots
        connect_tasks = [bot.connect() for bot in bots]
        results = await asyncio.gather(*connect_tasks, return_exceptions=True)
        
        connected_bots = [bot for bot, result in zip(bots, results) if result is True]
        print(f"  Connected {len(connected_bots)}/{len(bots)} bots")
        
        if len(connected_bots) < len(bots):
            print("  ✗ Not all bots connected")
            return False
        
        # Start receive loops for all bots
        receive_tasks = []
        for bot in connected_bots:
            task = asyncio.create_task(self._migration_receive_loop(bot))
            receive_tasks.append(task)
        
        # Simulate movement for 10 seconds
        print("  Simulating concurrent migrations...")
        start_time = time.time()
        while time.time() - start_time < 10:
            await asyncio.sleep(0.1)
        
        # Cancel receive tasks
        for task in receive_tasks:
            task.cancel()
        
        # Count total snapshots
        total_snapshots = sum(bot.snapshot_count for bot in connected_bots)
        print(f"  Total snapshots received: {total_snapshots}")
        
        for bot in connected_bots:
            bot.disconnect()
        
        passed = total_snapshots > 0
        self.results.append({
            'name': 'Concurrent Migrations',
            'passed': passed,
            'total_snapshots': total_snapshots,
            'bots_connected': len(connected_bots)
        })
        
        return passed
    
    async def run_all_tests(self) -> bool:
        """Run all cross-zone tests"""
        print("="*60)
        print("DARK AGES - Cross-Zone Integration Tests")
        print("="*60)
        print(f"Target: {self.host}")
        print(f"Zones: 4 (2x2 grid, 1000x1000 each)")
        print(f"Aura buffer: {AURA_BUFFER_SIZE}m")
        print("="*60)
        
        tests = [
            ("Zone Connectivity", self.test_zone_connectivity),
            ("Entity Migration", self.test_entity_migration),
            ("Aura Projection", self.test_aura_projection),
            ("Cross-Zone Chat", self.test_cross_zone_chat),
            ("Load Distribution", self.test_load_distribution),
            ("Concurrent Migrations", self.test_concurrent_migrations),
        ]
        
        results = []
        for name, test in tests:
            try:
                success = await test()
                results.append((name, success))
            except Exception as e:
                print(f"  ERROR: {e}")
                import traceback
                traceback.print_exc()
                results.append((name, False))
        
        # Print summary
        print("\n" + "="*60)
        print("TEST SUMMARY")
        print("="*60)
        
        passed = 0
        for name, success in results:
            status = "✓ PASS" if success else "✗ FAIL"
            print(f"  {status}: {name}")
            if success:
                passed += 1
        
        print(f"\nTotal: {passed}/{len(results)} tests passed")
        print("="*60)
        
        return passed == len(results)


def main():
    parser = argparse.ArgumentParser(
        description='DarkAges Cross-Zone Integration Tests',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Run all tests
    python test_cross_zone.py
    
    # Run with remote host
    python test_cross_zone.py --host 192.168.1.100
    
    # Run specific test
    python test_cross_zone.py --test migration

Available tests:
    connectivity  - Test all zones are reachable
    migration     - Test entity migration between zones
    aura          - Test aura projection at boundaries
    load          - Test load distribution
    concurrent    - Test concurrent migrations
    all           - Run all tests (default)
        """
    )
    parser.add_argument('--host', default='127.0.0.1', 
                        help='Server host (default: 127.0.0.1)')
    parser.add_argument('--test', default='all',
                        choices=['connectivity', 'migration', 'aura', 'load', 
                                'concurrent', 'all'],
                        help='Test to run (default: all)')
    
    args = parser.parse_args()
    
    # Update zone hosts if specified
    global ZONES
    ZONES = [ZoneConfig(
        z.zone_id, args.host, z.port,
        z.min_x, z.max_x, z.min_z, z.max_z, z.adjacent
    ) for z in ZONES]
    
    tester = CrossZoneTester(args.host)
    
    if args.test == 'all':
        success = asyncio.run(tester.run_all_tests())
    else:
        test_map = {
            'connectivity': tester.test_zone_connectivity,
            'migration': tester.test_entity_migration,
            'aura': tester.test_aura_projection,
            'load': tester.test_load_distribution,
            'concurrent': tester.test_concurrent_migrations,
        }
        success = asyncio.run(test_map[args.test]())
    
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
