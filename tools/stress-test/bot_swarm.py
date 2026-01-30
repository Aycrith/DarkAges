#!/usr/bin/env python3
"""
DarkAges MMO - Bot Swarm Stress Test Tool
Simulates multiple players connecting to the server for load testing.

Usage:
    python bot_swarm.py --host 127.0.0.1 --port 7777 --bots 10 --duration 60

This tool uses a simplified binary protocol that matches the FlatBuffers schema
in src/shared/proto/game_protocol.fbs but without requiring the flatc compiler.
"""

import asyncio
import argparse
import random
import struct
import time
import sys
from dataclasses import dataclass, field
from typing import Optional, List, Dict, Tuple
import socket


# Protocol constants from Constants.hpp
DEFAULT_SERVER_PORT = 7777
TICK_RATE_HZ = 60
SNAPSHOT_RATE_HZ = 20
MAX_DOWNSTREAM_BYTES_PER_SEC = 20480  # 20 KB/s
MAX_UPSTREAM_BYTES_PER_SEC = 2048     # 2 KB/s
PROTOCOL_VERSION = 1

# World bounds from Constants.hpp
WORLD_MIN_X = -5000.0
WORLD_MAX_X = 5000.0
WORLD_MIN_Z = -5000.0
WORLD_MAX_Z = 5000.0

# Movement constants from Constants.hpp
MAX_PLAYER_SPEED = 6.0  # m/s
SPRINT_SPEED_MULTIPLIER = 1.5
MAX_SPRINT_SPEED = MAX_PLAYER_SPEED * SPRINT_SPEED_MULTIPLIER


@dataclass
class BotConfig:
    """Configuration for a single bot"""
    host: str
    port: int
    bot_id: int
    movement_pattern: str = "random"  # random, circle, linear
    update_rate: float = 60.0  # Hz (input send rate)


class GameBot:
    """
    Simulates a game client - connects to server and sends inputs.
    Uses raw UDP with simplified protocol matching game_protocol.fbs structure.
    """
    
    # Simplified packet types (matches protocol schema)
    PACKET_CLIENT_INPUT = 1
    PACKET_SERVER_SNAPSHOT = 2
    PACKET_CONNECTION_REQUEST = 3
    PACKET_CONNECTION_RESPONSE = 4
    PACKET_EVENT = 5
    PACKET_SERVER_CORRECTION = 6
    
    # Input flags (matches input_flags bit field)
    INPUT_FORWARD = 0x01
    INPUT_BACKWARD = 0x02
    INPUT_LEFT = 0x04
    INPUT_RIGHT = 0x08
    INPUT_JUMP = 0x10
    INPUT_ATTACK = 0x20
    INPUT_BLOCK = 0x40
    INPUT_SPRINT = 0x80
    
    def __init__(self, config: BotConfig):
        self.config = config
        self.socket: Optional[socket.socket] = None
        self.connected = False
        self.entity_id: Optional[int] = None
        self.connection_id: Optional[int] = None
        self.server_tick: int = 0
        self.server_time: int = 0
        self.position = [0.0, 0.0, 0.0]  # Starting position
        self.velocity = [0.0, 0.0, 0.0]
        self.sequence: int = 0
        self.start_time: float = 0.0
        
        # Statistics
        self.packets_sent = 0
        self.packets_received = 0
        self.bytes_sent = 0
        self.bytes_received = 0
        self.snapshot_count = 0
        self.correction_count = 0
        self.event_count = 0
        self.last_processed_input = 0
        
        # Movement state
        self.yaw: float = random.uniform(0, 6.28318530718)  # Random initial direction (0-2π)
        self.pitch: float = 0.0
        self.input_state = {
            'forward': False,
            'backward': False,
            'left': False,
            'right': False,
            'sprint': False,
            'jump': False,
            'attack': False,
            'block': False
        }
        self.target_entity: int = 0
        
        # Latency tracking
        self.latencies: List[float] = []
        self.last_ping_time: float = 0.0
    
    async def connect(self) -> bool:
        """Connect to game server via UDP"""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.socket.setblocking(False)
            # Note: UDP is connectionless, but connect() sets default destination
            self.socket.connect((self.config.host, self.config.port))
            
            # Send connection request
            connect_packet = self._create_connection_request()
            await self._send_packet(connect_packet)
            
            # Wait for response with timeout
            try:
                await asyncio.wait_for(self._wait_for_connection(), timeout=5.0)
            except asyncio.TimeoutError:
                print(f"[Bot {self.config.bot_id}] Connection timeout")
                return False
            
            if self.connected:
                print(f"[Bot {self.config.bot_id}] Connected as entity {self.entity_id}, "
                      f"connection {self.connection_id}, server tick {self.server_tick}")
                return True
            else:
                print(f"[Bot {self.config.bot_id}] Connection rejected")
                return False
                
        except Exception as e:
            print(f"[Bot {self.config.bot_id}] Connection failed: {e}")
            return False
    
    async def _send_packet(self, data: bytes) -> None:
        """Send packet and update stats"""
        self.socket.send(data)
        self.packets_sent += 1
        self.bytes_sent += len(data)
    
    async def _wait_for_connection(self) -> None:
        """Wait for server connection response"""
        while not self.connected:
            try:
                data = self.socket.recv(1024)
                if data and len(data) >= 1:
                    packet_type = data[0]
                    if packet_type == self.PACKET_CONNECTION_RESPONSE:
                        self._parse_connection_response(data)
                    self.packets_received += 1
                    self.bytes_received += len(data)
            except BlockingIOError:
                await asyncio.sleep(0.01)
    
    def _create_connection_request(self) -> bytes:
        """
        Create connection request packet.
        Structure: [type:u8][protocol_version:u32][player_id:u64]
        """
        return struct.pack('<BIQ', 
            self.PACKET_CONNECTION_REQUEST,
            PROTOCOL_VERSION,
            self.config.bot_id  # Use bot_id as player_id for testing
        )
    
    def _parse_connection_response(self, data: bytes) -> None:
        """
        Parse connection response.
        Structure: [type:u8][success:u8][connection_id:u32][entity_id:u32][server_tick:u32]
        """
        if len(data) < 18:
            return
        
        success = data[1] != 0
        if success:
            self.connection_id = struct.unpack('<I', data[2:6])[0]
            self.entity_id = struct.unpack('<I', data[6:10])[0]
            self.server_tick = struct.unpack('<I', data[10:14])[0]
            self.server_time = struct.unpack('<I', data[14:18])[0]
            self.connected = True
            self.start_time = time.time()
    
    def _create_input_packet(self) -> bytes:
        """
        Create client input packet.
        Matches the ClientInput table structure from game_protocol.fbs:
        [type:u8][sequence:u32][timestamp:u32][input_flags:u8][yaw:i16][pitch:i16][target_entity:u32]
        Total: 20 bytes
        """
        # Build input flags
        flags = 0
        if self.input_state['forward']: flags |= self.INPUT_FORWARD
        if self.input_state['backward']: flags |= self.INPUT_BACKWARD
        if self.input_state['left']: flags |= self.INPUT_LEFT
        if self.input_state['right']: flags |= self.INPUT_RIGHT
        if self.input_state['jump']: flags |= self.INPUT_JUMP
        if self.input_state['attack']: flags |= self.INPUT_ATTACK
        if self.input_state['block']: flags |= self.INPUT_BLOCK
        if self.input_state['sprint']: flags |= self.INPUT_SPRINT
        
        # Quantize rotation to int16 (matches schema: actual = value / 10000.0)
        yaw_quantized = int(self.yaw * 10000) % 65536
        if yaw_quantized > 32767:
            yaw_quantized -= 65536
        pitch_quantized = int(self.pitch * 10000)
        
        # Timestamp in milliseconds (uint32)
        timestamp = int(time.time() * 1000) % 0xFFFFFFFF
        
        packet = struct.pack('<B I I B h h I',
            self.PACKET_CLIENT_INPUT,
            self.sequence,
            timestamp,
            flags,
            yaw_quantized,
            pitch_quantized,
            self.target_entity
        )
        self.sequence += 1
        return packet
    
    async def run(self, duration_seconds: float) -> Dict:
        """Main bot loop - sends inputs and receives snapshots"""
        if not self.connected:
            return self.get_stats()
        
        start_time = time.time()
        last_input_time = start_time
        input_interval = 1.0 / self.config.update_rate
        
        # Start receive loop as background task
        receive_task = asyncio.create_task(self._receive_loop())
        
        try:
            while time.time() - start_time < duration_seconds:
                current_time = time.time()
                elapsed = current_time - start_time
                
                # Update movement pattern
                self._update_movement(elapsed)
                
                # Send input at configured rate
                if current_time - last_input_time >= input_interval:
                    input_packet = self._create_input_packet()
                    await self._send_packet(input_packet)
                    last_input_time = current_time
                
                # Small yield to prevent busy-waiting
                await asyncio.sleep(0.001)
                
        except asyncio.CancelledError:
            pass
        finally:
            receive_task.cancel()
            try:
                await receive_task
            except asyncio.CancelledError:
                pass
        
        return self.get_stats()
    
    async def _receive_loop(self) -> None:
        """Background task to receive server packets"""
        while True:
            try:
                data = self.socket.recv(2048)
                if data:
                    self.packets_received += 1
                    self.bytes_received += len(data)
                    self._process_packet(data)
            except BlockingIOError:
                await asyncio.sleep(0.001)
            except Exception:
                break
    
    def _process_packet(self, data: bytes) -> None:
        """Process received packet"""
        if len(data) < 1:
            return
        
        packet_type = data[0]
        
        if packet_type == self.PACKET_SERVER_SNAPSHOT:
            self._parse_snapshot(data)
        elif packet_type == self.PACKET_SERVER_CORRECTION:
            self._parse_correction(data)
        elif packet_type == self.PACKET_EVENT:
            self._parse_event(data)
    
    def _parse_snapshot(self, data: bytes) -> None:
        """
        Parse server snapshot.
        Simplified parsing - extracts basic header info.
        Full schema: [type:u8][server_tick:u32][baseline_tick:u32][server_time:u32][...entities]
        """
        if len(data) < 17:
            return
        
        self.snapshot_count += 1
        self.server_tick = struct.unpack('<I', data[1:5])[0]
        baseline_tick = struct.unpack('<I', data[5:9])[0]
        self.server_time = struct.unpack('<I', data[9:13])[0]
        self.last_processed_input = struct.unpack('<I', data[13:17])[0]
        
        # Note: Entity parsing would require full FlatBuffers deserialization
        # For stress testing, we just count the snapshots received
    
    def _parse_correction(self, data: bytes) -> None:
        """Parse server correction packet"""
        self.correction_count += 1
        # Server corrections indicate prediction errors
    
    def _parse_event(self, data: bytes) -> None:
        """Parse event packet"""
        self.event_count += 1
    
    def _update_movement(self, elapsed: float) -> None:
        """
        Update movement based on configured pattern.
        Movement speeds are constrained by physics constants.
        """
        # Reset inputs
        for key in self.input_state:
            self.input_state[key] = False
        
        if self.config.movement_pattern == "random":
            self._update_random_movement()
        elif self.config.movement_pattern == "circle":
            self._update_circle_movement(elapsed)
        elif self.config.movement_pattern == "linear":
            self._update_linear_movement(elapsed)
        elif self.config.movement_pattern == "stationary":
            pass  # No movement
    
    def _update_random_movement(self) -> None:
        """Random wandering with momentum"""
        # 5% chance to change direction each update
        if random.random() < 0.05:
            self.yaw += random.uniform(-1.0, 1.0)
            # Normalize to 0-2π
            self.yaw = self.yaw % 6.28318530718
        
        # 70% chance to move forward
        self.input_state['forward'] = random.random() > 0.3
        
        # Occasionally strafe
        if random.random() < 0.1:
            if random.random() < 0.5:
                self.input_state['left'] = True
            else:
                self.input_state['right'] = True
        
        # 30% chance to sprint when moving
        if self.input_state['forward']:
            self.input_state['sprint'] = random.random() > 0.7
        
        # 5% chance to jump
        self.input_state['jump'] = random.random() < 0.05
    
    def _update_circle_movement(self, elapsed: float) -> None:
        """Move in a circular pattern"""
        # Rotate yaw slowly to create circle
        self.yaw += 0.03  # ~1.7 degrees per tick
        self.yaw = self.yaw % 6.28318530718
        self.input_state['forward'] = True
        self.input_state['sprint'] = False
    
    def _update_linear_movement(self, elapsed: float) -> None:
        """Walk back and forth in a line"""
        cycle = (elapsed % 10) / 5.0  # 0-2 over 10 seconds
        if cycle > 1:
            self.yaw = 3.14159265359  # π - facing opposite direction
        else:
            self.yaw = 0.0
        self.input_state['forward'] = True
        self.input_state['sprint'] = random.random() > 0.8
    
    def disconnect(self) -> None:
        """Disconnect from server"""
        if self.socket:
            self.socket.close()
            self.socket = None
        self.connected = False
    
    def get_stats(self) -> Dict:
        """Get bot statistics"""
        elapsed = max(time.time() - self.start_time, 0.001) if self.start_time else 0.001
        return {
            'bot_id': self.config.bot_id,
            'entity_id': self.entity_id,
            'connection_id': self.connection_id,
            'packets_sent': self.packets_sent,
            'packets_received': self.packets_received,
            'bytes_sent': self.bytes_sent,
            'bytes_received': self.bytes_received,
            'snapshots_received': self.snapshot_count,
            'corrections_received': self.correction_count,
            'events_received': self.event_count,
            'last_processed_input': self.last_processed_input,
            'duration_seconds': elapsed,
            'packets_sent_per_sec': self.packets_sent / elapsed,
            'packets_received_per_sec': self.packets_received / elapsed,
            'bytes_sent_per_sec': self.bytes_sent / elapsed,
            'bytes_received_per_sec': self.bytes_received / elapsed,
            'snapshots_per_sec': self.snapshot_count / elapsed,
            'avg_bandwidth_down_kbps': (self.bytes_received * 8 / 1024) / elapsed,
            'avg_bandwidth_up_kbps': (self.bytes_sent * 8 / 1024) / elapsed,
        }


class BotSwarm:
    """Manages multiple bots for stress testing"""
    
    def __init__(self, host: str, port: int, bot_count: int, duration: float, 
                 pattern: Optional[str] = None):
        self.host = host
        self.port = port
        self.bot_count = bot_count
        self.duration = duration
        self.pattern = pattern
        self.bots: List[GameBot] = []
        
    async def run(self) -> List[Dict]:
        """Run all bots and collect statistics"""
        print(f"="*60)
        print(f"DARKAGES BOT SWARM - Starting Test")
        print(f"="*60)
        print(f"Target server: {self.host}:{self.port}")
        print(f"Bot count: {self.bot_count}")
        print(f"Duration: {self.duration}s")
        print(f"Input rate: {TICK_RATE_HZ}Hz")
        print(f"-"*60)
        
        # Create bots
        patterns = ['random', 'circle', 'linear', 'stationary']
        for i in range(self.bot_count):
            # Use specified pattern or distribute randomly
            bot_pattern = self.pattern if self.pattern else patterns[i % len(patterns)]
            config = BotConfig(
                host=self.host,
                port=self.port,
                bot_id=i + 1,
                movement_pattern=bot_pattern,
                update_rate=TICK_RATE_HZ
            )
            self.bots.append(GameBot(config))
        
        # Connect all bots concurrently
        print(f"\nConnecting {self.bot_count} bots...")
        connect_tasks = [bot.connect() for bot in self.bots]
        connect_results = await asyncio.gather(*connect_tasks, return_exceptions=True)
        
        connected_bots = []
        connection_errors = 0
        for bot, result in zip(self.bots, connect_results):
            if isinstance(result, Exception):
                connection_errors += 1
            elif result is True:
                connected_bots.append(bot)
        
        print(f"Connected: {len(connected_bots)}/{self.bot_count}")
        if connection_errors > 0:
            print(f"Connection errors: {connection_errors}")
        
        if not connected_bots:
            print("ERROR: No bots could connect! Is the server running?")
            return []
        
        # Run all connected bots
        print(f"\nRunning test for {self.duration} seconds...")
        run_tasks = [bot.run(self.duration) for bot in connected_bots]
        stats = await asyncio.gather(*run_tasks, return_exceptions=True)
        
        # Filter out exceptions and collect valid stats
        valid_stats = []
        for i, s in enumerate(stats):
            if isinstance(s, Exception):
                print(f"[Bot {connected_bots[i].config.bot_id}] Runtime error: {s}")
            else:
                valid_stats.append(s)
        
        # Disconnect all
        print("\nDisconnecting bots...")
        for bot in self.bots:
            bot.disconnect()
        
        return valid_stats


def print_summary(stats: List[Dict]) -> None:
    """Print test summary statistics"""
    if not stats:
        print("\n" + "="*60)
        print("NO STATISTICS COLLECTED!")
        print("="*60)
        return
    
    print("\n" + "="*60)
    print("BOT SWARM TEST SUMMARY")
    print("="*60)
    
    total_sent = sum(s['packets_sent'] for s in stats)
    total_received = sum(s['packets_received'] for s in stats)
    total_bytes_sent = sum(s['bytes_sent'] for s in stats)
    total_bytes_received = sum(s['bytes_received'] for s in stats)
    total_snapshots = sum(s['snapshots_received'] for s in stats)
    total_corrections = sum(s['corrections_received'] for s in stats)
    
    duration = max(s['duration_seconds'] for s in stats)
    
    print(f"Bots Connected: {len(stats)}")
    print(f"Test Duration: {duration:.1f}s")
    print()
    print("Packet Statistics:")
    print(f"  Total packets sent: {total_sent:,}")
    print(f"  Total packets received: {total_received:,}")
    print(f"  Packets sent/sec: {total_sent/duration:.0f}")
    print(f"  Packets received/sec: {total_received/duration:.0f}")
    print()
    print("Bandwidth Usage:")
    print(f"  Total sent: {total_bytes_sent / 1024:.2f} KB ({total_bytes_sent / 1024/1024:.2f} MB)")
    print(f"  Total received: {total_bytes_received / 1024:.2f} KB ({total_bytes_received / 1024/1024:.2f} MB)")
    print(f"  Upload rate: {(total_bytes_sent * 8 / 1024) / duration:.2f} Kbps")
    print(f"  Download rate: {(total_bytes_received * 8 / 1024) / duration:.2f} Kbps")
    print()
    print("Per-Bot Averages:")
    print(f"  Snapshots received: {total_snapshots / len(stats):.1f}")
    print(f"  Server corrections: {total_corrections / len(stats):.1f}")
    print(f"  Avg upload: {(total_bytes_sent / len(stats) * 8 / 1024) / duration:.2f} Kbps/bot")
    print(f"  Avg download: {(total_bytes_received / len(stats) * 8 / 1024) / duration:.2f} Kbps/bot")
    
    # Check bandwidth compliance
    avg_up_per_bot = (total_bytes_sent / len(stats)) / duration
    avg_down_per_bot = (total_bytes_received / len(stats)) / duration
    print()
    print("Budget Compliance:")
    up_ok = avg_up_per_bot <= MAX_UPSTREAM_BYTES_PER_SEC
    down_ok = avg_down_per_bot <= MAX_DOWNSTREAM_BYTES_PER_SEC
    print(f"  Upload budget (2KB/s): {'PASS' if up_ok else 'FAIL'} ({avg_up_per_bot:.0f} bytes/s)")
    print(f"  Download budget (20KB/s): {'PASS' if down_ok else 'FAIL'} ({avg_down_per_bot:.0f} bytes/s)")
    
    # Snapshot rate check
    avg_snapshots = (total_snapshots / len(stats)) / duration
    expected_snapshots = SNAPSHOT_RATE_HZ * duration
    snapshot_ratio = avg_snapshots / SNAPSHOT_RATE_HZ
    print(f"  Snapshot rate: {avg_snapshots:.1f}/sec (expected {SNAPSHOT_RATE_HZ}/sec, ratio {snapshot_ratio:.2f})")
    
    print("="*60)


def main():
    parser = argparse.ArgumentParser(
        description='DarkAges Bot Swarm Stress Test',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Run 10 bots for 60 seconds with random movement
    python bot_swarm.py --bots 10 --duration 60
    
    # Run 50 bots with circular movement pattern
    python bot_swarm.py --bots 50 --duration 120 --pattern circle
    
    # Connect to remote server
    python bot_swarm.py --host 192.168.1.100 --port 7777 --bots 20
    
Movement patterns:
    random    - Random wandering with momentum
    circle    - Move in continuous circles
    linear    - Walk back and forth
    stationary- No movement (for baseline testing)
        """
    )
    parser.add_argument('--host', default='127.0.0.1', help='Server host (default: 127.0.0.1)')
    parser.add_argument('--port', type=int, default=DEFAULT_SERVER_PORT, 
                        help=f'Server port (default: {DEFAULT_SERVER_PORT})')
    parser.add_argument('--bots', type=int, default=10, help='Number of bots (default: 10)')
    parser.add_argument('--duration', type=int, default=60, help='Test duration in seconds (default: 60)')
    parser.add_argument('--pattern', choices=['random', 'circle', 'linear', 'stationary'],
                        help='Movement pattern (default: distribute all types)')
    
    args = parser.parse_args()
    
    try:
        swarm = BotSwarm(args.host, args.port, args.bots, args.duration, args.pattern)
        stats = asyncio.run(swarm.run())
        print_summary(stats)
        
        # Return non-zero exit code if no bots connected
        if not stats:
            sys.exit(1)
            
    except KeyboardInterrupt:
        print("\n\nTest interrupted by user")
        sys.exit(130)


if __name__ == '__main__':
    main()
