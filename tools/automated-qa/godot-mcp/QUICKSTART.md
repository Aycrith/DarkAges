# Godot MCP Quickstart Guide

## What is Godot MCP?

Godot MCP (Model Context Protocol) allows AI assistants to **directly control** the Godot game engine for automated testing and development.

## Capabilities

| Feature | Description | Use Case |
|---------|-------------|----------|
| Launch Editor | Open Godot with project loaded | Automated scene editing |
| Run Project | Launch game in debug mode | E2E testing |
| Debug Output | Capture console logs in real-time | Error detection |
| Screenshots | Capture game/editor views | Visual regression testing |
| Scene Management | Create/modify scenes programmatically | Automated content generation |
| GDScript Execution | Run code in live game | State inspection |
| Input Injection | Send keyboard/mouse events | Gameplay automation |

## Installation (5 minutes)

```powershell
# Run the setup script
powershell -ExecutionPolicy Bypass -File tools/automated-qa/godot-mcp/setup_mcp.ps1

# Or manually:
git clone https://github.com/bradypp/godot-mcp.git tools/godot-mcp
cd tools/godot-mcp
npm install
npm run build
```

## Configure Your AI Assistant

### For Cline (VS Code)

Add to `%APPDATA%\Code\User\globalStorage\saoudrizwan.claude-dev\settings\cline_mcp_settings.json`:

```json
{
  "mcpServers": {
    "godot": {
      "command": "node",
      "args": ["C:/Dev/DarkAges/tools/godot-mcp/build/index.js"],
      "env": {
        "GODOT_PATH": "C:/Tools/Godot/Godot_v4.2.exe",
        "DEBUG": "true"
      }
    }
  }
}
```

### For Cursor

Add to `.cursor/mcp.json`:

```json
{
  "mcpServers": {
    "godot": {
      "command": "node",
      "args": ["C:/Dev/DarkAges/tools/godot-mcp/build/index.js"],
      "env": {
        "GODOT_PATH": "C:/Tools/Godot/Godot_v4.2.exe"
      }
    }
  }
}
```

## Quick Test

```python
from tools.automated-qa.godot-mcp.client import GodotMCPClient

# Initialize
client = GodotMCPClient()

# Launch game
client.launch_project(debug=True)

# Wait for connection
client.wait_for_connection(timeout=10)

# Execute GDScript
result = client.execute_gdscript("""
    var player = get_tree().get_first_node_in_group("player")
    return player.global_position
""")

print(f"Player position: {result.result}")

# Take screenshot
screenshot = client.take_screenshot(save_path="test.png")

# Stop game
client.stop_project()
```

## Run Integration Tests

```bash
# Basic connection test
python tools/automated-qa/godot-mcp/test_mcp_connection.py

# Full movement sync test
python tools/automated-qa/godot-mcp/test_movement_sync_mcp.py \
    --server build/Release/darkages_server.exe \
    --project src/client
```

## Common Tasks

### Launch and Monitor

```python
client.launch_project()
logs = client.get_debug_output()
for line in logs:
    if "error" in line.lower():
        print(f"ERROR: {line}")
```

### Inject Input Sequence

```python
sequence = [
    {"input_type": "key", "key": "w", "state": "press", "delay_after": 0.5},
    {"input_type": "key", "key": "w", "state": "release"},
]
client.run_input_sequence(sequence)
```

### Visual Regression Testing

```python
baseline = client.take_screenshot(save_path="baseline.png")
# ... make changes ...
comparison = client.take_screenshot(save_path="comparison.png")
# Compare images
```

## Troubleshooting

| Issue | Solution |
|-------|----------|
| "Godot not found" | Set `GODOT_PATH` env var |
| "MCP server not found" | Run `setup_mcp.ps1` |
| "Connection refused" | Start AI assistant with MCP config |
| "Screenshots blank" | Ensure Godot window is not minimized |
| "Logs empty" | Enable verbose output: `launch_project(debug=True, verbose=True)` |

## Integration with Automated QA

```python
from tools.automated-qa.harness import AutomatedQA
from tools.automated-qa.godot-mcp.client import GodotMCPClient, GodotQAIntegration

# Initialize
qa = AutomatedQA()
godot = GodotMCPClient()
integration = GodotQAIntegration(godot)

# Start test session
integration.start_test_session()
qa.start_server()

# Execute test
godot.wait_for_connection()
godot.inject_input(type="key", key="w", state="press")
integration.capture_evidence("movement")

# Validate
assert integration.assert_log_contains("prediction_error", timeout=5) == False

# Cleanup
integration.end_test_session()
qa.stop_server()
```

## Resources

- **Full Documentation**: [README.md](README.md)
- **bradypp/godot-mcp**: https://github.com/bradypp/godot-mcp
- **MCP Protocol**: https://modelcontextprotocol.io/
