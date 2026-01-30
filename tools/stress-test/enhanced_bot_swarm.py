#!/usr/bin/env python3
"""
DarkAges MMO - Enhanced Bot Swarm for Load Testing
[DEVOPS_AGENT] Simulates 1000+ concurrent players for capacity testing

Features:
- Realistic movement patterns (wandering, combat, social)
- Network latency simulation
- Packet loss simulation
- Bandwidth monitoring
- Statistical reporting
"""

import asyncio
import random
import time
import json
import argparse
import statistics
from dataclasses import dataclass, field
from typing import List, Dict, Optional, Callable
from enum import Enum, auto
import logging

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger('BotSwarm')


class BotState(Enum):
    """Bot behavior states"""
    IDLE = auto()
    WANDERING = auto()
    MOVING_TO_TARGET = auto()
    IN_COMBAT = auto()
    DEAD = auto()
    RESPAWNING = auto()
    SOCIAL = auto()


@dataclass
class Position:
    """3D position in game world"""
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0
    
    def distance_to(self, other: 'Position') -> float:
        return ((self.x - other.x)**2 + (self.y - other.y)**2 + (self.z - other.z)**2) ** 0.5
    
    def move_towards(self, target: 'Position', speed: float, dt: float) -> 'Position':
        """Move towards target at given speed"""
        dist = self.distance_to(target)
        if dist == 0:
            return Position(self.x, self.y, self.z)
        
        move_dist = min(speed * dt, dist)
        ratio = move_dist / dist
        
        return Position(
            self.x + (target.x - self.x) * ratio,
            self.y + (target.y - self.y) * ratio,
            self.z + (target.z - self.z) * ratio
        )


@dataclass
class BotConfig:
    """Configuration for bot behavior"""
    # Movement
    move_speed: float = 5.0  # meters/second
    rotation_speed: float = 180.0  # degrees/second
    
    # Behavior timing
    idle_time_min: float = 2.0
    idle_time_max: float = 10.0
    wander_radius: float = 50.0
    
    # Combat
    combat_chance: float = 0.3  # 30% chance to engage in combat
    combat_duration_min: float = 5.0
    combat_duration_max: float = 30.0
    death_duration: float = 5.0
    
    # Network simulation
    latency_ms: float = 50.0  # Base latency
    latency_jitter_ms: float = 20.0
    packet_loss_percent: float = 0.1
    
    # Input rate
    input_rate_hz: float = 60.0  # Inputs per second


@dataclass
class BotMetrics:
    """Metrics collected for each bot"""
    connection_time: float = 0.0
    disconnect_time: Optional[float] = None
    messages_sent: int = 0
    messages_received: int = 0
    bytes_sent: int = 0
    bytes_received: int = 0
    latency_samples: List[float] = field(default_factory=list)
    errors: int = 0
    
    @property
    def avg_latency(self) -> float:
        return statistics.mean(self.latency_samples) if self.latency_samples else 0.0
    
    @property
    def max_latency(self) -> float:
        return max(self.latency_samples) if self.latency_samples else 0.0


class Bot:
    """
    Simulated player bot with realistic behavior
    """
    
    def __init__(self, bot_id: int, config: BotConfig, world_bounds: tuple):
        self.id = bot_id
        self.config = config
        self.world_bounds = world_bounds  # (min_x, max_x, min_z, max_z)
        
        # State
        self.state = BotState.IDLE
        self.position = Position(
            random.uniform(world_bounds[0], world_bounds[1]),
            0.0,
            random.uniform(world_bounds[2], world_bounds[3])
        )
        self.rotation = random.uniform(0, 360)
        self.velocity = Position(0, 0, 0)
        
        # Behavior
        self.state_timer = 0.0
        self.target_position: Optional[Position] = None
        self.combat_target: Optional[int] = None
        self.health = 100
        
        # Network
        self.connected = False
        self.connection_time = 0.0
        self.metrics = BotMetrics()
        
        # Input simulation
        self.last_input_time = 0.0
        self.pending_inputs: List[dict] = []
        
        # Callbacks
        self.on_state_change: Optional[Callable] = None
        
    def connect(self, current_time: float):
        """Connect bot to server"""
        self.connected = True
        self.connection_time = current_time
        self.metrics.connection_time = current_time
        logger.debug(f"Bot {self.id} connected")
        
    def disconnect(self, current_time: float, reason: str = ""):
        """Disconnect bot from server"""
        self.connected = False
        self.metrics.disconnect_time = current_time
        logger.debug(f"Bot {self.id} disconnected: {reason}")
        
    def update(self, current_time: float, dt: float, other_bots: List['Bot']):
        """Update bot state machine"""
        if not self.connected:
            return
        
        # Update state machine
        self._update_state(current_time, dt, other_bots)
        
        # Simulate network latency
        self._simulate_network(current_time)
        
        # Send inputs at configured rate
        self._send_inputs(current_time)
        
    def _update_state(self, current_time: float, dt: float, other_bots: List['Bot']):
        """Update behavior state machine"""
        self.state_timer -= dt
        
        # State transitions
        if self.state == BotState.IDLE:
            if self.state_timer <= 0:
                # Decide next action
                r = random.random()
                if r < 0.6:  # 60% wander
                    self._start_wandering()
                elif r < 0.8:  # 20% social
                    self._start_social(other_bots)
                else:  # 20% stay idle
                    self.state_timer = random.uniform(
                        self.config.idle_time_min,
                        self.config.idle_time_max
                    )
                    
        elif self.state == BotState.WANDERING:
            if self.target_position:
                # Move towards target
                self.position = self.position.move_towards(
                    self.target_position, self.config.move_speed, dt
                )
                
                # Check if reached target
                if self.position.distance_to(self.target_position) < 1.0:
                    self.state = BotState.IDLE
                    self.state_timer = random.uniform(1.0, 5.0)
                    self.target_position = None
                    
        elif self.state == BotState.IN_COMBAT:
            if self.state_timer <= 0 or self.health <= 0:
                if self.health <= 0:
                    self.state = BotState.DEAD
                    self.state_timer = self.config.death_duration
                else:
                    self.state = BotState.IDLE
                    self.state_timer = random.uniform(2.0, 5.0)
                    
        elif self.state == BotState.DEAD:
            if self.state_timer <= 0:
                self.state = BotState.RESPAWNING
                self.health = 100
                self.state_timer = 2.0
                
        elif self.state == BotState.RESPAWNING:
            if self.state_timer <= 0:
                self.state = BotState.IDLE
                self.state_timer = random.uniform(2.0, 5.0)
                
        elif self.state == BotState.SOCIAL:
            if self.state_timer <= 0:
                self.state = BotState.IDLE
                self.state_timer = random.uniform(2.0, 5.0)
        
        # Random combat engagement
        if self.state in [BotState.IDLE, BotState.WANDERING] and random.random() < 0.01:
            self._try_enter_combat(other_bots)
    
    def _start_wandering(self):
        """Start wandering to a random point"""
        self.state = BotState.WANDERING
        angle = random.uniform(0, 2 * 3.14159)
        distance = random.uniform(10, self.config.wander_radius)
        
        self.target_position = Position(
            self.position.x + distance * random.choice([-1, 1]),
            0.0,
            self.position.z + distance * random.choice([-1, 1])
        )
        
        # Clamp to world bounds
        self.target_position.x = max(self.world_bounds[0], 
                                     min(self.world_bounds[1], self.target_position.x))
        self.target_position.z = max(self.world_bounds[2], 
                                     min(self.world_bounds[3], self.target_position.z))
    
    def _start_social(self, other_bots: List['Bot']):
        """Start social behavior (follow/chase other bots)"""
        nearby = [b for b in other_bots 
                  if b.id != self.id and b.position.distance_to(self.position) < 30]
        if nearby:
            self.state = BotState.SOCIAL
            self.target_position = random.choice(nearby).position
            self.state_timer = random.uniform(5.0, 15.0)
        else:
            self._start_wandering()
    
    def _try_enter_combat(self, other_bots: List['Bot']):
        """Try to enter combat with nearby bot"""
        if random.random() > self.config.combat_chance:
            return
            
        nearby = [b for b in other_bots 
                  if b.id != self.id and b.state != BotState.DEAD 
                  and b.position.distance_to(self.position) < 20]
        if nearby:
            self.state = BotState.IN_COMBAT
            self.combat_target = random.choice(nearby).id
            self.state_timer = random.uniform(
                self.config.combat_duration_min,
                self.config.combat_duration_max
            )
    
    def _simulate_network(self, current_time: float):
        """Simulate network conditions"""
        # Simulate packet loss
        if random.random() < (self.config.packet_loss_percent / 100):
            return
        
        # Calculate latency
        latency = self.config.latency_ms + random.uniform(
            -self.config.latency_jitter_ms,
            self.config.latency_jitter_ms
        )
        latency = max(1.0, latency)  # Minimum 1ms
        
        self.metrics.latency_samples.append(latency)
        
        # Keep only last 100 samples
        if len(self.metrics.latency_samples) > 100:
            self.metrics.latency_samples = self.metrics.latency_samples[-100:]
    
    def _send_inputs(self, current_time: float):
        """Send input packets at configured rate"""
        input_interval = 1.0 / self.config.input_rate_hz
        
        while current_time - self.last_input_time >= input_interval:
            self.last_input_time += input_interval
            
            # Build input packet
            input_packet = {
                'sequence': self.metrics.messages_sent,
                'timestamp': time.time() * 1000,
                'position': {
                    'x': self.position.x,
                    'y': self.position.y,
                    'z': self.position.z
                },
                'rotation': self.rotation,
                'state': self.state.name,
                'health': self.health
            }
            
            self.pending_inputs.append(input_packet)
            self.metrics.messages_sent += 1
            self.metrics.bytes_sent += len(json.dumps(input_packet).encode())


class BotSwarm:
    """
    Manages a swarm of bots for load testing
    """
    
    def __init__(self, config: BotConfig = None, world_bounds: tuple = (-500, 500, -500, 500)):
        self.config = config or BotConfig()
        self.world_bounds = world_bounds
        self.bots: Dict[int, Bot] = {}
        self.running = False
        self.start_time = 0.0
        
        # Statistics
        self.stats = {
            'total_connections': 0,
            'peak_connections': 0,
            'total_messages_sent': 0,
            'total_messages_received': 0,
            'errors': 0
        }
        
    async def create_bots(self, count: int, ramp_up_time: float = 0):
        """Create and connect bots with optional ramp-up"""
        delay_per_bot = ramp_up_time / count if ramp_up_time > 0 else 0
        
        for i in range(count):
            bot = Bot(i, self.config, self.world_bounds)
            bot.connect(time.time())
            self.bots[bot.id] = bot
            self.stats['total_connections'] += 1
            
            if delay_per_bot > 0:
                await asyncio.sleep(delay_per_bot)
                
        self.stats['peak_connections'] = len(self.bots)
        logger.info(f"Created {count} bots (total: {len(self.bots)})")
        
    async def disconnect_bots(self, count: int, ramp_down_time: float = 0):
        """Disconnect random bots"""
        delay_per_bot = ramp_down_time / count if ramp_down_time > 0 else 0
        
        bots_to_disconnect = random.sample(
            list(self.bots.values()), 
            min(count, len(self.bots))
        )
        
        for bot in bots_to_disconnect:
            bot.disconnect(time.time(), "Test disconnect")
            del self.bots[bot.id]
            
            if delay_per_bot > 0:
                await asyncio.sleep(delay_per_bot)
                
        logger.info(f"Disconnected {count} bots (remaining: {len(self.bots)})")
        
    async def run(self, duration: float, tick_rate: float = 60.0):
        """Run simulation for specified duration"""
        self.running = True
        self.start_time = time.time()
        tick_interval = 1.0 / tick_rate
        
        logger.info(f"Starting bot swarm simulation for {duration}s")
        
        while self.running and (time.time() - self.start_time) < duration:
            loop_start = time.time()
            
            # Update all bots
            current_time = time.time()
            bot_list = list(self.bots.values())
            
            for bot in bot_list:
                try:
                    bot.update(current_time, tick_interval, bot_list)
                except Exception as e:
                    logger.error(f"Bot {bot.id} error: {e}")
                    self.stats['errors'] += 1
            
            # Update statistics
            self._update_stats()
            
            # Sleep until next tick
            elapsed = time.time() - loop_start
            sleep_time = tick_interval - elapsed
            if sleep_time > 0:
                await asyncio.sleep(sleep_time)
                
        self.running = False
        logger.info("Bot swarm simulation complete")
        
    def _update_stats(self):
        """Update aggregate statistics"""
        self.stats['total_messages_sent'] = sum(
            b.metrics.messages_sent for b in self.bots.values()
        )
        self.stats['total_messages_received'] = sum(
            b.metrics.messages_received for b in self.bots.values()
        )
        
    def get_report(self) -> dict:
        """Generate comprehensive test report"""
        if not self.bots:
            return {"error": "No bots were created"}
        
        # Collect all metrics
        latencies = []
        connection_durations = []
        messages_per_bot = []
        
        for bot in self.bots.values():
            latencies.extend(bot.metrics.latency_samples)
            if bot.metrics.disconnect_time:
                duration = bot.metrics.disconnect_time - bot.metrics.connection_time
            else:
                duration = time.time() - bot.metrics.connection_time
            connection_durations.append(duration)
            messages_per_bot.append(bot.metrics.messages_sent)
        
        report = {
            'test_duration': time.time() - self.start_time,
            'bot_count': len(self.bots),
            'statistics': {
                'total_connections': self.stats['total_connections'],
                'peak_connections': self.stats['peak_connections'],
                'total_messages_sent': self.stats['total_messages_sent'],
                'total_errors': self.stats['errors']
            },
            'latency': {
                'mean_ms': statistics.mean(latencies) if latencies else 0,
                'median_ms': statistics.median(latencies) if latencies else 0,
                'p95_ms': self._percentile(latencies, 95) if latencies else 0,
                'p99_ms': self._percentile(latencies, 99) if latencies else 0,
                'max_ms': max(latencies) if latencies else 0
            },
            'message_rate': {
                'total_per_second': self.stats['total_messages_sent'] / max(1, time.time() - self.start_time),
                'per_bot_mean': statistics.mean(messages_per_bot) if messages_per_bot else 0,
                'per_bot_max': max(messages_per_bot) if messages_per_bot else 0
            },
            'connection_duration': {
                'mean_s': statistics.mean(connection_durations) if connection_durations else 0,
                'min_s': min(connection_durations) if connection_durations else 0,
                'max_s': max(connection_durations) if connection_durations else 0
            },
            'state_distribution': self._get_state_distribution()
        }
        
        return report
    
    def _percentile(self, data: List[float], percentile: int) -> float:
        """Calculate percentile value"""
        if not data:
            return 0.0
        sorted_data = sorted(data)
        index = int(len(sorted_data) * percentile / 100)
        return sorted_data[min(index, len(sorted_data) - 1)]
    
    def _get_state_distribution(self) -> dict:
        """Get distribution of bot states"""
        distribution = {}
        for bot in self.bots.values():
            state_name = bot.state.name
            distribution[state_name] = distribution.get(state_name, 0) + 1
        return distribution
    
    def print_report(self):
        """Print formatted report to console"""
        report = self.get_report()
        
        print("\n" + "="*60)
        print("DARKAGES BOT SWARM TEST REPORT")
        print("="*60)
        print(f"Test Duration: {report['test_duration']:.1f}s")
        print(f"Active Bots: {report['bot_count']}")
        print()
        print("CONNECTION STATISTICS:")
        print(f"  Total Connections: {report['statistics']['total_connections']}")
        print(f"  Peak Connections: {report['statistics']['peak_connections']}")
        print(f"  Total Messages: {report['statistics']['total_messages_sent']:,}")
        print(f"  Total Errors: {report['statistics']['total_errors']}")
        print()
        print("LATENCY (ms):")
        print(f"  Mean:   {report['latency']['mean_ms']:6.2f}")
        print(f"  Median: {report['latency']['median_ms']:6.2f}")
        print(f"  P95:    {report['latency']['p95_ms']:6.2f}")
        print(f"  P99:    {report['latency']['p99_ms']:6.2f}")
        print(f"  Max:    {report['latency']['max_ms']:6.2f}")
        print()
        print("MESSAGE RATE:")
        print(f"  Total/sec: {report['message_rate']['total_per_second']:,.1f}")
        print(f"  Per bot mean: {report['message_rate']['per_bot_mean']:,.1f}")
        print()
        print("STATE DISTRIBUTION:")
        for state, count in report['state_distribution'].items():
            pct = count / report['bot_count'] * 100
            print(f"  {state}: {count} ({pct:.1f}%)")
        print("="*60)


async def main():
    parser = argparse.ArgumentParser(description='DarkAges Bot Swarm Load Test')
    parser.add_argument('-n', '--bots', type=int, default=100, 
                        help='Number of bots to simulate')
    parser.add_argument('-d', '--duration', type=float, default=60.0,
                        help='Test duration in seconds')
    parser.add_argument('--ramp-up', type=float, default=10.0,
                        help='Ramp-up time in seconds')
    parser.add_argument('--latency', type=float, default=50.0,
                        help='Simulated latency (ms)')
    parser.add_argument('--packet-loss', type=float, default=0.1,
                        help='Simulated packet loss (%%)')
    parser.add_argument('--output', type=str, default=None,
                        help='Output JSON file for results')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Enable verbose logging')
    
    args = parser.parse_args()
    
    if args.verbose:
        logging.getLogger().setLevel(logging.DEBUG)
    
    # Create bot configuration
    config = BotConfig(
        latency_ms=args.latency,
        packet_loss_percent=args.packet_loss
    )
    
    # Create and run swarm
    swarm = BotSwarm(config)
    
    # Create bots with ramp-up
    await swarm.create_bots(args.bots, args.ramp_up)
    
    # Run simulation
    try:
        await swarm.run(args.duration)
    except KeyboardInterrupt:
        logger.info("Interrupted by user")
    
    # Generate report
    swarm.print_report()
    
    # Save to file if requested
    if args.output:
        report = swarm.get_report()
        with open(args.output, 'w') as f:
            json.dump(report, f, indent=2)
        logger.info(f"Report saved to {args.output}")


if __name__ == '__main__':
    asyncio.run(main())
