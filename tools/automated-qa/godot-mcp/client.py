"""
Godot MCP Client for DarkAges MMO Testing

This module provides a Python client to interact with Godot MCP servers,
enabling automated testing of the Godot game client.

Usage:
    from client import GodotMCPClient
    
    client = GodotMCPClient()
    client.launch_project("src/client")
    logs = client.get_debug_output()
    client.stop_project()
"""

import asyncio
import json
import subprocess
import time
from pathlib import Path
from typing import Any, Dict, List, Optional, Union
from dataclasses import dataclass, asdict
import logging

# Configure logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("GodotMCPClient")


@dataclass
class ScreenshotResult:
    """Result of a screenshot capture."""
    success: bool
    path: Optional[str] = None
    data: Optional[bytes] = None
    error: Optional[str] = None


@dataclass
class GDScriptResult:
    """Result of GDScript execution."""
    success: bool
    result: Any = None
    error: Optional[str] = None


@dataclass
class GodotProject:
    """Godot project information."""
    path: str
    name: str
    version: str
    main_scene: Optional[str] = None


class GodotMCPClient:
    """
    Python client for Godot MCP server.
    
    This client interfaces with the Godot MCP server (bradypp/godot-mcp)
    to control Godot editor and game instances programmatically.
    """
    
    def __init__(
        self,
        mcp_server_path: Optional[str] = None,
        godot_path: Optional[str] = None,
        project_path: Optional[str] = None,
        debug: bool = False
    ):
        """
        Initialize Godot MCP client.
        
        Args:
            mcp_server_path: Path to godot-mcp build/index.js
            godot_path: Path to Godot executable
            project_path: Path to DarkAges client project
            debug: Enable debug logging
        """
        self.mcp_server_path = mcp_server_path or self._find_mcp_server()
        self.godot_path = godot_path or self._find_godot()
        self.project_path = project_path or self._find_project()
        self.debug = debug
        self._process: Optional[subprocess.Popen] = None
        self._running = False
        
        if debug:
            logger.setLevel(logging.DEBUG)
    
    def _find_mcp_server(self) -> str:
        """Find MCP server installation."""
        # Check common locations
        paths = [
            Path("tools/godot-mcp/build/index.js"),
            Path("../godot-mcp/build/index.js"),
            Path("../../godot-mcp/build/index.js"),
            Path("C:/Dev/DarkAges/tools/godot-mcp/build/index.js"),
        ]
        for path in paths:
            if path.exists():
                return str(path.resolve())
        raise FileNotFoundError("Godot MCP server not found. Run: git clone https://github.com/bradypp/godot-mcp.git")
    
    def _find_godot(self) -> str:
        """Find Godot executable."""
        # Check environment variable first
        import os
        if os.environ.get("GODOT_PATH"):
            return os.environ["GODOT_PATH"]
        
        # Check common paths
        paths = [
            Path("C:/Tools/Godot/Godot_v4.2.exe"),
            Path("C:/Program Files/Godot/Godot_v4.2.exe"),
            Path("C:/Program Files/Godot/Godot.exe"),
        ]
        for path in paths:
            if path.exists():
                return str(path)
        raise FileNotFoundError("Godot not found. Set GODOT_PATH environment variable.")
    
    def _find_project(self) -> str:
        """Find DarkAges client project."""
        paths = [
            Path("src/client"),
            Path("../src/client"),
            Path("../../src/client"),
            Path("C:/Dev/DarkAges/src/client"),
        ]
        for path in paths:
            if (path / "project.godot").exists():
                return str(path.resolve())
        raise FileNotFoundError("DarkAges client project not found. Ensure project.godot exists.")
    
    def _call_mcp_tool(self, tool: str, params: Dict[str, Any]) -> Dict[str, Any]:
        """
        Call an MCP tool via the server.
        
        In a real implementation, this would use proper MCP protocol
        communication (stdio or TCP). For now, we use subprocess.
        """
        cmd = [
            "node",
            self.mcp_server_path,
            tool,
            json.dumps(params)
        ]
        
        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=30
            )
            
            if result.returncode != 0:
                logger.error(f"MCP tool {tool} failed: {result.stderr}")
                return {"success": False, "error": result.stderr}
            
            return json.loads(result.stdout)
        
        except subprocess.TimeoutExpired:
            logger.error(f"MCP tool {tool} timed out")
            return {"success": False, "error": "Timeout"}
        except json.JSONDecodeError as e:
            logger.error(f"Failed to parse MCP response: {e}")
            return {"success": False, "error": str(e)}
    
    # -------------------------------------------------------------------------
    # System Tools
    # -------------------------------------------------------------------------
    
    def get_godot_version(self) -> Dict[str, Any]:
        """Get installed Godot version information."""
        return self._call_mcp_tool("get_godot_version", {})
    
    def launch_editor(self, project_path: Optional[str] = None) -> bool:
        """Launch Godot editor with project."""
        result = self._call_mcp_tool("launch_editor", {
            "projectPath": project_path or self.project_path
        })
        return result.get("success", False)
    
    def launch_project(
        self,
        project_path: Optional[str] = None,
        scene: Optional[str] = None,
        debug: bool = True,
        headless: bool = False
    ) -> bool:
        """
        Launch DarkAges game client.
        
        Args:
            project_path: Path to Godot project
            scene: Specific scene to load
            debug: Enable debug mode
            headless: Run without window (for server testing)
        """
        params = {
            "projectPath": project_path or self.project_path,
            "debug": debug,
            "headless": headless
        }
        if scene:
            params["scene"] = scene
        
        result = self._call_mcp_tool("run_project", params)
        self._running = result.get("success", False)
        return self._running
    
    def stop_project(self) -> bool:
        """Stop running Godot project."""
        result = self._call_mcp_tool("stop_project", {})
        self._running = not result.get("success", False)
        return result.get("success", False)
    
    def is_running(self) -> bool:
        """Check if Godot is currently running."""
        # In real implementation, this would check process status
        return self._running
    
    # -------------------------------------------------------------------------
    # Debug Output
    # -------------------------------------------------------------------------
    
    def get_debug_output(
        self,
        since_timestamp: Optional[float] = None,
        max_lines: int = 1000
    ) -> List[str]:
        """
        Get debug output from running Godot instance.
        
        Args:
            since_timestamp: Only get logs after this time
            max_lines: Maximum number of lines to return
        
        Returns:
            List of log lines
        """
        result = self._call_mcp_tool("get_debug_output", {
            "since": since_timestamp,
            "lines": max_lines
        })
        return result.get("output", [])
    
    def wait_for_log(
        self,
        pattern: str,
        timeout: float = 30.0,
        check_interval: float = 0.1
    ) -> Optional[str]:
        """
        Wait for specific log pattern to appear.
        
        Args:
            pattern: String or regex pattern to search for
            timeout: Maximum time to wait in seconds
            check_interval: How often to check in seconds
        
        Returns:
            Matching log line if found, None if timeout
        """
        import re
        
        start_time = time.time()
        last_check = start_time
        
        while time.time() - start_time < timeout:
            logs = self.get_debug_output(since_timestamp=last_check)
            last_check = time.time()
            
            for line in logs:
                if re.search(pattern, line):
                    return line
            
            time.sleep(check_interval)
        
        return None
    
    # -------------------------------------------------------------------------
    # Screenshots
    # -------------------------------------------------------------------------
    
    def take_screenshot(
        self,
        target: str = "game",
        save_path: Optional[str] = None
    ) -> ScreenshotResult:
        """
        Take screenshot of Godot window.
        
        Args:
            target: "game" or "editor"
            save_path: Where to save screenshot
        
        Returns:
            ScreenshotResult with success status and path
        """
        if not save_path:
            timestamp = int(time.time())
            save_path = f"screenshots/godot_{target}_{timestamp}.png"
        
        result = self._call_mcp_tool("take_screenshot", {
            "target": target,
            "savePath": save_path
        })
        
        return ScreenshotResult(
            success=result.get("success", False),
            path=result.get("path"),
            error=result.get("error")
        )
    
    # -------------------------------------------------------------------------
    # Scene Management
    # -------------------------------------------------------------------------
    
    def create_scene(self, scene_path: str, root_node_type: str = "Node2D") -> bool:
        """Create a new scene file."""
        result = self._call_mcp_tool("create_scene", {
            "scenePath": scene_path,
            "rootNodeType": root_node_type
        })
        return result.get("success", False)
    
    def add_node(
        self,
        scene_path: str,
        node_type: str,
        node_name: str,
        parent: str = ".",
        properties: Optional[Dict[str, Any]] = None
    ) -> bool:
        """Add a node to a scene."""
        params = {
            "scenePath": scene_path,
            "nodeType": node_type,
            "nodeName": node_name,
            "parent": parent
        }
        if properties:
            params["properties"] = properties
        
        result = self._call_mcp_tool("add_node", params)
        return result.get("success", False)
    
    def save_scene(self, scene_path: str) -> bool:
        """Save a scene file."""
        result = self._call_mcp_tool("save_scene", {
            "scenePath": scene_path
        })
        return result.get("success", False)
    
    # -------------------------------------------------------------------------
    # Custom DarkAges Tools
    # -------------------------------------------------------------------------
    
    def execute_gdscript(
        self,
        code: str,
        timeout: float = 5.0
    ) -> GDScriptResult:
        """
        Execute GDScript code in running game.
        
        This requires the game to have a remote script execution system
        or uses Godot's debugging protocol.
        
        Args:
            code: GDScript code to execute
            timeout: Maximum execution time
        
        Returns:
            GDScriptResult with return value or error
        """
        result = self._call_mcp_tool("execute_gdscript", {
            "code": code,
            "timeout": timeout
        })
        
        return GDScriptResult(
            success=result.get("success", False),
            result=result.get("result"),
            error=result.get("error")
        )
    
    def inject_input(
        self,
        input_type: str,
        key: Optional[str] = None,
        button: Optional[str] = None,
        position: Optional[tuple] = None,
        state: str = "press"  # press, release, click
    ) -> bool:
        """
        Inject input event into Godot.
        
        Args:
            input_type: "key", "mouse", "joypad"
            key: Key name (for keyboard input)
            button: Button name (for mouse/joypad)
            position: (x, y) tuple (for mouse)
            state: "press", "release", or "click"
        """
        params = {
            "type": input_type,
            "state": state
        }
        if key:
            params["key"] = key
        if button:
            params["button"] = button
        if position:
            params["position"] = {"x": position[0], "y": position[1]}
        
        result = self._call_mcp_tool("inject_input", params)
        return result.get("success", False)
    
    def execute_input_script(self, script_path: str) -> bool:
        """
        Execute a JSON input script file.
        
        Args:
            script_path: Path to JSON input script
        """
        script_file = Path(script_path)
        if not script_file.exists():
            logger.error(f"Input script not found: {script_path}")
            return False
        
        try:
            with open(script_file) as f:
                script = json.load(f)
            
            for event in script.get("events", []):
                self.inject_input(**event)
                if "delay" in event:
                    time.sleep(event["delay"])
            
            return True
        
        except Exception as e:
            logger.error(f"Failed to execute input script: {e}")
            return False
    
    def get_player_state(self) -> Dict[str, Any]:
        """Get current player state from running game."""
        result = self.execute_gdscript("""
            var player = get_tree().get_first_node_in_group("player")
            if player:
                return {
                    "position": player.global_position,
                    "velocity": player.velocity,
                    "health": player.health if "health" in player else 0
                }
            return null
        """)
        
        if result.success:
            return result.result or {}
        return {}
    
    def set_network_conditions(
        self,
        latency: int = 0,
        jitter: int = 0,
        loss: float = 0.0
    ) -> bool:
        """
        Set simulated network conditions.
        
        Requires network simulation layer in game.
        
        Args:
            latency: Added latency in milliseconds
            jitter: Latency variation in milliseconds
            loss: Packet loss percentage (0-100)
        """
        result = self.execute_gdscript(f"""
            if "network_manager" in get_tree().root:
                var nm = get_tree().root.get_node("network_manager")
                nm.set_simulation_conditions({latency}, {jitter}, {loss})
                return true
            return false
        """)
        
        return result.success and result.result
    
    # -------------------------------------------------------------------------
    # Utilities
    # -------------------------------------------------------------------------
    
    def connect_to_server(self, address: str, port: int = 7777) -> bool:
        """
        Connect running game to DarkAges server.
        
        Args:
            address: Server IP address
            port: Server port
        """
        result = self.execute_gdscript(f"""
            var network = get_node_or_null("/root/NetworkManager")
            if network:
                network.connect_to_server("{address}", {port})
                return true
            return false
        """)
        
        return result.success and result.result
    
    def wait_for_connection(self, timeout: float = 30.0) -> bool:
        """Wait for game to connect to server."""
        return self.wait_for_log("Connected to server", timeout) is not None
    
    def disconnect_from_server(self) -> bool:
        """Disconnect from server."""
        result = self.execute_gdscript("""
            var network = get_node_or_null("/root/NetworkManager")
            if network:
                network.disconnect()
                return true
            return false
        """)
        
        return result.success and result.result


# -------------------------------------------------------------------------
# Integration with AutomatedQA
# -------------------------------------------------------------------------

class GodotQAIntegration:
    """
    Integration between GodotMCPClient and AutomatedQA testing harness.
    """
    
    def __init__(self, godot_client: GodotMCPClient):
        self.godot = godot_client
        self._screenshots: List[str] = []
    
    def start_test_session(self) -> bool:
        """Launch Godot and prepare for testing."""
        return self.godot.launch_project(debug=True)
    
    def end_test_session(self) -> bool:
        """Clean up after testing."""
        return self.godot.stop_project()
    
    def capture_evidence(self, description: str) -> str:
        """Take screenshot and add to test evidence."""
        screenshot_path = f"screenshots/{int(time.time())}_{description}.png"
        result = self.godot.take_screenshot(save_path=screenshot_path)
        
        if result.success and result.path:
            self._screenshots.append({
                "path": result.path,
                "description": description,
                "timestamp": time.time()
            })
            return result.path
        
        return ""
    
    def run_input_sequence(self, sequence: List[Dict]) -> bool:
        """Execute a sequence of inputs."""
        for event in sequence:
            success = self.godot.inject_input(**event)
            if not success:
                logger.error(f"Failed to inject input: {event}")
                return False
            
            if "delay_after" in event:
                time.sleep(event["delay_after"])
        
        return True
    
    def assert_log_contains(self, pattern: str, timeout: float = 5.0) -> bool:
        """Assert that log contains pattern within timeout."""
        result = self.godot.wait_for_log(pattern, timeout)
        if result is None:
            logger.error(f"Pattern '{pattern}' not found in logs within {timeout}s")
            return False
        return True
    
    def get_test_report(self) -> Dict[str, Any]:
        """Generate test report with evidence."""
        return {
            "screenshots": self._screenshots,
            "screenshot_count": len(self._screenshots)
        }


# -------------------------------------------------------------------------
# Example Usage
# -------------------------------------------------------------------------

if __name__ == "__main__":
    # Example: Simple test
    client = GodotMCPClient(debug=True)
    
    print(f"Godot version: {client.get_godot_version()}")
    print(f"Project path: {client.project_path}")
    
    # Launch game
    if client.launch_project():
        print("Game launched successfully")
        
        # Wait for connection
        if client.wait_for_connection(timeout=10.0):
            print("Connected to server")
        
        # Get player state
        state = client.get_player_state()
        print(f"Player state: {state}")
        
        # Take screenshot
        screenshot = client.take_screenshot()
        print(f"Screenshot: {screenshot}")
        
        # Stop game
        client.stop_project()
        print("Game stopped")
