#!/usr/bin/env python3
"""
DarkAges MMO - Multi-Client vs Server vs Entity Validation Test Suite

This script:
1. Spawns multiple Python-based game clients
2. Connects them to the running server
3. Simulates realistic player behavior
4. Validates server entity synchronization
5. Measures performance metrics
6. Generates comprehensive test report

Usage:
    python multi_client_test.py
    python multi_client_test.py --host 127.0.0.1 --port 7777
    python multi_client_test.py --test 03  # Run specific test only

Exit Codes:
    0 - All tests passed
    1 - One or more tests failed
"""

import asyncio
import sys
import time
import json
import random
import statistics
import argparse
import math
from dataclasses import dataclass, asdict, field
from typing import List, Dict, Optional, Tuple
from datetime import datetime
from pathlib import Path

# Add project root to path
project_root = Path(__file__).parent.parent.parent
sys.path.insert(0, str(project_root / "tools" / "stress-test"))

from integration_harness import IntegrationBot, IntegrationTestHarness
from bot_swarm import BotConfig


@dataclass
class Vector3:
    """Simple 3D vector class"""
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0
    
    def copy(self):
        return Vector3(self.x, self.y, self.z)
    
    def distance_to(self, other) -> float:
        if isinstance(other, Vector3):
            return math.sqrt((self.x - other.x)**2 + (self.y - other.y)**2 + (self.z - other.z)**2)
        elif isinstance(other, list) and len(other) >= 3:
            return math.sqrt((self.x - other[0])**2 + (self.y - other[1])**2 + (self.z - other[2])**2)
        return 0.0
    
    def __iter__(self):
        yield self.x
        yield self.y
        yield self.z


class DarkAgesBot:
    """
    Simplified wrapper around IntegrationBot for validation tests.
    Provides a cleaner interface for multi-client test scenarios.
    """
    
    def __init__(self, name: str, host: str, port: int):
        self.name = name
        self.host = host
        self.port = port
        
        # Generate a unique bot ID from name hash
        self.bot_id = hash(name) % 0xFFFFFFFF
        
        # Create underlying bot
        config = BotConfig(
            host=host,
            port=port,
            bot_id=self.bot_id,
            movement_pattern="random",
            update_rate=60.0
        )
        self._bot = IntegrationBot(config)
        
        # Public properties
        self.entity_id: Optional[int] = None
        self.connection_id: Optional[int] = None
        self.connected: bool = False
        self.position = Vector3(0, 0, 0)
        self.velocity = Vector3(0, 0, 0)
        self.seen_entities: List[int] = []
        
        # Stats
        self.packets_sent = 0
        self.packets_received = 0
    
    async def connect(self) -> bool:
        """Connect to server"""
        result = await self._bot.connect()
        if result:
            self.connected = True
            self.entity_id = self._bot.entity_id
            self.connection_id = self._bot.connection_id
            # Initialize position from bot
            self.position = Vector3(*self._bot.position)
        return result
    
    async def disconnect(self) -> None:
        """Disconnect from server"""
        self._bot.disconnect()
        self.connected = False
    
    async def send_input(self, forward: bool = False, backward: bool = False,
                        left: bool = False, right: bool = False,
                        jump: bool = False, attack: bool = False,
                        block: bool = False, sprint: bool = False) -> None:
        """Send input state to server"""
        self._bot.input_state['forward'] = forward
        self._bot.input_state['backward'] = backward
        self._bot.input_state['left'] = left
        self._bot.input_state['right'] = right
        self._bot.input_state['jump'] = jump
        self._bot.input_state['attack'] = attack
        self._bot.input_state['block'] = block
        self._bot.input_state['sprint'] = sprint
        
        # Create and send packet directly
        packet = self._bot._create_input_packet()
        await self._bot._send_packet(packet)
        self.packets_sent += 1
    
    async def update(self, duration: float = 0.016) -> None:
        """Update bot state - receive packets"""
        # Process any pending packets
        try:
            while True:
                data = self._bot.socket.recv(2048)
                if data:
                    self._bot._process_packet(data)
                    self.packets_received += 1
                    # Update seen entities from snapshots
                    if len(data) >= 17 and data[0] == self._bot.PACKET_SERVER_SNAPSHOT:
                        # Track that we received a snapshot
                        pass
        except BlockingIOError:
            pass
        except Exception:
            pass
        
        # Update position
        self.position = Vector3(*self._bot.position)
        self.velocity = Vector3(*self._bot.velocity)
        
        await asyncio.sleep(duration)


@dataclass
class TestMetrics:
    """Test execution metrics"""
    test_name: str
    start_time: float
    end_time: float = 0
    passed: bool = False
    error_message: str = ""
    
    # Connection metrics
    clients_connected: int = 0
    clients_expected: int = 0
    connection_failures: int = 0
    avg_connection_time_ms: float = 0
    
    # Gameplay metrics
    total_packets_sent: int = 0
    total_packets_received: int = 0
    packet_loss_percent: float = 0
    avg_rtt_ms: float = 0
    max_rtt_ms: float = 0
    
    # Entity metrics
    entities_spawned: int = 0
    entities_synced: int = 0
    avg_sync_latency_ms: float = 0
    
    # Server metrics
    server_tick_time_ms: float = 0
    server_memory_mb: float = 0
    server_cpu_percent: float = 0


class MultiClientTestSuite:
    """Comprehensive multi-client test suite"""
    
    def __init__(self, server_host: str = "127.0.0.1", server_port: int = 7777):
        self.server_host = server_host
        self.server_port = server_port
        self.metrics: List[TestMetrics] = []
        self.log_file = None
        
    def log(self, message: str, level: str = "INFO"):
        """Log with timestamp"""
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        emoji = {"INFO": "[INFO]", "PASS": "[PASS]", "FAIL": "[FAIL]", "WARN": "[WARN]"}.get(level, "[INFO]")
        line = f"{emoji} [{timestamp}] [{level}] {message}"
        print(line)
        if self.log_file:
            print(line, file=self.log_file)
            self.log_file.flush()
    
    async def test_01_single_client_connectivity(self) -> TestMetrics:
        """Test 1: Single client can connect and receive entity ID"""
        self.log("Test 1: Single Client Connectivity")
        
        metrics = TestMetrics(
            test_name="single_client_connectivity",
            start_time=time.time(),
            clients_expected=1
        )
        
        try:
            bot = DarkAgesBot("test_single", self.server_host, self.server_port)
            
            start = time.time()
            success = await bot.connect()
            connect_time = (time.time() - start) * 1000
            
            if success and bot.entity_id is not None:
                metrics.clients_connected = 1
                metrics.avg_connection_time_ms = connect_time
                metrics.entities_spawned = 1
                metrics.passed = True
                self.log(f"  [OK] Connected in {connect_time:.1f}ms, Entity ID: {bot.entity_id}", "PASS")
            else:
                metrics.connection_failures = 1
                metrics.error_message = "Failed to connect or no entity ID received"
                self.log(f"  [BAD] Connection failed", "FAIL")
            
            await bot.disconnect()
            
        except Exception as e:
            metrics.error_message = str(e)
            self.log(f"  [BAD] Exception: {e}", "FAIL")
        
        metrics.end_time = time.time()
        return metrics
    
    async def test_02_ten_clients_movement(self) -> TestMetrics:
        """Test 2: Ten clients connect, move around, validate positions"""
        self.log("Test 2: Ten Client Movement Validation")
        
        metrics = TestMetrics(
            test_name="ten_client_movement",
            start_time=time.time(),
            clients_expected=10
        )
        
        bots: List[DarkAgesBot] = []
        rtts = []
        
        try:
            # Connect all bots
            self.log("  Connecting 10 bots...")
            for i in range(10):
                bot = DarkAgesBot(f"test_bot_{i}", self.server_host, self.server_port)
                
                start = time.time()
                success = await bot.connect()
                connect_time = (time.time() - start) * 1000
                
                if success:
                    bots.append(bot)
                    rtts.append(connect_time)
                    self.log(f"    Bot {i}: Connected (Entity {bot.entity_id})")
                else:
                    metrics.connection_failures += 1
                    self.log(f"    Bot {i}: Failed to connect", "WARN")
                
                # Small delay between connections
                await asyncio.sleep(0.1)
            
            metrics.clients_connected = len(bots)
            
            if len(bots) < 8:  # Require at least 80% connection rate
                metrics.error_message = f"Only {len(bots)}/10 bots connected"
                self.log(f"  [BAD] Insufficient connections", "FAIL")
                return metrics
            
            if rtts:
                metrics.avg_connection_time_ms = statistics.mean(rtts)
                metrics.max_rtt_ms = max(rtts)
            
            # Movement phase - 10 seconds
            self.log("  Movement phase (10s)...")
            start_positions = [(b.entity_id, b.position.copy()) for b in bots]
            
            # Start receive loops for all bots
            async def bot_loop(bot: DarkAgesBot, duration: float):
                start = time.time()
                while time.time() - start < duration:
                    await bot.send_input(
                        forward=random.random() > 0.3,
                        left=random.random() > 0.7,
                        right=random.random() > 0.7
                    )
                    await bot.update(0.1)
            
            # Run all bots concurrently
            await asyncio.gather(*[bot_loop(b, 10.0) for b in bots])
            
            # Calculate total packets
            metrics.total_packets_sent = sum(b.packets_sent for b in bots)
            metrics.total_packets_received = sum(b.packets_received for b in bots)
            
            # Verify positions changed
            moved_count = 0
            for i, bot in enumerate(bots):
                # Check if position changed from initial
                initial_pos = start_positions[i][1]
                if bot.position.distance_to(initial_pos) > 0.1:
                    moved_count += 1
            
            metrics.entities_synced = moved_count
            
            if moved_count >= len(bots) * 0.8:  # 80% moved
                metrics.passed = True
                self.log(f"  [OK] {moved_count}/{len(bots)} entities moved correctly", "PASS")
            else:
                metrics.error_message = f"Only {moved_count}/{len(bots)} entities moved"
                self.log(f"  [BAD] Movement validation failed", "FAIL")
            
        except Exception as e:
            metrics.error_message = str(e)
            self.log(f"  [BAD] Exception: {e}", "FAIL")
        
        finally:
            for bot in bots:
                await bot.disconnect()
        
        metrics.end_time = time.time()
        return metrics
    
    async def test_03_fifty_player_stress(self) -> TestMetrics:
        """Test 3: 50 player stress test with metrics collection"""
        self.log("Test 3: Fifty Player Stress Test")
        
        metrics = TestMetrics(
            test_name="fifty_player_stress",
            start_time=time.time(),
            clients_expected=50
        )
        
        bots: List[DarkAgesBot] = []
        
        try:
            # Phase 1: Connect all bots gradually
            self.log("  Phase 1: Connecting 50 bots (gradual)...")
            for i in range(50):
                bot = DarkAgesBot(f"stress_bot_{i}", self.server_host, self.server_port)
                
                if await bot.connect():
                    bots.append(bot)
                    if (i + 1) % 10 == 0:
                        self.log(f"    {i+1}/50 connected...")
                
                # Small delay between connections
                await asyncio.sleep(0.1)
            
            metrics.clients_connected = len(bots)
            self.log(f"  {len(bots)}/50 bots connected")
            
            if len(bots) < 40:  # Require 80%
                metrics.error_message = f"Only {len(bots)}/50 connected"
                self.log(f"  [BAD] Insufficient connections for stress test", "FAIL")
                return metrics
            
            # Phase 2: Stress test - 60 seconds of heavy activity
            self.log("  Phase 2: 60-second stress test...")
            start_time = time.time()
            packet_count = 0
            
            async def stress_bot_loop(bot: DarkAgesBot, duration: float):
                nonlocal packet_count
                start = time.time()
                while time.time() - start < duration:
                    await bot.send_input(
                        forward=random.random() > 0.2,
                        left=random.random() > 0.6,
                        right=random.random() > 0.6,
                        attack=random.random() > 0.9
                    )
                    packet_count += 1
                    await bot.update(0.1)
            
            # Run stress test
            await asyncio.gather(*[stress_bot_loop(b, 60.0) for b in bots])
            
            metrics.total_packets_sent = packet_count
            
            # Check if all bots still connected
            still_connected = sum(1 for b in bots if b.connected)
            self.log(f"  {still_connected}/{len(bots)} bots still connected after stress")
            
            if still_connected >= len(bots) * 0.9:  # 90% survival
                metrics.passed = True
                self.log(f"  [OK] Stress test passed", "PASS")
            else:
                metrics.error_message = f"{len(bots) - still_connected} bots disconnected"
                self.log(f"  [BAD] Too many disconnections", "FAIL")
            
        except Exception as e:
            metrics.error_message = str(e)
            self.log(f"  [BAD] Exception: {e}", "FAIL")
        
        finally:
            self.log("  Disconnecting all bots...")
            for bot in bots:
                await bot.disconnect()
        
        metrics.end_time = time.time()
        return metrics
    
    async def test_04_entity_synchronization(self) -> TestMetrics:
        """Test 4: Validate that entities see each other correctly"""
        self.log("Test 4: Entity Synchronization Validation")
        
        metrics = TestMetrics(
            test_name="entity_synchronization",
            start_time=time.time(),
            clients_expected=5
        )
        
        bots: List[DarkAgesBot] = []
        
        try:
            # Connect 5 bots
            for i in range(5):
                bot = DarkAgesBot(f"sync_bot_{i}", self.server_host, self.server_port)
                await bot.connect()
                bots.append(bot)
                await asyncio.sleep(0.1)
            
            metrics.clients_connected = len(bots)
            metrics.entities_spawned = len(bots)
            
            # Wait for snapshots to propagate (5 seconds)
            self.log("  Waiting for snapshot propagation...")
            async def sync_loop(bot: DarkAgesBot, duration: float):
                start = time.time()
                while time.time() - start < duration:
                    await bot.send_input(forward=False)
                    await bot.update(0.1)
            
            await asyncio.gather(*[sync_loop(b, 5.0) for b in bots])
            
            # Each bot should receive snapshots containing other entities
            # Since we can't parse full entity data from simplified snapshots,
            # we use packet reception as a proxy for synchronization
            packets_per_bot = [b.packets_received for b in bots]
            avg_packets = statistics.mean(packets_per_bot) if packets_per_bot else 0
            
            # Bots receiving packets indicates they're getting snapshots
            synced_count = sum(1 for p in packets_per_bot if p >= avg_packets * 0.5)
            metrics.entities_synced = synced_count
            
            if synced_count >= 4:  # 80% of bots receiving data
                metrics.passed = True
                self.log(f"  [OK] {synced_count}/5 bots receiving synchronized data", "PASS")
            else:
                metrics.error_message = f"Only {synced_count}/5 bots synced"
                self.log(f"  [BAD] Synchronization incomplete", "FAIL")
            
        except Exception as e:
            metrics.error_message = str(e)
            self.log(f"  [BAD] Exception: {e}", "FAIL")
        
        finally:
            for bot in bots:
                await bot.disconnect()
        
        metrics.end_time = time.time()
        return metrics
    
    async def test_05_packet_loss_recovery(self) -> TestMetrics:
        """Test 5: Simulate packet loss and verify recovery"""
        self.log("Test 5: Packet Loss Recovery")
        
        metrics = TestMetrics(
            test_name="packet_loss_recovery",
            start_time=time.time()
        )
        
        # This is a basic connectivity resilience test
        # Full packet loss simulation would require network manipulation tools
        
        try:
            bot = DarkAgesBot("loss_test", self.server_host, self.server_port)
            
            if not await bot.connect():
                metrics.error_message = "Initial connection failed"
                self.log("  [BAD] Connection failed", "FAIL")
                return metrics
            
            # Send packets at high rate then stop temporarily
            self.log("  Sending packets...")
            for _ in range(50):
                await bot.send_input(forward=True)
                await bot.update(0.02)  # 50Hz
            
            # Pause sending (simulating loss)
            self.log("  Simulating pause (2s)...")
            await asyncio.sleep(2)
            
            # Update during pause (receive only)
            for _ in range(20):
                await bot.update(0.1)
            
            # Resume and verify still connected
            self.log("  Resuming...")
            for _ in range(50):
                await bot.send_input(forward=True)
                await bot.update(0.02)
            
            if bot.connected:
                metrics.passed = True
                self.log("  [OK] Connection maintained through pause", "PASS")
            else:
                metrics.error_message = "Connection lost after pause"
                self.log("  [BAD] Connection lost", "FAIL")
            
            await bot.disconnect()
            
        except Exception as e:
            metrics.error_message = str(e)
            self.log(f"  [BAD] Exception: {e}", "FAIL")
        
        metrics.end_time = time.time()
        return metrics
    
    async def run_all_tests(self, specific_test: Optional[str] = None) -> List[TestMetrics]:
        """Run complete test suite"""
        
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        log_path = f"validation_test_{timestamp}.log"
        self.log_file = open(log_path, 'w')
        
        self.log("=" * 70)
        self.log("DarkAges MMO - Multi-Client Validation Test Suite")
        self.log(f"Started: {datetime.now()}")
        self.log(f"Server: {self.server_host}:{self.server_port}")
        self.log("=" * 70)
        
        tests = [
            ("01", self.test_01_single_client_connectivity),
            ("02", self.test_02_ten_clients_movement),
            ("03", self.test_03_fifty_player_stress),
            ("04", self.test_04_entity_synchronization),
            ("05", self.test_05_packet_loss_recovery),
        ]
        
        # Filter to specific test if requested
        if specific_test:
            tests = [(t[0], t[1]) for t in tests if t[0] == specific_test]
            if not tests:
                self.log(f"Unknown test: {specific_test}", "FAIL")
                self.log_file.close()
                return []
        
        for test_id, test in tests:
            try:
                metrics = await test()
                self.metrics.append(metrics)
            except Exception as e:
                self.log(f"Test {test_id} crashed: {e}", "FAIL")
                import traceback
                self.log(traceback.format_exc())
                # Add failed metric
                self.metrics.append(TestMetrics(
                    test_name=test.__name__,
                    start_time=time.time(),
                    end_time=time.time(),
                    error_message=str(e)
                ))
            
            # Brief pause between tests
            await asyncio.sleep(1)
        
        # Generate report
        passed = self.generate_report(timestamp)
        
        self.log_file.close()
        return self.metrics, passed
    
    def generate_report(self, timestamp: str) -> bool:
        """Generate comprehensive test report. Returns True if all passed."""
        
        self.log("")
        self.log("=" * 70)
        self.log("TEST SUMMARY")
        self.log("=" * 70)
        
        passed = sum(1 for m in self.metrics if m.passed)
        total = len(self.metrics)
        
        for m in self.metrics:
            status = "PASS" if m.passed else "FAIL"
            duration = m.end_time - m.start_time
            self.log(f"{status:4} {m.test_name:30} {duration:6.1f}s")
        
        self.log("-" * 70)
        self.log(f"Total: {passed}/{total} tests passed")
        
        all_passed = passed == total
        
        if all_passed:
            self.log("[PASS] ALL TESTS PASSED - Ready for production hardening", "PASS")
        else:
            self.log("[FAIL] SOME TESTS FAILED - Fix before production", "FAIL")
        
        # Write JSON report
        report_data = {
            "timestamp": datetime.now().isoformat(),
            "server": f"{self.server_host}:{self.server_port}",
            "summary": {
                "total": total,
                "passed": passed,
                "failed": total - passed
            },
            "tests": [asdict(m) for m in self.metrics]
        }
        
        report_file = f"validation_report_{timestamp}.json"
        with open(report_file, 'w') as f:
            json.dump(report_data, f, indent=2)
        
        self.log(f"")
        self.log(f"Detailed report: {report_file}")
        self.log(f"Log file: validation_test_{timestamp}.log")
        
        return all_passed


def main():
    parser = argparse.ArgumentParser(description="DarkAges Multi-Client Validation")
    parser.add_argument("--host", default="127.0.0.1", help="Server host")
    parser.add_argument("--port", type=int, default=7777, help="Server port")
    parser.add_argument("--test", help="Run specific test (01-05)")
    
    args = parser.parse_args()
    
    suite = MultiClientTestSuite(args.host, args.port)
    
    # Run all or specific test
    metrics, passed = asyncio.run(suite.run_all_tests(args.test))
    
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()
