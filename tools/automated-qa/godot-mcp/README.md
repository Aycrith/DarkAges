# Godot MCP Integration for DarkAges MMO Testing

## Overview

This module integrates **Godot MCP (Model Context Protocol)** servers with the DarkAges MMO automated testing framework, enabling AI agents to directly control and interact with the Godot game engine for comprehensive end-to-end testing.

## What is Godot MCP?

**Model Context Protocol (MCP)** is a standardized protocol that allows AI assistants to interact with external tools and services. **Godot MCP** servers bridge AI assistants with the Godot game engine, enabling:

- Launch and control Godot editor/game instances
- Capture debug output and error logs in real-time
- Take screenshots of game and editor views
- Create and modify scenes programmatically
- Generate and edit GDScript code
- Debug and analyze runtime behavior
- Control game execution (play, pause, stop)

## Architecture

```
AI AGENT (Claude/Kimi)
      │
      │ MCP Protocol
      ▼
Godot MCP Server Layer
      │
      │ Godot CLI / TCP / GDScript
      ▼
Godot Engine Instances
  - Editor (Development)
  - Game Instances (Test Clients)
  - Headless Server (Dedicated)
```

## Installation

### Prerequisites

1. **Godot Engine 4.x** installed
2. **Node.js 18+** and npm
3. **DarkAges Godot Client** project built or source available

### Step 1: Install Godot MCP Server

**Recommended: bradypp/godot-mcp**

```powershell
# Clone the repository
git clone https://github.com/bradypp/godot-mcp.git C:\Dev\DarkAges\tools\godot-mcp
cd C:\Dev\DarkAges\tools\godot-mcp

# Install dependencies
npm install

# Build the server
npm run build
```

### Step 2: Configure MCP Server

Create MCP configuration file at `tools/automated-qa/godot-mcp/config.json`:

```json
{
  "mcpServers": {
    "godot": {
      "command": "node",
      "args": ["C:/Dev/DarkAges/tools/godot-mcp/build/index.js"],
      "env": {
        "GODOT_PATH": "C:/Tools/Godot/Godot_v4.x.exe",
        "DEBUG": "true",
        "READ_ONLY_MODE": "false"
      },
      "disabled": false,
      "autoApprove": [
        "launch_editor",
        "run_project",
        "get_debug_output",
        "stop_project",
        "take_screenshot"
      ]
    }
  },
  "darkages": {
    "client_project_path": "C:/Dev/DarkAges/src/client",
    "server_address": "localhost:7777",
    "test_scenarios_path": "C:/Dev/DarkAges/tools/automated-qa/scenarios",
    "screenshot_output_path": "C:/Dev/DarkAges/tools/automated-qa/screenshots"
  }
}
```

### Step 3: Configure AI Assistant (Cline)

Add to `cline_mcp_settings.json`:

```json
{
  "mcpServers": {
    "godot": {
      "command": "node",
      "args": ["C:/Dev/DarkAges/tools/godot-mcp/build/index.js"],
      "env": {
        "GODOT_PATH": "C:/Tools/Godot/Godot_v4.x.exe",
        "DEBUG": "true"
      }
    }
  }
}
```

## Python Client API

```python
from tools.automated-qa.godot-mcp.client import GodotMCPClient

# Initialize client
client = GodotMCPClient(
    mcp_server_path="tools/godot-mcp/build/index.js",
    godot_path="C:/Tools/Godot/Godot_v4.2.exe",
    project_path="src/client"
)

# Launch Godot with DarkAges client
client.launch_project(
    scene="scenes/Main.tscn",
    debug=True,
    headless=False
)

# Capture debug output
logs = client.get_debug_output(since_timestamp=time.time())

# Take screenshot
screenshot = client.take_screenshot(
    target="game",
    save_path="screenshots/test_001.png"
)

# Execute GDScript in running game
result = client.execute_gdscript("""
    var player = get_tree().get_first_node_in_group("player")
    return player.global_position
""")

# Stop Godot
client.stop_project()
```

## MCP Tools Reference

| Tool | Description |
|------|-------------|
| `get_godot_version` | Get installed Godot version |
| `launch_editor` | Open Godot editor |
| `run_project` | Run project in debug mode |
| `stop_project` | Stop running project |
| `get_debug_output` | Retrieve console output |
| `create_scene` | Create new scene |
| `add_node` | Add node to scene |
| `take_screenshot` | Capture game/editor view |

## Testing Scenarios

### Movement Prediction Validation

```python
# Launch server and client
server = start_server()
godot = GodotMCPClient()
godot.launch_project(connect_to="localhost:7777")

# Inject movement
godot.inject_input(type="key", key="w", state="press")

# Monitor logs for prediction errors
logs = godot.get_debug_output()
errors = [l for l in logs if "prediction_error" in l]
assert len(errors) == 0
```

### Visual Regression Testing

```python
# Take baseline screenshot
baseline = godot.take_screenshot("game", "baseline.png")

# Make changes
godot.execute_gdscript("change_character_skin('new_skin')")

# Compare
comparison = godot.take_screenshot("game", "comparison.png")
diff = compare_images(baseline, comparison)
assert diff.similarity > 0.95
```

## Resources

- **bradypp/godot-mcp**: https://github.com/bradypp/godot-mcp
- **Coding-Solo/godot-mcp**: https://github.com/Coding-Solo/godot-mcp
- **GDAI MCP**: https://gdaimcp.com/
- **MCP Protocol**: https://modelcontextprotocol.io/
