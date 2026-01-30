#!/usr/bin/env python3
"""
Network latency/packet loss simulator for testing game behavior under poor network conditions.

Uses platform-specific tools:
- Linux: tc (traffic control) with netem
- Windows: netsh (limited functionality) or requires external tools like Clumsy
- macOS: pfctl and dummynet (limited functionality)

Usage:
    # Add 100ms latency with 20ms jitter
    sudo python latency_simulator.py --latency 100 --jitter 20
    
    # Add packet loss
    sudo python latency_simulator.py --latency 100 --loss 5
    
    # Reset (remove all rules)
    sudo python latency_simulator.py --reset

Note: This tool requires administrator/root privileges to modify network settings.
"""

import argparse
import subprocess
import sys
import platform
import shutil
from typing import Optional


def run_command(cmd: list, check: bool = False) -> tuple:
    """Run a command and return (returncode, stdout, stderr)"""
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, check=check)
        return result.returncode, result.stdout, result.stderr
    except Exception as e:
        return -1, "", str(e)


def is_admin() -> bool:
    """Check if running with administrator/root privileges"""
    try:
        import ctypes
        return ctypes.windll.shell32.IsUserAnAdmin()
    except:
        import os
        return os.getuid() == 0


class LinuxNetworkSimulator:
    """Linux network simulation using tc/netem"""
    
    def __init__(self, interface: str):
        self.interface = interface
        self.tc_path = shutil.which('tc')
        
    def is_available(self) -> bool:
        return self.tc_path is not None
    
    def reset(self) -> bool:
        """Remove all tc rules from interface"""
        # Delete existing qdisc if present (ignore errors if none exists)
        ret, _, stderr = run_command(['sudo', self.tc_path, 'qdisc', 'del', 
                                       'dev', self.interface, 'root'])
        # tc returns 2 if no qdisc exists, which is fine
        if ret != 0 and ret != 2:
            print(f"Warning during reset: {stderr}")
        print(f"Network simulation cleared on {self.interface}")
        return True
    
    def simulate(self, latency_ms: int, jitter_ms: int, loss_percent: float,
                 correlation: float = 0.0, duplicate_percent: float = 0.0,
                 reorder_percent: float = 0.0, limit: int = 1000) -> bool:
        """Apply network simulation rules"""
        
        # First clear existing rules
        self.reset()
        
        # Build tc command
        cmd = [
            'sudo', self.tc_path, 'qdisc', 'add', 'dev', self.interface, 
            'root', 'netem'
        ]
        
        # Add latency with optional jitter
        if jitter_ms > 0:
            cmd.extend(['delay', f'{latency_ms}ms', f'{jitter_ms}ms'])
        else:
            cmd.extend(['delay', f'{latency_ms}ms'])
        
        # Add correlation for more realistic jitter (if specified)
        if correlation > 0:
            cmd.append(f'{correlation}%')
        
        # Add packet loss
        if loss_percent > 0:
            cmd.extend(['loss', f'{loss_percent}%'])
        
        # Add packet duplication
        if duplicate_percent > 0:
            cmd.extend(['duplicate', f'{duplicate_percent}%'])
        
        # Add packet reordering
        if reorder_percent > 0:
            cmd.extend(['reorder', f'{reorder_percent}%'])
        
        # Add queue limit (prevents buffer bloat)
        cmd.extend(['limit', str(limit)])
        
        # Execute command
        ret, stdout, stderr = run_command(cmd)
        
        if ret != 0:
            print(f"Failed to apply network simulation: {stderr}")
            return False
        
        return True
    
    def show(self) -> None:
        """Display current tc rules"""
        ret, stdout, stderr = run_command([self.tc_path, 'qdisc', 'show', 
                                           'dev', self.interface])
        if ret == 0:
            print(f"Current rules on {self.interface}:")
            print(stdout)
        else:
            print(f"No active rules on {self.interface}")


class WindowsNetworkSimulator:
    """Windows network simulation using netsh (limited functionality)"""
    
    def __init__(self):
        self.netsh_path = shutil.which('netsh')
    
    def is_available(self) -> bool:
        return self.netsh_path is not None
    
    def reset(self) -> bool:
        """Reset Windows QoS policies"""
        print("Note: Windows netsh has limited network simulation capabilities.")
        print("For full latency/packet loss simulation, consider using Clumsy:")
        print("  https://jagt.github.io/clumsy/")
        
        # List and remove any existing QoS policies
        ret, stdout, _ = run_command(['netsh', 'qos', 'show', 'policy'])
        if ret == 0 and stdout:
            print("Existing QoS policies found. Manual removal may be needed.")
        
        return True
    
    def simulate(self, latency_ms: int, jitter_ms: int, loss_percent: float, **kwargs) -> bool:
        """Apply Windows QoS policy (very limited)"""
        print("Windows netsh does not support latency/jitter simulation.")
        print("Recommended alternatives:")
        print("  1. Clumsy - https://jagt.github.io/clumsy/ (GUI tool, recommended)")
        print("  2. WinDivert - https://reqrypt.github.io/windivert/ (programmatic)")
        print("  3. VM network adapters - Configure in Hyper-V/VMware/VirtualBox")
        print()
        print("Clumsy setup for {}ms latency:".format(latency_ms))
        print("  1. Download and run clumsy.exe")
        print("  2. Set 'Filtering' to 'udp.DstPort == 7777 or udp.SrcPort == 7777'")
        print("  3. Check 'Lag' and set to {}ms".format(latency_ms))
        if jitter_ms > 0:
            print("  4. Set 'Jitter' to {}ms".format(jitter_ms))
        if loss_percent > 0:
            print("  5. Check 'Drop' and set to {}%".format(loss_percent))
        print("  6. Click 'Start'")
        return False


class MacOSNetworkSimulator:
    """macOS network simulation using pfctl and dummynet"""
    
    def __init__(self, interface: str):
        self.interface = interface
        self.pipe_number = 1  # dummynet pipe number
    
    def is_available(self) -> bool:
        return shutil.which('pfctl') is not None and shutil.which('dnctl') is not None
    
    def reset(self) -> bool:
        """Remove all dummynet rules"""
        run_command(['sudo', 'dnctl', 'pipe', 'delete', str(self.pipe_number)])
        run_command(['sudo', 'pfctl', '-f', '/etc/pf.conf'])  # Reset to default
        print("Network simulation cleared")
        return True
    
    def simulate(self, latency_ms: int, jitter_ms: int, loss_percent: float, **kwargs) -> bool:
        """Apply dummynet rules for network simulation"""
        
        # macOS network simulation requires complex pfctl + dummynet setup
        # This is a simplified version
        
        print("Note: macOS network simulation requires complex pfctl/dummynet configuration.")
        print("Basic setup commands:")
        print()
        print(f"# Create dummynet pipe with {latency_ms}ms delay:")
        print(f"sudo dnctl pipe {self.pipe_number} config delay {latency_ms}ms")
        if loss_percent > 0:
            print(f"sudo dnctl pipe {self.pipe_number} config plr {loss_percent/100}")
        print()
        print("# Add pf rule to route traffic through pipe:")
        print(f"echo 'dummynet in quick proto udp from any to any port 7777 pipe {self.pipe_number}' | \\")
        print("  sudo pfctl -f -")
        print()
        print("# Enable pf:")
        print("sudo pfctl -e")
        print()
        print("For easier network simulation on macOS, consider:")
        print("  - Network Link Conditioner (Apple Developer Tools)")
        print("  - Clumsy (if running in a VM)")
        return False


def get_default_interface() -> str:
    """Try to determine the default network interface"""
    system = platform.system()
    
    if system == 'Linux':
        # Try to find default route interface
        ret, stdout, _ = run_command(['ip', 'route', 'show', 'default'])
        if ret == 0 and stdout:
            parts = stdout.split()
            if 'dev' in parts:
                return parts[parts.index('dev') + 1]
        return 'eth0'  # Fallback
    
    elif system == 'Darwin':  # macOS
        ret, stdout, _ = run_command(['route', '-n', 'get', 'default'])
        if ret == 0 and stdout:
            for line in stdout.split('\n'):
                if 'interface:' in line:
                    return line.split(':')[1].strip()
        return 'en0'  # Fallback
    
    return 'lo'  # Loopback fallback


def main():
    parser = argparse.ArgumentParser(
        description='Network Condition Simulator for DarkAges Testing',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Simulate 100ms latency with 20ms jitter (realistic WAN)
    sudo python latency_simulator.py --latency 100 --jitter 20
    
    # Simulate poor mobile connection (high latency + packet loss)
    sudo python latency_simulator.py --latency 200 --jitter 50 --loss 3
    
    # Simulate very bad connection (for stress testing)
    sudo python latency_simulator.py --latency 300 --loss 10 --duplicate 1
    
    # Reset network to normal
    sudo python latency_simulator.py --reset

Platform Notes:
    Linux: Full support via tc/netem (requires root)
    Windows: Limited support - recommends Clumsy tool
    macOS: Limited support - recommends Network Link Conditioner
        """
    )
    parser.add_argument('--latency', type=int, default=100, 
                        help='Base latency in milliseconds (default: 100)')
    parser.add_argument('--jitter', type=int, default=20,
                        help='Latency jitter/variation in milliseconds (default: 20)')
    parser.add_argument('--loss', type=float, default=0,
                        help='Packet loss percentage 0-100 (default: 0)')
    parser.add_argument('--correlation', type=float, default=25,
                        help='Jitter correlation percentage (default: 25)')
    parser.add_argument('--duplicate', type=float, default=0,
                        help='Packet duplication percentage (default: 0)')
    parser.add_argument('--reorder', type=float, default=0,
                        help='Packet reordering percentage (default: 0)')
    parser.add_argument('--interface', 
                        help='Network interface (default: auto-detect)')
    parser.add_argument('--limit', type=int, default=1000,
                        help='Queue limit in packets (default: 1000)')
    parser.add_argument('--reset', action='store_true',
                        help='Remove all network simulation rules')
    parser.add_argument('--show', action='store_true',
                        help='Show current network rules')
    
    args = parser.parse_args()
    
    # Detect platform
    system = platform.system()
    
    # Auto-detect interface if not specified
    if not args.interface:
        args.interface = get_default_interface()
    
    print(f"Network Simulator - Platform: {system}, Interface: {args.interface}")
    print("-" * 50)
    
    # Platform-specific simulator
    simulator = None
    
    if system == 'Linux':
        simulator = LinuxNetworkSimulator(args.interface)
    elif system == 'Windows':
        simulator = WindowsNetworkSimulator()
    elif system == 'Darwin':
        simulator = MacOSNetworkSimulator(args.interface)
    else:
        print(f"Unsupported platform: {system}")
        sys.exit(1)
    
    if not simulator.is_available():
        print(f"Required network tools not found for {system}")
        sys.exit(1)
    
    # Handle commands
    if args.show:
        if hasattr(simulator, 'show'):
            simulator.show()
        else:
            print("Show not supported on this platform")
        return
    
    if args.reset:
        simulator.reset()
        return
    
    # Validate parameters
    if args.latency < 0 or args.latency > 10000:
        print("Error: Latency must be between 0 and 10000ms")
        sys.exit(1)
    
    if args.jitter < 0 or args.jitter > args.latency:
        print("Error: Jitter must be between 0 and latency value")
        sys.exit(1)
    
    if args.loss < 0 or args.loss > 100:
        print("Error: Loss percentage must be between 0 and 100")
        sys.exit(1)
    
    # Apply simulation
    success = simulator.simulate(
        latency_ms=args.latency,
        jitter_ms=args.jitter,
        loss_percent=args.loss,
        correlation=args.correlation,
        duplicate_percent=args.duplicate,
        reorder_percent=args.reorder,
        limit=args.limit
    )
    
    if success:
        print()
        print("Network simulation ACTIVE:")
        print(f"  Interface: {args.interface}")
        print(f"  Latency: {args.latency}ms ± {args.jitter}ms")
        if args.loss > 0:
            print(f"  Packet loss: {args.loss}%")
        if args.duplicate > 0:
            print(f"  Duplication: {args.duplicate}%")
        if args.reorder > 0:
            print(f"  Reordering: {args.reorder}%")
        print()
        print("Run with --reset to clear rules")
        print("Run with --show to view current rules")
    else:
        sys.exit(1)


if __name__ == '__main__':
    main()
