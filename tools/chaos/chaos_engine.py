#!/usr/bin/env python3
"""
DarkAges MMO - Chaos Engineering Framework
Simulates failures to test system resilience.

Usage:
    python chaos_engine.py --scenario zone_failure --duration 300
    python chaos_engine.py --scenario network_partition --targets zone-1,zone-2
    python chaos_engine.py --scenario load_spike --intensity 10x

Environment Variables:
    CHAOS_REDIS_HOST    - Redis host (default: localhost)
    CHAOS_REDIS_PORT    - Redis port (default: 6379)
    CHAOS_NETWORK_NAME  - Docker network name (default: darkages-network)
    CHAOS_ZONE_PREFIX   - Zone container prefix (default: darkages-zone-)

Safety:
    This tool should ONLY be run in test/staging environments.
    NEVER run against production systems.
"""

import argparse
import asyncio
import random
import subprocess
import time
import sys
import json
import os
import warnings
from dataclasses import dataclass, asdict
from datetime import datetime
from typing import List, Dict, Optional, Callable, Tuple, Any
from pathlib import Path

# Suppress docker SSL warnings
warnings.filterwarnings('ignore', message='unclosed.*ssl')

try:
    import redis
except ImportError:
    print("ERROR: redis package not found. Run: pip install -r requirements.txt")
    sys.exit(1)

try:
    import docker
    from docker.errors import NotFound, APIError
except ImportError:
    print("ERROR: docker package not found. Run: pip install -r requirements.txt")
    sys.exit(1)


@dataclass
class ChaosEvent:
    """Records a single chaos action and its result"""
    timestamp: float
    scenario: str
    target: str
    action: str
    result: str
    duration_ms: float
    metadata: Optional[Dict[str, Any]] = None


@dataclass
class SystemMetrics:
    """System state at a point in time"""
    timestamp: float
    total_players: int
    online_zones: int
    avg_tick_time_ms: float
    packet_loss_pct: float
    migration_failures: int
    active_connections: int = 0
    total_migrations: int = 0


class ChaosTestResult:
    """Results from a single chaos test scenario"""
    
    def __init__(self, scenario_name: str):
        self.scenario_name = scenario_name
        self.start_time = time.time()
        self.end_time: Optional[float] = None
        self.events: List[ChaosEvent] = []
        self.metrics: List[SystemMetrics] = []
        self.success = False
        self.error_message: Optional[str] = None
        self.recovery_time_seconds: Optional[float] = None
    
    def add_event(self, event: ChaosEvent):
        """Record a chaos event"""
        self.events.append(event)
    
    def add_metrics(self, metrics: SystemMetrics):
        """Record system metrics snapshot"""
        self.metrics.append(metrics)
    
    def finish(self, success: bool, error: Optional[str] = None, 
               recovery_time: Optional[float] = None):
        """Mark test as complete"""
        self.end_time = time.time()
        self.success = success
        self.error_message = error
        self.recovery_time_seconds = recovery_time
    
    def to_dict(self) -> Dict:
        """Convert to dictionary for serialization"""
        return {
            'scenario_name': self.scenario_name,
            'start_time': self.start_time,
            'end_time': self.end_time,
            'duration_seconds': (self.end_time or time.time()) - self.start_time,
            'success': self.success,
            'error_message': self.error_message,
            'recovery_time_seconds': self.recovery_time_seconds,
            'event_count': len(self.events),
            'metrics_count': len(self.metrics),
            'events': [asdict(e) for e in self.events],
            'metrics': [asdict(m) for m in self.metrics],
        }
    
    def generate_report(self) -> str:
        """Generate human-readable report"""
        lines = [
            "=" * 70,
            f"CHAOS TEST REPORT: {self.scenario_name}",
            "=" * 70,
            f"Duration: {(self.end_time or time.time()) - self.start_time:.1f}s",
            f"Result: {'PASS ✓' if self.success else 'FAIL ✗'}",
        ]
        
        if self.recovery_time_seconds:
            lines.append(f"Recovery Time: {self.recovery_time_seconds:.1f}s")
        
        if self.error_message:
            lines.extend(["", f"ERROR: {self.error_message}"])
        
        lines.extend([
            "",
            "EVENTS:",
            "-" * 70,
        ])
        
        for event in self.events:
            ts = event.timestamp - self.start_time
            meta = f" | {event.metadata}" if event.metadata else ""
            lines.append(f"  {ts:7.2f}s | {event.scenario:15s} | "
                        f"{event.target:12s} | {event.action:20s} | "
                        f"{event.result:10s} | {event.duration_ms:7.1f}ms{meta}")
        
        lines.extend([
            "",
            "METRICS SNAPSHOTS:",
            "-" * 70,
        ])
        
        if len(self.metrics) >= 2:
            first = self.metrics[0]
            last = self.metrics[-1]
            lines.extend([
                f"  Players:        {first.total_players:4d} → {last.total_players:4d} "
                f"(Δ{last.total_players - first.total_players:+d})",
                f"  Online Zones:   {first.online_zones:4d} → {last.online_zones:4d} "
                f"(Δ{last.online_zones - first.online_zones:+d})",
                f"  Avg Tick:       {first.avg_tick_time_ms:6.2f}ms → {last.avg_tick_time_ms:6.2f}ms",
                f"  Packet Loss:    {first.packet_loss_pct:5.2f}% → {last.packet_loss_pct:5.2f}%",
                f"  Migrations:     {first.total_migrations:4d} → {last.total_migrations:4d} "
                f"(failures: {last.migration_failures})",
            ])
        elif self.metrics:
            m = self.metrics[0]
            lines.extend([
                f"  Players: {m.total_players}, Zones: {m.online_zones}, "
                f"Tick: {m.avg_tick_time_ms:.2f}ms"
            ])
        else:
            lines.append("  No metrics collected")
        
        lines.append("=" * 70)
        return "\n".join(lines)


class ChaosEngine:
    """Main chaos testing engine"""
    
    # Zone configuration matching docker-compose.multi-zone.yml
    ZONES = [1, 2, 3, 4]
    ZONE_PORTS = {1: 7777, 2: 7779, 3: 7781, 4: 7783}
    
    def __init__(self, redis_host: str = None, redis_port: int = None,
                 network_name: str = None, zone_prefix: str = None):
        """Initialize chaos engine with configuration"""
        self.redis_host = redis_host or os.environ.get('CHAOS_REDIS_HOST', 'localhost')
        self.redis_port = redis_port or int(os.environ.get('CHAOS_REDIS_PORT', 6379))
        self.network_name = network_name or os.environ.get('CHAOS_NETWORK_NAME', 'darkages-network')
        self.zone_prefix = zone_prefix or os.environ.get('CHAOS_ZONE_PREFIX', 'darkages-zone-')
        
        self.redis_client: Optional[redis.Redis] = None
        self.docker_client: Optional[docker.DockerClient] = None
        self.results: List[ChaosTestResult] = []
        self.running = False
        self.metrics_interval = 2.0
        
        # Initialize connections
        self._init_connections()
    
    def _init_connections(self):
        """Initialize Redis and Docker connections"""
        try:
            self.redis_client = redis.Redis(
                host=self.redis_host, 
                port=self.redis_port,
                decode_responses=True,
                socket_connect_timeout=5,
                socket_timeout=5
            )
            self.redis_client.ping()
            print(f"[CHAOS] Connected to Redis at {self.redis_host}:{self.redis_port}")
        except Exception as e:
            print(f"[CHAOS] WARNING: Redis connection failed: {e}")
            print(f"[CHAOS] Continuing without Redis metrics...")
            self.redis_client = None
        
        try:
            self.docker_client = docker.from_env()
            self.docker_client.ping()
            print(f"[CHAOS] Connected to Docker daemon")
        except Exception as e:
            print(f"[CHAOS] WARNING: Docker connection failed: {e}")
            self.docker_client = None
    
    async def collect_metrics(self, result: ChaosTestResult, interval: float = 2.0):
        """Collect system metrics in background"""
        while self.running:
            try:
                metrics = await self._query_metrics()
                result.add_metrics(metrics)
            except Exception as e:
                print(f"[CHAOS] Metrics collection error: {e}")
            
            await asyncio.sleep(interval)
    
    async def _query_metrics(self) -> SystemMetrics:
        """Query current system state from Redis"""
        timestamp = time.time()
        
        if not self.redis_client:
            return SystemMetrics(
                timestamp=timestamp,
                total_players=0,
                online_zones=0,
                avg_tick_time_ms=0.0,
                packet_loss_pct=0.0,
                migration_failures=0
            )
        
        zones_online = 0
        total_players = 0
        total_migrations = 0
        migration_failures = 0
        
        for zone_id in self.ZONES:
            try:
                status = self.redis_client.hgetall(f"zone:{zone_id}:status")
                if status and status.get('state') == 'ONLINE':
                    zones_online += 1
                    total_players += int(status.get('players', 0))
                    total_migrations += int(status.get('migrations_completed', 0))
                    migration_failures += int(status.get('migration_failures', 0))
            except Exception:
                pass
        
        # Get global metrics
        try:
            tick_time = float(self.redis_client.get('metrics:avg_tick_time_ms') or 0.0)
            packet_loss = float(self.redis_client.get('metrics:packet_loss_pct') or 0.0)
        except Exception:
            tick_time = 0.0
            packet_loss = 0.0
        
        return SystemMetrics(
            timestamp=timestamp,
            total_players=total_players,
            online_zones=zones_online,
            avg_tick_time_ms=tick_time,
            packet_loss_pct=packet_loss,
            migration_failures=migration_failures,
            total_migrations=total_migrations
        )
    
    async def wait_for_recovery(self, timeout: float = 120.0, 
                                stable_duration: float = 10.0,
                                expected_zones: int = 4) -> Tuple[bool, Optional[float]]:
        """
        Wait for system to recover to stable state.
        
        Returns:
            Tuple of (success, recovery_time_seconds)
        """
        print(f"[CHAOS] Waiting for system recovery (timeout: {timeout}s, stable: {stable_duration}s)...")
        
        start = time.time()
        stable_start = None
        recovery_time = None
        
        while time.time() - start < timeout:
            try:
                # Check if all zones are online
                zones_online = 0
                for zone_id in self.ZONES:
                    if self.redis_client:
                        status = self.redis_client.hgetall(f"zone:{zone_id}:status")
                        if status and status.get('state') == 'ONLINE':
                            zones_online += 1
                    elif self.docker_client:
                        # Fallback: check container status via Docker
                        try:
                            container = self.docker_client.containers.get(f"{self.zone_prefix}{zone_id}")
                            if container.status == 'running':
                                zones_online += 1
                        except NotFound:
                            pass
                
                if zones_online >= expected_zones:
                    if stable_start is None:
                        stable_start = time.time()
                        recovery_time = stable_start - start
                        print(f"[CHAOS] System stabilized at {recovery_time:.1f}s, waiting {stable_duration}s...")
                    elif time.time() - stable_start >= stable_duration:
                        total_time = time.time() - start
                        print(f"[CHAOS] System recovered in {total_time:.1f}s (stable for {stable_duration}s)")
                        return True, recovery_time
                else:
                    if stable_start is not None:
                        print(f"[CHAOS] Stability lost: only {zones_online}/{expected_zones} zones online")
                    stable_start = None
                
            except Exception as e:
                print(f"[CHAOS] Recovery check error: {e}")
            
            await asyncio.sleep(1.0)
        
        print(f"[CHAOS] Recovery timeout after {timeout}s")
        return False, None
    
    def _get_zone_container(self, zone_id: int) -> Optional[Any]:
        """Get Docker container for a zone"""
        if not self.docker_client:
            return None
        
        zone_name = f"{self.zone_prefix}{zone_id}"
        try:
            return self.docker_client.containers.get(zone_name)
        except NotFound:
            return None
    
    # ========================================================================
    # CHAOS SCENARIOS
    # ========================================================================
    
    async def scenario_zone_failure(self, duration: float, 
                                    target_zone: Optional[int] = None) -> ChaosTestResult:
        """
        Kill a zone server and verify recovery.
        
        Tests:
        - Zone failure detection
        - Player migration to adjacent zones
        - Zone restart capability
        - Recovery time within SLA
        """
        result = ChaosTestResult("zone_failure")
        self.running = True
        
        # Select target zone
        if target_zone is None:
            target_zone = random.choice(self.ZONES)
        
        if target_zone not in self.ZONES:
            result.finish(False, f"Invalid zone ID: {target_zone}")
            return result
        
        if not self.docker_client:
            result.finish(False, "Docker client not available")
            return result
        
        # Start metrics collection
        metrics_task = asyncio.create_task(self.collect_metrics(result))
        
        try:
            print(f"[CHAOS] Starting zone failure scenario (target: zone-{target_zone})")
            
            container = self._get_zone_container(target_zone)
            if not container:
                result.finish(False, f"Zone {target_zone} container not found")
                return result
            
            if container.status != 'running':
                result.finish(False, f"Zone {target_zone} not running (status: {container.status})")
                return result
            
            # Record pre-failure state
            result.add_event(ChaosEvent(
                timestamp=time.time(),
                scenario="zone_failure",
                target=f"zone-{target_zone}",
                action="verify_pre_state",
                result="success",
                duration_ms=0,
                metadata={'container_status': container.status}
            ))
            
            # Get initial player count
            initial_players = 0
            if self.redis_client:
                try:
                    status = self.redis_client.hgetall(f"zone:{target_zone}:status")
                    initial_players = int(status.get('players', 0)) if status else 0
                except Exception:
                    pass
            
            # Kill the zone
            print(f"[CHAOS] Killing zone-{target_zone} container...")
            kill_start = time.time()
            container.stop(timeout=5)
            kill_duration = (time.time() - kill_start) * 1000
            
            result.add_event(ChaosEvent(
                timestamp=time.time(),
                scenario="zone_failure",
                target=f"zone-{target_zone}",
                action="kill_container",
                result="success",
                duration_ms=kill_duration,
                metadata={'initial_players': initial_players}
            ))
            
            print(f"[CHAOS] Zone {target_zone} killed, waiting {duration}s of chaos...")
            await asyncio.sleep(duration)
            
            # Restart the zone
            print(f"[CHAOS] Restarting zone-{target_zone}...")
            restart_start = time.time()
            container.start()
            restart_duration = (time.time() - restart_start) * 1000
            
            result.add_event(ChaosEvent(
                timestamp=time.time(),
                scenario="zone_failure",
                target=f"zone-{target_zone}",
                action="restart_container",
                result="success",
                duration_ms=restart_duration
            ))
            
            # Wait for recovery
            recovered, recovery_time = await self.wait_for_recovery(
                timeout=120.0, 
                expected_zones=4
            )
            
            result.finish(recovered, recovery_time=recovery_time)
            
        except Exception as e:
            result.finish(False, str(e))
        finally:
            self.running = False
            metrics_task.cancel()
            try:
                await metrics_task
            except asyncio.CancelledError:
                pass
        
        return result
    
    async def scenario_network_partition(self, duration: float,
                                         targets: Optional[List[int]] = None) -> ChaosTestResult:
        """
        Partition zones from each other by disconnecting them from the network.
        
        Tests:
        - Aura buffer sync failure handling
        - Cross-zone messaging resilience
        - Eventual consistency recovery
        """
        result = ChaosTestResult("network_partition")
        self.running = True
        
        if targets is None:
            targets = [1, 2]  # Default: partition zones 1 and 2
        
        if not self.docker_client:
            result.finish(False, "Docker client not available")
            return result
        
        # Validate targets
        for zone_id in targets:
            if zone_id not in self.ZONES:
                result.finish(False, f"Invalid zone ID: {zone_id}")
                return result
        
        # Start metrics collection
        metrics_task = asyncio.create_task(self.collect_metrics(result))
        
        try:
            print(f"[CHAOS] Starting network partition scenario")
            print(f"[CHAOS] Targets: {targets}")
            
            # Get network object
            try:
                network = self.docker_client.networks.get(self.network_name)
            except NotFound:
                result.finish(False, f"Network '{self.network_name}' not found")
                return result
            
            disconnected_containers = []
            
            # Disconnect target zones from network
            for zone_id in targets:
                container = self._get_zone_container(zone_id)
                if not container:
                    result.finish(False, f"Zone {zone_id} container not found")
                    return result
                
                # Disconnect from network
                disconnect_start = time.time()
                try:
                    network.disconnect(container, force=True)
                    duration_ms = (time.time() - disconnect_start) * 1000
                    disconnected_containers.append((zone_id, container))
                    
                    result.add_event(ChaosEvent(
                        timestamp=time.time(),
                        scenario="network_partition",
                        target=f"zone-{zone_id}",
                        action="disconnect_network",
                        result="success",
                        duration_ms=duration_ms
                    ))
                    print(f"[CHAOS] Disconnected zone-{zone_id} from network")
                except Exception as e:
                    result.add_event(ChaosEvent(
                        timestamp=time.time(),
                        scenario="network_partition",
                        target=f"zone-{zone_id}",
                        action="disconnect_network",
                        result=f"failed: {e}",
                        duration_ms=0
                    ))
            
            print(f"[CHAOS] Network partitioned, waiting {duration}s...")
            await asyncio.sleep(duration)
            
            # Reconnect zones
            print(f"[CHAOS] Restoring network connectivity...")
            for zone_id, container in disconnected_containers:
                try:
                    network.connect(container)
                    result.add_event(ChaosEvent(
                        timestamp=time.time(),
                        scenario="network_partition",
                        target=f"zone-{zone_id}",
                        action="reconnect_network",
                        result="success",
                        duration_ms=0
                    ))
                    print(f"[CHAOS] Reconnected zone-{zone_id}")
                except Exception as e:
                    result.add_event(ChaosEvent(
                        timestamp=time.time(),
                        scenario="network_partition",
                        target=f"zone-{zone_id}",
                        action="reconnect_network",
                        result=f"failed: {e}",
                        duration_ms=0
                    ))
            
            # Wait for recovery
            recovered, recovery_time = await self.wait_for_recovery(timeout=120.0)
            result.finish(recovered, recovery_time=recovery_time)
            
        except Exception as e:
            result.finish(False, str(e))
        finally:
            self.running = False
            metrics_task.cancel()
            try:
                await metrics_task
            except asyncio.CancelledError:
                pass
        
        return result
    
    async def scenario_load_spike(self, duration: float, 
                                  intensity: int = 5) -> ChaosTestResult:
        """
        Sudden increase in player load by spawning many bots rapidly.
        
        Tests:
        - Server overload handling
        - QoS degradation
        - No crashes under load
        - Recovery after spike ends
        """
        result = ChaosTestResult("load_spike")
        self.running = True
        
        # Import bot swarm from parent directory
        sys.path.insert(0, str(Path(__file__).parent.parent / 'stress-test'))
        try:
            from bot_swarm import GameBot, BotConfig
        except ImportError as e:
            result.finish(False, f"Failed to import bot_swarm: {e}")
            return result
        
        # Start metrics collection
        metrics_task = asyncio.create_task(self.collect_metrics(result))
        
        bots: List[GameBot] = []
        connected_count = 0
        
        try:
            print(f"[CHAOS] Starting load spike scenario (intensity: {intensity}x)")
            
            # Calculate bot count based on intensity
            base_count = 50
            bot_count = base_count * intensity
            
            spawn_start = time.time()
            
            # Create bots for random zones
            for i in range(bot_count):
                zone_port = random.choice(list(self.ZONE_PORTS.values()))
                config = BotConfig(
                    host=self.redis_host if self.redis_host != 'localhost' else '127.0.0.1',
                    port=zone_port, 
                    bot_id=10000 + i
                )
                bot = GameBot(config)
                bots.append(bot)
            
            print(f"[CHAOS] Spawning {bot_count} bots...")
            
            # Connect bots in batches to avoid overwhelming
            batch_size = 20
            for batch_start in range(0, len(bots), batch_size):
                batch = bots[batch_start:batch_start + batch_size]
                connect_tasks = [bot.connect() for bot in batch]
                batch_results = await asyncio.gather(*connect_tasks, return_exceptions=True)
                
                for bot, res in zip(batch, batch_results):
                    if res is True:
                        connected_count += 1
                
                # Brief pause between batches
                await asyncio.sleep(0.5)
            
            spawn_duration = (time.time() - spawn_start) * 1000
            
            result.add_event(ChaosEvent(
                timestamp=time.time(),
                scenario="load_spike",
                target="all_zones",
                action=f"spawn_{bot_count}_bots",
                result=f"connected_{connected_count}",
                duration_ms=spawn_duration,
                metadata={'target_count': bot_count, 'connected': connected_count}
            ))
            
            print(f"[CHAOS] Connected {connected_count}/{bot_count} bots, holding {duration}s...")
            
            # Keep bots connected for the duration
            # Start all bot run loops concurrently
            run_tasks = [bot.run(duration) for bot in bots if bot.connected]
            if run_tasks:
                await asyncio.gather(*run_tasks, return_exceptions=True)
            else:
                await asyncio.sleep(duration)
            
            # Disconnect all bots
            disconnect_start = time.time()
            for bot in bots:
                bot.disconnect()
            disconnect_duration = (time.time() - disconnect_start) * 1000
            
            result.add_event(ChaosEvent(
                timestamp=time.time(),
                scenario="load_spike",
                target="all_zones",
                action="disconnect_all_bots",
                result="success",
                duration_ms=disconnect_duration,
                metadata={'disconnected': connected_count}
            ))
            
            print(f"[CHAOS] Bots disconnected, waiting for recovery...")
            
            # Wait for recovery
            recovered, recovery_time = await self.wait_for_recovery(timeout=60.0)
            result.finish(recovered, recovery_time=recovery_time)
            
        except Exception as e:
            result.finish(False, str(e))
        finally:
            self.running = False
            metrics_task.cancel()
            try:
                await metrics_task
            except asyncio.CancelledError:
                pass
            
            # Ensure all bots are disconnected
            for bot in bots:
                bot.disconnect()
        
        return result
    
    async def scenario_cascade_failure(self, duration: float) -> ChaosTestResult:
        """
        Simulate cascade failure by killing zones sequentially.
        
        Tests:
        - System resilience to multiple failures
        - Remaining zone overload handling
        - Recovery from degraded state
        """
        result = ChaosTestResult("cascade_failure")
        self.running = True
        
        if not self.docker_client:
            result.finish(False, "Docker client not available")
            return result
        
        metrics_task = asyncio.create_task(self.collect_metrics(result))
        
        killed_zones = []
        
        try:
            print(f"[CHAOS] Starting cascade failure scenario")
            
            # Randomize zone kill order
            kill_order = self.ZONES.copy()
            random.shuffle(kill_order)
            
            interval = duration / len(kill_order)
            
            for zone_id in kill_order:
                container = self._get_zone_container(zone_id)
                if not container or container.status != 'running':
                    continue
                
                kill_start = time.time()
                container.stop(timeout=5)
                kill_duration = (time.time() - kill_start) * 1000
                killed_zones.append(zone_id)
                
                result.add_event(ChaosEvent(
                    timestamp=time.time(),
                    scenario="cascade_failure",
                    target=f"zone-{zone_id}",
                    action="kill_container",
                    result="success",
                    duration_ms=kill_duration,
                    metadata={'zones_killed': len(killed_zones)}
                ))
                
                print(f"[CHAOS] Killed zone-{zone_id} ({len(killed_zones)}/{len(kill_order)})")
                await asyncio.sleep(interval)
            
            print(f"[CHAOS] Cascade complete, restarting zones...")
            
            # Restart all zones
            for zone_id in killed_zones:
                container = self._get_zone_container(zone_id)
                if container:
                    restart_start = time.time()
                    container.start()
                    restart_duration = (time.time() - restart_start) * 1000
                    
                    result.add_event(ChaosEvent(
                        timestamp=time.time(),
                        scenario="cascade_failure",
                        target=f"zone-{zone_id}",
                        action="restart_container",
                        result="success",
                        duration_ms=restart_duration
                    ))
                    print(f"[CHAOS] Restarted zone-{zone_id}")
            
            # Wait for full recovery
            recovered, recovery_time = await self.wait_for_recovery(
                timeout=180.0,  # Longer timeout for cascade
                expected_zones=4
            )
            result.finish(recovered, recovery_time=recovery_time)
            
        except Exception as e:
            result.finish(False, str(e))
        finally:
            self.running = False
            metrics_task.cancel()
            try:
                await metrics_task
            except asyncio.CancelledError:
                pass
        
        return result
    
    async def run_all_scenarios(self):
        """Run all chaos scenarios sequentially"""
        scenarios = [
            ("zone_failure", lambda: self.scenario_zone_failure(30.0)),
            ("network_partition", lambda: self.scenario_network_partition(30.0, [1, 2])),
            ("load_spike", lambda: self.scenario_load_spike(60.0, intensity=3)),
            ("cascade_failure", lambda: self.scenario_cascade_failure(60.0)),
        ]
        
        print("=" * 70)
        print("DARKAGES CHAOS ENGINE - Running All Scenarios")
        print("=" * 70)
        print(f"Time: {datetime.now().isoformat()}")
        print(f"Redis: {self.redis_host}:{self.redis_port}")
        print(f"Network: {self.network_name}")
        print("=" * 70)
        
        for name, scenario_func in scenarios:
            print(f"\n{'='*70}")
            print(f"Running scenario: {name}")
            print('='*70)
            
            result = await scenario_func()
            self.results.append(result)
            
            print(result.generate_report())
            
            # Brief pause between scenarios
            await asyncio.sleep(5)
        
        # Generate summary
        self._print_summary()
        
        return all(r.success for r in self.results)
    
    def _print_summary(self):
        """Print final summary of all tests"""
        print(f"\n{'='*70}")
        print("CHAOS TEST FINAL SUMMARY")
        print('='*70)
        print(f"Completed: {datetime.now().isoformat()}")
        print()
        
        passed = sum(1 for r in self.results if r.success)
        total = len(self.results)
        
        print(f"Total: {passed}/{total} scenarios passed")
        print()
        
        for result in self.results:
            status = "✓ PASS" if result.success else "✗ FAIL"
            recovery = f" (recovery: {result.recovery_time_seconds:.1f}s)" if result.recovery_time_seconds else ""
            print(f"  {status:8s} {result.scenario_name:20s} {recovery}")
        
        print()
        print(f"Overall: {'ALL TESTS PASSED ✓' if passed == total else 'SOME TESTS FAILED ✗'}")
        print('='*70)


def main():
    parser = argparse.ArgumentParser(
        description='DarkAges Chaos Testing Framework',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Run all scenarios
    python chaos_engine.py --scenario all

    # Kill zone 1 for 60 seconds
    python chaos_engine.py --scenario zone_failure --target 1 --duration 60

    # Partition zones 1 and 2 from the network
    python chaos_engine.py --scenario network_partition --targets 1,2 --duration 30

    # Spike load with 5x intensity for 120 seconds
    python chaos_engine.py --scenario load_spike --intensity 5 --duration 120

    # Cascade failure (kills all zones sequentially)
    python chaos_engine.py --scenario cascade_failure --duration 120

Environment Variables:
    CHAOS_REDIS_HOST    - Redis host (default: localhost)
    CHAOS_REDIS_PORT    - Redis port (default: 6379)
    CHAOS_NETWORK_NAME  - Docker network name (default: darkages-network)

Safety Warning:
    This tool manipulates Docker containers and should ONLY be used
    in test/staging environments. NEVER run against production.
        """
    )
    parser.add_argument('--scenario', 
                       choices=['zone_failure', 'network_partition', 
                               'load_spike', 'cascade_failure', 'all'],
                       default='all', 
                       help='Chaos scenario to run (default: all)')
    parser.add_argument('--duration', type=float, default=30.0,
                       help='Duration of chaos in seconds (default: 30)')
    parser.add_argument('--target', type=int, 
                       help='Target zone ID for zone_failure scenario')
    parser.add_argument('--targets', 
                       help='Comma-separated zone IDs for network_partition')
    parser.add_argument('--intensity', type=int, default=3,
                       help='Load spike intensity multiplier (default: 3)')
    parser.add_argument('--redis-host', default=None,
                       help='Redis host (overrides env var)')
    parser.add_argument('--redis-port', type=int, default=None,
                       help='Redis port (overrides env var)')
    parser.add_argument('--output', 
                       help='Output JSON file for results')
    parser.add_argument('--yes-i-know-what-im-doing', action='store_true',
                       help='Acknowledge this is not production')
    
    args = parser.parse_args()
    
    # Safety check
    if not args.yes_i_know_what_im_doing:
        print("=" * 70)
        print("⚠️  CHAOS TESTING FRAMEWORK - SAFETY WARNING")
        print("=" * 70)
        print("""
This tool will:
  - Kill and restart Docker containers
  - Disconnect zones from the network
  - Spawn heavy bot load on your servers

ONLY run this in test/staging environments.
NEVER run against production systems.

To proceed, add the --yes-i-know-what-im-doing flag.
""")
        print("=" * 70)
        sys.exit(1)
    
    # Initialize engine
    engine = ChaosEngine(
        redis_host=args.redis_host,
        redis_port=args.redis_port
    )
    
    success = False
    
    # Run selected scenario
    if args.scenario == 'all':
        success = asyncio.run(engine.run_all_scenarios())
    elif args.scenario == 'zone_failure':
        result = asyncio.run(engine.scenario_zone_failure(args.duration, args.target))
        print(result.generate_report())
        success = result.success
        engine.results.append(result)
    elif args.scenario == 'network_partition':
        targets = [int(t) for t in args.targets.split(',')] if args.targets else [1, 2]
        result = asyncio.run(engine.scenario_network_partition(args.duration, targets))
        print(result.generate_report())
        success = result.success
        engine.results.append(result)
    elif args.scenario == 'load_spike':
        result = asyncio.run(engine.scenario_load_spike(args.duration, args.intensity))
        print(result.generate_report())
        success = result.success
        engine.results.append(result)
    elif args.scenario == 'cascade_failure':
        result = asyncio.run(engine.scenario_cascade_failure(args.duration))
        print(result.generate_report())
        success = result.success
        engine.results.append(result)
    
    # Save results
    if args.output and engine.results:
        results_data = [r.to_dict() for r in engine.results]
        output_data = {
            'timestamp': datetime.now().isoformat(),
            'total_scenarios': len(engine.results),
            'passed': sum(1 for r in engine.results if r.success),
            'results': results_data
        }
        with open(args.output, 'w') as f:
            json.dump(output_data, f, indent=2)
        print(f"\nResults saved to {args.output}")
    
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
