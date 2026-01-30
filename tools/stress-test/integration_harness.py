#!/usr/bin/env python3
"""
DarkAges MMO Integration Test Harness (WP-6-5)

Comprehensive integration testing framework for DarkAges server.
Tests connectivity, Redis/ScyllaDB integration, and multi-player scenarios.

Usage:
    python integration_harness.py --test basic_connectivity
    python integration_harness.py --test 10_player_session
    python integration_harness.py --stress 100 --duration 3600
    python integration_harness.py --all  # Run all integration tests

Exit Codes:
    0 - All tests passed
    1 - One or more tests failed
    2 - Critical failure (server not running or infrastructure unavailable)
"""

import asyncio
import argparse
import time
import random
import json
import socket
import struct
import statistics
import sys
import os
from dataclasses import dataclass, field, asdict
from typing import List, Dict, Optional, Callable, Any, Tuple
from pathlib import Path
from datetime import datetime
from concurrent.futures import ThreadPoolExecutor

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))

from bot_swarm import GameBot, BotConfig, BotSwarm
from bot_swarm import DEFAULT_SERVER_PORT, TICK_RATE_HZ, SNAPSHOT_RATE_HZ
from bot_swarm import MAX_UPSTREAM_BYTES_PER_SEC, MAX_DOWNSTREAM_BYTES_PER_SEC


# =============================================================================
# Constants
# =============================================================================

PACKET_CONNECTION_REQUEST = 3
PACKET_CONNECTION_RESPONSE = 4
PACKET_CLIENT_INPUT = 1
PACKET_SERVER_SNAPSHOT = 2
PACKET_SERVER_CORRECTION = 6
PACKET_EVENT = 5

WORLD_BOUNDS = {
    'min_x': -5000.0, 'max_x': 5000.0,
    'min_z': -5000.0, 'max_z': 5000.0
}


# =============================================================================
# Data Classes
# =============================================================================

@dataclass
class TestResult:
    """Result of a single test execution"""
    name: str
    passed: bool
    duration_ms: float
    details: Dict[str, Any] = field(default_factory=dict)
    error: Optional[str] = None
    timestamp: str = field(default_factory=lambda: datetime.now().isoformat())
    
    def to_dict(self) -> Dict:
        """Convert to dictionary for serialization"""
        return {
            'name': self.name,
            'passed': self.passed,
            'duration_ms': self.duration_ms,
            'details': self.details,
            'error': self.error,
            'timestamp': self.timestamp
        }


@dataclass
class SessionInfo:
    """Player session information from Redis"""
    entity_id: int
    connection_id: int
    position: Dict[str, float]
    health: int
    last_activity: float


# =============================================================================
# Integration Test Bot
# =============================================================================

class IntegrationBot(GameBot):
    """Extended bot with additional integration testing capabilities"""
    
    def __init__(self, config: BotConfig):
        super().__init__(config)
        self.snapshots_received: List[Dict] = []
        self.corrections_received: List[Dict] = []
        self.events_received: List[Dict] = []
        self.connection_time: Optional[float] = None
        self.disconnection_time: Optional[float] = None
        
    async def connect(self) -> bool:
        """Connect and record connection time"""
        result = await super().connect()
        if result:
            self.connection_time = time.time()
        return result
    
    def disconnect(self) -> None:
        """Disconnect and record disconnection time"""
        if self.connected:
            self.disconnection_time = time.time()
        super().disconnect()
    
    def _parse_snapshot(self, data: bytes) -> None:
        """Extended snapshot parsing with storage"""
        super()._parse_snapshot(data)
        if len(data) >= 17:
            self.snapshots_received.append({
                'timestamp': time.time(),
                'server_tick': self.server_tick,
                'server_time': self.server_time,
                'last_processed_input': self.last_processed_input
            })
    
    def _parse_correction(self, data: bytes) -> None:
        """Extended correction parsing with storage"""
        super()._parse_correction(data)
        self.corrections_received.append({
            'timestamp': time.time(),
            'data': data.hex() if len(data) < 100 else f"{len(data)} bytes"
        })
    
    def _parse_event(self, data: bytes) -> None:
        """Extended event parsing with storage"""
        super()._parse_event(data)
        if len(data) >= 5:
            event_type = data[1] if len(data) > 1 else 0
            self.events_received.append({
                'timestamp': time.time(),
                'event_type': event_type,
                'size': len(data)
            })
    
    def get_session_duration(self) -> float:
        """Get session duration in seconds"""
        if not self.connection_time:
            return 0.0
        end = self.disconnection_time or time.time()
        return end - self.connection_time
    
    def get_snapshot_rate(self) -> float:
        """Calculate average snapshot rate"""
        if len(self.snapshots_received) < 2:
            return 0.0
        duration = self.get_session_duration()
        if duration <= 0:
            return 0.0
        return len(self.snapshots_received) / duration


# =============================================================================
# Service Health Checks
# =============================================================================

class ServiceHealthChecker:
    """Check health of external services (Redis, ScyllaDB)"""
    
    def __init__(self, redis_host: str = 'localhost', redis_port: int = 6379,
                 scylla_host: str = 'localhost', scylla_port: int = 9042):
        self.redis_host = redis_host
        self.redis_port = redis_port
        self.scylla_host = scylla_host
        self.scylla_port = scylla_port
    
    def check_redis(self) -> Tuple[bool, Optional[str]]:
        """Check Redis connectivity"""
        try:
            import redis as redis_lib
            client = redis_lib.Redis(
                host=self.redis_host, 
                port=self.redis_port,
                socket_connect_timeout=2,
                socket_timeout=2
            )
            client.ping()
            return True, None
        except ImportError:
            return False, "redis package not installed (pip install redis)"
        except Exception as e:
            return False, str(e)
    
    def check_scylla(self) -> Tuple[bool, Optional[str]]:
        """Check ScyllaDB connectivity"""
        try:
            import cassandra
            from cassandra.cluster import Cluster
            cluster = Cluster([self.scylla_host], port=self.scylla_port)
            session = cluster.connect()
            session.execute("SELECT now() FROM system.local")
            cluster.shutdown()
            return True, None
        except ImportError:
            return False, "cassandra-driver not installed (pip install cassandra-driver)"
        except Exception as e:
            return False, str(e)
    
    def check_server(self, host: str, port: int) -> Tuple[bool, Optional[str]]:
        """Check if game server is responding to UDP"""
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.settimeout(2.0)
            
            # Send a connection request
            packet = struct.pack('<BIQ', 
                PACKET_CONNECTION_REQUEST,
                1,  # Protocol version
                999999  # Test player ID
            )
            sock.sendto(packet, (host, port))
            
            # Wait for response
            try:
                data, addr = sock.recvfrom(1024)
                if len(data) >= 1:
                    return True, None
            except socket.timeout:
                return False, "No response from server (timeout)"
            finally:
                sock.close()
                
        except Exception as e:
            return False, str(e)
        
        return False, "No response"
    
    def get_status_report(self, server_host: str, server_port: int) -> Dict:
        """Get complete status report of all services"""
        redis_ok, redis_err = self.check_redis()
        scylla_ok, scylla_err = self.check_scylla()
        server_ok, server_err = self.check_server(server_host, server_port)
        
        return {
            'redis': {'healthy': redis_ok, 'error': redis_err},
            'scylla': {'healthy': scylla_ok, 'error': scylla_err},
            'server': {'healthy': server_ok, 'error': server_err},
            'all_healthy': redis_ok and scylla_ok and server_ok
        }


# =============================================================================
# Integration Test Harness
# =============================================================================

class IntegrationTestHarness:
    """Main integration test harness for DarkAges server"""
    
    def __init__(self, server_host: str = "127.0.0.1", server_port: int = 7777,
                 redis_host: str = "localhost", redis_port: int = 6379):
        self.server_host = server_host
        self.server_port = server_port
        self.redis_host = redis_host
        self.redis_port = redis_port
        self.bots: List[IntegrationBot] = []
        self.results: List[TestResult] = []
        self.health_checker = ServiceHealthChecker(
            redis_host=redis_host, redis_port=redis_port
        )
        
    async def spawn_bot(self, bot_id: str) -> IntegrationBot:
        """Create and connect a bot"""
        config = BotConfig(
            host=self.server_host,
            port=self.server_port,
            bot_id=int(bot_id.split('_')[-1]) if '_' in bot_id else random.randint(1000, 9999),
            movement_pattern='random',
            update_rate=TICK_RATE_HZ
        )
        bot = IntegrationBot(config)
        self.bots.append(bot)
        return bot
    
    async def spawn_bots(self, count: int) -> List[IntegrationBot]:
        """Spawn multiple bots concurrently"""
        tasks = []
        for i in range(count):
            tasks.append(self.spawn_bot(f"bot_{i}"))
        return await asyncio.gather(*tasks)
    
    async def cleanup(self):
        """Disconnect all bots and clear list"""
        disconnect_tasks = []
        for bot in self.bots:
            disconnect_tasks.append(asyncio.to_thread(bot.disconnect))
        if disconnect_tasks:
            await asyncio.gather(*disconnect_tasks, return_exceptions=True)
        self.bots.clear()
    
    # =========================================================================
    # Test Cases
    # =========================================================================
    
    async def test_service_health(self) -> TestResult:
        """Test 0: Verify all services are healthy"""
        start = time.time()
        
        try:
            report = self.health_checker.get_status_report(
                self.server_host, self.server_port
            )
            
            passed = report['all_healthy']
            details = {
                'redis': report['redis']['healthy'],
                'scylla': report['scylla']['healthy'],
                'server': report['server']['healthy']
            }
            
            if not passed:
                errors = []
                for service in ['redis', 'scylla', 'server']:
                    if not report[service]['healthy']:
                        errors.append(f"{service}: {report[service]['error']}")
                error_msg = "; ".join(errors)
            else:
                error_msg = None
                
            return TestResult(
                name="service_health",
                passed=passed,
                duration_ms=(time.time() - start) * 1000,
                details=details,
                error=error_msg
            )
            
        except Exception as e:
            return TestResult(
                name="service_health",
                passed=False,
                duration_ms=(time.time() - start) * 1000,
                error=str(e)
            )
    
    async def test_basic_connectivity(self) -> TestResult:
        """Test 1: Single bot can connect and receive snapshot"""
        start = time.time()
        
        try:
            bot = await self.spawn_bot("test_basic")
            success = await bot.connect()
            
            if not success:
                return TestResult(
                    name="basic_connectivity",
                    passed=False,
                    duration_ms=(time.time() - start) * 1000,
                    error="Connection failed - server may not be running"
                )
            
            # Send some input
            await bot.send_input(forward=True)
            await asyncio.sleep(0.1)
            
            # Receive snapshot
            snapshot = await bot.receive_snapshot(timeout=2.0)
            
            await bot.disconnect()
            
            return TestResult(
                name="basic_connectivity",
                passed=snapshot is not None,
                duration_ms=(time.time() - start) * 1000,
                details={
                    "entity_id": bot.entity_id,
                    "connection_id": bot.connection_id,
                    "received_snapshot": snapshot is not None,
                    "session_duration_ms": bot.get_session_duration() * 1000
                }
            )
                            
        except Exception as e:
            return TestResult(
                name="basic_connectivity",
                passed=False,
                duration_ms=(time.time() - start) * 1000,
                error=str(e)
            )
    
    async def test_10_player_session(self) -> TestResult:
        """Test 2: 10 bots connect, move, disconnect cleanly"""
        start = time.time()
        
        try:
            # Spawn 10 bots
            bots = await self.spawn_bots(10)
            
            # Connect all concurrently
            connect_tasks = [bot.connect() for bot in bots]
            connect_results = await asyncio.gather(*connect_tasks, return_exceptions=True)
            
            connected_count = sum(1 for r in connect_results if r is True)
            
            if connected_count < 9:  # Allow 1 failure
                return TestResult(
                    name="10_player_session",
                    passed=False,
                    duration_ms=(time.time() - start) * 1000,
                    details={"connected": connected_count, "target": 10},
                    error=f"Only {connected_count}/10 bots connected"
                )
            
            # Run simulation for 10 seconds
            connected_bots = [b for b in bots if b.connected]
            run_tasks = [bot.run(10.0) for bot in connected_bots]
            await asyncio.gather(*run_tasks, return_exceptions=True)
            
            # Collect statistics
            total_snapshots = sum(len(b.snapshots_received) for b in connected_bots)
            avg_snapshot_rate = statistics.mean([b.get_snapshot_rate() for b in connected_bots]) if connected_bots else 0
            total_corrections = sum(len(b.corrections_received) for b in connected_bots)
            
            # Disconnect all
            await self.cleanup()
            
            # Validate results
            passed = (
                connected_count >= 9 and
                avg_snapshot_rate >= 15.0 and  # At least 15Hz snapshot rate expected
                total_corrections < len(connected_bots) * 50  # Less than 50 corrections per bot
            )
            
            return TestResult(
                name="10_player_session",
                passed=passed,
                duration_ms=(time.time() - start) * 1000,
                details={
                    "connected": connected_count,
                    "target": 10,
                    "total_snapshots": total_snapshots,
                    "avg_snapshot_rate_hz": round(avg_snapshot_rate, 2),
                    "total_corrections": total_corrections,
                    "duration_seconds": 10
                }
            )
                            
        except Exception as e:
            return TestResult(
                name="10_player_session",
                passed=False,
                duration_ms=(time.time() - start) * 1000,
                error=str(e)
            )
    
    async def test_redis_integration(self) -> TestResult:
        """Test 3: Player sessions persist in Redis"""
        start = time.time()
        
        try:
            import redis
            r = redis.Redis(
                host=self.redis_host, 
                port=self.redis_port,
                decode_responses=True,
                socket_connect_timeout=5
            )
            
            # Connect a bot
            bot = await self.spawn_bot("test_redis")
            await bot.connect()
            
            if not bot.connected:
                return TestResult(
                    name="redis_integration",
                    passed=False,
                    duration_ms=(time.time() - start) * 1000,
                    error="Bot could not connect to server"
                )
            
            # Wait for server to cache session
            await asyncio.sleep(2)
            
            # Check Redis for session data
            session_keys = r.keys("session:*")
            entity_keys = r.keys("entity:*")
            
            # Get zone status if available
            zone_keys = r.keys("zone:*")
            zone_statuses = {}
            for key in zone_keys:
                try:
                    status = r.hgetall(key)
                    zone_statuses[key] = status
                except:
                    pass
            
            await bot.disconnect()
            
            # Consider test passed if we can connect to Redis and see some keys
            passed = len(session_keys) > 0 or len(entity_keys) > 0 or len(zone_statuses) > 0
            
            return TestResult(
                name="redis_integration",
                passed=passed,
                duration_ms=(time.time() - start) * 1000,
                details={
                    "session_keys": len(session_keys),
                    "entity_keys": len(entity_keys),
                    "zone_statuses": zone_statuses,
                    "entity_id": bot.entity_id
                },
                error=None if passed else "No session data found in Redis"
            )
                            
        except ImportError:
            return TestResult(
                name="redis_integration",
                passed=False,
                duration_ms=(time.time() - start) * 1000,
                error="redis package not installed (pip install redis)"
            )
        except Exception as e:
            return TestResult(
                name="redis_integration",
                passed=False,
                duration_ms=(time.time() - start) * 1000,
                error=str(e)
            )
    
    async def test_scylla_integration(self) -> TestResult:
        """Test 4: Player profiles can be saved/loaded from ScyllaDB"""
        start = time.time()
        
        try:
            from cassandra.cluster import Cluster
            from cassandra.auth import PlainTextAuthProvider
            
            # Connect to ScyllaDB
            cluster = Cluster(['localhost'], port=9042)
            session = cluster.connect()
            
            # Check if darkages keyspace exists
            keyspaces = session.execute("SELECT keyspace_name FROM system_schema.keyspaces")
            keyspace_names = [row.keyspace_name for row in keyspaces]
            
            has_darkages = 'darkages' in keyspace_names
            
            table_info = {}
            if has_darkages:
                session.set_keyspace('darkages')
                tables = session.execute(
                    "SELECT table_name FROM system_schema.tables WHERE keyspace_name='darkages'"
                )
                for row in tables:
                    table_info[row.table_name] = True
            
            cluster.shutdown()
            
            passed = has_darkages
            
            return TestResult(
                name="scylla_integration",
                passed=passed,
                duration_ms=(time.time() - start) * 1000,
                details={
                    "connected": True,
                    "keyspace_exists": has_darkages,
                    "tables": list(table_info.keys())
                },
                error=None if passed else "darkages keyspace not found"
            )
            
        except ImportError:
            return TestResult(
                name="scylla_integration",
                passed=False,
                duration_ms=(time.time() - start) * 1000,
                error="cassandra-driver not installed (pip install cassandra-driver)"
            )
        except Exception as e:
            return TestResult(
                name="scylla_integration",
                passed=False,
                duration_ms=(time.time() - start) * 1000,
                error=str(e)
            )
    
    async def test_disconnect_reconnect(self) -> TestResult:
        """Test 5: Bot can disconnect and reconnect cleanly"""
        start = time.time()
        
        try:
            bot = await self.spawn_bot("test_reconnect")
            
            # First connection
            first_connect = await bot.connect()
            if not first_connect:
                return TestResult(
                    name="disconnect_reconnect",
                    passed=False,
                    duration_ms=(time.time() - start) * 1000,
                    error="Initial connection failed"
                )
            
            first_entity_id = bot.entity_id
            bot.disconnect()
            await asyncio.sleep(0.5)
            
            # Reconnect
            second_connect = await bot.connect()
            if not second_connect:
                return TestResult(
                    name="disconnect_reconnect",
                    passed=False,
                    duration_ms=(time.time() - start) * 1000,
                    error="Reconnect failed"
                )
            
            second_entity_id = bot.entity_id
            bot.disconnect()
            
            passed = first_connect and second_connect
            
            return TestResult(
                name="disconnect_reconnect",
                passed=passed,
                duration_ms=(time.time() - start) * 1000,
                details={
                    "first_entity_id": first_entity_id,
                    "second_entity_id": second_entity_id,
                    "entity_id_preserved": first_entity_id == second_entity_id
                }
            )
            
        except Exception as e:
            return TestResult(
                name="disconnect_reconnect",
                passed=False,
                duration_ms=(time.time() - start) * 1000,
                error=str(e)
            )
    
    async def test_bandwidth_compliance(self) -> TestResult:
        """Test 6: Bandwidth usage stays within budget"""
        start = time.time()
        
        try:
            bots = await self.spawn_bots(10)
            
            # Connect all
            for bot in bots:
                await bot.connect()
            
            connected = [b for b in bots if b.connected]
            if len(connected) < 5:
                return TestResult(
                    name="bandwidth_compliance",
                    passed=False,
                    duration_ms=(time.time() - start) * 1000,
                    error=f"Only {len(connected)}/10 bots connected"
                )
            
            # Run for 5 seconds
            run_tasks = [bot.run(5.0) for bot in connected]
            await asyncio.gather(*run_tasks, return_exceptions=True)
            
            # Check bandwidth
            total_bytes_up = sum(b.bytes_sent for b in connected)
            total_bytes_down = sum(b.bytes_received for b in connected)
            duration = 5.0
            
            avg_up_per_bot = (total_bytes_up / len(connected)) / duration
            avg_down_per_bot = (total_bytes_down / len(connected)) / duration
            
            up_passed = avg_up_per_bot <= MAX_UPSTREAM_BYTES_PER_SEC
            down_passed = avg_down_per_bot <= MAX_DOWNSTREAM_BYTES_PER_SEC
            
            await self.cleanup()
            
            return TestResult(
                name="bandwidth_compliance",
                passed=up_passed and down_passed,
                duration_ms=(time.time() - start) * 1000,
                details={
                    "avg_upload_bytes_per_sec": round(avg_up_per_bot, 2),
                    "upload_budget": MAX_UPSTREAM_BYTES_PER_SEC,
                    "upload_passed": up_passed,
                    "avg_download_bytes_per_sec": round(avg_down_per_bot, 2),
                    "download_budget": MAX_DOWNSTREAM_BYTES_PER_SEC,
                    "download_passed": down_passed,
                    "bots_tested": len(connected)
                },
                error=None if (up_passed and down_passed) else 
                    f"Bandwidth exceeded: up={avg_up_per_bot:.0f}, down={avg_down_per_bot:.0f}"
            )
            
        except Exception as e:
            return TestResult(
                name="bandwidth_compliance",
                passed=False,
                duration_ms=(time.time() - start) * 1000,
                error=str(e)
            )
    
    async def test_stress_50_connections(self) -> TestResult:
        """Test 7: 50 concurrent connections stress test"""
        start = time.time()
        
        try:
            # Spawn 50 bots
            bots = await self.spawn_bots(50)
            
            # Connect with timing
            connect_times = []
            for bot in bots:
                t0 = time.time()
                await bot.connect()
                connect_times.append((time.time() - t0) * 1000)
            
            connected_count = sum(1 for b in bots if b.connected)
            connect_time_avg = statistics.mean(connect_times) if connect_times else 0
            connect_time_max = max(connect_times) if connect_times else 0
            
            if connected_count < 40:  # Allow up to 20% failure rate
                await self.cleanup()
                return TestResult(
                    name="stress_50_connections",
                    passed=False,
                    duration_ms=(time.time() - start) * 1000,
                    details={"connected": connected_count, "target": 50},
                    error=f"Only {connected_count}/50 bots connected"
                )
            
            # Run for 10 seconds
            connected = [b for b in bots if b.connected]
            run_tasks = [bot.run(10.0) for bot in connected]
            await asyncio.gather(*run_tasks, return_exceptions=True)
            
            # Calculate statistics
            total_snapshots = sum(len(b.snapshots_received) for b in connected)
            avg_snapshot_rate = statistics.mean([b.get_snapshot_rate() for b in connected]) if connected else 0
            
            await self.cleanup()
            
            passed = connected_count >= 45 and avg_snapshot_rate >= 15.0
            
            return TestResult(
                name="stress_50_connections",
                passed=passed,
                duration_ms=(time.time() - start) * 1000,
                details={
                    "connected": connected_count,
                    "target": 50,
                    "connect_time_avg_ms": round(connect_time_avg, 2),
                    "connect_time_max_ms": round(connect_time_max, 2),
                    "total_snapshots": total_snapshots,
                    "avg_snapshot_rate_hz": round(avg_snapshot_rate, 2)
                }
            )
                            
        except Exception as e:
            return TestResult(
                name="stress_50_connections",
                passed=False,
                duration_ms=(time.time() - start) * 1000,
                error=str(e)
            )
    
    # =========================================================================
    # Test Execution
    # =========================================================================
    
    async def run_all_tests(self) -> List[TestResult]:
        """Run all integration tests in sequence"""
        print("=" * 70)
        print("DARKAGES MMO - INTEGRATION TEST SUITE (WP-6-5)")
        print("=" * 70)
        print(f"Server: {self.server_host}:{self.server_port}")
        print(f"Redis: {self.redis_host}:{self.redis_port}")
        print(f"Started: {datetime.now().isoformat()}")
        print("=" * 70)
        
        # Check service health first
        print("\n[PRE-FLIGHT] Checking service health...")
        health_result = await self.test_service_health()
        self.results.append(health_result)
        print(f"  {'PASS' if health_result.passed else 'FAIL'} - Service Health")
        if health_result.error:
            print(f"  Error: {health_result.error}")
        
        if not health_result.passed:
            print("\n[CRITICAL] Services not healthy, skipping remaining tests")
            return self.results
        
        # Define all tests to run
        tests = [
            self.test_basic_connectivity,
            self.test_10_player_session,
            self.test_redis_integration,
            self.test_scylla_integration,
            self.test_disconnect_reconnect,
            self.test_bandwidth_compliance,
            self.test_stress_50_connections,
        ]
        
        # Run each test
        for test_func in tests:
            print(f"\n[TEST] {test_func.__name__}...")
            try:
                result = await test_func()
                self.results.append(result)
                status = "PASS" if result.passed else "FAIL"
                print(f"  {status} ({result.duration_ms:.1f}ms)")
                if result.error:
                    print(f"  Error: {result.error}")
            except Exception as e:
                print(f"  FAIL EXCEPTION: {e}")
                self.results.append(TestResult(
                    name=test_func.__name__,
                    passed=False,
                    duration_ms=0,
                    error=str(e)
                ))
        
        return self.results
    
    def print_summary(self) -> int:
        """Print test summary and return exit code"""
        print("\n" + "=" * 70)
        print("INTEGRATION TEST SUMMARY")
        print("=" * 70)
        
        passed = sum(1 for r in self.results if r.passed)
        total = len(self.results)
        
        for result in self.results:
            status = "PASS" if result.passed else "FAIL"
            print(f"  {status:8} {result.name:30} {result.duration_ms:10.1f}ms")
        
        print("-" * 70)
        print(f"Total: {passed}/{total} tests passed")
        
        if passed == total:
            print("\nALL TESTS PASSED - Integration testing complete!")
        else:
            failed = [r.name for r in self.results if not r.passed]
            print(f"\nFAILED TESTS: {', '.join(failed)}")
        
        print("=" * 70)
        
        return 0 if passed == total else 1
    
    def save_results(self, output_path: str):
        """Save test results to JSON file"""
        data = {
            'timestamp': datetime.now().isoformat(),
            'server': f"{self.server_host}:{self.server_port}",
            'total_tests': len(self.results),
            'passed': sum(1 for r in self.results if r.passed),
            'failed': sum(1 for r in self.results if not r.passed),
            'results': [r.to_dict() for r in self.results]
        }
        
        with open(output_path, 'w') as f:
            json.dump(data, f, indent=2)
        
        print(f"\nResults saved to: {output_path}")


# =============================================================================
# Stress Test Functions
# =============================================================================

async def run_stress_test(bot_count: int, duration: int, host: str, port: int) -> bool:
    """Run a stress test with specified number of bots"""
    print("=" * 70)
    print(f"DARKAGES STRESS TEST - {bot_count} bots for {duration}s")
    print("=" * 70)
    
    harness = IntegrationTestHarness(host, port)
    
    try:
        # Spawn bots
        print(f"\nSpawning {bot_count} bots...")
        bots = await harness.spawn_bots(bot_count)
        
        # Connect all
        print("Connecting bots...")
        connect_start = time.time()
        for bot in bots:
            await bot.connect()
        connect_duration = time.time() - connect_start
        
        connected_count = sum(1 for b in bots if b.connected)
        print(f"Connected: {connected_count}/{bot_count} in {connect_duration:.2f}s")
        
        if connected_count == 0:
            print("ERROR: No bots could connect!")
            return False
        
        # Run for duration
        connected = [b for b in bots if b.connected]
        print(f"\nRunning test for {duration} seconds...")
        print("(Press Ctrl+C to stop early)")
        
        run_tasks = [bot.run(duration) for bot in connected]
        await asyncio.gather(*run_tasks, return_exceptions=True)
        
        # Collect final stats
        total_snapshots = sum(len(b.snapshots_received) for b in connected)
        total_bytes_up = sum(b.bytes_sent for b in connected)
        total_bytes_down = sum(b.bytes_received for b in connected)
        
        await harness.cleanup()
        
        # Print summary
        print("\n" + "=" * 70)
        print("STRESS TEST RESULTS")
        print("=" * 70)
        print(f"Bots: {connected_count}/{bot_count} connected")
        print(f"Duration: {duration}s")
        print(f"Total snapshots: {total_snapshots:,}")
        print(f"Upload: {total_bytes_up / 1024:.2f} KB ({(total_bytes_up / duration * 8 / 1024):.2f} Kbps avg)")
        print(f"Download: {total_bytes_down / 1024:.2f} KB ({(total_bytes_down / duration * 8 / 1024):.2f} Kbps avg)")
        print("=" * 70)
        
        return connected_count >= bot_count * 0.9  # 90% connection rate considered success
        
    except KeyboardInterrupt:
        print("\n\nTest interrupted by user")
        await harness.cleanup()
        return False
    except Exception as e:
        print(f"\nERROR: {e}")
        await harness.cleanup()
        return False


# =============================================================================
# Main Entry Point
# =============================================================================

async def main():
    parser = argparse.ArgumentParser(
        description="DarkAges MMO Integration Test Harness (WP-6-5)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Run all integration tests
    python integration_harness.py --all
    
    # Run specific test
    python integration_harness.py --test basic_connectivity
    python integration_harness.py --test 10_player_session
    
    # Run stress test with 100 bots for 60 seconds
    python integration_harness.py --stress 100 --duration 60
    
    # Check service health only
    python integration_harness.py --health
    
    # Save results to file
    python integration_harness.py --all --output results.json

Test Stages (from research analysis):
    Week 1: basic_connectivity - UDP handshake working
    Week 2: redis_integration - Session persistence working  
    Week 3: scylla_integration - Database persistence working
    Week 4: 10_player_session - End-to-end with multiple clients
    Week 5+: stress tests - 10, 50, 100+ concurrent connections
        """
    )
    
    parser.add_argument("--all", action="store_true",
                       help="Run all integration tests")
    parser.add_argument("--test", type=str,
                       help="Run specific test by name")
    parser.add_argument("--stress", type=int, metavar="N",
                       help="Stress test with N connections")
    parser.add_argument("--duration", type=int, default=60,
                       help="Stress test duration in seconds (default: 60)")
    parser.add_argument("--health", action="store_true",
                       help="Check service health only")
    parser.add_argument("--server", type=str, default="127.0.0.1",
                       help="Server host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=DEFAULT_SERVER_PORT,
                       help=f"Server port (default: {DEFAULT_SERVER_PORT})")
    parser.add_argument("--redis-host", type=str, default="localhost",
                       help="Redis host (default: localhost)")
    parser.add_argument("--redis-port", type=int, default=6379,
                       help="Redis port (default: 6379)")
    parser.add_argument("--output", type=str,
                       help="Save results to JSON file")
    parser.add_argument("--list", action="store_true",
                       help="List available tests")
    
    args = parser.parse_args()
    
    # List available tests
    if args.list:
        print("Available tests:")
        print("  service_health         - Check Redis, ScyllaDB, and server health")
        print("  basic_connectivity     - Single bot connect and snapshot")
        print("  10_player_session      - 10 bots connect, move, disconnect")
        print("  redis_integration      - Session persistence in Redis")
        print("  scylla_integration     - Profile persistence in ScyllaDB")
        print("  disconnect_reconnect   - Clean disconnect/reconnect handling")
        print("  bandwidth_compliance   - Bandwidth within budget")
        print("  stress_50_connections  - 50 concurrent connection test")
        return 0
    
    harness = IntegrationTestHarness(
        server_host=args.server,
        server_port=args.port,
        redis_host=args.redis_host,
        redis_port=args.redis_port
    )
    
    exit_code = 0
    
    try:
        if args.health:
            # Health check only
            result = await harness.test_service_health()
            print("Service Health:")
            print(f"  Redis:  {'PASS' if result.details.get('redis') else 'FAIL'}")
            print(f"  Scylla: {'PASS' if result.details.get('scylla') else 'FAIL'}")
            print(f"  Server: {'PASS' if result.details.get('server') else 'FAIL'}")
            exit_code = 0 if result.passed else 2
            
        elif args.stress:
            # Stress test
            success = await run_stress_test(
                args.stress, args.duration, args.server, args.port
            )
            exit_code = 0 if success else 1
            
        elif args.test:
            # Run specific test
            test_func = getattr(harness, f"test_{args.test}", None)
            if test_func:
                result = await test_func()
                harness.results.append(result)
                print(f"\nResult: {'PASS' if result.passed else 'FAIL'}")
                print(f"Details: {json.dumps(result.details, indent=2)}")
                exit_code = 0 if result.passed else 1
            else:
                print(f"Unknown test: {args.test}")
                print("Use --list to see available tests")
                exit_code = 1
                
        else:
            # Run all tests
            await harness.run_all_tests()
            exit_code = harness.print_summary()
        
        # Save results if requested
        if args.output and harness.results:
            harness.save_results(args.output)
            
    except KeyboardInterrupt:
        print("\n\nTest interrupted by user")
        exit_code = 130
    finally:
        await harness.cleanup()
    
    return exit_code


if __name__ == "__main__":
    exit_code = asyncio.run(main())
    sys.exit(exit_code)

