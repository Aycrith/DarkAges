#!/usr/bin/env python3
"""
Zone Management Utility for DarkAges MMO
Start, stop, monitor, and manage zone servers.

Usage:
    # Start all zones
    python manage_zones.py start --all
    
    # Start specific zone
    python manage_zones.py start --zone 1
    
    # Stop all zones
    python manage_zones.py stop --all
    
    # Check status
    python manage_zones.py status
    
    # View logs
    python manage_zones.py logs --zone 1 --follow
    
    # Restart zone
    python manage_zones.py restart --zone 2
"""

import argparse
import subprocess
import json
import sys
import os
import time
from typing import Dict, List, Optional, Tuple
from dataclasses import dataclass
from datetime import datetime

try:
    import redis
    REDIS_AVAILABLE = True
except ImportError:
    REDIS_AVAILABLE = False
    print("Warning: redis package not installed. Install with: pip install redis")


@dataclass
class ZoneInfo:
    zone_id: int
    port: int
    monitor_port: int
    x_range: str
    z_range: str
    adjacent: List[int]


# Zone definitions matching docker-compose.multi-zone.yml
ZONES: Dict[int, ZoneInfo] = {
    1: ZoneInfo(1, 7777, 7778, "-1000:0", "-1000:0", [2, 3]),
    2: ZoneInfo(2, 7779, 7780, "0:1000", "-1000:0", [1, 4]),
    3: ZoneInfo(3, 7781, 7782, "-1000:0", "0:1000", [1, 4]),
    4: ZoneInfo(4, 7783, 7784, "0:1000", "0:1000", [2, 3]),
}

# Configuration
COMPOSE_FILE = "infra/docker-compose.multi-zone.yml"
PROJECT_NAME = "darkages"


class ZoneManager:
    """Manages DarkAges zone servers"""
    
    def __init__(self, use_docker: bool = True):
        self.use_docker = use_docker
        self.redis_client: Optional[redis.Redis] = None
        
        if REDIS_AVAILABLE:
            try:
                self.redis_client = redis.Redis(
                    host='localhost', port=6379, 
                    decode_responses=True, socket_connect_timeout=2
                )
                self.redis_client.ping()
            except (redis.ConnectionError, redis.TimeoutError):
                self.redis_client = None
    
    def _run_docker_compose(self, command: List[str], 
                           capture: bool = True) -> Tuple[int, str, str]:
        """Run docker-compose command"""
        cmd = ["docker-compose", "-f", COMPOSE_FILE, "-p", PROJECT_NAME] + command
        
        if capture:
            result = subprocess.run(cmd, capture_output=True, text=True)
            return result.returncode, result.stdout, result.stderr
        else:
            result = subprocess.run(cmd)
            return result.returncode, "", ""
    
    def _get_zone_container(self, zone_id: int) -> str:
        """Get container name for zone"""
        return f"{PROJECT_NAME}-zone-{zone_id}"
    
    def start_zone(self, zone_id: int) -> bool:
        """Start a specific zone"""
        if zone_id not in ZONES:
            print(f"Error: Invalid zone ID {zone_id}. Valid: {list(ZONES.keys())}")
            return False
        
        zone = ZONES[zone_id]
        
        if self.use_docker:
            print(f"Starting Zone {zone_id} (port {zone.port})...")
            returncode, stdout, stderr = self._run_docker_compose(
                ["up", "-d", f"zone-{zone_id}"]
            )
            
            if returncode == 0:
                print(f"  ✓ Zone {zone_id} started successfully")
                return True
            else:
                print(f"  ✗ Failed to start Zone {zone_id}")
                if stderr:
                    print(f"  Error: {stderr}")
                return False
        else:
            # Native process mode
            binary = "./build/bin/darkages-server"
            if not os.path.exists(binary):
                print(f"  ✗ Binary not found: {binary}")
                return False
            
            min_x, max_x = zone.x_range.split(":")
            min_z, max_z = zone.z_range.split(":")
            
            cmd = [
                binary,
                "--zone", str(zone_id),
                "--port", str(zone.port),
                "--min-x", min_x,
                "--max-x", max_x,
                "--min-z", min_z,
                "--max-z", max_z,
                "--adjacent-zones", ",".join(map(str, zone.adjacent))
            ]
            
            log_file = f"logs/zone-{zone_id}.log"
            os.makedirs("logs", exist_ok=True)
            
            print(f"Starting Zone {zone_id} (native mode)...")
            try:
                with open(log_file, 'w') as f:
                    subprocess.Popen(
                        cmd, stdout=f, stderr=subprocess.STDOUT,
                        start_new_session=True
                    )
                print(f"  ✓ Zone {zone_id} started (PID file: logs/zone-{zone_id}.pid)")
                return True
            except Exception as e:
                print(f"  ✗ Failed to start Zone {zone_id}: {e}")
                return False
    
    def stop_zone(self, zone_id: int) -> bool:
        """Stop a specific zone"""
        if zone_id not in ZONES:
            print(f"Error: Invalid zone ID {zone_id}")
            return False
        
        if self.use_docker:
            print(f"Stopping Zone {zone_id}...")
            returncode, stdout, stderr = self._run_docker_compose(
                ["stop", f"zone-{zone_id}"]
            )
            
            if returncode == 0:
                print(f"  ✓ Zone {zone_id} stopped")
                return True
            else:
                print(f"  ✗ Failed to stop Zone {zone_id}")
                if stderr:
                    print(f"  Error: {stderr}")
                return False
        else:
            # Native mode - would need PID tracking
            print("Native process stop not yet implemented")
            return False
    
    def restart_zone(self, zone_id: int) -> bool:
        """Restart a specific zone"""
        if self.stop_zone(zone_id):
            time.sleep(2)  # Wait for clean shutdown
            return self.start_zone(zone_id)
        return False
    
    def start_all(self) -> bool:
        """Start all zones and infrastructure"""
        print("Starting all DarkAges zones...")
        
        if self.use_docker:
            returncode, stdout, stderr = self._run_docker_compose(["up", "-d"])
            
            if returncode == 0:
                print("  ✓ All services started")
                print("\nAccess points:")
                print("  Gateway:    http://localhost:80")
                print("  Grafana:    http://localhost:3000 (admin/admin)")
                print("  Prometheus: http://localhost:9090")
                print("  Jaeger:     http://localhost:16686")
                print("\nZone ports:")
                for zone_id, zone in ZONES.items():
                    print(f"  Zone {zone_id}: localhost:{zone.port}")
                return True
            else:
                print("  ✗ Failed to start services")
                if stderr:
                    print(f"  Error: {stderr}")
                return False
        else:
            # Start zones sequentially in native mode
            success = True
            for zone_id in ZONES:
                if not self.start_zone(zone_id):
                    success = False
            return success
    
    def stop_all(self) -> bool:
        """Stop all zones and infrastructure"""
        print("Stopping all DarkAges zones...")
        
        if self.use_docker:
            returncode, stdout, stderr = self._run_docker_compose(["down"])
            
            if returncode == 0:
                print("  ✓ All services stopped")
                return True
            else:
                print("  ✗ Failed to stop services")
                if stderr:
                    print(f"  Error: {stderr}")
                return False
        else:
            print("Native stop all not yet implemented")
            return False
    
    def get_status(self) -> bool:
        """Get status of all zones"""
        print("\n" + "="*70)
        print("DARK AGES ZONE STATUS")
        print("="*70)
        
        if self.use_docker:
            # Check docker container status
            print("\nContainer Status:")
            print("-"*70)
            returncode, stdout, stderr = self._run_docker_compose(
                ["ps", "--format", "table"]
            )
            if stdout:
                print(stdout)
        
        # Get zone status from Redis
        print("\nZone Metrics:")
        print("-"*70)
        print(f"{'Zone':<6}{'Status':<12}{'Players':<10}{'Tick':<8}{'CPU %':<10}{'Memory':<12}")
        print("-"*70)
        
        if self.redis_client:
            for zone_id in sorted(ZONES.keys()):
                status_key = f"zone:{zone_id}:status"
                status = self.redis_client.hgetall(status_key)
                
                if status:
                    state = status.get('state', 'unknown')
                    players = status.get('players', '0')
                    tick = status.get('tick_rate', 'N/A')
                    cpu = status.get('cpu_percent', 'N/A')
                    memory = status.get('memory_mb', 'N/A')
                    
                    print(f"{zone_id:<6}{state:<12}{players:<10}{tick:<8}{cpu:<10}{memory:<12}")
                else:
                    print(f"{zone_id:<6}{'unknown':<12}{'0':<10}{'N/A':<8}{'N/A':<10}{'N/A':<12}")
        else:
            print("  (Redis not connected - status unavailable)")
        
        print("-"*70)
        
        # Total players
        if self.redis_client:
            try:
                total = sum(
                    int(self.redis_client.hget(f"zone:{z}:status", 'players') or 0)
                    for z in ZONES
                )
                print(f"Total players online: {total}")
            except Exception:
                pass
        
        print("="*70)
        return True
    
    def show_logs(self, zone_id: Optional[int] = None, 
                  follow: bool = False, tail: int = 100) -> bool:
        """Show logs for a zone or all zones"""
        if self.use_docker:
            cmd = ["logs"]
            
            if follow:
                cmd.append("-f")
            
            cmd.extend(["--tail", str(tail)])
            
            if zone_id:
                if zone_id not in ZONES:
                    print(f"Error: Invalid zone ID {zone_id}")
                    return False
                cmd.append(f"zone-{zone_id}")
                print(f"Showing logs for Zone {zone_id}...")
            else:
                print("Showing logs for all zones...")
            
            # Don't capture output - stream directly to console
            returncode, _, _ = self._run_docker_compose(cmd, capture=False)
            return returncode == 0
        else:
            # Native mode - read from log files
            if zone_id:
                log_file = f"logs/zone-{zone_id}.log"
                if os.path.exists(log_file):
                    cmd = ["tail"]
                    if follow:
                        cmd.append("-f")
                    cmd.extend(["-n", str(tail), log_file])
                    subprocess.run(cmd)
                    return True
                else:
                    print(f"Log file not found: {log_file}")
                    return False
            else:
                print("Native mode: Use --zone to specify which log file to view")
                return False
    
    def scale_zone(self, zone_id: int, replicas: int) -> bool:
        """Scale a zone (Docker Swarm mode only)"""
        print(f"Scaling Zone {zone_id} to {replicas} replicas...")
        print("Note: Requires Docker Swarm mode (not implemented)")
        return False
    
    def exec_command(self, zone_id: int, command: List[str]) -> bool:
        """Execute a command inside a zone container"""
        if not self.use_docker:
            print("Docker required for exec command")
            return False
        
        if zone_id not in ZONES:
            print(f"Error: Invalid zone ID {zone_id}")
            return False
        
        cmd = ["exec", f"zone-{zone_id}"] + command
        returncode, stdout, stderr = self._run_docker_compose(cmd)
        
        if stdout:
            print(stdout)
        if stderr:
            print(stderr, file=sys.stderr)
        
        return returncode == 0


def main():
    parser = argparse.ArgumentParser(
        description='DarkAges Zone Management Utility',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    # Start all zones
    python manage_zones.py start --all
    
    # Start Zone 1 only
    python manage_zones.py start --zone 1
    
    # Check status of all zones
    python manage_zones.py status
    
    # View logs for Zone 2
    python manage_zones.py logs --zone 2
    
    # Follow logs for Zone 1 (Ctrl+C to exit)
    python manage_zones.py logs --zone 1 --follow
    
    # Restart Zone 3
    python manage_zones.py restart --zone 3
    
    # Stop all zones
    python manage_zones.py stop --all
        """
    )
    
    subparsers = parser.add_subparsers(dest='command', help='Command to execute')
    
    # Start command
    start_parser = subparsers.add_parser('start', help='Start zones')
    start_group = start_parser.add_mutually_exclusive_group(required=True)
    start_group.add_argument('--zone', '-z', type=int, metavar='ID',
                            help='Start specific zone (1-4)')
    start_group.add_argument('--all', '-a', action='store_true',
                            help='Start all zones')
    start_parser.add_argument('--native', '-n', action='store_true',
                             help='Use native processes instead of Docker')
    
    # Stop command
    stop_parser = subparsers.add_parser('stop', help='Stop zones')
    stop_group = stop_parser.add_mutually_exclusive_group(required=True)
    stop_group.add_argument('--zone', '-z', type=int, metavar='ID',
                           help='Stop specific zone (1-4)')
    stop_group.add_argument('--all', '-a', action='store_true',
                           help='Stop all zones')
    
    # Restart command
    restart_parser = subparsers.add_parser('restart', help='Restart zones')
    restart_parser.add_argument('--zone', '-z', type=int, required=True,
                               help='Zone to restart (1-4)')
    
    # Status command
    subparsers.add_parser('status', help='Show zone status')
    
    # Logs command
    logs_parser = subparsers.add_parser('logs', help='View zone logs')
    logs_parser.add_argument('--zone', '-z', type=int,
                            help='Zone to view (default: all)')
    logs_parser.add_argument('--follow', '-f', action='store_true',
                            help='Follow log output')
    logs_parser.add_argument('--tail', '-t', type=int, default=100,
                            help='Number of lines to show (default: 100)')
    
    # Scale command (placeholder)
    scale_parser = subparsers.add_parser('scale', help='Scale zone (placeholder)')
    scale_parser.add_argument('--zone', '-z', type=int, required=True)
    scale_parser.add_argument('--replicas', '-r', type=int, required=True)
    
    # Exec command
    exec_parser = subparsers.add_parser('exec', help='Execute command in zone')
    exec_parser.add_argument('--zone', '-z', type=int, required=True)
    exec_parser.add_argument('cmd', nargs='+', help='Command to execute')
    
    args = parser.parse_args()
    
    if not args.command:
        parser.print_help()
        sys.exit(1)
    
    # Determine Docker vs native mode
    use_docker = True
    if hasattr(args, 'native') and args.native:
        use_docker = False
    
    manager = ZoneManager(use_docker=use_docker)
    
    success = False
    
    if args.command == 'start':
        if args.all:
            success = manager.start_all()
        else:
            success = manager.start_zone(args.zone)
    
    elif args.command == 'stop':
        if args.all:
            success = manager.stop_all()
        else:
            success = manager.stop_zone(args.zone)
    
    elif args.command == 'restart':
        success = manager.restart_zone(args.zone)
    
    elif args.command == 'status':
        success = manager.get_status()
    
    elif args.command == 'logs':
        success = manager.show_logs(args.zone, args.follow, args.tail)
    
    elif args.command == 'scale':
        success = manager.scale_zone(args.zone, args.replicas)
    
    elif args.command == 'exec':
        success = manager.exec_command(args.zone, args.cmd)
    
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
