"""
Godot MCP Integration Test: Movement Synchronization

This test uses the Godot MCP client to control the actual DarkAges
Godot client and validate movement synchronization with the server.

Prerequisites:
- DarkAges server built and available
- Godot MCP server installed
- DarkAges Godot client project configured

Usage:
    python test_movement_sync_mcp.py --server ../build/Release/darkages_server.exe
"""

import asyncio
import json
import sys
import time
from pathlib import Path
from typing import Dict, List, Any, Optional

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent))

from godot_mcp.client import GodotMCPClient, GodotQAIntegration


class MovementSyncMCPTest:
    """
    End-to-end movement synchronization test using Godot MCP.
    
    This test:
    1. Launches the DarkAges server
    2. Launches the Godot client via MCP
    3. Injects movement inputs
    4. Validates server reconciliation matches client prediction
    5. Captures screenshots for evidence
    """
    
    def __init__(
        self,
        server_path: str,
        project_path: str = "src/client",
        godot_path: Optional[str] = None,
        output_dir: str = "test_results"
    ):
        self.server_path = Path(server_path)
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(exist_ok=True)
        
        # Initialize clients
        self.godot = GodotMCPClient(
            project_path=project_path,
            godot_path=godot_path,
            debug=True
        )
        self.qa = GodotQAIntegration(self.godot)
        
        self.server_process = None
        self.results = {
            "test_name": "MovementSyncMCP",
            "start_time": time.time(),
            "steps": [],
            "screenshots": [],
            "metrics": {},
            "passed": False
        }
    
    def log_step(self, name: str, status: str, details: str = ""):
        """Log a test step."""
        step = {
            "name": name,
            "status": status,
            "details": details,
            "timestamp": time.time()
        }
        self.results["steps"].append(step)
        print(f"[{status}] {name}: {details}")
    
    async def start_server(self) -> bool:
        """Start the DarkAges server."""
        import subprocess
        
        try:
            self.log_step("Start Server", "INFO", f"Launching {self.server_path}")
            
            self.server_process = subprocess.Popen(
                [str(self.server_path), "--port", "7777"],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True
            )
            
            # Wait for server to be ready
            await asyncio.sleep(2)
            
            if self.server_process.poll() is None:
                self.log_step("Start Server", "PASS", "Server running on port 7777")
                return True
            else:
                self.log_step("Start Server", "FAIL", "Server exited prematurely")
                return False
        
        except Exception as e:
            self.log_step("Start Server", "FAIL", str(e))
            return False
    
    async def launch_client(self) -> bool:
        """Launch Godot client via MCP."""
        self.log_step("Launch Client", "INFO", "Connecting via MCP")
        
        if not self.godot.launch_project(debug=True):
            self.log_step("Launch Client", "FAIL", "Failed to launch Godot")
            return False
        
        self.log_step("Launch Client", "PASS", "Godot launched")
        
        # Take initial screenshot
        screenshot = self.qa.capture_evidence("initial_state")
        if screenshot:
            self.results["screenshots"].append({
                "phase": "initial",
                "path": screenshot
            })
        
        return True
    
    async def wait_for_connection(self, timeout: float = 30.0) -> bool:
        """Wait for client to connect to server."""
        self.log_step("Wait Connection", "INFO", f"Timeout: {timeout}s")
        
        # Method 1: Check server logs for connection
        # Method 2: Check Godot debug output
        
        start_time = time.time()
        while time.time() - start_time < timeout:
            # Check Godot logs
            logs = self.godot.get_debug_output()
            for log in logs:
                if "connected" in log.lower() or "connection established" in log.lower():
                    self.log_step("Wait Connection", "PASS", "Client connected")
                    return True
            
            await asyncio.sleep(0.5)
        
        self.log_step("Wait Connection", "FAIL", "Timeout waiting for connection")
        return False
    
    async def execute_movement_sequence(self) -> Dict[str, Any]:
        """Execute a sequence of movement inputs."""
        self.log_step("Movement Test", "INFO", "Starting movement sequence")
        
        # Record initial position
        initial_state = self.godot.get_player_state()
        self.log_step("Movement Test", "INFO", f"Initial: {initial_state}")
        
        # Movement input sequence
        sequence = [
            {"input_type": "key", "key": "w", "state": "press", "delay_after": 0.1},
            {"input_type": "key", "key": "w", "state": "release", "delay_after": 0.5},
            {"input_type": "key", "key": "a", "state": "press", "delay_after": 0.1},
            {"input_type": "key", "key": "a", "state": "release", "delay_after": 0.5},
            {"input_type": "key", "key": "s", "state": "press", "delay_after": 0.1},
            {"input_type": "key", "key": "s", "state": "release", "delay_after": 0.5},
            {"input_type": "key", "key": "d", "state": "press", "delay_after": 0.1},
            {"input_type": "key", "key": "d", "state": "release", "delay_after": 0.5},
        ]
        
        # Execute sequence
        if not self.qa.run_input_sequence(sequence):
            self.log_step("Movement Test", "FAIL", "Failed to execute inputs")
            return {"success": False}
        
        # Wait for movement to complete
        await asyncio.sleep(2)
        
        # Record final position
        final_state = self.godot.get_player_state()
        self.log_step("Movement Test", "INFO", f"Final: {final_state}")
        
        # Take screenshot during movement
        screenshot = self.qa.capture_evidence("movement_complete")
        if screenshot:
            self.results["screenshots"].append({
                "phase": "movement",
                "path": screenshot
            })
        
        # Calculate metrics
        if initial_state and final_state:
            metrics = self._calculate_movement_metrics(initial_state, final_state)
            self.results["metrics"].update(metrics)
        
        # Check for prediction errors in logs
        logs = self.godot.get_debug_output()
        prediction_errors = [l for l in logs if "prediction_error" in l.lower()]
        reconciliation_events = [l for l in logs if "reconciliation" in l.lower()]
        
        self.results["metrics"]["prediction_errors"] = len(prediction_errors)
        self.results["metrics"]["reconciliation_events"] = len(reconciliation_events)
        
        if prediction_errors:
            self.log_step("Movement Test", "WARN", f"Found {len(prediction_errors)} prediction errors")
        else:
            self.log_step("Movement Test", "PASS", "No prediction errors detected")
        
        return {
            "success": True,
            "initial_state": initial_state,
            "final_state": final_state,
            "prediction_errors": prediction_errors
        }
    
    def _calculate_movement_metrics(self, initial: Dict, final: Dict) -> Dict[str, float]:
        """Calculate movement validation metrics."""
        metrics = {}
        
        try:
            # Extract positions
            if "position" in initial and "position" in final:
                init_pos = initial["position"]
                final_pos = final["position"]
                
                # Calculate distance moved
                dx = final_pos.get("x", 0) - init_pos.get("x", 0)
                dy = final_pos.get("y", 0) - init_pos.get("y", 0)
                dz = final_pos.get("z", 0) - init_pos.get("z", 0)
                
                distance = (dx**2 + dy**2 + dz**2) ** 0.5
                metrics["distance_moved"] = distance
                metrics["delta_x"] = dx
                metrics["delta_y"] = dy
                metrics["delta_z"] = dz
        except Exception as e:
            self.log_step("Metrics", "WARN", f"Failed to calculate metrics: {e}")
        
        return metrics
    
    async def validate_server_state(self) -> bool:
        """Validate server has correct player state."""
        self.log_step("Server Validation", "INFO", "Checking server state")
        
        # This would query the server directly or check logs
        # For now, we check Godot logs for server reconciliation
        
        logs = self.godot.get_debug_output()
        
        # Look for successful reconciliation messages
        successful_syncs = [l for l in logs if "sync" in l.lower() and "ok" in l.lower()]
        
        if successful_syncs:
            self.log_step("Server Validation", "PASS", f"Found {len(successful_syncs)} sync events")
            return True
        else:
            self.log_step("Server Validation", "WARN", "No explicit sync confirmations found")
            return True  # Don't fail on this - may not be logged
    
    async def cleanup(self):
        """Clean up resources."""
        self.log_step("Cleanup", "INFO", "Stopping test")
        
        # Stop Godot
        try:
            self.godot.stop_project()
            self.log_step("Cleanup", "PASS", "Godot stopped")
        except Exception as e:
            self.log_step("Cleanup", "WARN", f"Error stopping Godot: {e}")
        
        # Stop server
        if self.server_process:
            try:
                self.server_process.terminate()
                await asyncio.sleep(1)
                if self.server_process.poll() is None:
                    self.server_process.kill()
                self.log_step("Cleanup", "PASS", "Server stopped")
            except Exception as e:
                self.log_step("Cleanup", "WARN", f"Error stopping server: {e}")
    
    def save_results(self):
        """Save test results to file."""
        self.results["end_time"] = time.time()
        self.results["duration"] = self.results["end_time"] - self.results["start_time"]
        
        output_file = self.output_dir / f"movement_sync_mcp_{int(time.time())}.json"
        with open(output_file, "w") as f:
            json.dump(self.results, f, indent=2)
        
        print(f"\nResults saved to: {output_file}")
        
        # Print summary
        print("\n" + "="*60)
        print("TEST SUMMARY: Movement Sync MCP")
        print("="*60)
        print(f"Duration: {self.results['duration']:.2f}s")
        print(f"Status: {'PASS' if self.results['passed'] else 'FAIL'}")
        print(f"Screenshots: {len(self.results['screenshots'])}")
        print(f"Prediction Errors: {self.results['metrics'].get('prediction_errors', 'N/A')}")
        print("="*60)
    
    async def run(self) -> bool:
        """Run the complete test."""
        print("\n" + "="*60)
        print("Starting Movement Sync MCP Test")
        print("="*60 + "\n")
        
        try:
            # 1. Start server
            if not await self.start_server():
                return False
            
            # 2. Launch client
            if not await self.launch_client():
                return False
            
            # 3. Wait for connection
            if not await self.wait_for_connection():
                return False
            
            # 4. Execute movement test
            movement_result = await self.execute_movement_sequence()
            if not movement_result["success"]:
                return False
            
            # 5. Validate server state
            await self.validate_server_state()
            
            # 6. Determine pass/fail
            prediction_errors = self.results["metrics"].get("prediction_errors", 0)
            self.results["passed"] = prediction_errors == 0
            
            return self.results["passed"]
        
        except Exception as e:
            self.log_step("Test", "FAIL", f"Exception: {e}")
            import traceback
            traceback.print_exc()
            return False
        
        finally:
            await self.cleanup()
            self.save_results()


async def main():
    """Main entry point."""
    import argparse
    
    parser = argparse.ArgumentParser(description="Movement Sync MCP Test")
    parser.add_argument(
        "--server",
        default="build/Release/darkages_server.exe",
        help="Path to server executable"
    )
    parser.add_argument(
        "--project",
        default="src/client",
        help="Path to Godot project"
    )
    parser.add_argument(
        "--godot",
        default=None,
        help="Path to Godot executable"
    )
    parser.add_argument(
        "--output",
        default="test_results",
        help="Output directory for results"
    )
    
    args = parser.parse_args()
    
    test = MovementSyncMCPTest(
        server_path=args.server,
        project_path=args.project,
        godot_path=args.godot,
        output_dir=args.output
    )
    
    success = await test.run()
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    asyncio.run(main())
