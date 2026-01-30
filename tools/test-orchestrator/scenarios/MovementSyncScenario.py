"""
Movement Synchronization Test Scenario
[TESTING_AGENT] Validates client prediction and server reconciliation

This scenario tests:
1. Client predicts movement locally
2. Server validates and sends corrections
3. Client reconciles with server state
4. Smooth movement despite network latency
"""

import asyncio
import time
import logging
import sys
from pathlib import Path
from typing import List, Tuple
from dataclasses import dataclass

logger = logging.getLogger('MovementSyncScenario')


@dataclass
class InputEvent:
    """Client input at a specific time"""
    timestamp: float
    sequence: int
    forward: bool
    backward: bool
    left: bool
    right: bool
    jump: bool
    yaw: float


@dataclass
class PositionSnapshot:
    """Position at a specific time"""
    timestamp: float
    tick: int
    x: float
    y: float
    z: float
    vx: float
    vy: float
    vz: float


class MovementSyncScenario:
    """
    Test scenario for movement synchronization
    """
    
    def __init__(self):
        self.name = "movement_sync"
        self.description = "Validates client prediction and server reconciliation"
    
    async def execute(self, config, report):
        """Execute the test scenario"""
        logger.info("Starting MovementSyncScenario")
        
        # Simulate clients sending inputs
        client_inputs = self._generate_test_inputs(config.duration_seconds)
        
        # Simulate server processing
        server_snapshots = self._simulate_server_processing(
            client_inputs, 
            config.latency_ms,
            config.jitter_ms
        )
        
        # Simulate client prediction
        client_predictions = self._simulate_client_prediction(
            client_inputs,
            server_snapshots,
            config.latency_ms
        )
        
        # Compare and validate
        await self._validate_synchronization(
            client_predictions,
            server_snapshots,
            report
        )
        
        logger.info("MovementSyncScenario completed")
    
    def _generate_test_inputs(self, duration_seconds: float) -> List[InputEvent]:
        """Generate realistic player input sequence"""
        inputs = []
        start_time = time.time()
        sequence = 0
        
        # Movement pattern: Move forward for 2s, strafe right for 2s, jump, move back
        patterns = [
            (2.0, {"forward": True, "yaw": 0.0}),
            (2.0, {"right": True, "yaw": 90.0}),
            (0.5, {"jump": True, "yaw": 90.0}),
            (2.0, {"backward": True, "yaw": 180.0}),
            (1.0, {"left": True, "yaw": 270.0}),
            (0.5, {}),  # Stationary
        ]
        
        current_time = 0.0
        for duration, params in patterns:
            while current_time < duration:
                inputs.append(InputEvent(
                    timestamp=start_time + current_time,
                    sequence=sequence,
                    forward=params.get("forward", False),
                    backward=params.get("backward", False),
                    left=params.get("left", False),
                    right=params.get("right", False),
                    jump=params.get("jump", False),
                    yaw=params.get("yaw", 0.0)
                ))
                sequence += 1
                current_time += 1.0 / 60.0  # 60Hz inputs
        
        return inputs
    
    def _simulate_server_processing(
        self,
        inputs: List[InputEvent],
        latency_ms: float,
        jitter_ms: float
    ) -> List[PositionSnapshot]:
        """
        Simulate server processing with validation.
        
        Server receives inputs, validates movement, and sends snapshots.
        """
        snapshots = []
        
        # Initial position
        x, y, z = 0.0, 0.0, 0.0
        vx, vy, vz = 0.0, 0.0, 0.0
        tick = 0
        
        # Process each input
        for i, input_event in enumerate(inputs):
            tick += 1
            
            # Calculate intended velocity from input
            target_vx = 0.0
            target_vz = 0.0
            
            speed = 5.0  # m/s
            yaw_rad = input_event.yaw * 3.14159 / 180.0
            
            if input_event.forward:
                target_vx += speed * cos(yaw_rad)
                target_vz += speed * sin(yaw_rad)
            if input_event.backward:
                target_vx -= speed * cos(yaw_rad)
                target_vz -= speed * sin(yaw_rad)
            if input_event.right:
                target_vx += speed * cos(yaw_rad + 1.5708)
                target_vz += speed * sin(yaw_rad + 1.5708)
            if input_event.left:
                target_vx -= speed * cos(yaw_rad + 1.5708)
                target_vz -= speed * sin(yaw_rad + 1.5708)
            
            # Apply velocity with smoothing
            vx = vx * 0.8 + target_vx * 0.2
            vz = vz * 0.8 + target_vz * 0.2
            
            # Apply jump
            if input_event.jump:
                vy = 8.0  # Jump velocity
            
            # Apply gravity
            vy -= 20.0 * (1.0 / 60.0)  # Gravity
            if y <= 0 and vy < 0:
                vy = 0
                y = 0
            
            # Update position (60Hz tick)
            dt = 1.0 / 60.0
            x += vx * dt
            y += vy * dt
            z += vz * dt
            
            # Ground collision
            if y < 0:
                y = 0
                vy = 0
            
            # Create snapshot (server sends this to client)
            snapshot = PositionSnapshot(
                timestamp=input_event.timestamp + (latency_ms / 1000.0),
                tick=tick,
                x=x,
                y=y,
                z=z,
                vx=vx,
                vy=vy,
                vz=vz
            )
            snapshots.append(snapshot)
        
        return snapshots
    
    def _simulate_client_prediction(
        self,
        inputs: List[InputEvent],
        server_snapshots: List[PositionSnapshot],
        latency_ms: float
    ) -> List[PositionSnapshot]:
        """
        Simulate client-side prediction.
        
        Client predicts movement locally, then corrects when server snapshot arrives.
        """
        predictions = []
        
        # Initial position
        x, y, z = 0.0, 0.0, 0.0
        vx, vy, vz = 0.0, 0.0, 0.0
        tick = 0
        
        # Buffer for server snapshots (simulating network delay)
        snapshot_buffer = []
        snapshot_index = 0
        
        for i, input_event in enumerate(inputs):
            tick += 1
            
            # Apply prediction (same physics as server)
            target_vx = 0.0
            target_vz = 0.0
            
            speed = 5.0
            yaw_rad = input_event.yaw * 3.14159 / 180.0
            
            if input_event.forward:
                target_vx += speed * cos(yaw_rad)
                target_vz += speed * sin(yaw_rad)
            if input_event.backward:
                target_vx -= speed * cos(yaw_rad)
                target_vz -= speed * sin(yaw_rad)
            if input_event.right:
                target_vx += speed * cos(yaw_rad + 1.5708)
                target_vz += speed * sin(yaw_rad + 1.5708)
            if input_event.left:
                target_vx -= speed * cos(yaw_rad + 1.5708)
                target_vz -= speed * sin(yaw_rad + 1.5708)
            
            vx = vx * 0.8 + target_vx * 0.2
            vz = vz * 0.8 + target_vz * 0.2
            
            if input_event.jump:
                vy = 8.0
            
            vy -= 20.0 * (1.0 / 60.0)
            if y <= 0 and vy < 0:
                vy = 0
                y = 0
            
            dt = 1.0 / 60.0
            x += vx * dt
            y += vy * dt
            z += vz * dt
            
            if y < 0:
                y = 0
                vy = 0
            
            # Check for server correction
            # (In real implementation, this would be when snapshot arrives)
            if snapshot_index < len(server_snapshots):
                server_snap = server_snapshots[snapshot_index]
                
                # If server snapshot is for this tick, reconcile
                if server_snap.tick == tick:
                    # Calculate error
                    error_x = x - server_snap.x
                    error_y = y - server_snap.y
                    error_z = z - server_snap.z
                    error_dist = (error_x**2 + error_y**2 + error_z**2) ** 0.5
                    
                    # If error is small, snap correction
                    # If error is large, smooth correction
                    if error_dist > 0.5:
                        # Large error - snap to server position
                        x = server_snap.x
                        y = server_snap.y
                        z = server_snap.z
                        vx = server_snap.vx
                        vy = server_snap.vy
                        vz = server_snap.vz
                    else:
                        # Small error - smooth correction
                        x = x * 0.9 + server_snap.x * 0.1
                        y = y * 0.9 + server_snap.y * 0.1
                        z = z * 0.9 + server_snap.z * 0.1
                    
                    snapshot_index += 1
            
            prediction = PositionSnapshot(
                timestamp=input_event.timestamp,
                tick=tick,
                x=x,
                y=y,
                z=z,
                vx=vx,
                vy=vy,
                vz=vz
            )
            predictions.append(prediction)
        
        return predictions
    
    async def _validate_synchronization(
        self,
        predictions: List[PositionSnapshot],
        server_snapshots: List[PositionSnapshot],
        report
    ):
        """Validate prediction accuracy and reconciliation"""
        import sys; sys.path.insert(0, str(Path(__file__).parent.parent))
        from TestOrchestrator import GamestateSample
        
        # Convert to gamestate samples for validation
        for pred, server in zip(predictions, server_snapshots):
            sample = GamestateSample(
                timestamp=pred.timestamp,
                client_tick=pred.tick,
                server_tick=server.tick,
                client_position=(pred.x, pred.y, pred.z),
                server_position=(server.x, server.y, server.z),
                client_velocity=(pred.vx, pred.vy, pred.vz),
                server_velocity=(server.vx, server.vy, server.vz),
                latency_ms=50.0,
                inputs_sent=pred.tick,
                inputs_acked=pred.tick,
                snapshots_received=pred.tick
            )
            report.samples.append(sample)
        
        # Validate using GamestateValidator
        from TestOrchestrator import GamestateValidator
        validator = GamestateValidator(report.config)
        validator.samples = report.samples
        
        # Run validations
        passed, errors = validator.validate_prediction()
        report.prediction_errors = errors
        if not passed:
            report.failures.append("Prediction validation failed")
        
        passed, rec_times = validator.validate_reconciliation()
        report.reconciliation_times = rec_times
        if not passed:
            report.failures.append("Reconciliation validation failed")
        
        passed, tick_rates = validator.validate_tick_stability()
        report.tick_rates = tick_rates
        if not passed:
            report.failures.append("Tick stability validation failed")


# Helper functions
def cos(angle):
    import math
    return math.cos(angle)


def sin(angle):
    import math
    return math.sin(angle)
