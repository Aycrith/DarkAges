#!/usr/bin/env python3
"""
Multi-player synchronization integration test.
Verifies that multiple players can connect, move, and receive state updates.

This test validates:
- All bots can connect to server
- All bots receive periodic snapshots
- Bandwidth usage is within acceptable limits
- No excessive server corrections (indicates prediction working)

Exit codes:
    0 - All tests passed
    1 - One or more tests failed
    2 - No bots could connect (server not running?)
"""

import asyncio
import sys
import argparse
from typing import List, Dict, Tuple

# Import from bot_swarm module
from bot_swarm import BotSwarm, DEFAULT_SERVER_PORT, TICK_RATE_HZ, SNAPSHOT_RATE_HZ
from bot_swarm import MAX_UPSTREAM_BYTES_PER_SEC, MAX_DOWNSTREAM_BYTES_PER_SEC


class MultiplayerTest:
    """Automated multiplayer synchronization test suite"""
    
    def __init__(self, host: str, port: int, bot_count: int, duration: float):
        self.host = host
        self.port = port
        self.bot_count = bot_count
        self.duration = duration
        self.stats: List[Dict] = []
        self.results: List[Tuple[str, bool, str]] = []  # (test_name, passed, details)
    
    async def run(self) -> int:
        """Run all tests and return exit code"""
        print("="*70)
        print("DARKAGES MULTI-PLAYER SYNCHRONIZATION TEST")
        print("="*70)
        print(f"Configuration:")
        print(f"  Server: {self.host}:{self.port}")
        print(f"  Bots: {self.bot_count}")
        print(f"  Duration: {self.duration}s")
        print(f"  Expected snapshot rate: {SNAPSHOT_RATE_HZ}Hz")
        print("="*70)
        print()
        
        # Run bot swarm
        swarm = BotSwarm(self.host, self.port, self.bot_count, self.duration)
        self.stats = await swarm.run()
        
        if not self.stats:
            print("\n[CRITICAL] No bots could connect to server!")
            print("Please ensure:")
            print("  1. The DarkAges server is running")
            print(f"  2. The server is listening on {self.host}:{self.port}")
            print("  3. No firewall is blocking UDP traffic")
            return 2
        
        # Run validation tests
        self._test_connection_rate()
        self._test_snapshot_reception()
        self._test_bandwidth_limits()
        self._test_packet_integrity()
        self._test_prediction_accuracy()
        self._test_input_acknowledgment()
        
        # Print summary
        return self._print_results()
    
    def _test_connection_rate(self) -> None:
        """Test that all bots successfully connected"""
        connected = len(self.stats)
        target = self.bot_count
        ratio = connected / target
        
        # Require 90% connection rate
        passed = ratio >= 0.9
        
        details = f"{connected}/{target} bots connected ({ratio*100:.1f}%)"
        if not passed:
            details += f" - Expected >= 90%"
        
        self.results.append(("Connection Rate", passed, details))
    
    def _test_snapshot_reception(self) -> None:
        """Test that bots receive regular snapshots"""
        if not self.stats:
            self.results.append(("Snapshot Reception", False, "No stats available"))
            return
        
        # Expected snapshots over test duration
        expected_snapshots = SNAPSHOT_RATE_HZ * self.duration
        min_acceptable = expected_snapshots * 0.8  # 80% threshold
        
        bots_passed = 0
        for stat in self.stats:
            if stat['snapshots_received'] >= min_acceptable:
                bots_passed += 1
        
        ratio = bots_passed / len(self.stats)
        passed = ratio >= 0.9  # 90% of bots must pass
        
        avg_snapshots = sum(s['snapshots_received'] for s in self.stats) / len(self.stats)
        actual_rate = avg_snapshots / self.duration
        
        details = (f"{bots_passed}/{len(self.stats)} bots received >= {min_acceptable:.0f} snapshots "
                   f"(avg rate: {actual_rate:.1f}/sec)")
        
        self.results.append(("Snapshot Reception", passed, details))
    
    def _test_bandwidth_limits(self) -> None:
        """Test that bandwidth usage is within budget"""
        if not self.stats:
            self.results.append(("Bandwidth Limits", False, "No stats available"))
            return
        
        # Check upstream (upload from bot perspective = server download)
        up_violations = 0
        down_violations = 0
        
        for stat in self.stats:
            bytes_up_per_sec = stat['bytes_sent'] / stat['duration_seconds']
            bytes_down_per_sec = stat['bytes_received'] / stat['duration_seconds']
            
            if bytes_up_per_sec > MAX_UPSTREAM_BYTES_PER_SEC:
                up_violations += 1
            if bytes_down_per_sec > MAX_DOWNSTREAM_BYTES_PER_SEC:
                down_violations += 1
        
        up_passed = up_violations == 0
        down_passed = down_violations == 0
        
        avg_up = sum(s['bytes_sent'] / s['duration_seconds'] for s in self.stats) / len(self.stats)
        avg_down = sum(s['bytes_received'] / s['duration_seconds'] for s in self.stats) / len(self.stats)
        
        up_details = f"Upload: {avg_up:.0f} bytes/s (limit: {MAX_UPSTREAM_BYTES_PER_SEC})"
        if not up_passed:
            up_details += f", {up_violations} bots exceeded"
        
        down_details = f"Download: {avg_down:.0f} bytes/s (limit: {MAX_DOWNSTREAM_BYTES_PER_SEC})"
        if not down_passed:
            down_details += f", {down_violations} bots exceeded"
        
        self.results.append(("Bandwidth Upload", up_passed, up_details))
        self.results.append(("Bandwidth Download", down_passed, down_details))
    
    def _test_packet_integrity(self) -> None:
        """Test that packets are being sent and received"""
        if not self.stats:
            self.results.append(("Packet Integrity", False, "No stats available"))
            return
        
        # Check for bots with zero packets received (possible firewall issue)
        no_receive = [s for s in self.stats if s['packets_received'] == 0]
        no_send = [s for s in self.stats if s['packets_sent'] == 0]
        
        passed = len(no_receive) == 0 and len(no_send) == 0
        
        total_sent = sum(s['packets_sent'] for s in self.stats)
        total_received = sum(s['packets_received'] for s in self.stats)
        
        details = f"Total sent: {total_sent}, received: {total_received}"
        if no_receive:
            details += f", {len(no_receive)} bots received no packets"
        if no_send:
            details += f", {len(no_send)} bots sent no packets"
        
        self.results.append(("Packet Integrity", passed, details))
    
    def _test_prediction_accuracy(self) -> None:
        """Test that client prediction is working (few corrections)"""
        if not self.stats:
            self.results.append(("Prediction Accuracy", False, "No stats available"))
            return
        
        # Corrections indicate prediction errors
        # Some corrections are normal (startup, collisions), but excessive indicates issues
        total_corrections = sum(s['corrections_received'] for s in self.stats)
        total_inputs = sum(s['packets_sent'] for s in self.stats)
        
        if total_inputs == 0:
            correction_rate = 0
        else:
            correction_rate = total_corrections / total_inputs
        
        # Acceptable: < 5% of inputs result in corrections
        passed = correction_rate < 0.05
        
        details = f"{total_corrections} corrections for {total_inputs} inputs ({correction_rate*100:.2f}%)"
        
        self.results.append(("Prediction Accuracy", passed, details))
    
    def _test_input_acknowledgment(self) -> None:
        """Test that server acknowledges inputs via last_processed_input"""
        if not self.stats:
            self.results.append(("Input Acknowledgment", False, "No stats available"))
            return
        
        # Check that last_processed_input increases over time
        bots_with_ack = sum(1 for s in self.stats if s['last_processed_input'] > 0)
        
        # All connected bots should have some inputs acknowledged
        passed = bots_with_ack == len(self.stats)
        
        avg_last_ack = sum(s['last_processed_input'] for s in self.stats) / len(self.stats)
        avg_inputs = sum(s['packets_sent'] for s in self.stats) / len(self.stats)
        
        ack_ratio = avg_last_ack / avg_inputs if avg_inputs > 0 else 0
        
        details = f"{bots_with_ack}/{len(self.stats)} bots received acks (avg ack ratio: {ack_ratio*100:.1f}%)"
        
        self.results.append(("Input Acknowledgment", passed, details))
    
    def _print_results(self) -> int:
        """Print test results and return exit code"""
        print("\n" + "="*70)
        print("TEST RESULTS")
        print("="*70)
        
        passed_count = sum(1 for _, passed, _ in self.results if passed)
        total_count = len(self.results)
        
        for test_name, passed, details in self.results:
            status = "PASS" if passed else "FAIL"
            print(f"  [{status}] {test_name:25s} - {details}")
        
        print("-"*70)
        
        if passed_count == total_count:
            print(f"RESULT: ALL TESTS PASSED ({passed_count}/{total_count})")
            print("="*70)
            return 0
        else:
            print(f"RESULT: {total_count - passed_count} TEST(S) FAILED ({passed_count}/{total_count} passed)")
            print("="*70)
            return 1


async def test_multiplayer_sync(host: str, port: int, bots: int, duration: float) -> int:
    """Main test entry point"""
    test = MultiplayerTest(host, port, bots, duration)
    return await test.run()


def main():
    parser = argparse.ArgumentParser(
        description='DarkAges Multi-Player Synchronization Test',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
This test validates that the DarkAges server correctly handles multiple
simultaneous connections and provides state updates to all clients.

The test will:
  1. Connect N bots to the server
  2. Send inputs at 60Hz for the specified duration
  3. Verify all bots receive state snapshots
  4. Check bandwidth usage is within budget
  5. Report success/failure

Exit codes:
  0 - All tests passed
  1 - One or more tests failed
  2 - Could not connect to server

Examples:
    # Standard 10-player test for 30 seconds
    python test_multiplayer.py
    
    # Quick 5-player smoke test
    python test_multiplayer.py --bots 5 --duration 10
    
    # Stress test with 100 players
    python test_multiplayer.py --bots 100 --duration 120
    
    # Test remote server
    python test_multiplayer.py --host 192.168.1.100 --port 7777
        """
    )
    parser.add_argument('--host', default='127.0.0.1',
                        help='Server host (default: 127.0.0.1)')
    parser.add_argument('--port', type=int, default=DEFAULT_SERVER_PORT,
                        help=f'Server port (default: {DEFAULT_SERVER_PORT})')
    parser.add_argument('--bots', type=int, default=10,
                        help='Number of bots to simulate (default: 10)')
    parser.add_argument('--duration', type=float, default=30.0,
                        help='Test duration in seconds (default: 30)')
    
    args = parser.parse_args()
    
    try:
        exit_code = asyncio.run(test_multiplayer_sync(
            args.host, args.port, args.bots, args.duration
        ))
        sys.exit(exit_code)
        
    except KeyboardInterrupt:
        print("\n\nTest interrupted by user")
        sys.exit(130)


if __name__ == '__main__':
    main()
