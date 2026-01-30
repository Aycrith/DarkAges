#!/usr/bin/env python3
"""
DarkAges MMO - Network Chaos Testing (WP-6-5)

Simulates network problems like latency, packet loss, and jitter
to test server resilience under adverse network conditions.

Requirements:
    - Linux: tc (traffic control) - usually pre-installed
    - macOS: Limited support via pfctl/dnctl
    - Windows: Requires external tool or WSL

Usage:
    # Add 100ms latency with 20ms jitter (realistic WAN)
    sudo python network_chaos.py --latency 100 --jitter 20
    
    # Simulate poor mobile connection
    sudo python network_chaos.py --latency 200 --jitter 50 --loss 3
    
    # Packet loss only
    sudo python network_chaos.py --loss 5
    
    # Add packet duplication and reordering
    sudo python network_chaos.py --latency 100 --duplicate 1 --reorder 0.5
    
    # Run automated chaos test for 5 minutes
    sudo python network_chaos.py --chaos-test 300
    
    # Reset to normal
    sudo python network_chaos.py --reset
    
    # Show current rules
    sudo python network_chaos.py --show

Safety:
    This tool modifies network settings. Always run --reset when done testing.
    Changes are automatically cleared on system reboot.
"""

import argparse
import subprocess
import random
import time
import sys
import os
import signal
import atexit
from dataclasses import dataclass
from typing import Optional, List, Tuple
from enum import Enum


class Platform(Enum):
    """Supported platforms"""
    LINUX = "linux"
    MACOS = "macos"
    WINDOWS = "windows"
    UNKNOWN = "unknown"


@dataclass
class NetworkCondition:
    """Network condition configuration"""
    latency_ms: int = 0
    jitter_ms: int = 0
    loss_percent: float = 0.0
    duplicate_percent: float = 0.0
    reorder_percent: float = 0.0
    corrupt_percent: float = 0.0
    
    def __str__(self):
        parts = []
        if self.latency_ms > 0:
            parts.append(f"{self.latency_ms}ms latency")
        if self.jitter_ms > 0:
            parts.append(f"±{self.jitter_ms}ms jitter")
        if self.loss_percent > 0:
            parts.append(f"{self.loss_percent}% loss")
        if self.duplicate_percent > 0:
            parts.append(f"{self.duplicate_percent}% duplicate")
        if self.reorder_percent > 0:
            parts.append(f"{self.reorder_percent}% reorder")
        if self.corrupt_percent > 0:
            parts.append(f"{self.corrupt_percent}% corrupt")
        return ", ".join(parts) if parts else "normal"


class NetworkChaosController:
    """Controller for network chaos testing"""
    
    # Network profiles for common scenarios
    PROFILES = {
        'lan': NetworkCondition(latency_ms=1),
        'wan-good': NetworkCondition(latency_ms=50, jitter_ms=5),
        'wan-average': NetworkCondition(latency_ms=100, jitter_ms=20, loss_percent=0.5),
        'mobile-4g': NetworkCondition(latency_ms=80, jitter_ms=15, loss_percent=0.3),
        'mobile-3g': NetworkCondition(latency_ms=200, jitter_ms=50, loss_percent=2.0),
        'poor': NetworkCondition(latency_ms=300, jitter_ms=100, loss_percent=5.0),
        'satellite': NetworkCondition(latency_ms=600, jitter_ms=50, loss_percent=1.0),
    }
    
    def __init__(self, interface: str = "lo", port: int = 7777):
        self.interface = interface
        self.port = port
        self.platform = self._detect_platform()
        self.current_condition = NetworkCondition()
        
        # Register cleanup on exit
        atexit.register(self.cleanup)
        signal.signal(signal.SIGINT, self._signal_handler)
        signal.signal(signal.SIGTERM, self._signal_handler)
    
    def _signal_handler(self, signum, frame):
        """Handle cleanup on signal"""
        print("\n[INFO] Received signal, cleaning up...")
        self.cleanup()
        sys.exit(0)
    
    def _detect_platform(self) -> Platform:
        """Detect the current platform"""
        if sys.platform.startswith('linux'):
            return Platform.LINUX
        elif sys.platform == 'darwin':
            return Platform.MACOS
        elif sys.platform == 'win32':
            return Platform.WINDOWS
        return Platform.UNKNOWN
    
    def _run_tc(self, args: List[str]) -> Tuple[bool, str]:
        """Run tc command with arguments"""
        cmd = ['tc'] + args
        try:
            result = subprocess.run(
                cmd, capture_output=True, text=True, check=False
            )
            if result.returncode != 0:
                # Ignore "No such file or directory" errors (rules don't exist)
                if "No such file" in result.stderr or "Cannot delete" in result.stderr:
                    return True, ""
                return False, result.stderr
            return True, result.stdout
        except FileNotFoundError:
            return False, "tc command not found. Install iproute2 package."
        except Exception as e:
            return False, str(e)
    
    def check_prerequisites(self) -> Tuple[bool, str]:
        """Check if prerequisites are met"""
        if self.platform == Platform.LINUX:
            return self._check_linux_prerequisites()
        elif self.platform == Platform.MACOS:
            return self._check_macos_prerequisites()
        elif self.platform == Platform.WINDOWS:
            return False, "Windows not directly supported. Use WSL or Clumsy tool."
        else:
            return False, f"Unsupported platform: {sys.platform}"
    
    def _check_linux_prerequisites(self) -> Tuple[bool, str]:
        """Check Linux prerequisites"""
        # Check for tc
        if not self._command_exists('tc'):
            return False, "tc not found. Install: sudo apt-get install iproute2"
        
        # Check for root/sudo
        if os.geteuid() != 0:
            return False, "Root privileges required. Run with sudo."
        
        return True, "OK"
    
    def _check_macos_prerequisites(self) -> Tuple[bool, str]:
        """Check macOS prerequisites"""
        # macOS has limited support
        if not self._command_exists('pfctl'):
            return False, "pfctl not available"
        
        if os.geteuid() != 0:
            return False, "Root privileges required. Run with sudo."
        
        return True, "macOS support is limited. Only basic rules supported."
    
    def _command_exists(self, cmd: str) -> bool:
        """Check if a command exists"""
        try:
            subprocess.run(['which', cmd], capture_output=True, check=True)
            return True
        except:
            return False
    
    def reset(self) -> bool:
        """Reset network to normal (remove all rules)"""
        if self.platform == Platform.LINUX:
            return self._reset_linux()
        elif self.platform == Platform.MACOS:
            return self._reset_macos()
        return False
    
    def _reset_linux(self) -> bool:
        """Reset Linux network settings"""
        # Remove existing qdisc
        success1, _ = self._run_tc(['qdisc', 'del', 'dev', self.interface, 'root'])
        
        # Also try ingress
        success2, _ = self._run_tc(['qdisc', 'del', 'dev', self.interface, 'ingress'])
        
        self.current_condition = NetworkCondition()
        return True  # Ignore errors (may not exist)
    
    def _reset_macos(self) -> bool:
        """Reset macOS network settings"""
        try:
            # Disable pf
            subprocess.run(['pfctl', '-d'], capture_output=True)
            # Flush rules
            subprocess.run(['pfctl', '-F', 'all'], capture_output=True)
            return True
        except Exception as e:
            print(f"[WARNING] macOS reset failed: {e}")
            return False
    
    def apply_condition(self, condition: NetworkCondition) -> bool:
        """Apply a network condition"""
        print(f"[INFO] Applying: {condition}")
        
        # First reset existing rules
        self.reset()
        
        if self.platform == Platform.LINUX:
            return self._apply_linux(condition)
        elif self.platform == Platform.MACOS:
            return self._apply_macos(condition)
        
        return False
    
    def _apply_linux(self, condition: NetworkCondition) -> bool:
        """Apply condition on Linux using tc"""
        # Build netem options
        netem_opts = []
        
        if condition.latency_ms > 0:
            netem_opts.append(f"{condition.latency_ms}ms")
            if condition.jitter_ms > 0:
                netem_opts[-1] += f" {condition.jitter_ms}ms"
                # Add correlation for more realistic jitter
                netem_opts.append("25%")
        
        if condition.loss_percent > 0:
            netem_opts.append(f"loss {condition.loss_percent}%")
        
        if condition.duplicate_percent > 0:
            netem_opts.append(f"duplicate {condition.duplicate_percent}%")
        
        if condition.reorder_percent > 0:
            # Reordering requires some latency
            if condition.latency_ms == 0:
                netem_opts.insert(0, "100ms")
            netem_opts.append(f"reorder {condition.reorder_percent}%")
        
        if condition.corrupt_percent > 0:
            netem_opts.append(f"corrupt {condition.corrupt_percent}%")
        
        if not netem_opts:
            print("[INFO] No conditions to apply (normal network)")
            return True
        
        # Add netem qdisc
        cmd = ['qdisc', 'add', 'dev', self.interface, 'root', 'netem'] + netem_opts
        success, error = self._run_tc(cmd)
        
        if not success:
            print(f"[ERROR] Failed to apply: {error}")
            return False
        
        self.current_condition = condition
        print(f"[OK] Applied to interface {self.interface}")
        return True
    
    def _apply_macos(self, condition: NetworkCondition) -> bool:
        """Apply condition on macOS (limited support)"""
        print("[WARNING] macOS support is limited. Consider using Linux VM or Clumsy on Windows.")
        # macOS implementation would use pfctl/dnctl - complex and limited
        return False
    
    def show_rules(self) -> None:
        """Display current tc rules"""
        print(f"\nCurrent rules for {self.interface}:")
        print("-" * 50)
        
        success, output = self._run_tc(['qdisc', 'show', 'dev', self.interface])
        if output:
            print(output)
        else:
            print("No rules applied (normal network)")
        
        print("-" * 50)
    
    def cleanup(self) -> None:
        """Cleanup on exit"""
        if self.current_condition != NetworkCondition():
            print("\n[INFO] Cleaning up network rules...")
            self.reset()
    
    def run_chaos_test(self, duration_seconds: int, interval_seconds: int = 30) -> None:
        """
        Run an automated chaos test that cycles through conditions.
        
        Args:
            duration_seconds: Total test duration
            interval_seconds: Seconds between condition changes
        """
        print("=" * 60)
        print("DARKAGES NETWORK CHAOS TEST")
        print("=" * 60)
        print(f"Duration: {duration_seconds}s")
        print(f"Change interval: {interval_seconds}s")
        print(f"Interface: {self.interface}")
        print("-" * 60)
        
        conditions = [
            NetworkCondition(latency_ms=0),  # Normal
            NetworkCondition(latency_ms=50, jitter_ms=5),  # Good WAN
            NetworkCondition(latency_ms=100, jitter_ms=20, loss_percent=0.5),  # Average
            NetworkCondition(latency_ms=200, jitter_ms=50, loss_percent=2.0),  # Poor
            NetworkCondition(loss_percent=5.0),  # Loss only
            NetworkCondition(latency_ms=300, loss_percent=3.0),  # High latency + loss
        ]
        
        start_time = time.time()
        iteration = 0
        
        try:
            while time.time() - start_time < duration_seconds:
                # Select condition (cycle through or random)
                condition = conditions[iteration % len(conditions)]
                
                # Random variation
                varied_condition = NetworkCondition(
                    latency_ms=max(0, condition.latency_ms + random.randint(-20, 20)),
                    jitter_ms=max(0, condition.jitter_ms + random.randint(-5, 5)),
                    loss_percent=max(0.0, condition.loss_percent + random.uniform(-0.5, 0.5))
                )
                
                elapsed = time.time() - start_time
                remaining = duration_seconds - elapsed
                
                print(f"\n[{elapsed:.0f}s / {duration_seconds}s] {varied_condition}")
                self.apply_condition(varied_condition)
                
                # Sleep for interval or remaining time
                sleep_time = min(interval_seconds, remaining)
                time.sleep(sleep_time)
                
                iteration += 1
            
            print("\n[OK] Chaos test complete")
            
        except KeyboardInterrupt:
            print("\n[INFO] Chaos test interrupted")
        finally:
            self.reset()
    
    def run_latency_test(self) -> None:
        """Run systematic latency test"""
        print("=" * 60)
        print("LATENCY TEST")
        print("=" * 60)
        
        latencies = [0, 50, 100, 200, 500]
        
        for latency in latencies:
            print(f"\nTesting with {latency}ms latency...")
            self.apply_condition(NetworkCondition(latency_ms=latency))
            print(f"Hold for 10 seconds (Ctrl+C to skip)...")
            try:
                time.sleep(10)
            except KeyboardInterrupt:
                pass
        
        self.reset()
        print("\n[OK] Latency test complete")
    
    def run_packet_loss_test(self) -> None:
        """Run systematic packet loss test"""
        print("=" * 60)
        print("PACKET LOSS TEST")
        print("=" * 60)
        
        loss_rates = [0, 1, 3, 5, 10]
        
        for loss in loss_rates:
            print(f"\nTesting with {loss}% packet loss...")
            self.apply_condition(NetworkCondition(loss_percent=loss))
            print(f"Hold for 10 seconds (Ctrl+C to skip)...")
            try:
                time.sleep(10)
            except KeyboardInterrupt:
                pass
        
        self.reset()
        print("\n[OK] Packet loss test complete")


def main():
    parser = argparse.ArgumentParser(
        description="DarkAges Network Chaos Testing (WP-6-5)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Add 100ms latency with 20ms jitter
    sudo python network_chaos.py --latency 100 --jitter 20
    
    # Poor mobile connection simulation
    sudo python network_chaos.py --latency 200 --jitter 50 --loss 3
    
    # Use predefined profile
    sudo python network_chaos.py --profile mobile-3g
    
    # Automated chaos test for 5 minutes
    sudo python network_chaos.py --chaos-test 300
    
    # Reset network to normal
    sudo python network_chaos.py --reset
    
    # Show current rules
    sudo python network_chaos.py --show

Profiles:
    lan          - 1ms latency (local testing)
    wan-good     - 50ms latency, 5ms jitter
    wan-average  - 100ms latency, 20ms jitter, 0.5% loss
    mobile-4g    - 80ms latency, 15ms jitter, 0.3% loss
    mobile-3g    - 200ms latency, 50ms jitter, 2% loss
    poor         - 300ms latency, 100ms jitter, 5% loss
    satellite    - 600ms latency, 50ms jitter, 1% loss

Safety:
    Always run --reset when done testing.
    Rules are automatically cleared on system reboot.
        """
    )
    
    parser.add_argument('--latency', type=int, default=0,
                       help='Add latency in milliseconds')
    parser.add_argument('--jitter', type=int, default=0,
                       help='Add jitter (variance) to latency')
    parser.add_argument('--loss', type=float, default=0.0,
                       help='Add packet loss percentage')
    parser.add_argument('--duplicate', type=float, default=0.0,
                       help='Add packet duplication percentage')
    parser.add_argument('--reorder', type=float, default=0.0,
                       help='Add packet reordering percentage')
    parser.add_argument('--corrupt', type=float, default=0.0,
                       help='Add packet corruption percentage')
    parser.add_argument('--profile', type=str,
                       choices=list(NetworkChaosController.PROFILES.keys()),
                       help='Use predefined network profile')
    parser.add_argument('--interface', type=str, default='lo',
                       help='Network interface (default: lo for loopback)')
    parser.add_argument('--port', type=int, default=7777,
                       help='Target port (default: 7777)')
    parser.add_argument('--reset', action='store_true',
                       help='Reset network to normal')
    parser.add_argument('--show', action='store_true',
                       help='Show current rules')
    parser.add_argument('--chaos-test', type=int, metavar='SECONDS',
                       help='Run automated chaos test for N seconds')
    parser.add_argument('--latency-test', action='store_true',
                       help='Run systematic latency test')
    parser.add_argument('--loss-test', action='store_true',
                       help='Run systematic packet loss test')
    
    args = parser.parse_args()
    
    # Create controller
    controller = NetworkChaosController(
        interface=args.interface,
        port=args.port
    )
    
    # Check prerequisites
    ok, message = controller.check_prerequisites()
    if not ok:
        print(f"[ERROR] {message}")
        sys.exit(1)
    elif message != "OK":
        print(f"[WARNING] {message}")
    
    # Execute command
    if args.reset:
        controller.reset()
        print("[OK] Network reset to normal")
        
    elif args.show:
        controller.show_rules()
        
    elif args.chaos_test:
        controller.run_chaos_test(args.chaos_test)
        
    elif args.latency_test:
        controller.run_latency_test()
        
    elif args.loss_test:
        controller.run_packet_loss_test()
        
    elif args.profile:
        condition = NetworkChaosController.PROFILES[args.profile]
        controller.apply_condition(condition)
        print(f"\n[OK] Applied profile: {args.profile}")
        print(f"Settings: {condition}")
        print("\nRun with --reset to restore normal network")
        
    elif any([args.latency, args.jitter, args.loss, args.duplicate, args.reorder, args.corrupt]):
        condition = NetworkCondition(
            latency_ms=args.latency,
            jitter_ms=args.jitter,
            loss_percent=args.loss,
            duplicate_percent=args.duplicate,
            reorder_percent=args.reorder,
            corrupt_percent=args.corrupt
        )
        controller.apply_condition(condition)
        print("\nRun with --reset to restore normal network")
        
    else:
        parser.print_help()
        print("\n[ERROR] No action specified. Use --help for usage.")


if __name__ == "__main__":
    main()
