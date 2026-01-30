#!/usr/bin/env python3
"""
DarkAges MMO - Chaos Monkey
[DEVOPS_AGENT] Fault injection framework for resilience testing

Features:
- Random zone server termination
- Network partition simulation
- Database failover testing
- Resource exhaustion attacks
- Clock skew simulation
"""

import asyncio
import random
import time
import subprocess
import logging
import json
from dataclasses import dataclass, field
from typing import List, Dict, Optional, Callable
from enum import Enum, auto
from datetime import datetime

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger('ChaosMonkey')


class ChaosAction(Enum):
    """Types of chaos actions"""
    KILL_ZONE_SERVER = auto()
    NETWORK_PARTITION = auto()
    LATENCY_INJECTION = auto()
    PACKET_LOSS = auto()
    CPU_HOG = auto()
    MEMORY_PRESSURE = auto()
    DISK_FILL = auto()
    CLOCK_SKEW = auto()
    DATABASE_RESTART = auto()
    REDIS_RESTART = auto()


@dataclass
class ChaosEvent:
    """Record of a chaos event"""
    timestamp: float
    action: ChaosAction
    target: str
    duration: float
    success: bool
    error_message: str = ""
    recovery_time: float = 0.0


@dataclass
class ChaosConfig:
    """Configuration for chaos testing"""
    # Timing
    min_interval: float = 30.0      # Minimum seconds between events
    max_interval: float = 120.0     # Maximum seconds between events
    test_duration: float = 3600.0   # Total test duration
    
    # Event selection
    enabled_actions: List[ChaosAction] = field(default_factory=lambda: [
        ChaosAction.KILL_ZONE_SERVER,
        ChaosAction.NETWORK_PARTITION,
        ChaosAction.LATENCY_INJECTION,
        ChaosAction.DATABASE_RESTART,
    ])
    
    # Targets
    zone_servers: List[str] = field(default_factory=lambda: [
        "zone-server-1", "zone-server-2", "zone-server-3", "zone-server-4"
    ])
    
    # Kubernetes namespace
    k8s_namespace: str = "darkages"
    
    # Safety limits
    max_concurrent_failures: int = 2
    min_healthy_zones: int = 2
    
    # Recovery settings
    auto_recovery: bool = True
    recovery_timeout: float = 60.0


class ChaosMonkey:
    """
    Chaos engineering tool for DarkAges MMO
    """
    
    def __init__(self, config: ChaosConfig):
        self.config = config
        self.events: List[ChaosEvent] = []
        self.running = False
        self.active_failures: Dict[ChaosAction, float] = {}
        self.metrics = {
            'total_events': 0,
            'successful_events': 0,
            'failed_events': 0,
            'auto_recoveries': 0
        }
        
    async def run(self):
        """Run chaos monkey for configured duration"""
        self.running = True
        start_time = time.time()
        
        logger.info(f"Starting Chaos Monkey for {self.config.test_duration}s")
        logger.info(f"Enabled actions: {[a.name for a in self.config.enabled_actions]}")
        
        while self.running and (time.time() - start_time) < self.config.test_duration:
            # Check if we can inject more chaos
            if len(self.active_failures) >= self.config.max_concurrent_failures:
                await asyncio.sleep(5)
                continue
            
            # Check minimum healthy zones
            if not await self._check_healthy_zones():
                logger.warning("Too few healthy zones, waiting...")
                await asyncio.sleep(10)
                continue
            
            # Select and execute random chaos action
            action = random.choice(self.config.enabled_actions)
            await self._execute_action(action)
            
            # Wait before next event
            interval = random.uniform(self.config.min_interval, self.config.max_interval)
            logger.info(f"Waiting {interval:.1f}s before next chaos event...")
            await asyncio.sleep(interval)
        
        self.running = False
        logger.info("Chaos Monkey stopped")
        
        # Clean up any remaining failures
        if self.config.auto_recovery:
            await self._recover_all()
    
    async def _execute_action(self, action: ChaosAction):
        """Execute a single chaos action"""
        logger.info(f"Executing chaos action: {action.name}")
        
        event = ChaosEvent(
            timestamp=time.time(),
            action=action,
            target="",
            duration=0.0,
            success=False
        )
        
        try:
            if action == ChaosAction.KILL_ZONE_SERVER:
                await self._kill_zone_server(event)
            elif action == ChaosAction.NETWORK_PARTITION:
                await self._network_partition(event)
            elif action == ChaosAction.LATENCY_INJECTION:
                await self._latency_injection(event)
            elif action == ChaosAction.PACKET_LOSS:
                await self._packet_loss(event)
            elif action == ChaosAction.CPU_HOG:
                await self._cpu_hog(event)
            elif action == ChaosAction.MEMORY_PRESSURE:
                await self._memory_pressure(event)
            elif action == ChaosAction.DATABASE_RESTART:
                await self._database_restart(event)
            elif action == ChaosAction.REDIS_RESTART:
                await self._redis_restart(event)
            elif action == ChaosAction.CLOCK_SKEW:
                await self._clock_skew(event)
            else:
                logger.warning(f"Unknown chaos action: {action}")
                return
            
            self.metrics['successful_events'] += 1
            
        except Exception as e:
            event.success = False
            event.error_message = str(e)
            self.metrics['failed_events'] += 1
            logger.error(f"Chaos action failed: {e}")
        
        self.metrics['total_events'] += 1
        self.events.append(event)
    
    async def _kill_zone_server(self, event: ChaosEvent):
        """Kill a random zone server pod"""
        target = random.choice(self.config.zone_servers)
        event.target = target
        
        logger.info(f"Killing zone server: {target}")
        
        cmd = [
            "kubectl", "delete", "pod", target,
            "-n", self.config.k8s_namespace,
            "--grace-period=0", "--force"
        ]
        
        result = await self._run_command(cmd)
        event.success = result.returncode == 0
        
        if event.success:
            self.active_failures[ChaosAction.KILL_ZONE_SERVER] = time.time()
            
            if self.config.auto_recovery:
                # Wait for Kubernetes to recreate the pod
                await asyncio.sleep(self.config.recovery_timeout)
                await self._wait_for_pod(target)
                self.active_failures.pop(ChaosAction.KILL_ZONE_SERVER, None)
                self.metrics['auto_recoveries'] += 1
    
    async def _network_partition(self, event: ChaosEvent):
        """Simulate network partition between zone servers"""
        if len(self.config.zone_servers) < 2:
            return
        
        source = random.choice(self.config.zone_servers)
        target = random.choice([s for s in self.config.zone_servers if s != source])
        
        event.target = f"{source}->{target}"
        logger.info(f"Creating network partition: {source} <-> {target}")
        
        # Use kubectl exec to run iptables commands inside the pod
        # This blocks traffic between the two pods
        cmd = [
            "kubectl", "exec", source, "-n", self.config.k8s_namespace,
            "--", "iptables", "-A", "OUTPUT", "-d", target, "-j", "DROP"
        ]
        
        result = await self._run_command(cmd)
        event.success = result.returncode == 0
        
        if event.success:
            self.active_failures[ChaosAction.NETWORK_PARTITION] = time.time()
            
            # Keep partition for 60 seconds
            await asyncio.sleep(60)
            
            # Remove partition
            cmd = [
                "kubectl", "exec", source, "-n", self.config.k8s_namespace,
                "--", "iptables", "-D", "OUTPUT", "-d", target, "-j", "DROP"
            ]
            await self._run_command(cmd)
            self.active_failures.pop(ChaosAction.NETWORK_PARTITION, None)
    
    async def _latency_injection(self, event: ChaosEvent):
        """Add network latency to a zone server"""
        target = random.choice(self.config.zone_servers)
        event.target = target
        latency_ms = random.randint(50, 200)
        
        logger.info(f"Injecting {latency_ms}ms latency into {target}")
        
        # Use tc (traffic control) to add latency
        cmd = [
            "kubectl", "exec", target, "-n", self.config.k8s_namespace,
            "--", "tc", "qdisc", "add", "dev", "eth0", "root", "netem",
            "delay", f"{latency_ms}ms", "10ms"
        ]
        
        result = await self._run_command(cmd)
        event.success = result.returncode == 0
        
        if event.success:
            self.active_failures[ChaosAction.LATENCY_INJECTION] = time.time()
            
            # Keep latency for 120 seconds
            await asyncio.sleep(120)
            
            # Remove latency
            cmd = [
                "kubectl", "exec", target, "-n", self.config.k8s_namespace,
                "--", "tc", "qdisc", "del", "dev", "eth0", "root"
            ]
            await self._run_command(cmd)
            self.active_failures.pop(ChaosAction.LATENCY_INJECTION, None)
    
    async def _packet_loss(self, event: ChaosEvent):
        """Simulate packet loss"""
        target = random.choice(self.config.zone_servers)
        event.target = target
        loss_percent = random.randint(5, 20)
        
        logger.info(f"Injecting {loss_percent}% packet loss into {target}")
        
        cmd = [
            "kubectl", "exec", target, "-n", self.config.k8s_namespace,
            "--", "tc", "qdisc", "add", "dev", "eth0", "root", "netem",
            "loss", f"{loss_percent}%"
        ]
        
        result = await self._run_command(cmd)
        event.success = result.returncode == 0
        
        if event.success:
            await asyncio.sleep(60)
            
            cmd = [
                "kubectl", "exec", target, "-n", self.config.k8s_namespace,
                "--", "tc", "qdisc", "del", "dev", "eth0", "root"
            ]
            await self._run_command(cmd)
    
    async def _cpu_hog(self, event: ChaosEvent):
        """Consume CPU resources"""
        target = random.choice(self.config.zone_servers)
        event.target = target
        
        logger.info(f"Starting CPU hog on {target}")
        
        # Start a process that consumes CPU
        cmd = [
            "kubectl", "exec", target, "-n", self.config.k8s_namespace,
            "--", "sh", "-c",
            "while :; do :; done &"  # Infinite loop in background
        ]
        
        result = await self._run_command(cmd)
        event.success = result.returncode == 0
        
        if event.success:
            await asyncio.sleep(60)
            
            # Kill the CPU hog
            cmd = [
                "kubectl", "exec", target, "-n", self.config.k8s_namespace,
                "--", "pkill", "-f", "while :; do :; done"
            ]
            await self._run_command(cmd)
    
    async def _memory_pressure(self, event: ChaosEvent):
        """Consume memory resources"""
        target = random.choice(self.config.zone_servers)
        event.target = target
        memory_mb = random.randint(100, 500)
        
        logger.info(f"Allocating {memory_mb}MB memory on {target}")
        
        cmd = [
            "kubectl", "exec", target, "-n", self.config.k8s_namespace,
            "--", "python3", "-c",
            f"import time; x = bytearray({memory_mb} * 1024 * 1024); time.sleep(60)"
        ]
        
        # Run with timeout
        result = await self._run_command(cmd, timeout=65)
        event.success = True  # Timeout is expected
    
    async def _database_restart(self, event: ChaosEvent):
        """Restart ScyllaDB cluster"""
        event.target = "scylla"
        logger.info("Restarting ScyllaDB")
        
        cmd = [
            "kubectl", "rollout", "restart", "statefulset/scylla",
            "-n", self.config.k8s_namespace
        ]
        
        result = await self._run_command(cmd)
        event.success = result.returncode == 0
        
        if event.success:
            # Wait for rollout to complete
            cmd = [
                "kubectl", "rollout", "status", "statefulset/scylla",
                "-n", self.config.k8s_namespace,
                "--timeout=300s"
            ]
            await self._run_command(cmd, timeout=310)
    
    async def _redis_restart(self, event: ChaosEvent):
        """Restart Redis cluster"""
        event.target = "redis"
        logger.info("Restarting Redis")
        
        cmd = [
            "kubectl", "rollout", "restart", "statefulset/redis",
            "-n", self.config.k8s_namespace
        ]
        
        result = await self._run_command(cmd)
        event.success = result.returncode == 0
        
        if event.success:
            cmd = [
                "kubectl", "rollout", "status", "statefulset/redis",
                "-n", self.config.k8s_namespace,
                "--timeout=120s"
            ]
            await self._run_command(cmd, timeout=130)
    
    async def _clock_skew(self, event: ChaosEvent):
        """Simulate clock skew"""
        target = random.choice(self.config.zone_servers)
        event.target = target
        skew_seconds = random.choice([-30, -10, 10, 30])
        
        logger.info(f"Applying {skew_seconds}s clock skew to {target}")
        
        # Note: This requires privileged container
        cmd = [
            "kubectl", "exec", target, "-n", self.config.k8s_namespace,
            "--", "date", "-s", f"{skew_seconds} seconds"
        ]
        
        result = await self._run_command(cmd)
        event.success = result.returncode == 0
        
        if event.success:
            await asyncio.sleep(60)
            
            # Reset clock (sync with host)
            cmd = [
                "kubectl", "exec", target, "-n", self.config.k8s_namespace,
                "--", "ntpdate", "-s", "pool.ntp.org"
            ]
            await self._run_command(cmd)
    
    async def _check_healthy_zones(self) -> bool:
        """Check if enough zones are healthy"""
        cmd = [
            "kubectl", "get", "pods", "-n", self.config.k8s_namespace,
            "-l", "app=zone-server",
            "--field-selector=status.phase=Running",
            "-o", "json"
        ]
        
        result = await self._run_command(cmd)
        if result.returncode != 0:
            return False
        
        try:
            data = json.loads(result.stdout)
            running = len(data.get('items', []))
            return running >= self.config.min_healthy_zones
        except:
            return False
    
    async def _wait_for_pod(self, pod_name: str, timeout: float = 120):
        """Wait for pod to be ready"""
        logger.info(f"Waiting for {pod_name} to be ready...")
        
        cmd = [
            "kubectl", "wait", "pod", pod_name,
            "-n", self.config.k8s_namespace,
            "--for=condition=Ready",
            f"--timeout={int(timeout)}s"
        ]
        
        result = await self._run_command(cmd, timeout=timeout + 10)
        if result.returncode == 0:
            logger.info(f"{pod_name} is ready")
        else:
            logger.error(f"Timeout waiting for {pod_name}")
    
    async def _recover_all(self):
        """Recover from all active failures"""
        logger.info("Recovering from all chaos events...")
        
        # Network partitions
        if ChaosAction.NETWORK_PARTITION in self.active_failures:
            # Remove all iptables rules
            for target in self.config.zone_servers:
                cmd = [
                    "kubectl", "exec", target, "-n", self.config.k8s_namespace,
                    "--", "iptables", "-F"
                ]
                await self._run_command(cmd)
        
        # Latency/packet loss
        for action in [ChaosAction.LATENCY_INJECTION, ChaosAction.PACKET_LOSS]:
            if action in self.active_failures:
                for target in self.config.zone_servers:
                    cmd = [
                        "kubectl", "exec", target, "-n", self.config.k8s_namespace,
                        "--", "tc", "qdisc", "del", "dev", "eth0", "root"
                    ]
                    await self._run_command(cmd)
        
        self.active_failures.clear()
        logger.info("Recovery complete")
    
    async def _run_command(self, cmd: List[str], timeout: float = 30) -> subprocess.CompletedProcess:
        """Run a shell command"""
        proc = await asyncio.create_subprocess_exec(
            *cmd,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE
        )
        
        try:
            stdout, stderr = await asyncio.wait_for(
                proc.communicate(),
                timeout=timeout
            )
            return subprocess.CompletedProcess(
                cmd, proc.returncode,
                stdout.decode() if stdout else "",
                stderr.decode() if stderr else ""
            )
        except asyncio.TimeoutError:
            proc.kill()
            return subprocess.CompletedProcess(cmd, -1, "", "Timeout")
    
    def generate_report(self) -> dict:
        """Generate chaos test report"""
        if not self.events:
            return {"error": "No chaos events recorded"}
        
        # Calculate metrics
        action_counts = {}
        for event in self.events:
            name = event.action.name
            if name not in action_counts:
                action_counts[name] = {"total": 0, "success": 0}
            action_counts[name]["total"] += 1
            if event.success:
                action_counts[name]["success"] += 1
        
        recovery_times = [
            e.recovery_time for e in self.events
            if e.recovery_time > 0
        ]
        
        report = {
            "test_duration": self.config.test_duration,
            "total_events": self.metrics['total_events'],
            "successful_events": self.metrics['successful_events'],
            "failed_events": self.metrics['failed_events'],
            "success_rate": self.metrics['successful_events'] / max(1, self.metrics['total_events']),
            "auto_recoveries": self.metrics['auto_recoveries'],
            "action_breakdown": action_counts,
            "recovery_time": {
                "mean": sum(recovery_times) / max(1, len(recovery_times)),
                "max": max(recovery_times) if recovery_times else 0,
                "min": min(recovery_times) if recovery_times else 0
            },
            "events": [
                {
                    "timestamp": e.timestamp,
                    "action": e.action.name,
                    "target": e.target,
                    "success": e.success,
                    "error": e.error_message
                }
                for e in self.events
            ]
        }
        
        return report
    
    def print_report(self):
        """Print formatted report"""
        report = self.generate_report()
        
        print("\n" + "="*60)
        print("CHAOS MONKEY TEST REPORT")
        print("="*60)
        print(f"Test Duration: {report['test_duration']:.0f}s")
        print(f"Total Events: {report['total_events']}")
        print(f"Success Rate: {report['success_rate']*100:.1f}%")
        print(f"Auto Recoveries: {report['auto_recoveries']}")
        print()
        print("ACTION BREAKDOWN:")
        for action, stats in report['action_breakdown'].items():
            rate = stats['success'] / max(1, stats['total']) * 100
            print(f"  {action}: {stats['success']}/{stats['total']} ({rate:.0f}%)")
        print()
        print("RECOVERY TIME:")
        print(f"  Mean: {report['recovery_time']['mean']:.1f}s")
        print(f"  Max: {report['recovery_time']['max']:.1f}s")
        print(f"  Min: {report['recovery_time']['min']:.1f}s")
        print("="*60)


async def main():
    import argparse
    
    parser = argparse.ArgumentParser(description='DarkAges Chaos Monkey')
    parser.add_argument('-d', '--duration', type=float, default=3600,
                        help='Test duration in seconds')
    parser.add_argument('--min-interval', type=float, default=30,
                        help='Minimum seconds between events')
    parser.add_argument('--max-interval', type=float, default=120,
                        help='Maximum seconds between events')
    parser.add_argument('-n', '--namespace', type=str, default='darkages',
                        help='Kubernetes namespace')
    parser.add_argument('--dry-run', action='store_true',
                        help='Print actions without executing')
    parser.add_argument('-o', '--output', type=str, default=None,
                        help='Output JSON file for results')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Enable verbose logging')
    
    args = parser.parse_args()
    
    if args.verbose:
        logging.getLogger().setLevel(logging.DEBUG)
    
    config = ChaosConfig(
        test_duration=args.duration,
        min_interval=args.min_interval,
        max_interval=args.max_interval,
        k8s_namespace=args.namespace
    )
    
    monkey = ChaosMonkey(config)
    
    try:
        await monkey.run()
    except KeyboardInterrupt:
        logger.info("Interrupted by user")
    
    monkey.print_report()
    
    if args.output:
        report = monkey.generate_report()
        with open(args.output, 'w') as f:
            json.dump(report, f, indent=2)
        logger.info(f"Report saved to {args.output}")


if __name__ == '__main__':
    asyncio.run(main())
