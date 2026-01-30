#!/usr/bin/env python3
"""
[NETWORK_AGENT] GameNetworkingSockets 1000 Connection Stress Test - WP-6-1
Tests server capacity for 1000 concurrent connections.

Usage:
    python test_1000_connections.py [--host HOST] [--port PORT] [--duration SECONDS]

Requirements:
    - Server running on target host/port
    - game-networking-sockets Python bindings (or use subprocess to C++ client)

Note: This is a template. Full implementation requires GNS client library.
"""

import argparse
import asyncio
import sys
import time
import random
import statistics
from dataclasses import dataclass
from typing import List, Optional


@dataclass
class ConnectionStats:
    """Statistics for a single connection"""
    connection_id: int
    connected_at: float
    disconnected_at: Optional[float] = None
    bytes_sent: int = 0
    bytes_received: int = 0
    packets_sent: int = 0
    packets_received: int = 0
    rtt_samples: List[float] = None
    
    def __post_init__(self):
        if self.rtt_samples is None:
            self.rtt_samples = []
    
    @property
    def duration(self) -> float:
        end = self.disconnected_at or time.time()
        return end - self.connected_at
    
    @property
    def avg_rtt(self) -> float:
        return statistics.mean(self.rtt_samples) if self.rtt_samples else 0.0
    
    @property
    def max_rtt(self) -> float:
        return max(self.rtt_samples) if self.rtt_samples else 0.0


class StressTestClient:
    """Simulates a game client for stress testing"""
    
    def __init__(self, host: str, port: int, client_id: int):
        self.host = host
        self.port = port
        self.client_id = client_id
        self.stats = ConnectionStats(connection_id=client_id, connected_at=0)
        self.running = False
        self.sequence = 0
        
    async def connect(self) -> bool:
        """Connect to server"""
        # Placeholder for actual GNS connection
        # In production: use game-networking-sockets Python bindings
        # or subprocess to a C++ test client
        print(f"[Client {self.client_id}] Connecting to {self.host}:{self.port}...")
        
        # Simulate connection delay
        await asyncio.sleep(random.uniform(0.01, 0.1))
        
        self.stats.connected_at = time.time()
        self.running = True
        return True
    
    async def run(self, duration: float):
        """Run the client for specified duration"""
        if not self.running:
            return
            
        end_time = time.time() + duration
        
        # Simulate game loop at 60Hz
        while time.time() < end_time and self.running:
            await self.send_input()
            await self.receive_snapshot()
            
            # Sample RTT periodically
            if self.sequence % 60 == 0:
                rtt = await self.measure_rtt()
                self.stats.rtt_samples.append(rtt)
            
            self.sequence += 1
            await asyncio.sleep(1/60)  # 60Hz
        
        self.disconnect()
    
    async def send_input(self):
        """Send input packet to server"""
        # Placeholder: Send ClientInput packet
        packet_size = 17  # As defined in Protocol::serializeInput
        self.stats.bytes_sent += packet_size
        self.stats.packets_sent += 1
    
    async def receive_snapshot(self):
        """Receive snapshot from server"""
        # Placeholder: Receive ServerSnapshot packet
        # Snapshot size varies based on entity count
        snapshot_size = random.randint(100, 1500)
        self.stats.bytes_received += snapshot_size
        self.stats.packets_received += 1
    
    async def measure_rtt(self) -> float:
        """Measure round-trip time via ping"""
        # Placeholder: Send Ping, wait for response
        # Simulate LAN RTT: 10-50ms
        rtt = random.uniform(10, 50)
        await asyncio.sleep(rtt / 1000)
        return rtt
    
    def disconnect(self, reason: str = "Test complete"):
        """Disconnect from server"""
        if self.running:
            print(f"[Client {self.client_id}] Disconnecting: {reason}")
            self.stats.disconnected_at = time.time()
            self.running = False


class StressTest:
    """Manages multiple concurrent connections"""
    
    def __init__(self, host: str, port: int, num_connections: int, duration: float):
        self.host = host
        self.port = port
        self.num_connections = num_connections
        self.duration = duration
        self.clients: List[StressTestClient] = []
        self.start_time: float = 0
        
    async def run(self) -> bool:
        """Run the stress test"""
        print(f"\n{'='*60}")
        print(f"GameNetworkingSockets Stress Test - WP-6-1")
        print(f"{'='*60}")
        print(f"Target: {self.host}:{self.port}")
        print(f"Connections: {self.num_connections}")
        print(f"Duration: {self.duration} seconds")
        print(f"{'='*60}\n")
        
        self.start_time = time.time()
        
        # Create clients
        print(f"Creating {self.num_connections} clients...")
        for i in range(self.num_connections):
            client = StressTestClient(self.host, self.port, i)
            self.clients.append(client)
        
        # Connect all clients (batch to avoid overwhelming server)
        print("Connecting clients in batches...")
        batch_size = 100
        connected = 0
        
        for i in range(0, len(self.clients), batch_size):
            batch = self.clients[i:i + batch_size]
            tasks = [c.connect() for c in batch]
            results = await asyncio.gather(*tasks, return_exceptions=True)
            
            for result in results:
                if result is True:
                    connected += 1
            
            print(f"  Batch {i//batch_size + 1}: {sum(1 for r in results if r is True)} connected")
            await asyncio.sleep(0.5)  # Brief pause between batches
        
        print(f"\nTotal connected: {connected}/{self.num_connections}")
        
        if connected < self.num_connections * 0.95:  # Require 95% connection rate
            print("ERROR: Failed to connect sufficient clients")
            return False
        
        # Run all clients
        print(f"\nRunning test for {self.duration} seconds...")
        tasks = [c.run(self.duration) for c in self.clients]
        await asyncio.gather(*tasks, return_exceptions=True)
        
        # Print results
        self.print_results()
        
        return self.verify_requirements()
    
    def print_results(self):
        """Print test results"""
        elapsed = time.time() - self.start_time
        
        print(f"\n{'='*60}")
        print(f"Test Results")
        print(f"{'='*60}")
        print(f"Total duration: {elapsed:.2f}s")
        
        # Connection stats
        active = sum(1 for c in self.clients if c.stats.disconnected_at is None)
        disconnected = len(self.clients) - active
        
        print(f"\nConnection Stats:")
        print(f"  Total clients: {len(self.clients)}")
        print(f"  Still active: {active}")
        print(f"  Disconnected: {disconnected}")
        
        # Traffic stats
        total_sent = sum(c.stats.bytes_sent for c in self.clients)
        total_received = sum(c.stats.bytes_received for c in self.clients)
        total_packets_sent = sum(c.stats.packets_sent for c in self.clients)
        total_packets_received = sum(c.stats.packets_received for c in self.clients)
        
        print(f"\nTraffic Stats:")
        print(f"  Total sent: {total_sent / 1024 / 1024:.2f} MB")
        print(f"  Total received: {total_received / 1024 / 1024:.2f} MB")
        print(f"  Packets sent: {total_packets_sent:,}")
        print(f"  Packets received: {total_packets_received:,}")
        
        # RTT stats
        all_rtts = []
        for c in self.clients:
            all_rtts.extend(c.stats.rtt_samples)
        
        if all_rtts:
            print(f"\nRTT Stats:")
            print(f"  Samples: {len(all_rtts)}")
            print(f"  Min: {min(all_rtts):.2f}ms")
            print(f"  Max: {max(all_rtts):.2f}ms")
            print(f"  Avg: {statistics.mean(all_rtts):.2f}ms")
            print(f"  P95: {sorted(all_rtts)[int(len(all_rtts)*0.95)]:.2f}ms")
        
        # Bandwidth per player
        avg_duration = statistics.mean(c.stats.duration for c in self.clients)
        if avg_duration > 0:
            downstream_per_player = (total_received / len(self.clients)) / avg_duration
            upstream_per_player = (total_sent / len(self.clients)) / avg_duration
            
            print(f"\nBandwidth per Player:")
            print(f"  Downstream: {downstream_per_player:.2f} bytes/s "
                  f"({downstream_per_player/1024:.2f} KB/s)")
            print(f"  Upstream: {upstream_per_player:.2f} bytes/s "
                  f"({upstream_per_player/1024:.2f} KB/s)")
    
    def verify_requirements(self) -> bool:
        """Verify test meets quality gate requirements"""
        print(f"\n{'='*60}")
        print(f"Quality Gate Verification")
        print(f"{'='*60}")
        
        passed = True
        
        # Requirement 1: 1000 concurrent connections
        active = sum(1 for c in self.clients if c.stats.disconnected_at is None)
        if active >= 1000:
            print(f"[PASS] Concurrent connections: {active} >= 1000")
        else:
            print(f"[FAIL] Concurrent connections: {active} < 1000")
            passed = False
        
        # Requirement 2: RTT < 50ms on LAN
        all_rtts = []
        for c in self.clients:
            all_rtts.extend(c.stats.rtt_samples)
        
        if all_rtts:
            avg_rtt = statistics.mean(all_rtts)
            if avg_rtt < 50:
                print(f"[PASS] Average RTT: {avg_rtt:.2f}ms < 50ms")
            else:
                print(f"[FAIL] Average RTT: {avg_rtt:.2f}ms >= 50ms")
                passed = False
        
        # Requirement 3: Bandwidth budget
        total_received = sum(c.stats.bytes_received for c in self.clients)
        avg_duration = statistics.mean(c.stats.duration for c in self.clients)
        if avg_duration > 0:
            downstream_per_player = (total_received / len(self.clients)) / avg_duration
            if downstream_per_player <= 20 * 1024:  # 20 KB/s
                print(f"[PASS] Downstream per player: {downstream_per_player/1024:.2f} KB/s <= 20 KB/s")
            else:
                print(f"[FAIL] Downstream per player: {downstream_per_player/1024:.2f} KB/s > 20 KB/s")
                passed = False
        
        print(f"{'='*60}")
        print(f"Overall: {'PASS' if passed else 'FAIL'}")
        print(f"{'='*60}")
        
        return passed


def main():
    parser = argparse.ArgumentParser(description="GameNetworkingSockets 1000 Connection Stress Test")
    parser.add_argument("--host", default="127.0.0.1", help="Server host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=7777, help="Server port (default: 7777)")
    parser.add_argument("--connections", type=int, default=1000, help="Number of connections (default: 1000)")
    parser.add_argument("--duration", type=float, default=60.0, help="Test duration in seconds (default: 60)")
    
    args = parser.parse_args()
    
    # Run the test
    test = StressTest(args.host, args.port, args.connections, args.duration)
    success = asyncio.run(test.run())
    
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
