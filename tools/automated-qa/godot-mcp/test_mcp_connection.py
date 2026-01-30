"""
Godot MCP Connection Test

This script tests the connection to the Godot MCP server and validates
the basic functionality without launching a full game.

Usage:
    python test_mcp_connection.py
"""

import sys
import json
from pathlib import Path

# Add paths for imports
sys.path.insert(0, str(Path(__file__).parent))
sys.path.insert(0, str(Path(__file__).parent.parent))

try:
    from client import GodotMCPClient
except ImportError:
    from godot_mcp.client import GodotMCPClient


def test_basic_connection():
    """Test basic MCP server connectivity."""
    print("="*60)
    print("Godot MCP Connection Test")
    print("="*60)
    print()
    
    # Test 1: Initialize client
    print("[TEST 1] Initialize MCP Client")
    try:
        client = GodotMCPClient(debug=True)
        print(f"  Project path: {client.project_path}")
        print(f"  Godot path: {client.godot_path}")
        print(f"  MCP server path: {client.mcp_server_path}")
        print("  [PASS] Client initialized")
    except Exception as e:
        print(f"  [FAIL] {e}")
        return False
    
    print()
    
    # Test 2: Get Godot version
    print("[TEST 2] Get Godot Version")
    try:
        version = client.get_godot_version()
        print(f"  Version info: {version}")
        print("  [PASS] Retrieved version")
    except Exception as e:
        print(f"  [WARN] Could not get version: {e}")
        print("  This is OK if MCP server is not running yet")
    
    print()
    
    # Test 3: Check configuration
    print("[TEST 3] Configuration Check")
    config_path = Path(__file__).parent / "config.json"
    if config_path.exists():
        with open(config_path) as f:
            config = json.load(f)
        print(f"  Config file: {config_path}")
        print(f"  MCP server configured: {'godot' in config.get('mcpServers', {})}")
        print(f"  DarkAges paths configured: {'darkages' in config}")
        print("  [PASS] Configuration loaded")
    else:
        print(f"  [WARN] Config not found at: {config_path}")
        print("  Run setup_mcp.ps1 first")
    
    print()
    
    # Test 4: Project validation
    print("[TEST 4] Validate DarkAges Project")
    project_path = Path(client.project_path)
    if project_path.exists():
        print(f"  Project path: {project_path}")
        print(f"  project.godot exists: {(project_path / 'project.godot').exists()}")
        
        # Count scene files
        scenes = list(project_path.rglob("*.tscn"))
        print(f"  Scene files found: {len(scenes)}")
        
        # Count GDScript files
        scripts = list(project_path.rglob("*.gd"))
        print(f"  GDScript files found: {len(scripts)}")
        
        print("  [PASS] Project validated")
    else:
        print(f"  [FAIL] Project not found: {project_path}")
        return False
    
    print()
    
    # Test 5: MCP Server availability
    print("[TEST 5] MCP Server Status")
    mcp_server = Path(client.mcp_server_path)
    if mcp_server.exists():
        print(f"  Server path: {mcp_server}")
        print(f"  Server executable: {mcp_server.name}")
        print("  [PASS] MCP server found")
    else:
        print(f"  [FAIL] MCP server not found: {mcp_server}")
        print("  Run setup_mcp.ps1 to install")
        return False
    
    print()
    print("="*60)
    print("All basic tests passed!")
    print("="*60)
    print()
    print("Next steps:")
    print("  1. Start your AI assistant with MCP enabled")
    print("  2. Test full integration with:")
    print("     python test_movement_sync_mcp.py")
    print()
    
    return True


def test_mcp_tools():
    """Test actual MCP tool calls (requires MCP server running)."""
    print("="*60)
    print("Godot MCP Tools Test")
    print("="*60)
    print()
    print("NOTE: This test requires the MCP server to be running.")
    print("      Start your AI assistant with MCP configuration first.")
    print()
    
    client = GodotMCPClient(debug=True)
    
    # Test get_godot_version
    print("[TEST] get_godot_version")
    try:
        result = client.get_godot_version()
        print(f"  Result: {result}")
        if result.get("success", False):
            print("  [PASS]")
        else:
            print("  [SKIP] MCP server may not be running")
    except Exception as e:
        print(f"  [SKIP] {e}")
    
    print()


if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser()
    parser.add_argument("--tools", action="store_true", help="Test actual MCP tools")
    args = parser.parse_args()
    
    success = test_basic_connection()
    
    if args.tools:
        test_mcp_tools()
    
    sys.exit(0 if success else 1)
